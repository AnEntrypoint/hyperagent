import { test } from 'node:test';
import assert from 'node:assert/strict';
import { isoWeekLabel, periodFromAnchor, periodTitle, previousPeriod } from './periods.mjs';
import { healthFor, incidentsOf, readHealth } from './collect.mjs';
import { buildReport } from './model.mjs';
import { barChartSvg, chartSvgs, toHtml, toMarkdown } from './render.mjs';

const at = (iso, path, code) => ({ ts: iso, ms: Date.parse(iso), path, code });

const openItems = [
  { id: 'a', kind: 'decision', title: 'Retention schedule adoption', owner: 'owner', status: 'open', closes: 'adopt' },
  { id: 'b', kind: 'evidence', title: 'Probe event unproved', owner: 'uhh', status: 'open', closes: 'recur' },
];

const commit = { sha: 'abc123456', when: '2026-11-24T10:00:00+02:00', author: 'lanmower', subject: 'fix | table cell' };

const fixture = (period, rows = [], delivery = [], turns = []) => ({
  generatedAt: '2026-11-30T06:11:00.000Z',
  period,
  delivery,
  peers: { turns },
  health: healthFor(period, rows, Date.parse('2026-11-30T06:11:00Z')),
  openItems,
});

test('ISO week labels follow the Thursday rule', () => {
  assert.equal(isoWeekLabel(new Date('2026-11-26T00:00:00Z')), '2026-W48');
  assert.equal(isoWeekLabel(new Date('2027-01-01T00:00:00Z')), '2026-W53');
});

test('weekly anchor covers Monday up to the next Monday', () => {
  const period = periodFromAnchor('weekly', '2026-11-26');
  assert.equal(period.label, '2026-W48');
  assert.equal(period.start.toISOString(), '2026-11-23T00:00:00.000Z');
  assert.equal(period.end.toISOString(), '2026-11-30T00:00:00.000Z');
  assert.equal(periodTitle(period), 'Weekly report 2026-W48, 2026-11-23 to 2026-11-29');
});

test('previous periods are the last complete ones', () => {
  const now = new Date('2026-11-26T10:00:00Z');
  assert.equal(previousPeriod('weekly', now).label, '2026-W47');
  assert.equal(previousPeriod('monthly', now).label, '2026-10');
  assert.equal(previousPeriod('monthly', new Date('2026-01-05T00:00:00Z')).label, '2025-12');
  assert.equal(previousPeriod('yearly', now).label, '2025');
});

test('monthly and yearly anchors', () => {
  const month = periodFromAnchor('monthly', '2026-10');
  assert.equal(month.start.toISOString(), '2026-10-01T00:00:00.000Z');
  assert.equal(month.end.toISOString(), '2026-11-01T00:00:00.000Z');
  assert.equal(periodFromAnchor('yearly', '2026').end.toISOString(), '2027-01-01T00:00:00.000Z');
});

test('consecutive failing samples form one incident', () => {
  const rows = [
    at('2026-11-26T10:00:00Z', '/', '200'),
    at('2026-11-26T10:05:00Z', '/', '502'),
    at('2026-11-26T10:10:00Z', '/', '000'),
    at('2026-11-26T10:15:00Z', '/', '200'),
    at('2026-11-26T10:20:00Z', '/', '503'),
  ];
  const incidents = incidentsOf(rows);
  assert.equal(incidents.length, 2);
  assert.equal(incidents[0].samples, 2);
  assert.equal(incidents[0].minutes, 10);
  assert.deepEqual(incidents[0].codes, ['000', '502']);
  assert.equal(incidents[1].start, '2026-11-26T10:20:00Z');
});

test('a period without health samples reports no uptime', () => {
  const period = periodFromAnchor('monthly', '2026-10');
  const health = healthFor(period, [], Date.parse('2026-11-30T00:00:00Z'));
  assert.equal(health.samples, 0);
  assert.equal(health.uptimePct, null);
  assert.equal(health.coveragePct, null);
  assert.equal(health.firstSampleAt, null);
});

test('the health log is read defensively', () => {
  assert.deepEqual(readHealth('/nonexistent/prod-health.csv'), []);
});

test('weekly report renders BLUF, sections and the map decision', () => {
  const period = periodFromAnchor('weekly', '2026-11-26');
  const rows = [
    at('2026-11-24T10:00:00Z', '/', '200'),
    at('2026-11-24T10:05:00Z', '/', '502'),
    at('2026-11-24T10:00:00Z', '/app', '200'),
    at('2026-11-24T10:05:00Z', '/app', '200'),
  ];
  const delivery = [{ name: 'uhh', commits: [commit], authors: ['lanmower'], changes: 2, insertions: 10, deletions: 3 }];
  const turns = [{ ts: '2026-11-24T09:58:45Z', agent: 'uhh', session: 's', status: 'turn_end', durationS: 126 }];
  const report = buildReport(fixture(period, rows, delivery, turns));
  const markdown = toMarkdown(report);
  assert.match(markdown, /^# Weekly report 2026-W48/);
  assert.match(markdown, /> \*\*BLUF\.\*\*/);
  assert.match(markdown, /## Production health/);
  assert.match(markdown, /fix \\\| table cell/);
  assert.match(markdown, /Map material: none/);
  assert.match(markdown, /!\[Minutes with a failing path, per day\]\(charts\/downtime\.svg\)/);
  assert.doesNotMatch(markdown, /undefined|NaN/);
  const html = toHtml(report, chartSvgs(report));
  assert.match(html, /<section class="bluf">/);
  assert.match(html, /<svg /);
  assert.doesNotMatch(html, /undefined|NaN/);
});

test('empty periods render honest placeholders', () => {
  const period = periodFromAnchor('yearly', '2025');
  const report = buildReport(fixture(period));
  const markdown = toMarkdown(report);
  assert.match(markdown, /Sampling has not started/);
  assert.match(markdown, /No peer turns in this period/);
  assert.match(markdown, /No commits in this period/);
  assert.doesNotMatch(markdown, /undefined|NaN/);
});

test('monthly BLUF names the decisions waiting on the owner', () => {
  const period = periodFromAnchor('monthly', '2026-11');
  const report = buildReport(fixture(period));
  assert.match(report.headline, /1 decision waits on you/);
  assert.match(report.bluf[3], /Retention schedule adoption/);
});

test('chart titles are escaped in SVG output', () => {
  const svg = barChartSvg({ title: '<b>x</b>', labels: ['a'], values: [1], unit: 'u' });
  assert.match(svg, /&lt;b&gt;x&lt;\/b&gt;/);
  assert.doesNotMatch(svg, /<b>x<\/b>/);
});
