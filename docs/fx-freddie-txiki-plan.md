# Freddie inside fx on txiki.js: the plan

Reference copy held by the dispatcher. It is re-sent to a peer whenever its context is lost, and deleted when the done condition at the end is met.

## What the owner wants (not negotiable)

- All of freddie's JavaScript runs inside the fx binary on an embedded txiki.js. Node.js is gone at run time: no node, npm, npx or pnpm on PATH is needed to run anything.
- Every freddie part that fx already does natively is cut from freddie and reached from freddie's JavaScript through the embedded txiki host.
- Cordis plugins are built and run inside that environment.
- The JavaScript that keeps freddie working sits on disk outside the binary, so behaviour is changed by editing files. Every JavaScript surface has in-place hot reload, so the running system can edit itself and keep repairing itself when a change goes wrong.
- The same setup is reachable from the fx command line and from thebird.
- What remains of freddie (about 115k lines once the vendored client modules and the cut parts are set aside) is cut to what it needs, deduplicated, and rearchitected: the simplest, most efficient, most maintainable code, data-driven and generated where that removes hand-written cases. No comments in code.
- Take the quickest road. Drive it until it is completely done.

## Facts this plan rests on (checked 2026-11-21)

- fx has no JavaScript engine today. fx docs/txiki-embedding.md (Stage 0) already plans one: txiki built as a static library from build.zig, a C shim, a Zig runner with gm's semantics, run_code routed through it.
- libfx's Node addon is headless by design (no native tools, no permissions). So the route is JavaScript inside the fx binary, where the native tools and the permission gate live, not libfx from Node.
- fx native coverage: model loop, streaming, providers (AI Gateway, Codex, Grok, OpenAI Chat Completions endpoints), file, search, shell, web, job, todo, skill, ask-user tools, permissions and auto review, sessions and resume, compaction, subagents and swarm, MCP client, ACP server, plugins and command hooks. Gaps: no Anthropic-compatible custom endpoint, no OS sandbox beyond the permission layer, no HTTP or WebSocket server for a host application.
- freddie is 172k lines of plain JavaScript without the 73k vendored client modules. Its classes: web UI 62.8k, protocol servers and API controllers 15k, self-modifying Cordis extensions 14.4k, tools 14k, session storage 10.3k, subagents and orchestration 7.3k, agent loop 7.2k, context and compaction 6.3k, providers 5.7k, config and boot 5.6k, utilities 5.3k, goal, plan and schedule 4.8k, Cordis framework 4.8k, permissions and sandbox 4.7k, telemetry 3k.
- freddie docs/fx-split.md is freddie's own inventory of this split (194 agent-side packages, 54 GUI-side, 7 split). packages/util/runtime is the only seam through which freddie's agent code reaches Node. framework/hmr already hot-reloads Cordis plugins.
- gm and thebird each vendor txiki with their own script and stubs (315 divergent lines). gm's build stubs network, TLS, websocket, sqlite and ffi. thebird has a browser (wasm) port.

## Architecture

1. One binary, one engine. fx embeds txiki (quickjs-ng and libuv). The binary carries the engine, the host interface and a bootstrap of a few lines. No freddie JavaScript is compiled into it.
2. One host interface. A single versioned module, imported as `fx:host`, is the only way JavaScript reaches the machine. It is generated from one table in Zig, so a new native capability appears in JavaScript without hand-written glue. It has four parts:
   - Native tools as verbs. Every fx tool is callable as `host.tools.<name>(args)`, and every call passes the same Zig permission gate as a model tool call. JavaScript cannot bypass the gate.
   - The agent core. The same shape libfx exposes (agent, session, prompt, steer, cancel, resume, events, custom tools, custom providers, hooks, persistence), in process, backed by fx's native loop, sessions, compaction and subagents.
   - System services. HTTP server, WebSocket, fetch and TLS backed by fx's native Zig networking; files, watch, process spawn, pty, timers, DNS, crypto and SQLite. These replace txiki's stubbed modules, so there is one network stack and one permission model.
   - The module loader. ES modules resolved from the JavaScript root on disk through one import map, with `import.meta.hot` on every module.
3. JavaScript on disk. The JavaScript root is found in this order: `--js-root`, `FX_JS_ROOT`, `<project>/.fx/js`, `~/.fx/js`. freddie's checkout is a valid root as it stands, so development is edit and save.
4. Hot reload everywhere, with a safety net.
   - Agent side: a native file watcher feeds the loader's module graph. A changed module is re-evaluated, and Cordis disposes and re-applies exactly the plugins that depend on it (framework/hmr, moved onto the host watcher). Plugin state crosses the reload through one declared hand-off.
   - Client side: the host HTTP server serves the web UI's modules from disk and pushes changes over a WebSocket; cordis-client-runner swaps the module in the page.
   - Host interface and bootstrap: the bootstrap file is on disk too and reloads the same way.
   - Safety net: a reload that throws leaves the previous module instance running and reports the error to the session, so the agent can fix its own change. Every healthy start records a content-addressed snapshot of the root as last known good; `fx js rollback` restores it, and a start that fails twice restores it by itself and says so.
5. freddie becomes a thin layer of Cordis plugins. For each native capability there is no freddie package: one generic adapter reads the host interface's table and registers the matching Cordis services through a Proxy. What stays as real code is what fx does not do: the Cordis framework, profiles and boot, the self-modifying extensions, the web UI with its API controllers, and orchestration that fx lacks (agent teams, workflow scripts, goals, schedules), each only if a shipped profile loads it.
6. One contract for two hosts. thebird implements the same `fx:host` module over its browser txiki port, so the same JavaScript root runs in the fx command line and in thebird. A capability a host lacks is absent from its table and the adapter does not register it.

## Stages

