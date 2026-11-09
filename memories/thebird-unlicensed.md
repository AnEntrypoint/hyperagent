---
name: thebird-unlicensed
description: "Owner decision (2026-11-09): thebird is unlicensed. Its own license claim is removed; third-party vendored licence notices stay. Licence questions are deferred."
metadata:
  node_type: memory
  type: project
  modified: 2026-11-08T22:10:16.207Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner statement (2026-11-09): "thebird is unlicensed, remove its license text, that's a concern for later".

**Why:** thebird is the owner's own project, not licensed for reuse. The only licence claim for thebird itself is `"license": "MIT"` in `package.json`, which is wrong for an unlicensed project.

**How to apply:**
- Remove the `"license": "MIT"` field from `package.json`. Do not add a LICENSE file for thebird.
- Keep every vendored third-party licence notice: `web/vendor/brotli/` (Apache-2.0 and MIT), `web/vendor/tte-js/` (MIT, with its LICENSES folder), `web/vendor/xterm/`, `web/vendor/acorn.mjs` and `web/vendor/isomorphic-git.mjs`, with `ACORN-LICENSE` and `ISOMORPHIC-GIT-LICENSE`. Those are required by their own terms, and `scripts/lint-comments.mjs` checks them.
- Licence questions beyond that, including the Pyodide bundle licence, are deferred. The owner said the licensing concern is for later.
- Peers do not add licence text for thebird's own code.

Related: [[tte-js-loading-screen]], [[jayzee-licence-msgpackr]]
