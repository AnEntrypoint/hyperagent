---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers (spoint wrap-up and stop; fx superagent crash and subagent misuse, owner report); send on the idle event in the same wake, then remove the sent section"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T06:03:19.240Z
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

Related: [[floosie-integration-wanted]], [[gm-run-code-under-txiki-browser]], [[jayzee-web-architecture-static-pools]], [[fx-memory-format-hyperagent]].
