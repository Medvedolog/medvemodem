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
			_('Everything here concerns the Telegram bot only. Applied by the “Save & Apply” button; the bot rereads them at a restart.'));

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
		var o = main.option(form.Flag, 'enabled', _('The bot is enabled'));
		o.default = '0';
		o.description = _('The first start is easier in the “Setup wizard”.');
		o = main.option(form.ListValue, 'lang', _('The bot\'s language'));
		o.value('auto', _('As the administrator\'s Telegram'));
		o.value('ru', _('Russian'));
		o.value('en', _('English'));
		o.default = 'auto';
		o.description = _('“As the administrator\'s Telegram”: the language of their Telegram app, from their first message or button press; Russian until then.');
		o = main.option(form.Flag, 'allow_control', _('Managing modems from Telegram'));
		o.default = '0';
		o.description = _('Buttons on a modem\'s card: make main, connect or disconnect, reconnect, pause the management. The bot asks again before actions that break the link. Off: the bot only shows the state.');
		o = main.option(form.Flag, 'allow_settings', _('Changing the settings from Telegram'));
		o.default = '1';
		o.description = _('The bot\'s “⚙️ Settings”: the notifications\' mode and kinds, quiet hours, the proxies to Telegram. Off: the pages only show them.');
		o = main.option(form.ListValue, 'snapshot_minutes', _('A state snapshot in flash memory'));
		o.value('0', _('Do not write'));
		o.value('15', _('At most once in 15 minutes'));
		o.description = _('Only anonymised fingerprints and counters; no SMS and no SIM identifiers.');

		group(main, _('SMS to Telegram'), _('Only the bot\'s administrator sees the sender\'s number and the text. On the router the messages stay unread; the bot keeps them nowhere.'));
		o = main.option(form.Flag, 'allow_sms_content', _('Show SMS'));
		o.default = '0';
		o.description = _('The “Incoming SMS” button in the bot: the last messages with the number, the modem, the SIM and the router\'s time.');
		o = main.option(form.Flag, 'sms_forward', _('Send new SMS'));
		o.default = '0';
		o.depends('allow_sms_content', '1');
		o.description = _('The bot sends each new incoming SMS by itself, as a message of its own; a code from an SMS comes with a “Copy” button.') + ' ' +
			_('When MedveModem\'s «SMS forwarding» is on and limited to some modems, the bot forwards from those modems only.');
		o = main.option(form.Flag, 'allow_send', _('Send SMS and USSD from Telegram'));
		o.default = '0';
		o.description = _('The /send and /ussd commands, «Write an SMS» under the incoming ones and «Reply» under a forwarded SMS. An SMS is sent only after a page that shows the modem, the number and the text; a USSD code goes at once. Both may cost money.');

		var admin = map.section(form.NamedSection, 'primary', 'administrator');
		admin.addremove = false;
		group(admin, _('Administrator'), _('The bot answers only them: their Telegram ID is enough.'));
		o = admin.option(form.Value, 'user_id', _('Telegram user ID'));
		o.placeholder = '123456789';
		o.description = _('The exact from.id, not @username.');
		o.validate = function(s, v) { return !v || bc.telegramId(v) ? true : _('A positive numeric user ID is needed'); };
		o = admin.option(form.Value, 'chat_id', _('Private chat ID'));
		o.placeholder = _('as the user ID');
		o.description = _('May be left empty: for a private dialogue it equals the user ID. Groups and channels do not fit.');
		o.validate = function(s, v) { return !v || bc.telegramId(v) ? true : _('A positive numeric chat ID is needed'); };

		var events = map.section(form.NamedSection, 'events', 'notifications');
		events.addremove = false;
		group(events, _('Notifications'));
		o = events.option(form.Flag, 'enabled', _('Send notifications about the modems'));
		o.default = '1';
		o = events.option(form.ListValue, 'mode', _('Which'));
		o.value('smart', _('Smart mode: brief failures are merged'));
		o.value('all', _('All confirmed events'));
		o.value('critical', _('Critical only'));
		o.depends('enabled', '1');
		o.description = _('Frequent switches of a USB modem and of the channel are merged so as not to flood you with messages.');
		[['backup', _('About the backup modems'), _('Off: nothing about a modem that does not carry the internet while it works through another channel.')],
		 ['services', _('ModemManager and QModem restarts'), null],
		 ['kernel', _('The router itself: a dead USB controller, voltage, hangs'), null],
		 ['quota', _('Traffic limits'), null],
		 ['restart', _('“MedveModem restarted”'), _('One quiet summary after the engine restarted or was updated.')]].forEach(function(t) {
			o = events.option(form.Flag, t[0], t[1]);
			o.default = '1';
			o.depends('enabled', '1');
			if (t[2]) o.description = t[2];
		});
		o = events.option(form.Flag, 'quiet', _('Quiet hours'));
		o.default = '0';
		o.depends('enabled', '1');
		o.description = _('In these hours notifications come without sound; none is dropped.');
		['quiet_from', 'quiet_to'].forEach(function(k, i) {
			o = events.option(form.Value, k, i ? _('Until') : _('From'));
			o.placeholder = i ? '07:00' : '23:00';
			o.depends('quiet', '1');
			o.validate = function(s, v) { return !v || /^([01][0-9]|2[0-3]):[0-5][0-9]$/.test(v) ? true : _('The time as HH:MM'); };
		});
		o = events.option(form.Flag, 'quiet_urgent', _('“No internet” with sound in quiet hours'));
		o.default = '1';
		o.depends('quiet', '1');
		o.description = _('A lost internet and a used-up traffic limit still ring.');

		[['poll', _('Reaching Telegram: getting commands'), _('How the bot picks up your commands and button presses.')],
		 ['send', _('Reaching Telegram: sending notifications'), _('How the bot sends messages and answers.')]].forEach(function(t) {
			var sec = map.section(form.NamedSection, t[0], 'transport');
			sec.addremove = false;
			group(sec, t[1], t[2]);
			var mode = sec.option(form.ListValue, 'mode', _('Route'));
			mode.value('auto', _('Auto: proxy first, then direct'));
			mode.value('proxy', _('Proxy only, no direct way'));
			mode.value('direct', _('Directly only'));
			mode.default = 'auto';
			var fb = sec.option(form.Flag, 'fallback_direct', _('The direct way if the proxies do not answer'));
			fb.default = '1';
			fb.depends('mode', 'auto');
			fb.description = _('Switch it off if the operator lets through by a whitelist only.');
			var mod = sec.option(form.Flag, 'modems', _('Try through other modems'));
			mod.default = '1';
			mod.depends('mode', 'auto');
			mod.depends('mode', 'direct');
			mod.description = _('If the main channel does not let through to Telegram, the bot tries each connected backup modem.');
			var bind = sec.option(form.Value, 'bind_interface', _('Interface for the direct way'));
			bind.placeholder = 'wwan0';
			bind.optional = true;
			bind.validate = function(s, v) { return bc.validInterface(v); };
			bind.description = _('Optional; only for the direct connection, not for a proxy.');
		});

		var proxy = map.section(form.TypedSection, 'proxy', _('Proxies to Telegram'),
			_('The bot\'s own routes; the Podkop, Forkop, NetShift, SSClash and nikki (mixed/SOCKS) already working on the router the bot finds by itself. The login and password are below, one for all proxies. A proxy added from Telegram keeps its own.'));
		proxy.anonymous = true;
		proxy.addremove = true;
		proxy.sortable = false;
		o = proxy.option(form.Flag, 'enabled', _('Use'));
		o.default = '0';
		o = proxy.option(form.Value, 'title', _('Name'));
		o.optional = true;
		o.placeholder = _('as the section');
		o.validate = function(s, v) { return !v || /^[^'"\\<>\x00-\x1f]{1,32}$/.test(v) ? true : _('Up to 32 characters, without quotes'); };
		o = proxy.option(form.Value, 'endpoint', _('Address'));
		o.placeholder = 'socks5h://127.0.0.1:2080';
		o.validate = function(s, v) { return bc.validEndpoint(v); };
		o.description = _('SOCKS only as socks5h:// (DNS through the proxy), or HTTP(S) CONNECT. No login or password in the address; VLESS links do not fit.');
		o = proxy.option(form.ListValue, 'role', _('Order'));
		o.value('primary', _('Before the router\'s local proxies'));
		o.value('fallback', _('After them'));
		o = proxy.option(form.Value, 'priority', _('Priority (lower goes first)'));
		o.default = '50';
		o.validate = function(s, v) { return bc.validPriority(v); };
		o = proxy.option(form.Button, '_check', _('Check'));
		o.inputtitle = _('Run the check');
		o.inputstyle = 'neutral';
		o.onclick = function(ev, sid) {
			var button = ev.target, report = button.parentNode.querySelector('.mm-bot-check');
			if (!report) { report = sayLine(); report.className = 'mm-bot-check'; button.parentNode.appendChild(report); }
			report.set('', _('checking the saved address…'));
			button.disabled = true;
			return calls.proxyCheck(sid).then(function(r) {
				report.set(r && r.ok ? 'ok' : 'bad', r && r.ok ? _('lets through to Telegram, %d ms').format(r.ms) : bc.CHECK_WORDS[r && r.code] || _('the check failed'));
			}).catch(function() { report.set('bad', _('no answer from the router')); }).finally(function() { button.disabled = false; });
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
		said.set('', set ? _('The password is saved.') : _('No password set: not needed if the proxy has no login.'));
		var login = el('input', { type: 'text', 'class': 'cbi-input-text', autocomplete: 'username', 'aria-label': _('Login'),
			placeholder: logins.length > 1 ? _('different on the proxies') : _('Login'), style: 'min-width:10rem;max-width:100%;flex:1 1 12rem' });
		login.value = logins.length === 1 ? logins[0] : '';
		var field = el('input', { type: 'password', 'class': 'cbi-input-text', autocomplete: 'new-password', 'aria-label': _('Password'),
			placeholder: set ? _('saved; type a new one to change it') : _('Password'), style: 'min-width:10rem;max-width:100%;flex:1 1 12rem' });
		var save = el('button', { type: 'button', 'class': 'btn cbi-button-action' }, [_('Save')]);
		var clear = el('button', { type: 'button', 'class': 'btn cbi-button-negative' }, [_('Delete the password')]);
		save.addEventListener('click', function() {
			var u = login.value.trim(), pw = field.value, jobs = [];
			field.value = '';
			if (u && !/^[A-Za-z0-9_.@-]{1,80}$/.test(u)) { said.set('bad', _('The login: Latin letters, digits and _ . @ - only.')); return; }
			if (pw && (pw.length > 256 || /[\x00-\x1f]/.test(pw))) { said.set('bad', _('The password: invalid length or control characters.')); return; }
			save.disabled = true;
			/* the login on every proxy set up here; nothing written where it is already so */
			mine().forEach(function(x) { if (x.user !== u) jobs.push(calls.uciSet('medvemodem-bot', x.name, { proxy_username: u })); });
			Promise.all(jobs).then(function() { return jobs.length ? calls.uciCommit('medvemodem-bot') : null; }).then(function() {
				return pw ? calls.proxyPasswordSet(pw).then(function(r) { if (!r || !r.ok) throw new Error('password'); }) : null;
			}).then(function() {
				said.set('ok', jobs.length || pw ? _('Saved: restart the bot.') : _('Nothing changed.'));
			}).catch(function(e) {
				said.set('bad', e && e.message === 'password' ? _('The password is not saved.') : _('The login is not saved.'));
			}).finally(function() { save.disabled = false; });
		});
		clear.addEventListener('click', function() {
			if (!window.confirm(_('Delete the proxy password?'))) return;
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
		var restart = el('button', { type: 'button', 'class': 'btn cbi-button-apply' }, [_('Restart the bot')]);
		restart.addEventListener('click', function() {
			restart.disabled = true;
			calls.restart().then(function(r) {
				said.set(r && r.ok ? 'ok' : 'bad', r && r.ok ? _('The bot is restarted.') : _('the restart failed: %s').format(txt(r && r.code)));
			}).catch(function() { said.set('bad', _('no answer from the bot\'s management service')); }).finally(function() { restart.disabled = false; });
		});
		var stop = el('button', { type: 'button', 'class': 'btn cbi-button-reset' }, [_('Switch the bot off')]);
		stop.addEventListener('click', function() {
			if (!window.confirm(_('Switch the bot off? It stops answering and sending notifications until switched on again.'))) return;
			calls.uciSet('medvemodem-bot', 'main', { enabled: '0' }).then(function() { return calls.uciCommit('medvemodem-bot'); })
				.then(function() { return calls.restart(); })
				.then(function() { said.set('', _('The bot is switched off.')); }).catch(function() { said.set('bad', _('the bot could not be switched off')); });
		});
		var forget = el('button', { type: 'button', 'class': 'btn cbi-button-negative' }, [_('Delete the token')]);
		forget.addEventListener('click', function() {
			if (!window.confirm(_('Delete the Telegram token from the protected storage? The bot stops connecting.'))) return;
			calls.tokenClear().then(function(r) {
				said.set(r && r.ok ? '' : 'bad', r && r.ok ? _('the token is deleted: the bot stops at a restart') : _('the token is not deleted'));
			}).catch(function() { said.set('bad', _('an error while deleting the token')); });
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
			el('p', { 'class': 'alert-message warning' }, [_('The medvemodem-bot package is not installed or its settings are unavailable.')])]);
		var self = this;
		return this.makeForm().render().then(function(formNode) {
			return el('div', { 'class': 'mm-bot-panel' }, [formNode, self.passwordPanel(data[1]), self.actions()]);
		});
	}
});
