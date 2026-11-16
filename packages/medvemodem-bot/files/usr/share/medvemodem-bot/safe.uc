'use strict';
import { sha256 } from 'digest';
import { T, W } from './lang.uc';

// RFC 2104 HMAC-SHA256; SHA256() returns lowercase hex.
function hmac256(key, value) {
    if (length(key) > 64)
        key = hexdec(sha256(key));
    let inner = '', outer = '';
    for (let n = 0; n < 64; n++) {
        let byte = n < length(key) ? ord(substr(key, n, 1)) : 0;
        inner += chr(byte ^ 0x36);
        outer += chr(byte ^ 0x5c);
    }
    return sha256(outer + hexdec(sha256(inner + value)));
}

function opaque(key, value) {
    return 'hmac128:' + substr(hmac256(key, '' + value), 0, 32);
}

// ucode's substr() counts bytes: a cut inside a Cyrillic letter is not UTF-8, and Telegram refuses the whole call
function cut(s, n) {
    if (length(s) <= n) return s;
    let i = n;
    while (i > 0 && (ord(s, i) & 0xc0) == 0x80) i--;
    return substr(s, 0, i);
}

// a page's marks (⟦b⟧ ⟦code⟧ ⟦i⟧) made tags, within the byte budget after the escaping, never a tag cut in two (audit R05:
// the cut came before & grew to &amp; and before the marks became tags: an unclosed tag, or a page refused whole).
// Cut at a line end where it can, «…» said, the open tags closed in their order
function htmlBudget(s, budget) {
    let make = (t) => replace(replace(t, /&/g, '&amp;'), /⟦(\/?)(b|code|i)⟧/g, '<$1$2>');
    let out = make(s);
    if (length(out) <= budget) return out;
    let keep = length(s);
    while (keep > 0) {
        keep = int(keep * 0.85);
        let part = cut(s, keep), nl = rindex(part, '\n');
        if (nl > length(part) / 2) part = substr(part, 0, nl);
        // a mark cut in two goes whole
        let open = rindex(part, '⟦');
        if (open >= 0 && index(substr(part, open), '⟧') < 0) part = substr(part, 0, open);
        let stack = [];
        for (let m in match(part, /⟦\/?(b|code|i)⟧/g) || []) {
            if (substr(m[0], 3, 1) == '/') { if (length(stack) && stack[length(stack) - 1] == m[1]) pop(stack); }
            else push(stack, m[1]);
        }
        let tail = '\n…';
        for (let i = length(stack) - 1; i >= 0; i--) tail = '⟦/' + stack[i] + '⟧' + tail;
        out = make(part + tail);
        if (length(out) <= budget) return out;
    }
    return '…';
}

// Device names are untrusted: no raw phone numbers, terminal escapes, HTML or URLs.
function label(value) {
    let s = type(value) == 'string' ? value : '';
    s = replace(s, /[\x01-\x1f\x7f<>]/g, ' ');
    s = replace(s, /\+?[0-9][0-9 \t().-]{7,}[0-9]/g, '[hidden]');
    s = replace(s, /[a-fA-F0-9]{15,}/g, '[hidden]');
    s = replace(s, /(https?|socks5h?):\/\/[^ \t\r\n]+/g, '[hidden]');
    return cut(s, 64);
}

// a host name the engine found (the operator's page): letters, digits, dots and dashes only, or nothing
function hostName(value) {
    return type(value) == 'string' && length(value) <= 63 && match(value, /^[a-z0-9][a-z0-9.-]*$/) ? value : null;
}

// allow: the router's own SIM numbers (simNumber), the only numbers a message may carry; any other is hidden
function message(value, allow, limit) {
    let s = type(value) == 'string' ? value : '';
    let own = {};
    for (let n in (type(allow) == 'array' ? allow : [])) if (type(n) == 'string') own[n] = true;
    s = replace(s, /[\x01-\x08\x0b-\x1f\x7f<>]/g, ' ');
    // a bot token before the numbers (its id would go and its secret stay); then links of any scheme: a proxy's vless://,
    // ss://, trojan:// carry their keys (the router's whole log, the owner 2026-11-02)
    s = replace(s, /[0-9]{5,12}:[A-Za-z0-9_-]{30,}/g, '[hidden]');
    s = replace(s, /[A-Za-z][A-Za-z0-9+.-]{1,15}:\/\/[^ \t\r\n]+/g, '[hidden]');
    s = replace(s, /\+?[0-9][0-9 \t().-]{7,}[0-9]/g, (m) => own[m] ? m : '[hidden]');
    s = replace(s, /[a-fA-F0-9]{15,}/g, '[hidden]');
    // an HTML page is cut later, after its marks became tags (htmlBudget): a bound only against a runaway string here
    return cut(s, type(limit) == 'int' && limit > 0 ? limit : 3500);
}

