#!/usr/bin/env python3
from __future__ import annotations

import asyncio
import contextlib
import importlib.machinery
import importlib.util
import json
import os
import signal
import sys
import time
import uuid
from pathlib import Path

import aiohttp

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "bin"))
import journal as outputs
import monitor

PROTOCOL_VERSION = "2025-06-18"
DEFAULT_TIMEOUT = 14400.0
DEFAULT_IDLE = 120.0
LOCK_WAIT = 14400.0
DEFAULT_CONCURRENCY = 8
TURNS: dict[str, dict] = {}
WRITE_LOCK = asyncio.Lock()
SUB_ID = "io.modelcontextprotocol/subscriptionId"
LOG_RANK = {"debug": 0, "info": 1, "notice": 2, "warning": 3, "error": 4,
            "critical": 5, "alert": 6, "emergency": 7}
LOG_LEVEL: str | None = None
LOG_QUEUE: list[dict] = []
SUBSCRIPTIONS: set[str] = set()
LISTENS: dict = {}
EVENT_LOG: list[dict] = []
KNOWN_OUTPUTS: set[str] = set()
MONITOR_TASK: asyncio.Task | None = None


def load_orch():
    loader = importlib.machinery.SourceFileLoader("hyperagent_orch", str(ROOT / "bin" / "orch"))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


orch = load_orch()


def number(args: dict, key: str, default: float) -> float:
    value = args.get(key)
    return float(value) if value is not None else default


def flag(args: dict, key: str, default: bool) -> bool:
    value = args.get(key)
    return bool(value) if value is not None else default


async def prepare_job(name: str, prompt: str, resume: bool = False,
                      harness: str | None = None, fork: bool = False) -> dict:
    entry = {"name": name, "prompt": prompt}
    if harness:
        entry["harness"] = harness
    if fork:
        entry["fork"] = True
    job = orch.normalize(entry, True)
    await orch.fill_cwds([job])
    if not job.get("cwd"):
        raise ValueError(f"unknown project {job['name']!r}; call list_projects first")
    if resume and not await orch.peer_present(job["name"]):
        session = orch.latest_session(job["harness"], job["cwd"])
        if session:
            job["resume"] = session["session_id"]
    return job


def prune_turns() -> None:
    cutoff = time.time() - 6 * 3600
    for key, record in list(TURNS.items()):
        if record["result"] is not None and record["started"] < cutoff:
            TURNS.pop(key, None)


def start_turn(job: dict, timeout_s: float, idle_s: float, tools: bool, trust: bool) -> str:
    prune_turns()
    turn_id = uuid.uuid4().hex[:12]
    record = {"turn_id": turn_id, "name": job["name"], "task": None, "result": None,
              "started": time.time()}

    async def runner() -> None:
        try:
            record["result"] = await orch.run_job(job, timeout_s, idle_s, LOCK_WAIT, tools, trust)
        except (Exception, SystemExit) as exc:
            record["result"] = {"name": job["name"], "harness": job["harness"],
                                "cwd": job.get("cwd"), "status": "error",
                                "text": f"{type(exc).__name__}: {exc}", "acked": False,
                                "pane_tail": [],
                                "duration_s": round(time.time() - record["started"], 1)}

    record["task"] = asyncio.create_task(runner())
    TURNS[turn_id] = record
    return turn_id


async def tool_list_projects(args: dict) -> str:
    return json.dumps(orch.discover(flag(args, "all", False)), indent=2)


async def tool_start_projects(args: dict) -> str:
    names = list(dict.fromkeys(str(name) for name in (args.get("names") or [])))
    if not names:
        raise ValueError("names must contain at least one project name")
    await orch.ensure_server()
    harness = str(args.get("harness") or "") or None
    jobs = []
    for name in names:
        entry = {"name": name}
        if harness:
            entry["harness"] = harness
        jobs.append(orch.normalize(entry, False))
    await orch.fill_cwds(jobs)
    missing = [job["name"] for job in jobs if not job.get("cwd")]
    if missing:
        raise ValueError(f"unknown project(s): {', '.join(missing)}; call list_projects")
    if flag(args, "resume", False):
        for job in jobs:
            if await orch.peer_present(job["name"]):
                continue
            session = orch.latest_session(job["harness"], job["cwd"])
            if session:
                job["resume"] = session["session_id"]
                if flag(args, "fork", False):
                    job["fork"] = True
    trust = flag(args, "trust", True)
    limit = int(args.get("concurrency") or DEFAULT_CONCURRENCY)
    gate = asyncio.Semaphore(max(1, limit))

    async def start(job: dict) -> dict:
        async with gate:
            return await orch.start_one(job, 3.0, trust)

    results = await asyncio.gather(*(start(job) for job in jobs))
    sessions = await orch.fetch_sessions()
    lines = []
    for job in results:
        info = sessions.get(job["name"]) or {}
        state = "started" if info.get("alive") else "not alive"
        suffix = " trust=accepted" if job.get("trusted") else ""
        if job.get("restarted"):
            suffix += " restarted=stale-resume"
        if job.get("resume"):
            suffix += f" resume={job['resume']}"
        lines.append(f"{job['name']}: {state} harness={job['harness']} "
                     f"cwd={job['cwd']}{suffix}")
    return "\n".join(lines)


