---
name: feedback-1024-budget-is-achievable
description: "owner rule (2026-11-10): jayzee's 1024-entity frame budget inside 16.7 ms is required and achievable on this environment; do not treat it as a trade-off to hand back, and do not cut behaviour first"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T08:00:59.886Z
---

Owner (2026-11-10): "we really do want that budget, 1024 isnt such a tall order for the environment we're setting up".

**Why:** jayzee reported the 1024 tier missing 16.7 ms on both backends (median 24 to 28 ms) and proposed cutting solver iterations as the fix. The dispatcher had allowed a solver-config change for 1024 with a regenerated hash. The owner rejects that as the first move: the budget is the target, and the environment (multi-core host, RTX 3060, the planned job system) can carry it.

**How to apply:** the 1024 budget is a must, not a negotiable. Find the cost first (profile the step on both backends, and the job system's thread use), then use the hardware: Jolt's multithreaded job system, SIMD builds, fixed pools, sleeping bodies, and broadphase layout. Reduce solver work only after those are measured and insufficient, and then record the behaviour change in docs/ENGINE.md with its hash. Do not hand the budget back to the owner as a trade-off.

Related: [[jayzee-realistic-scenarios-shared-terrain]], [[jayzee-web-architecture-static-pools]], [[jayzee-engine-core-cpp-jolt]].
