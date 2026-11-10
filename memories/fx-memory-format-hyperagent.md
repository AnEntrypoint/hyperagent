---
name: fx-memory-format-hyperagent
description: "owner direction: fx keeps one memory format, the hyperagent layout, and retires its own; the 16 hyperagent memories are carried over into fx's memory directory"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T05:29:53.633Z
---

Owner direction (2026-11-10): "no we want it to use that memory format instead of its own". fx must use the hyperagent memory format as its only format, not keep its own alongside it.

**Where it lives:** /config/.claude/projects/-workspace-fx/memory/ with MEMORY.md as the one-line index.

**Format:** frontmatter `name` (equal to the filename slug), `description`, and `metadata` with `node_type`, `type`, `originSessionId`, `modified`. The index holds one line per memory and no content.

**Carry-over:** the 16 files of /workspace/hyperagent/memories/ (MEMORY.md excluded), including fx-image-reading-router-fixed. The first carry-over dropped that file, which was wrong. Its "issue 3" reference should be "issue 2" in /workspace/fx/issues.md.

**Retirement:** fx's old-format files move to /workspace/fx/.fx/retired-memory/, not deleted, after every entry is carried over.

**How to apply:** when checking fx's memory, look for the hyperagent layout only. Sent as turn bd209709cb4d; its REPORT line is the witness to read.

Related: [[feedback-two-phase-work]].
