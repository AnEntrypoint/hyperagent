---
name: kill-by-pid-only
description: "owner rule: peers stop processes by their own PID only; never pkill, killall or pkill -0"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T21:21:30.826Z
---

Peers stop processes by their own PID only: read the PID, then `kill <pid>`. They never run `pkill`, `killall`, or `pkill -0`.

**Why:** this is a standing owner rule. Transcript review on 2026-11-09 found executed `pkill` commands in gm (1), oscraft (several), spoint (several), thebird (several), savi (1) and mikrotik (several). Some matched patterns broadly, such as `pkill -f "/tmp/browser-check/progress.mjs"`, which could kill processes the peer did not start.

**How to apply:** the gm skill's Validation section carries the rule. Every dispatch repeats it. The orchestrator checks peers' audit reports for kill commands before each push cycle.

Related: [[feedback-gm-skill-always-read]], [[feedback-validation-debug-first]].
