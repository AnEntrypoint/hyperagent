# Standing rules for peers

These apply to every assignment from the dispatcher. Read them again after a compaction.

## Where and how

- Work only in the directory you were started in, on main, in the shared checkout. No clones, scratch copies or worktrees.
- The host denies Bash, Read, Write, Edit, Grep and Glob. Use the gm verbs. Read `skill://gm/SKILL.md` as the first verb of your first `run_code`, batch verbs into one body, and wait on a gm job once with `job_status` and a `waitMs`.
- Text that holds backticks or `${` goes into a script file written once with `write_file`, and the file is run. The same text inline in a `run_code` body fails to load, and each retry costs a call.
- Commit as lanmower only. No trailers and no tool or model attribution.
- No comments in code. Say it in names and structure, and move what cannot be said that way into AGENTS.md after checking it is still true.
- Write the simplest, most efficient, most maintainable code with only what it needs: delete, deduplicate, share modules, and drive behaviour from data.
- Browser work goes through playwriter. If it fails, report the failure and record the check as open.
- Scratch goes under `/workspace/tmp/<your project>`, never `/tmp`. Every window and witness removes its scratch and stops every process it started, by PID. Never `pkill` or `killall`.

## Two phases

- Phase A: every change for the stretch in one pass, code, docs and tests only. Commit each change locally as you make it and list it in `docs/PENDING_VALIDATION.md` with its witness and its red check. A new item that arrives joins the open pass. Push nothing.
- Phase B: one window on the final tree. Run every listed witness, and the red checks the Pace section says are due, fix what fails, and push origin main once, only if everything that ran passed. A skip is not a run. Quote the push output and `git ls-remote origin main`.
- A second push is allowed only when a run shows a failure in the repository that the fix addresses. Never push to see what CI says, and check a hosted run at most once.
- A window covers the commits it ran on. A later commit that changes nothing the window reads (a ledger line, a doc, another area) does not void it: push the covered commits, reordered first if needed, and leave the rest for the next window. Stop a window by PID and re-queue it only when an edit changed what it tests.
- Every stretch ends pushed. Commits ahead of origin with no window queued or running are a defect: queue their window.

## The gate

- A heavy window (a compile, a browser, a cargo, zig or cc job) starts through the ticket gate, detached, with its output in a file: `setsid nohup node /workspace/hyperagent/scripts/gate-ticket.cjs run <project>-<part> -- <window command> > <result file> 2>&1 < /dev/null &`. The label starts with your project name. The window prints `<PART>_DONE <exit code>` as its last line. A ticket started any other way dies with the shell or session that started it.
- The gate admits a window on memory alone: the first with 2.5 GB available, a quick window with 4 GB, a second window with 6 GB, and neither while the host is swapping out. Another project's compile does not hold it. A witness that measures time (frame budgets, benchmarks) is queued with `GATE_EXCLUSIVE=1` in its environment and runs alone. The gate takes turns between projects: the project that ran longest ago goes next, and your own tickets keep their order. Do not write your own polling loop and do not cut the queue.
- Queue a window only on a tree that is final for it. A window re-queued because an edit landed costs a full turn of the queue.
- A window that runs in a few minutes (a browser probe, a single check, a reading) is queued with `GATE_QUICK=1` and goes past the queue as long as memory allows and no other quick window is running. Measured 2026-11-22: freddie's browser witness waited 10,445 s to run for 17 s, a probe waited 4,382 s to run for 3 s, and signal's waited 6,303 s to run for 143 s, all behind long windows. Anything over about 4 minutes is not quick.
- A window of plain scripts takes no ticket and starts when available memory is at least 2.5 GB. Memory pressure means swap-out or less than 2.5 GB available; swap-in alone, or someone else's compile, is no reason to pause between steps. Split a mixed window so only its heavy steps take a ticket, and split a heavy window over about 30 minutes into separately ticketed parts.
- While a ticket is queued, work through the rest of your list that the window does not test. When nothing else is reachable, end the turn with `REPORT: open`, naming each ticket label and its result file. The dispatcher resumes you when your tickets have left the queue. Never poll a queued ticket: a turn spent re-reading the queue is waste.
- When you are resumed, or your ticket is granted while you are working, read the result file. Wait on a window that is running now with one bounded wait on its PID.

- A heavy plain-script batch (more than about three parallel test processes, a full suite, many browsers) is a window, not a plain script: take a gate ticket for it, so a timing witness that runs exclusively (GATE_EXCLUSIVE=1) is not measured under someone else's load. Seen 2026-11-23: bevy's exclusive timing bench ran at load average 15 on the 3-core host while fx ran 29 test files outside the gate, so its over-budget count was noise. A timing result taken at load average above about 4 is rerun, not interpreted.

## Pace

- Before queueing a build to answer a question, look for a published binary or an artifact another project already built, and use it.
- Prove each change with the narrowest check that can fail for it, rerun a failing row alone, and run the full set once before the push. A check that passed on a tree is not run again on that tree.
- A red check is rerun only when the code it guards or its witness changed since it last passed. The whole red set runs once per stage, in the stage's last window.
- You are sent your whole remaining list, numbered, at every turn start. Work it in order, skip an item only when it is blocked, and say in the REPORT which numbers are finished, open and blocked.
- Decide every reversible choice yourself and give the reason in the REPORT. Never end a turn to ask about scope or size.
- A finished stretch is not the end of the turn. Carry on until nothing reachable is left.
- When the assignment points at a spec file, that file is the plan: read it in full, work your stages, and name what you need from another peer in the REPORT.
- End every turn with `REPORT: finished|open|blocked — reason — witness: <evidence>`, naming your longest step and its wall time.
