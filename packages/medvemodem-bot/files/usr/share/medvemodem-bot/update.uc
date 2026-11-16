'use strict';
// MedveModem's updates in Telegram (the owner, 2026-11-14: «проверка обновления в боте по аналогии с подкоп_бот»): the
// engine's self-update asks the releases every 12 h in the background (GitHub straight, then the local proxies), the bot
// says once of each new version with its short highlights, and with «allow_control» offers «⬆️ Обновить» behind a
// confirmation; «📊 Состояние» names an available update too. The bot reads only self-update's status (files, quick).
import * as fs from 'fs';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import { CONTROL, DIR, state, testPath } from './conf.uc';
import { enqueue } from './queue.uc';
import { VER, view as draw, statusLine as line } from './updview.uc';

const T = lang.T;
const SELFUP = testPath('MEDVEMODEM_BOT_SELFUP', '/usr/libexec/medvemodem/self-update');
const SEEN = DIR + '/update-seen';
const CHECK_EVERY = 12 * 3600, READ_EVERY = 900;

function read() {
    let p = fs.popen(SELFUP + ' status 2>/dev/null', 'r');
    if (!p) return null;
    let out = p.read(65536);
    p.close();
    let j = null;
    try { j = json(out || ''); } catch (e) { j = null; }
    return type(j) == 'object' && j.ok ? j : null;
}
// every tick: a check in the background every 12 h (the first 5 min after the start), its result read every 15 min,
// a new version said once (the version kept in the bot's state directory)
function tick(now) {
    if (!fs.access(SELFUP, 'x')) return;
    if (state.updCheckAt == null) state.updCheckAt = now - CHECK_EVERY + 300;
    if (now - state.updCheckAt >= CHECK_EVERY) {
        state.updCheckAt = now;
        state.updReadAt = now - READ_EVERY + 120;	/* read two minutes after the check started */
        system(SELFUP + ' check >/dev/null 2>&1 &');
        return;
    }
    if (now - (state.updReadAt || 0) < READ_EVERY) return;
    state.updReadAt = now;
    let st = read();
    state.update = st;
    if (!st?.newer || type(st.latest) != 'string' || !match(st.latest, VER)) return;
    if (trim(fs.readfile(SEEN, 64) || '') == st.latest) return;
    fs.writefile(SEEN, st.latest);
    let page = draw(st, CONTROL);
    enqueue(page.text, 'info', {markup:page.markup, html:true, raw:true});
}

// nv ask, nvy:<stamp> go, nvx back to the notice
function onPress(p, answer, edit) {
    if (!CONTROL) { answer(T('Control from Telegram is off', 'Управление из Telegram выключено')); return; }
    let st = read();
    if (!st?.newer || type(st.latest) != 'string' || !match(st.latest, VER)) {
        answer(T('Already the newest version', 'Уже установлена последняя версия'));
        return;
    }
    if (p.kind == 'update_cancel') { answer(''); edit(draw(st, CONTROL)); return; }
    if (p.kind == 'update_ask') {
        answer('');
        edit({text:'⬆️ ' + ui.B(sprintf(T('Update MedveModem to %s?', 'Обновить MedveModem до %s?'), st.latest)) + '\n' +
                   T('The MedveModem service, its LuCI pages and the bot are replaced, the settings stay. The bot restarts and writes when it is back.',
                     'Служба MedveModem, её страницы LuCI и бот будут заменены, настройки сохранятся. Бот перезапустится и напишет, когда снова будет на связи.'),
              markup:{inline_keyboard:[[{text:T('✅ Yes, update', '✅ Да, обновить'), callback_data:'nvy:' + ui.stamp()},
                                        {text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'nvx'}]]}});
        return;
    }
    if (p.kind == 'update_go') {
        if (!ui.stampFresh(p.stamp, time())) { answer(T('The button is out of date', 'Кнопка устарела')); edit(draw(st, CONTROL)); return; }
        if (st.state == 'running') { answer(T('An update is already running', 'Обновление уже идёт')); return; }
        system(SELFUP + ' start --lang=' + (lang.get() == 'en' ? 'en' : 'ru') + ' >/dev/null 2>&1');
        answer(T('Started', 'Запущено'));
        edit({text:'⬆️ ' + sprintf(T('The update to %s is running: the bot restarts and writes when it is back. The log is in LuCI.',
                                     'Идёт обновление до %s: бот перезапустится и напишет, когда снова будет на связи. Журнал — в LuCI.'), ui.C(st.latest)),
              markup:null});
        return;
    }
    answer('');
}

function statusLine() { return line(state.update); }
function view(st) { return draw(st, CONTROL); }

export { tick, statusLine, onPress, view };
