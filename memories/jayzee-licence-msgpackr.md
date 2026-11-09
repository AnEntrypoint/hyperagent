---
name: jayzee-licence-msgpackr
description: "Owner decision: msgpackr licence is approved for jayzee; jayzee stays UNLICENSED with no licence text in the repo"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-08T12:35:21.264Z
---

Owner decision (2026-11-08): the msgpackr licence is fine, so jayzee may use msgpackr. Jayzee stays UNLICENSED, and all licence text is removed from the repo. `package.json` already says `"license": "UNLICENSED"` and there is no LICENSE file.

**Why:** the msgpackr licence was the only thing holding PARITY row 40 (the shipped packer), row 22 and row 43 (replay dialect), rows 42 and 46 (snapshot and protocol codecs).

**How to apply:** when jayzee ports the msgpackr-dependent rows, the held-licence wording in `docs/IDEAL.md` and `AGENTS.md` is replaced with the decided state. Do not add a LICENSE file or a licence field other than UNLICENSED. The SPDX list in `src/sdk/AppManifest.js` validates apps' manifests and is not jayzee's own licence, so leave it.

Related: [[ci-checks-manual-builds-automated]]
