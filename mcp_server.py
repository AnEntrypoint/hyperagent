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
PROTOCOL_VERSION = "2025-06-18"
DEFAULT_TIMEOUT = 1800.0
DEFAULT_IDLE = 120.0
LOCK_WAIT = 1800.0
DEFAULT_CONCURRENCY = 8
TURNS: dict[str, dict] = {}
WRITE_LOCK = asyncio.Lock()


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
    if flag(args, "wait", True):
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
    if flag(args, "wait", True):
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
        if flag(args, "wait", True):
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


async def tool_peer_status(args: dict) -> str:
    await orch.ensure_server()
    return json.dumps(await orch.fetch_sessions(), indent=2, sort_keys=True)


async def tool_read_pane(args: dict) -> str:
    name = orch.check_name(str(args.get("name") or ""))
    pane = await orch.capture(name)
    lines = [line.rstrip() for line in pane.splitlines() if line.strip()]
    count = int(args.get("lines") or 40)
    return "\n".join(lines[-count:]) or "(empty pane)"


async def tool_stop_peer(args: dict) -> str:
    name = orch.check_name(str(args.get("name") or ""))
    if await orch.server_alive():
        async with aiohttp.ClientSession() as http:
            async with http.delete(orch.base_url() + f"/sessions/{name}",
                                   timeout=aiohttp.ClientTimeout(total=30)) as response:
                if response.status == 200:
                    return f"stopped {name}"
                if response.status != 404:
                    raise RuntimeError(f"stop {name} failed: HTTP {response.status}")
    proc = await asyncio.create_subprocess_exec(
        "tmux", "kill-session", "-t", f"ccb-{name}",
        stdout=asyncio.subprocess.DEVNULL, stderr=asyncio.subprocess.DEVNULL)
    code = await proc.wait()
    return f"no hub {name}; tmux kill-session ccb-{name} exit={code}"


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
                                             "description": "false returns a turn_id instead of waiting"},
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
                                    "wait": {"type": "boolean"},
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
                                    "wait": {"type": "boolean"},
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
     "description": "List live peers with alive and busy state, cwd, harness, launch args and pane tail.",
     "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
    {"name": "read_pane",
     "description": "Print the tail of a peer's terminal pane.",
     "inputSchema": {"type": "object",
                     "properties": {"name": {"type": "string"}, "lines": {"type": "number"}},
                     "required": ["name"], "additionalProperties": False}},
    {"name": "stop_peer",
     "description": "Retire one peer session and its tmux session.",
     "inputSchema": {"type": "object", "properties": {"name": {"type": "string"}},
                     "required": ["name"], "additionalProperties": False}},
    {"name": "server_down",
     "description": "Stop the airc server and every peer it owns.",
     "inputSchema": {"type": "object", "properties": {}, "additionalProperties": False}},
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
}

INSTRUCTIONS = (
    "Peer agents are project directories beside this one. list_projects launches nothing. "
    "start_projects starts only the names given and runs Claude Code in each project through airc. "
    "instruct and instruct_many prompt peers and return their turns; wait=false returns a turn_id "
    "for turn_result. resume_project continues a project's last conversation in claude, codex or "
    "freebuff (or a named one) after its peer stopped; list_project_sessions shows the candidates. Peers keep their "
    "conversation across prompts and stay alive until stop_peer or server_down.")


async def send(message: dict) -> None:
    async with WRITE_LOCK:
        sys.stdout.write(json.dumps(message, separators=(",", ":")) + "\n")
        sys.stdout.flush()


async def handle(message: dict) -> None:
    method = message.get("method")
    request_id = message.get("id")
    params = message.get("params") or {}
    if method == "initialize":
        await send({"jsonrpc": "2.0", "id": request_id, "result": {
            "protocolVersion": params.get("protocolVersion") or PROTOCOL_VERSION,
            "capabilities": {"tools": {"listChanged": False}},
            "serverInfo": {"name": "hyperagent", "version": "0.1.0"},
            "instructions": INSTRUCTIONS}})
        return
    if method in ("notifications/initialized", "initialized", "exit",
                  "notifications/cancelled", "notifications/progress"):
        return
    if method == "ping":
        await send({"jsonrpc": "2.0", "id": request_id, "result": {}})
        return
    if method == "tools/list":
        await send({"jsonrpc": "2.0", "id": request_id, "result": {"tools": TOOLS}})
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
        try:
            text = await handler(arguments)
            await send({"jsonrpc": "2.0", "id": request_id, "result": {
                "content": [{"type": "text", "text": text}], "isError": False}})
        except (Exception, SystemExit) as exc:
            await send({"jsonrpc": "2.0", "id": request_id, "result": {
                "content": [{"type": "text", "text": f"{type(exc).__name__}: {exc}"}],
                "isError": True}})
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


if __name__ == "__main__":
    asyncio.run(main())
