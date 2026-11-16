'use strict';
// The bot's start and what its modules share (the owner, 2026-10-31: main.uc had grown to 1900 lines): the paths, the
// salt, the settings, the administrator, the state, the routes. Imported first by main.uc; a module is evaluated once,
// so every module sees the same objects. What changes while the bot runs is in live, never in a variable of its own.
import * as fs from 'fs';
import * as uci from 'uci';
import * as log from 'log';
import { preflight, cleanup as transportCleanup } from './transport.uc';
import { discover } from './routes.uc';
import { label } from './safe.uc';
import * as lang from './lang.uc';
import * as daylog from './daylog.uc';

function testPath(env, fallback) {
    let val = getenv(env);
    return type(val) == 'string' && length(val) > 2 && length(val) <= 160 &&
           match(val, /^\/[A-Za-z0-9_.\/-]+$/) && index(val, '..') < 0 ? val : fallback;	/* ubus.sock, summary.json */
}
const DIR = testPath('MEDVEMODEM_BOT_STATE_DIR', '/tmp/medvemodem-bot');
const SALT_FILE = testPath('MEDVEMODEM_BOT_SALT_FILE', '/etc/medvemodem-bot/salt');
const SNAPSHOT_FILE = testPath('MEDVEMODEM_BOT_SNAPSHOT_FILE', '/etc/medvemodem-bot/state/summary.json');
const CONFIG_DIR = testPath('MEDVEMODEM_BOT_CONFIG_DIR', '/etc/config');
const UBUS_SOCKET = testPath('MEDVEMODEM_BOT_UBUS_SOCKET', '/var/run/ubus/ubus.sock');
const PROC = testPath('MEDVEMODEM_BOT_PROC', '/proc');	/* ModemManager's process, by its name (tests: a made-up /proc) */
const DAYLOG_FILE = DIR + '/daylog.json';
const TEST_REFRESH = int(getenv('MEDVEMODEM_BOT_TEST_REFRESH_SECONDS') || 60);
const REFRESH_SECONDS = TEST_REFRESH >= 1 && TEST_REFRESH <= 60 ? TEST_REFRESH : 60;
function newCursor() { return uci.cursor(CONFIG_DIR); }

// Kernel-enforced singleton lock. It is released automatically on process exit,
// including crashes. Unlike PID files, there is no stale-lock cleanup race.
log.openlog('medvemodem-bot', log.LOG_PID, log.LOG_DAEMON);
let instanceLock = fs.open(DIR + '/instance.lock', 'a+e', 0o600);
if (!instanceLock || !instanceLock.lock('xn')) { log.syslog(log.LOG_ERR, 'instance_lock_failed'); exit(1); }
const key = hexdec(fs.readfile(SALT_FILE, 64) || '');
if (!key || length(key) != 32) { log.syslog(log.LOG_ERR, 'hmac_key_invalid'); exit(1); }
let cursor = newCursor();
let cfg = cursor.get_all('medvemodem-bot', 'main') || {};
let user = cursor.get_all('medvemodem-bot', 'primary') || {};
// what changes while the bot runs, read by every module through this one object: the main settings and the
// notifications' (read again after the admin changed one from Telegram, botset.uc), the proxies' passwords, the routes
const live = {cfg, notices:{}, mode:'smart', smsForward:false, proxyPassword:null, routes:[], sendRoutes:[], clash:[]};
function loadNotices(c) {
    live.notices = c.get_all('medvemodem-bot', 'events') || {};
    live.mode = live.notices.mode || 'smart';
    if (live.mode != 'smart' && live.mode != 'all' && live.mode != 'critical' && live.mode != 'off') live.mode = 'smart';
}
loadNotices(cursor);
// a kind of notification switched off on the settings page: backup, services, quota, restart (on unless '0')
function noticeOn(kind) { return live.notices[kind] != '0'; }
let secret = cursor.get('medvemodem-bot-secret', 'main', 'token') || '';
let uid = '' + (user.user_id || '');
// a private chat's id is the user's id: an empty chat id is that one
let chat = '' + (user.chat_id || user.user_id || '');
// The init script checks incomplete configuration before spawning procd.
// The pairing window is the only exception to an empty administrator.
let pairingFile = DIR + '/pairing.json';
let pairingCandidate = DIR + '/pair-candidate.json';
function pairingOpen() {
    try {
        let p = json(fs.readfile(pairingFile, 256) || '{}');
        return type(p.code) == 'string' && match(p.code, /^[A-F0-9]{12}$/) &&
               type(p.until) == 'int' && p.until >= time() && p.until <= time() + 600 ? p : null;
    } catch (e) { return null; }
}
let hasAdmin = match(uid, /^[0-9]{3,20}$/) && match(chat, /^[0-9]{3,20}$/);
// the language (lang.uc): main.lang ru or en; auto (or none) is the admin's Telegram app's, remembered for a restart of
// the bot in the state directory, Russian until their first message
const LANG_FIXED = cfg.lang == 'ru' || cfg.lang == 'en' ? cfg.lang : null;
const LANG_FILE = DIR + '/lang';
lang.set(LANG_FIXED || trim(fs.readfile(LANG_FILE, 8) || '') || 'ru');
if (cfg.enabled != '1') { log.syslog(log.LOG_ERR, 'configuration_disabled'); exit(1); }
if (!hasAdmin && !pairingOpen()) { log.syslog(log.LOG_ERR, 'administrator_not_configured'); exit(1); }
let tokenError = preflight(secret);
if (tokenError && tokenError != 'clock_unsynced') {
    log.syslog(log.LOG_ERR, 'startup_preflight_%s', tokenError);
    exit(1);
}
log.syslog(log.LOG_INFO, 'started');

