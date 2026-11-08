# hyperagent

hyperagent orchestrates the projects beside it: peer coding-harness sessions are started
and prompted directly through `../airc` instead of Claude sessions reached over the
messaging bus. The orchestrator talks to them through the `hyperagent` MCP server.

## MCP server

`.mcp.json` registers the `hyperagent` stdio server (`python3 mcp_server.py`), and
`.claude/settings.json` lists it in `enabledMcpjsonServers` so opening the project
enables it without a prompt. `claude mcp list` reports it as connected.

Tools: `list_projects`, `start_projects`, `instruct`, `instruct_many`,
`turn_result`, `resume_project`, `list_project_sessions`, `list_outputs`,
`read_output`, `search_output`, `peer_status`, `read_pane`, `stop_peer`,
`server_down`.
`list_projects` launches nothing. `start_projects` starts only the names given and
runs Claude Code in each project through airc. `instruct` and `instruct_many` return
the peer's turn; `wait: false` returns a turn id for `turn_result`.
`resume_project` continues a project's saved Claude conversation; with no project it
resumes the last project used. `list_project_sessions` lists resumable sessions.

## Session output journal

Every frame a turn receives from airc is appended to
`.orch/projects/<slug(cwd)>/<session>.jsonl`, mirroring the `~/.claude/projects`
layout: one directory per cwd with every non-alphanumeric character replaced by `-`,
and one JSONL file per harness session. A record carries `ts`, `epoch`, `agent`,
`harness`, `cwd`, `session` and `kind`; `text` and extra fields follow. Kinds are
`instruct` (the prompt), `you` (prompt echo), `text`, `tool`, `raw` (an untagged
frame) and `result` (`status`, `duration_s`, `acked`, `pane_tail`).

The writer binds to a real session id from the job's resume id, the live airc
`launch_args`, or the newest harness transcript in the cwd, in that order. Until it
binds it writes to `peer-<name>.jsonl`; on bind that file is renamed and its
`session` fields are rewritten. `ORCH_JOURNAL` overrides the journal root.

Every frame is logged, including each streamed text delta. A turn's returned text is
only its last response block: the text after the last tool frame (or all text when
the turn had no tool call). Interim narration and tool names stay in the journal;
`tool_names` rides beside the result. `last_response` rebuilds that same final block
from the deltas, so a watcher event carries the whole last response rather than a
single delta.

`list_outputs` reads the index, `read_output` renders a compact transcript with
filters (`kinds`, `tail`, `since`, `format`), and `search_output` greps every
journal. The same files are MCP resources: `hyperagent://outputs` is the index,
`hyperagent://outputs/<slug>/<session>` is the raw JSONL, and
`hyperagent://outputs/<slug>/<session>/view` is the rendered transcript.
`hyperagent://events` is the recent monitor event stream. The CLI equivalents are
`bin/orch outputs` and `bin/orch output`.

`run_job` returns `output_path` and `output_session`, and `render_result` prints the
path; `peer_status` annotates live peers with their latest journal.

## Watcher and MCP events

`bin/watch` starts a local monitor. It polls `GET /sessions`, reads each peer's
`pane_tail`, and hashes the pane text. An event fires as the turn finishes, not after
a quiet window: a new `result` record in the session journal is the primary trigger,
and a pane that goes from busy to not busy is the second. Both report `idle_s: 0`
with `trigger: "result"` or `"pane"`. The `--idle` seconds (default 20) are only the
fallback for a peer with no journal and no busy marker (`trigger: "idle"`). One line
is emitted per finish, carrying the whole last response (or the pane tail when no
journal exists) and a ready `instruct` call whose prompt feeds that response back.
It emits `gone` when a live peer disappears. `--once` reports peers that airc marks
not busy. Output is one-line JSON, or `--text`.

The MCP server runs the same `bin/monitor.py` loop natively when a client asks for
events. Delivery surfaces:

| MCP surface | what it carries |
|---|---|
| `logging/setLevel` + `notifications/message` | every idle and gone event; `notice` for idle, `warning` for gone; queued up to 64 until a level is set |
| `resources/subscribe` + `notifications/resources/updated` | the session journal URI, `hyperagent://outputs`, or `hyperagent://events` |
| `subscriptions/listen` | `resourceSubscriptions` plus `resourcesListChanged`; acknowledged with `notifications/subscriptions/acknowledged` and a `io.modelcontextprotocol/subscriptionId` `_meta`; each update carries that id |
| `notifications/resources/list_changed` | a session URI seen for the first time |
| `notifications/progress` | tool calls whose `params._meta.progressToken` is set; progress starts at 0 |
| `server/discover` | supported versions, capabilities and instructions for modern clients |

