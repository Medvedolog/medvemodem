'use strict';
// «⚙️ Настройки» (settings.uc draws the pages): the changes go through rpcd's medvemodem.bot.config, then the bot
// reads them again.
import * as log from 'log';
import * as ui from './ui.uc';
import * as settings from './settings.uc';
import * as lang from './lang.uc';
import { label } from './safe.uc';
import { SETTINGS, SMS_ALLOWED, live, loadNotices, newCursor, pollCfg, proxySecrets, state } from './conf.uc';
import { enqueue, respond } from './queue.uc';
import { probeQueue } from './links.uc';

function reloadSettings() {
    let c = newCursor();
    loadNotices(c);
    live.cfg = c.get_all('medvemodem-bot', 'main') || live.cfg;
    live.smsForward = SMS_ALLOWED && live.cfg.sms_forward == '1';
    live.proxyPassword = proxySecrets(c);
    state.lastRouteRefresh = 0;	/* the next tick finds the live.routes again */
}
function settingsCall(method, args) {
    let r = null;
    try { r = state.bus ? state.bus.call('medvemodem.bot.config', method, args) : null; } catch (e) { r = null; }
    if (type(r) != 'object') r = {ok:false, code:'unavailable'};
    if (r.ok) reloadSettings();
    log.syslog(r.ok ? log.LOG_INFO : log.LOG_WARNING, 'settings_%s_%s', method, r.ok ? 'done' : (match(r.code || '', /^[a-z_]{1,32}$/) ? r.code : 'failed'));
    return r;
}
function settingsInfo() {
    let c = newCursor();
    return {events:c.get_all('medvemodem-bot', 'events') || {}, main:c.get_all('medvemodem-bot', 'main') || {},
            sms_allowed:SMS_ALLOWED, allowed:SETTINGS};
}
// the proxy sections, by the bot's order, with what the bot saw of each
function proxyList() {
    let out = [], all = newCursor().get_all('medvemodem-bot') || {};
    let current = length(live.routes) ? live.routes[state.tiers.poll % length(live.routes)]?.name : null;
    for (let sec, x in all) {
        if (x['.type'] != 'proxy' || !match(sec, /^[A-Za-z0-9_]{1,32}$/) || type(x.endpoint) != 'string') continue;
        let p = state.probe.results[sec], e = state.routeErrors[sec], check = null;
        if (state.probe.name == sec || index(map(state.probe.queue, (q) => q.name), sec) >= 0) check = {running:true};
        else if (p && (!e || p.at >= e.at)) check = {ok:p.bot_api_ok, ms:p.latency_ms, at:p.at, error:p.error, state:p.state};
        else if (e) check = {ok:false, at:e.at, state:e.state};
        push(out, {sec, title:type(x.title) == 'string' ? label(x.title) : null, endpoint:x.endpoint,
                   user:type(x.proxy_username) == 'string' && match(x.proxy_username, /^[A-Za-z0-9_.@-]{1,80}$/) ? x.proxy_username : null,
                   enabled:x.enabled == '1', primary:x.role == 'primary', order:int(x.priority || 100), current:current == sec, check});
    }
    return sort(out, (a, b) => a.primary != b.primary ? (a.primary ? -1 : 1) : a.order != b.order ? a.order - b.order : (a.sec < b.sec ? -1 : 1));
}
// a route's last check: running, its probe's answer or the error the bot met on it
function routeCheck(name) {
    let p = state.probe.results[name], e = state.routeErrors[name];
    if (state.probe.name == name || index(map(state.probe.queue, (q) => q.name), name) >= 0) return {running:true};
    if (p && (!e || p.at >= e.at)) return {ok:p.bot_api_ok, ms:p.latency_ms, at:p.at, error:p.error, state:p.state};
    return e ? {ok:false, at:e.at, state:e.state} : null;
}
function proxiesView(note) {
    let local = [], endpoints = [];
    let current = length(live.routes) ? live.routes[state.tiers.poll % length(live.routes)]?.name : null;
    for (let r in live.routes) if (r.source == 'local') {
        let user = type(r.auth) == 'string' && index(r.auth, ':') > 0 ? substr(r.auth, 0, index(r.auth, ':')) : null;
        push(local, {name:r.name, endpoint:type(r.proxy) == 'string' ? r.proxy : null, user, current:current == r.name,
                     check:routeCheck(r.name)});
        if (type(r.proxy) == 'string') push(endpoints, r.proxy);
    }
    return settings.proxiesPage(proxyList(), {local, endpoints, now:time(), allowed:SETTINGS, mode:pollCfg.mode || 'auto', note,
                                              clash:live.clash});
}
// switching off or removing this proxy leaves the bot no way to Telegram: proxy-only polling and no other proxy
function lastRoute(sec) {
    if ((pollCfg.mode || 'auto') != 'proxy' && pollCfg.fallback_direct != '0') return false;
    for (let r in live.routes) if (r.proxy && r.name != sec) return false;
    return true;
}
function settingsPage(page, note) {
    if (page == 'proxies') return proxiesView(note);
    let n = settingsInfo();
    if (page == 'notify') return settings.notifyPage(n, note);
    let p = {configured:0, enabled:0, local:0};
    for (let x in proxyList()) { p.configured++; if (x.enabled) p.enabled++; }
    for (let r in live.routes) if (r.source == 'local') p.local++;
    n.proxies = p;
    n.clash = live.clash;
    return settings.mainPage(n);
}
// the settings pages go raw (no number filter): they show the proxies' addresses from the bot's own config, never a SIM's
function settingsSend(page) { enqueue(page.text, 'info', {markup:page.markup, html:true, raw:true, reply:true}); }
function settingsEdit(mid, page) {
    enqueue(page.text, 'info', {method:'editMessageText', edit:mid, markup:page.markup, html:true, raw:true, urgent:true});
}
// a proxy check from the page ended: the page it was asked from is drawn again with the result
function settingsProbeDone(name) {
    let w = state.settingsProbe;
    if (!w || w.name != name) return;
    state.settingsProbe = null;
    if (time() - w.at > 120) return;
    settingsEdit(w.mid, proxiesView());
}
function onSettings(pressed, answer, mid) {
    state.ask = null;
    let edit = (page) => settingsEdit(mid, page);
    let op = pressed.op, note = null;
    if (op && !SETTINGS && op != 'check' && op != 'check_local') { answer(settings.errorWords('settings_off')); edit(settingsPage(pressed.page)); return; }
    if (op == 'mode') {
        let next = settings.nextMode(settingsInfo().events);
        let r = settingsCall('bot_set', {key:'events.mode', value:next});
        if (r.ok && next != 'off' && live.notices.enabled != '1') r = settingsCall('bot_set', {key:'events.enabled', value:'1'});
        answer(r.ok ? '' : settings.errorWords(r.code));
    } else if (op == 'toggle') {
        let t = settings.TOGGLES[pressed.key], n = settingsInfo();
        let r = settingsCall('bot_set', {key:t[0] + '.' + t[1], value:settings.on(n, pressed.key) ? '0' : '1'});
        answer(r.ok ? '' : settings.errorWords(r.code));
    } else if (op == 'ask_quiet' || op == 'ask_proxy') {
        answer('');
        state.ask = {what:op == 'ask_quiet' ? 'quiet' : 'proxy', mid, until:time() + 300};
        edit(op == 'ask_quiet' ? settings.askQuiet(settingsInfo().events) : settings.askProxy());
        return;
    } else if (op == 'enable' || op == 'check' || op == 'ask_delete' || op == 'delete') {
        let p = null;
        for (let x in proxyList()) if (x.sec == pressed.sec) p = x;
        if (!p) { answer(settings.errorWords('no_such_proxy')); edit(proxiesView()); return; }
        if (op == 'check') {
            if (!p.enabled || !probeQueue('poll', p.sec)) { answer(''); edit(proxiesView()); return; }
            state.settingsProbe = {name:p.sec, mid, at:time()};
            answer(lang.T('Checking…', 'Проверка…'));
        } else if (op == 'ask_delete') { answer(''); edit(settings.askDelete(p, time())); return; }
        else if ((op == 'delete' || p.enabled) && lastRoute(p.sec)) { answer(settings.errorWords('last_route')); note = '⚠️ ' + settings.errorWords('last_route'); }
        else if (op == 'delete' && !ui.stampFresh(pressed.stamp, time())) answer(lang.T('Button expired', 'Кнопка устарела'));
        else {
            let r = op == 'delete' ? settingsCall('bot_proxy_del', {section:p.sec}) : settingsCall('bot_proxy_set', {section:p.sec, enabled:p.enabled ? '0' : '1'});
            answer(r.ok ? (op == 'delete' ? lang.T('Removed', 'Удалён') : '') : settings.errorWords(r.code));
        }
    } else if (op == 'check_local') {
        // a proxy found on the router: checked as an own one (getMe through it), the page drawn again with the result
        if (!probeQueue('poll', pressed.name)) { answer(''); edit(proxiesView()); return; }
        state.settingsProbe = {name:pressed.name, mid, at:time()};
        answer(lang.T('Checking…', 'Проверка…'));
    } else answer('');
    edit(settingsPage(pressed.page, note));
}
// the admin's answer to «⏰» or «➕»: the quiet hours, or a proxy (its message is deleted at once: it may hold a password)
function onSettingsText(m) {
    let ask = state.ask;
    state.ask = null;
    if (ask.what == 'proxy' && type(m.message_id) == 'int') enqueue('', 'info', {method:'deleteMessage', edit:m.message_id, urgent:true});
    if (!SETTINGS) { respond(settings.errorWords('settings_off')); return; }
    if (ask.what == 'quiet') {
        let q = settings.parseQuiet(m.text);
        if (!q) { state.ask = ask; respond('⚠️ ' + lang.T('Invalid format. Send as 23:00-07:00.', 'Неверный формат. Пришлите в виде 23:00-07:00.')); return; }
        let r = settingsCall('bot_set', {key:'events.quiet_from', value:q.from});
        if (r.ok) r = settingsCall('bot_set', {key:'events.quiet_to', value:q.to});
        if (r.ok && live.notices.quiet != '1') r = settingsCall('bot_set', {key:'events.quiet', value:'1'});
        settingsSend(settingsPage('notify', r.ok ? '✅ ' + sprintf(lang.T('Quiet hours: %s–%s.', 'Тихие часы: %s–%s.'), q.from, q.to) : '⚠️ ' + settings.errorWords(r.code)));
        return;
    }
    let p = settings.parseProxy(m.text);
    if (p.error) { state.ask = ask; respond('⚠️ ' + settings.errorWords(p.error)); return; }
    // rpcd checks it through to Telegram (up to 8 s) before it is used
    let r = settingsCall('bot_proxy_add', {endpoint:p.endpoint, username:p.username, password:p.password, title:p.title || ''});
    p.password = null;
    settingsSend(proxiesView(r.ok ? settings.addedWords(r, p) : '⚠️ ' + settings.errorWords(r.code)));
}

export { onSettings, onSettingsText, settingsPage, settingsProbeDone, settingsSend };