async def tool_instruct(args: dict) -> str:
    name = str(args.get("name") or "")
    prompt = str(args.get("prompt") or "")
    if not prompt.strip():
        raise ValueError("prompt is required")
    job = await prepare_job(name, prompt, flag(args, "resume", False),
                           str(args.get("harness") or "") or None,
                           flag(args, "fork", False))
    timeout_s = number(args, "timeout_s", DEFAULT_TIMEOUT)
    idle_s = number(args, "idle_exit_s", DEFAULT_IDLE)
    tools = flag(args, "tools", False)
    trust = flag(args, "trust", True)
    if flag(args, "wait", False):
        result = await orch.run_job(job, timeout_s, idle_s, LOCK_WAIT, tools, trust)
        return orch.render_result(result, tools)
    turn_id = start_turn(job, timeout_s, idle_s, tools, trust)
    return json.dumps({"turn_id": turn_id, "name": job["name"], "state": "running"}, indent=2)


async def tool_instruct_many(args: dict) -> str:
    entries = args.get("jobs") or []
    if not isinstance(entries, list) or not entries:
        raise ValueError("jobs must contain at least one entry")
    timeout_s = number(args, "timeout_s", DEFAULT_TIMEOUT)
    idle_s = number(args, "idle_exit_s", DEFAULT_IDLE)
    tools = flag(args, "tools", False)
    trust = flag(args, "trust", True)
    prepared = []
    for entry in entries:
        name = str(entry.get("name") or "")
        prompt = str(entry.get("prompt") or "")
        if not prompt.strip():
            raise ValueError(f"job {name!r} has no prompt")
        harness = str(entry.get("harness") or args.get("harness") or "") or None
        job = await prepare_job(name, prompt, flag(args, "resume", False), harness,
                                flag(args, "fork", False))
        prepared.append((job, number(entry, "timeout_s", timeout_s),
                         number(entry, "idle_exit_s", idle_s)))
    if flag(args, "wait", False):
        results = await asyncio.gather(
            *(orch.run_job(job, t, i, LOCK_WAIT, tools, trust) for job, t, i in prepared))
        return "\n\n".join(orch.render_result(result, tools) for result in results)
    turns = [start_turn(job, t, i, tools, trust) for job, t, i in prepared]
    return json.dumps({"turns": turns, "state": "running"}, indent=2)


async def tool_turn_result(args: dict) -> str:
    turn_id = str(args.get("turn_id") or "")
    record = TURNS.get(turn_id)
    if record is None:
        raise ValueError(f"unknown turn {turn_id!r}")
    if record["result"] is None:
        with contextlib.suppress(asyncio.TimeoutError):
            await asyncio.wait_for(asyncio.shield(record["task"]),
                                   max(0.1, number(args, "wait_s", 50.0)))
    if record["result"] is None:
        return json.dumps({"turn_id": turn_id, "name": record["name"], "state": "running",
                           "elapsed_s": round(time.time() - record["started"], 1)}, indent=2)
    return json.dumps({"turn_id": turn_id, "name": record["name"], "state": "done",
                       "result": record["result"]}, indent=2)


async def tool_resume_project(args: dict) -> str:
    project = str(args.get("project") or "") or None
    session_id = str(args.get("session_id") or "") or None
    cwd = str(args.get("cwd") or "") or None
    harness = str(args.get("harness") or "") or None
    prompt = str(args.get("prompt") or "")
    fork = flag(args, "fork", False)
    target = orch.resume_target(project, session_id, cwd, harness)
    target_session = target["session"]["session_id"]
    job = {"name": target["name"], "harness": target["harness"],
           "cwd": target["cwd"], "resume": target_session}
    if fork:
        job["fork"] = True
    if target["session"].get("selector") in ("name", "path"):
        job["record_latest"] = True
    await orch.fill_cwds([job])
    present = await orch.peer_present(job["name"])
    live_args: list[str] = []
    live_harness = None
    if present and await orch.server_alive():
        live = (await orch.fetch_sessions()).get(job["name"]) or {}
        live_args = list(live.get("launch_args") or [])
        live_harness = live.get("harness")
    live_id = live_args[1] if len(live_args) > 1 else None
    same_harness = live_harness in (None, job["harness"])
    matches_live = (present and same_harness and not fork
                    and (not session_id or target_session == live_id
                         or target_session in live_args))
    if matches_live and not prompt:
        return json.dumps({"state": "already_alive", "project": job["name"],
                           "cwd": job["cwd"], "harness": job["harness"],
                           "session_id": live_id,
                           "requested_session_id": target_session,
                           "fork": fork, "global": bool(target["session"].get("global")),
                           "launch_args": live_args}, indent=2)
    if matches_live:
        job.pop("resume", None)
    timeout_s = number(args, "timeout_s", DEFAULT_TIMEOUT)
    idle_s = number(args, "idle_exit_s", DEFAULT_IDLE)
    tools = flag(args, "tools", False)
    trust = flag(args, "trust", True)
    if prompt:
        job["prompt"] = prompt
        if flag(args, "wait", False):
            result = await orch.run_job(job, timeout_s, idle_s, LOCK_WAIT, tools, trust)
            return json.dumps({"state": "prompted", "project": job["name"],
                               "cwd": job["cwd"], "harness": job["harness"],
                               "session_id": result.get("forked_session")
                               or result.get("resolved_session")
                               or target_session,
                               "source_session_id": target_session,
                               "fork": fork,
                               "global": bool(target["session"].get("global")),
                               "result": result}, indent=2)
        turn_id = start_turn(job, timeout_s, idle_s, tools, trust)
        return json.dumps({"state": "running", "project": job["name"],
                           "cwd": job["cwd"], "harness": job["harness"],
                           "session_id": target_session, "fork": fork,
                           "global": bool(target["session"].get("global")),
                           "turn_id": turn_id}, indent=2)
    await orch.ensure_server()
    started = await orch.start_one(job, 3.0, trust)
    sessions = await orch.fetch_sessions()
    info = sessions.get(job["name"]) or {}
    return json.dumps({"state": "resumed" if info.get("alive") else "not_alive",
                       "project": job["name"], "cwd": job["cwd"],
                       "harness": job["harness"],
                       "session_id": started.get("forked_session")
                       or started.get("resolved_session") or target_session,
                       "source_session_id": target_session, "fork": fork,
                       "global": bool(target["session"].get("global")),
                       "launch_args": info.get("launch_args") or []}, indent=2)


