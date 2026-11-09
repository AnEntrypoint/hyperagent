---
name: gm-live-state-compact-output
description: "Owner direction (2026-11-09): gm must show agents live state (open browser tabs, run state) continually and present compact, automated output, instead of peers being told everything in prompts"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-08T23:44:06.308Z
---

Owner direction (2026-11-09): agents will not handle browser work properly unless gm keeps open browser tabs in front of them. Instead of prompts that spell out every expectation, gm should present the agent with the information that makes the necessary choices easy.

**Why:** agents act on what is in their context. A tab they can't see is a tab they forget, so the tabs pile up. Long instructions in prompts cost token and turn bandwidth that gm should be saving.

**How to apply (for gm work, and for any dispatcher prompt to gm):**
- gm keeps the current browser state in view for the agent on every step: which tabs are open, which run or session owns each, and which are safe to close. Each run only ever sees and closes its own tabs and its own browser context.
- gm presents the state through automation, not prose in the agent's prompt: combine related fields, omit anything the agent does not need (dates, absolute paths, filler, structural headings), and send a delta when nothing changed.
- Minimise turn bandwidth: one reply per step that carries the choice and its inputs, not a restatement of rules the agent already has.
- The dispatcher's assignments to gm should name the outcome and the done condition, not spell out every expected behaviour.

Related: [[gm-skill-two-phase-and-defects]], [[feedback-decide-maximum-yield]]
