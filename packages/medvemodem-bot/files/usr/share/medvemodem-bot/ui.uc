'use strict';
// What the admin sees in Telegram: texts, the persistent keyboard, inline buttons and the command menu
// (ТЗ §9, §9.1). The pages are HTML, written with marks (⟦b⟧ ⟦/b⟧ ⟦code⟧ ⟦/code⟧ ⟦i⟧ ⟦/i⟧) that main.uc turns into
// tags after the privacy filter (safe.message drops < and >). Buttons change a modem only when the owner allowed
// control in LuCI (main.allow_control), and the disruptive ones ask first. The words are the LuCI page's, in English and
// Russian side by side (lang.uc picks one as a text is made).
import { cut, label, stateWord, quotaWord, roleWord } from './safe.uc';
import { T, W } from './lang.uc';

// the persistent keyboard under the input field: a button sends its own text, which maps to a command. Either
// language's text is taken: a keyboard drawn before the language changed stays in the chat until it is sent again
const BUTTONS = [['Status', 'Статус', '/status'], ['Modems', 'Модемы', '/modems'], ['Channels', 'Каналы', '/uplinks'],
                 ['Traffic', 'Трафик', '/quota'], ['Journal', 'Журнал', '/events'], ['Help', 'Справка', '/help']];
// one table for the «Меню» button and the help (audit 12: three lists drifted apart); /channels is /uplinks
const COMMANDS = [['menu', 'Summary & controls', 'Сводка и кнопки'], ['status', 'Router & modem status', 'Состояние роутера и модемов'],
                  ['modems', 'Modem list', 'Список модемов'], ['channels', 'Internet uplinks', 'Каналы доступа в интернет'],
                  ['sms', 'Incoming SMS', 'Входящие SMS'], ['send', 'Send an SMS', 'Отправить SMS'], ['ussd', 'A USSD request', 'USSD-запрос'],
                  ['network', 'IPs & diagnostics', 'Адреса и проверки'],
                  ['quota', 'Data caps', 'Лимиты трафика'], ['events', 'Latest events', 'Последние события'],
                  ['log', 'Download modem log', 'Скачать журнал модемов'],
                  ['logall', 'Download full router log', 'Скачать полный журнал роутера'], ['settings', 'Notifications and proxies', 'Уведомления и прокси'],
                  ['help', 'Help', 'Справка']];

// no keyboard of buttons under the input field (the owner, 2026-11-03: «убери вечно плавающие кнопки, оставь только
// выпадающее меню с командами и подсказками»): the «Меню» list of commands with their descriptions (setMyCommands)
// stays; a keyboard a client kept from before is taken away, and its keys typed still work (buttonCommand)
function menuKeyboard() {
    return {remove_keyboard:true};
}

function buttonCommand(text) {
    for (let b in BUTTONS) if (b[0] == text || b[1] == text) return b[2];
    // a keyboard drawn before r118 still has «Лимиты» (audit r117: «Трафик» reads clearer)
    if (text == 'Limits' || text == 'Лимиты') return '/quota';
    // a keyboard kept on the phone from before r102 («Квоты», «Quotas») still works
    if (text == 'Квоты' || text == 'Quotas') return '/quota';
    return null;
}

// setMyCommands: the «Меню» button beside the input field, for the admin's chat only
function commandList() {
    let out = [];
    for (let c in COMMANDS) push(out, {command:c[0], description:T(c[1], c[2])});
    return out;
}

// callback_data is the client's word: only these shapes are taken (all far below Telegram's 64 bytes)
//   n                       a label button (page counter)
//   l / l:<page>            the modem list
//   m:<key>                 a modem's card
//   v:<m|s|u|q|j|i|l>       menu, status, channels, quotas, journal, incoming SMS, the log as a file
//   x:… xt:… xa:… xc        the exclusion menu (links failover never makes main by itself)
//   a:<op>:<key>:<stamp>    an action (asks first when it disrupts); y:<op>:<key>:<stamp> its confirmation
//   w / wi / wt / ws:<key|main>  «🌍 Сеть»: the page, the public addresses, the tunnel, the services through a link
//   g…                      «⚙️ Настройки» (settings.uc parse, tried first in main.uc)
const OPS = {mn:true, up:true, dn:true, rc:true, rr:true, rs:true, fz:true, uf:true};
function parseCallback(data) {
    if (type(data) != 'string') return null;
    if (data == 'n') return {kind:'noop'};
    if (data == 'l') return {kind:'list', page:0};
    let m = match(data, /^l:([0-9]{1,2})$/);
    if (m) return {kind:'list', page:int(m[1])};
    m = match(data, /^m:([a-f0-9]{16})$/);
    if (m) return {kind:'modem', key:m[1]};
    m = match(data, /^v:([msuqjila])$/);
    if (m) return {kind:'view', view:m[1]};
    // the exclusion menu: x:<page> opens, xt:<i>:<page> ticks one in the draft, xa:<stamp> applies, xc drops the draft
    m = match(data, /^x:([0-9]{1,2})$/);
    if (m) return {kind:'exclude', page:int(m[1])};
    // the channel by its interface and the draft it was drawn for (audit R11: an index into a list that changed between
    // the drawing and the press ticked another channel)
    m = match(data, /^xt:([A-Za-z0-9_.-]{1,15}):([0-9]{1,2}):([a-f0-9]{1,6})$/);
    if (m) return {kind:'exclude_tick', device:m[1], page:int(m[2]), draft:m[3]};
    m = match(data, /^xa:([a-f0-9]{6})$/);
    if (m) return {kind:'exclude_apply', stamp:m[1]};
    if (data == 'xc') return {kind:'exclude_cancel'};
    if (data == 'w') return {kind:'net'};
    // «🚧 Белые списки» (the owner, 2026-11-12): the channels, the whitelist mode and the blocking test, apart from «Сеть»
    if (data == 'bl') return {kind:'net_wlpage'};
    if (data == 'wi') return {kind:'net_ip'};
    if (data == 'wt') return {kind:'net_tunnel'};
    if (data == 'wp') return {kind:'net_svcpick'};
    // a SIM's traffic limit (quotaedit.uc): qm:<key> the editor, qs:<key>:<gb>:<stamp>, qd:<key>:<stamp> asks, qy:… removes
    let q = match(data, /^qm:([a-f0-9]{16})$/);
    if (q) return {kind:'quota_edit', key:q[1]};
    q = match(data, /^qs:([a-f0-9]{16}):([0-9]{1,3}):([a-f0-9]{6})$/);
    if (q) return {kind:'quota_set', key:q[1], gb:int(q[2]), stamp:q[3]};
    q = match(data, /^q([dy]):([a-f0-9]{16}):([a-f0-9]{6})$/);
    if (q) return {kind:q[1] == 'd' ? 'quota_drop_ask' : 'quota_drop', key:q[2], stamp:q[3]};
    // the blocking test over every modem: wb asks first (what it does), wby runs it
    if (data == 'wb') return {kind:'net_blocks_ask'};
    if (data == 'wby') return {kind:'net_blocks'};
    if (data == 'wwo' || data == 'wwf') return {kind:'net_wl', action:data == 'wwo' ? 'on' : 'off'};
    m = match(data, /^ws:([a-f0-9]{16}|main)$/);
    if (m) return {kind:'net_svc', link:m[1]};
    // sending an SMS (smssend.uc): ss start, sm:<key> the modem, sc:<i> a contact, sy:<nonce> send, sx cancel,
    // sr:<key>:<number> answer a message; USSD (ussd.uc): us start, um:<key> the modem, up:<i> a code, uc end, ux cancel
    if (data == 'ss') return {kind:'sms_start'};
    if (data == 'sx') return {kind:'sms_cancel'};
    m = match(data, /^sm:([a-f0-9]{16})$/);
    if (m) return {kind:'sms_modem', key:m[1]};
    m = match(data, /^sc:([0-7])$/);
    if (m) return {kind:'sms_contact', index:int(m[1])};
    m = match(data, /^sy:([a-f0-9]{6})$/);
    if (m) return {kind:'sms_go', nonce:m[1]};
    m = match(data, /^sr:([a-f0-9]{16}):(\+?[0-9]{3,20})$/);
    if (m) return {kind:'sms_reply', key:m[1], number:m[2]};
    // MedveModem's update (update.uc): asked, confirmed with the minute's stamp, cancelled
    if (data == 'nv') return {kind:'update_ask'};
    if (data == 'nvx') return {kind:'update_cancel'};
    m = match(data, /^nvy:([a-f0-9]{6})$/);
    if (m) return {kind:'update_go', stamp:m[1]};
    if (data == 'us') return {kind:'ussd_begin'};
    if (data == 'uc') return {kind:'ussd_end'};
    if (data == 'ux') return {kind:'ussd_cancel_draft'};
    m = match(data, /^um:([a-f0-9]{16})$/);
    if (m) return {kind:'ussd_modem', key:m[1]};
    m = match(data, /^up:([0-7])$/);
    if (m) return {kind:'ussd_preset', index:int(m[1])};
    m = match(data, /^([ay]):([a-z]{2}):([a-f0-9]{16}):([a-f0-9]{6})$/);
    if (m && OPS[m[2]]) return {kind:m[1] == 'a' ? 'action' : 'confirm', op:m[2], key:m[3], stamp:m[4]};
    return null;
}

// an action button's stamp: the minute it was drawn. A press of a stale button (a card left open for hours, or
// Telegram delivering old presses again after a reboot) is refused instead of acting
function stamp(now) {
    return sprintf('%06x', int((now || time()) / 60) & 0xffffff);
}
function stampFresh(st, now) {
    let age = (int((now || time()) / 60) - hex(st)) & 0xffffff;
    return age >= 0 && age <= 30;
}

// marks for the HTML page; a value never carries a mark of its own
function esc(v) {
    // alternation, not a class: ucode's classes take bytes, and [⟦⟧] cut the E2 lead byte of every «—» and «€»
    return replace(type(v) == 'string' ? v : v == null ? '' : '' + v, /⟦|⟧/g, '');
}
function B(v) { return '⟦b⟧' + esc(v) + '⟦/b⟧'; }
function C(v) { return '⟦code⟧' + esc(v) + '⟦/code⟧'; }
function I(v) { return '⟦i⟧' + esc(v) + '⟦/i⟧'; }

// a modem's button key: its opaque key, cut (the device_id never leaves the router)
function shortKey(d) {
    return substr(substr(d.key, 8), 0, 16);
}

function plural(n, one, few, many) {
    let a = n % 10, b = n % 100;
    return n + ' ' + (a == 1 && b != 11 ? one : a >= 2 && a <= 4 && (b < 12 || b > 14) ? few : many);
}

function bytes(n) {
    if (type(n) != 'int' && type(n) != 'double' || n < 0) return null;
    let units = T(['B', 'KB', 'MB', 'GB', 'TB'], ['Б', 'КБ', 'МБ', 'ГБ', 'ТБ']), i = 0;
    while (n >= 1024 && i < length(units) - 1) { n /= 1024.0; i++; }	// 1024.0: an int division dropped the tenths
    let s = i && n < 10 ? replace(sprintf('%.1f', n), '.', T('.', ',')) : sprintf('%d', n + 0.5);
    return s + ' ' + units[i];
}

