'use strict';
import * as fs from 'fs';

// All routes are discovered without altering any external UCI package or daemon.
// This module never reads proxy URLs that embed credentials.
function validPort(value) {
    let n = int('' + value);
    return match('' + value, /^\d{1,5}$/) && n >= 1 && n <= 65535 ? n : null;
}

function validProxy(value) {
    if (type(value) != 'string' || length(value) > 256) return false;
    if (!match(value, /^(socks5h|http|https):\/\/([A-Za-z0-9.-]+|\[[0-9a-fA-F:]+\]):\d{1,5}$/)) return false;
    let m = match(value, /:(\d{1,5})$/);
    return m && validPort(m[1]) != null;
}

const PRIVATE_LISTEN = /^(127\.[0-9.]+|10\.[0-9.]+|192\.168\.[0-9.]+|172\.[0-9.]+|\[::1\])$/;
// null: no config with a SOCKS inbound read (being rewritten, not there yet, or only the stock one without any); '': the
// manager's config has no such inbound; else the address
function findListen(port, paths) {
    let parsed = false;
    for (let path in paths) {
        // A small, bounded read: avoid parsing an arbitrarily large generated config.
        let content = fs.readfile(path, 524288);
        if (!content) continue;
        let document;
        try { document = json(content); } catch (e) { continue; }
        if (type(document) != 'object') continue;
        for (let inbound in (type(document.inbounds) == 'array' ? document.inbounds : [])) {
            if (inbound.type != 'mixed' && inbound.type != 'socks' && inbound.type != 'socks5') continue;
            parsed = true;	/* a manager's config: a stock one without SOCKS inbounds says nothing (podkop 0.8, 2026-11-02) */
            if (int(inbound.listen_port || -1) != port) continue;
            let ip = inbound.listen || '';
            if (ip == '0.0.0.0' || ip == '::' || ip == '[::]' || ip == '') return '127.0.0.1';
            if (ip == '::1') return '[::1]';
            // Only connect to locally bound, explicitly configured socket addresses.
            if (match(ip, PRIVATE_LISTEN)) return ip;
        }
    }
    return parsed ? '' : null;
}

// where a manager's mixed inbound listens when its config can't be read: podkop's own rule, the service listen address
// or else the LAN's (podkop's own default)
function listenGuess(cursor, settings) {
    let ip = settings?.service_listen_address;
    if (type(ip) != 'string' || ip == '') {
        ip = (cursor.get_all('network') || {}).lan?.ipaddr;
        if (type(ip) == 'array') ip = ip[0];
    }
    ip = type(ip) == 'string' ? split(ip, '/')[0] : '';
    return match(ip, PRIVATE_LISTEN) ? ip : '127.0.0.1';
}

function validPublicV4(ip) {
    if (type(ip) != 'string' || !match(ip, /^([0-9]{1,3}\.){3}[0-9]{1,3}$/)) return false;
    let octets = split(ip, '.');
    for (let part in octets) if (int(part) > 255 || (length(part) > 1 && substr(part, 0, 1) == '0')) return false;
    let a = int(octets[0]), b = int(octets[1]);
    // Do not permit local, loopback, reserved or multicast addresses as emergency endpoints.
    if (a == 0 || a == 10 || a == 127 || a >= 224 || a == 169 && b == 254 ||
        a == 192 && b == 0 || a == 192 && b == 88 ||
        a == 198 && (b == 18 || b == 19 || b == 51) || a == 203 && b == 0 ||
        a == 172 && b >= 16 && b <= 31 || a == 192 && b == 168 || a == 100 && b >= 64 && b <= 127)
        return false;
    return true;
}

// links: the engine's other modems (uplink paths that do not carry the default route), {device, name, role}
function modemRoutes(links) {
    let out = [], rank = {backup_ready:0, backup_limited:1, backup_not_ready:2};
    let list = [];
    for (let l in (type(links) == 'array' ? links : []))
        if (type(l) == 'object' && type(l.device) == 'string' && match(l.device, /^[A-Za-z0-9_.-]{1,15}$/) &&
            rank[l.role] != null)
            push(list, l);
    list = sort(list, (a, b) => rank[a.role] != rank[b.role] ? rank[a.role] - rank[b.role] : (a.device < b.device ? -1 : 1));
    for (let l in list) {
        if (length(out) >= 4) break;
        // the name is the device (a code in the journal); the modem's own name is for the admin's eyes
        push(out, {name:'modem:' + l.device, title:type(l.name) == 'string' ? l.name : null, proxy:null,
                   source:'modem', auth:null, resolve:null, bind_interface:'if!' + l.device});
    }
    return out;
}

