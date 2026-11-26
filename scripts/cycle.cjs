#!/usr/bin/env node
const { execSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const peers = JSON.parse(fs.readFileSync(path.join(__dirname, 'peers.json'), 'utf8'));
const journalRoot = process.env.HYPERAGENT_JOURNAL || path.join(root, '.orch/projects');
const ghHelper = '-c credential.helper= -c credential.helper="!gh auth git-credential"';
const systemProcesses = /s6|dbus|gvfs|xfce|Xvfb|ssh-agent|gpg-agent|at-spi|dconf|udisks|crashpad|tumbler|gate-ticket\.cjs|ccbridge serve|hyperagent orch p/;

const sh = (command, cwd) => {
  try {
    return execSync(command, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'], timeout: 60000 }).trim();
  } catch {
    return '';
  }
};
const oneLine = (text, limit) => String(text ?? '').replace(/\s+/g, ' ').trim().slice(0, limit);
const peerRoots = () => {
  const candidates = [];
  if (process.env.HYPERAGENT_PEERS_ROOTS) candidates.push(...process.env.HYPERAGENT_PEERS_ROOTS.split(path.delimiter).filter(Boolean));
  candidates.push(path.dirname(root));
  if (process.env.HYPERAGENT_SCRATCH) candidates.push(path.dirname(process.env.HYPERAGENT_SCRATCH));
  return candidates.filter(Boolean);
};
const checkoutOf = (peer) => {
  if (path.isAbsolute(peer.checkout)) return peer.checkout;
  const roots = peerRoots();
  for (const base of roots) {
    const candidate = path.resolve(base, peer.checkout);
    if (fs.existsSync(candidate)) return candidate;
  }
  return path.resolve(roots[0], peer.checkout);
};
const slugOf = (cwd) => Array.from(path.resolve(cwd)).map((symbol) => /[0-9A-Za-z]/.test(symbol) ? symbol : '-').join('');
const journalOf = (peer) => path.join(journalRoot, slugOf(checkoutOf(peer)), peer.session + '.jsonl');
const unboundJournalOf = (peer) => path.join(journalRoot, slugOf(checkoutOf(peer)), `peer-${peer.name}.jsonl`);
const parse = (lines) => lines.flatMap((line) => {
  try {
    return [JSON.parse(line)];
  } catch {
    return [];
  }
});
const tailRecords = (file, bytes) => {
  if (!fs.existsSync(file)) return [];
  const size = fs.statSync(file).size;
  const start = Math.max(0, size - bytes);
  const buffer = Buffer.alloc(size - start);
  const fd = fs.openSync(file, 'r');
  fs.readSync(fd, buffer, 0, buffer.length, start);
  fs.closeSync(fd);
  return parse(buffer.toString('utf8').split('\n').slice(start ? 1 : 0));
};
const allRecords = (file) => (fs.existsSync(file) ? parse(fs.readFileSync(file, 'utf8').split('\n')) : []);
const ageOf = (record) => (record?.ts ? Math.round((Date.now() - Date.parse(record.ts)) / 1000) + 's' : '?');
const timeOf = (record) => {
  const parsed = Date.parse(record?.ts || '');
  if (Number.isFinite(parsed)) return parsed;
  const epoch = Number(record?.epoch || 0);
  return epoch > 1e12 ? epoch : epoch * 1000;
};
const recordsFor = (peer) => [journalOf(peer), unboundJournalOf(peer)].flatMap(allRecords).sort((left, right) => timeOf(left) - timeOf(right));
const newestRecords = (peer) => {
  const bound = tailRecords(journalOf(peer), 400000);
  const unbound = tailRecords(unboundJournalOf(peer), 400000);
  return timeOf(unbound.at(-1)) > timeOf(bound.at(-1)) ? unbound : bound;
};

let liveCache;
const liveSessions = () => liveCache ??= (() => {
  const port = process.env.ORCH_PORT || '8788';
  const host = process.env.ORCH_HOST || '127.0.0.1';
  const body = sh('curl -s -m 2 http://' + host + ':' + port + '/sessions');
  try { return JSON.parse(body); } catch { return {}; }
})();

const remoteHead = {
  gh: (cwd) => sh('git ' + ghHelper + ' fetch origin --quiet; git rev-parse --short origin/main', cwd),
  fetch: (cwd) => sh('git fetch origin --quiet; git rev-parse --short origin/main', cwd),
  'ls-remote': (cwd) => sh('git ls-remote origin main', cwd).slice(0, 7),
};

const queueOf = (name) => {
  const file = path.join(root, 'specs/queue', name + '.md');
  return fs.existsSync(file) ? fs.readFileSync(file, 'utf8').split('\n').filter((line) => /^\d+\. /.test(line)) : [];
};

function peerLine(peer) {
  const cwd = checkoutOf(peer);
  const records = newestRecords(peer);
  const last = records.at(-1);
  const lastText = records.findLast((record) => record.kind === 'text' && record.text);
  const ended = last?.kind === 'result';
  const exists = fs.existsSync(cwd);
  const head = exists ? sh('git rev-parse --short HEAD', cwd) : '';
  const origin = exists ? remoteHead[peer.remote](cwd) : '';
  const ahead = !exists ? '' : peer.remote === 'ls-remote' ? (head === origin ? '0' : '?') : sh('git rev-list --count origin/main..HEAD', cwd);
  const dirty = exists ? sh('git status --short | wc -l', cwd) : '';
  const queued = queueOf(peer.name).length;
  const tickets = ticketRows().filter((row) => row.owner === peer.name).length;
  const live = liveSessions()[peer.name];
  const due = tickets === 0 && queued > 0;
  const state = !exists ? 'MISSING' : !live ? (ended ? (due ? 'ENDED DUE' : 'ENDED') : 'STOPPED') : live.busy ? 'running' : ended ? (due ? 'ENDED DUE' : 'ENDED') : 'IDLE';
  return [peer.name, state, 'last=' + ageOf(last), 'HEAD=' + head, 'origin=' + origin, 'ahead=' + ahead, 'dirty=' + dirty, 'queue=' + queued, 'tickets=' + tickets, 'live=' + (live ? '1' : '0'), '| ' + oneLine(lastText?.text, 260)].join(' ');
}

let ticketCache;
const ticketRows = () => ticketCache ??= sh('node ' + path.join(__dirname, 'gate-ticket.cjs') + ' list').split('\n').filter(Boolean).map((file) => {
  const pid = file.replace(/.*-(\d+)\.json$/, '$1');
  const args = oneLine(sh('ps -o etimes=,args= -p ' + pid), 110);
  const label = (/ run ([^\s]+)/.exec(args) || [])[1] || '';
  const owner = [...peers].sort((left, right) => right.name.length - left.name.length).find((peer) => label === peer.name || label.startsWith(peer.name + '-'))?.name || '';
  return { pid, args, child: sh('pgrep -P ' + pid).split('\n')[0], owner };
});

function ticketLines() {
  return ticketRows().map((row) => 'ticket ' + row.pid + ' ' + (row.child ? 'RUNNING child=' + row.child : 'queued') + ' ' + row.args);
}

function hostLines() {
  const vmColumns = (sh('vmstat 1 2').split('\n').at(-1) || '').trim().split(/\s+/);
  const swap = Number(vmColumns[2] || 0);
  const swapIn = vmColumns[6] || '?';
  const swapOut = vmColumns[7] || '?';
  const idle = vmColumns[14] || '?';
  const available = sh("free -m | awk 'NR==2{print $7}'") || '?';
  const disk = (target) => sh('df --output=avail -BG ' + target + ' | tail -1').trim() || '?';
  const orphans = sh('ps -eo pid,ppid,etimes,rss,args').split('\n').slice(1)
    .map((line) => line.trim().split(/\s+/))
    .filter(([, parent, , , ...args]) => parent === '1' && !systemProcesses.test(args.join(' ')))
    .map(([pid, , age, rss, ...args]) => 'orphan ' + pid + ' ' + age + 's ' + Math.round(rss / 1024) + 'MB ' + args.join(' ').slice(0, 90));
  const network = sh('curl -s -o /dev/null -m 6 -w "%{http_code}" https://api.anthropic.com/');
  const swapText = Number.isFinite(swap) ? Math.round(swap / 1024) + 'MB' : '?';
  return [
    'host ' + new Date().toISOString().slice(11, 19) + ' si=' + swapIn + ' so=' + swapOut + ' KB/s idle=' + idle + '% available=' + available + 'MB swap=' + swapText + ' root=' + disk(path.parse(root).root) + ' workspace=' + disk(path.dirname(root)) + ' api=' + (network || 'unreachable'),
    ...orphans,
  ];
}

function specLines() {
  const dir = path.join(root, 'specs');
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir).filter((name) => name.endsWith('.md')).map((name) => 'spec ' + name);
}

function sent(name, phrase) {
  const peer = peers.find((candidate) => candidate.name === name);
  if (!peer) throw new Error('unknown peer ' + name);
  const records = recordsFor(peer);
  const wanted = oneLine(phrase, phrase.length);
  const index = records.findLastIndex((record) => oneLine(record.text, 1e9).includes(wanted));
  if (index < 0) return ['NOT DELIVERED ' + name + ': the phrase is not in the journal'];
  const after = records.slice(index + 1).filter((record) => record.kind === 'text' || record.kind === 'tool' || record.kind === 'result');
  return [
    (after.length ? 'DELIVERED ' : 'IN JOURNAL, NO RECORD AFTER IT YET ') + name + ' at ' + records[index].ts + ', ' + after.length + ' records after',
    ...after.slice(-4).map((record) => record.ts + ' ' + record.kind + ' ' + oneLine(record.text, 200)),
  ];
}

const [command, ...rest] = process.argv.slice(2);
const lines = command === 'sent' ? sent(rest[0], rest.slice(1).join(' ')) : command === 'queue' ? queueOf(rest[0]) : [...peers.map(peerLine), ...ticketLines(), ...specLines(), ...hostLines()];
process.stdout.write(lines.join('\n') + '\n');
