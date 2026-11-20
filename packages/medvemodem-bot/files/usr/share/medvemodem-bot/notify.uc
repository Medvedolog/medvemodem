'use strict';
// Notifications: incidents (episodes.uc) with what the internet does meanwhile, quotas, failovers, ModemManager's and
// QModem's restarts, the day's log.
import * as fs from 'fs';
import * as log from 'log';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import * as daylog from './daylog.uc';
import { opaque, stepWord } from './safe.uc';
import { update as updateEpisode } from './episodes.uc';
import { DAYLOG_FILE, PROC, key, live, noticeOn, state } from './conf.uc';
import { enqueue } from './queue.uc';

// a modem that does not carry the internet while the internet works through another channel (plan step 4)
function internetUp() {
    let i = state.uplinks.internet;
    return i == 'available' || i == 'up' || i == 'ok';
}
function isBackup(id) {
    let d = id ? state.deviceSafe[id] : null;
    // the main channel of the last 30 min stays the main one for its outage (pulled out, it carries nothing any more)
    if (id && state.carried?.[id] && time() - state.carried[id] < 1800) return false;
    return !!d && d.uplink.role != 'carrying';
}
// an episode waits 90 s before it is told; a backup modem 10 min while the internet works (mode «all»: 90 s for all)
function episodeWait(entry) {
    return live.mode != 'all' && (entry.kind == 'session' || entry.kind == 'present' || entry.kind == 'reach') &&
           entry.id != '-' && isBackup(entry.id) && internetUp() ? 600 : 90;
}
function incidentMessage(action, kind, id, count, level) {
    let d = id ? state.deviceSafe[id] : null;
    let name = d ? d.name : null;	/* null: the router itself */
    // the main channel says its operator and network: «T99 дача (МТС, LTE)»
    if (d && action == 'outage' && !isBackup(id) && length(d.operator)) {
        let tech = ui.techWords(d.access_tech);
        name += ' (' + d.operator + (tech ? ', ' + tech : '') + ')';
    }
    let carrier = state.uplinks.carrier, via = carrier ? (carrier.name || carrier.device) : null;
    if (d && carrier && (carrier.device == d.session.device || carrier.name == d.name)) via = null;	/* not through itself */
    let fix = id ? state.fixed[id] : null;
    let ready = filter(state.uplinks.paths || [], (p) => p.role == 'backup_ready');
    let ctx = {backup:isBackup(id), via, internet:state.uplinks.internet, fixing:!!d && d.recovery == 'running',
               switchOff:state.uplinks.switch_off === true && length(ready) > 0 ? (ready[0].name || ready[0].device) : null,
               fixed:action == 'recovery' && fix && time() - fix.t <= count + 180 ? stepWord(fix.step) : null};
    // the backup modems switched off on the settings page: nothing of them while the internet goes another way
    if (ctx.backup && !noticeOn('backup') && internetUp()) return;
    let text = ui.notice(action, kind, name, count, level, ctx);
    // an alert has a sound when the internet suffered; good news and a backup's flaps come quietly
    let quiet = action == 'recovery' || ((action == 'flapping' || action == 'outage') && ctx.backup && internetUp());
    // «the internet is gone» rings in quiet hours too (events.quiet_urgent)
    let critical = !quiet && action != 'recovery' && (kind == 'reach' || kind == 'engine') && (!id || id == '-');
    if (text) enqueue(text, action == 'recovery' ? 'info' : 'warning', quiet ? {silent:true} : critical ? {critical:true} : null);
}
function report(kind, id, previous, current) {
    if (!state.baseline || live.notices.enabled != '1' || live.mode == 'off') return;
    // the first 2 min after the engine (re)started: every modem comes up again, one summary says it (plan step 3)
    // the restart's quiet holds the messages, not the accounting (audit r101 R01): an outage that begins in it and goes on
    // is told when it ends
    let quietNow = time() < state.quietUntil;
    if (kind == 'present' || kind == 'session' || kind == 'reach' || kind == 'engine') {
        updateEpisode(state.episodes, id || '-', kind, previous, current, time(), live.mode, quietNow ? () => null : incidentMessage);
        return;
    }
    if (kind == 'wlmode') {
        if (quietNow || live.mode == 'off') return;
        enqueue(ui.wlmodeText(current), 'warning', null);
        return;
    }
    if (quietNow && kind == 'failover') return;
    // critical: the internet as a whole and a used-up quota only (plan step 7; audit r101 R02)
    if (live.mode == 'critical' && kind == 'failover') return;
    let msg = null;
    if (kind == 'quota' && !noticeOn('quota')) return;
    if (kind == 'quota') msg = current == 'warn' ? lang.T('Data cap warning', 'Лимит трафика: предупреждение') :
                               current == 'exceeded' ? lang.T('Data cap reached', 'Лимит трафика исчерпан') : null;
    if (kind == 'failover') msg = ui.failoverText(previous, current);
    if (!msg) return;
    if (live.mode == 'critical' && kind == 'quota' && current != 'exceeded') return;
    let fingerprint = opaque(key, kind + ':' + (id || '-') + ':' + current);
    if (state.seen[fingerprint] && time() - state.seen[fingerprint] < 180) return;
    state.seen[fingerprint] = time();
    let name = id && state.deviceSafe[id] ? state.deviceSafe[id].name : null;
    enqueue(name ? msg + ' · ' + name : msg, 'warning', kind == 'quota' && current == 'exceeded' ? {critical:true} : null);
}
// the day's log for «Статус» («За сутки»): written only when something changed
function dayNote(k, v) {
    if (daylog.add(state.daylog, k, v, time())) daylog.save(DAYLOG_FILE, state.daylog);
}
// ModemManager's process by its name: a new pid is a restart, none a stop (the owner, 2026-10-28: «падения/перезапуски
// modemmanager бот собирает, присылает?»); whoever restarted it, the engine or not
function mmPidNow() {
    let scanned = 0;
    for (let pid in (fs.lsdir(PROC) || [])) {
        if (++scanned > 4096) break;
        if (!match(pid, /^[0-9]{1,10}$/)) continue;
        let comm = fs.readfile(PROC + '/' + pid + '/comm', 32);
        if (comm == 'ModemManager\n' || comm == 'ModemManager') return pid;
    }
    return null;
}
function watchModemManager() {
    let pid = mmPidNow() || '', was = state.mmPid;	/* '': not running */
    state.mmPid = pid;
    if (was === null || was == pid) return;	/* the first look, or no change */
    /* an engine that watches it (r125, its status has services.modemmanager) says it, with who restarted it */
    if (state.engine == 'ok' && state.cache.status?.mem?.modemmanager) return;
    let what = pid == '' ? 'stopped' : was == '' ? 'started' : 'restarted';
    dayNote('mm', what);
    log.syslog(log.LOG_INFO, 'modemmanager_%s', what);
    if (!state.baseline || live.notices.enabled != '1' || live.mode == 'off' || !noticeOn('services')) return;
    if (what == 'stopped')
        enqueue('⚠️ ' + lang.T('ModemManager stopped: managed modems unavailable; verifying connections',
                               'ModemManager остановлен: управление через него недоступно, проверяем связь'), 'warning');
    else if (live.mode != 'critical')
        enqueue('🔄 ' + (what == 'started' ? lang.T('ModemManager is running again', 'ModemManager снова работает')
                                          : lang.T('ModemManager restarted', 'ModemManager перезапущен')), 'info', {silent:true});
}
// ModemManager's or QModem's restart or stop as the engine saw it (the owner, 2026-10-30: «в реальном времени присылать
// факт рестарта»): the day's line counts it, a notice says it and who did it
function serviceEvents() {
    while (length(state.svcEvents)) {
        let e = shift(state.svcEvents);
        dayNote(e.name == 'qmodem' ? 'qm' : 'mm', e.what);
        log.syslog(log.LOG_INFO, '%s_%s', e.name, e.what);
        if (!state.baseline || live.notices.enabled != '1' || live.mode == 'off' || !noticeOn('services')) continue;
        if (e.what == 'stopped' && e.by != 'medvemodem') enqueue(ui.serviceNotice(e), 'warning');
        else if (live.mode != 'critical')
            enqueue(ui.serviceNotice(e), 'info', {silent:e.by == 'medvemodem'});
    }
}

// the router's own trouble as the engine read it in the kernel log, once a kind a boot (the owner, 2026-10-31: «HC died и
// failed to set voltage — да обязательно и не только для r3s»): the critical ones ring even in quiet hours, as «no internet»
function kernelEvents() {
    while (length(state.kernEvents)) {
        let e = shift(state.kernEvents);
        log.syslog(log.LOG_WARNING, 'kernel_%s', e.kind);
        if (live.notices.enabled != '1' || live.mode == 'off' || !noticeOn('kernel')) continue;
        if (!e.critical && live.mode == 'critical') continue;
        enqueue(ui.kernelNotice(e), e.critical ? 'warning' : 'info', e.critical ? {critical:true} : {});
    }
}

export { dayNote, episodeWait, incidentMessage, kernelEvents, report, serviceEvents, watchModemManager };
