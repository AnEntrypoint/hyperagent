const DAY_MS = 86400000;
const WEEK_MS = 7 * DAY_MS;

const pad = (value) => String(value).padStart(2, '0');
const utc = (year, month, day) => new Date(Date.UTC(year, month, day));

export const dateOnly = (date) => date.toISOString().slice(0, 10);

export const mondayOf = (date) => {
  const offset = (date.getUTCDay() + 6) % 7;
  return utc(date.getUTCFullYear(), date.getUTCMonth(), date.getUTCDate() - offset);
};

export const isoWeekLabel = (date) => {
  const monday = mondayOf(date);
  const thursday = new Date(monday.getTime() + 3 * DAY_MS);
  const year = thursday.getUTCFullYear();
  const week = Math.round((monday - mondayOf(utc(year, 0, 4))) / WEEK_MS) + 1;
  return `${year}-W${pad(week)}`;
};

const weekPeriod = (date) => {
  const start = mondayOf(date);
  return { kind: 'weekly', label: isoWeekLabel(start), start, end: new Date(start.getTime() + WEEK_MS) };
};

const monthPeriod = (year, month) => {
  const start = utc(year, month, 1);
  const end = utc(year, month + 1, 1);
  return { kind: 'monthly', label: `${start.getUTCFullYear()}-${pad(start.getUTCMonth() + 1)}`, start, end };
};

const yearPeriod = (year) => ({
  kind: 'yearly',
  label: String(year),
  start: utc(year, 0, 1),
  end: utc(year + 1, 0, 1),
});

export const periodFromAnchor = (kind, anchor) => {
  if (kind === 'weekly') {
    const date = new Date(`${anchor}T00:00:00Z`);
    if (Number.isNaN(date.getTime())) throw new Error(`bad weekly anchor: ${anchor}`);
    return weekPeriod(date);
  }
  if (kind === 'monthly') {
    const [year, month] = anchor.split('-').map(Number);
    return monthPeriod(year, month - 1);
  }
  if (kind === 'yearly') return yearPeriod(Number(anchor));
  throw new Error(`unknown report kind: ${kind}`);
};

export const previousPeriod = (kind, now = new Date()) => {
  if (kind === 'weekly') return weekPeriod(new Date(mondayOf(now).getTime() - WEEK_MS));
  if (kind === 'monthly') return monthPeriod(now.getUTCFullYear(), now.getUTCMonth() - 1);
  if (kind === 'yearly') return yearPeriod(now.getUTCFullYear() - 1);
  throw new Error(`unknown report kind: ${kind}`);
};

export const periodTitle = (period) => {
  if (period.kind === 'weekly') {
    const last = new Date(period.end.getTime() - DAY_MS);
    return `Weekly report ${period.label}, ${dateOnly(period.start)} to ${dateOnly(last)}`;
  }
  if (period.kind === 'monthly') return `Monthly report ${period.label}`;
  return `Yearly report ${period.label}`;
};

export const inPeriod = (period, ms) => ms >= period.start.getTime() && ms < period.end.getTime();