// Candidate routes are resolved from explicit configuration and currently
// available local SOCKS listeners. A failed discovery must not add direct access
// when the administrator selected proxy-only mode.
let pollCfg = cursor.get_all('medvemodem-bot', 'poll') || {};
let sendCfg = cursor.get_all('medvemodem-bot', 'send') || {};
// the proxies' passwords: the one LuCI sets for all, and each proxy added from Telegram its own (pw_<section>)
function proxySecrets(c) {
    let all = c.get_all('medvemodem-bot-secret', 'main') || {}, per = {};
    let shared = all.proxy_password_b64 ? b64dec(all.proxy_password_b64) : (all.proxy_password || '');
    if (type(shared) != 'string' || length(shared) > 512) shared = '';
    for (let k, v in all) {
        let m = match(k, /^pw_([A-Za-z0-9_]{1,32})$/), pw = m && type(v) == 'string' ? b64dec(v) : null;
        if (type(pw) == 'string' && length(pw) && length(pw) <= 512) per[m[1]] = pw;
    }
    return {shared, per};
}
live.proxyPassword = proxySecrets(cursor);
let profiles = discover(cursor, pollCfg, sendCfg, live.proxyPassword, []);	/* the other modems: after the first uplink read */
// Proxy-only setups must remain alive while a local proxy starts or recovers.
// No permitted route is a degraded status, not a procd respawn storm.
live.routes = profiles.poll;
live.sendRoutes = profiles.send;
live.clash = profiles.clash || [];
// the calls that would close a circle between modules, set by main.uc: the bot's own shutdown (links.uc on a fatal
// answer), an update from Telegram (links.uc → pages.uc), a route check that ended (links.uc → botset.uc)
const hooks = {onFatal:null, onUpdate:null, probeDone:null};
let state = {
    engine: 'starting', deviceSafe: {}, deviceIds: [], links: [], linkKey: '', telegramIp: null,
    daylog: daylog.load(DAYLOG_FILE, time()), mmPid: null,
    uplinks: {internet:'unknown', paths:[]},
    cache: {status:null, events:[]}, dirty: true, pendingIds: {}, historyReady: {}, smsNew: [], svcEvents: [], kernEvents: [],
    baseline: false, uplinkBaseline:false, carried:{}, lastCarrier:null, previous: {}, seen: {}, queue: [], queueBytes: 0, queueNonce:0,
    episodes:{}, fixed:{}, quietUntil:0, restartInfo:null, settled:false, uncertain:0, queueFault:false, lastSnapshot:null, lastPreflight:null, botUsername:null,
    dropped: 0, offset: 0, updatedAt: 0,
    last: {devices:0, uplink:0, status:0, events:{}},
    tasks: {poll:null, send:null}, attempts: {poll:0, send:0},
    tiers: {poll:0, send:0}, next: {poll:0, send:0},
    pollFailures:0, pollConflicts:0, engineError:null, busRetryAt:0, busErrorLogged:null,
    active: {poll:null,send:null},
    lastRouteRefresh:time(), lastIdentityProbe:0, net:{}, netJob:null,
    pollConflictSince:0,
    follower:{task:null,next:0,index:0,good:{},streak:{},last_probe:null,started:0,runId:0,turn:0},
    tunnelBy:{poll:null, send:null}, routeErrors:{},
    probe:{task:null, queue:[], results:{}, runId:0},
    ask:null, settingsProbe:null,
    jobId:0, taskInfo:{poll:null,send:null},
    errors: {poll:null, send:null}, success: {poll:null, send:null}
};
// Remove only stale private curl config files left by a crashed previous instance.
fs.unlink(DIR + '/curl-poll.cfg');
fs.unlink(DIR + '/curl-send.cfg');
fs.unlink(DIR + '/curl-probe.cfg');
transportCleanup('all');
let saved = fs.readfile(DIR + '/offset.json', 128);
if (saved) {
    try { let o = json(saved); if (type(o.offset) == 'int') state.offset = o.offset; } catch (e) {}
}
// Restore only opaque dedupe fingerprints, never raw journals or identifiers.
let snapshot = fs.readfile(SNAPSHOT_FILE, 65536);
if (snapshot) {
    try {
        let old = json(snapshot);
        for (let fp in (type(old.seen) == 'array' ? old.seen : [])) {
            if (length(keys(state.seen)) >= 256) break;
            if (type(fp) == 'string' && match(fp, /^hmac128:[a-f0-9]{32}$/))
                state.seen[fp] = time();
        }
    } catch (e) {}
}
state.lastSnapshot = snapshot || null;
const HOST = label(trim(fs.readfile('/proc/sys/kernel/hostname', 64) || ''));
// the package's own version (Makefile install); the engine's may differ after a partial upgrade
const BOT_VERSION_TEXT = trim(fs.readfile('/usr/share/medvemodem-bot/version', 64) || '');
const BOT_VERSION = match(BOT_VERSION_TEXT, /^[0-9][0-9a-z.~_-]{0,31}$/) ? BOT_VERSION_TEXT : null;
// control from Telegram only when the owner allowed it in LuCI (main.allow_control)
const CONTROL = cfg.allow_control == '1';
// incoming SMS of every modem, newest first, five: read from the engine's last reading without marking them seen
// (peek). Shown only when the owner allowed SMS in Telegram (main.allow_sms_content); never written down.
const SMS_ALLOWED = cfg.allow_sms_content == '1';
// a new SMS forwarded at once (the owner, 2026-10-23: r85), only with the SMS shown in Telegram allowed too: the engine's
// medvemodem.sms event names the modem and the new messages' storage indexes; they are read here, unmarked (peek), one
// message each, and never written down (the queue file skips them, as the inbox page)
live.smsForward = SMS_ALLOWED && cfg.sms_forward == '1';
// the modems a new SMS is forwarded from (the owner, 2026-11-04: «при включённой глобальной пересылке»): when
// MedveModem's own «Пересылка SMS» is on and limited to the ticked modems, the bot forwards from those only; otherwise
// from every modem, as before
function smsFromAllowed(id) {
    let fw = newCursor().get_all('medvemodem', 'sms_forward');
    if (type(fw) != 'object' || fw.enabled != '1' || fw.modems_all != '0') return true;
    let list = type(fw.modem) == 'array' ? fw.modem : type(fw.modem) == 'string' ? [fw.modem] : [];
    return index(list, id) >= 0;
}

// sending SMS and USSD from Telegram (the owner, 2026-11-12: «Отправку смс сделаешь?», «и ussd»): main.allow_send, off by
// default and apart from showing SMS and from managing modems: a USSD code or an SMS can cost money
const SEND = cfg.allow_send == '1';

// «⚙️ Настройки» (settings.uc): the changes go through rpcd's medvemodem.bot.config (the same checks as LuCI's), then
// the bot reads them again; main.allow_settings 0 in LuCI leaves the pages read-only
const SETTINGS = cfg.allow_settings != '0';

export { BOT_VERSION, CONTROL, SEND, DAYLOG_FILE, DIR, HOST, LANG_FILE, LANG_FIXED, PROC, REFRESH_SECONDS, SETTINGS, SMS_ALLOWED, SNAPSHOT_FILE, UBUS_SOCKET, chat, hasAdmin, hooks, key, live, loadNotices, newCursor, noticeOn, pairingCandidate, pairingFile, pairingOpen, pollCfg, proxySecrets, secret, sendCfg, smsFromAllowed, state, testPath, uid };