async def tool_list_project_sessions(args: dict) -> str:
    if flag(args, "all", False):
        rows = []
        for project in orch.discover(True):
            for harness in orch.airc_mods().names():
                for session in orch.harness_sessions(harness, project["cwd"]):
                    rows.append({"project": project["name"], "cwd": project["cwd"],
                                 "harness": harness, **session})
        return json.dumps(rows, indent=2)
    project = str(args.get("project") or "") or None
    cwd = str(args.get("cwd") or "") or None
    harness = str(args.get("harness") or "") or None
    target = orch.project_target(project, cwd, harness)
    rows = [{"project": target["name"], "cwd": target["cwd"],
             "harness": target["harness"], **session}
            for session in orch.harness_sessions(target["harness"], target["cwd"])]
    return json.dumps(rows, indent=2)


PEER_STATUS_FIELDS = ("alive", "busy", "harness", "cwd", "clients", "launch_args",
                      "output_path", "output_session")


def pane_content(pane: list) -> list[str]:
    rows = [line.rstrip() for line in pane if line.strip()]
    return [line for line in rows if set(line.strip()) - set("─ ")]


def compact_peer(info: dict, pane_lines: int) -> dict:
    row = {key: info[key] for key in PEER_STATUS_FIELDS if key in info}
    if pane_lines:
        row["pane_tail"] = pane_content(info.get("pane_tail") or [])[-pane_lines:]
    return row


async def tool_peer_status(args: dict) -> str:
    await orch.ensure_server()
    sessions = await orch.fetch_sessions()
    pane_lines = int(args.get("pane_lines") or 0)
    status = {}
    for name, info in sessions.items():
        try:
            row = outputs.find_session(agent=name, cwd=info.get("cwd"))
        except Exception:
            row = None
        if row:
            info["output_path"] = row["path"]
            info["output_session"] = row["session"]
        status[name] = compact_peer(info, pane_lines)
    return json.dumps(status, indent=2, sort_keys=True)


async def tool_read_pane(args: dict) -> str:
    name = orch.check_name(str(args.get("name") or ""))
    pane = await orch.capture(name)
    lines = [line.rstrip() for line in pane.splitlines() if line.strip()]
    count = int(args.get("lines") or 40)
    return "\n".join(lines[-count:]) or "(empty pane)"


async def tool_stop_peer(args: dict) -> str:
    name = orch.check_name(str(args.get("name") or ""))
    if not await orch.server_alive():
        return f"server down; {name} has no hub"
    async with aiohttp.ClientSession() as http:
        async with http.delete(orch.base_url() + f"/sessions/{name}",
                               timeout=aiohttp.ClientTimeout(total=30)) as response:
            if response.status == 200:
                return f"stopped {name}"
            if response.status == 404:
                return f"no hub {name}"
            raise RuntimeError(f"stop {name} failed: HTTP {response.status}")


async def tool_server_down(args: dict) -> str:
    sessions = await orch.fetch_sessions() if await orch.server_alive() else {}
    pid_file = orch.ORCH_DIR / "server.pid"
    if not pid_file.is_file():
        return f"no pid file; running sessions={len(sessions)}"
    pid = int(pid_file.read_text().strip())
    with contextlib.suppress(ProcessLookupError):
        os.kill(pid, signal.SIGTERM)
    deadline = time.time() + 20
    while time.time() < deadline and await orch.server_alive(0.5):
        await asyncio.sleep(0.2)
    pid_file.unlink(missing_ok=True)
    return f"stopped airc server pid={pid}; ended {len(sessions)} sessions"


def output_cwd(args: dict) -> str | None:
    cwd = str(args.get("cwd") or "") or None
    project = str(args.get("project") or "") or None
    if not cwd and project:
        cwd = orch.project_target(project, None, str(args.get("harness") or "") or None)["cwd"]
    return cwd


