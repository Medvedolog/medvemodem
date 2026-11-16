'use strict';
// The engine's picture: medvemodem status, devices, uplink and events read into the state, projected (safe.uc), and the
// view the pages are drawn from.
import * as fs from 'fs';
import * as uci from 'uci';
import * as log from 'log';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import * as daylog from './daylog.uc';
import { opaque, label, modem, uplink, stepWordKey, eventPair, reading } from './safe.uc';
import { BOT_VERSION, HOST, REFRESH_SECONDS, key, live, state } from './conf.uc';
import { dayNote, report, watchModemManager } from './notify.uc';

function disconnectBus() {
    if (state.bus) {
        try { state.bus.disconnect(); } catch (e) {}
    }
    state.bus = null;
    state.service = null;
    state.listener = null;
    state.busRetryAt = time() + 5;
}
function engineFailure(error, method) {
    // NOT_FOUND is an absent engine, not a broken ubusd socket. Retain our
    // published service and listener so LuCI remains able to diagnose it.
    let reason = '' + (error || 'ubus_no_reply');
    if (state.bus) {
        try { reason += ' ' + (state.bus.error() || ''); } catch (e) {}
    }
    // libubus's words (ubus_strerror): the engine answered and refused the call (an older engine without a
    // method, an argument it does not know) keeps the connection too; only a dead link reconnects
    let code = match(reason, /(PERMISSION_DENIED|permission denied|access denied)/i) ? 'acl_denied' :
               match(reason, /(method not found|invalid argument|not supported|parsing message)/i) ? 'engine_call_refused' :
               match(reason, /(NOT_FOUND|not found|object not found)/i) ? 'engine_not_found' :
               match(reason, /(broken pipe|connection refused|connection reset|disconnected|connection closed)/i) ? 'bus_disconnected' :
               match(reason, /(timed out|timeout)/i) ? 'engine_timeout' :
               'engine_call_failed';
    // ucode says «Invalid argument: Failed to resolve object name» both for a call it refused and for a ubusd
    // that went away (pinned 24.10 and 25.12): the link is asked directly — a live ubusd lists, even nothing
    if (code != 'bus_disconnected' && state.bus) {
        let alive = null;
        try { alive = state.bus.list(); } catch (e) {}	/* by name it is null for an absent engine too */
        if (alive == null) code = 'bus_disconnected';
    }
    if (state.busErrorLogged != code) {
        log.syslog(log.LOG_ERR, 'ubus_%s_%s', method, code);
        state.busErrorLogged = code;
    }
    // a slow engine is not a broken link: dropping the bus withdrew medvemodem.bot and LuCI read «не отвечает»
    // (review 2026-10-22, HANDOFF 199 item 10)
    if (code != 'engine_not_found' && code != 'acl_denied' && code != 'engine_call_refused' && code != 'engine_timeout') disconnectBus();
    if (state.engine != 'offline') { report('engine', null, state.engine, 'offline'); dayNote('svc', 'down'); }
    state.engine = 'offline';
    state.engineError = code;
}
// the services' memory and restarts from the engine's status: numbers and flags only
function serviceMem(r) {
    let m = {};
    if (type(r?.self?.rss_kb) == 'int') m.engine = {rss_kb:r.self.rss_kb};
    // the engine's own respawns and the bot's restarts, as the engine counted them (the owner, 2026-10-31)
    let me = r?.services?.medvemodem, bot = r?.services?.bot;
    if (m.engine && type(me?.restarts_24h) == 'int') m.engine.restarts_24h = me.restarts_24h;
    if (type(bot?.restarts_24h) == 'int') m.bot = {restarts_24h:bot.restarts_24h};
    for (let k in ['modemmanager', 'qmodem']) {
        let e = r?.services?.[k];
        if (type(e) != 'object') continue;
        m[k] = {running:e.running === true, rss_kb:type(e.rss_kb) == 'int' ? e.rss_kb : null,
                restarts_24h:type(e.restarts_24h) == 'int' ? e.restarts_24h : 0};
    }
    return m;
}
function botRssKb() {
    let v = match(fs.readfile('/proc/self/status', 4096) || '', /VmRSS:[ \t]*([0-9]+)/);
    return v ? int(v[1]) : null;
}
function refreshStatus() {
    if (!state.bus || time() - state.last.status < REFRESH_SECONDS) return;
    state.last.status = time();
    try {
        let r = state.bus.call('medvemodem', 'status', {});
        if (!r) die('ubus_no_reply');
        // Only a minimal status projection; never cache the raw reply.
        let prev = state.cache.status, next = {version:label(r.version), uptime:type(r.uptime) == 'int' ? r.uptime : 0, devices:r.devices || 0,
                                               mem:serviceMem(r)};
        // the engine restarted or was updated while the bot ran: quiet for 2 min, then one summary (plan step 3);
        // at the bot's own start the greeting says it
        if (prev && (next.version != prev.version || next.uptime + REFRESH_SECONDS < prev.uptime)) {
            state.quietUntil = time() + 120;
            state.settled = false;
            state.restartInfo = {updated:next.version != prev.version, version:next.version};
            dayNote('svc', 'restarted');
            state.episodes = {};
        } else if (!prev && next.uptime < 120) state.quietUntil = time() + 120 - next.uptime;
        state.cache.status = next;
    } catch (e) {
        engineFailure(e, 'status');
    }
    watchModemManager();
}
function refreshDevices() {
    if (!state.bus || time() - state.last.devices < REFRESH_SECONDS) return;
    state.last.devices = time();
    let response;
    try { response = state.bus.call('medvemodem', 'devices', {absent:true}); if (!response) die('ubus_no_reply'); }
    catch (e) {
        engineFailure(e, 'devices'); return;
    }
    if (state.engine == 'offline') report('engine', null, 'offline', 'ok');
    state.engine = 'ok'; state.engineError = null; state.busErrorLogged = null;
    let ids = [], projected = {}, rawMap = {}, rank = {};
    let priorEvents = state.lastEventMarkers || {};
    let nextEvents = {};
    for (let d in (response.devices || [])) {
        if (length(ids) >= 32) break;
        if (!d || type(d.device_id) != 'string') continue;
        let safe = modem(d, key);
        if (!safe) continue;
        let id = d.device_id;
        // without ModemManager: the engine's last reading (at most a minute old; a web modem's page asked at most once a
        // minute, an AT port never: kept), as the dashboard reads it
        let ctl = d.owner?.control;
        if (safe.present && !d.modemmanager && !safe.frozen && ctl && ctl != 'modemmanager' && ctl != 'none' && ctl != 'unresolved') {
            let r = null;
            try { r = state.bus.call('medvemodem', 'metrics', {id:id, kept:true, max_age_ms:60000}); } catch (e) { r = null; }
            if (type(r?.metrics) == 'object') reading(safe, r.metrics);
        }
        push(ids, id);
        projected[id] = safe;
        // the order of «Модемы» and the status (the owner, 2026-10-30: «китайский первым, хотя должен первым быть основной»):
        // the one carrying the traffic, then by the channel priority (the interface metric), the rest after
        let metric = d.connection?.metric;
        rank[id] = d.uplink?.role == 'carrying' ? -1 : type(metric) == 'int' ? metric : 1e9;
        // the main channel is remembered for a while: a main modem pulled out loses the role before its outage is told,
        // and was then told as a backup or not at all (the owner, 2026-11-16: «бот вообще молчал о происходящем»)
        if (d.uplink?.role == 'carrying') state.carried[id] = time();
        // Retain ONLY state machine attributes in memory; no MM object/ICCID.
        rawMap[id] = {present:safe.present, session:safe.session.state,
                      reach:safe.uplink.reach, quota:safe.quota.state};
        let before = state.previous[id];
        if (before) {
            for (let field, kind in {present:'present',session:'session',reach:'reach',quota:'quota'})
                if (before[field] != rawMap[id][field]) report(kind,id,before[field],rawMap[id][field]);
            // a modem gone from USB is a failure of the day too («За сутки»), whatever the notices say
            if (before.present && !rawMap[id].present && state.baseline) dayNote('usb', 'gone');
        }
        // r50 last_event is a cheap change indicator in the devices reply.
        // A first snapshot establishes a baseline; only changes fetch events.
        let last = d.last_event || {};
        let mark = type(last.m) == 'int' ? '' + last.m : null;
        if (mark) {
            nextEvents[id] = mark;
            // Read the first journal immediately, even on the initial device
            // snapshot. It builds the dedupe history without sending old alerts;
            // the next increment of last_event.m is then treated as a new event.
            if (!priorEvents[id] && length(keys(state.pendingIds)) < 64) {
                state.pendingIds[id] = true;
            }
            if (priorEvents[id] && priorEvents[id] != mark && length(keys(state.pendingIds)) < 64) {
                state.pendingIds[id] = true;
                if (!state.eventFloors) state.eventFloors = {};
                state.eventFloors[id] = int(priorEvents[id]);
            }
        }
        // Ubus notifications remain a faster signal when available.
    }
    let order = {};
    for (let i = 0; i < length(ids); i++) order[ids[i]] = i;
    ids = sort(ids, (a, b) => rank[a] != rank[b] ? (rank[a] < rank[b] ? -1 : 1) : order[a] - order[b]);
    for (let id, old in state.previous) {
        if (old.present && !rawMap[id]) report('present', id, true, false);
    }
    // two modems of one name: the USB port tells them apart, as «Порты и устройства» does (plan step 5)
    let seenNames = {};
    for (let id in ids) seenNames[projected[id].name] = (seenNames[projected[id].name] || 0) + 1;
    let nth = {};
    for (let id in ids) {
        let n = projected[id].name;
        if (seenNames[n] < 2) continue;
        nth[n] = (nth[n] || 0) + 1;
        projected[id].name = label(n + ' (' + (projected[id].port ? lang.T('USB port ', 'USB-порт ') + projected[id].port : '№' + nth[n]) + ')');
    }
    state.deviceIds = ids;
    state.deviceSafe = projected;
    state.devicesReadAt = time();
    state.previous = rawMap;
    state.lastEventMarkers = nextEvents;
    // Avoid unbounded growth across repeated USB re-enumerations.
    for (let id in keys(state.last.events))
        if (!projected[id]) delete state.last.events[id];
    // and the first-read marks of modems gone (audit 13.5)
    for (let id in keys(state.historyReady))
        if (!projected[id]) delete state.historyReady[id];
    for (let id in keys(state.carried))
        if (!projected[id]) delete state.carried[id];
    state.baseline = true;
}
function refreshUplink() {
    if (!state.bus || time() - state.last.uplink < REFRESH_SECONDS) return;
    state.last.uplink = time();
    try {
        let raw = state.bus.call('medvemodem', 'uplink', {});
        if (!raw) die('ubus_no_reply');
        let next = uplink(raw);
        if (state.uplinkBaseline && state.uplinks.internet != next.internet)
            report('reach', null, state.uplinks.internet, next.internet);
        // Track failover from safe carrier identity, never raw detail strings.
        // against the last channel that carried the traffic, not the last reading: a moment without one between the two
        // (the main modem pulled out, the backup's route not yet up) lost the switch (the owner, 2026-11-16)
        let was = state.lastCarrier;
        if (state.uplinkBaseline && was && next.carrier &&
            (was.device != next.carrier.device || (was.name || '') != (next.carrier.name || '')))
            report('failover', null, was.name || was.device, next.carrier.name || next.carrier.device);
        if (state.uplinkBaseline && was && next.carrier &&
            (was.device != next.carrier.device || (was.name || '') != (next.carrier.name || '')))
            dayNote('main', 'switch');
        if (next.carrier) state.lastCarrier = {device:next.carrier.device, name:next.carrier.name};
        // spec §44: the whitelist mode turned on or off, or its server moved
        let w0 = state.uplinks?.wlmode, w1 = next.wlmode;
        if (state.uplinkBaseline && w1 && (!!w0?.on != !!w1.on || (w1.on && w0?.server && w1.server && w0.server != w1.server)))
            report('wlmode', null, w0?.on ? 'on' : 'off', w1);
        state.uplinks = next;
        state.uplinkBaseline = true;
        if (next.internet == 'available' || next.internet == 'limited' || next.internet == 'unavailable') dayNote('net', next.internet);
        // the modems that do not carry the default route: live.routes to Telegram of their own when the main one fails
        let links = [];
        for (let p in next.paths)
            if (p.device && (!next.carrier || p.device != next.carrier.device))
                // an unnamed modem by the name its card has (model, product), not by its device (the owner, 2026-11-16:
                // «часть модемов без названий» in the bot page's routes: «через модем eth1»)
                push(links, {device:p.device, name:p.name || (p.id ? state.deviceSafe?.[p.id]?.name : null) || null, role:p.role});
        let linkKey = join(',', map(links, (l) => l.device + '=' + l.role));
        if (linkKey != state.linkKey) { state.links = links; state.linkKey = linkKey; state.lastRouteRefresh = 0; }
    } catch (e) {
        engineFailure(e, 'uplink');
    }
}
function refreshEvents() {
    if (!state.bus) return;
    for (let id in state.deviceIds) {
        if (!state.pendingIds[id] || time() - (state.last.events[id] || 0) < REFRESH_SECONDS) continue;
        state.last.events[id] = time();
        delete state.pendingIds[id];
        let firstRead = !state.historyReady[id];
        try {
            let answer = state.bus.call('medvemodem','events',{id,limit:64});
            if (!answer) die('ubus_no_reply');
            for (let e in (answer.events || [])) {
                // Only fixed enum; NEVER persist or emit untrusted detail.
                if (!e || type(e.kind) != 'string') continue;
                // Never replay old journal entries on the first r50 marker change.
                if (state.eventFloors && type(state.eventFloors[id]) == 'int' &&
                    type(e.m) == 'int' && e.m <= state.eventFloors[id]) continue;
                let name = e.kind;
                // both words kept: /events says them in the language of the moment it is read; the why only from the
                // engine's closed lists (eventPair), never the raw detail: it can contain PII or SMS
                let summary = eventPair(name, e.detail);
                if (!summary) continue;
                let fp = opaque(key, id + ':' + e.m + ':' + e.t + ':' + name);
                if (state.seen[fp]) continue;
                state.seen[fp] = time();
                // /events: the engine's time of the event, the modem's name, the why in words
                if (length(state.cache.events) >= 64) shift(state.cache.events);
                push(state.cache.events, {t:type(e.t) == 'int' ? e.t : null, summary,
                                          name:state.deviceSafe[id] ? state.deviceSafe[id].name : null});
                // the step after which a link came back, for «Связь восстановилась после шага …» (only the engine's closed list of levels)
                if (name == 'recovery-end' && type(e.detail) == 'string') {
                    let step = match(e.detail, /^([a-z-]{1,12}) succeeded/);
                    if (step && stepWordKey(step[1]) && type(e.t) == 'int') {
                        // only a recent step is told; an old one (a modem gone since) is forgotten here
                        for (let k, v in state.fixed) if (e.t - v.t > 3600) delete state.fixed[k];
                        state.fixed[id] = {step:step[1], t:e.t};
                    }
                }
                // Never replay the initial journal batch as new alerts. Sessions and recovery are not told one entry
                // each (four «Сессия подключена» after a restart, 26.10): the episodes say them as one situation
                // (docs/BOT-NOTIFICATIONS-PLAN.md step 1); the quota is told as it comes.
                // the quota is told once, by its state (report 'quota'); its journal entry only goes to /events (audit r117
                // B02: both told it, in two wordings)
            }
            state.historyReady[id] = true;
            if (state.eventFloors) delete state.eventFloors[id];
        } catch (e) { engineFailure(e, 'events'); break; }
    }
    // Compact bounded dedupe window; flash snapshot stores only opaque keys.
    for (let keyName, at in state.seen)
        if (time() - at > 7200) delete state.seen[keyName];
    let seenKeys = keys(state.seen);
    while (length(seenKeys) > 512) delete state.seen[shift(seenKeys)];
}
function view() {
    let proxies = 0, proxyNames = [], proxyTitles = {};
    for (let r in live.routes) if (r.proxy) { proxies++; push(proxyNames, r.name); if (r.title) proxyTitles[r.name] = r.title; }
    return {host:HOST, ids:state.deviceIds, devices:state.deviceSafe, uplinks:state.uplinks,
            route:length(live.routes) ? live.routes[state.tiers.poll % length(live.routes)] : null, proxies, proxyNames, proxyTitles, tunnel:state.tunnelBy.poll, routeErrors:state.routeErrors,
            engine:state.engine, version:(state.cache.status || {}).version, bot_version:BOT_VERSION,
            mem:{...((state.cache.status || {}).mem || {}), bot:{...(((state.cache.status || {}).mem || {}).bot || {}), rss_kb:botRssKb()}},
            day:daylog.summary(state.daylog, time())};
}
/* the router itself for «Статус» (the owner, 2026-10-26: «там про
 * роутер много полезного»): the model and the OS, the time up, the load, the free memory, the LAN address, the tunnels */
