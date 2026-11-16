'use strict';
// «⚙️ Настройки бота» in Telegram (the owner, 2026-10-31: «в боте есть настройки бота, регулировка степени и типов
// оповещений, тихий период, добавление прокси прямо в боте»): the notifications' mode and kinds, quiet hours, the proxies to
// Telegram with a check before use. Ours differ where the router asks for it: every change goes through rpcd's bot_set
// and bot_proxy_* (one checked way for the bot and LuCI), which take only the keys below; quiet hours make notifications silent instead of
// dropping them, and «the internet is gone» keeps its sound unless the admin says otherwise; a proxy that failed its check
// is kept switched off; the last way to Telegram is never removed. The pages only draw; main.uc does the calls.
import { B, C, I, esc, stamp, joined } from './ui.uc';
import { label } from './safe.uc';
import { T, W } from './lang.uc';

// the kinds of notifications the admin switches, by their button's letter: [section, option, words, the option's default]
const TOGGLES = {
    b:['events', 'backup', ['Backup modems', 'Резервные модемы'], '1'],
    s:['events', 'services', ['Service restarts', 'Перезапуски служб'], '1'],	/* ModemManager, QModem */
    k:['events', 'kernel', ['Router hardware', 'Сбои роутера'], '1'],	/* the kernel's words: dead USB controller, voltage, hangs */
    q:['events', 'quota', ['Traffic limits', 'Лимиты трафика'], '1'],
    r:['events', 'restart', ['Summary at start', 'Сводка при запуске'], '1'],	/* «MedveModem restarted» */
    f:['main', 'sms_forward', ['New SMS', 'Новые SMS'], '0'],
    h:['events', 'quiet', ['Quiet hours', 'Тихие часы'], '0'],
    u:['events', 'quiet_urgent', ['“No internet” with sound', '«Нет интернета» со звуком'], '1']
};
const MODES = ['smart', 'critical', 'all', 'off'];
const MODE_WORD = {smart:['smart', 'сводные'], critical:['important only', 'только важные'], all:['all at once', 'все сразу'],
                   off:['off', 'выключены']};
const MODE_NOTE = {
    smart:['one message per situation: a lost link after 90 s, a backup modem after 10 min, and how it ended',
           'объединённые уведомления: потеря связи — через 90 с, резервного модема — через 10 мин; затем итог восстановления'],
    critical:['only when the internet is gone and a traffic limit is used up',
              'только о потере интернета и исчерпании лимита трафика'],
    all:['every modem at once, backups too, after 90 s', 'о каждом модеме, включая резервные, через 90 с'],
    off:['the bot answers but sends nothing by itself', 'бот отвечает на команды, но не присылает уведомления']
};

// callback_data of these pages (ui.parseCallback hands every «g…» here)
//   g  gn  gp            the settings, the notifications, the proxies
//   gm                   the next notifications' mode
//   gt:<letter>          a kind switched (TOGGLES)
//   gq / ga              asks for the quiet hours / a proxy as text
//   ge:<sec> gc:<sec>    a proxy switched on or off, checked
//   gd:<sec> / gy:<sec>:<stamp>  a proxy's removal asked, confirmed
//   gk:<route>           a proxy found on the router checked (its route's name, «podkop:main»)
function parse(data) {
    if (data == 'g') return {kind:'set', page:'main'};
    if (data == 'gn') return {kind:'set', page:'notify'};
    if (data == 'gp') return {kind:'set', page:'proxies'};
    if (data == 'gm') return {kind:'set', page:'notify', op:'mode'};
    if (data == 'gq') return {kind:'set', page:'notify', op:'ask_quiet'};
    if (data == 'ga') return {kind:'set', page:'proxies', op:'ask_proxy'};
    let m = match(data, /^gt:([bsqrfhu])$/);
    if (m) return {kind:'set', page:'notify', op:'toggle', key:m[1]};
    m = match(data, /^g([ecd]):([A-Za-z0-9_]{1,32})$/);
    if (m) return {kind:'set', page:'proxies', op:{e:'enable', c:'check', d:'ask_delete'}[m[1]], sec:m[2]};
    m = match(data, /^gk:([A-Za-z0-9_.:-]{1,48})$/);
    if (m) return {kind:'set', page:'proxies', op:'check_local', name:m[1]};
    m = match(data, /^gy:([A-Za-z0-9_]{1,32}):([a-f0-9]{6})$/);
    if (m) return {kind:'set', page:'proxies', op:'delete', sec:m[1], stamp:m[2]};
    return null;
}

