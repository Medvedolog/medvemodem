'use strict';
// Sending an SMS from Telegram (the owner, 2026-11-12: «Отправку смс сделаешь?»). Only with main.allow_send, off by
// default and apart from showing SMS: the modem, the number (typed or from the address book), the text, then one page
// that shows all three and «📤 Отправить». The engine sends it (ubus medvemodem sms_send): the bot opens no port and
// runs no AT. The draft lives in memory only, for 10 minutes; the confirmation sends it once, a second press of the same
// button sends nothing. The number and the text are never written to the log or the queue file (audit names only).
import * as uloop from 'uloop';
import * as log from 'log';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import { SEND, newCursor, state } from './conf.uc';
import { enqueue } from './queue.uc';
import { idByKey } from './engine.uc';

const DRAFT_S = 600;
const T = lang.T;

function number(v) {
    let n = replace(type(v) == 'string' ? trim(v) : '', /[ ()\-]/g, '');
    return match(n, /^\+?[0-9]{3,20}$/) ? n : null;
}
// the characters of a UTF-8 text, and how many SMS it takes: GSM-7 for plain ASCII, UCS-2 for the rest (Cyrillic)
function chars(t) {
    let n = 0;
    for (let i = 0; i < length(t); i++) if ((ord(t, i) & 0xc0) != 0x80) n++;
    return n;
}
function parts(t) {
    let n = chars(t), wide = false;
    for (let i = 0; i < length(t) && !wide; i++) if (ord(t, i) >= 0x80) wide = true;
    if (wide) return n <= 70 ? 1 : int((n + 66) / 67);
    return n <= 160 ? 1 : int((n + 152) / 153);
}
// the modems that send SMS now: present, not paused, the engine's SMS path open (capabilities.sms.ok)
function senders() {
    let out = [];
    for (let id in state.deviceIds) {
        let d = state.deviceSafe[id];
        if (d?.present && !d.frozen && d.sms_ok) push(out, d);
    }
    return out;
}
// the address book (config medvemodem, contact), the first eight with a number
function contacts() {
    let out = [];
    try {
        newCursor().foreach('medvemodem', 'contact', (s) => {
            let n = number(s.number);
            if (n && type(s.name) == 'string' && length(s.name) && length(out) < 8) push(out, {name:substr(s.name, 0, 40), number:n});
        });
    } catch (e) {}
    return out;
}
function nonce() {
    return sprintf('%06x', (time() * 7919 + (++state.queueNonce) * 104729) & 0xffffff);
}
function cancelRow() {
    return [{text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'sx'}];
}
function off() {
    return {text:'✉️ ' + ui.B(T('Sending SMS', 'Отправка SMS')) + '\n' +
                 T('Sending SMS and USSD from Telegram is off. It is switched on in LuCI, on the bot\'s page: “Send SMS and USSD from Telegram”.',
                   'Отправка SMS и USSD из Telegram выключена. Включается в LuCI, на странице бота: «Разрешить отправку SMS и USSD из Telegram».'),
            markup:ui.back('v:i')};
}
function pickPage(list) {
    let rows = map(list, (d) => [{text:'📶 ' + d.name, callback_data:'sm:' + ui.shortKey(d)}]);
    push(rows, cancelRow());
    return {text:'✍️ ' + ui.B(T('New SMS', 'Новое SMS')) + '\n' + T('From which modem?', 'С какого модема отправить?'),
            markup:{inline_keyboard:rows}};
}
function numberPage(dr) {
    let rows = [], book = dr.book;
    for (let i = 0; i < length(book); i += 2)
        push(rows, map(slice(book, i, i + 2), (c) => ({text:'👤 ' + c.name, callback_data:'sc:' + index(book, c)})));
    push(rows, cancelRow());
    return {text:'✍️ ' + ui.B(T('New SMS', 'Новое SMS')) + ' · 📶 ' + ui.esc(dr.name) + '\n' +
                 T('Send the recipient\'s number in reply (+79001234567 or 89001234567)',
                   'Пришлите в ответ номер получателя (+79001234567 или 89001234567)') +
                 (length(book) ? T(', or pick a contact:', ' или выберите контакт:') : '.'),
            markup:{inline_keyboard:rows}};
}
function textPage(dr) {
    return {text:'✍️ ' + ui.B(T('New SMS', 'Новое SMS')) + ' · 📶 ' + ui.esc(dr.name) + '\n' +
                 T('To: ', 'Кому: ') + ui.B(dr.contact ? dr.contact + ' (' + dr.number + ')' : dr.number) + '\n' +
                 T('Send the text of the message in reply.', 'Пришлите в ответ текст сообщения.'),
            markup:{inline_keyboard:[cancelRow()]}};
}
function confirmPage(dr) {
    let n = parts(dr.text);
    return {text:'✍️ ' + ui.B(T('Check the SMS before sending', 'Проверьте SMS перед отправкой')) + '\n' +
                 '📶 ' + T('Modem: ', 'Модем: ') + ui.esc(dr.name) + '\n' +
                 '👤 ' + T('To: ', 'Кому: ') + ui.B(dr.contact ? dr.contact + ' (' + dr.number + ')' : dr.number) + '\n' +
                 '💬 ' + ui.esc(dr.text) + '\n' +
                 ui.I(n > 1 ? sprintf(T('%d SMS parts', 'частей SMS: %d'), n) : T('one SMS', 'одно SMS')),
            markup:{inline_keyboard:[[{text:T('📤 Send', '📤 Отправить'), callback_data:'sy:' + dr.nonce}], cancelRow()]}};
}
// the engine's words (UBUS-API: sms_send) in the admin's
function failWords(r) {
    if (type(r) != 'object') return T('the MedveModem service did not answer', 'служба MedveModem не ответила');
    if (r.refused) return T('this modem does not send SMS through MedveModem', 'этот модем не отправляет SMS через MedveModem');
    let w = {'not created':T('the modem did not take the message', 'модем не принял сообщение'),
             'created but not sent':T('created, but not sent', 'создано, но не отправлено'),
             'not sent in time':T('the modem did not send it in time', 'модем не успел отправить'),
             'not sent':T('not sent', 'не отправлено'), 'sent in part':T('sent in part', 'отправлено частично')}[r.error];
    return w || T('not sent', 'не отправлено');
}
function send(page, mid) {
    // a press edits its page; a typed answer gets a new one under it. Raw: the number and the text as they are, never
    // in the queue file
    enqueue(page.text, 'info', mid ? {method:'editMessageText', edit:mid, markup:page.markup, html:true, raw:true, urgent:true} :
            {markup:page.markup, html:true, raw:true, reply:true});
}
function draftFor(d) {
    return {nonce:nonce(), id:null, key:ui.shortKey(d), name:d.name, number:null, contact:null, text:null,
            book:contacts(), until:time() + DRAFT_S, sent:false};
}
function askNumber(dr, mid) {
    state.smsDraft = dr;
    state.ask = {what:'sms_number', until:dr.until};
    send(numberPage(dr), mid);
}
function askText(dr, mid) {
    state.ask = {what:'sms_text', until:dr.until};
    send(textPage(dr), mid);
}

