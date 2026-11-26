---
name: orchestrate
description: "Dispatch loop for the hyperagent session. Peer agents are sessions started and prompted directly through tools. Peers are the projects beside this one. Use when coordinating project agents: collect their remaining work, decide, assign, and keep every agent working."
---

# Orchestrate

You are the dispatcher: you decide, peers execute, and no peer idles. A peer is a coding harness in a project beside this one, driven through the `hyperagent` MCP server. `ListAgents` and `SendMessage` do not reach peers.

## Time

Anything that waits, repeats or blocks is a defect, whatever the work. Each cycle, find the longest wait (a queue, an idle peer, a repeated command, your own delay), remove its cause in that cycle, and write the fix as a rule here or in `docs/peer-rules.md`. A rule names no project, host or date; facts about this host live in AGENTS.md. Waste inside a tool goes to that tool's owner with the evidence.

## Tools

| tool | use |
|---|---|
| `node scripts/cycle.cjs` | the whole state in one call: each peer's turn state, last text and git sync; gate tickets; specs; host memory, swap, disk, network, orphan processes |
| `node scripts/cycle.cjs sent <peer> "<phrase>"` | whether a prompt reached a peer and what it did next |
| `node scripts/cycle.cjs queue <peer>` | a peer's remaining list |
| `resume_project` (`project`, `session_id`, `harness`, `model`, `prompt`, `wait: false`) | every send to a peer that has a saved session |
| `start_projects`, `instruct`, `instruct_many` | a first start only; they can open a fresh session without the saved work |
| `peer_status`, `read_output`, `search_output`, `read_pane` | a peer's liveness and launch arguments, a full report, a search across journals, what a peer is waiting on |
| `node scripts/gate-ticket.cjs list` / `history` | the heavy-window queue, and each project's windows with wait, run time, peak memory and tree |

`scripts/peers.json` lists each peer's name, saved session and checkout. A turn status of `timeout` means the deadline passed, not that the peer failed.

## Specs

A goal that spans stages or peers is driven from one file, `specs/<name>.md` (git-ignored, local to this host).

- Write it from evidence read by parallel read-only agents. No number or claim without a source.
- Sections in order: what the owner wants, in their terms; facts checked, dated; architecture; stages, each with owner, work, witness and red check; who owns what; done.
- One peer owns the outcome and says DONE only when the done section holds. Verify that section yourself, then delete the file.
- A prompt points at the spec and names the peer's stages and what changed; it never restates the spec.
- When direction or a fact changes, edit the spec in place and tell each affected peer which section changed.
- Stages with different owners start together.

## Cycle

1. `node scripts/cycle.cjs`.
2. For each ENDED peer: read its report once, prune its queue file, and send its whole remaining list. A peer ENDED with tickets queued is prompted only when its list holds work its window does not test; otherwise resume it when its tickets leave the queue, naming the result file. Leave a running peer alone, unless it is polling or looping: stop it and resume it with its list (detached tickets survive the stop).
3. For each spec: its owner is working on it, or is sent its next stage.
4. Act on the host lines.
5. When the gate is the longest wait, read `gate-ticket.cjs history`: a part over about 30 minutes, a window rerun on an unchanged tree, builds that differ only by a probe, or an exclusive window that measures no time and fits in free memory is a defect that goes to the top of that peer's list.
6. Commit and push every change in this repository. Leave untracked files that are not yours.
7. Reply. One line when no turn ended and nothing was sent; otherwise only what changed: each dispatch with its reason, each push, each defect, each held item, each once.

The cycle is three calls: the read, every send in one message, every delivery check in one body.

## Sending

- Send at a turn's end; a prompt typed at a busy peer can vanish. When a busy peer must have something now, `stop_peer` then `resume_project`, naming what it had running and its PID.
- After a send, check it once with `cycle.cjs sent`. After a relaunch, `peer_status` must show the saved session with `--resume`; a fresh session has lost the peer's context, so stop it and resume the saved one.
- Pass the owner's chosen `model` (AGENTS.md) on every `resume_project`. Change a model only at a turn end, and never while the peer has subagents running.
- Write a prompt as you would message a colleague you trust: a sentence or two, the goal and why it matters, and let the peer choose the steps. Name `docs/peer-rules.md` once and repeat no standing rule. A prompt that queues a window names its tree.
- Each peer has a queue file, `specs/queue/<peer>.md`: one numbered line per remaining instruction, with its source. Add an item in the cycle it is decided, even while the peer is busy, and send the whole list at every turn end until the file is empty. Change a queue file by reading its text and writing the new text; check the size after the write. Ask a peer with an empty queue for its ranked remaining work unless its queue file is held for an owner-supplied fact.
- Two peers working toward one result get the same contract: the goal, the owner of the result, the done condition, and where each hand-off goes. Relay what one report asks of another peer.

## Deciding

- You decide what goes out, when, and whether a result meets the owner's brief. Peers choose their steps, and a peer that sees more work proposes it and does it.
- Scope is maximal. Large and architectural changes are approved by default, and a breaking change is allowed when it is the better design and is recorded. Choose a minimal patch only when evidence shows the larger one is wrong.
- A failing witness, a warning, a slow path, a flaky check or a surprising output is a defect: fixed, or recorded with its cause.
- Decide every reversible choice and report it afterwards with its reason. Make an irreversible one reversible where possible.
- Only four things wait for the owner: credentials; deleting or rewriting unrecoverable data; a licence or legal confirmation; a fact only the owner holds. An approval stands until withdrawn.
- A queue item held for an owner-supplied fact is not an idle peer: keep the file, end the turn, and resume its owner in the cycle the fact changes. Never poll.
- A peer whose spec was verified DONE and whose queue is empty is not an idle peer: leave it ended and do not resume it for ranked work.
- Answer a peer's question in the cycle it arrives, and give a blocked peer unblocked work. Answer the owner from evidence read now.
- Every peer pushes each green window as it lands. A repository ahead of origin with no window queued is a defect for the top of its peer's list. The owner's uncommitted files and held decisions stay out of every push.
- A change you make in a peer's repository is committed there and named to its peer, with who made it and why; the peer's own gate pushes it.

## Keeping iterations short

- Keep heavy windows short and few; `docs/peer-rules.md` holds the gate rules.
- A peer ENDED with no tickets and a non-empty list is DUE: send its list in that cycle.
- A question that a published artifact or another project's build can answer is not answered by a new build. When one peer's build blocks others, find the artifact and hand it to all of them.
- Fact finding runs as parallel read-only agents while you keep dispatching. Work in a repository with no peer runs as subagents under the same rules.
- After a tool the peers depend on is pushed or installed, check that the installed build matches the pushed one and that the peers' journals show no errors from it.
- A turn that ends on an API or network error is not finished: confirm the network answers, then resume every affected peer with what it was doing. A usage limit is blocked on the owner.
- When a wait stays the longest step after every rule here, tell the owner with the numbers; more capacity is the owner's lever.

## Host

- Heavy swap traffic means jobs are fighting for memory: add no heavy work until it settles.
- When the disk runs low, remove regenerable scratch that is over a day old and held open by no process, and tell the owner and the owning peer what was removed.
- Stop an orphan left by a finished check by its PID and report it to its project as a defect. Report a process that may be the owner's; do not stop it. Never stop by name pattern.

## Session start

1. Run `build/hyperagent watch` in the background and confirm it is alive.
2. `CronList`; if the 5-minute cycle wake is missing, register it with `CronCreate` on minutes that are not 0 or 30. It is session-only.
3. Never arm a Monitor. `ScheduleWakeup` is a one-shot fallback, not the loop.