function on(n, letter) {
    let t = TOGGLES[letter], v = n[t[0]]?.[t[1]];
    return (v == null || v == '' ? t[3] : v) == '1';
}
function dotted(n, letter) { return (on(n, letter) ? '🟢 ' : '⚪ ') + W(TOGGLES[letter][2]); }
function modeOf(events) {
    if (events?.enabled == '0') return 'off';
    return MODE_WORD[events?.mode] ? events.mode : 'smart';
}
function nextMode(events) { return MODES[(index(MODES, modeOf(events)) + 1) % length(MODES)]; }
function quietSpan(events) { return (events?.quiet_from || '23:00') + '–' + (events?.quiet_to || '07:00'); }
function back(to) {
    return [{text:T('← Back', '← Назад'), callback_data:to}, {text:T('🏠 Menu', '🏠 Меню'), callback_data:'v:m'}];
}

// SSClash and nikki without a usable mixed port (the owner, 2026-11-03: «если не включено - рекомендуем включить для бота
// в первую очередь»): what to switch on, said above the proxies; the bot changes neither
function clashAdvice(list) {
    let out = [];
    for (let m in (type(list) == 'array' ? list : [])) {
        let name = m.name == 'nikki' ? 'nikki' : m.name == 'justclash' ? 'JustClash' : 'SSClash';
        if (m.state == 'no_mixed' && m.name == 'justclash')
            push(out, '💡 ' + T('JustClash\'s mixed port is off. Switch it on in JustClash\'s settings (the mixed port, with its login) and restart JustClash: the bot then reaches Telegram through it by itself. For the bot this comes first.',
                                'У JustClash выключен общий порт (Mixed Port). Включите его в настройках (с именем пользователя и паролем) и перезапустите JustClash: бот сам подключится к Telegram через этот прокси.'));
        else if (m.state == 'no_mixed')
            push(out, '💡 ' + (m.name == 'nikki'
                ? T('nikki has no mixed port. Set it in nikki\'s settings (Mixin → Mixed Port, e.g. 7890) and restart nikki: the bot then reaches Telegram through it by itself. For the bot this comes first.',
                    'У nikki не задан общий порт HTTP/SOCKS5. Укажите его в настройках nikki (Mixin → Mixed Port, например 7890) и перезапустите nikki: бот сам подключится к Telegram через этот прокси.')
                : sprintf(T('SSClash has no mixed port. Add the line %s to %s and restart Clash: the bot then reaches Telegram through it by itself. For the bot this comes first.',
                            'В SSClash не задан общий порт HTTP/SOCKS5. Добавьте строку %s в %s и перезапустите Clash: бот сам подключится к Telegram через этот прокси.'),
                          C('mixed-port: 7890'), C(m.config || '/opt/clash/config.yaml'))));
        else if (m.state == 'login')
            push(out, '💡 ' + sprintf(T('%s asks a login on its mixed port, and the bot found no usable one: give it a user with a password (authentication).',
                                        '%s требует авторизации на общем порту (Mixed Port), а подходящих данных бот не нашёл: задайте имя пользователя и пароль (authentication).'), name));
        else if (m.state == 'unread')
            push(out, '💡 ' + sprintf(T('%s: the bot could not read where its mixed port listens (%s).', '%s: бот не смог определить адрес и порт прокси (%s).'),
                                      name, C(m.config || '')));
    }
    return out;
}

