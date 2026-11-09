---
name: feedback-decide-maximum-yield
description: "Owner rule (2026-11-09): the dispatcher decides every choice for maximum effort and maximum yield; it does not hold choices back for the owner"
metadata:
  node_type: memory
  type: feedback
  modified: 2026-11-08T22:12:36.545Z
  originSessionId: bc092253-97dd-4f77-9001-5a59318251ee
---

Owner instruction (2026-11-09): "from now on decide all work around maximum effort maximum yield solutions instead of holding back".

**Why:** the owner was getting asked to choose between options the dispatcher could settle. Each held decision stalled a peer and cost a round trip.

**How to apply:**
- Decide judgement calls, trade-offs, tuning and visual values, protocol and replay design, and anything an experiment or witness can settle. Record the reasoning in the affected ledger (`docs/PENDING_VALIDATION.md` or the peer's decision section) so it can be reversed.
- Only these wait for the owner: credentials; deleting or rewriting unrecoverable data; licence or legal confirmations the owner has deferred; and facts only the owner holds, such as product scope.
- Licence questions are deferred by the owner (see [[thebird-unlicensed]]). Do not raise them again until the owner brings them back.
- Mirrors the orchestrate skill, "Maximum effort and yield" and "Decision rules".

Related: [[feedback-two-phase-work]]
