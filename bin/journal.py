#!/usr/bin/env python3
from __future__ import annotations

import json
import os
import re
import time
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_HOME = Path(os.environ.get("ORCH_HOME") or (ROOT / ".orch")).resolve()
TEXT_KINDS = ("text", "tool", "result", "you")


def journal_root() -> Path:
    raw = os.environ.get("ORCH_JOURNAL")
    return Path(raw).expanduser().resolve() if raw else DEFAULT_HOME / "projects"


def slug(cwd: str) -> str:
    return re.sub(r"[^A-Za-z0-9]", "-", os.path.abspath(os.path.expanduser(str(cwd))))


def project_dir(cwd: str, root: Path | str | None = None) -> Path:
    return Path(root).expanduser().resolve() / slug(cwd) if root else journal_root() / slug(cwd)


def session_path(cwd: str, session_id: str, root: Path | str | None = None) -> Path:
    return project_dir(cwd, root) / f"{session_id}.jsonl"


def stamp(epoch: float | None = None) -> str:
    moment = datetime.fromtimestamp(time.time() if epoch is None else epoch, timezone.utc)
    return moment.isoformat(timespec="milliseconds").replace("+00:00", "Z")


def truncate(text: str, limit: int) -> str:
    if limit <= 0 or len(text) <= limit:
        return text
    return text[:limit] + f"… [{len(text) - limit} chars cut]"


class Journal:
    def __init__(self, agent: str, cwd: str, harness: str, session_id: str | None = None,
                 root: Path | str | None = None):
        self.agent = agent
        self.cwd = os.path.abspath(os.path.expanduser(str(cwd)))
        self.harness = harness
        self.root = Path(root).expanduser().resolve() if root else journal_root()
        self.session_id = session_id or None
        self.fallback_id = f"peer-{agent}"
        self.path = session_path(self.cwd, self.session_id or self.fallback_id, self.root)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self._pending: list[dict] = []
        self._buffer: list[dict] = []
        self._last_write = 0.0

    def bind(self, session_id: str | None) -> Path:
        self.flush()
        wanted = session_id or self.fallback_id
        target = session_path(self.cwd, wanted, self.root)
        target.parent.mkdir(parents=True, exist_ok=True)
        if target == self.path:
            self.session_id = session_id or None
            return target
        previous = self.path
        old_id = self.session_id or self.fallback_id
        if old_id != self.fallback_id:
            self.session_id = session_id or None
            self.path = target
            self._flush()
            return target
        if previous and previous.exists() and previous.stat().st_size > 0:
            if target.exists():
                with open(previous, "r", encoding="utf-8", errors="replace") as source, \
                        open(target, "a", encoding="utf-8") as destination:
                    for line in source:
                        destination.write(self._rebind_text(line, old_id, wanted))
                previous.unlink(missing_ok=True)
            else:
                os.replace(previous, target)
                text = target.read_text(encoding="utf-8", errors="replace")
                target.write_text(self._rebind_text(text, old_id, wanted), encoding="utf-8")
        self.session_id = session_id or None
        self.path = target
        self._flush()
        return target

    @staticmethod
    def _rebind_text(text: str, old_id: str, new_id: str) -> str:
        if old_id == new_id:
            return text
        return text.replace(f'"session":"{old_id}"', f'"session":"{new_id}"')

    def _record(self, kind: str, text: str, fields: dict, epoch: float) -> dict:
        record = {"ts": stamp(epoch), "epoch": round(epoch, 3), "agent": self.agent,
                  "harness": self.harness, "cwd": self.cwd,
                  "session": self.session_id or self.fallback_id, "kind": kind}
        if text:
            record["text"] = text
        record.update({key: value for key, value in fields.items() if value is not None})
        return record

    def append(self, kind: str, text: str = "", epoch: float | None = None, **fields) -> dict:
        now = time.time() if epoch is None else epoch
        record = self._record(kind, text, fields, now)
        if self.path is None:
            self._pending.append(record)
        else:
            self._buffer.append(record)
            if len(self._buffer) >= 32 or time.time() - self._last_write >= 0.2:
                self.flush()
        return record

    def __del__(self):
        try:
            self.flush()
        except Exception:
            pass

    def flush(self) -> None:
        if self._buffer:
            records, self._buffer = self._buffer, []
            self._write_records(records)
        self._last_write = time.time()

    def _write_records(self, records: list[dict]) -> None:
        with open(self.path, "a", encoding="utf-8") as handle:
            for record in records:
                handle.write(json.dumps(record, separators=(",", ":"), ensure_ascii=False) + "\n")

    def _flush(self) -> None:
        self.flush()
        pending, self._pending = self._pending, []
        for record in pending:
            record["session"] = self.session_id or self.fallback_id
        if pending:
            self._write_records(pending)
        self._last_write = time.time()


def iter_records(path: Path | str):
    try:
        handle = open(path, "r", encoding="utf-8", errors="replace")
    except OSError:
        return
    with handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            try:
                yield json.loads(line)
            except json.JSONDecodeError:
                continue


