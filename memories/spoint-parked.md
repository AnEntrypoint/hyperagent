---
name: spoint-parked
description: "owner direction: spoint is wrapped up and stopped (2026-11-10, push 5f43c86c); do not resume until the owner asks; open items and the withdrawn no-background line to correct on resume"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T06:25:39.797Z
---

Owner (2026-11-10): "after the next step lets wrap up spoint and stop it for now". spoint finished its Phase B, pushed origin main 5e0836a8..5f43c86c with a clean tree, and the peer was stopped. Session 29381184-a8b0-4498-9df0-771fb4e73ead is resumable.

**State at the stop:** `npm run check` exit 0, 139 PASS, 0 FAIL. Grass revisit, occlusion and weather witnesses pass on the tree and fail on the pre-change base. The hand-over is Run 43 in /workspace/spoint/docs/PENDING_VALIDATION.md.

**Open, for a resume:**
- Crowd-dot render witness: needs a page server and a headful browser. spoint did not run it because its prompt still carried the withdrawn "do not background" line. On resume, tell spoint that line was the dispatcher's mistake and is not a rule.
- Vegetation occlusion: ez-tree needs image decoding, so the witness needs a browser page.
- NVIDIA perf-run measurement and lease-windows: need Windows (owner-held).
- GrassDecal scaling: `markScorched` rebuilds the cell map from every stamp on each call, and stamps prune only on the wall clock. spoint left it as an owner decision.

**How to apply:** do not start or resume spoint until the owner asks. Terrain and vegetation streaming work continues in jayzee, see [[jayzee-realistic-scenarios-shared-terrain]].
