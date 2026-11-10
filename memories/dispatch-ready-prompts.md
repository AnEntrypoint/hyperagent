---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers; currently holds only the quiet-window airc server restart; send on the idle event in the same wake, then remove the sent section"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T08:03:58.183Z
---

The orchestrate skill's wall-clock rule: each busy peer's next dispatch is written before its turn ends. On the idle event, read the peer's REPORT, adjust only what the report changes, send the prompt, and delete its section here.

Every prompt ends with the standing lines: work only in your cwd on main in the shared checkout; commit as lanmower only, no trailer or attribution line; Phase A (code, docs, tests, listed in docs/PENDING_VALIDATION.md, committed locally) then Phase B (build once, witness, batch fixes, rerun only what failed, push once, one status check at most); treat any issue as a defect; stop processes by their own PID only; wait on a job once with a timeout sized from its recorded wall time; read git state once per phase; record witness wall times; `REPORT: finished|open|blocked — <summary> — witness: <output evidence>`.

## jayzee: stretch 1 Phase B go-ahead (dispatcher, 2026-11-10 08:45 UTC)

jayzee stretch 1 (tails) is Phase A complete with local commits 2296095 and 677f290, and blocked on Phase B: the project browser on port 10221 is down, and the controlled-run gate refuses PAUSED at host load above 0.20. gm was asked (turn 4b6cce525240) to start the browser through the playwriter setup and confirm it answers on 10221.

Send jayzee the Phase B go-ahead only when both hold: gm reports the browser up on 10221, and no peer is busy, so the host load is quiet enough for the controlled gate. Then say: run stretch 1 Phase B (controlled tails LIVE and PAUSED, WebGPU and WebGL2, 1024 threaded, tail-report.mjs), push origin main once carrying 2296095 and 677f290, and REPORT for stretch 1 only. Stretches 2 to 5 follow as separate sends.

## Quiet window: restart the airc server on the rebuilt ccbridge

Peers whose turn ran longer than thirty minutes were retired within a minute or two of the turn ending (gm, spoint, jayzee and fx on 2026-11-10), and any job they had left running died with them. Cause: the hub's idle clock was last set when the prompt was submitted, so a long turn was already past the 1800 s reap limit when the dispatcher's client detached. Fixed in airc/src/server/server.cpp (detach now resets the clock) and built into build/ccbridge. The running servers (pids 884614 and 931565, the older .orch/ccbridge/v0.1.0.4 binary) still have the defect. When no peer is busy, restart the server on build/ccbridge and resume each peer's newest session. Until then: answer a finished turn within a minute, resume with `resume: true` when a peer is gone, and ask peers to keep the turn open while their own job runs.
