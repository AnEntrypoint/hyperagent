---
name: local-token-not-a-concern
description: owner says the token pasted into chat is a local token; do not raise rotation as a held item
metadata:
  node_type: memory
  type: feedback
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-09T11:41:45.782Z
---

The owner said "dont worry about the token its just a local token" after a token was pasted into chat.

**Why:** the token is for a local relay (127.0.0.1), so rotating it is not worth an owner prompt.

**How to apply:** do not list token rotation in the owner report or held-for-owner lists. Still never echo the token value.
