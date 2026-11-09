---
name: fx-image-reading-router-fixed
description: owner: fx images withheld on a custom connection was a router problem, now fixed; do not change fx image reading for it
metadata:
  type: feedback
---

Owner statement (2026-11-10): "we dont need to fix image reading in fx, appears that it was our router we fixed that."

**Why:** fx's issue 3 (images withheld on a custom connection) and the vision path looked like an fx defect. The cause was the router, which the owner has fixed.

**How to apply:** close issue 3 in /workspace/fx/issues.md as resolved outside fx, with no code change. Keep issue 4 (JPEG and WebP crop is PNG-only) recorded in the parity matrix with its limit; change code only if a live probe proves a one-line fix. Do not reopen the image path for the router problem.

Related: [[fx-parity-claude-code-docs]], [[fx-parity-target-kimi-code]].
