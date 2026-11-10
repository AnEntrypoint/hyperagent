---
name: floosie-integration-wanted
description: "owner direction: we want the floosie integration in thebird's gm addition; reverses the dispatcher's decoupling of gm-in-page from floosie"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T02:33:16.587Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): "we want the floosie integration".

**Reverses:** the dispatcher's decoupling of `web/gm-in-page.js` from floosie (thebird commit `4686dce`, with `79754fe` and `6beb1d1`). That decoupling was the dispatcher's own call to unblock the gm commit; the owner had not approved it.

**Floosie source is in place:** `/workspace/floosie` is a shallow clone of AnEntrypoint/floosie main (commit `5417a53`), the sibling checkout thebird's `scripts/vendor-floosie.mjs` expects. It has `src/browser.ts` and the `build:browser` script.

**What to send thebird, once it is idle** (it is busy on witness run 3 for the decoupled tree now; never send while busy):
- Restore the floosie import in `web/gm-in-page.js` and the builtin wiring, by path. Revert only the decoupling hunks; keep the gm core and skills vendoring from `4686dce`.
- Vendor floosie with `FLOOSIE_REPO=/workspace/floosie node scripts/vendor-floosie.mjs`, so `web/vendor/floosie/` is built from floosie main and not hand-copied.
- Commit the floosie files and the restored import as lanmower only, no trailer, with each change in `docs/PENDING_VALIDATION.md`.
- Witness run 4 once on the clean tracked tree, fix every defect, push origin main once.

**Why (dispatcher call, 2026-11-10):** thebird benefits from floosie. floosie's browser bundle ships `createBrowserStdio`, which routes newline-framed rpc chunks to named endpoints. That is exactly the stdio MCP server gm-in-page runs in the browser. Without it, the decoupling duplicated that framing by hand, and the tracked `web/stream-hub.js` already carries a partial copy of the same codec. Cost: one pinned dependency, built by the vendor script from a floosie commit recorded in its manifest. floosie is pre-1.0 (0.7.9), so the pin matters, and its bundle size is not yet measured.

**How to apply:** do not revert the whole `4686dce` commit; the gm vendoring is accepted. Restore only the floosie-dependent lines.

Related: [[thebird-accepts-fx-work]].
