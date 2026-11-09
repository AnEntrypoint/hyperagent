---
name: pw-project-browser-isolation
description: "Owner report (2026-11-09): thebird and jayzee share tabs in one browser, so gm's playwriter setup does not separate projects. Cause points at gm's single playwriter server; pending dispatch to gm."
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T00:06:23.912Z
---

Owner report (2026-11-09): thebird and jayzee shared tabs in the same browser, so the `../pw` setup does not separate projects when used from `../gm`.

**What `../pw` does (read at pinned commit 1488a92):** `bin/playwriter-project` picks its project root as the nearest ancestor with `.playwriter`, else with `.git`, else the cwd (`PLAYWRITER_PROJECT_DIR` overrides it). Each root gets its own gate, `.playwriter/port`, and browser. So a server started from each project's own directory is already isolated.

**Likely cause in gm:** `/workspace/gm/defaults/mcp.json` starts one `playwriter-project` server for the whole gm process, with its cwd fixed to the gm side. Every worker, whatever project it belongs to, calls `gm.playwriter_execute` through that one server, so all of them land on one project root and one browser. `gm.playwriter_browsers` close is also scoped to that root. `skills/gm/SKILL.md` says every worker shares one browser context. This matches the tab sharing the owner saw. It is not yet reproduced: thebird and jayzee have not reported their browser pids or debug ports.

**Correction (owner, 2026-11-09):** profiles, not ports, separate browser sessions. Starting each browser in its own profile path is what makes it work. `bin/playwriter-project` sets that path to `<project root>/.playwriter/browser-profile` (`--user-data-dir`, line 322), and it refuses to start a second browser on a profile that a live pid holds (line 318). My earlier note that ports were the separator was wrong, and the witness below uses profiles.

**Decision (dispatcher):** fix in gm, as a defect. The calling run's project root (from its cwd) selects its own playwriter gate and browser, so each project's browser starts on its own profile path. One project's tabs are never visible to another. Witness: two projects each run `gm.playwriter_execute`, and the report shows distinct `--user-data-dir` profile paths, distinct browser pids and distinct page sets.

**Status:** pending. Gm is busy on the live tab view and tab ownership (turn 79c1bd4a3c88, Phase B witnesses). Deliver this as the next gm item, folded into that pass if its Phase A is still open, otherwise as the next Phase A.

Related: [[gm-live-state-compact-output]], [[gm-skill-two-phase-and-defects]]
