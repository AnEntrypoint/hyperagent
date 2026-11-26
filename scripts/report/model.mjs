import { SAMPLE_MINUTES } from './collect.mjs';
import { dateOnly, isoWeekLabel, mondayOf, periodTitle } from './periods.mjs';

const DAY_MS = 86400000;
const WEEK_MS = 7 * DAY_MS;
const UNIT = { weekly: 'day', monthly: 'week', yearly: 'month' };
const UNIT_NAME = { day: 'day', week: 'week', month: 'month' };
const SUBJECT_LIMIT = 15;

const pad = (value) => String(value).padStart(2, '0');
const plural = (count, word, many = `${word}s`) => `${count} ${count === 1 ? word : many}`;
const cap = (text) => text.charAt(0).toUpperCase() + text.slice(1);
const pct = (value) => (value === null ? 'no samples' : `${value.toFixed(2)}%`);
const stamp = (value) => `${new Date(value).toISOString().slice(0, 16).replace('T', ' ')} UTC`;

const bucketOf = (ms, unit) => {
  const date = new Date(ms);
  if (unit === 'day') return dateOnly(date);
  if (unit === 'week') return isoWeekLabel(date);
  return `${date.getUTCFullYear()}-${pad(date.getUTCMonth() + 1)}`;
};

const bucketKeys = (period, unit) => {
  const keys = [];
  const end = period.end.getTime();
  if (unit === 'day') {
    for (let t = period.start.getTime(); t < end; t += DAY_MS) keys.push(bucketOf(t, unit));
  } else if (unit === 'week') {
    for (let t = mondayOf(period.start).getTime(); t < end; t += WEEK_MS) keys.push(bucketOf(t, unit));
  } else {
    const cursor = new Date(period.start);
    while (cursor.getTime() < end) {
      keys.push(bucketOf(cursor.getTime(), unit));
      cursor.setUTCMonth(cursor.getUTCMonth() + 1, 1);
    }
  }
  return keys;
};

const countBy = (keys, items, unit, when, weight = () => 1) => {
  const totals = new Map(keys.map((key) => [key, 0]));
  for (const item of items) {
    const key = bucketOf(when(item), unit);
    if (totals.has(key)) totals.set(key, totals.get(key) + weight(item));
  }
  return keys.map((key) => totals.get(key));
};

const emptyOr = (rows, head, text) =>
  rows.length ? { type: 'table', head, rows } : { type: 'p', text };

