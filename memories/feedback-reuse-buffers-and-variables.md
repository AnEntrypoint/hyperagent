---
name: reuse-buffers-and-variables
description: owner rule: spoint, jayzee and oscraft reuse buffers and variables instead of allocating new ones, for stability; a standing tenet for all three
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T19:08:44.827Z
---

Spoint, jayzee and oscraft should reuse existing buffers and variables instead of creating new ones on each pass. This is for stability, and it applies across each project, not only at the hot spots already known.

**Why:** the owner wants fewer allocations in running code, so frame, tick and message paths do not churn memory or trigger collection pauses. Jayzee already holds this as a core tenet (one preallocated wasm), so it should need no instruction there; spoint and oscraft need the same rule stated.

**How to apply:** when dispatching these peers, ask for a read-only audit first: list each allocation in a per-frame, per-tick or per-message path (new arrays, typed arrays, objects, closures, strings built in loops), with file and line. Then propose a reuse for each (a pooled or preallocated buffer, a cleared-and-refilled array, a hoisted variable). The changes go in one Phase A pass like any other work, and Phase B validates them. Do not start that Phase A while another Phase A is open or a validation is running. Record each site and its reuse in the peer's docs/PENDING_VALIDATION.md.
