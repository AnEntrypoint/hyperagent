const fs = require('node:fs');

const files = ['.mcp.json', 'CMakePresets.json', 'scripts/peers.example.json'];
for (const file of files) {
    JSON.parse(fs.readFileSync(file, 'utf8'));
    console.log('JSON_OK ' + file);
}
console.log('JSON_VALIDATION_OK');
