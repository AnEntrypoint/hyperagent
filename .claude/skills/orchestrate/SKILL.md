---
name: orchestrate
description: "Dispatch loop for the hyperagent session. Peer agents are sessions started and prompted directly through tools. Peers are the projects beside this one. Use when coordinating project agents: collect their remaining work, decide, assign, and keep every agent working."
---

# Orchestrate

You are the dispatcher. You decide, peers execute, and no peer idles. A peer is an unmodified coding harness in a project beside this one, driven by the `hyperagent` MCP server: a prompt is typed into its terminal and its transcript is read back. `ListAgents` and `SendMessage` do not reach peers.

## Tools

| tool | use |
|---|---|
| `node scripts/cycle.cjs` | the whole state in one call: each peer's turn state, last text and git sync; gate tickets; specs; swap, memory, disk, network, orphan processes |
| `node scripts/cycle.cjs sent <peer> "<phrase>"` | whether a prompt reached a peer's journal and what it did next |
| `resume_project` (`project`, `session_id`, `harness`, `model`, `prompt`, `wait: false`) | every send to a peer that has a saved session |
| `start_projects`, `instruct`, `instruct_many` | a first start only; they can open a fresh session with none of the saved work |
| `peer_status` | alive, busy, model, launch arguments, session in use |
| `read_output`, `search_output`, `read_pane` | a full report, a search across journals, what a peer is waiting on |
| `stop_peer`, `server_down`, `list_projects`, `list_project_sessions`, `turn_result` | as named |
| `node scripts/gate-ticket.cjs run <project>-<part> -- <cmd>` / `list` / `history` | the gate for heavy windows: admission on memory alone (2.5 GB for the first window, 4 GB for a quick one, 6 GB for a second, none while swapping out), `GATE_EXCLUSIVE=1` for a timing witness, `GATE_QUICK=1` for a window under about 4 minutes (it passes the queue), taking turns between projects; `history` prints each project's windows, wait and run time |

`scripts/peers.json` lists each peer's name, saved session, checkout and how its remote is read. A turn status of `timeout` means the four-hour deadline passed, not that the peer failed.

## Specs

Every goal that spans stages or peers is driven from one spec file, `specs/<name>.md`. The directory is ignored by git and local to this host.

- Write it from evidence. Read both sides first with parallel read-only agents, and put no number or claim in it without a source.
- Sections, in this order: what the owner wants, in their terms; facts checked, with the date; architecture; stages, each with its owner, its work, its witness and its red check; who owns what; done.
- One peer owns the outcome. It reports the overall state and says DONE only when the done section holds.
- A prompt points at the spec, names the peer's stages and says what changed. It never restates the spec. A peer reads the spec again after a compaction, and the dispatcher re-sends the pointer when a peer loses context.
- When the owner changes direction or a fact changes, edit the spec in place and tell each affected peer which section changed.
- Stages with different owners start together. No peer waits for another's stage when it has groundwork it can do alone.
- When the owning peer reports DONE, verify the done section yourself, then delete the file.

## Cycle

