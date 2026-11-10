---
name: jayzee-nanite-macro-view-optimizations
description: "owner direction: jayzee reads the Nanite macro-view breakdown (elopezr.com) and implements every optimization that transfers to the web engine; queued until jayzee is idle"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T04:21:16.961Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): give jayzee https://www.elopezr.com/a-macro-view-of-nanite/ and instruct it to implement any transferable optimizations from that breakdown.

**Sequencing:** jayzee is mid-Phase B (validating its ranks 3 to 6 pass, push pending). This item is new work, so it is not folded into that pass. Send it only once jayzee is idle, as a new stretch: Phase A (read the article, decide which techniques transfer to the web engine, make the code, docs and tests changes, list each in docs/PENDING_VALIDATION.md, commit locally, push nothing), then Phase B (build, run every listed witness, fix defects, push origin main once).

**Decision rule:** jayzee decides which techniques transfer. Anything that needs the web's constraints (WebGPU/WebGL2, wasm memory, no mesh shaders) is adapted, not copied. Record each decision and its reason in docs/ENGINE.md. Keep the static-pool and pre-initialization rules from [[jayzee-web-architecture-static-pools]]; no per-frame allocation.

**How to apply:** when the watcher reports jayzee idle, send the instruction with the URL and the Phase A/B sequencing above. Delete this file once it has been delivered.

Related: [[jayzee-web-architecture-static-pools]], [[jayzee-engine-core-cpp-jolt]], [[feedback-two-phase-work]].
