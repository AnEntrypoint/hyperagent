# hyperagent

hyperagent orchestrates the projects beside it: peer coding-harness sessions are started
and prompted directly through airc instead of Claude sessions reached over the messaging
bus. The orchestrator talks to them through the `hyperagent` MCP server.

airc is included in this repository as `airc/` (its history was imported with git subtree).
The standalone AnEntrypoint/airc repository is no longer the source: ccbridge development
happens in `airc/` here, and the root CMake build compiles it into `build/ccbridge`.

## MCP server

`.mcp.json` registers the `hyperagent` stdio server (`build/hyperagent mcp`, built with `cmake -B build -G Ninja && cmake --build build`), and
`.claude/settings.json` lists it in `enabledMcpjsonServers` so opening the project
enables it without a prompt. `claude mcp list` reports it as connected.

`hyperagent mcp` (`src/mcp/`) serves the MCP tools, tool schemas, INSTRUCTIONS, resources,
events and protocol features (`initialize`, `server/discover`, `tools/list`, `tools/call`,
`logging/setLevel`, `resources/subscribe`, `subscriptions/listen`, `notifications/progress`).
`.mcp.json` points at the binary. The Python reference (`mcp_server.py`) was removed on the
conversion commit; before removal its tool list, the ten read-only tools and `peer_status`
(with the opt-in `pane_lines` tail) were witnessed identical.

Known deviations of `hyperagent mcp`:

- Peer and journal tools call the orch internals (`orch.hpp`, `turn.hpp`, `projects.hpp`,
  `ccbridge.hpp`) directly rather than the CLI.
- Tools outside the synchronous set (`list_projects`, `list_project_sessions`,
  `list_outputs`, `read_output`, `search_output`) run on worker threads. Responses to
  concurrent requests arrive in completion order; the ids correlate them, and sorted
  outputs are identical.
- At EOF in-flight tools get a 2 s grace before they are cancelled.
- The monitor runs on one thread per server and emits from there.
- Resource subscriptions keep insertion order.
- `search_output` lowercases Latin-1, Greek and Cyrillic only (simple Unicode lowercase).
- `server_down` checks the server with `server_alive(1)`, one second.
- The default `cmake -B build` has no build type and compiles at -O0; the journal tools run
  about 14x slower there than in a Release build (`list_outputs` 1.35 s at -O0, 0.10 s in
  Release; `search_output` 2.29 s and 0.12 s). Use `-DCMAKE_BUILD_TYPE=Release` for MCP work.

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
`build/hyperagent orch outputs` and `build/hyperagent orch output`.

`run_job` returns `output_path` and `output_session`, and `render_result` prints the
path; `peer_status` annotates live peers with their latest journal.

## Watcher and MCP events

`build/hyperagent watch` starts a local monitor. It polls `GET /sessions`, reads each peer's
`pane_tail`, and hashes the pane text. An event fires as the turn finishes, not after
a quiet window: a new `result` record in the session journal is the primary trigger,
and a pane that goes from busy to not busy is the second. Both report `idle_s: 0`
with `trigger: "result"` or `"pane"`. The `--idle` seconds (default 20) are only the
fallback for a peer with no journal and no busy marker (`trigger: "idle"`). One line
is emitted per finish, carrying the whole last response (or the pane tail when no
journal exists) and a ready `instruct` call whose prompt feeds that response back.
It emits `gone` when a live peer disappears. `--once` reports peers that airc marks
not busy. Output is one-line JSON, or `--text`.

The MCP server runs the monitor loop (`src/monitor/`) natively when a client asks for
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

`build/hyperagent open` starts Claude Code in this project with `MCP_TOOL_TIMEOUT` set to four
hours, so a turn can be collected in one call. Without it the client's shorter tool
timeout can cut a long turn; use `wait: false` plus `turn_result` then. `hyperagent open`
(`src/cli/open.cpp`) sets `MCP_TIMEOUT` (60000) and `MCP_TOOL_TIMEOUT` (14400000) only when
unset, changes to the repo root, and execs `claude`.

