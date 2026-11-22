#!/usr/bin/env node
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');

const script = path.join(__dirname, 'gate-ticket.cjs');
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gate-turns-'));
const log = path.join(dir, 'window.log');
const env = { ...process.env, GATE_TICKET_DIR: path.join(dir, 'tickets'), GATE_READING_CMD: 'true', GATE_POLL_MS: '50' };
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const holdCode = "const fs = require('fs'); const [label, file] = process.argv.slice(1); fs.appendFileSync(file, label + '\\n'); setTimeout(() => {}, 600);";
const startWindow = (label) => new Promise((resolve) => spawn('node', [script, 'run', label, '--', 'node', '-e', holdCode, label, log], { env, stdio: 'ignore' }).on('exit', resolve));

async function main() {
    const exits = [];
    for (const label of ['bevy-a', 'bevy-b', 'bevy-c', 'signal-probe', 'gm-build']) {
        exits.push(startWindow(label));
        await sleep(120);
    }
    const codes = await Promise.all(exits);
    const order = fs.readFileSync(log, 'utf8').trim().split('\n');
    const expected = ['bevy-a', 'signal-probe', 'gm-build', 'bevy-b', 'bevy-c'];
    const history = fs.readFileSync(path.join(dir, 'tickets-history.jsonl'), 'utf8').trim().split('\n').length;
    const ok = JSON.stringify(order) === JSON.stringify(expected) && codes.every((code) => code === 0) && history === 5;
    console.log((ok ? 'PASS ' : 'FAIL ') + JSON.stringify({ order, expected, codes, history }));
    fs.rmSync(dir, { recursive: true, force: true });
    process.exit(ok ? 0 : 1);
}

main();
