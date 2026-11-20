'use strict';
'require baseclass';
'require rpc';
'require uci';
'require ui';

/*
 * What the bot's three pages share (r85, the owner 2026-10-23): «Обзор» (view/medvemodem/bot.js), «Мастер настройки» (bot-setup.js), «Настройки» (bot-settings.js).
 */
var rpcStatus = rpc.declare({ object: 'medvemodem.bot', method: 'status', reject: false });
var rpcQueue = rpc.declare({ object: 'medvemodem.bot', method: 'queue_status', reject: false });
var rpcTransport = rpc.declare({ object: 'medvemodem.bot', method: 'transport_status', reject: false });
var rpcSecret = rpc.declare({ object: 'medvemodem.bot.config', method: 'status', reject: false });
var rpcProbe = rpc.declare({ object: 'medvemodem.bot.config', method: 'transport_probe', reject: false });
/* the bot's own routes and their checks (audit R02: the page checks what the bot uses, by its own resolver) */
var rpcRoutes = rpc.declare({ object: 'medvemodem.bot', method: 'routes', reject: false });

var calls = {
	tokenSet: rpc.declare({ object: 'medvemodem.bot.config', method: 'token_set', params: ['token'], reject: true }),
	tokenClear: rpc.declare({ object: 'medvemodem.bot.config', method: 'token_clear', reject: true }),
	tokenCheck: rpc.declare({ object: 'medvemodem.bot.config', method: 'token_check', params: ['via'], reject: true }),
	chatTest: rpc.declare({ object: 'medvemodem.bot', method: 'chat_test', reject: true }),
	probe: rpc.declare({ object: 'medvemodem.bot', method: 'probe', params: ['kind', 'route'], reject: true }),
	routes: rpcRoutes,
	pairBegin: rpc.declare({ object: 'medvemodem.bot.config', method: 'pair_begin', reject: true }),
	pairStatus: rpc.declare({ object: 'medvemodem.bot.config', method: 'pair_status', reject: true }),
	pairConfirm: rpc.declare({ object: 'medvemodem.bot.config', method: 'pair_confirm', reject: true }),
	proxyPasswordSet: rpc.declare({ object: 'medvemodem.bot.config', method: 'proxy_password_set', params: ['password'], reject: true }),
	proxyPasswordClear: rpc.declare({ object: 'medvemodem.bot.config', method: 'proxy_password_clear', reject: true }),
	restart: rpc.declare({ object: 'medvemodem.bot.config', method: 'restart', reject: true }),
	why: rpc.declare({ object: 'medvemodem.bot.config', method: 'why', reject: false }),
	proxyCheck: rpc.declare({ object: 'medvemodem.bot.config', method: 'proxy_check', params: ['section'], reject: true }),
	uciSet: rpc.declare({ object: 'uci', method: 'set', params: ['config', 'section', 'values'], reject: true }),
	uciCommit: rpc.declare({ object: 'uci', method: 'commit', params: ['config'], reject: true }),
	uciAdd: rpc.declare({ object: 'uci', method: 'add', params: ['config', 'type', 'name', 'values'], reject: true })
};