`notifications/cancelled` with a listen request id ends that stream with a
`resultType: complete` result. `resources/subscribe` and `resources/unsubscribe` are
kept alongside `subscriptions/listen` for clients that only implement the 2025-06-18
surface.

`bin/open` starts Claude Code in this project with `MCP_TOOL_TIMEOUT` set to 30
minutes, so a turn can be collected in one call. Without it the client's shorter tool
timeout can cut a long turn; use `wait: false` plus `turn_result` then. `hyperagent open`
is its C++ port: it sets `MCP_TIMEOUT` (60000) and `MCP_TOOL_TIMEOUT` (1800000) only when
unset, changes to the repo root, and then runs the `hyperagent mcp` entry point. Until that
subcommand is ported it exits 3 with "not yet ported" and launches nothing; `bin/open` stays
the working launcher until then.

`mcp_server.py` needs only aiohttp, declared in `requirements.txt` (on a PEP 668 system
Python install it with `sudo apt install python3-aiohttp`), and imports `bin/orch` as its
implementation. Stdout is the JSON-RPC channel: tool handlers must never print to
stdout, and only the launcher and diagnostics write to stderr.

## Peers are airc sessions

`bin/orch` talks to the airc WebSocket server at `ws://127.0.0.1:8787/ws`. Each peer
is a ccbridge hub named `NAME` whose PTY is owned by the ccbridge server and whose clean output is
the harness's own transcript. Prompts are pasted into the terminal by airc; replies are
read from the transcript, never scraped from the TUI. A hub is created on first connect
and survives client disconnects, so one peer keeps its conversation across cycles.

The client uses `mode=tagged` and `kinds=text,tool,you`: `t:` is agent text, `u:` is a
tool name, `y:` is the echoed prompt, and `e:` ends a turn. The empty frame means the
session died.

## Project discovery

Every immediate subdirectory of `ORCH_PARENT` (default: the parent of this project) is
a candidate, except this project and hidden directories. A directory counts as a
project when it is a git worktree or contains a project marker: `AGENTS.md`,
`CLAUDE.md`, `package.json`, `pyproject.toml`, `setup.py`, `Cargo.toml`, `go.mod`,
`CMakeLists.txt`, `Makefile`, `requirements.txt`, `Gemfile`, `composer.json`,
`pom.xml`, `build.gradle`, `meson.build`, `deno.json`, a solution/project file, or
`src/`. Peer names are sanitized directory names; a collision gets a numeric suffix.

On first creation Claude Code may show its folder-trust dialog. ccbridge has no raw
keypress channel, so the MCP server and `bin/orch` cannot select its default answer. They
detect the dialog in the pane (`trust_dialog_open`) and report it; a person confirms it in
the pane. A hub whose PTY died counts as absent: the server recreates it and trust is
checked again.

## Project resume

`resume_project` (MCP) and `bin/orch resume [NAME]` continue a project's saved
conversation in claude, codex or freebuff. With no project they use the last project
touched, persisted in `.orch/state.json`; that ledger also stores each project's cwd,
harness and touched time, so a project started with `--cwd` stays addressable by name
and resumes in the harness it last used. Pass `harness` / `--harness` to override.

Each harness answers `sessions(cwd)` and `resume_args` in its mod:

| harness | session source | resume launch |
|---|---|---|
| claude | `~/.claude/projects/<slug(cwd)>/<id>.jsonl` | `claude --resume <id>` |
| codex | `$CODEX_HOME/state_*.sqlite` table `threads` (`id`, `cwd`, `updated_at_ms`), plus legacy `sessions/**/rollout-*.jsonl` | `codex resume <id>` (or `codex resume --last`) |
| freebuff | `~/.config/manicode/projects/<basename(cwd)>/chats/<id>/chat-messages.json` | `freebuff --continue <id>` |

