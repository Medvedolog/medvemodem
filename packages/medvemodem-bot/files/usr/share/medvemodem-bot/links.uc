'use strict';
// The way to Telegram: the routes found (routes.uc), the long poll and the sends through them, the follower that goes
// back to the first route, the route checks LuCI and the settings page ask for.
import * as fs from 'fs';
import * as uloop from 'uloop';
import * as log from 'log';
import { label } from './safe.uc';
import { classify, routeHealthy } from './policy.uc';
import { discover, validPublicV4 } from './routes.uc';
import { start as transportStart, cleanup as transportCleanup } from './transport.uc';
import { hooks, live, newCursor, pollCfg, secret, sendCfg, state } from './conf.uc';
import { CREATES, dropItem, markUnsure, persistQueue, queueHead, remember, request, unsure } from './queue.uc';

function activeTasks() {
    return (state.tasks.poll ? 1 : 0) + (state.tasks.send ? 1 : 0) + (state.follower.task ? 1 : 0) + (state.probe.task ? 1 : 0);
}
// a route through another modem goes to the address of api.telegram.org a direct call reached last (the router's
// DNS may answer only through the main link); before any, the long-standing Bot API address. TLS still checks the
// name: a wrong address only fails.
function routed(route) {
    if (!route || route.source != 'modem' || route.resolve) return route;
    let r = {};
    for (let k, v in route) r[k] = v;
    r.resolve = 'api.telegram.org:443:' + (state.telegramIp || '149.154.167.220');
    return r;
}
function reachedTelegram(route, result, kind) {
    if (route && !route.proxy && route.source != 'emergency' && validPublicV4(result?.reached))
        state.telegramIp = result.reached;
    // a direct call that reached a fake-IP (198.18/15) went into a tunnel: podkop and its kin answer the router's DNS
    // with one and sing-box carries the connection (the owner 2026-10-28: «пишет напрямую хотя через прокси»); kept per
    // profile, the request that saw it (audit R12: one flag described both poll and send)
    if (route?.source == 'direct' && type(result?.reached) == 'string' && (kind == 'poll' || kind == 'send'))
        state.tunnelBy[kind] = match(result.reached, /^198\.1[89]\./) ? true : false;
}
function refreshRoutes() {
    // with no route at all (a proxy-only bot while the manager's config is being rewritten) every 15 s, not 5 min
    // (audit r117 B04); 300 s stays the background check
    let none = !length(live.routes) || !length(live.sendRoutes);
    if (time() - state.lastRouteRefresh < (none ? 15 : 300)) return;
    state.lastRouteRefresh = time();
    try {
        let next = discover(newCursor(), pollCfg, sendCfg, live.proxyPassword, state.links);
        let activePoll = length(live.routes) ? live.routes[state.tiers.poll % length(live.routes)]?.name : null;
        let activeSend = length(live.sendRoutes) ? live.sendRoutes[state.tiers.send % length(live.sendRoutes)]?.name : null;
        // Also accept empty candidate lists. Retaining a removed route would
        // otherwise violate proxy-only policy until a process restart.
        live.routes = next.poll;
        live.sendRoutes = next.send;
        live.clash = next.clash || [];
        state.tiers.poll = 0;
        state.tiers.send = 0;
        for (let i = 0; i < length(live.routes); i++)
            if (live.routes[i].name == activePoll) { state.tiers.poll = i; break; }
        for (let i = 0; i < length(live.sendRoutes); i++)
            if (live.sendRoutes[i].name == activeSend) { state.tiers.send = i; break; }
        if (!length(live.routes)) state.errors.poll = 'no_permitted_route';
        else if (state.errors.poll == 'no_permitted_route') state.errors.poll = null;
        if (!length(live.sendRoutes)) state.errors.send = 'no_permitted_route';
        else if (state.errors.send == 'no_permitted_route') state.errors.send = null;
    } catch (e) {}
}
function runFollower() {
    if (state.follower.task || time() < state.follower.next || !length(live.routes)) return;
    // Reserve a curl slot for outgoing notifications before any background probe.
    if (queueHead() >= 0 || state.tasks.send) return;
    if (activeTasks() >= 2) return;
    let probeRoutes = [];
    for (let r in live.routes) if (r.proxy) push(probeRoutes, r);
    if (!length(probeRoutes)) probeRoutes = [live.routes[0]];
    // Off the first route (poll or send): the first route is what is probed, every 5 minutes, to go back to it; a
    // metered modem or an emergency address carried the bot for an hour before (two probes 30 min apart)
    // each profile goes back to its own first route (audit R08: poll's first proxy proved nothing about send's direct);
    // the two take turns when both are away
    let awayPoll = state.tiers.poll % length(live.routes) != 0, awaySend = length(live.sendRoutes) && state.tiers.send % length(live.sendRoutes) != 0;
    let firsts = [];
    if (awayPoll) push(firsts, live.routes[0]);
    if (awaySend && (!awayPoll || live.sendRoutes[0].name != live.routes[0].name)) push(firsts, live.sendRoutes[0]);
    let away = length(firsts) > 0;
    // An independent getMe is a weak positive signal, never proof of long POLL.
    let route = away ? firsts[state.follower.turn++ % length(firsts)] : probeRoutes[state.follower.index++ % length(probeRoutes)];
    state.follower.last_probe = time();
    // every 10 min when calm, 5 when away: the hold of a proven proxy (FOLLOWER_TTL) is two of the calm periods
    // (audit 6.2: a 30-min period and an 11-min freshness left most of the half hour without the hold)
    // with a little random added (audit 6.3), so a router's probes do not line up with every other's
    let jitter = fs.readfile('/dev/urandom', 1);
    state.follower.next = time() + (state.botUsername && !away ? 600 : 300) + (jitter ? ord(jitter, 0) % 31 : 0);
    let probeId = ++state.follower.runId;
    state.follower.started = time();
    state.follower.task = transportStart('probe', secret, routed(route), 'getMe', {}, (result) => {
        // a uloop.task may call back again with nothing as it ends: that second call erased a good probe (its streak
        // and the hold), so the bot never went back to its first route; the first answer stands
        if (probeId != state.follower.runId) return;
        state.follower.runId++;
        state.follower.task = null;
        state.follower.started = 0;
        if (classify(result, 'probe').state == 'ok') {
            reachedTelegram(route, result, null);
            if (type(result?.data?.result?.username) == 'string')
                state.botUsername = label(result.data.result.username);
            state.follower.good[route.name] = time();
            state.follower.streak[route.name] = min(3, (state.follower.streak[route.name] || 0) + 1);
            // Prefer the configured primary again only after two independent,
            // successful HTTPS probes. This is NOT counted as long-poll success.
            // Send goes back with poll: nothing else ever reset it (a send that failed over stayed there).
            if (state.follower.streak[route.name] >= 2) {
                if (live.routes[0]?.name == route.name) { state.tiers.poll = 0; state.pollFailures = 0; }
                if (live.sendRoutes[0]?.name == route.name) state.tiers.send = 0;
            }
        } else {
            delete state.follower.good[route.name];
            state.follower.streak[route.name] = 0;
        }
    });
}
// a route of the bot's own list, as LuCI shows it: a kind, never a secret (no endpoint with a login, no password)
function routeKind(r) {
    return r.proxy ? 'proxy' : r.resolve && r.source == 'emergency' ? 'emergency' : r.source == 'modem' ? 'modem' : 'direct';
}
function routeList(list, kind) {
    let out = [], tier = length(list) ? state.tiers[kind] % length(list) : -1;
    for (let i = 0; i < length(list); i++) {
        let r = list[i], p = state.probe.results[r.name], e = state.routeErrors[r.name];
        push(out, {name:r.name, title:r.title || null, kind:routeKind(r), source:r.source, auth:!!r.auth, interface:r.bind_interface || null,
                   current:i == tier, failed:e ? {state:e.state, at:e.at} : null, checked:p || null,
                   queued:index(map(state.probe.queue, (q) => q.name), r.name) >= 0 || state.probe.name == r.name});
    }
    return out;
}
// LuCI's «check this route»: getMe through it, by the bot's own resolver and transport (audit R02: the page's shell
// check took other live.routes than the bot). Two answers: Telegram reached, and the Bot API confirmed the token
function probeQueue(kind, name) {
    let lists = kind == 'send' ? [live.sendRoutes] : kind == 'poll' ? [live.routes] : [live.routes, live.sendRoutes], n = 0, seen = {};
    for (let q in state.probe.queue) seen[q.name] = true;
    for (let list in lists)
        for (let r in list)
            if ((!name || r.name == name) && !seen[r.name] && length(state.probe.queue) < 12) {
                push(state.probe.queue, {name:r.name, route:r});
                seen[r.name] = true;
                n++;
            }
    return n;
}
function runProbe() {
    if (state.probe.task || !length(state.probe.queue) || activeTasks() >= 2) return;
    let q = shift(state.probe.queue), id = ++state.probe.runId, started = time();
    state.probe.name = q.name;
    state.probe.started = started;
    state.probe.task = transportStart('check', secret, routed(q.route), 'getMe', {}, (result) => {
        // the task's callback may come again with nothing when it ends: the first answer stands
        if (id != state.probe.runId) return;
        state.probe.runId++;
        state.probe.task = null;
        state.probe.name = null;
        let outcome = classify(result, 'probe');
        let api = result?.data?.ok === true && type(result?.data?.result?.username) == 'string';
        state.probe.results[q.name] = {at:time(), transport_reached:type(result?.data) == 'object',
                                       bot_api_ok:api && outcome.state == 'ok', state:outcome.state,
                                       error:type(result?.error) == 'string' ? substr(result.error, 0, 40) : null,
                                       latency_ms:type(result?.ms) == 'int' ? result.ms : (time() - started) * 1000};
        if (api) reachedTelegram(q.route, result, null);
        if (hooks.probeDone) hooks.probeDone(q.name);
    });
}
function cancelFollower() {
    if (!state.follower.task) return;
    let old = state.follower.task;
    state.follower.runId++; // late task callback has no authority over a new probe
    state.follower.task = null;
    state.follower.started = 0;
    state.follower.next = time() + 30;
    try { old.kill(15); } catch (e) {}
    transportCleanup('probe');
}
function watchdogTasks() {
    let now = time();
    if (state.follower.task && now - state.follower.started >= 18)
        cancelFollower();
    // LuCI's check of a route: curl's 8 s plus a margin, then the slot is free and the route said unchecked
    if (state.probe.task && now - state.probe.started >= 18) {
        let t = state.probe.task;
        state.probe.runId++;
        state.probe.task = null;
        try { if (!t.finished()) t.kill(15); } catch (e) {}
        transportCleanup('check');
        if (state.probe.name) state.probe.results[state.probe.name] = {at:now, transport_reached:false, bot_api_ok:false,
                                                                       state:'task_watchdog', latency_ms:null};
        state.probe.name = null;
    }
    for (let kind in ['poll', 'send']) {
        let meta = state.taskInfo[kind];
        if (!meta || !state.tasks[kind]) continue;
        // Extra deadline is a safety net when a uloop.task child dies without
        // producing an IPC message. The VM does not always call outputCallback.
        let limit = kind == 'poll' ? 75 : 25; // curl max-time plus 10 seconds
        if (now - meta.started < limit) continue;
        let task = state.tasks[kind];
        // Finished task without IPC or an overdue live task: both must free slots.
        let finished = false;
        try { finished = task.finished() == true; } catch (e) {}
        if (!finished) try { task.kill(15); } catch (e) {}
        transportCleanup(kind);
        state.jobId++; // invalidates the callback if the child returns late
        state.taskInfo[kind] = null;
        state.tasks[kind] = null;
        state.errors[kind] = 'task_watchdog';
        state.next[kind] = now + 30;
        if (kind == 'send' && meta.sentId) {
            unsure(meta.sentId); // never replay ambiguous sendMessage
        }
        log.syslog(log.LOG_WARNING, 'transport_%s_watchdog', kind);
    }
}
function callTelegram(kind, method, body) {
    if (state.tasks[kind] || time() < state.next[kind] || activeTasks() >= 2) return;
    let candidates = kind == 'poll' ? live.routes : live.sendRoutes;
    if (!length(candidates)) { state.errors[kind] = 'no_permitted_route'; return; }
    let tier = state.tiers[kind] % length(candidates);
    let route = candidates[tier];
    let sentId = null;
    if (kind == 'send') {
        let index = queueHead();
        if (index < 0) return;
        sentId = state.queue[index].id;
        let call = request(state.queue[index]);
        method = call.method; body = call.body;
        state.queue[index].status = 'inflight';
        // Never transmit if the intent cannot be journaled first.
        if (!persistQueue()) { state.queue[index].status = 'pending'; state.next.send = time() + 30; return; }
    }
    let taskId = ++state.jobId;
    state.taskInfo[kind] = {id:taskId, started:time(), sentId};
    state.tasks[kind] = transportStart(kind, secret, routed(route), method, body, (result) => {
        if (!state.taskInfo[kind] || state.taskInfo[kind].id != taskId) return;
        state.taskInfo[kind] = null;
        state.tasks[kind] = null;
        let outcome = classify(result, kind == 'send' && !CREATES[method] ? 'call' : kind);
        if (outcome.state != 'ok') {
            if (state.errors[kind] != outcome.state)
                log.syslog(log.LOG_WARNING, 'transport_%s_%s_route_%s', kind, outcome.state, label(route.name));
            state.errors[kind] = outcome.state;
            // what failed, where and when (audit R06): the status names it instead of guessing
            if (length(keys(state.routeErrors)) < 16 || state.routeErrors[route.name])
                state.routeErrors[route.name] = {kind, state:outcome.state, at:time()};
            state.attempts[kind]++;
            if (kind == 'poll') {
                if (outcome.state == 'poll_conflict') {
                    state.pollConflicts++;
                    if (!state.pollConflictSince) state.pollConflictSince = time();
                    // More than two consecutive 409 responses indicate another consumer.
                    // Do not rotate routes: wait for an operator or the other poller to stop.
                    if (state.pollConflicts >= 3) state.next.poll = time() + 300;
                } else {
                    state.pollConflicts = 0;
                    state.pollConflictSince = 0;
                }
                if (route.proxy || index(route.name, 'modem:') == 0) {
                    state.proxyPoll = max(10, (state.proxyPoll || 25) - 5);
                    state.proxyPollQuiet = 0;
                }
                if (outcome.demote) {
                    state.pollFailures++;
                    let hold = route.proxy && routeHealthy(state.follower.good, route.name, time(), 1200) && state.pollFailures < 2;
                    if (!hold) {
                        state.tiers.poll = (tier + 1) % length(candidates);
                        state.pollFailures = 0;
                    }
                }
            } else if (outcome.state == 'telegram_rejected') {
                // Telegram refused this one call (a 400: an edit of an old message, an expired button): retrying
                // it would hold every notice behind it for the queue's hour
                dropItem(sentId);
                state.dropped++;
                state.attempts.send = 0;
                state.next.send = time() + 1;
                return;
            } else {
                for (let x in state.queue) if (x.id == sentId) {
                    // no byte of the request left (a proxy that took the connection and stalled): Telegram never got
                    // it, so it goes again by the next route; only a request that left is «maybe delivered»
                    if (outcome.uncertain && !(type(result.uploaded) == 'int' && result.uploaded == 0)) markUnsure(x);
                    else x.status = 'pending';
                    break;
                }
                persistQueue();
                if (outcome.demote) state.tiers.send = (tier + 1) % length(candidates);
            }
            // the next route not tried yet goes in seconds; the backoff once every route has failed
            let moved = state.tiers[kind] % length(candidates) != tier && state.tiers[kind] % length(candidates) != 0;
            let backoff = moved ? 3 : min(300, 5 * (1 << min(6, state.attempts[kind])));
            state.next[kind] = max(state.next[kind], time() + max(outcome.delay, outcome.demote ? backoff : 0));
            return;
        }
        reachedTelegram(route, result, kind);
        delete state.routeErrors[route.name];
        if (state.errors[kind]) log.syslog(log.LOG_INFO, 'transport_%s_recovered', kind);
        state.errors[kind] = null;
        state.success[kind] = time();
        state.attempts[kind] = 0;
        state.next[kind] = time();
        if (kind == 'poll') {
            state.pollFailures = 0; state.pollConflicts = 0; state.pollConflictSince = 0;
            // a send failed by the network and nothing came after it to send: the poll just reached Telegram by a route
            // the sends may use too, so sending goes there, now, and its red state ends (the owner, 29.10: through the
            // proxy the bot «hung» on a send error until the next notice, with up to five minutes of backoff)
            let SEND_NET = {network:1, unreachable:1, tls_failure:1};
            if (SEND_NET[state.errors.send] && !state.tasks.send)
                for (let i = 0; i < length(live.sendRoutes); i++) if (live.sendRoutes[i].name == route.name) {
                    state.tiers.send = i;
                    state.errors.send = null;
                    state.attempts.send = 0;
                    state.next.send = time();
                    log.syslog(log.LOG_INFO, 'transport_send_recovered_by_poll_route_%s', label(route.name));
                    break;
                }
            if (state.proxyPoll && state.proxyPoll < 25 && ++state.proxyPollQuiet >= 20) {
                state.proxyPoll += 5;
                state.proxyPollQuiet = 0;
            }
            for (let up in (type(result.data.result) == 'array' ? result.data.result : [])) {
                if (type(up.update_id) != 'int' || up.update_id < state.offset) continue;
                // Offset must be durable before handling a command. Failing to
                // checkpoint must not execute the same command twice on replay.
                let oldOffset = state.offset;
                state.offset = up.update_id + 1;
                if (!remember()) { state.offset = oldOffset; state.errors.poll = 'offset_persistence'; state.next.poll = time() + 60; break; }
                hooks.onUpdate(up);
            }
        } else dropItem(sentId);
    });
    if (!state.tasks[kind]) {
        state.taskInfo[kind] = null;
        if (kind == 'send') for (let item in state.queue) if (item.id == sentId) item.status = 'pending';
        persistQueue();
        // the worker could not start (uloop.task: no fork or no pipe, the router short of memory or descriptors); the
        // owner, 30.10: «операция не выполнена (task_unavailable)» for half a day. Said once with its errno; after
        // five minutes of it the bot ends with an error and procd starts a fresh one (a full queue stays on disk)
        if (state.errors[kind] != 'task_unavailable') {
            log.syslog(log.LOG_WARNING, 'transport_%s_task_unavailable_%s', kind, '' + (uloop.error() || 'unknown'));
            state.unavailSince = state.unavailSince || time();
        }
        state.errors[kind] = 'task_unavailable';
        state.next[kind] = time() + 15;
        if (state.unavailSince && time() - state.unavailSince >= 300) {
            log.syslog(log.LOG_ERR, 'transport_workers_unavailable_restarting');
            state.exitCode = 75;
            if (hooks.onFatal) hooks.onFatal();	// shutdown(), declared below: assigned after it
        }
    } else if (state.errors.poll != 'task_unavailable' && state.errors.send != 'task_unavailable')
        state.unavailSince = 0;
}
// The long poll's wait on the server. After a failed poll the next one is a quick check (0 s): a route that answers
// TCP and never reaches Telegram fails in seconds, not in a minute (the reference bot's getMe precheck). Through a
// proxy or a modem 25 s: idle timers of proxies and of mobile NAT cut a quiet 50 s connection and the route flapped.
// A proxy that keeps cutting the poll (bench 2026-10-22: opera-proxy, every ~55 s) gets a shorter one by itself, down to
// 10 s, and back up after quiet polls; poll.timeout_seconds below 25 is taken there too.
function pollTimeout() {
    if (state.errors.poll) return 0;
    let route = length(live.routes) ? live.routes[state.tiers.poll % length(live.routes)] : null;
    if (route && (route.proxy || index(route.name, 'modem:') == 0)) {
        let t = int(pollCfg.timeout_seconds || 25), cap = t >= 10 && t < 25 ? t : 25;
        return min(cap, state.proxyPoll || 25);
    }
    let t = int(pollCfg.timeout_seconds || 50);
    return t >= 10 && t <= 50 ? t : 50;
}

export { activeTasks, callTelegram, cancelFollower, pollTimeout, probeQueue, refreshRoutes, routeList, runFollower, runProbe, watchdogTasks };
