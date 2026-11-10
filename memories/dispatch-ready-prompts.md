---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers (fx superagent view; holds the quiet-window airc server restart); send on the idle event in the same wake, then remove the sent section"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T06:16:17.530Z
---

The orchestrate skill's wall-clock rule: each busy peer's next dispatch is written before its turn ends. On the idle event, read the peer's REPORT, adjust only what the report changes, send the prompt, and delete its section here.

Every prompt ends with the standing lines: work only in your cwd on main in the shared checkout; commit as lanmower only, no trailer or attribution line; Phase A (code, docs, tests, listed in docs/PENDING_VALIDATION.md, committed locally) then Phase B (build once, witness, batch fixes, rerun only what failed, push once, one status check at most); treat any issue as a defect; stop processes by their own PID only; wait on a job once with a timeout sized from its recorded wall time; read git state once per phase; record witness wall times; `REPORT: finished|open|blocked — <summary> — witness: <output evidence>`.

## Quiet window: restart the airc server on the rebuilt ccbridge

Peers whose turn ran longer than thirty minutes were retired within a minute or two of the turn ending (gm, spoint, jayzee and fx on 2026-11-10), and any job they had left running died with them. Cause: the hub's idle clock was last set when the prompt was submitted, so a long turn was already past the 1800 s reap limit when the dispatcher's client detached. Fixed in airc/src/server/server.cpp (detach now resets the clock) and built into build/ccbridge. The running servers (pids 884614 and 931565, the older .orch/ccbridge/v0.1.0.4 binary) still have the defect. When no peer is busy, restart the server on build/ccbridge and resume each peer's newest session. Until then: answer a finished turn within a minute, resume with `resume: true` when a peer is gone, and ask peers to keep the turn open while their own job runs.

## fx (owner direction, 2026-11-10: the superagent's view of its agents)

we want fx's superagent to see its agents the way a dispatcher sees peers, not the full view of what they do. The owner's words: "superagents shouldnt get the full view of what agents are doing, we must make sure that fx's superagent view is a rough match of what we experience from the perspective of superagent feedback when running in the fx app".

The reference is the hyperagent dispatcher's experience: it prompts a peer and receives only the turn's final response; it is told the moment a turn ends, with that last response, and when a peer is gone; it can read alive and busy state; everything else a peer did (tool calls, streamed text, its context) stays in that peer's own journal and is read on request, in slices. Give the superagent the same shape from its roots, in the tool results and in what the fx app shows the user for a superagent turn. Record the design in docs/superagent.md and witness it: after a root runs a turn with many tool calls, the superagent's context holds the final response and status only.

My own experience as the dispatcher, so you can match it. I am the superagent in this loop, and this is roughly what I get:
- A prompt goes out and what comes back is the peer's final text for that turn, plus a status. Nothing in between: no tool calls, no streamed text, no intermediate reasoning. A turn that ends with a stub such as "waiting for the build" is still just a final text; I have to read the journal to learn what happened.
- A line arrives when a turn finishes: the peer's name, its session, and its last response. A line also arrives when a peer is gone. I act on that line; I do not poll for state.
- Alive and busy are the only state I read unless something is wrong. I do not read a peer's pane or its tool log by default.
- The full journal (every tool call, every line of output) exists and I can read it, but I read it in slices and only when a final text is not enough to decide. Dumping whole journals into my context overflowed it more than once today. Your superagent should treat the journal as something it can open on request, never as something it receives.
- Two mistakes I made that the design should prevent: I told a peer to copy memory files alongside its own format when I meant to replace them, and I wrote a rule into a peer's prompt that no one had asked for, which the peer then obeyed. So: the superagent passes a root only what the owner asked, and it does not add rules of its own.
- When a root is idle or gone, that is an event; when it is busy, the superagent waits for the event rather than asking again.

Fold this into the superagent stretch if it is still open; otherwise it is the next stretch, ahead of the ranked rows. The standing rules apply, and the turn stays open while your own jobs run.

## thebird (from gm's report, 2026-11-10 06:46: pin and artifact disagree)

we want thebird to make the txiki artifact and its pin agree. Its TXIKI_WASM_SHA256 is 137c08f6…, but the vendored web/vendor/txiki/tjs.wasm hashes to dcd3a434…. gm's witnesses ran with --allow-unpinned, because the driver fails closed on the mismatch. Rebuild the vendored artifact from the pinned sources (txiki v26.11.0, libuv dd9f081cc55a4d83e2a976848d074f28ee5f34eb, quickjs-ng 7b5f588e0f9b643e2996f291bfc861abeaceddc2), or regenerate the pin from the build that is shipped, whichever matches the sources, and record which in the ledger. Include the exit flush in the same rebuild, since that changes the hash anyway. Keep the seven gm_txiki_* exports. Report the final sha256 for gm. Then finish its queue as before; the standing rules apply, and the turn stays open while its own jobs run.

Related: [[jayzee-realistic-scenarios-shared-terrain]], [[floosie-integration-wanted]], [[gm-run-code-under-txiki-browser]], [[jayzee-web-architecture-static-pools]], [[fx-memory-format-hyperagent]].
