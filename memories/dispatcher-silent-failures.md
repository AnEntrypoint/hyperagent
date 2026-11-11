---
name: dispatcher-silent-failures
description: "troubleshooting: causes of a silent watch monitor and of a peer that takes a prompt but does no work"
metadata:
  type: reference
---

Two dispatcher failures that look like "peers are still busy" and were each found late (2026-11-11):

- **Watch monitor delivers no events while turns end.** `awk` on this host is mawk, which block-buffers piped input, so a `tail -F | grep | awk` monitor prints nothing until the buffer fills. Use `awk -W interactive`. Check: turn ends appear in `/tmp/watch.out` but the monitor expires with "no events delivered".
- **A resumed peer shows `running` but its turn ends `blocked` after about two minutes with no work.** A short session id (for example `57a3f21e`) does not resolve, and the peer sits at Claude Code's "Resume session" picker. `read_pane` shows it. Stop the peer and resume with the full id from `list_project_sessions`.
