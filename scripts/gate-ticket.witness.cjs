#!/usr/bin/env node
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');

const script = path.join(__dirname, 'gate-ticket.cjs');
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gate-ticket-'));
const log = path.join(dir, 'window.log');
const env = { ...process.env, GATE_TICKET_DIR: path.join(dir, 'tickets'), GATE_READING_CMD: 'true', GATE_POLL_MS: '50', GATE_SLOTS: '1' };
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const holdCode = "const fs = require('fs'); const [label, file] = process.argv.slice(1); fs.appendFileSync(file, 'start ' + label + '\\n'); setTimeout(() => fs.appendFileSync(file, 'end ' + label + '\\n'), 1200);";

function startWindow(label) {
    const child = spawn('node', [script, 'run', label, '--', 'node', '-e', holdCode, label, log], { env, stdio: 'ignore' });
    return new Promise((resolve) => child.on('exit', (code) => resolve(code)));
}

async function main() {
    const ticketDir = path.join(dir, 'tickets');
    fs.mkdirSync(ticketDir);
    const stale = path.join(ticketDir, '000000000000001-2147483646.json');
    fs.writeFileSync(stale, '{}\n');

    const exits = [startWindow('first')];
    await sleep(300);
    exits.push(startWindow('second'));
    await sleep(200);
    exits.push(startWindow('third'));
    const codes = await Promise.all(exits);

    const events = fs.existsSync(log) ? fs.readFileSync(log, 'utf8').trim().split('\n') : [];
    const expected = ['start first', 'end first', 'start second', 'end second', 'start third', 'end third'];
    const checks = [
        ['windows run one at a time in arrival order', JSON.stringify(events) === JSON.stringify(expected)],
        ['every window exited cleanly', codes.every((code) => code === 0)],
        ['stale ticket of a dead pid removed', !fs.existsSync(stale)],
        ['tickets removed on exit', fs.readdirSync(ticketDir).length === 0],
    ];
    let fail = 0;
    for (const [name, ok] of checks) {
        console.log((ok ? 'ok   ' : 'FAIL ') + name);
        if (!ok) fail = 1;
    }
    console.log('events ' + JSON.stringify(events));
    fs.rmSync(dir, { recursive: true, force: true });
    process.exitCode = fail;
}

main().catch((error) => {
    console.log('FAIL ' + error.message);
    process.exitCode = 1;
});
