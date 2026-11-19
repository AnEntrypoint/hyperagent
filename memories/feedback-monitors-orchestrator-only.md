---
name: feedback-monitors-orchestrator-only
description: "owner rule: the no-Monitor restriction applies only to the orchestrator, whose cycle is CronCreate; peers may arm Monitors"
metadata:
  node_type: memory
  type: feedback
  originSessionId: b53a69cc-8cf6-4e0b-b540-bd86f7cbf1e6
  modified: 2026-11-19T08:42:42.002Z
---

The owner's "never arm a Monitor" applies to the orchestrator only. The orchestrator's cycle is a CronCreate job, not a Monitor. Peers (the project agents) may arm Monitors for their own work, and they never see the orchestrator's instructions.

**Why:** I wrongly told peers to avoid Monitors, and wrote the same restriction into the orchestrate skill for peers. Freddie was stopped from a Monitor it had armed to wait on swap, which was not a rule for it. The owner corrected this directly.

**How to apply:** when writing a peer prompt, do not carry the orchestrator's constraints into it. The orchestrate skill's wake section says the dispatcher uses CronCreate and never a Monitor; peer-facing text says nothing about Monitors. Do not stop a peer's Monitor on the ground that Monitors are banned.
