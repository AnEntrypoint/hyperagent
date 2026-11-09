---
name: no-flyio-only-wireweave
description: Owner decision: fly.io is not wanted in any project; spoint is built on wireweave and its only hosting target is GH Pages
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-08T13:37:02.557Z
---

Owner statement (2026-11-08): "we dont want fly.io at all.. its not a feature we want, we only want wireweave".

**Why:** the owner does not want fly.io as a deploy target or feature. Spoint is built on wireweave and communicates with wireweave. An earlier turn treated the fly.io room bind as an open owner question. That question is closed: the bind exists only for a fly.io deploy, so it is dropped with it.

**How to apply:** do not propose, build, or keep fly.io deploy recipes, fly.io hostnames in docs, or fly-specific bind work. Remove `deploy/fly-rooms.toml` and fly.io passages in spoint docs when touching those files. Do not treat the general `0.0.0.0` server default as a fly.io item. The `.gm/pinned` snapshots are gm-managed and are left to gm.

Related: [[tooling-build-in-github-actions]]
