const http = require('node:http');

const port = Number(process.argv[2] || 0);
const server = http.createServer((request, response) => {
    response.writeHead(200, { 'Content-Type': 'application/json' });
    response.end('{}');
});
server.listen(port, '127.0.0.1', () => {
    process.stdout.write('READY ' + server.address().port + '\n');
});
