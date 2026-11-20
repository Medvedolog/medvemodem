'use strict';
'require view';
'require uci';
'require medvemodem.botcommon as bc';

/*
 * «Telegram-бот» → «Обзор» (r85): what the bot does now, read only, and
 * the three actions that do not change its settings. The first setup is «Мастер настройки», the settings «Настройки».
 */
var el = bc.el, txt = bc.txt, row = bc.row, indicator = bc.indicator, uciGet = bc.uciGet, calls = bc.calls;

return view.extend({
	load: function() {
		return Promise.all([uci.load('medvemodem-bot').then(function() { return true; }, function() { return false; }), bc.readAll()]);
	},

	refresh: function() {
		var self = this;
		if (this.busy) return Promise.resolve();
		this.busy = true;
		this.refreshButton.disabled = true;
		return bc.readAll().then(function(st) { self.apply(st); return self.routesRefresh(); }).finally(function() {
			self.busy = false;
			self.refreshButton.disabled = false;
		});
	},

	apply: function(st) {
		var self = this, bot = st.bot, q = st.queue, t = st.transport, tls = st.tls;
		var enabled = bc.bool(uciGet('main', 'enabled')), admin = uciGet('primary', 'user_id'), done = bc.setupDone(st);
		this.running = !!bot.running;
		if (bot.running)
			this.botState.set('ok', _('online') + (bot.username ? ' · @' + bot.username : '') + ' · ' + _('modems: %s').format(txt(bot.modems, '0')) +
				' · ' + _('service: %s').format(txt(({ ok: _('online'), offline: _('not running'), starting: _('starting') })[bot.engine], _('unknown'))));
		else if (enabled && bot.unanswered === 'timeout') {
			/* busy on its own call to the engine: asked again, not called dead (HANDOFF 199 item 10) */
			this.botState.set('wait', _('running, but busy (timed out); retrying in 15 s'));
			window.clearTimeout(this.again);
			this.again = window.setTimeout(function() { if (!document.hidden) self.refresh(); }, 15000);
		} else if (enabled)
			this.botState.set('bad', _('enabled, not responding — click "Restart bot"'));
		else
			this.botState.set('off', _('disabled'));
		if (!bot.running && bot.unanswered !== 'timeout') this.explain();
		this.tokenState.set(done.token ? 'ok' : 'off', done.token ? _('saved') : _('not set'));
		this.adminState.set(admin ? 'ok' : 'off', admin ? _('assigned (ID %s)').format(admin) : _('not assigned'));
		[[this.pollState, t.poll, true], [this.sendState, t.send, false]].forEach(function(x) {
			var info = bc.safe(x[1]);
			x[0].set(info.state === 'ok' ? 'ok' : bot.running ? 'bad' : 'off', bot.running || info.state ? bc.routeInfo(info, x[2]) : _('bot not running'));
		});
		this.tlsState.set(tls.ok ? 'ok' : 'bad', tls.ok ? _('router certificates and system clock OK') : _('check failed: %s').format(txt(tls.code, _('no answer'))));
		this.conflictNote.hidden = !(Number(bc.safe(t.poll).conflicts) > 0);
		this.queueState.textContent = _('queued: %s · sending: %s · unconfirmed: %s · dropped: %s').format(txt(q.pending, '0'), txt(q.inflight, '0'),
			txt(q.uncertain, '0'), txt(q.dropped_info, '0')) + (q.persistence_error ? ' · ' + _('cannot write queue to /tmp!') : '');
		var show = bc.bool(uciGet('main', 'allow_sms_content')), fwd = show && bc.bool(uciGet('main', 'sms_forward'));
		this.smsState.set(show ? 'ok' : 'off', !show ? _('not shown') : fwd ? _('available on demand and auto-forwarded') : _('available via "Incoming SMS" button'));
		this.controlState.set(bc.bool(uciGet('main', 'allow_control')) ? 'ok' : 'off',
			bc.bool(uciGet('main', 'allow_control')) ? _('allowed') : _('view only'));
		/* the first setup, not finished: said on top, with the way to it */
		this.setupNote.hidden = !done.left;
		this.setupWords.textContent = _('Initial setup incomplete (%s of 4 steps remaining).').format(done.left);
	},

	/* the bot's routes with their checks (audit R02, 11.3): what it uses for commands and for sending, each checkable */
	routesRefresh: function() {
		var self = this;
		return calls.routes().then(function(r) {
			r = typeof r === 'object' && r ? r : {};
			var groups = Object.create(null), keys = [], checks = 0, ok = 0, bad = 0, current = 0;
			[['poll', _('Commands')], ['send', _('Sending')]].forEach(function(k) {
				(Array.isArray(r[k[0]]) ? r[k[0]] : []).forEach(function(x) {
					if (!x || !x.name) return;
					/* A route is the same only when its identity and transport match. Never merge check results. */
					var key = JSON.stringify([x.name, x.kind || '', !!x.auth]);
					if (!groups[key]) { groups[key] = { name: x.name, title: x.title, kind: x.kind, auth: x.auth, roles: {} }; keys.push(key); }
					groups[key].roles[k[0]] = x;
					var state = bc.checkWords(x)[1];
					checks++;
					if (state === 'ok') ok++;
					if (state === 'bad') bad++;
					if (x.current) current++;
				});
			});
			/* the table holds still while checks run (the owner, 2026-11-16: «дергается в процессе проверок таблица прокси и
			   модемов»): the rows in the order first seen, an opened check stays open across the redraws, fixed columns */
			self.routeOrder = (self.routeOrder || []).filter(function(k) { return groups[k]; });
			keys.forEach(function(k) { if (self.routeOrder.indexOf(k) < 0) self.routeOrder.push(k); });
			keys = self.routeOrder.slice();
			var opened = {};
			Array.prototype.forEach.call(self.routesBody.querySelectorAll('details[open][data-key]'), function(d) { opened[d.getAttribute('data-key')] = true; });
			var rows = keys.map(function(key) {
				var g = groups[key], active = !!((g.roles.poll && g.roles.poll.current) || (g.roles.send && g.roles.send.current));
				var details = ['poll', 'send'].map(function(role) {
					var x = g.roles[role];
					if (!x) return el('span', { 'class': 'mmb-role-empty' }, ['—']);
					var w = bc.checkWords(x), lamp = indicator(), c = bc.safe(x.checked);
					/* the answer's time beside OK, the reason beside an error (the owner 2026-11-03: «нет задержки после ОК») */
					lamp.set(w[1], w[1] === 'ok' ? _('OK · %d ms').format(c.latency_ms || 0) :
						w[1] === 'bad' ? _('Error: %s').format(c.at && c.transport_reached ? _('token not confirmed') :
							bc.routeState(c.at ? c.state : bc.safe(x.failed).state)) :
						w[1] === 'wait' ? (r.probing === x.name ? _('Checking…') : _('Queued…')) : _('Not checked'));
					var button = el('button', { type: 'button', 'class': 'btn cbi-button-action mmb-probe' }, [_('Check')]);
					button.disabled = !!x.queued;
					var detail = el('details', { 'class': 'mmb-check-detail', 'data-key': key + '|' + role }, [
						el('summary', { title: w[0] }, [lamp]),
						el('div', { 'class': 'mmb-check-full' }, [w[0]]),
						button
					]);
					if (opened[key + '|' + role]) detail.open = true;
					button.addEventListener('click', function() { button.disabled = true; self.probe(role, x.name); });
					return detail;
				});
				return el('tr', { 'class': active ? 'is-current' : '' }, [
					el('td', { 'data-title': _('Route') }, [bc.routeMessage(g.name, false, g.title) + (active ? ' · ' + _('in use') : '')]),
					el('td', { 'data-title': _('Kind') }, [(bc.KIND_WORDS[g.kind] || g.kind || '—') + (g.auth ? ' · ' + _('authenticated') : '')]),
					el('td', { 'data-title': _('Commands') }, [details[0]]),
					el('td', { 'data-title': _('Sending') }, [details[1]])
				]);
			});
			/* a check under way: which path and how many wait, the way a phone shows a test's progress */
			var busy = r.probing || Number(r.waiting) > 0;
			self.routesSummary.textContent = busy ? _('Testing: %s · queued: %d').format(r.probing ? bc.routeMessage(r.probing, false,
				(groups[keys.filter(function(k) { return groups[k].name === r.probing; })[0]] || {}).title) : '…', Number(r.waiting) || 0) :
				keys.length ? _('Routes: %d · active: %d · OK: %d · errors: %d').format(keys.length, current, ok, bad) :
				_('No routes available');
			if (self.probeAll) self.probeAll.disabled = !!(busy || self.probing);
			/* SSClash's or nikki's mixed port to switch on, above the routes */
			var advice = bc.clashAdvice(r.clash);
			self.clashNote.hidden = !advice.length;
			self.clashNote.replaceChildren.apply(self.clashNote, advice.map(function(a) { return el('div', {}, [a]); }));
			self.routesBody.replaceChildren.apply(self.routesBody, rows.length ? rows :
				[el('tr', {}, [el('td', { colspan: 4 }, [self.running ? _('No route to Telegram allowed by current settings.') : _('Bot is not running; routes are visible only while active.')])])]);
			return r;
		}).catch(function() {
			self.routesSummary.textContent = _('Routes unavailable');
			self.routesBody.replaceChildren(el('tr', {}, [el('td', { colspan: 4 }, [_('Route data unavailable.')])]));
			return {};
		});
	},

	/* a press queues the path in the bot (one check at a time there): another press while one runs joins the queue,
	   the table follows until the queue is empty (the owner 2026-11-03: other paths could not be checked meanwhile) */
	probe: function(kind, name) {
		var self = this;
		this.until = Date.now() + 90000;
		this.probeAll.disabled = true;
		this.routesDetails.open = true;
		if (!this.probing) this.routesSummary.textContent = _('Testing routes…');
		return calls.probe(kind || '', name || '').then(function(r) {
			if (!r || !r.ok) { bc.notify(_('Failed to start check: %s').format(txt(r && r.code, _('nothing to check'))), true); return self.routesRefresh(); }
			if (self.probing) return;
			self.probing = true;
			var poll = function() {
				return self.routesRefresh().then(function(st) {
					if ((st.probing || st.waiting) && Date.now() < self.until)
						return new Promise(function(resolve) { window.setTimeout(resolve, 1500); }).then(poll);
				});
			};
			return poll().finally(function() { self.probing = false; return self.routesRefresh(); });
		}).catch(function() { bc.notify(_('Bot is not running or lacks permissions.'), true); self.probeAll.disabled = false; });
	},

	/* the router says why the bot is not running: the init script's checks, else the bot's last log line */
	explain: function() {
		var self = this;
		return calls.why().then(function(r) {
			r = bc.safe(r);
			if (r.running || !r.code || self.running) return;
			self.botState.set(r.code === 'disabled' ? 'off' : 'bad', _('not running: %s').format(txt(bc.whyWords(r), _('unknown error'))) +
				(r.code === 'exits' && r.last_log ? ' · ' + _('log: %s').format(r.last_log) : ''));
		}).catch(function() {});
	},

	render: function(data) {
		bc.css();
		if (!data[0]) return el('div', { 'class': 'cbi-map' }, [el('h2', {}, [_('MedveModem Telegram bot')]),
			el('p', { 'class': 'alert-message warning' }, [_('Package medvemodem-bot is not installed or its config is unavailable.')])]);
		var self = this;
		['botState', 'tokenState', 'adminState', 'pollState', 'sendState', 'tlsState', 'smsState', 'controlState'].forEach(function(k) { self[k] = indicator(); });
		this.queueState = el('span');
		this.setupWords = el('span');
		this.setupNote = el('div', { 'class': 'alert-message notice mmb-setup-note', hidden: true }, [this.setupWords, ' ',
			el('a', { 'class': 'btn cbi-button-action', href: L.url('admin/modem/medvemodem/bot/setup') }, [_('Open setup wizard')])]);
		this.refreshButton = el('button', { type: 'button', 'class': 'btn cbi-button-neutral' }, [_('Refresh')]);
		this.refreshButton.addEventListener('click', function() { self.refresh(); });
		var restart = el('button', { type: 'button', 'class': 'btn cbi-button-apply' }, [_('Restart bot')]);
		restart.addEventListener('click', function() {
			if (!window.confirm(_('Restart only the bot? Telegram connectivity will drop for a few seconds; modems are unaffected.'))) return;
			restart.disabled = true;
			calls.restart().then(function(r) {
				if (!r || !r.ok) { bc.notify(_('Failed to restart bot: %s').format(txt(r && r.code)), true); return; }
				bc.notify(_('Bot restarted.'));
				window.setTimeout(function() { self.refresh(); }, 4000);
			}).catch(function() { bc.notify(_('No response from bot management service.'), true); })
				.finally(function() { restart.disabled = false; });
		});
		var test = el('button', { type: 'button', 'class': 'btn cbi-button-action' }, [_('Send test message')]);
		test.addEventListener('click', function() {
			test.disabled = true;
			calls.chatTest().then(function(r) {
				bc.notify(r && r.ok ? _('Test message queued.') : _('Cannot send — check settings.'), !(r && r.ok));
			}).catch(function() { bc.notify(_('Bot is not running or lacks permissions.'), true); }).finally(function() { test.disabled = false; });
		});
		var root = el('div', { 'class': 'mm-bot-panel' }, [
			el('h2', {}, [_('MedveModem Telegram bot')]),
			el('p', { 'class': 'description' }, [_('Monitors modems and replies to status commands. Current state is shown below; use "Setup wizard" for initial setup or "Settings" for advanced options.')]),
			this.setupNote,
			this.clashNote = el('div', { 'class': 'alert-message notice mmb-clash-note', hidden: true }),
			el('div', { 'class': 'cbi-section', id: 'mm-bot-overview' }, [
				el('h3', {}, [_('State')]),
				row(_('Bot'), this.botState), row(_('Token'), this.tokenState), row(_('Administrator'), this.adminState),
				row(_('Getting commands'), this.pollState), row(_('Sending notifications'), this.sendState),
				row(_('Certificates and clock'), this.tlsState), row(_('Queue'), this.queueState),
				row(_('SMS to Telegram'), this.smsState), row(_('Modem management'), this.controlState),
				el('div', { 'class': 'mmb-line' }, [this.refreshButton, restart, test]),
				this.conflictNote = el('p', { 'class': 'alert-message warning mm-bot-conflict', hidden: true }, [_('Telegram error 409: another client is polling updates with this token.')])
			]),
			el('div', { 'class': 'cbi-section', id: 'mm-bot-routes' }, [
				el('h3', {}, [_('Telegram connectivity checks')]),
				el('p', { 'class': 'description' }, [_('Active routes are shown above; expand detailed route checks below if needed.')]),
				this.routesDetails = el('details', { 'class': 'mmb-route-details' }, [
					el('summary', { 'class': 'mmb-route-summary' }, [this.routesSummary = el('span', {}, [_('Loading routes…')]), ' · ', _('Show details')]),
					el('p', { 'class': 'description' }, [_('A passing check confirms Telegram Bot API accepted the token via this route.')]),
					el('div', { 'class': 'mmb-route-scroll' }, [
						el('table', { 'class': 'table mmb-routes' }, [
							el('thead', {}, [el('tr', {}, [el('th', {}, [_('Route')]), el('th', {}, [_('Kind')]),
								el('th', {}, [_('Commands')]), el('th', {}, [_('Sending')])])]),
							this.routesBody = el('tbody')])
					]),
					el('div', { 'class': 'mmb-line' }, [this.probeAll = el('button', { type: 'button', 'class': 'btn cbi-button-action' }, [_('Test all routes')])])
				])
			])
		]);
		this.probeAll.addEventListener('click', function() { self.probe('', ''); });
		this.apply(data[1]);
		this.routesRefresh();
		return root;
	},

	handleSave: null,
	handleSaveApply: null,
	handleReset: null
});
