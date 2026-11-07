# hyperagent

hyperagent is the airc-driven successor to `../orchestrate`: the dispatcher still
collects work, decides and assigns, but peers are coding-harness sessions started and
prompted directly through `../airc` instead of Claude sessions reached over the
messaging bus.

## Peers are airc sessions

`bin/orch` talks to the airc WebSocket server at `ws://127.0.0.1:8787/ws`. Each peer
is an airc hub named `NAME` whose tmux session is `ccb-NAME` and whose clean output is
the harness's own transcript. Prompts are pasted into the terminal by airc; replies are
read from the transcript, never scraped from the TUI. A hub is created on first connect
and survives client disconnects, so one peer keeps its conversation across cycles.

The client uses `mode=tagged` and `kinds=text,tool,you`: `t:` is agent text, `u:` is a
tool name, `y:` is the echoed prompt, and `e:` ends a turn. The empty frame means the
session died. `bin/orch` prints text only unless `--tools` is given.

## Commands

```
bin/orch up
bin/orch status --json --panes
bin/orch start --roster agents.json
bin/orch start NAME --cwd DIR --harness claude
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

## Environment

| variable | default | meaning |
|---|---|---|
| `AIRC_DIR` | `../airc` | checkout whose `server.py` and `mods/` are used |
| `ORCH_PORT` | `8787` | airc WebSocket server port |
| `ORCH_HOST` | `127.0.0.1` | airc WebSocket server host |
| `ORCH_HOME` | `.orch` | pid file, server log, session locks |

Relative cwds in `agents.json` and job files resolve against this project root.

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
- `agents.json` is machine-specific and ignored; `agents.example.json` is the template.
- Job files are written under `.orch/`, which is ignored.
- The dispatcher skill is `.claude/skills/orchestrate/SKILL.md`.
