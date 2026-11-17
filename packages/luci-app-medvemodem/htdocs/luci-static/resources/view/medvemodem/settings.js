'use strict';
'require view';
'require rpc';
'require fs';
'require medvemodem.common as mm';

var CSS_VERSION = mm.VERSION, text = mm.text, failText = mm.failText, LEVELS = mm.LEVELS, LEVEL_HINT = mm.LEVEL_HINT, level = mm.level, setLevel = mm.setLevel, CHEV = mm.CHEV, chev = mm.chev, pageHead = mm.pageHead, secOpen = mm.secOpen, secRemember = mm.secRemember, ncRows = mm.ncRows, ncSummary = mm.ncSummary, ncTable = mm.ncTable, NC_ERROR = mm.NC_ERROR, dnsRows = mm.dnsRows, dnsSummary = mm.dnsSummary, dnsTable = mm.dnsTable, list = mm.list, num = mm.num;

/* how a modem is told apart in another port, beside its id (name_follows; its own serial needs no word) */
var FOLLOWS = { imei: _('by IMEI'), port: _('by USB port') };
var UPDATE = '/usr/libexec/medvemodem/apn-update';
var QMODEM = '/usr/libexec/medvemodem/qmodem-install';
var PKG = '/usr/libexec/medvemodem/pkg-install';
/* MedveModem's own update from its GitHub releases (the owner, 2026-11-14) */
var SELFUP = '/usr/libexec/medvemodem/self-update';
var SERVICE = '/usr/libexec/medvemodem/service';
var FORWARD = '/usr/libexec/medvemodem/sms-forward';
/* the other modem services the switch may stop too */
/* qmodem_network dials QModem's modems; qmodem_init is only its boot scan, which MedveModem keeps off (block_auto_probe) */
var SVC_NAMES = { modemmanager: 'ModemManager', qmodem_network: _('QModem: modem connection'), qmodem_init: _('QModem: modem search at boot'),
	qmodem_monitor: _('QModem monitoring'), qmodem_hc_sim: _('QModem SIM check') };
/* an install's log is followed every 2 s while the tab is shown; a hidden tab waits until it is shown again (HANDOFF 200 item 17) */
function soonShown(fn, ms) {
	return setTimeout(function() {
		if (!document.hidden) return fn();
		var back = function() {
			if (document.hidden) return;
			document.removeEventListener('visibilitychange', back);
			fn();
		};
		document.addEventListener('visibilitychange', back);
	}, ms);
}
/* pkg-install's components: what each is for */
var PKG_NAMES = { modemmanager: ['ModemManager', _('manages most modems; without it MedveModem drives only modems with their own connection')],
	'usb-modeswitch': ['usb-modeswitch', _('switches flash-drive modems (a CD-ROM with drivers) into modem mode')],
	'qmi-utils': ['qmi-utils (qmicli)', _('antennas, carrier aggregation and FCC unlock through QMI')],
	usbutils: ['usbutils (lsusb)', _('optional: the USB tree as lsusb sees it, to compare with “Ports and devices”')],
	ethtool: ['ethtool', _('optional: the driver and the own counters of the modem\'s network interface')],
	umbim: ['umbim', _('a direct MBIM connection without ModemManager (the mbim protocol of netifd)')],
	uqmi: ['uqmi', _('a direct QMI connection without ModemManager (the qmi protocol of netifd)')],
	'comgt-ncm': ['comgt-ncm', _('a direct NCM connection (Huawei and others) without ModemManager')],
	option: [_('Driver %s').format('option'), _('the modems\' serial ports: AT commands, SMS, USSD, PPP')],
	qmi_wwan: [_('Driver %s').format('qmi_wwan'), _('the QMI data channel (Quectel, Sierra, Foxconn and others)')],
	cdc_mbim: [_('Driver %s').format('cdc_mbim'), _('the MBIM data channel')],
	cdc_ncm: [_('Driver %s').format('cdc_ncm'), _('the NCM data channel')],
	huawei_cdc_ncm: [_('Driver %s').format('huawei_cdc_ncm'), _('the NCM data channel of Huawei modems')],
	cdc_ether: [_('Driver %s').format('cdc_ether'), _('modems with their own router (ECM mode, HiLink)')],
	rndis_host: [_('Driver %s').format('rndis_host'), _('modems in RNDIS mode and Android phones as a modem')],
	ipheth: [_('Driver %s and %s').format('ipheth', 'usbmuxd'), _('an iPhone as a modem over USB (usbmuxd: for “Trust this computer”)')],
	/* MedveModem's own pinned lpac (the owner, 2026-11-15), from its release, not from the router's feeds */
	lpac: ['lpac (eSIM)', _('reads the eSIM chip and switches its profiles; MedveModem\'s own build, from its release on GitHub')] };
var PKG_WHY = { running: _('an installation is already running'), 'no package manager': _('the router has no package manager'), 'install failed': _('the installation failed'),
	'install failed: lpac': _('lpac did not install: the release was not reached or has no build for this router') };
function pkgState(c) {
	/* installed first, then whether the kernel has it loaded (bench 2026-10-19), as «установлен, работает» for ModemManager */
	return c.state === 'loaded' ? _('installed, loaded') : c.state === 'missing' ? _('not installed') :
		c.state === 'other' ? _('another build is installed') :
		c.kind === 'service' ? (c.running ? _('installed, works') : _('installed, not running')) :
		c.kind === 'driver' ? _('installed, not loaded') : _('installed');
}
var CLI = '/usr/sbin/medvemodem', NETCHECK = '/usr/libexec/medvemodem/netcheck';
var getDevices = rpc.declare({ object: 'medvemodem', method: 'devices', params: ['absent'], reject: true });
var setPriority = rpc.declare({ object: 'medvemodem', method: 'priority', params: ['order'], reject: true });
var getUplink = rpc.declare({ object: 'medvemodem', method: 'uplink', reject: true });
var uciGet = rpc.declare({ object: 'uci', method: 'get', params: ['config', 'type'], reject: true });
var uciAdd = rpc.declare({ object: 'uci', method: 'add', params: ['config', 'type', 'name', 'values'], reject: true });
var uciSet = rpc.declare({ object: 'uci', method: 'set', params: ['config', 'section', 'values'], reject: true });
var uciDelete = rpc.declare({ object: 'uci', method: 'delete', params: ['config', 'section', 'options'], reject: true });
var uciDrop = rpc.declare({ object: 'uci', method: 'delete', params: ['config', 'section'], reject: true });
var uciRevert = rpc.declare({ object: 'uci', method: 'revert', params: ['config'], reject: true });
var uciCommit = rpc.declare({ object: 'uci', method: 'commit', params: ['config'], reject: true });
var getStatus = rpc.declare({ object: 'medvemodem', method: 'status', reject: true });
/* the SMS forward's token in its own 0600 file through rpcd (audit 2026-10-21): uci read on medvemodem gave it back */
var getAlerts = rpc.declare({ object: 'medvemodem', method: 'alerts', params: ['test'], reject: true });
var forwardSmsTest = rpc.declare({ object: 'medvemodem', method: 'alerts', params: ['test', 'forward'], reject: true });
var wlmodeCall = rpc.declare({ object: 'medvemodem', method: 'wlmode', params: ['action'], reject: true });
var WLMODE = '/usr/libexec/medvemodem/wlmode';
/* the router's local tunnel proxies: found read-only in their programs, corrected or added in ours (local-proxy) */
var PROXY = '/usr/libexec/medvemodem/local-proxy';
var fwStatus = rpc.declare({ object: 'medvemodem.forward', method: 'status', reject: true });
var fwTokenSet = rpc.declare({ object: 'medvemodem.forward', method: 'token_set', params: ['token'], reject: true });
var fwTokenClear = rpc.declare({ object: 'medvemodem.forward', method: 'token_clear', reject: true });
var mmRestart = rpc.declare({ object: 'medvemodem', method: 'mm_restart', params: ['confirm', 'reason'], reject: true });

function words(value) { return typeof value === 'string' && value !== '--' ? value : ''; }
function list(value) { return Array.isArray(value) ? value : value == null || value === '' ? [] : [value]; }
/* an address and port: 1.1.1.1:443 or [2606:4700:4700::1111]:443 */
function target(value) {
	if (value == null || value === '') return true;
	var m = /^(?:\[([0-9a-fA-F:.]+)\]|(\d{1,3}(?:\.\d{1,3}){3})):(\d{1,5})$/.exec(String(value));
	if (!m || +m[3] < 1 || +m[3] > 65535 || (m[2] && m[2].split('.').some(function(o) { return +o > 255; })))
		return _('An address and a port, for example 1.1.1.1:443 or [2606:4700:4700::1111]:443');
	return true;
}
function apnWords(j) {
	return j && j.ok ? _('version %s, records %s').format(j.version || '—', j.count) + ' (' + (j.source === 'downloaded' ? _('updated from the server') : _('from the package')) + ')' : _('no data');
}
function updateWords(j) {
	return !j.ok ? _('Error: %s').format(j.error === 'the downloaded file is not an APN database' ? _('the downloaded file is not an APN database') :
		j.error ? _('the database server is unavailable') : _('no answer')) :
		j.updated ? _('Updated to version %s (%s records).').format(j.version, j.count) : _('The current version is installed (%s).').format(j.version);
}
/* qmodem-install status: what the card says (its «why» and refusals are the script's English) */
/* [the script's English, its words; %s takes the first group] */
var QM_WHY = [[/carries no QModem/, _('this MedveModem build has no QModem')],
	[/^OpenWrt (.*) is not 24\.10 or 25\.12$/, _('the packages are built for OpenWrt 24.10 and 25.12, and the router has %s')],
	[/^no QModem packages for (.*)$/, _('there are no QModem packages for the %s architecture')],
	[/is missing$/, _('the router has no package manager')],
	[/^running$/, _('an installation is already running')], [/^installed$/, _('QModem is already installed')],
	[/^no source$/, _('this MedveModem build has no address of QModem packages (a local build)')]];
function qmWhy(w) {
	w = String(w || '');
	for (var i = 0; i < QM_WHY.length; i++) { var m = QM_WHY[i][0].exec(w); if (m) return QM_WHY[i][1].format(m[1] || ''); }
	return w || _('no answer');
}
function qmWords(j) {
	if (!j || !j.ok) return _('no data');
	if (j.installed && j.update) return _('installed %s, an update to %s is available').format(j.installed_version || '', j.version);
	if (j.installed) return j.installed_version ? _('installed: %s').format(j.installed_version) : _('installed');
	if (j.state === 'running') return _('installing…');
	if (j.state === 'failed') return _('not installed: the last attempt failed');
	return _('not installed');
}
function agoLong(s) {
	return s < 60 ? _('%d s ago').format(Math.round(s)) : s < 3600 ? _('%d min ago').format(Math.floor(s / 60)) : s < 172800 ? _('%d h ago').format(Math.floor(s / 3600)) : _('%d d ago').format(Math.floor(s / 86400));
}
function parse(r) { try { return JSON.parse((r && r.stdout) || '{}'); } catch (e) { return {}; } }

/*
 * The browser keeps LuCI's scripts by LuCI's own version, not this package's: after an update a page met an old
 * common.js and showed dashes (the owner, 2026-10-20, fixed by a private window). This file knows its version; on a
 * mismatch both files are fetched past the cache and the page reloads once.
 */
var VIEW_VERSION = '0.16.0-alpha1-r2';
function staleFiles() {
	if (mm.VERSION === VIEW_VERSION) return false;
	try { if (sessionStorage.getItem('mm:refetched') === VIEW_VERSION + '/' + mm.VERSION) { mm.staleWarn(VIEW_VERSION); return false; }
		sessionStorage.setItem('mm:refetched', VIEW_VERSION + '/' + mm.VERSION); } catch (e) { return false; }
	var env = (window.L && L.env) || {}, v = env.resource_version ? '?v=' + env.resource_version : '', base = env.base_url || '/luci-static/resources';
	Promise.all(['medvemodem/common', 'view/medvemodem/dashboard', 'view/medvemodem/modem', 'view/medvemodem/settings'].map(function(p) {
		return fetch(base + '/' + p + '.js' + v, { cache: 'reload' }).catch(function() {});
	})).then(function() { location.reload(); });
	return true;
}

/* the installed DPI bypasses among the services the engine knows (zapret and its kin) */
var DPI = ['zapret', 'zapret2', 'byedpi', 'youtubeUnblock'];
function dpiOf(services) {
	return (services || []).filter(function(v) { return v && v.installed !== false && DPI.indexOf(v.name) >= 0; }).map(function(v) { return v.name; });
}

