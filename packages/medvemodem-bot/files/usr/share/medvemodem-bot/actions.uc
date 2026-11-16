'use strict';
// A modem's card and its actions through the engine, and the failover exclusions.
import * as log from 'log';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import { label } from './safe.uc';
import { CONTROL, REFRESH_SECONDS, state } from './conf.uc';
import { freshen, view } from './engine.uc';

// a card with its place among the modems: the arrows go round, the main one is marked
function cardOf(id, result) {
    let ids = state.deviceIds, n = index(ids, id), total = length(ids);
    // the data's own time beside the page's (audit R07: an old green card said «Обновлено» now while the engine was down)
    return ui.card(state.deviceSafe[id], {n:n + 1, total, main:ui.mainId(view()) == id, control:CONTROL, now:time(),
        readAt:state.devicesReadAt, stale:state.engine != 'ok' || time() - (state.devicesReadAt || 0) > 3 * REFRESH_SECONDS,
        prev:ui.shortKey(state.deviceSafe[ids[(n + total - 1) % total]]),
        next:ui.shortKey(state.deviceSafe[ids[(n + 1) % total]]), result});
}
// the engine's answer to an action, in words for the toast and the card
const REFUSAL = {'the modem is frozen by the operator':['the modem\'s management is paused: resume it first',
                                                        'управление модемом приостановлено — сначала возобновите управление'],
                 'the modem has no network interface yet':['the modem has no network interface yet', 'у модема ещё нет сетевого интерфейса'],
                 'a configuration change is being applied':['settings are being applied, try again in a minute',
                                                            'идёт применение настроек, повторите через минуту'],
                 'an eSIM operation holds this modem':['an eSIM operation is running', 'идёт операция с eSIM']};
// the engine's own warnings beside a result (audit R03: a «done» whose live.routes did not move or whose QModem dial kept on
// was told as a plain success)
const WARN = {'netifd did not take the interface down':['the interface did not go down', 'не удалось отключить интерфейс'],
              'netifd did not bring the interface up':['the interface did not come up', 'не удалось включить интерфейс'],
              'QModem\'s helper is busy; its dial was not stopped':['QModem\'s dial was not stopped', 'подключение в QModem не остановлено'],
              'QModem\'s helper is busy; its dial was not started again':['QModem\'s dial was not started again', 'подключение в QModem не возобновлено']};
function actionWarnings(r) {
    let out = [];
    for (let k in ['interface_warning', 'qmodem_warning'])
        if (type(r?.[k]) == 'string') push(out, lang.W(WARN[r[k]]) || label(r[k]));
    if (r?.routes == 'failed')
        push(out, lang.T('the live live.routes did not move: the order is saved for the next connection',
                         'действующие маршруты не изменились; сохранённый порядок применится при следующем подключении'));
    return out;
}
// four outcomes (audit R03): done, started (wait), done with a warning, not done
function actionWords(r) {
    let res = type(r) == 'object' ? r.result || r.status : null, why = type(r) == 'object' ? r.reason || r.why : null;
    if (res == 'done' || res == 'started' || res == 'unchanged') {
        let warn = actionWarnings(r);
        return {ok:true, level:length(warn) ? 'warn' : res == 'started' ? 'started' : 'done', warn,
                text:res == 'started' ? lang.T('started', 'запущено') : lang.T('done', 'готово')};
    }
    let w = lang.W(REFUSAL[why]) || (type(why) == 'string' ? label(why) : null);
    return {ok:false, text:(res == 'busy' ? lang.T('busy', 'занято') : res == 'refused' ? lang.T('refused', 'отказано') :
                            lang.T('not done', 'не выполнено')) + (w ? ': ' + w : '')};
}
const DONE = {mn:['➡️ First among the modems', '➡️ Первый среди модемов'], up:['▶️ Connecting started', '▶️ Подключение запущено'],
              dn:['⏹ Disconnected', '⏹ Отключён'], rc:['🔁 Reconnecting started', '🔁 Переподключение запущено'],
              rr:['📡 Registered in the network again', '📡 Модем заново зарегистрирован в сети'],
              rs:['♻️ The modem restart started', '♻️ Перезагрузка модема запущена'],
              fz:['🧊 Management paused', '🧊 Управление приостановлено'], uf:['☀️ Management resumed', '☀️ Управление возобновлено']};
