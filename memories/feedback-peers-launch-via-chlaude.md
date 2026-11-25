---
name: feedback-peers-launch-via-chlaude
description: Start airc peer agents through ~/.local/bin/chlaude's environment and flags, not a conflicting haiku model override.
metadata:
  type: feedback
---

Owner instruction 2026-11-25: peer agents must start the way `/config/.local/bin/chlaude` does. That script exports `ANTHROPIC_MODEL=claude-hy4[1m]`, a local proxy base URL and auth token, disables attribution and telemetry, and runs `claude --dangerously-skip-permissions "$@"`. It supersedes the older standing peer-model rule that asked for `claude-haiku-5-5`.

**Why:** A peer launched by the stock airc template silently inherits the wrong model/proxy settings, reproducing the model drift in [[dispatcher-silent-failures]]. The chlaude script has CRLF endings and no shebang, so airc cannot reliably exec it directly.

**How to apply:** Use a proper shebang wrapper that reads only the `export` lines from chlaude, then execs `claude --dangerously-skip-permissions "$@"`; point a custom airc `claude.json` at it through `CCBRIDGE_TEMPLATES`; launch and resume with `--model default` so hyperagent does not append a conflicting `--model`. Never copy the auth token into a spec, commit, journal, or report.
