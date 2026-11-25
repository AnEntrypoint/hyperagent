const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const targets = [
    { name: 'x64-linux', ext: 'tar.gz' },
    { name: 'arm64-linux', ext: 'tar.gz' },
    { name: 'arm64-macos', ext: 'tar.gz' },
    { name: 'x64-macos', ext: 'tar.gz' },
    { name: 'x64-windows', ext: 'zip' },
];

const stableName = (target) => 'hyperagent-' + target.name + '.' + target.ext;
const versionedName = (target, build) => 'hyperagent-' + build + '-' + target.name + '.' + target.ext;

const fail = (message) => {
    console.error('RELEASE_PUBLICATION_FAIL ' + message);
    process.exit(1);
};

const hashFile = (file) => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

function verifyArtifacts(distDir) {
    const rows = [];
    for (const target of targets) {
        const stable = stableName(target);
        const file = path.join(distDir, stable);
        const sidecar = file + '.sha256';
        if (!fs.existsSync(file)) fail('missing artifact ' + stable);
        if (!fs.existsSync(sidecar)) fail('missing checksum ' + stable + '.sha256');
        const actual = hashFile(file);
        const sidecarText = fs.readFileSync(sidecar, 'utf8').trim();
        const declared = sidecarText.split('\n')
            .map((line) => line.trim().split(/\s+/)[0])
            .find((value) => /^[a-fA-F0-9]{64}$/.test(value || ''));
        if (!declared || declared.toLowerCase() !== actual.toLowerCase()) fail('checksum mismatch for ' + stable);
        rows.push({ target: target.name, ext: target.ext, stable, file, sha256: actual, size: fs.statSync(file).size });
    }
    return rows;
}

function writeManifest(distDir, releaseDir, build, sha) {
    const rows = verifyArtifacts(distDir);
    fs.mkdirSync(releaseDir, { recursive: true });
    const sums = [];
    const manifestTargets = {};
    for (const row of rows) {
        const versioned = versionedName({ name: row.target, ext: row.ext }, build);
        fs.copyFileSync(row.file, path.join(releaseDir, row.stable));
        fs.copyFileSync(row.file, path.join(releaseDir, versioned));
        sums.push(row.sha256 + '  ' + row.stable);
        sums.push(row.sha256 + '  ' + versioned);
        manifestTargets[row.target] = { asset: versioned, sha256: row.sha256, size: row.size };
    }
    const manifest = { build, sha, targets: manifestTargets };
    fs.writeFileSync(path.join(releaseDir, 'SHA256SUMS'), sums.join('\n') + '\n');
    fs.writeFileSync(path.join(releaseDir, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
    console.log('RELEASE_PUBLICATION_OK targets=' + Object.keys(manifestTargets).length + ' checksums=' + sums.length);
}

function publishedComplete(manifestPath, sumsPath, sha) {
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    if (manifest.sha !== sha) fail('published manifest sha mismatch');
    const expected = targets.map((target) => target.name).sort();
    const found = Object.keys(manifest.targets || {}).sort();
    if (JSON.stringify(found) !== JSON.stringify(expected)) fail('published manifest targets incomplete');
    const lines = fs.readFileSync(sumsPath, 'utf8').trim().split('\n').filter(Boolean);
    if (lines.length !== 10) fail('published checksum lines ' + lines.length);
    const covered = new Set(lines.map((line) => line.trim().split(/\s+/).slice(1).join(' ')));
    for (const target of targets) {
        if (!covered.has(stableName(target))) fail('published stable checksum missing for ' + target.name);
        const asset = manifest.targets[target.name] && manifest.targets[target.name].asset;
        if (typeof asset !== 'string' || !covered.has(asset)) fail('published versioned checksum missing for ' + target.name);
    }
    console.log('RELEASE_PUBLISHED_COMPLETE targets=' + found.length + ' checksums=' + lines.length);
}

const [command, ...args] = process.argv.slice(2);
if (command === 'verify-artifacts') {
    verifyArtifacts(args[0]);
    console.log('RELEASE_ARTIFACTS_OK targets=5');
} else if (command === 'write-manifest') {
    writeManifest(args[0], args[1], args[2], args[3]);
} else if (command === 'published-complete') {
    publishedComplete(args[0], args[1], args[2]);
} else {
    fail('usage: verify-artifacts DIST | write-manifest DIST RELEASE BUILD SHA | published-complete MANIFEST SUMS SHA');
}
