# hyperagent

hyperagent is the airc-driven successor to `../orchestrate`: the dispatcher still
collects work, decides and assigns, but peers are coding-harness sessions started and
prompted directly through `../airc` instead of Claude sessions reached over the
messaging bus. Peers are discovered from the projects beside this one.

## Peers are airc sessions

`bin/orch` talks to the airc WebSocket server at `ws://127.0.0.1:8787/ws`. Each peer
is an airc hub named `NAME` whose tmux session is `ccb-NAME` and whose clean output is
the harness's own transcript. Prompts are pasted into the terminal by airc; replies are
read from the transcript, never scraped from the TUI. A hub is created on first connect
and survives client disconnects, so one peer keeps its conversation across cycles.

The client uses `mode=tagged` and `kinds=text,tool,you`: `t:` is agent text, `u:` is a
tool name, `y:` is the echoed prompt, and `e:` ends a turn. The empty frame means the
session died. `bin/orch` prints text only unless `--tools` is given.

## Project discovery

Every immediate subdirectory of `ORCH_PARENT` (default: the parent of this project) is
a candidate, except this project and hidden directories. A directory counts as a
project when it is a git worktree or contains a project marker: `AGENTS.md`,
`CLAUDE.md`, `package.json`, `pyproject.toml`, `setup.py`, `Cargo.toml`, `go.mod`,
`CMakeLists.txt`, `Makefile`, `requirements.txt`, `Gemfile`, `composer.json`,
`pom.xml`, `build.gradle`, `meson.build`, `deno.json`, a solution/project file, or
`src/`. `--all` includes unmarked directories. Peer names are sanitized directory
names; a collision gets a numeric suffix.

On first creation Claude Code may show its folder-trust dialog, whose default choice
exits. `bin/orch start`, `prompt` and `fanout` answer it by sending `Down` then
`Enter` (selecting "Yes, I trust this folder") and waiting for the dialog to clear.
`--no-trust` disables that and leaves the decision to a person. A listed hub whose
tmux session died counts as absent: the server recreates it and trust is checked again.

## Commands

```
bin/orch up
bin/orch projects [--json] [--all]
bin/orch start NAME [NAME...] [--concurrency 8]
bin/orch start NAME --cwd DIR --harness claude
bin/orch status --json --panes
bin/orch prompt NAME "instruction" --timeout 1800 --idle-exit 120
bin/orch fanout jobs.json --timeout 1800 --idle-exit 120
bin/orch pane NAME --lines 40
bin/orch stop NAME
bin/orch down
```

`prompt` and `fanout` return 0 when every turn ended, 2 on timeout or a blocked peer,
3 on a dead session. A turn that does not end prints the pane tail.

One prompt per peer at a time is enforced with an advisory lock in `.orch/locks/`.
Concurrent prompts to different peers are expected; `fanout` runs them in parallel.
A timeout or blocked return closes the client but leaves the harness turn running;
check `bin/orch status` for `busy` before prompting that peer again.

`start` launches only the names given; a bare `start` is an error and starts nothing.
`prompt` and `fanout` may create a named peer implicitly, which is still an explicit
instruction. Discovery only lists candidates; it never launches them.

## Environment

| variable | default | meaning |
|---|---|---|
| `AIRC_DIR` | `../airc` | checkout whose `server.py` and `mods/` are used |
| `ORCH_PARENT` | parent of this project | directory whose projects become peers |
| `ORCH_PORT` | `8787` | airc WebSocket server port |
| `ORCH_HOST` | `127.0.0.1` | airc WebSocket server host |
| `ORCH_HOME` | `.orch` | pid file, server log, session locks |

Relative cwds in job files resolve against this project root. A job that omits cwd
uses the discovered project's cwd for that name, so recreated hubs return to their
project instead of the dispatcher's folder. Two peers should not share one cwd: the
Claude transcript channel binds to the newest session file in a project directory.

## airc coupling

- Claude turn completion is the `system`/`turn_duration` transcript record. Assistant
  records are streamed chunks that each carry `stop_reason: end_turn`, so they cannot
  mark a turn end.
- `GET /sessions` reports `alive`, `busy`, `cwd`, `harness`, `clients` and `pane_tail`.
- `DELETE /sessions/{name}` stops one hub and its tmux session. Connecting to a name
  whose tmux session died creates a fresh session under that name.
- `bin/orch down` ends every peer. Use `stop` to retire a single peer.

## Source facts

- No comments in source: intent lives in names, durable facts live here.
- Job files are written under `.orch/`, which is ignored.
- The dispatcher skill is `.claude/skills/orchestrate/SKILL.md`.