Each stage is one Phase A (code, docs and tests, committed locally as lanmower with no trailers, listed in docs/PENDING_VALIDATION.md) and one Phase B (witnesses and red checks, one push). A window that builds native code or starts a browser goes through the ticket gate; a window of plain JavaScript checks starts directly when available memory is at least 2.5 GB. Stages run in parallel wherever the owners differ.

### Stage 0. The cut table (fx and freddie, in parallel, no build)

- freddie: from docs/fx-split.md, one table with a row per package family: cut (fx does it, with the fx source path as evidence), move (runs in txiki as it is), or drop (no shipped profile loads it). Next to it, the exact list of Node APIs that packages/util/runtime uses, and every `node:` import outside it.
- fx: the host interface specification as a file freddie can code against (docs/host-interface.md and a generated type listing): the tool table, the agent core, the system services, the loader and the reload protocol.
- Witness: every freddie package appears in exactly one row; every Node API in the list maps to a host interface entry or to a named gap.

### Stage 1. The engine in fx (fx; gm supplies the vendor step)

- One shared vendor step for gm, thebird and fx: one pin file and one script, taken from gm's tools/vendor-txiki.mjs and verified by sha256, not a third divergent copy.
- txiki as a static library built by cmake from build.zig; the C shim; the Zig runner with gm's semantics (per-call deadline, console to a sink, refused `node:` specifiers, verbs returning JSON).
- `fx js <file>` runs an ES module from disk. run_code runs its body on the runner; the old route and its fixtures stay until the new one passes.
- Witness: a module on disk runs and imports another; a body past its deadline is stopped with the named error; a denied verb is refused by the Zig gate. Red checks for each.

### Stage 2. The host interface (fx)

- The table-driven `fx:host` module with its four parts. Networking, files, watch, spawn, pty, DNS, crypto and SQLite as host functions over fx's native code.
- Close the gaps freddie needs: an Anthropic-compatible custom endpoint, and the HTTP and WebSocket server.
- Witness: a JavaScript file serves HTTP and a WebSocket echo from inside fx; a JavaScript file runs an agent turn with a JavaScript tool and receives the event stream; every tool verb is refused when the gate denies it.

### Stage 3. Loader and hot reload (fx)

- Root resolution, the import map, `import.meta.hot`, the native watcher, the module graph, last known good and `fx js rollback`.
- Witness: editing a module on disk changes behaviour in a running process without a restart; a module that throws on reload leaves the old one running and reports the error; a broken root is restored from last known good. Red checks for each.

### Stage 4. freddie boots inside fx (freddie; fx fixes what it finds)

- packages/util/runtime is rewritten on `fx:host`. Every `node:` import outside it is removed; a check fails on a new one.
- Until Stage 1 lands, freddie runs its agent-side code under the standalone tjs binary that gm and thebird already build, to find Node dependencies now and not later.
- Milestone 1: the headless profile completes a task inside fx with node removed from PATH.
- Milestone 2: `fx freddie web` serves the web UI and a browser session completes a task, with node removed from PATH.

### Stage 5. The cuts (freddie)

- In this order, each with a parity witness against the behaviour before the cut and each deleting the freddie packages it replaces: providers, agent loop, tools, permissions and sandbox, sessions and compaction, subagents, MCP client and ACP.
- The generic adapter replaces the per-capability packages. The event-sourced session format is kept only where the web UI reads it, as a projection of fx's sessions; one store, not two.
- Witness: the same task and the same web UI flows pass before and after each cut; the deleted packages have no remaining reference.

### Stage 6. Command line and thebird (fx and thebird)

- fx: `fx freddie <profile>`, `fx plugin add`, and `fx js` (run, rollback, doctor). A project with a Cordis configuration is detected and offered.
- thebird: the `fx:host` module over its browser txiki port; the same JavaScript root loads there.
- Witness: one plugin, written once, loads and hot reloads in the fx command line and in thebird.

### Stage 7. Cut, deduplicate and rearchitect what remains (freddie, from day one, alongside the other stages)

- Delete now: the 16 stale package shells, Windows-only code (pwsh tools, the Windows ACL sandbox) since fx ships for macOS and Linux, demos, and any package no shipped profile loads.
- Merge every seam that has one implementation into that implementation. Collapse the package count to the plugins that are separately loadable.
- Make the rest data-driven: one plugin manifest, schemas that generate settings screens, controllers and remote stubs, one table for commands and tools.
- A ledger of lines per family before and after each pass, in docs/PENDING_VALIDATION.md.
- Witness: the web UI and headless flows still pass; the ledger shows the removal.

## Who owns what

- fx peer (/workspace/fx/fx): stages 1, 2, 3 and the command line in 6; the fx half of stage 0. It owns the outcome and reports the overall state.
- freddie peer (/workspace/freddie): stages 4, 5 and 7; the freddie half of stage 0.
- gm peer (/workspace/gm): the shared vendor step and the txiki compatibility gaps freddie's parity runs name.
- thebird peer (/workspace/thebird): the thebird half of stage 6.
- Each peer works only in its own checkout. Hand-offs are files: fx publishes the host interface; freddie publishes the cut table and the Node API list; each REPORT names what it needs from another peer, and the dispatcher relays it.

## Done

- With node, npm and pnpm removed from PATH: `fx freddie web` serves the web UI and completes a task, the headless profile completes a task, and ACP answers.
- No freddie JavaScript is inside the fx binary; editing any agent-side or client-side file changes the running system without a restart, and a broken edit is survived and rolled back.
- Every capability fx has natively has no freddie implementation left.
- One plugin loads in the fx command line and in thebird.
- The line ledger is final and the remaining code has no comments.
- Then the fx peer reports DONE for this plan, and the dispatcher deletes this file.