// the way in: «✍️ Написать SMS» (no modem yet), «↩️ Ответить» (the modem and the number of the message answered)
function start(mid, key, to) {
    state.ask = null;
    state.smsDraft = null;
    if (!SEND) { send(off(), mid); return; }
    let list = senders(), d = key ? state.deviceSafe[idByKey(key)] : null;
    if (d && !(d.present && !d.frozen && d.sms_ok)) d = null;
    if (!d && length(list) == 1) d = list[0];
    if (!d) {
        if (!length(list)) {
            send({text:'✍️ ' + ui.B(T('New SMS', 'Новое SMS')) + '\n' +
                       T('No modem can send SMS now: none is connected, or its SMS are not available to MedveModem.',
                         'Сейчас ни один модем не может отправить SMS: модемы не подключены или их SMS недоступны MedveModem.'),
                  markup:ui.back('v:i')}, mid);
            return;
        }
        state.smsDraft = {nonce:nonce(), until:time() + DRAFT_S, pick:true, sent:false};
        send(pickPage(list), mid);
        return;
    }
    let dr = draftFor(d), n = number(to);
    if (n) {
        dr.number = n;
        for (let c in dr.book) if (c.number == n) dr.contact = c.name;
        state.smsDraft = dr;
        askText(dr, mid);
        return;
    }
    askNumber(dr, mid);
}

