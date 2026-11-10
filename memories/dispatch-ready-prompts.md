---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers; currently holds only the quiet-window airc server restart; send on the idle event in the same wake, then remove the sent section"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T09:58:51.630Z
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

## gm: installed-build step go-ahead (dispatcher, 2026-11-10 09:00 UTC)

gm's installed-build commits are all on origin. The install reaches every peer, so send gm the go only when no peer is busy. gm was given the waste-audit Phase A (turn 2c775d20e63a) in the meantime.

## Quiet window: restart the airc server on the rebuilt ccbridge

Peers whose turn ran longer than thirty minutes were retired within a minute or two of the turn ending (gm, spoint, jayzee and fx on 2026-11-10), and any job they had left running died with them. Cause: the hub's idle clock was last set when the prompt was submitted, so a long turn was already past the 1800 s reap limit when the dispatcher's client detached. Fixed in airc/src/server/server.cpp (detach now resets the clock) and built into build/ccbridge. The running servers (pids 884614 and 931565, the older .orch/ccbridge/v0.1.0.4 binary) still have the defect. When no peer is busy, restart the server on build/ccbridge and resume each peer's newest session. Until then: answer a finished turn within a minute, resume with `resume: true` when a peer is gone, and ask peers to keep the turn open while their own job runs.

## fx: superagent carries the dispatcher's habits (dispatcher, 2026-11-10)

Owner (2026-11-10): make sure the up-to-date orchestrate habits are built into fx's superagent instructions. Send on fx's idle event, after turn 93efeb13142f reports. Fold it into fx's open pass; do not start a second one. Leave fx's other uncommitted changes as they are until its current work ends.

Send: we want fx to take the superagent instruction change now. Phase A is code and docs only, committed locally as lanmower with no trailer, listed in docs/PENDING_VALIDATION.md, nothing pushed.

1. src/core/superagent/command.zig, system_prompt: add the wall-clock habits in general terms. Each root's next prompt is written before its turn ends and goes out on its turn-end event. A turn's end is read from the event, not asked for again. A root whose turn ends while its own job still runs is not idle, and its next prompt carries that job's result. One state read per decision, not per cycle. A change that touches every root waits until no root is busy. Blocked work is driven to unblocking while other roots take reachable work. The superagent keeps a dispatch ledger of what is sent and queued, updated at each send and each turn end.
2. The same prompt says that the owner's standing rules are the owner's words and go into every queue verbatim: two-phase work (Phase A code only; Phase B build, witness, one push), commits as lanmower with no trailer, stop processes by PID only, treat issues as defects, decide reversible choices and report them. Reword "add no rules of your own" so it separates the owner's standing rules, which pass through, from the superagent's own rules, which are none.
3. .fx/skills/superagent/SKILL.md: state the same habits in its Cycle section, in the same general wording.
4. docs/superagent.md: in "The superagent's view of a root", say that the ledger lives in the superagent and that a turn-end event carries the final response.

Phase B: build fx once; run the live superagent witness (start one root, instruct it, receive its turn-end event, read status once) and quote the output; fix every defect; push origin main once. Do not add a test that asserts the prompt's wording.

Standing lines: work only in /workspace/fx on main, in the shared checkout. Commit as lanmower only, with no trailer. Stop processes by their own PID, never pkill or killall.

End with REPORT: finished|open|blocked — <summary> — witness: <output evidence>.

## gm: installed-build step go-ahead

gm's installed-build commits are on origin, and its skill habits are pushed (4101e4a). The install reaches every peer, so send gm the go only when no peer is busy.

## fx: opencode template (dispatcher, 2026-11-10 11:20 UTC)

Owner (2026-11-10): build a working opencode template for hyperagent, then get it in fx too. The hyperagent side is done and pushed (airc `opencode` template and `opencode-sqlite` transcript kind; commit d8d44e6). fx is busy, so send this on its idle event, folded into the superagent Phase A above.

