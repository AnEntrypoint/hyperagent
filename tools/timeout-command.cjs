const { spawn } = require('node:child_process');

const seconds = Number(process.argv[2]);
const program = process.argv[3];
const args = process.argv.slice(4);
if (!Number.isFinite(seconds) || seconds <= 0 || !program) {
    console.error('TIMEOUT_USAGE seconds program [args...]');
    process.exit(2);
}
const child = spawn(program, args, { stdio: 'inherit' });
let timedOut = false;
const timer = setTimeout(() => {
    timedOut = true;
    child.kill();
}, seconds * 1000);
child.on('error', (error) => {
    clearTimeout(timer);
    console.error(error.message);
    process.exit(127);
});
child.on('exit', (code) => {
    clearTimeout(timer);
    if (timedOut) process.exit(124);
    process.exit(code === null ? 1 : code);
});
