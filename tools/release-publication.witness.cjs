const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { spawnSync } = require('node:child_process');

const script = path.join(__dirname, 'release-publication.cjs');
const base = process.env.HYPERAGENT_WITNESS_DIR || os.tmpdir();
fs.mkdirSync(base, { recursive: true });
const scratch = fs.mkdtempSync(path.join(base, 'release-publication-'));
const targets = [
    { name: 'x64-linux', ext: 'tar.gz' },
    { name: 'arm64-linux', ext: 'tar.gz' },
    { name: 'arm64-macos', ext: 'tar.gz' },
    { name: 'x64-macos', ext: 'tar.gz' },
    { name: 'x64-windows', ext: 'zip' },
];
const stableName = (target) => 'hyperagent-' + target.name + '.' + target.ext;
const fail = (message) => {
    console.error('RELEASE_PUBLICATION_WITNESS_FAIL ' + message);
    process.exit(1);
};

process.on('exit', () => fs.rmSync(scratch, { recursive: true, force: true }));

const run = (args) => spawnSync(process.execPath, [script, ...args], { encoding: 'utf8' });

function makeDist(dir, omitted) {
    fs.mkdirSync(dir, { recursive: true });
    for (const target of targets) {
        if (target.name === omitted) continue;
        const stable = stableName(target);
        const file = path.join(dir, stable);
        const bytes = Buffer.from('witness artifact ' + target.name + '\n');
        fs.writeFileSync(file, bytes);
        const sum = crypto.createHash('sha256').update(bytes).digest('hex');
        fs.writeFileSync(file + '.sha256', sum + '  ' + stable + '\n');
    }
}

const build = 'witness-1';
const sha = '0123456789abcdef0123456789abcdef01234567';
const completeDist = path.join(scratch, 'complete-dist');
const completeRelease = path.join(scratch, 'complete-release');
makeDist(completeDist);
const complete = run(['write-manifest', completeDist, completeRelease, build, sha]);
if (complete.status !== 0) fail('complete write manifest failed: ' + complete.stderr);
const manifest = JSON.parse(fs.readFileSync(path.join(completeRelease, 'manifest.json'), 'utf8'));
if (manifest.sha !== sha) fail('manifest sha mismatch');
const manifestTargets = Object.keys(manifest.targets || {}).sort();
if (JSON.stringify(manifestTargets) !== JSON.stringify(targets.map((target) => target.name).sort())) fail('manifest target count');
const sums = fs.readFileSync(path.join(completeRelease, 'SHA256SUMS'), 'utf8').trim().split('\n').filter(Boolean);
if (sums.length !== 10) fail('checksum line count ' + sums.length);
const covered = new Set(sums.map((line) => line.trim().split(/\s+/).slice(1).join(' ')));
for (const target of targets) {
    if (!covered.has(stableName(target))) fail('missing stable checksum for ' + target.name);
    const asset = manifest.targets[target.name] && manifest.targets[target.name].asset;
    if (typeof asset !== 'string' || !covered.has(asset)) fail('missing versioned checksum for ' + target.name);
    if (!fs.existsSync(path.join(completeRelease, stableName(target)))) fail('missing stable artifact ' + target.name);
    if (!fs.existsSync(path.join(completeRelease, asset))) fail('missing versioned artifact ' + target.name);
}
const completePublished = run(['published-complete', path.join(completeRelease, 'manifest.json'), path.join(completeRelease, 'SHA256SUMS'), sha]);
if (completePublished.status !== 0) fail('complete published manifest rejected');
console.log('RELEASE_PUBLICATION_COMPLETE_OK targets=5 checksums=10');
console.log('RELEASE_DUPLICATE_SKIP_OK');

const partialManifestPath = path.join(scratch, 'partial-manifest.json');
const partialSumsPath = path.join(scratch, 'partial-SHA256SUMS');
const partialManifest = JSON.parse(JSON.stringify(manifest));
delete partialManifest.targets['x64-macos'];
fs.writeFileSync(partialManifestPath, JSON.stringify(partialManifest, null, 2) + '\n');
fs.writeFileSync(partialSumsPath, sums.filter((line) => !line.includes('x64-macos')).join('\n') + '\n');
const partialPublished = run(['published-complete', partialManifestPath, partialSumsPath, sha]);
if (partialPublished.status === 0) fail('partial published manifest accepted');
console.log('RED_OK partial-published-manifest');

const missingDist = path.join(scratch, 'missing-dist');
const missingRelease = path.join(scratch, 'missing-release');
makeDist(missingDist, 'x64-macos');
fs.mkdirSync(missingRelease, { recursive: true });
const previousManifest = 'PREVIOUS_MANIFEST\n';
const previousSums = 'PREVIOUS_SUMS\n';
fs.writeFileSync(path.join(missingRelease, 'manifest.json'), previousManifest);
fs.writeFileSync(path.join(missingRelease, 'SHA256SUMS'), previousSums);
const missing = run(['write-manifest', missingDist, missingRelease, build, sha]);
if (missing.status === 0) fail('missing x64-macos artifact accepted');
if (!String(missing.stderr || '').includes('x64-macos')) fail('missing artifact failed for the wrong reason');
if (fs.readFileSync(path.join(missingRelease, 'manifest.json'), 'utf8') !== previousManifest) fail('previous manifest changed');
if (fs.readFileSync(path.join(missingRelease, 'SHA256SUMS'), 'utf8') !== previousSums) fail('previous checksums changed');
console.log('RED_OK missing-x64-macos');
console.log('RELEASE_PUBLICATION_WITNESS_OK');