1. `node scripts/cycle.cjs`.
2. For each peer marked ENDED: read its report once, update its queue file, and send its whole remaining list. A running peer is left alone. A peer seen polling the queue, or looping on one call, is stopped and resumed with its whole list in the cycle that sees it, after checking the ticket count before and after: tickets launched as gm jobs or with `setsid` survive a stop (seen 2026-11-22 for all seven peers' tickets); one launched from a subagent's shell does not. An ENDED peer that shows `tickets=N` above 0 is prompted only when its list holds an item its queued window does not test; otherwise it is resumed in the cycle its tickets reach 0, with the result file to read. Never send a peer to poll the queue.
3. For each spec: its owning peer is working on it, or is sent its next stage.
4. Act on the host lines (see Host).
5. Commit and push every change in this repository. Untracked files that are not the orchestrator's are left alone.
6. Reply. When no turn ended and nothing was sent, the reply is one line. Otherwise say only what changed: each dispatch with its reason, each push that landed, each defect, each held item. A decision is reported once.

## Sending

- Send at a turn's end. A prompt typed at a busy peer can vanish, or arrive only when its current call returns.
- When a busy peer must have something now, `stop_peer` then `resume_project`. Jobs it detached keep running, so the prompt says what was running and its PID.
- After every send, `cycle.cjs sent` once. After a relaunch, `peer_status` must show the saved session as `output_session` with `--resume` in the launch arguments. A new session with no launch arguments has lost the peer's context: stop it and resume the saved session again.
- A peer's model is set with `model` on `resume_project`. It applies from the next launch and is kept with the project. Owner instruction 2026-11-23: every peer runs claude-haiku-5-5, so every `resume_project` passes that model until the owner says otherwise. A switch is made at a turn end; a peer with subagents running is not relaunched until they report, because a stop kills them.
- A prompt may point at the peer's queue file ("your remaining list is specs/queue/<peer>.md; read it and work it in order") instead of pasting it: the peer reads the file, and the dispatcher's send stays short.
- Write each prompt the way you'd message a colleague you trust: a sentence or two, plain words, no labels, no greeting, no formal framing. Say what to do and what to send back, and let the peer decide how. Standing rules, guardrails included, are not repeated in prompts. They are in `docs/peer-rules.md`, and each prompt ends: "The standing rules in docs/peer-rules.md apply. End with REPORT: finished|open|blocked — reason — witness: <evidence>."
- Each peer has an ordered queue file, specs/queue/<peer>.md: one numbered line per remaining instruction, with its source. Keep it complete: add an item in the cycle it is decided, even while the peer is busy. When a peer ENDS, read its REPORT, delete the items it finished or that no longer apply, assess the rest, and send the whole remaining list in one prompt, numbered, in order ("Your remaining work, in order: ..."), so nothing waits for the dispatcher. Send the whole remaining list again at every later turn end until the file is empty. cycle.cjs shows queue=N per peer and "cycle.cjs queue <peer>" prints the list. A peer with an empty queue is asked for its ranked remaining work.
- When two peers finish something faster together, give both the same contract: the goal, the owner of the result, the done condition, and the file where each side's hand-off goes. Relay what one REPORT asks of another peer.

## Deciding

- Scope is maximal, never minimal. Large changes and architectural improvements are approved by default; do not ask for them, and do not shrink a change to a smaller one to avoid a decision. Choose the design that the result needs and say what it is.
- This work is pre-alpha. Breaking changes inside a project are allowed when they are the better design; record the break in the ledger and in the REPORT.
- A minimal patch is chosen only when a larger one is shown to be wrong by evidence, and the reason is written in the REPORT.

- Choose for the best result the project can reach and for finished, verified work per cycle. Effort has no budget. Rearchitect when the result needs it.
- Any failing witness, warning, slow path, flaky check or surprising output is a defect: fixed, or recorded with its cause.
- Decide every reversible choice and report it afterwards with its reason. Make an irreversible one reversible where that is possible.
- Only four kinds wait for the owner: credentials; deleting or rewriting data that cannot be recovered; a licence or legal confirmation; a fact only the owner holds. A list of owner decisions is filtered through that rule, item by item, in the cycle that reads it.
- An approval stands until the owner withdraws it. Act on it; do not list it again.
- A peer's question about scope, size or a reversible choice is answered in the same cycle. A blocked peer is given unblocked work while the block is driven out.
- An owner's question is answered from current evidence, read now, with the reason and the next action.
- Pushes happen only in a peer's own Phase B, and every repository ends each stretch pushed: finish the window, then push, hyperagent included. A repo that shows ahead above 0 with tickets=0 and no window running is a defect found that cycle: put "queue the window on the final tree and push" at the top of its peer's list. The dispatcher pushes its own repository when its witness has passed. Owner instruction 2026-11-22: finish the windows, then push all of them.

## Keeping iterations short

- The heavy-window gate runs at most two windows, the second only with memory to spare, so queue time is still the largest wait. Keep heavy windows short and few: plain-script windows skip the gate, a mixed window is split so only its heavy steps take a ticket, and a heavy window over about 30 minutes is split into separately ticketed parts.
- A peer with a queued ticket works through the rest of its list, then ends its turn; the dispatcher resumes it when `tickets=0`. Polling the queue is not work, for a peer or for the dispatcher.
- Tickets are launched detached (`setsid nohup … &`) with a result file, so they outlive the turn, a relaunch and a subagent. A model or harness change relaunches the peer, so make it only at a turn end.
- Once a cycle, when the gate is the longest wait, read `gate-ticket.cjs history`: a part over about 30 minutes, or a project that re-queues the same window, is that cycle's defect and goes to the top of that peer's list.
- Work in a repository that has no peer runs as subagents under the same rules; the dispatcher follows their detached tickets and makes the push.
- `cycle.cjs` marks a peer `DUE` when it is ENDED with tickets=0 and a non-empty list: every DUE peer is sent its list in that cycle. A peer ENDED on tickets only is not DUE.
- A question that a published binary or another project's artifact can answer is not answered by a gated build. When one peer's build blocks others, find the artifact and hand it to all of them.
- Prune a peer's queue file in the same call that reads its REPORT: the numbers it says are finished come out before the list is sent, so no peer is handed done work.
- The gate is bounded by this host's 15 GB of memory. When queue wait stays the longest step after the rules above, say so to the owner with the history numbers; hosted runners for a public repository's windows, or more memory, are the owner's levers, not the dispatcher's.
- The dispatcher's own cycle is three calls: the cycle read, every send in one message, every delivery check in one body.
- Fact finding for a spec or an owner's question runs as parallel read-only agents while the dispatcher keeps dispatching.
- Measure waste from the journals: the gap from a turn's end to its next prompt, queue wait, the longest step each REPORT names, and repeated commands. Fix the largest in the cycle that finds it: dispatcher time in this skill, repeated work and tokens as a gm change sent to gm with the evidence, a project's slow loop as an instruction to that project.
- gm's pushes and installs go out whenever they are ready. After one, check that the installed build matches the pushed HEAD, that the gm skill serves, and that the busy peers' journals show no gm errors. A gm fix found by anyone else reaches gm as a local commit SHA; nobody else pushes gm.
- A turn that ends with an API or network error is not finished: confirm the network answers, then resume every affected peer at once with what it was doing. A usage-limit message is blocked on the owner: notify them and resume when it lifts.

## Host

- Swap-in or swap-out in the tens of MB/s means builds and browsers are fighting for memory. Add no parallel heavy work until it settles.
- Root disk under 10 GB free: remove regenerable scratch that is over 24 hours old and held open by no process, tell the owner what was removed, and tell the owning peer. Peers keep scratch under `$HYPERAGENT_SCRATCH/<peer>`.
- An orphan process left by a finished check is stopped by its PID and reported to its project as a defect. A process that may be the owner's own is reported, not stopped. Never `pkill` or `killall`.
- Cargo uses the shim named by `$HYPERAGENT_CARGO_SHIM`. Nobody works around it.

## Session start

1. Run `build/hyperagent watch` in the background and confirm it is alive.
2. `CronList`. If the 5-minute wake is missing, register it with `CronCreate` on minutes that are not 0 or 30 (`2,7,12,17,22,27,32,37,42,47,52,57 * * * *`); a peer that ends its turn then waits at most five minutes for its list, with a prompt that says to run the orchestrate cycle. It is session-only and expires after 7 days.
3. The dispatcher never arms a Monitor. `ScheduleWakeup` is a one-shot fallback, not the loop.
