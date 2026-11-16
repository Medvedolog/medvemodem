'use strict';
// The outbox: every Bot API call the bot makes is a queue item, persisted on tmpfs before it is tried, an unsure one
// never sent twice; quiet hours; the request each item becomes.
import * as fs from 'fs';
import * as ui from './ui.uc';
import { opaque, message, cut, htmlBudget } from './safe.uc';
import { DIR, chat, key, live, state } from './conf.uc';

function atomic(path, data) {
    let temp = path + '.new';
    let f = fs.open(temp, 'w', 0o600);
    if (!f) return false;
    let n = f.write(data);
    let closed = f.close();
    if (n != length(data) || !closed) { fs.unlink(temp); return false; }
    return fs.rename(temp, path) == true;
}
function remember() {
    return atomic(DIR + '/offset.json', sprintf('%J', {offset:state.offset}));
}
// A bounded, sanitized outbox is persisted on tmpfs before attempting delivery.
// An in-flight send after a crash is marked uncertain, never blindly replayed.
function persistQueue() {
    // an SMS page (its sender and text) is never written down, not even to RAM disk: lost on a restart, as it should be
    let ok = atomic(DIR + '/queue.json', sprintf('%J', {schema:1, nonce:state.queueNonce, items:filter(state.queue, (i) => !i.secret)}));
    state.queueFault = !ok;
    return ok;
}
// before restoreQueue: ucode resolves a name where the function is written (second audit 2026-10-21: declared
// below it, every restore threw «access to undeclared variable METHODS» and the whole queue was lost on a restart)
// a queue item is one Bot API call: a message (the default), the edit of one, the answer to a button press
// or the command menu; extra = {method, markup, edit, cb, urgent}
const METHODS = {sendMessage:true, editMessageText:true, answerCallbackQuery:true, setMyCommands:true, sendDocument:true, deleteMessage:true};
// the calls that make a new message: cut off without an answer, they may have arrived, so they are not sent again
// (second audit 2026-10-21: a document was retried as harmless and could come twice)
const CREATES = {sendMessage:true, sendDocument:true};
function validMarkup(markup) {
    return markup == null || type(markup) == 'object' && length(sprintf('%J', markup)) <= 4096;
}
// an item whose delivery is unsure: an answer to the admin's command goes once more (a command answer is idempotent),
// anything else stays «uncertain», never replayed. Defined before restoreQueue: ucode does not hoist functions, and the
// restore of an in-flight item at the start called it undefined and lost the rest of the queue (r83, found in r85)
function markUnsure(item) {
    if (item.reply && !item.resent) { item.resent = true; item.status = 'pending'; return; }
    item.status = 'uncertain';
    state.uncertain++;
}
function restoreQueue() {
    let saved = fs.readfile(DIR + '/queue.json', 327680);
    if (!saved) return;
    try {
        let doc = json(saved);
        if (doc.schema != 1 || type(doc.items) != 'array') return;
        if (type(doc.nonce) == 'int' && doc.nonce >= 0) state.queueNonce = doc.nonce;
        for (let x in doc.items) {
            if (length(state.queue) >= 200 || type(x) != 'object') break;
            if (type(x.id) != 'string' || !match(x.id, /^hmac128:[a-f0-9]{32}$/)) continue;
            if (type(x.text) != 'string' || length(x.text) > 3500) continue;
            // an HTML page went through the privacy filter before its marks became tags: undone, it is what the filter
            // gives again (second audit 2026-10-21: every HTML item failed the plain check and was lost on a restart)
            let plain = x.html === true ? replace(replace(x.text, /<(\/?)(b|code|i)>/g, '⟦$1$2⟧'), /&amp;/g, '&') : x.text;
            if (match(plain, /[<>]/) || message(plain) != plain) continue;
            // a button's answer expires in seconds: after a restart it is not worth sending
            let method = x.method || 'sendMessage';
            if (!METHODS[method] || method == 'answerCallbackQuery' || !validMarkup(x.markup)) continue;
            if (method == 'editMessageText' && (type(x.edit) != 'int' || x.edit < 1)) continue;
            if (type(x.created) != 'int' || time() - x.created > 3600 || x.created > time()) continue;
            if (x.status != 'pending' && x.status != 'uncertain' && x.status != 'inflight') continue;
            if (state.queueBytes + length(x.text) > 262144) break;
            // a document: its file, the bot's own log-*.txt, back with it; without it the item cannot be sent
            if (method == 'sendDocument' && (type(x.file) != 'string' || !match(x.file, regexp('^' + DIR + '/log-[0-9a-f]{1,16}\\.txt$')) ||
                !fs.stat(x.file) || type(x.filename) != 'string' || !match(x.filename, /^[A-Za-z0-9._-]{1,64}$/))) continue;
            let item = {id:x.id, text:x.text, priority:x.priority == 'warning' ? 'warning' : 'info',
                        status:x.status, created:x.created};
            if (x.reply === true) item.reply = true;
            if (x.silent === true) item.silent = true;
            if (x.resent === true) item.resent = true;
            if (item.status == 'inflight') {
                if (CREATES[method]) markUnsure(item);
                else item.status = 'pending';
            } else if (item.status == 'uncertain') state.uncertain++;
            if (method != 'sendMessage') item.method = method;
            if (x.markup) item.markup = x.markup;
            if (method == 'editMessageText') item.edit = x.edit;
            if (x.html === true) item.html = true;
            if (method == 'sendDocument') { item.file = x.file; item.filename = x.filename; }
            push(state.queue, item);
            state.queueBytes += length(x.text);
        }
    } catch (e) { state.queueFault = true; }
    // a log file no item holds (a crash between writing and queueing it, an old version) goes
    let held = {};
    for (let item in state.queue) if (item.file) held[item.file] = true;
    for (let name in (fs.lsdir(DIR) || []))
        if (match(name, /^log-[0-9a-f]{1,16}\.txt$/) && !held[DIR + '/' + name]) fs.unlink(DIR + '/' + name);
    persistQueue();
}
// an item leaving the queue any way (sent, refused, expired, pushed out) takes its file with it (second audit
// 2026-10-21: the hour's expiry and the eviction left log-*.txt on the RAM disk)
function forget(item) {
    if (item?.file) fs.unlink(item.file);
}
function prune() {
    let now = time();
    let valid = [], total = 0, changed = false;
    for (let item in state.queue) {
        if (now - item.created > 3600) {
            if (item.status == 'uncertain' && state.uncertain > 0) state.uncertain--;
            forget(item);
            state.dropped++; changed = true; continue;
        }
        push(valid, item); total += length(item.text);
    }
    state.queue = valid; state.queueBytes = total;
    if (changed) persistQueue();
}
// the SIM numbers of the modems the bot knows: the only numbers its messages may show (safe.message)
function ownNumbers() {
    let out = [];
    for (let id in keys(state.deviceSafe || {}))
        if (state.deviceSafe[id]?.sim?.number) push(out, state.deviceSafe[id].sim.number);
    return out;
}
// quiet hours (events.quiet, quiet_from, quiet_to; the settings page): a notification comes without sound, none is
// dropped; «no internet» and a used-up limit keep the sound unless quiet_urgent is 0
function minutes(v) {
    let m = match(type(v) == 'string' ? v : '', /^([01][0-9]|2[0-3]):([0-5][0-9])$/);
    return m ? int(m[1]) * 60 + int(m[2]) : null;
}
function quietHours(now) {
    if (live.notices.quiet != '1') return false;
    let a = minutes(live.notices.quiet_from || '23:00'), b = minutes(live.notices.quiet_to || '07:00');
    if (a == null || b == null || a == b) return false;
    let t = localtime(now), m = t.hour * 60 + t.min;
    return a < b ? m >= a && m < b : m >= a || m < b;
}
function enqueue(text, priority, extra) {
    extra = extra || {};
    let method = extra.method || 'sendMessage';
    if (!METHODS[method] || !validMarkup(extra.markup)) return false;
    // an SMS page shows its sender's number and the text as they are: the owner's choice (main.allow_sms_content)
    let s = extra.html ? replace(text || '', /[\x01-\x08\x0b-\x1f\x7f<>]/g, ' ') : null;
    if (!extra.html) s = extra.raw ? cut(replace(text || '', /[\x01-\x08\x0b-\x1f\x7f<>]/g, ' '), 3500) : message(text || '', ownNumbers());
    else if (!extra.raw) s = message(s, ownNumbers(), 1 << 20);
    // an HTML page: its marks become tags after the privacy filter, which drops every < and > of the values, within the
    // budget counted after the escaping, the tags whole and closed (audit R05)
    if (extra.html) s = htmlBudget(s, 3500);
    if (length(s) == 0 && (method == 'sendMessage' || method == 'editMessageText')) return false;
    prune();
    // Prevent a single malformed string from ever blocking the queue.
    if (length(s) > 3500) return false;
    while (length(state.queue) >= 200 || state.queueBytes + length(s) > 262144) {
        // the oldest plain notice goes first; a warning or a pressed button's answer only when nothing else is left
        // (audit 13.7: a burst of recovery live.notices pushed out the warning that mattered)
        let at = -1;
        for (let i = 0; i < length(state.queue); i++)
            if (state.queue[i].priority != 'warning' && !state.queue[i].urgent && state.queue[i].status == 'pending') { at = i; break; }
        let removed = at >= 0 ? splice(state.queue, at, 1)[0] : shift(state.queue);
        if (!removed) break;
        forget(removed);
        state.queueBytes -= length(removed.text);
        if (removed.status == 'uncertain' && state.uncertain > 0) state.uncertain--;
        state.dropped++;
    }
    let id = opaque(key, '' + time() + ':' + (++state.queueNonce) + ':' + s);
    let item = {id, text:s, status:'pending', created:time(), priority:priority || 'info'};
    if (method != 'sendMessage') item.method = method;
    if (extra.markup) item.markup = extra.markup;
    if (type(extra.edit) == 'int') item.edit = extra.edit;
    if (type(extra.cb) == 'string') item.cb = extra.cb;
    if (extra.html) item.html = true;
    if (extra.raw) item.secret = true;
    if (extra.reply) item.reply = true;
    if (extra.silent) item.silent = true;
    // a notification, not an answer to the admin
    if (method == 'sendMessage' && !extra.reply && quietHours(time()) && !(extra.critical && live.notices.quiet_urgent != '0')) item.silent = true;
    if (type(extra.file) == 'string') { item.file = extra.file; item.filename = extra.filename; }
    if (extra.urgent) {
        // a button press is answered before the notifications that wait, in the order pressed
        item.urgent = true;
        let at = 0;
        while (at < length(state.queue) && state.queue[at].urgent) at++;
        splice(state.queue, at, 0, item);
    } else push(state.queue, item);
    state.queueBytes += length(s);
    state.lastQueued = id;
    return persistQueue();
}
// an answer to the admin's command: sent once more if a cut connection left it unsure (an alert is not: it could
// arrive twice); bench 2026-10-22, the proxy cut sends and /start got no answer (HANDOFF 199 item 13)
function respond(text, markup) { enqueue(text, 'info', {markup, html:true, reply:true}); }
function queueHead() {
    for (let i = 0; i < length(state.queue); i++)
        if (state.queue[i].status == 'pending') return i;
    return -1;
}
// Report distinct queue states: uncertain deliveries must not appear as pending.
function queueCount(status) {
    let count = 0;
    for (let item in state.queue) if (item.status == status) count++;
    return count;
}
// a call cut off without an answer: a message may have arrived, so it is not sent twice; an edit, an answer to
// a button or the command menu repeat harmlessly
function unsure(id) {
    for (let item in state.queue) if (item.id == id && item.status == 'inflight') {
        if (!CREATES[item.method || 'sendMessage']) item.status = 'pending';
        else markUnsure(item);
        break;
    }
    persistQueue();
}
// the Bot API call for a queue item
function request(item) {
    let method = item.method || 'sendMessage';
    if (method == 'answerCallbackQuery') {
        let body = {callback_query_id:item.cb};
        if (length(item.text)) body.text = item.text;
        return {method, body};
    }
    if (method == 'setMyCommands')
        return {method, body:{commands:ui.commandList(), scope:{type:'chat', chat_id:int(chat)}}};
    if (method == 'sendDocument')
        return {method, body:{chat_id:chat, file:item.file, filename:item.filename, caption:item.text}};
    if (method == 'deleteMessage') return {method, body:{chat_id:chat, message_id:item.edit}};
    let body = {chat_id:chat, text:item.text, disable_web_page_preview:true};
    if (item.html) body.parse_mode = 'HTML';
    // good news and summaries come without a sound (docs/BOT-NOTIFICATIONS-PLAN.md step 6)
    if (item.silent) body.disable_notification = true;
    if (item.markup) body.reply_markup = item.markup;
    if (method == 'editMessageText') body.message_id = item.edit;
    return {method, body};
}
function dropItem(id) {
    for (let i = 0; i < length(state.queue); i++) {
        if (state.queue[i].id != id) continue;
        state.queueBytes -= length(state.queue[i].text);
        forget(state.queue[i]);
        splice(state.queue, i, 1);
        break;
    }
    persistQueue();
}

export { CREATES, atomic, dropItem, enqueue, markUnsure, ownNumbers, persistQueue, prune, queueCount, queueHead, remember, request, respond, restoreQueue, unsure };
