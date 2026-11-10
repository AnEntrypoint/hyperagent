---
name: orchestrate
description: "Dispatch loop for the hyperagent session. Peer agents are sessions started and prompted directly through tools. Peers are the projects beside this one. Use when coordinating project agents: collect their remaining work, decide, assign, and keep every agent working."
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
| `mcp__hyperagent__list_outputs` | list journaled turn output per session with last-output preview |
| `mcp__hyperagent__read_output` | read one session journal as a compact transcript |
| `mcp__hyperagent__search_output` | search every session journal |
| `mcp__hyperagent__peer_status` | live peers: alive, busy, harness, cwd, pane tail |
| `mcp__hyperagent__read_pane` | a peer's terminal pane |
| `mcp__hyperagent__stop_peer` | retire one peer |
| `mcp__hyperagent__server_down` | stop the airc server and every peer it owns |

A turn that ends returns the peer's text with status `turn_end`. Any other status
(`timeout`, `blocked`, `died`) returns the pane tail so you can see what it waits on.
A `timeout` status means the four-hour turn deadline passed, not that the peer
failed; check `peer_status` before treating it as a failure.

Long turns: open the project with `bin/open`, which sets a four-hour
`MCP_TOOL_TIMEOUT`. Dispatch long assignments with `wait: false` and collect them
later with `turn_result`, so no single client call has to outlive the turn.

`bin/orch` is the same layer on the CLI for debugging; the MCP tools are the
orchestrator's channel.

## Projects

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
5. Send each peer an ordered queue of every reachable item it has, in the same turn as
   its report requirement. Use `instruct_many` for parallel assignments and `instruct`
   for one peer. Tell it to work the queue top to bottom without waiting for the
   dispatcher, one Phase A and one Phase B per stretch, and to end with a `REPORT:` line.
6. The output of each call is the peer's report: finished, open, blocked, and its
   witness output. Do not ask for a separate message.

## Peer collaboration

When two or more peers can solve an item faster or better together, give them a shared contract:
- Name the shared goal, the owner of the result, and the done condition.
- Send each participant the same brief: who else is involved, what each one owns, and
  the file or directory where results go.
- Peers hand off through files in their own checkout and report back to you. You relay
  what the other side needs; do not relay every message.
- The result owner reports the outcome in its `REPORT:` line.
- Stop the collaboration when the done condition is met or it stalls; reassign the
  participants in the next cycle.
- If you see messaging tools that list peers, tell them to message each other to collaborate

## Maximum effort and yield

Every decision the dispatcher makes is made for maximum effort and maximum yield.
This section is the standing rule; the other sections apply it.

- Choose the solution that gets the best result the project can reach, even when a
  cheaper one would pass a witness. Work around a platform's limits rather than copying
  another platform's habits. Rearchitect when the result needs it.
- Maximum yield means finished, verified work per cycle. Prefer one complete pass over
  several partial ones.
- Treat any issue as a defect. A witness that fails, a surprising output, a warning, a
  slow path, a flaky check, or behaviour the owner did not ask for is a defect to be
  fixed or recorded with its cause. It is never waved through as "expected".
- Decide, do not defer. Reversible choices are made by the dispatcher and reported
  afterwards, with the reasoning recorded in the affected ledger. Never hold a choice back
  because it is a judgement call, a trade-off, a visual or tuning value, a replay or
  protocol design, or an open question the evidence can settle.
- The only things that wait for the owner are: credentials; deleting or rewriting data
  that cannot be recovered; licence or legal confirmations the owner has deferred; and
  facts only the owner holds, such as the intended product scope. Everything else is decided.
- Ignore effort and labour budgets when judging what to do next.

## Wall-clock rules

Time a peer spends idle or waiting is lost progress. Where time is wasted, the fix
goes into this skill. Where work is repeated or tokens and turns are wasted, the fix
goes to the gm project as a change to the gm skill.

- A peer never waits on the dispatcher for its next item. Every dispatch carries the
  whole ordered queue, so a finished item is followed by the next one in the same turn.
- Dispatch on the idle event, not on the timer. A `bin/watch` line for a finished turn
  is answered at once; the ten-minute wake is only the fallback.
- A turn that ends with a stub such as "waiting for the build" is not a report. Read
  the pane or `read_output` for the result; do not spend a turn asking for it again.
- Check `alive` for every peer each cycle. A dead peer is resumed in that same cycle
  with its open queue.
- Blocked items never leave capacity idle. When a peer has no reachable item, start
  the peer of another project whose ledger has reachable open work.
- Peers do not sleep-and-poll. They block once on the job's PID or use a background
  job that notifies on exit, with a timeout, and do other queue items meanwhile.
- A peer does not end its turn while its own job is still running, unless the job
  re-invokes it on exit.
- Build and check once per Phase B, after the last change. Reuse build directories and
  caches. Run only the tests the changed surface needs.
- Shared installs, such as the gm binary, and `/model` switches run in a quiet window
  when no peer is busy. Queue them and act the moment the window opens.
- Each cycle, measure the waste from the transcripts (wait loops, repeated builds,
  round trips per item) and report the worst item per peer with its fix.

## Two-phase work

All changes a peer has for a stretch of work are made in one Phase A pass before any
build, test, gate, witness or browser run. Phase A does not stop between items to
validate, and it is not split into several small Phase A passes.

