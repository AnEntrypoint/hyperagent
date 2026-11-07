---
name: orchestrate
description: "Dispatch loop for the hyperagent session. Peer agents are Claude Code (or codex/freebuff) sessions started and prompted directly through airc by the hyperagent MCP tools. Peers are the projects beside this one. Use when coordinating project agents: collect their remaining work, decide, assign, and keep every agent working."
---

# Orchestrate dispatch (airc MCP)

You are the dispatcher. You decide; peers execute. Never leave a peer idle.

Peers are unmodified interactive coding harnesses started and prompted directly
through `../airc` by the `hyperagent` MCP server. They are not Claude sessions in this
session tree: `ListAgents` and `SendMessage` never reach them. Every exchange is a
prompt typed into the peer's terminal plus its transcript text read back over airc.

Peers come from the projects around this one and start only when you name them.

## MCP tools

The `hyperagent` MCP server is the channel to peers.

| tool | meaning |
|---|---|
| `mcp__hyperagent__list_projects` | list every project in the parent path except this one; launches nothing |
| `mcp__hyperagent__start_projects` | start the named projects' Claude Code peers only (`names`) |
| `mcp__hyperagent__instruct` | prompt one peer and wait for its turn; starts it if needed |
| `mcp__hyperagent__instruct_many` | prompt many peers concurrently (`jobs`) and collect every turn |
| `mcp__hyperagent__turn_result` | collect a background turn started with `wait: false` |
| `mcp__hyperagent__resume_project` | continue a project's saved conversation in claude, codex or freebuff; no project means the last project |
| `mcp__hyperagent__list_project_sessions` | list resumable sessions for a project or all projects, per harness |
| `mcp__hyperagent__peer_status` | live peers: alive, busy, harness, cwd, pane tail |
| `mcp__hyperagent__read_pane` | a peer's terminal pane |
| `mcp__hyperagent__stop_peer` | retire one peer |
| `mcp__hyperagent__server_down` | stop the airc server and every peer it owns |

A turn that ends returns the peer's text with status `turn_end`. Any other status
(`timeout`, `blocked`, `died`) returns the pane tail so you can see what it waits on.

Long turns: open the project with `bin/open`, which sets a 30-minute
`MCP_TOOL_TIMEOUT`. If a turn still outlives the client, call `instruct` with
`wait: false` and collect it later with `turn_result`.

`bin/orch` is the same layer on the CLI for debugging; the MCP tools are the
orchestrator's channel.

## Projects

Every immediate subdirectory of the parent path is a candidate, except this folder and
hidden directories. A directory counts as a project when it is a git worktree or
carries a project marker: `AGENTS.md`, `CLAUDE.md`, `package.json`,
`pyproject.toml`, `setup.py`, `Cargo.toml`, `go.mod`, `CMakeLists.txt`, `Makefile`,
`requirements.txt`, `Gemfile`, `composer.json`, `pom.xml`, `build.gradle`,
`meson.build`, `deno.json`, a solution/project file, or `src/`. Pass `all: true` to
`list_projects` to include unmarked directories.

Each peer's name is its project directory name. `start_projects` starts only the
names you give; nothing starts implicitly. Peers stay alive between cycles, so start a
name once.

On first creation the harness may show Claude Code's folder-trust dialog. The MCP
server selects "Yes, I trust this folder" and waits for it to clear. Pass
`trust: false` on `start_projects`, `instruct` or `instruct_many` to leave that
decision to a person.

## Cycle (repeat until no work remains)

1. `list_projects`.
2. `start_projects` for the peers this instruction calls for. Never start a peer with
   no work for it. Never restart one that is alive unless its context is wrong.
3. Ask every started peer for its full ranked remaining work, with current item and
   status, through `instruct_many`.
4. Decide the best next step for each peer.
5. Send each peer its single next step in the same turn as its report requirement.
   Use `instruct_many` for parallel assignments and `instruct` for one peer. Tell it
   to keep working on that item and to end with a `REPORT:` line.
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

## Resume

A peer that is not running can continue its saved conversation instead of starting
fresh. Call `resume_project` with no arguments to resume the last project touched, or
with `project` for a specific one; add `prompt` to send work in the same call. Pass
`session_id` to pick an older conversation and `harness` for claude, codex or freebuff;
`list_project_sessions` shows the ids per harness. `resume_project` restarts a dead
airc server and retires any stale hub or tmux session first. `start_projects`,
`instruct` and `instruct_many` accept `resume: true` and `harness` to resume a dead
peer's latest session automatically. Add `fork: true` to resume without reusing the
source session id; the ledger records the new one.

A resumed peer answers from its saved context, so it is the right choice whenever a
peer's next step depends on what it already did. Only start a fresh session when the
saved context is wrong for the work.

## Blocked peers

`idle_exit_s` returns early when the transcript is silent and the pane is not busy:
the peer is waiting for a person, usually a permission question. Call `read_pane` and
decide. Answer reversible questions yourself by re-prompting the peer; collect
irreversible or credential questions for the owner report.

A timeout or blocked return does not cancel the peer's turn; the harness keeps working.
Call `peer_status` before prompting that peer again, and never send a second prompt
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