// a manager section's mixed inbound login (podkop's mixed_proxy_auth_enabled/_username/_password;
// audit R01, 2026-10-30: a local inbound with auth got none and refused the bot).
// null: no login asked; false: asked but not usable (a missing or odd username, an empty password, a line break)
function mixedAuth(section) {
    if (section.mixed_proxy_auth_enabled != '1') return null;
    let u = section.mixed_proxy_username, p = section.mixed_proxy_password;
    if (type(u) != 'string' || !match(u, /^[A-Za-z0-9_.@-]{1,80}$/) || type(p) != 'string' || !length(p) ||
        length(p) > 256 || index(p, '\n') >= 0 || index(p, '\r') >= 0)
        return false;
    return u + ':' + p;
}

// SSClash and nikki (the owner, 2026-11-03: «SSClash и nikki только детектим и ищем mixed proxy и добавляем его боту, и
// если не включено - рекомендуем включить для бота в первую очередь»): both run mihomo; the bot only reads where its
// mixed (or SOCKS) port listens and its login, never starts, edits or restarts either. SSClash, the legacy luci-app
// (/etc/init.d/clash) and SSClash-Go (/etc/init.d/ssclash), keeps the one config /opt/clash/config.yaml, whose stock
// copy has no mixed-port; nikki writes the config it runs to /etc/nikki/run/config.yaml from its UCI (mixin.mixed_port,
// 7890 by default; the login from its «authentication» sections). Behaviour of zerolabnet/SSClash, SSClash-Go and
// nikkinikki-org/OpenWrt-nikki, studied, not copied.
const CLASH = {
    ssclash_init: ['/etc/init.d/clash', '/etc/init.d/ssclash'], ssclash_config: '/opt/clash/config.yaml',
    nikki_init: '/etc/init.d/nikki', nikki_run: '/etc/nikki/run/config.yaml',
    // JustClash (the owner, 2026-11-13): mihomo too; its UCI, not the config it generates in /tmp/justclash
    justclash_init: '/etc/init.d/justclash', justclash_config: '/etc/config/justclash'
};
const USER = /^[A-Za-z0-9_.@-]{1,80}$/;
function goodLogin(u, p) {
    return type(u) == 'string' && match(u, USER) && type(p) == 'string' && length(p) && length(p) <= 256 &&
        index(p, '\n') < 0 && index(p, '\r') < 0;
}
function unquote(v) {
    v = trim(v);
    let m = match(v, /^"(.*)"$/) || match(v, /^'(.*)'$/);
    return m ? m[1] : v;
}
// a mihomo config's top-level keys the bot needs: mixed-port, socks-port, bind-address, allow-lan, authentication
function clashYaml(content) {
    let out = {auth:[]}, inAuth = false;
    for (let line in split(content, '\n')) {
        line = rtrim(line, '\r');
        if (match(line, /^[ \t]*(#.*)?$/)) continue;
        let m = match(line, /^([A-Za-z-]+)[ \t]*:[ \t]*(.*)$/);
        if (m) {
            let v = replace(m[2], /[ \t]+#.*$/, '');
            inAuth = m[1] == 'authentication';
            if (inAuth && match(trim(v), /^\[.*\]$/)) {
                for (let x in split(substr(trim(v), 1, length(trim(v)) - 2), ','))
                    if (length(trim(x))) push(out.auth, unquote(x));
                inAuth = false;
            } else if (index(['mixed-port', 'socks-port', 'bind-address', 'allow-lan'], m[1]) >= 0)
                out[m[1]] = unquote(v);
            continue;
        }
        if (!inAuth) continue;
        let a = match(line, /^[ \t]+-[ \t]*(.*)$/);
        if (a) push(out.auth, unquote(replace(a[1], /[ \t]+#.*$/, '')));
        else if (!match(line, /^[ \t]/)) inAuth = false;
    }
    return out;
}
// the endpoint and login of a mihomo config: {port, endpoint, auth (null none, false asked and not usable)}
function clashListen(y) {
    let port = validPort(y['mixed-port'] || '');
    if (port == null) port = validPort(y['socks-port'] || '');
    if (port == null) return {port:null};
    let ip = y['bind-address'] || '*';
    // allow-lan off: mihomo listens on the loopback only
    if (y['allow-lan'] != 'true' || ip == '*' || ip == '0.0.0.0' || ip == '::' || ip == '[::]') ip = '127.0.0.1';
    else if (ip == '::1') ip = '[::1]';
    else if (!match(ip, PRIVATE_LISTEN)) return {port, endpoint:null};
    let auth = null;
    if (length(y.auth)) {
        auth = false;
        for (let x in y.auth) {
            let i = index(x, ':');
            if (i > 0 && goodLogin(substr(x, 0, i), substr(x, i + 1))) { auth = x; break; }
        }
    }
    return {port, endpoint:'socks5h://' + ip + ':' + port, auth};
}
function clashRead(path) {
    let content = fs.readfile(path, 524288);
    return content ? clashYaml(content) : null;
}
// what the router has of the two: [{name, state: 'ok'|'no_mixed'|'off'|'login'|'unread', endpoint?, auth?, config}];
// the auth stays in here: what leaves (the ubus reply, the pages) is clashHints's copy without it
function clashFound(cursor) {
    let out = [];
    for (let init in CLASH.ssclash_init) {
        if (!fs.access(init)) continue;
        let y = clashRead(CLASH.ssclash_config);
        let l = y ? clashListen(y) : {port:null};
        push(out, {name:'ssclash', config:CLASH.ssclash_config, endpoint:l.endpoint, auth:l.auth,
                   state:!y ? 'unread' : l.port == null ? 'no_mixed' : !l.endpoint ? 'unread' : l.auth === false ? 'login' : 'ok'});
        break;
    }
    if (fs.access(CLASH.nikki_init)) {
        let n = cursor.get_all('nikki') || {}, y = clashRead(CLASH.nikki_run), l;
        if (y) l = clashListen(y);
        else {
            // not started yet: its UCI, as nikki writes it into the run config
            let mx = n.mixin || {};
            y = {'mixed-port':mx.mixed_port || '', 'allow-lan':mx.allow_lan == '1' ? 'true' : 'false', auth:[]};
            if (mx.authentication == '1')
                for (let k, s in n)
                    if (s['.type'] == 'authentication' && s.enabled != '0') push(y.auth, (s.username || '') + ':' + (s.password || ''));
            l = clashListen(y);
        }
        push(out, {name:'nikki', config:CLASH.nikki_run, endpoint:l.endpoint, auth:l.auth,
                   state:n.config?.enabled == '0' ? 'off' : l.port == null ? 'no_mixed' : !l.endpoint ? 'unread' :
                         l.auth === false ? 'login' : 'ok'});
    }
    // JustClash: the mixed port is off by default (proxy.use_mixed_port), 1080, on the address of an interface (lan by
    // default, «-» every address) and always behind a login (list proxy_authentication 'user:password'; only 127.0.0.1
    // skips it, and the port is not there when bound to the LAN address). saltymonkey/justclash, studied, not copied
    if (fs.access(CLASH.justclash_init)) {
        let j = (cursor.get_all('justclash') || {}).proxy || {}, l = {port:null};
        if (j.use_mixed_port == '1') {
            let iface = j.mixed_port_bind_interface || 'lan', ip = '*';
            if (iface != '-') {
                ip = (cursor.get_all('network') || {})[iface]?.ipaddr;
                if (type(ip) == 'array') ip = ip[0];
                ip = type(ip) == 'string' ? split(ip, '/')[0] : '';
            }
            let auth = j.proxy_authentication;
            auth = type(auth) == 'array' ? auth : type(auth) == 'string' ? [auth] : [];
            l = clashListen({'mixed-port':j.mixed_port || '1080', 'allow-lan':'true', 'bind-address':ip || 'none', auth});
        }
        push(out, {name:'justclash', config:CLASH.justclash_config, endpoint:l.endpoint, auth:l.auth,
                   state:l.port == null ? 'no_mixed' : !l.endpoint ? 'unread' : l.auth === false ? 'login' : 'ok'});
    }
    return out;
}

function discover(cursor, pollCfg, sendCfg, proxyPassword, links) {
    let routes = [], seen = {};
    function add(name, endpoint, source, auth, title) {
        if (!validProxy(endpoint) || seen[endpoint] || length(routes) >= 8) return;
        seen[endpoint] = true;
        let r = {name, proxy:endpoint, source, auth:auth || null, resolve:null};
        if (title) r.title = title;
        push(routes, r);
    }
    // proxyPassword: the one LuCI sets for all, or {shared, per:{section: password}} with the ones added from Telegram
    let shared = type(proxyPassword) == 'object' ? proxyPassword.shared : proxyPassword;
    let per = type(proxyPassword) == 'object' && type(proxyPassword.per) == 'object' ? proxyPassword.per : {};
    // User-defined routes have precedence and a stable, deterministic ordering.
    let c = cursor.get_all('medvemodem-bot') || {};
    let custom = [];
    for (let sectionName, section in c) {
        if (section['.type'] != 'proxy' || section.enabled != '1') continue;
        if (!validProxy(section.endpoint)) continue;
        let username = type(section.proxy_username) == 'string' ? section.proxy_username : '';
        // Never put credentials in ordinary UCI endpoints or diagnostic route names.
        // Username is non-secret UCI, one shared password lives in root-only UCI.
        if (username && !match(username, /^[A-Za-z0-9_.@-]{1,80}$/)) continue;
        let pw = per[sectionName] || shared;
        let title = type(section.title) == 'string' && match(section.title, /^[^\x01-\x1f'"\\<>]{1,32}$/) ? section.title : null;
        push(custom, {sectionName, endpoint:section.endpoint, order:int(section.priority || 100), title,
                     type:section.role || 'fallback', auth:username && pw ? username + ':' + pw : null});
    }
    custom = sort(custom, (a, b) => a.order == b.order ? (a.sectionName < b.sectionName ? -1 : 1) : a.order - b.order);
    for (let item in custom)
        if (item.type == 'primary') add(item.sectionName, item.endpoint, 'configured', item.auth, item.title);
    // a configured fallback with a login for the same address as a local inbound wins over the inbound found
    // without one: the admin's entry is the one that works (audit R01)
    let authed = {};
    for (let item in custom) if (item.auth) authed[item.endpoint] = true;
    // Read existing local SOCKS listeners, never start or reconfigure a manager.
    // Forkop X uses the same forkop UCI namespace as Forkop. A manager gives every section with mixed_proxy_enabled its
    // own inbound, a VPN section's too (podkop's configure_section_mixed_proxy); its sing-box config is where its
    // settings.config_path says (the owner 2026-10-28: the bot lost podkop's and forkop's proxies)
    for (let pkg in ['podkop', 'podkop-plus', 'forkop', 'netshift']) {
        let settings = cursor.get_all(pkg) || {};
        let paths = [];
        let own = settings.settings?.config_path;
        if (type(own) == 'string' && match(own, /^\/[A-Za-z0-9_.\/-]{1,200}$/)) push(paths, own);
        if (own != '/etc/sing-box/config.json') push(paths, '/etc/sing-box/config.json');
        for (let sectionName, section in settings) {
            if (section['.type'] != 'section' || section.enabled == '0') continue;
            if (section.mixed_proxy_enabled != '1') continue;
            let action = section.connection_type || section.action || '';
            if (action == 'block' || action == 'exclusion') continue;
            let port = validPort(section.mixed_proxy_port || '2080');
            if (port == null) continue;
            let bind = findListen(port, paths);
            // a config being rewritten (a manager's restart) does not drop the route for the next five minutes, nor does
            // a config without any SOCKS inbound (podkop 0.8 keeps its own elsewhere, the stock /etc/sing-box/config.json
            // has none): the manager's own rule then, its LAN address (the owner 2026-11-02: the bot found nothing while
            // podkop listened on 192.168.1.1:2080)
            if (bind == null) bind = listenGuess(cursor, settings.settings);
            let auth = mixedAuth(section), endpoint = bind ? 'socks5h://' + bind + ':' + port : null;
            if (!endpoint || auth === false) continue;	/* a login asked and not readable: no route that fails */
            if (!auth && authed[endpoint]) continue;
            add(pkg + ':' + sectionName, endpoint, 'local', auth);
        }
        // forkop 2.0 moved the mixed proxy out of the sections: a «server» of protocol mixed (listen, listen_port; its
        // login is a server_client of it, mixed_auth_enabled on by default). The sections' own above stay for 1.x
        if (pkg == 'forkop')
            for (let sectionName, server in settings) {
                if (server['.type'] != 'server' || server.protocol != 'mixed' || server.enabled == '0') continue;
                let port = validPort(server.listen_port || '');
                if (port == null) continue;
                let ip = server.listen || '::';
                ip = ip == '::' || ip == '0.0.0.0' || ip == '' ? '127.0.0.1' : ip == '::1' ? '[::1]' : ip;
                if (!match(ip, PRIVATE_LISTEN)) continue;
                let auth = null;
                if (server.mixed_auth_enabled != '0')
                    for (let cn, client in settings) {
                        if (client['.type'] != 'server_client' || client.server != sectionName) continue;
                        let u = client.label || cn, p = client.password;
                        if (type(u) == 'string' && match(u, /^[A-Za-z0-9_.@-]{1,80}$/) && type(p) == 'string' && length(p) &&
                            length(p) <= 256 && index(p, '\n') < 0 && index(p, '\r') < 0)
                            auth = u + ':' + p;
                        break;
                    }
                let endpoint = 'socks5h://' + ip + ':' + port;
                if (!auth && authed[endpoint]) continue;
                add('forkop:' + sectionName, endpoint, 'local', auth);
            }
    }
    // SSClash's, nikki's and JustClash's mixed port, after the sing-box managers'
    let clash = clashFound(cursor);
    for (let m in clash) {
        if (m.state != 'ok' || (!m.auth && authed[m.endpoint])) continue;
        add(m.name + ':mixed', m.endpoint, 'local', m.auth);
    }
    for (let item in custom)
        if (item.type != 'primary') add(item.sectionName, item.endpoint, 'configured', item.auth, item.title);
    let direct = {name:'direct',proxy:null,source:'direct',auth:null,resolve:null};
    // Emergency IPs are opt-in, configured by the owner, and always subject to
    // the normal api.telegram.org TLS chain/hostname verification. No -k.
    let emergencies = [];
    let ips = c.main?.emergency_ip;
    if (type(ips) == 'string') ips = [ips];
    for (let ip in (type(ips) == 'array' ? ips : [])) {
        if (!validPublicV4(ip) || length(emergencies) >= 4) continue;
        push(emergencies, {name:'emergency_' + (length(emergencies)+1),proxy:null,source:'emergency',auth:null,
                          resolve:'api.telegram.org:443:' + ip});
    }
    function withInterface(route, cfg) {
        let bind = cfg.bind_interface || '';
        if (!match(bind, /^[A-Za-z0-9_.-]{1,15}$/)) return route;
        return {name:route.name,proxy:route.proxy,source:route.source,auth:route.auth,
                resolve:route.resolve,bind_interface:bind};
    }
    let result = {};
    for (let kind in ['poll','send']) {
        let cfg = kind == 'poll' ? pollCfg : sendCfg;
        let mode = cfg.mode || 'auto';
        let options = [];
        if (mode != 'direct') for (let route in routes) push(options, route);
        if (mode == 'direct' || (mode != 'proxy' && cfg.fallback_direct != '0')) {
            push(options, withInterface(direct, cfg));
            // the main modem failed: the others, each through its own interface (owner, 2026-10-20); not when the
            // admin pinned direct requests to one interface
            if (cfg.modems != '0' && !match(cfg.bind_interface || '', /^[A-Za-z0-9_.-]{1,15}$/))
                for (let r in modemRoutes(links)) push(options, r);
            if (mode != 'direct') for (let emergency in emergencies) push(options, withInterface(emergency, cfg));
        }
        // A route restriction must not silently broaden to an unwanted direct path.
        result[kind] = options;
    }
    // what the pages say of them, without the login
    result.clash = map(clash, (m) => ({name:m.name, state:m.state, endpoint:m.endpoint || null, config:m.config}));
    return result;
}

// ucode as pinned by OpenWrt 24.10 and 25.12 does not parse `export function` declarations
export { validProxy, validPublicV4, discover, CLASH };
