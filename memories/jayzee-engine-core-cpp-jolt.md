---
name: jayzee-engine-core-cpp-jolt
description: "Owner decision (2026-11-08): jayzee's core moves to C++20 with Jolt in one preallocated wasm; the preallocation invariant; docs/ENGINE.md is the contract and goes into every jayzee dispatch"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-08T19:16:20.471Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-08): jayzee's engine core moves to C++ with Jolt compiled into the same wasm, deeply integrated, with the bridge into wasm optimised because it is the bottleneck. The owner delegated the Zig-or-C++ choice to the dispatcher. The choice was C++20 with Emscripten: Jolt is C++, so no binding layer is needed, which gives the most control and least maintenance.

The invariant: a game preallocates every resource the device makes available over its lifetime at start, and reuses it. After warm-up it never allocates. Streaming content writes into slots the engine owns. Content fits the device budget, and never grows it. Terrain and vegetation must not be streamed as Jolt bodies (spoint's hitch is HeightfieldStreamer creating and removing heightfield bodies while moving). They use custom Jolt shapes over fixed page caches, so planet-wide collision is possible.

**Why:** hitches from allocation and from sync across the wasm boundary are a bigger bottleneck than calculation.

Owner rule (2026-11-08): the dispatcher manages `docs/ENGINE.md`, not the jayzee agent. Jayzee implements it and reports any part that proves wrong; the dispatcher edits the doc.

**How to apply:** the contract is `/workspace/jayzee/docs/ENGINE.md`. Every dispatch to jayzee names it and the current phase, and tells jayzee to check its work against it. The jz kernel is superseded and stays only as the reference until phases 2 to 7 are green. Builds follow [[tooling-build-in-github-actions]].
