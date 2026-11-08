#!/usr/bin/env python3
import argparse
import json
import mimetypes
import os
import queue
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

GUI_DIR = Path(__file__).resolve().parent
PUBLIC_DIR = GUI_DIR / "public"
REPO_DIR = GUI_DIR.parent
JOURNAL_ROOT = Path(os.environ.get("ORCH_JOURNAL", REPO_DIR / ".orch" / "projects"))
SERVER_LOG = REPO_DIR / ".orch" / "server.log"

POLL_SECONDS = 0.2
DISCOVER_SECONDS = 1.0
WATCH_SECONDS = 0.3
KEEPALIVE_SECONDS = 15
READ_BLOCK = 65536
BACKFILL_PER_SOURCE = 500
BACKFILL_TOTAL = 1500
SUBSCRIBER_DEPTH = 4096

mimetypes.add_type("text/javascript", ".js")
mimetypes.add_type("text/javascript", ".mjs")
mimetypes.add_type("text/css", ".css")


def read_last_lines(path, end, count):
    with path.open("rb") as handle:
        position = end
        block = b""
        while position > 0 and block.count(b"\n") <= count:
            step = min(READ_BLOCK, position)
            position -= step
            handle.seek(position)
            block = handle.read(step) + block
        starts_mid_line = False
        if position > 0:
            handle.seek(position - 1)
            starts_mid_line = handle.read(1) != b"\n"
    cursor = position
    entries = []
    for segment in block.split(b"\n")[:-1]:
        entries.append((cursor, segment))
        cursor += len(segment) + 1
    if starts_mid_line:
        entries = entries[1:]
    return [entry for entry in entries if entry[1].strip()][-count:]


def last_line_break(path):
    try:
        size = path.stat().st_size
    except FileNotFoundError:
        return 0
    if size == 0:
        return 0
    with path.open("rb") as handle:
        start = max(0, size - READ_BLOCK)
        handle.seek(start)
        block = handle.read(size - start)
    index = block.rfind(b"\n")
    return start + index + 1 if index >= 0 else 0


class Tail:
    def __init__(self, path, source, make_record):
        self.path = path
        self.source = source
        self.make_record = make_record
        self.read_offset = 0
        self.complete_end = 0
        self.carry = b""

    def start_at_end(self):
        self.read_offset = self.complete_end = last_line_break(self.path)

    def read_new(self):
        try:
            size = self.path.stat().st_size
        except FileNotFoundError:
            return []
        if size < self.read_offset:
            self.read_offset = 0
            self.complete_end = 0
            self.carry = b""
        if size <= self.read_offset:
            return []
        with self.path.open("rb") as handle:
            handle.seek(self.read_offset)
            chunk = handle.read(size - self.read_offset)
        data = self.carry + chunk
        position = self.complete_end
        pieces = data.split(b"\n")
        self.carry = pieces.pop()
        self.read_offset = size
        records = []
        for piece in pieces:
            if piece.strip():
                records.append(self.make_record(self.source, position, piece))
            position += len(piece) + 1
        self.complete_end = position
        return records

    def last_records(self, count):
        if self.complete_end == 0:
            return []
        try:
            lines = read_last_lines(self.path, self.complete_end, count)
        except FileNotFoundError:
            return []
        return [self.make_record(self.source, position, line) for position, line in lines]


def journal_record(source, position, line):
    text = line.decode("utf-8", "replace")
    try:
        record = json.loads(text)
    except ValueError:
        record = None
    if not isinstance(record, dict):
        record = {"kind": "malformed", "text": text}
    record.setdefault("epoch", time.time())
    record.setdefault("agent", source.split("/")[0])
    record["src"] = source
    record["id"] = f"{source}@{position}"
    return record


def log_record(source, position, line):
    return {
        "id": f"{source}@{position}",
        "src": source,
        "kind": "log",
        "agent": "airc",
        "session": "server.log",
        "epoch": time.time(),
        "text": line.decode("utf-8", "replace"),
    }


class Hub:
    def __init__(self):
        self.lock = threading.Lock()
        self.subscribers = set()

    def subscribe(self):
        subscriber = queue.Queue(SUBSCRIBER_DEPTH)
        with self.lock:
            self.subscribers.add(subscriber)
        return subscriber

    def unsubscribe(self, subscriber):
        with self.lock:
            self.subscribers.discard(subscriber)

    def publish(self, event, payload):
        with self.lock:
            targets = list(self.subscribers)
        for subscriber in targets:
            try:
                subscriber.put_nowait((event, payload))
            except queue.Full:
                self.evict(subscriber)

    def evict(self, subscriber):
        self.unsubscribe(subscriber)
        while True:
            try:
                subscriber.get_nowait()
            except queue.Empty:
                break
        subscriber.put_nowait(("close", ""))


