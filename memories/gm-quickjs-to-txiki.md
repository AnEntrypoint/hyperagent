---
name: gm-quickjs-to-txiki
description: "owner direction: gm swaps its embedded QuickJS-NG for an embedded txiki.js runtime; pins taken from thebird's vendor-txiki script; dispatch waits for gm to go idle"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T01:07:42.886Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): "we want gm to swap out quickjs for embedded txiki.js".

**Pins to use** (from `/workspace/thebird/scripts/vendor-txiki.mjs`, already verified there):
- txiki.js `v26.11.0`, sha256 `763025ac98e4d6629f9ebe8396c027ff22966a6322b0dc43e442d45c6d7f8059`
- libuv `dd9f081cc55a4d83e2a976848d074f28ee5f34eb`, sha256 `f5185440ee939936d6375755498b7f07915f747bd9bbfe6ea058a9ee173f88d0`
- quickjs-ng `7b5f588e0f9b643e2996f291bfc861abeaceddc2`, sha256 `f27b2e93647e73f91201e12c4f2ee0c0d07e2f6dc06b539422bf03269d601366`

**Where gm uses QuickJS today:** `native/components/quickjs/` (engine.hpp, engine.cpp, runcode.cpp, leaves.cpp, query.cpp), the vendored `native/third_party/quickjs-ng/`, and `tasks/rewrite/quickjs.md`. The run_code body, LeafVerbs, the per-call deadline (120 s through the interrupt handler) and the module loader all sit on that engine.

**Status:** dispatch pending. gm was busy with its parity harness when the owner asked. Send the brief only when gm is idle (never a second prompt to a busy peer). The brief keeps run_code, LeafVerbs and the deadline behaviour, and their tests, unchanged.

**Why:** the owner wants one JavaScript engine, txiki.js, shared with the thebird runtime. The existing txiki.js build in thebird uses quickjs-ng inside it, so the pins above are the same engine.

Related: [[txiki-js-browser-runtime]], [[gm-skill-two-phase-and-defects]].