const TECH = {gsm:'2G', gprs:'2G', edge:'2G', umts:'3G', hsdpa:'H', hsupa:'H', hspa:'H', 'hspa-plus':'H+',
              lte:'LTE', 'lte-cat-m':'LTE-M', 'lte-nb-iot':'NB-IoT', '5gnr':'5G'};
function techWords(list) {
    let out = [];
    for (let t in (type(list) == 'array' ? list : [])) {
        let w = TECH[t];
        if (w && index(out, w) < 0) push(out, w);
    }
    return join(' / ', out);
}
const REGISTRATION = {home:['home network', 'домашняя сеть'], roaming:['roaming', 'роуминг'], searching:['searching', 'поиск сети'],
                      denied:['registration denied', 'регистрация отклонена'], idle:['not searching', 'поиск не выполняется']};
// after «Стабильность:» and «восстановление:», so in lower case: ucode's lc() lowers ASCII only
const STABILITY = {stable:['stable', 'стабильно'], degraded:['errors detected', 'зафиксированы сбои'],
                   flapping:['frequent reconnections', 'частые переподключения']};
const RECOVERY = {running:['recovery step running', 'выполняется шаг'], cooldown:['cooldown after error', 'пауза после ошибки'],
                  manual_required:['manual check required', 'нужна ручная проверка']};

function joined(parts, sep) {
    let out = [];
    for (let p in parts) if (p != null && p != '') push(out, p);
    return join(sep || ' · ', out);
}

// « · MTS RUS · 82%», or nothing when there is nothing after the name
function sep(text) { return text != '' ? ' · ' + text : ''; }

// a modem's name for a narrow screen: ModemManager's «T99W175 - QMI Composition» is the T99W175 (the owner's screenshot,
// 2026-11-15: the names did not fit the buttons)
function shortName(name) {
    let n = label(name);
    return type(n) == 'string' ? replace(n, /\s*[-–]\s*[A-Za-z0-9+]+\s+Composition\s*$/, '') : n;
}
// tunnel: the direct call reached a fake-IP, the router's own tunnel carried it
function routeWords(route, tunnel) {
    if (!route) return T('no route', 'нет маршрута');
    if (route.source == 'direct' && tunnel) return T('via router tunnel (sing-box)', 'через туннель роутера (sing-box)');
    if (route.source == 'direct') return T('direct', 'напрямую');
    if (route.source == 'emergency') return T('via fallback IP', 'через резервный адрес');
    if (route.source == 'modem') return T('via modem "', 'через модем «') + (label(route.title) || label(substr(route.name, 6))) + T('”', '»');
    return T('via proxy (', 'через прокси (') + (label(route.title) || label(route.name)) + ')';
}

// the main link: the carrying uplink's modem
function mainId(s) {
    let c = s.uplinks?.carrier;
    if (!c) return null;
    // by the network device the default route goes through; the name only when no device matched and it is one modem's
    let named = [];
    for (let id in s.ids) {
        let d = s.devices[id];
        if (c.device && d.session.device == c.device) return id;
        if (c.name && d.name == c.name) push(named, id);
    }
    return length(named) == 1 ? named[0] : null;
}

// a channel by the name the modem list gives its modem (the owner, 2026-11-03: «QModem Enabled», «T77W968 - MBIM
// Composition» and a bare «eth2» in «Каналы»): the modem's own name, else its model; a wired WAN by its interface
function channelName(s, p) {
    let d = p.id && type(s.devices) == 'object' ? s.devices[p.id] : null;
    if (d && d.name) return d.name;
    for (let id, x in (type(s.devices) == 'object' ? s.devices : {}))
        if (x?.name && p.device && x.session?.device == p.device) return x.name;
    if (p.interface) return p.interface + (p.device ? ' (' + p.device + ')' : '');
    return p.name || p.device;
}

// a modem's state at a glance: frozen, absent, connected, other
// connected is green only with the internet through it (the owner, 2026-10-30: «китайский не готов но почему-то
// зеленый»): a session the check finds dead or limited is yellow, as the LuCI card's «Резервный канал, не готов»
function dot(d) {
    let reach = d.uplink?.reach, role = d.uplink?.role;
    // connected with the internet not checked yet: neutral, not a green it has not earned (audit 12)
    return d.frozen ? '🧊' : !d.present ? '⚫' : d.session.state == 'connected' ?
           (reach == 'unavailable' || reach == 'limited' || role == 'backup_not_ready' || role == 'no_internet' ||
            role == 'limited' || role == 'backup_limited' ? '🟡' : !reach ? '⚪' : '🟢') :
           d.session.state == 'connecting' ? '🟡' : '🔴';
}
const NET_DOT = {available:'🟢', limited:'🟡', unavailable:'🔴'};
// the internet line: limited directly while a tunnel carries the rest says so, green (r96; the dashboard says the same)
// two lines, not one long one wrapped in the chat (the owner 2026-10-28)
function internetLine(u) {
    if (u.internet == 'limited' && u.tunnel?.ok)
        return T('Internet: ', 'Интернет: ') + '🟢 ' + T('via tunnel', 'через туннель') +
            (u.tunnel.delay_ms != null ? ' (' + u.tunnel.delay_ms + T(' ms', ' мс') + ')' : '') + '\n' +
            T('Directly: ', 'Напрямую: ') + '🟡 ' + T('whitelist only', 'только белый список');
    return T('Internet: ', 'Интернет: ') + (NET_DOT[u.internet] || '⚪') + ' ' + stateWord(u.internet);
}

// a channel in a few words: its place and what answers through it, the whitelist said once (the owner 2026-10-28:
// «зачем по два раза писать про ограниченный БС»)
const PLACE = {carrying:['primary', 'основной'], limited:['primary', 'основной'], no_internet:['primary', 'основной'],
               backup_ready:['backup', 'резервный'], backup_limited:['backup', 'резервный'], backup_not_ready:['backup', 'резервный'],
               checking:['checking connectivity', 'проверка связи'], down:['not connected', 'не подключён']};
const REACH = {available:['online', 'интернет есть'], limited:['whitelist only', 'только белый список'],
               unavailable:['no internet', 'интернета нет']};
function channelWords(role, reach) {
    let place = PLACE[role] ? W(PLACE[role]) : null;
    let what = role == 'down' || role == 'checking' ? null : REACH[reach] ? W(REACH[reach]) :
               role == 'backup_not_ready' || role == 'no_internet' ? W(REACH.unavailable) : null;
    return joined([place, what], ', ') || roleWord(role);
}

// the router reaches the internet through another link while some answer only the whitelist (the owner's status,
// 2026-10-28: «о белых списках не пишет»): which ones, by their interface names
function limitedLine(u) {
    if (u.internet == 'limited') return null;
    let names = [];
    for (let p in (u.paths || [])) if (p.reach == 'limited' && length(names) < 4) push(names, p.name || p.device);
    return length(names) ? '🟡 ' + T('Whitelist only: ', 'Только белый список: ') + esc(join(', ', names)) : null;
}

// ▂▄▆█ from the signal quality: four steps, the empty ones as a low bar
function bars(q) {
    if (q == null) return '';
    let n = q >= 75 ? 4 : q >= 50 ? 3 : q >= 25 ? 2 : q > 0 ? 1 : 0, full = ['▂', '▄', '▆', '█'], out = '';
    for (let i = 0; i < 4; i++) out += i < n ? full[i] : '▁';
    return out;
}
// the signal in percent as LuCI's basic card says it (common.js signalPct, the owner 2026-11-12: «проценты в боте
// показываем тоже?»): the meters' fills of RSRP (0.6) and SINR (0.4), kept inside the quarter of the RSRP zone, heavy
// noise (SINR below 0) in the lowest; ModemManager's own percent only without an RSRP. null when not registered
const SIG_CUTS = {lte:{rsrp:[-110, -100, -90], sinr:[0, 13, 20]}, nr:{rsrp:[-115, -105, -95], sinr:[0, 13, 20]}};
const SIG_SPAN = {rsrp:[-140, -60], sinr:[-10, 30]};
function sigFill(kind, v, rat) {
    let c = SIG_CUTS[rat][kind], sp = SIG_SPAN[kind];
    let pts = [sp[0] < c[0] - 1 ? sp[0] : c[0] - 1, c[0], c[1], c[2], sp[1] > c[2] + 1 ? sp[1] : c[2] + 1];
    if (v <= pts[0]) return 2;
    if (v >= pts[4]) return 100;
    for (let i = 0; i < 4; i++)
        if (v < pts[i + 1]) { let f = 25 * i + 25 * (v - pts[i]) / (pts[i + 1] - pts[i]); return f < 2 ? 2 : f; }
    return 100;
}
function sigPct(d) {
    let sg = d?.signal || {};
    if (!(d?.registration == 'home' || d?.registration == 'roaming')) return null;
    let rat = sg.lte_rsrp_dbm != null ? 'lte' : sg.nr_rsrp_dbm != null ? 'nr' : null;
    if (!rat) return sg.quality_percent != null ? sg.quality_percent : null;
    let rsrp = rat == 'lte' ? sg.lte_rsrp_dbm : sg.nr_rsrp_dbm, snr = rat == 'lte' ? sg.lte_sinr_db : sg.nr_sinr_db;
    let c = SIG_CUTS[rat].rsrp, z = rsrp < c[0] ? 0 : rsrp < c[1] ? 1 : rsrp < c[2] ? 2 : 3;
    if (snr != null && snr < 0) z = 0;
    let p = snr == null ? sigFill('rsrp', rsrp, rat) : 0.6 * sigFill('rsrp', rsrp, rat) + 0.4 * sigFill('sinr', snr, rat);
    p = p < 25 * z ? 25 * z : p > 25 * z + 25 ? 25 * z + 25 : p;
    p = int(p + 0.5);
    return p < 1 ? 1 : p;
}
// when the page was read, on the router's clock: a message stays as it was until «Обновить», so the time and not an age
// that would go stale (second audit 2026-10-21: a screen should say how fresh it is)
function freshWords(now) {
    let t = localtime(now || time());
    return I(sprintf(T('🕒 Updated at %02d:%02d:%02d', '🕒 Обновлено в %02d:%02d:%02d'), t.hour, t.min, t.sec));
}
// the last data kept while the service does not answer: their time, and that it is not now (audit R07)
function staleWords(readAt) {
    if (type(readAt) != 'int' || readAt <= 0) return I(T('⚠️ Waiting for data from MedveModem', '⚠️ Данных от службы MedveModem ещё нет'));
    let t = localtime(readAt);
    return I(sprintf(T('⚠️ Snapshot from %02d:%02d:%02d · MedveModem not responding',
                       '⚠️ Данные на %02d:%02d:%02d · служба MedveModem сейчас не отвечает'), t.hour, t.min, t.sec));
}

// «45 с», «2 мин», «1 ч 05 мин»; «45 s», «2 min», «1 h 05 min»
function duration(sec) {
    sec = type(sec) == 'int' || type(sec) == 'double' ? int(sec) : 0;
    if (sec < 60) return sec + T(' s', ' с');
    let min = int((sec + 30) / 60);
    if (min < 60) return min + T(' min', ' мин');
    return int(min / 60) + T(' h ', ' ч ') + sprintf('%02d', min % 60) + T(' min', ' мин');
}

