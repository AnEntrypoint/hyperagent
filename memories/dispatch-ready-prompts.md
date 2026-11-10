---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers (spoint wrap-up and stop; fx superagent crash and subagent misuse, owner report); send on the idle event in the same wake, then remove the sent section"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T06:16:17.530Z
---

The orchestrate skill's wall-clock rule: each busy peer's next dispatch is written before its turn ends. On the idle event, read the peer's REPORT, adjust only what the report changes, send the prompt, and delete its section here.

Every prompt ends with the standing lines: work only in your cwd on main in the shared checkout; commit as lanmower only, no trailer or attribution line; Phase A (code, docs, tests, listed in docs/PENDING_VALIDATION.md, committed locally) then Phase B (build once, witness, batch fixes, rerun only what failed, push once, one status check at most); treat any issue as a defect; stop processes by their own PID only; wait on a job once with a timeout sized from its recorded wall time; read git state once per phase; record witness wall times; `REPORT: finished|open|blocked — <summary> — witness: <output evidence>`.

## spoint (owner, 2026-11-10: "after the next step lets wrap up spoint and stop it for now")

we want spoint wrapped up and parked. Finish the weather and snow witness you are on, and commit scripts/weather-snow-landing-alloc-witness.mjs with it. Do not start a new item. Run the one Phase B for everything committed locally (grass revisit 5ee7f8bc, its witness 1708e35c, the occlusion witness a70b3db1, the weather witness), fix what fails, and push to origin main once. Then write the hand-over into docs/PENDING_VALIDATION.md: every open item with its status, cause and next step, including groundy's correction population and the three witnesses that need hardware or Windows. Leave no process running and no uncommitted file.

After its REPORT: check `git -C /workspace/spoint status --short` and `git log origin/main..HEAD` are empty, then `stop_peer` spoint. Do not resume it until the owner asks.

## fx (owner report, 2026-11-10, about 08:00 SAST: superagent used subagents and fx crashed)

we want the superagent to work as the owner used it, and the crash fixed. This goes to the top of your queue, ahead of the ranked rows, and is folded into your open Phase A if one is open.

What the owner did, in fx v0.0.16 started in /workspace/sa (an empty directory, no `/skill superagent` typed):
1. "what superagent projects do you see?" fx answered through "1 tool call · 1 subagent · Dispatched" and listed 82 projects.
2. "lets start up nygrok and get it to combine all the features from ../sharesies so that it can do pages and terminals". fx said nygrok "isn't in the superagent project list", ran `ls` and six reads itself, then said "I'm starting /workspace/nygrok as a superagent project", showed "1 tool call · 1 subagent · Dispatched", generated for 23 s, and the process died: "Segmentation fault (core dumped) fx".

Defects to settle, each with its cause:
- The crash. Reproduce it from /workspace/sa with the same two prompts, get the stack (the core goes to apport; a debug build or a run under gdb will do), fix it, and add a witness that starts a root for a project, instructs it, reads its status and stops it without a crash.
- Subagent in place of superagent. Find whether fx called `Agent` or whether the `superagent` tool is shown as "subagent". A superagent peer is a root agent, never a subagent: the model must reach the `superagent` tool for these requests without the skill being loaded by hand, and the transcript must label it as superagent.
- Project list. /workspace/nygrok exists and is a git repository, yet it was "not in the list". Find why, and make `start` accept a project that exists beside the others.
- The dispatcher did the research itself (ls, six reads) before delegating. The superagent's job is to start the root and hand it the request; the skill text should say so.

The standing rules apply.

## gm (after its waste-fix REPORT)

we want gm's browser run_code to run under txiki.js with the project's imports, using thebird's artifact. thebird has built it: /workspace/thebird/web/vendor/txiki/tjs.wasm exports the seven gm_txiki_* shim functions (gm_txiki_new, gm_txiki_free, gm_txiki_context, gm_txiki_loop_alive, gm_txiki_loop_turn, gm_txiki_load_builtin, gm_txiki_refresh_loop_time), with pins txiki v26.11.0, libuv dd9f081cc55a4d83e2a976848d074f28ee5f34eb, quickjs-ng 7b5f588e0f9b643e2996f291bfc861abeaceddc2. Its sha256 changes once thebird adds an exit flush, so read the pin from thebird's TXIKI_WASM_SHA256 and do not hard-code today's value. Read the artifact; do not edit thebird. run_code bodies execute in the txiki context, project imports resolve through the module loader, the 120 s deadline still runs through the interrupt handler, and LeafVerbs stay as they are. Witness with a Node run and a browser run of the built site through playwriter. Then every open row left in your ledger, stretch after stretch. Also from the report: anything it left open in the waste-fix queue goes first.

The new gm binary is installed over the shared one by the dispatcher in a quiet window, never by gm.

## jayzee (owner direction, 2026-11-10: realistic scenarios, flawless vegetation streaming, one landscape dataset)

we want jayzee measured and built against real game scenarios, with vegetation and terrain streaming that never hitches, and one landscape dataset serving both collision and visuals. This goes to the top of your queue and is folded into your open Phase A if one is open.

Why jayzee exists: on spoint, terrain and vegetation streaming make the game unplayable. Flawless vegetation streaming is the most important requirement, and any hitch in it is the top defect.

1. Baseline feature set. Read the documentation in /workspace/spoint, /workspace/spawn and /workspace/savi (read only; do not edit those projects) and write the bottom-line feature set jayzee must support into docs/ENGINE.md as a checklist, each row marked supported, partial or missing. The difference from spawn's engine is that jayzee's landscapes are spherical.
2. Realistic physics scenarios. Replace synthetic body counts as the measure with scenarios taken from spoint's example games in /workspace/spoint/apps (character controller, deathrun, combat bots, lockstep RTS, boss fights and the rest): player capsules on the spherical terrain, projectiles, moving platforms, crowds, pickups. Each scenario is a repeatable witness with tick and frame budgets on every tier, on WebGPU and WebGL2.
3. Vegetation and terrain streaming. A walk and a fast fly-over across the sphere with dense vegetation from the real assets, with no hitch: frame-time p99 and worst frame inside budget, RAM and VRAM flat, uploads inside the per-frame budget, nothing compiled or allocated after the seal. Assets are in /workspace/hy3d and the AnEntrypoint/assets repository on GitHub, run through the one art pipeline (KTX2, meshopt).
4. One landscape dataset. The terrain data is processed and stored once and read by both the collider and the renderer (for example one height or patch store that feeds the Jolt shape and the vertex stream). Measure the memory and build time against two copies, and record the decision in docs/ENGINE.md; if a measured reason forbids sharing some part, record that part and the reason.

Then carry on with the earlier queue. The standing rules apply.

Related: [[jayzee-realistic-scenarios-shared-terrain]], [[floosie-integration-wanted]], [[gm-run-code-under-txiki-browser]], [[jayzee-web-architecture-static-pools]], [[fx-memory-format-hyperagent]].
