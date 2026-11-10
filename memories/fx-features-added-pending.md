---
name: fx-features-added-pending
description: "owner instruction: tell fx that new features were added; queued until fx goes idle because fx was busy when the owner asked"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T02:12:11.579Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner instruction (2026-11-10): "instruct fx that we've added features".

**Status:** pending. fx was busy (core test rerun, `esc to interrupt`) when the owner asked, so the prompt waits for fx's idle event. Never send it while fx is busy.

**What to send, once fx is idle:** "we want fx to take in the features we have just added: read its ledger and issues.md for every feature added since its last Phase B, add each one as a row in its parity matrix, and fold them into its queue after the current stretch. Standing rules apply: work only in /workspace/fx/fx on main; commit as lanmower only with no trailer; Phase A then one Phase B push; end with REPORT."

**Why:** the owner added features to fx's scope and wants fx to know before its next stretch.

**How to apply:** the owner did not name the features. fx reads its own ledger to find them. If fx cannot find them there, it records the gap in its REPORT line instead of guessing.

Related: [[fx-parity-target-kimi-code]], [[fx-parity-claude-code-docs]].
