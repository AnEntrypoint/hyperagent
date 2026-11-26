#!/usr/bin/env node
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');

const SCRATCH = process.env.GATE_SCRATCH || path.join(process.env.HYPERAGENT_SCRATCH || path.resolve(__dirname, '..', '..', 'tmp'), 'gate');
const TICKET_DIR = process.env.GATE_TICKET_DIR || path.join(SCRATCH, 'tickets');
const LEGACY_TICKET_DIR = path.join(os.tmpdir(), 'gate-tickets');
const HISTORY = process.env.GATE_HISTORY || path.join(SCRATCH, 'history.jsonl');
const LAST_TURNS = path.join(TICKET_DIR, 'turns.json');
const GATE_READING = process.env.GATE_READING_CMD || '';
const POLL_MS = Number(process.env.GATE_POLL_MS || 10000);
const PEAK_SAMPLE_MS = Number(process.env.GATE_PEAK_SAMPLE_MS || 5000);
const LEARN_MARGIN = 1.15;
const LEARN_MIN_SECOND_MB = 3000;
const SLOTS = Number(process.env.GATE_SLOTS || 2);
const FIRST_SLOT_MB = Number(process.env.GATE_FIRST_SLOT_MB || 2500);
const SECOND_SLOT_MB = Number(process.env.GATE_SECOND_SLOT_MB || 6000);
const QUICK_SLOT_MB = Number(process.env.GATE_QUICK_SLOT_MB || 4000);
const SWAP_OUT_LIMIT_KBS = 1000;
const QUICK_SLOTS = Number(process.env.GATE_QUICK_SLOTS || 1);
const TICKET = /^(\d{15})-(\d+)\.json$/;
const TAKES_TURNS = 2;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const ownerOf = (label) => String(label || '').split('-')[0];
const readJson = (file, fallback) => {
    try {
        return JSON.parse(fs.readFileSync(file, 'utf8'));
    } catch {
        return fallback;
    }
};

function alive(pid) {
    try {
        process.kill(pid, 0);
        return true;
    } catch (error) {
        return error.code === 'EPERM';
    }
}

function liveTickets() {
    fs.mkdirSync(TICKET_DIR, { recursive: true });
    const live = [];
    const dirs = [TICKET_DIR, ...(fs.existsSync(LEGACY_TICKET_DIR) && LEGACY_TICKET_DIR !== TICKET_DIR ? [LEGACY_TICKET_DIR] : [])];
    for (const dir of dirs) {
        for (const name of fs.readdirSync(dir).sort()) {
            const match = TICKET.exec(name);
            if (!match) continue;
            if (!alive(Number(match[2]))) {
                fs.rmSync(path.join(dir, name), { force: true });
                continue;
            }
            live.push({ name, ...readJson(path.join(dir, name), {}) });
        }
    }
    return live;
}

function historyRows() {
    if (!fs.existsSync(HISTORY)) return [];
    return fs.readFileSync(HISTORY, 'utf8').split('\n').filter(Boolean).map((line) => JSON.parse(line));
}

function learnedMb(label) {
    const peaks = historyRows().filter((row) => row.label === label && row.peakMb > 0).slice(-3).map((row) => row.peakMb);
    return peaks.length ? Math.ceil(Math.max(...peaks) * LEARN_MARGIN) : 0;
}

function treeRssKb(root) {
    const kids = new Map();
    for (const entry of fs.readdirSync('/proc')) {
        if (!/^\d+$/.test(entry)) continue;
        try {
            const stat = fs.readFileSync('/proc/' + entry + '/stat', 'utf8');
            const ppid = Number(stat.slice(stat.lastIndexOf(')') + 2).split(' ')[1]);
            if (!kids.has(ppid)) kids.set(ppid, []);
            kids.get(ppid).push(Number(entry));
        } catch {}
    }
    let total = 0;
    const stack = [root];
    while (stack.length) {
        const pid = stack.pop();
        try {
            const status = fs.readFileSync('/proc/' + pid + '/status', 'utf8');
            const rss = /VmRSS:\s+(\d+)/.exec(status);
            if (rss) total += Number(rss[1]);
        } catch {}
        for (const kid of kids.get(pid) || []) stack.push(kid);
    }
    return total;
}

function inTurn(tickets) {
    if (!tickets.every((ticket) => ticket.v === TAKES_TURNS)) return tickets;
    const last = readJson(LAST_TURNS, {});
    const queuedOwners = new Set();
    for (const ticket of tickets) {
        const owner = ownerOf(ticket.label);
        ticket.rank = ticket.granted ? 0 : queuedOwners.has(owner) ? 2 : 1;
        ticket.ownerLastTurn = ticket.rank === 1 ? last[owner] || 0 : 0;
        if (!ticket.granted) queuedOwners.add(owner);
    }
    return tickets.sort((a, b) => a.rank - b.rank || a.ownerLastTurn - b.ownerLastTurn || a.name.localeCompare(b.name));
}

function takeTicket(ownerPid, label) {
    const latest = liveTickets().reduce((max, ticket) => Math.max(max, Number(ticket.name.slice(0, 15))), 0);
    const arrival = Math.max(Date.now(), latest + 1);
    const name = String(arrival).padStart(15, '0') + '-' + ownerPid + '.json';
    const ticket = { pid: ownerPid, arrival, label, v: TAKES_TURNS, exclusive: process.env.GATE_EXCLUSIVE === '1', quick: process.env.GATE_QUICK === '1', minMb: Number(process.env.GATE_MIN_MB || 0), learnedMb: learnedMb(label), tree: process.env.GATE_TREE || '' };
    fs.writeFileSync(path.join(TICKET_DIR, name), JSON.stringify(ticket) + '\n');
    return { name, ticket };
}