`list_project_sessions` / `bin/orch sessions [NAME] [--harness H] [--all]` return
session ids with timestamps; the newest is the default and `session_id` / `--session`
selects a specific one. Claude also accepts a session name or an absolute `.jsonl`
transcript path there; codex accepts a name; freebuff accepts only chat ids. A name or
path resume records the resolved transcript id in the ledger. A UUID that is not in the
project's store is looked up globally by the harness (claude across all project
transcript dirs, codex across the thread index), and the channel binds the transcript
that lookup returns. An unqualified resume prefers the ledger's remembered session id
over the newest local transcript, so a globally resumed session stays the project's
last session. Resume retires any live
hub first, restarts the airc server when it is down, then asks airc for a new hub
with `resume=<session-id>` (or `continue=1`), and airc launches the harness's resume
form. The resumed channel binds to that exact session and starts at the end of its
store, so the reply contains only the new turn. Freebuff's JSON store is seeded with
the old message ids for the same reason.

`start_projects`, `instruct` and `instruct_many` take `resume: true` (and `harness`)
to resume a project's latest session automatically when its peer is not running. The
CLI equivalents are `start --resume`, `prompt --resume` and `prompt --session ID`.

`fork: true` / `--fork` adds the harness's fork form so the resumed session keeps its
original id and the peer gets a new one; the source store is left untouched. Claude
uses `--fork-session` and codex uses `fork`; freebuff has no fork form. After a fork
the new session id is discovered and recorded in the ledger, so the next unqualified
resume continues the fork.

Codex and freebuff need their own CLI login before a peer can run; `codex login
status` and freebuff's login screen report it.

## CLI (debug path)

```
bin/open                          # hyperagent open, once `hyperagent mcp` is ported
bin/orch up
bin/orch projects [--json] [--all]
bin/orch start NAME [NAME...] [--concurrency 8]
bin/orch start NAME --cwd DIR --harness claude
bin/orch status --json --panes
bin/orch sessions [NAME] [--all] [--json]
bin/orch resume [NAME] [--session ID] [--prompt "instruction"]
bin/orch prompt NAME "instruction" --timeout 1800 --idle-exit 120
bin/orch prompt NAME "instruction" --resume
bin/orch fanout jobs.json --timeout 1800 --idle-exit 120
bin/orch pane NAME --lines 40
bin/orch outputs [NAME] [--all] [--json]
bin/orch output [NAME] [--agent A] [--session ID] [--tail 40] [--json]
bin/orch stop NAME
bin/orch down
bin/watch [--idle 20] [--poll 1] [--agents A,B] [--once] [--text]
```

`start` launches only the names given; a bare `start` is an error and starts nothing.
`prompt` and `fanout` may create a named peer implicitly, which is still an explicit
instruction. One prompt per peer at a time is enforced with an advisory lock in
`.orch/locks/`. A job that omits cwd uses the discovered project's cwd for that name,
so recreated hubs return to their project. Two peers should not share one cwd: the
Claude transcript channel binds to the newest session file in a project directory.

## Environment

| variable | default | meaning |
|---|---|---|
| `AIRC_DIR` | `../airc` | checkout whose `mods/` (harness names, transcript readers) are used |
| `CCBRIDGE_BIN` | unset | ccbridge binary to serve; unset runs `bin/ccbridge-latest` (C++: `hyperagent ccbridge install`), which installs the newest GitHub release |
| `ORCH_PARENT` | parent of this project | directory whose projects become peers |
| `ORCH_PORT` | `8787` | airc WebSocket server port |
| `ORCH_HOST` | `127.0.0.1` | airc WebSocket server host |
| `ORCH_HOME` | `.orch` | pid file, server log, session locks |
| `ORCH_JOURNAL` | `$ORCH_HOME/projects` | root of the per-session output journal |
| `ORCH_WATCH_IDLE` | `20` | fallback idle seconds when no result or busy marker is available |
| `ORCH_WATCH_POLL` | `1` | MCP monitor poll seconds |
| `ORCH_WATCH_AGENTS` | empty | comma separated peers the MCP monitor keeps |
| `ORCH_WATCH_LAST_CHARS` | `4000` | last-output characters in an event |
| `ORCH_PROGRESS_INTERVAL` | `5` | seconds between MCP progress notifications |
| `MCP_TOOL_TIMEOUT` | set by `bin/open` (or `hyperagent open`) to 1800000 | client limit for one tool call |
| `CCBRIDGE_HOME` | `.orch/ccbridge` | install root for `hyperagent ccbridge`; a scratch directory keeps a test install away from the running binary |
| `GITHUB_TOKEN` | unset | optional bearer token for the GitHub API calls of `hyperagent ccbridge`; never sent to download hosts |

## airc coupling

- Claude turn completion is the `system`/`turn_duration` transcript record.
- `GET /sessions` reports `alive`, `busy`, `cwd`, `harness`, `clients` and `pane_tail`.
- `DELETE /sessions/{name}` stops one hub and its PTY.
- `server_down` / `bin/orch down` ends every peer.

