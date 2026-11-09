---
name: issues-lists-cleared-not-committed
description: "owner rule: issue lists (issues.md and similar) are never committed; once their issues are fixed they are cleared and deleted"
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T17:35:00.179Z
---

Issue lists such as `issues.md` are working notes, not project history. Do not commit them. Once every issue in the list is fixed or recorded with its cause, clear the file and delete it.

**Why:** the owner rejected a plan to commit gm's functionality-test issue list (`/workspace/gm/issues.md`, untracked). Issue lists go stale once fixed, and committing them leaves a second record that drifts from the code.

**How to apply:** when dispatching a peer to work an issue list, tell it to fix each item, then clear the list and delete the file, with the outcome of each item recorded in the peer's `docs/PENDING_VALIDATION.md` or in the commit that fixed it, not in the list. Never `git add` an issue list. Delete the list only after its items are resolved, not before, so no issue is lost.