function words(list) {
    let out = [];
    for (let w in (type(list) == 'array' ? list : []))
        if (type(w) == 'string' && match(w, /^[a-z0-9-]{1,16}$/) && length(out) < 8) push(out, w);
    return out;
}

function numeric(value) {
    return type(value) == 'int' || type(value) == 'double' ? value : null;
}

// a received power the modem really measured: -32768 and the like are «no reading» (the owner's chat, 2026-10-20:
// «NR RSRP -32768 дБм» for a modem on LTE alone)
function dbm(value) {
    let v = numeric(value);
    return v != null && v > -160 && v < 0 ? v : null;
}

// the engine's reach verdict: only the restricted targets answer (operator whitelists) is "limited", not "unavailable"
function reachWord(r) {
    if (type(r) != 'object') return label(r);
    if (r.ok == true) return 'available';
    // no check yet: unknown, not down (the list's dot stays green on a connected session)
    if (r.ok == null && r.limited == null && r.level == null) return '';
    return r.limited == true || r.level == 'limited' ? 'limited' : 'unavailable';
}

// the SIM's own number, as the owner wrote it in LuCI («SIM» → номер) or ModemManager read it: digits only
function simNumber(value) {
    let s = type(value) == 'string' ? replace(value, /[ \t()-]/g, '') : '';
    return match(s, /^\+?[0-9]{3,20}$/) ? s : null;
}

// a modem's name as the LuCI card says it (docs/BOT-NOTIFICATIONS-PLAN.md step 5, 26.10): the owner's name, else the
// model without the USB descriptor's tail («T99W175 - QMI Composition» → «T99W175»), a profile's title where the USB
// strings say nothing, the operator, «Модем». «QModem Enabled» is a QModem section's state, not a name.
const NOT_A_NAME = /^(qmodem\b|android\b|generic\b|mobile\b|usb\b|rndis\b|cdc\b|--$)/i;
function modelWords(v) {
    let s = type(v) == 'string' ? trim(v) : '';
    s = replace(s, /[ \t]*-?[ \t]*((QMI|MBIM|RNDIS|NCM|ECM|AT|MSFT)[ \t]+)?Composition$/i, '');
    s = replace(s, /[ \t]*-[ \t]*$/, '');
    return length(s) && !match(s, NOT_A_NAME) ? s : '';
}
function niceName(raw, mm) {
    let own = type(raw.name) == 'string' ? trim(raw.name) : '';
    if (length(own) && !match(own, NOT_A_NAME)) return own;
    let m = modelWords(raw.model) || modelWords(mm.model) || modelWords(raw.product);
    if (m) return m;
    let prof = type(raw.profile) == 'object' && type(raw.profile.title) == 'string' ? trim(raw.profile.title) : '';
    if (length(prof)) return prof;
    return type(mm.operator) == 'string' && length(mm.operator) ? T('Modem ', 'Модем ') + mm.operator : T('Modem', 'Модем');
}
// the USB chain of the modem's port («1.3»), only to tell two modems of one name apart
function usbChain(raw) {
    let c = type(raw.usb_port) == 'object' ? raw.usb_port.chain : null;
    if (type(c) != 'string') {
        let m = match(raw.device_id, /^usb-port:[^@]*@([0-9.]{1,15})$/);
        c = m ? m[1] : null;
    }
    return type(c) == 'string' && match(c, /^[0-9]{1,3}(\.[0-9]{1,3}){0,6}$/) ? c : null;
}

function downtime(t) {
    if (type(t) != 'object') return null;
    let out = {};
    for (let k in ['ok', 'limited', 'none', 'off', 'outages', 'longest', 'current'])
        out[k] = type(t[k]) == 'int' && t[k] >= 0 && t[k] <= 864000 ? t[k] : 0;
    return out;
}

function ranged(v, lo, hi) {
    let n = numeric(v);
    return n != null && n >= lo && n <= hi ? n : null;
}
const ACTION_CODES = {drivers:true, modemmanager:true, 'modeswitch-rule':true, 'modeswitch-pending':true, 'sim-pin':true,
                      'sim-puk':true, 'fcc-unlock':true, 'foreign-interface':true};

