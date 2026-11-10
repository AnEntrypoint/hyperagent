---
name: jayzee-web-architecture-static-pools
description: "owner direction: jayzee's engine is architected around the web's bottlenecks, RAM-stable and hitch-free; sizes resources to the device once and scales within pre-allocated pools; graphics pre-initialized so models never hitch on first appearance"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T03:26:35.645Z
---

Owner direction (2026-11-10): jayzee's engine must be architected with all the web's bottlenecks considered. RAM-stable and hitch-free operation is a must.

**Decisions the owner gave:**
- Graphics pre-initialization: every pipeline, shader variant and bind layout compiles before content is shown, so no compilation hitches three.js (or our renderer) when a model first appears.
- One art pipeline for every model that arrives on the fly. Tooling enforces the last mile: KTX2 (Basis) textures and meshopt geometry. Because of that, all models share one shader setup.
- Memory is static and preallocated wherever possible, in RAM and VRAM. Content reuses a fixed set of pre-allocated texture slots and vertex and index pools instead of allocating.
- The engine sizes its resources to the device at hand once, at startup, then scales within those pre-allocated pools. It does not grow resources dynamically and sync them in the stream.
- The dispatcher's brief generalizes the design and leaves the choice to jayzee. It must cover main thread versus workers, GC pressure, per-frame upload budgets and context-loss recovery, and record each decision in docs/ENGINE.md.

**Witnesses the owner asked for:** RAM flat over content churn; first-frame cost of a newly added model against steady frame time; VRAM within the tier budget.

**How to apply:** treat any hitch as a defect. Keep the work in docs/ENGINE.md and the asset tooling. The C++ core and Jolt stay in one preallocated wasm, as in [[jayzee-engine-core-cpp-jolt]], and [[feedback-reuse-buffers-and-variables]] applies.

Related: [[jayzee-engine-core-cpp-jolt]], [[feedback-reuse-buffers-and-variables]], [[jayzee-webgpu-vulkan-enabled]].
