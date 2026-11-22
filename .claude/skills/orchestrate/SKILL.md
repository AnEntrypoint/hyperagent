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
| `node scripts/gate-ticket.cjs run <label> -- <cmd>` / `list` | the first-in-first-out gate for heavy windows |

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
2. For each peer marked ENDED: read its report once, update its queue file, and send its whole remaining list. A peer whose own job is still running gets no prompt until the job exits. A running peer is left alone.
3. For each spec: its owning peer is working on it, or is sent its next stage.
4. Act on the host lines (see Host).
5. Commit and push every change in this repository. Untracked files that are not the orchestrator's are left alone.
6. Reply. When no turn ended and nothing was sent, the reply is one line. Otherwise say only what changed: each dispatch with its reason, each push that landed, each defect, each held item. A decision is reported once.

## Sending

- Send at a turn's end. A prompt typed at a busy peer can vanish, or arrive only when its current call returns.
- When a busy peer must have something now, `stop_peer` then `resume_project`. Jobs it detached keep running, so the prompt says what was running and its PID.
- After every send, `cycle.cjs sent` once. After a relaunch, `peer_status` must show the saved session as `output_session` with `--resume` in the launch arguments. A new session with no launch arguments has lost the peer's context: stop it and resume the saved session again.
- A peer's model is set with `model` on `resume_project`. It applies from the next launch and is kept with the project.
- A prompt begins "We want …" and uses plain sentences: what to do, what must not change, what to send back. Standing rules are not repeated in prompts. They are in `docs/peer-rules.md`, and each prompt ends: "The standing rules in /workspace/hyperagent/docs/peer-rules.md apply. End with REPORT: finished|open|blocked — reason — witness: <evidence>."
- Each peer has an ordered queue file, specs/queue/<peer>.md: one numbered line per remaining instruction, with its source. Keep it complete: add an item in the cycle it is decided, even while the peer is busy. When a peer ENDS, read its REPORT, delete the items it finished or that no longer apply, assess the rest, and send the whole remaining list in one prompt, numbered, in order ("Your remaining work, in order: ..."), so nothing waits for the dispatcher. Send the whole remaining list again at every later turn end until the file is empty. cycle.cjs shows queue=N per peer and "cycle.cjs queue <peer>" prints the list. A peer with an empty queue is asked for its ranked remaining work.
- When two peers finish something faster together, give both the same contract: the goal, the owner of the result, the done condition, and the file where each side's hand-off goes. Relay what one REPORT asks of another peer.

## Deciding

- Choose for the best result the project can reach and for finished, verified work per cycle. Effort has no budget. Rearchitect when the result needs it.
- Any failing witness, warning, slow path, flaky check or surprising output is a defect: fixed, or recorded with its cause.
- Decide every reversible choice and report it afterwards with its reason. Make an irreversible one reversible where that is possible.
- Only four kinds wait for the owner: credentials; deleting or rewriting data that cannot be recovered; a licence or legal confirmation; a fact only the owner holds. A list of owner decisions is filtered through that rule, item by item, in the cycle that reads it.
- An approval stands until the owner withdraws it. Act on it; do not list it again.
- A peer's question about scope, size or a reversible choice is answered in the same cycle. A blocked peer is given unblocked work while the block is driven out.
- An owner's question is answered from current evidence, read now, with the reason and the next action.
- Pushes happen only in a peer's own Phase B. The dispatcher approves no other push.

## Keeping iterations short

- The heavy-window gate runs one window at a time, so queue time is the largest wait. Keep heavy windows short and few: plain-script windows skip the gate, a mixed window is split so only its heavy steps take a ticket, and a heavy window over about 30 minutes is split into separately ticketed parts.
- A peer with a queued ticket keeps working on something the window does not test: another repository, the spec, the next stage's design. Polling the queue is not work.
- Fact finding for a spec or an owner's question runs as parallel read-only agents while the dispatcher keeps dispatching.
- Measure waste from the journals: the gap from a turn's end to its next prompt, queue wait, the longest step each REPORT names, and repeated commands. Fix the largest in the cycle that finds it: dispatcher time in this skill, repeated work and tokens as a gm change sent to gm with the evidence, a project's slow loop as an instruction to that project.
- gm's pushes and installs go out whenever they are ready. After one, check that the installed build matches the pushed HEAD, that the gm skill serves, and that the busy peers' journals show no gm errors. A gm fix found by anyone else reaches gm as a local commit SHA; nobody else pushes gm.
- A turn that ends with an API or network error is not finished: confirm the network answers, then resume every affected peer at once with what it was doing. A usage-limit message is blocked on the owner: notify them and resume when it lifts.

## Host

- Swap-in or swap-out in the tens of MB/s means builds and browsers are fighting for memory. Add no parallel heavy work until it settles.
- Root disk under 10 GB free: remove regenerable scratch that is over 24 hours old and held open by no process, tell the owner what was removed, and tell the owning peer. Peers keep scratch under `/workspace/tmp/<peer>`.
- An orphan process left by a finished check is stopped by its PID and reported to its project as a defect. A process that may be the owner's own is reported, not stopped. Never `pkill` or `killall`.
- Cargo goes through the slot shim at `/config/.local/bin/cargo`. Nobody works around it.

## Session start

1. Run `build/hyperagent watch` in the background and confirm it is alive.
2. `CronList`. If the 10-minute wake is missing, register it with `CronCreate` on minutes that are not 0 or 30 (`7,17,27,37,47,57 * * * *`), with a prompt that says to run the orchestrate cycle. It is session-only and expires after 7 days.
3. The dispatcher never arms a Monitor. `ScheduleWakeup` is a one-shot fallback, not the loop.
