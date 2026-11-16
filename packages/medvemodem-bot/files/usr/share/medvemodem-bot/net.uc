'use strict';
// «🌍 Сеть»: the addresses, the public ones, the tunnel and the service check through one link.
import * as fs from 'fs';
import * as uloop from 'uloop';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import { CONTROL, state, testPath } from './conf.uc';
import { enqueue } from './queue.uc';

// «🌍 Сеть» (the owner, 2026-10-28): the addresses, the public ones, the
// tunnel and the service check through one link. The helpers run one job at a time, in a task
// off the loop; its result edits the same message. The public addresses and the checks are asked only on a press.
const NETINFO = testPath('MEDVEMODEM_BOT_NETINFO', '/usr/libexec/medvemodem-bot/netinfo');
const NETCHECK = testPath('MEDVEMODEM_BOT_NETCHECK', '/usr/libexec/medvemodem/netcheck');
function runJson(cmd) {
    let p = fs.popen(cmd, 'r');
    if (!p) return null;
    let out = p.read(131072);
    p.close();
    try { let j = json(out || ''); return type(j) == 'object' ? j : null; } catch (e) { return null; }
}
// the addresses; a read that failed (a helper cut short as a check ends) keeps the last good one for a minute, rather
// than a page that says there is no link at all
function netAddr() {
    let a = runJson(NETINFO + ' addr');
    if (type(a?.addrs) == 'object') { state.netAddrLast = {at:time(), a}; return a; }
    return state.netAddrLast && time() - state.netAddrLast.at < 60 ? state.netAddrLast.a : {};
}
// the links a check can go through: each connected modem by its network device, and the default route's
// each with its role from the engine (main, backup N by the order of the routes) and the delay of its last check
// (the owner, 2026-11-14: «логично писать метрики и задержки»); nothing is checked for the page itself
function netLinks(addr) {
    let out = [], seen = {}, role = {}, rank = {}, backups = [];
    for (let p in (type(state.uplinks?.paths) == 'array' ? state.uplinks.paths : [])) {
        if (type(p?.device) != 'string') continue;
        role[p.device] = p.role;
        if (index(p.role || '', 'backup_') == 0) push(backups, p);
    }
    backups = sort(backups, (a, b) => (a.metric ?? 1000000) - (b.metric ?? 1000000));
    for (let i = 0; i < length(backups); i++) rank[backups[i].device] = i + 1;
    for (let id in state.deviceIds) {
        let d = state.deviceSafe[id];
        if (!d || d.session.state != 'connected' || !match(d.session.device || '', /^[A-Za-z0-9._@-]{1,15}$/) || seen[d.session.device]) continue;
        seen[d.session.device] = true;
        let dev = d.session.device;
        push(out, {key:ui.shortKey(d), name:d.name, device:dev, addr:addr.addrs?.[dev], role:role[dev] ?? d.uplink?.role, rank:rank[dev],
                   reach:d.uplink?.reach, ms:d.uplink?.reach == 'available' ? d.uplink?.latency_ms : null});
    }
    let dd = addr['default']?.device;
    if (type(dd) == 'string' && match(dd, /^[A-Za-z0-9._@-]{1,15}$/))
        push(out, {key:'main', name:null, device:dd, addr:addr['default'].src || addr.addrs?.[dd], main:true, modem:!!seen[dd]});
    return out;
}
function netView(note) {
    let addr = netAddr(), nb = null;
    // the engine's last look at other modem helpers (its neighbours helper, every 10 min)
    try { nb = state.bus ? state.bus.call('medvemodem', 'neighbours', {}) : null; } catch (e) { nb = null; }
    return ui.netPage({links:netLinks(addr), cache:state.net, note, now:time(), busy:!!state.netJob, neighbours:nb,
                       wlmode:state.uplinks?.wlmode, control:CONTROL});
}
// «🚧 Белые списки»: the engine's channels and whitelist mode, the blocking test's last answer
function wlView(note) {
    // a channel by the modem's name where the engine gave its device only (eth1 is the E3372: the owner, 2026-11-15)
    let paths = [];
    for (let p in (type(state.uplinks?.paths) == 'array' ? state.uplinks.paths : [])) {
        let q = {};
        for (let k, v in p) q[k] = v;
        if (!q.name) for (let id in state.deviceIds) {
            let d = state.deviceSafe[id];
            if (d?.name && p.device && d.session?.device == p.device) { q.name = d.name; break; }
        }
        push(paths, q);
    }
    return ui.wlPage({paths, tunnel:state.uplinks?.tunnel, wlmode:state.uplinks?.wlmode, control:CONTROL,
                      cache:state.net, note, now:time(), busy:!!state.netJob});
}
// raw: the page carries IP addresses, which message()'s phone-number rule would hide; every value on it is an
// address checked by the helper, a number, or label()ed
function netEdit(mid, page) {
    enqueue(page.text, 'info', {method:'editMessageText', edit:mid, markup:page.markup, html:true, raw:true, urgent:true});
}
// one job: kind 'ip', 'tunnel' or 'svc' (with the link); the command line is built from checked names only
// the same as transport.uc's: an input callback so the pinned ucode closes the task's pipes (an fd per press otherwise)
function noInput() { return null; }
function netJob(kind, link, mid) {
    if (state.netJob) return false;
    let cmd = null;
    if (kind == 'ip') {
        let devs = ['-'];
        for (let l in netLinks(netAddr())) if (!l.main) push(devs, l.device);
        cmd = NETINFO + ' public ' + join(' ', slice(devs, 0, 6));
    } else if (kind == 'tunnel') cmd = NETINFO + ' tunnel';
    else if (kind == 'blocks') cmd = 'blocks';
    else if (kind == 'svc' && link && match(link.device, /^[A-Za-z0-9._@-]{1,15}$/)) cmd = NETCHECK + ' ' + link.device;
    if (!cmd) return false;
    // the blocking test: every connected modem, one after another (netcheck runs one at a time on the router), each
    // straight through its own device; the router's routes are not touched
    let modems = [];
    if (kind == 'blocks') for (let l in netLinks(netAddr())) if (!l.main) push(modems, l);
    modems = slice(modems, 0, 8);
    let job = {kind, link, started:time(), id:++state.jobId, limit:kind == 'blocks' ? 45 * max(1, length(modems)) + 15 : 45};
    state.netJob = job;
    // bounded by busybox timeout too: a killed task left its helper and curl running (audit 13.4)
    job.task = uloop.task((pipe) => {
        if (kind != 'blocks') { pipe.send({r:runJson('timeout 40 ' + cmd + ' 2>/dev/null')}); return; }
        let rows = [];
        for (let l in modems) {
            let r = runJson('timeout 40 ' + NETCHECK + ' ' + l.device + ' 2>/dev/null');
            push(rows, {name:l.name, device:l.device, results:type(r?.results) == 'array' ? slice(r.results, 0, 24) : null, error:r?.error});
        }
        pipe.send({rows});
    }, (msg) => {
        if (state.netJob?.id != job.id) return;
        state.netJob = null;
        let r = msg?.r, at = time();
        if (kind == 'ip') state.net.ip = {at, public:type(r?.public) == 'object' ? r.public : {}};
        else if (kind == 'tunnel') state.net.tunnel = r ? {at, r} : {at, r:{}};
        else if (kind == 'blocks') state.net.blocks = {at, rows:type(msg?.rows) == 'array' ? msg.rows : []};
        else state.net.svc = {at, link, results:type(r?.results) == 'array' ? slice(r.results, 0, 24) : null, error:r?.error};
        netEdit(mid, kind == 'blocks' ? wlView() : netView());
    }, noInput);
    if (!job.task) { state.netJob = null; return false; }
    return true;
}
function netWatchdog() {
    if (!state.netJob || time() - state.netJob.started < (state.netJob.limit || 45)) return;
    try { state.netJob.task.kill(15); } catch (e) {}
    state.netJob = null;
}
function onNet(pressed, answer, edit, mid) {
    edit = (page) => netEdit(mid, page);
    if (pressed.kind == 'net') {
        answer('');
        // the tunnel's answer older than 10 min is asked again (the owner, 2026-11-14: «Проверено в 22:08» at 22:25)
        if (state.net.tunnel && time() - (state.net.tunnel.at || 0) > 600) netJob('tunnel', null, mid);
        edit(netView());
        return;
    }
    if (pressed.kind == 'net_wlpage') { answer(''); edit(wlView()); return; }
    if (pressed.kind == 'net_svcpick') { answer(''); edit(ui.svcPick(netLinks(netAddr()))); return; }
    if (pressed.kind == 'net_blocks_ask') {
        let n = 0;
        for (let l in netLinks(netAddr())) if (!l.main) n++;
        answer('');
        edit(n ? ui.blocksConfirm(n) : wlView(lang.T('No connected modem to check.', 'Нет подключённых модемов для проверки.')));
        return;
    }
    // spec §44: the admin's word on the whitelist mode, with management allowed only; the engine does the rest
    if (pressed.kind == 'net_wl') {
        if (!CONTROL) { answer(lang.T('Management is off in LuCI', 'Управление выключено в LuCI')); edit(wlView()); return; }
        let r = null;
        try { r = state.bus ? state.bus.call('medvemodem', 'wlmode', {action:pressed.action}) : null; } catch (e) { r = null; }
        answer(r?.result == 'started' ? (pressed.action == 'on' ? lang.T('Turning on…', 'Включение…') : lang.T('Turning off…', 'Выключение…')) :
               r?.result == 'busy' ? lang.T('Busy, try again in a minute', 'Занято, попробуйте через минуту') :
               lang.T('Not done', 'Не выполнено'));
        edit(wlView());
        return;
    }
    let link = null;
    if (pressed.kind == 'net_svc') {
        for (let l in netLinks(netAddr())) if (l.key == pressed.link) link = l;
        if (!link) { answer(lang.T('The link is gone, the page is refreshed', 'Канала уже нет, страница обновлена')); edit(netView()); return; }
    }
    let kind = pressed.kind == 'net_ip' ? 'ip' : pressed.kind == 'net_tunnel' ? 'tunnel' : pressed.kind == 'net_blocks' ? 'blocks' : 'svc';
    if (!netJob(kind, link, mid)) {
        answer(lang.T('A check is already running', 'Проверка уже идёт'));
        return;
    }
    answer(lang.T('Checking…', 'Проверка…'));
    edit(kind == 'blocks' ? wlView() : netView());
}

export { netView, netWatchdog, onNet, wlView };