// one action through the engine; the device_id stays on the router, the button carries its opaque key only
function runAction(op, id) {
    let args, method;
    if (op == 'mn') {
        // «Сделать основным»: this one first, the others keep their order (the dashboard's way)
        let order = [id];
        for (let o in state.deviceIds) if (o != id) push(order, o);
        method = 'priority'; args = {order};
    } else if (op == 'up' || op == 'dn') { method = 'connect'; args = {id, up:op == 'up', who:'Telegram'}; }
    else if (op == 'fz' || op == 'uf') { method = 'freeze'; args = {id, on:op == 'fz', who:'Telegram'}; }
    else if (op == 'rr') { method = 'reregister'; args = {id}; }
    else { method = 'recover'; args = {id, level:op == 'rs' ? 'soft-reset' : 'session', reason:'user_requested', wait:false, source:'telegram'}; }
    let r = null;
    try { r = state.bus.call('medvemodem', method, args); } catch (e) { r = null; }
    // reregister answers {ok, refused|error}: in the words of the others
    if (op == 'rr' && type(r) == 'object')
        r = r.ok === true ? {result:'done'} : {result:r.refused ? 'refused' : 'failed', reason:r.refused || r.error};
    let w = r ? actionWords(r) : {ok:false, text:lang.T('the MedveModem service did not answer', 'служба MedveModem не ответила')};
    log.syslog(log.LOG_INFO, 'control_%s_%s', op, w.ok ? w.level : 'refused');
    return w;
}
// the card's line for an action's outcome: what the engine did, its warnings, and for «first among the modems» where
// the router's traffic goes now (a wired WAN keeps its place: the carrier is said, not promised)
function actionLine(op, id, w) {
    if (!w.ok) return '❌ ' + w.text;
    let line = lang.W(DONE[op]) || lang.T('✅ Done', '✅ Готово');
    if (w.level == 'warn') line = '⚠️ ' + line + ': ' + join('; ', w.warn);
    else if (w.level == 'started') line += lang.T(' · in progress', ' · выполняется');
    if (op == 'mn') {
        let c = state.uplinks?.carrier, d = state.deviceSafe[id];
        if (c && d && c.device && c.device != d.session?.device)
            line += lang.T(' · the router\'s traffic still goes through ', ' · трафик роутера по-прежнему идёт через ') +
                    (label(c.name) || label(c.device));
    }
    return line;
}

// the exclusion menu: a draft over core.failover_manual, ticked on and off without touching the router, applied at
// once against the list it started from (the reference bot's URLTest exclusions)
function excludeCands() {
    let out = [], seen = {};
    for (let p in (state.uplinks?.paths || [])) if (p.device && !seen[p.device]) { seen[p.device] = true; push(out, {device:p.device, name:p.name}); }
    for (let d in (state.uplinks?.manual || [])) if (!seen[d]) { seen[d] = true; push(out, {device:d, name:null}); }
    return out;
}
function draftFresh() {
    if (state.exDraft && time() - state.exDraft.started < 1800) return state.exDraft;
    let base = slice(state.uplinks?.manual || []), sel = {};
    for (let d in base) sel[d] = true;
    return state.exDraft = {base, sel, started:time()};
}
function onExclude(pressed, answer, edit) {
    let cands = excludeCands();
    if (pressed.kind == 'exclude_cancel') { state.exDraft = null; answer(lang.T('Changes dropped', 'Изменения отменены')); edit({text:ui.header(view()), markup:ui.menu()}); return; }
    if (pressed.kind == 'exclude') { if (pressed.page == 0 && !state.exDraft) draftFresh(); answer(''); edit(ui.excludePage(cands, draftFresh(), pressed.page, CONTROL, time())); return; }
    if (!CONTROL) { answer(lang.T('Management is off in LuCI', 'Управление выключено в LuCI')); edit(ui.excludePage(cands, draftFresh(), 0, false, time())); return; }
    let draft = draftFresh();
    if (pressed.kind == 'exclude_tick') {
        let c = null;
        for (let x in cands) if (x.device == pressed.device) { c = x; break; }
        // another draft (applied, dropped, older than 30 min) or a channel gone: the menu is drawn again, nothing ticked
        if (!c || pressed.draft != sprintf('%x', (draft.started || 0) % 0xffffff)) {
            answer(lang.T('The channel list changed', 'Список каналов изменился'));
            edit(ui.excludePage(cands, draft, 0, true, time()));
            return;
        }
        if (draft.sel[c.device]) delete draft.sel[c.device]; else draft.sel[c.device] = true;
        answer(lang.T('The choice is updated', 'Выбор обновлён'));
        edit(ui.excludePage(cands, draft, pressed.page, true, time()));
        return;
    }
    if (!ui.stampFresh(pressed.stamp, time())) { answer(lang.T('The button is out of date', 'Кнопка устарела')); edit(ui.excludePage(cands, draft, 0, true, time())); return; }
    let devices = filter(keys(draft.sel), (d) => draft.sel[d]), r = null;
    try { r = state.bus ? state.bus.call('medvemodem', 'failover_manual', {devices, expect:draft.base}) : null; } catch (e) { r = null; }
    log.syslog(log.LOG_INFO, 'control_exclusions_%s', r?.result == 'done' ? 'done' : 'refused');
    state.exDraft = null;
    state.last.uplink = 0;
    freshen();
    if (r?.result == 'done') answer(lang.T('Exclusions applied: ', 'Исключения применены: ') + length(devices));
    else answer(r?.result == 'changed' ? lang.T('The list was changed in LuCI: the draft is dropped', 'Список изменили в LuCI — черновик сброшен') :
                                         lang.T('Not applied: the MedveModem service did not answer', 'Не применено: служба MedveModem не ответила'));
    edit(ui.excludePage(excludeCands(), draftFresh(), 0, true, time()));
}

export { actionLine, cardOf, onExclude, runAction };
