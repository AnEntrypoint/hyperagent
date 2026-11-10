---
name: jayzee-realistic-scenarios-shared-terrain
description: "owner direction: jayzee tests realistic game physics scenarios from spoint's example games; spoint, spawn and savi document the baseline feature set; vegetation and terrain streaming must be flawless; one landscape dataset serves collision and visuals"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T06:12:41.898Z
---

Owner direction (2026-11-10):

- **Realistic physics scenarios.** jayzee's physics witnesses should be realistic game scenarios, not synthetic body counts. spoint has example games (/workspace/spoint/apps: character-controller, deathrun, combat-bot, lockstep-rts, final-boss and others).
- **Baseline feature set.** /workspace/spoint, /workspace/spawn and /workspace/savi hold the documentation of the bottom-line feature set jayzee must support. The difference from spawn's engine: jayzee's landscapes are spherical.
- **Assets.** /workspace/hy3d and the AnEntrypoint/assets repository on GitHub.
- **Why jayzee exists.** On spoint, terrain and vegetation streaming make the game unplayable. Vegetation streaming must work flawlessly on jayzee. This is the most important requirement.
- **One landscape dataset.** The owner hopes the landscape's data can be shared between collision and visuals, so it is not processed and stored twice.

**How to apply:** every jayzee dispatch treats a streaming hitch as the top defect, measures with the realistic scenarios, and keeps one terrain dataset for the collider and the mesh unless a measured reason forbids it, recorded in docs/ENGINE.md. Sent to jayzee as the ready prompt in [[dispatch-ready-prompts]] on its next idle event.

Related: [[jayzee-web-architecture-static-pools]], [[jayzee-engine-core-cpp-jolt]], [[feedback-reuse-buffers-and-variables]].
