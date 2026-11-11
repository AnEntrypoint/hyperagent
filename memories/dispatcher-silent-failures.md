---
name: dispatcher-silent-failures
description: "troubleshooting: causes of a silent watch monitor and of a peer that takes a prompt but does no work"
metadata:
  type: reference
---

Two dispatcher failures that look like "peers are still busy" and were each found late (2026-11-11):

- **Watch monitor delivers no events while turns end.** `awk` on this host is mawk, which block-buffers piped input, so a `tail -F | grep | awk` monitor prints nothing until the buffer fills. Use `awk -W interactive`. Check: turn ends appear in `/tmp/watch.out` but the monitor expires with "no events delivered".
- **A resumed peer shows `running` but its turn ends `blocked` after about two minutes with no work.** A short session id (for example `57a3f21e`) does not resolve, and the peer sits at Claude Code's "Resume session" picker. `read_pane` shows it. Stop the peer and resume with the full id from `list_project_sessions`.

**Peer silently back on haiku after a relaunch.** The airc server (`ccbridge serve`) carries `AIRC_CLAUDE_MODEL=haiku` from its own start (`ORCH_CLAUDE_MODEL` default in `src/orch/config.cpp`), so every launched `claude` peer gets `--model haiku`, and a `/model` set in the session is lost on relaunch. `ps` always shows `--model haiku`, even after `/model` took effect, so it cannot confirm the model; the pane's "Set model to ..." line can. The MCP server process started before the `claude-fable` harness was built rejects that harness name, and the CLI accepts it but the running server still launched haiku. Fix in use: after a relaunch send `/model claude-fable-5-2` as its own prompt, then the work. Symptom that points here: many turns that end in under five minutes with work left.

**After a reboot, sends return a turn id but nothing runs.** `.orch/server.pid` survives the reboot, so `resume_project` returns `state: running` while no airc server exists; `turn_result` then shows `connect: Connection refused`. Fix: `build/hyperagent orch up`, then send again. Also: the `claude-fable` harness is accepted but resolves to `claude` (the result says `"harness": "claude"`) and the peer still launches with `--model haiku`, so the `/model` first prompt is still needed after every relaunch.

**Fixed 2026-11-11 (commit 799aaac, in service since the 14:57 UTC server restart):** every prompt path now starts the airc server if none answers, and a project's model is saved in state and applied at launch (`orch resume NAME --model claude-fable-5-2`; launch argv then shows `--model claude-fable-5-2`). The MCP server process of a dispatcher session started before the rebuild still runs the old code until it is reconnected; the `orch` CLI on disk is the new code. A CLI `resume --timeout 40` writes a `timeout` result record to the journal while the turn carries on, so the result monitor fires once per such send.

**`orch resume` on the CLI relaunches a live peer.** With the rebuilt binary, `orch resume NAME --session ID --prompt ...` stops the running process and starts it again (process age resets, a `gone` event fires for the peer, its gm relay restarts, any subagents it was waiting on are lost). For a peer that is alive and idle, `orch prompt NAME "text"` sends without a relaunch. A send that must leave a journal needs its client to stay connected: run it in the background with a long `--timeout`.

