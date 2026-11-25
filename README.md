# hyperagent

hyperagent orchestrates peer coding agents through the airc WebSocket server and exposes the dispatcher as an MCP server.

## Install

Use the one-command installer in `AnEntrypoint/hyperagent-install`:

```sh
curl -fsSL https://raw.githubusercontent.com/AnEntrypoint/hyperagent-install/main/install.sh | sh
```

```powershell
irm https://raw.githubusercontent.com/AnEntrypoint/hyperagent-install/main/install.ps1 | iex
```

The installer verifies the release checksum, installs the CLI, ccbridge, harness templates and GUI resources, runs `hyperagent setup`, prints `hyperagent doctor`, and tells you to restart Claude Code. A running Claude Code session keeps its old MCP environment until it is restarted.

## Source build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

`cmake --install build --prefix <dir>` produces the shipped layout. `tools/ship-witness.sh build` installs that layout into scratch and runs the portable setup, doctor and red-check witness. Release publication is a manual `workflow_dispatch` after the source push; it verifies all five target archives and checksum sidecars before uploading or moving the latest tag.

## Setup and doctor

```sh
hyperagent setup
hyperagent doctor
hyperagent setup --check
```

`setup` writes a user-scope install manifest containing the absolute installed binary, repository or shipped root, project parent, free airc port, state home, journal root, harness-template directory, ccbridge binary and MCP timeouts. It creates every directory, updates the user-scope Claude MCP registration with those values using `--` before the server name and command, rewrites the project `.mcp.json`, and prints the Claude Code restart instruction. `doctor` and `setup --check` verify the paths, binaries, templates, saved MCP registration, port identity, long tool timeout and absence of a conflicting model override.

Options for a custom installation:

```sh
hyperagent setup --parent DIR --home DIR --journal DIR --templates DIR --port PORT
```

`HYPERAGENT_INSTALL_MANIFEST` overrides the manifest path. `HYPERAGENT_WITNESS_DIR` selects scratch for witnesses. `HYPERAGENT_SCRATCH` selects peer scratch, `HYPERAGENT_PEERS_ROOTS` is a path-list of peer checkout roots, and `HYPERAGENT_JOURNAL` overrides the dispatcher journal root. No script or rule hardcodes a host-specific workspace mount.

## Custom peer launch

Put a ccbridge template in the directory passed to `--templates`, or set `CCBRIDGE_TEMPLATES`. A chlaude-compatible wrapper can export its own model and credentials outside the repository. Use `--model default` when resetting a saved peer model: hyperagent then leaves `AIRC_CLAUDE_MODEL` unset, the template appends no `--model`, and the wrapper's model choice is preserved. Never put an auth token in a template, manifest, log or report. `docs/chlaude-template.json` is an example.

## Open

```sh
build/hyperagent open
```

`open` applies the manifest environment when the variables are unset, sets `MCP_TIMEOUT=60000` and `MCP_TOOL_TIMEOUT=14400000`, changes to the checkout root and launches Claude Code.