## Log GUI

`hyperagent gui [--port 8790]` (the C++ port that replaces `bin/gui`; `bin/gui` runs `gui/server.py` until it lands) serves a realtime view of `.orch/projects/**/*.jsonl` and `.orch/server.log` at http://127.0.0.1:8790/.

- `gui/server.py` is stdlib only. Each journal tail starts at its end, and a connect gets a snapshot of the last 1500 records, then `records` batches over SSE `/api/events`. Records are keyed `<source>@<byte offset>`, so the client drops duplicates after a reconnect.
- HMR: `/api/reload` sends `change` on any edit under `gui/public`. A stylesheet edit swaps in place; other edits reload the page. An edit to `gui/server.py` restarts the server after a compile check, and the page reloads when it is back. The C++ `hyperagent gui` does not restart itself; it keeps the `/api/reload` stream for `gui/public` edits only.
- Client: `gui/public/app.js` renders with webjsx `createElement`/`applyDiff` through htm tagged templates, so there is no build step. Both are vendored under `gui/public/vendor` (webjsx 0.0.73, htm 3.1.1, MIT). Renders run on animation frames, the buffer holds 8000 records, and the last 300 matches show with "Show older".
- Streamed `text` deltas are hidden by default. The URL hash carries the filters (`q`, `agent`, `kinds`).
- Keys: `/` search, `p` pause, `f` follow, `Esc` clear selection.
- Browser checks run through the gm playwriter setup.

## Source facts

- No comments in source: intent lives in names, durable facts live here.
- `mcp_server.py` exposes the MCP tools; `bin/orch` holds the implementation; `bin/open`
  starts Claude Code with a long MCP tool timeout (C++: `hyperagent open`).
- `bin/journal.py` owns the per-session output files; `bin/monitor.py` owns the pane
  animation detector shared by `bin/watch` and the MCP server.
- The dispatcher skill is `.claude/skills/orchestrate/SKILL.md`; open the project with
  `bin/open` and run `/orchestrate`.

## C++ port (`src/`)

