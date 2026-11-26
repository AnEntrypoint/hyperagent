---
name: peer-first-start-needs-trust
description: troubleshooting - a peer first started with instruct_many dies in 5 s with empty output; the folder-trust dialog was left on "No, exit"; start_projects with trust true fixes it
metadata:
  type: reference
---

On 2026-11-26 `instruct_many` for agentgui and gmweb (projects with no saved session) returned status `died`, duration about 5 s, empty text. `read_pane` showed Claude Code's "Do you trust this folder" dialog with "No, exit" selected and "Claude Code didn't start because this folder isn't trusted".

**Why:** a first start in a folder never trusted before opens that dialog, and the default `trust` handling did not answer it.

**How to apply:** for a project with no saved session run `start_projects` with `trust: true` and the model first, then `instruct_many` with `trust: true`. Check `peer_status` for `alive` before reading a `died` turn as a failure of the work. Related: [[dispatcher-silent-failures]].
