---
name: owner-decisions-2026-11-08
description: "Owner answers on 2026-11-08 to the open questions: stripped Pyodide for thebird, thebird UI items implemented, wireweave primary for jayzee, savi documents spawn, fx driven to completion"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-08T14:42:19.476Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner decisions (2026-11-08), recorded so later sessions don't reopen them:

- **Thebird Python:** keep a stripped Pyodide (the pyodide-minimal bundle), not MicroPython and not dropping Python.
- **Thebird UI:** implement the paste, key-bar, battery and resize items, using the defaults I proposed.
- **Pyodide-minimal in thebird:** thebird loads the bundle, behind the tte-js loading screen.
- **Oscraft modulepreload fix:** approved. The GP-CHT-15 repick window and the rank 10 pages-scout gate are left to oscraft to decide.
- **Jayzee socket message token and live server (row 48):** wireweave is the primary goal; CLI and websocket are secondary.
- **Dropped jayzee rows 403, 469 and 694:** confirmed dropped.
- **Stuck zig build PID 1000346 (fx):** killed by the owner's instruction.
- **Savi:** not for improving savi. It documents spawn, and spawn and savi exist only to instruct jayzee on the bottom line. Jayzee must implement it. OPEN-001's vocabulary value is decided by savi.
- **Hyperagent uncommitted work:** the progress is ours, so it was committed (`03a011c`), not deleted.
- **fx:** driven to completion. Use gh Actions to build when that saves time, otherwise build here.

**Why:** these answers ended the held list in the owner report; they are the basis for the next dispatch.

**How to apply:** do not reopen these questions. Items 12 to 14 of the earlier list were not answered by the owner: OPEN-024 to 026 stay held, live spawn checks stay blocked by the no-spawn-contact rule, and the stale pin-bump snapshot text stays unchanged.

Related: [[tte-js-loading-screen]], [[termrover-ttfx-licence]], [[pyodide-minimal-bundle]]
