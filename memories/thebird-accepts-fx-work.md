---
name: thebird-accepts-fx-work
description: "owner direction: thebird must accept the new fx work (vendored fx-core in web/vendor/fx, vendor-fx.mjs, fx-in-page.js) into its build and browser runtime; dispatched to thebird as its open item"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T01:11:27.379Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): "thebird must accept the fx work that's the new work we've added". Follow-up the same day: "we're also busy adding gm to thebird using another agent, it must also accept that". So thebird accepts both the fx work and the gm work that another agent is adding to it.

**What that means for thebird:** the fx work is new and thebird takes it in. Thebird vendors the fx build through `scripts/vendor-fx.mjs` into `web/vendor/fx/` (fx-core.wasm plus manifest.json with a sha256), loads it through `web/fx-in-page.js`, and records the mapping in AGENTS.md and ARCHITECTURE.md. The fx files are committed as lanmower, and the 0-FAIL gate is witnessed on the committed HEAD alone, not on a dirty tree.

**Status:** thebird was blocked on this at 01:08 (uncommitted fx hunks, no owner decision). The dispatcher decided to finish it rather than drop it. Turn b37040d22437 is running it. Follow-up to send once thebird is idle: confirm the fx work is accepted and integrated, and that the gm addition being made by another agent is accepted the same way (its files, mapping and witness), then continue its queue. The gm addition is in progress elsewhere, so thebird takes the result when it lands rather than building it.

**Why:** the owner wants thebird's browser runtime to carry the fx engine as shipped work, not as a side tree.

**How to apply:**
- Do not send a second prompt while thebird is busy; wait for its idle event.
- Fx's git repo is `/workspace/fx/fx` (the watcher reports cwd `/workspace/fx`).

Related: [[txiki-js-browser-runtime]], [[fx-parity-target-kimi-code]], [[feedback-two-phase-work]].
