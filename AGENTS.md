# hyperagent

hyperagent orchestrates the projects beside it: peer coding-harness sessions are started
and prompted directly through `../airc` instead of Claude sessions reached over the
messaging bus. The orchestrator talks to them through the `hyperagent` MCP server.

## MCP server

`.mcp.json` registers the `hyperagent` stdio server (`python3 mcp_server.py`), and
`.claude/settings.json` lists it in `enabledMcpjsonServers` so opening the project
enables it without a prompt. `claude mcp list` reports it as connected.

Tools: `list_projects`, `start_projects`, `instruct`, `instruct_many`,
`turn_result`, `resume_project`, `list_project_sessions`, `peer_status`,
`read_pane`, `stop_peer`, `server_down`.
`list_projects` launches nothing. `start_projects` starts only the names given and
runs Claude Code in each project through airc. `instruct` and `instruct_many` return
the peer's turn; `wait: false` returns a turn id for `turn_result`.
`resume_project` continues a project's saved Claude conversation; with no project it
resumes the last project used. `list_project_sessions` lists resumable sessions.

`bin/open` starts Claude Code in this project with `MCP_TOOL_TIMEOUT` set to 30
minutes, so a turn can be collected in one call. Without it the client's shorter tool
timeout can cut a long turn; use `wait: false` plus `turn_result` then.

`mcp_server.py` has no dependencies beyond aiohttp and imports `bin/orch` as its
implementation. Stdout is the JSON-RPC channel: tool handlers must never print to
stdout, and only the launcher and diagnostics write to stderr.

## Peers are airc sessions

`bin/orch` talks to the airc WebSocket server at `ws://127.0.0.1:8787/ws`. Each peer
is an airc hub named `NAME` whose tmux session is `ccb-NAME` and whose clean output is
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

On first creation Claude Code may show its folder-trust dialog, whose default choice
exits. The MCP server and `bin/orch` answer it by sending `Down` then `Enter`
(selecting "Yes, I trust this folder") and waiting for the dialog to clear.
`trust: false` / `--no-trust` disables that and leaves the decision to a person. A
listed hub whose tmux session died counts as absent: the server recreates it and trust
is checked again.

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
path resume records the resolved transcript id in the ledger. Resume retires any live
hub and leftover `ccb-NAME` tmux
session first, restarts the airc server when it is down, then asks airc for a new hub
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
bin/orch stop NAME
bin/orch down
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
| `AIRC_DIR` | `../airc` | checkout whose `server.py` and `mods/` are used |
| `ORCH_PARENT` | parent of this project | directory whose projects become peers |
| `ORCH_PORT` | `8787` | airc WebSocket server port |
| `ORCH_HOST` | `127.0.0.1` | airc WebSocket server host |
| `ORCH_HOME` | `.orch` | pid file, server log, session locks |
| `MCP_TOOL_TIMEOUT` | set by `bin/open` to 1800000 | client limit for one tool call |

## airc coupling

- Claude turn completion is the `system`/`turn_duration` transcript record.
- `GET /sessions` reports `alive`, `busy`, `cwd`, `harness`, `clients` and `pane_tail`.
- `DELETE /sessions/{name}` stops one hub and its tmux session.
- `server_down` / `bin/orch down` ends every peer.

## Source facts

- No comments in source: intent lives in names, durable facts live here.
- `mcp_server.py` exposes the MCP tools; `bin/orch` holds the implementation; `bin/open`
  starts Claude Code with a long MCP tool timeout.
- The dispatcher skill is `.claude/skills/orchestrate/SKILL.md`; open the project with
  `bin/open` and run `/orchestrate`.
