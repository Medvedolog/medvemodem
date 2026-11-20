'use strict';
// The bot's loop: the bus, the engine's events, the queue and the long poll every tick; the snapshot; the stop.
import * as uloop from 'uloop';
import * as ubus from 'ubus';
import * as log from 'log';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import { preflight, cleanup as transportCleanup } from './transport.uc';
import { flush as flushEpisodes, settle as settleEpisodes, seed as seedEpisode } from './episodes.uc';
import { REFRESH_SECONDS, SNAPSHOT_FILE, UBUS_SOCKET, hasAdmin, hooks, live, noticeOn, secret, smsFromAllowed, state } from './conf.uc';
import { atomic, enqueue, prune, queueCount, queueHead, respond, restoreQueue, unsure } from './queue.uc';
import { episodeWait, incidentMessage, kernelEvents, serviceEvents } from './notify.uc';
import { disconnectBus, refreshDevices, refreshEvents, refreshStatus, refreshUplink, view } from './engine.uc';
import { activeTasks, callTelegram, cancelFollower, pollTimeout, probeQueue, refreshRoutes, routeList, runFollower, runProbe, watchdogTasks } from './links.uc';
import { forwardSms } from './sms.uc';
import { netWatchdog } from './net.uc';
import { settingsProbeDone } from './botset.uc';
import { onUpdate } from './pages.uc';
import { tick as updateTick } from './update.uc';