function el(tag, attrs, children) { return E(tag, attrs || {}, children || []); }
function txt(value, fallback) { return value == null || value === '' ? (fallback || _('no data')) : String(value); }
function bool(v) { return v === true || v === 1 || v === '1'; }
function safe(result) { return result && typeof result === 'object' ? result : {}; }
function timeLabel(v) {
	if (!Number.isFinite(Number(v)) || Number(v) <= 0) return _('not yet');
	return new Date(Number(v) * 1000).toLocaleString(document.documentElement.lang || undefined);
}
function tokenFormat(value) { return /^\d{6,12}:[A-Za-z0-9_-]{30,}$/.test(String(value || '')); }
function telegramId(value) { return /^\d{3,20}$/.test(String(value || '')); }
function validInterface(value) {
	return !value || /^[A-Za-z0-9_.-]{1,15}$/.test(String(value)) ? true :
		_('Enter a Linux interface name (up to 15 characters, e.g., wwan0)');
}
function validEndpoint(value) {
	value = String(value || '');
	if (value.length > 256) return _('Proxy address cannot exceed 256 characters');
	/* Credentials in public UCI would leak. Never accept userinfo, paths or queries. */
	var m = /^(socks5h|http|https):\/\/([A-Za-z0-9.-]+|\[[0-9a-fA-F:]+\]):(\d{1,5})$/.exec(value);
	if (!m || Number(m[3]) < 1 || Number(m[3]) > 65535)
		return _('Format: socks5h://127.0.0.1:2080 or https://proxy.example:443 (without credentials)');
	return true;
}
function validPriority(value) {
	return /^\d{1,3}$/.test(String(value)) && +value <= 999 ? true : _('Priority: integer from 0 to 999');
}
/* tunnel: the direct call reached a fake-IP, the router's own tunnel (podkop's sing-box) carried it */
/* title: the modem's name for a modem route (the owner 2026-11-03: «имена или модели модемов, интерфейс в скобочках») */
function routeMessage(tier, tunnel, title) {
	if (tier === 'direct' && tunnel === true) return _('via router tunnel (sing-box)');
	if (tier === 'direct') return _('direct (no proxy)');
	if (String(tier || '').indexOf('emergency_') === 0) return _('emergency IP (with TLS verification)');
	/* routes.uc's names: modem:<dev>, <package>:<section> of a local mixed proxy, a proxy section's own name */
	if (/^modem:/.test(String(tier || ''))) return _('via modem %s').format(title && title !== String(tier).slice(6) ?
		title + ' (' + String(tier).slice(6) + ')' : String(tier).slice(6));
	if (/^(podkop|podkop-plus|forkop|netshift):/.test(String(tier || ''))) return _('local proxy %s').format(String(tier).split(':')[0]);
	if (/^ssclash:/.test(String(tier || ''))) return _('local proxy %s').format('SSClash');
	if (/^nikki:/.test(String(tier || ''))) return _('local proxy %s').format('nikki');
	if (tier && tier !== 'none') return _('proxy “%s”').format(tier);
	return _('no route selected');
}
/* SSClash and nikki without a usable mixed port (the owner, 2026-11-03: «если не включено - рекомендуем включить для
   бота в первую очередь»): what to switch on; the bot reads their configs only. list: the bot's routes reply .clash */
function clashAdvice(list) {
	return (Array.isArray(list) ? list : []).map(function(m) {
		var name = m && m.name === 'nikki' ? 'nikki' : 'SSClash';
		if (!m) return null;
		if (m.state === 'no_mixed')
			return m.name === 'nikki' ?
				_('nikki has no mixed port configured. Set it in nikki (Mixin → Mixed Port, e.g., 7890) and restart nikki so the bot can reach Telegram automatically (recommended first step).') :
				_('SSClash has no mixed port configured. Add %s to %s and restart Clash so the bot can reach Telegram automatically (recommended first step).')
					.format('mixed-port: 7890', String(m.config || '/opt/clash/config.yaml'));
		if (m.state === 'login')
			return _('%s requires authentication on its mixed port, but no valid credentials were found. Configure a username and password.').format(name);
		if (m.state === 'unread')
			return _('%s: failed to detect the mixed-port listen address (%s).').format(name, String(m.config || ''));
		return null;
	}).filter(function(x) { return x; });
}
/* the transport's states in words (second audit 2026-10-21: the code was shown as it is); an unknown one with its code */
var ROUTE_STATES = { ok: _('online'), checking: _('not tested yet'), network: _('cannot reach Telegram'), unreachable: _('cannot reach Telegram'),
	poll_conflict: _('another instance is already running with this token'), rate_limited: _('Telegram rate limit exceeded (retry later)'),
	tls_untrusted: _('TLS verification failed'), tls_failure: _('secure connection error'),
	bad_token: _('Telegram rejected the token'), forbidden: _('Access denied by Telegram (bot blocked?)'),
	telegram_rejected: _('Telegram rejected the request'), telegram_error: _('Telegram server error'),
	malformed_ack: _('unexpected response from Telegram'), malformed_poll: _('unexpected response from Telegram'),
	/* the bot's own codes in words (audit 12: they came through the general fallback) */
	task_unavailable: _('router failed to spawn request (low memory?): bot will restart automatically'),
	task_watchdog: _('request timed out and was aborted'), no_permitted_route: _('no Telegram route allowed by current settings'),
	offset_persistence: _('bot cannot write state file to /tmp') };
