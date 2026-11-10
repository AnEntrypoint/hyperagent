---
name: feedback-generalizations-not-check-towers
description: "owner rule: instructions (skills, prompts, memories) are a few generalizations that anchor preferred habits, not a tower of specific checks and incident rules; less dense, no Chekhov's guns"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T06:22:20.408Z
---

Owner (2026-11-10): "our instructions must be generalizations ideally then a jenga tower of checks, rather semantic anchors of the types of habits we prefer, its verbally less dense and doesnt produce chekovs guns".

**Why:** each specific rule added for one incident is a Chekhov's gun: a reader acts on it where it is not needed, as happened with the foreground/background line. A stack of checks is dense and brittle.

**How to apply:** when waste or a defect calls for an instruction change, state the habit once in general terms (do each thing once; peers never wait on the dispatcher; keep what you carry small) and fold existing specifics into it. Do not add numbered rules, measurements, incident dates or enforcement gates to a skill. Mechanisms that must be exact, such as a command line, stay. This applies to the orchestrate skill, the gm skill and peer prompts. The orchestrate wall-clock section was rewritten this way, and gm was asked to rework its skill the same way (turn 72878d0d3b25).

Related: [[gm-skill-two-phase-and-defects]], [[feedback-wallclock-iteration-times]].
