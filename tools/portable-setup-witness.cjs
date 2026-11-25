const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawn, spawnSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const build = path.resolve(process.argv[2] || path.join(root, 'build'));
const base = process.env.HYPERAGENT_WITNESS_DIR || os.tmpdir();
fs.mkdirSync(base, { recursive: true });
const scratch = process.env.HYPERAGENT_WITNESS_DIR ? base : fs.mkdtempSync(path.join(base, 'hyperagent-portable-scratch-'));
const work = fs.mkdtempSync(path.join(scratch, 'portable-setup-'));
const children = [];

const cleanup = () => {
    for (const child of children) {
        if (child.exitCode === null && !child.killed) child.kill();
    }
    fs.rmSync(work, { recursive: true, force: true });
    if (!process.env.HYPERAGENT_WITNESS_DIR) fs.rmSync(scratch, { recursive: true, force: true });
};

process.on('exit', cleanup);

const fail = (reason) => {
    console.error('PORTABLE_SETUP_FAIL ' + reason);
    process.exit(1);
};

const run = (program, args, env) => {
    const result = spawnSync(program, args, { encoding: 'utf8', env: env || process.env, timeout: 600000 });
    const detail = result.error ? result.error.message : (result.stdout || '') + (result.stderr || '');
    if (result.error || result.status !== 0) {
        fail(program + ' ' + args.join(' ') + ' failed: ' + detail);
    }
    return (result.stdout || '').trim();
};

const startServer = (port) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'fake-http-server.cjs'), String(port)], { stdio: ['ignore', 'pipe', 'pipe'] });
    children.push(child);
    return new Promise((resolve, reject) => {
        let output = '';
        child.stdout.on('data', (chunk) => {
            output += chunk.toString();
            const match = output.match(/READY (\d+)/);
            if (match) resolve(Number(match[1]));
        });
        child.stderr.on('data', (chunk) => {
            output += chunk.toString();
        });
        child.on('exit', (code) => {
            if (!output.includes('READY')) reject(new Error('fake HTTP server exited ' + code + ' ' + output));
        });
    });
};

const stopChild = async (child) => {
    if (child.exitCode === null && !child.killed) {
        child.kill();
        await new Promise((resolve) => child.once('exit', resolve));
    }
};

const close = (condition, reason) => {
    if (!condition) fail(reason);
};

