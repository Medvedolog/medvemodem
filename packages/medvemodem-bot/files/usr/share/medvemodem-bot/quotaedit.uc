'use strict';
// A SIM's traffic limit from Telegram (the owner, 2026-11-15: «Лимиты трафика невозможно изменять или удалять из бота …
// это было в ТЗ»; the bot spec's quota_write). The same UCI as LuCI's «SIM-карта» form: config sim keyed by the card's
// ICCID, option quota_gb; the engine reads the file again when it changes. The ICCID is read from the engine at the press,
// used for the section and never shown, logged or kept by the bot.
import * as log from 'log';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import { CONTROL, newCursor, state } from './conf.uc';
import { freshen } from './engine.uc';

const T = lang.T;
const PRESETS = [1, 5, 10, 20, 30, 50, 100, 200];

// the label key of the card in the modem now: the service's salted hash of its ICCID (r12), as mgr_quota counts it;
// the ICCID itself is never written
function keyOf(id) {
    let all = null;
    try { all = state.bus ? state.bus.call('medvemodem', 'devices', {}) : null; } catch (e) { all = null; }
    for (let d in (type(all?.devices) == 'array' ? all.devices : [])) {
        if (d?.device_id != id) continue;
        let k = d.sim_label_key;
        return type(k) == 'string' && match(k, /^[0-9a-f]{4}:[0-9a-f]{16}$/) ? k : null;
    }
    return null;
}

// gb: a number of gigabytes, or null to remove the limit; true when written
function write(id, gb) {
    let key = keyOf(id);
    if (!key) return 'no_sim';
    let cur = newCursor(), name = null;
    cur.foreach('medvemodem', 'sim', (s) => { if (s.key == key && !name) name = s['.name']; });
    if (gb == null) {
        if (!name) return 'done';
        cur.delete('medvemodem', name, 'quota_gb');
    } else {
        if (!name) {
            name = cur.add('medvemodem', 'sim');
            if (!name) return 'failed';
            cur.set('medvemodem', name, 'key', key);
        }
        cur.set('medvemodem', name, 'quota_gb', '' + gb);
        let s = cur.get_all('medvemodem', name) || {};
        if (s.quota_day == null) cur.set('medvemodem', name, 'quota_day', '1');
        if (s.quota_warn == null) cur.set('medvemodem', name, 'quota_warn', '80');
        if (s.quota_action == null) cur.set('medvemodem', name, 'quota_action', 'warn');
    }
    return cur.commit('medvemodem') ? 'done' : 'failed';
}

function page(id, note) {
    let d = state.deviceSafe[id], key = ui.shortKey(d), st = ui.stamp(time()), q = d?.quota || {};
    let lines = ['📊 ' + ui.B(T('Traffic limit', 'Лимит трафика')) + ' · ' + ui.esc(ui.shortName(d?.name))];
    push(lines, q.limit_bytes ? ui.quotaWords(q) : T('No limit is set for the SIM in this modem.', 'Для SIM в этом модеме лимит не задан.'));
    if (note) push(lines, '', note);
    push(lines, '', ui.I(T('A limit per month from the 1st (the day, the warning and «backup» are in LuCI: «Modem» → «Settings» → «SIM card»). Another value too.',
                           'Лимит считается с 1-го числа месяца. День начала, порог предупреждения, уход в резерв и любое другое значение — в LuCI: «Модем» → «Настройки» → «SIM-карта».')));
    let rows = [], row = [];
    for (let gb in PRESETS) {
        push(row, {text:gb + T(' GB', ' ГБ'), callback_data:'qs:' + key + ':' + gb + ':' + st});
        if (length(row) == 4) { push(rows, row); row = []; }
    }
    if (q.limit_bytes) push(rows, [{text:T('🗑 Remove the limit', '🗑 Убрать лимит'), callback_data:'qd:' + key + ':' + st}]);
    push(rows, [{text:T('◀️ Traffic limits', '◀️ Лимиты трафика'), callback_data:'v:q'}]);
    return {text:join('\n', lines), markup:{inline_keyboard:rows}};
}

// qm:<key> the editor, qs:<key>:<gb>:<stamp> a limit, qd asks to remove it, qy:<key>:<stamp> removes
function onPress(p, id, answer, edit) {
    if (!CONTROL) { answer(T('Control disabled in LuCI', 'Управление отключено в LuCI')); return; }
    if (p.kind == 'quota_edit') { answer(''); edit(page(id)); return; }
    if (!ui.stampFresh(p.stamp, time())) { answer(T('Button expired', 'Кнопка устарела')); edit(page(id)); return; }
    let d = state.deviceSafe[id], key = ui.shortKey(d);
    if (p.kind == 'quota_drop_ask') {
        answer('');
        edit({text:'❓ ' + ui.B(T('Confirm', 'Подтвердите')) + '\n' +
                   sprintf(T('Remove the traffic limit of the SIM in “%s”? The counted traffic stays.', 'Убрать лимит трафика для SIM в «%s»? Подсчитанный трафик сохранится.'),
                           ui.esc(ui.shortName(d?.name))),
              markup:{inline_keyboard:[[{text:T('✅ Yes', '✅ Да'), callback_data:'qy:' + key + ':' + ui.stamp(time())},
                                        {text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'qm:' + key}]]}});
        return;
    }
    let gb = p.kind == 'quota_set' ? p.gb : null;
    if (p.kind == 'quota_set' && index(PRESETS, gb) < 0) { answer(T('Not a value of the list', 'Выберите значение из списка')); return; }
    let r = write(id, gb);
    log.syslog(log.LOG_INFO, 'control_quota_%s', r);
    state.last.devices = 0;
    freshen();
    if (r == 'done') {
        answer(gb == null ? T('Limit removed', 'Лимит убран') : sprintf(T('Limit: %d GB', 'Лимит: %d ГБ'), gb));
        edit(page(id, gb == null ? '✅ ' + T('The limit is removed.', 'Лимит убран.') :
                                   '✅ ' + sprintf(T('The limit is %d GB a month; the engine takes it at its next count.', 'Лимит — %d ГБ в месяц; служба учтёт его при следующем подсчёте.'), gb)));
    } else {
        answer(T('Not saved', 'Не сохранено'));
        edit(page(id, '❌ ' + (r == 'no_sim' ? T('The modem\'s SIM card is not read yet: the limit belongs to a card.', 'SIM-карта модема ещё не прочитана: лимит привязывается к самой карте.') :
                                              T('The setting was not saved.', 'Настройку сохранить не удалось.'))));
    }
}

export { onPress, page };
