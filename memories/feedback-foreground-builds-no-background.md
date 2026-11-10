---
name: feedback-foreground-builds-no-background
description: "owner rule: peers run build, test and CLI checks in the foreground; a backgrounded check makes the owner type fg by hand each time"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T03:12:17.250Z
---

Owner report (2026-11-10): fx keeps backgrounding itself when testing its CLI build, and the owner recovers by typing `fg` in the pane every time.

**Why:** fx ran its CLI checks through a background `general-purpose` agent and waited on its completion notification. The wait stopped the turn, so the owner had to resume it by hand. The dispatcher's own prompts had told fx to "let the background agents finish" and to "wait on the completion notifications", which encouraged the pattern.

**How to apply:**
- Build, test and CLI-run checks run in the foreground of the peer's own turn, with the shell timeout set to cover them (up to 600000 ms per call). Split a longer check into sequential foreground calls.
- No peer delegates a build, test or CLI run to a background agent, or backgrounds its own shell job.
- Do not write "wait for the background agent" or "wait on the completion notification" into dispatch prompts.
- Recorded in the orchestrate skill's standing instructions and wall-clock rules.

Related: [[feedback-two-phase-work]], [[feedback-wallclock-iteration-times]].