function modem(raw, key) {
    if (type(raw) != 'object' || type(raw.device_id) != 'string')
        return null;
    let mm = raw.modemmanager || {}, sess = raw.session || {}, up = raw.uplink || {};
    let quota = raw.quota || {}, signal = mm.signal || {}, owner = raw.owner || {};
    let nr = signal['5g'] || {};
    let reach = up.reach || {};
    let lab = raw.sim_label || {}, own = type(mm.own_numbers) == 'array' ? mm.own_numbers : [];
    return {
        schema_version: 1,
        key: opaque(key, raw.device_id),
        name: label(niceName(raw, mm)),
        port: usbChain(raw),
        present: raw.present == true,
        // «Приостановить управление»: the engine's services leave it alone; who froze it is not carried
        frozen: type(raw.frozen) == 'object' || raw.frozen == true,
        // what the bot may send through it (r184): SMS when the engine's SMS path is open; USSD as the modem page offers it
        // (not through a third-party manager, not through a web without USSD, not on a firmware that never answers it)
        sms_ok: raw.capabilities?.sms?.ok === true,
        ussd_ok: raw.capabilities?.sms?.ok === true && raw.capabilities.sms.via != 'qmodem' &&
                 (raw.capabilities.sms.via != 'web' || raw.capabilities.sms.ussd == 'web') && raw.capabilities?.ussd?.ok !== false,
        model: label(raw.model || mm.model || raw.product),
        owner: label(owner.control),
        // the operator's name and the network are not the SIM's identity; access_tech is a closed list of words
        operator: label(mm.operator),
        // the network's code and the SIM's own operator's (2026-11-05: Yota's SIM in MegaFon's network): digits only
        operator_code: type(mm.operator_code) == 'string' && match(mm.operator_code, /^[0-9]{5,6}$/) ? mm.operator_code : null,
        sim_operator_code: type(raw.sim_operator?.code) == 'string' && match(raw.sim_operator.code, /^[0-9]{5,6}$/) ?
                           raw.sim_operator.code : null,
        registration: label(mm.registration),
        access_tech: words(mm.access_tech),
        recovery: label((raw.recovery || {}).state),
        // the network device the session runs on: the main link is matched by it, not by the modem's name (two T99 of
        // one name, second audit 2026-10-21)
        session: {state: label(sess.state), interface: label(sess.interface), device: label(sess.netifd?.l3_device)},
        signal: {
            quality_percent: numeric(mm.signal_quality),
            lte_rsrp_dbm: dbm((signal.lte || {}).rsrp),
            nr_rsrp_dbm: dbm(nr.rsrp),
            // the noise apart from the power (audit 10: «good signal, high noise» are two things)
            lte_sinr_db: ranged((signal.lte || {}).snr, -30, 50),
            nr_sinr_db: ranged(nr.snr, -30, 50)
        },
        uplink: {role: label(up.role), reach: reachWord(reach),
                 latency_ms: type(reach) == 'object' ? ranged(reach.latency_ms, 0, 60000) : null,
                 checked_s: type(reach) == 'object' ? ranged(reach.age_s, 0, 864000) : null,
                 // r156: the host of the operator's own page plain HTTP is sent to (a blocked or unpaid SIM)
                 redirect: hostName(type(reach) == 'object' ? reach.redirect : null)},
        // connected for, from the engine's moment the interface came up
        up_s: type(sess.up_at) == 'int' && sess.up_at > 0 && time() - sess.up_at >= 0 ? time() - sess.up_at : null,
        // what only a person can fix (the engine's action_needed): a closed list of codes
        action_needed: filter(type(raw.action_needed) == 'array' ? raw.action_needed : [], (a) => ACTION_CODES[a] === true),
        quota: {
            state: label(quota.state),
            used_bytes: numeric(quota.used_bytes),
            limit_bytes: numeric(quota.limit_bytes),
            percent: numeric(quota.percent),
            resets_on: type(quota.resets_on) == 'string' && match(quota.resets_on, /^[0-9]{4}-[0-9]{2}-[0-9]{2}$/) ? quota.resets_on : null
        },
        health: {state: label((raw.health || {}).state || (raw.stability || {}).state)},
        // the day's seconds per state and the outages (the engine's downtime, r109)
        downtime: downtime(raw.downtime),
        // the SIM's name and number for the admin (owner, 2026-10-20); never its ICCID, IMSI or the IMEI
        sim: {name: label(lab.description), number: simNumber(lab.number) || simNumber(own[0])}
    };
}

