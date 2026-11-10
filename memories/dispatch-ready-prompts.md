---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers (spoint wrap-up and stop, jayzee next queue); send on the idle event in the same wake, then remove the sent section"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T05:52:09.140Z
---

The orchestrate skill's wall-clock rule: each busy peer's next dispatch is written before its turn ends. On the idle event, read the peer's REPORT, adjust only what the report changes, send the prompt, and delete its section here.

Every prompt ends with the standing lines: work only in your cwd on main in the shared checkout; commit as lanmower only, no trailer or attribution line; Phase A (code, docs, tests, listed in docs/PENDING_VALIDATION.md, committed locally) then Phase B (build once, witness, batch fixes, rerun only what failed, push once, one status check at most); treat any issue as a defect; stop processes by their own PID only; wait on a job once with a timeout sized from its recorded wall time; read git state once per phase; record witness wall times; `REPORT: finished|open|blocked — <summary> — witness: <output evidence>`.

## spoint (owner, 2026-11-10: "after the next step lets wrap up spoint and stop it for now")

we want spoint wrapped up and parked. Finish the weather and snow witness you are on, and commit scripts/weather-snow-landing-alloc-witness.mjs with it. Do not start a new item. Run the one Phase B for everything committed locally (grass revisit 5ee7f8bc, its witness 1708e35c, the occlusion witness a70b3db1, the weather witness), fix what fails, and push to origin main once. Then write the hand-over into docs/PENDING_VALIDATION.md: every open item with its status, cause and next step, including groundy's correction population and the three witnesses that need hardware or Windows. Leave no process running and no uncommitted file.

After its REPORT: check `git -C /workspace/spoint status --short` and `git log origin/main..HEAD` are empty, then `stop_peer` spoint. Do not resume it until the owner asks.

## jayzee

we want jayzee to carry on through every reachable item of its engine ledger, stretch after stretch, without returning to the dispatcher: the remaining rows of docs/ENGINE.md that are deferred or unwitnessed, the Nanite path (visibility buffer, GPU culling, HZB) on WebGPU and WebGL2, and the static-pool witnesses the owner asked for (RAM flat over content churn; first-frame cost of a newly added model against steady frame time; VRAM within the tier budget). Any hitch is a defect. docs/ENGINE.md records each decision.

Related: [[floosie-integration-wanted]], [[gm-run-code-under-txiki-browser]], [[jayzee-web-architecture-static-pools]], [[fx-memory-format-hyperagent]].
