---
name: feedback-two-phase-work
description: "Owner rule (2026-11-08): peers make all remaining changes first with no builds, tests or witnesses, then validate, fix and push in a second phase"
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-08T21:33:09.488Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner instruction (2026-11-08): for the rest of the work, make all the changes first without building, testing or validating. Then go over building, validating and fixing once all the changes are done, so the fixing happens in one pass.

**Why:** running builds and gates between each change made each cycle slow, and the owner wants every change in before any of them are checked.

**How to apply:**
- Phase A: code, docs and tests only. No builds, npm test, gates, witnesses, browser runs or emsdk builds. Each change is committed locally and listed in `docs/PENDING_VALIDATION.md` in that peer's repo, with the file, the row or phase, and the witness it needs.
- Phase B: build, run every listed witness, fix what fails, then push to origin main.
- Nothing is pushed during Phase A. This replaces the usual push-after-each-verified-item rule for that stretch only.
- The owner's instruction must be written into each peer's brief, because peers do not see this memory.
- The dispatcher, not the peer, edits `docs/ENGINE.md` in jayzee.
- Peers that were already running a gate or Pages build when Phase A began may finish that check, since it is validation of earlier work.
- Owner confirmed (2026-11-08): the phases are per peer. Each peer starts its own Phase B as soon as its own Phase A is done. It does not wait for the other peers. Peers are not held for each other.
- The gm skill must carry this workflow so gm runs it for every peer task. Dispatched to the gm project on 2026-11-08 (see [[gm-skill-two-phase-and-defects]]).

Related: [[jayzee-engine-core-cpp-jolt]]