// n: {events, main, sms_allowed, allowed, proxies:{configured, enabled, local}, clash}
function mainPage(n) {
    let mode = modeOf(n.events), p = n.proxies || {};
    let lines = ['⚙️ ' + B(T('Bot settings', 'Настройки бота')),
        T('Notifications: ', 'Уведомления: ') + B(W(MODE_WORD[mode])) +
            (on(n, 'h') ? T(' · quiet hours ', ' · тихие часы ') + C(quietSpan(n.events)) : ''),
        T('Proxies to Telegram: ', 'Прокси для Telegram: ') +
            (p.configured ? sprintf(T('%d of mine, %d on', 'добавлено вручную: %d, включено: %d'), p.configured, p.enabled) : T('none of mine', 'добавленных вручную нет')) +
            (p.local ? sprintf(T(' · found on the router: %d', ' · найдено на роутере: %d'), p.local) : '')];
    // SSClash's or nikki's mixed port to switch on: said here too, first among what the bot asks of the admin
    for (let a in clashAdvice(n.clash)) push(lines, a);
    if (!n.allowed)
        push(lines, I(T('Changing the settings from Telegram is off in LuCI: the pages only show them.',
                        'Изменение настроек через Telegram запрещено в LuCI. Доступен только просмотр.')));
    return {text:joined(lines, '\n'), markup:{inline_keyboard:[
        [{text:T('🔔 Notifications', '🔔 Уведомления'), callback_data:'gn'}, {text:T('🌐 Proxies', '🌐 Прокси'), callback_data:'gp'}],
        [{text:T('🏠 Menu', '🏠 Меню'), callback_data:'v:m'}]]}};
}

function notifyPage(n, note) {
    let mode = modeOf(n.events);
    let lines = ['🔔 ' + B(T('Notifications', 'Уведомления')),
        T('Mode: ', 'Режим: ') + B(W(MODE_WORD[mode])) + ' — ' + W(MODE_NOTE[mode]),
        on(n, 'h') ? T('Quiet hours ', 'Тихие часы ') + C(quietSpan(n.events)) +
            (on(n, 'u') ? T(': without sound, except “no internet” and a used-up limit', ': без звука, кроме «нет интернета» и исчерпанного лимита')
                        : T(': all without sound', ': всё без звука')) +
            T('. Nothing is lost.', '. Доставка не отключается.') : T('Quiet hours are off.', 'Тихие часы выключены.')];
    if (note) push(lines, note);
    let rows = [[{text:T('Mode: ', 'Режим: ') + W(MODE_WORD[mode]) + ' ↻', callback_data:'gm'}],
                [{text:dotted(n, 'b'), callback_data:'gt:b'}, {text:dotted(n, 's'), callback_data:'gt:s'}],
                [{text:dotted(n, 'q'), callback_data:'gt:q'}, {text:dotted(n, 'r'), callback_data:'gt:r'}]];
    if (n.sms_allowed) push(rows, [{text:dotted(n, 'f'), callback_data:'gt:f'}]);
    push(rows, [{text:dotted(n, 'h'), callback_data:'gt:h'}, {text:'⏰ ' + quietSpan(n.events), callback_data:'gq'}]);
    if (on(n, 'h')) push(rows, [{text:dotted(n, 'u'), callback_data:'gt:u'}]);
    push(rows, back('g'));
    return {text:joined(lines, '\n'), markup:{inline_keyboard:rows}};
}

// a proxy as it is shown: the scheme, the address and the login, never a password
function where(p) {
    let m = match(p.endpoint || '', /^([a-z0-9]+):\/\/(.*)$/);
    return (m ? m[1] + '://' : '') + (p.user ? p.user + '@' : '') + (m ? m[2] : p.endpoint || '');
}
// why a check failed, in words (the owner 2026-11-02: «не ответил» alone, and nothing listened on the address)
const CURL_WHY = {curl_exit_7:['nothing accepts connections at this address', 'по этому адресу соединения не принимаются'],
                  curl_exit_28:['no answer within 8 s', 'нет ответа за 8 с'], curl_exit_5:['the proxy\'s name does not resolve', 'не удалось определить IP-адрес прокси'],
                  curl_exit_97:['the proxy refused (a login needed or a wrong one)', 'прокси отклонил вход: проверьте имя пользователя и пароль'],
                  curl_exit_56:['the proxy cut the connection', 'прокси оборвал соединение'], curl_exit_35:['the TLS handshake failed', 'не удалось установить TLS'],
                  curl_exit_60:['a forged certificate on the way', 'не удалось проверить сертификат соединения'], curl_exit_52:['an empty answer', 'пустой ответ']};
