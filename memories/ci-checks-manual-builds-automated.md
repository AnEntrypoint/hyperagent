---
name: ci-checks-manual-builds-automated
description: "Owner policy: no automated checks or test gates in CI; checks are run by hand. Builds stay automated."
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-08T12:25:51.108Z
---

Owner decision (2026-11-08): retire automated checks and test gates in CI. Checks are done manually. Automated builds stay.

**Why:** the owner wants CI to build artifacts, not to gate work through automated tests. Test and witness workflows add CI time and noise that the owner does not want.

**How to apply:** when a peer proposes or keeps a CI workflow that only runs checks, witnesses or measurements, retire it. Keep build and deploy workflows (pages build, release build). Do not treat a removed check as a reason to block a build. The oscraft retirement of `9a17155e` is the first case, and the remaining sim, mod-host and lua-wire-size witness workflows fall under the same policy.

Related: [[tooling-build-in-github-actions]]
