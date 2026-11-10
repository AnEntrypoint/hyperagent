---
name: gm-run-code-under-txiki-browser
description: "owner direction: gm's run_code must run under txiki.js in the browser with project imports, sharing thebird's tjs.wasm; reverses the dispatcher's no-share call"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T02:41:18.047Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): "run_code needs to be able to interact with the projects imports though to get its repl-like properties, without txiki.js its not much its just orchestration". So gm's browser `run_code` runs under txiki.js, and txiki.js is shared with thebird's browser build.

**Reverses:** the dispatcher's call that gm's browser path needs no txiki. gm-in-page's `run_code` runs in plain page JavaScript today, which is why the txiki was not needed. The owner says that is not enough.

**Shared contract** (both sides must agree on it before either builds):
- Owner of the artifact: thebird. Its browser build `web/vendor/txiki/tjs.wasm` is the one txiki for the browser. Its browser entry exports the seven gm shim functions from `native/components/quickjs/txiki/txiki_shim.h`: `gm_txiki_new`, `gm_txiki_free`, `gm_txiki_context`, `gm_txiki_loop_alive`, `gm_txiki_loop_turn`, `gm_txiki_load_builtin`, `gm_txiki_refresh_loop_time`.
- Owner of the consumer: gm. `run_code` bodies execute in the txiki context, with the project's imports resolved through the module loader (`gm_txiki_load_builtin` and the embedded-builtin prefix). The 120 s deadline still runs through the interrupt handler, and LeafVerbs stay as they are.
- Pins: txiki v26.11.0, libuv `dd9f081cc55a4d83e2a976848d074f28ee5f34eb`, quickjs-ng `7b5f588e0f9b643e2996f291bfc861abeaceddc2`, the same as the native gm build. Both vendor pipelines must check these pins.

**Blocking:** both peers are busy now (thebird on witness run 3, gm on the txiki size measurement). Send the thebird half first, because the artifact comes before the consumer. Never send while a peer is busy.

**Then:** gm builds its engine for wasm32-wasi against thebird's browser entry, and gm-in-page runs `run_code` through that wasm. Witness with a Node run and a headless-Chromium run of the built site.

Related: [[floosie-integration-wanted]], [[thebird-accepts-fx-work]].
