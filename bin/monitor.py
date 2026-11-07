#!/usr/bin/env python3
from __future__ import annotations

import asyncio
import hashlib
import json
import time

import journal

IDLE_PROMPT = ("You stopped producing output. Your last output was:\n{last}\n\n"
               "Continue the task from there and report what remains.")
GONE_PROMPT = ("Your session stopped before the task finished. Your last output was:\n{last}\n\n"
               "Resume and continue from there.")


def pane_digest(pane: str) -> str:
    lines = [line.rstrip() for line in pane.splitlines()]
    while lines and not lines[0].strip():
        lines.pop(0)
    while lines and not lines[-1].strip():
        lines.pop()
    return hashlib.sha256("\n".join(lines).encode("utf-8", "replace")).hexdigest()


def pane_text(pane: str, count: int = 8) -> str:
    lines = [line.rstrip() for line in pane.splitlines() if line.strip()]
    return "\n".join(lines[-count:])


def last_output(agent: str, info: dict, pane: str, last_chars: int = 4000) -> dict:
    cwd = info.get("cwd")
    row = None
    record = None
    if cwd:
        try:
            row, record = journal.latest_output(str(cwd), agent)
        except Exception:
            row, record = None, None
    if record:
        text = str(record.get("text") or "")
        if not text and record.get("status"):
            text = f"status={record.get('status')} duration={record.get('duration_s')}s"
        return {"kind": record.get("kind") or "text", "ts": record.get("ts") or journal.stamp(),
                "text": journal.truncate(text, last_chars), "source": "journal",
                "path": (row or {}).get("path"), "session": (row or {}).get("session")}
    return {"kind": "pane", "ts": journal.stamp(),
            "text": journal.truncate(pane_text(pane), last_chars), "source": "pane",
            "path": None, "session": None}


def make_prompt(template: str, agent: str, last: str, session: str | None) -> str:
    return (template.replace("{agent}", agent)
                    .replace("{last}", last or "(no output recorded)")
                    .replace("{session}", session or ""))


def render(event: dict, text_mode: bool) -> str:
    if not text_mode:
        return json.dumps(event, separators=(",", ":"), ensure_ascii=False)
    head = f'{event["event"].upper()} agent={event["agent"]}'
    if event.get("session"):
        head += f' session={event["session"]}'
    if event.get("idle_s") is not None:
        head += f' idle={event["idle_s"]}s'
    if event.get("cwd"):
        head += f' cwd={event["cwd"]}'
    last = event.get("last") or {}
    head += f' last={json.dumps(last.get("text") or "", ensure_ascii=False)}'
    head += f' instruct={json.dumps(event["instruct"]["arguments"], ensure_ascii=False)}'
    return head


class Monitor:
    def __init__(self, orch, idle: float = 20.0, poll: float = 1.0, agents=None,
                 last_chars: int = 4000, prompt: str = IDLE_PROMPT,
                 gone_prompt: str = GONE_PROMPT):
        self.orch = orch
        self.idle = max(0.0, float(idle))
        self.poll = max(0.05, float(poll))
        self.agents = {item for item in (agents or []) if item}
        self.last_chars = last_chars
        self.prompt = prompt
        self.gone_prompt = gone_prompt
        self.states: dict[str, dict] = {}

    async def sessions(self) -> dict:
        try:
            return await self.orch.fetch_sessions()
        except Exception:
            return {}

    def idle_event(self, agent: str, info: dict, last: dict, idle_s: float,
                   trigger: str = "idle") -> dict:
        prompt = make_prompt(self.prompt, agent, last["text"], last.get("session"))
        return {"event": "idle", "agent": agent, "session": last.get("session"),
                "cwd": info.get("cwd"), "harness": info.get("harness"),
                "idle_s": round(idle_s, 1), "busy": bool(info.get("busy")),
                "trigger": trigger, "last": last,
                "instruct": {"tool": "instruct",
                             "arguments": {"name": agent, "prompt": prompt}}}

    def gone_event(self, agent: str, info: dict, last: dict) -> dict:
        prompt = make_prompt(self.gone_prompt, agent, last["text"], last.get("session"))
        return {"event": "gone", "agent": agent, "session": last.get("session"),
                "cwd": info.get("cwd"), "harness": info.get("harness"),
                "last": last,
                "instruct": {"tool": "resume_project",
                             "arguments": {"project": agent, "prompt": prompt}}}

    async def once(self):
        sessions = await self.sessions()
        for name in sorted(sessions):
            info = sessions[name]
            if not info.get("alive") or (self.agents and name not in self.agents):
                continue
            if info.get("busy"):
                continue
            pane = await self.orch.capture(name)
            last = last_output(name, info, pane, self.last_chars)
            yield self.idle_event(name, info, last, 0.0, "once")

    def result_mark(self, name: str, info: dict):
        cwd = info.get("cwd")
        if not cwd:
            return None, None
        try:
            path, record = journal.latest_result(str(cwd), name)
        except Exception:
            return None, None
        if not record:
            return None, None
        return record.get("epoch") or record.get("ts"), path

    def busy(self, name: str, info: dict, pane: str) -> bool:
        value = info.get("busy")
        if value is not None:
            return bool(value)
        try:
            return bool(self.orch.is_busy(info.get("harness"), pane))
        except Exception:
            return False

    async def stream(self):
        while True:
            sessions = await self.sessions()
            now = time.time()
            alive = {name: info for name, info in sessions.items()
                     if info.get("alive") and (not self.agents or name in self.agents)}
            for name in sorted(alive):
                info = alive[name]
                pane = await self.orch.capture(name)
                digest = pane_digest(pane)
                busy = self.busy(name, info, pane)
                mark, _ = self.result_mark(name, info)
                state = self.states.get(name)
                if state is None:
                    self.states[name] = {"digest": digest, "changed": now,
                                         "reported": False, "gone": False, "info": info,
                                         "result_mark": mark, "was_busy": busy}
                    continue
                state["info"] = info
                if mark and mark != state.get("result_mark"):
                    state.update(result_mark=mark, digest=digest, changed=now,
                                 reported=True, was_busy=busy, gone=False)
                    last = last_output(name, info, pane, self.last_chars)
                    yield self.idle_event(name, info, last, 0.0, "result")
                    continue
                if state.get("was_busy") and not busy and not state["reported"]:
                    state.update(digest=digest, changed=now, reported=True,
                                 was_busy=busy, gone=False)
                    last = last_output(name, info, pane, self.last_chars)
                    yield self.idle_event(name, info, last, 0.0, "pane")
                    continue
                if busy and not state.get("was_busy"):
                    state["reported"] = False
                    state["changed"] = now
                state["was_busy"] = busy
                if digest != state["digest"]:
                    state.update(digest=digest, changed=now, reported=False, gone=False)
                    continue
                if not state["reported"] and now - state["changed"] >= self.idle:
                    last = last_output(name, info, pane, self.last_chars)
                    yield self.idle_event(name, info, last, now - state["changed"], "idle")
                    state["reported"] = True
            for name, state in list(self.states.items()):
                if name in alive or state["gone"]:
                    continue
                state["gone"] = True
                info = sessions.get(name) or state.get("info") or {"cwd": None, "harness": None}
                last = last_output(name, info, "", self.last_chars)
                yield self.gone_event(name, info, last)
            await asyncio.sleep(self.poll)
