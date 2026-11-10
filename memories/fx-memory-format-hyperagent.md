---
name: fx-memory-format-hyperagent
description: "owner direction: fx uses the hyperagent memory format and reads and writes its memory files directly in its own current path (./memory), with no second copy anywhere; the 16 hyperagent memories are carried over into it"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T05:47:54.131Z
---

Owner direction (2026-11-10): "no we want it to use that memory format instead of its own", then "fx must directly use and interact with the memory files in the current path, the way we do in ./memory, and it must not keep a second copy of it".

**Where it lives:** /workspace/fx/memory/ (the current path's ./memory), with MEMORY.md as the one-line index. fx reads and writes these files directly. It is the only copy.

**Format:** frontmatter `name` (equal to the filename slug), `description`, and `metadata` with `node_type`, `type`, `originSessionId`, `modified`. The index holds one line per memory, `- [Title](file.md) — hook`, and no content.

**Carry-over:** the 16 files of /workspace/hyperagent/memories/ (MEMORY.md excluded), including fx-image-reading-router-fixed. The first carry-over dropped that file, which was wrong. Its "issue 3" reference should be "issue 2" in /workspace/fx/issues.md.

**Not wanted, to be removed:** the copy at /config/.claude/projects/-workspace-fx/memory/, which is a second copy, and the retired-memory directory /workspace/fx/.fx/retired-memory/, which would be a second copy of fx's old format. Once every old entry exists in /workspace/fx/memory in the new format, both go.

**Sent state:** turn bd209709cb4d carried the earlier direction and finished at 05:37 UTC with 17 memories in /config and 55 files in /workspace/fx/.fx/retired-memory. The correction went out as turn cdc892afed71 (2026-11-10): /workspace/fx/memory is the only store, and the /config copy and the retired copy are removed after every entry is verified.

**How to apply:** read the REPORT of turn cdc892afed71 and check that /workspace/fx/memory exists, that the /config copy and the retired directory are gone, and that fx's reader points at /workspace/fx/memory. fx's last turn left src/core/parity/item1.zig and scripts/acceptance/item1.mjs modified and uncommitted; they belong in fx's next Phase A.

Related: [[feedback-two-phase-work]].
