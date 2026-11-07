# hyperagent

This project orchestrates the projects beside it through the `hyperagent` MCP server.
Peer agents are Claude Code sessions started and prompted directly through airc.

## MCP tools

- `list_projects` — projects that can become peers; launches nothing.
- `start_projects` — start Claude Code peers for the named projects only.
- `instruct` / `instruct_many` — prompt one or many peers and return their turns.
- `turn_result` — collect a background turn started with `wait: false`.
- `resume_project` — continue a project's saved conversation in claude, codex or
  freebuff; the last project and harness are the default. `list_project_sessions`
  lists the candidates.
- `peer_status`, `read_pane`, `stop_peer`, `server_down`.

Never start a peer that has no work. The full dispatch loop is `/orchestrate`.
Open this project with `bin/open` so MCP tool calls get a long timeout.
The equivalent CLI is `bin/orch`.
