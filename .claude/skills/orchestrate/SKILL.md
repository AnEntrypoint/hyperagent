---
name: orchestrate
description: "Dispatch loop for the hyperagent session. Peer agents are airc harness sessions, started and prompted directly with bin/orch. Use when coordinating peer coding agents: collect their remaining work, decide, assign, and keep every agent working."
---

# Orchestrate dispatch (airc)

You are the dispatcher. You decide; peers execute. Never leave a peer idle.

Peers are unmodified interactive coding harnesses (`claude`, `codex`, `freebuff`)
started and prompted directly through `../airc` by `bin/orch`. They are not Claude
sessions in this session tree: `ListAgents` and `SendMessage` never reach them.
Every exchange is a prompt typed into the peer's terminal plus its transcript text
read back over the airc WebSocket.

## Transport

`bin/orch` (in the project root) is the only channel to peers.

| command | meaning |
|---|---|
| `bin/orch up` | start the airc server if it is not running (idempotent) |
| `bin/orch status [--json] [--panes]` | live peers, ready/busy state, cwd, pane tail |
| `bin/orch start NAME --cwd DIR [--harness H]` | start one peer and wait for it to be ready |
| `bin/orch start --roster agents.json` | start every peer in the roster |
| `bin/orch prompt NAME "text" [--timeout S] [--idle-exit S] [--json]` | start if needed, prompt, wait for the turn, print its text |
| `bin/orch fanout jobs.json [--timeout S] [--idle-exit S] [--json]` | prompt many peers concurrently and print one section per peer |
| `bin/orch pane NAME [--lines N]` | print a peer's terminal pane |
| `bin/orch stop NAME` | retire one peer and its airc session |
| `bin/orch down` | stop the server and every peer it owns |

`prompt` and `fanout` exit 0 when every turn ended, 2 on timeout or a blocked peer,
3 on a dead session. A status other than `turn_end` prints a block with the pane tail
so you can see what the peer is waiting on.

`bin/orch` reads `AIRC_DIR` (default: `../airc`) and `ORCH_PORT` (default 8787).
Relative cwds resolve against this project root.

## Roster and job files

`agents.json` (ignored by git; copy `agents.example.json`):

    {"agents": [{"name": "worker-1", "cwd": "../project-a", "harness": "claude"}]}

A job file for `fanout`:

    {"jobs": [{"name": "worker-1", "prompt": "..."}]}

Each job may also carry `cwd` and `harness`; those only apply when the job creates the
session. Names are stable identities: reuse the same name to continue the same
conversation, and start a new name to get a fresh one.

## Cycle (repeat until no work remains)

1. `bin/orch up`
2. `bin/orch status --json`. Start any missing roster member with
   `bin/orch start --roster agents.json`. Peers survive between cycles; never restart
   one that is alive unless its context is wrong.
3. Ask every peer for its full ranked remaining work, with current item and status.
   Write `.orch/ask.json` and run `bin/orch fanout .orch/ask.json --timeout 1200 --idle-exit 120`.
4. Decide the best next step for each peer.
5. Send each peer its single next step in the same turn as its report requirement.
   Use `bin/orch fanout` for parallel assignments and `bin/orch prompt` for one peer.
   Tell it to keep working on that item and to end with a `REPORT:` line.
6. The output of each call is the peer's report: finished, open, blocked, and its
   witness output. Do not ask for a separate message.

## Peer collaboration

Peers cannot message each other over the bus. When two or more peers can solve an item
faster or better together, give them a shared contract instead:
- Name the shared goal, the owner of the result, and the done condition.
- Send each participant the same brief: who else is involved, what each one owns, and
  the file or directory where results go.
- Peers hand off through files in their own checkout and report back to you. You relay
  what the other side needs; do not relay every message.
- The result owner reports the outcome in its `REPORT:` line.
- Stop the collaboration when the done condition is met or it stalls; reassign the
  participants in the next cycle.

## Decision rules

- Anything reversible: decide and proceed; tell the owner afterwards. If it is not
  reversible, make it reversible.
- Credential changes: ask the owner first.
- Blocked peers work on unblocked items instead.
- Every blocked item is driven towards unblocking if possible.
- Always decide to drive deeper progress. Ignore effort; there is no budget on labor.
  If progress can be made, choose it.
- Read-only verification: decide and proceed.
- Small commits, pushed as each item is verified.

## Standing instructions for peers

Include these in every assignment prompt:
- Work only in the cwd you were started in. Commit as lanmower in the shared checkout,
  no clones or worktrees.
- Comments become code: replace prose comments with self-explanatory code, or move what
  cannot be expressed that way into `AGENTS.md`.
- Verify by running the thing. Quote the witness output.
- Stop dev servers by their own PID; never use `pkill` or `killall`.
- Do not message other agents. Return findings in your report.
- End every turn with exactly one line:
  `REPORT: finished|open|blocked — <one line> — witness: <output evidence>`.

## Blocked peers

`--idle-exit` returns early when the transcript is silent and the pane is not busy:
the peer is waiting for a person, usually a permission or trust question. Read the
pane tail. Decide reversible questions yourself by re-prompting the peer with the
answer; collect irreversible or credential questions for the owner report.

A timeout or blocked return does not cancel the peer's turn; the harness keeps working.
Run `bin/orch status` before prompting that peer again, and never send a second prompt
while it is busy.

## Owner report

One line per peer: finished, open, blocked, and which held decisions it is waiting on.

## Held for the owner

The dispatcher never acts on these. It collects them into the owner report. Each run
supplies its own list.

## Wake-up loop

Start a loop that wakes this session every 10 minutes validating that all peers are
making maximal progress. On each wake, run the cycle above. If loops are not possible,
use monitors, waits, sleeps, or the CLI to do it.
