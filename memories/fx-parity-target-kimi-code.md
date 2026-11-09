---
name: fx-parity-target-kimi-code
description: "Owner decision: fx must match https://github.com/MoonshotAI/kimi-code (Kimi Code CLI) in instructions and functionality; the Python kimi-cli is archived and is not the target"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-08T18:13:32.571Z
---

Owner statement (2026-11-08): "https://github.com/MoonshotAI/kimi-code is what we want to match", in answer to whether fx has the same instructions and functionality as Kimi CLI.

Owner statement (2026-11-08): "no need to list, just drive it to full parity, kimi code cli is the bottom-line featurset and we'd like to have the exact same instructions". Full parity means tools, slash commands, instruction handling, config, MCP, hooks, sessions, permissions, skills, subagents, print and ACP modes, and plugins. The inventory of kimi-code surfaces versus fx is at /tmp/kimi-parity-inventory.md (a handoff copy; regenerate if missing).

**Why:** fx (Vercel Labs fx, Zig, /workspace/fx/fx) is not a Kimi clone. Its README and docs do not reference Kimi CLI; the only kimi references are the kimi-k3 gateway model. The owner wants parity with the current Kimi Code CLI, not the archived Python kimi-cli.

**How to apply:**
- The reference checkout is /workspace/refs/kimi-code at eb2035f, which matches upstream main. Use it as the source of truth for tools, slash commands, instruction files, config, hooks, MCP, sessions, permissions, skills, subagents and print or ACP modes.
- Do not use /workspace/refs/kimi-cli (archived) as the target.
- Build a parity matrix per surface, with a red-then-green witness per item, the same way the other parity work is done. Each surface gets its own commit in /workspace/fx/fx, committed as lanmower with no trailers.
- Known gaps at the time of writing (not yet verified in a full sweep): computer-use tools (computer_click, computer_screenshot and others), apply_patch, batch, todo list, goal, notify, TowerPlan, AgentSwarm, the task list, output and stop tools, plugins, and the IDE and remote-control surfaces. Fx has ACP and skills, but has not been compared against kimi-code's instruction handling.
- Sequencing: fx's current MCP spec run (roots done; resources, prompts, completion, logging, tasks, output-schema outstanding) comes first, because kimi-code is an MCP client and those items are part of that surface.

Related: [[owner-decisions-2026-11-08]]
