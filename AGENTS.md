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
timeout can cut a long turn; use `wait: false` plus `turn_result` then.

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
bin/open
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
| `CCBRIDGE_BIN` | unset | ccbridge binary to serve; unset runs `bin/ccbridge-latest`, which installs the newest GitHub release |
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
| `MCP_TOOL_TIMEOUT` | set by `bin/open` to 1800000 | client limit for one tool call |

## airc coupling

- Claude turn completion is the `system`/`turn_duration` transcript record.
- `GET /sessions` reports `alive`, `busy`, `cwd`, `harness`, `clients` and `pane_tail`.
- `DELETE /sessions/{name}` stops one hub and its PTY.
- `server_down` / `bin/orch down` ends every peer.

## Log GUI

`bin/gui [--port 8790]` serves a realtime view of `.orch/projects/**/*.jsonl` and `.orch/server.log` at http://127.0.0.1:8790/.

- `gui/server.py` is stdlib only. Each journal tail starts at its end, and a connect gets a snapshot of the last 1500 records, then `records` batches over SSE `/api/events`. Records are keyed `<source>@<byte offset>`, so the client drops duplicates after a reconnect.
- HMR: `/api/reload` sends `change` on any edit under `gui/public`. A stylesheet edit swaps in place; other edits reload the page. An edit to `gui/server.py` restarts the server after a compile check, and the page reloads when it is back.
- Client: `gui/public/app.js` renders with webjsx `createElement`/`applyDiff` through htm tagged templates, so there is no build step. Both are vendored under `gui/public/vendor` (webjsx 0.0.73, htm 3.1.1, MIT). Renders run on animation frames, the buffer holds 8000 records, and the last 300 matches show with "Show older".
- Streamed `text` deltas are hidden by default. The URL hash carries the filters (`q`, `agent`, `kinds`).
- Keys: `/` search, `p` pause, `f` follow, `Esc` clear selection.
- Browser checks run through the gm playwriter setup.

## Source facts

- No comments in source: intent lives in names, durable facts live here.
- `mcp_server.py` exposes the MCP tools; `bin/orch` holds the implementation; `bin/open`
  starts Claude Code with a long MCP tool timeout.
- `bin/journal.py` owns the per-session output files; `bin/monitor.py` owns the pane
  animation detector shared by `bin/watch` and the MCP server.
- The dispatcher skill is `.claude/skills/orchestrate/SKILL.md`; open the project with
  `bin/open` and run `/orchestrate`.
