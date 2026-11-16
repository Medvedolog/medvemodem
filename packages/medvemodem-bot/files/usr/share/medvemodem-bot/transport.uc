'use strict';
import * as fs from 'fs';
import * as uloop from 'uloop';

const DIR = getenv('MEDVEMODEM_BOT_STATE_DIR') || '/tmp/medvemodem-bot';
const CA = getenv('MEDVEMODEM_BOT_CA_FILE') || '/etc/ssl/certs/ca-certificates.crt';
const CONFIG_DIR = getenv('MEDVEMODEM_BOT_CONFIG_DIR') || '/etc/config';
const PORT = getenv('MEDVEMODEM_BOT_TEST_TELEGRAM_PORT') || '';
// This option is for an isolated localhost TLS server; production procd never sets it.
const TEST_PORT = match(PORT, /^[0-9]{4,5}$/) && int(PORT) >= 1024 && int(PORT) <= 65535 ? PORT : '';


function preflight(secret) {
    let info = fs.stat(CONFIG_DIR + '/medvemodem-bot-secret');
    // root's alone, as init-secrets sets and checks it (r128: 0640 was asked here while init-secrets made it 0600 since
    // the bot runs as root, r127: the bot never started, the owner's field report 01.11)
    if (!info || (info.mode & 0o777) != 0o600) return 'secret_permissions';
    if (!fs.access(CA, 'r')) return 'ca_bundle_missing';
    // Reject implausible RTC time, never disable certificate validation.
    if (time() < 1790812800) return 'clock_unsynced';
    if (!match(secret, /^[0-9]{6,12}:[A-Za-z0-9_-]{30,}$/)) return 'token_invalid';
    return null;
}

