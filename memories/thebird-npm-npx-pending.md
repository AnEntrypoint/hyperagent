---
name: thebird-npm-npx-pending
description: "owner direction: npm and npx must work in thebird's browser shell; queued until thebird is idle, and includes a correction to the foreground line I wrongly sent it"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T05:27:56.432Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): "we want npm and npx working in thebird".

**Current state:** thebird's browser shell aliases `node` to the txiki runtime, per [[thebird-tjs-as-node]]. `npm` and `npx` are not available in the shell (`which npm npx` finds nothing there). The package.json `engines` field asks for node >=22.

**Decision (dispatcher):** npm and npx are shell programs in thebird, resolved the same way `node` is, and run under the txiki runtime. thebird decides the design: vendor the npm CLI or a compatible subset, with the registry access and the filesystem it needs, as long as it is pinned, licence-checked against [[thebird-unlicensed]], and recorded in the ledger. Witness through the live site with the playwriter setup, not only in node.

**Witnesses:** `npm -v` and `npx -v` print versions in the live shell; `npm install <pkg>` into the shell filesystem; `npx <bin>` runs a package's bin; `node` still resolves to txiki.

**Correction to send with it:** the last dispatch to thebird included "Do not background any check or wait on a background agent". The owner asked for that line to be removed; it was my mistake. Tell thebird to disregard it. Foreground checks are not a rule for the agent.

**How to apply:** when thebird goes idle, send the instruction above as a Phase A/Phase B stretch with the correction. Then delete this file and its index line.

Related: [[thebird-tjs-as-node]], [[txiki-js-browser-runtime]], [[feedback-two-phase-work]].
