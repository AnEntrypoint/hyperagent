import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { inPeriod } from './periods.mjs';

export const SAMPLE_MINUTES = 5;
export const PATHS = ['/', '/app', '/privacy', '/terms'];
export const HEALTH_LOG = '/config/logs/prod-health.csv';
const HYPER = '/config/hyperagent';
const UHH = '/config/uhh';
export const JOURNAL_ROOT = join(HYPER, '.orch/projects');
export const REPOS = [
  { name: 'hyperagent', path: HYPER },
  { name: 'uhh', path: UHH },
  { name: 'casey', path: join(UHH, 'deps/casey') },
  { name: 'freddie', path: join(UHH, 'deps/casey/deps/freddie') },
];

const git = (path, args) =>
  execFileSync('git', ['-C', path, ...args], { encoding: 'utf8', maxBuffer: 256 * 1024 * 1024 });

const gitWindow = (period) => [
  '--since', period.start.toISOString(),
  '--until', period.end.toISOString(),
  '--no-merges',
];

const totalsOf = (numstat) =>
  numstat
    .split('\n')
    .filter(Boolean)
    .reduce(
      (acc, line) => {
        const [added, removed] = line.split('\t');
        return {
          changes: acc.changes + 1,
          insertions: acc.insertions + (Number(added) || 0),
          deletions: acc.deletions + (Number(removed) || 0),
        };
      },
      { changes: 0, insertions: 0, deletions: 0 },
    );

export const collectDelivery = (period, repos = REPOS) =>
  repos
    .filter((repo) => existsSync(join(repo.path, '.git')))
    .map((repo) => {
      const log = git(repo.path, ['log', ...gitWindow(period), '--format=%H%x1f%aI%x1f%an%x1f%s']).trim();
      const commits = log
        ? log.split('\n').map((line) => {
            const [sha, when, author, subject] = line.split('\x1f');
            return { sha: sha.slice(0, 9), when, author, subject: subject.slice(0, 160) };
          })
        : [];
      const totals = totalsOf(git(repo.path, ['log', ...gitWindow(period), '--numstat', '--format=']));
      return {
        name: repo.name,
        commits,
        authors: [...new Set(commits.map((commit) => commit.author))],
        ...totals,
      };
    });

const safeJson = (line) => {
  try {
    return JSON.parse(line);
  } catch {
    return null;
  }
};

const journalFiles = (root) =>
  readdirSync(root)
    .map((project) => join(root, project))
    .filter((dir) => statSync(dir).isDirectory())
    .flatMap((dir) =>
      readdirSync(dir)
        .filter((name) => name.endsWith('.jsonl'))
        .map((name) => join(dir, name)),
    );

export const collectPeers = (period, root = JOURNAL_ROOT) => {
  if (!existsSync(root)) return { turns: [] };
  const seen = new Set();
  const turns = [];
  for (const file of journalFiles(root)) {
    for (const line of readFileSync(file, 'utf8').split('\n')) {
      if (!line.includes('"kind":"result"')) continue;
      const record = safeJson(line);
      if (!record || !inPeriod(period, Date.parse(record.ts))) continue;
      const key = `${record.session}|${record.ts}`;
      if (seen.has(key)) continue;
      seen.add(key);
      turns.push({
        ts: record.ts,
        agent: record.agent,
        session: record.session,
        status: record.status,
        durationS: Number(record.duration_s) || 0,
      });
    }
  }
  return { turns };
};

export const readHealth = (file = HEALTH_LOG) => {
  if (!existsSync(file)) return [];
  return readFileSync(file, 'utf8')
    .split('\n')
    .filter(Boolean)
    .map((line) => {
      const [ts, path, code] = line.split(',');
      return { ts, ms: Date.parse(ts), path, code };
    })
    .filter((row) => Number.isFinite(row.ms));
};

const finishIncident = (open) => ({
  start: open.start,
  end: open.end,
  samples: open.samples,
  minutes: open.samples * SAMPLE_MINUTES,
  codes: [...open.codes].sort(),
});

export const incidentsOf = (rows) => {
  const incidents = [];
  let open = null;
  for (const row of rows) {
    if (row.code !== '200') {
      open ??= { start: row.ts, end: row.ts, samples: 0, codes: new Set() };
      open.end = row.ts;
      open.samples += 1;
      open.codes.add(row.code);
    } else if (open) {
      incidents.push(finishIncident(open));
      open = null;
    }
  }
  if (open) incidents.push(finishIncident(open));
  return incidents;
};

export const healthFor = (period, rows, now = Date.now()) => {
  const inWindow = rows.filter((row) => inPeriod(period, row.ms));
  const firstMs = rows.length ? rows.reduce((min, row) => Math.min(min, row.ms), Infinity) : null;
  const perPath = PATHS.map((path) => {
    const mine = inWindow.filter((row) => row.path === path).sort((a, b) => a.ms - b.ms);
    const ok = mine.filter((row) => row.code === '200').length;
    return {
      path,
      samples: mine.length,
      uptimePct: mine.length ? (100 * ok) / mine.length : null,
      incidents: incidentsOf(mine),
    };
  });
  const ok = inWindow.filter((row) => row.code === '200').length;
  const from = firstMs === null ? null : Math.max(period.start.getTime(), firstMs);
  const to = Math.min(period.end.getTime(), now);
  const expectedSamples =
    from === null || to <= from
      ? 0
      : Math.round(((to - from) / (SAMPLE_MINUTES * 60000)) * PATHS.length);
  return {
    firstSampleAt: firstMs === null ? null : new Date(firstMs).toISOString(),
    samples: inWindow.length,
    expectedSamples,
    coveragePct: expectedSamples ? Math.min(100, (100 * inWindow.length) / expectedSamples) : null,
    uptimePct: inWindow.length ? (100 * ok) / inWindow.length : null,
    perPath,
    rows: inWindow,
  };
};
