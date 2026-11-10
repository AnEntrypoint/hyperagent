---
name: hyperagent-cpp-airc-vendored
description: "owner direction: hyperagent is C++ only and includes airc at airc/; the Python reference is gone; airc was imported as a squashed snapshot because a git guard blocks commits carrying the owner's email"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T00:59:46.283Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): convert hyperagent to C++ and include airc in it.

**State after the conversion (pushed as e06e4a3 on origin/main):**
- The Python reference is deleted: mcp_server.py, gui/server.py, bin/journal.py, bin/monitor.py, bin/ccbridge-latest, requirements.txt. The shell wrappers are gone too: orch, watch, gui and open are native build/hyperagent subcommands.
- airc is vendored at airc/ (squashed snapshot of /workspace/airc main; its Python reference files removed). The root CMakeLists builds the ccbridge target into build/ccbridge, and HYPERAGENT_CCBRIDGE is compiled in as the default binary path. CCBRIDGE_BIN overrides it. Spawn sets CCBRIDGE_TEMPLATES to airc/templates unless already set.
- The GitHub release installer and its HTTPS client are removed.
- Parity witnessed before deleting Python: peer_status (tools/list and output, with pane_lines), the read-only MCP tools, the monitor replay on tests/fixtures/monitor/replay.json, and the GUI snapshot against its fixture.

**Why the import is squashed:** a git guard (`git-guard` in /config/.githooks) refuses any push where admin@coas.co.za appears on a commit. airc's history had two commits authored with that address, so the full subtree import could not be pushed. The squashed import keeps every commit lanmower-authored. airc's full history stays on the archived AnEntrypoint/airc repo; the /workspace/airc checkout was deleted.

**How to apply:**
- Do not re-import airc with its history, and do not rewrite commits to get past the guard. A squash or a plain copy is the path.
- Edit ccbridge code in airc/ in this repo; there is no standalone checkout.
- Still pending: the live airc server on port 8787 runs the old release binary and the watcher is the old Python process. Both switch to the built binary at the next quiet window (`build/hyperagent orch down` ends peers, so no peer may be busy).
- The local backup branch backup/pre-squash-airc was deleted.

Related: [[owner-decisions-2026-11-08]], [[feedback-two-phase-work]].