const STATE_WHY = {bad_token:['Telegram refused the token', 'Telegram не принял токен'], forbidden:['Telegram refused the token', 'Telegram не принял токен'],
                   telegram_rejected:['Telegram refused the request', 'Telegram отклонил запрос'], rate_limited:['Telegram asks to wait', 'Telegram просит подождать']};
function failWords(c) {
    let w = (type(c.error) == 'string' && CURL_WHY[c.error]) || (type(c.state) == 'string' && STATE_WHY[c.state]) || null;
    return w ? T(w[0], w[1]) : null;
}
function checkWords(c, now) {
    if (!c) return null;
    if (c.running) return T('checking…', 'проверка…');
    // the owner, 2026-11-13: «звучит не по-русски» — «отвечает за 230 мс, проверено только что»
    let ago = now - (c.at || now) < 60 ? T('checked just now', 'проверено только что') :
              sprintf(T('checked %d min ago', 'проверено %d мин назад'), int((now - c.at) / 60));
    if (c.ok) return sprintf(T('answers in %d ms, %s', 'отвечает за %d мс, %s'), c.ms || 0, ago);
    let why = failWords(c);
    return why ? sprintf(T('no answer: %s, %s', 'не отвечает: %s, %s'), why, ago) : sprintf(T('no answer, %s', 'не отвечает, %s'), ago);
}

