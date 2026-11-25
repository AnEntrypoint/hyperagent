const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '..');
const fragments = ['work' + 'space', 'config/work' + 'space', 'data/work' + 'space', 'tmp', 'config/.local'];
const forbidden = fragments.map((fragment) => '/' + fragment);
const skipped = new Set(['.git', '.gm', '.orch', '.agentplug', 'node_modules', '__pycache__', 'memories', 'specs', 'work', 'dist']);
const skippedPaths = new Set(['tests/fixtures']);
const binary = new Set(['.png', '.jpg', '.jpeg', '.gif', '.webp', '.ico', '.wasm', '.so', '.a', '.o', '.exe', '.dll', '.pdf', '.zip', '.gz', '.jar', '.sqlite', '.woff', '.woff2']);
let checked = 0;

function checkFile(file, label) {
    if (binary.has(path.extname(file).toLowerCase())) return true;
    const text = fs.readFileSync(file, 'utf8');
    const lines = text.split('\n');
    checked += 1;
    for (let index = 0; index < lines.length; index += 1) {
        for (const value of forbidden) {
            if (lines[index].includes(value)) {
                console.log('PATH_FAIL ' + label + ':' + (index + 1) + ' ' + value);
                return false;
            }
        }
    }
    return true;
}

function walk(relative) {
    if (skippedPaths.has(relative.replace(/\\/g, '/'))) return true;
    const absolute = path.join(root, relative);
    const entries = fs.readdirSync(absolute, { withFileTypes: true });
    for (const entry of entries) {
        const child = path.join(relative, entry.name);
        const normalized = child.replace(/\\/g, '/');
        if (entry.isDirectory()) {
            if (skipped.has(entry.name) || skippedPaths.has(normalized)) continue;
            if (normalized.startsWith('build-') || normalized === 'build') continue;
            if (!walk(child)) return false;
        } else if (entry.isFile()) {
            if (!checkFile(path.join(root, child), normalized)) return false;
        }
    }
    return true;
}

if (!walk('')) process.exit(1);
for (const extra of process.argv.slice(2)) {
    if (!checkFile(path.resolve(extra), path.resolve(extra))) process.exit(1);
}
console.log('PORTABLE_PATHS_OK files=' + checked);
