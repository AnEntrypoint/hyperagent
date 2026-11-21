# Standing rules for peers

These apply to every assignment from the dispatcher. Read them again after a compaction.

## Where and how

- Work only in the directory you were started in, on main, in the shared checkout. No clones, scratch copies or worktrees.
- The host denies Bash, Read, Write, Edit, Grep and Glob. Use the gm verbs. Read `skill://gm/SKILL.md` as the first verb of your first `run_code`, batch verbs into one body, and wait on a gm job once with `job_status` and a `waitMs`.
- Commit as lanmower only. No trailers and no tool or model attribution.
- No comments in code. Say it in names and structure, and move what cannot be said that way into AGENTS.md after checking it is still true.
- Write the simplest, most efficient, most maintainable code with only what it needs: delete, deduplicate, share modules, and drive behaviour from data.
- Browser work goes through playwriter. If it fails, report the failure and record the check as open.
- Scratch goes under `/workspace/tmp/<your project>`, never `/tmp`. Every window and witness removes its scratch and stops every process it started, by PID. Never `pkill` or `killall`.

## Two phases

- Phase A: every change for the stretch in one pass, code, docs and tests only. Commit each change locally as you make it and list it in `docs/PENDING_VALIDATION.md` with its witness and its red check. A new item that arrives joins the open pass. Push nothing.
- Phase B: one window on the final tree. Run every listed witness and red check, fix what fails, and push origin main once, only if everything that ran passed. A skip is not a run. Quote the push output and `git ls-remote origin main`.
- A second push is allowed only when a run shows a failure in the repository that the fix addresses. Never push to see what CI says, and check a hosted run at most once.
- A window is valid only for the tree it started on. If an edit lands after it starts, stop it by PID and run one window on the final tree.

## The gate

- A heavy window (a compile, a browser, a cargo, zig or cc job) starts through the ticket gate: `node /workspace/hyperagent/scripts/gate-ticket.cjs run <label> -- <window command>`. Do not write your own polling loop and do not cut the queue.
- A window of plain scripts takes no ticket and starts when available memory is at least 2.5 GB. Split a mixed window so only its heavy steps take a ticket, and split a heavy window over about 30 minutes into separately ticketed parts.
- While a ticket is queued, work on something the window does not test. Polling the queue is not work.

## Pace

- Prove each change with the narrowest check that can fail for it, rerun a failing row alone, and run the full set once before the push. A check that passed on a tree is not run again on that tree.
- Wait on a job inside the turn, with a bounded timeout on its PID. Do not end a turn with your job running.
- Decide every reversible choice yourself and give the reason in the REPORT. Never end a turn to ask about scope or size.
- A finished stretch is not the end of the turn. Carry on until nothing reachable is left.
- When the assignment points at a spec file, that file is the plan: read it in full, work your stages, and name what you need from another peer in the REPORT.
- End every turn with `REPORT: finished|open|blocked — reason — witness: <evidence>`, naming your longest step and its wall time.
