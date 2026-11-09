---
name: oscraft-control-scheme
description: "Owner direction for oscraft's controls: slick twin-stick walking with full control, plus a one-handed mode that auto-attacks intelligently like Vampire Survivors and lets the player cast effectively with one hand"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-08T18:07:18.447Z
---

Owner statement (2026-11-08): "walking in oscraft must feel slick and accept full control just like a twinstick, and we must also include a one handed mode where it plays like vampire survivor intelligently attacking, and allowing the user to cast effectively with one hand".

**Why:** the owner wants oscraft's hero to move with twin-stick precision (separate move and aim), and wants a one-handed mode for players who can only use one hand. In that mode the hero attacks on its own, picking targets sensibly, and the player still casts with one hand.

**How to apply:**
- Two requirements. (1) Twin-stick walking: smooth, full analog or equivalent control, with aim independent of movement. (2) One-handed mode: automatic attacks with intelligent target choice (nearest or most threatening, not random), and a one-hand cast scheme that covers every cast the two-hand scheme covers.
- Witness each piece in the sim and in the browser. The movement feel needs a measured lag or input-to-motion figure, not only a logic check.
- Start from the current code. The survey of existing movement, aim and cast paths is the first step, and the brief to oscraft should point at its results.
- Oscraft's rank 8 hero view lock measurements are the existing baseline for the feel work.

Related: [[owner-decisions-2026-11-08]]
