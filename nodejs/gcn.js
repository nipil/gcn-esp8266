// Server side for GCN, simplest tool
// MIT licence
// nodejs 8.11.1

// common
const http = require('http');
const https = require('https');
const querystring = require('querystring');

// your data
const IFTTT_ACCOUNT_KEY = "xxxxxxxxxxxxxxxxx";
const IFTTT_WEBHOOK_TRIGGER = "yyyyy";

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
        path: `/trigger/${IFTTT_WEBHOOK_TRIGGER}/with/key/${IFTTT_ACCOUNT_KEY}`,
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
            var gpio_data = host_data[gpio];
            // skip if already notified
            if (gpio_data.value === undefined) {
                continue;
            }
            // notify if not seen for a while
            if (gpio_data.time + INACTIVE_THRESHOLD_SEC < server_time) {
                console.warn(`host ${host} gpio ${gpio} : went silent`);
                gpio_data.value = undefined;
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

            var sample = {
                "host": params.host.trim(),
                "gpio": Number(params.gpio),
                "time": Number(params.time),
                "value": Number(params.value),
            };
            if (sample.time == NaN || sample.gpio == NaN || sample.value == NaN) {
                reply(response, 400, ```Invalid body {body}```);
                return;
            }

            // console.debug('Received', sample);

            // fetch last known
            var host_data = cache[sample.host];
            if (host_data === undefined || host_data === null) {
                host_data = {};
                cache[sample.host] = host_data;
            }

            // server time
            var server_time = Math.floor(Date.now() / 1000);

            // none found
            var gpio_data = host_data[sample.gpio];
            if (gpio_data === undefined || gpio_data === null) {
                console.info(`Host ${sample.host} gpio ${sample.gpio} : detected with time ${sample.time}`);
                gpio_data = {
                    "time": server_time,
                    "value": sample.value,
                };
                host_data[sample.gpio] = gpio_data;
                ifttt_webhook(sample.host, sample.gpio, "DETECTED");
            }

            // check when coming back
            if (gpio_data.value === undefined) {
                console.info(`Host ${sample.host} gpio ${sample.gpio} : speaks again`);
                ifttt_webhook(sample.host, sample.gpio, "SPEAKING");
            }

            // check if there was a value change
            if (gpio_data.value !== undefined && gpio_data.value !== sample.value) {
                console.info(`Host ${sample.host} gpio ${sample.gpio} : value changed ${gpio.value} => ${sample.value}`);
                ifttt_webhook(sample.host, sample.gpio, sample.value ? "STOP" : "START");
            }

            // update latest entry
            host_data[sample.gpio] = {
                "time": server_time,
                "value": sample.value,
            };

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
