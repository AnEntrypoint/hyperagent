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
