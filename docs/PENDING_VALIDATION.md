# Pending validation

- 67d7ae1 core manifest, free port, paths, config, ccbridge identity, setup and doctor — witness: Release build, ctest install_unit, tools/portable-setup-witness.cjs setup/doctor; red check: occupied port, missing parent, stale MCP environment, conflicting model override.
- 7b4c7e0 portable orchestrator paths, peer rules, gate scratch and shipped scripts — witness: node tools/portable-paths.witness.cjs, node --check scripts/*.cjs and tools/*.cjs, bash -n tools/*.sh and tests/*.sh, JSON validation; red check: hardcoded host-specific path.
- af7ece1 install_unit, setup/doctor witnesses, cmake install layout, Windows preset and release CI — witness: ctest, tools/ship-witness.sh build, tools/portable-setup-witness.cjs, Release workflow CTest and ship witness; red check: bad shipped resource, occupied port, missing parent, stale MCP config, model override, hardcoded path.
- Documentation and this ledger — witness: node tools/validate-json.cjs, node tools/portable-paths.witness.cjs, tools/window.sh; red check: hardcoded host-specific path.
- Local test fix — witness: ctest claude_channel, opencode_channel and orch_launch with an explicit ORCH_CLAUDE_MODEL; red check: a missing unit executable or an implicit server-wide model default.
- macOS release fix — witness: ctest orch_launch, idle_reap and follow_transcript with the Node timeout helper, arithmetic loops, ps-based server count and a mkdir lock; red check: BSD pgrep usage or a missing GNU timeout, seq or flock.