// list: [{sec, title, endpoint, user, enabled, current, check}], ctx: {local:[names], current, now, allowed, mode, note, clash}
function proxiesPage(list, ctx) {
    let lines = ['🌐 ' + B(T('Proxies to Telegram', 'Прокси для Telegram')),
        T('The bot tries them in this order, then ', 'Бот пробует их по очереди, затем ') +
            (ctx.mode == 'proxy' ? T('nothing else (only through a proxy)', 'больше ничего (только через прокси)')
                                 : T('the direct way and the other modems', 'напрямую и через другие модемы')) + '.'];
    for (let a in clashAdvice(ctx.clash)) push(lines, a);
    let rows = [];
    for (let p in list) {
        let c = checkWords(p.check, ctx.now);
        push(lines, (p.enabled ? '🟢 ' : '⚪ ') + B(p.title || p.sec) + ' — ' + C(where(p)));
        let more = [];
        if (p.current) push(more, T('the bot works through it now', 'бот сейчас работает через него'));
        if (c) push(more, c);
        if (length(more)) push(lines, '    ' + join('; ', more));
        // a loopback proxy where a manager's inbound listens on the same port at another address (the owner 2026-11-02:
        // 127.0.0.1:2080 refused, podkop listened on 192.168.1.1:2080)
        let lp = match(p.endpoint || '', /^socks5h?:\/\/(127\.[0-9.]+|localhost):([0-9]{1,5})$/);
        if (lp && p.check && !p.check.ok && !p.check.running)
            for (let e in (type(ctx.endpoints) == 'array' ? ctx.endpoints : [])) {
                let m = match(e, /^socks5h?:\/\/([^\/@]+):([0-9]{1,5})$/);
                if (m && m[2] == lp[2] && m[1] != lp[1]) {
                    push(lines, '   ↳ ' + sprintf(T('on this port the router listens at %s: change the address or remove this one, the bot takes that one by itself',
                                                    'на этом порту работает %s: исправьте адрес или удалите запись — бот обнаружит этот прокси автоматически'), C(e)));
                    break;
                }
            }
        if (!ctx.allowed) continue;
        // the name across the row (three in a row cut it), its check and removal below
        let name = label(p.title || p.sec);
        push(rows, [{text:(p.enabled ? T('⏸ Switch off “', '⏸ Выключить «') : T('▶️ Switch on “', '▶️ Включить «')) + name + T('”', '»'),
                     callback_data:'ge:' + p.sec}]);
        let tools = [];
        if (p.enabled) push(tools, {text:T('🔍 Check', '🔍 Проверить'), callback_data:'gc:' + p.sec});
        push(tools, {text:T('🗑 Remove', '🗑 Удалить'), callback_data:'gd:' + p.sec});
        push(rows, tools);
    }
    if (!length(list)) push(lines, I(T('No proxies of your own yet.', 'Прокси ещё не добавлены вручную.')));
    // the ones found on the router, each as the own ones (the owner, 2026-11-13: «как подкоп бот»): its address (never
    // a password), which one the bot is on, its last check and a check button; nothing to switch off: they are the
    // managers' own
    if (length(ctx.local)) {
        push(lines, T('Found on the router (podkop, SSClash, nikki, JustClash and the like), used by themselves:',
                      'Прокси на роутере (podkop, SSClash, nikki, JustClash и другие), найдены автоматически:'));
        let tools = [];
        // the program by its own name, its section when it has sections («podkop, секция main»; a clash's «mixed» is none)
        const PROGRAM = {podkop:'podkop', 'podkop-plus':'podkop-plus', forkop:'forkop', netshift:'netshift', ssclash:'SSClash',
                         nikki:'nikki', justclash:'JustClash'};
        for (let x in ctx.local) {
            if (type(x) == 'string') x = {name:x};
            let c = checkWords(x.check, ctx.now), at = type(x.endpoint) == 'string' ? replace(x.endpoint, /^[a-z0-9]+:\/\//, '') : null;
            let i = index(x.name, ':'), prog = i > 0 ? substr(x.name, 0, i) : x.name, sec = i > 0 ? substr(x.name, i + 1) : '';
            let title = (PROGRAM[prog] || prog) + (sec && sec != 'mixed' ? T(', section ', ', секция ') + sec : '');
            push(lines, '🔵 ' + B(title) + (at ? ' — ' + C(at) : '') + (x.user ? T(', user ', ', пользователь ') + C(x.user) : ''));
            let more = [];
            if (x.current) push(more, T('the bot works through it now', 'бот сейчас работает через него'));
            if (c) push(more, c);
            if (length(more)) push(lines, '    ' + join('; ', more));
            if (match(x.name, /^[A-Za-z0-9_.:-]{1,48}$/) && length(tools) < 6)
                push(tools, {text:'🔍 ' + label(x.name), callback_data:'gk:' + x.name});
        }
        for (let i = 0; i < length(tools); i += 2) push(rows, slice(tools, i, i + 2));
    }
    if (ctx.note) push(lines, ctx.note);
    let last = [];
    if (ctx.allowed) push(last, {text:T('➕ Add', '➕ Добавить'), callback_data:'ga'});
    push(last, {text:T('🔄 Refresh', '🔄 Обновить'), callback_data:'gp'});
    push(rows, last);
    push(rows, back('g'));
    return {text:joined(lines, '\n'), markup:{inline_keyboard:rows}};
}

function askQuiet(events) {
    return {text:'⏰ ' + B(T('Quiet hours', 'Тихие часы')) + '\n' +
        T('Send the start and the end, for example ', 'Пришлите начало и конец, например ') + C('23:00-07:00') +
        T(' (now ', ' (сейчас ') + C(quietSpan(events)) + ').\n' +
        T('In these hours notifications come without sound.', 'В эти часы уведомления приходят без звука.'),
        markup:{inline_keyboard:[[{text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'gn'}]]}};
}
function askProxy() {
    return {text:'➕ ' + B(T('A proxy to Telegram', 'Прокси для Telegram')) + '\n' +
        T('Send its address in one message:\n', 'Пришлите адрес одним сообщением:\n') +
        C('socks5h://host:port') + '\n' + C('socks5h://login:password@host:port#Name') + '\n' +
        T('or a ', 'или ссылку ') + C('https://t.me/socks?server=…') + T(' link. HTTP(S) proxies too; ', '. Подойдут и HTTP(S)-прокси; ') +
        T('MTProto (t.me/proxy) is for the Telegram app only.\n', 'MTProto (t.me/proxy) — только для приложения Telegram.\n') +
        T('The bot deletes your message at once and checks the proxy before using it.',
          'Бот сразу удалит ваше сообщение и проверит прокси, прежде чем им пользоваться.'),
        markup:{inline_keyboard:[[{text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'gp'}]]}};
}
function askDelete(p, now) {
    return {text:'🗑 ' + sprintf(T('Remove the proxy “%s” (%s)?', 'Удалить прокси «%s» (%s)?'), esc(p.title || p.sec), esc(where(p))),
        markup:{inline_keyboard:[[{text:T('🗑 Remove', '🗑 Удалить'), callback_data:'gy:' + p.sec + ':' + stamp(now)},
                                  {text:T('✖️ Cancel', '✖️ Отмена'), callback_data:'gp'}]]}};
}

// «23:00-07:00», «23-7», «23:00 07:00», «23.00–7.00»: {from, to} as HH:MM, or null
function parseQuiet(text) {
    let m = match(trim(text || ''), /^([0-9]{1,2})([:.]([0-9]{2}))?[ \t]*(-|–|—|\.\.| )[ \t]*([0-9]{1,2})([:.]([0-9]{2}))?$/);
    if (!m) return null;
    let h1 = int(m[1]), m1 = int(m[3] || '0'), h2 = int(m[5]), m2 = int(m[7] || '0');
    if (h1 > 23 || h2 > 23 || m1 > 59 || m2 > 59 || (h1 == h2 && m1 == m2)) return null;
    return {from:sprintf('%02d:%02d', h1, m1), to:sprintf('%02d:%02d', h2, m2)};
}

function unpercent(v) {
    let out = '', i = 0;
    while (i < length(v)) {
        let c = substr(v, i, 1);
        if (c == '%' && match(substr(v, i + 1, 2), /^[0-9A-Fa-f]{2}$/)) { out += chr(hex(substr(v, i + 1, 2))); i += 3; }
        else { out += c; i++; }
    }
    return out;
}
// a query string's value: «server=1.2.3.4&port=1080»
function queryValue(q, name) {
    for (let part in split(q, '&')) {
        let kv = split(part, '=', 2);
        if (kv[0] == name && length(kv) == 2) return unpercent(replace(kv[1], /\+/g, ' '));
    }
    return null;
}
// the admin's text as a proxy: {endpoint, username, password, title, socks5} or {error}. socks5:// becomes socks5h://
// (the names resolved by the proxy: a router's DNS may lie about api.telegram.org); a bare host:port is SOCKS5
function parseProxy(text) {
    let s = trim(text || ''), title = null, user = '', pass = '', socks5 = false;
    if (length(s) > 400 || match(s, /[\x01-\x1f]/)) return {error:'format'};
    if (match(s, /^(tg:\/\/proxy|https?:\/\/t\.me\/proxy)\?/)) return {error:'mtproto'};
    let link = match(s, /^(tg:\/\/socks|https?:\/\/t\.me\/socks)\?(.*)$/);
    if (link) {
        let host = queryValue(link[2], 'server'), port = queryValue(link[2], 'port');
        if (!host || !port) return {error:'format'};
        user = queryValue(link[2], 'user') || '';
        pass = queryValue(link[2], 'pass') || '';
        s = 'socks5h://' + host + ':' + port;
    } else {
        let hash = index(s, '#');
        if (hash >= 0) { title = trim(unpercent(substr(s, hash + 1))); s = substr(s, 0, hash); }
        if (!match(s, /^[a-z0-9]+:\/\//)) s = 'socks5h://' + s;
        let m = match(s, /^([a-z0-9]+):\/\/(.*)$/), scheme = m[1], rest = replace(m[2], /\/$/, '');
        if (scheme == 'socks5' || scheme == 'socks') { scheme = 'socks5h'; socks5 = true; }
        if (scheme != 'socks5h' && scheme != 'http' && scheme != 'https') return {error:'scheme'};
        let at = -1;
        for (let i = length(rest) - 1; i >= 0; i--) if (substr(rest, i, 1) == '@') { at = i; break; }
        if (at >= 0) {
            let info = substr(rest, 0, at), colon = index(info, ':');
            user = unpercent(colon >= 0 ? substr(info, 0, colon) : info);
            pass = colon >= 0 ? unpercent(substr(info, colon + 1)) : '';
            rest = substr(rest, at + 1);
        }
        s = scheme + '://' + rest;
    }
    let hp = match(s, /^(socks5h|http|https):\/\/([A-Za-z0-9.-]+|\[[0-9a-fA-F:]+\]):([0-9]{1,5})$/);
    if (!hp) return {error:'format'};
    if (int(hp[3]) < 1 || int(hp[3]) > 65535) return {error:'port'};
    if (length(user) && !match(user, /^[A-Za-z0-9_.@-]{1,80}$/)) return {error:'login'};
    if (length(user) && !length(pass)) return {error:'password'};
    if (!length(user) && length(pass)) return {error:'login'};
    if (length(pass) > 256 || match(pass, /[\x01-\x1f]/)) return {error:'password'};
    if (title != null) {
        title = replace(title, /['"\\<>]/g, '');
        title = length(title) ? substr(title, 0, 32) : null;
    }
    return {endpoint:s, username:user, password:pass, title, socks5};
}
const PROXY_ERROR = {
    format:['Not understood. An example: socks5h://host:1080 or socks5h://login:password@host:1080#Name',
            'Неверный адрес. Пример: socks5h://host:1080 или socks5h://user:pass@host:1080#Имя'],
    mtproto:['This is an MTProto proxy: only the Telegram app speaks it, the bot needs SOCKS5 or HTTP(S).',
             'MTProto-прокси предназначен для приложения Telegram. Боту нужен SOCKS5 или HTTP(S).'],
    scheme:['Only socks5h://, socks5://, http:// and https:// proxies.', 'Подходят только прокси socks5h://, socks5://, http:// и https://.'],
    port:['The port is from 1 to 65535.', 'Порт — от 1 до 65535.'],
    login:['The login: Latin letters, digits and . _ - @, up to 80.', 'Имя пользователя: латинские буквы, цифры и символы . _ - @, до 80 знаков.'],
    password:['A login needs its password (up to 256 characters).', 'Если задано имя пользователя, нужен пароль длиной до 256 знаков.'],
    bad_endpoint:['The address was not taken.', 'Адрес не принят.'], bad_port:['The port is from 1 to 65535.', 'Порт — от 1 до 65535.'],
    bad_username:['The login was not taken.', 'Имя пользователя не принято.'], bad_password:['The password was not taken.', 'Пароль не принят.'],
    bad_title:['The name was not taken.', 'Имя не принято.'],
    duplicate:['This proxy is in the list already.', 'Этот прокси уже есть в списке.'],
    too_many:['There are 20 proxies already: remove one first.', 'Достигнут предел: 20 прокси. Сначала удалите ненужный.'],
    settings_off:['Changing the settings from Telegram is off in LuCI.', 'Изменение настроек через Telegram запрещено в LuCI.'],
    sms_off:['SMS in Telegram are off in LuCI.', 'SMS в Telegram выключены в LuCI.'],
    no_such_proxy:['There is no such proxy any more.', 'Такого прокси уже нет.'],
    last_route:['This is the bot\'s only way to Telegram: add another proxy first, or allow the direct way in LuCI.',
                'Это единственный маршрут бота к Telegram. Сначала добавьте другой прокси или разрешите прямое подключение в LuCI.'],
    unavailable:['The router did not take the change (the settings service did not answer).',
                 'Роутер не принял изменение (служба настроек не ответила).']
};
function errorWords(code) { return W(PROXY_ERROR[code] || PROXY_ERROR.unavailable); }

// the result of an added proxy: {ok, section, enabled, check:{ok, ms, code}}
function addedWords(r, parsed) {
    let name = esc(parsed.title || r.section);
    if (r.check?.ok)
        return '✅ ' + sprintf(T('“%s” added and answers: %d ms. The bot uses it after the proxies above it.',
                                 '«%s» добавлен и отвечает за %d мс. Бот будет проверять его после прокси выше в списке.'), name, r.check.ms || 0) +
               (parsed.socks5 ? '\n' + T('socks5:// was written as socks5h://: Telegram\'s name is resolved by the proxy.',
                                       'socks5:// заменён на socks5h://: DNS-запросы к Telegram выполняются через прокси.') : '');
    return '⚠️ ' + sprintf(r.check?.code == 'proxy_auth' ? T('“%s” added switched off: the proxy refused the login or password.',
                                                            '«%s» добавлен, но выключен: прокси не принял имя пользователя или пароль.')
                                                          : T('“%s” added switched off: it did not answer through to Telegram. Check the address and switch it on.',
                                                            '«%s» добавлен выключенным: через него Telegram не ответил. Проверьте адрес и включите.'), name);
}

export { TOGGLES, parse, on, modeOf, nextMode, mainPage, notifyPage, proxiesPage, askQuiet, askProxy, askDelete, parseQuiet,
         parseProxy, errorWords, addedWords, where };
