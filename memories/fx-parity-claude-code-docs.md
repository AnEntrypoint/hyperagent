---
name: fx-parity-claude-code-docs
description: "owner direction: fx parity extends beyond kimi-code to the Claude Code documentation; fx runs a deep research agent over those docs to collect parity issues"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T21:56:44.690Z
---

Owner direction (2026-11-10): fx should match the Claude Code documentation as well as the kimi-code reference, so that everything in the Claude Code docs is covered. The owner asked fx to start a deep research agent that traverses the Claude Code documentation and collects further parity issues.

**Why:** kimi-code (`/workspace/refs/kimi-code`, commit `eb2035f`) is the reference for behaviour, but the owner also wants fx to match the documented Claude Code surface. The documentation has no local copy in this workspace, so the research has to read it from the web.

**How to apply:**
- The research pass is read-only. It writes a list of parity issues with each doc page's URL, the exact documented behaviour, and fx's current behaviour with file:line. It changes no code.
- Each issue goes into the same parity matrix as the kimi-code rows, marked with its source (kimi-code or Claude Code docs).
- The issues then feed a later Phase A. Use the validation policy: debug live first, local build only, no hosted CI.
- Related: [[fx-parity-target-kimi-code]], [[feedback-build-locally-avoid-actions-limits]].
