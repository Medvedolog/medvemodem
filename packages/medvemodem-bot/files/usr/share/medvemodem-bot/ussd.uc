'use strict';
// USSD from Telegram (the owner, 2026-11-12: «может сразу и ussd научить отправлять для модемов это поддерживающих?»).
// With main.allow_send, as SMS. The modem (one that offers USSD on its page), then the code: the operator's own from the
// engine's presets or typed; the engine sends it (ubus medvemodem ussd_start) and the bot follows ussd_status once a
// second, editing one message: «Ожидание ответа…», then the network's answer. A menu is answered with a message
// (ussd_respond) or closed (ussd_cancel); the engine closes it itself after a minute. Codes and answers are never
// logged or written to the queue file.
import * as uloop from 'uloop';
import * as log from 'log';
import * as ui from './ui.uc';
import * as lang from './lang.uc';
import { SEND, state } from './conf.uc';
import { enqueue } from './queue.uc';
import { idByKey } from './engine.uc';

const T = lang.T;
const WAIT_S = 90;	/* the engine answers within 75 s */

function code(v) {
    let c = replace(type(v) == 'string' ? trim(v) : '', / /g, '');
    return match(c, /^[0-9*#+]{2,32}$/) ? c : null;
}
function reply(v) {
    let r = type(v) == 'string' ? trim(v) : '';
    return length(r) && length(r) <= 64 && !match(r, /['"]/) ? r : null;
}
function able() {
    let out = [];
    for (let id in state.deviceIds) {
        let d = state.deviceSafe[id];
        if (d?.present && !d.frozen && d.ussd_ok) push(out, d);
    }
    return out;
}
function send(page, mid) {
    enqueue(page.text, 'info', mid ? {method:'editMessageText', edit:mid, markup:page.markup, html:true, raw:true, urgent:true} :
            {markup:page.markup, html:true, raw:true, reply:true});
}
function head(name) {
    return '📞 ' + ui.B('USSD') + (name ? ' · 📶 ' + ui.esc(name) : '');
}
function off() {
    return {text:head(null) + '\n' +
                 T('Sending SMS and USSD from Telegram is disabled. Enable "Allow sending SMS and USSD from Telegram" on the bot page in LuCI.',
                   'Отправка SMS и USSD из Telegram отключена. Включите опцию «Разрешить отправку SMS и USSD из Telegram» на странице бота в LuCI.'),
            markup:null};
}
// the operator's codes the engine knows for this SIM (ubus presets: ussd.codes [{title, code}]), eight at most
function presets(id) {
    let r = null;
    try { r = state.bus ? state.bus.call('medvemodem', 'presets', {id}) : null; } catch (e) { r = null; }
    let out = [];
    for (let c in (type(r?.ussd?.codes) == 'array' ? r.ussd.codes : []))
        if (type(c?.title) == 'string' && code(c.code) && length(out) < 8) push(out, {title:substr(c.title, 0, 40), code:code(c.code)});
    return out;
}
function codePage(job) {
    let rows = map(job.codes, (c) => [{text:c.title + ' · ' + c.code, callback_data:'up:' + index(job.codes, c)}]);
    push(rows, [{text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'ux'}]);
    return {text:head(job.name) + '\n' + T('Send the code in reply (*100#)', 'Пришлите в ответ код (*100#)') +
                 (length(job.codes) ? T(', or pick the operator\'s one:', ' или выберите код оператора:') : '.'),
            markup:{inline_keyboard:rows}};
}
// the network's answer, as the modem page says it
function outcome(job, st) {
    let menu = st.state == 'done' && st.session == 'user-response';
    let lines = [head(job.name) + ' · ' + ui.C(job.code)];
    if (st.state == 'done') push(lines, ui.esc(type(st.reply) == 'string' && length(st.reply) ? substr(st.reply, 0, 1500) :
                                              T('The network answered without text.', 'Сеть ответила без текста.')));
    else if (st.state == 'cancelled') push(lines, st.closed ? T('The menu is closed: there was no reply within a minute.', 'Меню закрыто: ответа не было в течение минуты.') :
                                                            T('The USSD session ended.', 'Сеанс USSD завершён.'));
    else if (match(type(st.error) == 'string' ? st.error : '', /terminated by network/i))
        push(lines, T('The network closed the session without text. Some operators send the answer by SMS: check the incoming messages.',
                      'Сеть закрыла сеанс без текста. Некоторые операторы присылают ответ по SMS: проверьте входящие.'));
    else push(lines, T('No answer from the network.', 'Сеть не ответила.'));
    if (menu) push(lines, '\n' + ui.I(T('Reply with a message within a minute: then the menu closes by itself.',
                                        'Ответьте сообщением в течение минуты: потом меню закроется само.')));
    let keys = menu ? [[{text:T('⏹ End the USSD session', '⏹ Завершить сеанс'), callback_data:'uc'}]] :
                      [[{text:T('📞 One more', '📞 Ещё запрос'), callback_data:'us'}]];
    return {text:join('\n', lines), markup:{inline_keyboard:keys}, menu};
}
// one look at ussd_status a second until the answer, WAIT_S at most
function follow(job) {
    if (state.ussdJob !== job) return;
    let st = null;
    try { st = state.bus ? state.bus.call('medvemodem', 'ussd_status', {id:job.id}) : null; } catch (e) { st = null; }
    let waited = time() - job.since;
    if ((type(st) != 'object' || st.state == 'running' || st.state == 'idle') && waited < WAIT_S) {
        if (waited >= 3 && waited % 5 == 0)
            send({text:head(job.name) + ' · ' + ui.C(job.code) + '\n' + T('Waiting for the network\'s answer… ', 'Ожидание ответа сети… ') +
                       sprintf(T('%d s', '%d с'), waited), markup:{inline_keyboard:[[{text:T('⏹ End the USSD session', '⏹ Завершить сеанс'), callback_data:'uc'}]]}}, job.mid);
        uloop.timer(1000, () => follow(job));
        return;
    }
    let page = type(st) == 'object' ? outcome(job, st) :
               {text:head(job.name) + '\n' + T('The MedveModem service does not answer.', 'Служба MedveModem не отвечает.'), markup:null, menu:false};
    log.syslog(log.LOG_INFO, 'ussd_' + (type(st) == 'object' ? st.state : 'lost'));
    send(page, job.mid);
    if (page.menu) state.ask = {what:'ussd_reply', until:time() + 60};
    else state.ussdJob = null;
}
function run(job, mid, call, args) {
    let r = null;
    try { r = state.bus ? state.bus.call('medvemodem', call, args) : null; } catch (e) { r = null; }
    if (type(r) != 'object' || r.refused) {
        state.ussdJob = null;
        send({text:head(job.name) + '\n' + T('The modem does not take USSD now', 'Модем сейчас не принимает USSD') +
                   (type(r?.refused) == 'string' ? ': ' + ui.esc(r.refused) : '.'), markup:null}, mid);
        return;
    }
    job.mid = mid;
    job.since = time();
    state.ussdJob = job;
    send({text:head(job.name) + ' · ' + ui.C(job.code) + '\n' + T('Waiting for the network\'s answer…', 'Ожидание ответа сети…'),
          markup:{inline_keyboard:[[{text:T('⏹ End the USSD session', '⏹ Завершить сеанс'), callback_data:'uc'}]]}}, mid);
    uloop.timer(1000, () => follow(job));
}
function askCode(d, mid) {
    let job = {id:null, key:ui.shortKey(d), name:d.name, codes:null, code:null, until:time() + 600};
    job.id = idByKey(job.key);
    job.codes = presets(job.id);
    state.ussdDraft = job;
    state.ask = {what:'ussd_code', until:job.until};
    send(codePage(job), mid);
}

function start(mid) {
    state.ask = null;
    state.ussdDraft = null;
    if (!SEND) { send(off(), mid); return; }
    let list = able();
    if (length(list) == 1) { askCode(list[0], mid); return; }
    if (!length(list)) {
        send({text:head(null) + '\n' + T('No modem can send USSD now: none is connected, or its USSD are not available to MedveModem.',
                                         'Сейчас ни один модем не может отправить USSD: модемы не подключены или USSD у них недоступен MedveModem.'),
              markup:null}, mid);
        return;
    }
    let rows = map(list, (d) => [{text:'📶 ' + d.name, callback_data:'um:' + ui.shortKey(d)}]);
    push(rows, [{text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'ux'}]);
    send({text:head(null) + '\n' + T('From which modem?', 'С какого модема отправить?'), markup:{inline_keyboard:rows}}, mid);
}

// a press: us start, um:<key> the modem, up:<i> a preset, uc end the session, ux cancel the draft
function onPress(p, answer, mid) {
    if (!SEND) { answer(''); send(off(), mid); return; }
    if (p.kind == 'ussd_begin') { answer(''); start(mid); return; }
    if (p.kind == 'ussd_cancel_draft') {
        state.ussdDraft = null; state.ask = null;
        answer(T('Cancelled', 'Отменено'));
        send({text:head(null) + '\n' + T('No request was sent.', 'Запрос не отправлен.'), markup:null}, mid);
        return;
    }
    if (p.kind == 'ussd_end') {
        let job = state.ussdJob;
        if (!job) { answer(T('No session', 'Нет активного сеанса')); return; }
        answer(T('Ending…', 'Завершение…'));
        state.ask = null;
        try { state.bus.call('medvemodem', 'ussd_cancel', {id:job.id}); } catch (e) {}
        state.ussdJob = null;
        send({text:head(job.name) + '\n' + T('The USSD session ended.', 'Сеанс USSD завершён.'),
              markup:{inline_keyboard:[[{text:T('📞 One more', '📞 Ещё запрос'), callback_data:'us'}]]}}, mid);
        return;
    }
    if (p.kind == 'ussd_modem') {
        let d = state.deviceSafe[idByKey(p.key)];
        if (!d || !d.present || d.frozen || !d.ussd_ok) { answer(T('The modem cannot send USSD now', 'Модем сейчас не может отправить USSD')); start(mid); return; }
        answer('');
        askCode(d, mid);
        return;
    }
    if (p.kind == 'ussd_preset') {
        let job = state.ussdDraft, c = job?.codes?.[p.index];
        if (!job || !c || time() > job.until) { answer(T('Button expired', 'Кнопка устарела')); return; }
        if (state.ussdJob) { answer(T('A request is already running', 'Запрос уже выполняется')); return; }
        answer(T('Sending…', 'Отправка…'));
        state.ask = null; state.ussdDraft = null;
        job.code = c.code;
        run(job, mid, 'ussd_start', {id:job.id, code:c.code});
    }
}

// a typed answer: the code, or the reply to a menu
function onText(m) {
    let ask = state.ask;
    state.ask = null;
    if (!SEND) return;
    if (ask.what == 'ussd_reply') {
        let job = state.ussdJob, r = reply(m.text);
        if (!job) return;
        if (!r) { state.ask = ask; send({text:'⚠️ ' + T('A reply: up to 64 characters, without quotes.', 'Ответ: до 64 символов, без кавычек.'), markup:null}, null); return; }
        job.code = r;
        run(job, null, 'ussd_respond', {id:job.id, reply:r});
        return;
    }
    let job = state.ussdDraft, c = code(m.text);
    if (!job || time() > job.until) { state.ussdDraft = null; return; }
    if (!c) { state.ask = ask; send({text:'⚠️ ' + T('Not a USSD code: digits, * and #, as *100#.', 'Это не USSD-код: цифры, * и #, например *100#.'), markup:null}, null); return; }
    if (state.ussdJob) { send({text:'⚠️ ' + T('A request is already running.', 'Запрос уже выполняется.'), markup:null}, null); return; }
    state.ussdDraft = null;
    job.code = c;
    run(job, null, 'ussd_start', {id:job.id, code:c});
}

export { onPress, onText, start };