- Build: `cmake -B build -G Ninja && cmake --build build`; target `hyperagent` (C++20; Boost.Beast/Asio headers, nlohmann-json, CLI11, sqlite3, OpenSSL). Boost.System is header-only here, so it is linked only when its target exists.
- Layout: `src/main.cpp` (CLI11 dispatch); `src/core/` (`text`, `json`, `files`, `paths`, `clock`, `process`, `http`); `src/journal/` (`Journal` writer, `records` readers, `command`); `src/monitor/` (`Monitor` state machine behind `SessionSource`, `command` replay); `src/watch/` (ccbridge `/sessions` source and loop); `src/cli/` (`ccbridge` install, `open`, `https` client); `src/gui/` (`command` options and startup, `server` Beast HTTP with SSE and static files, `feed` `JournalFeed` over the `tail` readers and a `hub` of subscribers, `reload` watcher for `gui/public`, `wire` Python-style JSON with ASCII escapes); `src/orch/` (the `orch` port: `config`, `state`, `projects`, `harness` (airc mods), `ccbridge` (HTTP and server lifecycle), `websocket`, `lock`, `turn` (run_job), `orch` (the function API for the MCP port), `command` (CLI11)); `src/pending/` (`mcp` stub, exit 3 until ported).
- The C++ GUI reads the same journal bytes as `gui/server.py`: `tail` replays the backward block scan of `read_last_lines`, the 500-per-source and 1500-total backfill, and the `<source>@<offset>` ids. Its server log is `orch_home()/server.log`, so `ORCH_HOME` moves it. That is kept on purpose: `bin/orch` writes the airc server log under `ORCH_HOME` (line 184, read back at line 209) and the C++ `ccbridge` does the same, so the viewer follows that path. `gui/server.py` always reads `REPO/.orch/server.log` and its journal root ignores `ORCH_HOME`; the two agree only when `ORCH_HOME` is unset. It has no server.py restart: `/api/reload` still sends `change` with `restart: false`, and a rebuild plus restart replaces the Python HMR. Responses are HTTP/1.0 with Python's header order (`Server`, `Date`, then the response fields); a 501 reason phrase is the error message, as in `HTTP/1.0 501 Unsupported method ('POST')`. The one header difference is that Python appends `Python/<version>` to `Server`, and the C++ server sends `orch-gui` only. Journal lines are parsed by `core::json::parse_python`, which accepts NaN, Infinity, -Infinity, float literals that overflow to Infinity, and lone `\uD800`-`\uDFFF` escapes as Python's `json.loads` does; a lone surrogate is kept as WTF-8 and written back as a `\udXXX` escape. The strict `core::json::parse` used by the other readers still rejects these. The Python journal writer uses `ensure_ascii=False`, so it cannot write a lone surrogate (UnicodeEncodeError), which is why the journal does not contain them today. `tests/fixtures/gui/` is the parity fixture: `journal/root` holds the NaN, Infinity and lone surrogate lines, and `expected-snapshot.json` is the `gui/server.py` backfill with `airc/server` records removed (drop `src == "airc/server"` from both sides, then compare `json.dumps(records, indent=1)`).
- Journal bytes: records are written compact with insertion order (`ordered_json`), Python float repr and unescaped UTF-8. `core::json::dumps(value, spaced)` gives Python's default `json.dumps` text.
- Text is counted in code points, as Python `len` and slicing do, and `splitlines` uses Python's separator set.
- A session without a `busy` field falls back to airc's busy markers (`esc to interrupt` for claude and codex, `Starting your model session` for freebuff) rather than loading the airc mods.
- `ORCH_JOURNAL`, `ORCH_HOME`, `ORCH_HOST`, `ORCH_PORT` mean the same as in `bin/orch`. The repo root is found by walking up from the executable to a directory holding `CMakeLists.txt` and `AGENTS.md`; `HYPERAGENT_ROOT` overrides it.
- `hyperagent orch` writes `.orch/state.json` as `json.dumps(indent=2, sort_keys=True)` and prints JSON with Python's default `ensure_ascii`, via `core::json::python_dumps`.
- `fanout` runs one thread per job; each turn owns one ccbridge WebSocket (`orch/websocket`, Beast async reads driven in `run_for` slices) and holds the per-name `.orch/locks/` flock.
- Transcript lines that are not JSON objects are skipped when reading a session's first timestamp. `bin/orch` raises on a line that parses to a list (a single-line `chat-messages.json`); the C++ port does not.
- Parity sandbox: a `ccbridge serve` on a spare port with `CCBRIDGE_TEMPLATES` pointing at templates copied from `airc/tests/fixtures/templates/fake.json` runs prompt, fanout, resume, stop and down with no real harness. Mutating commands never target the live peer server.
- Parity fixtures: `tests/fixtures/journal/root` (journal root) and `tests/fixtures/monitor/replay.json` (ticks for `hyperagent monitor --replay`, run with `ORCH_JOURNAL` pointing at the journal fixture).
- `bin/journal.py`, `bin/monitor.py` and `bin/watch` remain the reference. `bin/orch` stays the reference until the whole port passes; `hyperagent orch` serves its subcommands with the same argument surface, JSON shapes and `.orch/` state.
- `hyperagent ccbridge [install|latest|path]` ports `bin/ccbridge-latest` (bare `ccbridge` is `install`). It asks `api.github.com` for `releases/latest` of `AnEntrypoint/airc`, maps this OS and CPU to a release target (`linux-x64`, `linux-arm64`, `macos-arm64`, `macos-x64`, `windows-x64`, `windows-arm64`), and fetches `ccbridge-<target>.tar.gz` (`.zip` on Windows) from the release download URL. Downloads follow redirects over TLS (Beast with OpenSSL, peer and host verified with the system CA paths). `GITHUB_TOKEN` is optional and only sent to the API; a 403 or 429 with an exhausted limit reports the reset time and asks for a token. The archive is kept in `downloads/`, unpacked by `tar` (`tar -xzf`; `tar -xf` for zip) into `<tag>.partial`, and renamed to `<tag>` only when `ccbridge-<target>/bin/ccbridge` (`.exe` on Windows) is present. `path` prints the newest installed binary with no network access.
- `hyperagent open` sets `MCP_TIMEOUT` and `MCP_TOOL_TIMEOUT` only when unset and changes to the repo root. `hyperagent gui` replaces `bin/gui`. Callers of `bin/ccbridge-latest`, `bin/open` and `bin/gui` are listed in the port notes below; remove the scripts only after the whole port passes.
- Port notes: `bin/orch` (line 173) still calls `bin/ccbridge-latest`, `bin/open` is documented in `CLAUDE.md` and `.claude/skills/orchestrate/SKILL.md`, and `docs/index.html` describes `bin/open` and `bin/ccbridge-latest`. Update them when their owning port lands.
