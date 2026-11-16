'use strict';
'require view';
'require uci';
'require medvemodem.botcommon as bc';

/*
 * «Telegram-бот» → «Мастер настройки» (r85): one step at a time, «Шаг N из
 * 4», back and next, opened on the first step not done; each step's state as the router sees it. The token and the
 * proxy password go to the router's closed store only, never back to the browser.
 */
var el = bc.el, txt = bc.txt, indicator = bc.indicator, calls = bc.calls;
/* after a write over RPC the page's copy of the config is read again: uci.set here would show «unsaved changes» */
function reloadUci() {
	try { uci.unload('medvemodem-bot'); } catch (e) {}
	return uci.load('medvemodem-bot').catch(function() {});
}
/* the administrator before the start: the bot does not start without one (the owner, 2026-10-26: the ID in the list is
 * enough, no confirmation by a message) */
var STEPS = [['token', _('Bot token')], ['route', _('Reaching Telegram')], ['admin', _('Administrator')], ['run', _('Start-up')]];

return view.extend({
	load: function() {
		return Promise.all([uci.load('medvemodem-bot').then(function() { return true; }, function() { return false; }), bc.readAll()]);
	},

	reread: function() {
		var self = this;
		return bc.readAll().then(function(st) { self.st = st; self.drawSteps(); return st; });
	},

	/* the chips on top: done ones ticked, the current one marked, any one opened by a click */
	drawSteps: function() {
		var self = this, done = bc.setupDone(this.st, this.routeOk);
		this.done = done;
		this.chips.replaceChildren.apply(this.chips, STEPS.map(function(s, i) {
			var b = el('button', { type: 'button', 'class': done[s[0]] ? 'is-done' : '' }, [(done[s[0]] ? '✓ ' : (i + 1) + '. ') + s[1]]);
			if (i === self.step) b.setAttribute('aria-current', 'step');
			b.addEventListener('click', function() { self.go(i); });
			return el('li', {}, [b]);
		}));
	},

	go: function(i) {
		this.step = Math.max(0, Math.min(STEPS.length - 1, i));
		this.drawSteps();
		var body = this['step' + (this.step + 1)]();
		var self = this, nav = [];
		if (this.step > 0) nav.push(this.button(_('Back'), 'cbi-button-neutral', function() { self.go(self.step - 1); }));
		if (this.step < STEPS.length - 1) nav.push(this.button(_('Next'), 'cbi-button-action', function() { self.go(self.step + 1); }));
		else nav.push(el('a', { 'class': 'btn cbi-button-action', href: L.url('admin/modem/medvemodem/bot/overview') }, [_('Done: to the overview')]));
		this.card.replaceChildren(el('div', { 'class': 'description' }, [_('Step %d of %d').format(this.step + 1, STEPS.length)]),
			el('h3', { style: 'margin-top:0' }, [_('Step %d. %s').format(this.step + 1, STEPS[this.step][1])]),
			el('div', { id: 'mm-bot-step-' + STEPS[this.step][0] }, body), el('div', { 'class': 'mmb-nav' }, nav));
	},

	button: function(label, cls, fn) {
		var b = el('button', { type: 'button', 'class': 'btn ' + cls }, [label]);
		b.addEventListener('click', fn);
		return b;
	},

	/* step 1: the token from @BotFather, kept in a closed file, checked against Telegram */
	step1: function() {
		var self = this, saved = bc.bool(this.st.secret.token_configured), said = indicator();
		var field = el('input', { type: 'password', 'class': 'cbi-input-text', autocomplete: 'new-password', spellcheck: 'false',
			autocapitalize: 'none', 'aria-label': _('Telegram token'), maxlength: '128', placeholder: saved ? _('Enter a new one to replace it') : '123456789:ABC…',
			style: 'min-width:12rem;max-width:100%;flex:1 1 16rem' });
		said.set(saved ? 'ok' : 'off', saved ? _('the token is saved') : _('no token set'));
		var check = function() {
			said.set('wait', _('checking with Telegram…'));
			return calls.tokenCheck('auto').then(function(r) {
				if (r && r.ok) {
					self.routeOk = true;
					said.set('ok', _('Telegram took the token') + (r.username ? ': ' + _('bot @%s').format(r.username) : '') + (r.via === 'proxy' ? ' (' + _('through a proxy') + ')' : ''));
				} else said.set('bad', bc.CHECK_WORDS[r && r.code] || _('the check failed'));
				self.drawSteps();
			}).catch(function() { said.set('bad', _('no answer from the router')); });
		};
		var save = this.button(_('Save the token'), 'cbi-button-action', function() {
			var token = field.value.trim();
			field.value = '';
			if (!bc.tokenFormat(token)) { said.set('bad', _('this is not a token: the form 123456789:ABC… from @BotFather is needed; nothing is saved')); return; }
			save.disabled = true;
			calls.tokenSet(token).then(function(r) {
				if (!r || !r.ok) { said.set('bad', _('the router did not save the token (%s)').format(txt(r && r.code, _('no answer')))); return; }
				field.placeholder = _('Enter a new one to replace it');
				return self.reread().then(check);
			}).catch(function() { said.set('bad', _('an error while saving the token')); }).finally(function() { save.disabled = false; });
		});
		var checkButton = this.button(_('Check the token'), 'cbi-button-neutral', function() { check(); });
		return [el('p', { 'class': 'description' }, [_('Create a bot with @BotFather in Telegram (the /newbot command) and paste its token. The token is kept in a closed file on the router and not returned to the browser: so the field is empty after saving.')]),
			el('div', { 'class': 'mmb-line' }, [field, save, checkButton]), el('p', {}, [said])];
	},

	/* step 2: the way to Telegram; in Russia's mobile networks it is blocked (the owner, 2026-10-22), so a proxy: a saved
	 * one with the same address is reused (a second section for one address was the bench's backup1 and wizard), saved off
	 * and turned on only after its check (HANDOFF 200 item 14) */
	step2: function() {
		var self = this, bot = this.st.bot, poll = bc.safe(this.st.transport.poll), list = bc.proxies(), said = indicator();
		var on = list.filter(function(p) { return p.enabled; })[0], shown = on || list.filter(function(p) { return p.name === 'wizard'; })[0];
		if (bot.running && poll.state === 'ok') said.set('ok', _('the bot already reaches Telegram: %s').format(bc.routeMessage(poll.tier)));
		else if (on) said.set('wait', _('the proxy “%s” (%s) is saved: press “Check the proxy”').format(on.name, on.endpoint));
		else said.set('off', _('no proxy set; without it the bot goes directly'));
		var addr = el('input', { type: 'text', 'class': 'cbi-input-text', 'aria-label': _('Proxy address'), placeholder: 'socks5h://192.168.1.2:1080',
			spellcheck: 'false', autocapitalize: 'none', style: 'min-width:12rem;max-width:100%;flex:1 1 16rem' });
		var user = el('input', { type: 'text', 'class': 'cbi-input-text', 'aria-label': _('Proxy login'), placeholder: _('login (if needed)'),
			autocomplete: 'off', style: 'min-width:8rem;flex:0 1 10rem' });
		var pass = el('input', { type: 'password', 'class': 'cbi-input-text', 'aria-label': _('Proxy password'), placeholder: _('password (if needed)'),
			autocomplete: 'new-password', style: 'min-width:8rem;flex:0 1 10rem' });
		addr.value = shown ? shown.endpoint : '';
		user.value = shown ? shown.user : '';
		/* how: direct (no proxy at all) or proxy (the proxies only): the check takes that way only (audit R02) */
		var through = function(how) {
			said.set('wait', _('checking the way to Telegram…'));
			return calls.tokenCheck(how || 'auto').then(function(r) {
				if (r && r.ok) {
					self.routeOk = true;
					said.set('ok', (r.via === 'proxy' ? _('Telegram is reachable through a proxy') : _('Telegram is reachable directly')) + (r.username ? ': ' + _('bot @%s').format(r.username) : ''));
				} else said.set('bad', (bc.CHECK_WORDS[r && r.code] || _('the check failed')) +
					(how === 'direct' && r && r.code === 'telegram_unreachable' ? ': ' + _('set a proxy') : '') +
					(r && r.unchecked > 0 ? ' · ' + _('not checked here: %d more proxies (the running bot checks them on its “Overview”)').format(r.unchecked) : ''));
				self.drawSteps();
			}).catch(function() { said.set('bad', _('no answer from the router')); });
		};
		var token = function() {
			if (bc.bool(self.st.secret.token_configured)) return true;
			said.set('bad', _('save the token first (step 1)'));
			return false;
		};
		/* the proxy checked by itself, then turned on and Telegram asked through it */
		var checkAndEnable = function(sec) {
			return calls.proxyCheck(sec).then(function(r) {
				if (!r || !r.ok) { said.set('bad', (bc.CHECK_WORDS[r && r.code] || _('the proxy failed the check')) + '; ' + _('the proxy is switched off')); return; }
				return calls.uciSet('medvemodem-bot', sec, { enabled: '1' }).then(function() { return calls.uciCommit('medvemodem-bot'); })
					.then(reloadUci).then(function() { return through('proxy'); });
			});
		};
		var save = this.button(_('Save the proxy and check'), 'cbi-button-action', function() {
			var v = addr.value.trim(), ok = bc.validEndpoint(v), name = user.value.trim();
			if (ok !== true) { said.set('bad', ok); return; }
			if (name && !/^[A-Za-z0-9_.@-]{1,80}$/.test(name)) { said.set('bad', _('login: Latin letters, digits and _ . @ -')); return; }
			if (pass.value && (pass.value.length > 256 || /[\x00-\x1f]/.test(pass.value))) { said.set('bad', _('invalid password')); return; }
			if (!token()) return;
			save.disabled = true;
			var same = bc.proxies().filter(function(p) { return p.endpoint === v; })[0];
			var wizard = bc.proxies().filter(function(p) { return p.name === 'wizard'; })[0];
			var sec = same ? same.name : 'wizard';
			var values = { enabled: '0', role: 'primary', priority: '10', endpoint: v, proxy_username: name };
			var write = same || wizard ? calls.uciSet('medvemodem-bot', sec, values) : calls.uciAdd('medvemodem-bot', 'proxy', 'wizard', values);
			write.then(function() { return calls.uciCommit('medvemodem-bot'); })
				.then(function() { return pass.value ? calls.proxyPasswordSet(pass.value) : null; })
				.then(function() {
					pass.value = '';
					return reloadUci().then(function() { return checkAndEnable(sec); });
				}).catch(function() { said.set('bad', _('the router did not save the proxy')); })
				.finally(function() { save.disabled = false; });
		});
		var check = this.button(_('Check the proxy'), 'cbi-button-neutral', function() {
			var cur = bc.proxies().filter(function(p) { return p.endpoint === addr.value.trim(); })[0];
			if (!cur) { said.set('bad', _('this address is not saved yet: press “Save the proxy and check”')); return; }
			if (token()) checkAndEnable(cur.name);
		});
		var direct = this.button(_('Check without a proxy'), 'cbi-button-neutral', function() { if (token()) through('direct'); });
		return [el('p', { 'class': 'description' }, [_('If Telegram is not reachable directly, set a proxy: SOCKS5 (socks5h://address:port) or HTTPS. A proxy on this router (Podkop, mihomo and the like) works too. The password is kept apart and not shown. If Telegram opens anyway, press “Check without a proxy”.')]),
			el('div', { 'class': 'mmb-line' }, [addr, user, pass]), el('div', { 'class': 'mmb-line' }, [save, check, direct]),
			el('p', { id: 'mm-bot-route-state' }, [said])];
	},

	/* step 3: the administrator by their Telegram ID; the bot answers only them */
	step3: function() {
		var self = this, said = indicator(), admin = bc.uciGet('primary', 'user_id');
		said.set(admin ? 'ok' : 'off', admin ? _('the administrator is assigned (ID %s)').format(admin) : _('not assigned'));
		var id = el('input', { type: 'text', inputmode: 'numeric', 'class': 'cbi-input-text', 'aria-label': _('Telegram user ID'), placeholder: '123456789', value: admin || '' });
		var save = this.button(_('Save'), 'cbi-button-positive', function() {
			var v = String(id.value || '').trim();
			if (!bc.telegramId(v)) { said.set('bad', _('A positive numeric user ID is needed')); return; }
			save.disabled = true;
			calls.uciSet('medvemodem-bot', 'primary', { user_id: v, chat_id: '' }).then(function() { return calls.uciCommit('medvemodem-bot'); })
				/* a running bot reads the administrator at its start */
				.then(function() { return self.st.bot.running ? calls.restart() : null; })
				.then(function() { said.set('ok', _('the administrator is assigned (ID %s)').format(v)); return reloadUci(); })
				.then(function() { self.reread(); })
				.catch(function() { said.set('bad', _('the ID could not be saved')); }).finally(function() { save.disabled = false; });
		});
		return [el('p', { 'class': 'description' }, [_('The bot answers only this Telegram user. Your ID: write to @userinfobot in Telegram, it answers with “Id”. A number, not @username.')]),
			el('div', { 'class': 'mmb-line' }, [id, save]), el('p', {}, [said])];
	},

	/* step 4: the bot switched on and started; only its own service, the modems untouched */
	step4: function() {
		var self = this, bot = this.st.bot, said = indicator(), enabled = bc.bool(bc.uciGet('main', 'enabled'));
		said.set(bot.running ? 'ok' : 'off', bot.running ? _('the bot works') + (bot.username ? ' · @' + bot.username : '') : enabled ? _('enabled, but not running') : _('disabled'));
		var start = this.button(enabled ? _('Restart the bot') : _('Enable and start the bot'), 'cbi-button-positive', function() {
			start.disabled = true;
			said.set('wait', _('starting…'));
			var on = enabled ? Promise.resolve() :
				calls.uciSet('medvemodem-bot', 'main', { enabled: '1' }).then(function() { return calls.uciCommit('medvemodem-bot'); });
			on.then(function() { return calls.restart(); }).then(function(r) {
				if (!r || !r.ok) { said.set('bad', _('the bot is not started: %s').format(txt(r && r.code))); return; }
				if (!enabled) { enabled = true; reloadUci(); }
				/* the init script returns 0 when it declines to start (no token, no administrator): ask the router */
				window.setTimeout(function() {
					calls.why().then(function(w) {
						w = bc.safe(w);
						said.set(w.running ? 'ok' : 'bad', w.running ? _('the bot is started') : _('the bot is not started: %s').format(txt(bc.whyWords(w), _('the reason is unknown'))) +
							(w.code === 'exits' && w.last_log ? ' · ' + _('log: %s').format(w.last_log) : ''));
					}).finally(function() { self.reread(); });
				}, 3000);
			}).catch(function() { said.set('bad', _('the bot could not be enabled')); }).finally(function() { start.disabled = false; });
		});
		var test = this.button(_('Send a test message'), 'cbi-button-neutral', function() {
			calls.chatTest().then(function(r) {
				said.set(r && r.ok ? 'ok' : 'bad', r && r.ok ? _('the test message is queued: check Telegram') : _('sending is unavailable: check the steps above'));
			}).catch(function() { said.set('bad', _('the bot is not started or has no rights')); });
		});
		return [el('p', { 'class': 'description' }, [_('Enables the bot and starts only its service; the modems are not touched.')]), el('div', { 'class': 'mmb-line' }, [start, test]), el('p', {}, [said])];
	},

	render: function(data) {
		bc.css();
		if (!data[0]) return el('div', { 'class': 'cbi-map' }, [el('h2', {}, [_('Bot setup wizard')]),
			el('p', { 'class': 'alert-message warning' }, [_('The medvemodem-bot package is not installed or its settings are unavailable.')])]);
		this.st = data[1];
		this.chips = el('ol', { 'class': 'mmb-steps', 'aria-label': _('Setup steps') });
		this.card = el('div', { 'class': 'mmb-card mm-bot-wizard' });
		var done = bc.setupDone(this.st), first = STEPS.findIndex(function(s) { return !done[s[0]]; });
		this.go(first < 0 ? STEPS.length - 1 : first);
		return el('div', { 'class': 'mm-bot-panel' }, [el('h2', {}, [_('Bot setup wizard')]),
			el('p', { 'class': 'description' }, [_('Four steps; the done ones are marked ✓. Everything set here is also in “Settings”.')]),
			this.chips, this.card]);
	},

	handleSave: null,
	handleSaveApply: null,
	handleReset: null
});
