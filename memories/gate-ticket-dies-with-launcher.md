---
name: gate-ticket-dies-with-launcher
description: "troubleshooting: a gate ticket that vanishes from the queue without running was started attached to a shell, subagent or session that ended"
metadata:
  type: project
---

A gate ticket is a live `gate-ticket.cjs run` process. If it was started with a plain `nohup … &` from a subagent or a gm job, it dies when that shell ends, and the ticket disappears from `gate-ticket.cjs list` with no result file (seen 2026-11-22 with the hyperagent-ship ticket, PID 1323160).

**Why:** the gate removes a ticket whose owner PID is dead, so a dead launcher looks the same as a finished window.

**How to apply:** start tickets with `setsid nohup node scripts/gate-ticket.cjs run <project>-<part> -- <cmd> > <result file> 2>&1 < /dev/null &`. When a ticket is missing, check the result file for the `_DONE` line before assuming it ran; no line means it died and needs one re-queue.
