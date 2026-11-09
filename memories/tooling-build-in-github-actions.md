---
name: tooling-build-in-github-actions
description: Owner wants build tooling set up to run in GitHub Actions; anything may be installed; local builds are fine when they save time
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-08T11:06:38.892Z
---

Owner says we can install anything we need. Set tooling up to build on GitHub Actions first, so the build runs there instead of locally when that is possible. A local build is still fine when it saves real time.

**Why:** the owner wants pinned, reproducible artifacts from CI, not a machine-specific build. thebird's txiki.js wasm is the first case: zig 0.17.1 is already pinned into the Pages workflow for qjs.

**How to apply:** when a peer proposes a build or vendor step, ask for it to be a workflow step with pinned action SHAs and a checked hash. Local builds are allowed for iteration only. Don't keep a build that exists only on one machine.

Related: [[txiki-js-browser-runtime]]
