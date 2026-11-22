#!/usr/bin/env node
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');

const script = path.join(__dirname, 'gate-ticket.cjs');
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const holdCode = "const fs = require('fs'); const [label, file] = process.argv.slice(1); fs.appendFileSync(file, 'start ' + label + '\\n'); setTimeout(() => fs.appendFileSync(file, 'end ' + label + '\\n'), 800);";

async function scenario(memoryMb) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gate-slots-'));
    const log = path.join(dir, 'window.log');
    const env = { ...process.env, GATE_TICKET_DIR: path.join(dir, 'tickets'), GATE_READING_CMD: 'true', GATE_POLL_MS: '50', GATE_SLOTS: '2', GATE_MEMORY_MB: String(memoryMb) };
    const start = (label) => new Promise((resolve) => spawn('node', [script, 'run', label, '--', 'node', '-e', holdCode, label, log], { env, stdio: 'ignore' }).on('exit', resolve));
    const exits = [start('alpha-a')];
    await sleep(150);
    exits.push(start('beta-a'));
    const codes = await Promise.all(exits);
    const events = fs.readFileSync(log, 'utf8').trim().split('\n');
    fs.rmSync(dir, { recursive: true, force: true });
    return { events, codes };
}

async function main() {
    const roomy = await scenario(9999);
    const tight = await scenario(100);
    const overlapped = roomy.events[1] === 'start beta-a';
    const serial = JSON.stringify(tight.events) === JSON.stringify(['start alpha-a', 'end alpha-a', 'start beta-a', 'end beta-a']);
    const clean = [...roomy.codes, ...tight.codes].every((code) => code === 0);
    console.log((overlapped ? 'ok   ' : 'FAIL ') + 'a second window starts when memory allows ' + JSON.stringify(roomy.events));
    console.log((serial ? 'ok   ' : 'FAIL ') + 'windows run one at a time when memory is short ' + JSON.stringify(tight.events));
    console.log((clean ? 'ok   ' : 'FAIL ') + 'every window exited cleanly');
    process.exit(overlapped && serial && clean ? 0 : 1);
}

main();
