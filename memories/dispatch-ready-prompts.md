---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers (spoint wrap-up and stop, thebird npm/npx with floosie and the txiki shim, jayzee next queue); send on the idle event in the same wake, then remove the sent section"
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

## thebird (owner, 2026-11-10: "we want npm and npx working in thebird")

we want npm and npx working in thebird's browser shell. They are shell programs resolved the way `node` is (web/shell-lookup.js), and they run under the txiki runtime; `node` stays aliased to txiki. You choose the design: vendor the npm CLI or a compatible subset with the registry access and filesystem it needs, pinned, with its licence notice kept as vendored third-party notices and the choice recorded in the ledger. Witnesses, through the live site with the playwriter setup: `npm -v` and `npx -v` print versions; `npm install <pkg>` installs into the shell filesystem; `npx <bin>` runs a package's bin; `node` still resolves to txiki.

Correction: an earlier dispatch said "Do not background any check or wait on a background agent". Disregard that line. It was the dispatcher's mistake, and it is not a rule.

Then, in later stretches without returning to the dispatcher: (2) if not already done, restore floosie in the gm addition by reverting only the decoupling hunks, with /workspace/floosie in place; (3) the browser txiki artifact for gm: web/vendor/txiki/tjs.wasm exports the seven shim functions of gm's native/components/quickjs/txiki/txiki_shim.h (gm_txiki_new, gm_txiki_free, gm_txiki_context, gm_txiki_loop_alive, gm_txiki_loop_turn, gm_txiki_load_builtin, gm_txiki_refresh_loop_time), with pins txiki v26.11.0, libuv dd9f081cc55a4d83e2a976848d074f28ee5f34eb, quickjs-ng 7b5f588e0f9b643e2996f291bfc861abeaceddc2 checked by the vendor pipeline; (4) every open row left in your ledger.

After sending: delete thebird-npm-npx-pending.md and its index line; update floosie-integration-wanted and gm-run-code-under-txiki-browser with the turn id.

## jayzee

we want jayzee to carry on through every reachable item of its engine ledger, stretch after stretch, without returning to the dispatcher: the remaining rows of docs/ENGINE.md that are deferred or unwitnessed, the Nanite path (visibility buffer, GPU culling, HZB) on WebGPU and WebGL2, and the static-pool witnesses the owner asked for (RAM flat over content churn; first-frame cost of a newly added model against steady frame time; VRAM within the tier budget). Any hitch is a defect. docs/ENGINE.md records each decision.

Related: [[thebird-npm-npx-pending]], [[floosie-integration-wanted]], [[gm-run-code-under-txiki-browser]], [[jayzee-web-architecture-static-pools]], [[fx-memory-format-hyperagent]].
