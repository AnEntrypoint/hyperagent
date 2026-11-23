#!/usr/bin/env node
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn } = require('node:child_process');

const script = path.join(__dirname, 'gate-ticket.cjs');
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function startedWithin(memoryMb, minMb, waitMs) {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'gate-minmb-'));
    const mark = path.join(dir, 'started');
    const env = { ...process.env, GATE_TICKET_DIR: path.join(dir, 'tickets'), GATE_READING_CMD: 'true', GATE_POLL_MS: '50', GATE_MEMORY_MB: String(memoryMb) };
    if (minMb) env.GATE_MIN_MB = String(minMb);
    else delete env.GATE_MIN_MB;
    const child = spawn('node', [script, 'run', 'minmb-window', '--', 'node', '-e', "require('fs').writeFileSync(process.argv[1], 'x')", mark], { env, stdio: 'ignore' });
    await sleep(waitMs);
    const started = fs.existsSync(mark);
    child.kill();
    fs.rmSync(dir, { recursive: true, force: true });
    return started;
}

(async () => {
    const waits = await startedWithin(3000, 8000, 1200);
    const runsWhenRoomy = await startedWithin(9000, 8000, 1200);
    const control = await startedWithin(3000, 0, 1200);
    const checks = [
        ['a ticket with GATE_MIN_MB=8000 waits at 3000 MB available', waits === false],
        ['the same ticket starts at 9000 MB available', runsWhenRoomy === true],
        ['red check: without GATE_MIN_MB the ticket starts at 3000 MB, so the wait above is the new floor', control === true],
    ];
    for (const [name, ok] of checks) console.log((ok ? 'PASS ' : 'FAIL ') + name);
    process.exit(checks.every(([, ok]) => ok) ? 0 : 1);
})();
