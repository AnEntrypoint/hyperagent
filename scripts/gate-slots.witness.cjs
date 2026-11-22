#!/usr/bin/env node
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');

const script = path.join(__dirname, 'gate-ticket.cjs');
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
const holdCode = "const fs = require('fs'); const [label, file] = process.argv.slice(1); fs.appendFileSync(file, 'start ' + label + '\\n'); setTimeout(() => fs.appendFileSync(file, 'end ' + label + '\\n'), 800);";

async function scenario(memoryMb, firstIsExclusive = false) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gate-slots-'));
    const log = path.join(dir, 'window.log');
    const env = { ...process.env, GATE_TICKET_DIR: path.join(dir, 'tickets'), GATE_READING_CMD: 'true', GATE_POLL_MS: '50', GATE_SLOTS: '2', GATE_MEMORY_MB: String(memoryMb) };
    const start = (label, exclusive) => new Promise((resolve) => spawn('node', [script, 'run', label, '--', 'node', '-e', holdCode, label, log], { env: { ...env, GATE_EXCLUSIVE: exclusive ? '1' : '0' }, stdio: 'ignore' }).on('exit', resolve));
    const exits = [start('alpha-a', firstIsExclusive)];
    await sleep(150);
    exits.push(start('beta-a'));
    const codes = await Promise.all(exits);
    const events = fs.readFileSync(log, 'utf8').trim().split('\n');
    fs.rmSync(dir, { recursive: true, force: true });
    return { events, codes };
}

async function quickScenario() {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gate-quick-'));
    const log = path.join(dir, 'window.log');
    const longHold = "const fs = require('fs'); const [label, file] = process.argv.slice(1); fs.appendFileSync(file, 'start ' + label + '\\n'); setTimeout(() => fs.appendFileSync(file, 'end ' + label + '\\n'), 1500);";
    const quickHold = "const fs = require('fs'); const [label, file] = process.argv.slice(1); fs.appendFileSync(file, 'start ' + label + '\\n'); setTimeout(() => fs.appendFileSync(file, 'end ' + label + '\\n'), 200);";
    const env = { ...process.env, GATE_TICKET_DIR: path.join(dir, 'tickets'), GATE_READING_CMD: 'true', GATE_POLL_MS: '50', GATE_SLOTS: '1', GATE_MEMORY_MB: '9999' };
    const start = (label, quick, hold) => new Promise((resolve) => spawn('node', [script, 'run', label, '--', 'node', '-e', hold, label, log], { env: { ...env, GATE_QUICK: quick ? '1' : '0' }, stdio: 'ignore' }).on('exit', resolve));
    const exits = [start('alpha-long', false, longHold)];
    await sleep(150);
    exits.push(start('beta-long', false, longHold));
    await sleep(150);
    exits.push(start('gamma-quick', true, quickHold));
    const codes = await Promise.all(exits);
    const events = fs.readFileSync(log, 'utf8').trim().split('\n');
    fs.rmSync(dir, { recursive: true, force: true });
    return { events, codes };
}

async function main() {
    setTimeout(() => {
        console.log('FAIL the witness did not finish in time');
        process.exit(1);
    }, 25000).unref();
    const quick = await quickScenario();
    const passed = quick.events.indexOf('end gamma-quick') < quick.events.indexOf('start beta-long');
    console.log((passed ? 'ok   ' : 'FAIL ') + 'a quick window runs while longer ones wait ' + JSON.stringify(quick.events));
    const roomy = await scenario(9999);
    const tight = await scenario(3000);
    const timed = await scenario(9999, true);
    const alone = JSON.stringify(timed.events) === JSON.stringify(['start alpha-a', 'end alpha-a', 'start beta-a', 'end beta-a']);
    const overlapped = roomy.events[1] === 'start beta-a';
    const serial = JSON.stringify(tight.events) === JSON.stringify(['start alpha-a', 'end alpha-a', 'start beta-a', 'end beta-a']);
    const clean = [...roomy.codes, ...tight.codes, ...timed.codes].every((code) => code === 0);
    console.log((alone ? 'ok   ' : 'FAIL ') + 'an exclusive window runs alone ' + JSON.stringify(timed.events));
    console.log((overlapped ? 'ok   ' : 'FAIL ') + 'a second window starts when memory allows ' + JSON.stringify(roomy.events));
    console.log((serial ? 'ok   ' : 'FAIL ') + 'windows run one at a time when memory is short ' + JSON.stringify(tight.events));
    console.log((clean ? 'ok   ' : 'FAIL ') + 'every window exited cleanly');
    process.exit(passed && overlapped && serial && alone && clean ? 0 : 1);
}

main();
