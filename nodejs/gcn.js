// Server side for GCN, simplest tool
// MIT licence
// nodejs 8.11.1

// common
const http = require('http');
const https = require('https');
const querystring = require('querystring');
const fs = require('fs');

// copy credentials.js.example to credentials.js and update it
const credentials_file = __dirname + '/credentials.js';
fs.chmodSync(credentials_file, 0o600);

// your data: IFTTT_ACCOUNT_KEY, IFTTT_WEBHOOK_TRIGGER
const credentials = require(credentials_file);

// customizable
const INACTIVE_THRESHOLD_SEC = 3 * 60;
const WEBSERVER_HTTP_PORT = 8090
const WEBSERVER_LISTEN_ADDR = '::'

// globals
var cache = {}

// lib

function reply(response, code, msg) {
    response.writeHead(code, { 'Content-Type': 'text/plain' })
    response.end(msg);
}

function ifttt_webhook(value1 = null, value2 = null, value3 = null) {
    var json = {};
    if (value1 !== null) {
        json.value1 = value1;
    }
    if (value2 !== null) {
        json.value2 = value2;
    }
    if (value3 !== null) {
        json.value3 = value3;
    }
    json = JSON.stringify(json);

    const options = {
        hostname: 'maker.ifttt.com',
        path: `/trigger/${credentials.IFTTT_WEBHOOK_TRIGGER}/with/key/${credentials.IFTTT_ACCOUNT_KEY}`,
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
            'Content-Length': json.length
        }
    }

    const req = https.request(options, res => {
        res.on('error', err => {
            console.error(`Error during IFTTT webhook ${err.message}`);
        });
    });
    req.write(json);
    req.end();
}

function check_inactive() {
    var server_time = Math.floor(Date.now() / 1000);

    // check whole cache
    for (const host in cache) {
        var host_data = cache[host];
        for (const gpio in host_data) {
            var stored = host_data[gpio];
            // skip if already notified
            if (stored.value === undefined) {
                continue;
            }
            // notify if not seen for a while
            if (stored.time + INACTIVE_THRESHOLD_SEC < server_time) {
                console.warn(`host ${host} gpio ${gpio} : went silent`);
                stored.value = undefined;
                ifttt_webhook(host, gpio, "SILENT");
            }
        }
    }

    // schedule another check
    setTimeout(check_inactive, INACTIVE_THRESHOLD_SEC * 1000)
}

function web_handler(request, response) {

    if (request.method == 'POST' && request.url == '/gcn') {
        var body = ''
        request.on('data', function (data) {
            body += data
        })
        request.on('end', function () {
            // parse
            var params = querystring.parse(body);
            if (params == undefined || params == null) {
                reply(response, 400, ```Invalid body {body}```);
                return;
            }

            // analyse
            if (params.host == undefined || params.gpio == undefined || params.time == undefined || params.value == undefined) {
                reply(response, 400, ```Invalid body {body}```);
                return;
            }

            var current = {
                "ip": request.socket.remoteAddress,
                "host": params.host.trim(),
                "gpio": Number(params.gpio),
                "time": Number(params.time),
                "value": Number(params.value),
            };
            if (current.time == NaN || current.gpio == NaN || current.value == NaN) {
                reply(response, 400, ```Invalid body {body}```);
                return;
            }

            // console.debug('Received', current);

            // fetch last known
            var host_data = cache[current.host];
            if (host_data === undefined || host_data === null) {
                host_data = {};
                cache[current.host] = host_data;
            }

            // server time
            var server_time = Math.floor(Date.now() / 1000);

            // none found
            var stored = host_data[current.gpio];
            if (stored === undefined || stored === null) {
                stored = {
                    "time": server_time,
                    "value": current.value,
                    "ip": current.ip,
                };
                host_data[current.gpio] = stored;
                console.info(`Host ${current.host} gpio ${current.gpio} : detected from ${current.ip} with time ${current.time}`);
                ifttt_webhook(current.host, current.gpio, `DETECTED ${current.ip}`);
            }


            // check for ip change
            if (stored.ip !== current.ip) {
                console.info(`Host ${current.host} gpio ${current.gpio} : changed ip ${current.ip}`);
                ifttt_webhook(current.host, current.gpio, `NEWIP ${current.ip}`);
                stored.ip = current.ip;
            }

            // check when coming back
            if (stored.value === undefined) {
                console.info(`Host ${current.host} gpio ${current.gpio} : speaks again`);
                ifttt_webhook(current.host, current.gpio, "SPEAKING");
                stored.value = current.value;
            }

            // check if there was a value change
            if (stored.value !== current.value) {
                console.info(`Host ${current.host} gpio ${current.gpio} : value changed ${stored.value} => ${current.value}`);
                ifttt_webhook(current.host, current.gpio, current.value ? "STOP" : "START");
                stored.value = current.value;
            }

            // update latest entry
            host_data[current.gpio] = stored;

            // provide server unix timestamp to the client
            reply(response, 200, server_time.toString());
        })
    }
}

// start webserver
const server = http.createServer(web_handler);
server.listen(WEBSERVER_HTTP_PORT, WEBSERVER_LISTEN_ADDR);
console.log(`gcn starts, listening for http on address ${WEBSERVER_LISTEN_ADDR} and port ${WEBSERVER_HTTP_PORT}`);

// schedule first inactive check (fives a chance to everyone to say hello)
setTimeout(check_inactive, INACTIVE_THRESHOLD_SEC * 1000);