function routerInfo() {
    let up = split(trim(fs.readfile('/proc/uptime', 64) || ''), ' ')[0], load = split(trim(fs.readfile('/proc/loadavg', 64) || ''), ' ');
    let mem = fs.readfile('/proc/meminfo', 4096) || '', total = match(mem, /MemTotal:[ \t]*([0-9]+)/), free = match(mem, /MemAvailable:[ \t]*([0-9]+)/);
    let os = match(fs.readfile('/etc/openwrt_release', 2048) || '', /DISTRIB_DESCRIPTION='([^'\n]{1,64})'/);
    let lan = null, tunnels = [];
    try { lan = uci.cursor().get('network', 'lan', 'ipaddr'); } catch (e) {}
    for (let n in (fs.lsdir('/sys/class/net') || []))
        if (match(n, /^(tun|wg|awg|tailscale|zt|ppp|l2tp|ipsec)[0-9a-z_-]{0,12}$/)) push(tunnels, n);
    return {model:label(trim(fs.readfile('/tmp/sysinfo/model', 96) || '')), os:os ? label(os[1]) : null,
            uptime_s:up && match(up, /^[0-9]+/) ? int(up) : null, load:length(load) >= 3 && match(join(' ', slice(load, 0, 3)), /^[0-9]{1,4}\.[0-9]{2}( [0-9]{1,4}\.[0-9]{2}){2}$/) ?
                join(' / ', slice(load, 0, 3)) : null,
            mem_total_mb:total ? int(int(total[1]) >> 10) : null, mem_free_mb:free ? int(int(free[1]) >> 10) : null,
            lan:type(lan) == 'string' && match(lan, /^[0-9.]{7,15}$/) ? lan : null, tunnels:slice(sort(tunnels), 0, 8)};
}
// an answer reads the engine again if its picture is older than a few seconds: «Обновить» must mean it
function freshen() {
    if (!state.bus) return;
    if (time() - state.last.devices >= 5) { state.last.devices = 0; refreshDevices(); }
    if (time() - state.last.uplink >= 5) { state.last.uplink = 0; refreshUplink(); }
}
function idByKey(short) {
    for (let id in state.deviceIds)
        if (ui.shortKey(state.deviceSafe[id]) == short) return id;
    return null;
}
function deviceByKey(short) {
    let id = idByKey(short);
    return id ? state.deviceSafe[id] : null;
}

export { disconnectBus, freshen, idByKey, refreshDevices, refreshEvents, refreshStatus, refreshUplink, routerInfo, view };