function uplink(raw) {
    let r = {schema_version: 1, internet: 'unknown', paths: [], carrier: null};
    if (type(raw) != 'object') return r;
    r.internet = label(raw.internet);
    if (type(raw.carrier) == 'object')
        r.carrier = {name: label(raw.carrier.name), device: label(raw.carrier.device)};
    for (let p in (type(raw.paths) == 'array' ? raw.paths : [])) {
        if (length(r.paths) >= 16) break;
        // the modem's id, to name the channel as the modem list does; a wired WAN's interface (wan, wwan)
        // why a backup has no internet when its own web said so (r154: a stick without a SIM), from a closed list; the
        // operator's priority (the configured route metric) to list the backups in order
        let err = type(p.reach) == 'object' && type(p.reach.error) == 'string' ? p.reach.error : '';
        push(r.paths, {device: label(p.device), name: label(p.name), role: label(p.role),
                      reach: reachWord(p.reach), id: type(p.device_id) == 'string' ? label(p.device_id) : null,
                      why: type(p.reach) == 'object' && hostName(p.reach.redirect) ? 'restricted' :
                           index(err, "the modem's web says: no SIM") == 0 ? 'no_sim' :
                           index(err, "the modem's web says: the SIM") == 0 ? 'bad_sim' :
                           index(err, "the modem's web says: no mobile network") == 0 ? 'no_network' : null,
                      metric: type(p.metric) == 'int' && p.metric >= 0 && p.metric < 1000000 ? p.metric : null,
                      interface: type(p.interface) == 'string' && match(p.interface, /^[A-Za-z0-9_.-]{1,32}$/) ? p.interface : null});
    }
    // the links failover never makes main by itself (core.failover_manual): network device names
    // r96: the engine's tunnel probe while the carrier is limited (podkop's VLESS over a whitelisted link): ok and the delay
    if (type(raw.tunnel) == 'object' && raw.tunnel.ok === true)
        r.tunnel = {ok: true, delay_ms: type(raw.tunnel.delay_ms) == 'int' && raw.tunnel.delay_ms >= 0 && raw.tunnel.delay_ms < 100000 ?
                    raw.tunnel.delay_ms : null};
    // channel switching off while the carrier has no internet and a backup answers (the tester's T99 with no money,
    // 2026-11-03: the traffic stayed on the dead channel and nobody said why)
    r.switch_off = raw.failover?.enabled === false;
    // spec §44: the whitelist mode as the engine has it; the server's name only as a label
    if (type(raw.wlmode) == 'object')
        r.wlmode = {on: raw.wlmode.on === true, enabled: raw.wlmode.enabled === true, manual: raw.wlmode.manual === true,
                    server: type(raw.wlmode.label) == 'string' && raw.wlmode.label ? label(raw.wlmode.label) : type(raw.wlmode.server) == 'string' ? label(raw.wlmode.server) : null,
                    delay_ms: type(raw.wlmode.delay_ms) == 'int' && raw.wlmode.delay_ms >= 0 && raw.wlmode.delay_ms < 100000 ? raw.wlmode.delay_ms : null,
                    lan: raw.wlmode.lan === true, router: raw.wlmode.router === true};
    r.manual = [];
    for (let d in (type(raw.failover?.manual) == 'array' ? raw.failover.manual : []))
        if (type(d) == 'string' && match(d, /^[A-Za-z0-9._@-]{1,15}$/) && length(r.manual) < 16) push(r.manual, d);
    return r;
}

function incident(key, device, kind, state, summary, severity, origin) {
    let now = time(), t = gmtime(now);
    return {
        schema_version: 1, event_id: opaque(key, origin),
        time: now >= 1790812800 ? sprintf('%04d-%02d-%02dT%02d:%02d:%02dZ', t.year, t.mon, t.mday, t.hour, t.min, t.sec) : null,
        source: 'medvemodem', severity: severity || 'warning',
        device_id: device ? opaque(key, device) : null,
        iccid_hash: null, kind, state, summary,
        correlation_id: opaque(key, (device || 'system') + '/' + kind)
    };
}

// the engine's recovery levels (src/recovery.c names[]), in the words the LuCI pages use
const STEP_WORDS = {observe:['refreshing the modem info', 'обновление данных модема'], session:['reconnecting the internet', 'перезапуск соединения'],
    control:['releasing stuck ports', 'освобождение зависших портов'], radio:['restarting the network search', 'перезапуск поиска сети'],
    'soft-reset':['rebooting the modem', 'перезагрузка модема'], rebind:['rebinding the driver', 'переподключение драйвера'],
    'usb-reset':['resetting the USB device', 'сброс USB-устройства'], power:['power-cycling the USB port', 'отключение и включение питания USB-порта']};
