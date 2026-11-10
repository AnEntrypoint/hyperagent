---
name: wallclock-iteration-times
description: "owner direction: peers cut wall-clock per iteration; measured waste (sleep-poll loops, per-step gm round trips, repeated builds and checks) and the advice given"
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-10T00:41:12.093Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): check how the agents achieve things and advise them on how to reach better wall-clock iteration times.

**Measured** from the Claude transcripts under `/config/.claude/projects/-workspace-<peer>/` (tool-call durations by category, all sessions, read-only; script at /tmp/wallclock/analyze.py, not kept):
- sleep-and-poll loops: fx 40 min over 53 calls; gm 69 min over 88; jayzee 32 min over 69; spoint 133 min over 70 (about 1.9 min per loop).
- gm round trips: spoint 3920 run_code and 373 next_context; jayzee 2630 run_code and 93 next_context; fx 1299 run_code; gm itself 335 next_context, 209 min.
- repeated builds and checks: fx zig build/test 76 min over 58 calls; gm cmake 37 min over 62 (reconfigures each time); jayzee npm test/check 27 min over 65; spoint npm check 18 min over 36 (three runs in one pass).
- git looked slow in transcripts (spoint 48 min over 196), but `git status` runs in about 40 ms here, so the cost is push and network time, not local git.

**Advice given** (spoint first, 2026-11-10; others when they free up):
1. Replace sleep-and-check loops with one blocking wait on the PID or a background job that notifies on exit, with a timeout.
2. Batch several gm verbs into one run_code body; read the result once.
3. Build and check once per Phase B. Keep the zig cache, skip cmake reconfigure when CMakeCache exists, and rerun `npm run check` only after the final change.
4. Record each witness's wall time in the ledger so the slow ones show next cycle.

**Why:** the wait is wall-clock the owner pays for every iteration, and the loops and round trips add up across every peer.

**How to apply:** check the next cycle's transcripts with the same categories to see whether the sleep-poll and run_code totals fall. Related: [[feedback-two-phase-work]], [[feedback-validation-debug-first]].
