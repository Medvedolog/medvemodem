'use strict';
// What the admin sends: commands, the keyboard's keys, the buttons, the answers to a settings question.
import * as fs from 'fs';
import * as uloop from 'uloop';
import * as log from 'log';
import * as ui from './ui.uc';
import * as settings from './settings.uc';
import * as lang from './lang.uc';
import { CONTROL, LANG_FILE, LANG_FIXED, chat, hasAdmin, pairingCandidate, pairingFile, pairingOpen, state, uid } from './conf.uc';
import { atomic, enqueue, respond } from './queue.uc';
import { freshen, idByKey, routerInfo, view } from './engine.uc';
import { actionLine, cardOf, onExclude, runAction } from './actions.uc';
import { sendLog, smsInbox } from './sms.uc';
import { netView, onNet } from './net.uc';
import { onSettings, onSettingsText, settingsPage, settingsSend } from './botset.uc';
import * as smssend from './smssend.uc';
import * as ussd from './ussd.uc';
import * as update from './update.uc';
import * as quotaedit from './quotaedit.uc';

// what the admin did, one syslog line each (the owner 2026-11-02): a name from the
// bot's closed lists only, never the text that came with it (an SMS or USSD answer, a PIN, a proxy link). The admin is
// one person, so no name or id of who.
const VIEWS = {m:'menu', s:'status', u:'channels', q:'quota', i:'sms', l:'log', a:'logall', j:'journal'};
const COMMANDS_SEEN = {'/start':true, '/menu':true, '/modem':true, '/status':true, '/modems':true, '/uplinks':true, '/channels':true, '/sms':true,
                       '/network':true, '/quota':true, '/settings':true, '/events':true, '/log':true, '/logall':true, '/help':true,
                       '/send':true, '/ussd':true};
function audit(what) {
    if (type(what) == 'string' && match(what, /^[a-z0-9_]{1,40}$/)) log.syslog(log.LOG_INFO, 'admin_%s', what);
}
function pressName(p) {
    if (!p) return 'button_stale';
    if (p.kind == 'view') return 'view_' + (VIEWS[p.view] || 'journal');
    if (p.kind == 'set') return 'settings_' + (type(p.op) == 'string' ? p.op + (p.op == 'toggle' && type(p.key) == 'string' ? '_' + p.key : '') : p.page);
    if ((p.kind == 'action' || p.kind == 'confirm') && type(p.op) == 'string') return p.kind + '_' + p.op;
    if (p.kind == 'net_wl' && (p.action == 'on' || p.action == 'off')) return 'wlmode_' + p.action;
    return p.kind;
}