def tail_records(path: Path | str, limit: int = 40, kinds=None,
                 max_bytes: int = 1_000_000) -> list[dict]:
    try:
        size = os.path.getsize(path)
    except OSError:
        return []
    with open(path, "rb") as handle:
        if size > max_bytes:
            handle.seek(size - max_bytes)
        chunk = handle.read().decode("utf-8", "replace")
    lines = chunk.splitlines()
    if size > max_bytes and lines:
        lines = lines[1:]
    wanted = set(kinds) if kinds else None
    found: list[dict] = []
    for line in reversed(lines):
        line = line.strip()
        if not line:
            continue
        try:
            record = json.loads(line)
        except json.JSONDecodeError:
            continue
        if wanted and record.get("kind") not in wanted:
            continue
        found.append(record)
        if len(found) >= limit:
            break
    found.reverse()
    return found


def read_records(path: Path | str, kinds=None, limit: int | None = None,
                 since: float | None = None) -> list[dict]:
    wanted = set(kinds) if kinds else None
    found: list[dict] = []
    for record in iter_records(path):
        if wanted and record.get("kind") not in wanted:
            continue
        if since is not None and float(record.get("epoch") or 0) < since:
            continue
        found.append(record)
    return found[-limit:] if limit else found


def last_record(path: Path | str, kinds=None) -> dict | None:
    found = tail_records(path, 1, kinds)
    return found[0] if found else None


def summarize(path: Path) -> dict:
    stat = path.stat()
    last = last_record(path)
    spoken = (last_record(path, kinds=("text",))
              or last_record(path, kinds=("tool", "you")))
    return {"project": path.parent.name, "session": path.stem,
            "agent": (last or {}).get("agent") or path.stem,
            "harness": (last or {}).get("harness"),
            "cwd": (last or {}).get("cwd"),
            "path": str(path), "bytes": stat.st_size,
            "mtime": round(stat.st_mtime, 3),
            "last_ts": (last or {}).get("ts"),
            "last_kind": (last or {}).get("kind"),
            "last_text": truncate(str((spoken or {}).get("text") or ""), 500)}


def scan(root: Path | str | None = None, cwd: str | None = None, agent: str | None = None,
         session_id: str | None = None, limit: int | None = None) -> list[dict]:
    base = Path(root).expanduser().resolve() if root else journal_root()
    if cwd:
        directories = [project_dir(cwd, base)]
    elif base.is_dir():
        directories = [path for path in base.iterdir() if path.is_dir()]
    else:
        directories = []
    rows: list[dict] = []
    for directory in directories:
        try:
            files = [path for path in directory.glob("*.jsonl") if path.is_file()]
        except OSError:
            continue
        for path in files:
            if session_id and path.stem != session_id:
                continue
            try:
                row = summarize(path)
            except OSError:
                continue
            if agent and row["agent"] != agent:
                continue
            rows.append(row)
    rows.sort(key=lambda row: row["mtime"], reverse=True)
    return rows[:limit] if limit else rows


def find_session(agent: str | None = None, cwd: str | None = None,
                 session_id: str | None = None, root: Path | str | None = None) -> dict | None:
    rows = scan(root=root, cwd=cwd, agent=agent, session_id=session_id, limit=1)
    return rows[0] if rows else None


def latest_output(cwd: str, agent: str,
                  root: Path | str | None = None) -> tuple[dict | None, dict | None]:
    base = Path(root).expanduser().resolve() if root else journal_root()
    directory = project_dir(cwd, base)
    if not directory.is_dir():
        return None, None
    try:
        files = sorted((path for path in directory.glob("*.jsonl") if path.is_file()),
                       key=lambda path: path.stat().st_mtime, reverse=True)
    except OSError:
        return None, None
    for path in files:
        records = tail_records(path, 400, kinds=TEXT_KINDS)
        text = None
        tool = None
        fallback = None
        for record in reversed(records):
            if record.get("agent") != agent:
                continue
            kind = record.get("kind")
            if kind == "text" and text is None:
                text = record
            elif kind == "tool" and tool is None:
                tool = record
            elif fallback is None:
                fallback = record
        selected = text or tool or fallback
        if selected is not None:
            return summarize(path), selected
    return None, None


def render_records(records: list[dict], header: str = "", max_chars: int = 20000,
                   per_record: int = 2000) -> str:
    lines: list[str] = []
    if header:
        lines.append(header)
    for record in records:
        body = str(record.get("text") or "")
        if not body and record.get("status"):
            body = f"status={record.get('status')} duration={record.get('duration_s')}s"
        body = truncate(body.replace("\r", ""), per_record)
        body = body.replace("\n", "\n  ")
        value = str(record.get("ts") or "")
        clock = value[11:19] if len(value) >= 19 else value
        kind = str(record.get("kind") or "?")
        lines.append(f"{clock} {kind:<7} {record.get('agent') or ''}  {body}".rstrip())
    text = "\n".join(lines)
    if len(text) > max_chars:
        text = text[:max_chars] + f"\n… [{len(text) - max_chars} chars cut]"
    return text