`hyperagent mcp` is a single binary with no runtime dependency. Stdout is the JSON-RPC
channel: tool handlers must never print to stdout, and only the launcher and diagnostics
write to stderr.

## Peers are airc sessions

`build/hyperagent orch` talks to the airc WebSocket server at `ws://127.0.0.1:8787/ws`. Each peer
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
keypress channel, so the MCP server and `build/hyperagent orch` cannot select its default answer. They
detect the dialog in the pane (`trust_dialog_open`) and report it; a person confirms it in
the pane. A hub whose PTY died counts as absent: the server recreates it and trust is
checked again.

## Project resume

`resume_project` (MCP) and `build/hyperagent orch resume [NAME]` continue a project's saved
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

`list_project_sessions` / `build/hyperagent orch sessions [NAME] [--harness H] [--all]` return
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
build/hyperagent open                          # claude in the repo root with the MCP timeouts
build/hyperagent orch up
build/hyperagent orch projects [--json] [--all]
build/hyperagent orch start NAME [NAME...] [--concurrency 8]
build/hyperagent orch start NAME --cwd DIR --harness claude
build/hyperagent orch status --json --panes
build/hyperagent orch sessions [NAME] [--all] [--json]
build/hyperagent orch resume [NAME] [--session ID] [--prompt "instruction"]
build/hyperagent orch prompt NAME "instruction" --timeout 14400 --idle-exit 120
build/hyperagent orch prompt NAME "instruction" --resume
build/hyperagent orch fanout jobs.json --timeout 14400 --idle-exit 120
build/hyperagent orch pane NAME --lines 40
build/hyperagent orch outputs [NAME] [--all] [--json]
build/hyperagent orch output [NAME] [--agent A] [--session ID] [--tail 40] [--json]
build/hyperagent orch stop NAME
build/hyperagent orch down
build/hyperagent watch [--idle 20] [--poll 1] [--agents A,B] [--once] [--text]
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
| `CCBRIDGE_BIN` | unset | ccbridge binary to serve; unset runs the compiled-in `build/ccbridge` |
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
| `MCP_TOOL_TIMEOUT` | set by `hyperagent open` to 14400000 | client limit for one tool call |

## airc coupling

- Claude turn completion is the `system`/`turn_duration` transcript record.
- `GET /sessions` reports `alive`, `busy`, `cwd`, `harness`, `clients` and `pane_tail`.
- `DELETE /sessions/{name}` stops one hub and its PTY.
- `server_down` / `build/hyperagent orch down` ends every peer.

## Log GUI

`hyperagent gui [--port 8790]` (`build/hyperagent gui` runs it) serves a realtime view of `.orch/projects/**/*.jsonl` and `.orch/server.log` at http://127.0.0.1:8790/.

- The server is C++ (`src/gui/`). Each journal tail starts at its end, and a connect gets a snapshot of the last 1500 records, then `records` batches over SSE `/api/events`. Records are keyed `<source>@<byte offset>`, so the client drops duplicates after a reconnect.
- HMR: `/api/reload` sends `change` on any edit under `gui/public`. A stylesheet edit swaps in place; other edits reload the page. The server does not restart itself: a rebuild and restart picks up a source change.
- Client: `gui/public/app.js` renders with webjsx `createElement`/`applyDiff` through htm tagged templates, so there is no build step. Both are vendored under `gui/public/vendor` (webjsx 0.0.73, htm 3.1.1, MIT). Renders run on animation frames, the buffer holds 8000 records, and the last 300 matches show with "Show older".
- Streamed `text` deltas are hidden by default. The URL hash carries the filters (`q`, `agent`, `kinds`).
- Keys: `/` search, `p` pause, `f` follow, `Esc` clear selection.
- Browser checks run through the gm playwriter setup.

## Source facts

- No comments in source: intent lives in names, durable facts live here.
- `hyperagent mcp` exposes the MCP tools; `hyperagent orch` holds the implementation;
  `hyperagent open` starts Claude Code with a long MCP tool timeout.
- `src/journal/` owns the per-session output files; `src/monitor/` owns the pane
  animation detector shared by `build/hyperagent watch` and the MCP server.
