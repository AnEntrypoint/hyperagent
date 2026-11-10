---
name: fx-features-added-pending
description: "owner instruction: tell fx that new features were added; queued until fx goes idle because fx was busy when the owner asked"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T02:27:14.388Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner instruction (2026-11-10): "instruct fx that we've added features".

**Status:** sent at 04:27 SAST as a resume of fx's session `afca55c2` (turn `2390203dbf1a`), after fx's Phase B landed as `ae644306` and fx went gone. The prompt carries the same features text as below.

**What to send, once fx is idle:** "we want fx to take in the features we have just added: read its ledger and issues.md for every feature added since its last Phase B, add each one as a row in its parity matrix, and fold them into its queue after the current stretch. Standing rules apply: work only in /workspace/fx/fx on main; commit as lanmower only with no trailer; Phase A then one Phase B push; end with REPORT."

**Why:** the owner added features to fx's scope and wants fx to know before its next stretch.

**How to apply:** the owner did not name the features. fx reads its own ledger to find them. If fx cannot find them there, it records the gap in its REPORT line instead of guessing.

Related: [[fx-parity-target-kimi-code]], [[fx-parity-claude-code-docs]].
