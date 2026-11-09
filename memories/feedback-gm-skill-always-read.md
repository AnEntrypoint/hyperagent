---
name: gm-skill-always-read
description: "owner rule: every peer reads skill://gm/SKILL.md as its first action in every session; the gm MCP server enforces it"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T21:21:28.905Z
---

Every peer agent uses the gm skill, always. Its first tool call in every session is a read of `skill://gm/SKILL.md`.

**Why:** the owner asked whether all agents use the gm skill. Journal review on 2026-11-09 found that they do not. Across the transcripts, gm read it in 5 of 6 sessions, jayzee in 5 of 6, oscraft in 14 of 62, spoint in 7 of 9, fx in 4 of 4, savi in 3 of 6 and mikrotik in 6 of 9. Several first reads came after more than five other tool calls.

**How to apply:** the rule is written in the gm skill's Validation section, and gm was asked (2026-11-09) to make the gm MCP server refuse `run_code` until the skill is read in that session. Every dispatch to a peer should name the read as its first step. The orchestrator re-checks the first-read position in each peer's transcripts after each cycle.

Related: [[feedback-validation-debug-first]].