def output_kinds(args: dict) -> list[str] | None:
    raw = args.get("kinds")
    if raw is None:
        return None
    if isinstance(raw, str):
        raw = [item.strip() for item in raw.split(",") if item.strip()]
    wanted = [str(item) for item in raw if item]
    return wanted or None


async def tool_list_outputs(args: dict) -> str:
    rows = outputs.scan(
        cwd=output_cwd(args),
        agent=str(args.get("agent") or "") or None,
        session_id=str(args.get("session_id") or "") or None,
        limit=int(args.get("limit") or 50))
    return json.dumps(rows, indent=2)


async def tool_read_output(args: dict) -> str:
    row = outputs.find_session(
        agent=str(args.get("agent") or "") or None,
        cwd=output_cwd(args),
        session_id=str(args.get("session_id") or "") or None)
    if row is None:
        raise ValueError("no session output found")
    if args.get("since") is None:
        records = outputs.tail_records(row["path"], int(args.get("tail") or 40),
                                       kinds=output_kinds(args))
    else:
        records = outputs.read_records(
            row["path"], kinds=output_kinds(args),
            limit=int(args.get("tail") or 40),
            since=float(args["since"]))
    if str(args.get("format") or "text") == "json":
        return json.dumps({"session": row, "records": records}, indent=2)
    header = (f"# {row['agent']} {row['session']} harness={row['harness']} "
              f"cwd={row['cwd']} {row['path']}")
    return outputs.render_records(
        records, header=header,
        max_chars=int(args.get("max_chars") or 20000),
        per_record=int(args.get("per_record_chars") or 2000))


async def tool_search_output(args: dict) -> str:
    query = str(args.get("query") or "")
    if not query:
        raise ValueError("query is required")
    ignore_case = bool(args.get("ignore_case", True))
    needle = query.lower() if ignore_case else query
    limit = int(args.get("limit") or 50)
    max_chars = int(args.get("max_chars") or 500)
    rows = outputs.scan(
        cwd=output_cwd(args),
        agent=str(args.get("agent") or "") or None,
        session_id=str(args.get("session_id") or "") or None)
    found = []
    for row in rows:
        for record in outputs.iter_records(row["path"]):
            text = str(record.get("text") or "")
            haystack = text.lower() if ignore_case else text
            if needle not in haystack:
                continue
            found.append({"agent": row["agent"], "session": row["session"],
                          "kind": record.get("kind"), "ts": record.get("ts"),
                          "text": outputs.truncate(text, max_chars),
                          "path": row["path"]})
            if len(found) >= limit:
                return json.dumps(found, indent=2)
    return json.dumps(found, indent=2)


def resource_uri(row: dict) -> str:
    return f"hyperagent://outputs/{row['project']}/{row['session']}"


def resource_list() -> list[dict]:
    items = [{"uri": "hyperagent://outputs", "name": "session-output-index",
              "description": "Every journaled peer session",
              "mimeType": "application/json"},
             {"uri": "hyperagent://events", "name": "monitor-events",
              "description": "Idle and gone events from the peer monitor",
              "mimeType": "application/x-ndjson"}]
    for row in outputs.scan(limit=200):
        items.append({"uri": resource_uri(row),
                      "name": f"{row['agent']} {row['session']}",
                      "description": f"{row['harness'] or ''} {row['cwd'] or ''} "
                                     f"{row['last_kind'] or ''}".strip(),
                      "mimeType": "application/x-ndjson"})
    return items


def resource_templates() -> list[dict]:
    return [
        {"uriTemplate": "hyperagent://outputs/{project}/{session}",
         "name": "session-output", "description": "Raw journal for one peer session",
         "mimeType": "application/x-ndjson"},
        {"uriTemplate": "hyperagent://outputs/{project}/{session}/view",
         "name": "session-output-view",
         "description": "Compact rendered journal for one peer session",
         "mimeType": "text/plain"},
    ]


def read_resource(uri: str) -> tuple[str, str]:
    bare = uri.rstrip("/")
    if bare == "hyperagent://events":
        return "\n".join(json.dumps(event, separators=(",", ":"), ensure_ascii=False)
                         for event in EVENT_LOG[-100:]), "application/x-ndjson"
    if bare == "hyperagent://outputs":
        return json.dumps(outputs.scan(limit=500), indent=2), "application/json"
    prefix = "hyperagent://outputs/"
    if not uri.startswith(prefix):
        raise ValueError(f"unknown resource {uri}")
    rest = uri[len(prefix):].strip("/")
    view = rest.endswith("/view")
    if view:
        rest = rest[:-len("/view")]
    parts = rest.split("/")
    if len(parts) != 2 or not all(parts):
        raise ValueError(f"bad resource {uri}")
    project, session = parts
    rows = [row for row in outputs.scan(session_id=session) if row["project"] == project]
    if not rows:
        raise ValueError(f"no session output {project}/{session}")
    row = rows[0]
    if view:
        records = outputs.tail_records(row["path"], 200)
        header = f"# {row['agent']} {row['session']} {row['path']}"
        return outputs.render_records(records, header=header), "text/plain"
    return Path(row["path"]).read_text(encoding="utf-8", errors="replace"), "application/x-ndjson"



