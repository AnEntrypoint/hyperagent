---
name: thebird-tjs-as-node
description: "Owner directive (2026-11-08): thebird must alias tjs to node so the browser runtime looks like it has node"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-08T18:19:56.661Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner statement (2026-11-08): "thebird must alias tjs to node so that it looks like it has node".

**Why:** txiki.js is thebird's primary runtime. Scripts and tools written for node should run unchanged, so the txiki runtime is exposed under the name `node` as well as `tjs`.

**How to apply:** in thebird's browser runtime, `node` must resolve to the txiki (`tjs`) runtime, so that `node <script>` and `require`-style node calls go to txiki. Witness it live through the playwriter setup. Do not add a separate JavaScript runtime as the `node` alias; see [[txiki-js-browser-runtime]].
