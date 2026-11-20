'use strict';
// Incoming SMS (shown only when LuCI allows), each new one forwarded when asked, the modems' log as a file.
import * as fs from 'fs';
import * as log from 'log';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import { message } from './safe.uc';
import { DIR, SEND, SMS_ALLOWED, newCursor, state } from './conf.uc';
import { enqueue, ownNumbers, respond } from './queue.uc';

// the sender's name from the address book (config medvemodem, sections contact: name, number), read afresh each time:
// the owner, 2026-10-30, «если в записной есть человек, то пересылка в бот пусть будет с номером И с именем». Numbers
// match by their last 10 digits (+7 916…, 8 916… and 916… are one)
function digits10(v) {
    let d = replace(type(v) == 'string' ? v : '', /[^0-9]/g, '');
    return length(d) > 10 ? substr(d, length(d) - 10) : d;
}
function contactNames() {
    let out = {};
    try {
        let c = newCursor();
        c.foreach('medvemodem', 'contact', (s) => {
            let k = digits10(s.number);
            if (length(k) >= 5 && type(s.name) == 'string' && length(s.name) && !out[k]) out[k] = substr(s.name, 0, 40);
        });
    } catch (e) {}
    return out;
}
function forwardSms() {
    while (length(state.smsNew) && state.bus) {
        let ev = shift(state.smsNew), d = state.deviceSafe[ev.id], r = null;
        try { r = state.bus.call('medvemodem', 'sms_list', {id:ev.id, peek:true}); } catch (e) { r = null; }
        if (type(r?.messages) != 'array') continue;
        let names = contactNames();
        for (let x in r.messages) {
            if (type(x) != 'object' || index(ev.ids, x.id) < 0 || x.type == 'submit' || x.state != 'received') continue;
            let it = {d:d || {name:lang.T('modem', 'модем')}, number:type(x.number) == 'string' ? x.number : null,
                      contact:type(x.number) == 'string' ? names[digits10(x.number)] : null,
                      text:type(x.text) == 'string' ? x.text : x.binary ? lang.T('(binary message)', '(двоичное сообщение)') : null,
                      epoch:type(x.epoch) == 'int' ? x.epoch : time()};
            let code = ui.smsCode(it.text), keys = [];
            if (code) push(keys, {text:lang.T('📋 Code ', '📋 Код ') + code, copy_text:{text:code}});
            // «↩️ Ответить» from the modem that got it (r184), when sending is allowed and the sender is a number
            if (SEND && d && type(it.number) == 'string' && match(it.number, /^\+?[0-9]{3,20}$/))
                push(keys, {text:lang.T('↩️ Reply', '↩️ Ответить'), callback_data:'sr:' + ui.shortKey(d) + ':' + it.number});
            // queued is not «forwarded»: a page the queue refused is written down as such (audit R05)
            let queued = enqueue(ui.smsNew(it), 'info', {html:true, raw:true, markup:length(keys) ? {inline_keyboard:[keys]} : null});
            if (!queued) state.renderFailures = (state.renderFailures || 0) + 1;
            log.syslog(queued ? log.LOG_INFO : log.LOG_WARNING, queued ? 'sms_queued' : 'sms_forward_not_queued');
        }
    }
}
function smsInbox(mid) {
    let items = [], note = null, names = contactNames();
    if (SMS_ALLOWED && state.bus)
        for (let id in state.deviceIds) {
            let d = state.deviceSafe[id];
            if (!d?.present) continue;
            let r = null;
            try { r = state.bus.call('medvemodem', 'sms_list', {id, peek:true}); } catch (e) { r = null; }
            if (type(r?.messages) != 'array') {
                if (r?.reason || r?.why) note = lang.T('Still loading messages — tap "Refresh" in a minute.',
                                                       'Сообщения ещё читаются, нажмите «Обновить» через минуту.');
                continue;
            }
            for (let x in r.messages)
                if (type(x) == 'object' && x.type != 'submit' && x.state != 'sent' && x.state != 'sending' && x.state != 'failed')
                    push(items, {d, number:type(x.number) == 'string' ? x.number : null,
                                 contact:type(x.number) == 'string' ? names[digits10(x.number)] : null, text:type(x.text) == 'string' ? x.text :
                                 x.binary ? lang.T('(binary message)', '(двоичное сообщение)') : null, epoch:type(x.epoch) == 'int' ? x.epoch : 0});
        }
    items = sort(items, (a, b) => b.epoch - a.epoch);
    items = slice(items, 0, 5);
    // a code in a message: a button that copies it (Bot API copy_text), the newest three
    let markup = ui.back('v:i'), codes = [];
    if (SMS_ALLOWED)
        for (let it in items)
            if (ui.smsCode(it.text) && length(codes) < 3 && index(codes, ui.smsCode(it.text)) < 0) push(codes, ui.smsCode(it.text));
    if (length(codes))
        markup = {inline_keyboard:[map(codes, (c) => ({text:lang.T('📋 Code ', '📋 Код ') + c, copy_text:{text:c}})), ...markup.inline_keyboard]};
    // writing one and a USSD request, when sending is allowed (r184)
    if (SEND)
        markup = {inline_keyboard:[[{text:lang.T('✍️ Write an SMS', '✍️ Написать SMS'), callback_data:'ss'}, {text:'📞 USSD', callback_data:'us'}],
                                   ...markup.inline_keyboard]};
    // a press edits its message; the /sms command sends a new one
    enqueue(ui.smsPage(items, SMS_ALLOWED, note), 'info', mid ? {method:'editMessageText', edit:mid, markup, html:true, raw:SMS_ALLOWED, urgent:true} :
            {markup, html:true, raw:SMS_ALLOWED, reply:true});
}

