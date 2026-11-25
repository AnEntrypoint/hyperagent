---
name: project-browser-stops-without-lease
description: troubleshooting - the project playwriter browser (port 10210) stops and loses all tabs and the Google sign-in when no session holds a lease for the grace period; gate.log names it
metadata:
  type: reference
---

The shared project browser is owned by a gate (`playwriter-project --gate`) whose lifetime follows session leases, not just the 8 h `PLAYWRITER_IDLE_MS`. On 2026-11-25 10:16 UTC `/config/docstudio/.playwriter/gate.log` read "stopping browser: no lease for the grace period", the browser and the owner's 8 tabs went with it, and the restarted profile was signed out of Google.

**Why:** a peer session ending or being resumed releases its lease; with no lease for the grace period the gate stops Chrome even with pages open. The 8 h idle setting does not cover this.

**How to apply:** when a tab or sign-in vanishes, read `.playwriter/gate.log` first (grep "stopping browser", "gate exiting", "adopted running browser"), do not blame the peer, ask the owner for one Google sign-in, and send the lease-loss behaviour to gm. Do not kill the gate or Chrome to "fix" it. Related: [[dispatcher-silent-failures]].
