#!/usr/bin/env node
const fs = require('node:fs');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');

const TICKET_DIR = process.env.GATE_TICKET_DIR || '/tmp/gate-tickets';
const GATE_READING = process.env.GATE_READING_CMD || '';
const HEAVY_JOBS_CHECK = process.env.HEAVY_JOBS_CHECK || '/workspace/bevy/scripts/heavy-jobs.cjs';
const POLL_MS = Number(process.env.GATE_POLL_MS || 60000);
const TICKET = /^(\d{15})-(\d+)\.json$/;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

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

function takeTicket(ownerPid) {
    const latest = liveTickets().reduce((max, name) => Math.max(max, Number(name.slice(0, 15))), 0);
    const arrival = Math.max(Date.now(), latest + 1);
    const name = String(arrival).padStart(15, '0') + '-' + ownerPid + '.json';
    fs.writeFileSync(path.join(TICKET_DIR, name), JSON.stringify({ pid: ownerPid, arrival, label: process.env.GATE_LABEL || '' }) + '\n');
    return name;
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
        const live = liveTickets();
        if (live[0] === name && gateClear()) return;
        await sleep(POLL_MS);
    }
}

async function run(args) {
    const separator = args.indexOf('--');
    if (separator < 1 || separator === args.length - 1) throw new Error('usage: gate-ticket.cjs run <label> -- <command> [args]');
    process.env.GATE_LABEL = args[0];
    const command = args.slice(separator + 1);
    const name = takeTicket(process.pid);
    const ticket = path.join(TICKET_DIR, name);
    process.on('exit', () => fs.rmSync(ticket, { force: true }));
    process.on('SIGINT', () => process.exit(130));
    process.on('SIGTERM', () => process.exit(143));
    await waitForTurn(name);
    console.log('GRANTED ' + args[0] + ' ' + name.slice(0, 15));
    const child = spawn(command[0], command.slice(1), { stdio: 'inherit' });
    const code = await new Promise((resolve) => child.on('exit', (status) => resolve(status ?? 1)));
    process.exit(code);
}

async function main() {
    const [verb, ...rest] = process.argv.slice(2);
    if (verb === 'run') return run(rest);
    if (verb === 'list') {
        for (const name of liveTickets()) console.log(name);
        return;
    }
    throw new Error('usage: gate-ticket.cjs run <label> -- <command> [args] | list');
}

main().catch((error) => {
    console.error(error.message);
    process.exit(2);
});