// the log as a file (the reference bot's «Файлы и журнал»): the system log's lines of the modems' world, the last
// 1000, with numbers, long hex and links hidden as in every message; sent as a document, the file removed after
const LOG_WORDS = /medvemodem|netifd|ModemManager|qmodem|modem|usb|rndis|cdc_|qmi|mbim|wwan|dnsmasq|odhcp|udhcpc/;
// the modems' lines (all: false) or the router's whole log (all: true, the owner 2026-11-02: «загрузить вообще весь лог
// роутера»), each line through the same filter as any text the bot sends: numbers but the SIM's own, long hex
// identifiers, links of any scheme and bot tokens hidden. logd answers in one ubus message: a large log_size can pass
// its bound, so fewer lines are asked then
function sendLog(all) {
    let r = null, lines = [];
    for (let n in (all ? [20000, 5000, 1000] : [5000, 1000])) {
        try { r = state.bus ? state.bus.call('log', 'read', {lines:n, stream:false, oneshot:true}) : null; } catch (e) { r = null; }
        if (type(r?.log) == 'array') break;
    }
    for (let e in (type(r?.log) == 'array' ? r.log : [])) {
        if (type(e?.msg) != 'string' || (!all && !match(e.msg, LOG_WORDS))) continue;
        let t = type(e.time) == 'int' ? localtime(int(e.time / 1000)) : null;
        push(lines, (t ? sprintf('%02d.%02d %02d:%02d:%02d ', t.mday, t.mon, t.hour, t.min, t.sec) : '') + message(e.msg, ownNumbers()));
    }
    let most = all ? 20000 : 1000;
    if (length(lines) > most) lines = slice(lines, length(lines) - most);
    if (!length(lines)) { respond(lang.T('Log is empty or inaccessible.', 'Журнал пуст или недоступен боту.')); return; }
    let rnd = fs.readfile('/dev/urandom', 6), file = DIR + '/log-' + (rnd ? hexenc(rnd) : time()) + '.txt';
    if (!fs.writefile(file, join('\n', lines) + '\n')) { respond(lang.T('Failed to prepare log file.', 'Не удалось подготовить файл журнала.')); return; }
    let t = localtime(time());
    let caption = all ? sprintf(lang.T('📜 Router log · %d lines', '📜 Журнал роутера · строк: %d'), length(lines))
                      : sprintf(lang.T('📜 Modem log · %d lines', '📜 Журнал модемов · строк: %d'), length(lines));
    if (!enqueue(caption, 'info', {method:'sendDocument', file,
                 filename:sprintf('%s-%04d%02d%02d-%02d%02d.txt', all ? 'router' : 'medvemodem', t.year, t.mon, t.mday, t.hour, t.min)}))
        fs.unlink(file);
}

export { forwardSms, sendLog, smsInbox };
