const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const home = process.env.HOME || process.env.USERPROFILE || os.homedir();
const configPath = path.join(home, '.claude.json');
const args = process.argv.slice(2);

const read = () => {
    try {
        const parsed = JSON.parse(fs.readFileSync(configPath, 'utf8'));
        return parsed && typeof parsed === 'object' ? parsed : {};
    } catch {
        return {};
    }
};

const write = (config) => {
    fs.mkdirSync(path.dirname(configPath), { recursive: true });
    fs.writeFileSync(configPath, JSON.stringify(config, null, 2) + '\n');
};

const serverOf = (config) => config.mcpServers && config.mcpServers.hyperagent;

if (args[0] === 'mcp' && args[1] === 'get') {
    process.exit(serverOf(read()) ? 0 : 1);
}

if (args[0] === 'mcp' && args[1] === 'remove') {
    const config = read();
    if (config.mcpServers) delete config.mcpServers.hyperagent;
    write(config);
    process.exit(0);
}

if (args[0] === 'mcp' && args[1] === 'add') {
    const separator = args.indexOf('--');
    const before = args.slice(2, separator);
    const after = args.slice(separator + 1);
    const env = {};
    let name = '';
    for (let index = 0; index < before.length; index += 1) {
        const argument = before[index];
        if (argument === '-e' || argument === '--env') {
            const pair = before[index + 1] || '';
            const equals = pair.indexOf('=');
            if (equals > 0) env[pair.slice(0, equals)] = pair.slice(equals + 1);
            index += 1;
            continue;
        }
        if (argument === '-s' || argument === '--scope') {
            index += 1;
            continue;
        }
        name = argument;
    }
    const config = read();
    config.mcpServers = config.mcpServers || {};
    config.mcpServers[name || 'hyperagent'] = {
        type: 'stdio',
        command: after[0] || '',
        args: after.slice(1),
        env,
    };
    write(config);
    process.exit(0);
}

process.exit(1);
