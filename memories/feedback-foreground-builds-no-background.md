---
name: feedback-foreground-builds-no-background
description: "owner report: the fx app's CLI under test suspends mid-run and the owner has to type fg; the cause is unconfirmed (Ctrl-Z is one suspect). This is about the app, not the fx peer agent"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T05:10:49.282Z
---

Owner correction (2026-11-10): the foreground/background stall was in the fx app, its CLI build and test runs, not in the fx peer agent. The earlier note blaming fx's agent for running checks through background agents was my misattribution and is wrong. The owner also says the cause is not confirmed; a Ctrl-Z-like suspend is one suspect.

**Why:** the owner has to type `fg` to resume the app's CLI when it suspends during a build or test. Foreground checks are kept as a precaution until the cause is known.

**How to apply:**
- Keep build, test and CLI-run checks in the foreground of the peer's own turn, with the shell timeout set to cover them (up to 600000 ms per call). Split a longer check into sequential foreground calls.
- Do not describe this as a peer-agent problem in dispatch prompts or reports. Name it as the fx app's CLI suspending mid-run, with the cause unconfirmed.
- The owner has parked the Ctrl-Z investigation; pick it up only if it recurs.
- Recorded in the orchestrate skill's wall-clock rules.

Related: [[feedback-two-phase-work]], [[feedback-wallclock-iteration-times]].