TOOLS = [
    {"name": "list_projects",
     "description": "List the projects in the parent directory that can become peer agents. "
                    "Launches nothing.",
     "inputSchema": {"type": "object",
                     "properties": {"all": {"type": "boolean",
                                            "description": "include directories without a project marker"}},
                     "additionalProperties": False}},
    {"name": "start_projects",
     "description": "Start Claude Code peer sessions for the named projects through airc. "
                    "Only the names given are started. Returns ready state per peer.",
     "inputSchema": {"type": "object",
                     "properties": {"names": {"type": "array", "items": {"type": "string"},
                                              "minItems": 1},
                                    "concurrency": {"type": "number"},
                                    "harness": {"type": "string",
                                                "description": "claude, codex or freebuff; defaults to the project's remembered harness"},
                                    "resume": {"type": "boolean",
                                               "description": "resume each project's latest session when it is not running"},
                                    "fork": {"type": "boolean",
                                             "description": "fork the resumed session instead of reusing its id"},
                                    "trust": {"type": "boolean",
                                              "description": "answer the folder-trust dialog (default true)"}},
                     "required": ["names"], "additionalProperties": False}},
    {"name": "instruct",
     "description": "Prompt one peer session and wait for its turn. Starts the peer if needed. "
                    "Returns the peer's text plus status; a non-turn-end status includes the pane tail.",
     "inputSchema": {"type": "object",
                     "properties": {"name": {"type": "string"},
                                    "prompt": {"type": "string"},
                                    "timeout_s": {"type": "number"},
                                    "idle_exit_s": {"type": "number"},
                                    "tools": {"type": "boolean"},
                                    "wait": {"type": "boolean",
                                             "description": "true blocks until the turn ends; false (default) returns a turn_id at once"},
                                    "harness": {"type": "string",
                                                "description": "claude, codex or freebuff; defaults to the project's remembered harness"},
                                    "resume": {"type": "boolean",
                                               "description": "resume the project's latest session when the peer is not running"},
                                    "fork": {"type": "boolean",
                                             "description": "fork the resumed session instead of reusing its id"},
                                    "trust": {"type": "boolean"}},
                     "required": ["name", "prompt"], "additionalProperties": False}},
    {"name": "instruct_many",
     "description": "Prompt many peer sessions concurrently and collect their turns. Each job "
                    "names a peer and a prompt; a job without cwd uses that project's directory.",
     "inputSchema": {"type": "object",
                     "properties": {"jobs": {"type": "array", "minItems": 1,
                                             "items": {"type": "object",
                                                       "properties": {"name": {"type": "string"},
                                                                      "prompt": {"type": "string"},
                                                                      "harness": {"type": "string"},
                                                                      "timeout_s": {"type": "number"},
                                                                      "idle_exit_s": {"type": "number"}},
                                                       "required": ["name", "prompt"]}},
                                    "timeout_s": {"type": "number"},
                                    "idle_exit_s": {"type": "number"},
                                    "tools": {"type": "boolean"},
                                    "wait": {"type": "boolean",
                                             "description": "true blocks until every turn ends; false (default) returns turn_ids at once"},
                                    "harness": {"type": "string"},
                                    "resume": {"type": "boolean"},
                                    "fork": {"type": "boolean"},
                                    "trust": {"type": "boolean"}},
                     "required": ["jobs"], "additionalProperties": False}},
    {"name": "turn_result",
     "description": "Read a background turn started by instruct or instruct_many with wait=false.",
     "inputSchema": {"type": "object",
                     "properties": {"turn_id": {"type": "string"},
                                    "wait_s": {"type": "number"}},
                     "required": ["turn_id"], "additionalProperties": False}},
    {"name": "resume_project",
     "description": "Resume a project's conversation in claude, codex or freebuff. With no project "
                    "it resumes the last project used; with no session_id it resumes that project's "
                    "newest session. A specific project, harness or session_id can be given. Starts "
                    "the airc server and peer as needed and optionally sends a prompt, returning the "
                    "resumed session and the turn.",
     "inputSchema": {"type": "object",
                     "properties": {"project": {"type": "string",
                                               "description": "project name; omitted resumes the last project"},
                                    "session_id": {"type": "string",
                                                   "description": "specific Claude session id; omitted resumes the newest"},
                                    "cwd": {"type": "string",
                                            "description": "project directory for a name that is not discovered"},
                                    "harness": {"type": "string",
                                                "description": "claude, codex or freebuff; defaults to the project's remembered harness"},
                                    "prompt": {"type": "string",
                                               "description": "optional prompt sent after resume"},
                                    "timeout_s": {"type": "number"},
                                    "idle_exit_s": {"type": "number"},
                                    "tools": {"type": "boolean"},
                                    "wait": {"type": "boolean",
                                             "description": "true blocks until the prompted turn ends; false (default) returns a turn_id at once"},
                                    "fork": {"type": "boolean",
                                             "description": "fork the resumed session instead of reusing its id"},
                                    "trust": {"type": "boolean"}},
                     "additionalProperties": False}},
    {"name": "list_project_sessions",
     "description": "List resumable harness sessions for one project, or for every discovered "
                    "project and harness with all=true. Shows session ids with timestamps and paths.",
     "inputSchema": {"type": "object",
                     "properties": {"project": {"type": "string"},
                                    "cwd": {"type": "string"},
                                    "harness": {"type": "string"},
                                    "all": {"type": "boolean"}},
                     "additionalProperties": False}},
    {"name": "peer_status",
     "description": "List live peers with alive and busy state, cwd, harness, client count, launch "
                    "args and output journal paths. The pane tail is left out unless pane_lines is given.",
     "inputSchema": {"type": "object",
                     "properties": {"pane_lines": {"type": "number"}},
                     "additionalProperties": False}},
    {"name": "read_pane",
     "description": "Print the tail of a peer's terminal pane.",
     "inputSchema": {"type": "object",
                     "properties": {"name": {"type": "string"}, "lines": {"type": "number"}},
                     "required": ["name"], "additionalProperties": False}},
    {"name": "stop_peer",
     "description": "Retire one peer session.",
     "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}},
                     "required": ["name"], "additionalProperties": False}},
    {"name": "server_down",
     "description": "Stop the airc server and every peer it owns.",
     "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
    {"name": "list_outputs",
     "description": "List journaled session output files with agent, session, harness, cwd, "
                    "size, last record time and a short last-output preview.",
     "inputSchema": {"type": "object",
                     "properties": {"project": {"type": "string"},
                                    "cwd": {"type": "string"},
                                    "agent": {"type": "string"},
                                    "session_id": {"type": "string"},
                                    "limit": {"type": "number"}},
                     "additionalProperties": False}},
    {"name": "read_output",
     "description": "Read one journaled session output as a compact transcript, newest records "
                    "last. Filter by kinds, tail count, since epoch, or format=json.",
     "inputSchema": {"type": "object",
                     "properties": {"project": {"type": "string"},
                                    "cwd": {"type": "string"},
                                    "agent": {"type": "string"},
                                    "session_id": {"type": "string"},
                                    "kinds": {"type": "array", "items": {"type": "string"},
                                              "description": "text, tool, you, instruct, result, raw"},
                                    "tail": {"type": "number"},
                                    "since": {"type": "number", "description": "epoch seconds"},
                                    "max_chars": {"type": "number"},
                                    "per_record_chars": {"type": "number"},
                                    "format": {"type": "string", "enum": ["text", "json"]}},
                     "additionalProperties": False}},
    {"name": "search_output",
     "description": "Search every journaled session output for a substring and return matching "
                    "records with agent, session, kind, time and path.",
     "inputSchema": {"type": "object",
                     "properties": {"query": {"type": "string"},
                                    "project": {"type": "string"},
                                    "cwd": {"type": "string"},
                                    "agent": {"type": "string"},
                                    "session_id": {"type": "string"},
                                    "limit": {"type": "number"},
                                    "max_chars": {"type": "number"},
                                    "ignore_case": {"type": "boolean"}},
                     "required": ["query"], "additionalProperties": False}},
]

