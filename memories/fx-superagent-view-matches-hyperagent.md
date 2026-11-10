---
name: fx-superagent-view-matches-hyperagent
description: "owner direction: fx's superagent does not see the full view of what its agents do; its view roughly matches what the hyperagent dispatcher gets (final response per turn, idle/gone events, status), with detail kept in a journal"
metadata:
  node_type: memory
  type: project
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
  modified: 2026-11-10T06:45:02.675Z
---

Owner (2026-11-10): "superagents shouldnt get the full view of what agents are doing, we must make sure that fx's superagent view is a rough match of what we experience from the perspective of superagent feedback when running in the fx app".

**What the hyperagent dispatcher experiences, as the reference:** it prompts a peer and gets back only the turn's final response block; an event the moment a turn ends, with the last response; alive and busy state; interim narration and tool calls stay in a per-session journal that is read on request, in slices.

**How to apply:** fx's superagent gets the same shape from its roots: the final response of a turn, turn-end and gone events, and status. A root's tool calls, streamed text and context do not flow into the superagent's context. Sent to fx as the next prompt in [[dispatch-ready-prompts]] when its turn ends; fold it into the superagent stretch.

Related: [[fx-parity-target-kimi-code]], [[feedback-generalizations-not-check-towers]].
