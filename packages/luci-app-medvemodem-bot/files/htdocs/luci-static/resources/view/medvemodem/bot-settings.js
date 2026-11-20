'use strict';
'require view';
'require uci';
'require form';
'require medvemodem.botcommon as bc';

/*
 * «Telegram-бот» → «Настройки» (r85): one form in groups — Основные, SMS в
 * Telegram, Администратор, Уведомления, Выход в Telegram, Прокси — then the proxy password, the actions and, folded,
 * the danger zone. Nothing is empty or folded away with nothing inside (the owner, 2026-10-22: «каша»).
 */
var el = bc.el, txt = bc.txt, indicator = bc.indicator, calls = bc.calls;
/* a result said in words, no lamp: a settings page is no status board (the owner, 2026-11-03); red only for a failure */
function sayLine() {
	var node = el('p', { 'class': 'mmb-say', hidden: true });
	node.set = function(state, words) {
		node.textContent = words || '';
		node.hidden = !words;
		node.style.color = state === 'bad' ? 'var(--error-color-medium, #cc2b2b)' : '';
	};
	return node;
}

return view.extend({
	load: function() {
		return Promise.all([uci.load('medvemodem-bot').then(function() { return true; }, function() { return false; }), bc.readAll()]);
	},

	makeForm: function() {
		var map = new form.Map('medvemodem-bot', _('Bot settings'),
			_('Settings below apply to the Telegram bot only. Click "Save & Apply" — changes take effect after the bot restarts.'));

		/* a group's title inside a section (CBI has no fieldset inside a NamedSection) */
		var n = 0;
		function group(sec, title, hint) {
			var d = sec.option(form.DummyValue, '_grp_' + (++n));
			d.rawhtml = true;
			d.cfgvalue = function() {
				return '<h3 style="margin:.8em 0 .2em;border-bottom:1px solid rgba(127,127,127,.25);padding-bottom:.2em">' + title + '</h3>' +
					(hint ? '<div class="cbi-value-description">' + hint + '</div>' : '');
			};
			return d;
		}

		var main = map.section(form.NamedSection, 'main', 'bot');
		main.addremove = false;
		group(main, _('General'));
		var o = main.option(form.Flag, 'enabled', _('Enable bot'));
		o.default = '0';
		o.description = _('For initial setup, use the "Setup wizard".');
		o = main.option(form.ListValue, 'lang', _('Bot language'));
		o.value('auto', _('Match admin\'s Telegram language'));
		o.value('ru', _('Russian'));
		o.value('en', _('English'));
		o.default = 'auto';
		o.description = _('"Match admin\'s Telegram language": follows the admin\'s Telegram app language after their first message or click (defaults to Russian until then).');
		o = main.option(form.Flag, 'allow_control', _('Allow modem control from Telegram'));
		o.default = '0';
		o.description = _('Shows control buttons on modem cards (set primary, connect/disconnect, reconnect, pause management). Disruptive actions require confirmation. When off, the bot is view-only.');
		o = main.option(form.Flag, 'allow_settings', _('Allow changing settings from Telegram'));
		o.default = '1';
		o.description = _('Allows changing notification mode, alert types, quiet hours, and proxies in bot "⚙️ Settings". When off, those screens are view-only.');
		o = main.option(form.ListValue, 'snapshot_minutes', _('Save state snapshot to flash'));
		o.value('0', _('Disabled (do not write)'));
		o.value('15', _('At most every 15 minutes'));
		o.description = _('Anonymized hashes and counters only; excludes SMS and SIM identifiers.');

		group(main, _('SMS to Telegram'), _('Visible only to the bot admin. Messages remain unread on the router and are not stored by the bot.'));
		o = main.option(form.Flag, 'allow_sms_content', _('Show SMS'));
		o.default = '0';
		o.description = _('Enables the "Incoming SMS" button in the bot (recent messages with sender, modem, SIM, and router timestamp).');
		o = main.option(form.Flag, 'sms_forward', _('Send new SMS'));
		o.default = '0';
		o.depends('allow_sms_content', '1');
		o.description = _('Automatically forwards each incoming SMS as a separate message, adding a "Copy" button for verification codes.') + ' ' +
			_('When MedveModem "SMS forwarding" is enabled for specific modems, the bot forwards from those modems only.');
		o = main.option(form.Flag, 'allow_send', _('Allow sending SMS and USSD from Telegram'));
		o.default = '0';
		o.description = _('Enables /send and /ussd commands, plus "Write SMS" and "Reply" buttons. Outgoing SMS require confirmation of modem, recipient, and text; USSD codes are sent immediately. Carrier charges may apply.');

		var admin = map.section(form.NamedSection, 'primary', 'administrator');
		admin.addremove = false;
		group(admin, _('Administrator'), _('The bot responds only to this user ID.'));
		o = admin.option(form.Value, 'user_id', _('Telegram user ID'));
		o.placeholder = '123456789';
		o.description = _('Numeric from.id, not @username.');
		o.validate = function(s, v) { return !v || bc.telegramId(v) ? true : _('Enter a positive numeric Telegram user ID'); };
		o = admin.option(form.Value, 'chat_id', _('Private chat ID'));
		o.placeholder = _('same as user ID');
		o.description = _('Optional: for private chats, this matches your user ID. Groups and channels are not supported.');
		o.validate = function(s, v) { return !v || bc.telegramId(v) ? true : _('Enter a positive numeric chat ID'); };

		var events = map.section(form.NamedSection, 'events', 'notifications');
		events.addremove = false;
		group(events, _('Notifications'));
		o = events.option(form.Flag, 'enabled', _('Send modem notifications'));
		o.default = '1';
		o = events.option(form.ListValue, 'mode', _('Which'));
		o.value('smart', _('Smart mode (groups brief outages)'));
		o.value('all', _('All confirmed events'));
		o.value('critical', _('Critical alerts only'));
		o.depends('enabled', '1');
		o.description = _('Frequent modem and uplink flaps are grouped to prevent alert spam.');
		[['backup', _('Backup modem alerts'), _('When off, backup modem events are suppressed while another uplink provides internet.')],
		 ['services', _('ModemManager and QModem restarts'), null],
		 ['kernel', _('Router hardware faults: USB controller, power, hangs'), null],
		 ['quota', _('Data caps'), null],
		 ['restart', _('“MedveModem restarted”'), _('Sends a single silent summary after a service restart or update.')]].forEach(function(t) {
			o = events.option(form.Flag, t[0], t[1]);
			o.default = '1';
			o.depends('enabled', '1');
			if (t[2]) o.description = t[2];
		});
		o = events.option(form.Flag, 'quiet', _('Quiet hours'));
		o.default = '0';
		o.depends('enabled', '1');
		o.description = _('Notifications are delivered silently during these hours; none are dropped.');
		['quiet_from', 'quiet_to'].forEach(function(k, i) {
			o = events.option(form.Value, k, i ? _('Until') : _('From'));
			o.placeholder = i ? '07:00' : '23:00';
			o.depends('quiet', '1');
			o.validate = function(s, v) { return !v || /^([01][0-9]|2[0-3]):[0-5][0-9]$/.test(v) ? true : _('Time in HH:MM format'); };
		});
		o = events.option(form.Flag, 'quiet_urgent', _('Play sound for "No internet" during quiet hours'));
		o.default = '1';
		o.depends('quiet', '1');
		o.description = _('Internet loss and data cap alerts still play a sound.');

		[['poll', _('Telegram connection: receiving commands'), _('Connection method for receiving commands and button clicks.')],
		 ['send', _('Telegram connection: sending notifications'), _('Connection method for sending notifications and replies.')]].forEach(function(t) {
			var sec = map.section(form.NamedSection, t[0], 'transport');
			sec.addremove = false;
			group(sec, t[1], t[2]);
			var mode = sec.option(form.ListValue, 'mode', _('Route'));
			mode.value('auto', _('Auto (proxy first, then direct)'));
			mode.value('proxy', _('Proxy only (no direct connection)'));
			mode.value('direct', _('Direct only'));
			mode.default = 'auto';
			var fb = sec.option(form.Flag, 'fallback_direct', _('Direct fallback if proxies fail'));
			fb.default = '1';
			fb.depends('mode', 'auto');
			fb.description = _('Disable if your carrier allows whitelisted sites only.');
			var mod = sec.option(form.Flag, 'modems', _('Try backup modems'));
			mod.default = '1';
			mod.depends('mode', 'auto');
			mod.depends('mode', 'direct');
			mod.description = _('If Telegram is unreachable via the primary uplink, try connected backup modems.');
			var bind = sec.option(form.Value, 'bind_interface', _('Interface for direct connection'));
			bind.placeholder = 'wwan0';
			bind.optional = true;
			bind.validate = function(s, v) { return bc.validInterface(v); };
			bind.description = _('Optional; applies to direct connections only, not proxies.');
		});

		var proxy = map.section(form.TypedSection, 'proxy', _('Telegram proxies'),
			_('Custom bot proxies. Local proxies on the router (Podkop, Forkop, NetShift, SSClash, nikki) are detected automatically. Shared credentials below apply to all entries here; proxies added via Telegram keep their own.'));
		proxy.anonymous = true;
		proxy.addremove = true;
		proxy.sortable = false;
		o = proxy.option(form.Flag, 'enabled', _('Use'));
		o.default = '0';
		o = proxy.option(form.Value, 'title', _('Name'));
		o.optional = true;
		o.placeholder = _('same as section name');
		o.validate = function(s, v) { return !v || /^[^'"\\<>\x00-\x1f]{1,32}$/.test(v) ? true : _('Up to 32 characters, without quotes'); };
		o = proxy.option(form.Value, 'endpoint', _('Address'));
		o.placeholder = 'socks5h://127.0.0.1:2080';
		o.validate = function(s, v) { return bc.validEndpoint(v); };
		o.description = _('Use socks5h:// (remote DNS) or HTTP(S) CONNECT. Do not include credentials in the URL; VLESS links are not supported.');
		o = proxy.option(form.ListValue, 'role', _('Order'));
		o.value('primary', _('Before router local proxies'));
		o.value('fallback', _('After local proxies'));
		o = proxy.option(form.Value, 'priority', _('Priority (lower first)'));
		o.default = '50';
		o.validate = function(s, v) { return bc.validPriority(v); };
		o = proxy.option(form.Button, '_check', _('Check'));
		o.inputtitle = _('Check');
		o.inputstyle = 'neutral';
		o.onclick = function(ev, sid) {
			var button = ev.target, report = button.parentNode.querySelector('.mm-bot-check');
			if (!report) { report = sayLine(); report.className = 'mm-bot-check'; button.parentNode.appendChild(report); }
			report.set('', _('checking saved address…'));
			button.disabled = true;
			return calls.proxyCheck(sid).then(function(r) {
				report.set(r && r.ok ? 'ok' : 'bad', r && r.ok ? _('Telegram reachable, %d ms').format(r.ms) : bc.CHECK_WORDS[r && r.code] || _('check failed'));
			}).catch(function() { report.set('bad', _('no response from router')); }).finally(function() { button.disabled = false; });
		};
		return map;
	},

	/* the proxies' login and password together (the owner, 2026-11-03: a password asked for without its login made no
	 * sense): one pair for every proxy set up here; a proxy added from Telegram (tg<N>) keeps its own. The password is
	 * kept in a closed file and never sent back; the login is plain UCI on each proxy section */
	passwordPanel: function(st) {
		var said = sayLine(), set = st.secret.proxy_password_configured === true;
		var mine = function() { return bc.proxies().filter(function(x) { return !/^tg\d+$/.test(x.name); }); };
		var logins = mine().map(function(x) { return x.user; }).filter(function(v, i, a) { return a.indexOf(v) === i; });
		said.set('', set ? _('The password is saved.') : _('No password set (not needed if the proxy requires no authentication).'));
		var login = el('input', { type: 'text', 'class': 'cbi-input-text', autocomplete: 'username', 'aria-label': _('Login'),
			placeholder: logins.length > 1 ? _('varies per proxy') : _('Login'), style: 'min-width:10rem;max-width:100%;flex:1 1 12rem' });
		login.value = logins.length === 1 ? logins[0] : '';
		var field = el('input', { type: 'password', 'class': 'cbi-input-text', autocomplete: 'new-password', 'aria-label': _('Password'),
			placeholder: set ? _('saved; enter a new value to replace') : _('Password'), style: 'min-width:10rem;max-width:100%;flex:1 1 12rem' });
		var save = el('button', { type: 'button', 'class': 'btn cbi-button-action' }, [_('Save')]);
		var clear = el('button', { type: 'button', 'class': 'btn cbi-button-negative' }, [_('Delete password')]);
		save.addEventListener('click', function() {
			var u = login.value.trim(), pw = field.value, jobs = [];
			field.value = '';
			if (u && !/^[A-Za-z0-9_.@-]{1,80}$/.test(u)) { said.set('bad', _('Username: Latin letters, digits, and _ . @ - only.')); return; }
			if (pw && (pw.length > 256 || /[\x00-\x1f]/.test(pw))) { said.set('bad', _('Invalid password length or control characters.')); return; }
			save.disabled = true;
			/* the login on every proxy set up here; nothing written where it is already so */
			mine().forEach(function(x) { if (x.user !== u) jobs.push(calls.uciSet('medvemodem-bot', x.name, { proxy_username: u })); });
			Promise.all(jobs).then(function() { return jobs.length ? calls.uciCommit('medvemodem-bot') : null; }).then(function() {
				return pw ? calls.proxyPasswordSet(pw).then(function(r) { if (!r || !r.ok) throw new Error('password'); }) : null;
			}).then(function() {
				said.set('ok', jobs.length || pw ? _('Saved. Restart the bot to apply.') : _('Nothing changed.'));
			}).catch(function(e) {
				said.set('bad', e && e.message === 'password' ? _('The password is not saved.') : _('Failed to save username.'));
			}).finally(function() { save.disabled = false; });
		});
		clear.addEventListener('click', function() {
			if (!window.confirm(_('Delete proxy password?'))) return;
			calls.proxyPasswordClear().then(function(r) {
				said.set(r && r.ok ? '' : 'bad', r && r.ok ? _('The password is deleted.') : _('The password is not deleted.'));
			});
		});
		return el('div', { 'class': 'cbi-section', id: 'mm-bot-proxy-password' }, [el('h3', {}, [_('Proxy login and password')]),
			el('div', { 'class': 'mmb-line' }, [login, field, save, clear]), said]);
	},

	/* the actions after the form; the irreversible ones folded in the danger zone, each asked first */
	actions: function() {
		var said = sayLine();
		var restart = el('button', { type: 'button', 'class': 'btn cbi-button-apply' }, [_('Restart bot')]);
		restart.addEventListener('click', function() {
			restart.disabled = true;
			calls.restart().then(function(r) {
				said.set(r && r.ok ? 'ok' : 'bad', r && r.ok ? _('Bot restarted.') : _('restart failed: %s').format(txt(r && r.code)));
			}).catch(function() { said.set('bad', _('no response from bot management service')); }).finally(function() { restart.disabled = false; });
		});
		var stop = el('button', { type: 'button', 'class': 'btn cbi-button-reset' }, [_('Disable bot')]);
		stop.addEventListener('click', function() {
			if (!window.confirm(_('Disable the bot? It will stop responding and sending alerts until re-enabled.'))) return;
			calls.uciSet('medvemodem-bot', 'main', { enabled: '0' }).then(function() { return calls.uciCommit('medvemodem-bot'); })
				.then(function() { return calls.restart(); })
				.then(function() { said.set('', _('Bot disabled.')); }).catch(function() { said.set('bad', _('failed to disable bot')); });
		});
		var forget = el('button', { type: 'button', 'class': 'btn cbi-button-negative' }, [_('Delete token')]);
		forget.addEventListener('click', function() {
			if (!window.confirm(_('Delete the Telegram token from secure storage? The bot will disconnect.'))) return;
			calls.tokenClear().then(function(r) {
				said.set(r && r.ok ? '' : 'bad', r && r.ok ? _('token deleted — bot will stop on next restart') : _('failed to delete token'));
			}).catch(function() { said.set('bad', _('error deleting token')); });
		});
		var danger = el('div', { 'class': 'mmb-danger', hidden: true }, [el('p', { style: 'color:#cc2b2b;margin-top:0' }, [_('Irreversible actions.')]),
			el('div', { 'class': 'mmb-line' }, [stop, forget])]);
		var toggle = el('button', { type: 'button', 'class': 'btn cbi-button-neutral', 'aria-expanded': 'false' }, [_('Danger zone')]);
		toggle.addEventListener('click', function() {
			danger.hidden = !danger.hidden;
			toggle.setAttribute('aria-expanded', String(!danger.hidden));
		});
		return el('div', { 'class': 'cbi-section', id: 'mm-bot-actions' }, [el('h3', {}, [_('Actions')]),
			el('div', { 'class': 'mmb-line' }, [restart, toggle]), danger, said]);
	},

	render: function(data) {
		bc.css();
		if (!data[0]) return el('div', { 'class': 'cbi-map' }, [el('h2', {}, [_('Bot settings')]),
			el('p', { 'class': 'alert-message warning' }, [_('Package medvemodem-bot is not installed or its config is unavailable.')])]);
		var self = this;
		return this.makeForm().render().then(function(formNode) {
			return el('div', { 'class': 'mm-bot-panel' }, [formNode, self.passwordPanel(data[1]), self.actions()]);
		});
	}
});
