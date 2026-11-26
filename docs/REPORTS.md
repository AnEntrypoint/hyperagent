# Reports

Weekly, monthly and yearly reports on the hyperagent, uhh, casey and freddie repositories, the peer turns, and production health. Every report opens with a BLUF: a headline, five facts and the decisions waiting on the owner.

## Outputs

Each run writes to `reports/<kind>/<label>/`:

- `report.html`: print-ready A4. Open it in a browser and print to PDF, or upload it to the systems Drive, where Google converts it to a Doc.
- `report.md`: the same content, for review in git or chat.
- `charts/*.svg`: the charts, embedded in `report.html`.
- `data.json`: the numbers behind every figure.

`reports/` is generated output and is not committed.

## Schedule

The entries live in `/config/crontab`, which persists across container restarts.

| When (UTC) | What | Covers |
|---|---|---|
| every 5 minutes | `scripts/report/health-sample.sh` samples `/`, `/app`, `/privacy` and `/terms` | appends to `/config/logs/prod-health.csv` |
| Monday 06:11 | weekly report | the previous Monday to Sunday |
| 1st of the month 06:23 | monthly report | the previous month |
| 2 January 06:31 | yearly report | the previous year |

Run output goes to `/config/logs/reports.log`.

## By hand

```
node scripts/report/run.mjs weekly 2026-11-16
node scripts/report/run.mjs monthly 2026-10
node scripts/report/run.mjs yearly 2025
node --test scripts/report/report.test.mjs
```

A weekly anchor is any date in the week. Without an anchor, each run covers the previous complete period.

## Decisions built in

- **No map material.** These reports have no geographic dimension. A map would need case locations, which are personal information under POPIA, so it stays out.
- **No case data.** Peer turns keep only the timestamp, agent, session, status and duration. Prompts and responses are never read.
- **No uptime before sampling began.** Sampling began on 26 November 2026 at 09:41 UTC. Earlier periods show no uptime figures.
- **No PDF engine.** None is installed, so PDF comes from printing `report.html`. The charts are plain SVG with no dependencies.

## Open items

The Open items section reads `scripts/report/open-items.json`. Edit that file when an item closes.

## Limits

- Uptime comes from five-minute samples, so an outage shorter than five minutes can be missed.
- The first monthly report with a full month of uptime is for December 2026, generated on 1 January 2027.
- Drive upload is manual until a Drive uploader is set up.
