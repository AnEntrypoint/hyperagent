---
name: dispatch-ready-prompts
description: "ready-to-send next prompts for busy peers (thebird TUI parity; holds the quiet-window airc server restart); send on the idle event in the same wake, then remove the sent section"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T07:29:47.123Z
---

The orchestrate skill's wall-clock rule: each busy peer's next dispatch is written before its turn ends. On the idle event, read the peer's REPORT, adjust only what the report changes, send the prompt, and delete its section here.

Every prompt ends with the standing lines: work only in your cwd on main in the shared checkout; commit as lanmower only, no trailer or attribution line; Phase A (code, docs, tests, listed in docs/PENDING_VALIDATION.md, committed locally) then Phase B (build once, witness, batch fixes, rerun only what failed, push once, one status check at most); treat any issue as a defect; stop processes by their own PID only; wait on a job once with a timeout sized from its recorded wall time; read git state once per phase; record witness wall times; `REPORT: finished|open|blocked — <summary> — witness: <output evidence>`.

## Quiet window: restart the airc server on the rebuilt ccbridge

Peers whose turn ran longer than thirty minutes were retired within a minute or two of the turn ending (gm, spoint, jayzee and fx on 2026-11-10), and any job they had left running died with them. Cause: the hub's idle clock was last set when the prompt was submitted, so a long turn was already past the 1800 s reap limit when the dispatcher's client detached. Fixed in airc/src/server/server.cpp (detach now resets the clock) and built into build/ccbridge. The running servers (pids 884614 and 931565, the older .orch/ccbridge/v0.1.0.4 binary) still have the defect. When no peer is busy, restart the server on build/ccbridge and resume each peer's newest session. Until then: answer a finished turn within a minute, resume with `resume: true` when a peer is gone, and ask peers to keep the turn open while their own job runs.

## thebird: fx TUI parity (owner report, 2026-11-10 07:30 UTC)

we want thebird to show fx's full TUI in the browser, the way fx's own terminal shows it. The owner's words: "it looks like thebird is missing most of fx's tui we must fix that". This goes ahead of the rest of your queue and folds into your open Phase A.

1. Diagnose by comparison: run fx in the native terminal and in thebird's terminal, and go surface by surface: the input box, footer, transcript, full transcript screen, approval screen, ask presentation, resize, colour, cursor, keys and the alternate screen. The surfaces are in /workspace/fx/fx/src/ui/. The browser build is /workspace/fx/fx/src/wasm_term_main.zig, which imports the same main.zig as the native binary. Find which modules the term build compiles out or stubs, and which terminal capabilities thebird's terminal does not provide.
2. Fix every cause in Phase A. Record each cause and its fix in docs/PENDING_VALIDATION.md.
3. Witness in Phase B: run the TUI in the browser through the playwriter setup, one surface at a time, and quote the screen text for each. Add a witness to the full witness, so a missing surface fails it.
4. The owner pointed at https://github.com/vercel-labs/fx/tree/main/examples/browser-agent. It is not a TUI: a form with a textarea that streams text_delta into the reply. Its two useful pieces are for the fx ask path. First, createFxEngine({ apiKey, model, fetch }) lets the host override fetch. Second, with no key it routes requests through a server proxy that allows only the two gateway paths, using a demo key. Check thebird's fx ask against that shape, and check whether thebird's vendored core matches libfx 0.0.16. The TUI itself is the native fx terminal in /workspace/fx/fx/src/ui/. The examples node-chat/chat.mjs (readline) and browser-agent show no footer, approval, transcript or input surface.
5. The owner also points at https://fx.sh/try, where the TUI shows. Its page runs the full fx CLI as fx-term.wasm (the -Dwasm-surface=term build, /workspace/fx/fx/build.zig:420), with output drawn through ghostty-vt.wasm as a VT emulator (GhosttyCore) and rendered by a WTerm grid. Input comes from a keydown listener and a textarea. thebird vendors only fx-core.wasm (the libfx SDK core, web/vendor/fx/manifest.json) and runs fx as line builtins in web/fx-in-page.js. That is the likely cause of the missing TUI. Compare the two, then build fx-term from /workspace/fx/fx and vendor it, with a VT surface for it in your xterm, keeping fx-core for the SDK path if it is still needed. Record the choice and the reason in docs/PENDING_VALIDATION.md, and say which surfaces it now covers.
6. Keep your fx ask path and the rest of your queue. The standing rules apply, the turn stays open while your own jobs run, and you push once per Phase B.