function stepWordKey(v) { return type(v) == 'string' && STEP_WORDS[v] ? v : null; }
function stepWord(v) { return STEP_WORDS[v] ? W(STEP_WORDS[v]) : null; }

// the journal's line for an engine event: [en, ru] with its why in words, from the engine's closed lists only (the
// owner 2026-10-28: «события без подробностей»); null for a kind the journal does not show. A detail outside the lists
// is never shown: it may carry anything
const FOUND_WHY = {start:['at the service start', 'при запуске службы'], uevent:['a USB event', 'событие USB'],
    'rescan request':['a rescan', 'повторный поиск модемов'], returned:['it was gone and came back', 'отключался и снова подключён']};
const NET_WHY = {ok:['the whole internet', 'интернет доступен'], limited:['only the whitelist', 'только сайты из белого списка'],
    down:['no internet', 'интернета нет']};
const MOVE_WHY = {down:['no internet, the traffic went elsewhere', 'нет интернета, трафик переключён на другой канал'],
    limited:['only allowed sites, the traffic went elsewhere', 'только сайты из белого списка, трафик переключён на другой канал'],
    quota:['its traffic limit is used up, kept behind', 'лимит трафика исчерпан, приоритет канала понижен'],
    manual:['a manual channel, kept behind', 'канал с ручным управлением, приоритет понижен'],
    demoted:['answers again, kept last', 'связь восстановлена, канал оставлен последним'],
    back:['answers again, its priority restored', 'снова отвечает, приоритет восстановлен']};
const EVENT_TITLES = {'quota-warn':['The traffic limit\'s threshold is reached', 'Достигнут порог лимита трафика'],
    'quota-exceeded':['The traffic limit is used up', 'Лимит трафика исчерпан'],
    'session-down':['The connection broke', 'Подключение прервано'], 'session-up':['The connection is up', 'Подключение установлено'],
    'recovery-start':['Recovery', 'Восстановление'], 'recovery-end':['Recovery finished', 'Восстановление завершено'],
    added:['Modem found', 'Модем найден'], reattached:['Modem back on USB', 'Модем снова подключён к USB'],
    removed:['Modem gone from USB', 'Модем отключён от USB'], internet:['Internet through the modem', 'Интернет через модем'],
    failover:['Traffic switching', 'Переключение трафика']};
function eventPair(kind, detail) {
    let title = EVENT_TITLES[kind];
    if (!title) return null;
    let d = type(detail) == 'string' ? detail : '', why = null, m;
    if (kind == 'added' || kind == 'reattached')
        why = FOUND_WHY[d] || (index(d, 'attachment') >= 0 ? ['its USB connection changed', 'изменилось USB-подключение'] :
                               index(d, 'ports') >= 0 ? ['its ports changed', 'сменились порты'] : null);
    else if (kind == 'internet') why = NET_WHY[d];
    else if (kind == 'failover' && (m = match(d, /^([a-z]{1,12}) ([A-Za-z0-9_.-]{1,15})$/)) && MOVE_WHY[m[1]])
        why = [m[2] + ': ' + MOVE_WHY[m[1]][0], m[2] + ': ' + MOVE_WHY[m[1]][1]];
    else if ((kind == 'session-up' || kind == 'session-down') && match(d, /^[A-Za-z0-9_.-]{1,15}$/)) why = [d, d];
    else if ((kind == 'recovery-start' || kind == 'recovery-end') && (m = match(d, /^([a-z-]{1,12})[ :]/)) && STEP_WORDS[m[1]])
        why = STEP_WORDS[m[1]];
    return why ? [title[0] + ': ' + why[0], title[1] + ': ' + why[1]] : title;
}

// UI projection only: raw enum values never appear in Telegram messages. Each word in English and Russian (lang.uc)
const STATE_WORDS = {ok:['works', 'работает'], starting:['starting', 'запуск'], offline:['does not answer', 'не отвечает'],
    unknown:['no data', 'нет данных'], connected:['connected', 'установлено'], connecting:['connecting', 'устанавливается'],
    disconnected:['disconnected', 'нет'], failed:['error', 'ошибка'], available:['available', 'доступен'],
    unavailable:['unavailable', 'недоступен'], limited:['limited access', 'доступ ограничен (белый список)'], up:['works', 'работает'],
    down:['does not work', 'не работает'], carrying:['main', 'основной'], ready:['ready', 'готов'],
    checking:['checking', 'проверяется'], healthy:['works', 'работает'], degraded:['unstable', 'нестабильно'],
    no_sim:['no SIM card', 'нет SIM-карты'], no_sim_card:['no SIM card', 'нет SIM-карты']};
