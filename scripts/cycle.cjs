#!/usr/bin/env node
const { execSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const peers = JSON.parse(fs.readFileSync(path.join(__dirname, 'peers.json'), 'utf8'));
const ghHelper = '-c credential.helper= -c credential.helper="!gh auth git-credential"';
const systemProcesses = /s6|dbus|gvfs|xfce|Xvfb|ssh-agent|gpg-agent|at-spi|dconf|udisks|crashpad|tumbler|gate-ticket\.cjs/;

const sh = (command, cwd) => {
  try {
    return execSync(command, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'], timeout: 60000 }).trim();
  } catch {
    return '';
  }
};
const oneLine = (text, limit) => String(text ?? '').replace(/\s+/g, ' ').trim().slice(0, limit);
const journalOf = (peer) => path.join(root, '.orch/projects', peer.journal, peer.session + '.jsonl');
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
  const records = tailRecords(journalOf(peer), 400000);
  const last = records.at(-1);
  const lastText = records.findLast((record) => record.kind === 'text' && record.text);
  const ended = last?.kind === 'result';
  const head = sh('git rev-parse --short HEAD', peer.checkout);
  const origin = remoteHead[peer.remote](peer.checkout);
  const ahead = peer.remote === 'ls-remote' ? (head === origin ? '0' : '?') : sh('git rev-list --count origin/main..HEAD', peer.checkout);
  const dirty = sh('git status --short | wc -l', peer.checkout);
  const queued = queueOf(peer.name).length;
  const tickets = ticketRows().filter((row) => row.owner === peer.name).length;
  const state = !ended ? 'running' : tickets === 0 && queued > 0 ? 'ENDED DUE' : 'ENDED';
  return [peer.name, state, 'last=' + ageOf(last), 'HEAD=' + head, 'origin=' + origin, 'ahead=' + ahead, 'dirty=' + dirty, 'queue=' + queued, 'tickets=' + tickets, '| ' + oneLine(lastText?.text, 260)].join(' ');
}

let ticketCache;
const ticketRows = () => ticketCache ??= sh('node ' + path.join(__dirname, 'gate-ticket.cjs') + ' list').split('\n').filter(Boolean).map((file) => {
  const pid = file.replace(/.*-(\d+)\.json$/, '$1');
  const args = oneLine(sh('ps -o etimes=,args= -p ' + pid), 110);
  return { pid, args, child: sh('pgrep -P ' + pid).split('\n')[0], owner: (/ run ([^\s-]+)/.exec(args) || [])[1] || '' };
});

function ticketLines() {
  return ticketRows().map((row) => 'ticket ' + row.pid + ' ' + (row.child ? 'RUNNING child=' + row.child : 'queued') + ' ' + row.args);
}

function hostLines() {
  const [, , swap, , , , swapIn, swapOut, , , , , , , idle] = sh('vmstat 1 2').split('\n').at(-1).trim().split(/\s+/);
  const available = sh("free -m | awk 'NR==2{print $7}'");
  const disk = (mount) => sh('df --output=avail -BG ' + mount + ' | tail -1').trim();
  const orphans = sh('ps -eo pid,ppid,etimes,rss,args').split('\n').slice(1)
    .map((line) => line.trim().split(/\s+/))
    .filter(([, parent, , , ...args]) => parent === '1' && !systemProcesses.test(args.join(' ')))
    .map(([pid, , age, rss, ...args]) => 'orphan ' + pid + ' ' + age + 's ' + Math.round(rss / 1024) + 'MB ' + args.join(' ').slice(0, 90));
  const network = sh('curl -s -o /dev/null -m 6 -w "%{http_code}" https://api.anthropic.com/');
  return [
    'host ' + new Date().toISOString().slice(11, 19) + ' si=' + swapIn + ' so=' + swapOut + ' KB/s idle=' + idle + '% available=' + available + 'MB swap=' + Math.round(swap / 1024) + 'MB root=' + disk('/') + ' workspace=' + disk('/workspace') + ' api=' + (network || 'unreachable'),
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
  const records = allRecords(journalOf(peer));
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
