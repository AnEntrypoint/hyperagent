---
name: termrover-ttfx-licence
description: "TermRover TTFX (termrover.sh/ttfx) is a third-party effect; its terms forbid copying or deriving from the app design, so it is not vendored into any project without permission"
metadata:
  node_type: memory
  type: reference
  modified: 2026-11-08T14:31:28.757Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

The owner asked that thebird use https://termrover.sh/ttfx/ as a dynamic loading screen for the one-file brotli package. The page is TermRover's marketing demo: a Matrix field and a LaserEtch hero, with a "WASM_ENGINE: LOADING TTFX 0.3.2" line. Its scripts are `/assets/index-*.js` and `/assets/ttfx_bg-*.wasm`.

The terms at https://termrover.sh/terms say the app, its name and its design are owned by TermRover. Users may not "copy, modify, or create derivative works of the App" except as open-source components allow. The page has no licence file, and no open-source licence is named for the TTFX code.

**Why:** vendoring the wasm or JS into thebird's public Pages site would copy TermRover's design without permission. Hot-linking it from termrover.sh would still use their assets and make thebird depend on their uptime. Neither is authorised.

**How to apply:** do not copy, vendor or hot-link TermRover TTFX code or assets into any project. A loading screen must be original, or wait for TermRover's written permission. Ask the owner before reopening this.

Related: [[txiki-js-browser-runtime]]