return view.extend({
	handleSaveApply: null,
	handleSave: null,
	handleReset: null,

	load: function() {
		return Promise.all([
			fs.exec(UPDATE, ['version']).then(parse, function() { return {}; }),
			getDevices(true).then(function(r) { return r && Array.isArray(r.devices) ? r.devices.filter(Boolean) : []; }, function() { return []; }),
			uciGet('medvemodem', 'core').then(function(r) { return (r && r.values) || {}; }, function() { return {}; }),
			uciGet('medvemodem', 'modem').then(function(r) { return (r && r.values) || {}; }, function() { return {}; }),
			getUplink().then(function(r) { return r || {}; }, function() { return {}; })
		]);
	},

	render: function(data) {
		if (staleFiles()) return E('div', { 'class': 'mm-root' }, E('p', { 'class': 'mm-faint' }, _('The page was updated on the router: loading the new version…')));
		var self = this;
		/* one view, two pages (the owner, 2026-11-12: «выделения в главном меню раздела Белые Списки и переноса туда всего с
		 * ними связанного»): «Белые списки» is this view at its own path, the same cards, the same loading and saving */
		this.wlPage = /\/medvemodem\/whitelist(\/|$)/.test(location.pathname || '');
		this.apn = data[0]; this.devices = data[1];
		this.core = data[2].core || {};
		this.names = data[3];
		this.wired = list((data[4] || {}).wired).filter(function(w) { return w && words(w.interface); });
		this.services = list((data[4] || {}).services).filter(function(v) { return v && words(v.name); });
		this.uplinkData = data[4] || {};
		this.message = E('div', { 'class': 'mm-callout is-info mm-message', role: 'status', hidden: true });
		this.save = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-save', disabled: true, click: mm.busy(function() { return self.store(); }) }, _('Save'));
		this.undo = E('button', { type: 'button', 'class': 'mm-btn mm-undo', disabled: true, click: mm.busy(function() { return self.reset(); }) }, _('Revert the changes'));
		/* built in the order they depend on each other (the recovery card makes the boxes the others take) */
		var c = {};
		c.service = this.serviceCard(); c.rec = this.recoveryCard(); c.names = this.namesCard();
		c.priority = this.priorityCard(); c.fo = this.failoverCard(); c.wl = this.wlCard(); c.reach = this.reachNode = this.reachCard(); c.svc = this.servicesCard();
		c.check = this.checkNode = this.channelsCard(); c.radio = this.radioCard(); c.forward = this.forwardCard(); c.alerts = this.alertsCard(); c.archive = this.archiveCard(); c.bot = this.botCard();
		c.apn = this.apnNode = this.apnCard(); c.qm = this.qmodemCard(); c.up = this.updateCard(); c.pkgs = this.pkgsCard(); c.mmr = this.mmrCard();
		if (this.wlPage) return this.renderWl(c);
		/* moved to «Белые списки»: a pointer in their place */
		this.al.wlRows.forEach(function(n) { n.hidden = true; });
		this.al.wlRows[1].after(this.movedNote(_('The whitelist alert')));
		c.wl = this.movedNote(_('The whitelist mode'), true);
		this.root = E('div', { 'class': 'mm-root mm-settings' }, [
			E('link', { rel: 'stylesheet', href: L.resource('medvemodem/dashboard.css') + '?v=' + CSS_VERSION }),
			this.head = pageHead(_('Settings'), E('p', {}, _('Common for all modems of this router')), function() { self.applyLevel(); }, 'settings'),
			/* tabs by purpose (second audit 2026-10-21, the owner: «разбить «Настройки» на 4 вкладки»); one «Сохранить»
			 * under them for all, but on «Служба», which saves nothing. The level is set in the page's head only */
			this.tabStrip = E('div', { 'class': 'mm-tabs-wrap' }, E('div', { 'class': 'mm-tabs', role: 'tablist' }))
		].concat(this.tabPanels([
			/* the service's state first, a tab of its own (the owner, 2026-10-23) */
			['service', _('Service status'), [c.service]],
			['net', _('Internet and backup'), [c.priority, E('div', { 'class': 'mm-grid2' }, [c.fo, c.reach]), c.wl, c.svc, this.chansPref()]],
			['rec', _('Modems'), [c.rec, c.radio, c.names]],
			['notify', _('Notifications'), [c.alerts, c.forward, c.archive, c.bot]],
			/* the emergency ModemManager restart, for every modem it drives: last (the owner, 2026-10-19) */
			['diag', _('Diagnostics and components'), [c.check, c.up, E('div', { 'class': 'mm-grid2' }, [c.apn, c.qm]), c.pkgs, c.mmr]]
		]), [
			this.message,
			this.saveBar = E('div', { 'class': 'mm-actions mm-savebar' }, [this.save, this.undo])
		]));
		this.reset();
		this.applyLevel();
		/* a link to a card (#service, #qmodem, #mmr) opens its tab and the card; else the tab last open in this browser */
		var want = String(location.hash || '').replace(/^#/, ''), node = want && this.root.querySelector('[id="' + want.replace(/"/g, '') + '"]');
		var last = null;
		try { last = localStorage.getItem('mm:settings-tab'); } catch (e) {}
		this.showTab(node && node.closest('[data-tab]') ? node.closest('[data-tab]').getAttribute('data-tab') : this.tabs[last] ? last : 'service', true);
		if (node && node.mmOpen) node.mmOpen(true);
		return this.root;
	},

	/* «moved to Белые списки», with the link; a card of its own where a whole card moved */
	movedNote: function(what, card) {
		var n = E('p', { 'class': 'mm-faint mm-wl-moved' }, [_('%s: on the «Whitelists» page.').format(what), ' ',
			E('a', { href: L.url('admin/modem/medvemodem/whitelist') }, _('Open') + ' →')]);
		return card ? E('div', { 'class': 'mm-card mm-wl-moved-card' }, n) : n;
	},

	/* «Белые списки»: what each channel has now, the services through any or all, the mode, the addresses, the tunnels'
	 * restart (the same card as on «Настройки», the owner: «дубликат логичнее»), the alerts and the bot */
	renderWl: function(c) {
		var self = this;
		this.tabs = {};
		this.root = E('div', { 'class': 'mm-root mm-settings mm-whitelist' }, [
			E('link', { rel: 'stylesheet', href: L.resource('medvemodem/dashboard.css') + '?v=' + CSS_VERSION }),
			this.head = pageHead(_('Whitelists'), E('p', {}, _('When the operator lets only its list of sites through: what each channel has now, which services open, and what MedveModem does then')),
				function() { self.applyLevel(); }, 'settings'),
			this.wlNowCard(), this.wlCheckCard(), this.wlProxyCard(), c.wl,
			this.sec('set-wl-reach', _('How a whitelist is told'), this.reachWl),
			c.svc, this.wlAlertsCard(), this.wlTipsCard(),
			this.message,
			this.saveBar = E('div', { 'class': 'mm-actions mm-savebar' }, [this.save, this.undo])]);
		this.reset();
		this.applyLevel();
		this.loadChannels();
		this.pxLoad();
		return this.root;
	},

	/* the local proxies (the owner, 2026-11-12/13: «почему не показывать в LuCI список всех найденных прокси», «с
	 * возможностью откорректировать адрес и порт и логин и пароль»): what downloads and package installs go through
	 * when GitHub or OpenWrt's server is closed directly. A found proxy is read from its program and never changed there:
	 * a correction is a section «proxy» of MedveModem's own config that replaces it in the list */
	wlProxyCard: function() {
		var self = this, k = this.px = {};
		k.body = E('tbody');
		k.say = E('p', { 'class': 'mm-faint mm-px-say', role: 'status', hidden: '' });
		k.form = E('div', { 'class': 'mm-px-form', hidden: '' });
		k.add = E('button', { type: 'button', 'class': 'mm-btn mm-px-add', click: function() { self.pxEdit(null); } }, _('Add a proxy'));
		return this.sec('set-wl-proxy', _('Local proxies'), [
			E('p', { 'class': 'mm-faint' }, _('The tunnel\'s proxies on the router. When direct access to GitHub or the OpenWrt package server is blocked, MedveModem downloads and installs through them, in this order. A found proxy is read from its program and never changed there: a correction is kept in MedveModem\'s settings.')),
			E('div', { 'class': 'mm-table-scroll' }, E('table', { 'class': 'mm-table mm-px-table' }, [
				E('thead', {}, E('tr', {}, [E('th', {}, _('Address')), E('th', {}, _('Source')), E('th', {}, _('Login')), E('th', {}, _('Check')), E('th', {}, '')])),
				k.body])),
			k.say, k.form, E('div', { 'class': 'mm-row' }, k.add)]);
	},

	pxLoad: function() {
		var self = this, k = this.px;
		return fs.exec(PROXY, ['show']).then(function(r) {
			var j = [];
			try { j = JSON.parse((r && r.stdout) || '[]'); } catch (e) {}
			self.pxRows(Array.isArray(j) ? j : []);
		}, function(e) { self.pxRows([]); text(k.say, _('The list is not read: %s.').format(failText(e))); k.say.hidden = false; });
	},

	pxRows: function(items) {
		var self = this, k = this.px;
		var TYPE = { mixed: _('SOCKS5 and HTTP'), socks: 'SOCKS5', http: 'HTTP' };
		k.items = items;
		if (!items.length) {
			k.body.replaceChildren(E('tr', {}, E('td', { colspan: '5', 'class': 'mm-faint' },
				_('No local proxy found. Switch the mixed proxy on in podkop or forkop, or add one here: then downloads and installs go through it when direct access is blocked.'))));
			return;
		}
		k.body.replaceChildren.apply(k.body, items.map(function(x) {
			var cell = E('span', { 'class': 'mm-faint mm-px-check' }, '—');
			var acts = [E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-px-test', click: mm.busy(function() { return self.pxCheck(x, cell); }) }, _('Run the check')),
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-px-edit', click: function() { self.pxEdit(x); } }, _('Change'))];
			if (x.source === 'own')
				acts.push(E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-px-drop', click: mm.busy(function() { return self.pxDrop(x); }) }, _('Delete')));
			return E('tr', { 'class': x.source === 'own' ? 'mm-px-own' : '' }, [
				E('td', { 'class': 'mm-mono' }, [x.host + ':' + x.port, E('div', { 'class': 'mm-faint' }, TYPE[x.type] || x.type)]),
				E('td', {}, x.source === 'own' ? (x.replaces ? _('added here, instead of %s').format(x.replaces.replace(/^[a-z0-9]+:\/\//, '')) : _('added here')) : x.source),
				E('td', {}, x.login ? x.user : '—'),
				E('td', {}, cell),
				E('td', {}, E('div', { 'class': 'mm-row' }, acts))]);
		}));
	},

	pxCheck: function(x, cell) {
		text(cell, _('Checking…'));
		return fs.exec(PROXY, ['check', x.url]).then(parse, function(e) { return { error: failText(e) }; }).then(function(j) {
			if (!j.ok) { text(cell, _('not checked: %s').format(j.error || _('no answer'))); cell.className = 'mm-px-check mm-nc-bad'; return; }
			var one = function(name, v) { return v && v.ok ? _('%s: %s ms').format(name, mm.num(v.ms)) : _('%s: no answer').format(name); };
			text(cell, one('GitHub', j.github) + ' · ' + one('OpenWrt', j.openwrt));
			cell.className = 'mm-px-check ' + ((j.github || {}).ok && (j.openwrt || {}).ok ? 'mm-nc-ok' : (j.github || {}).ok || (j.openwrt || {}).ok ? 'mm-nc-warn' : 'mm-nc-bad');
		});
	},

	/* the form: a found proxy corrected (its url in «replaces», its login kept when the password is left empty), or our own */
	pxEdit: function(x) {
		var self = this, k = this.px, own = x && x.source === 'own';
		var f = k.f = {
			type: E('select', { 'class': 'mm-select', id: 'mm-px-type' }, [['mixed', _('SOCKS5 and HTTP')], ['socks', 'SOCKS5'], ['http', 'HTTP']].map(function(o) {
				return E('option', { value: o[0] }, o[1]); })),
			host: E('input', { type: 'text', 'class': 'mm-input mm-mono', id: 'mm-px-host', placeholder: '127.0.0.1', spellcheck: 'false' }),
			port: E('input', { type: 'number', 'class': 'mm-input mm-mono', id: 'mm-px-port', min: '1', max: '65535', placeholder: '2080' }),
			user: E('input', { type: 'text', 'class': 'mm-input mm-mono', id: 'mm-px-user', autocomplete: 'off', spellcheck: 'false' }),
			pass: E('input', { type: 'password', 'class': 'mm-input mm-mono', id: 'mm-px-pass', autocomplete: 'new-password',
				placeholder: x && x.login ? _('unchanged') : '' })
		};
		f.type.value = x ? x.type : 'mixed';
		f.host.value = x ? x.host : '';
		f.port.value = x ? String(x.port) : '';
		f.user.value = x && x.login ? x.user : '';
		var field = function(id, label, input) { return E('label', { 'for': id, 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, label), input]); };
		k.editing = x;
		k.form.replaceChildren(
			E('p', { 'class': 'mm-faint' }, !x ? _('A proxy of your own: it is tried first.') : own ? _('Your proxy.') :
				_('A correction of the proxy found in %s: it takes its place in the list, %s itself is not changed.').format(x.source, x.source)),
			field('mm-px-type', _('Type'), f.type), field('mm-px-host', _('Address'), f.host), field('mm-px-port', _('Port'), f.port),
			field('mm-px-user', _('Login'), f.user), field('mm-px-pass', _('Password'), f.pass),
			E('div', { 'class': 'mm-actions' }, [
				E('button', { type: 'button', 'class': 'mm-btn is-primary mm-px-save', click: mm.busy(function() { return self.pxSave(); }) }, _('Save')),
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-px-cancel', click: function() { k.form.hidden = true; k.editing = undefined; } }, _('Cancel'))]));
		k.form.hidden = false;
		f.host.focus();
	},

	pxSave: function() {
		var self = this, k = this.px, f = k.f, x = k.editing, own = x && x.source === 'own';
		var host = f.host.value.trim(), port = f.port.value.trim(), user = f.user.value.trim(), pass = f.pass.value;
		var bad = !/^([A-Za-z0-9.-]{1,253}|\[[0-9A-Fa-f:.]{2,45}\])$/.test(host) ? _('the address: a name or an IP address, for example 127.0.0.1') :
			!/^[0-9]{1,5}$/.test(port) || +port < 1 || +port > 65535 ? _('the port: a number from 1 to 65535') :
			user && !/^[A-Za-z0-9_.@-]{1,80}$/.test(user) ? _('the login: Latin letters, digits and . _ @ -') :
			pass && (pass.length > 256 || /["\\\n\r]/.test(pass)) ? _('the password: up to 256 characters, without quotes and backslashes') :
			pass && !user ? _('a password without a login') : '';
		if (bad) { text(k.say, _('Not saved: %s.').format(bad)); k.say.hidden = false; return Promise.resolve(); }
		var values = { type: f.type.value, host: host, port: port, enabled: '1' }, drop = [];
		if (user) values.username = user; else drop.push('username', 'password');
		if (pass) values.password = pass;
		/* a found proxy's login is kept when its password is left empty; a changed login without one is a new login */
		if (x && !own) values.replaces = x.url;
		/* an emptied login is deleted only when the section has one: deleting an absent option is ubus «not found» */
		var op = own ? function() { return uciSet('medvemodem', x.section, values).then(function() {
				return drop.length && x.login ? uciDelete('medvemodem', x.section, drop) : null; }); } :
			function() { return uciAdd('medvemodem', 'proxy', null, values); };
		text(k.say, _('Saving…')); k.say.hidden = false;
		return op().then(function() { return uciCommit('medvemodem'); }).then(function() {
			k.form.hidden = true; k.editing = undefined;
			text(k.say, _('Saved: downloads and installs try this proxy now.'));
			return self.pxLoad();
		}, function(e) {
			text(k.say, _('Saving error: %s. The changes are not applied.').format(failText(e)));
			return uciRevert('medvemodem').catch(function() {});
		});
	},

	pxDrop: function(x) {
		var self = this, k = this.px;
		if (!window.confirm(x.replaces ? _('Delete your correction? The found proxy is used as its program has it.') : _('Delete this proxy?')))
			return Promise.resolve();
		return uciDrop('medvemodem', x.section).then(function() { return uciCommit('medvemodem'); }).then(function() {
			text(k.say, _('Deleted.')); k.say.hidden = false;
			return self.pxLoad();
		}, function(e) {
			text(k.say, _('Saving error: %s. The changes are not applied.').format(failText(e))); k.say.hidden = false;
			return uciRevert('medvemodem').catch(function() {});
		});
	},

	/* each channel now, from the engine's uplink: open, the whitelist only, the internet through the tunnel, silent */
	wlNowCard: function() {
		var self = this, u = this.uplinkData, tunnel = (u.tunnel || {}).ok === true, names = {};
		var given = {};
		Object.keys(this.names || {}).forEach(function(k) { var n = self.names[k]; if (n && n.device_id && words(n.name)) given[n.device_id] = words(n.name); });
		list(this.devices).forEach(function(d) { names[d.device_id] = given[d.device_id] || words(d.name) || words((d.modemmanager || {}).model) || words(d.product) || d.device_id; });
		/* the main first, then the backups in their priority (the route metric), numbered as the dashboard numbers them (the
		 * owner, 2026-11-16: «резерв номер какой надо бы писать, мы же уже знаем») */
		var isMain = function(p) { return p.role === 'carrying' || p.role === 'limited'; };
		var metricOf = function(p) { return typeof p.metric === 'number' ? p.metric : typeof p.kernel_metric === 'number' ? p.kernel_metric : 1e9; };
		var paths = list(u.paths).slice().sort(function(a, b) { return (isMain(b) ? 1 : 0) - (isMain(a) ? 1 : 0) || metricOf(a) - metricOf(b); });
		var backups = paths.filter(function(p) { return !isMain(p); }).length, nth = 0;
		var rows = paths.map(function(pth) {
			var r = pth.reach || {}, lim = r.limited || pth.role === 'limited' || pth.role === 'backup_limited';
			var st = lim ? (tunnel && pth.role === 'limited' ? [_('the whitelist; the internet through the tunnel'), 'ok'] : [_('the whitelist only'), 'warn']) :
				r.ok === true ? [_('the whole internet'), 'ok'] : r.ok === false ? [_('no internet'), 'err'] : [_('not checked yet'), 'idle'];
			/* the main channel filled (the owner, 2026-11-12: «основной надо выделять заливкой») */
			var main = pth.role === 'carrying' || pth.role === 'limited';
			return E('tr', { 'class': main ? 'mm-wl-main' : '' }, [E('td', {}, names[pth.device_id] || words(pth.name) || pth.device || '—'),
				E('td', {}, E('span', { 'class': 'mm-light is-' + st[1] }, [E('i'), st[0]])),
				E('td', { 'class': 'mm-faint' }, isMain(pth) ? _('main') : backups > 1 ? _('backup %d').format(++nth) : _('backup'))]);
		});
		return this.sec('set-wl-now', _('Now'), [rows.length ?
			E('table', { 'class': 'mm-table mm-wl-now' }, [E('thead', {}, E('tr', {}, [E('th', {}, _('Channel')), E('th', {}, _('Channel status')), E('th', {}, _('Role'))])),
				E('tbody', {}, rows)]) : E('p', { 'class': 'mm-faint' }, _('No channel is checked yet.')),
			E('p', { 'class': 'mm-faint' }, _('A channel counts as «the whitelist only» when the main check addresses are silent and the whitelist ones answer. MedveModem does not restart such a modem and moves the traffic to a free channel when there is one.'))]);
	},

	/* the services through one channel or through all of them in turn (the owner, 2026-11-12: «с выбором из выпадающего
	 * меню всех или любого из модемов»): the same netcheck as the modem page's, a table service × channel for all */
	wlCheckCard: function() {
		var self = this, k = this.wlc = {};
		k.pick = E('select', { 'class': 'mm-select', id: 'mm-wlc-dev' }, E('option', { value: '*' }, _('All channels')));
		k.say = E('p', { 'class': 'mm-faint mm-wlc-say', role: 'status', hidden: '' });
		k.out = E('div', { 'class': 'mm-wlc-out' });
		/* One dismissible diagnostic after the whole run, not a per-route 0/N balloon. */
		k.verdict = mm.dismissible(E('div', { 'class': 'mm-callout is-info mm-wlc-result', hidden: '' }), 'medvemodem:whitelist-check');
		k.run = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-wlc-run', disabled: true, click: mm.busy(function() { return self.wlCheck(); }) }, _('Check the services'));
		return this.sec('set-wl-check', _('Service check'), [
			E('p', { 'class': 'mm-faint' }, _('Through the chosen channel, even if another one is main: do the services open. About 10 seconds a channel and less than 1 MB of traffic. Yandex, VK and Wildberries are for comparison: under whitelists only such open.')),
			E('label', { 'for': 'mm-wlc-dev', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Channel')), k.pick]),
			E('div', { 'class': 'mm-row' }, k.run), k.say, k.out, k.verdict]);
	},

	wlCheck: function() {
		var self = this, k = this.wlc, all = k.pick.value === '*';
		var devs = all ? Array.prototype.slice.call(k.pick.options, 1).map(function(o) { return [o.value, o.textContent]; }) :
			[[k.pick.value, (k.pick.selectedOptions[0] || {}).textContent]];
		if (!devs.length || k.running) return Promise.resolve();
		k.running = true; k.run.disabled = true; k.say.hidden = false; k.verdict.hidden = true; k.out.replaceChildren();
		var got = [];
		return devs.reduce(function(p, d) {
			return p.then(function() {
				text(k.say, _('Checking the services through %s…').format(d[0]));
				return mm.execLong(fs, 'netcheck', NETCHECK, [d[0]]).then(function(r) {
					var j = null;
					try { j = JSON.parse((r && r.stdout) || ''); } catch (e) {}
					got.push({ dev: d[0], name: d[1], res: j && !j.error ? list(j.results) : null,
						err: !j ? _('no answer') : j.error ? (NC_ERROR[j.error] || j.error) : '' });
				}, function(e) { got.push({ dev: d[0], name: d[1], res: null, err: failText(e) }); });
			});
		}, Promise.resolve()).then(function() {
			k.running = false; k.run.disabled = false;
			text(k.say, _('Checked at %s.').format(new Date().toLocaleTimeString(mm.LANG)));
			/* The whitelist diagnosis needs positive reachability of an allowed-site control.
			 * All failures are not proof of an operator whitelist. */
			var suspicious = [], clear = [], unknown = [];
			got.forEach(function(g) {
				var services = g.res || [], open = services.filter(function(x) { return x.group === 'open'; }),
					allow = services.filter(function(x) { return x.group === 'list'; });
				if (!open.length || !allow.length || !g.res) { unknown.push(g.name); return; }
				if (open.some(function(x) { return x.verdict === 'ok'; })) clear.push(g.name);
				else if (allow.some(function(x) { return x.verdict === 'ok'; }) &&
					open.some(function(x) { return x.verdict !== 'tunnel'; }))
					suspicious.push(g.name);
				else unknown.push(g.name);
			});
			var diagnosis = suspicious.length ? _('Possible operator whitelist on: %s. Ordinary sites fail while control sites answer.').format(suspicious.join(', ')) :
				clear.length ? _('No whitelist signs on the checked routes: ordinary internet sites respond.') :
				_('Whitelist mode cannot be determined: no control-site response or insufficient results.');
			if (unknown.length && (suspicious.length || clear.length))
				diagnosis += ' ' + _('Inconclusive routes: %s.').format(unknown.join(', '));
			k.verdict.classList.toggle('is-err', !!suspicious.length);
			k.verdict.classList.toggle('is-ok', !suspicious.length && !!clear.length && !unknown.length);
			var close = k.verdict.querySelector('.mm-dismiss');
			k.verdict.replaceChildren(E('strong', {}, _('Whitelist diagnosis')), E('span', {}, diagnosis));
			if (close) k.verdict.appendChild(close);
			mm.bannerShow(k.verdict, true);
			if (!all) {
				var g = got[0], body = E('tbody');
				if (!g.res) { text(k.say, _('The check did not run: %s.').format(g.err)); return; }
				body.replaceChildren.apply(body, ncRows(g.res));
				k.out.replaceChildren(ncTable(body));
				return;
			}
			/* all: the services down, the channels across */
			var first = got.filter(function(g) { return g.res && g.res.length; })[0];
			if (!first) { text(k.say, _('The check did not run: %s.').format(got.map(function(g) { return g.dev + ': ' + g.err; }).join('; '))); return; }
			var head = E('tr', {}, [E('th', {}, _('Service'))].concat(got.map(function(g) { return E('th', { title: g.name }, g.dev); })));
			var rows = first.res.map(function(x) {
				return E('tr', {}, [E('td', {}, mm.ncName(x))].concat(got.map(function(g) {
					var y = g.res ? g.res.filter(function(z) { return z.id === x.id; })[0] : null, cell = mm.ncCell(y);
					return E('td', { 'class': 'mm-nc-signal' + (y ? '' : ' mm-nc-unchecked'), title: y ? '' : (g.err || _('not checked')) }, cell.node);
				})));
			});
			k.out.replaceChildren(E('div', { 'class': 'mm-table-scroll' }, E('table', { 'class': 'mm-table mm-wlc-table' }, [E('thead', {}, head), E('tbody', {}, rows)])));
		});
	},

	/* the whitelist alert (its rows of the SMS alerts card, saved by that card's own save) and the bot's own */
	wlAlertsCard: function() {
		var self = this, a = this.al;
		var save = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-wl-al-save', click: mm.busy(function() { return self.alertsSave(); }) }, _('Save the alert'));
		var off = E('p', { 'class': 'mm-faint mm-wl-al-off' }, [_('SMS alerts as a whole are switched on, and their recipients chosen, in Settings → Notifications.'), ' ',
			E('a', { href: L.url('admin/modem/medvemodem/settings') + '#set-alerts' }, _('Open') + ' →')]);
		var bot = E('a', { 'class': 'mm-btn is-quiet mm-wl-bot', href: L.url('admin/modem/medvemodem/bot/settings') }, _('The bot\'s notifications in LuCI'));
		return this.sec('set-wl-alerts', _('Notifications'), [
			this.group(_('SMS alert')), a.wlRows[0], a.wlRows[1], E('div', { 'class': 'mm-actions' }, [save]), a.out, off,
			this.group(_('Telegram bot')),
			E('p', { 'class': 'mm-faint' }, _('The bot says when a channel is left with the whitelist only and when it is lifted; the kinds and the quiet hours are in its settings: in LuCI, or in Telegram with /settings. Its «🚧 Whitelists» button shows the channels, switches the whitelist mode and runs the blocking test.')),
			E('div', { 'class': 'mm-row' }, bot)]);
	},

	wlTipsCard: function() {
		return this.sec('set-wl-tips', _('Tips'), [E('ul', { 'class': 'mm-wl-tips' }, [
			E('li', {}, _('The router\'s DNS under a whitelist: 77.88.8.8 and 77.88.8.1 (Yandex) usually answer; foreign resolvers do not.')),
			E('li', {}, _('A DPI bypass (zapret, byedpi, youtubeUnblock) does not help under a whitelist and may break the tunnel: on a mobile main channel stop it.')),
			E('li', {}, _('A tunnel survives a whitelist when its server sits on an address the operator lets through: «Suggest by the community lists» in the whitelist mode marks such servers.'))])], '', false);
	},

	/* the channels table under the dashboard's verdict («Подробный»): on or off in this browser, at once, nothing to save
	 * (the owner, 2026-10-26: «для неё отдельный тумблер вкл/выкл в настройках») */
	chansPref: function() {
		var on = true, box;
		try { on = localStorage.getItem('mm:chans') !== '0'; } catch (e) {}
		box = E('input', { type: 'checkbox', id: 'mm-chans-pref', change: function() {
			try { localStorage.setItem('mm:chans', box.checked ? '1' : '0'); } catch (e) {}
		} });
		box.checked = on;
		return E('div', { 'class': 'mm-card mm-chans-pref' }, [E('label', { 'for': 'mm-chans-pref', 'class': 'mm-check' }, [box, ' ',
			_('Show the channels table on the main page («Detailed» level)')]),
			E('p', { 'class': 'mm-faint' }, _('Applies in this browser at once, without «Save».'))]);
	},

	/* a group of switches inside a card, under its own small heading (the owner, 2026-11-09: «тематически визуально
	 * галочки разделить внутри вкладок») */
	group: function(title) { return E('h4', { 'class': 'mm-group' }, title); },

	tabPanels: function(list) {
		var self = this, strip = this.tabStrip.firstChild;
		this.tabs = {};
		return list.map(function(t) {
			var panel = E('section', { 'class': 'mm-panel mm-set-panel', role: 'tabpanel', id: 'mm-set-' + t[0], 'data-tab': t[0] }, t[2]);
			var tab = E('button', { type: 'button', role: 'tab', 'class': 'mm-tab', 'aria-controls': 'mm-set-' + t[0],
				click: function() { self.showTab(t[0]); } }, t[1]);
			self.tabs[t[0]] = { tab: tab, panel: panel };
			strip.appendChild(tab);
			return panel;
		});
	},

	showTab: function(name, quiet) {
		Object.keys(this.tabs).forEach(function(k) {
			this.tabs[k].panel.hidden = k !== name;
			this.tabs[k].tab.setAttribute('aria-selected', String(k === name));
		}, this);
		if (this.tabStrip) (this.tabsSeen = this.tabsSeen || mm.tabsHint(this.tabStrip, this.tabStrip.firstChild))(this.tabs[name] && this.tabs[name].tab);
		/* «Служба» acts at once (start, stop, the boot switch): no «Сохранить» under it */
		if (this.saveBar) this.saveBar.hidden = name === 'service';
		if (!quiet) try { localStorage.setItem('mm:settings-tab', name); } catch (e) {}
	},

	/* the Telegram bot has its own page (its package); here where it is, so «Уведомления» holds both ways to be told */
	botCard: function() {
		var link = E('a', { 'class': 'mm-btn is-quiet mm-bot-link', href: L.url('admin/modem/medvemodem/bot') }, _('Open the Telegram bot settings'));
		var none = E('p', { 'class': 'mm-faint mm-bot-none', hidden: '' }, _('The bot package is not installed: System → Software → luci-app-medvemodem-bot.'));
		/* installed when its page's file is there: a static file, no rpc */
		if (window.fetch) fetch(L.resource('view/medvemodem/bot.js'), { method: 'HEAD', cache: 'no-store' }).then(function(r) {
			if (!r.ok) { link.hidden = true; none.hidden = false; }
		}, function() {});
		return this.sec('set-bot', _('Telegram bot'), [none,
			E('p', { 'class': 'mm-faint' }, _('The bot sends the modems\' state, failures and recovery to Telegram, incoming SMS if you wish; from it a modem can be reconnected or made the main one. It is a separate package, luci-app-medvemodem-bot.')),
			E('div', { 'class': 'mm-row' }, link)], '', false);
	},

	/* a section card with a collapsible body, as on the other pages */
	sec: function(key, title, body, gist, dflt) {
		var n = mm.section(key, title, body, gist, dflt);
		(this.secs = this.secs || {})[key] = n;
		return n;
	},

	/* the collapsed headers say what the form holds (the owner, 2026-10-19) */
	applyGists: function() {
		var b = this.boxes, secs = this.secs || {}, on = function(x) { return x && x.checked; };
		function set(key, line) { if (secs[key] && secs[key].mmGist) secs[key].mmGist(line); }
		set('set-rec', [on(b.auto_connect) ? _('auto-connect') : '', on(b.auto_recovery) ? _('recovery') : '', on(b.host_fixes) ? _('port fixes') : '']
			.filter(Boolean).join(' · ') || _('all off'));
		set('set-fo', on(b.failover) ? _('enabled') + (this.selects.failback.value === 'demote' ? ' · ' + _('no return') : '') : _('disabled'));
		set('set-sms-archive', on(b.sms_keep_all) ? _('all kept in the router') : on(b.sms_archive) ? _('moves to the router') : _('disabled'));
		set('set-radio', [on(b.antennas) ? _('antennas') : '', on(b.cell_info) ? _('cells') : ''].filter(Boolean).join(' · ') || _('nothing on'));
		var reach = list((this.areas.reach_target || {}).value ? this.areas.reach_target.value.split(/\s+/).filter(Boolean) : []);
		set('set-reach', reach.length ? _('addresses: %d').format(reach.length) : _('by default'));
		set('set-names', _('modems: %d').format(Object.keys(this.nameInputs || {}).length));
	},


	dirty: function() {
		var changed = this.read() !== this.saved;
		this.save.disabled = this.undo.disabled = !changed;
		this.applyGists();
		if (changed) this.say('');
	},

	say: function(line, err) {
		text(this.message, line); this.message.hidden = !line;
		this.message.className = 'mm-callout mm-message ' + (err ? 'is-err' : 'is-info');
	},

	/* «Базовый» keeps what a household needs (audit 2026-10-13): the switches; the check addresses, the manual
	 * channels, the failback choice and the APN database from «Подробный». Hidden, not reset: a save keeps them */
	applyLevel: function() {
		var basic = level() === 'normal';
		[this.reachNode, this.apnNode, this.checkNode].concat(this.foFine || []).forEach(function(n) { if (n) n.hidden = basic; });
		/* the channel list only when the card shows: «Базовый» runs nothing for it */
		if (!basic && this.chk && !this.chk.loaded) { this.chk.loaded = true; this.loadChannels(); }
	},

	/* «Проверка каналов» (decision 2026-10-14): the service check and the DNS check by hand through any channel with a
	 * default route — WAN, Wi-Fi, a modem; the same tables as the modem page */
	channelsCard: function() {
		var self = this, c = this.chk = {};
		c.pick = E('select', { 'class': 'mm-select', id: 'mm-chk-dev' });
		c.say = E('p', { 'class': 'mm-faint mm-chk-say', role: 'status' }, _('Loading the channel list…'));
		c.ncVerdict = E('div', { 'class': 'mm-callout is-info mm-chk-verdict', hidden: '' });
		c.ncBody = E('tbody'); c.ncTable = ncTable(c.ncBody);
		c.dnsBody = E('tbody'); c.dnsSay = E('p', { 'class': 'mm-faint mm-chk-dns-say', role: 'status' });
		c.dns = E('div', { 'class': 'mm-dns', hidden: '' }, [E('h4', {}, 'DNS'), c.dnsSay, dnsTable(c.dnsBody)]);
		c.svc = E('button', { type: 'button', 'class': 'mm-btn mm-chk-svc', disabled: true, click: mm.busy(function() { return self.runCheck('svc'); }) }, _('Check the services'));
		c.dnsBtn = E('button', { type: 'button', 'class': 'mm-btn mm-chk-dns', disabled: true, click: mm.busy(function() { return self.runCheck('dns'); }) }, _('Check DNS'));
		var card = this.sec('set-check', _('Channel check'), [
			E('p', { 'class': 'mm-faint' }, _('Through the chosen channel, even if another one is main: do the services open and does the provider not replace DNS. The services take about 10 seconds, DNS up to a minute; less than 1 MB of traffic.')),
			E('label', { 'for': 'mm-chk-dev', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Channel')), c.pick]),
			E('div', { 'class': 'mm-row' }, [c.svc, c.dnsBtn]), c.say, c.ncVerdict, c.ncTable, c.dns]);
		return card;
	},

	/* a channel by its modem's name, the interface and the device after it (the owner, 2026-11-16: «не понятно какой
	 * интерфейс какому модему соответствует, угадайка»): «E3372 (mm5 · eth1)»; a wired one as before */
	chanLabel: function(x) {
		var self = this, given = {}, id = null;
		Object.keys(this.names || {}).forEach(function(k) { var n = self.names[k]; if (n && n.device_id && words(n.name)) given[n.device_id] = words(n.name); });
		list((this.uplinkData || {}).paths).forEach(function(p) { if (p && p.device === x.device && p.device_id) id = p.device_id; });
		var d = id ? list(this.devices).filter(function(z) { return z.device_id === id; })[0] : null;
		var name = id ? given[id] || (d && (words(d.name) || words((d.modemmanager || {}).model) || words(d.product))) : '';
		return name ? name + ' (' + x.interface + ' · ' + x.device + ')' : x.interface + ' — ' + x.device;
	},

	loadChannels: function() {
		var self = this, c = this.chk;
		return mm.execLong(fs, 'channels', CLI, ['channels']).then(function(r) {
			var j = [];
			try { j = JSON.parse((r && r.stdout) || '[]'); } catch (e) {}
			c.pick.replaceChildren.apply(c.pick, list(j).map(function(x) {
				return E('option', { value: x.device }, self.chanLabel(x) + (x.dns && x.dns.length ? ' (DNS ' + x.dns[0] + ')' : ''));
			}));
			c.svc.disabled = c.dnsBtn.disabled = !j.length;
			if (self.wlc) {
				self.wlc.pick.replaceChildren.apply(self.wlc.pick, [E('option', { value: '*' }, _('All channels'))].concat(list(j).map(function(x) {
					return E('option', { value: x.device }, self.chanLabel(x));
				})));
				self.wlc.run.disabled = !j.length;
			}
			text(c.say, j.length ? '' : _('No channels are up with a route to the internet.'));
			c.say.hidden = !!j.length;
		}, function(e) { text(c.say, _('The channel list was not received: %s.').format(failText(e))); });
	},

	runCheck: function(kind) {
		var c = this.chk, dev = c.pick.value, where = (c.pick.selectedOptions[0] || {}).textContent;
		if (!dev || c.running) return Promise.resolve();
		c.running = true; c.svc.disabled = c.dnsBtn.disabled = true; c.say.hidden = false;
		text(c.say, (kind === 'svc' ? _('Checking the services through %s…') : _('Checking DNS through %s…')).format(dev));
		var run = kind === 'svc' ? mm.execLong(fs, 'netcheck', NETCHECK, [dev]) : mm.execLong(fs, 'dns-check', CLI, ['dns-check', dev]);
		return run.then(function(r) {
			var j = null;
			try { j = JSON.parse((r && r.stdout) || ''); } catch (e) {}
			if (!j || j.error) throw new Error(NC_ERROR[(j || {}).error] || (j && j.error) || (r && r.stderr) || _('no answer'));
			if (kind === 'svc') {
				var say = ncSummary(j.results, dev);
				c.ncBody.replaceChildren.apply(c.ncBody, ncRows(j.results));
				c.ncTable.hidden = !list(j.results).length;
				c.ncVerdict.hidden = !say; text(c.ncVerdict, say);
			} else {
				c.dnsBody.replaceChildren.apply(c.dnsBody, dnsRows(j));
				text(c.dnsSay, dnsSummary(j));
				c.dns.hidden = false;
			}
			text(c.say, _('Checked through the channel %s.').format(where));
		}).catch(function(e) {
			text(c.say, _('The check did not run: %s.').format(failText(e)));
		}).then(function() { c.running = false; c.svc.disabled = c.dnsBtn.disabled = !c.pick.value; });
	},

	/* «Имена модемов»: a name per modem the engine knows, kept when the modem moves to another USB port */
	/* «Приоритет модемов» (owner's request 2026-10-15): first = main, the rest back it up in this order. The engine
	 * gives the interfaces the route metrics 100, 110, 120... at once (ubus priority); the dashboard follows the order */
	priorityCard: function() {
		var self = this, p = this.prio = {};
		/* the router's own uplinks (a wired WAN) take a place in the order too (field request 2026-10-23: the wire first,
		 * the modems its backup, or the other way round); the engine knows them as iface:<name> */
		var rows = this.devices.filter(function(d) { return typeof d.device_id === 'string' && words((d.connection || {}).interface); })
			.map(function(d) { var m = (d.connection || {}).metric; return { key: d.device_id, d: d, metric: typeof m === 'number' ? m : 1e9 }; })
			.concat(this.wired.map(function(w) { return { key: 'iface:' + w.interface, w: w, metric: typeof w.metric === 'number' ? w.metric : 0 }; }));
		p.rows = rows.map(function(r, i) { return [r, i]; }).sort(function(a, b) { return a[0].metric - b[0].metric || a[1] - b[1]; })
			.map(function(x) { return x[0]; });
		p.devices = p.rows.filter(function(r) { return r.d; }).map(function(r) { return r.d; });
		p.order = p.rows.map(function(r) { return r.key; });
		p.saved = p.order.join('|');
		p.list = E('ol', { 'class': 'mm-thrs mm-prio' });
		p.apply = E('button', { type: 'button', 'class': 'mm-btn mm-prio-apply', disabled: true, click: mm.busy(function() { return self.applyPriority(); }) }, _('Apply the order'));
		this.drawPriority();
		var wiredNote = this.wired.length ? E('p', { 'class': 'mm-faint mm-prio-wired' }, this.core.failover === '1' ?
			_('A wired channel is in the list: when the cable is unplugged or the provider\'s link breaks, the traffic goes to the next channel by itself; when the wire answers again, it comes back.') :
			_('A wired channel is in the list. When the cable is unplugged, the traffic goes to the next channel by itself; to switch also when the wire is connected but the internet does not answer, turn on “Switch the internet to a working channel” on the “Internet and backup” tab.')) : '';
		return this.sec('set-priority', this.wired.length ? _('Channel priority') : _('Modem priority'), [
			p.rows.length ? p.list : E('p', { 'class': 'mm-faint' }, _('No modems with a network interface. The order can be set after the first connection.')),
			E('p', { 'class': 'mm-faint' }, _('The first is the main channel, the rest are backups in this order. “Apply” gives the interfaces the route metrics 100, 110, 120… at once: the traffic goes through the modem with the lower one, on a failure it switches to the next. The cards on “Modems” follow the same order.')),
			wiredNote, p.rows.length > 1 ? E('div', { 'class': 'mm-actions' }, [p.apply]) : ''],
			p.rows.length ? _('main: %s').format(this.prioName(p.rows[0])) : '');
	},

	prioName: function(r) {
		var d = r.d;
		return d ? words(d.name) || words((d.modemmanager || {}).model) || words(d.product) || (d.device_id || _('USB modem')) :
			_('wired channel “%s”').format(r.w.interface);
	},

	drawPriority: function() {
		var self = this, p = this.prio, byKey = {};
		p.rows.forEach(function(r) { byKey[r.key] = r; });
		var move = function(i, by) { var o = p.order, t = o[i]; o[i] = o[i + by]; o[i + by] = t; self.drawPriority(); };
		p.list.replaceChildren.apply(p.list, p.order.map(function(key, i) {
			var r = byKey[key], d = r.d, w = r.w, name = d ? (words(d.name) || words((d.modemmanager || {}).model) || words(d.product) || _('USB modem')) :
				_('Wired channel “%s”').format(w.interface);
			var where = d ? _('interface %s').format(d.connection.interface) + (d.present === false ? ' · ' + _('disconnected from USB') : '') :
				[w.proto, words(w.device), w.up ? '' : _('no link')].filter(Boolean).join(' · ');
			return E('li', { 'class': 'mm-contact mm-prio-row' + (w ? ' is-wired' : ''), 'data-id': key }, [
				E('b', { 'class': 'mm-prio-n' }, String(i + 1)),
				E('div', { 'class': 'mm-field' }, [E('span', {}, name), E('span', { 'class': 'mm-faint' },
					(i ? _('backup %d').format(i) : _('main')) + ' · ' + where)]),
				E('span', { 'class': 'mm-row' }, [
					E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-prio-up', disabled: i === 0 ? '' : null, 'aria-label': _('Up: %s').format(name),
						click: function() { move(i, -1); } }, '↑'),
					E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-prio-down', disabled: i === p.order.length - 1 ? '' : null, 'aria-label': _('Down: %s').format(name),
						click: function() { move(i, 1); } }, '↓')])]);
		}));
		p.apply.disabled = p.order.join('|') === p.saved;
	},

	applyPriority: function() {
		var self = this, p = this.prio;
		p.apply.disabled = true;
		return setPriority(p.order.slice()).then(function(r) {
			r = r || {};
			if (r.result !== 'done') { self.say(_('The order is not applied: %s.').format(r.reason === 'a configuration change is being applied' ?
				_('other settings are being applied now, try again in a few seconds') : r.reason || r.result || _('no answer')), true); p.apply.disabled = false; return; }
			p.saved = p.order.join('|');
			self.say(_('The order is applied: the main one is %s.').format(p.list.querySelector('.mm-prio-row .mm-field span').textContent));
		}, function(e) { self.say(_('The order is not applied: %s').format(failText(e)), true); p.apply.disabled = false; });
	},

	/* «Службы при появлении интернета» (the owner 2026-10-23: podkop with a VLESS server under
	 * the operator's whitelist): the chosen tunnels and bypasses restart once the channel that carries the traffic answers */
	servicesCard: function() {
		var self = this;
		this.svcBoxes = {};
		var rows = this.services.map(function(v) {
			var box = E('input', { type: 'checkbox', id: 'mm-svc-' + v.name, change: function() { self.dirty(); } });
			self.svcBoxes[v.name] = box;
			return E('label', { 'class': 'mm-check mm-svc', 'for': 'mm-svc-' + v.name }, [box, ' ' + v.name + (v.installed === false ? ' (' + _('not installed') + ')' : '')]);
		});
		return this.sec('set-svc', _('Services when the internet appears'), [
			rows.length ? E('div', { 'class': 'mm-svc-list' }, rows) : E('p', { 'class': 'mm-faint' }, _('No tunnels or bypasses known to MedveModem are installed (podkop, zapret, passwall, clash, sing-box and others).')),
			/* a DPI bypass on mobile internet (the owner's whitelist guide, 2026-11-08): it does not help under a whitelist and
			 * often breaks the mobile channel; the tunnel does the work there */
			dpiOf(this.services).length ? E('p', { 'class': 'mm-faint mm-svc-dpi' }, _('%s: on mobile internet such a bypass usually works badly, and under the operator\'s whitelist it does not help and may break the tunnel. If the modem is your main channel, stop it or remove it.').format(dpiOf(this.services).join(', '))) : '',
			E('p', { 'class': 'mm-faint' }, _('A tunnel or a bypass starts at the router\'s boot before the modem has connected, finds no internet and waits for its own check, sometimes ten minutes. The checked ones restart when the channel that carries the traffic starts answering: after a boot and after it was without internet for a minute or more; not when a backup comes up. Under the operator\'s whitelist a channel with limited access counts as working: the server of the tunnel on an allowed address answers through it.'))],
			N_(this.svcChosen(this.core).length, '%d service', '%d services').format(this.svcChosen(this.core).length));
	},

	/* the services restarted now: the saved list without «-» (none); never saved, what the engine chose by itself
	 * (ZeroBlock when installed, the owner 2026-11-08) */
	svcChosen: function(c) {
		var names = Object.keys(this.svcBoxes || {});
		var on = c.restart_on_uplink == null ? this.services.filter(function(v) { return v.restart; }).map(function(v) { return v.name; }) :
			list(c.restart_on_uplink).filter(function(n) { return n !== '-'; });
		return names.filter(function(n) { return on.indexOf(n) >= 0; });
	},

	namesCard: function() {
		var self = this, ul = E('ul', { 'class': 'mm-thrs mm-names' });
		this.nameInputs = {};
		var known = {};
		this.devices.forEach(function(d) { if (typeof d.device_id === 'string') known[d.device_id] = d; });
		Object.keys(this.names).forEach(function(s) { var id = self.names[s].device_id; if (id && !known[id]) known[id] = { device_id: id }; });
		var ids = Object.keys(known);
		ids.forEach(function(id) {
			var d = known[id], model = words((d.modemmanager || {}).model) || words(d.model) || words(d.product) || _('USB modem');
			var input = E('input', { type: 'text', 'class': 'mm-input mm-name', maxlength: '32', placeholder: _('No name set'), 'aria-label': _('Name: %s').format(model),
				input: function() { self.dirty(); } });
			self.nameInputs[id] = input;
			ul.appendChild(E('li', { 'class': 'mm-contact' }, [E('div', { 'class': 'mm-field' }, [
				E('span', { 'class': 'mm-dim' }, [model, ' ', E('span', { 'class': 'mm-faint mm-mono' }, id + (FOLLOWS[d.name_follows] ? ' · ' + FOLLOWS[d.name_follows] : '') + (d.present === false ? ' · ' + _('disconnected') : ''))]), input])]));
		});
		return this.sec('set-names', _('Modem names'), [
			ids.length ? ul : E('p', { 'class': 'mm-faint' }, _('No modems found. A name can be set when the router sees the modem.')),
			E('p', { 'class': 'mm-faint' }, _('The name is kept on the router and follows the modem into another USB port: by its serial number, and for modems without their own number (clones with one firmware) by its IMEI, of which only a hash is kept. Until the IMEI is read, such a modem is known by its port. An empty field means the name by the model.'))],
			N_(ids.length, '%d modem', '%d modems').format(ids.length));
	},

	recoveryCard: function() {
		var self = this;
		function check(name, title, hint) {
			var box = E('input', { type: 'checkbox', id: 'mm-' + name, change: function() { self.dirty(); } });
			return [box, E('label', { 'class': 'mm-check', 'for': 'mm-' + name }, [box, ' ' + title]), E('p', { 'class': 'mm-faint' }, hint)];
		}
		/* the owner, 2026-10-17: a new modem connects by itself, its APN by the SIM */
		var ac = check('auto_connect', _('Connect a new modem automatically'),
			_('A modem with no interface yet connects by itself as soon as the managing service sees it; the access point (APN) is picked by the SIM card. Once for each connection of the modem to USB. Modems with an interface set up by hand and QModem\'s modems are not touched.'));
		var a = check('auto_recovery', _('Restore the link automatically'),
			_('Reconnecting the session works for all models; restarting and resetting the modem only for models tested on the bench. The other steps are by hand, on the modem\'s page.'));
		var h = check('host_fixes', _('Bind the modem\'s service ports'),
			_('The router binds a driver to the AT command port itself when the standard driver does not serve that port.'));
		/* the owner, 2026-10-20: a PC with a proxy client in TUN mode could not open 192.168.8.1; the router's address it can */
		var wf = check('web_forward', _('Access to the modems\' web pages through the router'),
			_('Modems in router mode (HiLink, UFI) have their own web page (for example 192.168.8.1). The router opens it at its own address on a separate port as well (the “Open the modem\'s web interface” button leads there): so the page opens from a computer with a VPN or a proxy in TUN mode too. Only from the local network and only for 15 minutes after the button is pressed. The modem\'s page opens over HTTP without encryption, as directly: better not to enter the modem\'s password from a guest network.'));
		/* the owner, 2026-10-30: the table on «Status → Overview» can be switched off; on by default */
		var so = check('status_overview', _('Modems table on the OpenWrt overview page'),
			_('“Status → Overview” shows a table of the modems: which one carries the internet, the operator and signal, the protocol, the address and the state.'));
		var f = check('failover', _('Switch the internet to a working channel'),
			_('If the channel with the default route (wire, Wi-Fi, modem) loses the internet, the router lowers its priority and the traffic goes through another available channel. When the channel answers again, the priority comes back. The network settings do not change. Does not work with mwan3 installed: it manages the routes.'));
		var d = check('failover_dns', _('DNS follows the traffic'),
			_('While a channel has no internet, its DNS servers are taken off the router\'s list, so site names do not go into a channel without a link. When the channel\'s link comes back, the servers come back.'));
		var g = check('proxy_guard', _('Do not restart the modem if a firewall blocks the checks'),
			_('If all direct checks fail but the internet works through a local proxy (clash, mihomo), a firewall or a VPN blocks the checks, not the modem. Then the modem is not restarted.'));
		/* off by default (the owner, 2026-11-03: under the TSPU nearly every operator intercepts DNS, and the warning only
		 * frightened people); the check by hand on the modem's page stays */
		var dc = check('dns_check', _('Check whether the operator intercepts DNS'),
			_('Once per connection the router asks the operator\'s DNS a few test questions and warns when it answers for other servers, gives stub addresses or rewrites «no such name». In Russia this is so on almost every operator, so it is off by default; the check by hand («DNS through this modem» in the site access check on the modem\'s page) works anyway.'));
		var fb = E('select', { 'class': 'mm-input', id: 'mm-failback', change: function() { self.dirty(); } }, [
			E('option', { value: 'restore' }, _('restore the priority at once')),
			E('option', { value: 'demote' }, _('keep it last until restored by hand'))]);
		/* the connections tracked through the channel the traffic left (mwan3's flush_conntrack; field request 2026-10-23) */
		var fl = E('select', { 'class': 'mm-input', id: 'mm-failover_flush', change: function() { self.dirty(); } }, [
			E('option', { value: 'link' }, _('those of the channel the traffic left')),
			E('option', { value: 'all' }, _('all, as mwan3 does')),
			E('option', { value: '0' }, _('none of them'))]);
		var man = E('input', { type: 'text', 'class': 'mm-input mm-mono', id: 'mm-failover_manual', placeholder: _('for example wlan0'), spellcheck: 'false',
			input: function() { self.dirty(); } });
		/* what the engine asks the modems for their radio (the owner, 2026-10-16: a switch with the why) */
		var ci = check('cell_info', _('Cells and location from ModemManager'),
			_('Neighbour cells, the area code (TAC), the distance to the cell. Off by default: on an Orange Pi Zero 3 with a T99W175 the router hung and rebooted 30–90 seconds after these requests (bench, 15 October). Switch it on to test on other hardware and watch whether reboots start. The serving cell and the aggregation are seen without it: Foxconn models read them through the AT port.'));
		var an = check('antennas', _('Levels per antenna'),
			_('The signal level on each antenna and the aggregated carriers, through qmicli, while the modem\'s page is open, at most once in 30 seconds. Not related to the hangs: the router hung without this function too.'));
		/* the cell-lock guard (the owner 2026-10-28): minutes without a network, '0' off */
		var lg = E('select', { 'class': 'mm-input', id: 'mm-radio_lock_guard', change: function() { self.dirty(); } }, [
			E('option', { value: '5' }, _('after 5 minutes')), E('option', { value: '10' }, _('after 10 minutes')),
			E('option', { value: '15' }, _('after 15 minutes')), E('option', { value: '0' }, _('never'))]);
		this.radioParts = { ci: ci, an: an, lg: lg };
		this.boxes = { auto_connect: ac[0], auto_recovery: a[0], host_fixes: h[0], web_forward: wf[0], status_overview: so[0], failover: f[0], failover_dns: d[0], proxy_guard: g[0], dns_check: dc[0], cell_info: ci[0], antennas: an[0] };
		this.selects = { failback: fb, failover_flush: fl, radio_lock_guard: lg };
		this.areas = Object.assign(this.areas || {}, { failover_manual: man });
		this.foParts = { f: f, d: d, fb: fb, fl: fl, man: man };
		return this.sec('set-rec', _('Modem maintenance'), [
			this.group(_('Connecting and recovery')), ac[1], ac[2], a[1], a[2], h[1], h[2],
			this.group(_('Checks')), g[1], g[2], dc[1], dc[2],
			this.group(_('Web pages and overview')), wf[1], wf[2], so[1], so[2]]);
	},

	/* the router's copy of the SMS when the modem's storage fills (r97; the engine's core.sms_archive, unset means on) */
	archiveCard: function() {
		var self = this, box = E('input', { type: 'checkbox', id: 'mm-sms_archive', change: function() { self.dirty(); } });
		this.boxes = this.boxes || {};
		this.boxes.sms_archive = box;
		/* «Хранить все SMS в роутере» (the owner, 2026-11-08): every received message copied at once, the modem keeps its own;
		 * core.sms_keep_all, unset means off */
		var all = E('input', { type: 'checkbox', id: 'mm-sms_keep_all', change: function() { self.dirty(); } });
		this.boxes.sms_keep_all = all;
		return this.sec('set-sms-archive', _('SMS in the router'), [
			E('label', { 'class': 'mm-check', 'for': 'mm-sms_keep_all' }, [all, ' ' + _('Keep all SMS in the router')]),
			E('p', { 'class': 'mm-faint' }, _('Every incoming message is copied to the router\'s flash as soon as MedveModem reads it; nothing is deleted from the modem. While the modem keeps a message, the list shows it once; when the modem loses it (some lose their messages at a restart), the router\'s copy stays. A message deleted in the SMS tab is gone from the router too. The router keeps the last 500 per modem.')),
			E('label', { 'class': 'mm-check', 'for': 'mm-sms_archive' }, [box, ' ' + _('Move the oldest SMS to the router when the modem\'s storage fills up')]),
			E('p', { 'class': 'mm-faint' }, _('When the SIM card or the modem is three quarters full, the oldest incoming messages that have been on the modem for at least ten minutes are written to the router\'s flash and only then deleted from the modem, down to half its storage. New messages keep coming. The router keeps the last 500 per modem; a message deleted in the SMS tab is gone from the router too. Off: the modem keeps its messages, and a full storage stops new ones.'))
		]);
	},

	/* «Пересылка SMS» (the owner, 2026-10-19): each new received message to ntfy or a webhook, and
	 * (spec §43, the owner 2026-11-03) by SMS to up to 5 numbers through another modem; from all modems or the ticked ones.
	 * Saved by its own button; the token goes to its own 0600 file through rpcd and is never read back */
	forwardCard: function() {
		var self = this, f = this.fw = {};
		f.on = E('input', { type: 'checkbox', id: 'mm-fw-on' });
		f.all = E('select', { 'class': 'mm-select', id: 'mm-fw-all', change: function() { f.mods.hidden = f.all.value === '1'; } }, [
			E('option', { value: '1' }, _('from all modems')), E('option', { value: '0' }, _('from the modems ticked below'))]);
		f.mods = E('ul', { 'class': 'mm-thrs mm-fw-mods' });
		f.web = E('input', { type: 'checkbox', id: 'mm-fw-web', change: function() { f.webBox.hidden = !f.web.checked; } });
		f.kind = E('select', { 'class': 'mm-select', id: 'mm-fw-kind' }, [E('option', { value: 'ntfy' }, 'ntfy'), E('option', { value: 'webhook' }, 'Webhook (JSON)')]);
		f.url = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-fw-url', spellcheck: 'false', autocomplete: 'off', placeholder: _('https://ntfy.sh/your-topic') });
		f.token = E('input', { type: 'password', 'class': 'mm-input', id: 'mm-fw-token', autocomplete: 'new-password', placeholder: _('Not set') });
		f.sms = E('input', { type: 'checkbox', id: 'mm-fw-sms', change: function() { f.smsBox.hidden = !f.sms.checked; } });
		f.book = E('ul', { 'class': 'mm-thrs mm-fw-book' });
		f.numbers = E('input', { type: 'text', 'class': 'mm-input mm-mono', id: 'mm-fw-numbers', spellcheck: 'false', autocomplete: 'off', placeholder: _('+79001234567 +79007654321') });
		f.modem = E('select', { 'class': 'mm-select', id: 'mm-fw-modem' });
		f.parts = E('select', { 'class': 'mm-select', id: 'mm-fw-parts' }, [E('option', { value: '1' }, _('one SMS (70 characters)')),
			E('option', { value: '2' }, _('up to two SMS')), E('option', { value: '3' }, _('up to three SMS'))]);
		f.rate = E('input', { type: 'number', 'class': 'mm-input mm-num', id: 'mm-fw-rate', min: '1', max: '60', step: '1' });
		f.out = E('p', { 'class': 'mm-faint mm-fw-out', role: 'status' });
		f.save = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-fw-save', click: mm.busy(function() { return self.forwardSave(); }) }, _('Save'));
		f.test = E('button', { type: 'button', 'class': 'mm-btn mm-fw-test', click: mm.busy(function() { return self.forwardTest(); }) }, _('Send a test'));
		f.drop = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-fw-drop', hidden: '', click: mm.busy(function() { return self.forwardSave(true); }) }, _('Delete the token'));
		var field = function(id, label, node) { return E('label', { 'class': 'mm-field', 'for': id }, [E('span', { 'class': 'mm-dim' }, label), node]); };
		f.webBox = E('div', { 'class': 'mm-fw-webbox' }, [field('mm-fw-kind', _('Service'), f.kind), field('mm-fw-url', _('Address'), f.url),
			field('mm-fw-token', _('Token (optional)'), f.token),
			E('p', { 'class': 'mm-faint' }, _('ntfy gets the text with the sender\'s number, a webhook gets JSON {"modem", "number", "text", "time"} by POST. The server\'s certificate is verified. The token is sent as Bearer and needs curl.'))]);
		f.smsBox = E('div', { 'class': 'mm-fw-smsbox' }, [E('p', { 'class': 'mm-dim' }, _('Recipients: up to 5 together')), f.book,
			field('mm-fw-numbers', _('Other numbers, separated by spaces'), f.numbers),
			field('mm-fw-modem', _('Send through'), f.modem), field('mm-fw-parts', _('Length'), f.parts), field('mm-fw-rate', _('At most SMS an hour'), f.rate),
			E('p', { 'class': 'mm-faint' }, _('The SMS goes through another modem than the one it came to (through that one only when no other can send). It starts with ↪, the modem\'s name and the sender; a longer text is cut. SMS cost money.'))]);
		var card = this.sec('set-forward', _('SMS forwarding'), [
			E('label', { 'class': 'mm-check', 'for': 'mm-fw-on' }, [f.on, ' ' + _('Forward new incoming SMS')]),
			this.group(_('From where')),
			field('mm-fw-all', _('Which SMS'), f.all), f.mods,
			E('p', { 'class': 'mm-faint' }, _('The ticked modems apply to the Telegram bot\'s forwarding too, while this forwarding is on.')),
			this.group(_('Where to')),
			E('label', { 'class': 'mm-check', 'for': 'mm-fw-web' }, [f.web, ' ' + _('To ntfy or a webhook')]), f.webBox,
			E('label', { 'class': 'mm-check', 'for': 'mm-fw-sms' }, [f.sms, ' ' + _('By SMS to numbers')]), f.smsBox,
			E('div', { 'class': 'mm-actions' }, [f.save, f.test, f.drop]), f.out,
			E('p', { 'class': 'mm-faint' }, _('The number and the text go only where chosen here and are not written to the log. An SMS forwarded by SMS is not forwarded again.'))]);
		card.id = 'forward';
		this.fwGist = card.querySelector('.mm-sec-gist');
		this.forwardLoad();
		return card;
	},

	/* «SMS-оповещения» (spec 38.6; the owner, 2026-10-29): the engine's alerts by SMS to numbers from the address book, which
	 * all modems share; whitelists, the internet gone, the traffic moved, each with its hysteresis, and their end. Saved by
	 * its own button, as the forwarding is */
	alertsCard: function() {
		var self = this, a = this.al = {};
		function box(id, label) {
			var b = E('input', { type: 'checkbox', id: 'mm-al-' + id });
			a[id] = b;
			return E('label', { 'class': 'mm-check', 'for': 'mm-al-' + id }, [b, ' ' + label]);
		}
		function minutes(id, label) {
			var i = E('input', { type: 'number', 'class': 'mm-input mm-num', id: 'mm-al-' + id, min: '1', max: '1440', step: '1' });
			a[id] = i;
			return E('label', { 'class': 'mm-field', 'for': 'mm-al-' + id }, [E('span', { 'class': 'mm-dim' }, label), i]);
		}
		a.book = E('ul', { 'class': 'mm-thrs mm-al-book' });
		a.modem = E('select', { 'class': 'mm-select', id: 'mm-al-modem' });
		a.rate = E('input', { type: 'number', 'class': 'mm-input mm-num', id: 'mm-al-rate', min: '1', max: '60', step: '1' });
		a.out = E('p', { 'class': 'mm-faint mm-al-out', role: 'status' });
		a.state = E('p', { 'class': 'mm-faint mm-al-state', role: 'status' });
		a.save = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-al-save', click: mm.busy(function() { return self.alertsSave(); }) }, _('Save'));
		a.test = E('button', { type: 'button', 'class': 'mm-btn mm-al-test', click: mm.busy(function() { return self.alertsTest(); }) }, _('Send a test SMS'));
		var card = this.sec('set-alerts', _('SMS alerts'), [
			box('on', _('Send SMS alerts')),
			this.group(_('Recipients')), a.book,
			E('p', { 'class': 'mm-faint' }, _('The numbers come from the address book, one for all modems: add them on any modem\'s SMS tab.')),
			this.group(_('Events')),
			(a.wlRows = [box('whitelist', _('Only the operator\'s whitelist answers')), minutes('whitelist_min', _('Whitelist for, min'))])[0], a.wlRows[1],
			box('down', _('No internet through any channel')), minutes('down_min', _('No internet for, min')),
			box('switch', _('The traffic moved to another modem, tunnel or adapter')), minutes('switch_min', _('On the other channel for, min')),
			box('restore', _('When it is over: the whitelist lifted, the internet back, the traffic back')),
			minutes('clear_min', _('Works again for, min')),
			this.group(_('Sending')),
			E('label', { 'class': 'mm-field', 'for': 'mm-al-modem' }, [E('span', { 'class': 'mm-dim' }, _('Send through')), a.modem]),
			E('label', { 'class': 'mm-field', 'for': 'mm-al-rate' }, [E('span', { 'class': 'mm-dim' }, _('At most SMS an hour')), a.rate]),
			E('div', { 'class': 'mm-actions' }, [a.save, a.test]), a.out, a.state,
			E('p', { 'class': 'mm-faint' }, _('An SMS goes over the radio, so it gets out while the internet is gone, and through a SIM with the whitelist only. A short whitelist or a blip is not sent: each event waits its minutes, and its end waits until it works that long. If the chosen modem cannot send, the next one does. SMS cost money: Cyrillic is 70 characters an SMS, an alert takes one or two. The numbers are not written to the log.'))]);
		card.id = 'alerts';
		this.alGist = card.querySelector('.mm-sec-gist');
		this.alertsLoad();
		return card;
	},

	alertsLoad: function() {
		var self = this, a = this.al;
		return Promise.all([uciGet('medvemodem', 'alerts'), uciGet('medvemodem', 'contact'), getAlerts().catch(function() { return {}; })]).then(function(rr) {
			var v = ((rr[0] && rr[0].values) || {}).alerts || {}, book = (rr[1] && rr[1].values) || {}, st = rr[2] || {};
			var chosen = list(v.contact).concat(typeof v.contact === 'string' ? [v.contact] : []);
			a.exists = !!v['.name'];
			a.on.checked = v.enabled === '1';
			['whitelist', 'down', 'switch', 'restore'].forEach(function(k) { a[k].checked = v[k] !== '0'; });
			a.whitelist_min.value = v.whitelist_min || '15';
			a.down_min.value = v.down_min || '5';
			a.switch_min.value = v.switch_min || '2';
			a.clear_min.value = v.clear_min || '3';
			a.rate.value = v.max_per_hour || '6';
			a.picks = {};
			var names = Object.keys(book).filter(function(k) { return /^\+?\d{3,20}$/.test(words(book[k].number)); });
			a.book.replaceChildren.apply(a.book, names.length ? names.map(function(k) {
				var c = E('input', { type: 'checkbox', id: 'mm-al-c-' + k });
				c.checked = chosen.indexOf(k) >= 0;
				a.picks[k] = c;
				return E('li', { 'class': 'mm-contact' }, [E('label', { 'class': 'mm-check', 'for': 'mm-al-c-' + k }, [c, ' ',
					E('b', {}, words(book[k].name) || '—'), ' ', E('span', { 'class': 'mm-mono mm-faint' }, words(book[k].number))])]);
			}) : [E('li', { 'class': 'mm-faint' }, _('The address book is empty.'))]);
			var opts = [E('option', { value: '' }, _('Any modem that can'))];
			self.devices.forEach(function(d) {
				if (typeof d.device_id !== 'string') return;
				var n = words(d.name) || words((d.modemmanager || {}).model) || words(d.model) || words(d.product) || _('USB modem');
				opts.push(E('option', { value: d.device_id }, n));
			});
			a.modem.replaceChildren.apply(a.modem, opts);
			a.modem.value = v.modem && self.devices.some(function(d) { return d.device_id === v.modem; }) ? v.modem : '';
			var who = list(st.senders).map(function(s) { return words(s.name) || words(s.device_id); }).filter(Boolean);
			var last = list(st.recent).slice(-1)[0];
			text(a.state, !st.enabled ? '' : (who.length ? _('Sends through: %s.').format(who.join(', ')) : _('No modem can send SMS now: the alerts wait for one.')) +
				(last ? ' ' + _('Last: %s, %s.').format(new Date(last.at * 1000).toLocaleString(mm.LANG), last.ok ? _('sent') : _('not sent')) : ''));
			var nr = chosen.filter(function(k) { return a.picks[k]; }).length;
			if (self.alGist) text(self.alGist, a.on.checked ? N_(nr, 'active · %d recipient', 'active · %d recipients').format(nr) : _('alerts are off'));
		}, function() {});
	},

	alertsSave: function() {
		var self = this, a = this.al, picks = Object.keys(a.picks || {}).filter(function(k) { return a.picks[k].checked; });
		function n(i, lo, hi, dflt) { var v = parseInt(i.value, 10); return String(v >= lo && v <= hi ? v : dflt); }
		if (a.on.checked && !picks.length) { text(a.out, _('Choose at least one recipient.')); return Promise.resolve(); }
		/* the SMS are worded in the language of the page that set them up */
		var values = { enabled: a.on.checked ? '1' : '0', whitelist: a.whitelist.checked ? '1' : '0', down: a.down.checked ? '1' : '0',
			'switch': a['switch'].checked ? '1' : '0', restore: a.restore.checked ? '1' : '0',
			whitelist_min: n(a.whitelist_min, 1, 1440, 15), down_min: n(a.down_min, 1, 1440, 5), switch_min: n(a.switch_min, 1, 1440, 2),
			clear_min: n(a.clear_min, 1, 1440, 3), max_per_hour: n(a.rate, 1, 60, 6), lang: mm.langCode() };
		var ops = (a.exists ? uciSet('medvemodem', 'alerts', values) : uciAdd('medvemodem', 'alerts', 'alerts', values)).then(function() {
			/* the lists start again from the boxes; options not there yet are no error */
			return Promise.all(['contact', 'modem'].map(function(k) { return uciDelete('medvemodem', 'alerts', [k]).catch(function() {}); }));
		}).then(function() {
			var more = {};
			if (picks.length) more.contact = picks;
			if (a.modem.value) more.modem = a.modem.value;
			return Object.keys(more).length ? uciSet('medvemodem', 'alerts', more) : null;
		});
		return ops.then(function() { return uciCommit('medvemodem'); }).then(function() {
			text(a.out, _('Saved.'));
			return self.alertsLoad();
		}, function(e) { text(a.out, _('Not saved: %s.').format(failText(e))); });
	},

	alertsTest: function() {
		var self = this, a = this.al;
		text(a.out, _('Sending a test…'));
		return getAlerts(true).then(function(r) {
			var W = { 'no recipients: choose them from the address book': _('no recipients are saved: choose them and save'),
				'no modem to send through': _('no modem can send SMS now') };
			text(a.out, r && r.test === 'queued' ? _('A test SMS is on its way: it comes in a minute or two.') :
				_('Not sent: %s.').format(W[(r || {}).refused] || (r || {}).refused || _('no answer')));
			return self.alertsLoad();
		}, function(e) { text(a.out, _('Not sent: %s.').format(failText(e))); });
	},

	forwardLoad: function() {
		var self = this, f = this.fw;
		return Promise.all([uciGet('medvemodem', 'sms_forward'), fwStatus().catch(function() { return {}; }), uciGet('medvemodem', 'contact')]).then(function(rr) {
			var r = rr[0], v = ((r && r.values) || {}).sms_forward || {}, book = (rr[2] && rr[2].values) || {};
			var lst = function(x) { return Array.isArray(x) ? x : typeof x === 'string' && x ? [x] : []; };
			f.exists = !!v['.name'];
			f.on.checked = v.enabled === '1';
			f.all.value = v.modems_all === '0' ? '0' : '1';
			f.mods.hidden = f.all.value === '1';
			/* an SMS set up before §43 went to ntfy or the webhook: «to a service» stays on unless switched off */
			f.web.checked = v.web !== '0';
			f.webBox.hidden = !f.web.checked;
			f.sms.checked = v.sms === '1';
			f.smsBox.hidden = !f.sms.checked;
			f.kind.value = v.kind === 'webhook' ? 'webhook' : 'ntfy';
			f.url.value = v.url || '';
			f.hasToken = !!(rr[1] || {}).token;
			f.token.value = '';
			f.token.placeholder = f.hasToken ? _('Saved; enter a new one to replace it') : _('Not set');
			f.drop.hidden = !f.hasToken;
			f.parts.value = ['1', '2', '3'].indexOf(v.sms_parts) >= 0 ? v.sms_parts : '3';
			f.rate.value = v.sms_max_per_hour || '10';
			f.numbers.value = lst(v.sms_number).join(' ');
			/* the modems: those known now, and a ticked one that is away (kept, said absent) */
			var ticked = lst(v.modem), seen = {};
			f.modPicks = {};
			var rows = self.devices.filter(function(d) { return typeof d.device_id === 'string'; }).map(function(d) {
				seen[d.device_id] = true;
				return [d.device_id, words(d.name) || words((d.modemmanager || {}).model) || words(d.model) || words(d.product) || _('USB modem')];
			}).concat(ticked.filter(function(id) { return !seen[id]; }).map(function(id) { return [id, _('%s (not connected)').format(id)]; }));
			f.mods.replaceChildren.apply(f.mods, rows.length ? rows.map(function(m, i) {
				var c = E('input', { type: 'checkbox', id: 'mm-fw-m-' + i });
				c.checked = ticked.indexOf(m[0]) >= 0;
				f.modPicks[m[0]] = c;
				return E('li', {}, E('label', { 'class': 'mm-check', 'for': 'mm-fw-m-' + i }, [c, ' ' + m[1]]));
			}) : [E('li', { 'class': 'mm-faint' }, _('No modems.'))]);
			var chosen = lst(v.sms_contact);
			f.picks = {};
			var names = Object.keys(book).filter(function(k) { return /^\+?\d{3,20}$/.test(words(book[k].number)); });
			f.book.replaceChildren.apply(f.book, names.length ? names.map(function(k) {
				var c = E('input', { type: 'checkbox', id: 'mm-fw-c-' + k });
				c.checked = chosen.indexOf(k) >= 0;
				f.picks[k] = c;
				return E('li', { 'class': 'mm-contact' }, [E('label', { 'class': 'mm-check', 'for': 'mm-fw-c-' + k }, [c, ' ',
					E('b', {}, words(book[k].name) || '—'), ' ', E('span', { 'class': 'mm-mono mm-faint' }, words(book[k].number))])]);
			}) : [E('li', { 'class': 'mm-faint' }, _('The address book is empty: add numbers on any modem\'s SMS tab or type them below.'))]);
			var opts = [E('option', { value: '' }, _('Any modem that can'))];
			self.devices.forEach(function(d) {
				if (typeof d.device_id !== 'string') return;
				opts.push(E('option', { value: d.device_id }, words(d.name) || words((d.modemmanager || {}).model) || words(d.model) || words(d.product) || _('USB modem')));
			});
			f.modem.replaceChildren.apply(f.modem, opts);
			f.modem.value = v.sms_modem && self.devices.some(function(d) { return d.device_id === v.sms_modem; }) ? v.sms_modem : '';
			var ways = [f.web.checked ? (f.kind.value === 'ntfy' ? 'ntfy' : 'webhook') : '', f.sms.checked ? 'SMS' : ''].filter(Boolean).join(', ');
			if (self.fwGist) text(self.fwGist, f.on.checked && ways ? _('active · %s').format(ways) : _('inactive'));
		}, function() {});
	},

	forwardSave: function(dropToken) {
		var self = this, f = this.fw, url = f.url.value.trim();
		var picks = Object.keys(f.picks || {}).filter(function(k) { return f.picks[k].checked; });
		var mods = Object.keys(f.modPicks || {}).filter(function(k) { return f.modPicks[k].checked; });
		var nums = f.numbers.value.trim() ? f.numbers.value.trim().split(/[\s,;]+/) : [];
		if (f.on.checked && f.web.checked && !/^https?:\/\/[^\s"\\]+$/.test(url)) { text(f.out, _('Address: https://… without spaces and quotes.')); return Promise.resolve(); }
		if (/[\s"\\]/.test(f.token.value)) { text(f.out, _('A token without spaces and quotes.')); return Promise.resolve(); }
		var bad = nums.filter(function(n) { return !/^\+?\d{3,20}$/.test(n); });
		if (bad.length) { text(f.out, _('Not a phone number: %s.').format(bad.join(', '))); return Promise.resolve(); }
		if (f.on.checked && f.sms.checked && !(picks.length + nums.length)) { text(f.out, _('Choose at least one recipient.')); return Promise.resolve(); }
		if (picks.length + nums.length > 5) { text(f.out, _('At most 5 recipients together.')); return Promise.resolve(); }
		if (f.on.checked && f.all.value === '0' && !mods.length) { text(f.out, _('Tick at least one modem, or choose «from all modems».')); return Promise.resolve(); }
		if (f.on.checked && !f.web.checked && !f.sms.checked) { text(f.out, _('Choose where to: a service, SMS or both.')); return Promise.resolve(); }
		function n(i, lo, hi, dflt) { var v = parseInt(i.value, 10); return String(v >= lo && v <= hi ? v : dflt); }
		/* the forwarded SMS is headed in the language of the page that set it up */
		var values = { enabled: f.on.checked ? '1' : '0', kind: f.kind.value, url: url, lang: mm.langCode(), web: f.web.checked ? '1' : '0',
			sms: f.sms.checked ? '1' : '0', modems_all: f.all.value, sms_parts: f.parts.value, sms_max_per_hour: n(f.rate, 1, 60, 10) }, token = f.token.value;
		return (f.exists ? uciSet('medvemodem', 'sms_forward', values) : uciAdd('medvemodem', 'sms_forward', 'sms_forward', values)).then(function() {
			/* the lists start again from the boxes; options not there yet are no error */
			return Promise.all(['modem', 'sms_contact', 'sms_number', 'sms_modem'].map(function(k) { return uciDelete('medvemodem', 'sms_forward', [k]).catch(function() {}); }));
		}).then(function() {
			var more = {};
			if (mods.length) more.modem = mods;
			if (picks.length) more.sms_contact = picks;
			if (nums.length) more.sms_number = nums;
			if (f.modem.value) more.sms_modem = f.modem.value;
			return Object.keys(more).length ? uciSet('medvemodem', 'sms_forward', more) : null;
		}).then(function() {
			return uciCommit('medvemodem');
		}).then(function() {
			return dropToken ? fwTokenClear() : token ? fwTokenSet(token).then(function(r) {
				if (!r || !r.ok) throw new Error(_('the token is not saved'));
			}) : null;
		}).then(function() {
			text(f.out, dropToken ? _('The token is deleted.') : _('Saved.'));
			return self.forwardLoad();
		}, function(e) { text(f.out, _('Not saved: %s.').format(failText(e))); });
	},

	forwardTest: function() {
		var f = this.fw, said = [];
		text(f.out, _('Sending a test…'));
		var web = !f.web.checked ? Promise.resolve() : fs.exec(FORWARD, ['test']).then(parse, function(e) { return { ok: false, error: failText(e) }; }).then(function(j) {
			var W = { 'forwarding is off': _('forwarding is off: save it switched on'), 'the kind or the address is not set': _('no address is set'),
				'a token needs curl, which is not installed': _('a token needs curl, it is not installed') };
			said.push(j.ok ? _('A test message is sent.') : _('Not sent: %s.').format(W[j.error] || j.error || _('no answer')));
		});
		/* by SMS: the saved recipients, through the modems that can send */
		var sms = !f.sms.checked ? Promise.resolve() : forwardSmsTest(true, true).then(function(r) {
			var W = { 'no recipients: choose them from the address book': _('no recipients are saved: choose them and save'),
				'no modem to send through': _('no modem can send SMS now') };
			said.push(r && r.test === 'queued' ? _('A test SMS is on its way: it comes in a minute or two.') :
				_('Not sent: %s.').format(W[(r || {}).refused] || (r || {}).refused || _('no answer')));
		}, function(e) { said.push(_('Not sent: %s.').format(failText(e))); });
		return Promise.all([web, sms]).then(function() { text(f.out, said.join(' ') || _('Choose where to: a service, SMS or both.')); });
	},

	radioCard: function() {
		var p = this.radioParts;
		return this.sec('set-radio', _('Detailed network data'), [this.group(_('What to read from the modems')), p.an[1], p.an[2], p.ci[1], p.ci[2],
			this.group(_('Cell and band lock')),
			E('label', { 'class': 'mm-field', 'for': 'mm-radio_lock_guard' }, [E('span', { 'class': 'mm-dim' }, _('Take a cell or band lock back when the modem has no network')), p.lg]),
			E('p', { 'class': 'mm-dim' }, _('A modem locked to one cell or to a few bands on its page loses the network when that cell or band goes away (a sector switched off, the operator\'s works, another place). After this long without a registration MedveModem writes back the settings the modem had before the lock, not «everything automatic». Only a lock set from MedveModem; a paused modem is left alone.'))], '', false);
	},

	/* failover: built with the recovery card's controls (one set of boxes for the save) */
	failoverCard: function() {
		var p = this.foParts;
		this.foFine = [
			E('label', { 'class': 'mm-field', 'for': 'mm-failback' }, [E('span', { 'class': 'mm-dim' }, _('A channel that answers again')), p.fb]),
			E('p', { 'class': 'mm-faint' }, _('“Keep it last” is for an unreliable channel: the traffic does not come back to it until “Restore the priority” is pressed on the main page.')),
			E('label', { 'class': 'mm-field', 'for': 'mm-failover_manual' }, [E('span', { 'class': 'mm-dim' }, _('Manual-only channels')), p.man]),
			E('p', { 'class': 'mm-faint' }, _('Network devices separated by spaces. Such a channel gets no traffic by itself even when it works, only if it is first in the network settings. For example, sharing from a smartphone.')),
			E('label', { 'class': 'mm-field', 'for': 'mm-failover_flush' }, [E('span', { 'class': 'mm-dim' }, _('Connections to drop on a switch')), p.fl]),
			E('p', { 'class': 'mm-faint' }, _('After a switch the open connections keep the old channel\'s address and hang until their timeouts; dropped, they start anew through the new channel at once. Only those of the channel the traffic left need the conntrack package; without it the whole table is dropped.')),
			E('p', { 'class': 'mm-faint mm-hooks-note' }, _('Your own scripts: put them in /etc/hotplug.d/medvemodem/. They run on a switch (ACTION=switch, DEVICE, PREVIOUS), a failover move (ACTION=failover), a modem\'s session up or down (ACTION=session, DEVICE_ID, STATE, INTERFACE), a recovery step, a modem\'s arrival or departure, and when the internet works again (ACTION=ready, DEVICE, LEVEL).'))];
		return this.sec('set-fo', _('Backup channel'), [this.group(_('Switching')), p.f[1], p.f[2], p.d[1], p.d[2],
			this.group(_('Fine tuning'))].concat(this.foFine.slice(0, -1), [this.group(_('Your scripts')), this.foFine[this.foFine.length - 1]]));
	},

	/*
	 * The whitelist mode (spec §44; the owner, 2026-11-03): under an operator's whitelist a server of the admin's pool is
	 * pinned in a podkop-exp / forkop 2 section, and, if asked, the whole LAN and the router go through it. Its own save:
	 * the manager's settings are changed only while the mode is on, and given back after it.
	 */
	wlCard: function() {
		var self = this, w = this.wl = {};
		function box(id, label) {
			var b = E('input', { type: 'checkbox', id: 'mm-wl-' + id });
			w[id] = b;
			return E('label', { 'class': 'mm-check', 'for': 'mm-wl-' + id }, [b, ' ' + label]);
		}
		w.state = E('p', { 'class': 'mm-wl-state', role: 'status' });
		/* what the manager's group uses now, the mode on or off (the owner, 2026-11-06: «показывать статус включенности
		 * обхода и какая выбрана прокси (автоматически или как)») */
		w.live = E('p', { 'class': 'mm-faint mm-wl-live', hidden: true });
		w.srcNote = E('p', { 'class': 'mm-faint mm-wl-src', hidden: true });
		w.found = E('p', { 'class': 'mm-faint mm-wl-found' }, _('Looking for podkop and forkop…'));
		w.mgr = E('select', { 'class': 'mm-input', id: 'mm-wl-mgr', change: function() { self.wlSections(); } });
		w.sec = E('select', { 'class': 'mm-input', id: 'mm-wl-sec', change: function() { w.list.replaceChildren(); w.boxes = []; w.live.hidden = w.srcNote.hidden = true; } });
		w.mode = E('select', { 'class': 'mm-input', id: 'mm-wl-mode' }, [
			E('option', { value: 'pool' }, _('the first that answers, in this order')), E('option', { value: 'one' }, _('only the first ticked'))]);
		w.pattern = E('input', { type: 'text', 'class': 'mm-input mm-mono', id: 'mm-wl-pattern', placeholder: _('for example WL|whitelist'), spellcheck: 'false',
			input: function() { self.wlTick(); } });
		w.list = E('div', { 'class': 'mm-wl-list' });
		w.out = E('p', { 'class': 'mm-faint mm-wl-out', role: 'status' });
		var show = E('button', { type: 'button', 'class': 'mm-btn mm-wl-show', click: mm.busy(function() { return self.wlServers(); }) }, _('Show the section\'s servers'));
		/* spec §44.7: the community's list of what stays open under the whitelists, asked on a press */
		var hint = E('button', { type: 'button', 'class': 'mm-btn mm-wl-hint', click: mm.busy(function() { return self.wlHints(); }) },
			_('Suggest by the community lists'));
		var save = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-wl-save', click: mm.busy(function() { return self.wlSave(); }) }, _('Save the whitelist mode'));
		w.onBtn = E('button', { type: 'button', 'class': 'mm-btn mm-wl-on', click: mm.busy(function() { return self.wlAct('on'); }) }, _('Turn on now'));
		w.offBtn = E('button', { type: 'button', 'class': 'mm-btn mm-wl-off', click: mm.busy(function() { return self.wlAct('off'); }) }, _('Turn off now'));
		w.form = E('div', { 'class': 'mm-wl-form', hidden: true }, [
			this.group(_('Switching on')),
			box('enabled', _('Use the whitelist mode')),
			box('auto', _('Turn on by itself when the operator\'s whitelist starts, off when it ends')),
			this.group(_('The tunnel\'s servers')),
			E('label', { 'class': 'mm-field', 'for': 'mm-wl-mgr' }, [E('span', { 'class': 'mm-dim' }, _('Manager')), w.mgr]),
			E('label', { 'class': 'mm-field', 'for': 'mm-wl-sec' }, [E('span', { 'class': 'mm-dim' }, _('Section')), w.sec]),
			E('div', { 'class': 'mm-actions' }, [show, hint]),
			E('label', { 'class': 'mm-field', 'for': 'mm-wl-pattern' }, [E('span', { 'class': 'mm-dim' }, _('Tick the servers whose names match')), w.pattern]),
			w.list, w.srcNote,
			E('label', { 'class': 'mm-field', 'for': 'mm-wl-mode' }, [E('span', { 'class': 'mm-dim' }, _('Which server')), w.mode]),
			this.group(_('What goes through the tunnel')),
			box('lan', _('The whole LAN through the tunnel')),
			box('router', _('The router itself through the tunnel (apk, opkg, curl)')),
			w.warn = E('p', { 'class': 'mm-callout is-err mm-wl-warn', hidden: true },
				_('MedveModem will change the settings of the manager (fully_routed_ips of this section) and reload it while the whitelist lasts; your edits of this section meanwhile may be overwritten. The original is kept and put back when the mode ends.')),
			E('div', { 'class': 'mm-actions' }, [save, w.onBtn, w.offBtn]), w.out]);
		[w.lan, w.router].forEach(function(b) { b.addEventListener('change', function() { w.warn.hidden = !(w.lan.checked || w.router.checked); }); });
		var node = this.sec('set-wl', _('Whitelist mode'), [
			E('p', { 'class': 'mm-faint' }, _('When the operator lets only its whitelist through, a server of the tunnel that survives it is chosen in podkop 0.8 or forkop 2, and the traffic can go through it whole.')),
			w.state, w.live, w.found, w.form]);
		/* the managers are asked when the card is opened, not with every page */
		node.mmOnOpen = function() { if (!w.asked) { w.asked = true; self.wlLoad(); } };
		if (node.mmIsOpen()) node.mmOnOpen();
		return node;
	},

	wlLoad: function() {
		var self = this, w = this.wl;
		return Promise.all([fs.exec(WLMODE, ['detect']).catch(function() { return {}; }), uciGet('medvemodem', 'wlmode').catch(function() { return {}; }),
			wlmodeCall('status').catch(function() { return {}; })]).then(function(rr) {
			var d = {};
			try { d = JSON.parse((rr[0] && rr[0].stdout) || '{}'); } catch (e) {}
			var v = ((rr[1] && rr[1].values) || {}).wlmode || {};
			w.exists = !!v['.name'];
			w.conf = v;
			w.managers = list(d.managers);
			var ok = w.managers.filter(function(m) { return m.switchable; });
			var other = w.managers.filter(function(m) { return !m.switchable; }).map(function(m) {
				return m.kind === 'forkop1' ? _('forkop 1.x') : m.kind === 'podkop-classic' ? _('podkop (classic, before 0.8)') : m.name; });
			text(w.found, ok.length ? _('Found: %s.').format(ok.map(function(m) { return m.kind === 'forkop2' ? 'forkop 2' : 'podkop 0.8'; }).join(', ')) :
				other.length ? _('Found %s: it cannot switch its servers while it runs. The mode needs podkop 0.8 (podkop-exp) or forkop 2.').format(other.join(', ')) :
				_('No podkop or forkop on the router: the mode needs podkop 0.8 (podkop-exp) or forkop 2.'));
			w.form.hidden = !ok.length;
			w.mgr.replaceChildren.apply(w.mgr, ok.map(function(m) { return E('option', { value: m.name }, m.kind === 'forkop2' ? 'forkop 2' : 'podkop 0.8'); }));
			if (v.manager) w.mgr.value = v.manager;
			self.wlSections();
			if (v.section) w.sec.value = v.section;
			w.enabled.checked = v.enabled === '1';
			w.auto.checked = v.auto !== '0';
			w.lan.checked = v.lan === '1';
			w.router.checked = v.router === '1';
			w.warn.hidden = !(w.lan.checked || w.router.checked);
			w.mode.value = v.mode === 'one' ? 'one' : 'pool';
			w.pattern.value = v.pattern || '';
			w.chosen = list(v.server);
			if (w.chosen.length) self.wlList(w.chosen, w.chosen);
			self.wlState((rr[2] || {}).wlmode || {});
			if (ok.length && w.sec.value) return self.wlServers(true);
		});
	},

	wlSections: function() {
		var w = this.wl, m = (w.managers || []).filter(function(x) { return x.name === w.mgr.value; })[0] || {};
		w.sec.replaceChildren.apply(w.sec, list(m.sections).map(function(s) { return E('option', { value: s }, s); }));
	},

	wlState: function(st) {
		var w = this.wl;
		text(w.state, st.on ? _('On since %s: server “%s”%s.').format(st.since ? new Date(st.since * 1000).toLocaleTimeString(mm.LANG) : '—', st.label || st.server || '—',
			typeof st.delay_ms === 'number' ? ', ' + _('%d ms').format(st.delay_ms) : '') + (st.manual ? ' ' + _('Turned on by hand.') : '') :
			st.enabled ? _('Off: waits for the operator\'s whitelist.') : '');
		if (st.error) text(w.out, _('The last attempt failed: %s.').format(st.error));
		w.offBtn.hidden = !st.on;
		w.onBtn.hidden = !!st.on || !st.enabled;
	},

	wlList: function(tags, chosen, names) {
		var self = this, w = this.wl, groups = [], by = {};
		w.boxes = tags.map(function(n, i) {
			var b = E('input', { type: 'checkbox', value: n }), shown = (names && names[i]) || (w.names || {})[n] || n;
			b.checked = chosen.indexOf(n) >= 0;
			/* «Akenai_Priests: Германия»: a head per source, the server's own name under it */
			var cut = shown.indexOf(': '), src = cut > 0 ? shown.slice(0, cut) : '', own = cut > 0 ? shown.slice(cut + 2) : shown;
			var row = E('label', { 'class': 'mm-check mm-wl-server', title: shown }, [b, E('span', {}, own)]);
			if (!by[src]) { by[src] = []; groups.push(src); }
			by[src].push(row);
			return [b, row];
		});
		w.list.replaceChildren.apply(w.list, groups.map(function(src) {
			return E('div', { 'class': 'mm-wl-group' }, (src ? [E('div', { 'class': 'mm-wl-group-head mm-dim' }, src)] : []).concat([E('div', { 'class': 'mm-wl-rows' }, by[src])]));
		}));
		if (!chosen.length) self.wlTick();
	},

	wlTick: function() {
		var w = this.wl, re = null;
		try { re = w.pattern.value ? new RegExp(w.pattern.value, 'i') : null; } catch (e) { re = null; }
		if (re) (w.boxes || []).forEach(function(x) { x[0].checked = re.test(x[0].value); });
	},

	wlServers: function(quiet) {
		var self = this, w = this.wl;
		if (!quiet) text(w.out, _('Asking the manager…'));
		return fs.exec(WLMODE, ['servers', w.mgr.value, w.sec.value]).then(function(r) {
			var d = {};
			try { d = JSON.parse((r && r.stdout) || '{}'); } catch (e) {}
			if (!d.ok) { if (!quiet) text(w.out, _('The manager did not give the servers: %s.').format(d.error || _('no answer'))); return; }
			var all = list(d.servers), shown = list(d.names), tags = [], names = [];
			w.names = {};
			/* the automatic choices of the group are no server of the pool */
			all.forEach(function(n, i) {
				w.names[n] = shown[i] || n;
				if (/-urltest(-out)?$|^direct|^block/.test(n)) return;
				tags.push(n); names.push(shown[i] || n);
			});
			text(w.live, d.auto ? (d.auto_now_name ? _('Now in the manager: the automatic choice (URL test), it uses “%s”.').format(d.auto_now_name) :
				_('Now in the manager: the automatic choice (URL test).')) :
				d.now ? _('Now in the manager: “%s”, chosen by hand or by the whitelist mode.').format(d.now_name || d.now) : '');
			w.live.hidden = !d.auto && !d.now;
			var away = list(d.sources).filter(function(x) { return !x.used; }).map(function(x) { return mm.LANG === 'ru' ? '«' + (x.label || x.name) + '»' : '“' + (x.label || x.name) + '”'; });
			text(w.srcNote, away.length ? N_(away.length, 'The source %s is not in this rule: the manager has none of its servers in the rule\'s group. Add it to the rule\'s servers or fallbacks in the manager, and they will show here.',
				'The sources %s are not in this rule: the manager has none of their servers in the rule\'s group. Add them to the rule\'s servers or fallbacks in the manager, and they will show here.').format(away.join(', ')) : '');
			w.srcNote.hidden = !away.length;
			if (quiet && !(w.boxes || []).length && !(w.chosen || []).length) return;
			self.wlList(tags, (w.chosen || []).filter(function(n) { return tags.indexOf(n) >= 0; }), names);
			if (!quiet) text(w.out, N_(tags.length, '%d server in the group.', '%d servers in the group.').format(tags.length));
		}, function() { if (!quiet) text(w.out, _('The manager did not give the servers: %s.').format(_('no answer'))); });
	},

	wlHints: function() {
		var w = this.wl;
		if (!(w.boxes || []).length) { text(w.out, _('Show the section\'s servers first.')); return Promise.resolve(); }
		text(w.out, _('Comparing the servers\' addresses with the community lists…'));
		return fs.exec(WLMODE, ['hints', w.mgr.value]).then(function(r) {
			var d = {};
			try { d = JSON.parse((r && r.stdout) || '{}'); } catch (e) {}
			if (!d.ok) { text(w.out, _('No suggestion: %s.').format(d.error || _('no answer'))); return; }
			var n = 0;
			w.boxes.forEach(function(x) {
				var h = (d.hints || {})[x[0].value], tag = x[1].querySelector('.mm-wl-hint-mark');
				if (tag) tag.remove();
				if (h === true) { n++; x[1].append(E('span', { 'class': 'mm-wl-hint-mark mm-ok-text' }, ' ✓ ' + _('likely to live under the whitelist'))); }
			});
			text(w.out, n ? N_(n, '%d server is in the community lists of addresses open under the whitelists.', '%d servers are in the community lists of addresses open under the whitelists.').format(n) :
				_('None of the servers is in the community lists (a server named by a domain is not judged).'));
		}, function() { text(w.out, _('No suggestion: %s.').format(_('no answer'))); });
	},

	wlSave: function() {
		var self = this, w = this.wl;
		var servers = (w.boxes || []).filter(function(x) { return x[0].checked; }).map(function(x) { return x[0].value; });
		if (!servers.length) servers = w.chosen || [];
		var values = { enabled: w.enabled.checked ? '1' : '0', auto: w.auto.checked ? '1' : '0', manager: w.mgr.value || '', section: w.sec.value || '',
			mode: w.mode.value, lan: w.lan.checked ? '1' : '0', router: w.router.checked ? '1' : '0', pattern: w.pattern.value || '' };
		if (values.enabled === '1' && !servers.length) { text(w.out, _('Tick at least one server.')); return Promise.resolve(); }
		return (w.exists ? uciSet('medvemodem', 'wlmode', values) : uciAdd('medvemodem', 'wlmode', 'wlmode', values)).then(function() {
			return uciDelete('medvemodem', 'wlmode', ['server']).catch(function() {});
		}).then(function() {
			return servers.length ? uciSet('medvemodem', 'wlmode', { server: servers }) : null;
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			w.exists = true;
			w.chosen = servers;
			text(w.out, _('Saved.'));
			return wlmodeCall('status').then(function(r) { self.wlState((r || {}).wlmode || {}); }, function() {});
		}, function(e) { text(w.out, _('Not saved: %s.').format(failText(e))); });
	},

	wlAct: function(action) {
		var self = this, w = this.wl;
		return wlmodeCall(action).then(function(r) {
			r = r || {};
			text(w.out, r.result === 'started' ? (action === 'on' ? _('Turning on…') : _('Turning off: the manager\'s settings are given back…')) :
				r.result === 'busy' ? _('Busy: try again in a minute.') : _('Not done: %s.').format(r.reason || r.result || _('no answer')));
			if (r.result !== 'started') return;
			/* the helper tries the servers one by one (3 s each) and the manager reloads: watched until the engine is done
			 * (the owner's r161: «Включение…» stayed while podkop had switched) */
			var tries = 0;
			function look() {
				return new Promise(function(ok) { window.setTimeout(ok, 3000); }).then(function() { return wlmodeCall('status'); }).then(function(s) {
					var st = (s || {}).wlmode || {};
					self.wlState(st);
					if (st.busy && ++tries < 40) return look();
					text(w.out, st.busy ? _('Still working: the state above refreshes when the page is opened again.') :
						st.error ? _('Not done: %s.').format(st.error) :
						st.on ? _('On: server “%s”.').format(st.label || st.server || '—') : _('Off: the manager\'s settings are given back.'));
					return self.wlServers(true);
				}, function() {});
			}
			return look();
		}, function(e) { text(w.out, _('Not done: %s.').format(failText(e))); });
	},

	reachCard: function() {
		var self = this;
		function area(name, title, ph) {
			var t = E('textarea', { 'class': 'mm-input mm-ta', id: 'mm-' + name, rows: '2', placeholder: ph, spellcheck: 'false', input: function() { self.dirty(); } });
			return [t, E('label', { 'class': 'mm-field', 'for': 'mm-' + name }, [E('span', { 'class': 'mm-dim' }, title), t])];
		}
		var w = area('reach_target', _('Main addresses'), '1.1.1.1:443 8.8.8.8:443');
		var r = area('reach_restricted', _('Whitelist addresses'), '77.88.55.242:443 87.240.132.72:443');
		this.areas = Object.assign(this.areas || {}, { reach_target: w[0], reach_restricted: r[0] });
		/* the whitelist addresses live on «Белые списки» (the owner, 2026-11-12); here a pointer to them */
		this.reachWl = [r[1], E('p', { 'class': 'mm-faint' }, _('If the main addresses are silent and these answer, access is limited (the operator may restrict it): the state is “Access limited” and the modem is not restarted. Empty means ya.ru and vk.com (77.88.55.242:443, 87.240.132.72:443); the word none switches this check off.'))];
		return this.sec('set-reach', _('Connection check'), [w[1],
			E('p', { 'class': 'mm-faint' }, _('Through each modem the router checks whether these addresses answer. Any answer, even a refused connection, means there is a link. Empty means 1.1.1.1:443 and 8.8.8.8:443.')),
			this.wlPage ? '' : this.movedNote(_('The whitelist addresses'))].concat(this.wlPage ? this.reachWl : []).filter(Boolean));
	},

	apnCard: function() {
		var self = this, info = E('span', { 'class': 'mm-mono' }, apnWords(this.apn)), out = E('span', { 'class': 'mm-faint mm-apn-result' });
		var btn = E('button', { type: 'button', 'class': 'mm-btn mm-apn-update', click: mm.busy(function() {
			btn.disabled = true; text(out, _('Checking…'));
			return mm.execLong(fs, 'apn-update', UPDATE, ['update']).then(parse, function() { return {}; }).then(function(j) {
				btn.disabled = false; text(out, updateWords(j));
				return fs.exec(UPDATE, ['version']).then(parse, function() { return {}; }).then(function(v) { if (v.ok) text(info, apnWords(v)); return updateWords(j); });
			});
		}) }, _('Check and update'));
		this.apnResult = out;
		return this.sec('set-apn', _('Access point database (APN)'), [
			E('dl', { 'class': 'mm-kv' }, [E('dt', {}, _('Current version')), E('dd', {}, info)]),
			E('div', { 'class': 'mm-actions' }, [btn, out]),
			E('p', { 'class': 'mm-faint' }, _('The APN for a SIM card is picked from it. The source is the open GNOME and Android databases; it is updated only by this button.'))], apnWords(this.apn));
	},

	/* «QModem»: its core, built by MedveModem's CI for this router, installed on request with a live log
	 * (qmodem-install, spec 12.6). The status is read after the page is up: the page never waits for it. */
	qmodemCard: function() {
		var self = this, info = E('span', {}, '…'), out = E('p', { 'class': 'mm-faint mm-qm-result', role: 'status' });
		var pre = E('pre', { 'class': 'mm-pre mm-qm-text' }, '');
		var log = E('details', { 'class': 'mm-qm-log', hidden: '' }, [E('summary', {}, _('Installation log')), pre]);
		var btn = E('button', { type: 'button', 'class': 'mm-btn mm-qm-install', hidden: '', click: mm.busy(function() {
			btn.disabled = true; text(out, _('Starting…'));
			return fs.exec(QMODEM, ['start', mm.langArg()]).then(parse, function(e) { return { error: failText(e) }; }).then(function(j) {
				if (!j.ok) { btn.disabled = false; text(out, _('Not started: %s.').format(qmWhy(j.error))); return; }
				log.hidden = false; log.open = true;
				return self.qmodemRefresh();
			});
		}) }, _('Install QModem'));
		/* QModem removed on the operator's confirmed button, with the same log (the owner, 2026-10-19) */
		var del = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-qm-remove', hidden: '', click: mm.busy(function() {
			if (!window.confirm(_('Remove QModem? Its services stop, its packages are removed; MedveModem connects the modems it drove by itself. QModem\'s settings stay.')))
				return;
			del.disabled = true; text(out, _('Starting…'));
			return fs.exec(QMODEM, ['remove', mm.langArg()]).then(parse, function(e) { return { error: failText(e) }; }).then(function(j) {
				if (!j.ok) { del.disabled = false; text(out, _('Not started: %s.').format(qmWhy(j.error))); return; }
				log.hidden = false; log.open = true;
				return self.qmodemRefresh();
			});
		}) }, _('Remove QModem'));
		this.qm = { info: info, out: out, pre: pre, log: log, btn: btn, del: del };
		var card = this.sec('set-qmodem', 'QModem', [
			E('dl', { 'class': 'mm-kv' }, [E('dt', {}, _('State')), E('dd', {}, info)]),
			E('div', { 'class': 'mm-actions' }, [btn, del]), out, log,
			/* which families it suits (the owner, 2026-10-31: «для каких семейств он может быть лучше, но точно не для просто
			 * qmi»; HANDOFF 71: QModem preferred for the FM350 and Quectel) */
			E('p', { 'class': 'mm-qm-when' }, [E('strong', {}, _('When it is worth it:')), ' ',
				_('Fibocom FM350-GL and Quectel modules (RM5xx, EM/EP06, EC2x): QModem is made for their USB modes and AT commands.')]),
			E('p', { 'class': 'mm-qm-when' }, [E('strong', {}, _('When it is not needed:')), ' ',
				_('modems that ModemManager drives over plain QMI or MBIM (Foxconn T99W175, Dell DW5821e / T77W968, Sierra, Telit, Fibocom L850 / L860), modems with a router inside (Huawei HiLink, ZTE, UFI sticks) and phones. There it adds nothing and is one more program at the modem. Without QModem MedveModem connects an FM350 by itself.')]),
			E('p', { 'class': 'mm-faint' }, _('Needed for modems that QModem manages by their profile (Quectel) and for those you chose QModem for by hand. Only QModem\'s core is installed, without its LuCI pages, built by MedveModem for this OpenWrt version; what is missing comes from the router\'s feeds. If something is not in the feeds, nothing is installed. After the installation QModem gets only the modems MedveModem hands to it. QModem\'s licence forbids commercial use.'))]);
		card.id = 'qmodem';
		this.qmGist = card.querySelector('.mm-sec-gist');
		this.qmodemRefresh();
		return card;
	},

	/* «Обновление MedveModem» (the owner, 2026-11-14: «обновления МедвеМодема и его бота автоматически с гитхаба изнутри
	 * Люси с проверкой наличия обновления и фоллбэком на прокси»): what is installed, what the releases offer, one button,
	 * the log. The check runs when the card opens if the last one is over an hour old */
	updateCard: function() {
		var self = this, u = this.up = {};
		u.kv = E('dl', { 'class': 'mm-kv mm-up-kv' });
		u.news = E('div', { 'class': 'mm-up-news', hidden: '' });
		u.out = E('p', { 'class': 'mm-faint mm-up-result', role: 'status' });
		u.pre = E('pre', { 'class': 'mm-pre mm-up-text' }, '');
		u.log = E('details', { 'class': 'mm-up-log', hidden: '' }, [E('summary', {}, _('Update log')), u.pre]);
		u.check = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-up-check', click: mm.busy(function() {
			text(u.out, _('Checking the releases…'));
			return fs.exec(SELFUP, ['check']).then(parse, function(e) { return { ok: false, error: failText(e) }; }).then(function(j) { self.updateDraw(j); });
		}) }, _('Check for updates'));
		u.btn = E('button', { type: 'button', 'class': 'mm-btn mm-up-install', hidden: '', click: mm.busy(function() {
			if (!window.confirm(_('Update MedveModem to %s? The engine and its LuCI pages (and the Telegram bot, if installed) are replaced; the settings stay. LuCI reloads its pages afterwards.').format(u.latest || '')))
				return;
			u.btn.disabled = true; text(u.out, _('Starting…'));
			return fs.exec(SELFUP, ['start', mm.langArg()]).then(parse, function(e) { return { error: failText(e) }; }).then(function(j) {
				if (!j.ok) { u.btn.disabled = false; text(u.out, _('Not started: %s.').format(j.error || _('no answer'))); return; }
				u.log.hidden = false; u.log.open = true;
				return self.updateRefresh();
			});
		}) }, _('Update'));
		var card = this.sec('set-update', _('MedveModem update'), [u.kv, u.news, E('div', { 'class': 'mm-actions' }, [u.btn, u.check]), u.out, u.log,
			E('p', { 'class': 'mm-faint' }, _('From the project\'s releases on GitHub; when GitHub does not open directly, through the router\'s tunnel proxies (Whitelists → Local proxies). The archive is checked against the release\'s SHA256SUMS before anything is installed. Bench builds are tests, not releases.'))]);
		card.id = 'update';
		this.upGist = card.querySelector('.mm-sec-gist');
		this.updateRefresh(true);
		return card;
	},

	updateDraw: function(j) {
		var u = this.up, ins = j.installed || {}, running = j.state === 'running';
		var row = function(k, v) { return [E('dt', {}, k), E('dd', {}, v || '—')]; };
		/* OpenWrt package managers spell alpha1 as _alpha1 and releases as -alpha1-r1.
		 * Normalize only for display; the updater itself compares installed packages. */
		var pretty = function(v) { return String(v || '').replace(/_alpha([0-9]+)/, '-alpha$1').replace(/-(\d+)$/, '-r$1'); };
		var versionLink = j.tag && /^[A-Za-z0-9._-]+$/.test(j.tag) && /^[A-Za-z0-9_-]+\/[A-Za-z0-9_.-]+$/.test(j.repo || '') ?
			E('a', { href: 'https://github.com/' + j.repo + '/releases/tag/' + j.tag, target: '_blank', rel: 'noopener noreferrer' }, pretty(j.latest)) : pretty(j.latest);
		u.latest = j.latest;
		u.kv.replaceChildren.apply(u.kv, [].concat(
			row(_('Installed'), ins.engine ? pretty(ins.engine) : _('not installed')),
			ins.luci && ins.luci !== ins.engine ? row(_('LuCI version'), pretty(ins.luci) + ' · ' + _('does not match the engine')) : [],
			ins.bot ? row(_('Telegram bot'), pretty(ins.bot)) : [],
			row(_('Available'), j.latest ? [versionLink, ' · ', j.newer ? _('update available') : _('already installed')] : j.error ? _('Check failed: %s').format(j.error) : _('Not checked yet')),
			row(_('Channel'), j.channel === 'bench' ? _('Test builds') : _('Published releases')),
			row(_('Checked'), j.checked_at ? new Date(j.checked_at * 1000).toLocaleString(mm.LANG) : '—')));
		/* what is new since the installed version: the release's short highlights, by version (the owner, 2026-11-14) */
		var news = list(j.highlights).filter(function(h) { return h && h.t; }), by = [], cur = null;
		news.forEach(function(h) { if (!cur || cur.v !== h.v) by.push(cur = { v: h.v, t: [] }); cur.t.push(h.t); });
		u.news.hidden = !by.length;
		u.news.replaceChildren.apply(u.news, by.length ? [E('strong', {}, _('What is new'))].concat(by.map(function(g) {
			return E('div', { 'class': 'mm-up-ver' }, [E('div', { 'class': 'mm-faint' }, g.v),
				E('ul', {}, g.t.map(function(t) { return E('li', {}, t); }))]);
		})) : []);
		u.btn.hidden = !j.newer || !!j.error || running;
		u.btn.disabled = running;
		text(u.btn, j.latest ? _('Update to %s').format(j.latest) : _('Update'));
		var gist = j.error ? _('Update check failed') : j.newer ? _('update %s available').format(pretty(j.latest)) : j.latest ? _('up to date') : '';
		if (this.upGist) text(this.upGist, gist);
		text(u.out, j.error ? _('Update check failed: %s').format(j.error) : running ? _('Updating, the log is below.') : j.state === 'failed' ? _('The last attempt: %s. Details in the log.').format(j.result || _('error')) :
			j.state === 'done' && j.result && j.result !== 'current' ? _('Updated to %s.').format(j.result) : '');
		return running;
	},

	updateRefresh: function(first) {
		var self = this, u = this.up;
		clearTimeout(this.upTimer);
		return fs.exec(SELFUP, ['status']).then(parse, function() { return {}; }).then(function(j) {
			if (!j.ok) { text(u.out, _('The update script does not answer.')); return; }
			/* over an hour since the last check: ask again by itself, once when the card opens */
			if (first && j.state !== 'running' && (!j.checked_at || Date.now() / 1000 - j.checked_at> 3600))
				return fs.exec(SELFUP, ['check']).then(parse, function() { return j; }).then(function(k) { self.updateDraw(k.ok ? k : j); });
			var running = self.updateDraw(j);
			if (!running && j.state !== 'failed' && j.state !== 'done') return;
			u.log.hidden = false;
			return fs.read(j.log).then(function(t) { u.pre.textContent = t || ''; u.pre.scrollTop = u.pre.scrollHeight; }, function() {}).then(function() {
				if (running) self.upTimer = soonShown(function() { self.updateRefresh(); }, 2000);
			});
		});
	},

	/* the status, and the log while a run goes on (every 2 s) and once after it ended */
	qmodemRefresh: function() {
		var self = this, q = this.qm;
		clearTimeout(this.qmTimer);
		return fs.exec(QMODEM, ['status']).then(parse, function() { return {}; }).then(function(j) {
			var running = j.state === 'running';
			text(q.info, qmWords(j));
			if (self.qmGist) text(self.qmGist, qmWords(j));
			/* an earlier build installed: the same button updates it (QModem's stable line with our resolvers, 2026-10-22) */
			q.btn.hidden = !j.ok || (j.installed && !j.update) || !j.supported || !j.source;
			text(q.btn, j.installed && j.update ? _('Update QModem to %s').format(j.version) : _('Install QModem'));
			q.btn.disabled = running;
			q.del.hidden = !j.ok || !j.installed;
			q.del.disabled = running;
			text(q.out, !j.ok ? _('The installation script does not answer.') : j.installed && j.update && !running ?
				_('The update: QModem\'s stable line with fixes, without Chinese DNS by default. QModem\'s settings are kept.') : j.installed ? '' : !j.supported ? _('Installation unavailable: %s.').format(qmWhy(j.why)) :
				!j.source ? _('Installation unavailable: %s.').format(qmWhy('no source')) : running ? _('Installing, the log is below.') :
				j.state === 'failed' ? _('The last attempt: %s. Details in the log.').format(j.result || _('error')) : '');
			if (!running && j.state !== 'failed' && j.state !== 'done') return j;
			q.log.hidden = false;
			return fs.read(j.log).then(function(t) { q.pre.textContent = t || ''; q.pre.scrollTop = q.pre.scrollHeight; }, function() {}).then(function() {
				if (running) self.qmTimer = soonShown(function() { self.qmodemRefresh(); }, 2000);
				return j;
			});
		});
	},

	/* «Служба MedveModem» (the owner, 2026-10-19: «тумблер отключения службы MedveModem глобально с гашением остальных
	 * модемных служб (и без, галочка)»): off alone, the other services take the modems over; with the box, they stop too */
	serviceCard: function() {
		var self = this;
		this.sv = { kv: E('dl', { 'class': 'mm-kv mm-svc-kv' }), procs: E('div', { 'class': 'mm-tablewrap mm-svc-procs', hidden: true }), out: E('p', { 'class': 'mm-faint mm-svc-result', role: 'status' }),
			box: E('input', { type: 'checkbox', id: 'mm-svc-others' }), boxText: E('span', {}, '') };
		this.sv.btn = E('button', { type: 'button', 'class': 'mm-btn mm-svc-toggle', click: mm.busy(function() { return self.serviceToggle(); }) }, '');
		/* the owner, 2026-10-20: «кнопки остановить, запустить» — now only; the switch above decides the boot */
		this.sv.run = E('button', { type: 'button', 'class': 'mm-btn mm-svc-run', click: mm.busy(function() { return self.serviceRun(self.svRunning ? 'stop' : 'start'); }) }, '');
		this.sv.again = E('button', { type: 'button', 'class': 'mm-btn mm-svc-restart', click: mm.busy(function() { return self.serviceRun('restart'); }) }, _('Restart MedveModem'));
		/* QModem's dial alone (field 2026-10-20: «Перезапустить» restarted MedveModem only, the FM350 stayed undialled) */
		this.sv.qm = E('button', { type: 'button', 'class': 'mm-btn mm-svc-qmodem', hidden: true, click: mm.busy(function() { return self.serviceQmodem(); }) }, '');
		/* the emergency one (the owner, 2026-10-20: «вариант А и как аварийный Б с ворнингом»): QModem hangs every dial up and
		 * takes the whole network down, LAN included; a bordered red button, not a link among filled ones (the owner, 2026-10-21:
		 * «кнопка не красивая») */
		this.sv.qmHard = E('button', { type: 'button', 'class': 'mm-btn is-danger mm-svc-qmodem-hard', hidden: true,
			click: mm.busy(function() { return self.serviceQmodem(true); }) }, _('QModem emergency restart'));
		var card = this.sec('set-service', _('MedveModem service'), [
			this.sv.kv, this.sv.procs,
			E('label', { 'class': 'mm-check mm-svc-box' }, [this.sv.box, ' ', this.sv.boxText]),
			E('div', { 'class': 'mm-actions' }, [this.sv.run, this.sv.again, this.sv.qm, this.sv.qmHard, this.sv.btn]), this.sv.out,
			E('p', { 'class': 'mm-faint' }, _('“Stop” and “Start” act now; after a router restart the service starts again if it is switched on. A switched-off service does not start after a restart either, until it is switched on here. Without the box the other modem services keep working and take the modems themselves. With the box they stop too and do not start at boot; “Switch on” with the box starts exactly those stopped here. The modems\' network interfaces are not deleted.'))]);
		card.id = 'service';
		this.svGist = card.querySelector('.mm-sec-gist');
		this.serviceRefresh();
		return card;
	},

	/* one list, one column of names: state lights, versions, the footprint, the other services */
	serviceDraw: function() {
		var v = this.sv, j = this.svState || {}, me = this.svSelf || {}, rows = [];
		function lit(tone, words) { return E('span', { 'class': 'mm-svc-state' }, [E('span', { 'class': 'mm-svc-dot is-' + tone }), words]); }
		var said = typeof j.enabled !== 'boolean' ? _('no data') : !j.enabled ? _('inactive') : j.running ? _('works') : _('enabled, not running');
		rows.push(['MedveModem', lit(typeof j.enabled !== 'boolean' ? 'off' : j.running ? 'ok' : j.enabled ? 'bad' : 'off', said)]);
		rows.push([_('Engine version'), me.version || '—']);
		rows.push([_('Web interface version'), mm.VERSION]);
		/* the PID and the memory are in the processes' table below when the engine says them */
		if (me.pid && !this.svProcs) rows.push([_('Process'), 'PID ' + me.pid]);
		if (me.rss_kb && !this.svProcs) rows.push([_('Memory (RSS)'), _('%s MB').format(num(me.rss_kb / 1024, 1))]);
		var win = me.cpu_window_s, pct = me.cpu_pct != null ? me.cpu_pct : me.cpu_pct_since_start;
		if (pct != null)
			rows.push([_('CPU load'), num(pct) + ' % ' + (me.cpu_pct != null ?
				(win >= 23 * 3600 ? _('over a day') : _('over %s').format(win >= 3600 ? _('%d h').format(Math.round(win / 3600)) : _('%d min').format(Math.max(1, Math.round(win / 60))))) :
				_('since the service started')) + ', ' + _('together with the programs it runs')]);
		var others = j.others || {}, stopped = this.svStopped || [];
		Object.keys(SVC_NAMES).filter(function(n) { return (others[n] || {}).installed; }).forEach(function(n) {
			var o = others[n];
			rows.push([SVC_NAMES[n], lit(o.running ? 'ok' : 'off', (o.running ? _('works') : _('not running')) + (o.enabled ? '' : ', ' + _('does not start at boot')) +
				(stopped.indexOf(n) >= 0 ? ' (' + _('stopped here') + ')' : ''))]);
		});
		v.kv.replaceChildren.apply(v.kv, [].concat.apply([], rows.map(function(r) { return [E('dt', {}, r[0]), E('dd', {}, r[1])]; })));
		this.procsDraw();
		if (this.svGist) text(this.svGist, said);
	},

	/* the processes around MedveModem, as the bot's status says them (the owner, 2026-10-31: «RSS всех движков и процессов
	 * вокруг Медвемодема, их pid, rss и количество респаунов»): the engine, the bot, ModemManager, QModem */
	procsDraw: function() {
		var sv = this.svProcs, me = this.svSelf || {}, v = this.sv, rows = [];
		function up(s) {
			if (typeof s !== 'number' || s < 0) return '—';
			var d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), m = Math.floor(s % 3600 / 60);
			return d ? _('%d d %d h').format(d, h) : h ? _('%d h %d min').format(h, m) : _('%d min').format(m);
		}
		function mb(kb) { return typeof kb === 'number' && kb > 0 ? _('%s MB').format(num(kb / 1024, 1)) : '—'; }
		function again(n24, extra) {
			var w = n24 ? N_(n24, '%d in 24 h', '%d in 24 h').format(n24) : _('none in 24 h');
			return extra ? w + ', ' + extra : w;
		}
		if (!sv || !me.pid) { v.procs.hidden = true; return; }
		var eng = sv.medvemodem || {};
		rows.push([_('MedveModem engine'), String(me.pid), mb(me.rss_kb), up(me.up_s),
			again(eng.restarts_24h || 0, eng.crashes_since_boot ? N_(eng.crashes_since_boot, '%d crash since the router started', '%d crashes since the router started').format(eng.crashes_since_boot) : '')]);
		[['bot', _('Telegram bot')], ['modemmanager', 'ModemManager'], ['qmodem', 'QModem']].forEach(function(k) {
			var o = sv[k[0]];
			if (!o) return;
			if (!o.running) {
				rows.push([k[1], '—', '—', _('not running'), again(o.restarts_24h || 0, o.stops_24h ? N_(o.stops_24h, 'stopped %d time', 'stopped %d times').format(o.stops_24h) : '')]);
				return;
			}
			rows.push([k[1] + (o.processes > 1 ? ' (' + N_(o.processes, '%d process', '%d processes').format(o.processes) + ')' : ''), String(o.pid), mb(o.rss_kb), up(o.up_s),
				again(o.restarts_24h || 0, o.stops_24h ? N_(o.stops_24h, 'stopped %d time', 'stopped %d times').format(o.stops_24h) : '')]);
		});
		var tb = E('table', { 'class': 'mm-table mm-procs' }, [
			E('thead', {}, E('tr', {}, [E('th', {}, _('Process')), E('th', { 'class': 'num' }, 'PID'), E('th', { 'class': 'num' }, _('Memory (RSS)')),
				E('th', {}, _('Running for')), E('th', {}, _('Restarts'))])),
			E('tbody', {}, rows.map(function(r) { return E('tr', {}, r.map(function(c, i) { return E('td', { 'class': i === 1 || i === 2 ? 'num mm-mono' : '' }, c); })); }))]);
		var foot = typeof eng.router_up_s === 'number' ? E('p', { 'class': 'mm-faint' }, _('The router has been running for %s.').format(up(eng.router_up_s))) : '';
		v.procs.replaceChildren(tb, foot);
		v.procs.hidden = false;
	},

	serviceRefresh: function() {
		var self = this, v = this.sv;
		return fs.exec(SERVICE, ['status']).then(parse, function() { return {}; }).then(function(j) {
			var others = j.others || {}, names = Object.keys(SVC_NAMES).filter(function(n) { return (others[n] || {}).installed; });
			self.svState = j;
			self.svOn = j.enabled !== false;
			self.svRunning = !!j.running;
			self.svStopped = list(j.stopped_services);
			text(v.btn, self.svOn ? _('Switch the MedveModem service off') : _('Switch the MedveModem service on'));
			v.btn.className = 'mm-btn mm-svc-toggle' + (self.svOn ? '' : ' is-primary');
			v.btn.hidden = typeof j.enabled !== 'boolean';
			text(v.run, j.running ? _('Stop') : _('Start'));
			v.run.className = 'mm-btn mm-svc-run' + (j.running ? '' : ' is-primary');
			v.run.hidden = v.again.hidden = !self.svOn || typeof j.enabled !== 'boolean';
			v.again.hidden = v.again.hidden || !j.running;
			var qn = others.qmodem_network || {};
			v.qm.hidden = !qn.installed;
			v.qmHard.hidden = !qn.installed || !qn.running;
			text(v.qm, qn.running ? _('Restart QModem') : _('Start QModem'));
			v.qm.className = 'mm-btn mm-svc-qmodem' + (qn.installed && !qn.running ? ' is-primary' : '');
			text(v.boxText, self.svOn ? _('Also stop ModemManager and the QModem services') :
				_('Also start the services stopped here') + (self.svStopped.length ? ': ' + self.svStopped.map(function(n) { return SVC_NAMES[n] || n; }).join(', ') : ''));
			v.box.parentNode.hidden = self.svOn ? !names.length : !self.svStopped.length;
			v.box.checked = false;
			self.serviceDraw();
			if (j.running) self.selfRefresh(); else { self.svSelf = {}; self.serviceDraw(); }
			return j;
		});
	},

	/* the service's own footprint (the owner, 2026-10-19): PID, memory, and the CPU over the last day with the helpers it ran */
	selfRefresh: function() {
		var self = this;
		return getStatus().then(function(st) {
			self.svSelf = Object.assign({}, (st || {}).self || {}, { version: (st || {}).version, up_s: (st || {}).uptime });
			self.svProcs = (st || {}).services || null;
			self.serviceDraw();
		}, function() { self.svSelf = {}; self.serviceDraw(); });
	},

	serviceRun: function(how) {
		var self = this;
		if (how === 'stop' && !window.confirm(_('Stop the MedveModem service now? The modems stay connected; recovery and channel switching do not work until it is started.')))
			return Promise.resolve();
		text(this.sv.out, { stop: _('Stopping…'), start: _('Starting…'), restart: _('Restarting…') }[how]);
		return fs.exec(SERVICE, [how]).then(parse, function(e) { return { error: failText(e) }; }).then(function(j) {
			text(self.sv.out, j.error === 'disabled' ? _('The service is switched off: first “Switch the MedveModem service on”.') : j.error ? _('Not done: %s.').format(j.error) :
				{ stop: _('The MedveModem service is stopped.'), start: _('The MedveModem service is started.'), restart: _('The MedveModem service is restarted.') }[how]);
			return self.serviceRefresh();
		});
	},

	/* «Перезапустить QModem» is QModem's reload: the modems without a dial get one, a dial running is left alone; the
	 * emergency restart hangs every dial up and restarts the network (bench 2026-10-20: the LAN went down with it) */
	serviceQmodem: function(hard) {
		var self = this, running = !!(((this.svState || {}).others || {}).qmodem_network || {}).running;
		if (hard && !window.confirm(_('QModem emergency restart: QModem breaks all its connections and restarts the whole router network: for half a minute the internet through all modems and the link to the router over LAN are gone, this page included. “Restart QModem” is usually enough. Continue?')))
			return Promise.resolve();
		var how = hard ? 'hard-restart' : running ? 'reload' : 'start';
		text(this.sv.out, hard ? _('QModem emergency restart…') : running ? _('Restarting QModem…') : _('Starting QModem…'));
		return fs.exec(SERVICE, ['qmodem', how]).then(parse, function(e) { return { error: failText(e) }; }).then(function(j) {
			var run = !!((j.others || {}).qmodem_network || {}).running;
			text(self.sv.out, j.error ? _('Not done: %s.').format(j.error === 'not installed' ? _('QModem is not installed') : j.error) :
				!run ? _('QModem started, but it has nothing to connect: no modem is switched on in its settings.') :
				hard ? _('QModem restarted; the router network comes up again.') :
				running ? _('QModem reread its settings: modems without a connection connect, working connections are not touched.') : _('QModem started.'));
			return self.serviceRefresh();
		}, function(e) {
			/* the emergency restart takes the LAN down: the answer may never come */
			if (hard) text(self.sv.out, _('The router network restarts: reload the page in a minute.'));
			else throw e;
		});
	},

	/* the operator's emergency button: restart ModemManager (H16 revised 2026-10-09; never automatic). Moved here from the
	 * modem page (the owner, 2026-10-19): it breaks every modem ModemManager drives, not one */
	mmrCard: function() {
		var self = this, m = this.mmr = {};
		m.call = E('div', { 'class': 'mm-callout is-err', hidden: '' });
		m.state = E('p', { 'class': 'mm-faint', hidden: '' });
		m.btn = E('button', { type: 'button', 'class': 'mm-btn is-danger mm-mmr', click: mm.busy(function() { return self.restartMM(); }) }, _('Restart ModemManager'));
		m.node = this.sec('set-mmr', _('Emergency: ModemManager'), [m.call,
			E('p', { 'class': 'mm-faint' }, _('If ModemManager hangs (the modems do not answer, the data does not refresh for several minutes, recovery steps do not help), it can be restarted. The link breaks through all the router\'s modems it manages for about a minute. At most once in 5 minutes; after it automatic recovery stays out for 3 minutes.')),
			m.state, E('div', { 'class': 'mm-row' }, m.btn)]);
		m.node.id = 'mmr';
		m.node.hidden = true;
		m.gist = m.node.querySelector('.mm-sec-gist');
		this.mmrRefresh();
		return m.node;
	},

	mmrRefresh: function() {
		var m = this.mmr;
		return getStatus().then(function(st) {
			var mmb = (st || {}).modemmanager || {}, r = mmb.restart;
			var trouble = mmb.installed === false ? '' : { timeout: _('does not answer the requests'), error: _('answers with an error'), unavailable: _('is not running') }[mmb.backend];
			m.node.hidden = !mmb.backend || mmb.backend === 'disabled' || mmb.installed === false;
			text(m.call, trouble ? _('ModemManager %s').format(trouble) + (mmb.age_s != null ? '; ' + _('the last answer %s').format(agoLong(mmb.age_s)) : '') + '.' : '');
			m.call.hidden = !trouble;
			if (m.gist) text(m.gist, trouble ? _('ModemManager %s').format(trouble) : _('works'));
			var wait = r && r.next_allowed_s > 0 ? Math.ceil(r.next_allowed_s / 60) : 0;
			text(m.state, !r ? '' : r.state === 'running' ? _('ModemManager is restarting…') :
				_('The last restart: %s').format(new Date(r.at * 1000).toLocaleTimeString(mm.LANG)) + (r.state === 'failed' ? ' — ' + _('failed') : '') +
				(wait ? '. ' + _('The next one is possible in %d min.').format(wait) : '.'));
			m.state.hidden = !r;
			m.btn.disabled = !!(r && (r.state === 'running' || wait));
		}, function() { m.node.hidden = true; });
	},

	restartMM: function() {
		var self = this, m = this.mmr;
		if (!window.confirm(_('Restart ModemManager? The link through all the router\'s modems it manages breaks for about a minute.')))
			return Promise.resolve();
		text(m.state, _('Restarting ModemManager…')); m.state.hidden = false;
		return mmRestart(true, 'LuCI').then(function(r) {
			r = r || {};
			self.say(r.result === 'started' ? _('ModemManager is restarting; the modems come back in about a minute.') :
				r.result === 'running' ? _('ModemManager is already restarting.') :
				_('The ModemManager restart was not done') + (r.retry_s ? ': ' + _('the next one is possible in %d min.').format(Math.ceil(r.retry_s / 60)) : r.why ? ': ' + r.why : '.'));
			return self.mmrRefresh();
		}, function(e) {
			self.say(_('This version of the MedveModem service cannot restart ModemManager (%s).').format(failText(e)));
		});
	},

	serviceToggle: function() {
		var self = this, on = !this.svOn, others = this.sv.box.checked && !this.sv.box.parentNode.hidden;
		if (!on && !window.confirm((others ? _('Switch the MedveModem service off, and also ModemManager and the QModem services?') : _('Switch the MedveModem service off?')) + ' ' +
			(others ? _('No service will manage the modems, the link through them is gone.') :
				_('ModemManager and QModem will manage the modems, if they are there; the MedveModem pages show only this switch.'))))
			return Promise.resolve();
		this.sv.btn.disabled = true;
		text(this.sv.out, on ? _('Switching on…') : _('Switching off…'));
		return fs.exec(SERVICE, [on ? 'on' : 'off'].concat(others ? ['others'] : [])).then(parse, function(e) { return { error: failText(e) }; }).then(function(j) {
			text(self.sv.out, j.error ? _('Not done: %s.').format(j.error) : on ? _('The MedveModem service is switched on.') : _('The MedveModem service is switched off.'));
			return self.serviceRefresh();
		}).finally(function() { self.sv.btn.disabled = false; });
	},

	/* «Драйверы и ModemManager» (the owner, 2026-10-17): what the router has for modems, installed from its feeds on
	 * request with a live log (pkg-install), as QModem's core is. Read after the page is up. */
	pkgsCard: function() {
		var self = this;
		this.pk = { rows: E('dl', { 'class': 'mm-kv mm-pkg-rows' }), out: E('p', { 'class': 'mm-faint mm-pkg-result', role: 'status' }),
			pre: E('pre', { 'class': 'mm-pre mm-pkg-text' }, '') };
		this.pk.log = E('details', { 'class': 'mm-pkg-log', hidden: '' }, [E('summary', {}, _('Installation log')), this.pk.pre]);
		var card = this.sec('set-pkg', _('Drivers and ModemManager'), [this.pk.rows, this.pk.out, this.pk.log,
			E('p', { 'class': 'mm-faint' }, _('The packages come from the router\'s feeds (apk or opkg). First a dry run: if something is not in the feeds, nothing is installed. Kernel modules need a repository that matches this firmware\'s kernel; on a SNAPSHOT the packages for an older build may be gone. A driver is bound at once, without a restart. An installed ModemManager is enabled and started; one already working is not restarted. Packages are removed by OpenWrt\'s own means.'))]);
		card.id = 'components';
		this.pkGist = card.querySelector('.mm-sec-gist');
		this.pkgsRefresh();
		return card;
	},

	pkgsInstall: function(ids) {
		var self = this;
		text(this.pk.out, _('Starting…'));
		return fs.exec(PKG, ['start', mm.langArg()].concat(ids)).then(parse, function(e) { return { error: failText(e) }; }).then(function(j) {
			if (!j.ok) { text(self.pk.out, _('Not started: %s.').format(PKG_WHY[j.error] || j.error || _('no answer'))); return; }
			self.pk.log.hidden = false; self.pk.log.open = true;
			return self.pkgsRefresh();
		});
	},

	pkgsRefresh: function() {
		var self = this, k = this.pk;
		clearTimeout(this.pkTimer);
		return fs.exec(PKG, ['status']).then(parse, function() { return {}; }).then(function(j) {
			var running = j.state === 'running', comps = list(j.components).filter(function(c) { return c && PKG_NAMES[c.id]; });
			/* lpac is for an eSIM only: not counted as missing, not in «install everything» */
			var missing = comps.filter(function(c) { return c.state === 'missing' && c.kind !== 'ours'; });
			k.rows.replaceChildren.apply(k.rows, [].concat.apply([], comps.map(function(c) {
				var n = PKG_NAMES[c.id], b = (c.state === 'missing' || c.state === 'other') && j.supported ? E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-pkg-one',
					'data-id': c.id, disabled: running ? '' : null, click: mm.busy(function() { return self.pkgsInstall([c.id]); }) }, _('Install')) : '';
				/* the installed package's version (bench 2026-10-19); none for a driver built into the kernel */
				return [E('dt', { title: c.packages }, n[0]), E('dd', {}, [E('span', { 'class': c.state === 'missing' ? 'mm-pkg-state is-off' : 'mm-pkg-state' }, pkgState(c)),
					c.version && c.state !== 'missing' ? E('span', { 'class': 'mm-faint mm-pkg-ver' }, ' · ' + c.version) : '',
					' ', b, E('div', { 'class': 'mm-faint' }, n[1])])];
			})));
			if (self.pkGist) text(self.pkGist, !j.ok ? _('no data') : missing.length ? _('not installed: %d').format(missing.length) : _('all installed'));
			text(k.out, !j.ok ? _('The installation script does not answer.') : !j.supported ? _('Installation unavailable: the router has no package manager.') :
				running ? _('Installing (%s), the log is below.').format(j.target) :
				j.state === 'failed' ? _('The last attempt (%s): %s. Details in the log.').format(j.target, PKG_WHY[j.result] || j.result || _('error')) :
				j.state === 'done' ? _('Installed: %s.').format(j.target) : '');
			if (!running && j.state !== 'failed' && j.state !== 'done') return j;
			k.log.hidden = false;
			return fs.read(j.log).then(function(t) { k.pre.textContent = t || ''; k.pre.scrollTop = k.pre.scrollHeight; }, function() {}).then(function() {
				if (running) self.pkTimer = soonShown(function() { self.pkgsRefresh(); }, 2000);
				return j;
			});
		});
	},

	/* the form as one string, to tell whether anything changed */
	read: function() {
		var self = this, o = { names: {} };
		Object.keys(this.boxes).forEach(function(k) { o[k] = self.boxes[k].checked; });
		Object.keys(this.selects).forEach(function(k) { o[k] = self.selects[k].value; });
		Object.keys(this.areas).forEach(function(k) { o[k] = self.areas[k].value.trim().split(/[\s,]+/).filter(Boolean); });
		Object.keys(this.nameInputs).forEach(function(id) { o.names[id] = self.nameInputs[id].value.trim(); });
		o.restart_on_uplink = Object.keys(this.svcBoxes || {}).filter(function(n) { return self.svcBoxes[n].checked; });
		return JSON.stringify(o);
	},

	nameSection: function(id) {
		var n = this.names;
		return Object.keys(n).filter(function(s) { return n[s].device_id === id; })[0];
	},

	reset: function() {
		var self = this, c = this.core;
		/* unset means on, except failover: it moves routes, so only when asked for */
		Object.keys(this.boxes).forEach(function(k) { self.boxes[k].checked = k === 'failover' || k === 'cell_info' || k === 'dns_check' || k === 'sms_keep_all' ? c[k] === '1' : c[k] !== '0'; });
		this.selects.failback.value = c.failback === 'demote' ? 'demote' : 'restore';
		this.selects.failover_flush.value = c.failover_flush === 'all' || c.failover_flush === '0' ? c.failover_flush : 'link';
		this.selects.radio_lock_guard.value = ['0', '5', '15'].indexOf(c.radio_lock_guard) >= 0 ? c.radio_lock_guard : '10';
		Object.keys(this.areas).forEach(function(k) { self.areas[k].value = list(c[k]).join(' '); });
		var svcOn = this.svcChosen(c);
		Object.keys(this.svcBoxes || {}).forEach(function(n) { self.svcBoxes[n].checked = svcOn.indexOf(n) >= 0; });
		Object.keys(this.nameInputs).forEach(function(id) { var s = self.nameSection(id); self.nameInputs[id].value = s ? words(self.names[s].name) : ''; });
		this.saved = this.read();
		this.dirty();
	},

	/* checks first, then one commit: a half-saved form never reaches the engine */
	store: function() {
		var self = this, f = JSON.parse(this.read()), bad = [];
		var q = function(v) { return _('“%s”').format(v); };
		f.reach_target.forEach(function(v) { if (target(v) !== true) bad.push(q(v)); });
		f.reach_restricted.forEach(function(v) { if (v !== 'none' && target(v) !== true) bad.push(q(v)); });
		if (bad.length) { this.say(_('Connection check: %s — an address and a port are needed, for example 1.1.1.1:443 or [2606:4700:4700::1111]:443.').format(bad.join(', ')), true); return Promise.resolve(); }
		bad = f.failover_manual.filter(function(v) { return !/^[A-Za-z0-9._@-]{1,15}$/.test(v); });
		if (bad.length) { this.say(_('Manual-only channels: %s — a network device name is needed, for example wlan0.').format(bad.map(q).join(', ')), true); return Promise.resolve(); }
		var ops = [], drop = [], values = { auto_connect: f.auto_connect ? '1' : '0', auto_recovery: f.auto_recovery ? '1' : '0', host_fixes: f.host_fixes ? '1' : '0', failover: f.failover ? '1' : '0',
			failover_dns: f.failover_dns ? '1' : '0', proxy_guard: f.proxy_guard ? '1' : '0', dns_check: f.dns_check ? '1' : '0', failback: f.failback, failover_flush: f.failover_flush, radio_lock_guard: f.radio_lock_guard,
			cell_info: f.cell_info ? '1' : '0', antennas: f.antennas ? '1' : '0', web_forward: f.web_forward ? '1' : '0', status_overview: f.status_overview ? '1' : '0', sms_archive: f.sms_archive ? '1' : '0', sms_keep_all: f.sms_keep_all ? '1' : '0' };
		/* an emptied list is deleted only when the config has it: deleting an absent option is ubus «not found» (bench, 2026-10-09) */
		['reach_target', 'reach_restricted', 'failover_manual', 'restart_on_uplink'].forEach(function(k) { if (f[k].length) values[k] = f[k]; else if (self.core[k] != null) drop.push(k); });
		/* none ticked is «-»: an absent list is the engine's own choice (ZeroBlock when installed) */
		if (!f.restart_on_uplink.length && this.services.some(function(v) { return v.restart; })) { values.restart_on_uplink = ['-']; drop = drop.filter(function(k) { return k !== 'restart_on_uplink'; }); }
		ops.push(function() { return self.core['.name'] ? uciSet('medvemodem', 'core', values) : uciAdd('medvemodem', 'core', 'core', values); });
		if (drop.length) ops.push(function() { return uciDelete('medvemodem', 'core', drop); });
		Object.keys(f.names).forEach(function(id) {
			var s = self.nameSection(id), name = f.names[id];
			if (s && name === words(self.names[s].name)) return;
			if (s && !name) ops.push(function() { return uciDrop('medvemodem', s); });
			else if (s) ops.push(function() { return uciSet('medvemodem', s, { name: name }); });
			else if (name) ops.push(function() { return uciAdd('medvemodem', 'modem', null, { device_id: id, name: name }); });
		});
		this.save.disabled = true;
		this.say(_('Saving…'));
		return ops.reduce(function(p, op) { return p.then(op); }, Promise.resolve()).then(function() { return uciCommit('medvemodem'); })
			.then(function() { return Promise.all([uciGet('medvemodem', 'core'), uciGet('medvemodem', 'modem')]); })
			.then(function(r) {
				self.core = (r[0] && r[0].values && r[0].values.core) || {};
				self.names = (r[1] && r[1].values) || {};
				self.reset();
				self.say(_('Saved. The MedveModem service applies the changes itself.'));
			}, function(e) {
				/* nothing half-done stays staged for a later commit */
				self.save.disabled = false;
				self.say(_('Saving error: %s. The changes are not applied.').format(failText(e)), true);
				return uciRevert('medvemodem').catch(function() {});
			});
	}
});
