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
- `list_outputs`, `read_output`, `search_output` — MCP views of the per-session
  output journal under `.orch/projects`.
- `peer_status`, `read_pane`, `stop_peer`, `server_down`.

Every turn is journaled to `.orch/projects/<slug(cwd)>/<session>.jsonl` with every
streamed delta, while `instruct` and `turn_result` return only the turn's final
response block. The server also emits MCP events at turn end: `notifications/message`
after `logging/setLevel`, `notifications/resources/updated` for `resources/subscribe`
or `subscriptions/listen`, `notifications/resources/list_changed` for new sessions,
and `notifications/progress` for tool calls that pass a progress token.
`hyperagent://events` is the recent event stream.

`bin/watch` prints one line the moment a peer finishes, with its whole last response
and a ready `instruct` call. Run it locally beside the orchestrator.

Never start a peer that has no work. The full dispatch loop is `/orchestrate`.
Open this project with `bin/open` so MCP tool calls get a long timeout.
The equivalent CLI is `bin/orch`.
