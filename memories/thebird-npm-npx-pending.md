---
name: thebird-npm-npx-pending
description: "owner direction: npm and npx must work in thebird's browser shell; sent in turn 450f347730c5 with the foreground-line correction, floosie and the gm txiki shim artifact as later stretches"
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

**Sent (2026-11-10, turn 450f347730c5):** npm and npx, the correction, then floosie, the txiki shim artifact for gm, and the open ledger rows. thebird's previous push was 837c78c (144 PASS, 0 FAIL). thebird lists as owner-held: Pages Ctrl-C without isolation, removing web/stream-hub.js, the licence questions, the journal limit for C-extension writes, the upstream floosie esbuild declaration.

**How to apply:** read the REPORT of that turn for the four witnesses.

**Result (2026-11-10, push 79bd180):** npm and npx run on txiki as a pinned subset, version 0.1.0 (fflate 0.8.3, @noble/hashes 2.4.0, semver 7.8.6); 8 of 8 focused checks pass in headless Chromium, not playwriter. Limits: no lockfile, no .bin links, dependencies only, no auth, CommonJS bins only. Open: process.exit drops buffered output (needs a flush in scripts/txiki/browser/entry.c and a new wasm pin); the full witness was not rerun; scripts/esbuild-pin.mjs, untracked at session start, was overwritten by thebird. floosie is restored. The gm artifact web/vendor/txiki/tjs.wasm exports the seven gm_txiki_* functions (sha256 dcd3a434..., will change with the flush fix).

**Follow-up sent (turn after 450f347730c5):** real npm (lockfile, .bin, dependency kinds, ESM bins, run/ci/uninstall/ls, auth), the exit flush, playwriter witnesses, the full witness rerun, the esbuild-pin account, URLs in npm errors.

Related: [[thebird-tjs-as-node]], [[txiki-js-browser-runtime]], [[feedback-two-phase-work]].