// curl's config reads \n, \r and \t in quotes: a newline in a hand-edited value never starts another option
// (HANDOFF 200 item 17)
function quoted(v) {
    v = replace(replace(v, /\\/g, '\\\\'), /"/g, '\\"');
    return '"' + replace(replace(replace(v, /\n/g, '\\n'), /\r/g, '\\r'), /\t/g, '\\t') + '"';
}

// Responses flow through pipes in RAM. Only the curl options (which contain
// the token) are staged in 0600 files on tmpfs and removed immediately.
function cleanup(kind) {
    if (kind != 'poll' && kind != 'send' && kind != 'probe' && kind != 'check' && kind != 'all') return;
    // Killing uloop.task stops the ucode worker, not necessarily its child curl.
    // Find only our curl processes by the unguessable tmpfs config path and the
    // executable name. Linux /proc/cmdline contains NULs; ucode index() is
    // binary-safe (length and memmem), unlike regex matching on this buffer.
    // A cancelled probe MUST NOT continue to occupy a hidden curl slot.
    let scanned = 0;
    for (let pid in (fs.lsdir('/proc') || [])) {
        if (++scanned > 2048) break;
        if (type(pid) != 'string' || !match(pid, /^[0-9]{1,10}$/)) continue;
        let comm = fs.readfile('/proc/' + pid + '/comm', 32);
        if (comm != 'curl\n' && comm != 'curl') continue;
        let args = fs.readfile('/proc/' + pid + '/cmdline', 4096);
        if (!args) continue;
        for (let k in ['poll', 'send', 'probe', 'check']) {
            if (kind != 'all' && k != kind) continue;
            if (index(args, DIR + '/curl-' + k + '-') < 0) continue;
            // PID is digits only. No dynamic shell text, token or proxy secret.
            system('kill -TERM ' + pid + ' >/dev/null 2>&1');
            break;
        }
    }
    for (let name in (fs.lsdir(DIR) || [])) {
        if (type(name) != 'string') continue;
        if (!match(name, /^curl-(poll|send|probe|check)-[a-f0-9]{16}\.cfg$/)) continue;
        if (kind == 'all' || substr(name, 5, length(kind)) == kind) fs.unlink(DIR + '/' + name);
    }
}

// uloop.task's input callback, never asked (the child never calls pipe.receive): the ucode that OpenWrt 24.10 and 25.12
// pin closes a task's output pipe only when it has one (fixed upstream in af8a383, 2026-09-01), so a task without it
// left one descriptor open for good; at a poll every ~25 s the bot ran out of them in ~7.6 h and restarted
// («transport_poll_task_unavailable_No file descriptors available», the owner's OPiZ3 log 02.11)
function noInput() { return null; }
function start(kind, token, route, method, body, callback) {
    // check: LuCI's check of one route (audit R02), apart from the follower's probe, which a send cancels
    if (kind != 'poll' && kind != 'send' && kind != 'probe' && kind != 'check') return null;
    let randomBytes = fs.readfile('/dev/urandom', 8);
    if (!randomBytes || length(randomBytes) != 8) return null;
    let configFile = DIR + '/curl-' + kind + '-' + hexenc(randomBytes) + '.cfg';
    let task = uloop.task((pipe) => {
        let error = null, data = null, reached = null, uploaded = null, ms = null;
        try {
            // a document (the log) goes as a multipart form: the file by its path, never through a command line
            let form = method == 'sendDocument' && type(body?.file) == 'string' && match(body.file, /^\/[A-Za-z0-9_.\/-]+$/);
            let options = [
                'url = ' + quoted('https://api.telegram.org' + (TEST_PORT ? ':' + TEST_PORT : '') + '/bot' + token + '/' + method),
                'request = "POST"'];
            if (form) {
                push(options, 'form = ' + quoted('chat_id=' + body.chat_id));
                if (length(body.caption)) push(options, 'form-string = ' + quoted('caption=' + body.caption));
                push(options, 'form = ' + quoted('document=@' + body.file + ';filename=' + (body.filename || 'log.txt') + ';type=text/plain'));
            } else push(options, 'header = "Content-Type: application/json"', 'data = ' + quoted(sprintf('%J', body)));
            push(options,
                'cacert = ' + quoted(CA),
                'silent', 'max-filesize = 131072',
                'connect-timeout = 8',
                // a long poll waits body.timeout on the server, then a margin for the answer
                'max-time = ' + (kind == 'poll' ? '' + (int(body?.timeout || 0) + 15) : kind == 'probe' || kind == 'check' ? '8' : '15'),
                'noproxy = ""',
                'proxy = ""');
            if (TEST_PORT) push(options, 'resolve = ' + quoted('api.telegram.org:' + TEST_PORT + ':127.0.0.1'));
            else if (route && route.resolve) push(options, 'resolve = ' + quoted(route.resolve));
            if (route && route.bind_interface) push(options, 'interface = ' + quoted(route.bind_interface));
            // the last line: the bytes of the request sent (0: Telegram never got it, a stalled proxy, so it goes again)
            // and, for a direct call, the address it reached: routes through the other modems use it, their DNS may be
            // the dead main link's. curl writes it on a failure too
            let direct = !(route && route.proxy);
            push(options, 'write-out = "\\n%{size_upload}|%{remote_ip}|%{time_total}"');
            if (route && route.proxy) {
                push(options, 'proxy = ' + quoted(route.proxy));
                if (route.auth) push(options, 'proxy-user = ' + quoted(route.auth));
            }
            let f = fs.open(configFile, 'wx', 0o600);
            if (!f) die('config_create_failed');
            let configText = join('\n', options) + '\n';
            if (f.write(configText) != length(configText) || !f.close())
                die('config_write_failed');
            // OpenWrt 24.10 and 25.12 pin ucode versions whose fs.popen()
            // accepts ONLY a command string, not argv arrays. The shell command
            // is entirely constructed from the fixed allowlisted kind above;
            // token, proxy, interface and JSON live exclusively in 0600 tmpfs.
            // uloop.task already redirects stderr to /dev/null in both versions.
            // Config pathname is generated from 8 random bytes, no shell metacharacters.
            let proc = fs.popen('/usr/bin/curl -q -K ' + configFile, 'r');
            if (!proc) die('curl_unavailable');
            let output = proc.read(131200);
            let rc = proc.close();
            if (output) {
                let at = rindex(output, '\n');
                let tail = match(at >= 0 ? substr(output, at + 1) : '', /^([0-9]{1,9})\|([0-9a-fA-F.:]*)\|?([0-9.]{0,12})$/);
                if (tail) {
                    uploaded = int(tail[1]);
                    // the request's own time (curl's), for a route's check in LuCI (audit R02)
                    if (match(tail[3], /^[0-9]{1,5}(\.[0-9]+)?$/)) ms = int(+tail[3] * 1000);
                    if (direct && match(tail[2], /^[0-9.]{7,15}$/)) reached = tail[2];
                    output = substr(output, 0, at);
                }
            }
            if (length(output || '') > 131072) die('response_too_large');
            if (rc != 0) die('curl_exit_' + rc);
            if (!output) die('empty_response');
            data = json(output);
            if (type(data) != 'object') die('invalid_json');
        }
        catch (e) {
            let reason = '' + e;
            error = match(reason, /^curl_exit_[0-9]+$/) ? reason :
                    reason == 'response_too_large' ? reason : 'transport_failure';
        }
        fs.unlink(configFile);
        // Raw remote payload remains in RAM only, bounded to 128 KiB.
        pipe.send({error, data, reached, uploaded, ms});
    }, (message) => callback(message || {error: 'task_failure'}), noInput);
    return task;
}

export { preflight, start, cleanup };