HANDLERS = {
    "list_projects": tool_list_projects,
    "start_projects": tool_start_projects,
    "instruct": tool_instruct,
    "instruct_many": tool_instruct_many,
    "turn_result": tool_turn_result,
    "resume_project": tool_resume_project,
    "list_project_sessions": tool_list_project_sessions,
    "peer_status": tool_peer_status,
    "read_pane": tool_read_pane,
    "stop_peer": tool_stop_peer,
    "server_down": tool_server_down,
    "list_outputs": tool_list_outputs,
    "read_output": tool_read_output,
    "search_output": tool_search_output,
}

INSTRUCTIONS = (
    "Peer agents are project directories beside this one. list_projects launches nothing. "
    "start_projects starts only the names given and runs Claude Code in each project through airc. "
    "instruct and instruct_many prompt peers and return their turns; wait=false returns a turn_id "
    "for turn_result. resume_project continues a project's last conversation in claude, codex or "
    "freebuff (or a named one) after its peer stopped; list_project_sessions shows the candidates. "
    "Peers keep their conversation across prompts and stay alive until stop_peer or server_down. "
    "Every turn is journaled per session under .orch/projects; list_outputs, read_output and "
    "search_output are the MCP views, and resources under hyperagent://outputs read the same files. "
    "The peer monitor emits an idle event at turn end (trigger result or pane) with the "
    "whole last response; subscribe with resources/subscribe or "
    "subscriptions/listen (resourceSubscriptions, resourcesListChanged), follow logging/setLevel "
    "for notifications/message, and read hyperagent://events for the recent event stream. "
    "Run bin/watch locally for the same events as lines.")


async def send(message: dict) -> None:
    async with WRITE_LOCK:
        sys.stdout.write(json.dumps(message, separators=(",", ":")) + "\n")
        sys.stdout.flush()


def capabilities() -> dict:
    return {"tools": {"listChanged": False},
            "resources": {"subscribe": True, "listChanged": True},
            "logging": {}}


def valid_resource_uri(uri: str) -> bool:
    bare = uri.rstrip("/")
    return (bare in ("hyperagent://outputs", "hyperagent://events")
            or bare.startswith("hyperagent://outputs/"))


def listen_wants_resource(spec: dict, uri: str) -> bool:
    wanted = spec.get("resourceSubscriptions") or []
    return uri in wanted or "hyperagent://outputs" in wanted or "hyperagent://events" in wanted


