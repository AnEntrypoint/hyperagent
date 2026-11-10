---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers; currently holds only the quiet-window airc server restart; send on the idle event in the same wake, then remove the sent section"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T08:59:29.623Z
---

The orchestrate skill's wall-clock rule: each busy peer's next dispatch is written before its turn ends. On the idle event, read the peer's REPORT, adjust only what the report changes, send the prompt, and delete its section here.

Every prompt ends with the standing lines: work only in your cwd on main in the shared checkout; commit as lanmower only, no trailer or attribution line; Phase A (code, docs, tests, listed in docs/PENDING_VALIDATION.md, committed locally) then Phase B (build once, witness, batch fixes, rerun only what failed, push once, one status check at most); treat any issue as a defect; stop processes by their own PID only; wait on a job once with a timeout sized from its recorded wall time; read git state once per phase; record witness wall times; `REPORT: finished|open|blocked — <summary> — witness: <output evidence>`.

## jayzee: stretch 1 Phase B go-ahead (dispatcher, 2026-11-10 09:00 UTC)

jayzee stretch 1 (tails) is Phase A complete with local commits 2296095 and 677f290. Phase A is also complete for stretch 2 (8d9aa39) and stretch 3 (ae770a4), and stretch 4 Phase A was sent at 09:00 (turn 6f4be8d54eb3). Browser on 10221 is confirmed up by gm.

Send jayzee the Phase B go-ahead only when no peer is busy, so the host load is quiet enough for the controlled gate (fx was busy at 09:00). Then say: run stretch 1 Phase B (controlled tails LIVE and PAUSED, WebGPU and WebGL2, 1024 threaded, tail-report.mjs), push origin main once carrying 2296095 and 677f290, and REPORT for stretch 1 only. Stretches 2, 3 and 4 Phase B follow as separate sends, each with its own push, and each waits for the quiet host.

## gm: installed-build step go-ahead (dispatcher, 2026-11-10 09:00 UTC)

gm's installed-build commits are all on origin. The install reaches every peer, so send gm the go only when no peer is busy. gm was given the waste-audit Phase A (turn 2c775d20e63a) in the meantime.

## Quiet window: restart the airc server on the rebuilt ccbridge

Peers whose turn ran longer than thirty minutes were retired within a minute or two of the turn ending (gm, spoint, jayzee and fx on 2026-11-10), and any job they had left running died with them. Cause: the hub's idle clock was last set when the prompt was submitted, so a long turn was already past the 1800 s reap limit when the dispatcher's client detached. Fixed in airc/src/server/server.cpp (detach now resets the clock) and built into build/ccbridge. The running servers (pids 884614 and 931565, the older .orch/ccbridge/v0.1.0.4 binary) still have the defect. When no peer is busy, restart the server on build/ccbridge and resume each peer's newest session. Until then: answer a finished turn within a minute, resume with `resume: true` when a peer is gone, and ask peers to keep the turn open while their own job runs.

## gm: waste findings for the gm skill (dispatcher, 2026-11-10 09:10 UTC)

Send on gm's idle event, after turn 2c775d20e63a reports. Fold into gm's Phase A for the skill change, not a new pass. The findings are from this session:
- gm's one-read-per-run habit: the skill is read again after each compaction; the read is cheap, but a run that reads the skill and then the verbs list separately costs two turns. Put the verb list and the rules in the same read.
- Batch gm verbs into one run_code body per phase; where a run made one round trip per verb, change the skill to group them.
- A witness that needs a running browser on 10221 checks the port once, not per step.
- Duplicate status reads in one run (state, then state again to confirm) are waste; one read per phase.
Then remove this section.
