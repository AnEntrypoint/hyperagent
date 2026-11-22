#!/usr/bin/env node
const fs = require('node:fs');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');

const TICKET_DIR = process.env.GATE_TICKET_DIR || '/tmp/gate-tickets';
const HISTORY = process.env.GATE_HISTORY || TICKET_DIR + '-history.jsonl';
const LAST_TURNS = TICKET_DIR + '-turns.json';
const GATE_READING = process.env.GATE_READING_CMD || '';
const HEAVY_JOBS_CHECK = process.env.HEAVY_JOBS_CHECK || '/workspace/bevy/scripts/heavy-jobs.cjs';
const POLL_MS = Number(process.env.GATE_POLL_MS || 60000);
const TICKET = /^(\d{15})-(\d+)\.json$/;
const TAKES_TURNS = 2;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const ownerOf = (label) => String(label || '').split('-')[0];

function alive(pid) {
    try {
        process.kill(pid, 0);
        return true;
    } catch (error) {
        return error.code === 'EPERM';
    }
}

function readTicket(name) {
    try {
        return JSON.parse(fs.readFileSync(path.join(TICKET_DIR, name), 'utf8'));
    } catch {
        return {};
    }
}

function liveTickets() {
    fs.mkdirSync(TICKET_DIR, { recursive: true });
    const live = [];
    for (const name of fs.readdirSync(TICKET_DIR).sort()) {
        const match = TICKET.exec(name);
        if (!match) continue;
        if (!alive(Number(match[2]))) {
            fs.rmSync(path.join(TICKET_DIR, name), { force: true });
            continue;
        }
        live.push(name);
    }
    return live;
}

function lastTurns() {
    try {
        return JSON.parse(fs.readFileSync(LAST_TURNS, 'utf8'));
    } catch {
        return {};
    }
}

function turnOrder() {
    const tickets = liveTickets().map((name) => ({ name, ...readTicket(name) }));
    if (!tickets.every((ticket) => ticket.v === TAKES_TURNS)) return tickets.map((ticket) => ticket.name);
    const last = lastTurns();
    const queuedOwners = new Set();
    for (const ticket of tickets) {
        const owner = ownerOf(ticket.label);
        ticket.rank = ticket.granted ? 0 : queuedOwners.has(owner) ? 2 : 1;
        ticket.ownerLastTurn = ticket.rank === 1 ? last[owner] || 0 : 0;
        if (!ticket.granted) queuedOwners.add(owner);
    }
    return tickets.sort((a, b) => a.rank - b.rank || a.ownerLastTurn - b.ownerLastTurn || a.name.localeCompare(b.name)).map((ticket) => ticket.name);
}

function takeTicket(ownerPid, label) {
    const latest = liveTickets().reduce((max, name) => Math.max(max, Number(name.slice(0, 15))), 0);
    const arrival = Math.max(Date.now(), latest + 1);
    const name = String(arrival).padStart(15, '0') + '-' + ownerPid + '.json';
    const ticket = { pid: ownerPid, arrival, label, v: TAKES_TURNS };
    fs.writeFileSync(path.join(TICKET_DIR, name), JSON.stringify(ticket) + '\n');
    return { name, ticket };
}

function gateClear() {
    if (GATE_READING) return spawnSync('bash', ['-c', GATE_READING], { stdio: 'ignore' }).status === 0;
    const vm = spawnSync('vmstat', ['1', '2'], { encoding: 'utf8' }).stdout.trim().split('\n');
    const sample = vm[vm.length - 1].trim().split(/\s+/);
    const si = Number(sample[6]);
    const so = Number(sample[7]);
    const heavy = spawnSync('node', [HEAVY_JOBS_CHECK], { encoding: 'utf8', timeout: 60000 });
    return si < 1000 && so < 1000 && heavy.status === 0;
}

async function waitForTurn(name) {
    for (;;) {
        if (turnOrder()[0] === name && gateClear()) return;
        await sleep(POLL_MS);
    }
}

async function run(args) {
    const separator = args.indexOf('--');
    if (separator < 1 || separator === args.length - 1) throw new Error('usage: gate-ticket.cjs run <label> -- <command> [args]');
    const label = args[0];
    const command = args.slice(separator + 1);
    const { name, ticket } = takeTicket(process.pid, label);
    const file = path.join(TICKET_DIR, name);
    process.on('exit', () => fs.rmSync(file, { force: true }));
    process.on('SIGINT', () => process.exit(130));
    process.on('SIGTERM', () => process.exit(143));
    process.on('SIGHUP', () => {});
    await waitForTurn(name);
    const granted = Date.now();
    fs.writeFileSync(file, JSON.stringify({ ...ticket, granted }) + '\n');
    fs.writeFileSync(LAST_TURNS, JSON.stringify({ ...lastTurns(), [ownerOf(label)]: granted }) + '\n');
    console.log('GRANTED ' + label + ' ' + name.slice(0, 15));
    const child = spawn(command[0], command.slice(1), { stdio: 'inherit' });
    const code = await new Promise((resolve) => child.on('exit', (status) => resolve(status ?? 1)));
    const seconds = (from, to) => Math.round((to - from) / 1000);
    fs.appendFileSync(HISTORY, JSON.stringify({ label, waited: seconds(ticket.arrival, granted), ran: seconds(granted, Date.now()), code, ended: new Date().toISOString() }) + '\n');
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
    if (verb === 'list') return console.log(turnOrder().join('\n'));
    if (verb === 'history') return console.log(history().join('\n'));
    throw new Error('usage: gate-ticket.cjs run <label> -- <command> [args] | list | history');
}

main().catch((error) => {
    console.error(error.message);
    process.exit(2);
});