def event_output_uri(event: dict) -> str | None:
    session = event.get("session")
    cwd = event.get("cwd")
    if not session or not cwd:
        return None
    return f"hyperagent://outputs/{outputs.slug(str(cwd))}/{session}"


async def log_message(level: str, data: dict) -> None:
    entry = {"level": level, "logger": "hyperagent", "data": data}
    if LOG_LEVEL is None:
        LOG_QUEUE.append(entry)
        del LOG_QUEUE[:-64]
        return
    if LOG_RANK.get(level, 4) < LOG_RANK.get(LOG_LEVEL, 4):
        return
    await send({"jsonrpc": "2.0", "method": "notifications/message", "params": entry})


async def emit_event(event: dict) -> None:
    event = dict(event)
    uri = event_output_uri(event)
    if uri:
        event["output"] = uri
    EVENT_LOG.append(event)
    del EVENT_LOG[:-200]
    new_output = bool(uri and uri not in KNOWN_OUTPUTS)
    if uri:
        KNOWN_OUTPUTS.add(uri)
    await log_message("warning" if event.get("event") == "gone" else "notice", event)
    for listen_id, spec in list(LISTENS.items()):
        meta = {SUB_ID: listen_id}
        if uri and listen_wants_resource(spec, uri):
            await send({"jsonrpc": "2.0", "method": "notifications/resources/updated",
                        "params": {"uri": uri, "_meta": meta}})
        if new_output and spec.get("resourcesListChanged"):
            await send({"jsonrpc": "2.0", "method": "notifications/resources/list_changed",
                        "params": {"_meta": meta}})
    for subscribed in list(SUBSCRIPTIONS):
        if subscribed in (uri, "hyperagent://outputs"):
            await send({"jsonrpc": "2.0", "method": "notifications/resources/updated",
                        "params": {"uri": subscribed}})
        elif subscribed == "hyperagent://events" and uri:
            await send({"jsonrpc": "2.0", "method": "notifications/resources/updated",
                        "params": {"uri": uri}})


async def monitor_loop() -> None:
    agents = [item.strip() for item in (os.environ.get("ORCH_WATCH_AGENTS") or "").split(",")
              if item.strip()]
    watcher = monitor.Monitor(
        orch,
        idle=float(os.environ.get("ORCH_WATCH_IDLE") or 20.0),
        poll=float(os.environ.get("ORCH_WATCH_POLL") or 1.0),
        agents=agents,
        last_chars=int(os.environ.get("ORCH_WATCH_LAST_CHARS") or 4000))
    try:
        async for event in watcher.stream():
            await emit_event(event)
    except asyncio.CancelledError:
        raise
    except (Exception, SystemExit) as exc:
        await log_message("error", {"error": f"{type(exc).__name__}: {exc}"})


async def ensure_monitor() -> None:
    global MONITOR_TASK
    if MONITOR_TASK is None or MONITOR_TASK.done():
        MONITOR_TASK = asyncio.create_task(monitor_loop())


async def progress_loop(token, name: str, step: int = 1) -> None:
    started = time.time()
    interval = float(os.environ.get("ORCH_PROGRESS_INTERVAL") or 5.0)
    while True:
        await asyncio.sleep(interval)
        await send({"jsonrpc": "2.0", "method": "notifications/progress",
                    "params": {"progressToken": token, "progress": step,
                               "message": f"{name} {round(time.time() - started)}s"}})
        step += 1


