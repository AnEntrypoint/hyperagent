---
name: tte-js-loading-screen
description: "Owner decision: the loading screen for thebird's one-file brotli package is flaviocopes/tte-js (MIT), not TermRover TTFX"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-08T14:33:21.368Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner decision (2026-11-08): use https://github.com/flaviocopes/tte-js as the dynamic loading screen. It replaces the TermRover TTFX option, which was blocked by TermRover's terms.

Licence check: the repo is MIT (SPDX MIT, `LICENSE`). It is a plain JavaScript Canvas renderer with no dependencies and no WebAssembly. Its effect designs come from ChrisBuilds' TerminalTextEffects (MIT). `THIRD_PARTY_NOTICES.md` and `LICENSES/` carry the credits, and tte.js states it does not include TermRover's or Omarchy's WebAssembly code.

**Why:** a licence-clear option that gives the same effect family, with credits kept intact.

**How to apply:** vendor the `src/` folder with `LICENSE`, `THIRD_PARTY_NOTICES.md` and `LICENSES/` at a pinned commit. Keep the upstream files unmodified and keep the notices in the repo. Use an effect such as `laseretch` while the bundle downloads and decodes. Fall back to plain text when Canvas is unavailable. The repo has issues and PRs turned off, so changes go to a fork if they are needed.

Related: [[termrover-ttfx-licence]], [[pyodide-minimal-bundle]]
