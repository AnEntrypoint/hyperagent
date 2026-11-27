# Standing rules for peers

These apply to every assignment. Read them again after a compaction.

## Time

Anything that waits, repeats or blocks is a defect, whatever the work.

- Do each thing once, on final inputs. A check that passed on a tree is not run again on that tree.
- A cycle state read is issued once per cycle. A second read on unchanged state is a defect: report the duplicate and do not issue it again.
- Prove each change with the narrowest check that can fail for it. Rerun a failing check alone, and run the full set once before a push. A red check is rerun only when its code or its witness changed.
- Reuse before you build. A published artifact, a cache or another project's build that answers the question replaces a build. One build serves a series: switch behaviour at run time, not by rebuilding.
- While something runs or is queued, work everything it does not test. With nothing reachable left, end the turn naming what is pending and where its result will land; you are resumed when it does. Never poll.
- Decide every reversible choice yourself and give the reason afterwards. Never end a turn to ask about scope or size. A finished stretch is not the end of the turn.
- You are sent your whole remaining list each turn. Work it in order and skip only what is blocked.
- When a stretch ends, say where the work stands (finished, open or blocked), what you ran to show it, and your longest step with its wall time. Say what wasted time, so it becomes a rule.
- A block only the owner can lift is a held queue item, not an idle peer: end the turn, keep the queue file, and resume in the cycle the owner-supplied fact changes. Never poll.
- A turn that reaches a harness stall deadline with work still uncommitted or committed but unpushed is a defect: stop the peer by session, preserve the tree, launch the missing window through the gate, record its result file in the queue, and resume with one narrower step when the ticket leaves. Never repeat the original combined prompt.
- A peer that repeats output-limit resumes at near-full context without new tool output is treated as a stalled turn before the deadline: stop by session, preserve the tree, queue the missing window, and resume with one narrower step when its ticket leaves.
- A peer compacting at near-full context with no new tool output for longer than one cycle is treated as a stalled turn before the deadline: stop by session, preserve the tree, and resume with one narrower step; queue a window only when its code or witness changed.
- A saved session that compacts at near-full context for longer than one cycle on two consecutive resumes is forked into a fresh session carrying only the spec, queue and next step; do not reuse the saved session until it has produced a green push.
- A forked session that compacts at near-full context with no new tool output for longer than one cycle is replaced with a truly fresh session with no saved context; do not reuse the saved session until it has produced a green push.
- A live witness that writes to an external service treats a rate-limit response as a witness defect: bound the attempts, honor backoff, avoid repeated marker runs on the same tree, and queue the final window on the tree that actually holds the changes.
- A shipped-layout install rewrites the portable project MCP file. Regenerate the installed project configuration with setup immediately after the install and check the doctor before dispatching peers.
- A live channel, server-identity or author-attribution mismatch is a defect: stop the active acceptance claim, add the first-person requirement to the spec, queue a source-of-truth witness and red check, and do not claim DONE until every live surface agrees.
- An ended peer with unblocked work is sent in the same cycle its state is read. A spec, queue or held-file rewrite that delays that send is a defect and is recorded as a rule.
- A detached send writes its output to a directory that already exists. Create that directory in the same command before launching the send.
- A delivery check made before a detached start attaches is a defect: confirm the peer is alive, then read the journal once.

## Where and how

- Work only in the directory you were started in, on main, in the shared checkout. No clones, copies or worktrees.
- The host denies its built-in shell and file tools; use the gm verbs. Read `skill://gm/SKILL.md` as the first verb of your first `run_code`.
- Commit under the owner's configured git identity. No trailers and no tool or model attribution.
- No comments in code. Say it in names and structure; what cannot be said that way goes into AGENTS.md once checked true.
- Write the simplest code that does the job: delete, deduplicate, share modules, drive behaviour from data.
- Browser work goes through playwriter. If it fails, report the failure and record the check as open.
- Scratch goes under `$HYPERAGENT_SCRATCH/<your project>` (default: the `tmp` directory beside the projects), never an OS temp directory. Remove your scratch and stop every process you started, by PID, never by name pattern.
- Consolidate: push every green change in the repository, including files that were left uncommitted, once the build is green.
- Progress-oriented decisions are made and executed by the peer, with the reason recorded, not held for the owner.
- Never request a key or credential for a witness that does not need one.

## Two phases

- Phase A: every change in one pass, code, docs and tests, no builds. Commit each change locally as you make it and list it in `docs/PENDING_VALIDATION.md` with its witness and its red check. A new item joins the open pass.
- Phase B: one window on the final tree. Run every listed witness and the red checks that are due, fix what fails, and push as soon as everything that ran passed. Each green window is its own push. Never push a red tree or push to see what CI says; read a hosted run at most once.
- Push policy (dispatcher decision, 2026-11-27): rebase onto origin/main immediately before each window, run the window on that rebased tree, and push right after it if it is green. There is no shared-tree lock. A push that finds origin moved during its window rebases and re-runs the window; it does not force.
- A window covers the commits it ran on. A later commit that changes nothing the window reads does not void it: push the covered commits and leave the rest for the next window.
- Commits ahead of origin with no window queued or running are a defect: queue their window.
- When the assignment points at a spec file, that file is the plan: read it in full, work your stages, and name what you need from another peer.

## The gate

- A heavy window (a compile, a browser, many parallel test processes) starts through the gate, detached, with its output in a file: `GATE_TREE=$(git rev-parse --short HEAD) setsid nohup node scripts/gate-ticket.cjs run <project>-<part> -- <command> > <result file> 2>&1 < /dev/null &`. Its last line is `<PART>_DONE <exit code>`. Started any other way, it dies with its shell.
- A label that already passed on its `GATE_TREE` prints CACHED and runs nothing. Set `GATE_FORCE=1` only when its code or its witness changed. Queue a window only on a tree that is final for it.
- A rerun of a red check records its changed code or witness and the reason before a ticket exists; a launcher without that record queues nothing.
- A gate launcher derives `GATE_TREE` from the tree it runs on; a recorded tree that differs from the worktree is a defect and its result covers no final tree.
- The gate admits a window on memory: its label's last recorded peak plus 15%, or a fixed floor for a new label, and nothing while the host swaps out. Set `GATE_MIN_MB` only for a new label that needs more than the floor, never above what the idle host shows.
- `GATE_EXCLUSIVE=1` runs a window alone: use it for a window that peaks above the free memory and for any witness that measures time. A timing result taken under load is rerun, not interpreted. Any other exclusive window makes every project wait for nothing; a check that only passes alone has a load-dependent timeout, and that timeout is the defect to fix.
- `GATE_QUICK=1` lets a window of a few minutes pass the queue. A window whose last run took under five minutes also passes an exclusive ticket that is waiting for the gate to empty.
- Plain scripts take no ticket. Split a mixed window so only its heavy steps take one, and split a heavy window over about 30 minutes into separately ticketed parts.
- The gate polls for you. When resumed, read the result file; wait on a window that is running now with one bounded wait on its PID.