async def handle(message: dict) -> None:
    global LOG_LEVEL
    method = message.get("method")
    request_id = message.get("id")
    params = message.get("params") or {}
    if method == "initialize":
        await send({"jsonrpc": "2.0", "id": request_id, "result": {
            "protocolVersion": params.get("protocolVersion") or PROTOCOL_VERSION,
            "capabilities": capabilities(),
            "serverInfo": {"name": "hyperagent", "version": "0.1.0"},
            "instructions": INSTRUCTIONS}})
        return
    if method == "server/discover":
        await send({"jsonrpc": "2.0", "id": request_id, "result": {
            "resultType": "complete",
            "supportedVersions": ["2025-11-25", PROTOCOL_VERSION],
            "capabilities": capabilities(),
            "serverInfo": {"name": "hyperagent", "version": "0.1.0"},
            "instructions": INSTRUCTIONS}})
        return
    if method in ("notifications/initialized", "initialized", "exit",
                  "notifications/progress"):
        return
    if method == "notifications/cancelled":
        cancelled = params.get("requestId")
        if isinstance(cancelled, (str, int)) and cancelled in LISTENS:
            LISTENS.pop(cancelled, None)
            await send({"jsonrpc": "2.0", "id": cancelled,
                        "result": {"resultType": "complete"}})
        return
    if method == "logging/setLevel":
        level = str(params.get("level") or "")
        if level not in LOG_RANK:
            await send({"jsonrpc": "2.0", "id": request_id,
                        "error": {"code": -32602,
                                  "message": f"unknown log level: {level}"}})
            return
        LOG_LEVEL = level
        queued, LOG_QUEUE[:] = list(LOG_QUEUE), []
        for entry in queued:
            if LOG_RANK.get(entry.get("level"), 4) >= LOG_RANK[level]:
                await send({"jsonrpc": "2.0", "method": "notifications/message",
                            "params": entry})
        await ensure_monitor()
        await send({"jsonrpc": "2.0", "id": request_id, "result": {}})
        return
    if method in ("resources/subscribe", "resources/unsubscribe"):
        uri = str(params.get("uri") or "")
        if not valid_resource_uri(uri):
            await send({"jsonrpc": "2.0", "id": request_id,
                        "error": {"code": -32602, "message": f"unknown resource {uri}"}})
            return
        if method == "resources/subscribe":
            SUBSCRIPTIONS.add(uri)
        else:
            SUBSCRIPTIONS.discard(uri)
        await ensure_monitor()
        await send({"jsonrpc": "2.0", "id": request_id, "result": {}})
        if method == "resources/subscribe":
            await send({"jsonrpc": "2.0", "method": "notifications/resources/updated",
                        "params": {"uri": uri}})
        return
    if method == "subscriptions/listen":
        spec = params.get("notifications") or {}
        if not isinstance(spec, dict):
            await send({"jsonrpc": "2.0", "id": request_id,
                        "error": {"code": -32602,
                                  "message": "notifications must be an object"}})
            return
        LISTENS[request_id] = spec
        await send({"jsonrpc": "2.0", "method": "notifications/subscriptions/acknowledged",
                    "params": {"_meta": {SUB_ID: request_id}, "notifications": spec}})
        meta = {SUB_ID: request_id}
        for subscribed in spec.get("resourceSubscriptions") or []:
            await send({"jsonrpc": "2.0", "method": "notifications/resources/updated",
                        "params": {"uri": subscribed, "_meta": meta}})
        if spec.get("resourcesListChanged"):
            await send({"jsonrpc": "2.0", "method": "notifications/resources/list_changed",
                        "params": {"_meta": meta}})
        await ensure_monitor()
        return
    if method == "ping":
        await send({"jsonrpc": "2.0", "id": request_id, "result": {}})
        return
    if method == "tools/list":
        await send({"jsonrpc": "2.0", "id": request_id, "result": {"tools": TOOLS}})
        return
    if method == "resources/list":
        await send({"jsonrpc": "2.0", "id": request_id,
                    "result": {"resources": resource_list()}})
        return
    if method == "resources/templates/list":
        await send({"jsonrpc": "2.0", "id": request_id,
                    "result": {"resourceTemplates": resource_templates()}})
        return
    if method == "resources/read":
        uri = str(params.get("uri") or "")
        try:
            text, mime = read_resource(uri)
        except (OSError, ValueError) as exc:
            await send({"jsonrpc": "2.0", "id": request_id,
                        "error": {"code": -32602, "message": str(exc)}})
            return
        await send({"jsonrpc": "2.0", "id": request_id,
                    "result": {"contents": [{"uri": uri, "mimeType": mime, "text": text}]}})
        return
    if method == "tools/call":
        name = params.get("name")
        arguments = params.get("arguments") or {}
        handler = HANDLERS.get(name)
        if handler is None:
            await send({"jsonrpc": "2.0", "id": request_id, "result": {
                "content": [{"type": "text", "text": f"unknown tool {name!r}"}],
                "isError": True}})
            return
        token = (params.get("_meta") or {}).get("progressToken")
        ticker = None
        if token is not None:
            await send({"jsonrpc": "2.0", "method": "notifications/progress",
                        "params": {"progressToken": token, "progress": 0,
                                   "message": f"{name} 0s"}})
            ticker = asyncio.create_task(progress_loop(token, str(name)))
        try:
            text = await handler(arguments)
            await send({"jsonrpc": "2.0", "id": request_id, "result": {
                "content": [{"type": "text", "text": text}], "isError": False}})
        except (Exception, SystemExit) as exc:
            await send({"jsonrpc": "2.0", "id": request_id, "result": {
                "content": [{"type": "text", "text": f"{type(exc).__name__}: {exc}"}],
                "isError": True}})
        finally:
            if ticker is not None:
                ticker.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await ticker
        return
    if request_id is not None:
        await send({"jsonrpc": "2.0", "id": request_id,
                    "error": {"code": -32601, "message": f"method not found: {method}"}})


async def main() -> None:
    loop = asyncio.get_running_loop()
    pending: set[asyncio.Task] = set()
    while True:
        line = await loop.run_in_executor(None, sys.stdin.readline)
        if not line:
            break
        line = line.strip()
        if not line:
            continue
        try:
            message = json.loads(line)
        except json.JSONDecodeError:
            continue
        task = asyncio.create_task(handle(message))
        pending.add(task)
        task.add_done_callback(pending.discard)
    for task in list(pending):
        task.cancel()
    await asyncio.gather(*pending, return_exceptions=True)
    with contextlib.suppress(BrokenPipeError):
        for listen_id in list(LISTENS):
            await send({"jsonrpc": "2.0", "method": "notifications/cancelled",
                        "params": {"requestId": listen_id}})
    if MONITOR_TASK is not None:
        MONITOR_TASK.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await MONITOR_TASK


if __name__ == "__main__":
    asyncio.run(main())