// a press: sm:<key> the modem, sc:<i> a contact, sy:<nonce> send, sx cancel
function onPress(p, answer, mid) {
    let dr = state.smsDraft;
    if (!SEND) { answer(''); send(off(), mid); return; }
    if (p.kind == 'sms_start') { answer(''); start(mid, null, null); return; }
    if (p.kind == 'sms_reply') { answer(''); start(null, p.key, p.number); return; }
    if (p.kind == 'sms_cancel') {
        state.smsDraft = null; state.ask = null;
        answer(T('Cancelled', 'Отменено'));
        send({text:'✍️ ' + T('The SMS is not sent: cancelled.', 'SMS не отправлено: отменено.'), markup:ui.back('v:i')}, mid);
        return;
    }
    if (!dr || time() > dr.until) {
        state.smsDraft = null; state.ask = null;
        answer(T('The draft is out of date', 'Черновик устарел'));
        send({text:'✍️ ' + T('The draft is out of date. Start again: /send', 'Черновик устарел. Начните заново: /send'), markup:ui.back('v:i')}, mid);
        return;
    }
    if (p.kind == 'sms_modem') {
        let d = state.deviceSafe[idByKey(p.key)];
        if (!d || !d.present || d.frozen || !d.sms_ok) { answer(T('The modem cannot send SMS now', 'Модем сейчас не может отправить SMS')); start(mid, null, null); return; }
        answer('');
        askNumber(draftFor(d), mid);
        return;
    }
    if (p.kind == 'sms_contact') {
        let c = dr.book?.[p.index];
        if (!c || dr.pick) { answer(T('The button is out of date', 'Кнопка устарела')); return; }
        answer('');
        dr.number = c.number; dr.contact = c.name;
        askText(dr, mid);
        return;
    }
    if (p.kind != 'sms_go') { answer(''); return; }
    // once: the draft of this very page, not yet sent
    if (p.nonce != dr.nonce || !dr.text || !dr.number) { answer(T('The button is out of date', 'Кнопка устарела')); return; }
    if (dr.sent) { answer(T('Already sent', 'Уже отправлено')); return; }
    dr.sent = true;
    state.ask = null;
    let id = idByKey(dr.key);
    if (!id || !state.bus) { answer(T('The modem is not found', 'Модем не найден')); dr.sent = false; return; }
    answer(T('Sending…', 'Отправка…'));
    send({text:'✍️ ' + T('Sending the SMS…', 'Отправка SMS…'), markup:null}, mid);
    // the engine is asked once the answer has had its turn in the queue (as an action from the card)
    uloop.timer(250, () => {
        let r = null;
        try { r = state.bus.call('medvemodem', 'sms_send', {id, number:dr.number, text:dr.text}); } catch (e) { r = null; }
        let ok = type(r) == 'object' && r.ok == true;
        log.syslog(ok ? log.LOG_INFO : log.LOG_WARNING, ok ? 'sms_sent' : 'sms_send_failed');
        let who = dr.contact ? dr.contact + ' (' + dr.number + ')' : dr.number;
        let text = ok ? '✅ ' + ui.B(T('SMS sent', 'SMS отправлено')) + ' · 📶 ' + ui.esc(dr.name) + T(' → ', ' → ') + ui.esc(who) +
                        (type(r.parts) == 'int' && r.parts > 1 ? sprintf(T(' · %d parts', ' · частей: %d'), r.parts) : '')
                      : '⚠️ ' + ui.B(T('SMS not sent', 'SMS не отправлено')) + ': ' + ui.esc(failWords(r)) +
                        (type(r?.detail) == 'string' ? '\n' + ui.I(ui.esc(substr(r.detail, 0, 200))) : '');
        state.smsDraft = null;
        send({text, markup:{inline_keyboard:[[{text:T('✍️ One more', '✍️ Написать ещё'), callback_data:'ss'},
                                              {text:T('✉️ Incoming', '✉️ Входящие'), callback_data:'v:i'}]]}}, mid);
    });
}

// a typed answer: the number, then the text
function onText(m) {
    let ask = state.ask, dr = state.smsDraft;
    state.ask = null;
    if (!SEND || !dr || time() > dr.until || dr.pick) { state.smsDraft = null; return; }
    if (ask.what == 'sms_number') {
        let n = number(m.text);
        if (!n) { state.ask = ask; send({text:'⚠️ ' + T('Not a number: send it as +79001234567 or 89001234567.', 'Это не номер: пришлите его в виде +79001234567 или 89001234567.'),
                                             markup:{inline_keyboard:[cancelRow()]}}, null); return; }
        dr.number = n;
        for (let c in dr.book) if (c.number == n) dr.contact = c.name;
        askText(dr, null);
        return;
    }
    let t = trim(m.text);
    if (!length(t) || length(t) > 1600) {
        state.ask = ask;
        send({text:'⚠️ ' + T('The text is empty or longer than 1600 bytes (about 10 SMS).', 'Текст пустой или длиннее 1600 байт (около 10 SMS).'),
              markup:{inline_keyboard:[cancelRow()]}}, null);
        return;
    }
    dr.text = t;
    dr.nonce = nonce();
    send(confirmPage(dr), null);
}

export { onPress, onText, start };
