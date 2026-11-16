'use strict';
import * as fs from 'fs';

// The last day of the router's internet and services, for «Статус» (the owner, 2026-10-28: «в статусе логично писать
// за сутки хотя бы»). Only enums and times: the router's internet (available, limited, unavailable), the MedveModem
// service (down, back, restarted), ModemManager and QModem (stopped, started, restarted). In the bot's state directory (tmpfs):
// a bot restart keeps it, a router reboot starts it again, and the page says since when it counts.
const DAY = 86400;
// and the modems (the owner, 2026-11-16: «надо учитывать» the ones pulled out): one gone from USB, the main channel moved
const KINDS = {net:true, svc:true, mm:true, qm:true, usb:true, main:true};

function fresh(now) { return {since:now, items:[]}; }

function load(path, now) {
    let log = null;
    try { log = json(fs.readfile(path, 65536) || 'null'); } catch (e) { log = null; }
    if (type(log) != 'object' || type(log.since) != 'int' || type(log.items) != 'array' || log.since > now) return fresh(now);
    let items = [];
    for (let x in log.items)
        if (type(x) == 'object' && KINDS[x.k] && type(x.t) == 'int' && type(x.v) == 'string' && match(x.v, /^[a-z_]{1,16}$/))
            push(items, {k:x.k, t:x.t, v:x.v});
    log.items = items;
    return log;
}

function save(path, log) {
    let tmp = path + '.tmp';
    if (fs.writefile(tmp, sprintf('%J', log)) == null) return false;
    return fs.rename(tmp, path);
}

// the items of the last day; the router's internet keeps the one state it was in when the day began
function prune(log, now) {
    let from = now - DAY, items = [], before = null;
    for (let x in log.items) {
        if (x.t >= from) push(items, x);
        else if (x.k == 'net') before = x;
    }
    if (before) unshift(items, {k:'net', t:from, v:before.v});
    log.items = items;
    if (log.since < from) log.since = from;
}

function add(log, k, v, now) {
    if (!KINDS[k] || type(v) != 'string') return false;
    // the same state again is no change (the first read after a restart of the bot)
    if (k == 'net')
        for (let i = length(log.items) - 1; i >= 0; i--)
            if (log.items[i].k == 'net') { if (log.items[i].v == v) return false; break; }
    push(log.items, {k, t:now, v});
    prune(log, now);
    // a bound on the items, not only on their age (audit R04: a flapping link wrote the whole array each change and the
    // 64 KiB read at the start lost the rest): the oldest go, the state the window began in is kept by prune()
    while (length(log.items) > 400) splice(log.items, 1, 1);
    return true;
}

// {since, down:{count, sec}, limited:{count, sec}, svc:{restarts, down}, mm:{restarts, down}, qm:{restarts, down}}
function summary(log, now) {
    let s = {since:log.since, down:{count:0, sec:0}, limited:{count:0, sec:0}, svc:{restarts:0, down:0}, mm:{restarts:0, down:0},
             qm:{restarts:0, down:0}, usb:{gone:0}, main:{switches:0}};
    // the router's internet as it was when the window began: the last state before it, cut at the window's start
    // (audit R04: «limited» for 25 h with no change read «без сбоев», its one item was older than the day)
    let net = [], anchor = null;
    for (let x in log.items) {
        if (x.t < now - DAY) { if (x.k == 'net') anchor = x; continue; }
        if (x.k == 'net') push(net, x);
        else if (x.k == 'svc') { if (x.v == 'restarted') s.svc.restarts++; else if (x.v == 'down') s.svc.down++; }
        else if (x.k == 'mm') { if (x.v == 'restarted' || x.v == 'started') s.mm.restarts++; else if (x.v == 'stopped') s.mm.down++; }
        else if (x.k == 'usb') { if (x.v == 'gone') s.usb.gone++; }
        else if (x.k == 'main') { if (x.v == 'switch') s.main.switches++; }
        else if (x.k == 'qm') { if (x.v == 'restarted' || x.v == 'started') s.qm.restarts++; else if (x.v == 'stopped') s.qm.down++; }
    }
    if (anchor && (!length(net) || net[0].t > now - DAY)) unshift(net, {k:'net', t:now - DAY, v:anchor.v, held:true});
    for (let i = 0; i < length(net); i++) {
        let x = net[i], end = i + 1 < length(net) ? net[i + 1].t : now;
        let from = x.t < now - DAY ? now - DAY : x.t, sec = end > from ? end - from : 0;
        let into = !x.held && (i > 0 || x.t > log.since);	/* a state the day began in is not counted as an onset */
        if (x.v == 'unavailable') { s.down.sec += sec; if (into) s.down.count++; }
        else if (x.v == 'limited') { s.limited.sec += sec; if (into) s.limited.count++; }
    }
    return s;
}

// ucode as pinned by OpenWrt 24.10 and 25.12 does not parse `export function` declarations
export { load, save, add, summary };
