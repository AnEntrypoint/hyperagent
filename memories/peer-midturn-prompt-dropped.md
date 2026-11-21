---
name: peer-midturn-prompt-dropped
description: troubleshooting: a resume_project prompt sent to a busy peer can vanish; check the journal for the prompt text and resend at turn end
metadata:
  type: feedback
---

resume_project returns state running with a turn_id even when the prompt never reached a peer that is mid-turn. Three of four standing directives and one priority update sent to busy peers left no record in their journals, and the pane input box was empty, so nothing was queued. One peer (gm, in a short bounded wait) did receive its directive.

**Why:** the harness types the prompt into the terminal; a peer in a long tool call or a gate wait does not consume it, and the text is lost.

**How to apply:** send prompts only to a peer whose turn has ended. After any send, grep its journal once for a phrase from the prompt, and resend at its next turn end if it is missing. Read the pane to see whether input is queued. This is the rule the orchestrate skill already states (never send a second prompt while it is busy); it was broken once and cost a long delay. See [[dispatcher-silent-failures]].
