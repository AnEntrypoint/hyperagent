# Standing rules for peers

These apply to every assignment. Read them again after a compaction.

## Time

Anything that waits, repeats or blocks is a defect, whatever the work.

- Do each thing once, on final inputs. A check that passed on a tree is not run again on that tree.
- Prove each change with the narrowest check that can fail for it. Rerun a failing check alone, and run the full set once before a push. A red check is rerun only when its code or its witness changed.
- Reuse before you build. A published artifact, a cache or another project's build that answers the question replaces a build. One build serves a series: switch behaviour at run time, not by rebuilding.
- While something runs or is queued, work everything it does not test. With nothing reachable left, end the turn naming what is pending and where its result will land; you are resumed when it does. Never poll.
- Decide every reversible choice yourself and give the reason afterwards. Never end a turn to ask about scope or size. A finished stretch is not the end of the turn.
- You are sent your whole remaining list each turn. Work it in order and skip only what is blocked.
- When a stretch ends, say where the work stands (finished, open or blocked), what you ran to show it, and your longest step with its wall time. Say what wasted time, so it becomes a rule.
- A block only the owner can lift is a held queue item, not an idle peer: end the turn, keep the queue file, and resume in the cycle the owner-supplied fact changes. Never poll.

## Where and how

- Work only in the directory you were started in, on main, in the shared checkout. No clones, copies or worktrees.
- The host denies its built-in shell and file tools; use the gm verbs. Read `skill://gm/SKILL.md` as the first verb of your first `run_code`.
- Commit under the owner's configured git identity. No trailers and no tool or model attribution.
- No comments in code. Say it in names and structure; what cannot be said that way goes into AGENTS.md once checked true.
- Write the simplest code that does the job: delete, deduplicate, share modules, drive behaviour from data.
- Browser work goes through playwriter. If it fails, report the failure and record the check as open.
- Scratch goes under `$HYPERAGENT_SCRATCH/<your project>` (default: the `tmp` directory beside the projects), never an OS temp directory. Remove your scratch and stop every process you started, by PID, never by name pattern.
- Never stage or push a file the owner left uncommitted.

## Two phases

- Phase A: every change in one pass, code, docs and tests, no builds. Commit each change locally as you make it and list it in `docs/PENDING_VALIDATION.md` with its witness and its red check. A new item joins the open pass.
- Phase B: one window on the final tree. Run every listed witness and the red checks that are due, fix what fails, and push as soon as everything that ran passed. Each green window is its own push. Never push a red tree or push to see what CI says; read a hosted run at most once.
- A window covers the commits it ran on. A later commit that changes nothing the window reads does not void it: push the covered commits and leave the rest for the next window.
- Commits ahead of origin with no window queued or running are a defect: queue their window.
- When the assignment points at a spec file, that file is the plan: read it in full, work your stages, and name what you need from another peer.

## The gate

- A heavy window (a compile, a browser, many parallel test processes) starts through the gate, detached, with its output in a file: `GATE_TREE=$(git rev-parse --short HEAD) setsid nohup node scripts/gate-ticket.cjs run <project>-<part> -- <command> > <result file> 2>&1 < /dev/null &`. Its last line is `<PART>_DONE <exit code>`. Started any other way, it dies with its shell.
- A label that already passed on its `GATE_TREE` prints CACHED and runs nothing. Set `GATE_FORCE=1` only when its code or its witness changed. Queue a window only on a tree that is final for it.
- The gate admits a window on memory: its label's last recorded peak plus 15%, or a fixed floor for a new label, and nothing while the host swaps out. Set `GATE_MIN_MB` only for a new label that needs more than the floor, never above what the idle host shows.
- `GATE_EXCLUSIVE=1` runs a window alone: use it for a window that peaks above the free memory and for any witness that measures time. A timing result taken under load is rerun, not interpreted. Any other exclusive window makes every project wait for nothing; a check that only passes alone has a load-dependent timeout, and that timeout is the defect to fix.
- `GATE_QUICK=1` lets a window of a few minutes pass the queue. A window whose last run took under five minutes also passes an exclusive ticket that is waiting for the gate to empty.
- Plain scripts take no ticket. Split a mixed window so only its heavy steps take one, and split a heavy window over about 30 minutes into separately ticketed parts.
- The gate polls for you. When resumed, read the result file; wait on a window that is running now with one bounded wait on its PID.