Send: we want fx to add an opencode template as part of the superagent change. Create superagent/templates/opencode.json with these fields and nothing else:
{"argv": ["opencode", "--prompt", "{prompt}"], "resume": ["--session", "{session}"], "continue": ["--continue"]}
It follows the same shape as claude.json and codex.json. Phase B: confirm the template launches opencode with the prompt, that opencode runs the turn, and quote the output. Do not copy any transcript reader into fx; fx's external templates only launch. If `--prompt` does not submit the prompt by itself, make the template submit it and record the change in docs/PENDING_VALIDATION.md.

Measured for hyperagent (airc/templates/opencode.json, for reference): ready marker "Ask anything", busy marker "esc interrupt", live turn 4.8 s, transcript `~/.local/share/opencode/opencode.db`.

## Dispatch ledger (dispatcher, 2026-11-10 11:18 UTC)

- thebird: resumed session f76f541c with the witness run 7 failures (5 checks), client pid 284260; running run 8 at 09:17 UTC on its own clock.
- jayzee: stretch 5 Phase A accepted as 0183bef; stretch 6 Phase A (camera-relative origin, precision defect from stretch 4) on turn c5ed44fb7844. Stretches 1 to 5 Phase B wait for quiet host (load 5.6 on 12 cores at 11:17).
- fx: busy; superagent pass and opencode template still queued above.
- gm: idle; installed-build go still held until no peer is busy.

- 2026-11-10 09:23 UTC: jayzee's stretch 6 turn c5ed44fb7844 ended "open" with uncommitted camera-relative work in its checkout. Redispatched as turn 6addf105c7c2 to finish Phase A in one pass (items 1 to 7 of its open report, no build, no push). Its claim that my instruction was text-only was wrong; the instruction did not say that, and the correction is in the prompt.

- 2026-11-10 09:26 UTC: jayzee finished stretch 6 Phase A as 9a51e5a (turn 6addf105c7c2). Phase A is done for stretches 1 to 6; no reachable item is left except the Phase B runs, which wait for a quiet host (load 4.3 on 12 cores, PAUSED gate limit 0.20). Next go-ahead: when fx and thebird are both idle and the load is quiet, send jayzee the one Phase B for stretches 1 to 6 together, since they are all in the same checkout and share the witnesses.

- 2026-11-10 09:28 UTC: jayzee turn 4c5681b887de is stretch 7 Phase A (light positions, Jolt body rebase, WebGPU visibility path), dispatched because the stretch 6 residuals are defects. thebird is busy on run 9 (resize split). fx busy. gm idle, install still held.

- 2026-11-10 09:31 UTC: the queued fx superagent section (with the opencode template item 5) is sent as turn 191300025bb5 and removed from this file. gm is gone (the gone event is real; its session is no longer alive), so resume it with the installed-build go when no peer is busy. jayzee busy on stretch 7 (4c5681b887de); thebird busy on run 9.

- 2026-11-10 09:35 UTC: fx ended turn 191300025bb5 at 09:30:53 with its stash held, waiting on a HEAD ACP run (pid 331025) with no monitor to wake it. Nudged as turn 5639b5d7707f to block on that PID, restore the stash, then do the superagent change and the opencode template.

- 2026-11-10 09:38 UTC: jayzee finished stretch 7 Phase A as c4685de (turn 4c5681b887de). Phase A is done for stretches 1 to 7. The single Phase B waits for fx and thebird to be idle; load is 2.65 now. fx busy on 5639b5d7707f; thebird busy on run 9.

- 2026-11-10 09:53 UTC: fx pushed 108d86e6 (superagent habits, opencode template confirmed unchanged: --prompt submits by itself). Its open items went to stretch 9 Phase A as turn 4da24d18c531: Q16, Q10, queue rows 10+, and the ACP suite (48 of 173 failing at HEAD, treated as defects). jayzee idle on the single Phase B for stretches 1 to 7. gm still gone.

## fx: resize defect (queued, now in stretch 9 as turn f4783d59ad91)

Sent 09:55: fx's stretch 9 ended open at 09:53 after one commit (faabf61a). The continuation (turn f4783d59ad91) finishes Phase A first, includes the resize defect in src/ui/resize_runtime.zig, then one Phase B. thebird's push waits on that fx push; when it lands, send thebird the go-ahead (its Phase A is committed as 854c9a8, turn 038505de3032 adds the pin, orphan-cleanup and import-contract items).
- 2026-11-10 09:56 UTC: thebird gone event was noise; thebird alive and busy on 038505de3032. fx continuation sent as f4783d59ad91.