// «📅 За сутки: интернет пропадал 2 раза (14 мин) · белые списки 5 ч 10 мин · перезапуск MedveModem 1 · ModemManager
// перезапускался 1 раз»; «с 09:15» while the bot has not counted a whole day (the owner, 2026-10-28)
function dayLine(d, now) {
    if (!d) return null;
    now = now || time();
    let parts = [];
    if (d.down.count || d.down.sec >= 60)
        push(parts, T('the internet was lost ', 'интернет пропадал ') +
             plural(d.down.count || 1, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')) + ' (' + duration(d.down.sec) + ')');
    if (d.main?.switches) push(parts, T('the main channel changed ', 'основной канал менялся ') +
                               plural(d.main.switches, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')));
    if (d.usb?.gone) push(parts, T('modems dropped off USB ', 'модемы отключались от USB ') +
                          plural(d.usb.gone, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')));
    if (d.limited.sec >= 60) push(parts, T('whitelist only ', 'белые списки ') + duration(d.limited.sec));
    if (d.svc.down) push(parts, T('the MedveModem service did not answer ', 'служба MedveModem не отвечала ') +
                            plural(d.svc.down, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')));
    if (d.svc.restarts) push(parts, T('MedveModem restarted ', 'MedveModem перезапускался ') +
                                plural(d.svc.restarts, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')));
    if (d.mm.down) push(parts, T('ModemManager stopped ', 'ModemManager останавливался ') +
                           plural(d.mm.down, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')));
    if (d.mm.restarts) push(parts, T('ModemManager restarted ', 'ModemManager перезапускался ') +
                               plural(d.mm.restarts, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')));
    if (d.qm?.down) push(parts, T('QModem stopped ', 'QModem останавливался ') +
                            plural(d.qm.down, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')));
    if (d.qm?.restarts) push(parts, T('QModem restarted ', 'QModem перезапускался ') +
                                plural(d.qm.restarts, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')));
    let head;
    if (now - d.since >= 86400 - 120) head = T('📅 The last 24 h: ', '📅 За сутки: ');
    else {
        let t = localtime(d.since);
        head = sprintf(T('📅 Since %02d:%02d: ', '📅 С %02d:%02d: '), t.hour, t.min);
    }
    return head + (length(parts) ? join(' · ', parts) : T('no issues', 'без сбоев'));
}

function hhmm(t) { let x = localtime(t); return sprintf('%02d:%02d', x.hour, x.min); }

// the backups in the operator's priority, one a line (the owner, 2026-11-05: «не структурированно и захламлено»): what each
// is ready for, and why not when the engine knows (no SIM by the stick's own web, r154)
const BACKUP_WORD = {backup_ready:['ready', 'готов'], backup_limited:['whitelist only', 'только белый список'],
                     backup_not_ready:['not ready', 'не готов']};
const WHY_WORD = {no_sim:['no SIM card', 'нет SIM-карты'], bad_sim:['SIM card unreadable', 'SIM-карта не читается'],
                  no_network:['no mobile network', 'нет сети оператора'],
                  restricted:['the operator restricted access', 'оператор ограничил доступ']};
// the operators' names by their code (MCC+MNC), as LuCI's badge names them
const SIM_OPS = {'25001':['MTS', 'МТС'], '25002':['MegaFon', 'МегаФон'], '25099':['Beeline', 'Билайн'], '25020':['T2', 'T2'],
                 '25011':['Yota', 'Yota'], '25035':['Motiv', 'Мотив'], '25039':['Rostelecom', 'Ростелеком'],
                 '25062':['T-Mobile', 'Т-Мобайл'], '25050':['SberMobile', 'СберМобайл']};
function backupLines(s) {
    let list = [];
    for (let p in (type(s.uplinks?.paths) == 'array' ? s.uplinks.paths : [])) if (BACKUP_WORD[p.role]) push(list, p);
    list = sort(list, (a, b) => (a.metric ?? 1000000) - (b.metric ?? 1000000));
    let lines = [];
    for (let i = 0; i < length(list) && i < 6; i++) {
        let p = list[i], word = (p.role == 'backup_not_ready' || p.why == 'restricted') && WHY_WORD[p.why] ? W(WHY_WORD[p.why]) : W(BACKUP_WORD[p.role]);
        push(lines, '   ' + (i + 1) + '. ' + esc(channelName(s, p)) + ' — ' + (p.role == 'backup_ready' ? '🟢 ' : '🟡 ') + word);
    }
    if (length(list) > 6) push(lines, '   ' + T('and ', 'и ещё ') + (length(list) - 6));
    return length(lines) ? T('🔁 Backups by priority:', '🔁 Резерв по порядку:') + '\n' + join('\n', lines) : null;
}
// kept for the old one-line form (notices): «Резерв: FM350 готов · EC25 только разрешённые сайты»
function backupLine(s) {
    let parts = [];
    for (let p in (type(s.uplinks?.paths) == 'array' ? s.uplinks.paths : []))
        if (BACKUP_WORD[p.role] && length(parts) < 4) push(parts, esc(channelName(s, p)) + ' ' + W(BACKUP_WORD[p.role]));
    return length(parts) ? T('Backup: ', 'Резерв: ') + join(' · ', parts) : null;
}

// the proxies whose last request failed, with the time, on a line of their own under Telegram's route
function proxyNote(s) {
    let failed = [];
    for (let n in (type(s.proxyNames) == 'array' ? s.proxyNames : [])) {
        let e = s.routeErrors?.[n];
        if (type(e?.at) == 'int') push(failed, '«' + esc(s.proxyTitles?.[n] || n) + '» ' + C(hhmm(e.at)));
    }
    return '   ' + (length(failed) ? T('a backup path: proxies did not answer: ', 'резервный маршрут: прокси не ответили: ') + join(', ', failed) :
                                     T('fallback route (proxies inactive)', 'резервный маршрут: прокси не используются'));
}

// s: {host, ids, devices, uplinks, route, engine, version}
// in blocks with a blank line between (the owner, 2026-11-05): who and when; the internet, the main channel and the
// backups; what needs attention; how Telegram is reached
function header(s) {
    let connected = 0, mid = mainId(s), main = mid ? s.devices[mid] : null;
    for (let id in s.ids) if (s.devices[id].session.state == 'connected') connected++;
    let carrier = s.uplinks.carrier, ms = main?.uplink?.latency_ms;
    let mainText = main ? B(main.name) + (main.operator ? ' (' + esc(main.operator) + ')' : '') +
                          (type(ms) == 'int' ? ' · ' + ms + T(' ms', ' мс') : '') :
                   carrier ? B(carrier.name || carrier.device) : null;
    let warn = [];
    // channel switching off while a backup could carry the traffic (2026-11-03) and the whitelist
    if (s.uplinks.switch_off && s.uplinks.internet == 'unavailable')
        push(warn, '⚠️ ' + T('Failover disabled: traffic remains on the failed uplink',
                              'Переключение каналов выключено: трафик остаётся на неработающем канале'));
    push(warn, limitedLine(s.uplinks));
    if (s.engine != 'ok') push(warn, T('⚠️ MedveModem service: ', '⚠️ Служба MedveModem: ') + stateWord(s.engine));
    let blocks = [
        joined(['📡 ' + B('MedveModem') + T(' · router “', ' · роутер «') + esc(label(s.host) || 'OpenWrt') + T('”', '»'), freshWords()], '\n'),
        joined([internetLine(s.uplinks),
                mainText ? T('➡️ Main: ', '➡️ Основной: ') + mainText : null,
                backupLines(s),
                T('📶 Modems: ', '📶 Модемы: ') + B(length(s.ids)) + (length(s.ids) ? T(', connected ', ', подключено ') + B(connected) : '')], '\n'),
        joined(warn, '\n'),
        // the proxies come first: off them only after they failed, back after two good probes. What is said is what was
        // seen (audit R06): the proxy whose request failed and when
        joined(['💬 Telegram: ' + esc(routeWords(s.route, s.tunnel)),
                s.proxies && s.route && !s.route.proxy ? proxyNote(s) : null], '\n')];
    return joined(blocks, '\n\n');
}


// the memory the services take (RSS), MedveModem's engine, ModemManager, QModem when it is used, the bot itself (the
// owner, 2026-10-30), with each one's restarts in the last 24 h as the engine counted them
function memLine(m, noRestarts) {
    if (type(m) != 'object') return null;
    let mb = (kb) => sprintf('%.1f', kb / 1024.0);
    let parts = [];
    for (let k in [['engine', 'MedveModem'], ['modemmanager', 'ModemManager'], ['qmodem', 'QModem'], ['bot', T('bot', 'бот')]]) {
        let e = m[k[0]];
        if (type(e) != 'object') continue;
        if (e.running === false) { push(parts, k[1] + ' ' + T('not running', 'не работает')); continue; }
        if (type(e.rss_kb) != 'int') continue;
        push(parts, k[1] + ' ' + C(replace(mb(e.rss_kb), '.', T('.', ',')) + T(' MB', ' МБ')) +
             (!noRestarts && e.restarts_24h > 0 ? ' (' + T('restarts in 24 h: ', 'перезапусков за сутки: ') + e.restarts_24h + ')' : ''));
    }
    return length(parts) ? T('🧠 Memory: ', '🧠 Память: ') + join(' · ', parts) : null;
}

// the router's own trouble in the kernel's words: what it means for the modems and what to do
const KERNEL_WORDS = {
    'hc-died':['the USB controller %s died: the modems on it are gone until the router restarts', 'сбой USB-контроллера %s: модемы на нём недоступны до перезагрузки роутера'],
    voltage:['the board could not set a voltage (%s): usually the power supply or the power chip', 'не удалось установить напряжение питания (%s): проверьте блок питания или контроллер питания платы'],
    undervoltage:['the supply voltage sags (%s): a weak power supply or cable', 'падение напряжения питания (%s): проверьте блок питания и кабель'],
    stall:['a processor core hung', 'зависло ядро процессора'], lockup:['a processor core hung', 'зависло ядро процессора'],
    oops:['a kernel error (oops)', 'сбой ядра (oops)'], panic:['a kernel panic', 'аварийная остановка ядра'],
    overheat:['critical temperature (%s)', 'критическая температура (%s)'], 'hung-task':['a task hung for minutes (%s)', 'задача зависла на несколько минут (%s)'],
    oom:['out of memory: the kernel stopped %s', 'не хватило памяти: ядро остановило %s'], 'fs-error':['storage errors (%s)', 'ошибки накопителя (%s)']
};
function kernelNotice(e) {
    let w = KERNEL_WORDS[e.kind] || [e.kind, e.kind];
    let s = replace(replace(replace(T(w[0], w[1]), '%s', e.who || ''), ' ()', ''), /  +/g, ' ');
    s = uc(substr(s, 0, 1)) + substr(s, 1);
    return (e.critical ? '🔥 ' + B(T('The router itself is in trouble', 'Сбой самого роутера')) + '\n' + esc(s) + '\n' +
            T('This is the hardware or the power, not the modems. Save logread and dmesg, restart the router; if it comes back: a power supply with a margin, a powered USB hub for the modems, cooling.',
              'Сбой оборудования роутера или питания. Сохраните журналы logread и dmesg, затем перезагрузите роутер. Если сбой повторится, проверьте блок питания, питание USB-хаба и охлаждение.')
                       : '⚠️ ' + esc(s));
}

// a restart or a stop of ModemManager or QModem as it happens: who did it, and how many in the day
function serviceNotice(e) {
    let name = e.name == 'qmodem' ? 'QModem' : 'ModemManager';
    let ours = e.by == 'medvemodem';
    if (e.what == 'stopped')
        return '⚠️ ' + sprintf(T('%s stopped', '%s остановлен'), name) +
               (ours ? T(' (MedveModem restarts it)', ' (его перезапускает MedveModem)') : T('(external)', ': вне MedveModem'));
    return (ours ? '🔄 ' : '⚠️ ') + sprintf(ours ? T('%s restarted by MedveModem', '%s перезапущен MedveModem') :
                                           T('%s restarted outside MedveModem (crash or manual)', '%s перезапущен вне MedveModem (после сбоя или вручную)'), name) +
           (e.restarts_24h > 1 ? ' · ' + T('in 24 h: ', 'за сутки: ') + e.restarts_24h : '');
}

function status(s, r) {
    // the bot apart from the service only when they differ: one build is installed as a whole (the owner's r153 engine
    // beside an r146 bot, 2026-11-05)
    let other = s.bot_version && s.version && s.bot_version != s.version;
    let svc = joined(['⚙️ ' + T('MedveModem service: ', 'Служба MedveModem: ') + stateWord(s.engine) +
                      (s.version ? T(' · version ', ' · версия ') + C(s.version) : ''),
                      s.bot_version ? T('Bot: version ', 'Бот: версия ') + C(s.bot_version) +
                          (other ? ' ⚠️ ' + T('version mismatch with service — update both packages', 'версия не совпадает со службой: обновите оба пакета') : '') : null,
                      memLine(s.mem, !!s.day)], '\n');
    let text = joined([header(s), s.day ? esc(dayLine(s.day)) : null, svc], '\n\n');
    if (!r) return text;
    // one division a line: the pins' regex check reads «/ … /» on one line as a literal
    let up = r.uptime_s ?? 0, d = int(up / 86400);
    let h = int((up % 86400) / 3600);
    let m = int((up % 3600) / 60);
    if (r.uptime_s == null) up = null;
    let lines = ['🖥 ' + B(T('Router', 'Роутер')),
        joined([r.model ? C(r.model) : null, r.os ? esc(r.os) : null]),
        joined([up != null ? T('Up ', 'Время работы ') + (d ? d + T(' d ', ' д ') + h + T(' h', ' ч') : h + T(' h ', ' ч ') + m + T(' min', ' мин')) : null,
                r.load ? T('load ', 'нагрузка ') + C(r.load) : null]),
        r.mem_free_mb != null ? T('Memory: free ', 'Память: свободно ') + C(r.mem_free_mb + (r.mem_total_mb ? T(' of ', ' из ') + r.mem_total_mb : '') + T(' MB', ' МБ')) : null,
        joined([r.lan ? 'LAN ' + C(r.lan) : null, length(r.tunnels) ? T('tunnels: ', 'туннели: ') + esc(join(', ', r.tunnels)) : null])];
    return text + '\n\n' + joined(lines, '\n');
}

// the main menu under the summary: every page is a button, a press edits this very message
function menu() {
    return {inline_keyboard:[[{text:T('📶 Modems', '📶 Модемы'), callback_data:'l'}, {text:T('🌐 Channels', '🌐 Каналы'), callback_data:'v:u'}],
                             [{text:T('📊 Data caps', '📊 Лимиты трафика'), callback_data:'v:q'}, {text:T('🧾 Journal', '🧾 Журнал'), callback_data:'v:j'}],
                             [{text:T('✉️ Incoming SMS', '✉️ Входящие SMS'), callback_data:'v:i'}, {text:T('📜 Download log', '📜 Скачать журнал'), callback_data:'v:l'}],
                             [{text:T('🌍 Network', '🌍 Сеть'), callback_data:'w'}, {text:T('🚧 Whitelists', '🚧 Белые списки'), callback_data:'bl'}],
                             [{text:T('⚙️ Settings', '⚙️ Настройки'), callback_data:'g'}],
                             [{text:T('🔄 Refresh', '🔄 Обновить'), callback_data:'v:m'}]]};
}
// a page reached from the menu: back to it, or refresh this one
function back(self) {
    return {inline_keyboard:[[{text:T('🔄 Refresh', '🔄 Обновить'), callback_data:self}, {text:T('🏠 Menu', '🏠 Меню'), callback_data:'v:m'}]]};
}
// the journal page: the system log as a file beside it (the owner 2026-10-28: «где получить лог по модемам»)
function journalKeys() {
    let k = back('v:j');
    push(k.inline_keyboard, [{text:T('📜 Modem log file', '📜 Скачать журнал модемов'), callback_data:'v:l'}]);
    push(k.inline_keyboard, [{text:T('📜 Full router log', '📜 Весь журнал роутера'), callback_data:'v:a'}]);
    return k;
}
// the channels page: the failover exclusions live here, not on the top menu (the audit: they need the routing policy)
function uplinkKeys() {
    let k = back('v:u');
    push(k.inline_keyboard, [{text:T('🚫 Failover exclusions', '🚫 Исключения автопереключения'), callback_data:'x:0'}]);
    return k;
}

// the SIM in words: «Работа» +79161234567
function simWords(d) {
    let sim = d.sim || {};
    return joined([sim.name ? T('“', '«') + esc(sim.name) + T('”', '»') : null, sim.number ? C(sim.number) : null], ' ');
}

const PER_PAGE = 8;
// a pager row: ◀ n/N ▶, the arrows only where there is somewhere to go
function pager(page, pages, to) {
    let row = [];
    if (page > 0) push(row, {text:'◀', callback_data:to(page - 1)});
    push(row, {text:(page + 1) + '/' + pages, callback_data:'n'});
    if (page < pages - 1) push(row, {text:'▶', callback_data:to(page + 1)});
    return row;
}

function modemList(s, page) {
    let n = length(s.ids);
    if (!n) return {text:'📶 ' + B(T('Modems', 'Модемы')) + T('\nNo modems found, or MedveModem service is not responding', '\nМодемов нет или служба MedveModem не отвечает'), markup:back('l')};
    let pages = int((n + PER_PAGE - 1) / PER_PAGE), mid = mainId(s);
    page = page > 0 && page < pages ? page : 0;
    let lines = ['📶 ' + B(T('Modems', 'Модемы')) + ' (' + n + ')'], rows = [];
    for (let i = page * PER_PAGE; i < n && i < (page + 1) * PER_PAGE; i++) {
        let id = s.ids[i], d = s.devices[id];
        push(lines, (i + 1) + '. ' + dot(d) + ' ' + B(d.name) + (id == mid ? ' ➡️' : '') +
             (d.present ? sep(joined([esc(d.operator), sigPct(d) != null ? sigPct(d) + '%' : null])) :
                          T(' · unplugged from USB', ' · отключён от USB')));
        if (simWords(d)) push(lines, '    SIM ' + simWords(d));
        push(rows, [{text:cut((i + 1) + '. ' + dot(d) + ' ' + d.name + (id == mid ? ' ➡️' : ''), 40), callback_data:'m:' + shortKey(d)}]);
    }
    if (pages > 1) push(rows, pager(page, pages, (p) => 'l:' + p));
    push(rows, [{text:T('🔄 Refresh', '🔄 Обновить'), callback_data:page ? 'l:' + page : 'l'}, {text:T('🏠 Menu', '🏠 Меню'), callback_data:'v:m'}]);
    push(lines, I(T('Tap a modem to open its card', 'Нажмите на модем — откроется его карточка')));
    return {text:join('\n', lines), markup:{inline_keyboard:rows}};
}

// a SIM's limit in the operator's decimal units, as the engine and LuCI count it (audit R09, 2026-10-30: 50 GB read 47)
function bytesDec(n) {
    if (type(n) != 'int' && type(n) != 'double' || n < 0) return null;
    let units = T(['B', 'KB', 'MB', 'GB', 'TB'], ['Б', 'КБ', 'МБ', 'ГБ', 'ТБ']), i = 0;
    while (n >= 1000 && i < length(units) - 1) { n /= 1000.0; i++; }
    let s = i && n < 10 ? replace(sprintf('%.1f', n), '.', T('.', ',')) : sprintf('%d', n + 0.5);
    return s + ' ' + units[i];
}

function quotaWords(q) {
    let used = bytesDec(q.used_bytes), limit = bytesDec(q.limit_bytes);
    let until = q?.resets_on ? T('until ', 'до ') + substr(q.resets_on, 8, 2) + '.' + substr(q.resets_on, 5, 2) : null;
    return joined([used ? used + (limit ? T(' of ', ' из ') + limit : '') : null, quotaWord(q.state), until]);
}

function signalWords(sig, pct) {
    return joined([pct != null ? bars(pct) + ' ' + pct + '%' : null,
                   sig.lte_rsrp_dbm != null ? 'LTE RSRP ' + sig.lte_rsrp_dbm + T(' dBm', ' дБм') : null,
                   sig?.lte_sinr_db != null ? 'SINR ' + sig.lte_sinr_db + T(' dB', ' дБ') : null,
                   sig.nr_rsrp_dbm != null ? 'NR RSRP ' + sig.nr_rsrp_dbm + T(' dBm', ' дБм') : null,
                   sig?.nr_sinr_db != null ? 'NR SINR ' + sig.nr_sinr_db + T(' dB', ' дБ') : null]);
}
// the noise in words from the SINR (LuCI's cuts: 20+ low, 13+ moderate, 0+ noticeable, below high)
function noiseWords(sig) {
    let s = sig?.lte_sinr_db != null ? sig.lte_sinr_db : sig?.nr_sinr_db;
    if (s == null) return null;
    return s >= 20 ? T('low noise', 'слабые помехи') : s >= 13 ? T('moderate noise', 'умеренные помехи') :
           s >= 0 ? T('noticeable noise', 'заметные помехи') : T('high noise', 'сильные помехи');
}
const ACTION_WORDS = {drivers:['a driver is missing', 'не хватает драйвера'], modemmanager:['ModemManager is not installed', 'не установлен ModemManager'],
    'modeswitch-rule':['usb-modeswitch rule resets modem', 'правило usb-modeswitch сбрасывает модем'],
    'modeswitch-pending':['modem in mass-storage mode', 'модем в режиме USB-накопителя'],
    'sim-pin':['SIM requires PIN', 'для SIM-карты требуется PIN-код'], 'sim-puk':['the SIM card is locked, a PUK is needed', 'SIM-карта заблокирована, нужен PUK-код'],
    'fcc-unlock':['radio off (FCC unlock required)', 'радио выключено, нужна FCC-разблокировка'],
    'foreign-interface':['managed by external interface', 'модемом управляет интерфейс, настроенный вне MedveModem']};


// the day's time without the internet (the engine's downtime, r109): «без интернета 12 мин (2 раза) · только БС 1 ч 05 мин»;
// nothing on a day that had neither
function downtimeWords(t) {
    if (!t) return null;
    let off = (t.none || 0) + (t.off || 0);
    let parts = [];
    if (off >= 60 || t.outages) push(parts, T('without the internet ', 'без интернета ') + duration(off) +
        (t.outages ? ' (' + plural(t.outages, T('time', 'раз'), T('times', 'раза'), T('times', 'раз')) + ')' : ''));
    if (t.current) push(parts, T('down now for ', 'отсутствует уже ') + duration(t.current));
    if ((t.limited || 0) >= 60) push(parts, T('whitelist only ', 'только белый список ') + duration(t.limited));
    if (!length(parts)) return (t.ok || 0) >= 60 ? T('no outages recorded', 'за время наблюдения — без сбоев') : null;
    return join(' · ', parts);
}

// a card: ctx {n, total, prev, next (keys), main (bool), control (bool), now}; the arrows go round the modems
function card(d, ctx) {
    ctx = ctx || {};
    let key = shortKey(d), st = stamp(ctx.now), rows = [];
    let lines = ['📶 ' + B(d.name) + (d.model && d.model != d.name ? ' (' + esc(d.model) + ')' : '') + (ctx.main ? ' ➡️' : '')];
    if (!d.present) push(lines, T('⚫ Modem unplugged from USB', '⚫ Модем отключён от USB'));
    else {
        // last known values are shown grey, not as a current green (audit R07)
        push(lines, B(T('Connection:', 'Подключение:')) + ' ' + (ctx.stale ? '⚪' : dot(d)) + ' ' + joined([stateWord(d.session.state), esc(d.session.interface)]) +
             (ctx.stale ? T(' (last known)', ' (последнее известное)') : ''));
        if (d.uplink.role) push(lines, B(T('Channel:', 'Канал:')) + ' ' + (ctx.main ? '➡️ ' : '') + channelWords(d.uplink.role, d.uplink.reach));
        if (simWords(d)) push(lines, B('SIM:') + ' ' + simWords(d));
        let net = joined([esc(d.operator), techWords(d.access_tech), W(REGISTRATION[d.registration])]);
        // an MVNO's SIM on another network (the owner, 2026-11-05: Yota's SIM, the network MegaFon): the SIM's operator too
        if (d.sim_operator_code && d.sim_operator_code != d.operator_code)
            net = joined([net, T('SIM: ', 'SIM: ') + (SIM_OPS[d.sim_operator_code] ? W(SIM_OPS[d.sim_operator_code]) : d.sim_operator_code)]);
        if (net) push(lines, B(T('Network:', 'Сеть:')) + ' ' + net);
        let sig = signalWords(d.signal, sigPct(d));
        if (sig) push(lines, B(T('Signal:', 'Сигнал:')) + ' ' + joined([sig, noiseWords(d.signal)]));
        // the internet check of this modem: its delay, or that it does not answer (audit 9.3)
        if (d.uplink?.reach == 'available' && d.uplink?.latency_ms != null)
            push(lines, B(T('Internet:', 'Интернет:')) + ' ' + d.uplink.latency_ms + T(' ms', ' мс'));
        else if (d.uplink?.redirect && d.uplink?.reach != 'available')
            // r156: plain HTTP goes to the operator's own page (a blocked or unpaid SIM, BUGREPORT 2026-11-05 §9)
            push(lines, B(T('Internet:', 'Интернет:')) + ' ⚠️ ' + T('the operator restricted access (sends to ', 'оператор ограничил доступ (перенаправляет на ') +
                 esc(d.uplink.redirect) + T('): check the balance', '): проверьте баланс'));
        else if (d.uplink?.reach == 'unavailable' && d.session?.state == 'connected')
            push(lines, B(T('Internet:', 'Интернет:')) + ' ' + T('not responding', 'не отвечает'));
        if (d.up_s != null && d.session?.state == 'connected') push(lines, B(T('Connected:', 'Подключён:')) + ' ' + duration(d.up_s));
        let dt = downtimeWords(d.downtime);
        if (dt) push(lines, B(T('Today:', 'Сегодня:')) + ' ' + dt);
        if (d.quota.state && d.quota.state != 'none') push(lines, B(T('Traffic limit:', 'Лимит трафика:')) + ' ' + quotaWords(d.quota));
        let health = joined([W(STABILITY[d.health.state]), RECOVERY[d.recovery] ? T('recovery: ', 'восстановление: ') + W(RECOVERY[d.recovery]) : null]);
        if (health) push(lines, B(T('Stability:', 'Стабильность:')) + ' ' + health);
    }
    if (d.frozen) push(lines, '🧊 ' + I(T('Management paused: services will not touch this modem', 'Управление приостановлено: службы не обращаются к модему')));
    // what only a person can fix, first thing to read (audit 10)
    if (type(d.action_needed) == 'array' && length(d.action_needed)) push(lines, '🛠 ' + B(T('Action required:', 'Требуется действие:')) + ' ' + join('; ', map(d.action_needed, (a) => W(ACTION_WORDS[a]))) +
                                       T(' (on the modem page in LuCI)', ' (на странице модема в LuCI)'));
    if (ctx.result) push(lines, '', ctx.result);
    push(lines, ctx.stale ? staleWords(ctx.readAt) : freshWords(ctx.now));
    if (ctx.control && d.present && !ctx.stale) {
        let row = [];
        if (!ctx.main && !d.frozen) push(row, {text:T('➡️ Set as primary', '➡️ Сделать основным'), callback_data:'a:mn:' + key + ':' + st});
        if (!d.frozen) push(row, d.session.state == 'connected' || d.session.state == 'connecting' ?
            {text:T('⏹ Disconnect', '⏹ Отключить'), callback_data:'a:dn:' + key + ':' + st} :
            {text:T('▶️ Connect', '▶️ Подключить'), callback_data:'a:up:' + key + ':' + st});
        if (length(row)) push(rows, row);
        row = [];
        if (!d.frozen) push(row, {text:T('🔁 Reconnect', '🔁 Переподключить'), callback_data:'a:rc:' + key + ':' + st});
        // the steps of LuCI's «Диагностика» a phone needs (the owner, 2026-11-15: the bot brought up to LuCI): re-register
        // and restart the modem, each asked first
        if (!d.frozen) push(row, {text:T('📡 Re-register', '📡 Перерегистрировать'), callback_data:'a:rr:' + key + ':' + st});
        if (length(row)) push(rows, row);
        if (!d.frozen) push(rows, [{text:T('♻️ Restart the modem', '♻️ Перезагрузить модем'), callback_data:'a:rs:' + key + ':' + st}]);
    }
    if (ctx.total > 1)
        push(rows, [{text:'◀', callback_data:'m:' + ctx.prev}, {text:ctx.n + '/' + ctx.total, callback_data:'n'},
                    {text:'▶', callback_data:'m:' + ctx.next}]);
    push(rows, [{text:T('🔄 Refresh', '🔄 Обновить'), callback_data:'m:' + key}, {text:T('📶 Modems', '📶 Модемы'), callback_data:'l'}]);
    // the freeze last, everywhere (the owner, 2026-10-21): the rarest and the most far-reaching
    if (ctx.control && d.present && !ctx.stale)
        push(rows, [d.frozen ? {text:T('☀️ Resume management', '☀️ Возобновить управление'), callback_data:'a:uf:' + key + ':' + st} :
                               {text:T('🧊 Pause management', '🧊 Приостановить управление'), callback_data:'a:fz:' + key + ':' + st}]);
    return {text:join('\n', lines), markup:{inline_keyboard:rows}};
}

// what an action does, for its confirmation and its result
// «main» is the first place among the modems: a wired WAN keeps its own place (audit R03, 2026-10-30; LuCI says the same)
const ASK = {mn:['Set "%s" as primary modem? Wired WAN keeps its priority.', 'Сделать «%s» основным модемом? Проводной WAN сохранит приоритет.'],
             dn:['Disconnect "%s"? It will stop carrying traffic until reconnected.',
                 'Отключить «%s»? Трафик через модем прекратится до повторного подключения.'],
             rc:['Reconnect "%s"? Connection will drop for a few seconds.', 'Переподключить «%s»? Связь прервётся на несколько секунд.'],
             rr:['Re-register “%s” in the network? The modem leaves the network and registers again: the internet through it stops for a few seconds.',
                 'Перерегистрировать «%s» в сети? Модем отключится от сети и зарегистрируется заново: интернет через него пропадёт на несколько секунд.'],
             rs:['Restart “%s”? The link through it breaks for 1–2 minutes while the modem starts again.',
                 'Перезагрузить модем «%s»? Связь через него пропадёт на 1–2 минуты, пока он запускается заново.'],
             fz:['Pause management for "%s"? Connection will drop; MedveModem, ModemManager, and QModem will release it.',
                 'Приостановить управление «%s»? Соединение разорвётся; MedveModem, ModemManager и QModem освободят устройство.']};
function confirm(d, op, now) {
    let key = shortKey(d);
    return {text:'❓ ' + B(T('Confirm', 'Подтвердите')) + '\n' + sprintf(W(ASK[op]), esc(d.name)),
            markup:{inline_keyboard:[[{text:T('✅ Yes', '✅ Да'), callback_data:'y:' + op + ':' + key + ':' + stamp(now)},
                                      {text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'m:' + key}]]}};
}
function asks(op) { return ASK[op] != null; }

// «20.10 15:03» on the router's clock (its time zone): when the modem got the message
function stampWords(epoch) {
    if (type(epoch) != 'int' || epoch <= 0) return '—';
    let t = localtime(epoch);
    return sprintf('%02d.%02d %02d:%02d', t.mday, t.mon, t.hour, t.min);
}

// the one-time code in a message (the owner, 2026-10-21: as a phone shows it), as LuCI's smsCode: 4–8 digits (also
// «123-456») next to «код», «code», «пароль», «password»; else the only such group of a short message that names a
// code. An amount («… р», «руб», «₽», «%») or a phone number is not one. null for none. Bytes: the words are UTF-8.
const CODE_BEFORE = /(код|Код|КОД|code|Code|CODE|пароль|Пароль|password|Password|kod|parol)[^0-9]*$/;
const CODE_AFTER = /^[^0-9]{0,40}(код|Код|code|Code)/;
const CODE_ANY = /(код|Код|КОД|code|Code|CODE|пароль|Пароль|password|Password|подтвержд|Подтвержд|verif|Verif|OTP|otp)/;
function smsCode(s) {
    if (type(s) != 'string') return null;
    let n = length(s), groups = [], i = 0, digit = (k) => k < n && ord(s, k) >= 48 && ord(s, k) <= 57;
    while (i < n) {
        if (!digit(i)) { i++; continue; }
        let j = i;
        while (digit(j)) j++;
        let code = substr(s, i, j - i), end = j;
        // «123-456», «123 456»
        if (j - i == 3 && (substr(s, j, 1) == '-' || substr(s, j, 1) == ' ') && digit(j + 1) && digit(j + 3) && !digit(j + 4)) {
            code += substr(s, j + 1, 3);
            end = j + 4;
        }
        let before = i > 0 ? substr(s, i - 1, 1) : '', after = substr(s, end, 12);
        if (length(code) >= 4 && length(code) <= 8 && (before == '' || index('+.,', before) < 0) &&
            !match(after, /^[.,][0-9]/) && !match(after, /^ ?(р\.|р$|р |р,|руб|₽|rub|RUB|%)/))
            push(groups, {code, at:i, end});
        i = end;
    }
    for (let g in groups)
        if (match(substr(s, g.at > 96 ? g.at - 96 : 0, g.at > 96 ? 96 : g.at), CODE_BEFORE) || match(substr(s, g.end, 64), CODE_AFTER))
            return g.code;
    return length(groups) == 1 && length(s) <= 320 && match(s, CODE_ANY) ? groups[0].code : null;
}

// the sender: the address book's name with the number beside it, else the number
function smsFrom(it) {
    let n = it.number || T('unknown', 'неизвестного');
    return it.contact ? it.contact + ' (' + n + ')' : n;
}

// incoming SMS, newest first: from whom, when, which modem and SIM got it, the text. items: [{d, number, text, epoch}]
function smsPage(items, allowed, note) {
    let lines = ['✉️ ' + B(T('Incoming SMS', 'Входящие SMS'))];
    if (!allowed) push(lines, T('SMS viewing in Telegram is disabled. Enable "Show SMS" on the bot page in LuCI.',
                               'Показ SMS в Telegram отключён. Включите «Показывать SMS» на странице бота в LuCI.'));
    else if (!length(items)) push(lines, note || T('No incoming SMS.', 'Входящих SMS нет.'));
    else {
        for (let it in items) {
            push(lines, '📨 ' + B(T('from ', 'от ') + smsFrom(it)) + ' · ' + C(stampWords(it.epoch)));
            push(lines, '📶 ' + esc(it.d.name) + (simWords(it.d) ? ' · SIM ' + simWords(it.d) : ''));
            push(lines, esc(cut(it.text || T('(no text)', '(без текста)'), 600)));
            // tapped, Telegram copies what is in <code>; the button under the page copies it too
            if (smsCode(it.text)) push(lines, T('🔑 Code: ', '🔑 Код: ') + C(smsCode(it.text)));
            push(lines, '');
        }
        if (note) push(lines, I(note));
        push(lines, I(T('Messages remain unread on the router', 'На роутере они остаются непрочитанными')));
    }
    return join('\n', lines);
}

// one new SMS as it came (r85): from whom, when, which modem and SIM, the text, the code. it: {d, number, text, epoch}
function smsNew(it) {
    let lines = ['✉️ ' + B(T('New SMS', 'Новое SMS')) + T(' · from ', ' · от ') + B(smsFrom(it)) + ' · ' + C(stampWords(it.epoch)),
                 '📶 ' + esc(it.d.name) + (simWords(it.d) ? ' · SIM ' + simWords(it.d) : ''),
                 esc(cut(it.text || T('(no text)', '(без текста)'), 3000))];
    if (smsCode(it.text)) push(lines, T('🔑 Code: ', '🔑 Код: ') + C(smsCode(it.text)));
    return join('\n', lines);
}

// the exclusion menu: every link the engine knows, ✅ the ones failover never makes main by itself.
// cands [{device, name}], draft {sel:{dev:true}, base:[...]}, the draft is applied at once
const EX_PER_PAGE = 8;
function excludePage(cands, draft, page, control, now) {
    let n = length(cands), pages = int((n + EX_PER_PAGE - 1) / EX_PER_PAGE) || 1;
    page = page > 0 && page < pages ? page : 0;
    let chosen = [], changed = false;
    for (let c in cands) if (draft.sel[c.device]) push(chosen, c.name || c.device);
    for (let c in cands) if (!!draft.sel[c.device] != (index(draft.base, c.device) >= 0)) changed = true;
    let lines = ['🚫 ' + B(T('Failover exclusions', 'Исключения автопереключения')),
                 T('Checked uplinks are never promoted by failover (manual switch only).',
                   'Отмеченные каналы не выбираются автопереключением — только вручную.'),
                 T('Saved: ', 'Сохранено: ') + B(length(draft.base)) + T(' · chosen: ', ' · выбрано: ') + B(length(chosen)) +
                     (changed ? ' · ' + B(T('unsaved changes', 'есть несохранённые изменения')) : ''),
                 length(chosen) ? T('Chosen: ', 'Выбор: ') + join(', ', map(chosen, (x) => esc(x))) :
                                  T('None excluded: all uplinks participate in failover.', 'Исключений нет: все каналы участвуют в автопереключении.')];
    if (!control) push(lines, I(T('Control disabled in LuCI (view-only).', 'Управление отключено в LuCI (только просмотр).')));
    let rows = [];
    for (let i = page * EX_PER_PAGE; i < n && i < (page + 1) * EX_PER_PAGE; i++) {
        let c = cands[i];
        push(rows, [{text:cut((draft.sel[c.device] ? '✅ ' : '⬜ ') + (c.name ? c.name + ' (' + c.device + ')' : c.device), 48),
                     callback_data:control && match(c.device, /^[A-Za-z0-9_.-]{1,15}$/) ?
                         'xt:' + c.device + ':' + page + ':' + sprintf('%x', (draft.started || 0) % 0xffffff) : 'n'}]);
    }
    if (!n) push(lines, T('No channels yet.', 'Каналов пока нет.'));
    if (pages > 1) push(rows, pager(page, pages, (p) => 'x:' + p));
    if (control) push(rows, [{text:changed ? T('✅ Apply (', '✅ Применить (') + length(chosen) + ')' : T('✅ Apply · no changes', '✅ Применить · нет изменений'),
                              callback_data:'xa:' + stamp(now)},
                             {text:T('← Without saving', '← Без сохранения'), callback_data:'xc'}]);
    push(rows, [{text:T('🏠 Menu', '🏠 Меню'), callback_data:'v:m'}]);
    return {text:join('\n', lines), markup:{inline_keyboard:rows}};
}

// «📊 Лимиты трафика»: with management allowed, a ✏️ per present modem (the owner, 2026-11-15)
function quotaKeys(s, control) {
    let rows = [], row = [];
    if (control) for (let id in s.ids) {
        let d = s.devices[id];
        if (!d?.present) continue;
        push(row, {text:'✏️ ' + shortName(d.name), callback_data:'qm:' + shortKey(d)});
        if (length(row) == 2) { push(rows, row); row = []; }
    }
    if (length(row)) push(rows, row);
    push(rows, ...back('v:q').inline_keyboard);
    return {inline_keyboard:rows};
}
function quotas(s) {
    let lines = ['📊 ' + B(T('Data caps', 'Лимиты трафика'))], n = 0, any = false;
    for (let id in s.ids) {
        let d = s.devices[id];
        n++;
        if (!d.quota.state || d.quota.state == 'none') continue;
        any = true;
        push(lines, n + '. ' + B(d.name) + ': ' + quotaWords(d.quota));
    }
    return any ? join('\n', lines) : '📊 ' + B(T('Data caps', 'Лимиты трафика')) +
        T('\nNo traffic limits set. They are set with ✏️ below or in LuCI: “Modem” → “SIM”.', '\nЛимиты трафика не заданы. Их задают кнопкой ✏️ ниже или в LuCI: «Модем» → «Настройки» → «SIM-карта».');
}

function uplinks(s) {
    let lines = ['🌐 ' + B(T('Channels', 'Каналы')), internetLine(s.uplinks)];
    for (let p in s.uplinks.paths)
        push(lines, (NET_DOT[p.reach] || '⚪') + ' ' + B(channelName(s, p)) + ' · ' + channelWords(p.role, p.reach));
    return join('\n', lines);
}


// an episode of episodes.uc in words: outage (count — seconds it lasts, 0 the moment it starts), recovery
// (count — how long it lasted), flapping (count — how many short ones); name is null for the router itself
// the words of LuCI's dictionary (audit r101: подключение, служба MedveModem; no «сессия», no «движок»)
const DOWN = {session:['Failed to restore connection', 'Не удаётся восстановить подключение'], present:['USB modem unplugged', 'USB-модем отключён'],
              reach:['Internet unavailable', 'Интернет недоступен'], engine:['MedveModem service not responding', 'Служба MedveModem не отвечает']};
const DOWN_NOW = {session:['Connection dropped', 'Подключение прервано'], present:['USB modem unplugged', 'USB-модем отключён'],
                  reach:['Internet unavailable', 'Интернет недоступен'], engine:['MedveModem service not responding', 'Служба MedveModem не отвечает']};
const BACK = {session:['Connection restored', 'Подключение восстановлено'], present:['USB modem reconnected', 'USB-модем снова подключён'],
              reach:['Internet back online', 'Интернет снова доступен'], engine:['MedveModem service responding again', 'Служба MedveModem снова отвечает']};
const SHORT = {session:['Connection', 'Подключение'], present:['USB modem', 'USB-модем'], reach:['Internet', 'Интернет'], engine:['MedveModem service', 'Служба MedveModem']};
// only the second group of addresses (ya.ru, vk.com by default) answers: limited access, its cause not proven (audit r101)
const LIMITED = {outage:['Limited internet access (whitelist)', 'Ограниченный доступ (белый список)'],
                 recovery:['Full internet access restored', 'Полный доступ к интернету восстановлен']};
function incidentText(action, kind, name, count, level) {
    let after = (n) => n > 0 ? T('. Down for: ', '. Перерыв: ') + duration(n) : '';
    let text = kind == 'reach' && level == 'limited' && LIMITED[action] ?
               W(LIMITED[action]) + (action == 'outage' ? (count > 0 ? ' ' + duration(count) : '') : after(count)) :
               action == 'outage' ? (count > 0 ? (W(DOWN[kind]) || T('No link', 'Связь недоступна')) + ' ' + duration(count) :
                                                 W(DOWN_NOW[kind]) || T('No link', 'Связь недоступна')) :
               action == 'recovery' ? (W(BACK[kind]) || T('Connection restored', 'Связь восстановлена')) + after(count) :
               action == 'flapping' ? (W(SHORT[kind]) || T('Link', 'Связь')) + T(': frequent brief drops (', ': частые короткие сбои (') + count + ')' : null;
    return text && name ? text + ' · ' + name : text;
}

// a notice as one situation (docs/BOT-NOTIFICATIONS-PLAN.md, 26.10): what happened, whether the internet works and
// through what, what is being done. ctx from main.uc: {backup (the modem does not carry the internet), via (the
// channel the internet goes through now, or null), internet (the engine's reach word), fixing (a recovery runs),
// fixed (the step after which the link came back, already in words)}
function notice(action, kind, name, count, level, ctx) {
    let text = incidentText(action, kind, name, count, level);
    if (!text || type(ctx) != 'object') return text;
    let lines = [text];
    let up = ctx.internet == 'available' || ctx.internet == 'up' || ctx.internet == 'ok';
    let via = ctx.via ? '«' + label(ctx.via) + '»' : null;
    let device = kind == 'session' || kind == 'present';
    if (action == 'outage' && device) {
        if (up && via) push(lines, T('The internet is available through ', 'Интернет доступен через ') + via + '.');
        else if (!up) push(lines, T('There is no internet now.', 'Интернета сейчас нет.'));
        if (ctx.fixing) push(lines, T('MedveModem is restoring the connection.', 'MedveModem восстанавливает подключение.'));
    } else if (action == 'outage' && kind == 'reach' && ctx.switchOff)
        push(lines, T('The backup channel «', 'Резервный канал «') + label(ctx.switchOff) +
             T('" is online, but failover is disabled — enable "Failover to working uplink" in MedveModem settings.',
               '» работает, но переключение каналов выключено. Включите «Переключать интернет на работающий канал» в настройках MedveModem.'));
    else if (action == 'recovery' && ctx.fixed)
        push(lines, T('Connection recovered after: "', 'Связь восстановлена после: «') + ctx.fixed + T('”.', '».'));	/* not «помогло»: the order only */
    else if (action == 'flapping' && device) {
        if (up && via) push(lines, T('The internet is available through ', 'Интернет доступен через ') + via + '.');
        /* a cause only where the USB itself came and went (audit r101: a session's flaps prove no cable) */
        if (kind == 'present') push(lines, T('Check the cable and the USB hub\'s power.', 'Проверьте кабель и питание USB-хаба.'));
    }
    let icon = action == 'recovery' ? '✅' : action == 'flapping' ? '〰️' :
               kind == 'reach' && level == 'limited' ? '🟡' : kind == 'reach' || !up ? '🔴' : '⚠️';
    return icon + ' ' + join('\n', lines);
}

// one message after the engine restarted (or was updated) instead of a «session connected» per modem
// info: {version, updated (the version changed), up (modems connected), total, via, internet}
function restartText(info) {
    let lines = ['🔄 ' + (info.updated && info.version ? T('MedveModem restarted (updated to ', 'MedveModem перезапущен (обновлён до ') + label(info.version) + ').' :
                          T('MedveModem restarted.', 'MedveModem перезапущен.'))];
    let up = info.internet == 'available' || info.internet == 'up' || info.internet == 'ok';
    let modems = T('Modems connected: ', 'На связи модемов: ') + (info.up || 0) + T(' of ', ' из ') + (info.total || 0);
    push(lines, modems + (up && info.via ? T(', internet via "', ', интернет через «') + label(info.via) + '».' :
                          up ? T(', internet online.', ', интернет работает.') : T(', no internet yet.', ', интернета пока нет.')));
    return join('\n', lines);
}

// the main link moved: from whom to whom, by the names the engine gives
// spec §44: the whitelist mode on (its server, what goes through it) or off (the manager's settings back)
function wlmodeText(w) {
    if (!w?.on)
        return '🛡 ' + T('Whitelist mode disabled; proxy manager settings restored', 'Режим белых списков выключен: исходные настройки менеджера восстановлены');
    let what = [];
    if (w.lan) push(what, T('the whole LAN', 'вся сеть'));
    if (w.router) push(what, T('the router', 'роутер'));
    return '🛡 ' + T('Whitelist mode enabled: server "', 'Режим белых списков включён: сервер «') + label(w.server || '—') + '»' +
           (type(w.delay_ms) == 'int' ? ' (' + w.delay_ms + T(' ms)', ' мс)') : '') +
           (length(what) ? T(', through it: ', ', через него: ') + join(T(' and ', ' и '), what) : '') +
           (w.manual ? T(' · turned on by hand', ' · включён вручную') : '');
}

function failoverText(from, to) {
    return T('Main channel: ', 'Основной канал: ') + (label(from) || T('none', 'нет')) + ' → ' + (label(to) || T('none', 'нет'));
}

// /events: newest first, the time on the router's clock (today without the date), the modem
// entries: [{t (unix), summary ([en, ru], or a string from a snapshot of before), name}]
function journal(entries, now) {
    let list = [];
    for (let e in entries) push(list, e);
    list = sort(list, (a, b) => (b.t || 0) - (a.t || 0));
    if (!length(list)) return '🧾 ' + B(T('Journal', 'Журнал')) + T('\nNo events yet.', '\nСобытий пока нет.');
    let today = localtime(now || time()), lines = ['🧾 ' + B(T('Latest events', 'Последние события'))];
    let shown = 0, seen = {};
    for (let i = 0; i < length(list) && shown < 10; i++) {
        let e = list[i], when = '—';
        // the same line twice within two minutes (a restart's journal read again, 28.10: every modem twice) once
        let words = (type(e.summary) == 'array' ? W(e.summary) : e.summary) + '|' + (e.name || '');
        if (seen[words] != null && seen[words] - (e.t || 0) <= 120) continue;
        seen[words] = e.t || 0;
        shown++;
        if (type(e.t) == 'int' && e.t > 0) {
            let lt = localtime(e.t);
            when = (lt.mday == today.mday && lt.mon == today.mon && lt.year == today.year ? '' :
                    sprintf('%02d.%02d ', lt.mday, lt.mon)) + sprintf('%02d:%02d', lt.hour, lt.min);
        }
        // the time and the modem, what happened on its own line: one long line wrapped badly in the chat
        push(lines, joined([C(when), e.name ? B(e.name) : null]) + '\n' + esc(type(e.summary) == 'array' ? W(e.summary) : e.summary));
    }
    return join('\n', lines);
}

function help(control) {
    return '❔ ' + B(T('Help', 'Справка')) + '\n' +
           T('Tap "Menu" next to the input bar for commands, or send /menu for the dashboard (modems, uplinks, traffic, logs).\n',
             'Кнопка «Меню» у поля ввода — список команд; /menu — сводка с меню: модемы, каналы, трафик, журнал.\n') +
           T('Tap any modem to open its card; use ◀ ▶ to switch between modems.\n',
             'В списке модемов нажмите на модем — откроется карточка; стрелки ◀ ▶ листают модемы.\n') +
           (control ? T('Card actions: set primary, connect/disconnect, reconnect, pause management. Disruptive actions require confirmation.\n',
                        'На карточке: сделать основным, подключить/отключить, переподключить, приостановить управление. ' +
                        'Перед действиями с разрывом связи бот запросит подтверждение.\n') :
                      T('Modem control is disabled (view-only). Enable it on the bot page in LuCI.\n',
                        'Управление модемами отключено (только просмотр); включается на странице бота в LuCI.\n')) +
           T('Download modem log: /log or the button below Journal.\n',
             'Журнал модемов файлом: /log или кнопка под журналом.\n') +
           T('Alerts (mode, types, quiet hours) and Telegram proxies: /settings.\n',
             'Уведомления (режим, виды, тихие часы) и прокси для Telegram: /settings.\n') +
           T('Commands: ', 'Команды: ') + join(' ', map(COMMANDS, (c) => '/' + c[0]));
}

// a link's role and the delay of the engine's last check: « · основной · 48 мс», « · резерв 2 · нет связи»
function linkState(l) {
    let role = l.role == 'carrying' || l.role == 'limited' || l.role == 'no_internet' ? T('primary', 'основной') :
               index(l.role || '', 'backup_') == 0 ? T('backup', 'резерв') + (l.rank ? ' ' + l.rank : '') : null;
    let how = l.ms != null ? l.ms + T(' ms', ' мс') : l.reach == 'unavailable' ? T('no internet', 'нет связи') :
              l.reach == 'limited' ? T('whitelist only', 'только белый список') : null;
    let out = joined([role, how]);
    return out != '' ? ' · ' + out : '';
}
// «🌍 Сеть» (the owner, 2026-10-28): info {links: [{key, name, device, addr, main, modem}],
// cache: {ip: {at, public: {dev|'-': addr}}, tunnel: {at, r}, svc: {at, link, results, error}}, now, busy}
const SVC = {ok:'✅', blocked:'⛔', timeout:'⏱', dns:'❓', refused:'🚫', reset:'✂️', tls:'🔒', cert:'⚠️', tunnel:'🛡', other:'❔'};
const SVC_WORD = {blocked:['blocked', 'блокировка'], timeout:['no answer', 'нет ответа'], dns:['DNS', 'DNS'], refused:['denied', 'отказ'],
                  reset:['reset', 'обрыв'], tls:['TLS', 'TLS'], cert:['invalid cert', 'неверный сертификат'],
                  tunnel:['sent to the router\'s tunnel', 'уходит в туннель роутера'], other:['other', 'другое']};
// the same, as the head of a group of services
const SVC_HEAD = {blocked:['Blocked', 'Блокировка'], timeout:['No answer', 'Нет ответа'], dns:['Name not found', 'Имя не найдено'],
                  refused:['Refused', 'Отказ в соединении'], reset:['Cut', 'Обрыв соединения'], tls:['TLS error', 'Ошибка TLS'],
                  cert:['A forged certificate', 'Ошибка проверки сертификата'], other:['Another answer', 'Другой ответ']};
function hm(epoch) { let t = localtime(epoch); return sprintf('%02d:%02d', t.hour, t.min); }
// the blocking test (the owner 2026-11-02): one modem's state from its service check, by weights, never a site by name.
// open: the open internet's services, the common ones heavier; list: the ones expected to stay under «белые списки»
const BLOCK_WEIGHT = {google:3, github:2, wikipedia:2};
function blockVerdict(results, error) {
    if (type(results) != 'array' || !length(results))
        return {kind:'none', words:error == 'busy' ? T('another test is already running', 'на роутере уже идёт проверка') : T('test failed to start', 'не удалось выполнить проверку')};
    let ow = 0, ook = 0, lw = 0, lok = 0;
    for (let r in results) {
        // known blocks (the regulator's, a service closed to Russia) and a name the tunnel takes say nothing of a
        // whitelist (the owner, 2026-11-15)
        if (r.group == 'known' || r.verdict == 'tunnel') continue;
        let w = r.group == 'list' ? 1 : (BLOCK_WEIGHT[r.id] || 1), ok = r.verdict == 'ok';
        if (r.group == 'list') { lw += w; if (ok) lok += w; } else { ow += w; if (ok) ook += w; }
    }
    let open = ow ? int(ook * 100 / ow) : null, list = lw ? int(lok * 100 / lw) : null, kind;
    if (!ook && !lok) kind = 'down';
    else if ((open ?? 0) < 20 && (list ?? 0) >= 50) kind = 'whitelist';
    else if ((open ?? 0) >= 80) kind = 'open';
    else kind = 'partial';
    return {kind, open, list};
}
const BLOCK_WORDS = {open:['🟢', 'no blocking', 'блокировки не обнаружены'], partial:['🟡', 'partly blocked', 'частичные блокировки'],
                     whitelist:['⚪', 'whitelists', 'белые списки'], down:['🔴', 'no internet', 'интернета нет'], none:['❔', '', '']};
function blockLine(name, v) {
    let w = BLOCK_WORDS[v.kind] || BLOCK_WORDS.none;
    return w[0] + ' ' + B(label(name)) + ': ' + (v.kind == 'none' ? v.words : T(w[1], w[2])) +
           (v.open != null ? T(' · open services ', ' · общедоступные сервисы ') + v.open + '%' : '') +
           (v.list != null ? T(' · whitelisted ', ' · из белых списков ') + v.list + '%' : '');
}
function blocksConfirm(count) {
    return {text:'🚦 ' + B(T('Blocking test', 'Тест блокировок')) + '\n\n' +
        sprintf(T('Tests all probe services directly over each connected modem (%d) in turn without changing active routes or the primary uplink (~10 s and minimal traffic per modem).',
                  'Проверка тестовых сервисов напрямую через каждый подключённый модем (%d) по очереди. Маршруты и основной канал не меняются (~10 с и <1 МБ на модем).'), count) +
        '\n' + I(T('Reports whether each modem appears whitelisted based on weighted probe results (no site names listed).',
                    'В результате показано, действует ли на модеме белый список (по весам ответивших сервисов, без перечня сайтов).')),
        markup:{inline_keyboard:[[{text:T('▶️ Run', '▶️ Запустить'), callback_data:'wby'}], [{text:T('◀️ Back', '◀️ Назад'), callback_data:'bl'}]]}};
}
// «🧪 Проверить сервисы»: one button, then the links one per row with their whole names
function svcPick(links) {
    let rows = [];
    for (let l in links) {
        if (l.main && l.modem) continue;
        push(rows, [{text:'🧪 ' + (l.main ? T('Main route', 'Основной маршрут') : shortName(l.name)) + ' (' + l.device + ')', callback_data:'ws:' + l.key}]);
    }
    push(rows, [{text:T('◀️ Back', '◀️ Назад'), callback_data:'w'}]);
    return {text:'🧪 ' + B(T('Services check', 'Проверка сервисов')) + '\n' +
            T('Through which link? A modem is checked straight, without the router\'s tunnel; the main route as the router\'s own traffic goes.',
              'Через какой канал проверить? Модем проверяется напрямую, без туннеля роутера; основной маршрут — тем же путём, что и трафик роутера.'),
            markup:{inline_keyboard:rows}};
}
// the service check's answers grouped by what they mean, one line each: no wall of fifteen lines
function svcLines(results) {
    let ok = [], closed = [], tunnel = [], other = {}, order = [], lines = [], slow = null;
    for (let r in results) {
        let n = esc(label(r.name));
        if (r.verdict == 'ok') {
            push(ok, n);
            if (type(r.ms) == 'int' && (!slow || r.ms > slow.ms)) slow = {name:n, ms:r.ms};
        } else if (r.verdict == 'tunnel') push(tunnel, n);
        else if (r.group == 'known') push(closed, n);
        else {
            let w = W(SVC_HEAD[r.verdict]) || esc(label(r.verdict));
            if (!other[w]) { other[w] = {mark:SVC[r.verdict] || '❔', names:[]}; push(order, w); }
            push(other[w].names, n);
        }
    }
    if (length(ok)) push(lines, '✅ ' + T('Answer: ', 'Отвечают: ') + join(', ', ok));
    for (let w in order) push(lines, other[w].mark + ' ' + w + ': ' + join(', ', other[w].names));
    if (length(closed)) push(lines, '🚫 ' + T('Usually closed in Russia, no answer: ', 'Обычно закрыты в России, не ответили: ') + join(', ', closed));
    if (length(tunnel)) push(lines, '🛡 ' + T('Sent to the router\'s tunnel: ', 'Уходят в туннель роутера: ') + join(', ', tunnel));
    push(lines, I(sprintf(T('Answered: %d of %d', 'Ответили: %d из %d'), length(ok), length(results)) +
                  (slow && slow.ms >= 1000 ? sprintf(T(' · the slowest %s (%s s)', ' · медленнее всех %s (%s с)'), slow.name, sprintf('%.1f', slow.ms / 1000.0)) : '')));
    return lines;
}
function netPage(info) {
    let c = info.cache || {}, pub = c.ip?.public || {};
    let lines = ['🌍 ' + B(T('Network', 'Сеть'))];
    if (info.note) push(lines, I(esc(info.note)));
    for (let l in info.links) {
        // the main route through a modem is that modem's line («основной»): said once
        if (l.main && l.modem) continue;
        let p = l.main ? pub['-'] : pub[l.device];
        let name = l.main ? T('➡️ Main route', '➡️ Основной маршрут') : '📶 ' + esc(shortName(l.name));
        push(lines, B(name) + (l.main ? '' : linkState(l)));
        push(lines, '     ' + esc(l.device) + (l.addr ? ' · ' + C(l.addr) : '') +
             (p ? ' → ' + C(p) : c.ip && p === null ? T(' · public address unknown', ' · внешний адрес не определён') : ''));
    }
    if (!length(info.links)) push(lines, T('No uplinks with an IP address.', 'Нет ни одного канала с адресом.'));
    if (c.ip) push(lines, I(T('Public addresses checked at ', 'Внешние адреса проверены в ') + hm(c.ip.at)));
    let t = c.tunnel?.r;
    if (t) {
        push(lines, '');
        let ms = type(t.managers) == 'array' ? t.managers : [];
        if (!length(ms)) push(lines, '🛡 ' + T('podkop, podkop-plus, forkop, or netshift not installed', 'podkop, podkop-plus, forkop, netshift не установлены'));
        for (let m in ms)
            push(lines, '🛡 ' + B(label(m.name)) + (m.version ? ' ' + C(m.version) : '') + ' · ' +
                 (m.enabled ? T('enabled', 'включён') : T('autostart disabled', 'не включён в автозапуск')));
        if (length(ms)) {
            push(lines, (t.builtin ? T('sing-box (inside podkop): ', 'sing-box (встроен в podkop): ') : 'sing-box: ') +
                 (t.sing_box ? T('running', 'работает') : T('not running', 'не запущен')));
            push(lines, T('Router DNS through sing-box: ', 'DNS роутера через sing-box: ') + (t.fakeip === true ? '✅' : t.fakeip === false ? '❌' : T('no answer', 'нет ответа')));
            // podkop 0.8 routes no outside check name: its exit address is not known, not «no answer»
            if (!t.builtin)
                push(lines, T('Through the tunnel: ', 'Через туннель: ') + (t.exit_ip ? '✅ ' + T('exit ', 'выход ') + C(t.exit_ip) : '❌ ' + T('no answer', 'нет ответа')));
        }
        push(lines, I(T('Checked at ', 'Проверено в ') + hm(c.tunnel.at)));
    }
    let v = c.svc;
    if (v) {
        push(lines, '', '🧪 ' + B(T('Services through ', 'Сервисы через ') + (v.link?.main ? T('the main route', 'основной маршрут') : shortName(v.link?.name)) +
             ' (' + esc(v.link?.device) + ')') + ', ' + hm(v.at));
        // a modem's own device: straight through its operator, the router's tunnel not used (the owner, 2026-11-14)
        if (!v.link?.main) push(lines, I(T('Straight through the modem, without the tunnel', 'Напрямую через модем, без туннеля')));
        if (!v.results) push(lines, v.error == 'busy' ? T('Another test is running — retry in a minute', 'На роутере уже идёт другая проверка, повторите через минуту') :
                                    T('Test failed', 'Проверка не выполнена'));
        else for (let x in svcLines(v.results)) push(lines, x);
    }
    // other modem helpers on the router (the engine's neighbours, 2026-10-28): names and holders only
    let nb = info.neighbours, pk = type(nb?.packages) == 'array' ? nb.packages : [], ho = type(nb?.holders) == 'array' ? nb.holders : [],
        sc = type(nb?.scripts) == 'array' ? nb.scripts : [];
    if (length(pk) || length(ho) || length(sc)) {
        push(lines, '', '⚠️ ' + B(T('Other modem tools on the router', 'Другие службы управления модемами')));
        for (let p in slice(pk, 0, 8))
            push(lines, '• ' + esc(label(p.id)) + (p.running ? T(' · running', ' · работает') : '') + (p.cron ? T(' · by cron', ' · по cron') : ''));
        for (let h in slice(ho, 0, 6))
            push(lines, '• ' + esc(label(h.comm)) + T(' holds ', ' занимает ') + esc(label(h.node)));
        if (length(sc)) push(lines, '• ' + T('scripts using the modems: ', 'скрипты, обращающиеся к модемам: ') + length(sc));
        push(lines, I(T('MedveModem does not stop them; see LuCI for details and potential conflicts', 'MedveModem их не останавливает; подробности и возможные конфликты — в LuCI')));
    }
    if (info.busy) push(lines, '', '⏳ ' + T('Testing…', 'Идёт проверка…'));
    push(lines, '', freshWords(info.now));
    return {text:join('\n', lines), markup:{inline_keyboard:[
        [{text:T('🔍 Public IP', '🔍 Внешние IP'), callback_data:'wi'}, {text:T('🛡 Tunnel', '🛡 Туннель'), callback_data:'wt'}],
        [{text:T('🧪 Check the services', '🧪 Проверить сервисы'), callback_data:'wp'}],
        [{text:T('🔄 Refresh', '🔄 Обновить'), callback_data:'w'}, {text:T('🏠 Menu', '🏠 Меню'), callback_data:'v:m'}]]}};
}

// «🚧 Белые списки» (the owner, 2026-11-12): each channel's access now, the whitelist mode with its switch (management
// allowed), the blocking test over every modem. info {paths, tunnel, wlmode, control, cache, busy, now}
function wlPage(info) {
    let lines = ['🚧 ' + B(T('Whitelists', 'Белые списки'))], c = info.cache || {};
    if (info.note) push(lines, I(esc(info.note)));
    for (let p in (info.paths || [])) {
        let lim = p.reach == 'limited' || p.role == 'limited' || p.role == 'backup_limited';
        let st = lim ? (info.tunnel?.ok && p.role == 'limited' ? '🟢 ' + T('the whitelist; the internet through the tunnel', 'белый список; интернет через туннель') :
                                                                 '🟡 ' + T('the whitelist only', 'только белый список')) :
                 p.reach == 'available' ? '🟢 ' + T('full internet access', 'полный доступ в интернет') : p.reach == 'unavailable' ? '🔴 ' + T('no internet', 'нет интернета') :
                 '⚪ ' + T('not checked yet', 'ещё не проверен');
        // a modem without a channel yet (no device, no name) has nothing to say here
        if (!p.name && !p.device) continue;
        push(lines, B(esc(p.name || p.device)) + ' · ' + st);
    }
    if (!length(info.paths || [])) push(lines, T('No channel is checked yet.', 'Каналы ещё не проверялись.'));
    let wl = info.wlmode, wlRow = [];
    push(lines, '', wl?.on ? wlmodeText(wl) : wl?.enabled ? '🛡 ' + T('Whitelist mode: standby (waiting for carrier whitelist)', 'Режим белых списков: ожидание ограничений оператора') :
                    '🛡 ' + T('Whitelist mode is not set up: LuCI → Modem → MedveModem → Whitelists', 'Режим белых списков не настроен: LuCI → Модем → MedveModem → Белые списки'));
    if (info.control && (wl?.enabled || wl?.on))
        push(wlRow, wl.on ? {text:T('🛡 Disable mode', '🛡 Выключить режим'), callback_data:'wwf'} :
                            {text:T('🛡 Enable mode', '🛡 Включить режим'), callback_data:'wwo'});
    let bl = c.blocks;
    if (bl) {
        push(lines, '', '🚦 ' + B(T('Blocking test', 'Тест блокировок')) + ', ' + hm(bl.at));
        if (!length(bl.rows)) push(lines, T('No connected modems to test.', 'Нет подключённых модемов для проверки.'));
        for (let r in bl.rows) push(lines, blockLine(r.name, blockVerdict(r.results, r.error)));
    }
    if (info.busy) push(lines, '', '⏳ ' + T('Testing…', 'Идёт проверка…'));
    push(lines, '', freshWords(info.now));
    return {text:join('\n', lines), markup:{inline_keyboard:[
        [{text:T('🚦 Blocking test', '🚦 Тест блокировок'), callback_data:'wb'}], ...(length(wlRow) ? [wlRow] : []),
        [{text:T('🔄 Refresh', '🔄 Обновить'), callback_data:'bl'}, {text:T('🏠 Menu', '🏠 Меню'), callback_data:'v:m'}]]}};
}

// ucode as pinned by OpenWrt 24.10 and 25.12 does not parse `export function` declarations
export { svcPick, kernelNotice, dayLine, journalKeys, menuKeyboard, buttonCommand, commandList, parseCallback, shortKey, plural, bytes, techWords, stamp, stampFresh,
         header, status, menu, back, uplinkKeys, modemList, card, confirm, asks, quotas, uplinks, help, duration, incidentText,
         failoverText, wlmodeText, journal, mainId, smsPage, smsNew, smsCode, serviceNotice, memLine, excludePage, stampWords, notice, restartText, netPage, wlPage, blocksConfirm, blockVerdict,
         B, C, I, esc, joined, shortName, quotaWords, quotaKeys };
