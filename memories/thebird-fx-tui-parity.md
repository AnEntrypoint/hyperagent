---
name: thebird-fx-tui-parity
description: "owner report (2026-11-10): thebird is missing most of fx's TUI; thebird must show fx's full TUI in the browser, witnessed screen by screen; queued until thebird's current turn ends"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T07:25:03.812Z
---

Owner (2026-11-10): "it looks like thebird is missing most of fx's tui we must fix that".

**Where it stands:** thebird's fx builtin runs the wasm term build (`fx/fx/src/wasm_term_main.zig`, selected by `-Dwasm-surface=term` in `fx/fx/build.zig`), which imports the same `main.zig` as the native binary. The TUI modules are in `fx/fx/src/ui/` (approval_screen, ask_presentation, assistant, event_loop, footer, full_transcript_screen, input, render, render_engine, resize_runtime, shell_runtime, terminal, transcript). The gap is in what the browser build compiles out, stubs, or does not provide as a terminal (size, colour, cursor, keys, alt screen). Cause not yet found.

**How to apply:** thebird's current turn (35b4b2408fe0, fx reproduction) finishes first. Then send the prompt in [[dispatch-ready-prompts]] (section "thebird: fx TUI parity") and remove that section once it lands. It goes ahead of thebird's remaining queue and folds into its open Phase A.

Related: [[thebird-npm-npx-pending]], [[fx-superagent-view-matches-hyperagent]].