function routeState(s) { return !s ? _('no connection') : ROUTE_STATES[s] || _('operation failed (%s)').format(txt(s, '')); }
function routeInfo(info, polling) {
	info = safe(info);
	return routeState(info.state) + ' · ' + routeMessage(info.tier, info.tunnel) + ' · ' + _('last success: %s').format(timeLabel(info.last_ok)) +
		(polling && Number(info.conflicts) > 0 ? ' · ' + _('command polling conflicts: %s').format(info.conflicts) : '');
}
/* why the bot does not run (medvemodem-bot's why, by its code): the router's words are English, for its log */
var WHY_WORDS = {
	disabled: _('bot is disabled — use "Enable and start bot" on the bot page'),
	ucode_missing: _('ucode not found: install the ucode package'),
	ca_bundle_missing: _('missing TLS certificates: install ca-bundle'),
	token_missing: _('token missing or incomplete (see Step 1 on the bot page)'),
	/* the wizard's step by its name, not a number that moves (audit r101 R06: it sent to the one-time code it no longer has) */
	administrator_missing: _('no administrator set: enter your Telegram ID in Setup Wizard → Administrator'),
	secret_permissions: _('invalid token file permissions: run /usr/libexec/medvemodem-bot/init-secrets'),
	exits: _('checks passed, but the bot exited immediately after startup')
};
function whyWords(r) {
	var m = r && r.code === 'module_missing' && /ucode-mod-[a-z]+/.exec(r.words || '');
	return m ? _('missing module %s: install the corresponding package').format(m[0]) : WHY_WORDS[r && r.code] || (r && r.words) || '';
}

var CHECK_WORDS = {
	token_missing: _('token not saved'),
	token_rejected: _('Telegram rejected the token — verify it in @BotFather'),
	telegram_unreachable: _('Telegram is unreachable both directly and via proxy'),
	not_bot_api: _('Unexpected response instead of Telegram Bot API (likely intercepted by a proxy or filter)'),
	no_proxy: _('no proxy enabled'),
	no_route: _('no Telegram route allowed by current settings'),
	no_such_proxy: _('save settings first'),
	bad_endpoint: _('invalid proxy address'),
	proxy_failed: _('proxy unreachable or unable to reach Telegram'),
	proxy_auth: _('proxy rejected username or password')
};
function notify(message, error) {
	ui.addNotification(null, el('p', { 'class': error ? 'alert-message warning' : '' }, [message]));
}
/* a label and its value: one line on a wide screen, stacked on a phone */
function row(title, body) {
	return el('div', { 'class': 'mmb-row' }, [el('span', { 'class': 'mmb-row-label' }, [title]), el('span', { 'class': 'mmb-row-val' }, [body])]);
}
/* a coloured dot: ok green, bad red, wait amber, off grey */
var LAMP = { ok: '#2e9d4f', bad: '#d9534f', wait: '#e0a800', off: '#8a8f98' };
function indicator() {
	var dot = el('span', { 'class': 'mm-bot-lamp', 'data-state': 'off', 'aria-hidden': 'true',
		style: 'display:inline-block;width:.75rem;height:.75rem;border-radius:50%;margin-right:.45rem;vertical-align:middle;flex:none;background:' + LAMP.off });
	var words = el('span');
	var node = el('span', { style: 'display:inline-flex;align-items:center;flex-wrap:wrap' }, [dot, words]);
	node.set = function(state, text) {
		dot.setAttribute('data-state', state);
		dot.style.background = LAMP[state] || LAMP.off;
		words.textContent = text;
	};
	return node;
}
function uciGet(section, option) {
	try { return typeof uci.get === 'function' ? uci.get('medvemodem-bot', section, option) : null; } catch (e) { return null; }
}
/* the proxy sections as UCI has them: [{name, endpoint, enabled, user}] */
function proxies() {
	var out = [];
	try {
		(uci.sections('medvemodem-bot', 'proxy') || []).forEach(function(s) {
			out.push({ name: s['.name'], endpoint: s.endpoint || '', enabled: s.enabled === '1', user: s.proxy_username || '' });
		});
	} catch (e) {}
	return out;
}