- Phase A: code, docs and tests only. Each change is committed locally and listed in
  the peer's `docs/PENDING_VALIDATION.md` with the file, the row or phase, and the
  witness it needs. Nothing is pushed.
- Phase B: build, run every listed witness, fix every defect, then push to origin main
  in one pass. Each peer runs its own Phase B when its own Phase A is done. Peers do
  not wait on each other.
- One push per Phase B. Do not push to check CI, and do not push a fix for a CI run
  that is still in progress. Finish every outstanding change first, then validate once.
- A second push is allowed only when the first Phase B push itself breaks a listed
  witness. The dispatcher approves it and states the reason in its owner report. A
  push that only repairs a CI result on an already-validated commit is not allowed.
- Do not watch or poll CI between fixes. Check the hosted run at most once, with a
  single status call, after the one push. Never loop on `gh run view` or `gh run list`.
- When hosted Actions are unavailable, for example at the spending limit, validate
  locally and push once. Do not push to find out whether CI would pass.
- When a peer gets a new item while its Phase A is open, fold the item into that same
  pass. Do not start a second Phase A or a validation in the middle.
- A peer that is already running a gate or Pages build may finish that check, since it
  validates earlier work.

## Decision rules

- Anything reversible: decide and proceed; tell the owner afterwards. If it is not
  reversible, make it reversible.
- Credential changes: ask the owner first.
- Blocked peers work on unblocked items instead.
- Every blocked item is driven towards unblocking if possible.
- Always decide to drive deeper progress. Ignore effort; there is no budget on labor.
  If progress can be made, choose it.
- When the owner's question has several options, pick the one that makes the most
  progress, say which one and why in the reply, and dispatch it. Do not hand the
  choice back when the choice is reversible.
- Read-only verification: decide and proceed.
- Pushes happen in Phase B only, as one pass per peer (see Two-phase work). Commits are
  local until then. The dispatcher approves no push outside that rule, including a
  push to repair CI on an earlier commit.
- Every change in the hyperagent repo is committed and pushed to origin/main before
  the cycle ends. At the end of each cycle, check `git status` and
  `git log origin/main..HEAD` in /workspace/hyperagent, and push anything outstanding.
  Untracked files that are not part of the orchestrator's work are left alone.

## Standing instructions for peers

Include these in every assignment prompt:
- When you address peers use the following format: 'we want to x' describing what you want it to do, no further instruction 
- Work only in the cwd you were started in, on main, in the shared checkout. Do not clone
  scratch copies or fake worktrees.
- Commit as lanmower only. Commit messages and PR descriptions carry no `Co-Authored-By`
  or other trailer, and no tool or model attribution line of any kind.
- Two-phase work: make every change for the stretch first (Phase A, code, docs and tests
  only, no build, test, gate or witness), list each change in `docs/PENDING_VALIDATION.md`,
  commit locally, and push nothing. Then run Phase B: build, witness, fix, push once.
  Do not watch or poll CI between fixes; at most one status check after the push.
- Maximum effort and yield. Choose the solution that gets the best result, work around
  the platform's limits, and rearchitect where needed.
- Treat any issue as a defect: fix it, or record it with its cause. Do not call it expected.
- Verify by running the thing in Phase B. Quote the witness output. Fix any issues that arise.
- Stop dev servers by their own PID; never use `pkill` or `killall`.
- Work the whole queue without waiting for the dispatcher. No sleep-and-poll loops:
  block once on the job's PID with a timeout, or do the next item while it runs. Batch
  gm verbs into one `run_code` body. Record each witness's wall time in the ledger.
- End every turn with: `REPORT: finished|open|blocked — <your rhetoric> — witness: <output evidence>`.

## Resume

A peer that is not running can continue its saved conversation instead of starting
fresh. Call `resume_project` with no arguments to resume the last project touched, or
with `project` for a specific one; add `prompt` to send work in the same call. Pass
`session_id` to pick an older conversation (an id; for claude also a session name or
absolute `.jsonl` path, for codex a name) and `harness` for claude, codex or freebuff.
A session id missing from the project's store is looked up globally; freebuff has no
global lookup. The ledger prefers the last session actually used when no id is given;
`list_project_sessions` shows the ids per harness. `resume_project` restarts a dead
airc server and retires any stale hub first. `start_projects`,
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

Start `bin/watch` in the background before the first cycle. It reports the moment a
turn finishes, not after a quiet window: a new journal `result` record or a pane
going from busy to not busy prints one line immediately with the peer, its session,
the whole last response, and the exact `instruct` arguments that feed that response
back (`trigger: result|pane`; `--idle` is only the fallback for a peer with neither
signal). Use that line to decide the next assignment; it removes the need to poll
`peer_status` or re-read a pane. `--once` reports peers that airc already marks not
busy, and `--text` prints the human form.

`instruct` and `turn_result` return only the turn's final response block. Interim
narration and tool calls remain in the journal, so `list_outputs` and `read_output`
show everything without bloating the returned turn. `bin/orch output NAME --tail 40`
is the CLI view.

A client that speaks MCP events can skip `bin/watch`: subscribe with
`resources/subscribe` or `subscriptions/listen`, set `logging/setLevel`, and receive
the same idle and gone payloads as `notifications/resources/updated` and
`notifications/message`; `hyperagent://events` holds the recent stream.

Also start a loop that wakes this session every 10 minutes validating that all peers
are making maximal progress. On each wake, run the cycle above. If loops are not
possible, use monitors, waits, sleeps, or the CLI to do it.