export const buildReport = (data) => {
  const { period, delivery, peers, health, openItems, generatedAt } = data;
  const unit = UNIT[period.kind];
  const keys = bucketKeys(period, unit);
  const commits = delivery.flatMap((repo) => repo.commits.map((commit) => ({ ...commit, repo: repo.name })));
  const totals = {
    commits: commits.length,
    authors: new Set(commits.map((commit) => commit.author)).size,
    activeRepos: delivery.filter((repo) => repo.commits.length > 0).length,
    insertions: delivery.reduce((sum, repo) => sum + repo.insertions, 0),
    deletions: delivery.reduce((sum, repo) => sum + repo.deletions, 0),
  };
  const turns = peers.turns;
  const notFinished = turns.filter((turn) => turn.status !== 'turn_end');
  const peerHours = turns.reduce((sum, turn) => sum + turn.durationS, 0) / 3600;
  const incidents = health.perPath.flatMap((entry) =>
    entry.incidents.map((incident) => ({ ...incident, path: entry.path })),
  );
  const longest = incidents.reduce((max, incident) => Math.max(max, incident.minutes), 0);
  const decisions = openItems.filter((item) => item.kind === 'decision' && item.status !== 'closed');
  const gaps = openItems.filter((item) => item.kind === 'evidence' && item.status !== 'closed');
  const open = openItems.filter((item) => item.status !== 'closed');

  const sampledFrom = health.firstSampleAt ? stamp(health.firstSampleAt) : null;
  const samplingAfterPeriod = health.firstSampleAt !== null && Date.parse(health.firstSampleAt) >= period.end.getTime();
  const production =
    health.samples > 0
      ? `production returned 200 for ${pct(health.uptimePct)} of samples, with ${plural(incidents.length, 'incident')}${incidents.length ? `, longest ${longest} minutes` : ''}`
      : `production was not measured in this period, because ${sampledFrom ? `sampling began ${sampledFrom}` : 'sampling has not started'}`;
  const decisionClause = decisions.length
    ? `${plural(decisions.length, 'decision')} ${decisions.length === 1 ? 'waits' : 'wait'} on you`
    : 'nothing waits on you';

  const bluf = [
    `**Production:** ${production}.`,
    `**Delivery:** ${plural(totals.commits, 'commit')} in ${plural(totals.activeRepos, 'repository', 'repositories')} by ${plural(totals.authors, 'author')}; ${totals.insertions} lines added, ${totals.deletions} removed.`,
    `**Peers:** ${plural(turns.length, 'turn')}, ${notFinished.length} not finished, ${peerHours.toFixed(1)} hours of peer work.`,
    `**Decisions for you:** ${decisions.length ? decisions.map((item) => item.title).join('; ') : 'none'}.`,
    `**Evidence gaps:** ${gaps.length ? gaps.map((item) => item.title).join('; ') : 'none'}.`,
  ];

  const healthNote = sampledFrom === null
    ? 'Sampling has not started, so this period has no uptime figures.'
    : samplingAfterPeriod
      ? `Sampling began ${sampledFrom}, after this period ended, so this period has no uptime figures.`
      : `Sampling began ${sampledFrom}. Coverage ${health.coveragePct === null ? 'not measurable' : `${health.coveragePct.toFixed(1)}%`}: ${health.samples} of ${health.expectedSamples} expected samples.`;

  const charts = [];
  const downtimeTimes = [...new Set(health.rows.filter((row) => row.code !== '200').map((row) => row.ms))];
  charts.push({
    name: 'downtime',
    title: `Minutes with a failing path, per ${UNIT_NAME[unit]}`,
    labels: keys,
    values: countBy(keys, downtimeTimes, unit, (ms) => ms, () => SAMPLE_MINUTES),
    unit: 'minutes',
  });
  charts.push({
    name: 'commits',
    title: `Commits per ${UNIT_NAME[unit]}`,
    labels: keys,
    values: countBy(keys, commits, unit, (commit) => Date.parse(commit.when)),
    unit: 'commits',
  });
  charts.push({
    name: 'turns',
    title: `Peer turns per ${UNIT_NAME[unit]}`,
    labels: keys,
    values: countBy(keys, turns, unit, (turn) => Date.parse(turn.ts)),
    unit: 'turns',
  });

  const blocks = [];
  blocks.push({ type: 'h2', text: 'Production health' });
  blocks.push({ type: 'p', text: healthNote });
  blocks.push({
    type: 'table',
    head: ['Path', 'Samples', 'Uptime', 'Incidents', 'Longest (minutes)'],
    rows: health.perPath.map((entry) => [
      entry.path,
      entry.samples,
      pct(entry.uptimePct),
      entry.incidents.length,
      entry.incidents.reduce((max, incident) => Math.max(max, incident.minutes), 0),
    ]),
  });
  blocks.push(
    incidents.length
      ? {
          type: 'table',
          head: ['Path', 'Start', 'End', 'Samples', 'Approx. minutes', 'Codes'],
          rows: incidents
            .sort((a, b) => Date.parse(a.start) - Date.parse(b.start))
            .map((incident) => [
              incident.path,
              stamp(incident.start),
              stamp(incident.end),
              incident.samples,
              incident.minutes,
              incident.codes.join(', '),
            ]),
        }
      : { type: 'p', text: 'No incidents in this period.' },
  );
  blocks.push({ type: 'chart', name: 'downtime', alt: charts[0].title });

  blocks.push({ type: 'h2', text: 'Delivery' });
  blocks.push({
    type: 'table',
    head: ['Repository', 'Commits', 'Authors', 'Insertions', 'Deletions', 'File changes'],
    rows: delivery.map((repo) => [
      repo.name,
      repo.commits.length,
      repo.authors.length,
      repo.insertions,
      repo.deletions,
      repo.changes,
    ]),
  });
  blocks.push({ type: 'chart', name: 'commits', alt: charts[1].title });
  blocks.push({ type: 'h3', text: 'Most recent changes' });
  const recent = [...commits]
    .sort((a, b) => Date.parse(b.when) - Date.parse(a.when))
    .slice(0, SUBJECT_LIMIT)
    .map((commit) => [stamp(commit.when), commit.repo, commit.sha, commit.author, commit.subject]);
  blocks.push(emptyOr(recent, ['When', 'Repository', 'Commit', 'Author', 'Subject'], 'No commits in this period.'));

  blocks.push({ type: 'h2', text: 'Peer activity' });
  const byAgent = new Map();
  for (const turn of turns) {
    const entry = byAgent.get(turn.agent) ?? { turns: 0, finished: 0, hours: 0 };
    entry.turns += 1;
    entry.finished += turn.status === 'turn_end' ? 1 : 0;
    entry.hours += turn.durationS / 3600;
    byAgent.set(turn.agent, entry);
  }
  blocks.push(
    emptyOr(
      [...byAgent.entries()].map(([agent, entry]) => [
        agent,
        entry.turns,
        entry.finished,
        entry.turns - entry.finished,
        entry.hours.toFixed(1),
      ]),
      ['Agent', 'Turns', 'Finished', 'Not finished', 'Hours'],
      'No peer turns in this period.',
    ),
  );
  blocks.push({ type: 'chart', name: 'turns', alt: charts[2].title });

  blocks.push({ type: 'h2', text: 'Open items' });
  blocks.push(
    emptyOr(
      open.map((item) => [item.title, item.kind, item.owner, item.status, item.closes]),
      ['Item', 'Kind', 'Owner', 'Status', 'Closes when'],
      'None.',
    ),
  );

  blocks.push({ type: 'h2', text: 'Method and limits' });
  blocks.push({
    type: 'ul',
    items: [
      'Sources: git history of hyperagent, uhh, casey and freddie for the period, merges excluded; peer turn records from the hyperagent journal; production health samples of /, /app, /privacy and /terms taken every five minutes.',
      'Times are UTC. Minutes are sample counts multiplied by five, so each incident is accurate to about five minutes.',
      'Uptime is the share of samples that returned 200. An outage shorter than one sample interval is not visible.',
      'Map material: none. These reports have no geographic dimension. A map would need case locations, which are personal information under POPIA and stay out of these reports.',
      'No case data is read. Peer records keep only timestamps, agent, session, status and duration.',
      `Generated ${stamp(generatedAt)}.`,
    ],
  });

  return {
    title: periodTitle(period),
    headline: `${health.samples > 0 ? `Production uptime ${pct(health.uptimePct)}, ${plural(incidents.length, 'incident')}` : 'Production was not measured'}. ${cap(decisionClause)}.`,
    meta: `Period ${period.label}. Contains no case data.`,
    bluf,
    blocks,
    charts,
  };
};