/*
 * The bot's status as it answered, or why it did not: LuCI's rpc with reject:false resolves a failed call with
 * ubus's status code. A busy bot (it waits on its own call to the engine) times out (7) while it runs; only «not
 * found» (4) means its ubus object is gone (review 2026-10-22, HANDOFF 199 item 10: any failure read «не отвечает»).
 */
function statusCall() {
	return rpcStatus().then(function(r) {
		if (typeof r === 'number') return { unanswered: r === 7 ? 'timeout' : r === 4 ? 'gone' : 'error' };
		return r;
	}, function(e) { return { unanswered: /timed? ?out/i.test(String(e && e.message || e)) ? 'timeout' : 'error' }; });
}
/* everything the pages show, read only: {bot, queue, transport, secret, tls} */
function readAll() {
	return Promise.all([statusCall(), rpcQueue().catch(function() { return {}; }), rpcTransport().catch(function() { return {}; }),
		rpcSecret().catch(function() { return {}; }), rpcProbe().catch(function() { return {}; })]).then(function(d) {
		return { bot: safe(d[0]), queue: safe(d[1]), transport: safe(d[2]), secret: safe(d[3]), tls: safe(d[4]) };
	});
}
/* the first setup's four steps as the router sees them: the token, Telegram reached, the bot running, an administrator */
function setupDone(st, routeOk) {
	var bot = st.bot || {}, poll = safe((st.transport || {}).poll), send = safe((st.transport || {}).send);
	/* Telegram reached by either path, now or once (the owner, 2026-11-06: after every restart of the bot the overview
	 * said «steps left — 1 of 4» until the first long poll came back, while the sending already worked) */
	var reached = [poll, send].some(function(x) { return x.state === 'ok' || !!x.last_ok; });
	var done = { token: bool((st.secret || {}).token_configured), route: !!routeOk || (!!bot.running && reached),
		run: !!bot.running, admin: telegramId(uciGet('primary', 'user_id')) };
	done.left = ['token', 'route', 'run', 'admin'].filter(function(k) { return !done[k]; }).length;
	return done;
}
/* the pages' styles, once */
function css() {
	if (document.getElementById('mmb-css')) return;
	document.head.appendChild(el('style', { id: 'mmb-css' }, [
		'.mmb-check-detail{min-width:110px}.mmb-check-detail summary{cursor:pointer;white-space:nowrap}.mmb-check-detail summary:focus-visible{outline:2px solid currentColor;outline-offset:2px}.mmb-check-full{max-width:290px;padding:.4em 0;font-weight:400;overflow-wrap:anywhere}.mmb-check-detail .mmb-probe{margin:.2em 0 .4em}.mmb-role-empty{opacity:.5}.mmb-route-details{max-width:960px;border:1px solid rgba(127,127,127,.24);border-radius:7px;padding:.65em .9em}.mmb-route-summary{cursor:pointer;font-weight:600;line-height:1.6}.mmb-route-summary:focus-visible{outline:2px solid currentColor;outline-offset:3px}.mmb-route-scroll{max-width:100%;overflow-x:auto}.mmb-route-details[open] .mmb-route-summary{margin-bottom:.8em}.mmb-row{display:flex;justify-content:space-between;gap:1em;padding:.45em 0;border-bottom:1px solid rgba(127,127,127,.15)}.mmb-row-label{color:#888;flex:0 1 auto;min-width:0}.mmb-row-val{font-weight:500;text-align:right;min-width:0;overflow-wrap:anywhere}@media (max-width:600px){.mmb-row{flex-direction:column;gap:.2em}.mmb-row-val{text-align:left}}.mmb-card{max-width:760px;border:1px solid rgba(127,127,127,.25);border-radius:8px;padding:1em 1.2em;margin:.6em 0}.mmb-steps{display:flex;flex-wrap:wrap;gap:.4em;margin:.4em 0 .8em;padding:0;list-style:none}.mmb-steps button{border:1px solid rgba(127,127,127,.35);border-radius:999px;padding:.25em .8em;background:none;color:inherit;cursor:pointer;font:inherit}.mmb-steps button[aria-current="step"]{border-color:#2e9d4f;font-weight:700}.mmb-steps button.is-done{opacity:.7}.mmb-nav{display:flex;flex-wrap:wrap;gap:.5em;margin-top:1em}.mmb-line{display:flex;flex-wrap:wrap;gap:.5rem;align-items:center;margin:.5rem 0}.mmb-danger{border:1px solid #cc2b2b;border-radius:6px;padding:.8em;margin-top:.6em;max-width:640px}.mmb-routes{width:100%;max-width:960px;table-layout:fixed}.mmb-routes th:nth-child(1){width:36%}.mmb-routes th:nth-child(2){width:14%}.mmb-routes th:nth-child(3),.mmb-routes th:nth-child(4){width:25%}.mmb-check-detail summary{overflow:hidden;text-overflow:ellipsis}.mmb-routes td{overflow-wrap:anywhere;vertical-align:middle}.mmb-routes tr.is-current td{font-weight:600}@media (max-width:700px){.mmb-routes thead{display:none}.mmb-routes,.mmb-routes tbody,.mmb-routes tr,.mmb-routes td{display:block;width:auto}.mmb-routes tr{border:1px solid rgba(127,127,127,.25);border-radius:6px;margin:.4em 0;padding:.3em .5em}.mmb-routes td{padding:.15em 0;border:0}.mmb-routes td[data-title]::before{content:attr(data-title) ": ";color:#888;font-weight:400}}'
	]));
}