class Journal:
    def __init__(self, hub):
        self.hub = hub
        self.lock = threading.Lock()
        self.tails = {}

    def discover(self, initial):
        found = {}
        if JOURNAL_ROOT.is_dir():
            for path in JOURNAL_ROOT.rglob("*.jsonl"):
                found[path] = f"{path.parent.name}/{path.stem}"
        found[SERVER_LOG] = "airc/server"
        with self.lock:
            for path in [known for known in self.tails if known not in found]:
                del self.tails[path]
            for path, source in found.items():
                if path in self.tails:
                    continue
                make = log_record if path == SERVER_LOG else journal_record
                tail = Tail(path, source, make)
                if initial:
                    tail.start_at_end()
                self.tails[path] = tail

    def snapshot_tails(self):
        with self.lock:
            return list(self.tails.values())

    def run(self):
        next_discover = 0.0
        initial = True
        while True:
            if time.monotonic() >= next_discover:
                self.discover(initial)
                initial = False
                next_discover = time.monotonic() + DISCOVER_SECONDS
            batch = []
            for tail in self.snapshot_tails():
                batch.extend(tail.read_new())
            if batch:
                batch.sort(key=lambda record: record.get("epoch", 0))
                self.hub.publish("records", json.dumps(batch))
            time.sleep(POLL_SECONDS)

    def backfill(self):
        records = []
        for tail in self.snapshot_tails():
            records.extend(tail.last_records(BACKFILL_PER_SOURCE))
        records.sort(key=lambda record: record.get("epoch", 0))
        return json.dumps(records[-BACKFILL_TOTAL:])


class Watcher:
    def __init__(self, hub):
        self.hub = hub

    def snapshot(self):
        files = {}
        for path in PUBLIC_DIR.rglob("*"):
            if path.is_file():
                files[str(path.relative_to(PUBLIC_DIR))] = path.stat().st_mtime_ns
        files["server.py"] = (GUI_DIR / "server.py").stat().st_mtime_ns
        return files

    def run(self):
        seen = self.snapshot()
        while True:
            time.sleep(WATCH_SECONDS)
            current = self.snapshot()
            changed = sorted(name for name, stamp in current.items() if seen.get(name) != stamp)
            seen = current
            if changed:
                self.react(changed)

    def react(self, changed):
        restart = "server.py" in changed and self.restart_is_safe()
        self.hub.publish("change", json.dumps({"files": changed, "restart": restart}))
        if restart:
            time.sleep(0.2)
            os.execv(sys.executable, [sys.executable, *sys.argv])

    def restart_is_safe(self):
        try:
            compile((GUI_DIR / "server.py").read_text(encoding="utf-8"), "server.py", "exec")
        except (SyntaxError, OSError) as error:
            print(f"gui: server.py not restarted: {error}", file=sys.stderr, flush=True)
            return False
        return True


def make_handler(journal, reloads):
    class Handler(BaseHTTPRequestHandler):
        server_version = "orch-gui"

        def do_GET(self):
            route = urlsplit(self.path).path
            if route == "/api/events":
                self.stream(journal.hub, journal.backfill)
            elif route == "/api/reload":
                self.stream(reloads)
            else:
                self.static("index.html" if route == "/" else route.lstrip("/"))

        def stream(self, hub, initial=None):
            self.begin_stream()
            subscriber = hub.subscribe()
            try:
                if initial is not None:
                    self.send_event("snapshot", initial())
                self.write(b"retry: 1000\n\n")
                while True:
                    try:
                        event, payload = subscriber.get(timeout=KEEPALIVE_SECONDS)
                    except queue.Empty:
                        self.write(b": keepalive\n\n")
                        continue
                    if event == "close":
                        break
                    self.send_event(event, payload)
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                hub.unsubscribe(subscriber)

        def begin_stream(self):
            self.send_response(200)
            self.send_header("Content-Type", "text/event-stream")
            self.send_header("Cache-Control", "no-store")
            self.end_headers()

        def send_event(self, event, payload):
            self.write(f"event: {event}\ndata: {payload}\n\n".encode("utf-8"))

        def write(self, data):
            self.wfile.write(data)
            self.wfile.flush()

        def static(self, relative):
            target = (PUBLIC_DIR / relative).resolve()
            if not target.is_relative_to(PUBLIC_DIR) or not target.is_file():
                self.send_error(404)
                return
            body = target.read_bytes()
            kind = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
            self.send_response(200)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, format, *args):
            return

    return Handler


def main():
    parser = argparse.ArgumentParser(description="Realtime GUI for hyperagent logs")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8790)
    arguments = parser.parse_args()

    reloads = Hub()
    journal = Journal(Hub())
    threading.Thread(target=journal.run, daemon=True).start()
    threading.Thread(target=Watcher(reloads).run, daemon=True).start()

    ThreadingHTTPServer.daemon_threads = True
    server = ThreadingHTTPServer((arguments.host, arguments.port), make_handler(journal, reloads))
    print(f"orch gui on http://{arguments.host}:{arguments.port}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()


if __name__ == "__main__":
    main()