function onButton(cq, m) {
    if (type(cq.id) != 'string' || !match(cq.id, /^[A-Za-z0-9_-]{1,64}$/)) return;
    let pressed = settings.parse(cq.data) || ui.parseCallback(cq.data);
    audit(pressName(pressed));
    let answer = (text) => enqueue(text || '', 'info', {method:'answerCallbackQuery', cb:cq.id, urgent:true});
    let edit = (page) => enqueue(page.text, 'info', {method:'editMessageText', edit:m.message_id, markup:page.markup, html:true, urgent:true});
    if (!pressed || type(m.message_id) != 'int' || m.message_id < 1) { answer(lang.T('The button is out of date', 'Кнопка устарела')); return; }
    if (pressed.kind == 'noop') { answer(''); return; }
    freshen();
    if (pressed.kind == 'view') {
        answer('');
        let v = view();
        if (pressed.view == 'm') edit({text:ui.header(v), markup:ui.menu()});
        else if (pressed.view == 's') edit({text:ui.status(v, routerInfo()) + update.statusLine(), markup:ui.back('v:s')});
        else if (pressed.view == 'u') edit({text:ui.uplinks(v), markup:ui.uplinkKeys()});
        else if (pressed.view == 'q') edit({text:ui.quotas(v), markup:ui.quotaKeys(v, CONTROL)});
        else if (pressed.view == 'i') smsInbox(m.message_id);
        else if (pressed.view == 'l') { sendLog(false); edit({text:ui.journal(state.cache.events, time()), markup:ui.journalKeys()}); }
        else if (pressed.view == 'a') { sendLog(true); edit({text:ui.journal(state.cache.events, time()), markup:ui.journalKeys()}); }
        else edit({text:ui.journal(state.cache.events, time()), markup:ui.journalKeys()});
        return;
    }
    if (pressed.kind == 'set') { onSettings(pressed, answer, m.message_id); return; }
    if (index(pressed.kind, 'exclude') == 0) { onExclude(pressed, answer, edit); return; }
    if (index(pressed.kind, 'net') == 0) { onNet(pressed, answer, edit, m.message_id); return; }
    if (index(pressed.kind, 'sms_') == 0) { smssend.onPress(pressed, answer, m.message_id); return; }
    if (index(pressed.kind, 'ussd_') == 0) { ussd.onPress(pressed, answer, m.message_id); return; }
    if (index(pressed.kind, 'update_') == 0) { update.onPress(pressed, answer, edit); return; }
    let id = pressed.key ? idByKey(pressed.key) : null;
    if (pressed.kind == 'list' || !id) {
        answer(pressed.kind != 'list' ? lang.T('Modem not found, the list is refreshed', 'Модем не найден, список обновлён') : '');
        edit(ui.modemList(view(), pressed.page || 0));
        return;
    }
    if (pressed.kind == 'modem') { answer(''); edit(cardOf(id)); return; }
    if (index(pressed.kind, 'quota_') == 0) { quotaedit.onPress(pressed, id, answer, edit); return; }
    // an action: allowed, fresh, asked first when it disrupts, done through the engine
    if (!CONTROL) { answer(lang.T('Management is off in LuCI', 'Управление выключено в LuCI')); edit(cardOf(id)); return; }
    if (!ui.stampFresh(pressed.stamp, time())) { answer(lang.T('The button is out of date, the card is refreshed', 'Кнопка устарела, карточка обновлена')); edit(cardOf(id)); return; }
    if (pressed.kind == 'action' && ui.asks(pressed.op)) { answer(''); edit(ui.confirm(state.deviceSafe[id], pressed.op, time())); return; }
    // answered before the call: an action can take the bot's own route down with it (the reference bot's lesson), so
    // the engine is asked a moment later, once the answer has had its turn in the queue
    answer(lang.T('Doing it…', 'Выполнение…'));
    // the engine is asked once the answer left (audit R10: a fixed 1.5 s did not wait for it, and the action could take
    // the bot's route down before), or after 6 s at the latest: the action never waits on Telegram for ever
    let op = pressed.op, mid = m.message_id, aid = state.lastQueued, asked = time();
    function go() {
        let waiting = false;
        for (let x in state.queue) if (x.id == aid && x.status != 'sent') { waiting = true; break; }
        if (waiting && time() - asked < 6) { uloop.timer(250, go); return; }
        if (!state.bus || !state.deviceSafe[id]) return;
        let w = runAction(op, id);
        state.last.devices = 0; state.last.uplink = 0;
        freshen();
        let page = cardOf(id, actionLine(op, id, w));
        enqueue(page.text, 'info', {method:'editMessageText', edit:mid, markup:page.markup, html:true, urgent:true});
    }
    uloop.timer(250, go);
}
function onUpdate(up) {
    if (type(up) != 'object') return;
    let cq = type(up.callback_query) == 'object' ? up.callback_query : null;
    let m = up.message || (cq || {}).message || {};
    let from = up.message ? up.message.from : (cq || {}).from;
    if (type(from) != 'object' || !m.chat || m.chat.type != 'private') return;
    // Time-limited, LuCI-initiated pairing. No change to UCI and no echo of
    // identity to Telegram; an operator must confirm the candidate in LuCI.
    let pairing = pairingOpen();
    let proposed = type(m.text) == 'string' ? match(m.text, /^\/pair ([A-F0-9]{12})$/) : null;
    if (!hasAdmin && pairing && proposed && proposed[1] == pairing.code &&
        up.message && type(m.date) == 'int' && m.date >= state.startedAt - 60 &&
        type(from.id) == 'int' && type(m.chat.id) == 'int' && from.id > 0 && m.chat.id > 0) {
        let candidate = {user_id:'' + from.id, chat_id:'' + m.chat.id, until:pairing.until};
        atomic(pairingCandidate, sprintf('%J', candidate));
        fs.unlink(pairingFile); // one use, even before LuCI confirmation
        return;
    }
    if (!hasAdmin || '' + from.id != uid || '' + m.chat.id != chat) return;
    // auto: the admin's app language, from this message or press on; the «Меню» commands follow it
    let said = LANG_FIXED ? null : lang.fromTelegram(from.language_code);
    if (said && said != lang.get()) {
        lang.set(said);
        fs.writefile(LANG_FILE, said);
        let queued = false;
        for (let x in state.queue) if (x.method == 'setMyCommands') queued = true;
        if (!queued) enqueue('', 'info', {method:'setMyCommands'});
    }
    if (cq) { onButton(cq, m); return; }
    if (type(m.date) != 'int' || m.date < state.startedAt - 60) return;
    // the answer to a question: a setting (a proxy may come as a long t.me link), an SMS's number or text (1600 bytes), a
    // USSD code or a menu's reply; a command or a key drops the question
    if (state.ask && time() < state.ask.until && type(m.text) == 'string' &&
        length(m.text) <= (state.ask.what == 'sms_text' ? 1600 : 400) && substr(m.text, 0, 1) != '/' && !ui.buttonCommand(m.text)) {
        let what = state.ask.what;
        if (index(what, 'sms_') == 0) { audit('sms_answer'); smssend.onText(m); }
        else if (index(what, 'ussd_') == 0) { audit('ussd_answer'); ussd.onText(m); }
        else { audit('settings_answer'); onSettingsText(m); }
        return;
    }
    state.ask = null;
    if (type(m.text) != 'string' || length(m.text) > 80) return;
    // a key of the persistent keyboard sends its own text; «/status@name_bot» is the command in a menu
    let cmd = ui.buttonCommand(m.text) || split(split(m.text, ' ')[0], '@')[0];
    audit('cmd_' + (COMMANDS_SEEN[cmd] ? substr(cmd, 1) : 'unknown'));
    if (cmd == '/start') {
        freshen();
        respond(ui.header(view()), ui.menuKeyboard());
    } else if (cmd == '/menu') { freshen(); respond(ui.header(view()), ui.menu()); }
    else if (cmd == '/help') respond(ui.help(CONTROL), ui.menuKeyboard());
    else if (cmd == '/status') { freshen(); respond(ui.status(view(), routerInfo()) + update.statusLine()); }
    else if (cmd == '/modems' || cmd == '/modem') {
        freshen();
        let page = ui.modemList(view());
        respond(page.text, page.markup);
    } else if (match(cmd, /^\/modem[0-9]{1,2}$/)) {
        freshen();
        let n = int(substr(cmd, 6)) - 1;
        if (n < 0 || n >= length(state.deviceIds)) { respond(lang.T('No such modem. The list: /modems', 'Такого модема нет. Список: /modems')); return; }
        let page = cardOf(state.deviceIds[n]);
        respond(page.text, page.markup);
    } else if (cmd == '/uplinks' || cmd == '/channels') { freshen(); respond(ui.uplinks(view())); }
    else if (cmd == '/sms') { freshen(); smsInbox(null); }
    else if (cmd == '/send') { freshen(); smssend.start(null, null, null); }
    else if (cmd == '/ussd') { freshen(); ussd.start(null); }
    else if (cmd == '/network') {
        let page = netView();
        enqueue(page.text, 'info', {markup:page.markup, html:true, raw:true, reply:true});
    }
    else if (cmd == '/quota') { freshen(); respond(ui.quotas(view()), ui.quotaKeys(view(), CONTROL)); }
    else if (cmd == '/settings') settingsSend(settingsPage('main'));
    else if (cmd == '/events') {
        // Command reads the projected cache only; no bulk journal polling.
        respond(ui.journal(state.cache.events, time()), ui.journalKeys());
    } else if (cmd == '/log') sendLog(false);
    else if (cmd == '/logall') sendLog(true);
    else respond(lang.T('Not understood. The commands are in the menu beside the input field, or /help.', 'Команда не распознана. Список команд — в меню у поля ввода или /help.'), ui.menuKeyboard());
}

export { onUpdate };
