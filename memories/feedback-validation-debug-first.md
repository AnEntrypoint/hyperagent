---
name: validation-debug-first
description: "owner rule: validation is as cheap as possible; debugging and live troubleshooting before tests; no unit tests in the iteration loop; HMR and live debugging for quick iteration"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T21:21:27.019Z
---

Validation should take as little time as possible. Prefer troubleshooting and debugging over tests, always. Unit tests slow every iteration, so they stay out of the loop. Use HMR for quick iteration and live debugging to find causes.

**Why:** the owner saw unit-test runs eat the time of each fix cycle across peers (jayzee ran about 314 npm test commands, oscraft about 691 test-style commands, spoint about 2199 test-style commands, measured from peer transcripts on 2026-11-09). The owner wants the fix loop short.

**How to apply:** the policy is codified in the gm skill's Validation section (dispatched to gm 2026-11-09). Peers reproduce the one failing path and debug it live, use HMR or a running dev server, and run the targeted witnesses once per push in Phase B. Any new check must justify itself against a live probe. The orchestrator audits each peer's validation time with a read-only audit before changing its policy further.

Related: [[feedback-gm-skill-always-read]], [[feedback-two-phase-work]], [[feedback-kill-by-pid-only]].
