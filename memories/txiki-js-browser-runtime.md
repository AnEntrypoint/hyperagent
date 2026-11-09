---
name: txiki-js-browser-runtime
description: "Owner decision: txiki.js must run in the browser in thebird, with no exceptions; it is thebird's primary purpose"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-08T13:45:44.254Z
---

Owner decision (2026-11-08): txiki.js must be built to run in the browser in thebird, with no exceptions. It is the primary purpose of thebird; everything else exists to get txiki.js complete. This reverses the earlier rejection recorded in thebird's DESIGN.md (txiki.js was rejected for having no browser or wasm build).

**Why:** the owner's stated purpose for thebird. The earlier rejection is no longer the decision.

Owner statement (2026-11-08): "we want txiki.js in the browser thats all we care about net". The thebird UI questions (paste, key-bar, battery, resize) are deferred. They are not built until txiki.js runs in the browser.

Owner statement (2026-11-08): "we want txiki to replace all the old parts that the code use to handle in thebird, including sqlite". The old parts are the non-txiki runtime pieces: the sql.js sqlite3 builtin (`web/sqlite-in-page.js`), isomorphic-git, the curl and wget fetch builtins, Pyodide python3 and pip, and bashkit as the shell. The replacement path is txiki-first: sqlite as the C amalgamation compiled into the txiki wasm, git and curl/wget on txiki's JS and fetch, and the shell on txiki once it can host one. Python cannot be a txiki replacement because txiki runs JavaScript, so it is the one item that needs an owner decision.

**How to apply:** when thebird's plan keeps a non-txiki runtime for one of these, ask why, and do not accept a wasm side-runtime as the replacement. Python is reported as blocked, not silently dropped.

Owner statement (2026-11-08): "txiki has sqlite", "says so in the readme, has all node features". Checked: txiki's README lists `tjs:sqlite` in its standard library (source `src/mod_sqlite3.c`, `deps/sqlite3`). The README does not claim general Node compatibility, so "all node features" is not a stated claim to build on. thebird's browser link (`scripts/vendor-txiki.mjs`) compiles neither `mod_sqlite3.c` nor `deps/sqlite3`, so it has no sqlite yet.

**How to apply:** thebird's peer was told to reverse the DESIGN.md line, replace libuv with a browser event-loop shim, build to wasm with pinned zig and hashes, and witness it live through playwriter. Don't propose a JavaScript-runtime alternative (QuickJS-only, a `node` alias) as the answer. Build location follows [[tooling-build-in-github-actions]].