/* one route's last check in words: Telegram reached and the Bot API confirmed are two answers (audit R02) */
function checkWords(r) {
	var c = safe(r.checked), f = safe(r.failed);
	if (r.queued) return [_('checking…'), 'wait'];
	if (c.at) {
		var when = new Date(c.at * 1000).toLocaleTimeString(document.documentElement.lang || undefined, { hour: '2-digit', minute: '2-digit' });
		if (c.bot_api_ok) return [_('online: Bot API responded in %d ms (%s)').format(c.latency_ms || 0, when), 'ok'];
		if (c.transport_reached) return [_('Telegram reachable, but Bot API rejected the token (%s)').format(when), 'bad'];
		return [_('no connection: %s (%s)').format(routeState(c.state), when), 'bad'];
	}
	if (f.at) return [_('no response at %s: %s').format(new Date(f.at * 1000).toLocaleTimeString(document.documentElement.lang || undefined,
		{ hour: '2-digit', minute: '2-digit' }), routeState(f.state)), 'bad'];
	return [_('not tested'), 'off'];
}
var KIND_WORDS = { proxy: _('proxy'), direct: _('direct'), modem: _('via modem'), emergency: _('emergency IP') };

return baseclass.extend({
	calls: calls, el: el, txt: txt, bool: bool, safe: safe, timeLabel: timeLabel, tokenFormat: tokenFormat,
	telegramId: telegramId, validInterface: validInterface, validEndpoint: validEndpoint, validPriority: validPriority,
	routeMessage: routeMessage, clashAdvice: clashAdvice, routeState: routeState, routeInfo: routeInfo, CHECK_WORDS: CHECK_WORDS, whyWords: whyWords, notify: notify,
	row: row, indicator: indicator, uciGet: uciGet, proxies: proxies, statusCall: statusCall, readAll: readAll,
	setupDone: setupDone, css: css, checkWords: checkWords, KIND_WORDS: KIND_WORDS
});