function stateWord(value) {
    return W(STATE_WORDS[value]) || T('no data', 'нет данных');	// a new engine word must not reach Telegram raw
}
// the engine's quota.state (UBUS-API: none | ok | warn | exceeded)
// after the numbers, of «Лимит трафика» (masculine): «1,0 МБ из 4,0 ГБ · в норме» (the owner 2026-10-28: «в пределах ужасно
// звучит» for Russian)
const QUOTA_WORDS = {none:['not set', 'не задан'], ok:['fine', 'в норме'], warn:['nearly used up', 'почти исчерпан'],
    exceeded:['used up', 'исчерпан']};
function quotaWord(value) {
    return W(QUOTA_WORDS[value]) || T('no data', 'нет данных');
}
// the engine's uplink roles (UBUS-API «uplink»), worded as the LuCI dashboard says them
const ROLE_WORDS = {carrying:['Main channel', 'Основной канал'], backup_ready:['Backup channel, ready', 'Резервный канал, готов'],
    backup_not_ready:['Backup channel, not ready', 'Резервный канал, не готов'],
    backup_limited:['Backup channel, limited access', 'Резервный канал, только белый список'],
    no_internet:['No internet access', 'Нет доступа в интернет'], limited:['Limited access', 'Доступ ограничен (белый список)'],
    checking:['Checking the link', 'Проверка связи'], down:['Not connected', 'Не подключён']};
function roleWord(value) {
    return W(ROLE_WORDS[value]) || T('Internet channel', 'Канал доступа в интернет');
}

// ucode as pinned by OpenWrt 24.10 and 25.12 does not parse `export function` declarations
// A modem ModemManager does not run (the engine's own AT port, QModem, a HiLink's or ZTE's web): its signal, operator and
// network from the engine's last reading (metrics, kept), as LuCI's cards show them (common.js atAsMm; the owner,
// 2026-11-15: «почему не видно уровень приема в ТГ» for the FM350, the HiLink, the E3372). Only what ModemManager did not
// give is filled; the same closed fields as modem(): levels in range, the network's code digits only.
const AT_TECH = {lte:['lte'], 'nr-nsa':['lte', '5gnr'], nr:['5gnr'], 'nr-sa':['5gnr'], umts:['umts'], hspa:['hspa'],
                 gsm:['gsm'], edge:['edge']};
function reading(safe, m) {
    if (type(safe) != 'object' || type(m) != 'object') return safe;
    let sg = safe.signal, n = type(m.nr) == 'object' ? m.nr : {};
    let hasLte = dbm(m.rsrp_dbm) != null;
    if (sg.lte_rsrp_dbm == null && hasLte) {
        sg.lte_rsrp_dbm = dbm(m.rsrp_dbm);
        sg.lte_sinr_db = ranged(m.sinr_db, -30, 50);
    }
    if (sg.nr_rsrp_dbm == null && dbm(n.rsrp_dbm) != null) {
        sg.nr_rsrp_dbm = dbm(n.rsrp_dbm);
        sg.nr_sinr_db = ranged(n.sinr_db, -30, 50);
    }
    let code = type(m.operator) == 'string' && match(m.operator, /^[0-9]{5,6}$/) ? m.operator :
               type(m.operator_code) == 'string' && match(m.operator_code, /^[0-9]{5,6}$/) ? m.operator_code : null;
    if (!safe.operator_code && code) safe.operator_code = code;
    if (!safe.operator && type(m.operator) == 'string' && m.operator != code) safe.operator = label(m.operator);
    // a reading with an operator is a modem in the network (as LuCI lights its bars)
    if (!safe.registration && (safe.operator || safe.operator_code)) safe.registration = 'home';
    if (!length(safe.access_tech) && type(m.access_technology) == 'string')
        safe.access_tech = words(AT_TECH[m.access_technology] || [m.access_technology]);
    return safe;
}

export { hmac256, opaque, cut, htmlBudget, label, message, simNumber, numeric, modem, uplink, incident, stateWord, quotaWord, roleWord,
         niceName, usbChain, stepWord, stepWordKey, eventPair, reading };