function availableMb() {
    if (process.env.GATE_MEMORY_MB) return Number(process.env.GATE_MEMORY_MB);
    if (GATE_READING && !process.env.GATE_MEMORY_MB) return Infinity;
    const line = fs.readFileSync('/proc/meminfo', 'utf8').split('\n').find((row) => row.startsWith('MemAvailable:'));
    return Math.floor(Number(line.split(/\s+/)[1]) / 1024);
}

function hostHasRoom(floorMb) {
    if (GATE_READING) return spawnSync('bash', ['-c', GATE_READING], { stdio: 'ignore' }).status === 0 && availableMb() >= floorMb;
    const vm = spawnSync('vmstat', ['1', '2'], { encoding: 'utf8' }).stdout.trim().split('\n');
    const swapOut = Number(vm[vm.length - 1].trim().split(/\s+/)[7]);
    return swapOut < SWAP_OUT_LIMIT_KBS && availableMb() >= floorMb;
}

function myTurn(name) {
    const tickets = inTurn(liveTickets());
    const mine = tickets.find((ticket) => ticket.name === name);
    if (mine?.quick) return tickets.filter((ticket) => ticket.quick && ticket.granted).length < QUICK_SLOTS && hostHasRoom(QUICK_SLOT_MB);
    if (!tickets.every((ticket) => ticket.v === TAKES_TURNS)) return tickets[0]?.name === name && hostHasRoom(Math.max(FIRST_SLOT_MB, mine?.minMb || 0));
    const running = tickets.filter((ticket) => ticket.granted && !ticket.quick);
    const next = tickets.find((ticket) => !ticket.granted && !ticket.quick);
    if (next?.name !== name || running.length >= SLOTS) return false;
    if (running.length === 0) return hostHasRoom(Math.max(next.learnedMb || FIRST_SLOT_MB, FIRST_SLOT_MB, next.minMb || 0));
    const second = next.learnedMb ? Math.max(LEARN_MIN_SECOND_MB, next.learnedMb) : SECOND_SLOT_MB;
    return !next.exclusive && !running.some((ticket) => ticket.exclusive) && hostHasRoom(Math.max(second, next.minMb || 0));
}

async function run(args) {
    const separator = args.indexOf('--');
    if (separator < 1 || separator === args.length - 1) throw new Error('usage: gate-ticket.cjs run <label> -- <command> [args]');
    const label = args[0];
    const command = args.slice(separator + 1);
    const tree = process.env.GATE_TREE || '';
    if (tree && !process.env.GATE_FORCE && historyRows().some((row) => row.label === label && row.tree === tree && row.code === 0)) {
        console.log('CACHED ' + label + ' on ' + tree + ' already passed; GATE_FORCE=1 reruns it');
        process.exit(0);
    }
    const { name, ticket } = takeTicket(process.pid, label);
    const file = path.join(TICKET_DIR, name);
    process.on('exit', () => fs.rmSync(file, { force: true }));
    process.on('SIGINT', () => process.exit(130));
    process.on('SIGTERM', () => process.exit(143));
    process.on('SIGHUP', () => {});
    while (!myTurn(name)) await sleep(POLL_MS);
    const granted = Date.now();
    fs.writeFileSync(file, JSON.stringify({ ...ticket, granted }) + '\n');
    fs.writeFileSync(LAST_TURNS, JSON.stringify({ ...readJson(LAST_TURNS, {}), [ownerOf(label)]: granted }) + '\n');
    console.log('GRANTED ' + label + ' ' + name.slice(0, 15));
    const child = spawn(command[0], command.slice(1), { stdio: 'inherit' });
    const peak = { kb: 0 };
    const sampler = setInterval(() => { peak.kb = Math.max(peak.kb, treeRssKb(child.pid)); }, PEAK_SAMPLE_MS);
    const code = await new Promise((resolve) => child.on('exit', (status) => resolve(status ?? 1)));
    clearInterval(sampler);
    const seconds = (from, to) => Math.round((to - from) / 1000);
    fs.mkdirSync(path.dirname(HISTORY), { recursive: true });
    fs.appendFileSync(HISTORY, JSON.stringify({ label, tree: ticket.tree, waited: seconds(ticket.arrival, granted), ran: seconds(granted, Date.now()), peakMb: Math.round(peak.kb / 1024), code, ended: new Date().toISOString() }) + '\n');
    process.exit(code);
}

function history() {
    if (!fs.existsSync(HISTORY)) return [];
    const perOwner = new Map();
    for (const line of fs.readFileSync(HISTORY, 'utf8').split('\n').filter(Boolean)) {
        const row = JSON.parse(line);
        const total = perOwner.get(ownerOf(row.label)) || { windows: 0, waited: 0, ran: 0, longest: 0, longestLabel: '' };
        total.windows += 1;
        total.waited += row.waited;
        total.ran += row.ran;
        if (row.ran > total.longest) Object.assign(total, { longest: row.ran, longestLabel: row.label });
        perOwner.set(ownerOf(row.label), total);
    }
    return [...perOwner].map(([owner, total]) => owner + ' windows=' + total.windows + ' waited=' + total.waited + 's ran=' + total.ran + 's longest=' + total.longest + 's ' + total.longestLabel);
}

async function main() {
    const [verb, ...rest] = process.argv.slice(2);
    if (verb === 'run') return run(rest);
    if (verb === 'list') return console.log(inTurn(liveTickets()).map((ticket) => ticket.name).join('\n'));
    if (verb === 'history') return console.log(history().join('\n'));
    throw new Error('usage: gate-ticket.cjs run <label> -- <command> [args] | list | history');
}

main().catch((error) => {
    console.error(error.message);
    process.exit(2);
});