async function main() {
    const occupiedPort = await startServer(0);
    const binaryName = process.platform === 'win32' ? 'hyperagent.exe' : 'hyperagent';
    const ccbridgeName = process.platform === 'win32' ? 'ccbridge.exe' : 'ccbridge';
    const ship = path.join(work, 'ship');
    run('cmake', ['--install', build, '--config', 'Release', '--prefix', ship], process.env);
    for (const relative of [
        'bin/' + binaryName,
        'bin/' + ccbridgeName,
        'share/ccbridge/templates/claude.json',
        'share/ccbridge/web/index.html',
        'share/hyperagent/gui/public/index.html',
        '.mcp.json',
        'docs/peer-rules.md',
        'scripts/cycle.cjs',
        'tools/portable-setup-witness.cjs',
        'tools/timeout-command.cjs',
        'tools/hold-lock.py',
        'tools/lock-held.py',
        'tools/check-python.py',
    ]) {
        close(fs.existsSync(path.join(ship, relative)), 'missing shipped resource ' + relative);
    }

    const binary = path.join(ship, 'bin', binaryName);
    const fakeBin = path.join(work, 'bin');
    const fakeClaude = path.join(__dirname, 'fake-claude.cjs');
    const wrapperName = process.platform === 'win32' ? 'claude.cmd' : 'claude';
    const wrapper = process.platform === 'win32'
        ? '@echo off\r\n"' + process.execPath + '" "' + fakeClaude + '" %*\r\n'
        : '#!/bin/sh\nexec "' + process.execPath + '" "' + fakeClaude + '" "$@"\n';
    fs.mkdirSync(fakeBin, { recursive: true });
    fs.writeFileSync(path.join(fakeBin, wrapperName), wrapper);
    if (process.platform !== 'win32') fs.chmodSync(path.join(fakeBin, wrapperName), 0o755);

    const home = path.join(work, 'home');
    const parent = path.join(work, 'projects');
    const state = path.join(work, 'state');
    const journal = path.join(work, 'journal');
    const templates = path.join(work, 'templates');
    const manifestPath = path.join(work, 'manifest', 'install.json');
    fs.mkdirSync(home, { recursive: true });
    fs.mkdirSync(templates, { recursive: true });
    const template = {
        default: true,
        argv: ['chlaude'],
        ready: ['❯', 'Claude Code'],
        busy: ['esc to interrupt'],
        transcript: { kind: 'claude-jsonl', root: '{home}/.claude/projects' },
        resume: ['--resume', '{session}'],
    };
    const templateFile = path.join(templates, 'claude.json');
    const templateText = JSON.stringify(template, null, 2) + '\n';
    fs.writeFileSync(templateFile, templateText);

    const env = {
        PATH: [fakeBin, path.dirname(process.execPath), process.env.PATH || process.env.Path || ''].filter(Boolean).join(path.delimiter),
        HOME: home,
        USERPROFILE: home,
        TMPDIR: work,
        TEMP: work,
        TMP: work,
        SystemRoot: process.env.SystemRoot || '',
        ComSpec: process.env.ComSpec || '',
        PATHEXT: process.env.PATHEXT || '',
        HYPERAGENT_INSTALL_MANIFEST: manifestPath,
        ORCH_PORT: String(occupiedPort),
        ORCH_HOST: '127.0.0.1',
        ORCH_CLAUDE_MODEL: 'default',
    };

    const setupOutput = run(binary, ['setup', '--parent', parent, '--home', state, '--journal', journal, '--templates', templates], env);
    console.log(setupOutput);
    const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
    close(String(manifest.port) !== String(occupiedPort), 'occupied preferred port did not fall back');
    close(fs.existsSync(parent), 'setup did not create the missing project parent');
    close(fs.existsSync(state), 'setup did not create the state directory');
    close(fs.existsSync(journal), 'setup did not create the journal root');
    close(fs.existsSync(templates), 'setup did not create the custom template directory');

    const expectedEnvironment = {
        CCBRIDGE_BIN: path.join(ship, 'bin', ccbridgeName),
        CCBRIDGE_TEMPLATES: templates,
        HYPERAGENT_ROOT: ship,
        MCP_TIMEOUT: '60000',
        MCP_TOOL_TIMEOUT: '14400000',
        ORCH_HOME: state,
        ORCH_HOST: '127.0.0.1',
        ORCH_JOURNAL: journal,
        ORCH_PARENT: parent,
        ORCH_PORT: String(manifest.port),
    };
    const expectedKeys = Object.keys(expectedEnvironment).sort();
    const userConfigPath = path.join(home, '.claude.json');
    const userConfigText = fs.readFileSync(userConfigPath, 'utf8');
    const userConfig = JSON.parse(userConfigText);
    const userServer = userConfig.mcpServers && userConfig.mcpServers.hyperagent;
    close(userServer && userServer.command === manifest.binary, 'user MCP command is not the absolute installed binary');
    close(userServer && JSON.stringify(Object.keys(userServer.env || {}).sort()) === JSON.stringify(expectedKeys), 'user MCP environment keys are incomplete');
    for (const key of expectedKeys) {
        close(userServer.env[key] === expectedEnvironment[key], 'user MCP environment value mismatch for ' + key);
    }
    const projectConfig = JSON.parse(fs.readFileSync(path.join(ship, '.mcp.json'), 'utf8'));
    const projectServer = projectConfig.mcpServers && projectConfig.mcpServers.hyperagent;
    close(projectServer && String(projectServer.timeout) === manifest.mcp_tool_timeout, 'project MCP tool timeout is stale');
    close(projectServer && JSON.stringify(Object.keys(projectServer.env || {}).sort()) === JSON.stringify(expectedKeys), 'project MCP environment keys are incomplete');
    for (const key of expectedKeys) {
        close(projectServer.env[key] === expectedEnvironment[key], 'project MCP environment value mismatch for ' + key);
    }

    delete env.ORCH_PORT;
    const doctorOutput = run(binary, ['doctor'], env);
    console.log(doctorOutput);
    close(doctorOutput.includes('DOCTOR_OK'), 'doctor did not pass a clean setup');
    close(doctorOutput.includes('model override none'), 'doctor did not report the absent model override');
    const checkOutput = run(binary, ['setup', '--check'], env);
    close(checkOutput.includes('DOCTOR_OK'), 'setup --check did not pass');
    const pathsOutput = run(process.execPath, [path.join(__dirname, 'portable-paths.witness.cjs')], process.env);
    console.log(pathsOutput);
    console.log('SETUP_OK custom-template model-default port=' + manifest.port);

    await stopChild(children[0]);

    const doctorFailure = (label, fragment, mutate, restore) => {
        mutate();
        const result = spawnSync(binary, ['doctor'], { encoding: 'utf8', env, timeout: 600000 });
        const output = (result.stdout || '') + (result.stderr || '');
        restore();
        close(result.status !== 0, label + ' red check unexpectedly passed');
        close(output.includes(fragment), label + ' red check failed for the wrong reason: ' + output.trim());
        console.log('RED_OK ' + label);
    };

    doctorFailure('missing-parent', 'missing directory',
        () => fs.rmSync(parent, { recursive: true, force: true }),
        () => fs.mkdirSync(parent, { recursive: true }));

    doctorFailure('stale-mcp-env', 'stale user MCP environment',
        () => {
            const stale = JSON.parse(userConfigText);
            stale.mcpServers.hyperagent.env.ORCH_PORT = '1';
            fs.writeFileSync(userConfigPath, JSON.stringify(stale, null, 2) + '\n');
        },
        () => fs.writeFileSync(userConfigPath, userConfigText));

    doctorFailure('model-override', 'model-overriding harness templates',
        () => {
            const overridden = JSON.parse(templateText);
            overridden.argv.push('--model');
            fs.writeFileSync(templateFile, JSON.stringify(overridden, null, 2) + '\n');
        },
        () => fs.writeFileSync(templateFile, templateText));

    const staleOrder = spawnSync(process.execPath, [fakeClaude, 'mcp', 'add', '--scope', 'user', '-e', 'STALE=1', 'hyperagent', '--', manifest.binary, 'mcp'], { encoding: 'utf8', env, timeout: 600000 });
    const staleOutput = (staleOrder.stdout || '') + (staleOrder.stderr || '');
    close(staleOrder.status !== 0, 'mcp-add-order red check unexpectedly passed');
    close(staleOutput.includes('Invalid environment variable format: hyperagent'), 'mcp-add-order red check failed for the wrong reason: ' + staleOutput.trim());
    console.log('RED_OK mcp-add-order');

    const originalHolder = children[0];
    await startServer(Number(manifest.port));
    const blocker = children[children.length - 1];
    const portResult = spawnSync(binary, ['doctor'], { encoding: 'utf8', env, timeout: 600000 });
    const portOutput = (portResult.stdout || '') + (portResult.stderr || '');
    close(portResult.status !== 0, 'occupied-port red check unexpectedly passed');
    close(portOutput.includes('occupied by another server'), 'occupied-port red check failed for the wrong reason: ' + portOutput.trim());
    console.log('RED_OK occupied-port');
    await stopChild(blocker);
    close(originalHolder.exitCode !== null || originalHolder.killed, 'original port holder was not stopped');

    const forbiddenFile = path.join(work, 'forbidden.txt');
    fs.writeFileSync(forbiddenFile, Buffer.from('L3dvcmtzcGFjZQ==', 'base64').toString('utf8') + '\n');
    const pathResult = spawnSync(process.execPath, [path.join(__dirname, 'portable-paths.witness.cjs'), forbiddenFile], { encoding: 'utf8', env: process.env, timeout: 600000 });
    const pathOutput = (pathResult.stdout || '') + (pathResult.stderr || '');
    close(pathResult.status !== 0, 'hardcoded-path red check unexpectedly passed');
    close(pathOutput.includes('PATH_FAIL'), 'hardcoded-path red check failed for the wrong reason: ' + pathOutput.trim());
    console.log('RED_OK hardcoded-path');

    const finalDoctor = run(binary, ['doctor'], env);
    close(finalDoctor.includes('DOCTOR_OK'), 'doctor did not recover after the red checks');
    console.log(finalDoctor);
    console.log('PORTABLE_SETUP_WITNESS_OK');
}

main().catch((error) => fail(error.message));
