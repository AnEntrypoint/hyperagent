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
    if (separator < 2) {
        console.error('Invalid environment variable format: missing -- separator');
        process.exit(1);
    }
    const before = args.slice(2, separator);
    const after = args.slice(separator + 1);
    const env = {};
    for (let index = 0; index < before.length; index += 1) {
        const argument = before[index];
        if (argument === '-e' || argument === '--env') {
            const pair = before[index + 1] || '';
            const equals = pair.indexOf('=');
            if (equals <= 0) {
                console.error('Invalid environment variable format: ' + pair);
                process.exit(1);
            }
            env[pair.slice(0, equals)] = pair.slice(equals + 1);
            index += 1;
            continue;
        }
        if (argument === '-s' || argument === '--scope') {
            index += 1;
            continue;
        }
        console.error('Invalid environment variable format: ' + argument);
        process.exit(1);
    }
    if (after.length < 3) {
        console.error('Invalid MCP add command');
        process.exit(1);
    }
    const config = read();
    config.mcpServers = config.mcpServers || {};
    config.mcpServers[after[0]] = {
        type: 'stdio',
        command: after[1],
        args: after.slice(2),
        env,
    };
    write(config);
    process.exit(0);
}

process.exit(1);