restoreQueue();
// the «Меню» button beside the input field, in the admin's chat only; once a start
if (hasAdmin) {
    let queued = false;
    for (let x in state.queue) if (x.method == 'setMyCommands') queued = true;
    if (!queued) enqueue('', 'info', {method:'setMyCommands'});
    state.greet = true;	/* the status and the keyboard once the engine was read (HANDOFF 199 item 14) */
}
function tick() {
    watchdogTasks();
    updateTick(time());
    netWatchdog();
    refreshRoutes();
    if (!state.bus && time() >= state.busRetryAt) {
        try {
            // 5 s, not ucode's 30: the bot's own calls to the engine run on the connection that serves
            // medvemodem.bot, and LuCI's call to the bot waits behind them (its RPC gives up after 20 s)
            state.bus = ubus.connect(UBUS_SOCKET, 5);
            if (state.bus) {
                state.listener = state.bus.listener('medvemodem.*', (event, data) => {
                    state.dirty = true;
                    // read on the next tick, not inside the event: no call to the engine from its own event
                    if (event == 'medvemodem.service' && length(state.svcEvents) < 8 && (data?.name == 'modemmanager' || data?.name == 'qmodem') &&
                        (data?.what == 'restarted' || data?.what == 'stopped'))
                        push(state.svcEvents, {name:data.name, what:data.what, by:data.by == 'medvemodem' ? 'medvemodem' : 'unknown',
                                               restarts_24h:type(data.restarts_24h) == 'int' ? data.restarts_24h : 0});
                    // the router's own trouble (the owner, 2026-10-31): a kind and a short name, nothing of the log line
                    if (event == 'medvemodem.kernel' && length(state.kernEvents) < 8 && type(data?.kind) == 'string' &&
                        match(data.kind, /^[a-z-]{1,16}$/))
                        push(state.kernEvents, {kind:data.kind, critical:data.critical === true,
                                                who:type(data.who) == 'string' && match(data.who, /^[A-Za-z0-9._:-]{1,47}$/) ? data.who : null});
                    if (event == 'medvemodem.sms' && live.smsForward && type(data?.device_id) == 'string' &&
                        length(data.device_id) <= 192 && type(data?.ids) == 'array' && length(state.smsNew) < 16 &&
                        smsFromAllowed(data.device_id))
                        push(state.smsNew, {id:data.device_id, ids:filter(slice(data.ids, 0, 20), (v) => type(v) == 'int')});
                    let id = data && (data.device_id || data.id);
                    if (type(id) == 'string' && length(id) <= 192 && length(keys(state.pendingIds)) < 64)
                        state.pendingIds[id] = true;
                });
                /* rpcd adds ubus_rpc_session to every call from LuCI, and the pinned ucode refuses a key the method does not
                 * declare (INVALID_ARGUMENT): the bot page read «не отвечает» while the shell's call worked (HANDOFF 199
                 * item 10; the owner, 2026-10-26) */
                const RPC = {ubus_rpc_session:''};
                state.service = state.bus.publish('medvemodem.bot', {
                    status: {call:(req, msg) => req.reply({schema_version:1,running:true,engine:state.engine,engine_error:state.engineError,modems:length(state.deviceIds),token_configured:true,username:state.botUsername,preflight:state.lastPreflight || 'ready',paired:!!hasAdmin}),args:RPC},
                    chat_test: {call:(req, msg) => {
                        if (!hasAdmin) return req.reply({ok:false,code:'admin_not_configured'});
                        let ok = enqueue(lang.T('MedveModem: test message · verifying bot connection',
                                                'MedveModem: тестовое сообщение · проверка связи с ботом'), 'info');
                        req.reply({ok,code:ok ? 'queued' : 'queue_unavailable'});
                    },args:RPC},
                    transport_status: {call:(req, msg) => req.reply({schema_version:1,poll:{state:state.errors.poll || (state.success.poll ? 'ok' : 'checking'),tier:length(live.routes) ? live.routes[state.tiers.poll % length(live.routes)].name : 'none',tunnel:state.tunnelBy.poll === true,last_ok:state.success.poll,conflicts:state.pollConflicts},send:{state:state.errors.send || (state.success.send ? 'ok' : 'checking'),tier:length(live.sendRoutes) ? live.sendRoutes[state.tiers.send % length(live.sendRoutes)].name : 'none',tunnel:state.tunnelBy.send === true,last_ok:state.success.send},follower:{last_probe:state.follower.last_probe}}),args:RPC},
                    queue_status: {call:(req, msg) => req.reply({schema_version:1,pending:queueCount('pending'),inflight:queueCount('inflight'),total:length(state.queue),bytes:state.queueBytes,dropped_info:state.dropped,uncertain:queueCount('uncertain'),persistence_error:state.queueFault,max_items:200,max_bytes:262144}),args:RPC},
                    audit_tail: {call:(req, msg) => req.reply({entries:[]}),args:RPC},
                    routes: {call:(req, msg) => req.reply({schema_version:1, poll:routeList(live.routes, 'poll'), send:routeList(live.sendRoutes, 'send'),
                        probing:state.probe.name || null, waiting:length(state.probe.queue), clash:live.clash || []}),args:RPC},
                    probe: {call:(req) => {
                        let msg = req.args;
                        let kind = msg?.kind == 'poll' || msg?.kind == 'send' ? msg.kind : null;
                        let name = type(msg?.route) == 'string' && match(msg.route, /^[A-Za-z0-9_.:!-]{1,64}$/) ? msg.route : null;
                        if (msg?.route && !name) return req.reply({ok:false, code:'bad_route'});
                        let n = probeQueue(kind, name);
                        req.reply({ok:n > 0 || state.probe.name != null, queued:n});
                    },args:{ubus_rpc_session:'', kind:'', route:''}}
                });
                // a refused publish returns null, it does not throw: without this the bot ran with no object
                // for LuCI and never tried again (2026-10-17 review)
                if (!state.service) die('publish_failed');
            }
        } catch (e) {
            log.syslog(log.LOG_ERR, 'ubus_connection_or_publish_failed');
            disconnectBus();
        }
    }
    // A router without RTC must remain online while NTP synchronizes.
    // NTP may be delayed on boards without RTC; check readiness only every 30s.
    if (!state.preflightChecked || time() - state.preflightChecked >= 30) {
        state.cachedPreflight = preflight(secret);
        state.preflightChecked = time();
    }
    let readiness = state.cachedPreflight;
    if (readiness != state.lastPreflight) {
        state.lastPreflight = readiness;
        log.syslog(readiness ? log.LOG_WARNING : log.LOG_INFO,
                   'preflight_%s', readiness || 'ready');
    }
    if (time() - state.last.devices >= REFRESH_SECONDS) refreshDevices();
    if (time() - state.last.uplink >= REFRESH_SECONDS) refreshUplink();
    if (time() - state.last.status >= REFRESH_SECONDS) refreshStatus();
    refreshEvents();
    if (hasAdmin) forwardSms();
    if (hasAdmin) serviceEvents();
    if (hasAdmin) kernelEvents();
    // the greeting of every start: the status screen with the reply keyboard, as /start gives it, once the engine
    // was read; it waits in the queue like any answer (the owner, 2026-10-22, HANDOFF 199 item 14)
    if (state.greet && state.bus && state.cache.status) {
        state.greet = false;
        respond(ui.header(view()), ui.menuKeyboard());
    }
    if (!readiness && !state.tasks.poll) callTelegram('poll','getUpdates',{
        offset:state.offset, timeout:pollTimeout(), allowed_updates:['message','callback_query']
    });
    prune();
    if (time() >= state.quietUntil) {
        if (state.restartInfo && state.bus && live.notices.enabled == '1' && live.mode != 'off' && live.mode != 'critical' && noticeOn('restart')) {
            let up = 0, carrier = state.uplinks.carrier;
            for (let id in state.deviceIds) if (state.deviceSafe[id].session.state == 'connected') up++;
            enqueue(ui.restartText({updated:state.restartInfo.updated, version:state.restartInfo.version, up,
                                    total:length(state.deviceIds), via:carrier ? (carrier.name || carrier.device) : null,
                                    internet:state.uplinks.internet}), 'info', {silent:true});
        }
        state.restartInfo = null;
        // once after the quiet, and at the bot's own start: the restart's blips forgotten, an internet already down opened
        if (!state.settled && state.baseline && state.uplinkBaseline) {
            settleEpisodes(state.episodes);
            seedEpisode(state.episodes, '-', 'reach', state.uplinks.internet, time());
            state.settled = true;
        }
        flushEpisodes(state.episodes, time(), live.mode, episodeWait, incidentMessage);
    }
    let h = queueHead();
    // If a notification appears during an 8s follower request while the long
    // poll occupies the other slot, stop the optional probe. Its async task
    // callback will release the slot; the next tick sends the notification.
    // Never exceed the two-curl cap just to deliver faster.
    if (!readiness && hasAdmin && h >= 0 && state.follower.task && !state.tasks.send)
        cancelFollower();
    if (!readiness && hasAdmin && h >= 0 && !state.tasks.send && activeTasks() < 2)
        callTelegram('send');
    if (!readiness && hasAdmin) runFollower();
    if (!readiness) runProbe();
}
state.startedAt = time();
uloop.init();
// Flash snapshots contain only HMAC digests and counters, never ubus replies: at most every snapshot_minutes
// and at a regular stop (ТЗ §13.3), and only when they changed
function writeSnapshot() {
    if (live.cfg.snapshot_minutes == '0') return;
    let fingerprints = sort(keys(state.seen));
    if (length(fingerprints) > 256) fingerprints = slice(fingerprints, 0, 256);
    let serialized = sprintf('%J', {schema:1,seen:fingerprints,dropped:state.dropped});
    // Compare to the existing flash image across restarts as well; no write
    // when the HMAC keys and counters have not changed.
    if (serialized == state.lastSnapshot) return;
    if (atomic(SNAPSHOT_FILE, serialized)) state.lastSnapshot = serialized;
}
uloop.interval(max(1,int(live.cfg.snapshot_minutes || 15)) * 60000, writeSnapshot);
uloop.interval(REFRESH_SECONDS == 60 ? 5000 : 1000, tick);
// procd SIGTERM: stop workers before releasing the singleton and ubus object.
function shutdown() {
    cancelFollower();
    if (state.netJob?.task) try { state.netJob.task.kill(15); } catch (e) {}
    if (state.probe.task) try { state.probe.task.kill(15); } catch (e) {}
    transportCleanup('check');
    for (let kind in ['poll','send']) {
        let task = state.tasks[kind], meta = state.taskInfo[kind];
        if (!task) continue;
        state.tasks[kind] = null;
        state.taskInfo[kind] = null;
        if (kind == 'send' && meta && meta.sentId) {
            unsure(meta.sentId);
        }
        try { task.kill(15); } catch (e) {}
        transportCleanup(kind);
    }
    disconnectBus();
    writeSnapshot();
    uloop.end();
}
let stopSignal = uloop.signal('SIGTERM', () => shutdown());
hooks.onFatal = shutdown;
hooks.onUpdate = onUpdate;
hooks.probeDone = settingsProbeDone;
tick();
uloop.run();
log.syslog(log.LOG_INFO, 'stopped');
if (state.exitCode) exit(state.exitCode);
