---
name: opencode-harness-template
description: "opencode is a hyperagent harness: airc template plus opencode-sqlite transcript kind, pushed d8d44e6; fx gets the same template through its superagent pass"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T11:20:00Z
---

Owner direction (2026-11-10): build a working opencode template for hyperagent, then get it into fx.

**What exists:** `airc/templates/opencode.json` (argv `opencode`, ready marker `Ask anything`, busy marker `esc interrupt`, resume `--session`, continue `--continue`, fork `--session <id> --fork`), the `opencode-sqlite` transcript kind in `airc/src/harness/kinds/opencode.cpp` (reads `~/.local/share/opencode/opencode.db` read-only: session, message, part tables), orchestrator sessions/find/selector and pane markers in `src/orch/harness.cpp`, and `tests/unit/opencode_channel_test.cpp`. Pushed in d8d44e6.

**Measured:** a live turn through `ccbridge send --harness opencode` returned the reply in 16 s; `capture` showed the ready and busy markers; the orchestrator lists the project's sessions from the real store.

**Why:** opencode keeps sessions in SQLite, not JSONL, so its reader is a new kind in the binary, not a template-only change. Its root sessions have `parent_id` set for subagents, which the reader filters out.

**How to apply:** fx's external-harness template is queued in [[dispatch-ready-prompts]] (fx: opencode template) and folds into fx's superagent Phase A. Resume was checked against the CLI help only, not with a live resumed turn.

Related: [[feedback-two-phase-work]], [[fx-superagent-view-matches-hyperagent]].