- 2026-11-10 09:58 UTC: thebird finished Phase A as df289ff (turn f913a6b9, REPORT finished, push held). Sent its Phase B witness pass now as turn 17f01d291f51: build, full witness, gm import checks, browser cleanup line, no push. Its push waits for fx's resize fix; then rerun only the failed checks and push once. fx busy on f4783d59ad91. jayzee is gone (not alive in orch status); resume it with resume_project for the single Phase B for stretches 1 to 7 only when fx is idle and the host is quiet. gm still gone.

- 2026-11-10 10:11 UTC: fx's turn ended at 10:09:38 while it was waiting on a background PID (it was not idle; no bun or acp process was visible). Sent the continuation as turn 296f0e2c9c8a: check the PID once, read its result, then finish stretch 9 Phase A (Q16, Q10, queue rows 10+, ACP suite, resize fix), one Phase B, one push. thebird busy on 17f01d291f51 (witness). jayzee gone; resume only when fx and thebird are idle and the host is quiet (load 5.34 at 10:10).
- 2026-11-10 10:12 UTC: fx's continuation 296f0e2c9c8a ended open after 8 s with no code change (the ACP run it waited on had finished: 173 tests, 48 failures). Re-sent as turn c41e8db2734f: resize root cause first, then Q16, Q10, queue rows 10+, ACP failures grouped by cause, then one Phase B and one push. thebird busy on 17f01d291f51. jayzee still gone.
- 2026-11-10 10:13 UTC: fx's turn c41e8db2734f ended open after 31 s asking which item to take first (resize or Q16/Q10). Answered as the dispatcher: resize first (thebird is blocked on it), then Q16, Q10, rows 10+, ACP by cause, one Phase B, one push. Sent as turn 233969bef29a. thebird busy on 17f01d291f51. jayzee still gone.
- 2026-11-10 10:14 UTC: thebird's turn 17f01d291f51 ended at 10:13:08 while its witness (node PID 386507) was still running, so it was not idle. Continuation sent as turn bbd37ce81f25: block on the PID once, quote the results, fix own-item failures, hold the push. fx busy on 233969bef29a (resize first). jayzee still gone.
- 2026-11-10 10:16 UTC: thebird finished (turn bbd37ce81f25; local commits df289ff and 2c8bb69, push held). Witness 183 PASS, 1 FAIL: the fx resize check (settled:false after palette use). Four gm import checks pass; browser cleanup line present; lint clean. Idle now. Its rerun of the resize check and its one push wait for fx's push. fx busy on 233969bef29a.
- 2026-11-10 10:20 UTC: owner asked to install the new gm now. The installed build was 6582078-339, older than HEAD 4101e4a. gm resumed on session 526213ff and sent turn ee1fa83eadcd: build at HEAD, install once, installed-build check. fx busy and thebird idle; the install does not wait for them, per the owner's direct request.
- 2026-11-10 10:19 UTC: fx ended turn 233969bef29a open at 10:17 (1c5397de local; the rapid tui-resize test fails with "no such window" at tmux creation; Q16, Q10, rows 10+ and ACP not started) and asked whether to read createResizeSession first. Answered yes as turn 152896d3684b: read createResizeSession, fix the launch, rerun only the rapid test, then Q16, Q10, rows 10+, ACP by cause, one Phase B, one push. thebird ended 10:15 finished (183 PASS, 1 FAIL: fx resize); it now has the fx TUI parity work (Phase A, folded into its open pass, no build) as turn 824cfe797659; its Phase B waits for fx's push. gm busy on the install (ee1fa83eadcd).
- 2026-11-10 10:20 UTC: gm finished the HEAD install (turn ee1fa83eadcd): ~/.gmnew/current now reports 4101e4a-1 (4101e4a); previous build kept at /config/.gmnew/previous-6582078-339; registration unchanged; record commit 5c4bbd8 local, not pushed (Phase B). Open: the next published release check could replace this local build. gm idle. fx busy on 152896d3684b; thebird busy on 824cfe797659.
- 2026-11-10 10:29 UTC: fx fixed the rapid tui-resize window failure (4ec73ced: target the session by name) and the rapid test passes 1 of 1; it is running Phase B on turn 152896d3684b. thebird ended its TUI parity turn 824cfe797659 open at 10:20 (d36bb78 local; ghostty-vt.wasm and WTerm are not in any checkout). Decided: keep xterm.js as the VT surface and revisit only on a parity gap. Sent turn 1c8c2af7d58a: finish Phase A (stub gateway streamed turn and tool call, checks in PENDING_VALIDATION), no build, push held for fx. gm: idle; sent turn 6eaca3a52ad6 as Phase A to take thebird's txiki pin f716451 and fold its record commit 5c4bbd8. jayzee still gone; host load 6.96.
- 2026-11-10 10:30 UTC: thebird ended its Phase A turn 1c8c2af7d58a open-with-Phase-A-done (78e3b64, c301762 local; stub gateway streams tool calls and long replies; xterm.js kept). Two open points (tool-calls finish value, full-transcript key) sent as read-only Phase A turn 1d70091b89d8, no build or push. fx busy on the core zig test (Phase B). gm busy on the txiki pin Phase A (6eaca3a52ad6). jayzee still gone.
- 2026-11-10 10:31 UTC: gm finished the txiki pin Phase A (turn 6eaca3a52ad6; cdf218f pin change and 5c4bbd8 record, local, unpushed; the import rerun is held for thebird's push). Sent turn c465ea9b9db7: rank its reachable open items and take them as Phase A, no build or push. thebird busy on its read-only open-point turn 1d70091b89d8; fx busy on Phase B.
- 2026-11-10 10:32 UTC: thebird finished the two read-only open points (934ffee; tool-calls confirmed; Ctrl-O is the full-transcript key, escape closes). Idle, so sent turn 93fb3bab8be0: rank its open npm/npx and pin rows, take the reachable ones as Phase A, no build or push. gm busy on c465ea9b9db7; fx busy on Phase B.
- 2026-11-10 10:33 UTC: gm finished its reachable Phase A (turn c465ea9b9db7: fbdcb89 DESIGN.md -32002 wording, 747e3dd ledger closures). Its remaining rows need a build, a Windows or macOS host, fx's reply, or the owner (ship.sh bypass). Its single Phase B, with cdf218f and 5c4bbd8, waits for thebird's push and the import rerun. gm idle with nothing reachable. thebird busy on 93fb3bab8be0 (npm and pin rows); fx busy on its Phase B (152896d3684b).
- 2026-11-10 10:39 UTC: thebird finished its reachable Phase A (turn 93fb3bab8be0; b6238f1 curl and git error text on stdout, new witness checks unrun; npm defects 2, 4, 5, 6, 8 closed by evidence; the rest owner-held). Idle with nothing reachable. gm idle with nothing reachable. fx busy on its Phase B: core 7279 of 7306 pass, superagent view 2 of 2, bun acp run PID 431418 live. Monitor re-armed as bz9nid9py (bn596rvif stopped); expires around 11:09 UTC.
- 2026-11-10 10:48 UTC: fx's turn ended at 10:39:36 while the ACP run (PID 431418) was live; the PID has since exited. Continuation sent as turn 8b3efb44b814: read the ACP output once, group and fix the failures as defects, run the full tui-resize file and the rapid test once, then one push. thebird and gm wait on it.
- 2026-11-10 10:50 UTC: fx's turn 8b3efb44b814 ended open at 10:48 (ACP 111 pass, 49 fail, grouped; one decision asked). fx's resize and tmux fix is on origin main at b7b3cdd4 (fx checkout /workspace/fx/fx). Decided: advisory cautions go into the next model request (the held-caution tests assert the designed behaviour). Sent fx turn fee84747880f: product fix plus remaining ACP groups, Q16, Q10, rows 10+, one Phase B push. Sent thebird the go-ahead as turn 1ee2bd6c0930: re-vendor fx-term at b7b3cdd4, full witness and TUI screens, push its Phase A commits once. gm waits on thebird's push for the import rerun. jayzee still gone.
