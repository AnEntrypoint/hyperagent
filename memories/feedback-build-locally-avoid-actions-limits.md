---
name: build-locally-avoid-actions-limits
description: "owner rule: fx builds and validates locally; hosted GitHub Actions workflows are disabled on the fx repo because the account hit its Actions spending limit; keep API and Actions usage low to avoid rate limits"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T21:44:01.216Z
---

Fx builds and validates on the local machine. Its GitHub Actions workflows were disabled on 2026-11-09, because the account hit its Actions spending limit and the push CI and nightly runs failed before running any step.

**Why:** the owner asked why the spending limit was hit, and then asked to optimise usage to avoid rate limits and to keep building locally. This supersedes the older guidance in [[tooling-build-in-github-actions]] for fx. That note still applies to projects whose CI has budget.

**How to apply:**
- Fx: validate with `zig build test` (minimal set), the live TUI probes and the local full suite when a change needs it. Do not watch CI and do not run `gh workflow run`. The disabled workflows are CI, Nightly full unit suite, Benchmarks, Binary Size, CDN Backfill, Dev Release, Examples, Fuzz, macOS, Linux arm64 PGSO candidate, Linux x86_64 PGSO candidate, macOS arm64 PGSO candidate, TLA+ fx mcp call and Thread Sanitizer. Release and Publish libfx stay active; they run on tags. Re-enable with `gh workflow enable <name> -R AnEntrypoint/gmfx`.
- Other repos (gm, jayzee, spoint, oscraft) still run hosted CI on push. Limit pushes to one per Phase B, and do not poll runs in loops.
- Dispatchers: poll each peer at most once per wake-up, use `bin/watch` rather than repeated `peer_status`, and avoid bulk `gh api` scans of past runs.
