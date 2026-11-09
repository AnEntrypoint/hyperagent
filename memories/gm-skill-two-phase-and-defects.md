---
name: gm-skill-two-phase-and-defects
description: "Owner direction (2026-11-08): the gm skill must carry the per-peer two-phase workflow, and gm has five open defects that were dispatched to the gm project"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-08T21:33:20.761Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner decision (2026-11-08): two-phase work is per peer. Each peer makes its own changes in Phase A (no builds, tests, gates, witnesses or browser runs, local commits, entries in its `docs/PENDING_VALIDATION.md`). It then runs its own Phase B (build, witness, gate, fix, push). Peers do not wait on one another.

Owner direction: gm must carry this workflow in its skill, so it runs it for every peer task.

Defects reported by peers and dispatched to the gm project (`/workspace/gm`):
- `gm.read_file` silently truncates files over about 19.6 KB. Found by jayzee, which lost tail content in `PENDING_VALIDATION.md`.
- `run_code` has no `require` (jayzee).
- `skill://gm/SKILL.md` cannot be read through the host Skill tool. The research agent had to use `run_code` instead (invariant 1 incident).
- `next_context` returned another run's task (fx).
- The first step of a large task returns too much output (jayzee).

**Why:** the owner wants every peer's work to run in the same phased order, and the gm worker is the layer that every peer task passes through.

**How to apply:** when a gm incident is found, route it to the gm project as an assignment, not a local fix. Check the reply for the owner's workflow in the gm skill before dispatching gm-driven peer work again.

Related: [[feedback-two-phase-work]]
