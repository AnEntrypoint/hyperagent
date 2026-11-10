---
name: fx-memory-carry-over-pending
description: "owner direction: fx keeps its per-project memories in the same layout and format as the hyperagent set, and carries the fx and peer-wide memories over; queued until fx is idle"
metadata:
  node_type: memory
  type: project
  modified: 2026-11-10T05:15:24.228Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner direction (2026-11-10): fx must use the same per-project memory layout and format as the other projects, and the memories that apply to fx must carry over to fx. The owner asked for this to be communicated to the fx agent.

**Layout and format** (the one used under `/config/.claude/projects/-workspace-hyperagent/memory/`):
- One file per memory, named in kebab-case, with frontmatter: `name`, `description`, and `metadata` with `node_type: memory`, `type` (`user`, `feedback`, `project` or `reference`), `originSessionId`, and `modified`.
- The body states the fact. Feedback and project memories add `**Why:**` and `**How to apply:**` lines. Link related memories with `[[name]]`.
- `MEMORY.md` is the index: one line per memory, `- [Title](file.md) — hook`, no frontmatter, no memory content.

**What carries over to fx:**
- fx project memories: `fx-features-added-pending`, `fx-image-reading-router-fixed`, `fx-parity-claude-code-docs`, `fx-parity-target-kimi-code`.
- Peer-wide feedback rules (all `feedback` memories): `feedback-two-phase-work`, `feedback-validation-debug-first`, `feedback-build-locally-avoid-actions-limits`, `feedback-kill-by-pid-only`, `feedback-decide-maximum-yield`, `feedback-wallclock-iteration-times`, `feedback-gm-skill-always-read`, `feedback-issues-lists-cleared-not-committed`, `feedback-local-token-not-a-concern`, `ci-checks-manual-builds-automated`, `tooling-build-in-github-actions`.
- `owner-decisions-2026-11-08`, which names fx.

**Not carried over:** the project memories for jayzee, thebird, gm, spoint, oscraft and the other projects, the hyperagent dispatcher memories, and the termrover reference.

**How to apply:** when fx goes idle, send the instruction: fx copies each listed file from `/workspace/hyperagent/memories/` into its own memory directory under the same filename, keeps the frontmatter format above, writes its own `MEMORY.md` index, and removes any memory it finds wrong. Then delete this file and its index line here.

Related: [[feedback-two-phase-work]], [[feedback-decide-maximum-yield]].
