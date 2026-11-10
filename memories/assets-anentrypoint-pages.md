---
name: assets-anentrypoint-pages
description: "reference: https://anentrypoint.github.io/assets/ serves 934 streaming-cluster GLBs in 20 categories with thumbs, indexed by manifest.json; the repo AnEntrypoint/assets on main holds only docs"
metadata:
  node_type: memory
  type: reference
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T08:10:00.000Z
---

The owner pointed at https://anentrypoint.github.io/assets/ as a copy of the assets (2026-11-10).

**What is there:** `manifest.json` lists 934 assets in 20 categories (Outdoor 172, Vehicles 107, Furniture 91, Electronics 49, Industrial 51, Medical 55, Urban 52, Inside 80, Weapons 42, and others). Each entry has `name`, `path` (`streaming-cluster/<Category>__<name>.cluster.glb`) and `thumb`. Every listed path is a `.cluster.glb`. A sample (Outdoor, 371,820 bytes) served as `model/gltf-binary` with HTTP 200.

**Why it matters:** the repo AnEntrypoint/assets on main holds only `docs/lib/webjsx/factory.js` (the repo reports about 2.4 GB, but its only branch is main and it has no releases). The models are on the Pages site, not in the repo.

**How to apply:** fetch `https://anentrypoint.github.io/assets/manifest.json`, then the `path` entries under the same base. Use it as a second source after hy3d, oscraft and spoint for witness scenes. Record which entries each witness used.
