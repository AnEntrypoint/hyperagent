---
name: kaggle-cli-access
description: Kaggle CLI is installed and authenticated as heclgang (OAuth); usable to run jayzee's benchmarks on Kaggle kernels
metadata:
  type: reference
---

The `kaggle` CLI (2.2.4, /data/cache/bin/kaggle) is authenticated through OAuth for the Kaggle user `heclgang` (config at /config/.kaggle). Read calls work: `kaggle kernels list --mine` and `kaggle kernels status`, and `kaggle kernels pull` of the existing `heclgang/dawdreamer-kaggle-probe` kernel (a Python smoke test that reports Python version, CPU count and memory; status COMPLETE).

jayzee's bench is Node (`npm run bench -- 2000 200`, scripts/bench.mjs, CPU world step), so a Kaggle CPU kernel is the natural target. Node availability on Kaggle is unverified. WebGPU runs need a browser and are not covered by that path.

**Why:** pushing a kernel uploads the code to the owner's Kaggle account, so a push needs the owner's go-ahead first.

**How to apply:** before any `kaggle kernels push`, confirm with the owner which code goes up and whether it is private. Read-only status and pull calls need no confirmation.
