---
name: jayzee-webgpu-vulkan-enabled
description: "Owner (2026-11-09): WebGPU support and Vulkan are enabled; delivered to jayzee in turn dfc2ef024f18 (resume of its interrupted Phase A), folded into its Phase B."
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T00:04:25.361Z
---

Owner statement (2026-11-09): "explain to jayzee that we've just enabled webgpu support and vulkan".

**Status:** delivered in turn dfc2ef024f18 (2026-11-10), folded into jayzee's Phase B. The timings and adapter record are its REPORT witness.

**Owner observation (2026-11-09):** the last test jayzee started ran significantly faster after WebGPU and Vulkan were enabled. This is unverified until the numbers are in. At the time of the check, the WebGPU 1024 run (build/diag/webgpu1024.json, started 02:03) had not finished and its output was empty. When jayzee's turn ends, ask it to quote the timings from that file and the matching WebGL2 run, side by side, in docs/PENDING_VALIDATION.md. Record the speedup only from those numbers.

**What to tell jayzee:** WebGPU support and Vulkan are now enabled on this machine. Chromium should expose a WebGPU adapter through the Vulkan backend, so the render witnesses can run the real WebGPU path and not only the WebGL2 fallback. Re-run the WebGPU witnesses (the 1024 tier included) and record the adapter info and backend in docs/PENDING_VALIDATION.md. If an adapter still isn't available, record its cause as a defect. Do not call it expected.

**Why:** the owner decided WebGPU via webgpu_cpp.h with a WebGL2 fallback. Until WebGPU runs here, the WebGPU path is unverified (see docs/ENGINE.md).

**How to apply:** when jayzee's turn returns, dispatch this as its next step, folded into its open Phase B if that pass is still open. Keep the instruction short: the outcome and the witnesses to run.

Related: [[jayzee-engine-core-cpp-jolt]]