- The dispatcher skill is `.claude/skills/orchestrate/SKILL.md`; open the project with
  `build/hyperagent open` and run `/orchestrate`.

## C++ port (`src/`)

- Build: `cmake -B build -G Ninja && cmake --build build`; targets `hyperagent` and `ccbridge` (from `airc/`; C++20; Boost.Beast/Asio headers, nlohmann-json, CLI11, sqlite3, OpenSSL). Boost.System is header-only here, so it is linked only when its target exists.
- Layout: `src/main.cpp` (CLI11 dispatch); `src/core/` (`text`, `json`, `files`, `paths`, `clock`, `process`, `http`); `src/journal/` (`Journal` writer, `records` readers, `command`); `src/monitor/` (`Monitor` state machine behind `SessionSource`, `command` replay); `src/watch/` (ccbridge `/sessions` source and loop); `src/cli/` (`ccbridge` path, `open`); `src/gui/` (`command` options and startup, `server` Beast HTTP with SSE and static files, `feed` `JournalFeed` over the `tail` readers and a `hub` of subscribers, `reload` watcher for `gui/public`, `wire` Python-style JSON with ASCII escapes); `src/orch/` (the `orch` port: `config`, `state`, `projects`, `harness` (airc mods), `ccbridge` (HTTP and server lifecycle), `websocket`, `lock`, `turn` (run_job), `orch` (the function API for the MCP port), `command` (CLI11)); `src/mcp/` (`hyperagent mcp`: `server` JSON-RPC loop, `tools`, `peers`, `outputs` resources, `turns`, `events`, `args` Python-compatible JSON and argument coercion, `schema` (the tool and resource definitions), `command`).
- The GUI reads the journal bytes the way the removed Python server did: `tail` replays the backward block scan of `read_last_lines`, the 500-per-source and 1500-total backfill, and the `<source>@<offset>` ids. Its server log is `orch_home()/server.log`, so `ORCH_HOME` moves it. That is kept on purpose: the airc server log lives under `ORCH_HOME`, and `build/hyperagent orch` and `hyperagent ccbridge` read it from there, so the viewer follows that path. `/api/reload` sends `change` with `restart: false`; a rebuild plus restart replaces the old Python HMR restart. Responses are HTTP/1.0 with Python's header order (`Server`, `Date`, then the response fields); a 501 reason phrase is the error message, as in `HTTP/1.0 501 Unsupported method ('POST')`. The `Server` header is `orch-gui`. Journal lines are parsed by `core::json::parse_python`, which accepts NaN, Infinity, -Infinity, float literals that overflow to Infinity, and lone `\uD800`-`\uDFFF` escapes as Python's `json.loads` does; a lone surrogate is kept as WTF-8 and written back as a `\udXXX` escape. The strict `core::json::parse` used by the other readers still rejects these. The journal writer keeps UTF-8 unescaped, so it cannot write a lone surrogate, which is why the journal does not contain them today. `tests/fixtures/gui/` is the parity fixture: `journal/root` holds the NaN, Infinity and lone surrogate lines, and `expected-snapshot.json` is the backfill snapshot with `airc/server` records removed (drop `src == "airc/server"` from both sides, then compare `json.dumps(records, indent=1)`).
- Journal bytes: records are written compact with insertion order (`ordered_json`), Python float repr and unescaped UTF-8. `core::json::dumps(value, spaced)` gives Python's default `json.dumps` text.
- Text is counted in code points, as Python `len` and slicing do, and `splitlines` uses Python's separator set.
- A session without a `busy` field falls back to airc's busy markers (`esc to interrupt` for claude and codex, `Starting your model session` for freebuff) rather than loading the airc mods.
- `ORCH_JOURNAL`, `ORCH_HOME`, `ORCH_HOST`, `ORCH_PORT` mean the same as in `build/hyperagent orch`. The repo root is found by walking up from the executable to a directory holding `CMakeLists.txt` and `AGENTS.md`; `HYPERAGENT_ROOT` overrides it.
- `hyperagent orch` writes `.orch/state.json` as `json.dumps(indent=2, sort_keys=True)` and prints JSON with Python's default `ensure_ascii`, via `core::json::python_dumps`.
- `fanout` runs one thread per job; each turn owns one ccbridge WebSocket (`orch/websocket`, Beast async reads driven in `run_for` slices) and holds the per-name `.orch/locks/` flock.
- Transcript lines that are not JSON objects are skipped when reading a session's first timestamp, in `build/hyperagent orch` and in the C++ port alike; a single-line `chat-messages.json` (a JSON array) therefore yields no stamp. Freebuff's real chat files are single-line arrays, so the line-based stamp never binds them.
- `hyperagent orch <sub> --help` prints argparse's layout (usage wrapping at the terminal width, help column at 24, `-h, --help` first). The per-subcommand spec lives in `src/orch/help.cpp` and reproduces the argparse layout of the removed Python `build/hyperagent orch`; CLI11 still does the parsing.
- Parity sandbox: a `ccbridge serve` on a spare port with `CCBRIDGE_TEMPLATES` pointing at templates copied from `airc/tests/fixtures/templates/fake.json` runs prompt, fanout, resume, stop and down with no real harness. Mutating commands never target the live peer server.
- Parity fixtures: `tests/fixtures/journal/root` (journal root) and `tests/fixtures/monitor/replay.json` (ticks for `hyperagent monitor --replay`, run with `ORCH_JOURNAL` pointing at the journal fixture).
- `tests/fake_claude/` builds `build/fake_claude/bin/claude` (CMake target `fake_claude`, added by the root CMakeLists). It is the claude harness that the stock `claude.json` template drives: a PTY screen with `❯` and `Claude Code` ready markers and `esc to interrupt` while a turn runs; input arrives as bracketed paste (`ESC[200~` ... `ESC[201~`) and is submitted by Enter. Each turn appends to `$HOME/.claude/projects/<slug(cwd)>/<session>.jsonl`: a user record with string content, assistant `text` and `tool_use` blocks with `apiBlockIndex`, a `tool_result` user record, and a `system` `turn_duration` record that ends the turn. A prompt containing `TOOL` runs a Bash tool_use. `--resume <id>` (missing id exits 1 with `No conversation found`), `--continue` and `--fork-session` are supported; `FAKE_CLAUDE_TURN_MS` sets the turn length (default 1500).
- MCP success-path sandbox (no `harness fake`, which orch rejects): a `mktemp -d` root with `HOME`, `ORCH_HOME`, `CCBRIDGE_TEMPLATES` (a copy of `airc/templates`), `ORCH_PARENT` holding throwaway project dirs with an `AGENTS.md` marker, `CCBRIDGE_BIN` set to the installed binary, `ORCH_PORT` on a spare port, and `PATH` with `build/fake_claude/bin` first. Drive `hyperagent mcp` through a FIFO pair. `stop_peer` ends peers, but closing the MCP stdin leaves the ccbridge running: stop it by the PID in `$ORCH_HOME/server.pid` and its children, never by name.
- `stop_peer` during an outstanding background turn: airc keeps the turn's WebSocket open after `DELETE /sessions/{name}`, so the turn cannot see the stop through its socket. Each idle poll (`poll_interval`, 5 s) checks the hub instead (`hub_stopped` in `src/orch/turn.cpp`, same name in `build/hyperagent orch`); a missing or dead hub ends the turn with status `stopped` and releases `.orch/locks/<name>.lock`, so a queued `resume_project` prompt runs. A fetch failure keeps the turn waiting. airc's claude reader stitches text blocks inside one `poll()` only, so a message whose text blocks land in different polls shows the tail of the second block alone; the fake writes one text block per final message for that reason.
- `hyperagent orch`, `hyperagent watch` and `hyperagent gui` keep the same argument surface, JSON shapes and `.orch/` state as the removed Python tools.
- `hyperagent ccbridge` prints the binary hyperagent runs: `CCBRIDGE_BIN` when set, else the compiled-in `build/ccbridge`. There is no release download. At spawn, `CCBRIDGE_TEMPLATES` is set to `airc/templates` unless the caller already set it.
- `hyperagent open` sets `MCP_TIMEOUT` and `MCP_TOOL_TIMEOUT` only when unset and changes to the repo root.
