import { mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { periodFromAnchor, previousPeriod } from './periods.mjs';
import { HEALTH_LOG, collectDelivery, collectPeers, healthFor, readHealth } from './collect.mjs';
import { buildReport } from './model.mjs';
import { chartSvgs, toHtml, toMarkdown } from './render.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const root = resolve(here, '../..');
const KINDS = ['weekly', 'monthly', 'yearly'];

const [kind, anchor] = process.argv.slice(2);
if (!KINDS.includes(kind)) {
  process.stderr.write('usage: run.mjs weekly|monthly|yearly [anchor]\n');
  process.exit(2);
}

const now = Date.now();
const period = anchor ? periodFromAnchor(kind, anchor) : previousPeriod(kind, new Date(now));
const health = healthFor(period, readHealth(HEALTH_LOG), now);
const openItems = JSON.parse(readFileSync(join(here, 'open-items.json'), 'utf8'));
const data = {
  generatedAt: new Date(now).toISOString(),
  period,
  delivery: collectDelivery(period),
  peers: collectPeers(period),
  health,
  openItems,
};

const report = buildReport(data);
const svgs = chartSvgs(report);
const dir = join(root, 'reports', kind, period.label);
mkdirSync(join(dir, 'charts'), { recursive: true });
for (const [name, svg] of Object.entries(svgs)) {
  writeFileSync(join(dir, 'charts', `${name}.svg`), svg);
}
writeFileSync(join(dir, 'report.md'), toMarkdown(report));
writeFileSync(join(dir, 'report.html'), toHtml(report, svgs));
const { rows, ...healthSummary } = health;
writeFileSync(
  join(dir, 'data.json'),
  `${JSON.stringify({ ...data, health: healthSummary, period: { kind, label: period.label, start: period.start, end: period.end } }, null, 2)}\n`,
);
process.stdout.write(`${dir}\n`);
