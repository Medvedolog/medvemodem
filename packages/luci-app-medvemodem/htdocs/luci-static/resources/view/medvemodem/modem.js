'use strict';
'require view';
'require rpc';
'require poll';
'require fs';
'require medvemodem.common as mm';

var REASONS = mm.REASONS, STAGES = mm.STAGES, EVENTS = mm.EVENTS, FOLLOW_WHAT = mm.FOLLOW_WHAT, WHO = mm.WHO, eventWords = mm.eventWords, RESULTS = mm.RESULTS;
/* the engine's AT dial (dial_at.c), step by step (the owner, 2026-10-17: what went wrong on an FM350 without QModem) */
var DIAL_STEPS = { cops: _('the network checked'), 'deregistered: AT+COPS=0': _('the modem was taken off the network: sent to search for it'),
	registered: _('the modem is in the network'), 'cold: asked for a new session': _('a new session is needed (after a recovery)'),
	'cold: the network changed': _('the network changed: a new session'), 'no address: the other PDP type': _('no address given: retrying with the other type'),
	reused: _('the modem\'s session reused'), activated: _('the session is up') };
/* [the engine's English, its words; %s takes the first group] */
var DIAL_WHY = [[/^no network registration within (\d+) s/, _('the modem did not register in the network within %s s')],
	[/^the network refused the registration.*/, _('the network refused the registration: the SIM card, the tariff or the IMEI')],
	[/^(AT\S*): the dial took longer than its budget/, _('the dial did not fit its time (%s)')], [/^(AT\S*): no answer/, _('the modem did not answer %s')]];
function dialWords(x) {
	if (!x || typeof x.t !== 'number') return '';
	var why = words(x.error), steps = list(x.steps).map(function(s) { return DIAL_STEPS[s] || s; });
	DIAL_WHY.forEach(function(w) { why = why.replace(w[0], function(m0, g) { return w[1].format(g || ''); }); });
	return clock(x.t) + ' · ' + (x.result === 'connected' ? _('connected') + (x.reused ? ', ' + _('the modem\'s previous session') : '') +
		(x.pdp ? ' (' + x.pdp + ')' : '') : _('not connected') + (why ? ': ' + why : '')) +
		(steps.length ? '. ' + _('Steps: %s').format(steps.join(' → ')) : '');
}
var CSS_VERSION = mm.VERSION, cutsOf = mm.cutsOf, zoneOf = mm.zoneOf, fillOf = mm.fillOf, sinrOf = mm.sinrOf, TECH_WORDS = mm.TECH_WORDS, text = mm.text, failText = mm.failText, duration = mm.duration, bytes = mm.bytes, techWords = mm.techWords, signalIcon = mm.signalIcon, opBadge = mm.opBadge, level = mm.level, CHEV = mm.CHEV, chev = mm.chev, pageHead = mm.pageHead, barsNode = mm.barsNode, setBars = mm.setBars, TONE_PILL = mm.TONE_PILL, secOpen = mm.secOpen, secRemember = mm.secRemember, num = mm.num, LANG = mm.LANG, secret = mm.secret, privacy = mm.privacy, ncRows = mm.ncRows, ncSummary = mm.ncSummary, ncTable = mm.ncTable, NC_ERROR = mm.NC_ERROR, dnsRows = mm.dnsRows, dnsSummary = mm.dnsSummary, dnsTable = mm.dnsTable, CHAIN_LINKS = mm.CHAIN_LINKS, CHAIN_WHY = mm.CHAIN_WHY, dnsDetail = mm.dnsDetail, cacheGet = mm.cacheGet, cacheSet = mm.cacheSet, knownLess = mm.knownLess, bannerShow = mm.bannerShow;
var smsLinked = mm.smsLinked, smsCode = mm.smsCode, copyText = mm.copyText, smsMatches = mm.smsMatches, smsOperator = mm.smsOperator;

var getDevice = rpc.declare({ object: 'medvemodem', method: 'device', params: ['id'], reject: true });
var handover = rpc.declare({ object: 'medvemodem', method: 'handover', params: ['id'], reject: true });
var recover = rpc.declare({ object: 'medvemodem', method: 'recover', params: ['id', 'level', 'wait', 'source'], reject: true });
/* the last resort by hand (the owner, 2026-11-09): the USB controller restarted, or a board's power line off and on */
var usbRescue = rpc.declare({ object: 'medvemodem', method: 'usb_rescue', params: ['id'], reject: true });
var usbRescueRun = rpc.declare({ object: 'medvemodem', method: 'usb_rescue_run', params: ['id', 'what', 'gpio', 'confirm'], reject: true });
var readRadio = rpc.declare({ object: 'medvemodem', method: 'radio', params: ['id', 'refresh'], reject: true });
var setRadio = rpc.declare({ object: 'medvemodem', method: 'radio_set', params: ['id', 'modes', 'bands', 'wait_s', 'cell_lock'], reject: true });
var confirmRadio = rpc.declare({ object: 'medvemodem', method: 'radio_confirm', params: ['id', 'action'], reject: true });
var getEvents = rpc.declare({ object: 'medvemodem', method: 'events', params: ['id', 'limit'], reject: true });
var atHistory = rpc.declare({ object: 'medvemodem', method: 'at_history', params: ['id', 'clear'], reject: true });
var getMetrics = rpc.declare({ object: 'medvemodem', method: 'metrics', params: ['id', 'max_age_ms'], reject: true });
var getCells = rpc.declare({ object: 'medvemodem', method: 'cells', params: ['id', 'levels'], reject: true });
/* the engine's AT reading (metrics) in ModemManager's terms: in common.js, the dashboard reads it too */
var atAsMm = mm.atAsMm, recoveryNotes = mm.recoveryNotes, execLong = mm.execLong;	/* «mm» is the reading inside apply(): the module by alias */
var bringup = rpc.declare({ object: 'medvemodem', method: 'bringup', params: ['id'], reject: true });
var check = rpc.declare({ object: 'medvemodem', method: 'check', params: ['id'], reject: true });
var getStatus = rpc.declare({ object: 'medvemodem', method: 'status', reject: true });
var signalHistory = rpc.declare({ object: 'medvemodem', method: 'signal_history', params: ['id', 'range'], reject: true });
/* eSIM reads/probes eUICC on demand (seconds); enabling an already installed profile is supported. Download/delete wait until Alpha 2. */
var esimStatus = rpc.declare({ object: 'medvemodem', method: 'esim_status', params: ['id'], reject: true });
var esimAction = rpc.declare({ object: 'medvemodem', method: 'esim_action', params: ['id', 'action'], reject: true });
/* a switch to a profile already on the chip (the owner, 2026-11-15); download and delete wait for Alpha 2 */
var esimEnable = rpc.declare({ object: 'medvemodem', method: 'esim_action', params: ['id', 'action', 'iccid'], reject: true });
var trafficHistory = rpc.declare({ object: 'medvemodem', method: 'traffic_history', params: ['id', 'range'], reject: true });
var probePorts = rpc.declare({ object: 'medvemodem', method: 'probe', params: ['id'], reject: true });
var modeswitchFix = rpc.declare({ object: 'medvemodem', method: 'modeswitch_fix', params: ['id'], reject: true });
var modeswitchRun = rpc.declare({ object: 'medvemodem', method: 'modeswitch_run', params: ['id'], reject: true });
var fccUnlock = rpc.declare({ object: 'medvemodem', method: 'fcc_unlock', params: ['id', 'enable'], reject: true });
var freezeModem = rpc.declare({ object: 'medvemodem', method: 'freeze', params: ['id', 'on', 'who'], reject: true });
var webOpen = rpc.declare({ object: 'medvemodem', method: 'web_open', params: ['id'], reject: true });
var webInfo = rpc.declare({ object: 'medvemodem', method: 'web_info', params: ['id'], reject: true });
var webDo = rpc.declare({ object: 'medvemodem', method: 'web_do', params: ['id', 'op', 'mode', 'lte_band'], reject: true });
var PKG = '/usr/libexec/medvemodem/pkg-install';
/* a title from the engine's AT and USSD presets (profiles/presets): English there, its Russian in this catalog */
function tr(s) { return s ? _(s) : s; }
/* an AT command's risk (the owner, 2026-11-14: «разделил бы класс риска и побочные эффекты»): read_only goes as it is;
 * disruptive (the link drops) and persistent (a setting stays after a restart) are asked first; dangerous is documentation
 * only. A preset says its own risk and effects (profiles/presets); a typed command is judged by its form: a radio off or a
 * detach drops the link, a value written into a setting persists; a read, a test (=?), Quectel's read of a named setting,
 * the report formats (+CREG=2, +CMEE=2, +COPS=3…) and the radio on are read_only; a restart the engine refuses itself.
 * The engine's hazard list still decides what is sent at all: a confirmation never widens it */
function atRisk(cmd, preset) {
	if (preset && preset.risk) return { risk: preset.risk, effects: list(preset.effects) };
	var r = { risk: 'read_only', effects: [] };
	String(cmd || '').toUpperCase().replace(/^\s*AT/, '').split(';').forEach(function(p) {
		p = p.trim().replace(/^AT/, '');
		if (!p) return;
		if (/^\+CFUN=[04](,|$)|^\+COPS=2$|^\+CGATT=0$|^\+CGACT=0(,|$)/.test(p)) {
			if (r.risk === 'read_only') r.risk = 'disruptive';
			if (r.effects.indexOf('drops_connection') < 0) r.effects.push('drops_connection');
			return;
		}
		if (p !== '^BAND_PREF' && (p.indexOf('=') < 0 || /\?$/.test(p) || /^\+Q[A-Z0-9_]+="[^"]*"$/.test(p) || /^\+CFUN=(1(,|$)|1[56]$)/.test(p) ||
		    /^\+(CREG|CGREG|CEREG|C5GREG|CMEE|CSCS|CMGF|CNMI|CSQ)=|^\+COPS=3(,|$)/.test(p))) return;
		r.risk = 'persistent';
		if (r.effects.indexOf('writes_nvram') < 0) r.effects.push('writes_nvram');
	});
	return r;
}
var PKG_WHY = { running: _('an installation is already running'), 'no package manager': _('the router has no package manager'), 'install failed': _('the installation failed') };
var STICK_WHY = { 'usb-modeswitch is not installed': _('the usb-modeswitch package is not installed'),
	'a mode switch ran less than 30 s ago': _('a mode switch ran less than 30 seconds ago'), 'a mode switch is running': _('a mode switch is already running'),
	'the device is not waiting for a mode switch': _('the modem is no longer in flash drive mode'), 'usb-mode.json has no rule for this device': _('/etc/usb-mode.json has no rule for this modem') };
/* SMS and USSD (spec 38): the engine reads and sends through ModemManager; USSD runs in the background and is polled */
var smsList = rpc.declare({ object: 'medvemodem', method: 'sms_list', params: ['id', 'refresh'], reject: true });
var smsSend = rpc.declare({ object: 'medvemodem', method: 'sms_send', params: ['id', 'number', 'text'], reject: true });
var smsDelete = rpc.declare({ object: 'medvemodem', method: 'sms_delete', params: ['id', 'sms'], reject: true });
var smsStorage = rpc.declare({ object: 'medvemodem', method: 'sms_storage', params: ['id'], reject: true });
var smsClear = rpc.declare({ object: 'medvemodem', method: 'sms_clear', params: ['id'], reject: true });
var smsCenter = rpc.declare({ object: 'medvemodem', method: 'sms_center', params: ['id', 'number'], reject: true });
var ussdStart = rpc.declare({ object: 'medvemodem', method: 'ussd_start', params: ['id', 'code'], reject: true });
var ussdRespond = rpc.declare({ object: 'medvemodem', method: 'ussd_respond', params: ['id', 'reply'], reject: true });
var ussdCancel = rpc.declare({ object: 'medvemodem', method: 'ussd_cancel', params: ['id'], reject: true });
var ussdStatus = rpc.declare({ object: 'medvemodem', method: 'ussd_status', params: ['id'], reject: true });
/* the SIM's PIN, or its PUK and a new PIN: sent through ModemManager, never kept by the engine */
var simUnlock = rpc.declare({ object: 'medvemodem', method: 'sim_unlock', params: ['id', 'pin', 'confirm'], reject: true });
var simSlot = rpc.declare({ object: 'medvemodem', method: 'sim_slot', params: ['id', 'slot'], reject: true });
/* «Перерегистрация в сети» (the owner, 2026-11-06): ModemManager's register-home, or AT+COPS=2/0 over the engine's port */
var reregister = rpc.declare({ object: 'medvemodem', method: 'reregister', params: ['id'], reject: true });
/* the SIM's forbidden networks (EF_FPLMN), read or cleared over the engine's AT port (the owner, 2026-11-06) */
var fplmn = rpc.declare({ object: 'medvemodem', method: 'fplmn', params: ['id', 'clear'], reject: true });
/* «+CRSM: 144,0,"52F01062F220FFFFFFFFFFFF"» → ['25001', '25002']: three BCD bytes a network (TS 31.102 4.2.16), the
 * digits swapped in each byte, F for nothing; null when the answer is not the file */
function fplmnList(lines) {
	var m = null;
	(lines || []).some(function(l) { m = /^\+CRSM:\s*(\d+),(\d+),"?([0-9A-Fa-f]*)"?/.exec(String(l)); return !!m; });
	if (!m || m[1] !== '144') return null;
	var hex = m[3].toUpperCase(), out = [];
	for (var i = 0; i + 6 <= hex.length; i += 6) {
		var b = hex.substr(i, 6);
		if (b === 'FFFFFF') continue;
		var mcc = b[1] + b[0] + b[3], mnc = b[5] + b[4] + (b[2] === 'F' ? '' : b[2]);
		if (/^\d{3}$/.test(mcc) && /^\d{2,3}$/.test(mnc)) out.push(mcc + mnc);
	}
	return out;
}
var simUnlockPuk = rpc.declare({ object: 'medvemodem', method: 'sim_unlock', params: ['id', 'pin', 'puk', 'confirm'], reject: true });
/* the AT console (spec 11.5) and the presets that fit the modem, with the USSD codes of its SIM's operator */
var getPresets = rpc.declare({ object: 'medvemodem', method: 'presets', params: ['id'], reject: true });
var atCommand = rpc.declare({ object: 'medvemodem', method: 'at_command', params: ['id', 'command'], reject: true });
var uciDelete = rpc.declare({ object: 'uci', method: 'delete', params: ['config', 'section'], reject: true });
/* the modem's settings live in the medvemodem package (config device / option id), read on the fly by the engine */
var uciGet = rpc.declare({ object: 'uci', method: 'get', params: ['config', 'type'], reject: true });
var uciAdd = rpc.declare({ object: 'uci', method: 'add', params: ['config', 'type', 'values'], reject: true });
var uciSet = rpc.declare({ object: 'uci', method: 'set', params: ['config', 'section', 'values'], reject: true });
var uciDelOption = rpc.declare({ object: 'uci', method: 'delete', params: ['config', 'section', 'option'], reject: true });
var uciCommit = rpc.declare({ object: 'uci', method: 'commit', params: ['config'], reject: true });
/* the interface up or down through the engine, so its journal says who did it (audit 2026-10-13) */
var connectIface = rpc.declare({ object: 'medvemodem', method: 'connect', params: ['id', 'up', 'who'], reject: true });


function words(value) { return typeof value === 'string' && value !== '--' ? value : ''; }
/* one conversation per subscriber (HANDOFF 138): the network gives «+79…», a sent one «89…» or «7 (900) …»;
 * a Russian 8/7 + 10 digits and any long number become «+digits», a short or letter sender stays as it is */
function smsThreadKey(value) {
	var w = words(value), d = w.replace(/[\s()\-]/g, '');
	if (!/^\+?\d{6,}$/.test(d)) return w || '—';
	d = d.replace(/^\+/, '');
	if (d.length === 11 && d.charAt(0) === '8') d = '7' + d.slice(1);
	return d.length >= 11 ? '+' + d : d;
}
function list(value) { return Array.isArray(value) ? value : []; }
/* the address book (the owner, 2026-10-26: «записную книжку с импортом .vcf, но ограничил бы 20 номерами»): a «name;number»
 * list with a picker when sending */
var CONTACTS_MAX = 20;
/* quoted-printable (old Android and Nokia exports: N;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:=D0=98=D0=B2...) in the
 * card's CHARSET (audit r101 R05: a WINDOWS-1251 name read as UTF-8 was saved as «����»); null for a charset this
 * browser does not know, so nothing broken is saved */
function qpDecode(v, charset) {
	var bytes = [], i;
	for (i = 0; i < v.length; i++) {
		if (v[i] === '=' && /^[0-9A-Fa-f]{2}$/.test(v.substr(i + 1, 2))) { bytes.push(parseInt(v.substr(i + 1, 2), 16)); i += 2; }
		else bytes.push(v.charCodeAt(i) & 0xff);
	}
	try { return new TextDecoder(charset || 'utf-8', { fatal: true }).decode(new Uint8Array(bytes)); } catch (e) { return null; }
}
/* text only (the owner, 2026-10-26: «фото и qr не надо»): the name (FN, else N), a description (NOTE, else ORG) and the
 * first TEL, a mobile one preferred; a photo or anything else of the card is skipped */
function vcfContacts(textIn) {
	/* folded lines (a space or a tab first) join their line; a quoted-printable value ending with «=» goes on in the next
	 * line (only there: a photo's base64 ends with «=» too) */
	var raw = String(textIn || '').replace(/\r?\n[ \t]/g, '').split(/\r?\n/), lines = [], out = [], card = null, i;
	for (i = 0; i < raw.length; i++) {
		var l = raw[i];
		if (/ENCODING=QUOTED-PRINTABLE/i.test(l.split(':')[0]))
			while (/=$/.test(l) && i + 1 < raw.length) l = l.slice(0, -1) + raw[++i];
		lines.push(l);
	}
	lines.forEach(function(line) {
		var m = /^([^:]+):(.*)$/.exec(line), head, key, val;
		if (/^BEGIN:VCARD/i.test(line)) { card = { tels: [] }; return; }
		if (/^END:VCARD/i.test(line)) { if (card) out.push(card); card = null; return; }
		if (!card || !m) return;
		head = m[1].split(';'); key = head[0].replace(/^item\d+\./i, '').toUpperCase(); val = m[2];
		if (/ENCODING=QUOTED-PRINTABLE/i.test(m[1])) {
			var cs = /CHARSET=([A-Za-z0-9_-]+)/i.exec(m[1]);
			val = qpDecode(val, cs ? cs[1] : null);
			if (val === null) return;	/* not decodable: the field is left out */
		}
		val = val.replace(/\\([,;\\])/g, '$1').replace(/\\n/gi, ' ').trim();
		if (key === 'FN') card.fn = val;
		/* N: family;given;additional;prefix;suffix — said «Иван Иванович Иванов» (audit r101 R05: the reverse of all of it
		 * read «Иванович Иван Иванов») */
		else if (key === 'N') { var n = val.split(';'); card.n = [n[3], n[1], n[2], n[0], n[4]].filter(Boolean).join(' '); }
		else if (key === 'NOTE') card.note = val;
		else if (key === 'ORG') card.org = val.split(';').filter(Boolean).join(', ');
		else if (key === 'TEL') card.tels.push({ value: val.replace(/^tel:/i, ''), cell: /CELL|MOBILE/i.test(m[1]) });
	});
	return out.map(function(c) {
		var tel = c.tels.filter(function(x) { return x.cell; })[0] || c.tels[0];
		return { name: (c.fn || c.n || '').replace(/[\x00-\x1f]/g, ' ').slice(0, 40), description: (c.note || c.org || '').replace(/[\x00-\x1f]/g, ' ').slice(0, 80),
			number: tel ? tel.value.replace(/[\s()\-.]/g, '') : '' };
	}).filter(function(c) { return /^\+?\d{3,20}$/.test(c.number); });
}
function rate(bps) {
	if (typeof bps !== 'number' || bps < 0) return '—';
	var bits = bps * 8;
	return bits >= 1e6 ? _('%s Mbit/s').format(num(bits / 1e6, bits >= 1e7 ? 0 : 1)) : _('%s kbit/s').format(Math.round(bits / 1e3));
}
/* ---- «История приёма» (spec 25.9.13; the owner's design 2026-10-21): one filled relief with a switch above it —
 * «Уровень сигнала», «Помехи и качество», «Технология сети» — under one ribbon of the network's technology, a dashed
 * rule with «5G → 4G» at every switch, gaps where nothing was measured; never a second y-axis ---- */
var HIST_RANGES = [['5m', _('5 min'), 300], ['1h', _('Hour'), 3600], ['24h', _('Day'), 86400]];
var HIST_MODES = [['level', _('Signal level')], ['quality', _('Noise and quality')], ['tech', _('Network technology')]];
var HIST_EVENT = { tech: _('Technology'), cell: _('Cell'), radio: _('Network settings') };
/* the modem's journal entries of its network settings shown on the history (a cell lock, its guard and rollback) */
var RADIO_KINDS = { 'radio-change': 1, 'radio-kept': 1, 'radio-rollback': 1, 'radio-guard': 1, 'band-guard': 1 };
/* the diagnosis by chain: the links and the reasons in words (chain.c) */
var tempOf = mm.tempOf, profileTitle = mm.profileTitle;
/* ModemManager's words for the radio, as people say them: «lte, 5gnr» -> «LTE + 5G» */
function techSay(t) {
	var w = { lte: 'LTE', '5gnr': '5G', umts: '3G', hsdpa: '3G', hsupa: '3G', hspa: '3G', 'hspa-plus': '3G+', gsm: '2G', edge: '2G', gprs: '2G' };
	return String(t || '').split(/,\s*/).filter(Boolean).map(function(x) { return w[x] || x.toUpperCase(); })
		.filter(function(x, i, a) { return a.indexOf(x) === i; }).join(' + ');
}
function eventSay(e) { return e.kind === 'tech' ? String(e.detail).split(' → ').map(techSay).join(' → ') : e.detail; }
function hhmm(t) { return new Date(t * 1000).toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit' }); }
/* what a chart may show: a modem's «no value» (-32768 and the like) is not a sample (the owner's T99, 2026-10-21) */
var HIST_SANE = { rsrp: [-160, -20], sinr: [-30, 60] };
/* the network's technology as a level: 2G 1 … 5G 4; the ribbon's and the step chart's colour by it */
var TECH_LV = { 1: '2G', 2: '3G', 3: '4G', 4: '5G' };
function techLevel(t) {
	t = String(t || '');
	return /5gnr|\bnr\b/.test(t) ? 4 : /lte/.test(t) ? 3 : /umts|hspa|hsdpa|hsupa|wcdma|evdo|cdma/.test(t) ? 2 : /gsm|edge|gprs/.test(t) ? 1 : 0;
}
var LEVEL_WORDS = [_('Weak'), _('Fair'), _('Good'), _('Excellent')];
var NOISE_WORDS = [_('Strong noise'), _('Noticeable noise'), _('Little noise'), _('Clean signal')];
/*
 * The samples as the chart reads them: time, technology level, and the one value that speaks for the connection —
 * the 5G cell's on 5G (NSA or SA) when it is there, else LTE's — with the technology its grade is judged by
 * («оценка с учётом технологии»: 5G's thresholds are not LTE's).
 */
function histSamples(h) {
	var now = h.now, span = h.span, step = h.step_s || 15, lv = 0;
	var ok = function(v, k) { return typeof v === 'number' && v >= HIST_SANE[k][0] && v <= HIST_SANE[k][1] ? v : null; };
	return list(h.points).filter(function(p) { return typeof p.t === 'number' && p.t >= now - span && p.t <= now + step; }).map(function(p) {
		var l = techLevel(p.tech) || lv, nr = ok(p.nr_rsrp, 'rsrp') !== null, use = l === 4 && nr ? 'nr' : ok(p.lte_rsrp, 'rsrp') !== null ? 'lte' : nr ? 'nr' : null;
		lv = l;
		return { t: p.t, lv: l, tech: p.tech || '', rat: use === 'nr' ? 'nr' : 'lte',
			rsrp: use ? ok(p[use + '_rsrp'], 'rsrp') : null, sinr: ok(p[(use || 'lte') + '_sinr'], 'sinr') };
	});
}
/* the value a mode draws, 0…100 on the grade scale (25 per zone, as the meters), or null */
function histScore(mode, q) {
	if (mode === 'tech') return q.lv ? q.lv * 25 : null;
	var v = mode === 'level' ? q.rsrp : q.sinr;
	return v === null ? null : fillOf(mode === 'level' ? 'rsrp' : 'sinr', v, q.rat);
}
function histGrade(mode, q) {
	if (mode === 'tech') return TECH_LV[q.lv] || '';
	var v = mode === 'level' ? q.rsrp : q.sinr;
	return v === null ? '' : (mode === 'level' ? LEVEL_WORDS : NOISE_WORDS)[zoneOf(mode === 'level' ? 'rsrp' : 'sinr', v, q.rat)];
}
function histValue(mode, q) {
	var n = function(v) { return num(v).replace('-', '−'); };
	return mode === 'level' ? (q.rsrp === null ? '' : _('RSRP %s dBm').format(n(q.rsrp))) : mode === 'quality' ? (q.sinr === null ? '' : _('SINR %s dB').format(n(q.sinr))) : '';
}
/* the widest of some words as an axis tick draws them: a hidden SVG text of the tick's class in the page (a canvas measured
 * another font where system-ui and sans-serif differ: the owner's build runner, r131) */
function textWidth(words) {
	var NS = 'http://www.w3.org/2000/svg', svg = document.createElementNS(NS, 'svg'), host = document.querySelector('.mm-root') || document.body;
	svg.setAttribute('style', 'position:absolute;visibility:hidden;width:0;height:0;overflow:hidden');
	try {
		host.appendChild(svg);
		return words.reduce(function(m, w) {
			var t = document.createElementNS(NS, 'text');
			t.setAttribute('class', 'mm-hist-tick');
			t.textContent = String(w || '');
			svg.appendChild(t);
			return Math.max(m, t.getComputedTextLength ? t.getComputedTextLength() : 0);
		}, 0);
	} catch (e) { return 0; }
	finally { if (svg.parentNode) svg.parentNode.removeChild(svg); }
}
/* one chart: the ribbon, the switches, the relief and both axes; the hover is wired by the card (applyHistory) */
function reliefPanel(h, mode, width) {
	var NS = 'http://www.w3.org/2000/svg', W = Math.max(280, Math.round(width)), L = W < 420 ? 74 : 104, R = 12;
	/* the left axis as wide as its longest word in the page's language: «Нормальный», «Сильные помехи» were cut at the
	 * phone's 74 px (footstrap on a phone, the owner 2026-11-02) */
	var wordsLeft = mode === 'level' ? LEVEL_WORDS : mode === 'quality' ? [_('Strong noise'), _('Some noise'), _('Clean signal')] : Object.keys(TECH_LV).map(function(k) { return TECH_LV[k]; });
	L = Math.max(L, Math.ceil(textWidth(wordsLeft)) + 14);
	var RB = 4, RH = 18, CY = RB + RH + 6, CT = CY + 22, CH = 150, H = CT + CH + 22, pw = W - L - R;
	var now = h.now, span = h.span, step = h.step_s || 15, gap = Math.max(step * 2.5, 90), qs = histSamples(h);
	var x = function(t) { return L + (t - (now - span)) / span * pw; }, y = function(v) { return CT + CH - v / 100 * CH; };
	var svg = document.createElementNS(NS, 'svg');
	svg.setAttribute('width', String(W)); svg.setAttribute('height', String(H));
	svg.setAttribute('class', 'mm-hist-svg mm-relief mm-relief-' + mode); svg.setAttribute('role', 'img');
	function el(name, attrs, parent, txt) {
		var e = document.createElementNS(NS, name);
		Object.keys(attrs).forEach(function(k) { e.setAttribute(k, attrs[k]); });
		if (txt != null) e.textContent = txt;
		(parent || svg).appendChild(e);
		return e;
	}
	/* each sample holds until the next one, or for one step when the next is past a gap */
	var end = function(i) { var n = qs[i + 1]; return n && n.t - qs[i].t <= gap ? n.t : Math.min(now, qs[i].t + step); };
	/* the grade's bands and their words */
	var bands = mode === 'level' ? [[12.5, LEVEL_WORDS[0]], [37.5, LEVEL_WORDS[1]], [62.5, LEVEL_WORDS[2]], [87.5, LEVEL_WORDS[3]]] :
		mode === 'quality' ? [[12.5, _('Strong noise')], [50, _('Some noise')], [87.5, _('Clean signal')]] :
		[1, 2, 3, 4].filter(function(l) { return qs.some(function(q) { return q.lv === l; }) || l === 3; }).map(function(l) { return [l * 25, TECH_LV[l]]; });
	(mode === 'quality' ? [25, 75] : mode === 'level' ? [25, 50, 75] : bands.map(function(b) { return b[0]; })).forEach(function(v) {
		el('line', { x1: L, x2: L + pw, y1: y(v).toFixed(1), y2: y(v).toFixed(1), 'class': 'mm-hist-grid' });
	});
	el('line', { x1: L, x2: L + pw, y1: CT + CH, y2: CT + CH, 'class': 'mm-hist-base' });
	bands.forEach(function(b) { el('text', { x: L - 8, y: (y(b[0]) + 4).toFixed(1), 'text-anchor': 'end', 'class': 'mm-hist-tick' }, null, b[1]); });
	/* five times on a wide chart, three on a phone: «11:45 PM» and «12:00 AM» ran into each other at 390 px (the owner's
	 * phone, 2026-11-01) */
	(pw < 360 ? [0, 0.5, 1] : [0, 0.25, 0.5, 0.75, 1]).forEach(function(f) {
		var t = now - span + f * span;
		el('text', { x: x(t).toFixed(1), y: H - 6, 'text-anchor': f === 0 ? 'start' : f === 1 ? 'end' : 'middle', 'class': 'mm-hist-tick' }, null, f === 1 ? _('now') : hhmm(t));
	});
	/* the ribbon: one run per technology, named inside when it fits */
	el('rect', { x: L, y: RB, width: pw.toFixed(1), height: RH, rx: 4, 'class': 'mm-tr-none' });
	for (var i = 0, run = null; i <= qs.length; i++) {
		var q = qs[i], joined = run && q && q.lv === run.lv && q.t - run.last <= gap;
		if (run && !joined) {
			var x0 = x(run.from), x1 = Math.max(x0 + 2, x(run.to));
			el('rect', { x: x0.toFixed(1), y: RB, width: (x1 - x0).toFixed(1), height: RH, rx: 4, 'class': 'mm-tr mm-t' + (run.lv + 1) });
			if (x1 - x0 > 30 && TECH_LV[run.lv]) el('text', { x: ((x0 + x1) / 2).toFixed(1), y: RB + RH - 5, 'text-anchor': 'middle', 'class': 'mm-tr-label' }, null, TECH_LV[run.lv]);
			run = null;
		}
		if (!q || !q.lv) continue;
		if (!run) run = { lv: q.lv, from: q.t, to: end(i), last: q.t };
		else { run.to = end(i); run.last = q.t; }
	}
	/* a switch of technology between two samples with no gap: a dashed rule over the ribbon and the chart, named once */
	var chipAt = -1e9;
	qs.forEach(function(q, i) {
		var prev = qs[i - 1];
		if (!prev || !prev.lv || !q.lv || prev.lv === q.lv || q.t - prev.t > gap) return;
		var xs = x(q.t);
		el('line', { x1: xs.toFixed(1), x2: xs.toFixed(1), y1: RB, y2: CT + CH, 'class': 'mm-hist-switch' });
		if (xs - chipAt < 70 || xs < L + 26 || xs > L + pw - 26) return;
		chipAt = xs;
		var g = el('g', { 'class': 'mm-hist-chip' });
		el('rect', { x: (xs - 26).toFixed(1), y: CY, width: 52, height: 17, rx: 4 }, g);
		el('text', { x: xs.toFixed(1), y: CY + 12.5, 'text-anchor': 'middle' }, g, TECH_LV[prev.lv] + ' → ' + TECH_LV[q.lv]);
	});
	/* a serving-cell change: a fainter dotted rule in the chart, named in its title */
	list(h.events).forEach(function(e) {
		if ((e.kind !== 'cell' && e.kind !== 'radio') || e.t < now - span) return;
		var l = el('line', { x1: x(e.t).toFixed(1), x2: x(e.t).toFixed(1), y1: CT, y2: CT + CH, 'class': 'mm-hist-event' });
		el('title', {}, l, hhmm(e.t) + ' · ' + (HIST_EVENT[e.kind] || e.kind) + ': ' + eventSay(e));
	});
	if (!qs.length) {
		el('text', { x: L + pw / 2, y: CT + CH / 2, 'text-anchor': 'middle', 'class': 'mm-hist-empty' }, null, _('No data for this period'));
		return { node: svg, qs: qs, x: x };
	}
	/* where the network was there but this measure is not (3G has no SINR): said, not drawn as a bad value */
	if (mode !== 'tech') {
		for (var k = 0, hole = null; k <= qs.length; k++) {
			var m = qs[k], none = !!(m && m.lv && histScore(mode, m) === null);
			if (none && !hole) hole = { from: m.t };
			if (none) hole.to = end(k);
			if (hole && !none) {
				var hx0 = x(hole.from), hx1 = x(hole.to);
				if (hx1 - hx0 > 3) {
					el('rect', { x: hx0.toFixed(1), y: CT, width: (hx1 - hx0).toFixed(1), height: CH, 'class': 'mm-hist-nodata' });
					if (hx1 - hx0 > 120) el('text', { x: ((hx0 + hx1) / 2).toFixed(1), y: CT + CH / 2, 'text-anchor': 'middle', 'class': 'mm-hist-empty' }, null, _('No comparable data'));
				}
				hole = null;
			}
		}
	}
	/* the relief: steps, filled to the baseline; a gap, a missing value or (by technology) a switch ends a piece */
	var pieces = [], cur = null;
	qs.forEach(function(q, i) {
		var v = histScore(mode, q), cont = cur && v !== null && q.t - qs[i - 1].t <= gap && (mode !== 'tech' || q.lv === cur.lv);
		if (!cont && cur) { pieces.push(cur); cur = null; }
		if (v === null) return;
		if (!cur) cur = { lv: q.lv, steps: [] };
		cur.steps.push([x(q.t), Math.max(x(end(i)), x(q.t) + 1.5), y(v)]);
	});
	if (cur) pieces.push(cur);
	pieces.forEach(function(pc) {
		var st = pc.steps, cls = mode === 'tech' ? ' mm-t' + (pc.lv + 1) : '';
		var top = st.map(function(s, i) { return (i ? 'L' : 'M') + s[0].toFixed(1) + ' ' + s[2].toFixed(1) + 'L' + s[1].toFixed(1) + ' ' + s[2].toFixed(1); }).join('');
		el('path', { d: top + 'L' + st[st.length - 1][1].toFixed(1) + ' ' + (CT + CH) + 'L' + st[0][0].toFixed(1) + ' ' + (CT + CH) + 'Z', 'class': 'mm-relief-area' + cls });
		el('path', { d: top, 'class': 'mm-relief-line' + cls });
	});
	var cross = el('line', { y1: RB, y2: CT + CH, 'class': 'mm-hist-cross', visibility: 'hidden' });
	var dot = el('circle', { r: 4.5, 'class': 'mm-relief-dot', visibility: 'hidden' });
	var hit = el('rect', { x: L, y: RB, width: pw.toFixed(1), height: CT + CH - RB, fill: 'transparent' });
	return { node: svg, qs: qs, x: x, hit: hit, cross: cross, dot: dot, y: function(q) { var v = histScore(mode, q); return v === null ? null : y(v); } };
}
/* a rate in bits per second as people read it: «7,0 Мбит/с», «640 кбит/с» (spec §42) */
function bitrate(b) {
	if (typeof b !== 'number' || b < 0) return '—';
	if (b >= 1e6) return _('%s Mbit/s').format(num(b / 1e6, b >= 1e7 ? 0 : 1));
	return _('%s kbit/s').format(num(Math.round(b / 1e3)));
}
/* the top of a rate axis: 1, 2 or 5 times a power of ten, at least 100 kbit/s */
function rateTop(v) {
	var m = Math.max(v, 1e5), p = Math.pow(10, Math.floor(Math.log(m) / Math.LN10));
	return [1, 2, 5, 10].map(function(k) { return k * p; }).filter(function(t) { return t >= m; })[0];
}
/* the traffic chart (spec §42): one direction at a time, chosen as the history's modes are (the owner, 2026-11-03: two
 * panels at once were too much), the average as the filled steps and the peak as a thin line above it; a gap is a gap */
var TRAF_DIRS = [['rx', _('Received')], ['tx', _('Sent')]];
function trafficPanel(h, width, k) {
	var NS = 'http://www.w3.org/2000/svg', W = Math.max(280, Math.round(width)), R = 12;
	var now = h.now, span = h.span, step = h.step_s || 60, gap = Math.max(step * 2.5, 15), pts = list(h.points).filter(function(p) {
		return typeof p.t === 'number' && p.t >= now - span && p.t <= now + step; });
	var top_ = rateTop(pts.reduce(function(m, p) { return Math.max(m, p[k + '_peak_bps'] || 0, p[k + '_bps'] || 0); }, 0));
	var L = Math.max(W < 420 ? 64 : 84, Math.ceil(textWidth([bitrate(top_), bitrate(top_ / 2)])) + 14);
	var PH = 170, T0 = 10, H = T0 + PH + 22, pw = W - L - R;
	var x = function(t) { return L + (t - (now - span)) / span * pw; }, y = function(v) { return T0 + PH - Math.min(v, top_) / top_ * PH; };
	var svg = document.createElementNS(NS, 'svg');
	svg.setAttribute('width', String(W)); svg.setAttribute('height', String(H));
	svg.setAttribute('class', 'mm-hist-svg mm-traf-svg mm-traf-' + k); svg.setAttribute('role', 'img');
	function el(name, attrs, parent, txt) {
		var e = document.createElementNS(NS, name);
		Object.keys(attrs).forEach(function(a) { e.setAttribute(a, attrs[a]); });
		if (txt != null) e.textContent = txt;
		(parent || svg).appendChild(e);
		return e;
	}
	var end = function(i) { var n = pts[i + 1]; return n && n.t - pts[i].t <= gap ? n.t : Math.min(now, pts[i].t + step); };
	[top_, top_ / 2].forEach(function(v) { el('line', { x1: L, x2: L + pw, y1: y(v).toFixed(1), y2: y(v).toFixed(1), 'class': 'mm-hist-grid' }); });
	el('line', { x1: L, x2: L + pw, y1: T0 + PH, y2: T0 + PH, 'class': 'mm-hist-base' });
	[[top_, bitrate(top_)], [top_ / 2, bitrate(top_ / 2)], [0, '0']].forEach(function(t) {
		el('text', { x: L - 8, y: (y(t[0]) + 4).toFixed(1), 'text-anchor': 'end', 'class': 'mm-hist-tick' }, null, t[1]);
	});
	var pieces = [], cur = null;
	pts.forEach(function(p, i) {
		if (cur && p.t - pts[i - 1].t > gap) { pieces.push(cur); cur = null; }
		if (!cur) cur = [];
		cur.push([x(p.t), Math.max(x(end(i)), x(p.t) + 1.5), y(p[k + '_bps'] || 0), y(p[k + '_peak_bps'] || 0)]);
	});
	if (cur) pieces.push(cur);
	pieces.forEach(function(st) {
		var line = function(j) { return st.map(function(q, i) { return (i ? 'L' : 'M') + q[0].toFixed(1) + ' ' + q[j].toFixed(1) + 'L' + q[1].toFixed(1) + ' ' + q[j].toFixed(1); }).join(''); };
		var avg = line(2);
		el('path', { d: avg + 'L' + st[st.length - 1][1].toFixed(1) + ' ' + (T0 + PH) + 'L' + st[0][0].toFixed(1) + ' ' + (T0 + PH) + 'Z', 'class': 'mm-relief-area mm-traf-' + k });
		el('path', { d: avg, 'class': 'mm-relief-line mm-traf-' + k });
		if (h.step_s > 5) el('path', { d: line(3), 'class': 'mm-traf-peak mm-traf-' + k });
	});
	(pw < 360 ? [0, 0.5, 1] : [0, 0.25, 0.5, 0.75, 1]).forEach(function(f) {
		var t = now - span + f * span;
		el('text', { x: x(t).toFixed(1), y: H - 6, 'text-anchor': f === 0 ? 'start' : f === 1 ? 'end' : 'middle', 'class': 'mm-hist-tick' }, null, f === 1 ? _('now') : hhmm(t));
	});
	if (!pts.length) {
		el('text', { x: L + pw / 2, y: T0 + PH / 2, 'text-anchor': 'middle', 'class': 'mm-hist-empty' }, null, _('No data for this period'));
		return { node: svg, pts: pts, x: x };
	}
	var cross = el('line', { y1: T0, y2: T0 + PH, 'class': 'mm-hist-cross', visibility: 'hidden' });
	var dot = el('circle', { r: 4.5, 'class': 'mm-relief-dot', visibility: 'hidden' });
	var hit = el('rect', { x: L, y: T0, width: pw.toFixed(1), height: PH, fill: 'transparent' });
	return { node: svg, pts: pts, x: x, y: y, hit: hit, cross: cross, dot: dot };
}
/* a carrier's key from the history ("lte-3", "5gnr-78", "lte-3#2") as people name it: «B3», «n78», «B3 (2)» */
function carrierName(k) {
	var m = /^([a-z0-9]+?)(?:-(\d+))?(?:#(\d+))?$/.exec(String(k)) || [];
	var b = m[2] ? (m[1] === '5gnr' ? 'n' + m[2] : m[1] === 'lte' ? 'B' + m[2] : (TECH_WORDS[m[1]] || m[1].toUpperCase()) + ' B' + m[2]) :
		(TECH_WORDS[m[1]] || String(k));
	return b + (m[3] ? ' (' + m[3] + ')' : '');
}
function carrierOrder(a, b) {
	var ra = /^5gnr/.test(a) ? 1 : 0, rb = /^5gnr/.test(b) ? 1 : 0, na = +(/-(\d+)/.exec(a) || [0, 0])[1], nb = +(/-(\d+)/.exec(b) || [0, 0])[1];
	return ra - rb || na - nb || (a < b ? -1 : a > b ? 1 : 0);
}
/* the operator's gigabytes: decimal, as the SIM's tariff counts them (spec 20.9) */
function gb(n) { return typeof n !== 'number' ? '—' : _('%s GB').format(n / 1e9 < 10 ? num(n / 1e9, 1) : String(Math.round(n / 1e9))); }
/* «5 November», «5 ноября»: the day and the month in the page's language */
/* the day's time without the internet (the owner, 2026-10-27): the outages, the whitelist-only time apart; '' until the
 * engine counted anything */
function offlineWords(dt, lvl) {
	if (!dt || !(dt.ok + dt.limited + dt.none + dt.off)) return '';
	var gone = dt.none + dt.off, out = [];
	out.push(!gone ? _('not once') : N_(dt.outages || 1, '%s, %d time', '%s, %d times').format(duration(gone), dt.outages || 1) +
		(lvl !== 'normal' && dt.outages > 1 ? ' · ' + _('the longest %s').format(duration(dt.longest)) : ''));
	if (dt.current >= 60) out.push(_('now without it for %s').format(duration(dt.current)));
	if (dt.limited) out.push(_('only the whitelist: %s').format(duration(dt.limited)));
	return out.join(' · ');
}
function onDate(iso) { var m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(iso || ''); return m ? new Date(+m[1], +m[2] - 1, +m[3]).toLocaleDateString(LANG, { day: 'numeric', month: 'long' }) : ''; }
function quotaWords(q) {
	if (!q || q.state === 'none' || typeof q.limit_bytes !== 'number') return '';
	return _('%s of %s (%s %%)').format(gb(q.used_bytes), gb(q.limit_bytes), q.percent) + (q.state === 'exceeded' ? ' — ' + _('used up') : '') +
		(onDate(q.resets_on) ? ' · ' + _('a new period from %s').format(onDate(q.resets_on)) : '');
}
function totals(b) { return b && typeof b.rx_bytes === 'number' ? '↓ ' + bytes(b.rx_bytes) + ' · ↑ ' + bytes(b.tx_bytes) : '—'; }
function ago(s) { return typeof s !== 'number' || s < 0 ? '' : s < 60 ? _('%d s ago').format(s) : _('%d min ago').format(Math.floor(s / 60)); }
/*
 * The shape of a speed test (the owner, 2026-11-03: «спидтест с предсказанием шейпа и советами», after a
 * WH3000 Pro owner's report): per-second rates in kbit/s and the signal. «shelf»: a flat speed at a round number — an operator's cap
 * (tariff, tethering or a used-up quota); «cut»: fast for a few seconds, then cut down and flat — a cap that starts after
 * the first megabytes; «dead»: almost nothing; «radio»: wandering with a weak signal; «ok»: no sign of a cap.
 */
var SPEED_ROUND = [64, 128, 256, 512, 1000, 1024, 2000, 2048, 3000, 4000, 5000, 6000, 8000, 10000, 12000, 15000, 16000, 20000, 25000, 30000, 50000];
function speedShape(kbps, sig) {
	var all = list(kbps).filter(function(x) { return typeof x === 'number' && x >= 0; });
	if (all.length < 3) return { kind: 'none' };
	var tail = all.slice(1), avg = function(a) { return a.reduce(function(s, x) { return s + x; }, 0) / (a.length || 1); };
	var mean = avg(tail), peak = Math.max.apply(null, all);
	var sd = Math.sqrt(avg(tail.map(function(x) { return (x - mean) * (x - mean); }))), cv = mean ? sd / mean : 0;
	var early = avg(all.slice(0, 3)), late = all.slice(-3), lateMean = avg(late);
	var lateSd = Math.sqrt(avg(late.map(function(x) { return (x - lateMean) * (x - lateMean); }))), lateCv = lateMean ? lateSd / lateMean : 1;
	var near = function(m) { return SPEED_ROUND.filter(function(r) { return Math.abs(m - r) / r < 0.1; })[0]; };
	var weak = sig && ((typeof sig.rsrp === 'number' && sig.rsrp < -112) || (typeof sig.sinr === 'number' && sig.sinr < 0));
	var o = { mean: Math.round(mean), peak: Math.round(peak) };
	if (mean < 50) o.kind = 'dead';
	else if (all.length >= 6 && early > 2.5 * lateMean && lateCv < 0.15) { o.kind = 'cut'; o.cap = Math.round(lateMean); }
	else if (cv < 0.1 && near(mean) && mean < 20000) { o.kind = 'shelf'; o.cap = near(mean); }
	else if (weak) o.kind = 'radio';
	else o.kind = 'ok';
	return o;
}
function kbpsWords(k) { return k >= 1000 ? _('%s Mbit/s').format(num(Math.round(k / 100) / 10)) : _('%d kbit/s').format(Math.round(k)); }

function checkWords(r) {
	if (!r || typeof r.ok !== 'boolean') return '';
	var target = String(r.target || '').replace(/^\[|\]?:\d+$/g, '');
	if (r.limited) return _('%s: access to the internet is limited: only %s from the second group of addresses answers; the operator may restrict access').format(r.device, target) +
		(ago(r.age_s) ? ' (' + _('updated %s').format(ago(r.age_s)) + ')' : '');
	var err = mm.reachError(r.error);
	return r.device + ', ' + target + ': ' + (r.ok ? _('%d ms').format(r.latency_ms) : _('no answer') + (err && err !== 'no answer' ? ' (' + err + ')' : '')) +
		(ago(r.age_s) ? (r.ok ? ' (' + _('updated %s').format(ago(r.age_s)) + ')' : '; ' + _('updated %s').format(ago(r.age_s))) : '');
}
/* «Базовый»: the check as a person says it, as on the dashboard */
/* the card's words (common.js, audit r101 R12): «Интернет · 57 мс», an old result said as out of date */
function checkPlain(r, role) {
	var s = mm.checkState(r, role);
	return s ? s.text : '';
}

var ROLES = { carrying: _('Main channel'), backup_ready: _('Backup channel, ready'), backup_not_ready: _('Backup channel, not ready'),
	no_internet: _('No internet access'), checking: _('Connection check'), down: _('Not connected'),
	limited: _('Limited access'), backup_limited: _('Backup channel, limited access') };
var HEALTH = { ok: _('Normal'), watching: _('Under watch'), recovering: _('Recovery'), stalled: _('A check by hand is required') };
var STABILITY = { stable: _('Stable'), degraded: _('Failures recorded'), flapping: _('Frequent reconnects') };
var REC_STATE = { idle: _('Not running'), running: _('A step is running'), cooldown: _('A pause after an error'), manual_required: _('A check by hand is needed') };
/* the engine's reasons (recovery.c) stay English in logs and the CLI; the page says them in Russian */
var WHY = [
	[/does not answer USSD/, _('the modem\'s firmware does not answer USSD')],
	/* r94: the cell lock, the FM350's GTACT and a HiLink's SMS through its web */
	[/cell lock could not be read/, _('this modem\'s cell lock could not be read')],
	[/a technology this modem cannot lock/, _('this modem cannot lock a cell of this kind')],
	[/cell lock is set over the engine's own AT port/, _('the cell lock is set only over the AT port MedveModem works through')],
	[/needs the cell's PCI/, _('this modem locks only to a cell with its PCI')],
	[/Fibocom keeps its own order/, _('Fibocom has no technology priority setting')],
	[/the modem does not offer this combination/, _('the modem does not support this combination of technologies')],
	[/did not answer AT\+GTACT/, _('the modem did not answer AT+GTACT?')],
	[/the modem has no SIM card/, _('there is no SIM card in the modem')],
	/* the last resort (mgr_usbrescue.c, r180) */
	[/the router's own storage \((\S+) on/, _('the router\'s own storage (%s) is on this controller: restarting it would take the system down')],
	[/a mounted disk \((\S+) on (\S+)\)/, _('a disk is mounted from USB (%s): unmount it first, or its data may be lost')],
	[/the devices on the controller changed/, _('the devices on the controller changed: look at the list again and confirm')],
	[/the modem is not on a USB controller/, _('the modem is not on a USB controller')],
	[/a rescue is running/, _('a restart of the USB or of the power is already running')],
	[/no such power line/, _('there is no such power line')],
	[/the controller has no driver to unbind/, _('the controller has no driver that can be restarted')],
	[/the modem is not connected/, _('the modem is not connected')],
	[/the modem has no mobile network/, _('the modem has no mobile network: no signal of an operator')],
	[/the modem's SIM card is not usable/, _('the modem\'s SIM card is not usable (locked or rejected)')],
	[/web wants its password/, _('the modem\'s web page wants its password: set it in «Settings» on this page')],
	[/web locked its logins/, _('the modem\'s web page locked its logins for a while')],
	[/last login to the modem's web failed/, _('the last login to the modem\'s web page failed less than 5 minutes ago')],
	[/keeps its SMS behind its own web/, _('this modem keeps its SMS in its own web page: the storage and the SMS centre are set there')],
	[/an SMS request to the modem's web is running/, _('a request to the modem\'s web page is running; try again in a moment')],
	/* the engine's refusals (English, for its log and ubus) the page had shown as they came */
	[/^device not present$/, _('the modem is not plugged in')],
	[/^port is not on this device any more$/, _('the port is no longer on this modem; the engine is rescanning, try again in a moment')],
	[/^another configuration change is being applied$/, _('other settings are being applied now, try again in a few seconds')],
	[/^the modem is not known to the registry$/, _('the engine does not know this modem (unplugged or removed)')],
	[/^QModem gets only the modems it is handed$/, _('QModem gets only the modems MedveModem hands it')],
	[/^not a USB device$/, _('the modem is not a USB device')],
	[/^cannot write the one-entry usbmode config$/, _('the router could not write the switch rule')],
	[/^cannot start the tool$/, _('the switch tool did not start')],
	[/^ports belong to another device$/, _('the ports belong to another modem')],
	[/^several ModemManager objects claim this device$/, _('ModemManager shows this modem several times')],
	[/session restart does not help/, _('a session restart does not help for this modem')],
	[/no platform power-cycle plugin|nor a switchable USB port/, _('the router cannot switch off the modem\'s USB port')],
	[/USB port of this modem cannot be switched off/, _('the modem\'s USB port cannot be switched off')],
	[/has not left the USB bus and come back/, _('the modem has not come up on USB yet')],
	[/QModem has a section for this modem again \(([^)]*)\); its dial may run/, _('QModem has created a section for this modem again (%s) and may connect it itself: MedveModem manages the modem, better delete QModem\'s section in QModem')],
	[/QModem has a section for this modem again \(([^)]*)\)/, _('QModem has created a section for this modem again (%s); MedveModem manages the modem, QModem\'s section does not get in the way')],
	[/QModem owns this modem/, _('QModem manages the modem; use QModem\'s tools')],
	[/frozen by the operator/, _('the modem management is paused: resume it first')],
	[/QModem still has a section for this modem/, _('the modem is set up in QModem (not MedveModem): delete it there or choose “QModem”')],
	[/QModem manages this modem; choose/, _('QModem manages the modem; for MedveModem to take it, choose another way of management')],
	[/QModem kept its section/, _('QModem did not let the modem go')],
	[/QModem's section names an AT port that is not this modem's/, _('QModem\'s settings for this modem name another modem\'s AT port: commands are not sent')],
	[/QModem's helper is busy/, _('QModem is busy, try again in a minute')],
	[/QModem let go of the modem; the bring-up follows/, _('QModem let the modem go; the connection follows as soon as ModemManager takes it')],
	[/the AT port belongs to ModemManager/, _('the AT port is ModemManager\'s')],
	[/no specific profile matches/, _('the model is not known; steps that break the link are unavailable')],
	[/does not declare this level/, _('this step is not provided for this model')],
	[/capabilities are not validated/, _('the steps for this model are not tested')],
	[/only bench-validated capabilities run without an operator/, _('only the steps tested on this model run automatically')],
	[/through ModemManager, which does not own/, _('the step runs through ModemManager, but another service manages the modem')],
	[/^manual recovery required after (\d+) failed/, _('a check by hand is needed: failed attempts in a row: %s')],
	[/^cooling down for (\d+) s/, _('a pause after an error, %s s left')],
	[/^(\d+) disruptive recoveries in the last 5 minutes/, _('the limit of steps that break the link is reached (in 5 min: %s)')],
	[/another recovery is running/, _('another recovery is running')],
	[/a recovery is running on this modem/, _('a recovery step runs on the modem; try again later')],
	[/an eSIM operation holds this modem/, _('the modem is busy with an eSIM operation; try again later')],
	[/^a bring-up is running$/, _('the modem is being brought up; try again later')],
	[/a configuration change is being applied/, _('settings changes are being applied; try again later')],
	[/the modem has no network interface yet/, _('the modem has no network interface yet: press “Save and connect”')],
	[/netifd did not take the request/, _('the network service (netifd) did not take the request')],
	[/QModem owns this modem; its SMS/, _('QModem manages the modem; SMS are in QModem\'s interface')],
	[/need ModemManager or an AT port of the engine/, _('SMS and USSD work through ModemManager or through the modem\'s AT port, and this modem has neither')],
	[/exchange is running on this modem's AT port/, _('the modem\'s AT port is busy with another SMS/USSD exchange; try again in a few seconds')],
	[/the message is in the modem's storage and the AT port/, _('the message is in the modem\'s storage, and the AT port is unavailable now')],
	[/does not fit 16 parts/, _('the text is longer than 16 SMS parts')],
	[/^the AT port: /, _('the AT port is unavailable:')],
	[/USSD not supported by the network or the modem/, _('USSD is not supported by the network or the modem')],
	[/the network ended the session/, _('the network ended the session')],
	[/the modem refused the request/, _('the modem refused the request')],
	[/the modem did not list its messages over AT/, _('the modem did not list its messages over AT')],
	[/^the number:/, _('number: only digits and “+” at the start, 3 to 20 digits')],
	[/^the text:/, _('text: 1 to 1600 bytes, without control characters')],
	[/does not fit ModemManager's request/, _('the message does not fit ModemManager\'s request')],
	[/a message is being sent/, _('a message is already being sent from the modem')],
	[/a message is being deleted/, _('the message is already being deleted')],
	[/the messages are being read/, _('the messages are being read; try again in a second')],
	[/too many SMS/, _('too many SMS/USSD tasks at once')],
	[/^the code:/, _('code: digits, “*”, “#” and “+”, 2 to 32 characters')],
	[/^the reply:/, _('reply: up to 64 printable characters, without quotes')],
	[/a USSD request is running/, _('a USSD request is already running')],
	[/the AT port belongs to ModemManager/, _('this modem\'s AT port is ModemManager\'s; the console is unavailable (through ModemManager: mmcli)')],
	[/QModem owns this modem's AT ports/, _('QModem manages the modem\'s AT ports')],
	[/no AT port is known/, _('the modem\'s AT port is not known: “Diagnostics” → “AT port” → “Find the AT port” (the “Detailed” level)')],
	[/^identity writes are never sent/, _('identifiers (IMEI and so on) are never written')],
	[/^modem restarts belong to the recovery ladder/, _('a modem restart only as a recovery step on the “Diagnostics” tab')],
	[/^composition changes run only as a validated recipe/, _('a USB composition change only by a tested recipe, not from the console')],
	[/^denied by the hazard list/, _('the command is on the hazard list; the “Detailed” level does not lift it')],
	[/^dial, online and prompting commands/, _('dial (ATD) and prompting commands (“>”) do not run from the console')],
	[/^one printable line only/, _('only one line of printable characters')],
	[/^the PIN: 4 to 8 digits/, _('the PIN: 4 to 8 digits')],
	[/^the new PIN: 4 to 8 digits/, _('the new PIN: 4 to 8 digits')],
	[/^the PUK: exactly 8 digits/, _('the PUK: exactly 8 digits')],
	[/^one PIN try left/, _('one PIN try left')],
	[/^one PUK try left/, _('one PUK try left')],
	[/^no tries left/, _('no tries left')],
	[/^the SIM is locked by its PUK/, _('the SIM card is locked: the PUK and a new PIN are needed')],
	[/^the SIM asks for its PIN, not its PUK/, _('the SIM card asks for its PIN, not its PUK')],
	[/^the SIM asks for neither/, _('the SIM card asks for neither the PIN nor the PUK any more')],
	[/^the SIM is unlocked through ModemManager/, _('the SIM card is unlocked through ModemManager, and it does not manage this modem')],
	[/^ModemManager reports no SIM/, _('ModemManager does not see the SIM card in the modem')],
	[/^an unlock of this SIM is running/, _('an unlock of this SIM card is already running')],
	[/^too many unlocks are running/, _('too many unlocks are running')],
	[/^cannot start mmcli/, _('mmcli could not be started')],
	[/^no answer from ModemManager in time/, _('ModemManager did not answer in time')],
	[/IncorrectPassword|Incorrect password/, _('wrong code')],
	/* why a session cannot be set up (netcfg.c): what to do, not the engine's words (an FM350 tester, r135) */
	[/backend native: the modem has no network device/, _('MedveModem cannot connect the modem itself: the modem has no network interface, its network driver is not loaded. Install it in “Drivers and services” on the “Diagnostics” tab (an FM350 needs kmod-usb-net-rndis), then choose “Automatically”.')],
	[/backend native: the modem's AT port is not known yet/, _('MedveModem cannot connect the modem itself yet: its AT port is not found. Wait a minute or press “Find the AT port” on the “Diagnostics” tab.')],
	[/backend native: no QMI\/MBIM control port and no AT dial recipe/, _('MedveModem cannot connect this modem itself: it has no QMI/MBIM control port and its profile has no AT dial. Choose “Automatically”.')],
	/* netifd's protocol handler gave up (session.reason): ModemManager's failure in words (an LN960 without its SIM, r135) */
	[/gave up; netifd: MM_FAILED_REASON_SIM_MISSING/, _('ModemManager does not see a SIM card in the modem; the connection waits for one')],
	[/gave up; netifd: MM_FAILED_REASON_UNLOCK_REQUIRED/, _('the SIM card is locked with a PIN code: enter it on the “Settings” tab')],
	[/gave up; netifd: MM_FAILED_REASON_SIM_ERROR/, _('ModemManager reports a SIM card error')],
	[/the protocol handler gave up; netifd: (.+)$/, _('the connection was given up by the network service: %s')],
	[/netifd does not know the session's interface/, _('the modem\'s interface is gone from the network settings: “Connect” creates it again')],
	[/^not an AT command/, _('not an AT command (it must start with AT)')],
	[/^empty or longer than 512/, _('empty or longer than 512 characters')],
	[/^a template: the value in <>/, _('a template: the value in <> is yours to choose')],
	[/^documentation only: a dangerous command/, _('for reference only: a dangerous command')],
	[/the same kind of AT job is already running/, _('the console is already running a command on this modem')],
	[/the port did not answer in time/, _('the port did not answer in time')],
	[/^no answer to (.+) within (\d+) ms$/, _('the modem did not answer within the time given (the port may be the wrong one: «Settings» → «AT port»)')],
	[/^busy/, _('the port is busy with another task')],
	[/the port is held by another program[:,]\s*(.+)$/, _('the port is busy with another program: %s; MedveModem does not touch it')],
	[/no answer from the network in time/, _('the network did not answer in time')],
	[/ModemManager did not answer in time/, _('ModemManager did not answer in time')],
	[/ModemManager did not list the messages/, _('ModemManager did not list the messages')],
	[/not attached/, _('the modem is not connected')],
	[/ModemManager has not taken the modem back/, _('ModemManager has not taken the modem back')],
	[/the session is not connected again/, _('the connection is not restored')],
	[/not all ports of the modem are back/, _('not all the modem\'s ports are back')],
	[/no discovery pass since/, _('the USB rescan failed')],
	[/ModemManager has not been read/, _('ModemManager\'s state is not received yet')],
	[/not usable yet/, _('ModemManager found the modem, but it is not ready yet')],
	[/holds no modem object/, _('ModemManager runs, but did not find the modem')],
	[/every level of the ladder was tried/, _('all steps ran without result; a check by hand is required')],
	[/^registration denied/, _('registration denied: the SIM card is blocked, there is no roaming, or the modem\'s firmware is for another region')],
	[/^carrier restriction/, _('reconnecting did not help: check the balance, the traffic limit and the operator\'s network')],
	[/configuration finding is reported/, _('setup is needed; a restart will not fix it')],
	[/SIM problem is reported/, _('a SIM card error; a restart will not fix it')],
	/* radio settings, stage 2 */
	[/does not offer this combination of technologies/, _('ModemManager does not offer this combination of technologies for this modem')],
	[/a band the modem does not have/, _('the modem does not support the chosen band')],
	[/^read the settings first/, _('read the settings first')],
	[/radio settings is still running/, _('the previous change has not finished yet')],
	[/the connection did not come back in (\d+) s/, _('the internet through the modem did not come back in %s s')],
	[/taken back by the operator/, _('by your command')],
	[/the change was refused/, _('the modem refused the change, the previous settings are written')],
	[/keep at least one band of each kind/, _('keep at least one band of each kind (LTE, 5G, 3G)')],
	[/QModem sets the allowed technologies only/, _('QModem does not set a technology priority')],
	[/mode_pref has no preferred/, _('Quectel has no technology priority setting')],
	[/Quectel: name the bands/, _('for Quectel mark the bands explicitly')],
	[/dialect is not done yet/, _('changing the settings is not done for this model yet')],
	[/generation \(QCFG\) is read only/, _('the change is not done for this Quectel generation yet')],
	[/could not be undone/, _('the change could not have been undone, so it was not made')],
	[/refused the old settings/, _('the modem refused the previous settings')],
	[/the rollback could not start/, _('the rollback did not start')]
];
/* a service action's risk, said the same way on every card (the owner, 2026-10-19): what it changes, then how to undo it */
function risk(tag, line) {
	return E('p', { 'class': 'mm-risk' }, [E('span', { 'class': 'mm-pill is-warn mm-risk-tag' }, tag), ' ', line]);
}
/* the engine's eSIM errors (mgr_esim.c, esim.c) in words: what stands in the way, not the code */
function esimWhy(r, switching) {
	var w = { owner_busy: _('the modem\'s control channel is held by ModemManager or QModem: the chip cannot be reached beside them now'),
		transport_unavailable: _('MedveModem has no way to the chip of this modem (no free QMI, MBIM or AT port)'),
		operation_busy: _('another operation is running on this modem, try again in a minute'),
		lpac_missing: _('the lpac program is not installed (package medvemodem-lpac)'),
		driver_unavailable: _('the installed lpac lacks the driver this modem needs'),
		euicc_absent: _('no eSIM chip answered'),
		timeout: _('the chip did not answer in time'),
		stale_generation: _('the modem was re-plugged during the reading, try again'),
		unsupported_action: _('this version cannot do that yet'),
		bad_iccid: _('the profile has no ICCID the chip would take'),
		frozen: _('the modem is frozen') }[r.error];
	if (!w && /profileNotInDisabledState/.test(r.detail || '')) w = _('this profile is already in use');
	if (!w && /iccidOrAidNotFound/.test(r.detail || '')) w = _('the chip has no such profile any more: read it again');
	return (switching ? _('eSIM not switched: %s.') : _('eSIM not read: %s.')).format(w || r.detail || r.error || _('no answer'));
}
function why(line) {
	var t = words(line).replace(/^L\d+: /, ''), m;
	for (var i = 0; i < WHY.length; i++)
		if ((m = WHY[i][0].exec(t))) return WHY[i][1].format(m[1] || '');
	return t;
}
/* the engine's step log while a recovery runs (rec_log in medvemodemd.c) */
var STEPS = { started: _('Start-up'), snapshot: _('Saving the modem\'s state'), 'owner-quiesced': _('Releasing from ModemManager'),
	'owner-command': _('A command to the modem'), 'leases-released': _('Freeing the ports'), 'control-remapped': _('Remapping the control ports'),
	'ports-reported': _('Handing the ports to ModemManager'), 'interface-down': _('Interface down'), 'interface-up': _('Interface up'),
	'port-disabled': _('USB port off'), 'port-enabled': _('USB port on'), unbound: _('Driver unbound'), bound: _('Driver bound'), deauthorized: _('Modem off the USB bus'), authorized: _('Modem on the USB bus'),
	verifying: _('Waiting for the modem to initialise'), succeeded: _('Done'), failed: _('Not done') };
/* what keeps the name and the settings with the modem (name_follows) */
var NAME_FOLLOWS = { serial: _('The name and the settings are bound to the modem\'s serial number: in another USB port they stay.'),
	imei: _('The modem has no serial number of its own, the name and the settings are bound to it by IMEI (the router keeps only its hash): in another USB port they stay.'),
	port: _('The modem has no serial number of its own, and the IMEI is not read yet: the name and the settings are bound to the USB port, in another port the modem does not keep them.') };
function clock(t) {
	if (typeof t !== 'number') return '';
	var dt = new Date(t * 1000), now = new Date(), hm = dt.toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit', second: '2-digit' });
	return dt.toDateString() === now.toDateString() ? hm : dt.toLocaleDateString(LANG, { day: 'numeric', month: 'short' }) + ' ' + hm;
}
var DNSWHY = { 'no answer': _('no answer'), 'server failure': _('server failure'), 'no such name': _('name not found'),
	refused: _('refusal'), 'no address in the answer': _('empty answer') };
var HOSTFIX = { applied: _('binding'), bound: _('bound by the router'), 'not-needed': _('fine'),
	blocked: _('held by another driver'), failed: _('binding error') };
var OWNERS = { modemmanager: 'ModemManager', 'netifd-qmi': 'MedveModem · QMI', 'netifd-mbim': 'MedveModem · MBIM',
	'medvemodem-at': 'MedveModem · AT', qmodem: 'QModem', hilink: _('The modem itself (a router inside it)'), unresolved: _('Not determined'), none: _('Not assigned') };

/* the hash: #<device_id>, or #svc:<device_id> from a tile's «Исправить» (opens «Сервис») */
function deviceId() {
	try { return decodeURIComponent(String(location.hash || '').replace(/^#/, '').replace(/^(svc|sms|radio|set|rescue):/, '')); } catch (e) { return ''; }
}
function wantsService() { return /^#(svc|rescue):/.test(String(location.hash || '')); }
/* the dashboard's «крайние меры» (#rescue:<id>; the owner, 2026-11-09: «иногда оправдано направить пользователя») */
function wantsRescue() { return /^#rescue:/.test(String(location.hash || '')); }
/* the dashboard's envelope opens the SMS tab (#sms:<id>) */
function wantsSms() { return /^#sms:/.test(String(location.hash || '')); }
/* the dashboard's signal (the bars and the words) opens «Радио» (#radio:<id>; the owner, 2026-10-16) */
function wantsRadio() { return /^#radio:/.test(String(location.hash || '')); }
/* the modem's own settings (#set:<id>; UX audit 2026-10-17) */
function wantsSettings() { return /^#set:/.test(String(location.hash || '')); }
/* one scale for words, bar fill and colour (one scale everywhere): four zones, quarter by quarter */
/* the cuts and the fill come from the shared scale (common.js, per technology); here the words */
var SCALES = {
	rsrp: { min: -140, max: -60, unit: _('dBm'), name: 'RSRP', hint: _('signal strength') },
	rsrq: { min: -20, max: -3, unit: _('dB'), name: 'RSRQ', hint: _('signal quality with noise and the tower\'s load') },
	sinr: { min: -10, max: 30, unit: _('dB'), name: 'SINR', hint: _('signal to noise: the main measure of speed') }
};
var STRENGTH = [_('weak'), _('fair'), _('good'), _('excellent')];
/* SINR zones: below 0 the noise is stronger than the signal (it was «noticeable», the owner 2026-10-31) */
var NOISE = [_('heavy noise'), _('noticeable noise'), _('moderate noise'), _('almost no noise')];
var ZONE_WORDS = [_('poor'), _('satisfactory'), _('good'), _('excellent')];
function zone(kind, v, rat) { return zoneOf(kind, v, rat); }
function pct(kind, v, rat) { return fillOf(kind, v, rat); }
/* a cell or a carrier's technology for the scale: NR or not */
function ratOf(type) { return /5g|nr/.test(String(type || '')) ? 'nr' : 'lte'; }
function numText(v) { return num(Math.round(v * 10) / 10); }
/* a bar with its value and a lamp; the word and the scale pop up on hover or tap */
function meter(kind, v, rat) {
	var s = SCALES[kind], cuts = cutsOf(kind, rat), z = zone(kind, v, rat), lo = z ? cuts[z - 1] : null, hi = z < 3 ? cuts[z] : null;
	var tip = _('%s (%s %s). %s — %s.').format(ZONE_WORDS[z].charAt(0).toUpperCase() + ZONE_WORDS[z].slice(1),
		lo === null ? _('below %s').format(hi) : hi === null ? _('from %s').format(lo) : lo + '…' + hi, s.unit, s.name, s.hint);
	var track = E('div', { 'class': 'mm-q-track', role: 'meter', 'aria-label': s.name, 'aria-valuemin': String(s.min), 'aria-valuemax': String(s.max), 'aria-valuenow': String(v) },
		[E('span', { 'class': 'mm-q-fill z' + z, style: 'width:' + pct(kind, v, rat) + '%' })].concat(cuts.map(function(c) {
			return E('span', { 'class': 'mm-q-tick', style: 'left:calc(' + pct(kind, c, rat) + '% - 1px)' }); })));
	return E('div', { 'class': 'mm-q' }, [
		E('div', { 'class': 'mm-q-head' }, [E('span', {}, [E('span', { 'class': 'mm-dim' }, s.name), ' ', E('span', { 'class': 'mm-mono' }, numText(v)), E('span', { 'class': 'mm-unit' }, s.unit)]),
			E('span', { 'class': 'mm-lamp z' + z, tabindex: '0', role: 'img', 'aria-label': tip, 'data-tip': tip })]),
		track]);
}
/* the kind of a journal line, as the mockup's timeline shows it */
function kindOf(k) {
	if (/^recovery-/.test(k)) return ['rec', _('recovery')];
	if (/^session-(down|failed)/.test(k)) return ['err', _('network')];
	if (/^session-|^ifup/.test(k)) return ['net', _('network')];
	if (/^mm-|^backend-/.test(k)) return ['dim', 'ModemManager'];
	if (/^sms-/.test(k)) return ['dim', 'SMS'];
	if (/^radio-/.test(k)) return ['dim', _('radio')];
	if (/^quota-/.test(k)) return ['dim', 'SIM'];
	if (/^config|^iface-repair|^hostfix|^settings-|^usb-config/.test(k)) return ['dim', _('setup')];
	return ['usb', 'USB'];
}
/* what a step touches: a tag beside its title, coloured by how hard it hits */
var STAGE_TAG = { observe: ['soft', _('check')], session: ['soft', _('session')], control: ['soft', _('management')], radio: ['restart', _('radio')],
	'soft-reset': ['restart', _('modem')], rebind: ['usb', _('driver')], 'usb-reset': ['usb', 'USB'], power: ['power', _('power')] };
/* a cell as the page names it: B3 for LTE, n78 for 5G, the role in words */
/* radio settings (spec 25.9.12): ModemManager's mode words and band names in the operator's terms */
var MODE_WORDS = { '2g': '2G', '3g': '3G', '4g': 'LTE', '5g': '5G', any: _('any'), none: '—' };
/* a cell lock as the page says it: «LTE, channel 1850, PCI 123» */
function lockText(l) {
	var rat = l.rat === 'nr' ? '5G' : 'LTE';
	return l.pci != null ? _('%s, channel %s, PCI %s').format(rat, l.arfcn, l.pci) : _('%s, channel %s (any cell on it)').format(rat, l.arfcn);
}

/* a receive chain's RSRP as a measurement: -140 and below (Quectel QRSRP, QMI tx-rx «no reading»), -32768 and the T99's -256
 * are no reading (field notes, 2026-11-02): such a chain is shown off, never «10 dB weaker» */
function chainLive(c) { return c.tuned !== false && typeof c.rsrp === 'number' && c.rsrp > -140 && c.rsrp < -31; }

function modesText(m) {
	return m && Array.isArray(m.allowed) && m.allowed.length ? m.allowed.map(function(x) { return MODE_WORDS[x] || x; }).join(' + ') : '';
}
/* "eutran-3", "ngran-78", "utran-1" -> LTE: B3; 5G: n78; 3G: 1, grouped and in band order */
function bandsText(list_) {
	var g = { lte: [], nr: [], umts: [], gsm: [] }, out = [];
	(Array.isArray(list_) ? list_ : []).forEach(function(b) {
		var m = /^(eutran|ngran|utran|g)-?(\d+)$/.exec(String(b));
		if (!m) { if (String(b) !== 'unknown') g.gsm.push(String(b)); return; }
		(m[1] === 'eutran' ? g.lte : m[1] === 'ngran' ? g.nr : m[1] === 'utran' ? g.umts : g.gsm).push(+m[2]);
	});
	[['lte', 'LTE', 'B'], ['nr', '5G', 'n'], ['umts', '3G', ''], ['gsm', '2G', '']].forEach(function(t) {
		var v = g[t[0]].sort(function(a, b) { return a - b; });
		if (v.length) out.push(t[1] + ': ' + v.map(function(n) { return typeof n === 'number' ? t[2] + n : n; }).join(', '));
	});
	return out.join('; ');
}
var VIA_WORDS = { modemmanager: 'ModemManager', qmodem: 'QModem', at: _('MedveModem\'s AT commands') };
var RS_WHY = [[/QModem drives this modem/, _('QModem manages the modem, the settings are read through it')],
	[/reads it over AT/, _('the settings are read by AT commands')],
	[/no AT reader/, _('reading the settings over AT is not done for this model yet')],
	[/modem's own router holds/, _('the modem is its own router: the network mode and the bands are set on its web page (“Settings” → “The modem\'s web interface”)')],
	[/no owner reports/, _('nobody manages the modem now: the radio settings are unavailable')]];
/* the engine's why for a reading not made yet: the card asks for one itself */
var RS_UNREAD = /not read yet/;

var CELL_ROLE = { primary: _('primary'), secondary: _('extra band'), 'scg-primary': '5G NR', 'scg-secondary': _('5G NR, extra'), serving: _('serving') };
/* LTE's cell identity (28 bits, hex as ModemManager gives it): the eNodeB and its sector */
function enbOf(hex) {
	var n = /^[0-9a-f]{1,8}$/i.test(String(hex || '')) ? parseInt(hex, 16) : 0;
	return n > 0 && n <= 0xfffffff ? { enb: Math.floor(n / 256), sector: n % 256 } : null;
}
/* the distance from LTE's timing advance: one step is 16 Ts of a round trip, 78.12 m one way (36.213 4.2.3) */
function taDistance(c) {
	var ta = c && c.type === 'lte' ? c.timing_advance : null;
	if (typeof ta !== 'number' || ta < 0 || ta > 1282) return '';
	var m = ta * 78.12;
	return '≈ ' + (m < 1000 ? _('%d m').format(Math.round(m / 10) * 10) : _('%s km').format(num(m / 1000, 1))) + ' (TA ' + ta + ', ±' + _('%d m').format(80) + ')';
}
/* a band's common frequency in MHz, as phones and shops name it (3GPP 36.101 / 38.101 tables, rounded to the usual
 * name) */
var BAND_MHZ = {
	lte: { 1: 2100, 2: 1900, 3: 1800, 4: 1700, 5: 850, 7: 2600, 8: 900, 11: 1500, 12: 700, 13: 700, 14: 700, 17: 700, 18: 800, 19: 800,
		20: 800, 21: 1500, 25: 1900, 26: 850, 28: 700, 29: 700, 30: 2300, 31: 450, 32: 1500, 34: 2000, 38: 2600, 39: 1900, 40: 2300,
		41: 2500, 42: 3500, 43: 3700, 46: 5200, 48: 3600, 66: 1700, 71: 600, 72: 450 },
	'5gnr': { 1: 2100, 2: 1900, 3: 1800, 5: 850, 7: 2600, 8: 900, 12: 700, 20: 800, 25: 1900, 28: 700, 38: 2600, 40: 2300, 41: 2500,
		48: 3600, 66: 1700, 71: 600, 77: 3700, 78: 3500, 79: 4700, 257: 28000, 258: 26000, 260: 39000, 261: 28000 }
};
/* the channel's own frequencies (3GPP 36.101 table 5.7.3-1): band -> [F_DL_low, N_Offs-DL, N_DL max, F_UL_low or null, UL span];
 * TDD bands send and receive on one frequency. «2850» on B7 is 2630.0 down, 2510.0 up (as a reference project shows it) */
var EARFCN = { 1: [2110, 0, 599, 1920, 599], 2: [1930, 600, 1199, 1850, 599], 3: [1805, 1200, 1949, 1710, 749], 4: [2110, 1950, 2399, 1710, 449],
	5: [869, 2400, 2649, 824, 249], 7: [2620, 2750, 3449, 2500, 699], 8: [925, 3450, 3799, 880, 349], 12: [729, 5010, 5179, 699, 169],
	13: [746, 5180, 5279, 777, 99], 17: [734, 5730, 5849, 704, 119], 18: [860, 5850, 5999, 815, 149], 19: [875, 6000, 6149, 830, 149],
	20: [791, 6150, 6449, 832, 299], 25: [1930, 8040, 8689, 1850, 649], 26: [859, 8690, 9039, 814, 349], 28: [758, 9210, 9659, 703, 449],
	31: [462.5, 9870, 9919, 452.5, 49], 32: [1452, 9920, 10359, null, 0], 38: [2570, 37750, 38249, 'tdd'], 39: [1880, 38250, 38649, 'tdd'],
	40: [2300, 38650, 39649, 'tdd'], 41: [2496, 39650, 41589, 'tdd'], 42: [3400, 41590, 43589, 'tdd'], 43: [3600, 43590, 45589, 'tdd'],
	66: [2110, 66436, 67335, 1710, 699], 71: [617, 68586, 68935, 663, 349] };
/* {dl, ul} in MHz from the channel number; NR from its global raster (38.104 5.4.2.1), the downlink only */
function channelMhz(c) {
	if (c && c.type === '5gnr' && typeof c.nrarfcn === 'number') {
		var n = c.nrarfcn;
		return n < 0 || n > 3279165 ? null : { dl: n < 600000 ? n * 0.005 : n < 2016667 ? 3000 + (n - 600000) * 0.015 : 24250.08 + (n - 2016667) * 0.06 };
	}
	var t = c && c.type === 'lte' && typeof c.earfcn === 'number' ? EARFCN[c.band] : null;
	if (!t || c.earfcn < t[1] || c.earfcn > t[2]) return null;
	var k = c.earfcn - t[1], dl = t[0] + k / 10;
	return { dl: dl, ul: t[3] === 'tdd' ? dl : typeof t[3] === 'number' && k <= t[4] ? t[3] + k / 10 : null };
}
function channelText(c) {
	var f = channelMhz(c);
	if (!f) return '';
	return f.ul != null && f.ul !== f.dl ? _('↓%s / ↑%s MHz').format(num(f.dl, 1), num(f.ul, 1)) : _('%s MHz').format(num(f.dl, f.dl >= 10000 ? 2 : 1));
}
/* the base station on a map by its identity (4cells.ru): LTE by the eNB, 3G by the cell and
 * its area; none for 5G SA, which has no such public map */
function cellMapUrl(mcc, mnc, type, cidHex, lac) {
	var cid = /^[0-9a-f]{1,8}$/i.test(String(cidHex || '')) ? parseInt(cidHex, 16) : NaN, p3 = function(x) { return ('00' + x).slice(-3); };
	if (!/^\d{3}$/.test(String(mcc || '')) || !/^\d{2,3}$/.test(String(mnc || '')) || !(cid > 0)) return null;
	var base = 'https://4cells.ru/?plmn=' + p3(mcc) + p3(mnc);
	if (type === 'lte') return base + '&tech=3&num=' + Math.floor(cid / 256);
	var area = /^[0-9a-f]{1,4}$/i.test(String(lac || '')) ? parseInt(lac, 16) : NaN;
	if (type === 'umts' && area > 0) return base + '&tech=2&num=' + (cid % 65536) + '&lac=' + area;
	return null;
}
function bandMhz(c) { var t = BAND_MHZ[c && c.type] || {}; return c && c.band ? t[c.band] || null : null; }
function bandName(c) { return c.band ? (c.type === '5gnr' ? 'n' : c.type === 'lte' ? 'B' : '') + c.band + (c.doubt ? '?' : '') : (c.type === '5gnr' ? '5G' : String(c.type || '').toUpperCase()); }
/* the band is computed from the channel number; the modem's own list of allowed bands ("eutran-3", "ngran-78") cross-checks it */
function bandDoubt(c, allowed) {
	if (!c.band || !allowed.length || allowed.indexOf('any') >= 0) return false;
	var key = (c.type === '5gnr' ? 'ngran-' : c.type === 'lte' ? 'eutran-' : '') + c.band;
	return key.indexOf('-') > 0 && allowed.some(function(b) { return b.indexOf(key.split('-')[0] + '-') === 0; }) && allowed.indexOf(key) < 0;
}
function allowedText(allowed, type) {
	var pre = type === '5gnr' ? 'ngran-' : 'eutran-', mark = type === '5gnr' ? 'n' : 'B';
	return allowed.filter(function(b) { return b.indexOf(pre) === 0; }).map(function(b) { return mark + b.slice(pre.length); }).join(', ');
}
/* ModemManager gives the carrier bandwidth without a unit: kHz from QMI, MHz from some others */
function bwMhz(v) { return typeof v !== 'number' || v <= 0 ? null : v >= 1000 ? v / 1000 : v <= 100 ? v : null; }
function mhzText(v) { return num(Math.round(v * 10) / 10); }
function agoLong(s) {
	return s < 60 ? _('%d s ago').format(Math.round(s)) : s < 3600 ? _('%d min ago').format(Math.floor(s / 60)) : s < 172800 ? _('%d h ago').format(Math.floor(s / 3600)) : _('%d d ago').format(Math.floor(s / 86400));
}
/* the SMS alphabet as the network counts it (sms.c mm_sms_measure): GSM 7-bit with its extension table, else UCS-2 */
var GSM7 = '@£$¥èéùìòÇ\nØø\rÅåΔ_ΦΓΛΩΠΨΣΘΞÆæßÉ !"#¤%&\'()*+,-./0123456789:;<=>?¡ABCDEFGHIJKLMNOPQRSTUVWXYZÄÖÑÜ§¿abcdefghijklmnopqrstuvwxyzäöñüà';
var GSM7_EXT = '\f^{}\\[~]|€';
function smsMeasure(t) {
	var g = 0, u = 0, seven = true;
	Array.from(String(t || '')).forEach(function(ch) {
		if (GSM7_EXT.indexOf(ch) >= 0) g += 2; else if (GSM7.indexOf(ch) >= 0) g += 1; else seven = false;
		u += ch.codePointAt(0) > 0xffff ? 2 : 1;
	});
	var units = seven ? g : u, one = seven ? 160 : 70, part = seven ? 153 : 67;
	return { gsm7: seven, units: units, segments: !units ? 0 : units <= one ? 1 : Math.ceil(units / part), limit: units <= one ? one : part * Math.ceil(units / part) };
}
var SMS_STATE = { receiving: _('being received'), sending: _('being sent'), sent: _('sent'), stored: _('not sent'), unknown: _('not sent'), failed: _('not sent') };
/* a conversation in time: by the network's time when both have one, else by the storage number (a sent one has no time) */
/* one order for a conversation (bench 2026-10-15: new messages landed in the middle): the network time, else the key
 * smsKeys gave it; the id only breaks a tie (ModemManager reuses the index of a deleted message) */
function smsOrder(a, b) { return (a.key || 0) - (b.key || 0) || a.id - b.id; }
var SMS_SEND_ERR = { 'created but not sent': _('the modem did not send the message'), 'not created': _('ModemManager did not take the message'), 'not sent in time': _('the modem did not answer in time'),
	'not sent': _('the modem did not send the message'), 'sent in part': _('only a part of a long message was sent') };
var STORE = { sm: _('SIM card'), me: _('modem storage'), mt: _('modem storage and SIM card'), sr: _('delivery reports'),
	router: _('router') };
function smsWhen(m) {
	/* a sent message has no network time: when this page first saw it, marked as approximate */
	var t = typeof m.epoch === 'number' ? m.epoch : m.seen;
	if (typeof t !== 'number') return typeof m.timestamp === 'string' ? m.timestamp : _('the time is not kept by the modem');
	var d = new Date(t * 1000), today = new Date();
	return (typeof m.epoch === 'number' ? '' : '≈ ') + (d.toDateString() === today.toDateString() ? d.toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit' }) :
		d.toLocaleString(LANG, { day: '2-digit', month: '2-digit', year: '2-digit', hour: '2-digit', minute: '2-digit' }));
}
/*
 * A sort key for every message. With a network time, that time. Without (sent ones, MM keeps no time for them): the
 * time this browser first saw it, kept per modem in localStorage; on the first visit, right after the message before
 * it by index that has a time. Per-viewer convenience only; the page works without storage.
 */
/* a message's identity as a hash (UX audit 2026-10-17): the browser keeps neither the number nor the text */
function smsSig(m) {
	var s = [m.id, m.type, m.number, String(m.text || '').slice(0, 40)].join('|'), h1 = 0xdeadbeef, h2 = 0x41c6ce57, i, c;
	for (i = 0; i < s.length; i++) { c = s.charCodeAt(i); h1 = Math.imul(h1 ^ c, 2654435761); h2 = Math.imul(h2 ^ c, 1597334677); }
	h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507) ^ Math.imul(h2 ^ (h2 >>> 13), 3266489909);
	h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507) ^ Math.imul(h1 ^ (h1 >>> 13), 3266489909);
	return 'h' + (4294967296 * (2097151 & h2) + (h1 >>> 0)).toString(36);
}
function smsKeys(dev, msgs, now) {
	var name = 'mm:sms-seen:' + dev, store = null, keep = {}, after = 0;
	try { store = JSON.parse(localStorage.getItem(name) || 'null'); } catch (e) { store = null; }
	var first = !store || typeof store !== 'object';
	if (first) store = {};
	msgs.slice().sort(function(a, b) { return a.id - b.id; }).forEach(function(m) {
		var sig = smsSig(m), old = [m.id, m.type, m.number, String(m.text || '').slice(0, 40)].join('|');
		if (typeof m.epoch === 'number') { m.key = m.epoch; after = Math.max(after, m.epoch); return; }
		if (typeof store[sig] !== 'number' && typeof store[old] === 'number') store[sig] = store[old];	/* kept by 0.7.x in clear: carried over, then dropped */
		if (typeof store[sig] !== 'number') store[sig] = first ? -(after + 0.5) : now;	/* negative: placed, not seen */
		keep[sig] = store[sig];
		m.key = Math.abs(store[sig]);
		if (store[sig] > 0) m.seen = store[sig];
	});
	try { localStorage.setItem(name, JSON.stringify(keep)); } catch (e) { /* no storage: the order still holds for this view */ }
}

/* closed until the operator opens them (UX audit 2026-10-17): the details, the history, the rare tools; the drivers,
 * PIN and web cards appear only when they matter */
var SEC_CLOSED = { events: 1, stab: 1, neigh: 1, ant: 1, rset: 1, netcheck: 1, report: 1,
	ussd: 1, atport: 1, temp: 1 };

/*
 * The browser keeps LuCI's scripts by LuCI's own version, not this package's: after an update a page met an old
 * common.js and showed dashes (the owner, 2026-10-20, fixed by a private window). This file knows its version; on a
 * mismatch both files are fetched past the cache and the page reloads once.
 */
var VIEW_VERSION = '0.16.0-alpha1-r9';
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

return view.extend({
	handleSaveApply: null,
	handleSave: null,
	handleReset: null,

	render: function() {
		if (staleFiles()) return E('div', { 'class': 'mm-root' }, E('p', { 'class': 'mm-faint' }, _('The page was updated on the router: loading the new version…')));
		var self = this;
		this.id = deviceId();
		this.level = level();
		this.refs = {};
		this.rowLevel = {};
		this.stageRows = {};
		this.lastSuccess = null;
		this.error = E('div', { 'class': 'mm-callout is-info mm-notice', role: 'status' }, _('Loading the modem\'s data…'));
		this.message = E('div', { 'class': 'mm-callout is-info mm-message', role: 'status', hidden: true });
		this.notices = E('div', { 'class': 'mm-notices', 'aria-live': 'polite' });
		/* a port another program holds: said while it lasts, not as a one-off notice */
		this.heldText = E('span');
		this.heldNote = E('div', { 'class': 'mm-callout is-warn mm-held', role: 'status', hidden: '' }, [this.heldText]);
		/* a model MedveModem does not fully know: the report its profile is made from (the owner, 2026-10-21) */
		this.fitText = E('span');
		this.fitNote = E('div', { 'class': 'mm-callout is-info mm-fit-note', role: 'status', hidden: '' }, [
			E('span', {}, [this.fitText, ' ' + _('Collect a report and send it to the developers on GitHub: a profile of this model is added from it.')]),
			E('span', {}, [E('button', { type: 'button', 'class': 'mm-link mm-fit-report', click: mm.busy(function() {
				self.show('service');
				return self.saveReport();
			}) }, _('Collect a report')), ' · ',
			E('a', { 'class': 'mm-fit-issue', href: mm.NEW_MODEM_ISSUE, target: '_blank', rel: 'noopener' }, _('Send to GitHub →'))])]);
		/* the profile hands this model to QModem, and QModem is not on the router */
		this.qmodemNote = E('div', { 'class': 'mm-callout is-info mm-qm-note', role: 'status', hidden: '' }, [
			_('For this model MedveModem hands the management to QModem, but QModem is not installed on the router: MedveModem connects the modem itself (over AT).') + ' ',
			E('a', { href: L.url('admin/modem/medvemodem/settings') + '#qmodem' }, _('Install QModem'))]);
		/* the modem's network inside the router's LAN (field 2026-10-30, an M150-4 at 192.168.1.10): up, yet nothing goes through */
		this.clashText = E('span');
		this.clashNote = E('div', { 'class': 'mm-callout is-err mm-clash-note', role: 'alert', hidden: '' }, [this.clashText]);
		/* an emergency the «Сервис» tab can fix, said above the tabs (audit 2026-10-13: the cards lay at its bottom) */
		this.alerts = {};
		this.alertText = E('span');
		this.alertNote = E('div', { 'class': 'mm-callout is-err mm-alert', role: 'alert', hidden: '' }, [this.alertText, ' ',
			E('button', { type: 'button', 'class': 'mm-link mm-alert-go', click: mm.busy(function() { return self.goAlert(); }) }, _('Go to the fix →'))]);
		/* every banner above the tabs closes with ×; a closed one waits in the journal with «Show again» */
		var redraw = function() { if (self.lastEvents) self.applyJournal(self.lastEvents); };
		[[this.alertNote, 'alert'], [this.clashNote, 'clash'], [this.heldNote, 'held'], [this.fitNote, 'fit'], [this.qmodemNote, 'qmodem']]
			.forEach(function(b) { mm.dismissible(b[0], (self.id || '-') + ':' + b[1], redraw); });
		this.headSub = E('p', {}, '');
		this.hero = this.makeHero();
		this.tabs = {};
		var strip = E('div', { 'class': 'mm-tabs', role: 'tablist' });
		/* «Настройки» gathers what was spread over four tabs (UX audit 2026-10-17); built after «Сеть и сигнал», whose band card it takes */
		var panels = [['overview', _('Overview'), this.overview()], ['radio', _('Network and signal'), this.radio()], ['sms', 'SMS', this.smsTab()],
			['esim', 'eSIM', [this.esimCard()]], ['settings', _('Settings'), this.settingsTab()], ['at', 'AT', this.atTab()], ['service', _('Diagnostics'), this.service()]].map(function(t) {
			var panel = E('section', { 'class': 'mm-section mm-panel', role: 'tabpanel', id: 'mm-tab-' + t[0] }, t[2]);
			var tab = E('button', { type: 'button', role: 'tab', 'class': 'mm-tab', 'aria-controls': 'mm-tab-' + t[0],
				click: function() { self.show(t[0]); } }, t[1]);
			self.tabs[t[0]] = { tab: tab, panel: panel };
			strip.appendChild(tab);
			return panel;
		});
		/* «Настройки» last and set apart in the theme's accent: the place where things are changed (spec §42.3, the owner
		 * 2026-11-02); built in the middle, where it takes the band card from «Сеть и сигнал» */
		this.tabs.settings.tab.classList.add('mm-tab-set');
		strip.appendChild(this.tabs.settings.tab);
		this.tabs.at.tab.hidden = this.level === 'normal';		/* the AT console from «Подробный» */
		this.esimShow();						/* a modem with an eSIM only, at every level */
		this.root = E('div', { 'class': 'mm-root mm-modem' + (this.level === 'normal' ? ' is-basic' : '') }, [
			E('link', { rel: 'stylesheet', href: L.resource('medvemodem/dashboard.css') + '?v=' + CSS_VERSION }),
			pageHead(_('Modem'), this.headSub, function(k) { self.level = k; if (self.data) self.apply(self.data); }, ''),
			E('div', {}, E('a', { href: L.url('admin/modem/medvemodem/dashboard'), 'class': 'mm-btn is-quiet mm-back' }, _('← All modems'))),
			this.notices, this.alertNote, this.clashNote, this.heldNote, this.fitNote, this.qmodemNote, this.error, this.message, this.hero.node,
			this.tabsWrap = E('div', { 'class': 'mm-tabs-wrap' }, strip)
		].concat(panels));
		this.radioNote = this.root.querySelector('[data-ref="r_note"]');
		this.show(wantsService() ? 'service' : wantsSms() ? 'sms' : wantsRadio() ? 'radio' : wantsSettings() ? 'settings' : 'overview');
		text(this.hero.title, this.id ? _('Modem') : _('No modem chosen'));
		this.pollFn = this.update.bind(this);
		poll.add(this.pollFn, 5);
		this.update();
		return this.root;
	},

	show: function(name) {
		Object.keys(this.tabs).forEach(function(k) {
			this.tabs[k].panel.hidden = k !== name;
			this.tabs[k].tab.setAttribute('aria-selected', String(k === name));
		}, this);
		this.current = name;
		if (this.tabsWrap) (this.tabsSeen = this.tabsSeen || mm.tabsHint(this.tabsWrap, this.tabsWrap.firstChild))(this.tabs[name] && this.tabs[name].tab);
		if (name === 'sms' && this.id) this.loadSms(true);
		if (name === 'sms' && this.id && !this.ussdOwn) this.loadUssdOwn();	/* the user's own codes: read once, when SMS are opened */
		if (name === 'sms' && !this.contacts) this.loadContacts();
		if ((name === 'sms' || name === 'at') && this.id) this.loadPresets();
		if (name === 'radio' && this.id) this.loadHistory(true);
		if (name === 'radio' && this.id) this.loadTraffic(true);
	},

	/* «AT»: the engine's console with presets for this modem (from «Подробный»); what the hazard list
	 * keeps from the console is listed as reference, never as a button */
	atTab: function() {
		var self = this, a = this.atUi = {};
		a.note = E('div', { 'class': 'mm-callout is-info mm-at-note', hidden: '' });
		a.preset = E('select', { 'class': 'mm-select mm-at-preset', id: 'mm-at-preset', change: function() {
			if (a.preset.value) { a.cmd.value = a.preset.value; a.cmd.focus(); } } }, [E('option', { value: '' }, _('Ready commands…'))]);
		a.cmd = E('input', { type: 'text', 'class': 'mm-input mm-at-cmd', id: 'mm-at-cmd', autocomplete: 'off', spellcheck: 'false',
			placeholder: 'AT+CSQ', maxlength: '512', keydown: function(ev) { if (ev.key === 'Enter') self.sendAt(); } });
		a.send = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-at-send', click: mm.busy(function() { return self.sendAt(); }) }, _('Send'));
		a.out = E('pre', { 'class': 'mm-at-out', hidden: '' });
		a.danger = E('ul', { 'class': 'mm-at-danger' });
		a.dangerBox = E('details', { 'class': 'mm-at-danger-box', hidden: '' }, [E('summary', {}, _('Hazardous and template commands: for reference only')), a.danger]);
		a.sources = E('p', { 'class': 'mm-faint mm-at-sources' });
		a.free = E('div', { 'class': 'mm-at-free' }, [a.cmd]);
		/* the day's commands of this modem (engine RAM), folded until asked for */
		a.histList = E('ul', { 'class': 'mm-at-hist-list' });
		a.hist = E('details', { 'class': 'mm-at-hist', toggle: function() { if (a.hist.open) self.loadAtHistory(); } }, [
			E('summary', {}, _('Command history for the day')),
			E('div', { 'class': 'mm-row mm-at-hist-bar' }, [E('span', { 'class': 'mm-faint' }, _('Kept in the engine\'s memory until a restart; from the page and from the command line.')),
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-at-hist-clear', click: mm.busy(function() { return self.loadAtHistory(true); }) }, _('Clear'))]),
			a.histList]);
		return [a.note, this.sec('at', _('AT console'), [
			E('p', { 'class': 'mm-faint' }, _('The command goes into the modem\'s AT port through the MedveModem service, which holds the port for the command\'s time. Commands on the hazard list (restart, USB composition change, identifier writes) are not sent.')),
			E('p', { 'class': 'mm-faint mm-at-risk-note' }, _('⚠ marks a command that cuts the link or changes a setting of the modem: it is sent only after a confirmation.')),
			E('label', { 'for': 'mm-at-preset', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Ready commands for this modem')), a.preset]),
			E('div', { 'class': 'mm-row mm-at-line' }, [a.free, a.send]), a.out, a.hist, a.dangerBox, a.sources])];
	},

	/* which tty is the AT port: the engine's choice, the operator's (UCI device option at_interface), a probe */
	atPortCard: function() {
		var self = this, p = this.atPort = {};
		p.now = E('p', { 'class': 'mm-muted' });
		p.pick = E('select', { 'class': 'mm-select', id: 'mm-at-port', change: function() { p.dirty = true; } });
		p.save = E('button', { type: 'button', 'class': 'mm-btn mm-at-save', click: mm.busy(function() { return self.saveAtPort(); }) }, _('Save'));
		p.probe = E('button', { type: 'button', 'class': 'mm-btn mm-at-probe', click: mm.busy(function() { return self.probeAt(); }) }, _('Find the AT port'));
		/* the probe's result where the button is (the owner, 2026-10-21: «просто дергается без понятия результата») */
		p.result = E('div', { 'class': 'mm-callout mm-at-result', role: 'status', hidden: '' });
		return p.card = this.sec('atport', _('AT port'), [p.now, p.result,
			E('label', { 'for': 'mm-at-port', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Port for commands')), p.pick]),
			E('p', { 'class': 'mm-faint' }, _('“Find the AT port” sends AT into the ports whose role is unknown; service ports (DIAG, ADB) are not asked. Choosing by hand is needed if the modem numbers its ports not as its profile says.')),
			E('div', { 'class': 'mm-actions' }, [p.save, p.probe])]);
	},

	/* «Температура» (bench 2026-10-15): through the model's own temperature preset, on request or, when asked for, every 2 min while the page is open */
	tempCard: function() {
		var self = this, t = this.temp = {}, pref = mm.tempPrefs(this.id);
		t.value = E('p', { 'class': 'mm-temp-value' }, _('Not measured yet.'));
		t.raw = E('pre', { 'class': 'mm-faint mm-temp-raw', hidden: '' });
		t.run = E('button', { type: 'button', 'class': 'mm-btn mm-temp-run', click: mm.busy(function() { return self.measureTemp(); }) }, _('Measure'));
		/* automatically, and on the dashboard's card (the owner, 2026-10-19): kept in this browser, per modem */
		t.auto = E('input', { type: 'checkbox', 'class': 'mm-temp-auto', change: function() {
			mm.tempPrefs(self.id, { auto: t.auto.checked });
			self.tempHist();
			if (t.auto.checked) self.measureTemp();
		} });
		t.dash = E('input', { type: 'checkbox', 'class': 'mm-temp-dash', change: function() {
			mm.tempPrefs(self.id, { dash: t.dash.checked });
		} });
		t.auto.checked = !!pref.auto; t.dash.checked = !!pref.dash;
		var last = pref.c != null && pref.t ? pref : null;
		if (last) text(t.value, numText(last.c) + ' °C · ' + agoLong(Date.now() / 1000 - last.t));
		t.hist = E('div', { 'class': 'mm-temp-hist', hidden: '' });
		t.card = this.sec('temp', _('Temperature'), [t.value, t.hist,
			E('p', { 'class': 'mm-faint' }, _('One AT command from this model\'s ready commands; the hottest sensor is shown.')), t.raw,
			E('label', { 'class': 'mm-check' }, [t.auto, ' ' + _('Measure automatically every %d min while the page is open').format(Math.round(mm.TEMP_EVERY_S / 60))]),
			E('label', { 'class': 'mm-check' }, [t.dash, ' ' + _('Show on the modem\'s card on the main page (the “Detailed” level)')]),
			E('div', { 'class': 'mm-actions' }, [t.run])]);
		t.gist = t.card.querySelector('.mm-sec-gist');
		/* a folded card said something (an LN960 tester, r135: an empty «Температура») */
		if (t.gist) text(t.gist, last ? numText(last.c) + ' °C' : _('not measured yet'));
		this.tempHist();
		return t.card;
	},

	/* the 12 h chart under the value, while the automatic measurement is on */
	tempHist: function() {
		var self = this, t = this.temp, box = t.card && t.card.querySelector('.mm-sec-b');
		var ch = t.auto.checked ? mm.tempChart(mm.tempPrefs(this.id).h, ((box && box.clientWidth) || 560) - 4) : null;
		var hm = function(s) { return new Date(s * 1000).toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit' }); };
		t.hist.replaceChildren.apply(t.hist, ch ? [ch.node, E('p', { 'class': 'mm-faint mm-temp-sum' },
			(ch.min === ch.max ? _('12 h: %s °C').format(numText(ch.min)) : _('12 h: %s…%s °C · min at %s, max at %s').format(numText(ch.min), numText(ch.max), hm(ch.minAt), hm(ch.maxAt))) +
			' · ' + _('measurements: %d').format(ch.points) + '. ' + _('The open modem page measures: the gaps in the chart are the time it was closed.'))] : []);
		t.hist.hidden = !ch;
		/* the chart is as wide as the card: drawn again when the window changes width */
		if (!this.tempResize) {
			this.tempResize = true;
			var wait = null;
			window.addEventListener('resize', function() { clearTimeout(wait); wait = setTimeout(function() { if (!self.temp.hist.hidden) self.tempHist(); }, 200); });
		}
	},

	tempCommand: function() {
		return mm.tempCommand(this.presets);
	},

	/* the automatic measurement, from the page's tick: when asked for, the card shows and the last one is old enough */
	tempTick: function() {
		var t = this.temp, pref;
		if (!t || !t.auto.checked || t.card.hidden || this.present !== true || t.busy || document.hidden) return;
		pref = mm.tempPrefs(this.id);
		if (pref.t && Date.now() / 1000 - pref.t < mm.TEMP_EVERY_S) return;
		this.measureTemp(true);
	},

	measureTemp: function(quiet) {
		var self = this, t = this.temp;
		t.run.disabled = true;
		t.busy = true;
		return Promise.resolve(this.loadPresets()).then(function() {
			var cmds = mm.tempCommands(self.presets);
			if (!cmds.length) { text(t.value, _('The temperature command for this model is unknown.')); return; }
			if (!quiet) text(t.value, _('Measuring…'));
			/* each of the model's commands in turn until one gives a temperature (the FM350: GTSENRDTEMP, then ETHERMAL) */
			return mm.tempAsk(cmds, function(cmd) { return atCommand(self.id, cmd); }).then(function(a) {
				var r = a.r || {}, c = a.c, cmd = a.cmd;
				if (r.refused || (r.error && !r.final)) { text(t.value, _('Not measured: %s').format(why(r.refused || r.error))); return; }
				/* a lone jump waits for two more readings that agree (a sensor's slip) */
				var kept = c === null ? false : mm.tempRecord(self.id, c) !== false;
				text(t.value, c === null ? _('The modem answered, but no temperature was found in the answer.') :
					kept ? numText(c) + ' °C' : _('%s °C: a sudden jump, shown once it repeats').format(numText(c)));
				if (kept) {
					if (t.gist) text(t.gist, numText(c) + ' °C');
					self.tempHist();
				}
				text(t.raw, [cmd].concat(a.lines).join('\n'));
				t.raw.hidden = false;
			});
		}).catch(function(e) { text(t.value, _('The MedveModem service does not answer (%s).').format(failText(e))); })
			.finally(function() { t.busy = false; t.run.disabled = self.present !== true; });
	},

	applyAtPort: function(d) {
		var p = this.atPort, roles = d.port_roles || {}, at = roles.at, k = d.connection || {};
		var SRC = { profile: _('from the model\'s profile'), operator: _('chosen by hand'), probe: _('found by a probe'), modemmanager: _('by ModemManager\'s data'), interface: _('by the interface type') };
		var ttys = list(roles.candidates).filter(function(c) { return c.kind === 'tty' && c.role !== 'reserved' && c.number; });
		var never = list(roles.candidates).filter(function(c) { return c.kind === 'tty' && c.source === 'never-at' && c.number; });
		text(p.now, at ? _('Now: %s (%s)').format(at.node, SRC[at.source] || at.source) + (at.owner === 'modemmanager' ? ', ' + _('the port is ModemManager\'s') : '') + '.' :
			ttys.length ? _('The AT port is not known. Choose it or press “Find the AT port”.') :
			_('The modem has no serial ports: none of its interfaces is given to the AT port driver (option), there is nothing to search. Usually the model\'s profile fixes it (the “Service ports (AT)” line above).'));
		if (!p.dirty && document.activeElement !== p.pick)
			p.pick.replaceChildren.apply(p.pick, [E('option', { value: '' }, _('Automatically'))].concat(ttys.map(function(c) {
				return E('option', { value: c.number, selected: k.at_interface && parseInt(k.at_interface, 16) === parseInt(c.number, 16) ? '' : null },
					c.node + ' · ' + _('interface %s').format(c.number) + (c.role === 'at' ? ' · AT' : c.role === 'unknown' ? '' : ' · ' + c.role));
			}), never.map(function(c) {
				/* audit F01: a DIAG/QDL port is shown, never offered: AT into it hangs the modem */
				return E('option', { value: c.number, disabled: '', 'class': 'mm-at-never' }, c.node + ' · ' + _('interface %s').format(c.number) + ' · ' + _('a service port, AT forbidden'));
			})));
		p.save.disabled = p.probe.disabled = d.present !== true || !ttys.length;
	},

	saveAtPort: function() {
		var self = this, p = this.atPort, id = this.id, v = p.pick.value;
		return uciGet('medvemodem', 'device').then(function(r) {
			var secs = (r && r.values) || {}, name = Object.keys(secs).filter(function(n) { return secs[n].id === id; })[0];
			if (name) return uciSet('medvemodem', name, { at_interface: v });
			return v ? uciAdd('medvemodem', 'device', { id: id, at_interface: v }) : null;
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			p.dirty = false;
			self.say(v ? _('The AT port is chosen: interface %s.').format(v) : _('The AT port is chosen automatically again.'));
			return self.update();
		}).catch(function(e) { self.say(_('Saving error: %s').format(failText(e))); });
	},

	probeAt: function() {
		var self = this, p = this.atPort;
		var tell = function(msg, tone) {
			p.result.className = 'mm-callout mm-at-result' + (tone ? ' is-' + tone : '');
			text(p.result, msg);
			p.result.hidden = false;
		};
		p.probe.disabled = true;
		text(p.probe, _('Searching…'));
		tell(_('Searching for the AT port: asking the ports with an unknown role, up to half a minute…'), 'info');
		return probePorts(this.id).then(function(r) {
			r = r || {};
			var res = r.results || {}, names = Object.keys(res);
			var found = names.filter(function(n) { return res[n] === 'at'; });
			var said = { at: _('answers AT'), silent: _('silent'), 'not-at': _('not AT (binary data)'), 'no-answer': _('silent'), busy: _('busy with another program'), interrupted: _('interrupted') };
			var each = names.length ? ' ' + _('Checked: %s.').format(names.map(function(n) { return String(n).replace(/^\/dev\//, '') + ' — ' +
				(said[res[n]] || res[n]); }).join(', ')) : '';
			if (r.refused) tell(_('The search did not run: %s.').format(why(r.refused)), 'warn');
			else if (found.length) tell(_('AT port found: %s.').format(found.map(function(n) { return String(n).replace(/^\/dev\//, ''); }).join(', ')) + each, 'ok');
			else if (names.length) tell(_('None of the checked ports answered AT.') + each, 'warn');
			else tell(_('Nothing to check: the roles of all ports are known (the current AT port is in the line above).'), 'info');
			return self.update();
		}, function(e) { tell(_('The search did not run: %s.').format(failText(e)), 'err'); })
			.finally(function() { p.probe.disabled = false; text(p.probe, _('Find the AT port')); });
	},

	loadPresets: function() {
		var self = this;
		if (this.presets || this.presetsPending) return this.presetsPending;
		this.presetsPending = getPresets(this.id).then(function(r) {
			self.presets = r || {};
			self.applyPresets(self.presets);
		}, function() { /* an older engine: no presets, the console and USSD still work */ })
			.finally(function() { self.presetsPending = null; });
		return this.presetsPending;
	},

	applyPresets: function(r) {
		var self = this, a = this.atUi, s = this.smsUi, at = r.at || {}, groups = {}, order = [];
		list(at.groups).forEach(function(g) { groups[g.id] = { title: tr(g.title), items: [] }; order.push(g.id); });
		var refused = [], byCmd = this.presetByCmd = {};
		list(at.sets).forEach(function(set) {
			list(set.presets).forEach(function(p) {
				if (p && p.command) byCmd[p.command] = p;
				if (!p.allowed) { refused.push([set, p]); return; }
				if (!groups[p.group]) { groups[p.group] = { title: p.group, items: [] }; order.push(p.group); }
				groups[p.group].items.push(p);
			});
		});
		a.preset.replaceChildren.apply(a.preset, [E('option', { value: '' }, _('Ready commands…'))].concat(order.filter(function(g) {
			return groups[g].items.length; }).map(function(g) {
			return E('optgroup', { label: groups[g].title }, groups[g].items.map(function(p) {
				return E('option', { value: p.command }, (atRisk(p.command, p).risk !== 'read_only' ? '⚠ ' : '') + tr(p.title) + ' — ' + p.command); }));
		})));
		a.danger.replaceChildren.apply(a.danger, refused.map(function(x) {
			return E('li', {}, [E('code', {}, x[1].command), ' — ' + tr(x[1].title) + ' ', E('span', { 'class': 'mm-faint' }, '(' + why(x[1].why || '') + ')')]);
		}));
		a.dangerBox.hidden = !refused.length;
		a.sources.textContent = list(at.sets).length ? _('Sets: %s.').format(list(at.sets).map(function(x) { return tr(x.title); }).join(', ')) : '';
		var owner = r.at_owner;
		a.note.hidden = owner !== 'modemmanager' && owner !== 'qmodem';
		text(a.note, owner === 'modemmanager' ? _('This modem\'s AT port is ModemManager\'s: the console is unavailable. The ready commands are for reference.') :
			owner === 'qmodem' ? _('QModem manages the modem\'s AT ports: MedveModem\'s console is unavailable.') : '');
		a.send.disabled = owner === 'modemmanager' || owner === 'qmodem';
		if (s) this.applyUssdCodes();
	},

	loadAtHistory: function(clear) {
		var self = this, a = this.atUi;
		return atHistory(this.id, !!clear).then(function(r) {
			var cmds = list((r || {}).commands).slice().reverse();
			a.histList.replaceChildren.apply(a.histList, cmds.length ? cmds.map(function(c) {
				var res = c.refused ? _('not sent: %s').format(why(c.refused)) : c.error ? _('error: %s').format(why(c.error)) : c.final || (c.ok ? 'OK' : '');
				var bad = c.refused || c.error || (c.final && !c.ok);
				var cmdBtn = E('button', { type: 'button', 'class': 'mm-link mm-mono mm-at-hist-cmd', title: _('Into the command line'), click: function(ev) {
					ev.preventDefault();
					if (self.level === 'expert') { a.cmd.value = c.cmd; a.cmd.focus(); } } }, c.cmd);
				return E('li', {}, [
					E('details', {}, [E('summary', {}, [E('time', { 'class': 'mm-t mm-faint' }, clock(c.t)), ' ', cmdBtn, ' ',
						E('span', { 'class': bad ? 'mm-q-warn' : 'mm-faint' }, res)]),
						E('pre', { 'class': 'mm-at-out' }, list(c.lines).join('\n') + (c.final ? (list(c.lines).length ? '\n' : '') + c.final : '') +
							(c.port ? '\n\n(' + _('port %s').format(c.port) + (typeof c.elapsed_ms === 'number' ? ', ' + _('%d ms').format(c.elapsed_ms) : '') + ')' : ''))])]);
			}) : [E('li', { 'class': 'mm-faint' }, _('No commands for the day.'))]);
		}, function(e) { a.histList.replaceChildren(E('li', { 'class': 'mm-faint' }, _('The history is unavailable: %s.').format(failText(e)))); });
	},

	sendAt: function() {
		var self = this, a = this.atUi, cmd = (this.level === 'expert' ? a.cmd.value : a.preset.value).trim();
		if (!/^AT/i.test(cmd)) { this.say(_('An AT command must start with AT.')); return Promise.resolve(); }
		var preset = (this.presetByCmd || {})[cmd], rk = atRisk(cmd, preset);
		if (rk.risk === 'dangerous') { this.say(_('%s is for reference only: a dangerous command is not sent.').format(cmd)); return Promise.resolve(); }
		if (rk.risk !== 'read_only') {
			var fx = [];
			if (rk.effects.indexOf('drops_connection') >= 0) fx.push(_('the link through this modem drops until it is undone'));
			if (rk.effects.indexOf('writes_nvram') >= 0) fx.push(_('the setting stays in the modem after a restart'));
			if (rk.effects.indexOf('requires_reboot') >= 0) fx.push(_('the modem must restart for it to apply'));
			if (!window.confirm(_('%s: %s. Send it?').format(cmd, fx.length ? fx.join('; ') : _('it changes the modem\'s state'))))
				return Promise.resolve();
		}
		a.send.disabled = true;
		a.out.hidden = false;
		text(a.out, cmd + '\n…');
		return atCommand(this.id, cmd).then(function(r) {
			r = r || {};
			if (a.hist.open) self.loadAtHistory();
			if (r.refused) { text(a.out, cmd + '\n\n' + _('Not sent: %s').format(why(r.refused))); return; }
			if (r.error && !r.final) { text(a.out, cmd + '\n\n' + _('Error: %s').format(why(r.error))); return; }
			text(a.out, [cmd, ''].concat(list(r.lines)).concat([r.final || '']).join('\n') +
				(self.level === 'expert' && r.port ? '\n\n(' + _('port %s').format(r.port) + (typeof r.elapsed_ms === 'number' ? ', ' + _('%d ms').format(r.elapsed_ms) : '') + ')' : ''));
		}, function(e) { text(a.out, cmd + '\n\n' + _('The MedveModem service does not answer (%s).').format(failText(e))); })
			.finally(function() { a.send.disabled = false; });
	},

	/* USSD buttons: the SIM operator's codes (not yet checked on a live SIM) and the user's own (UCI config ussd) */
	applyUssdCodes: function() {
		var self = this, s = this.smsUi, r = this.presets || {}, op = r.ussd, plmn = r.plmn || '';
		var own = (this.ussdOwn || []).filter(function(c) { return !c.plmn || c.plmn === plmn; });
		var btn = function(title, code, extra) {
			return E('span', { 'class': 'mm-ussd-chip' }, [E('button', { type: 'button', 'class': 'mm-btn mm-ussd-code', title: code,
				click: function() { s.ussdCode.value = code; self.ussd('start'); } }, [title + ' ', E('span', { 'class': 'mm-mono mm-faint' }, code)])].concat(extra || []));
		};
		/* the first three as buttons, the rest in a list that fills the code field (the owner, 2026-10-15/16) */
		var codes = op ? list(op.codes) : [], more = codes.slice(3);
		var pick = more.length ? E('select', { 'class': 'mm-select mm-ussd-more', 'aria-label': _('Other operator requests'),
			change: function(ev) { if (ev.target.value) { s.ussdCode.value = ev.target.value; s.ussdCode.focus(); } ev.target.value = ''; } },
			[E('option', { value: '' }, _('Other requests…'))].concat(more.map(function(c) { return E('option', { value: c.code }, tr(c.title) + ' — ' + c.code); }))) : null;
		s.ussdCodes.replaceChildren.apply(s.ussdCodes, codes.slice(0, 3).map(function(c) { return btn(tr(c.title), c.code); }).concat(pick ? [pick] : []).concat(
			own.map(function(c) {
				return btn(c.name, c.code, [E('button', { type: 'button', 'class': 'mm-link mm-ussd-del', title: _('Delete your code'),
					click: mm.busy(function() { return self.deleteUssdCode(c['.name']); }) }, '×')]);
			})));
		text(s.ussdOp, op ? _('The codes of the operator “%s” (from open sources, not yet checked on this SIM):').format(tr(op.name)) :
			plmn ? _('No ready codes for the operator %s: add your own.').format(plmn) : _('The SIM\'s operator is not known.'));
	},

	loadUssdOwn: function() {
		var self = this;
		return uciGet('medvemodem', 'ussd').then(function(res) {
			var v = (res && res.values) || {};
			self.ussdOwn = Object.keys(v).map(function(k) { var c = v[k]; c['.name'] = k; return c; })
				.filter(function(c) { return /^[0-9*#+]{2,32}$/.test(c.code || ''); });
			self.applyUssdCodes();
		}, function() {});
	},

	/* the book as UCI has it now; a failed read keeps the last one and blocks writing until a read succeeds (audit r101
	 * R08: it read as an empty book and lifted the limit) */
	loadContacts: function() {
		var self = this;
		return uciGet('medvemodem', 'contact').then(function(res) {
			var v = (res && res.values) || {};
			self.contacts = Object.keys(v).map(function(k) { return { '.name': k, name: words(v[k].name), description: words(v[k].description), number: words(v[k].number) }; })
				.filter(function(c) { return /^\+?\d{3,20}$/.test(c.number); });
			self.contactsFailed = false;
			self.applyContacts();
			return self.contacts;
		}, function() {
			self.contactsFailed = true;
			if (!self.contacts) self.contacts = [];
			self.applyContacts();
			self.say(_('The address book could not be read: adding is off until it is read again.'));
			return null;
		});
	},

	/* the name of a number from the book, by the conversations' key (+7 and 8 are one number) */
	contactName: function(num) {
		var key = smsThreadKey(num), c = list(this.contacts).filter(function(x) { return smsThreadKey(x.number) === key; })[0];
		return c ? c.name : '';
	},

	applyContacts: function() {
		var self = this, s = this.smsUi, all = list(this.contacts);
		if (!s) return;
		s.book.replaceChildren.apply(s.book, all.length ? all.map(function(c) {
			/* as a phone's book: the name with its note under it, the number, the actions together (the owner, 2026-10-26) */
			return E('li', { 'class': 'mm-contact' }, [
				E('span', { 'class': 'mm-contact-who' }, [E('b', {}, c.name || '—'), c.description ? E('span', { 'class': 'mm-faint mm-contact-desc' }, c.description) : '']),
				E('span', { 'class': 'mm-mono mm-contact-num' }, c.number),
				E('span', { 'class': 'mm-contact-acts' }, [
					E('button', { type: 'button', 'class': 'mm-link mm-contact-write', click: function() { self.smsOpen(null); s.number.value = c.number; s.text.focus(); } }, _('Write')),
					E('button', { type: 'button', 'class': 'mm-link mm-contact-del', click: mm.busy(function() { return self.deleteContact(c['.name']); }) }, _('Delete'))])]);
		}) : [E('li', { 'class': 'mm-faint' }, _('No contacts yet.'))]);
		text(s.bookCount, _('%d of %d').format(all.length, CONTACTS_MAX));
		s.bookAdd.disabled = s.bookImport.disabled = all.length >= CONTACTS_MAX || !!this.contactsFailed;
		s.picker.replaceChildren.apply(s.picker, all.map(function(c) { return E('option', { value: c.number, label: c.name }, c.name); }));
		if (this.smsThreads) this.applySmsNames();
	},

	/* the conversations named by the book, where the list already shows them */
	applySmsNames: function() {
		var self = this, s = this.smsUi;
		Array.prototype.forEach.call(s.list.children, function(b) {
			var num = b.getAttribute('data-number'), name = self.contactName(num), h = b.querySelector('.mm-thr-h b');
			var n = +b.getAttribute('data-unread') || 0, cnt = n ? E('span', { 'class': 'mm-thr-new', title: _('Unread: %d').format(n) }, ' (' + n + ')') : '';
			/* the name on its line in the page's font, the number under it: a narrow list broke both mid-word (2026-10-26) */
			if (h) {
				h.classList.toggle('mm-mono', !name);
				h.replaceChildren.apply(h, name ? [E('span', { 'class': 'mm-thr-name' }, [name, cnt]), E('span', { 'class': 'mm-mono mm-faint mm-thr-num' }, num)] :
					[E('span', {}, [num, cnt])]);
			}
		});
		var cur = this.smsCurrent, name = cur ? this.contactName(cur) : '';
		text(s.title, cur ? (name ? name + ' · ' + cur : cur) : _('New message'));
	},

	addContact: function(name, description, number) {
		var self = this;
		/* the limit and the duplicate by the book as it is now: another tab may have written since (audit r101 R08) */
		return this.loadContacts().then(function(fresh) { return fresh ? self.addContactNow(name, description, number) : null; });
	},

	addContactNow: function(name, description, number) {
		var self = this;
		name = String(name || '').trim(); description = String(description || '').trim(); number = String(number || '').replace(/[\s()\-]/g, '');
		if (list(this.contacts).length >= CONTACTS_MAX) { this.say(_('The book holds up to %d numbers.').format(CONTACTS_MAX)); return Promise.resolve(); }
		if (!name || /[\x00-\x1f]/.test(name)) { this.say(_('Name: without control characters.')); return Promise.resolve(); }
		if (/[\x00-\x1f]/.test(description)) { this.say(_('Description: without control characters.')); return Promise.resolve(); }
		if (!/^\+?\d{3,20}$/.test(number)) { this.say(_('Number: digits and “+” at the start, for example +79161234567.')); return Promise.resolve(); }
		if (this.contactName(number)) { this.say(_('This number is already in the book.')); return Promise.resolve(); }
		return uciAdd('medvemodem', 'contact', { name: name.slice(0, 40), description: description.slice(0, 80), number: number }).then(function() { return uciCommit('medvemodem'); })
			.then(function() { self.smsUi.bookName.value = ''; self.smsUi.bookDesc.value = ''; self.smsUi.bookNumber.value = ''; self.say(''); return self.loadContacts(); })
			.catch(function(e) { self.say(_('Saving error: %s').format(failText(e))); });
	},

	deleteContact: function(section) {
		var self = this;
		if (!window.confirm(_('Delete the contact?'))) return Promise.resolve();
		return uciDelete('medvemodem', section).then(function() { return uciCommit('medvemodem'); })
			.then(function() { return self.loadContacts(); })
			.catch(function(e) { self.say(_('Deleting error: %s').format(failText(e))); });
	},

	/* a .vcf from a phone: new numbers only, up to the book's limit, one commit */
	importVcf: function(file) {
		var self = this;
		if (!file) return Promise.resolve();
		if (file.size > 1048576) { this.say(_('The file is larger than 1 MB: export the contacts you need only.')); return Promise.resolve(); }
		return Promise.all([file.text(), this.loadContacts()]).then(function(r) {
			var body = r[0];
			if (!r[1]) return;	/* the book unread: nothing written */
			var found = vcfContacts(body), seen = {}, fresh = [], dup = 0, room = CONTACTS_MAX - list(self.contacts).length;
			list(self.contacts).forEach(function(c) { seen[smsThreadKey(c.number)] = true; });
			found.forEach(function(c) {
				var k = smsThreadKey(c.number);
				if (seen[k]) { dup++; return; }
				seen[k] = true;
				fresh.push(c);
			});
			var take = fresh.slice(0, Math.max(0, room)), over = fresh.length - take.length;
			if (!found.length) { self.say(_('No contacts with a phone number in the file.')); return; }
			return take.reduce(function(p, c) { return p.then(function() { return uciAdd('medvemodem', 'contact', { name: c.name || c.number, description: c.description, number: c.number }); }); }, Promise.resolve())
				.then(function() { return take.length ? uciCommit('medvemodem') : null; })
				.then(function() {
					self.say([_('Imported: %d.').format(take.length), dup ? _('Already in the book: %d.').format(dup) : '',
						over ? _('Not added: %d (the book holds up to %d numbers).').format(over, CONTACTS_MAX) : ''].filter(Boolean).join(' '));
					return self.loadContacts();
				});
		}).catch(function(e) { self.say(_('Import error: %s').format(failText(e))); });
	},

	addUssdCode: function() {
		var self = this, s = this.smsUi, name = s.ownName.value.trim(), code = s.ownCode.value.trim();
		if (!name || /[\x00-\x1f]/.test(name)) { this.say(_('Code name: without control characters.')); return Promise.resolve(); }
		if (!/^[0-9*#+]{2,32}$/.test(code)) { this.say(_('USSD code: digits, “*”, “#” and “+”, for example *100#.')); return Promise.resolve(); }
		var values = { name: name, code: code };
		if ((this.presets || {}).plmn) values.plmn = this.presets.plmn;
		return uciAdd('medvemodem', 'ussd', values).then(function() { return uciCommit('medvemodem'); }).then(function() {
			s.ownName.value = ''; s.ownCode.value = '';
			self.say(_('The code “%s” is saved.').format(name));
			return self.loadUssdOwn();
		}).catch(function(e) { self.say(_('Saving error: %s').format(failText(e))); });
	},

	deleteUssdCode: function(section) {
		var self = this;
		if (!window.confirm(_('Delete your USSD code?'))) return Promise.resolve();
		return uciDelete('medvemodem', section).then(function() { return uciCommit('medvemodem'); })
			.then(function() { return self.loadUssdOwn(); })
			.catch(function(e) { self.say(_('Deleting error: %s').format(failText(e))); });
	},

	/* «SMS»: conversations by number, a new message with the alphabet counter, USSD from «Подробный» (spec 38) */
	smsTab: function() {
		var self = this, s = this.smsUi = {};
		s.note = E('div', { 'class': 'mm-callout is-info mm-sms-note', hidden: '' });
		/* a messenger (field 2026-10-13): conversations on the left, the open one on the right, the reply under it */
		s.list = E('div', { 'class': 'mm-sms-list mm-sms-threads' });
		s.title = E('b', { 'class': 'mm-mono mm-sms-title' });
		s.back = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sms-back', click: function() { s.box.classList.remove('is-thread'); } }, _('← Back'));
		s.delAll = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sms-delall', hidden: '', click: function() {
			var msgs = (self.smsThreads || {})[self.smsCurrent] || [];
			self.deleteSms(msgs.map(function(m) { return m.id; }));
		} }, _('Delete the conversation'));
		/* several at once (bench 2026-10-15): tick the messages, then «Удалить выбранные» */
		s.delSel = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sms-delsel', hidden: '', click: function() {
			self.deleteSms(Array.prototype.map.call(s.msgs.querySelectorAll('.mm-sms-pick:checked'), function(c) { return +c.value; }));
		} }, _('Delete the selected'));
		s.head = E('div', { 'class': 'mm-row mm-sms-pane-head' }, [s.back, s.title, s.delSel, s.delAll]);
		s.msgs = E('div', { 'class': 'mm-bubs mm-sms-msgs' });
		s.alpha = E('p', { 'class': 'mm-faint mm-sms-alpha', hidden: '' }, _('A letter sender cannot be answered.'));
		s.newBtn = E('button', { type: 'button', 'class': 'mm-btn mm-sms-new', click: function() { self.smsOpen(null); } }, _('New message'));
		s.empty = E('p', { 'class': 'mm-faint mm-sms-empty', hidden: '' }, _('No messages.'));
		/* a search over the conversations (the owner, 2026-11-15): the number, a contact's name, the text */
		s.search = E('input', { type: 'search', 'class': 'mm-input mm-sms-search', id: 'mm-sms-search', autocomplete: 'off',
			placeholder: _('Search the messages'), 'aria-label': _('Search the messages'), input: function() { self.smsList(); } });
		s.none = E('p', { 'class': 'mm-faint mm-sms-none', hidden: '' });
		s.read = E('span', { 'class': 'mm-faint' });
		s.export = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sms-export', click: function() { self.exportSms(); } }, _('Export'));
		/* what stops reception, said with the way out (the owner, 2026-10-16) */
		s.healthText = E('span');
		s.healthFix = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sms-unstick', hidden: '',
			click: mm.busy(function() { return self.deleteSms(self.smsStuck || []); }) }, _('Delete the unsent'));
		/* a full storage (field 2026-10-17, an E171: 5 of 5, one long message in parts): every message and part in one go */
		s.healthAll = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sms-delall-store', hidden: '',
			click: mm.busy(function() { return self.clearSms(); }) }, _('Delete all messages'));
		s.health = E('div', { 'class': 'mm-callout is-warn mm-sms-health', hidden: '' }, [s.healthText, ' ', s.healthFix, ' ', s.healthAll]);
		s.refresh = E('button', { type: 'button', 'class': 'mm-btn is-quiet', click: mm.busy(function() { return self.refreshSms(); }) }, _('Refresh'));
		s.picker = E('datalist', { id: 'mm-sms-contacts' });
		s.number = E('input', { type: 'tel', 'class': 'mm-input', id: 'mm-sms-number', autocomplete: 'off', placeholder: '+7…', maxlength: '21', list: 'mm-sms-contacts' });
		s.numField = E('label', { 'for': 'mm-sms-number', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Number')), s.number]);
		s.text = E('textarea', { 'class': 'mm-input mm-sms-text', id: 'mm-sms-text', rows: '4', maxlength: '1600',
			input: function() { self.smsCount(); } });
		s.count = E('span', { 'class': 'mm-faint mm-sms-count' });
		s.send = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-sms-send', click: mm.busy(function() { return self.sendSms(); }) }, _('Send'));
		s.ussdCode = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-ussd-code', autocomplete: 'off', placeholder: '*100#', maxlength: '32',
			keydown: function(ev) { if (ev.key === 'Enter') self.ussd('start'); } });
		s.ussdGo = E('button', { type: 'button', 'class': 'mm-btn mm-ussd-go', click: mm.busy(function() { return self.ussd('start'); }) }, _('Send the request'));
		s.ussdOut = E('pre', { 'class': 'mm-ussd-out', hidden: '' });
		s.ussdReply = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-ussd-reply', autocomplete: 'off', maxlength: '64', placeholder: _('Reply, for example 1'),
			keydown: function(ev) { if (ev.key === 'Enter') self.ussd('respond'); } });
		s.ussdAnswer = E('div', { 'class': 'mm-row mm-ussd-answer', hidden: '' }, [s.ussdReply,
			E('button', { type: 'button', 'class': 'mm-btn', click: mm.busy(function() { return self.ussd('respond'); }) }, _('Reply'))]);
		s.ussdStop = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-ussd-cancel', hidden: '', click: mm.busy(function() { return self.ussd('cancel'); }) }, _('End the session'));
		s.ussdOp = E('p', { 'class': 'mm-faint mm-ussd-op' });
		s.ussdCodes = E('div', { 'class': 'mm-row mm-ussd-codes' });
		s.ownName = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-ussd-own-name', placeholder: _('Name, for example “Bonuses”'), maxlength: '40', autocomplete: 'off' });
		s.ownCode = E('input', { type: 'text', 'class': 'mm-input mm-mono', id: 'mm-ussd-own-code', placeholder: '*111#', maxlength: '32', autocomplete: 'off' });
		s.ussdCard = this.sec('ussd', _('USSD request'), [
			E('p', { 'class': 'mm-faint' }, _('Balance, the rest of a package, the operator\'s menu. The network\'s answer may take 5–30 seconds; on some modems and in LTE-only networks USSD does not work.')),
			(s.ussdNote = E('p', { 'class': 'mm-callout mm-ussd-note', hidden: '' })), s.ussdOp, s.ussdCodes,
			E('div', { 'class': 'mm-row' }, [s.ussdCode, s.ussdGo]), s.ussdOut, s.ussdAnswer, E('div', { 'class': 'mm-row' }, s.ussdStop),
			E('details', { 'class': 'mm-ussd-own' }, [E('summary', {}, _('Add your code')),
				E('div', { 'class': 'mm-row' }, [s.ownName, s.ownCode,
					E('button', { type: 'button', 'class': 'mm-btn mm-ussd-add', click: mm.busy(function() { return self.addUssdCode(); }) }, _('Save'))])])]);
		s.ussdCard.hidden = this.level === 'normal';
		s.book = E('ul', { 'class': 'mm-contacts' });
		s.bookCount = E('span', { 'class': 'mm-faint mm-contacts-count' });
		s.bookName = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-contact-name', placeholder: _('Name'), maxlength: '40', autocomplete: 'off', 'aria-label': _('Name') });
		s.bookDesc = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-contact-desc', placeholder: _('Description, for example “dacha, the neighbour”'), maxlength: '80', autocomplete: 'off', 'aria-label': _('Description') });
		s.bookNumber = E('input', { type: 'tel', 'class': 'mm-input mm-mono', id: 'mm-contact-number', placeholder: '+7…', maxlength: '21', autocomplete: 'off', 'aria-label': _('Number') });
		s.bookAdd = E('button', { type: 'button', 'class': 'mm-btn mm-contact-add', click: mm.busy(function() { return self.addContact(s.bookName.value, s.bookDesc.value, s.bookNumber.value); }) }, _('Add'));
		s.bookFile = E('input', { type: 'file', accept: '.vcf,text/vcard,text/x-vcard', hidden: '', change: function() {
			var f = s.bookFile.files && s.bookFile.files[0];
			s.bookFile.value = '';
			self.importVcf(f);
		} });
		s.bookImport = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-contact-import', click: function() { s.bookFile.click(); } }, _('Import .vcf'));
		s.bookCard = this.sec('contacts', _('Contacts'), [
			E('p', { 'class': 'mm-faint' }, _('Names instead of numbers in the conversations and a choice when writing; one book for all modems, up to 20 numbers. A .vcf file is exported by a phone’s Contacts app.')),
			E('div', { 'class': 'mm-row' }, [s.bookCount, s.bookImport, s.bookFile]), s.book,
			E('div', { 'class': 'mm-row' }, [s.bookName, s.bookDesc, s.bookNumber, s.bookAdd])]);
		this.smsCount();
		return [s.note,
			this.sec('sms', _('Messages'), [E('div', { 'class': 'mm-row mm-sms-bar' }, [s.read, E('span', { 'class': 'mm-row' }, [s.newBtn, s.refresh, s.export])]),
				s.health,
				s.box = E('div', { 'class': 'mm-sms' }, [E('div', { 'class': 'mm-sms-side' }, [s.search, s.empty, s.none, s.list]),
					E('div', { 'class': 'mm-sms-pane' }, [s.head, s.msgs, s.alpha,
						s.compose = E('div', { 'class': 'mm-sms-form' }, [s.numField,
							E('label', { 'for': 'mm-sms-text', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Text')), s.text]),
							E('div', { 'class': 'mm-row mm-sms-actions' }, [s.count, s.send])])])]),
				E('p', { 'class': 'mm-faint mm-b' }, _('Messages are kept in the modem or on the SIM card; when it fills up, the oldest move to the router.')),
				E('p', { 'class': 'mm-faint mm-x' }, _('Messages are kept in the modem or on the SIM card. When its storage is three quarters full, the oldest received ones move to the router (the last 500 are kept there until deleted), so new ones keep coming. The modem does not keep those sent through the AT port: they show here until the engine restarts.'))]),
			s.picker, s.bookCard, s.ussdCard];
	},

	smsCount: function() {
		var s = this.smsUi, m = smsMeasure(s.text.value);
		var plain = this.level === 'normal';
		text(s.count, !m.units ? (plain ? _('Up to 160 characters in one SMS, up to 70 in Cyrillic') : _('GSM-7: up to 160 characters in one SMS, Cyrillic (UCS-2): up to 70')) :
			(plain ? '' : (m.gsm7 ? 'GSM-7' : 'UCS-2') + ' · ') + m.units + ' / ' + m.limit + ' · ' + m.segments + ' SMS' +
			(m.segments > 1 ? ' ' + _('(long: the operator counts each part)') : ''));
		s.count.classList.toggle('is-warn', m.segments > 3);
	},

	loadSms: function(refresh) {
		var self = this, s = this.smsUi;
		if (this.smsPending) return this.smsPending;
		this.smsPending = smsList(this.id, !!refresh).then(function(r) {
			self.applySms(r || {});
		}, function(e) {
			s.note.hidden = false;
			text(s.note, /Method not found|-32601|not found/i.test(String(failText(e))) ?
				_('This version of the MedveModem service cannot work with SMS; update the medvemodem package.') : _('The MedveModem service does not answer.'));
		}).finally(function() { self.smsPending = null; });
		return this.smsPending;
	},

	/* who sent an outgoing message: this SIM by its label and number (the owner, 2026-10-16: not «Вы») */
	smsOwnName: function() {
		var d = this.simLast || {}, lab = d.sim_label || {}, mm = d.modemmanager || {};
		var own = list(mm.own_numbers).filter(function(n) { return typeof n === 'string'; });
		var name = [words(lab.description), words(lab.number) || own[0] || ''].filter(Boolean).join(' · ');
		return name || _('This SIM card');
	},

	/* every message of the modem as a CSV file (UTF-8 with a BOM: a spreadsheet opens it as is) */
	exportSms: function() {
		var self = this, rows = [[_('Time'), _('Direction'), _('Number'), _('Text'), _('State'), _('Memory'), _('Number in the storage')]];
		var all = list((this.smsLast || {}).messages).slice().sort(smsOrder);
		if (!all.length) { this.say(_('No messages to export.')); return; }
		all.forEach(function(m) {
			var out = m.type === 'submit', t = typeof m.epoch === 'number' ? new Date(m.epoch * 1000).toLocaleString(LANG) : words(m.timestamp);
			rows.push([t, out ? _('outgoing (%s)').format(self.smsOwnName()) : _('incoming'), words(m.number), m.binary ? _('[binary message]') : words(m.text),
				SMS_STATE[m.state] || words(m.state), STORE[m.storage] || words(m.storage), m.local || m.storage === 'router' ? '' : String(m.id)]);
		});
		var csv = '\ufeff' + rows.map(function(r) { return r.map(function(v) { return '"' + String(v == null ? '' : v).replace(/"/g, '""') + '"'; }).join(';'); }).join('\r\n');
		var a = E('a', { href: URL.createObjectURL(new Blob([csv], { type: 'text/csv;charset=utf-8' })),
			download: 'sms-' + String(this.id).replace(/[^A-Za-z0-9]+/g, '-') + '-' + new Date().toISOString().slice(0, 10) + '.csv' });
		document.body.appendChild(a); a.click(); a.remove();
		this.say(_('Messages exported: %d.').format(all.length));
	},

	/* «Обновить»: the modem read again, and the page says what came of it (the owner, 2026-10-16) */
	refreshSms: function() {
		var self = this, keys = function() {
			return list((self.smsLast || {}).messages).filter(function(m) { return m.type !== 'submit'; })
				.map(function(m) { return [m.id, m.number, m.text].join('|'); });
		}, was;
		this.say(_('Reading the messages from the modem…'));
		/* a read already under way finishes first: «новых» counts from what the page showed, and this read is a fresh one */
		var go = function() { was = keys(); return self.loadSms(true); };
		return (this.smsPending ? this.smsPending.then(go, go) : go()).then(function() {
			var r = self.smsLast || {}, now = keys(), fresh = now.filter(function(k) { return was.indexOf(k) < 0; }).length;
			if (typeof r.refused === 'string') self.say(_('SMS unavailable: %s').format(why(r.refused)));
			else if (r.error) self.say(_('The messages are not read: %s.').format(why(r.error)));
			else self.say(_('The messages are read from the modem at %s:').format(new Date().toLocaleTimeString(LANG)) + ' ' +
				(fresh ? _('new — %d.').format(fresh) : _('none new.')) + (r.store && typeof r.store.used === 'number' ?
				' ' + _('In the modem\'s storage %s of %s.').format(r.store.used, r.store.total) : ''));
		});
	},

	applySms: function(r) {
		this.smsLast = r;
		var self = this, s = this.smsUi, threads = {}, order = [];
		var off = typeof r.refused === 'string';
		s.note.hidden = !off && !r.error && !r.fallback;
		text(s.note, off ? _('SMS unavailable: %s').format(why(r.refused)) : r.error ? _('The messages are not read: %s.').format(why(r.error)) + ' ' + _('The last received are shown.') :
			r.fallback ? _('ModemManager did not give the messages') + (this.level !== 'normal' ? ' (' + r.fallback + ')' : '') + '; ' + _('the modem\'s storage read through the AT port is shown.') : '');
		/* not while a USSD request runs: the SMS list's own refresh turned the button on again each time, and it blinked
		 * on and off for the whole wait (the owner's T77, 2026-11-02) */
		s.send.disabled = off; s.ussdGo.disabled = off || !!this.ussdSince;
		var st = r.store || {};
		s.read.textContent = typeof r.read_at === 'number' ? (r.path === 'at' ? _('Read through the AT port at %s') : r.path === 'web' ? _('Read from the modem\'s web page at %s') :
			r.path === 'qmodem' ? _('Read through QModem at %s') :
			_('Read from the modem at %s')).format(new Date(r.read_at * 1000).toLocaleTimeString(LANG)) +
			(typeof st.used === 'number' && typeof st.total === 'number' ? ' · ' + (STORE[st.name] || st.name || _('storage')) + ': ' + _('%s of %s').format(st.used, st.total) : '') : '';
		s.read.classList.toggle('is-warn', typeof st.used === 'number' && st.total > 0 && st.used >= st.total - 2);
		var h = r.health || {}, said = [];
		this.smsStuck = list(h.stuck);
		if (this.smsStuck.length) said.push((this.smsStuck.length === 1 ? _('An unsent message lies in the modem\'s storage') :
			_('Unsent messages lie in the modem\'s storage (%d)').format(this.smsStuck.length)) + ': ' + _('it may take the cell the modem puts new incoming messages into, and then they are lost. Delete it.'));
		if (h.receiving) said.push(_('A long message is being received in parts (%s); if the other parts do not come, the engine deletes it in 5 minutes.').format(h.receiving));
		if (st.incoming) said.push(_('The modem puts new incoming messages into one storage (%s), and the list is read from another (%s): new messages may not show here. Choose one SMS storage in the modem\'s “Settings”.')
			.format(STORE[st.incoming] || st.incoming, STORE[st.name] || st.name || '?'));
		if (h.full === true) said.push(_('The SMS storage is full: new messages are not received. Delete the unneeded ones.'));
		else if (h.full === false) said.push(_('The SMS storage is almost full: delete the unneeded ones, or new ones stop being received.'));
		s.health.hidden = !said.length;
		text(s.healthText, said.join(' '));
		s.healthFix.hidden = !this.smsStuck.length;
		var all = list(r.messages), ids = {};
		all.forEach(function(m) { [m.id].concat(list(m.parts)).forEach(function(n) { if (typeof n === 'number') ids[n] = true; }); });
		this.smsAllIds = Object.keys(ids).map(Number);
		/* the full storage whose parts the list never showed is exactly the empty-list case (review 2026-10-17) */
		s.healthAll.hidden = h.full === undefined;
		smsKeys(this.id, all, typeof r.read_at === 'number' ? r.read_at : Math.floor(Date.now() / 1000));
		/* the unread ones (the engine says which, before its reply marks them seen) stay marked on this page until their
		 * conversation is opened (the owner, 2026-11-09: «непрочитанные помечать заливкой», their count per correspondent) */
		var unreadIds = list(r.unread_ids);
		this.smsNew = this.smsNew || {};
		this.smsLooked = this.smsLooked || {};
		/* one that came into the open conversation is read already: filled, not counted */
		all.forEach(function(m) { if (unreadIds.indexOf(m.id) < 0) return;
			if (smsThreadKey(m.number) === self.smsCurrent && s.box.classList.contains('is-thread')) self.smsLooked[smsSig(m)] = true;
			else self.smsNew[smsSig(m)] = true; });
		all.forEach(function(m) {
			var key = smsThreadKey(m.number);
			if (!threads[key]) { threads[key] = []; order.push(key); }
			threads[key].push(m);
		});
		/* the newest conversation first, by the same key as the messages in it */
		function last(k) { return Math.max.apply(null, threads[k].map(function(m) { return m.key || 0; })); }
		order.sort(function(a, b) { return last(b) - last(a); });
		s.empty.hidden = off || order.length > 0;
		this.smsThreads = threads;
		this.smsOrder = order;
		this.smsList();
		/* the open conversation stays open across polls; the newest one opens first on a wide screen */
		if (this.smsCurrent === undefined || (this.smsCurrent && !threads[this.smsCurrent]))
			this.smsCurrent = order.length ? order[0] : null;
		this.smsRender();
		this.applySmsNames();
	},

	/* the conversations' list, narrowed by the search box */
	smsList: function() {
		var self = this, s = this.smsUi, threads = this.smsThreads || {}, q = s.search ? s.search.value : '';
		var shown = (this.smsOrder || []).filter(function(num) { return smsMatches(q, num, self.contactName(num), threads[num]); });
		s.none.hidden = !q.trim() || shown.length > 0 || !(this.smsOrder || []).length;
		text(s.none, _('Nothing found for “%s”.').format(q.trim()));
		s.list.replaceChildren.apply(s.list, shown.map(function(num) {
			var msgs = threads[num].slice().sort(smsOrder), tail = msgs[msgs.length - 1];
			var fresh = threads[num].filter(function(m) { return (self.smsNew || {})[smsSig(m)]; }).length;
			return E('button', { type: 'button', 'class': 'mm-thr mm-sms-thread' + (num === self.smsCurrent ? ' is-sel' : '') + (fresh ? ' is-unread' : '') +
				(smsOperator(num) ? ' is-op' : ''), title: smsOperator(num) ? _('The operator\'s own messages') : null,
				'data-number': num, 'data-unread': String(fresh), click: function() { self.smsOpen(num); } }, [
				E('span', { 'class': 'mm-thr-h' }, [E('b', { 'class': 'mm-mono' }, num), E('span', { 'class': 'mm-faint' }, smsWhen(tail))]),
				E('span', { 'class': 'mm-thr-x mm-sms-preview' }, (tail.type === 'submit' ? _('You:') + ' ' : '') + (tail.binary ? _('[data]') : words(tail.text)) +
					(msgs.length > 1 ? ' · ' + msgs.length : ''))]);
		}));
		this.applySmsNames();
	},

	/* the open conversation: num null is a new message */
	smsOpen: function(num) {
		var s = this.smsUi, self = this;
		this.smsCurrent = num;
		/* opened: its messages are read; their marks and its count go (the fill stays for this one look) */
		this.smsLooked = {};
		((this.smsThreads || {})[num] || []).forEach(function(m) {
			if (self.smsNew && self.smsNew[smsSig(m)]) { self.smsLooked[smsSig(m)] = true; delete self.smsNew[smsSig(m)]; } });
		Array.prototype.forEach.call(s.list.children, function(b) {
			if (b.getAttribute('data-number') === num) { b.classList.remove('is-unread'); b.setAttribute('data-unread', '0'); } });
		this.applySmsNames();
		Array.prototype.forEach.call(s.list.children, function(b) { b.classList.toggle('is-sel', b.getAttribute('data-number') === num); });
		s.box.classList.add('is-thread');
		this.smsRender();
		(num === null ? s.number : s.text).focus();
	},

	smsRender: function() {
		var self = this, s = this.smsUi, num = this.smsCurrent, msgs = num ? (this.smsThreads || {})[num] || [] : [];
		var numeric = num && /^\+?[0-9]{3,20}$/.test(num);
		text(s.title, num ? (this.contactName(num) ? this.contactName(num) + ' · ' + num : num) : _('New message'));
		s.delAll.hidden = !num;
		s.numField.hidden = !!num;
		if (num && numeric) s.number.value = num;
		else if (!num && this.smsNumberFor !== null) s.number.value = '';
		this.smsNumberFor = num;
		s.alpha.hidden = !num || numeric;
		s.compose.hidden = !!num && !numeric;
		/* the newest on top (the owner, 2026-11-09: «свежие смс надо бы сверху»): the page stays at the top for them */
		var stick = s.msgs.scrollTop < 40;
		/* the ticks survive a poll's redraw (the same conversation only) */
		var picked = this.smsPickedFor === num ? Array.prototype.map.call(s.msgs.querySelectorAll('.mm-sms-pick:checked'), function(c) { return c.value; }) : [];
		this.smsPickedFor = num;
		s.msgs.replaceChildren.apply(s.msgs, msgs.slice().sort(smsOrder).reverse().map(function(m) {
			var out = m.type === 'submit', st = SMS_STATE[m.state], fresh = (self.smsNew || {})[smsSig(m)] || (self.smsLooked || {})[smsSig(m)];
			return E('div', { 'class': 'mm-bub mm-sms-msg' + (out ? ' is-out' : '') + (fresh ? ' is-new' : '') + (!out && smsOperator(m.number) ? ' is-op' : '') }, [
				E('label', { 'class': 'mm-sms-pickl', title: _('Select for deletion') }, [(function() { var c = E('input', { type: 'checkbox', 'class': 'mm-sms-pick', value: String(m.id),
					change: function() { self.smsPicked(); } }); c.checked = picked.indexOf(String(m.id)) >= 0; return c; })(), E('span', { 'class': 'mm-dim' }, out ? self.smsOwnName() : self.contactName(num) || num)]),
				E('div', { 'class': 'mm-sms-body' }, m.binary ? _('[binary message, no text]') : smsLinked(m.text)),
				E('div', { 'class': 'mm-bub-t mm-sms-meta' }, [smsWhen(m), out || st ? (out ? _('outgoing') : '') + (st ? (out ? ', ' : '') + st : '') : '',
					typeof m.have === 'number' ? _('part %s of %s, waiting for the rest').format(m.have, m.total) : '',
					m.storage === 'router' ? _('moved to the router') : '',
					self.level === 'expert' && !m.local && m.storage !== 'router' ? '№ ' + m.id + (m.storage ? ' · ' + m.storage : '') : ''].filter(Boolean).join(' · ')),
				out || m.binary || !smsCode(m.text) ? '' : E('button', { type: 'button', 'class': 'mm-link mm-sms-code', title: _('Copy the code %s').format(smsCode(m.text)),
					click: function() { copyText(smsCode(m.text)).then(function() { self.say(_('The code %s is copied.').format(smsCode(m.text))); },
						function() { self.say(_('The browser did not let copy: the code %s.').format(smsCode(m.text))); }); } }, _('Code %s').format(smsCode(m.text))),
				E('button', { type: 'button', 'class': 'mm-link mm-sms-del', click: mm.busy(function() { return self.deleteSms([m.id]); }) }, _('Delete'))]);
		}));
		if (stick) s.msgs.scrollTop = 0;
		this.smsPicked();
	},

	smsPicked: function() {
		var s = this.smsUi, n = s.msgs.querySelectorAll('.mm-sms-pick:checked').length;
		s.delSel.hidden = !n;
		text(s.delSel, _('Delete the selected (%d)').format(n));
	},

	sendSms: function() {
		var self = this, s = this.smsUi, number = s.number.value.replace(/[\s()-]/g, ''), body = s.text.value;
		if (!/^\+?[0-9]{3,20}$/.test(number)) { this.say(_('Number: digits and “+” at the start, 3 to 20 digits.')); return Promise.resolve(); }
		if (!body.trim()) { this.say(_('Enter the message text.')); return Promise.resolve(); }
		var m = smsMeasure(body);
		if (m.segments > 1 && !window.confirm(_('The message will be sent in %d parts, the operator counts each. Send?').format(m.segments)))
			return Promise.resolve();
		s.send.disabled = true;
		this.say(_('Sending an SMS to %s…').format(number));
		return smsSend(this.id, number, body).then(function(r) {
			r = r || {};
			if (r.refused) { self.say(_('SMS not sent: %s').format(why(r.refused))); return; }
			if (!r.ok) {
				self.say(_('SMS not sent: %s').format(SMS_SEND_ERR[r.error] || r.error || _('no answer')) +
					(r.detail && self.level !== 'normal' ? ' (' + r.detail + ')' : '') + '.');
				return;
			}
			s.text.value = ''; self.smsCount();
			self.smsCurrent = smsThreadKey(number);
			self.say(_('SMS to %s sent').format(number) + (r.via === 'at' ? ' ' + _('through the modem\'s AT port') +
				(r.mm_error ? ' (' + _('ModemManager could not') + (self.level !== 'normal' ? ': ' + r.mm_error : '') + ')' : '') : '') + '.');
			return self.loadSms(true);
		}, function(e) { self.say(_('SMS not sent: %s').format(_('the MedveModem service does not answer (%s).').format(failText(e)))); })
			.finally(function() { s.send.disabled = typeof (self.smsLast || {}).refused === 'string'; });  // the newest answer decides
	},

	/* the whole storage in one AT command, the parts the list never showed too; where the AT port is ModemManager's,
	 * every listed message and part one by one */
	clearSms: function() {
		var self = this;
		if (!window.confirm(_('Delete all messages from the storage, including parts of long ones not seen in the list?')))
			return Promise.resolve();
		this.say(_('Clearing the SMS storage…'));
		return smsClear(this.id).then(function(r) {
			r = r || {};
			/* one by one only where the AT port is ModemManager's; a busy modem or a running recovery is said */
			if (r.refused && /AT port is ModemManager's/.test(r.refused) && (self.smsAllIds || []).length) {
				var ids = self.smsAllIds || [], failed = 0;
				return ids.reduce(function(p, n) {
					return p.then(function() { return smsDelete(self.id, n).then(function(x) { if (!x || !x.ok) failed++; }, function() { failed++; }); });
				}, Promise.resolve()).then(function() {
					self.say(failed ? _('Messages not deleted: %d.').format(failed) : _('Messages deleted: %d.').format(ids.length));
				});
			}
			self.say(r.ok ? _('The SMS storage is cleared.') : _('The storage is not cleared: %s.').format(r.error === 'the modem did not clear the storage' ? _('the modem did not take the command') :
				why(r.refused || r.error || _('no answer'))));
		}, function(e) { self.say(_('The storage is not cleared: %s.').format(failText(e))); }).then(function() { return self.loadSms(true); });
	},

	deleteSms: function(ids) {
		var self = this;
		if (!ids.length) return Promise.resolve();
		if (!window.confirm(ids.length > 1 ? N_(ids.length, 'Delete %d message from the modem?', 'Delete %d messages from the modem?').format(ids.length) : _('Delete the message from the modem?')))
			return Promise.resolve();
		var failed = 0;
		return ids.reduce(function(p, n) {
			return p.then(function() { return smsDelete(self.id, n).then(function(r) { if (!r || !r.ok) failed++; }, function() { failed++; }); });
		}, Promise.resolve()).then(function() {
			self.say(failed ? _('Messages not deleted: %d.').format(failed) : ids.length > 1 ? _('Messages deleted: %d.').format(ids.length) : _('The message is deleted.'));
			return self.loadSms(true);
		});
	},

	/* USSD: start answers at once, the result is polled (U2); a menu waits for the operator's reply */
	ussd: function(what) {
		var self = this, s = this.smsUi, call;
		if (what === 'start') {
			var code = s.ussdCode.value.trim();
			if (!/^[0-9*#+]{2,32}$/.test(code)) { this.say(_('USSD code: digits, “*”, “#” and “+”, for example *100#.')); return Promise.resolve(); }
			/* a model whose USSD on LTE falls back to 3G and can drop the data session (bench 2026-10-16, the T99): asked first */
			var d = this.data || {}, tech = String((d.modemmanager || {}).access_tech || '');
			if ((((d.profile || {}).radio || {}).ussd_cs_fallback) && /lte|5gnr/i.test(tech) &&
				!window.confirm(_('On this modem USSD in an LTE network moves it to 3G (so the operator serves the requests), and the internet through the modem may break for the request\'s time, sometimes longer: the link comes back by itself. Send %s?').format(code)))
				return Promise.resolve();
			call = ussdStart(this.id, code);
		} else if (what === 'respond') {
			var reply = s.ussdReply.value.trim();
			if (!reply || /['"]/.test(reply)) { this.say(_('Reply: up to 64 characters, without quotes.')); return Promise.resolve(); }
			call = ussdRespond(this.id, reply);
		} else {
			call = ussdCancel(this.id);
		}
		s.ussdGo.disabled = true;
		return call.then(function(r) {
			r = r || {};
			if (r.refused) { s.ussdGo.disabled = false; self.say('USSD: ' + why(r.refused)); return; }
			if (what === 'respond') s.ussdReply.value = '';
			self.say('');
			s.ussdOut.hidden = false;
			if (what !== 'cancel') text(s.ussdOut, _('Waiting for the network\'s answer…'));
			self.ussdSince = Date.now();
			return self.ussdPoll();
		}, function(e) { s.ussdGo.disabled = false; self.say('USSD: ' + _('the MedveModem service does not answer (%s).').format(failText(e))); });
	},

	ussdPoll: function() {
		var self = this, s = this.smsUi;
		window.clearTimeout(this.ussdTimer);
		return ussdStatus(this.id).then(function(st) {
			st = st || {};
			var running = st.state === 'running', menu = st.state === 'done' && st.session === 'user-response';
			s.ussdGo.disabled = running;
			s.ussdAnswer.hidden = !menu;
			s.ussdStop.hidden = !(menu || running);
			if (running) {
				var sec = self.ussdSince ? Math.round((Date.now() - self.ussdSince) / 1000) : 0;
				s.ussdOut.hidden = false;
				text(s.ussdOut, _('Waiting for the network\'s answer…') + (sec >= 3 ? ' ' + _('%d s').format(sec) : ''));
				self.ussdWaiting = true;
				self.ussdTimer = window.setTimeout(function() { self.ussdPoll(); }, 1000);
				return;
			}
			self.ussdSince = null;
			if (st.state === 'idle') { if (!s.ussdOut.hidden && self.ussdWaiting) s.ussdOut.hidden = true; self.ussdWaiting = false; return; }
			self.ussdWaiting = false;
			s.ussdOut.hidden = false;
			text(s.ussdOut, (st.state === 'done' ? (words(st.reply) || _('The network answered without text.')) +
					(menu ? '\n\n' + _('Reply within a minute: then the menu closes by itself.') : '') :
				st.state === 'cancelled' && st.closed ? _('The menu is closed: there was no reply within a minute.') :
				st.state === 'cancelled' ? _('The USSD session ended.') :
				/* not always a failure: some operators send the answer by SMS after it (4PDA: Yota) */
				/terminated by network/i.test(st.error || '') ? _('The network closed the session without text. Some operators send the answer by SMS: check the incoming messages.') :
				_('No answer: %s').format(why(st.error || '') || _('the network did not answer'))) +
				(st.via === 'at' && st.mm_error && self.level !== 'normal' ? '\n\n(' + _('ModemManager could not: %s; the request went through the modem\'s AT port').format(st.mm_error) + ')' : ''));
			if (menu) {
				s.ussdReply.focus();
				/* the engine closes an unanswered menu after a minute: the page learns it */
				self.ussdTimer = window.setTimeout(function() { self.ussdPoll(); }, 5000);
			}
		}, function(e) {
			/* a missed status read is not the end of the request: keep asking while it may run (the engine answers in 75 s) */
			if (self.ussdSince && Date.now() - self.ussdSince < 90000) {
				self.ussdTimer = window.setTimeout(function() { self.ussdPoll(); }, 2000);
				return;
			}
			self.ussdSince = null;
			s.ussdGo.disabled = false;
			s.ussdOut.hidden = false;
			text(s.ussdOut, _('The request\'s state is not received: %s').format(_('the MedveModem service does not answer (%s).').format(failText(e))));
		});
	},

	/* the modem at a glance (the mockup's hero): verdict, network, signal, address, session, traffic */
	makeHero: function() {
		var self = this, h = {};
		h.mark = E('span', { 'class': 'mm-verdict-mark', 'aria-hidden': 'true' }, '…');
		h.title = E('h2', { 'class': 'mm-hero-name' });
		h.model = E('span', { 'class': 'mm-faint' });
		h.pill = E('span', { 'class': 'mm-pill' });
		h.ownerTag = E('span', { 'class': 'mm-owner-tag', hidden: true });
		h.sayB = E('b'); h.sayText = E('span');
		h.badge = E('span', { 'class': 'mm-badge-slot' });
		h.net = E('span'); h.bars = barsNode(); h.sig = E('span');
		/* «Подробный»: the aggregation in use beside the signal (the owner, 2026-10-19) */
		h.ca = E('span', { 'class': 'mm-faint mm-hero-ca' });
		h.addr = E('dd', { 'class': 'mm-mono' }); h.uptime = E('dd'); h.traffic = E('dd'); h.rates = E('dd');
		h.check = E('button', { type: 'button', 'class': 'mm-btn mm-check-now', click: mm.busy(function() { return self.checkNow(); }) }, _('Check the connection'));
		/* the fix by one button (the owner, 2026-10-17: an E171 with no interface stayed «Нет соединения» until «Сохранить и подключить») */
		h.connect = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-hero-connect', hidden: '', click: mm.busy(function() { return self.connectNow(); }) }, _('Connect'));
		var menu = E('details', { 'class': 'mm-menu' }), sum = E('summary', { 'class': 'mm-btn', 'aria-label': _('Other actions') }, [_('More options') + ' ']);
		sum.appendChild(chev());
		h.reconnect = E('button', { type: 'button', 'class': 'mm-btn is-quiet', click: function() { menu.removeAttribute('open'); self.run('session'); } }, _('Reconnect the internet'));
		menu.appendChild(sum);
		menu.appendChild(E('div', { 'class': 'mm-pop' }, [h.reconnect,
			E('button', { type: 'button', 'class': 'mm-btn is-quiet', click: function() { menu.removeAttribute('open'); self.show('service'); } }, _('Recovery steps →'))]));
		/* the modem's own web through the router, as on the main page's card (the owner, 2026-10-30: «ярлык ВЕБ морды на
		 * карточке модема есть только на главной странице, при заходе в сам модем уже нет») */
		h.web = E('a', { 'class': 'mm-web-chip mm-hero-web', target: '_blank', rel: 'noopener noreferrer', hidden: true }, _('🌐 Web'));
		h.web.addEventListener('click', function(ev) {
			var url = h.web.getAttribute('href');
			if (!url || h.web.dataset.open === '1') return;
			ev.preventDefault();
			var win = window.open('about:blank', '_blank');
			webOpen(self.id).then(function() {
				setTimeout(function() { if (win) win.location = url; else location.href = url; }, 500);
			}, function(e) {
				if (win) win.close();
				mm.toast('web', _('The web interface forward did not open: %s').format(failText(e)), 'err', 8000);
			});
		});
		var nm = this.nameEditor();
		h.node = E('section', { 'class': 'mm-card mm-hero mm-header' }, [
			E('div', { 'class': 'mm-hero-top' }, [h.mark, E('div', { 'class': 'mm-hero-id' }, [E('div', { 'class': 'mm-hero-name-row' }, [h.title, h.ownerTag, nm.open, h.web]),
				h.model, h.pill])]),
			nm.box,
			E('div', { 'class': 'mm-hero-say' }, [h.sayB, h.sayText]),
			h.chain = E('ol', { 'class': 'mm-chain', 'aria-label': _('Diagnosis by chain'), hidden: '' }),
			h.chainSay = E('p', { 'class': 'mm-faint mm-chain-say', hidden: '' }),
			h.grid = E('div', { 'class': 'mm-hero-grid' }, [
				E('dl', { 'class': 'mm-kv' }, [E('dt', {}, _('Network')), E('dd', {}, [h.badge, h.net]),
					E('dt', {}, _('Signal')), E('dd', {}, [h.bars, h.sig, h.ca])]),
				E('dl', { 'class': 'mm-kv' }, [E('dt', {}, _('Address')), h.addr, h.uptimeDt = E('dt', {}, _('Session')), h.uptime,
					E('dt', {}, _('Traffic')), h.traffic, E('dt', {}, _('Speed')), h.rates])]),
			E('div', { 'class': 'mm-actions' }, [h.connect, h.check, menu])]);
		return h;
	},

	/* a section card with a collapsible body (the mockup's sec); its state is kept per browser */
	/* dflt false: closed until the operator opens it (UX audit 2026-10-17); this.autoOpen opens one the page points at */
	sec: function(key, title, body, gist, dflt) {
		var n = mm.section(key, title, body, gist, dflt !== undefined ? dflt : !SEC_CLOSED[key]);
		(this.secs = this.secs || {})[key] = n;
		return n;
	},

	/* a section the operator never opened or closed follows the state: open while it matters */
	autoOpen: function(key, want) {
		var n = (this.secs || {})[key];
		if (n && !n.mmSaved()) n.mmOpen(!!want);
	},


	/* key/value rows; a third element ('expert') hides them at «Базовый» */
	dl: function(keys) {
		var dl = E('dl', { 'class': 'mm-kv' }), refs = this.refs, lv = this.rowLevel;
		keys.forEach(function(k) {
			var dt = E('dt', {}, k[1]);
			refs[k[0]] = E('dd'); dl.appendChild(dt); dl.appendChild(refs[k[0]]);
			lv[k[0]] = [dt, refs[k[0]], k[2]];
		});
		return dl;
	},

	/* a card of rows only: hidden when none of its rows has anything to say */
	rows: function(title, keys, key) {
		var node = this.sec(key || title, title, [this.dl(keys)]);
		(this.rowCards = this.rowCards || []).push([node, keys.map(function(k) { return k[0]; })]);
		return node;
	},

	/* «Подключение»: the APN and IP type, and who looks after the interface (spec 25.9.10, user request 2026-10-06) */
	connection: function() {
		var self = this, c = this.conn = {};
		c.who = E('p', { 'class': 'mm-muted mm-x' });
		c.apn = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-apn', autocomplete: 'off',
			spellcheck: 'false', placeholder: _('Not set: by the operator\'s settings'), input: function() { c.dirty = true; } });
		c.suggest = E('span', { 'class': 'mm-faint', hidden: true });
		c.use = E('button', { type: 'button', 'class': 'mm-link', hidden: true, click: function() {
			c.apn.value = c.suggested || ''; c.dirty = true; } }, _('Use'));
		/* the APN follows the SIM (the owner's request, test report 2026-10-15): the engine writes the suggestion, and
		 * again after a SIM swap while the connection is down */
		c.auto = E('input', { type: 'checkbox', id: 'mm-apn-auto', change: function() { c.dirty = true; self.applyApnAuto(); } });
		c.ip = E('select', { 'class': 'mm-select', id: 'mm-ip', change: function() { c.dirty = true; } }, [
			E('option', { value: '' }, _('Automatically')), E('option', { value: 'ipv4' }, _('IPv4 only')),
			E('option', { value: 'ipv4v6' }, _('IPv4 and IPv6')), E('option', { value: 'ipv6' }, _('IPv6 only'))]);
		c.backend = E('select', { 'class': 'mm-select', id: 'mm-backend', change: function() { c.dirty = true; self.checkQmodemChoice(); self.ownerChoiceChanged(); } }, [
			E('option', { value: 'auto' }, _('Automatically, by the model')), E('option', { value: 'modemmanager' }, 'ModemManager'),
			c.nativeOpt = E('option', { value: 'native' }, _('MedveModem directly (QMI, MBIM or AT)')), E('option', { value: 'qmodem' }, 'QModem')]);
		c.adoptNote = E('div', { 'class': 'mm-callout is-info mm-note mm-adopt-note', hidden: true });
		/* the choice saved while QModem still has the modem (bench 2026-10-19: the FM350 said QModem everywhere) */
		c.ownNote = E('div', { 'class': 'mm-callout is-warn mm-own-pending', hidden: true });
		/* DNS servers typed by hand instead of the operator's (field request 2026-10-23): netifd's peerdns '0' and list dns */
		c.dns = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-dns', autocomplete: 'off', spellcheck: 'false',
			placeholder: _('Not set: the operator\'s servers'), input: function() { c.dirty = true; } });
		/* beside the operator's by default: under an operator's whitelist foreign resolvers do not answer;
		 * «instead» only by choice */
		c.dnsMode = E('select', { 'class': 'mm-select', id: 'mm-dns-mode', change: function() { c.dirty = true; } }, [
			E('option', { value: 'add' }, _('in addition to the operator\'s')), E('option', { value: 'replace' }, _('instead of the operator\'s'))]);
		c.dnsBypassed = E('p', { 'class': 'mm-faint mm-warn-text mm-dns-bypassed', hidden: '' },
			_('The router\'s names are resolved by another program now (podkop or similar: dnsmasq does not read the uplinks\' servers), so these servers and the operator\'s are not used for them.'));
		c.dnsField = E('div', { 'class': 'mm-dns-servers mm-x' }, [
			E('label', { 'for': 'mm-dns', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('DNS servers')), c.dns]),
			E('label', { 'for': 'mm-dns-mode', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Use them')), c.dnsMode]),
			E('p', { 'class': 'mm-faint' }, _('When the operator lets through only its whitelist, foreign servers (8.8.8.8, 1.1.1.1) do not answer: use 77.88.8.8 77.88.8.1, and “instead of the operator\'s” leaves the router without names if they are silent.')),
			c.dnsBypassed]);
		/* the fixed TTL of outgoing packets: a tariff for phones (field request 2026-10-13) */
		c.ttl = E('input', { type: 'checkbox', id: 'mm-ttl', change: function() { c.dirty = true; } });
		/* the engine's auto-connect, per modem (review 2026-10-17: a modem kept for SMS only, a metered standby SIM) */
		c.autoc = E('input', { type: 'checkbox', id: 'mm-auto-connect', change: function() { c.dirty = true; } });
		c.ttlSay = E('p', { 'class': 'mm-faint mm-ttl-say', role: 'status', hidden: '' });
		/* QModem picked while it is not on the router: said at once, not after «Сохранить» (audit 2026-10-13) */
		c.qmNote = E('div', { 'class': 'mm-callout is-warn mm-qm-pick', hidden: true }, [
			_('QModem is not installed on the router: install it first, or the modem stays without management.') + ' ',
			E('a', { href: L.url('admin/modem/medvemodem/settings') + '#qmodem' }, _('Install QModem'))]);
		c.ownerStatus = E('div', { 'class': 'mm-callout is-info mm-owner-switch-status', role: 'status', 'aria-live': 'polite', hidden: true });
		c.save = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-primary', click: mm.busy(function() { return self.saveConnection(); }) }, _('Save and connect'));
		c.toggle = E('button', { type: 'button', 'class': 'mm-btn mm-secondary', hidden: true, click: mm.busy(function() { return self.toggleConnection(); }) }, _('Disconnect'));
		/* a router inside the modem (HiLink, a UFI stick, a phone) dials by itself: no APN, IP version or owner to choose here
		 * (bench 2026-10-19) */
		c.routerNote = E('p', { 'class': 'mm-faint mm-router-note', hidden: '' },
			_('The modem connects to the operator\'s network by itself: there is a router inside it. The access point (APN) and the network mode are set in its web interface; the router gets an address from the modem by DHCP.'));
		/* who talks to the modem, first in the card: SMS, AT, the signal and the recovery depend on it, and people looked
		 * for it under another name (the owner, 2026-10-31: «люди ищут и тупят») */
		c.cellOnly = [
			E('label', { 'for': 'mm-backend', 'class': 'mm-field mm-backend-field' }, [E('span', { 'class': 'mm-dim' }, _('Who drives the modem')), c.backend]),
			E('p', { 'class': 'mm-faint mm-backend-hint' }, _('Important: this is the program that talks to the modem, and SMS, AT commands, the signal and the recovery depend on it. Keep “Automatically, by the model” unless the modem misbehaves: MedveModem picks by the model, ModemManager for most QMI and MBIM modems, QModem for the FM350 and Quectel when it is installed.')),
			E('label', { 'for': 'mm-apn', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Access point (APN)')), c.apn]),
			E('div', { 'class': 'mm-row mm-suggest' }, [c.suggest, c.use]),
			E('label', { 'class': 'mm-check mm-apn-auto' }, [c.auto, ' ' + _('Pick the APN by the SIM card automatically when the field is empty')]),
			E('label', { 'for': 'mm-ip', 'class': 'mm-field mm-x' }, [E('span', { 'class': 'mm-dim' }, _('IP version')), c.ip])];
		return c.node = this.sec('conn', _('Connection setup'), [c.who, c.routerNote].concat(c.cellOnly, [c.dnsField,
			E('label', { 'class': 'mm-check mm-ttl' }, [c.ttl, ' ' + _('Fix TTL 64 (a smartphone tariff)')]), c.ttlSay,
			E('label', { 'class': 'mm-check mm-auto-connect' }, [c.autoc, ' ' + _('Connect automatically when the modem has no interface yet')]),
			c.ownNote, c.qmNote, c.adoptNote, c.ownerStatus, E('div', { 'class': 'mm-actions' }, [c.save, c.toggle])]));
	},
	applyConnection: function(d) {
		var c = this.conn, k = d.connection || {}, s = k.apn_suggestion, connected = d.present === true && (d.session || {}).state === 'connected';
		this.connInfo = k;
		var routed = (d.owner || {}).control === 'hilink' || (d.owner || {}).session === 'device';
		c.routerNote.hidden = !routed;
		c.cellOnly.forEach(function(n) { n.hidden = routed; });
		text(c.who, !k.interface ? _('No interface is created for this modem; MedveModem creates one when connecting.') :
			k.managed ? _('The interface “%s” is managed by MedveModem').format(k.interface) + (k.adopted ? ' (' + _('management handed over') + ')' : '') + '.' :
			_('The interface “%s” is set up outside MedveModem (in the firmware image or by hand).').format(k.interface));
		if (!c.dirty && document.activeElement !== c.apn) c.apn.value = k.apn || '';
		if (!c.dirty && document.activeElement !== c.ip) c.ip.value = k.iptype || '';
		/* the engine's own connection only where the modem's profile names it (the owner, 2026-10-27: a way not supported
		 * for this composition yet is not offered): its first or fallback owner, or an AT dial recipe of its own (the Intel
		 * XMM: ModemManager takes PPP on its ACM composition and gets no address, the owner: «xmm не надо скрывать»);
		 * a recipe never run on a modem of ours says so. A choice saved earlier stays visible, greyed, until changed */
		var p = d.profile || {}, portCtl = ((d.port_roles || {}).control || {}), drv = portCtl.driver || '';
		/* The netcfg engine supports explicit native QMI/MBIM by the actual USB
		 * control driver, even when profile matching is incomplete. Previously
		 * LuCI hid/disabled the choice until a model-specific profile opted in,
		 * despite a working qmi_wwan / cdc-wdm control node on the router. */
		var nativeTransport = !!portCtl.node && (/^qmi_wwan(?:_|$)/.test(drv) || drv === 'cdc_mbim');
		var nativeOk = p.prefer_owner === 'native' || p.fallback_owner === 'native' || p.native_qmi === true || !!p.dial || nativeTransport;
		c.nativeOpt.disabled = !nativeOk;
		c.nativeOpt.hidden = !nativeOk && k.backend !== 'native';
		text(c.nativeOpt, (p.dial || {}).status === 'unverified' && !nativeTransport ? _('MedveModem directly (AT; not yet checked on this model)') :
			nativeTransport && /^qmi_wwan(?:_|$)/.test(drv) ? _('MedveModem directly (native QMI, without ModemManager)') :
			nativeTransport && drv === 'cdc_mbim' ? _('MedveModem directly (native MBIM, without ModemManager)') :
			_('MedveModem directly (QMI, MBIM or AT)'));
		if (!c.dirty && document.activeElement !== c.backend) c.backend.value = k.backend || 'auto';
		if (!nativeOk && c.backend.value === 'native' && !this.ownerSwitch && !c.nativeDenied) {
			c.nativeDenied = true;
			this.ownerStage(_('Native modem management is unavailable: this modem profile or its QMI/MBIM capability was not detected.'), 'err');
		} else if (nativeOk) c.nativeDenied = false;
		if (!c.dirty && document.activeElement !== c.ttl) c.ttl.checked = !!k.ttl;
		if (!c.dirty && document.activeElement !== c.dns) c.dns.value = (k.dns || []).join(' ');
		if (!c.dirty && document.activeElement !== c.dnsMode) c.dnsMode.value = k.dns_mode === 'replace' ? 'replace' : 'add';
		c.dnsBypassed.hidden = !k.dns_bypassed;
		/* QModem writes its own interfaces: the engine sets no DNS there */
		c.dnsField.hidden = k.backend === 'qmodem' || (d.owner || {}).control === 'qmodem';
		this.applyUsbConfig(k.usb_config);
		if (!c.dirty && document.activeElement !== c.autoc) c.autoc.checked = k.auto_connect !== false;
		if (!c.dirty && document.activeElement !== c.auto) c.auto.checked = !!k.apn_auto;
		this.applyTtl(k.ttl);
		c.suggested = s && s.apn;
		text(c.suggest, s ? (s.operator ? _('The recommended access point for “%s”: %s').format(s.operator, s.apn) : _('The recommended access point for this SIM card: %s').format(s.apn)) : '');
		c.suggest.hidden = c.use.hidden = !s || c.apn.value === s.apn;
		this.applyApnAuto();
		var q = d.qmodem || {}, pending = (d.owner || {}).control === 'qmodem' && (k.backend === 'native' || k.backend === 'modemmanager');
		c.ownNote.hidden = !pending;
		text(c.ownNote, !pending ? '' : q.managed_by_engine ?
			_('The way “%s” is chosen, but QModem still manages the modem. “Save and connect” takes it from QModem and connects it the chosen way.').format(k.backend === 'native' ? _('MedveModem directly') : 'ModemManager') :
			_('The way “%s” is chosen, but the modem is set up in QModem itself (section %s). MedveModem does not touch others\' settings: delete the modem in QModem, then “Save and connect”.')
				.format(k.backend === 'native' ? _('MedveModem directly') : 'ModemManager', q.section || '?'));
		/* a session interface set up outside MedveModem (umbim, uqmi...) drives the modem: offered, never taken silently (the
		 * owner, 2026-10-27: «предлагать переделать под нас, но не молча все сносить») */
		var f = k.foreign;
		c.adoptNote.hidden = !k.user_interface;
		c.adoptNote.className = 'mm-callout mm-note mm-adopt-note ' + (f && !f.handed ? 'is-warn' : 'is-info');
		text(c.adoptNote, f ? (f.handed ? _('Handed over: “%s” is down, ModemManager is taking the modem; then MedveModem connects it through the same interface.').format(f.interface) :
				_('The modem is driven by the interface “%s” (%s) set up outside MedveModem. MedveModem does not touch it and keeps ModemManager away from the modem.').format(f.interface, (f.proto || '').toUpperCase()) +
				(this.level === 'normal' ? '' : ' ' + _('“Hand over to MedveModem” takes “%s” down; once ModemManager holds the modem, MedveModem rewrites that interface for itself: its name and other parameters stay, “Put back as it was” brings it back.').format(f.interface))) :
			k.user_interface ? (this.level === 'normal' ? _('MedveModem takes the connection of this modem over and connects it itself.') :
			_('The interface “%s” comes under MedveModem\'s management: the device path, the firewall zone and the APN. The interface\'s other parameters stay.').format(k.user_interface)) : '');
		text(c.save, f ? _('Hand over to MedveModem') : k.user_interface ? _('Hand over the management and connect') : _('Save and connect'));
		c.save.disabled = d.present !== true || !!(f && f.handed);
		this.alerts.foreign = f && !f.handed ? f.interface : '';
		this.applyAlert();
		c.toggle.hidden = !k.interface;
		text(c.toggle, connected ? _('Disconnect') : _('Connect'));
		c.connected = connected;
	},

	/* «Конфигурация USB»: only for a module with more than one; the engine sets it at each attach, at most twice in 10 min.
	 * A change of the modem's USB mode, so among the service actions with its own confirmed button (the owner, 2026-10-19) */
	usbCard: function() {
		var self = this, u = this.usbc = {};
		u.sel = E('select', { 'class': 'mm-select', id: 'mm-usb-config', change: function() { u.dirty = true; } });
		u.say = E('p', { 'class': 'mm-faint mm-usb-say', hidden: true });
		u.id = E('p', { 'class': 'mm-faint mm-usb-id' });
		u.save = E('button', { type: 'button', 'class': 'mm-btn mm-usb-save', click: mm.busy(function() { return self.saveUsbConfig(); }) }, _('Apply'));
		u.node = this.sec('usbconf', _('USB composition'), [
			E('p', {}, _('The module has several USB compositions (USB configurations) with different sets of functions: for example, a Dell DW5821e has one with QMI and another with MBIM. MedveModem sets the chosen one at each connection of the modem; nothing is written into the modem\'s memory.')),
			risk(_('changes the modem\'s USB mode'), _('the modem reconnects, the link through it breaks for 1–2 minutes. To undo: “Automatically”.')),
			E('label', { 'for': 'mm-usb-config', 'class': 'mm-field mm-usb-config' }, [E('span', { 'class': 'mm-dim' }, _('Composition')), u.sel]),
			u.id, u.say, E('div', { 'class': 'mm-row' }, u.save)]);
		u.node.hidden = true;
		return u.node;
	},

	applyUsbConfig: function(c) {
		var u = this.usbc, n = (c && c.count) || 0, opts = [['', _('Automatically') + (c && c.current ? ' (' + _('now %s').format(c.current) + ')' : '')]];
		u.node.hidden = !c || (n < 2 && !c.wanted);
		if (u.node.hidden) { u.say.hidden = true; return; }
		/* what each one carries (the owner, 2026-10-19): the data channel and the COM ports, from the USB descriptors */
		var cf = {};
		(Array.isArray(c.configs) ? c.configs : []).forEach(function(x) { if (x && typeof x.value === 'number') cf[x.value] = x; });
		var gist = function(x) {
			if (!x) return '';
			var parts = [x.name || x.data || ''].concat(typeof x.serial === 'number' ? [x.serial ? x.serial + ' COM' : _('no COM')] : []).filter(Boolean);
			return parts.length ? ' — ' + parts.join(', ') : '';
		};
		for (var i = 1; i <= Math.max(n, c.wanted || 0); i++) opts.push([String(i), _('Composition %d').format(i) + gist(cf[i]) + (c.current === i ? ' (' + _('now') + ')' : '')]);
		text(u.id, c.usb_id ? _('USB ID %s: one for all compositions; only the set of interfaces changes.').format(c.usb_id) : '');
		this.gist('usbconf', c.current ? c.current + gist(cf[c.current]) : '');
		if (u.sel.options.length !== opts.length || Array.prototype.some.call(u.sel.options, function(o, j) { return o.textContent !== opts[j][1]; })) {
			var keep = u.sel.value;
			u.sel.textContent = '';
			opts.forEach(function(o) { u.sel.appendChild(E('option', { value: o[0] }, o[1])); });
			u.sel.value = keep;
		}
		if (!u.dirty && document.activeElement !== u.sel) u.sel.value = c.wanted ? String(c.wanted) : '';
		var said = { kept: _('The modem is in the chosen configuration.'), set: _('The configuration is switched, the modem reconnects…'),
			waiting: _('It switches when the recovery or the bring-up of the modem ends.'),
			refused: c.why === 'no such configuration' ? _('The modem has no such configuration.') :
				_('The usb-modeswitch rule for this modem sets configuration 0 and will get in the way: first switch it off in the “usb-modeswitch rule” card.'),
			failed: _('The configuration did not hold after two attempts, the modem returned to the previous one. For it to stop trying, choose “Automatically” and apply.') }[c.state];
		u.say.hidden = !said;
		text(u.say, said || '');
	},

	saveUsbConfig: function() {
		var self = this, u = this.usbc, id = this.id, want = u.sel.value;
		if (!window.confirm(want ? _('Switch the modem to USB configuration %s? The modem reconnects, the link through it breaks for 1–2 minutes.').format(want) :
			_('Return the USB configuration to automatic? The modem may reconnect.')))
			return Promise.resolve();
		this.say(_('Saving…'));
		return uciGet('medvemodem', 'device').then(function(r) {
			var secs = (r && r.values) || {}, name = Object.keys(secs).filter(function(n) { return secs[n].id === id; })[0];
			return name ? uciSet('medvemodem', name, { usb_config: want }) : uciAdd('medvemodem', 'device', { id: id, usb_config: want });
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			u.dirty = false;
			return bringup(id);
		}).then(function() {
			self.say(want ? _('Composition %s is chosen: the modem reconnects.').format(want) : _('The USB composition is automatic.'));
			return self.update();
		}).catch(function(e) { self.say(_('Saving error: %s').format(failText(e))); });
	},

	/* the field is always the administrator's (the owner, 2026-10-16): an APN typed there wins; with the box ticked an
	 * empty field takes the SIM's APN from the database, again after a SIM swap */
	applyApnAuto: function() {
		var c = this.conn, s = (this.connInfo || {}).apn_suggestion, known = !!(s && s.apn);
		c.apn.placeholder = !c.auto.checked ? _('Not set: by the operator\'s settings') :
			known ? _('Empty means by the SIM card: %s').format(s.apn) : _('Empty: this SIM card is not in the database, by the operator\'s settings');
	},

	/* what became of the TTL asked for; the rule's counter proves it works (a ping from the router cannot) */
	applyTtl: function(t) {
		var self = this, c = this.conn, say = !t ? '' : {
			on: _('On: outgoing packets through “%s” leave with TTL %s.').format(t.device, t.value),
			applying: _('The rules are being applied…'),
			waiting: _('It applies when connecting.'),
			'no-egress': _('Partly on: the kernel does not support the egress hook, and with connection offloading (Network → Firewall → “Software flow offloading”) some packets leave with their original TTL. Switch the offloading off.'),
			failed: _('The rules did not apply: nft refused them (details in the system log).') }[t.state] || '';
		c.ttlSay.hidden = !t;
		c.ttlSay.classList.toggle('mm-warn-text', !!t && (t.state === 'no-egress' || t.state === 'failed'));
		if (!t) return;
		if (t.state === 'on' && c.ttlPackets != null) say += ' ' + N_(c.ttlPackets, 'The rule worked for %s packet.', 'The rule worked for %s packets.').format(c.ttlPackets.toLocaleString(LANG));
		/* hardware offloading passes the kernel by: the rule stands and still most packets keep their TTL */
		if (t.hw_offload) {
			say += ' ' + _('Hardware flow offloading is on (Network → Firewall): offloaded connections pass the router\'s rules by and keep their original TTL, so the operator may still see the tethering. Switch the hardware offloading off.');
			c.ttlSay.classList.add('mm-warn-text');
		}
		text(c.ttlSay, say + ' ' + _('It removes only the check by TTL: the operator may tell a computer by other signs too.'));
		var now = Date.now();
		if (t.state === 'on' && !(c.ttlAsked > now - 30000)) {
			c.ttlAsked = now;
			fs.exec('/usr/libexec/medvemodem/ttl-fix', ['status']).then(function(r) {
				var j = {};
				try { j = JSON.parse((r && r.stdout) || '{}'); } catch (e) {}
				c.ttlPackets = (j[t.device] || {}).packets;
				if (c.ttlPackets != null) self.applyTtl(t);
			}, function() {});
		}
	},

	checkQmodemChoice: function() {
		var c = this.conn;
		if (c.backend.value !== 'qmodem') { c.qmNote.hidden = true; return Promise.resolve(); }
		return fs.exec('/usr/libexec/medvemodem/qmodem-install', ['status']).then(function(r) {
			var j = {};
			try { j = JSON.parse((r && r.stdout) || '{}'); } catch (e) { /* no word, no note */ }
			c.qmNote.hidden = !(j.ok && !j.installed) || c.backend.value !== 'qmodem';
		}, function() { c.qmNote.hidden = true; });
	},

	saveConnection: function() {
		var self = this, c = this.conn, k = this.connInfo || {}, id = this.id;
		var values = { id: id, apn: c.apn.value.trim(), apn_auto: c.auto.checked ? '1' : '0', iptype: c.ip.value,
			backend: c.backend.value, ttl: c.ttl.checked ? '64' : '', auto_connect: c.autoc.checked ? '1' : '0',
			dns: c.dns.value.trim().split(/[\s,]+/).filter(Boolean).join(' '), dns_mode: c.dnsMode.value };
		if ((k.backend || 'auto') !== values.backend && k.interface && !window.confirm(_('Change the modem\'s managing service? The connection through this modem breaks for the switch\'s time.')))
			return Promise.resolve();
		/* checked before either way: a hand-over saves the same values (audit r108, R108-01) */
		if (!/^[A-Za-z0-9._-]*$/.test(values.apn)) { this.say(_('APN: only Latin letters, digits, dot, hyphen and underscore.')); return Promise.resolve(); }
		if (values.dns.split(' ').some(function(a) { return a && !/^(\d{1,3}(\.\d{1,3}){3}|[0-9A-Fa-f:]*:[0-9A-Fa-f:.]*)$/.test(a); }) || values.dns.split(' ').length > 4) {
			this.say(_('DNS servers: up to four IPv4 or IPv6 addresses separated by spaces.')); return Promise.resolve(); }
		if (k.foreign && !k.foreign.handed)
			return this.handOver(values);
		if (k.user_interface) values.adopt = '1';
		var changedOwner = (k.backend || 'auto') !== values.backend;
		this.ownerStage(_('Saving modem connection settings…'), 'run');
		c.save.disabled = true;
		return uciGet('medvemodem', 'device').then(function(r) {
			var secs = (r && r.values) || {}, name = Object.keys(secs).filter(function(n) { return secs[n].id === id; })[0];
			return name ? uciSet('medvemodem', name, values) : uciAdd('medvemodem', 'device', values);
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			/* Commit returned without a transport error: verify its durable value,
			 * not a stale frontend select element or a previous UCI image. */
			return uciGet('medvemodem', 'device').then(function(r) {
				var secs = (r && r.values) || {};
				var found = Object.keys(secs).some(function(n) { return secs[n].id === id && secs[n].backend === values.backend; });
				if (!found) throw new Error(_('The managing service was not saved in the modem configuration.'));
			});
		}).then(function() {
			c.dirty = false;
			if (changedOwner) self.ownerSwitch = { backend: values.backend, at: Date.now() };
			self.ownerStage(changedOwner ? _('The managing service choice is saved. Handing control over…') :
				_('The modem settings are saved. Connecting…'), 'run');
			return bringup(id);
		}).then(function(r) {
			r = r || {};
			if (r.result === 'connected' || r.result === 'started' || r.result === 'running') {
				if (!changedOwner) self.watchConnect(_('The settings are saved. Connecting the modem…'));
				else self.ownerStage(_('The handover has started. Waiting for the modem to change its managing service…'), 'run');
			} else {
				self.ownerSwitch = null;
				self.ownerStage(_('The settings are saved, but modem bring-up was refused: %s').format(
					why(r.reason) || r.result || _('no answer')), 'err');
			}
			return self.update();
		}).catch(function(e) {
			self.ownerSwitch = null;
			self.ownerStage(_('Saving or switching error: %s').format(failText(e)), 'err');
		}).finally(function() { c.save.disabled = false; });
	},

	/* «Передать MedveModem»: the settings saved, the other interface down, the rewrite once ModemManager holds the modem */
	handOver: function(values) {
		var self = this, c = this.conn, f = (this.connInfo || {}).foreign || {}, id = this.id;
		if (!window.confirm(_('Take the interface “%s” down and hand the modem over to MedveModem? Until ModemManager takes the modem there is no connection through it; “Put back as it was” brings the interface back afterwards.').format(f.interface)))
			return Promise.resolve();
		this.ownerStage(_('Saving modem connection settings…'), 'run');
		c.save.disabled = true;
		return uciGet('medvemodem', 'device').then(function(r) {
			var secs = (r && r.values) || {}, name = Object.keys(secs).filter(function(n) { return secs[n].id === id; })[0];
			return name ? uciSet('medvemodem', name, values) : uciAdd('medvemodem', 'device', values);
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			return uciGet('medvemodem', 'device').then(function(r) {
				var secs = (r && r.values) || {};
				if (!Object.keys(secs).some(function(n) { return secs[n].id === id && secs[n].backend === values.backend; }))
					throw new Error(_('The managing service was not saved in the modem configuration.'));
			});
		}).then(function() {
			c.dirty = false;
			self.ownerSwitch = { backend: values.backend, at: Date.now() };
			self.ownerStage(_('The managing service choice is saved. Handing control over…'), 'run');
			return handover(id);
		}).then(function(r) {
			r = r || {};
			if (r.result === 'started') self.ownerStage(_('The interface handover has started. Waiting for the modem to change its managing service…'), 'run');
			else {
				self.ownerSwitch = null;
				self.ownerStage(_('The settings are saved, but modem bring-up was refused: %s').format(why(r.reason) || r.result || _('no answer')), 'err');
			}
			return self.update();
		}).catch(function(e) {
			self.ownerSwitch = null;
			self.ownerStage(_('Saving or switching error: %s').format(failText(e)), 'err');
		}).finally(function() { c.save.disabled = false; });
	},

	/* a connection asked for is followed in three steps (UX audit 2026-10-17), never «done» on the job's start alone:
	 * sent → the modem on the network → the internet checked (or why not); given up after 2 minutes without a session */
	watchConnect: function(first) {
		this.say(first);
		this.connWatch = { at: Date.now(), phase: 1, said: first };
	},

	/* a step of a followed connect; another message said since (another action) ends the following quietly (CI 2026-10-17:
	 * the 2-minute verdict overwrote an SMS refresh's) */
	sayConnect: function(line, done) {
		var w = this.connWatch;
		if (!w || this.message.textContent !== w.said) { this.connWatch = null; return; }
		this.say(line);
		w.said = line;
		if (done) this.connWatch = null;
	},

	followConnect: function(d) {
		var w = this.connWatch, sess = d.session || {}, up = d.uplink || {}, name = words(d.name) || this.heroModel || _('modem');
		if (!w) return;
		if (this.message.textContent !== w.said) { this.connWatch = null; return; }
		if (w.phase === 1 && sess.state === 'connected') { w.phase = 2; w.at = Date.now(); this.sayConnect(_('The modem is connected to the network. Checking the internet access…')); }
		else if (w.phase === 1 && Date.now() - w.at > 120000) this.sayConnect(_('The modem has not connected yet') + (why(sess.reason) ? ': ' + why(sess.reason) : '') + '. ' + _('Details are on the “Diagnostics” tab.'), true);
		if (!this.connWatch || w.phase !== 2) return;
		if (up.reach && up.reach.ok === true) this.sayConnect(_('The internet works through “%s”.').format(name), true);
		else if (up.role === 'no_internet' || up.role === 'limited' || (up.reach && up.reach.ok === false))
			this.sayConnect(_('The modem is connected to the network, but the internet through it is unavailable') + (checkWords(up.reach) ? ': ' + checkWords(up.reach) : '') + '.', true);
		else if (Date.now() - w.at > 120000) this.sayConnect(_('The modem is connected to the network; the internet check has not passed yet.'), true);
	},

	/* the hero's «Подключить»: the interface made with the form's settings, or the kept-down one brought up */
	connectNow: function() {
		var self = this;
		if (this.heroConnect === 'create') return this.saveConnection();
		if (this.heroConnect !== 'up') return Promise.resolve();
		this.say(_('Connecting…'));
		return connectIface(this.id, true, 'LuCI').then(function(r) {
			r = r || {};
			if (r.result === 'done') self.watchConnect(_('Connecting the modem…'));
			else self.say(_('Not connected: %s').format(why(r.reason || r.result || '')));
			return self.update();
		}, function(e) { self.say(_('Error: %s').format(failText(e))); });
	},

	toggleConnection: function() {
		var self = this, k = this.connInfo || {}, c = this.conn;
		if (!k.interface) return Promise.resolve();
		if (c.connected && !window.confirm(_('Switch the interface “%s” off? There is no link through this modem until the “Connect” button is pressed; no automatic reconnect runs.').format(k.interface)))
			return Promise.resolve();
		this.say(c.connected ? _('Disconnecting…') : _('Connecting…'));
		return connectIface(this.id, !c.connected, 'LuCI').then(function(r) {
			r = r || {};
			if (r.result !== 'done') self.say((c.connected ? _('Not switched off:') + ' ' : _('Not connected:') + ' ') + why(r.reason || r.result || ''));
			else if (!c.connected) self.watchConnect(_('Connecting the modem…'));
			return self.update();
		}, function(e) { self.say(_('Error: %s').format(failText(e))); });
	},

	overview: function() {
		this.strip = E('div', { 'class': 'mm-strip' });
		this.journal = E('ul', { 'class': 'mm-tl mm-journal' });
		this.stripGist = E('span', { 'class': 'mm-faint' });
		var self = this;
		/* the journal from «Подробный»; «Базовый» keeps the 24-hour strip only */
		this.journalBox = E('div', { 'class': 'mm-journal-box' }, [
			E('div', { 'class': 'mm-row mm-journal-head' }, [
				E('p', { 'class': 'mm-faint' }, _('Disconnects, connects, recovery steps and settings changes (⚠). The newest events on top.')),
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-journal-copy', click: function() {
					copyText((self.journalText || []).join('\n')).then(function() { self.say(_('The journal is copied.')); },
						function() { self.say(_('The browser did not let copy: select the journal\'s lines and copy them by hand.')); });
				} }, _('Copy the journal'))]),
			this.journal]);
		return [
			E('div', { 'class': 'mm-grid2' }, [
				this.rows(_('Connection'), [['role', _('Connection setup')], ['address', _('Addresses'), 'expert'], ['iface', _('Interface'), 'expert'], ['device', _('Network device'), 'expert'],
					['gateway', _('Gateway'), 'expert'], ['dns', _('Operator\'s DNS'), 'expert'], ['mtu', 'MTU', 'expert'],
					['route', _('Default gateway'), 'expert'], ['metric', _('Metric'), 'expert']], 'link'),
				this.rows(_('Link and traffic'), [['check', _('Connection check')], ['rates', _('Current speed')], ['session', _('For the session')],
					['day', _('For today')], ['offline', _('Without the internet today')], ['month', _('For the month')], ['quota', _('Traffic limit')]], 'traffic')]),
			E('div', { 'class': 'mm-grid2' }, [
				this.rows(_('Connection reliability'), [['stability', _('Attached to USB'), 'issue'], ['power', _('USB power'), 'issue'], ['health', _('State monitoring'), 'issue'], ['blocked', _('Restriction')],
					['p_ctl', _('Management'), 'expert'], ['p_sess', _('Session'), 'expert'], ['p_at', _('AT port'), 'expert']], 'stab'),
				this.sec('events', _('Event journal'), [this.strip, this.journalBox], this.stripGist)])
		];
	},

	/* measurements ModemManager polls (--signal-get): words first, a bar on one scale, numbers next to them */
	radio: function() {
		this.meters = { lte: E('div', { 'class': 'mm-metrics' }), nr: E('div', { 'class': 'mm-metrics' }) };
		this.radioCards = {};
		var lte = this.radioCards.lte = this.sec('lte', 'LTE', [this.meters.lte, this.dl([['lte_rsrp', _('Signal strength (RSRP)')], ['lte_snr', _('Signal clarity (SINR)')],
			['lte_rsrq', 'RSRQ', 'expert'], ['lte_rssi', 'RSSI', 'expert']])]);
		var nr = this.radioCards.nr = this.sec('nr', '5G', [this.meters.nr, this.dl([['nr_rsrp', _('Signal strength (RSRP)')], ['nr_snr', _('Signal clarity (SINR)')],
			['nr_rsrq', 'RSRQ', 'expert']])]);
		/* the mockup's carrier aggregation and neighbour cells, from ModemManager's cell info (every 30 s) */
		this.caBar = E('div', { 'class': 'mm-ca', role: 'img' });
		this.carrierList = E('div', { 'class': 'mm-carriers' });
		this.caTotal = E('span', { 'class': 'mm-faint' });
		this.caDoubt = E('div', { 'class': 'mm-callout', hidden: '' });
		this.caCard = this.sec('ca', _('Carrier aggregation'), [this.caTotal, this.caDoubt, this.caBar, this.carrierList]);
		this.caGist = this.caCard.querySelector('.mm-sec-gist');
		this.neighBody = E('tbody');
		this.neighCard = this.sec('neigh', _('Neighbour cells'), [
			E('p', { 'class': 'mm-faint' }, _('Cells the modem hears besides its own. One tower usually has several cells, one per frequency and direction.')),
			E('div', { 'class': 'mm-tablewrap' }, E('table', { 'class': 'mm-table mm-neigh' }, [
				E('thead', {}, E('tr', {}, [E('th', {}, _('Band')), E('th', { 'class': 'num' }, 'PCI'), E('th', { 'class': 'num' }, _('Channel')),
					E('th', { 'class': 'num' }, 'RSRP'), E('th', { 'class': 'num' }, 'RSRQ'), E('th', {}, '')])),
				this.neighBody]))]);
		this.neighGist = this.neighCard.querySelector('.mm-sec-gist');
		/* the mockup's antennas: one bar per receive chain on the RSRP scale, a callout when one is 10 dB or more below the best */
		this.antBody = E('div', { 'class': 'mm-ant-groups' });
		this.antCall = E('div', { 'class': 'mm-callout', hidden: '' });
		this.antTx = E('p', { 'class': 'mm-faint mm-ant-tx', hidden: '' });
		this.antWhy = E('p', { 'class': 'mm-faint', hidden: '' });
		/* the modem's heat beside the cells (2026-11-02): degrees on a T99, or only a throttling level where the firmware has no degrees */
		this.heatLine = E('p', { 'class': 'mm-faint mm-heat', hidden: '' });
		this.antCard = this.sec('ant', _('Antennas · RSRP'), [this.antBody, this.antCall, this.antTx, this.antWhy, this.heatLine]);
		/* network mode and bands, the same card whoever drives the modem (spec 25.9.12); changes with a rollback (stage 2) */
		this.rsWhy = E('p', { 'class': 'mm-faint mm-rs-why', hidden: '' });
		this.rsCard = this.sec('rset', _('Network mode and bands'), [this.dl([['rs_modes', _('Allowed')], ['rs_pref', _('Priority')],
			['rs_bands', _('Bands')], ['rs_lock', _('Cell lock')], ['rs_supported', _('The modem supports'), 'expert'], ['rs_via', _('Source'), 'expert']]), this.rsWhy,
			this.rsRead = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-rs-read', hidden: '',
				click: this.readRadioSettings.bind(this, true) }, _('Read again')),
			this.rsChange = this.radioChangeBox(), this.rsEdit = this.radioEditor()]);
		this.rsCard.classList.add('mm-rset');
		this.rsGist = this.rsCard.querySelector('.mm-sec-gist');
		this.antGist = this.antCard.querySelector('.mm-sec-gist');
		return [
			this.rows(_('Network'), [['r_tech', _('Technology')], ['r_operator', _('Operator')], ['r_band', _('Bands')],
				['r_rs', _('Network setup'), 'expert'], ['r_reg', _('Registration')],
				['r_plmn', _('Network code'), 'expert'], ['r_cell', _('Cell (TAC / CID)'), 'expert'], ['r_enb', _('Base station'), 'expert'], ['r_map', _('Base station on a map')],
				['r_dist', _('Distance to the cell'), 'expert'], ['r_quality', _('Signal quality'), 'expert']], 'net'),
			/* the antennas before the carriers, on every modem (the owner, 2026-11-03) */
			this.antCard,
			this.caCard,
			E('div', { 'class': 'mm-grid2' }, [lte, nr]),
			/* on «Сеть и сигнал», beside the signal the bands are chosen by (the owner, 2026-10-19) */
			this.rsCard,
			this.histCard(),
			this.trafCard(),
			this.neighCard,
			E('p', { 'class': 'mm-faint', 'data-ref': 'r_note' })
		];
	},

	service: function() {
		var self = this;
		this.stages = E('div', { 'class': 'mm-ladder mm-stages' });
		this.progress = E('div', { 'class': 'mm-callout is-info mm-progress', hidden: true, role: 'status' });
		this.busyNote = E('div', { 'class': 'mm-callout mm-note mm-busy', hidden: true }, _('While a step runs, the other actions are unavailable. The progress is shown above.'));
		/* by what the operator came for (UX audit 2026-10-17): what is wrong, restoring, checking, what support needs; the
		 * problem cards show only with a problem */
		return [
			E('h3', { 'class': 'mm-group' }, _('The modem and its state')),
			this.pkgCard(),
			E('div', { 'class': 'mm-grid2' }, [
				this.rows(_('Device'), [['model', _('Model')], ['fw', _('Firmware'), 'expert'], ['imei', 'IMEI', 'expert'], ['id', _('Identifier'), 'expert'], ['path', _('USB connection'), 'expert'],
					['usbid', 'USB ID (VID:PID)', 'expert'], ['usbname', _('USB: manufacturer, product'), 'expert'], ['usbserial', _('USB serial number'), 'expert'],
					['drivers', _('Interface drivers'), 'expert'], ['owner', _('Who runs the connection')], ['owner_why', _('Why so'), 'expert'], ['dial', _('Last connection attempt'), 'expert'], ['ports', _('Modem ports (AT / data)'), 'expert'], ['hostfix', _('Service ports (AT)'), 'expert'], ['nocap', _('Unavailable'), 'expert']], 'dev'),
				this.tempCard()]),
			/* the actions that change the modem's mode or ModemManager's setup on the router, together (the owner, 2026-10-19):
			 * each card only for a modem it concerns, each on a confirmed button; nothing here runs by itself */
			this.svcHead = E('h3', { 'class': 'mm-group mm-svc-group', hidden: '' }, _('Service actions with the modem')),
			this.svcIntro = E('p', { 'class': 'mm-faint mm-svc-intro', hidden: '' }, _('They change the modem\'s mode or ModemManager\'s settings on the router. Each one by a button and with a confirmation, nothing starts by itself; each says what changes and how to undo it. MedveModem does not send commands that change the mode in the modem\'s memory for good.')),
			this.mswCard(),
			this.stickCard(),
			this.fccCard(),
			this.usbCard(),
			E('h3', { 'class': 'mm-group' }, _('Recovery')),
			this.sec('rec', _('Link recovery'), [
				this.dl([['rec_state', _('Current state')], ['rec_last', _('Last run')], ['rec_hold', _('Restriction')]]),
				this.progress,
				E('p', { 'class': 'mm-faint mm-b' }, _('If the internet does not come back by itself: reconnect it first, then restart the modem.')),
				E('p', { 'class': 'mm-faint mm-x' }, _('Run the actions in order: each next step breaks the link for longer. Steps marked “Automatically on a failure” are run by the router when the link is lost, without the user.')),
				/* the owner, 2026-11-04: «кнопки последовательного восстановления, а не тыкать по этапам рандомно» */
				E('div', { 'class': 'mm-actions mm-rec-chain' }, [
					this.chainBtn = E('button', { type: 'button', 'class': 'mm-btn is-primary', click: mm.busy(function() { return self.runChain(); }) },
						_('Restore in order')),
					E('span', { 'class': 'mm-faint' }, _('the steps one after another, from the gentlest, until the internet answers'))]),
				E('div', { 'class': 'mm-actions mm-rereg' }, [
					E('button', { type: 'button', 'class': 'mm-btn', click: mm.busy(function() { return self.reregisterNow(true); }) },
						_('Re-register in the network')),
					E('span', { 'class': 'mm-faint' }, _('the gentlest step: the modem leaves the network and registers again (a few seconds without the internet)'))]),
				this.busyNote, this.stages, this.rescueCard()]),
			E('h3', { 'class': 'mm-group' }, _('Checks and details for support')),
			this.netcheckCard(),
			this.speedCard(),
			this.esimProbe(),
			this.fplmnCard(),
			this.mmlogCard(),
			this.reportCard(),
			/* the freeze last, everywhere (the owner, 2026-10-21): the rarest and the most far-reaching */
			this.freezeCard()
		];
	},

	/* «Крайние меры» (the owner, 2026-11-09: «как крайняя степень спасения, вручную с поиском gpio»): folded; opened, it asks the
	 * engine what the modem sits on and who goes down with it; each button on a confirmation, nothing runs by itself */
	rescueCard: function() {
		var self = this, r = this.rescue = {};
		r.body = E('div', { 'class': 'mm-rescue-body' }, E('p', { 'class': 'mm-faint' }, _('loading…')));
		/* fresh on each opening (who is on the controller changes); closed, nothing of it stays to press */
		r.box = E('details', { 'class': 'mm-rescue', toggle: function() {
			if (r.box.open) self.loadRescue();
			else r.body.replaceChildren(E('p', { 'class': 'mm-faint' }, _('loading…')));
		} }, [
			E('summary', {}, _('Last resort: the USB controller and the power')),
			E('p', { 'class': 'mm-faint' }, _('When no step above helps: the modem does not answer even its USB reconnection, and there is no hub with switchable ports. Never runs by itself.')),
			r.body]);
		return r.box;
	},

	loadRescue: function() {
		var self = this, r = this.rescue;
		return usbRescue(this.id).then(function(v) {
			r.view = v;
			if (v.refused) { r.body.replaceChildren(E('p', { 'class': 'mm-faint' }, why(v.refused))); return; }
			var others = (v.devices || []).filter(function(d) { return d.device_id !== self.id; }).map(function(d) {
				return words(d.name) || words(d.product) || d.vidpid || d.usb; });
			var ctl = v.controller || {}, kids = [
				E('p', {}, _('USB controller: %s').format([ctl.name, ctl.driver].filter(Boolean).join(' · '))),
				E('p', { 'class': 'mm-rescue-others' }, others.length ? _('Will go down with the modem: %s.').format(others.join(', ')) :
					_('Nothing else is on this controller.'))];
			if (v.blocked) kids.push(E('p', { 'class': 'mm-callout is-warn' }, _('Not available: %s.').format(why(v.blocked))));
			else kids.push(E('div', { 'class': 'mm-actions' }, [
				(function(b) { b.disabled = !!v.running; return b; })(E('button', { type: 'button', 'class': 'mm-btn is-danger mm-rescue-ctl',
					click: mm.busy(function() { return self.runRescue('controller'); }) }, _('Restart the USB controller'))),
				E('span', { 'class': 'mm-faint' }, _('every device on it drops for a few seconds and comes back, as if unplugged'))]));
			if ((v.gpios || []).length && v.gpio_blocked)
				kids.push(E('p', { 'class': 'mm-callout is-warn mm-rescue-gpio-off' }, _('The power lines are not available: %s.').format(why(v.gpio_blocked))));
			else if ((v.gpios || []).length) {
				kids.push(E('p', {}, _('Power lines of the board:')));
				v.gpios.forEach(function(g) {
					kids.push(E('div', { 'class': 'mm-actions' }, [
						E('button', { type: 'button', 'class': 'mm-btn mm-rescue-gpio', 'data-gpio': g.name, click: mm.busy(function() { return self.runRescue('gpio', g.name); }) },
							_('Switch “%s” off and on').format(g.name)),
						E('span', { 'class': 'mm-faint' }, _('5 s without power; which ports or modems the line feeds is up to the board'))]));
				});
			} else kids.push(E('p', { 'class': 'mm-faint' }, _('The board shows no power line of USB or of a modem.')));
			r.body.replaceChildren.apply(r.body, kids);
		}).catch(function(e) { r.body.replaceChildren(E('p', { 'class': 'mm-faint' }, failText(e))); });
	},

	runRescue: function(what, gpio) {
		var self = this, v = this.rescue.view || {}, others = (v.devices || []).filter(function(d) { return d.device_id !== self.id; });
		var ask = what === 'controller' ? _('Restart the USB controller %s? The modem and every other device on it drop for a few seconds.').format((v.controller || {}).name || '') +
			(others.length ? ' ' + _('Will go down too: %s.').format(others.map(function(d) { return words(d.name) || words(d.product) || d.vidpid || d.usb; }).join(', ')) : '') :
			_('Switch the power line “%s” off for 5 seconds? Whatever it feeds goes down with it.').format(gpio);
		if (!window.confirm(ask)) return Promise.resolve();
		return usbRescueRun(this.id, what, gpio || null, v.confirm || '').then(function(res) {
			if (res.refused) { self.say(_('Not done: %s').format(why(res.refused))); return self.loadRescue(); }
			self.say(what === 'controller' ? _('The USB controller is being restarted: the modem comes back in a few seconds.') :
				_('The power line is switched off: the modem comes back in a few seconds.'));
		}).catch(function(e) { self.say(_('Not done: %s').format(failText(e))); });
	},

	/* «Настройки» (UX audit 2026-10-17): everything set for this one modem, in one place; the same savers as before */
	/* «eSIM»: a tab for a built-in eUICC or a removable UICC/9eSIM discovered by the diagnostic probe.
	 * It reads EID/profiles and can enable an already installed profile (connection may reset).
	 * Until Alpha 2, downloading new profiles to the card is done on a compatible phone; the
	 * modem is held for a few seconds during an on-demand read. */
	/* per modem and SIM slot: a modem with a plastic SIM in slot 1 and its built-in eUICC in slot 2 reads each apart */
	esimSlot: function() { return ((this.data || {}).modemmanager || {}).primary_sim_slot || 1; },
	esimKey: function() { return 'mm:' + location.host + ':esim:' + this.id + (this.esimSlot() > 1 ? ':' + this.esimSlot() : ''); },
	hasEsim: function() {
		var mmd = (this.data || {}).modemmanager || {}, card = mmd.sim_card || (this.data || {}).sim || {};
		if (card.type === 'esim') return true;
		if (this.esimFound != null && this.esimFor === this.id + ':' + this.esimSlot()) return this.esimFound;
		try { return !!this.id && localStorage.getItem(this.esimKey()) === '1'; } catch (e) { return false; }
	},
	esimSeen: function(found) {
		this.esimFound = found;
		this.esimFor = this.id + ':' + this.esimSlot();
		try { if (found) localStorage.setItem(this.esimKey(), '1'); else localStorage.removeItem(this.esimKey()); } catch (e) { /* no storage */ }
		this.esimShow();
	},
	esimShow: function() {
		var has = this.hasEsim();
		if (this.tabs && this.tabs.esim) this.tabs.esim.tab.hidden = !has;
		if (this.esimOpenBtn) this.esimOpenBtn.hidden = !has;
	},
	/* «Диагностика», every level: a card of its own with the result that stays (the owner, 2026-11-16: «не как между карточками
	 * зажатая исчезающая ссылка, а полноценная карточка в Диагностике с результатом проверки»); a chip found opens the tab */
	esimProbe: function() {
		var self = this, out = E('p', { 'class': 'mm-esim-probe-out', hidden: '' });
		var open = this.esimOpenBtn = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-esim-open', hidden: '',
			click: function() { self.show('esim'); } }, _('Open the eSIM tab'));
		var btn = E('button', { type: 'button', 'class': 'mm-btn mm-esim-probe', click: mm.busy(function() {
			out.hidden = false;
			text(out, _('Reading the eSIM chip…'));
			return self.readEsim().then(function(found) {
				text(out, self.esim.state.textContent);
				if (found) self.show('esim');
			});
		}) }, _('Check for an eSIM'));
		this.esimProbeNode = this.sec('esimcheck', _('eSIM check'), [
			E('p', { 'class': 'mm-faint' }, _('Whether the SIM in this modem is an eSIM: a built-in chip or a card such as 9eSIM. A few seconds of reading; nothing is changed.')),
			out, E('div', { 'class': 'mm-row mm-esim-probe-row' }, [btn, open])]);
		this.esimShow();
		return this.esimProbeNode;
	},
	esimCard: function() {
		var self = this, e = this.esim = {};
		e.read = E('button', { type: 'button', 'class': 'mm-btn mm-esim-read', click: mm.busy(function() { return self.readEsim(); }) }, _('Read the eSIM'));
		e.state = E('p', { 'class': 'mm-esim-state' });
		e.kv = E('dl', { 'class': 'mm-kv mm-esim-kv', hidden: '' });
		e.list = E('tbody');
		e.table = E('div', { 'class': 'mm-tablewrap', hidden: '' }, E('table', { 'class': 'mm-table mm-esim-table' }, [
			E('thead', {}, E('tr', {}, [E('th', {}, _('Profile')), E('th', {}, _('Operator')), E('th', {}, _('State')), E('th', {}, 'ICCID'),
				E('th', {}, '')])), e.list]));
		e.slot = E('p', { 'class': 'mm-faint mm-esim-slot', hidden: '' });
		e.done = E('p', { 'class': 'mm-esim-done', hidden: '' });
		/* after a switch, the steps «Диагностика» has, here too (the owner, 2026-11-15: «кнопку перегрузить и другие полезные
		 * из диагностики может дублировать»): each the same call, asked first the same way */
		e.restart = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-esim-restart', click: mm.busy(function() { return self.run('soft-reset'); }) },
			(STAGES['soft-reset'] || [_('Restart the modem')])[0]);
		e.after = E('div', { 'class': 'mm-esim-after' }, [
			E('div', { 'class': 'mm-faint' }, _('If the modem does not connect with the new profile:')),
			E('div', { 'class': 'mm-row' }, [
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-esim-session', click: mm.busy(function() { return self.run('session'); }) },
					(STAGES.session || [_('Reconnect the internet')])[0]),
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-esim-rereg', click: mm.busy(function() { return self.reregisterNow(true); }) },
					_('Re-register in the network')),
				e.restart])]);
		this.esimNode = this.sec('esim', 'eSIM', [
			E('p', { 'class': 'mm-callout is-info mm-esim-note' }, _('Here the eSIM switches between the profiles already on the chip. Download and delete profiles on a smartphone for now, with the app of the card (9eSIM, eSIM.me and the like) or EasyEUICC / OpenEUICC on Android.')),
			e.slot, E('div', { 'class': 'mm-row' }, e.read), e.state, e.done, e.kv, e.table, e.after,
			E('p', { 'class': 'mm-faint' }, _('The reading talks to the SIM chip and takes a few seconds; the modem\'s connection is not touched. A modem whose control channel ModemManager or QModem holds is read only where the engine can reach the chip beside them.'))]);
		this.esimNode.classList.add('mm-esim');
		return this.esimNode;
	},

	readEsim: function() {
		var self = this, e = this.esim, id = this.id;
		if (!id) return Promise.resolve();
		e.read.disabled = true;
		text(e.state, _('Reading the eSIM chip…'));
		e.kv.hidden = e.table.hidden = true;
		var mmd = (this.data || {}).modemmanager || {}, nslots = list(mmd.sim_slots).length;
		e.slot.hidden = nslots < 2;
		text(e.slot, nslots > 1 ? _('The SIM in slot %d of %d is read, the one in use. To read the other slot, switch it in «Settings» → «SIM card».').format(this.esimSlot(), nslots) : '');
		return esimStatus(id).then(function(st) {
			if (id !== self.id) return false;
			st = st || {};
			if (!st.ok) { text(e.state, esimWhy(st)); return false; }
			var eu = st.euicc || {};
			if (eu.state === 'absent') {
				text(e.state, _('There is no eSIM chip in this modem (or the SIM inserted is an ordinary one).'));
				self.esimSeen(false);
				return false;
			}
			self.esimSeen(true);
			var chip = st.data || {}, info = chip.EUICCInfo2 || {}, rows = [];
			e.kv.replaceChildren();
			var row = function(label, node) { e.kv.appendChild(E('dt', {}, label)); e.kv.appendChild(E('dd', {}, node)); };
			var eid = E('span', {});
			secret(eid, eu.eid || chip.eidValue || '');
			row('EID', eid);
			if (typeof info.freeNVMemory === 'number') row(_('Free memory on the chip'), bytes(info.freeNVMemory));
			if ((st.target || {}).selected) row(_('Reached through'), String(st.target.selected).toUpperCase());
			e.kv.hidden = false;
			text(e.state, _('Reading the profiles…'));
			return esimAction(id, 'profiles').then(function(pr) {
				if (id !== self.id) return true;
				pr = pr || {};
				if (!pr.ok) { text(e.state, esimWhy(pr)); return true; }
				var list_ = Array.isArray(pr.data) ? pr.data : [];
				text(e.state, list_.length ? N_(list_.length, '%d profile on the chip', '%d profiles on the chip').format(list_.length) : _('No profiles on the chip yet.'));
				e.list.replaceChildren.apply(e.list, list_.map(function(x) {
					var ic = E('span', { 'class': 'mm-mono' });
					secret(ic, x.iccid || '');
					return E('tr', {}, [E('td', {}, [x.profileNickname || x.profileName || '—', x.profileClass && x.profileClass !== 'operational' ?
							E('div', { 'class': 'mm-faint' }, String(x.profileClass)) : '']),
						E('td', {}, x.serviceProviderName || '—'),
						E('td', {}, x.profileState === 'enabled' ? E('span', { 'class': 'mm-pill is-ok' }, _('in use')) : _('off')),
						E('td', {}, ic),
						E('td', {}, x.profileState !== 'enabled' && x.iccid ? E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-esim-enable',
							click: mm.busy(function() { return self.enableEsim(x); }) }, _('Switch to it')) : '')]);
				}));
				e.table.hidden = !list_.length;
				return true;
			});
		}, function(err) {
			text(e.state, /Method not found|code 3\b/i.test(String((err && err.message) || err)) ? _('This version of the MedveModem service cannot read eSIM.') :
				_('The MedveModem service does not answer.'));
		}).finally(function() { e.read.disabled = false; });
	},

	/* a switch: asked first, the outage named; the answer read back from the chip, a restart advised where the modem needs it */
	enableEsim: function(x) {
		var self = this, e = this.esim, id = this.id, name = x.profileNickname || x.profileName || x.serviceProviderName || _('the profile');
		if (!window.confirm(_('Switch the eSIM to «%s»? The connection through this modem drops for 1–3 minutes while the modem reads the SIM again; then MedveModem connects it with the new profile (the APN by its operator, unless one is set in the settings).').format(name)))
			return Promise.resolve();
		e.done.hidden = true;
		text(e.state, _('Switching the eSIM…'));
		return esimEnable(id, 'enable', x.iccid).then(function(r) {
			if (id !== self.id) return;
			r = r || {};
			if (!r.ok) { text(e.state, esimWhy(r, true)); return; }
			e.done.hidden = false;
			/* the restart this modem needs stands out (not quiet) */
			e.restart.classList.toggle('is-quiet', !r.restart_advised);
			text(e.done, r.restart_advised ? _('Switched to «%s». This modem keeps the old profile until it restarts: press «Restart the modem» on «Diagnostics».').format(name) :
				_('Switched to «%s». The modem reads the SIM again; the connection comes back in a few minutes.').format(name));
			return self.readEsim();
		}, function(err) { text(e.state, _('The MedveModem service does not answer.')); });
	},

	settingsTab: function() {
		return [
			/* the name is renamed in the page head (✎), where it shows (review 2026-10-17): the SIM sits beside the connection */
			E('div', { 'class': 'mm-grid2' }, [this.connection(), this.simCard()]),
			this.webCard(),
			/* here, not under «AT»: that tab is hidden exactly when no AT port is known (bench 2026-10-15, an MBIM T77W968) */
			this.atPortCard()
		];
	},

	/* the modem's name, kept on the router (config modem / option device_id); the same as «Настройки» → «Имена модемов».
	 * Edited in the page head by ✎, where the name shows (review 2026-10-17: a card of one field beside «Подключение») */
	nameEditor: function() {
		var self = this, n = this.nameUi = {};
		n.input = E('input', { type: 'text', 'class': 'mm-input mm-modem-name', id: 'mm-modem-name', maxlength: '32', autocomplete: 'off',
			placeholder: _('No name set: by the model'), input: function() { n.dirty = true; },
			keydown: function(ev) { if (ev.key === 'Escape') self.renameOpen(false); } });
		n.save = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-name-save', click: mm.busy(function() { return self.saveName(); }) }, _('Save'));
		n.cancel = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-name-cancel', click: function() { self.renameOpen(false); } }, _('Cancel'));
		n.box = E('div', { 'class': 'mm-hero-rename', hidden: '' }, [
			E('label', { 'for': 'mm-modem-name', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Modem name')), n.input]),
			E('div', { 'class': 'mm-row' }, [n.save, n.cancel]),
			n.hint = E('p', { 'class': 'mm-faint mm-name-hint' }, _('For example: “Dacha”, “Backup”. An empty field means the name by the model.'))]);
		n.open = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-hero-rename-btn', title: _('Rename the modem'),
			'aria-label': _('Rename the modem'), click: function() { self.renameOpen(true); } }, '✎');
		return n;
	},

	renameOpen: function(on) {
		var n = this.nameUi;
		n.box.hidden = !on;
		n.open.hidden = !!on;
		if (on) { n.input.focus(); return; }
		n.dirty = false;
		if (this.lastData) n.input.value = words(this.lastData.name);
	},

	applyName: function(d) {
		var n = this.nameUi;
		this.lastData = d;
		if (n && !n.dirty && document.activeElement !== n.input) n.input.value = words(d.name);
		if (n) n.hint.textContent = _('For example: “Dacha”, “Backup”.') + ' ' + (NAME_FOLLOWS[d.name_follows] || NAME_FOLLOWS.port) + ' ' + _('An empty field means the name by the model.');
	},

	saveName: function() {
		var self = this, n = this.nameUi, id = this.id, name = n.input.value.trim();
		return uciGet('medvemodem', 'modem').then(function(r) {
			var secs = (r && r.values) || {}, s = Object.keys(secs).filter(function(k) { return secs[k].device_id === id; })[0];
			if (s && !name) return uciDelete('medvemodem', s);
			if (s) return uciSet('medvemodem', s, { name: name });
			return name ? uciAdd('medvemodem', 'modem', { device_id: id, name: name }) : null;
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			n.dirty = false;
			n.box.hidden = true;
			n.open.hidden = false;
			self.say(name ? _('The name “%s” is saved.').format(name) : _('The name is deleted: the modem is called by its model.'));
			return self.update();
		}).catch(function(e) { self.say(_('The name is not saved: %s.').format(failText(e))); });
	},

	/* which services answer through this modem's own operator (the alpha's cut: no keys, no speed) */
	netcheckCard: function() {
		var self = this, n = this.nc = {};
		n.say = E('p', { 'class': 'mm-faint mm-nc-say', role: 'status', hidden: '' });
		n.verdict = E('div', { 'class': 'mm-callout is-info mm-nc-verdict', hidden: '' });
		n.body = E('tbody');
		n.table = ncTable(n.body);
		n.button = E('button', { type: 'button', 'class': 'mm-btn mm-nc-run', click: mm.busy(function() { return self.runNetcheck(); }) }, _('Check the services'));
		/* the engine's DNS check of this modem (spec 25.9.20): who answers each public resolver and how fast */
		n.dnsBody = E('tbody');
		n.dnsSay = E('p', { 'class': 'mm-faint mm-dns-say' });
		n.dns = E('div', { 'class': 'mm-dns', hidden: '' }, [E('h4', {}, _('DNS through this modem')), n.dnsSay, dnsTable(n.dnsBody)]);
		n.node = this.sec('netcheck', _('Site access check'), [
			E('p', { 'class': 'mm-faint' }, _('Do the services open through this modem, that is through its operator, even if another channel is main. About 10 seconds and less than 1 MB of traffic. Yandex, VK and Wildberries are for comparison: under whitelists only such open.')),
			n.verdict, n.table, n.say, E('div', { 'class': 'mm-row' }, n.button), n.dns]);
		return n.node;
	},

	/* the SIM's forbidden networks: read on a press; cleared on a confirmed one (a network that refused this SIM once is
	 * never tried again by the automatic selection: roaming, a failed registration) */
	fplmnCard: function() {
		var self = this, f = this.fp = {};
		f.say = E('p', { 'class': 'mm-faint mm-fp-say', role: 'status', hidden: '' });
		f.list = E('ul', { 'class': 'mm-fp-list', hidden: '' });
		f.clear = E('button', { type: 'button', 'class': 'mm-btn mm-fp-clear', hidden: '', click: mm.busy(function() { return self.fplmnRun(true); }) },
			_('Clear the list'));
		/* «Подробный» only (mm-x): an engineer's tool */
		var node = this.sec('fplmn', _('Forbidden networks on the SIM card'), [
			E('p', { 'class': 'mm-faint' }, _('A network that once refused this SIM card (roaming, a failed registration) is written to the card and never tried again by the automatic selection. If the modem does not register in a network it should, look here.')),
			f.list, f.say, E('div', { 'class': 'mm-row' }, [
				E('button', { type: 'button', 'class': 'mm-btn mm-fp-read', click: mm.busy(function() { return self.fplmnRun(false); }) }, _('Read')), f.clear])]);
		node.classList.add('mm-x');
		return node;
	},

	fplmnRun: function(clear) {
		var self = this, f = this.fp;
		if (clear && !window.confirm(_('Clear the forbidden networks on the SIM card? The modem may then try those networks again; the registration may change.')))
			return Promise.resolve();
		f.say.hidden = false;
		text(f.say, clear ? _('Clearing…') : _('Reading…'));
		return fplmn(this.id, !!clear).then(function(r) {
			r = r || {};
			if (r.refused || r.ok === false) {
				text(f.say, r.refused || r.error || _('The SIM card did not answer.'));
				return;
			}
			if (clear) return self.fplmnRun(false);
			var nets = fplmnList(r.lines);
			if (!nets) { text(f.say, _('The SIM card did not give the list: %s').format((r.lines || []).join(' ') || r.final || _('no answer'))); return; }
			f.list.replaceChildren.apply(f.list, nets.map(function(c) {
				var o = mm.OPS[c];
				return E('li', {}, (o ? o[0] + ' · ' : '') + c.slice(0, 3) + '-' + c.slice(3));
			}));
			f.list.hidden = !nets.length;
			f.clear.hidden = !nets.length;
			text(f.say, nets.length ? _('Forbidden: %d.').format(nets.length) : _('The list is empty: no network is forbidden.'));
		}, function(e) {
			text(f.say, _('the MedveModem service does not answer (%s)').format(failText(e)));
		});
	},

	/* the speed through this modem and the shape of it, on a press (spec: the owner 2026-11-03) */
	speedCard: function() {
		var self = this, sp = this.sp = {};
		sp.say = E('p', { 'class': 'mm-faint mm-sp-say', role: 'status', hidden: '' });
		sp.verdict = E('div', { 'class': 'mm-callout is-info mm-sp-verdict', hidden: '' });
		sp.bars = E('div', { 'class': 'mm-sp-bars', 'aria-hidden': 'true' });
		sp.tips = E('ul', { 'class': 'mm-sp-tips', hidden: '' });
		sp.button = E('button', { type: 'button', 'class': 'mm-btn mm-sp-run', click: mm.busy(function() { return self.runSpeed(); }) }, _('Measure the speed'));
		sp.node = this.sec('speed', _('Speed and tethering'), [
			E('p', { 'class': 'mm-faint' }, _('A download and an upload straight through this modem, about 20 seconds; on a good LTE link it uses 30–100 MB of the SIM\'s traffic. The shape of the speed tells an operator\'s cap (a tariff, tethering or a used-up limit) from the radio.')),
			sp.verdict, sp.bars, sp.tips, sp.say, E('div', { 'class': 'mm-row' }, sp.button)]);
		return sp.node;
	},

	runSpeed: function() {
		var self = this, sp = this.sp, dev = this.nc && this.nc.dev;
		if (!dev || sp.running) return Promise.resolve();
		if (!window.confirm(_('Measure the speed through “%s”? It takes about 20 seconds and 30–100 MB of the SIM\'s traffic.').format(dev))) return Promise.resolve();
		sp.running = true; sp.button.disabled = true; sp.say.hidden = false;
		text(sp.say, _('Measuring through “%s”…').format(dev));
		return execLong(fs, 'speedtest', '/usr/libexec/medvemodem/speedtest', [dev], 120000).then(function(r) {
			var j = null;
			try { j = JSON.parse((r && r.stdout) || ''); } catch (e) {}
			if (!j) throw new Error((r && r.stderr) || _('no answer'));
			/* the download measured is a result even when the upload was not (r184) */
			if (!j.down || j.down.error) throw new Error(mm.speedErr(j));
			self.showSpeed(j);
			text(sp.say, _('Measured through “%s”, server %s; used %s.').format(j.device, j.server, bytes(((j.down || {}).bytes || 0) + ((j.up || {}).bytes || 0))) +
				(j.error ? ' ' + _('Not measured: %s.').format(mm.speedErr(j)) : ''));
		}).catch(function(e) {
			text(sp.say, _('The measurement did not run: %s.').format(e && e.message || e));
		}).then(function() { sp.running = false; sp.button.disabled = !(self.nc && self.nc.dev); });
	},

	showSpeed: function(j) {
		var sp = this.sp, d = this.data || {}, m = d.modemmanager || {}, lte = ((m.signal || {}).lte) || ((m.signal || {})['5g']) || {};
		var sh = speedShape((j.down || {}).kbps, { rsrp: lte.rsrp, sinr: lte.snr }), up = speedShape((j.up || {}).kbps, null);
		var ttl = (d.connection || {}).ttl;
		var say = {
			none: _('Too short to tell: the measurement gave fewer than 3 seconds.'),
			dead: _('Almost nothing comes through: the operator blocks the traffic or the limit is used up.'),
			cut: _('Fast for the first seconds, then cut down to about %s and flat: that is how an operator caps tethering or a used-up limit.').format(kbpsWords(sh.cap || 0)),
			shelf: _('A flat speed of about %s: it looks like the operator caps it (the tariff, tethering or a used-up limit).').format(kbpsWords(sh.cap || 0)),
			radio: _('The speed follows the signal, which is weak: it is the radio, not a cap.'),
			ok: _('No sign of a cap.') }[sh.kind];
		sp.verdict.hidden = false;
		sp.verdict.className = 'mm-callout mm-sp-verdict ' + (sh.kind === 'ok' ? 'is-ok' : sh.kind === 'radio' || sh.kind === 'none' ? 'is-info' : 'is-err');
		text(sp.verdict, _('Download: %s at peak, %s on average; upload: %s on average.').format(kbpsWords(sh.peak || 0), kbpsWords(sh.mean || 0), kbpsWords(up.mean || 0)) + ' ' + say);
		/* the shape itself, one bar a second */
		var max = Math.max.apply(null, [1].concat(list((j.down || {}).kbps)));
		sp.bars.replaceChildren.apply(sp.bars, list((j.down || {}).kbps).map(function(k) {
			return E('span', { 'class': 'mm-sp-bar', style: 'height:' + Math.max(2, Math.round(40 * k / max)) + 'px', title: kbpsWords(k) });
		}));
		/* what to do, only what fits this router (a WH3000 Pro owner's report: no TTL, hardware offloading) */
		var tips = [];
		if (sh.kind === 'shelf' || sh.kind === 'cut' || sh.kind === 'dead') {
			if (!ttl) tips.push(_('TTL is not fixed for this modem: turn on «Fix TTL 64» in its settings, the operator may tell tethering by TTL.'));
			else if (ttl.hw_offload) tips.push(_('Hardware flow offloading is on: offloaded connections keep their TTL. Switch it off in Network → Firewall.'));
			else tips.push(_('TTL is already fixed: the operator may tell tethering by the modem\'s IMEI or by the requests. Check that the tariff allows a modem; send the clients\' DNS and traffic through the tunnel.'));
			if (sh.kind !== 'dead') tips.push(_('Measure again with the computers and phones of the LAN switched off: a cap that stays is the tariff, one that goes is for tethering.'));
		}
		sp.tips.replaceChildren.apply(sp.tips, tips.map(function(t) { return E('li', {}, t); }));
		sp.tips.hidden = !tips.length;
	},

	applyNetcheck: function(d) {
		var n = this.nc, dev = words(((d || {}).uplink || {}).device), up = d && d.present === true && ((d.session || {}).state === 'connected');
		n.node.hidden = this.level === 'normal';
		n.dev = up ? dev : '';
		if (this.sp) { this.sp.node.hidden = this.level === 'normal'; if (!this.sp.running) this.sp.button.disabled = !n.dev; }
		if (!n.running) n.button.disabled = !n.dev;
		this.applyDnsCheck(((d || {}).uplink || {}).dns_check);
		if (!n.running && !n.dev) { n.say.hidden = false; text(n.say, _('The modem is not connected: nothing to check through.')); }
		else if (!n.running && n.say.textContent === _('The modem is not connected: nothing to check through.')) n.say.hidden = true;
	},

	applyClash: function(c) {
		if (!c) { this.clashNote.hidden = true; return; }
		if (c && c.modem) text(this.clashText, _('The modem gave the router the address %s, in the same network %s as the modem “%s”: the router cannot tell the two apart, one of them has no internet and its web page does not open. MedveModem moves an MF150-4 by itself; otherwise give one of them another subnet in its web, for example 192.168.8.1.')
			.format(c.address + (c.gateway ? ' ' + _('from %s').format(c.gateway) : ''), c.router, c.modem_name || c.modem));
		else if (c) text(this.clashText, _('The modem gave the router the address %s, inside the router\'s own network %s (%s): nothing goes to the internet through it, its web page and port forward point into the LAN. Give the modem another subnet in its web, for example 192.168.8.1.')
			.format(c.address + (c.gateway ? ' ' + _('from %s').format(c.gateway) : ''), c.router, c.interface));
		bannerShow(this.clashNote, true);
	},

	applyDnsCheck: function(c) {
		var n = this.nc;
		n.dns.hidden = !c || !list(c.resolvers).length;
		if (n.dns.hidden) return;
		n.dnsBody.replaceChildren.apply(n.dnsBody, dnsRows(c));
		text(n.dnsSay, dnsSummary(c));
	},

	runNetcheck: function() {
		var self = this, n = this.nc, dev = n.dev;
		if (!dev || n.running) return Promise.resolve();
		n.running = true; n.button.disabled = true; n.say.hidden = false;
		text(n.say, _('Checking through “%s”…').format(dev));
		return execLong(fs, 'netcheck', '/usr/libexec/medvemodem/netcheck', [dev]).then(function(r) {
			var j = null;
			try { j = JSON.parse((r && r.stdout) || ''); } catch (e) {}
			if (!j || j.error) throw new Error(NC_ERROR[(j || {}).error] || (j && j.error) || (r && r.stderr) || _('no answer'));
			self.showNetcheck(j);
			text(n.say, _('Checked at %s through “%s”.').format(new Date((j.at || 0) * 1000).toLocaleTimeString(LANG), j.device));
		}).catch(function(e) {
			text(n.say, _('The check did not run: %s.').format(e && e.message || e));
		}).then(function() { n.running = false; n.button.disabled = !n.dev; });
	},

	showNetcheck: function(j) {
		var n = this.nc, res = list(j.results), say = ncSummary(res, _('this modem'));
		n.body.replaceChildren.apply(n.body, ncRows(res));
		n.table.hidden = !res.length;
		n.verdict.hidden = !say;
		text(n.verdict, say);
	},

	/* ModemManager's own words about a port it cannot drive (the owner, 2026-10-27): a hint, nothing runs by it */
	mmlogCard: function() {
		this.mmlogList = E('ul', { 'class': 'mm-mmlog' });
		this.mmlogNode = this.sec('mmlog', _('ModemManager reports'), [
			E('p', { 'class': 'mm-faint' }, _('What ModemManager wrote to the system log about this modem\'s ports. It is a hint for the diagnosis: MedveModem starts nothing because of it.')),
			this.mmlogList]);
		this.mmlogNode.hidden = true;
		return this.mmlogNode;
	},

	applyMmlog: function(d) {
		var hints = list((d || {}).mm_log);
		var SAY = {
			'port-lost': _('lost control of the port: another program took it'),
			'port-busy': _('could not open the port: another program holds it'),
			'mbim-not-opened': _('the modem answers that the MBIM session is not open: two programs drive the port in turn'),
			reprobe: _('the connection to the port broke, ModemManager probes the modem again'),
			'no-at': _('found no AT port of the modem'),
			'unhandled-port': _('does not know what one of the ports is'),
			'sim-changed': _('the SIM card changed'),
			'init-timeout': _('the modem did not answer ModemManager\'s start in time (a cold start of a T77W968 or DW5821e)'),
			unusable: _('gave up on the modem («unusable») and does not try again by itself: if it stays so, the recovery resets it on USB; when that does not help either, only taking its power off does') };
		var CONFLICT = { 'port-lost': 1, 'port-busy': 1, 'mbim-not-opened': 1, reprobe: 1 };
		/* a program outside MedveModem's stack holding this modem's port (the neighbours helper, 2026-10-28) */
		var holders = list((d || {}).port_holders).map(function(h) {
			return E('li', { 'class': 'mm-warn-text' }, _('%s (PID %d) holds %s: no AT command goes to that port while it is held. If it came with another modem package, switch that package off.').format(h.comm, h.pid, h.node));
		});
		this.mmlogNode.hidden = !hints.length && !holders.length;
		this.mmlogList.replaceChildren.apply(this.mmlogList, holders.concat(hints.slice().reverse().map(function(h) {
			return E('li', {}, (SAY[h.kind] || h.kind) + (h.port ? ' (' + h.port + ')' : '') + ' · ' +
				N_(h.count || 1, '%d time, the last at %s', '%d times, the last at %s').format(h.count || 1, clock(h.at)));
		}).concat(hints.some(function(h) { return CONFLICT[h.kind]; }) ? [E('li', { 'class': 'mm-warn-text' },
			_('Usually it means a second program drives the same port: an interface set up by hand (umbim, uqmi), QModem or another modem manager. Leave the modem to one of them.'))] : [])));
	},

	/* one button for a report to send to whoever helps (alpha): the engine's sanitized diagnostic bundle, downloaded as a file */
	reportCard: function() {
		var self = this;
		this.reportSay = E('p', { 'class': 'mm-faint mm-report-say', role: 'status', hidden: '' });
		this.reportButton = E('button', { type: 'button', 'class': 'mm-btn mm-report', click: mm.busy(function() { return self.saveReport(); }) }, _('Collect a report for analysis'));
		return this.sec('report', _('Modem report'), [
			E('p', { 'class': 'mm-faint mm-b' }, _('A file for whoever helps to sort the problem out. Identifiers, passwords and SMS text do not get into it.')),
			E('p', { 'class': 'mm-faint mm-x' }, _('A file for the one who helps to sort it out or adds a model profile: the device\'s composition, the engine\'s decisions and diagnosis, the event history, the signal, the system log lines about the modem, the modem\'s answers to the model and USB mode requests (only if the AT port is MedveModem\'s; a modem under ModemManager or QModem is not touched). IMEI, IMSI, ICCID, phone numbers, PINs and passwords do not get into the file, the serial number is replaced by a hash; SMS text is not collected. Open the file and check it before sending.')),
			this.reportSay, E('div', { 'class': 'mm-row' }, [this.reportButton,
				E('a', { 'class': 'mm-btn is-quiet mm-report-issue', href: mm.NEW_MODEM_ISSUE, target: '_blank', rel: 'noopener' },
					_('Send to GitHub'))])]);
	},

	saveReport: function() {
		var self = this, id = this.id, b = this.reportButton;
		if (!id) return;
		b.disabled = true;
		this.reportSay.hidden = false;
		text(this.reportSay, _('Collecting the report…'));
		/* --at: the model's answers through the engine's own AT port; one held by ModemManager or QModem is not touched */
		return execLong(fs, 'diag', '/usr/sbin/medvemodem', ['diag', 'profile', id, '--label', 'report', '--stdout', '--at']).then(function(r) {
			var out = r && r.code === 0 ? String(r.stdout || '') : '';
			var json = null;
			try { json = out ? JSON.parse(out) : null; } catch (e) {}
			if (!json) throw new Error((r && r.stderr || '').trim() || _('empty answer'));
			var name = 'medvemodem-report-' + String(((json.device || {}).device_id) || 'modem').replace(/[^A-Za-z0-9._-]+/g, '_') + '-' +
				new Date().toISOString().slice(0, 16).replace(/[-:T]/g, '') + '.json';
			var a = E('a', { href: URL.createObjectURL(new Blob([out], { type: 'application/json' })), download: name, hidden: '' });
			document.body.appendChild(a);
			a.click();
			window.setTimeout(function() { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
			text(self.reportSay, _('The report is saved: %s.').format(name));
		}).catch(function(e) {
			text(self.reportSay, _('The report is not collected: %s.').format(e && e.message || e));
		}).then(function() { b.disabled = false; });
	},

	/* the SIM, and the operator's label for it (number, description), kept in UCI by its ICCID */
	simCard: function() {
		var self = this, f = this.simForm = {};
		var node = this.simNode = this.rows(_('SIM card'), [['sim_desc', _('Description')], ['sim_state', _('State')], ['sim_slot', _('SIM slot')], ['sim_op', _('SIM operator')], ['sim_num', _('Number')],
			['sim_smsc', _('SMS centre')], ['iccid', 'ICCID', 'expert'], ['imsi', 'IMSI', 'expert']], 'sim');
		f.number = E('input', { type: 'tel', 'class': 'mm-input', id: 'mm-sim-number', autocomplete: 'off', placeholder: '+7…', maxlength: '20',
			input: function() { f.dirty = true; } });
		f.description = E('input', { type: 'text', 'class': 'mm-input', id: 'mm-sim-desc', autocomplete: 'off', placeholder: _('For example: work, unlimited'), maxlength: '64',
			input: function() { f.dirty = true; } });
		/* the SMS centre (bench 2026-10-16): on the label it goes with every message; on the SIM over the engine's AT port */
		f.smsc = E('input', { type: 'tel', 'class': 'mm-input', id: 'mm-sim-smsc', autocomplete: 'off', placeholder: _('Empty means as written on the SIM card'),
			maxlength: '21', input: function() { f.dirty = true; } });
		f.simSc = E('span', { 'class': 'mm-dim' });
		f.writeSc = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sim-smsc-write', click: mm.busy(function() { return self.writeSmsCenter(); }) },
			_('Write to the SIM card'));
		f.scRow = E('div', { 'class': 'mm-row mm-sim-smsc-row', hidden: '' }, [f.simSc, f.writeSc]);
		/* the T99's storage switch (a field recipe, HANDOFF 150): after a modem flash the SMS stop arriving */
		f.kick = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sms-storage', click: mm.busy(function() { return self.smsStorageKick(); }) },
			_('Switch the SMS storage'));
		f.kickRow = E('div', { 'class': 'mm-sms-kick', hidden: '' }, [
			E('p', { 'class': 'mm-faint' }, _('If incoming SMS stopped coming after the modem was reflashed (firmware F0.6.0.0.6 and newer): the modem once switches the SMS storage to the SIM card and back to its own. The link does not break, the setting is kept in the modem.')),
			f.kick]);
		f.quota = E('input', { type: 'text', inputmode: 'decimal', 'class': 'mm-input', id: 'mm-sim-quota', autocomplete: 'off', placeholder: _('No limit'),
			maxlength: '8', input: function() { f.dirty = true; } });
		f.day = E('select', { 'class': 'mm-select', id: 'mm-sim-day', change: function() { f.dirty = true; } },
			Array.apply(null, Array(28)).map(function(x, i) { return E('option', { value: String(i + 1) }, String(i + 1)); }));
		f.warn = E('select', { 'class': 'mm-select', id: 'mm-sim-warn', change: function() { f.dirty = true; } },
			[50, 70, 80, 90, 95].map(function(p) { return E('option', { value: String(p) }, p + ' %'); }));
		f.action = E('select', { 'class': 'mm-select', id: 'mm-sim-action', change: function() { f.dirty = true; } }, [
			E('option', { value: 'warn' }, _('Warn only')), E('option', { value: 'backup' }, _('Move the channel to backup'))]);
		f.save = E('button', { type: 'button', 'class': 'mm-btn mm-sim-save', click: mm.busy(function() { return self.saveSim(); }) }, _('Save'));
		f.box = E('div', { 'class': 'mm-sim-edit', hidden: '' }, [
			E('p', { 'class': 'mm-faint' }, _('The SIM card\'s label is kept in the router\'s settings by its ICCID and shows whichever modem the card is in.')),
			E('label', { 'for': 'mm-sim-number', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Number')), f.number]),
			E('label', { 'for': 'mm-sim-desc', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Description')), f.description]),
			E('label', { 'for': 'mm-sim-smsc', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('SMS centre')), f.smsc]),
			E('p', { 'class': 'mm-faint' }, _('Needed only if SMS do not go out: a SIM card of another operator, the centre on it is erased. The one set here goes into each message sent from this SIM card; it does not affect receiving.')),
			f.scRow,
			f.kickRow,
			E('label', { 'for': 'mm-sim-quota', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Monthly traffic limit, GB')), f.quota]),
			E('label', { 'for': 'mm-sim-day', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Period start day')), f.day]),
			E('label', { 'for': 'mm-sim-warn', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Warn at usage')), f.warn]),
			E('label', { 'for': 'mm-sim-action', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('When the limit is used up')), f.action]),
			E('p', { 'class': 'mm-faint' }, _('The modem\'s traffic counts while this SIM card is in it. “To backup”: with channel switching on, the traffic goes through another working channel, and this one stays spare until the next period.')),
			E('div', { 'class': 'mm-row' }, f.save)]);
		node.querySelector('.mm-sec-b').appendChild(f.box);
		/* a SIM that asks for its PIN or PUK (the owner, 2026-10-17): unlocked through ModemManager; the codes go to the
		 * engine's sim_unlock only, the PIN to the router's settings only when the operator ticks «Запомнить» */
		var num = function(id, hint, max) { return E('input', { type: 'password', inputmode: 'numeric', autocomplete: 'off', 'class': 'mm-input', id: id,
			maxlength: max, placeholder: hint, pattern: '[0-9]*' }); };
		f.pin = num('mm-sim-pin', _('4–8 digits'), '8');
		f.pinLeft = E('span', { 'class': 'mm-dim mm-sim-left' });
		f.keep = E('input', { type: 'checkbox', id: 'mm-sim-keep' });
		f.unlock = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-sim-unlock', click: mm.busy(function() { return self.unlockSim(false); }) },
			_('Unlock'));
		f.pinBox = E('div', { 'class': 'mm-callout is-warn mm-sim-pinbox', hidden: '' }, [
			E('strong', {}, _('The SIM card is locked by its PIN')),
			E('label', { 'for': 'mm-sim-pin', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('PIN')), f.pin]),
			f.pinLeft,
			E('label', { 'class': 'mm-check', 'for': 'mm-sim-keep' }, [f.keep, _('Remember the PIN on the router: the modem unlocks by itself after a restart')]),
			E('div', { 'class': 'mm-row' }, f.unlock)]);
		f.puk = num('mm-sim-puk', _('8 digits'), '8');
		f.newPin = num('mm-sim-newpin', _('4–8 digits'), '8');
		f.newPin2 = num('mm-sim-newpin2', _('again'), '8');
		f.pukLeft = E('span', { 'class': 'mm-dim mm-sim-left' });
		f.unlockPuk = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-sim-unlock-puk', click: mm.busy(function() { return self.unlockSim(true); }) },
			_('Unlock'));
		f.pukBox = E('div', { 'class': 'mm-callout is-err mm-sim-pukbox', hidden: '' }, [
			E('strong', {}, _('The SIM card is locked: the PUK is needed')),
			E('p', { 'class': 'mm-faint' }, _('The PUK (8 digits) is on the plastic card the SIM card came with, or at the operator. A new PIN is set with it.')),
			E('label', { 'for': 'mm-sim-puk', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('PUK')), f.puk]),
			E('label', { 'for': 'mm-sim-newpin', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('New PIN')), f.newPin]),
			E('label', { 'for': 'mm-sim-newpin2', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('New PIN again')), f.newPin2]),
			f.pukLeft,
			E('div', { 'class': 'mm-row' }, f.unlockPuk)]);
		/* a PIN kept on the router: said, and taken back on the operator's word */
		f.forget = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sim-forget', click: mm.busy(function() { return self.forgetPin(); }) },
			_('Forget the PIN'));
		f.keptRow = E('div', { 'class': 'mm-row mm-sim-kept', hidden: '' }, [
			E('span', { 'class': 'mm-dim' }, _('The SIM card\'s PIN is saved on the router: after a restart the modem unlocks it by itself.')), f.forget]);
		/* a module with two slots, a physical one and an eUICC (the owner, 2026-10-27) */
		f.slotSel = E('select', { 'class': 'mm-select', id: 'mm-sim-slot' });
		f.slotGo = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-sim-slot-go', click: mm.busy(function() { return self.switchSlot(); }) },
			_('Switch the slot'));
		f.slotRow = E('div', { 'class': 'mm-row mm-sim-slot', hidden: '' }, [
			E('label', { 'for': 'mm-sim-slot', 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Use the SIM slot')), f.slotSel]), f.slotGo]);
		var body = node.querySelector('.mm-sec-b');
		body.insertBefore(f.slotRow, body.firstChild);
		body.insertBefore(f.keptRow, body.firstChild);
		body.insertBefore(f.pukBox, body.firstChild);
		body.insertBefore(f.pinBox, body.firstChild);
		return node;
	},

	/* the SIM's PIN, or its PUK with a new PIN, through the engine (sim_unlock); the fields are emptied after every try */
	unlockSim: function(puk) {
		var self = this, f = this.simForm, id = this.id, k = this.connInfo || {};
		var pin = (puk ? f.newPin : f.pin).value.trim(), code = puk ? f.puk.value.trim() : '', keep = !puk && f.keep.checked;
		var left = (this.simRetries || {})[puk ? 'sim-puk' : 'sim-pin'], last = left === 1;
		var clear = function() { [f.pin, f.puk, f.newPin, f.newPin2].forEach(function(i) { i.value = ''; }); };
		if (puk && !/^\d{8}$/.test(code)) { this.say(_('The PUK is exactly 8 digits.')); return Promise.resolve(); }
		if (!/^\d{4,8}$/.test(pin)) { this.say(puk ? _('The new PIN is 4 to 8 digits.') : _('The PIN is 4 to 8 digits.')); return Promise.resolve(); }
		if (puk && pin !== f.newPin2.value.trim()) { this.say(_('The new PIN and its repeat differ.')); return Promise.resolve(); }
		if (last && !window.confirm(puk ? _('The last PUK try: on an error the SIM card is locked for good. Continue?') :
			_('The last try: on an error the SIM card is locked and the PUK is needed. Continue?'))) return Promise.resolve();
		this.say(puk ? _('Sending the PUK…') : _('Sending the PIN…'));
		var call = puk ? (last ? simUnlockPuk(id, pin, code, true) : simUnlockPuk(id, pin, code)) : (last ? simUnlock(id, pin, true) : simUnlock(id, pin));
		clear();
		return call.then(function(r) {
			r = r || {};
			if (r.refused) { self.say(_('The SIM card is not unlocked: %s.').format(why(r.refused))); return self.update(); }
			if (!r.ok) {
				var lock = r.unlock_required, n = (r.retries || {})[lock === 'sim-puk' ? 'sim-puk' : 'sim-pin'];
				self.say(_('The SIM card is not unlocked: %s').format(why(r.error) || _('no answer')) +
					(!puk && lock === 'sim-puk' ? '. ' + _('The PIN tries are used up: the PUK is needed') : typeof n === 'number' ? '. ' + _('Tries left: %d').format(n) : '') + '.');
				return self.update();
			}
			/* a PUK sets a new PIN: a kept one follows it, else the router would send the old one at every boot */
			var save = keep ? pin : puk && k.pin_saved ? pin : null;
			return (save || k.pin_saved ? self.keepPin(save) : Promise.resolve('')).then(function(note) {
				self.say(_('The SIM card is unlocked.') + note);
				return self.update();
			});
		}, function(e) { self.say(_('The SIM card is not unlocked: %s').format(_('the MedveModem service does not answer (%s).').format(failText(e)))); });
	},

	/* the PIN in the device section (option pin; the engine hands it to netifd as pincode), or taken out (pin null) */
	keepPin: function(pin) {
		var id = this.id, k = this.connInfo || {};
		return uciGet('medvemodem', 'device').then(function(r) {
			var secs = (r && r.values) || {}, name = Object.keys(secs).filter(function(n) { return secs[n].id === id; })[0];
			if (pin) return name ? uciSet('medvemodem', name, { pin: pin }) : uciAdd('medvemodem', 'device', { id: id, pin: pin });
			return name ? uciDelOption('medvemodem', name, 'pin') : null;
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			/* MedveModem's interface takes the change at its next configuration: written now with the bring-up */
			if (k.managed) return bringup(id).then(function() {
				return ' ' + (pin ? _('The PIN is saved on the router: after a restart the modem unlocks the SIM card by itself.') : _('The PIN is deleted from the router.'));
			});
			if (!pin) return ' ' + _('The PIN is deleted from the router.');
			return ' ' + (k.user_interface ? _('The PIN is saved, but the interface “%s” is not set up through MedveModem: the PIN does not get into it.').format(k.user_interface) :
				_('The PIN is saved: it is written to the connection settings when connecting through MedveModem.'));
		}).catch(function(e) { return ' ' + (pin ? _('The PIN is not saved: %s.') : _('The PIN is not deleted: %s.')).format(failText(e)); });
	},

	forgetPin: function() {
		var self = this, k = this.connInfo || {};
		if (k.managed && !window.confirm(_('Delete the PIN from the router? The connection through the modem restarts, and after a restart the SIM card has to be unlocked by hand.'))) return Promise.resolve();
		return this.keepPin(null).then(function(note) { self.say(note.trim()); return self.update(); });
	},

	/* the slot ModemManager uses: the modem is probed again, its link breaks for that time */
	switchSlot: function() {
		var self = this, f = this.simForm, slot = +f.slotSel.value;
		if (!window.confirm(_('Switch the modem to SIM slot %d? ModemManager restarts the modem for it: the link through it breaks for up to a minute.').format(slot)))
			return Promise.resolve();
		f.slotGo.disabled = true;
		return simSlot(this.id, slot).then(function(r) {
			r = r || {};
			self.say(r.ok ? _('The modem switches to slot %d…').format(slot) : _('Error: %s').format(why(r.refused || r.error) || _('no answer')));
			return self.update();
		}).catch(function(e) { self.say(_('Error: %s').format(failText(e))); });
	},

	applySim: function(d, mm) {
		/* a modem ModemManager does not hold: the SIM the engine read through QModem or on the AT port (r64) */
		var r = this.refs, f = this.simForm, card = mm.sim_card || d.sim || {}, lab = d.sim_label || {};
		var own = list(mm.own_numbers).filter(function(n) { return typeof n === 'string'; });
		text(r.sim_desc, words(lab.description));
		/* PIN2/PUK2 guard only the SIM's fixed-dialling list, never the connection: not a lock (as the engine reads it) */
		var lock = words(mm.unlock_required), soft = lock === 'sim-pin2' || lock === 'sim-puk2';
		/* a SIM that reads, not just +CPIN: READY (an old modem says READY with no SIM in it: a tester's MF192, 2026-11-11) */
		var simRead = !!(card.imsi || card.iccid || card.operator_code || (d.sim_operator || {}).code || ['home', 'roaming'].indexOf(mm.registration) >= 0);
		text(r.sim_state, !(mm.state || mm.model || mm.sim) ? (d.sim && d.sim.iccid ? _('Inserted') + ' (' + (d.sim.source === 'qmodem' ? _('read through QModem') :
			_('read over AT')) + ')' : '') : lock === 'sim-pin' ? _('The PIN is needed') : lock === 'sim-puk' ? _('Locked, the PUK is needed') :
			lock && lock !== 'none' && !soft ? _('Locked (%s)').format(lock) : mm.sim && !simRead ? _('Not read: the modem reports it ready, but gives no IMSI') :
			mm.sim ? _('Inserted') + (soft && this.level === 'expert' ? ' (' + _('PIN2 not entered: it does not affect the link') + ')' : '') : _('Not found'));
		text(r.sim_op, [words(card.operator_name), words(card.operator_code) ? '(' + card.operator_code + ')' : ''].filter(Boolean).join(' '));
		secret(r.sim_num, words(lab.number) || own.join(', '));
		this.simAt = !!((d.capabilities || {}).at || {}).ok;
		text(r.sim_smsc, !card.iccid ? '' : words(lab.sms_center) ? _('%s (set for this SIM card)').format(lab.sms_center) :
			this.simCenter ? _('%s (on the SIM card)').format(this.simCenter) : this.simAt ? '' : _('as written on the SIM card'));
		f.scRow.hidden = !this.simAt;
		f.kickRow.hidden = !this.simAt || !/^foxconn-t99/.test(((d.profile || {}).id) || '');
		text(f.simSc, _('On the SIM card: %s').format(this.simCenter || '—'));
		if (this.simAt && f.iccid && this.simCenterFor !== f.iccid) {
			var self = this;
			this.simCenterFor = f.iccid;
			smsCenter(this.id).then(function(res) {
				if (res && res.center) { self.simCenter = res.center; if (self.simLast) self.applySim(self.simLast, self.simLast.modemmanager || {}); }
			}).catch(function() {});
		}
		/* the lock: the PIN or the PUK box; the tries left as ModemManager counts them */
		var tries = mm.unlock_retries || {}, conn = d.connection || {};
		this.simRetries = tries;
		f.pinBox.hidden = lock !== 'sim-pin';
		f.pukBox.hidden = lock !== 'sim-puk';
		/* said above the tabs: the card sits in «Настройки» */
		this.alerts.sim = lock === 'sim-pin' ? _('The SIM card waits for its PIN: without it the modem does not connect.') :
			lock === 'sim-puk' ? _('The SIM card is locked: the PUK is needed.') : '';
		this.applyAlert();
		text(f.pinLeft, typeof tries['sim-pin'] === 'number' ? _('Tries left: %d').format(tries['sim-pin']) : '');
		text(f.pukLeft, typeof tries['sim-puk'] === 'number' ? _('PUK tries left: %d').format(tries['sim-puk']) : '');
		f.pinLeft.classList.toggle('mm-warn-text', tries['sim-pin'] === 1);
		f.pukLeft.classList.toggle('mm-warn-text', tries['sim-puk'] === 1);
		if (lock === 'sim-pin' && this.simLockShown !== lock) f.keep.checked = !!conn.pin_saved;	/* the operator's last choice */
		this.simLockShown = lock;
		f.keptRow.hidden = !conn.pin_saved || lock === 'sim-pin' || lock === 'sim-puk';
		secret(r.iccid, words(card.iccid)); secret(r.imsi, words(card.imsi));
		/* the slots ModemManager reports: which one is in use, which hold a card; eSIM said when the SIM says so */
		var slots = list(mm.sim_slots), prim = mm.primary_sim_slot;
		text(r.sim_slot, slots.length > 1 && prim ? _('%d of %d').format(prim, slots.length) + (card.type === 'esim' ? ' (eSIM)' : '') : '');
		f.slotRow.hidden = slots.length < 2 || !prim;
		if (slots.length > 1 && document.activeElement !== f.slotSel && f.slotSel.options.length !== slots.length) {
			f.slotSel.replaceChildren.apply(f.slotSel, slots.map(function(has, i) {
				return E('option', { value: String(i + 1) }, _('Slot %d').format(i + 1) + (has ? '' : ' (' + _('empty') + ')'));
			}));
		}
		if (prim && document.activeElement !== f.slotSel && f.slotGo.disabled) f.slotSel.value = String(prim);
		f.slotGo.disabled = !prim || f.slotSel.value === String(prim);
		f.slotSel.onchange = function() { f.slotGo.disabled = f.slotSel.value === String(prim); };
		f.iccid = words(card.iccid);
		f.box.hidden = !f.iccid;
		if (!f.dirty && [f.number, f.description, f.quota, f.day, f.warn, f.action].indexOf(document.activeElement) < 0) {
			var q = d.quota || {};
			f.number.value = words(lab.number) || own[0] || '';
			f.description.value = words(lab.description);
			f.smsc.value = words(lab.sms_center);
			f.quota.value = typeof q.limit_bytes === 'number' ? num(q.limit_bytes / 1e9) : '';
			f.day.value = /-(\d{2})$/.test(q.period_start || '') ? String(+q.period_start.slice(8)) : '1';
			f.warn.value = [50, 70, 80, 90, 95].indexOf(q.warn_percent) >= 0 ? String(q.warn_percent) : '80';
			f.action.value = q.action === 'backup' ? 'backup' : 'warn';
		}
	},

	saveSim: function() {
		var self = this, f = this.simForm, iccid = f.iccid;
		var quota = f.quota.value.trim().replace(',', '.');
		var values = { iccid: iccid, number: f.number.value.trim(), description: f.description.value.trim(),
			sms_center: f.smsc.value.replace(/[\s()-]/g, ''),
			quota_gb: quota, quota_day: f.day.value, quota_warn: f.warn.value, quota_action: f.action.value };
		if (!iccid) return Promise.resolve();
		if (quota && !(/^\d{1,6}(\.\d{1,3})?$/.test(quota) && +quota > 0)) { this.say(_('Limit: a number of gigabytes, for example 50 or 2.5; empty means no limit.')); return Promise.resolve(); }
		if (values.number && !/^\+?[0-9 ()-]{3,20}$/.test(values.number)) { this.say(_('Number: digits, spaces, brackets, hyphen and “+” at the start.')); return Promise.resolve(); }
		if (/[\x00-\x1f]/.test(values.description)) { this.say(_('Description: without control characters.')); return Promise.resolve(); }
		if (values.sms_center && !/^\+?\d{3,20}$/.test(values.sms_center)) { this.say(_('SMS centre: digits and “+” at the start, for example +79037011111.')); return Promise.resolve(); }
		this.say(_('Saving the SIM card\'s label…'));
		return uciGet('medvemodem', 'sim').then(function(res) {
			var secs = (res && res.values) || {}, name = Object.keys(secs).filter(function(n) { return secs[n].iccid === iccid; })[0];
			return name ? uciSet('medvemodem', name, values) : uciAdd('medvemodem', 'sim', values);
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			f.dirty = false;
			self.say(_('The SIM card\'s label is saved.'));
			return self.update();
		}).catch(function(e) { self.say(_('Saving error: %s').format(failText(e))); });
	},

	smsStorageKick: function() {
		var self = this;
		if (!confirm(_('Switch the SMS storage to the SIM card and back to the modem\'s storage? A one-time command for a T99 after reflashing.')))
			return Promise.resolve();
		this.say(_('Switching the SMS storage…'));
		return smsStorage(this.id).then(function(r) {
			r = r || {};
			if (r.refused) { self.say(_('Not done: %s').format(why(r.refused))); return; }
			self.say(r.ok ? _('The SMS storage is switched') + (r.storage ? ' (' + r.storage + ')' : '') + '. ' + _('Send an SMS to this number to check.') :
				_('The modem did not take the command') + (r.detail ? ' (' + r.detail + ')' : '') + '.');
		}, function(e) { self.say(_('Not done: %s').format(_('the MedveModem service does not answer (%s).').format(failText(e)))); });
	},

	/* the SMS centre written on the SIM itself (AT+CSCA over the engine's port): it travels with the card */
	writeSmsCenter: function() {
		var self = this, f = this.simForm, v = f.smsc.value.replace(/[\s()-]/g, '');
		if (!/^\+?\d{3,20}$/.test(v)) { this.say(_('Enter the SMS centre in the field above, for example +79037011111.')); return Promise.resolve(); }
		if (!confirm(_('Write the SMS centre %s to the SIM card? The previous one (%s) is replaced.').format(v, this.simCenter || _('unknown')))) return Promise.resolve();
		f.writeSc.disabled = true;
		this.say(_('Writing the SMS centre to the SIM card…'));
		return smsCenter(this.id, v).then(function(r) {
			r = r || {};
			if (r.refused) { self.say(_('The SMS centre is not written: %s').format(why(r.refused))); return; }
			if (r.center) self.simCenter = r.center;
			self.say(r.ok ? _('The SMS centre %s is written to the SIM card.').format(r.center || v) :
				_('The modem did not take the SMS centre') + (r.detail ? ' (' + r.detail + ')' : '') + '.');
			if (self.simLast) self.applySim(self.simLast, self.simLast.modemmanager || {});
		}, function(e) { self.say(_('The SMS centre is not written: %s').format(_('the MedveModem service does not answer (%s).').format(failText(e)))); })
			.finally(function() { f.writeSc.disabled = false; });
	},

	/* a usb-mode.json rule that unconfigures this modem on every hotplug (H7): the operator takes it out */
	mswCard: function() {
		var self = this;
		this.mswButton = E('button', { type: 'button', 'class': 'mm-btn is-danger', click: mm.busy(function() { return self.fixModeswitch(); }) }, _('Switch the rule off'));
		this.mswNode = this.sec('msw', _('usb-modeswitch rule'), [
			E('div', { 'class': 'mm-callout is-err' }, _('/etc/usb-mode.json has a rule that removes the USB configuration from this modem at each connection. While the modem works, such a reset may crash the kernel.')),
			risk(_('changes a file on the router'), _('reversible: the original file is kept beside it as /etc/usb-mode.json.medvemodem-backup.')),
			E('p', { 'class': 'mm-faint' }, _('The rule will be renamed (usbmode stops finding it). Other modem packages do the same.')),
			E('div', { 'class': 'mm-row' }, this.mswButton)]);
		this.mswNode.hidden = true;
		return this.mswNode;
	},

	applyMSW: function(d) {
		this.mswNode.hidden = ((d || {}).configuration || {}).state !== 'modeswitch-rule';
		this.alerts.msw = !this.mswNode.hidden;
		this.applyStick(d);
		this.applyAlert();
	},

	/* the FCC unlock of a model whose firmware keeps the radio off without it (T77W968 family, H6; HANDOFF 176): the
	 * operator turns ModemManager's hook on or off, nothing does it by itself */
	fccCard: function() {
		var self = this;
		this.fccState = E('div', { 'class': 'mm-callout mm-fcc-state' });
		this.fccButton = E('button', { type: 'button', 'class': 'mm-btn mm-fcc-toggle', click: mm.busy(function() { return self.toggleFcc(); }) }, '');
		this.fccNode = this.sec('fcc', _('FCC unlock'), [
			E('p', {}, _('This model\'s firmware may keep the radio off until the router sends the FCC unlock command: the modem is seen but does not go online, and a Dell DW5821e also reconnects about every 5 minutes.')),
			this.fccState,
			risk(_('changes a ModemManager setting on the router'), _('reversible by the “Switch off” button; nothing is written into the modem\'s memory.')),
			E('p', { 'class': 'mm-faint' }, _('If switched on, ModemManager itself sends this command to the modem when it refuses to switch the radio on. Nothing is written into the modem\'s memory, it can be switched off here. The qmi-utils package is needed. If the modem has already given up and is offline, reconnect it.')),
			E('div', { 'class': 'mm-row' }, this.fccButton)]);
		this.fccNode.hidden = true;
		return this.fccNode;
	},

	applyFcc: function(d) {
		var f = (d || {}).fcc_unlock;
		this.fccNode.hidden = !f;
		this.alerts.fcc = list((d || {}).action_needed).indexOf('fcc-unlock') >= 0;
		this.applyAlert();
		if (!f) return;
		this.fccOn = !!f.enabled;
		this.fccState.className = 'mm-callout mm-fcc-state' + (f.enabled ? ' is-ok' : f.foreign ? ' is-warn' : '');
		text(this.fccState, f.foreign ? _('ModemManager already has another FCC unlock handler for this modem. MedveModem does not touch it.') :
			f.enabled ? _('On: ModemManager sends the command if the modem refuses to switch the radio on.') :
			f.installed === false ? _('The handler is not installed: update the medvemodem package.') : _('Off.'));
		text(this.fccButton, f.enabled ? _('Switch off') : _('Switch the FCC unlock on'));
		this.fccButton.className = 'mm-btn mm-fcc-toggle' + (f.enabled ? '' : ' is-primary');
		this.fccButton.hidden = !!f.foreign || (!f.enabled && f.installed === false);
	},

	/* «Приостановить управление» (the owner, 2026-10-19): not only the interface down — no service touches the modem on USB until
	 * «Возобновить управление», across reboots: MedveModem starts nothing on it, ModemManager is kept off it */
	freezeCard: function() {
		var self = this;
		this.frzState = E('div', { 'class': 'mm-callout mm-freeze-state' });
		this.frzButton = E('button', { type: 'button', 'class': 'mm-btn mm-freeze-toggle', click: mm.busy(function() { return self.toggleFreeze(); }) }, '');
		this.frzNode = this.sec('freeze', _('Pausing the management'), [
			E('p', { 'class': 'mm-b' }, _('Nothing touches the modem until you resume the management: it is handy to work with it by hand. The internet through it goes off.')),
			E('p', { 'class': 'mm-x' }, _('The modem stays connected to USB, but no service touches it: MedveModem does not connect, read or recover it, ModemManager lets it go, the connection through it goes off. Handy to work with the modem by hand or to leave it alone without pulling it out.')),
			this.frzState,
			risk(_('switches the link through the modem off'), _('reversible by the “Resume management” button; it stays after a router restart too.')),
			E('p', { 'class': 'mm-faint mm-x' }, _('If the modem is set up in QModem, QModem\'s dialling for it stops too and comes back as it was when the management resumes.')),
			E('div', { 'class': 'mm-row' }, this.frzButton)]);
		this.frzNode.hidden = true;
		return this.frzNode;
	},

	applyFreeze: function(d) {
		var f = (d || {}).frozen, present = (d || {}).present === true;
		this.frzNode.hidden = !present && !f;
		this.frozen = !!f;
		this.frzState.className = 'mm-callout mm-freeze-state' + (f ? ' is-warn' : '');
		text(this.frzState, f ? _('The modem management is paused') + (words(f.who) ? ' (' + words(f.who) + ')' : '') +
			(typeof f.at === 'number' ? ' ' + new Date(f.at * 1000).toLocaleString(LANG) : '') + ': ' + _('services do not touch it.') : _('The management is not paused.'));
		text(this.frzButton, f ? _('Resume management') : _('Pause management'));
		this.frzButton.className = 'mm-btn mm-freeze-toggle' + (f ? ' is-primary' : '');
	},

	toggleFreeze: function() {
		var self = this, on = !this.frozen;
		if (on && !window.confirm(_('Pause the modem\'s management? The connection through it goes off, MedveModem and ModemManager stop touching it until you unfreeze it.')))
			return Promise.resolve();
		this.frzButton.disabled = true;
		return freezeModem(this.id, on, 'LuCI').then(function(r) {
			r = r || {};
			self.say(r.result === 'done' || r.result === 'unchanged' ? (on ? _('The modem management is paused.') + (r.qmodem_warning ?
				' ' + _('QModem\'s dialling is not stopped: QModem is busy, try again in a minute.') : '') :
				_('The modem management is resumed.') + (r.interface ? ' ' + _('The connection “%s” is being switched on.').format(r.interface) : '')) :
				_('It did not work: %s.').format(why(r.reason || r.result)));
			return self.update();
		}, function(e) {
			self.say(_('This version of the MedveModem service cannot freeze the modem (%s).').format(failText(e)));
		}).finally(function() { self.frzButton.disabled = false; });
	},

	/* the group's heading only while one of its cards concerns this modem */
	applySvc: function() {
		var any = [this.mswNode, this.stickNode, this.fccNode, this.usbc.node, this.frzNode].some(function(n) { return n && !n.hidden; });
		this.svcHead.hidden = this.svcIntro.hidden = !any;
	},

	toggleFcc: function() {
		var self = this, on = !this.fccOn;
		if (on && !window.confirm(_('Switch the FCC unlock on for this modem? ModemManager sends it when the modem refuses to switch the radio on.')))
			return Promise.resolve();
		this.fccButton.disabled = true;
		return fccUnlock(this.id, on).then(function(r) {
			r = r || {};
			self.say(r.result === 'enabled' ? _('The FCC unlock is on. If the modem is offline, reconnect it.') :
				r.result === 'disabled' ? _('The FCC unlock is off.') :
				_('It did not work: %s.').format(r.reason || r.result));
			return self.update();
		}, function(e) {
			self.say(_('This version of the MedveModem service cannot switch the FCC unlock on (%s).').format(failText(e)));
		}).finally(function() { self.fccButton.disabled = false; });
	},

	/* a stick still in its storage mode (F19, field 2026-10-15: an E171 stayed a CD-ROM): the switch on the operator's button */
	stickCard: function() {
		var self = this;
		this.stickSay = E('div', { 'class': 'mm-callout' });
		this.stickTool = E('p', { 'class': 'mm-faint' });
		this.stickButton = E('button', { type: 'button', 'class': 'mm-btn mm-stick-run', click: mm.busy(function() { return self.runModeswitch(); }) }, _('Switch to modem mode'));
		this.stickNode = this.sec('stick', _('Flash drive mode'), [this.stickSay, this.stickTool,
			risk(_('one command to the modem over USB'), _('the modem reconnects with another USB ID; nothing is written into its memory.')),
			E('p', { 'class': 'mm-faint' }, _('The switch is one command to the modem over USB, nothing is written into its memory. After it the modem reconnects as a modem, with another USB ID, and shows here anew. MedveModem does not send the AT^U2DIAG and AT^SETPORT commands that change the mode for good.')),
			E('div', { 'class': 'mm-row' }, this.stickButton)]);
		this.stickNode.hidden = true;
		return this.stickNode;
	},

	applyStick: function(d) {
		var msw = (d || {}).modeswitch, on = (d || {}).detection_status === 'modeswitch-pending' && !!msw;
		this.stickNode.hidden = !on;
		this.alerts.stick = on;
		if (!on) return;
		text(this.stickSay, _('The modem came up as a flash drive (a CD-ROM with drivers) and waits to be switched into modem mode') +
			(msw.target ? ': ' + _('after it, it becomes %s').format(msw.target) : '') + '.');
		text(this.stickTool, !msw.tool ? _('The usb-modeswitch package is not installed, without it the router does not switch such modems by itself. Install it by the button in “Drivers and services” above and press “Switch to modem mode”.') :
			msw.rule ? _('/etc/usb-mode.json has a rule for this modem, but it did not switch by itself. The switch can be started by hand.') :
			_('/etc/usb-mode.json has no rule for this ID. MedveModem sends the Huawei switch command (HuaweiNewMode) once.'));
		this.stickButton.disabled = !msw.tool;
	},

	runModeswitch: function() {
		var self = this;
		if (!window.confirm(_('Switch the modem from flash drive mode to modem mode? It reconnects over USB.')))
			return Promise.resolve();
		this.stickButton.disabled = true;
		return modeswitchRun(this.id).then(function(r) {
			r = r || {};
			self.say(r.result === 'started' ? _('The switch is started: the modem reconnects in a few seconds.') :
				_('The switch is not started: %s.').format(STICK_WHY[r.reason] || r.reason || r.result));
			return self.update();
		}, function(e) {
			self.say(_('This version of the MedveModem service cannot switch the mode (%s).').format(failText(e)));
		}).finally(function() { self.stickButton.disabled = false; });
	},

	/* the first emergency of the two, with the way to its card */
	applyAlert: function() {
		var a = this.alerts;
		this.alertTarget = a.msw ? this.mswNode : a.stick ? this.stickNode : a.pkg ? this.pkgNode : a.sim ? this.simNode :
			a.fcc ? this.fccNode : a.web ? this.webForm.node : a.foreign ? this.conn.node : null;
		text(this.alertText, a.msw ? _('A usb-modeswitch rule resets this modem\'s USB configuration at each connection: kernel crashes are possible.') :
			a.stick ? _('The modem is in flash drive mode and waits to be switched into modem mode.') : a.pkg ? a.pkg : a.sim ? a.sim :
			a.fcc ? _('The modem\'s radio is off: this model does so without an FCC unlock.') : a.web ? a.web :
			a.foreign ? _('The modem is driven by the interface “%s” set up outside MedveModem: MedveModem does not touch it until you hand it over.').format(a.foreign) :
			a.mmr ? _('ModemManager %s: the modem is not refreshed and not recovered.').format(a.mmr) : '');
		/* the ModemManager restart is on the settings page, for all its modems (the owner, 2026-10-19) */
		bannerShow(this.alertNote, !!(this.alertTarget || a.mmr));
	},

	/* why this owner and not another (audit 2026-10-13): the operator's choice, QModem's own section, or what «auto» found */
	applyOwnerWhy: function(st, d) {
		var own = (d.owner || {}).control, choice = (d.connection || {}).backend || 'auto', mmb = ((st || {}).modemmanager || {}).backend;
		var prefer = (d.profile || {}).prefer_owner, q = d.qmodem || {}, out = '';
		var MMB = { unavailable: ((st || {}).modemmanager || {}).installed === false ? _('ModemManager not installed') : _('ModemManager not running'), disabled: _('ModemManager is switched off in the settings'), timeout: _('ModemManager does not answer'),
			error: _('ModemManager answers with an error') };
		var CHOICE = { native: _('“MedveModem directly”'), modemmanager: '«ModemManager»' };
		if (own === 'qmodem' && CHOICE[choice])
			/* bench 2026-10-19: the choice saved while QModem still had the modem said nothing */
			out = _('%s is chosen, but QModem still has the modem').format(CHOICE[choice]) + (q.managed_by_engine ?
				': ' + _('“Save” or “Connect” takes it from QModem') :
				' — ' + _('its section %s is set up in QModem itself: delete the modem there, MedveModem does not touch others\' settings').format(q.section || ''));
		else if (own === 'qmodem')
			out = choice === 'qmodem' ? _('chosen by hand in “Connection setup”') : q.section && !q.managed_by_engine ?
				_('QModem has its own section for this modem (%s), set up outside MedveModem').format(q.section) : _('the model\'s profile hands it to QModem');
		else if (own === 'modemmanager')
			out = choice === 'modemmanager' ? _('chosen by hand in “Connection setup”') : _('automatically: ModemManager is installed and drives this modem') +
				(prefer === 'qmodem' ? _('; the profile prefers QModem, but it is not installed') : '');
		else if (/^netifd-|^medvemodem-at$/.test(own || ''))
			out = choice === 'native' ? _('chosen by hand in “Connection setup”') : (d.profile || {}).fallback_owner === 'native' ?
				_('automatically: QModem is not installed, and ModemManager does not bring the link up for this model: MedveModem connects it itself over AT') :
				_('automatically: %s').format(MMB[mmb] || _('ModemManager did not take the modem'));
		/* the owner, 2026-10-20 (E3372-325): a HiLink was «владелец ещё не выбран: модем не подключали» while connected */
		else if (own === 'hilink' || (d.owner || {}).session === 'device')
			out = _('automatically: the modem goes online by itself (router mode), the router gets an address from it by DHCP; it needs neither ModemManager nor the AT port');
		else if (own)
			out = _('no owner chosen yet: the modem was not connected');
		if ((d.owner || {}).conflict) out += (out ? '. ' : '') + _('Conflict: %s').format(why(d.owner.conflict));
		if (d.frozen) out = _('The modem management is paused: services do not touch it.') + ' ' + out;
		text(this.refs.owner_why, out);
	},

	/*
	 * The diagnosis by chain (alpha): device → SIM and network → session → internet → DNS, from the engine
	 * (the same in the TUI and the CLI). «Базовый» shows it only when a link is broken.
	 */
	applyChain: function(dg) {
		var h = this.hero, show = !!(dg && dg.links) && (!dg.ok || this.level !== 'normal'), w = dg && dg.warning;
		h.chain.hidden = h.chainSay.hidden = !show;
		if (!show) return;
		var MARK = { ok: '✓', fail: '✗', unknown: '?', skipped: '–', warn: '!' },
			SAY = { ok: _('all right'), fail: _('the problem is here'), unknown: _('not checked yet'), skipped: _('not checked'), warn: _('works, but with a remark') };
		h.chain.replaceChildren.apply(h.chain, dg.links.map(function(l) {
			return E('li', { 'class': 'mm-chain-' + l.state, title: (CHAIN_LINKS[l.link] || l.link) + ': ' + (SAY[l.state] || l.state) },
				[E('b', { 'aria-hidden': 'true' }, MARK[l.state] || '?'), ' ' + (CHAIN_LINKS[l.link] || l.link)]);
		}));
		text(h.chainSay, dg.ok && w ? _('The link works, but “%s”: %s').format(CHAIN_LINKS[w.link] || w.link, CHAIN_WHY[w.reason] || w.reason) +
			(w.detail ? ' (' + why(dnsDetail(w.detail)) + ')' : '') + '.' :
			dg.ok ? (dg.standby ? _('All links fine; the modem is on standby: the traffic goes through another channel.') : _('All links fine.')) :
			_('The first link with a problem is “%s”: %s').format(CHAIN_LINKS[dg.link] || dg.link, CHAIN_WHY[dg.reason] || dg.reason) +
			(dg.detail ? ' (' + why(dnsDetail(dg.detail)) + ')' : '') + '.' + this.webLinkSay(dg));
	},

	/* a HiLink's own mobile data link, when the internet fails behind its address (the owner, 2026-11-04: «SIM и сеть ✓,
	 * Интернет ✗» for 63 h on an E3372h, nothing said whether the stick itself was connected) */
	webLinkSay: function(dg) {
		var l = this.webLink;
		if (!l || dg.ok || dg.link !== 'internet' || l.state === 'connected') return '';
		var W = { connecting: _('it is connecting'), disconnected: _('it is disconnected (its data switch may be off, or the balance is out)'),
			disconnecting: _('it is disconnecting'), no_autoconnect: _('its automatic connection is off: switch it on in the modem\'s web'),
			roaming_off: _('it is in roaming and its connection in roaming is off'), failed: _('the network refused the connection (code %s)').format(l.code) };
		return ' ' + _('The modem itself says of its mobile data: %s.').format(W[l.state] || l.state);
	},

	goAlert: function() {
		if (!this.alertTarget && this.alerts.mmr) { location.href = L.url('admin/modem/medvemodem/settings') + '#mmr'; return; }
		this.goTo(this.alertTarget);
	},

	/* open the tab a card sits in, open the card and bring it into view */
	goTo: function(node) {
		var panel = node && node.closest('[role="tabpanel"]');
		if (!node) return;
		this.show(panel ? panel.id.replace(/^mm-tab-/, '') : 'service');
		if (node.mmOpen) node.mmOpen(true);
		if (node.scrollIntoView) node.scrollIntoView({ block: 'start', behavior: 'smooth' });
	},

	fixModeswitch: function() {
		var self = this;
		if (!window.confirm(_('Switch the usb-modeswitch rule off for this modem? The original file is kept as /etc/usb-mode.json.medvemodem-backup.')))
			return Promise.resolve();
		this.mswButton.disabled = true;
		return modeswitchFix(this.id).then(function(r) {
			r = r || {};
			self.say(r.result === 'fixed' ? _('The rule %s is switched off.').format(r.rule) + (r.replug ? ' ' + _('The modem has no configuration: reconnect it or restart the router.') : '') :
				r.result === 'not-needed' ? _('The rule is already switched off.') : _('The rule is not switched off: %s.').format(r.reason || r.result));
			return self.update();
		}, function(e) {
			self.say(_('This version of the MedveModem service cannot switch the rule off (%s).').format(failText(e)));
		}).finally(function() { self.mswButton.disabled = false; });
	},

	/* what the router lacks for this modem (the owner, 2026-10-17: «наличие или отсутствие ModemManager и нужных пакетов
	 * подобно QModem надо уметь показывать и доставлять»): kernel drivers and ModemManager, from the router's feeds */
	pkgCard: function() {
		var self = this;
		this.pkgCall = E('div', { 'class': 'mm-callout is-warn mm-pkg-call' });
		this.pkgOut = E('p', { 'class': 'mm-faint mm-pkg-result', hidden: '' });
		this.pkgPre = E('pre', { 'class': 'mm-pre mm-pkg-text' }, '');
		this.pkgLog = E('details', { 'class': 'mm-pkg-log', hidden: '' }, [E('summary', {}, _('Installation log')), this.pkgPre]);
		this.pkgButton = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-pkg-install', click: mm.busy(function() { return self.installPkgs(); }) }, _('Install'));
		/* the same over SSH, for a router the button cannot help (no internet yet, a firmware's own feeds): the owner 2026-11-02 */
		this.pkgCmd = E('p', { 'class': 'mm-faint mm-pkg-cmd', hidden: '' });
		this.pkgNode = this.sec('pkg', _('Drivers and services'), [this.pkgCall, E('div', { 'class': 'mm-row' }, this.pkgButton), this.pkgCmd, this.pkgOut, this.pkgLog,
			E('p', { 'class': 'mm-faint' }, _('Packages are installed from the router\'s feeds. First a dry run: if something is not in the feeds, nothing is installed. A driver is bound at once, without a restart. A ModemManager already working is not restarted. All at once: in “Settings”, the “Components” section.'))]);
		this.pkgNode.hidden = true;
		this.pkgWant = [];
		return this.pkgNode;
	},

	/* a HiLink or ZTE modem's own web (HANDOFF 164): the signal is read there; its password when the default does not fit */
	webCard: function() {
		var self = this, f = this.webForm = {};
		f.say = E('div', { 'class': 'mm-callout mm-web-say' });
		f.pw = E('input', { type: 'password', 'class': 'mm-input mm-web-pw', id: 'mm-web-pw', autocomplete: 'new-password', maxlength: '64',
			placeholder: _('The modem\'s web interface password') });
		f.save = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-web-save', click: mm.busy(function() { return self.saveWebPw(f.pw.value); }) },
			_('Save and check'));
		f.ask = E('div', { 'class': 'mm-web-ask', hidden: '' }, [
			E('div', { 'class': 'mm-row' }, [f.pw, f.save]),
			E('p', { 'class': 'mm-faint' }, _('It is usually on the modem\'s sticker (often “admin”). The password is kept on the router only and never shown. A wrong password is checked at most once in 5 minutes, so the modem does not block logins.'))]);
		f.forget = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-web-forget', hidden: '', click: mm.busy(function() { return self.saveWebPw(null); }) },
			_('Forget the password'));
		/* the modem's own web, opened from here (UFI stick, bench 2026-10-19: admin/admin at 192.168.100.1) */
		f.open = E('a', { 'class': 'mm-btn is-quiet mm-web-open', target: '_blank', rel: 'noopener noreferrer', hidden: '' }, _('Open the modem\'s web interface'));
		/* through the router the forward opens on asking, for a while (audit 2026-10-21): asked first, the page after */
		f.open.addEventListener('click', function(ev) {
			var port = ((self.data || {}).connection || {}).web_port, href = f.open.getAttribute('href');
			if (!port || !href || href.indexOf(':' + port + '/') < 0) return;
			ev.preventDefault();
			var win = window.open('about:blank', '_blank');
			webOpen(self.id).then(function() {
				setTimeout(function() { if (win) win.location = href; else location.href = href; }, 500);
			}, function(e) {
				if (win) win.close();
				mm.toast('web', _('The web interface forward did not open: %s').format(failText(e)), 'err', 8000);
			});
		});
		/* r117: a HiLink's own data from its web, read when asked (the owner, 2026-10-28: «там вон сколько полезного») */
		f.data = E('dl', { 'class': 'mm-kv mm-web-data', hidden: '' });
		f.read = E('button', { type: 'button', 'class': 'mm-btn mm-web-read', hidden: '', click: mm.busy(function() { return self.readWebInfo(); }) },
			_('Read the modem\'s data'));
		/* a HiLink's network mode and LTE bands through its web, and its counters cleared (the E3372-325 HAR, 2026-10-30) */
		f.mode = E('select', { id: 'mm-web-mode', 'class': 'mm-web-mode' });
		f.lte = E('select', { id: 'mm-web-lte', 'class': 'mm-web-lte' });
		f.apply = E('button', { type: 'button', 'class': 'mm-btn mm-web-apply', click: mm.busy(function() { return self.setWebMode(); }) }, _('Apply'));
		f.modeRow = E('div', { 'class': 'mm-row mm-web-netmode', hidden: '' }, [
			E('label', { 'for': 'mm-web-mode' }, _('Network mode')), f.mode, E('label', { 'for': 'mm-web-lte' }, _('LTE bands')), f.lte, f.apply]);
		f.clear = E('button', { type: 'button', 'class': 'mm-btn mm-web-clear', hidden: '', click: mm.busy(function() { return self.clearWebTraffic(); }) },
			_('Clear the modem\'s traffic counters'));
		f.node = this.sec('web', _('The modem\'s web interface'), [f.say, f.ask, E('div', { 'class': 'mm-row' }, [f.open, f.forget]),
			f.data, f.modeRow, E('div', { 'class': 'mm-row' }, [f.read, f.clear])]);
		f.node.hidden = true;
		return f.node;
	},

	applyWeb: function(r) {
		var f = this.webForm, w = (r || {}).web || {}, mins = function(s) { return Math.max(1, Math.ceil((s || 0) / 60)); };
		if (!f) return;
		var host = w.host ? ' (' + w.host + ')' : '', say, tone = 'is-info', ask = false;
		this.webLink = w.data_link && typeof w.data_link === 'object' ? w.data_link : null;
		if (r.metrics) say = _('The signal is read from the modem\'s web page') + host + (w.auth === 'ok' ? ', ' + _('logged in.') : '.');
		else if (w.auth === 'needed') { say = _('The modem\'s web interface asks for a password, the factory “admin” did not fit. Enter the modem\'s password.'); tone = 'is-warn'; ask = true; }
		else if (w.auth === 'wrong') { say = _('The saved password did not fit the modem\'s web interface. Enter the right one.'); tone = 'is-warn'; ask = true; }
		else if (w.auth === 'locked') { say = _('The modem has blocked logins for a while after wrong passwords. The next try is in about %d min.').format(mins(w.wait_s)); tone = 'is-warn'; }
		else if (w.auth === 'busy') { say = _('Someone else is logged into the modem\'s web interface now (from a computer, for example). MedveModem does not log in, not to throw them out: log out of the modem\'s web interface.'); tone = 'is-warn'; }
		else if (w.auth === 'waiting') { say = _('The password is checked at most once in 5 minutes. The next try is in about %d min.').format(mins(w.wait_s)); tone = 'is-warn'; ask = !w.password_saved; }
		else if (w.unknown_kind) say = _('The modem\'s web interface%s answers, but MedveModem does not read its format yet: see the signal, the operator and SMS in it. The login and the password are usually on the modem\'s sticker (often admin / admin).').format(host);
		else say = _('The modem\'s web page%s does not answer: the signal is not read.').format(host);
		/* through the router's own address when the engine forwards it (the owner, 2026-10-20: a PC with a proxy client in
		 * TUN mode sends 192.168.8.1 into its tunnel); the modem's address said beside it */
		var port = (((this.data || {}).connection) || {}).web_port, via = port && location.hostname;
		if (via || (w.host && /^[0-9.]+$/.test(w.host))) {
			/* a HiLink's «/» redirects to its own address (http://192.168.8.1/html/index.html?origin=…, the owner,
			 * 2026-10-20): through the router its home page is asked by path, the links inside are relative */
			var home = w.kind === 'huawei' ? 'html/home.html' : '';
			f.open.setAttribute('href', via ? 'http://' + (location.hostname.indexOf(':') >= 0 ? '[' + location.hostname + ']' : location.hostname) + ':' + port + '/' + home :
				'http://' + w.host + '/' + home);
			f.open.title = via ? _('Through the router, port %s').format(port) + (w.host ? '; ' + _('the modem\'s address is %s').format(w.host) : '') : '';
			f.open.hidden = false;
		}
		/* two web modems at one address: the router reaches only one of them there (second audit 2026-10-21, R03) */
		if (via && ((this.data || {}).connection || {}).web_shared_host)
			say += ' ' + _('Attention: another modem has the same address%s. The router sees only one of them at this address, so the link may open the other modem\'s page. Change the address of one modem in its web interface (for example to 192.168.9.1).').format(host);
		f.node.hidden = false;
		f.say.className = 'mm-callout mm-web-say ' + tone;
		text(f.say, say);
		f.ask.hidden = !ask;
		f.forget.hidden = !w.password_saved;
		/* a HiLink answers its own API: its data by a button; its restart is the ladder's «Reboot the modem» (the owner,
		 * 2026-10-30: one button for it, in the recovery table) */
		f.read.hidden = w.kind !== 'huawei' && w.kind !== 'mark' && w.kind !== 'uz' && w.kind !== 'zte';	/* an MF150-4's and a UZ801's web too (2026-10-30), a ZTE goform's (11-03) */
		this.webKind = w.kind;
		/* said above the tabs: the card sits in «Настройки» */
		this.alerts.web = ask ? _('The modem\'s web interface asks for a password: without it the signal is not read.') : '';
		this.applyAlert();
	},

	/* a router-mode modem (HiLink, UFI) whose page cannot be reached: said why, not a card that stays away (the owner,
	 * 2026-10-28: a UFI moved to another port took its pause along, so its link never came up and no link was offered) */
	webIdle: function(d) {
		var f = this.webForm, conn = d.connection || {};
		if (!f || ((d.profile || {}).prefer_owner) !== 'hilink' || /^phone-/.test((d.profile || {}).id || '') || conn.web_port) return;
		var say = d.frozen ? _('The modem management is paused: MedveModem does not bring its connection up, so the router has no address of the modem and its web interface cannot be opened. Resume it in the «Pausing the management» card.') :
			d.present === true && ((d.session || {}).state !== 'connected') ? _('The connection through the modem is not up: the router has no address of the modem yet, the link to its web interface appears once it is.') : '';
		if (!say || f.open.getAttribute('href')) return;
		f.node.hidden = false;
		f.say.className = 'mm-callout mm-web-say is-warn';
		text(f.say, say);
	},

	/* the operator's address, the traffic of the session, the month and all time, the versions: one table */
	readWebInfo: function() {
		var self = this, f = this.webForm;
		return webInfo(this.id).then(function(r) {
			if (r.refused || r.error) { self.say(_('The modem\'s web did not give its data: %s').format(words(r.refused || r.error)), true); return; }
			var t = r.traffic || {}, d = r.device || {}, rows = [];
			var way = function(x) { return x && (x.rx != null || x.tx != null) ? '↓ ' + bytes(x.rx || 0) + ' · ↑ ' + bytes(x.tx || 0) +
				(x.s != null ? ' · ' + duration(x.s) : '') : null; };
			var add = function(k, v) { if (v) rows.push(E('dt', {}, k), E('dd', { 'class': 'mm-mono' }, v)); };
			add(_('Address at the operator'), r.wan_ip);
			add(_('IPv6 address at the operator'), r.wan_ipv6);
			add(_('This session'), way(t.session));
			add(_('Today'), t.day && t.day.bytes != null ? bytes(t.day.bytes) + (t.day.s != null ? ' · ' + duration(t.day.s) : '') : null);
			add(_('This month'), way(t.month));
			var pl = t.plan || {};
			add(_('Billing month'), pl.start_day ? _('from day %d').format(pl.start_day) + (pl.limit ? ' · ' + _('limit %s').format(pl.limit) +
				(pl.limit_cuts_data ? ' · ' + _('the data stops at the limit') : '') : '') : null);
			add(_('All time'), way(t.total));
			var lk = r.link || {}, ln = [];
			if (lk.rrc) ln.push(lk.rrc === 'connected' ? _('radio link active') : _('radio link idle'));
			if (lk.tx_power_dbm != null) ln.push(_('transmit power %d dBm').format(lk.tx_power_dbm));
			if (lk.bandwidth_mhz != null || lk.ul_bandwidth_mhz != null)
				ln.push(_('channel ↓ %s · ↑ %s MHz').format(lk.bandwidth_mhz != null ? lk.bandwidth_mhz : '—', lk.ul_bandwidth_mhz != null ? lk.ul_bandwidth_mhz : '—'));
			if (lk.transmission_mode) ln.push(lk.transmission_mode);
			add(_('Radio link'), ln.join(' · '));
			add(_('Firmware'), d.firmware);
			add(_('Hardware version'), d.hardware);
			add(_('Web interface version'), d.webui);
			f.data.replaceChildren.apply(f.data, rows);
			f.data.hidden = !rows.length;
			self.applyWebMode(r.netmode);
			f.clear.hidden = self.webKind !== 'huawei';
		}, function(e) { self.say(_('The modem\'s web did not give its data: %s').format(failText(e)), true); });
	},

	/* the modes and LTE bands the modem's own list allows, the current ones chosen */
	applyWebMode: function(nm) {
		var f = this.webForm, names = { '00': _('Automatic'), '01': _('2G only'), '02': _('3G only'), '03': _('4G only'),
			'0302': _('4G, then 3G'), '0301': _('4G, then 2G'), '0201': _('3G, then 2G') };
		f.modeRow.hidden = !nm || !Array.isArray(nm.modes) || !nm.modes.length;
		if (f.modeRow.hidden) return;
		/* the preferred orders a modded web offers (the owner, 2026-11-01: «LTE→UMTS», «LTE→GSM», «UMTS→GSM»), where the
		 * modem's own list has both technologies; and the one set now, whatever it is */
		var modes = nm.modes.slice();
		[['0302', '03', '02'], ['0301', '03', '01'], ['0201', '02', '01']].forEach(function(c) {
			if (modes.indexOf(c[0]) < 0 && modes.indexOf(c[1]) >= 0 && modes.indexOf(c[2]) >= 0) modes.push(c[0]);
		});
		if (nm.mode && modes.indexOf(nm.mode) < 0) modes.push(nm.mode);
		f.mode.replaceChildren.apply(f.mode, modes.map(function(m) { return E('option', { value: m }, names[m] || m); }));
		f.mode.value = nm.mode || nm.modes[0];
		var bands = (nm.lte_bands || []).slice(), cur = String(nm.lte_band || '').toLowerCase();
		if (cur && !bands.some(function(b) { return String(b.value).toLowerCase() === cur; }))
			bands.unshift({ name: _('as set now (%s)').format(nm.lte_band), value: cur });
		f.lte.replaceChildren.apply(f.lte, bands.map(function(b) { return E('option', { value: String(b.value).toLowerCase() }, b.name); }));
		f.lte.value = cur || (bands[0] || {}).value || '';
		f.lte.disabled = !bands.length;
	},

	setWebMode: function() {
		var self = this, f = this.webForm;
		return webDo(this.id, 'netmode', f.mode.value, f.lte.value || '').then(function(r) {
			if (r.refused || r.error || !r.ok) { self.say(_('The modem\'s web did not take the mode: %s').format(words(r.refused || r.error || '')), true); return; }
			self.say(_('The network mode is set in the modem; it may re-register for a few seconds.'));
		}, function(e) { self.say(_('The modem\'s web did not take the mode: %s').format(failText(e)), true); });
	},

	clearWebTraffic: function() {
		var self = this;
		if (!confirm(_('Clear the modem\'s own traffic counters (the month and all time)?'))) return Promise.resolve();
		return webDo(this.id, 'clear-traffic').then(function(r) {
			if (r.refused || r.error || !r.ok) { self.say(_('The modem\'s web did not clear the counters: %s').format(words(r.refused || r.error || '')), true); return; }
			self.say(_('The modem\'s traffic counters are cleared.'));
			return self.readWebInfo();
		}, function(e) { self.say(_('The modem\'s web did not clear the counters: %s').format(failText(e)), true); });
	},

	/* the password in the device section (option web_password), or taken out (null); read again at once */
	saveWebPw: function(pw) {
		var self = this, id = this.id;
		if (pw !== null && !pw) { this.say(_('Enter the modem\'s web interface password.')); return Promise.resolve(); }
		return uciGet('medvemodem', 'device').then(function(r) {
			var secs = (r && r.values) || {}, name = Object.keys(secs).filter(function(n) { return secs[n].id === id; })[0];
			if (pw) return name ? uciSet('medvemodem', name, { web_password: pw }) : uciAdd('medvemodem', 'device', { id: id, web_password: pw });
			return name ? uciDelOption('medvemodem', name, 'web_password') : null;
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			self.webForm.pw.value = '';
			return getMetrics(id, 0);
		}).then(function(r) {
			if (r && r.web) self.applyWeb(r);
			if (r && r.metrics) {
				self.atMm = atAsMm(r.metrics);
				self.atMmAt = { id: self.id, gen: (self.data || {}).generation, at: Date.now() };
				if (self.atMm && r.source === 'web') self.atMm.via_web = true;
				self.saveAtMm();
				if (self.data) self.hero_(self.data, self.data.present === true && (self.data.session || {}).state === 'connected', self.heroModel || '');
			}
			self.say(pw ? (r && r.metrics ? _('The password fits, the signal is read.') : _('The password is saved.')) : _('The modem\'s web interface password is deleted from the router.'));
		}).catch(function(e) { self.say((pw ? _('The password is not saved: %s.') : _('The password is not deleted: %s.')).format(failText(e))); });
	},

	applyPkg: function(st, d) {
		var mms = (st || {}).modemmanager || {}, prefer = ((d || {}).profile || {}).prefer_owner;
		var drivers = list((d || {}).drivers_missing).filter(function(m) { return m && typeof m.driver === 'string'; });
		var needMM = mms.installed === false && mms.backend !== 'disabled' && (!prefer || prefer === 'modemmanager');
		var needSwitch = (d || {}).detection_status === 'modeswitch-pending' && !!(d || {}).modeswitch && !d.modeswitch.tool;
		/* a direct session whose netifd protocol handler is missing (the engine's session.proto_missing) */
		var protoPkg = { mbim: 'umbim', qmi: 'uqmi', ncm: 'comgt-ncm' }[((d || {}).session || {}).proto_missing] || null;
		var names = drivers.map(function(m) { return m.package || m.driver; }).concat(needSwitch ? ['usb-modeswitch'] : [], needMM ? ['ModemManager'] : [],
			protoPkg ? [protoPkg] : []);
		this.pkgWant = drivers.map(function(m) { return m.driver; }).concat(needSwitch ? ['usb-modeswitch'] : [], needMM ? ['modemmanager'] : [],
			protoPkg ? [protoPkg] : []);
		this.pkgNode.hidden = !this.pkgWant.length && !this.pkgRunning && !this.pkgShown;
		var drvList = drivers.map(function(m) { var n = list(m.interfaces); return (m.package || m.driver) + (n.length ? ' (' + N_(n.length, 'interface: %s', 'interfaces: %s').format(n.join(', ')) + ')' : ''); }).join('; ');
		text(this.pkgCall, [drivers.length ? (drivers.length > 1 ? _('The modem lacks drivers: %s. Without them the modem has no ports for the link and commands, recovery steps will not help.') :
			_('The modem lacks a driver: %s. Without it the modem has no ports for the link and commands, recovery steps will not help.')).format(drvList) : '',
			needSwitch ? _('The usb-modeswitch package is not installed: without it the modem cannot be switched out of flash drive mode.') : '',
			needMM ? _('ModemManager is not installed, and it manages this modem by its profile.') : '',
			protoPkg ? _('The %s package is not installed: netifd cannot start this connection protocol. After it, restart the router once.').format(protoPkg) : ''].filter(Boolean).join(' '));
		this.applyPkgCmd();
		this.pkgCall.hidden = !this.pkgWant.length;
		this.pkgButton.hidden = !this.pkgWant.length;
		text(this.pkgButton, _('Install %s').format(names.join(', ')));
		this.alerts.pkg = drivers.length ? _('The modem lacks a driver (%s): there are no ports for the link.').format(drivers.map(function(m) { return m.package || m.driver; }).join(', ')) :
			needMM ? _('ModemManager is not installed: the modem does not connect.') : '';
		this.applyAlert();
	},

	/* the packages behind the wanted components, as the installer names them, in a command for the router's package manager */
	applyPkgCmd: function() {
		var self = this, want = this.pkgWant || [];
		if (!want.length) { this.pkgCmd.hidden = true; return; }
		/* asked again when the wanted set changes: the installer's answer names the packages and the package manager */
		if (this.pkgAskedFor !== want.join(' ')) {
			this.pkgAskedFor = want.join(' ');
			this.pkgInfo = null;
			fs.exec(PKG, ['status']).then(function(r) {
				try { self.pkgInfo = JSON.parse((r && r.stdout) || '{}'); } catch (e) { self.pkgInfo = {}; }
				self.applyPkgCmd();
			}, function() { self.pkgInfo = {}; });
			return;
		}
		if (!this.pkgInfo) return;
		var comps = {};
		list(this.pkgInfo.components).forEach(function(c) { if (c && c.id) comps[c.id] = c.packages; });
		var pkgs = want.map(function(id) { return comps[id] || id; }).join(' ');
		var cmd = this.pkgInfo.pm === 'opkg' ? 'opkg update && opkg install ' + pkgs : this.pkgInfo.pm === 'apk' ? 'apk update && apk add ' + pkgs : '';
		this.pkgCmd.hidden = !cmd;
		this.pkgCmd.replaceChildren(_('Or over SSH:') + ' ', E('code', { 'class': 'mm-mono' }, cmd));
	},

	installPkgs: function() {
		var self = this, want = this.pkgWant.slice();
		if (!want.length) return Promise.resolve();
		this.say(_('Installing from the router\'s feeds: %s…').format(want.join(', ')));
		return fs.exec(PKG, ['start', mm.langArg()].concat(want)).then(function(r) {
			try { return JSON.parse((r && r.stdout) || '{}'); } catch (e) { return {}; }
		}, function(e) { return { error: failText(e) }; }).then(function(j) {
			if (!j.ok) { self.say(_('The installation is not started: %s.').format(PKG_WHY[j.error] || j.error || _('no answer'))); return; }
			self.pkgShown = true;
			self.pkgLog.hidden = false; self.pkgLog.open = true;
			return self.pkgRefresh();
		});
	},

	/* the run's state and log every 2 s while it goes on; the page reads the modem again when it ends */
	pkgRefresh: function() {
		var self = this;
		clearTimeout(this.pkgTimer);
		return fs.exec(PKG, ['status']).then(function(r) {
			try { return JSON.parse((r && r.stdout) || '{}'); } catch (e) { return {}; }
		}, function() { return {}; }).then(function(j) {
			var running = j.state === 'running', was = self.pkgRunning;
			self.pkgRunning = running;
			self.pkgButton.disabled = running;
			self.pkgOut.hidden = false;
			text(self.pkgOut, !j.ok ? _('The installation script does not answer.') : running ? _('Installing, the log is below.') :
				j.state === 'failed' ? _('Not installed: %s. Details in the log.').format(PKG_WHY[j.result] || j.result || _('error')) :
				j.state === 'done' ? _('Installed. The modem gets the driver by itself, the page refreshes.') : '');
			return (j.log ? fs.read(j.log).then(function(t) { self.pkgPre.textContent = t || ''; self.pkgPre.scrollTop = self.pkgPre.scrollHeight; }, function() {}) :
				Promise.resolve()).then(function() {
				if (running) self.pkgTimer = setTimeout(function() { self.pkgRefresh(); }, 2000);
				else if (was) self.update();
				return j;
			});
		});
	},

	/* ModemManager in trouble is said above the tabs where it hurts this modem: ModemManager drives it, or nobody does yet;
	 * the restart itself is on the settings page */
	applyMMR: function(st, d) {
		var mm = (st || {}).modemmanager || {}, owner = ((d || {}).owner || {}).control;
		var trouble = { timeout: _('does not answer the requests'), error: _('answers with an error'), unavailable: _('is not running') }[mm.backend];
		/* not installed is no emergency to restart: «Драйверы и службы» installs it (field 2026-10-17: «не запущен», status 127) */
		if (mm.installed === false) trouble = '';
		this.alerts.mmr = trouble && (owner === 'modemmanager' || !owner) ? trouble : '';
		this.applyAlert();
	},

	stage: function(level) {
		var self = this, row = this.stageRows[level.name];
		if (row) return row;
		var names = STAGES[level.name] || [level.name, '', '', true], tag = STAGE_TAG[level.name] || ['soft', level.name];
		row = { status: E('span', { 'class': 'mm-chip mm-stage-status' }) };
		row.button = E('button', { type: 'button', 'class': 'mm-btn mm-run', click: mm.busy(function() { return self.run(level.name); }) }, _('Run'));
		/* «Базовый»: reconnect the internet and restart the modem; the rest of the ladder at «Подробный» (the owner, 2026-10-26) */
		row.node = E('div', { 'class': 'mm-rung mm-stage' + (['session', 'soft-reset'].indexOf(level.name) < 0 ? ' mm-x' : '') }, [
			E('span', { 'class': 'mm-rung-l is-' + tag[0] }, tag[1]),
			E('div', {}, [E('div', { 'class': 'mm-rung-t' }, names[0]), E('div', { 'class': 'mm-rung-d mm-stage-what' }, names[1]),
				E('div', { 'class': 'mm-rung-meta' }, [E('span', { 'class': 'mm-chip mm-weight' }, names[2]), row.status])]),
			row.button]);
		this.stages.appendChild(row.node);
		this.stageRows[level.name] = row;
		return row;
	},

	run: function(name) {
		var self = this, names = STAGES[name] || [name, '', '', true];
		if (names[3] && !window.confirm(_('“%s”: the link through this modem breaks (%s). Continue?').format(names[0], names[2])))
			return Promise.resolve();
		this.say(_('“%s”: starting…').format(names[0]));
		return recover(this.id, name, false, 'luci').then(function(r) {
			r = r || {};
			self.say(_('“%s”: %s').format(names[0], RESULTS[r.result] || r.result || _('no answer')) +
				(r.result !== 'started' && r.reason ? ' — ' + why(r.reason) : ''));
			return self.update();
		}, function(e) {
			self.say(_('“%s”: %s').format(names[0], _('the MedveModem service does not answer (%s)').format(failText(e))));
		});
	},

	/* «Восстановить по порядку»: the supported steps of the ladder one after another, each after the last has ended,
	 * until the connection check passes; one confirmation for the whole chain */
	/* «Перерегистрация в сети»: asked of the engine; true when it took it. ask: confirm first (the button alone) */
	reregisterNow: function(ask) {
		var self = this;
		if (ask && !window.confirm(_('The modem leaves the network and registers again: the internet through it stops for a few seconds. Continue?')))
			return Promise.resolve(false);
		this.say(_('Re-registering in the network…'));
		return reregister(this.id).then(function(r) {
			r = r || {};
			if (r.refused || r.ok === false) {
				self.say(_('Re-register: %s').format(r.refused || r.error || _('not done')));
				return false;
			}
			self.say(_('The modem registered again; the connection check follows.'));
			window.setTimeout(function() { self.update(); }, 3000);
			return true;
		}, function(e) {
			self.say(_('Re-register: %s').format(_('the MedveModem service does not answer (%s)').format(failText(e))));
			return false;
		});
	},

	runChain: function() {
		var self = this, rec = (this.data || {}).recovery || {};
		var names = list(rec.levels).filter(function(l) { return l && l.supported; }).map(function(l) { return l.name; });
		if (!names.length) { this.say(_('No recovery step is available for this modem.')); return Promise.resolve(); }
		if (!window.confirm(_('The steps run in order until the internet answers: %s. The link through this modem breaks for each step. Continue?')
			.format([_('“%s”').format(_('Re-register in the network'))].concat(names.map(function(n) { return _('“%s”').format((STAGES[n] || [n])[0]); })).join(', '))))
			return Promise.resolve();
		var ok = function() { var u = (self.data || {}).uplink || {}; return !!(u.reach && u.reach.ok); };
		var pause = function(ms) { return new Promise(function(r) { window.setTimeout(r, ms); }); };
		/* the step has ended: the engine says running, then not; at most 6 minutes a step */
		var settle = function(started) {
			return pause(3000).then(function() { return self.update(); }).then(function() {
				var running = ((self.data || {}).recovery || {}).state === 'running';
				if (running || Date.now() - started < 9000) return Date.now() - started < 360000 ? settle(started) : null;
				/* the connection check runs again after a step: a moment for its answer */
				return pause(8000).then(function() { return self.update(); });
			});
		};
		var step = function(i) {
			if (i >= names.length) { self.say(_('All the steps ran, the internet has not come back. A check by hand is needed: the SIM, the balance, the antenna.')); return; }
			var name = names[i], label = (STAGES[name] || [name])[0];
			self.say(_('Step %d of %d: “%s”…').format(i + 1, names.length, label));
			return recover(self.id, name, false, 'luci').then(function(r) {
				r = r || {};
				if (r.result !== 'started') {
					self.say(_('“%s”: %s').format(label, RESULTS[r.result] || r.result || _('no answer')) + (r.reason ? ' — ' + why(r.reason) : ''));
					return pause(1500).then(function() { return step(i + 1); });
				}
				return settle(Date.now()).then(function() {
					if (ok()) { self.say(_('The internet is back after “%s”.').format(label)); return; }
					return step(i + 1);
				});
			}, function(e) {
				self.say(_('“%s”: %s').format(label, _('the MedveModem service does not answer (%s)').format(failText(e))));
			});
		};
		/* the gentlest first (the owner, 2026-11-06): a new registration, then the ladder's steps */
		return this.reregisterNow(false).then(function(took) {
			if (!took) return step(0);
			return pause(15000).then(function() { return self.update(); }).then(function() {
				if (ok()) { self.say(_('The internet is back after “%s”.').format(_('Re-register in the network'))); return; }
				return step(0);
			});
		});
	},

	/* «Проверить связь»: the engine checks now; the answer arrives with the next poll */
	checkNow: function() {
		var self = this;
		this.say(_('Checking the connection…'));
		return check(this.id).then(function(r) {
			var res = (r || {}).result;
			self.say(res === 'started' ? _('The connection check is started; the result shows in a few seconds.') :
				res === 'running' ? _('The connection check is already running.') : res === 'not-connected' ? _('The connection is not up: no check possible.') :
				_('Connection check: %s').format(res || _('no answer')));
			window.setTimeout(function() { self.update(); }, 3000);
		}, function() {
			self.say(_('This version of the MedveModem service does not check on request; the check runs automatically.'));
		});
	},

	/* a message is an operator's action or its outcome: the next refresh asks the journal and the status too */
	say: function(line) { text(this.message, line); this.message.hidden = !line; if (line) this.askAgain = true; },

	/* Changing the controller is two distinct events: the UCI choice is saved,
	 * and the live modem actually changes hands. Show both, never call a queued
	 * bring-up a successful hand-over. All stages persist beside Save and in the
	 * shared bottom toast; the top action message stays visible too. */
	ownerStage: function(line, tone) {
		this.say(line);
		if (this.conn && this.conn.ownerStatus) {
			text(this.conn.ownerStatus, line);
			this.conn.ownerStatus.hidden = !line;
			this.conn.ownerStatus.className = 'mm-callout mm-owner-switch-status ' +
				(tone === 'err' ? 'is-err' : tone === 'warn' ? 'is-warn' : 'is-info');
		}
		mm.toast('backend-' + this.id, line, tone || 'run', tone === 'err' ? 15000 : tone === 'ok' ? 10000 : 0);
	},

	ownerChoiceChanged: function() {
		var c = this.conn || {}, previous = (this.connInfo || {}).backend || 'auto';
		if (!c.backend) return;
		if (c.backend.value !== previous)
			this.ownerStage(_('Managing service selected: %s. Press “Save and connect” to apply; the modem connection may be interrupted.')
				.format(c.backend.options[c.backend.selectedIndex].textContent), 'warn');
	},

	/* Confirm the live owner, not only an RPC result of `started`. The daemon's
	 * owner.control names native QMI as netifd-qmi, never simply native. */
	followOwnerSwitch: function(d) {
		var change = this.ownerSwitch;
		if (!change || !d || d.device_id !== this.id) return;
		var cfg = d.connection || {}, owner = d.owner || {}, ctl = owner.control || '',
			actual = d.backend || '', state = (d.session || {}).state || '',
			selected = cfg.backend || 'auto', elapsed = Date.now() - change.at;
		var expected = change.backend === 'native' ? actual === 'native' &&
			['netifd-qmi', 'netifd-mbim', 'medvemodem-at'].indexOf(ctl) >= 0 :
			change.backend === 'modemmanager' ? ctl === 'modemmanager' :
			change.backend === 'qmodem' ? ctl === 'qmodem' : selected === 'auto';
		if (selected !== change.backend) {
			if (elapsed >= 15000) {
				this.ownerStage(_('The saved managing service could not be confirmed; check that configuration changes are committed.'), 'err');
				this.ownerSwitch = null;
			}
			return;
		}
		if (owner.conflict) {
			this.ownerStage(_('Managing service conflict: %s').format(why(owner.conflict)), 'err');
			this.ownerSwitch = null;
			return;
		}
		if (expected && !change.owned) {
			change.owned = true;
			change.since = Date.now();
			this.ownerStage(_('Modem management transferred to %s; checking the connection…').format(
				change.backend === 'native' ? (ctl === 'netifd-qmi' ? 'MedveModem / QMI' : ctl === 'netifd-mbim' ? 'MedveModem / MBIM' : 'MedveModem / AT') :
				change.backend === 'modemmanager' ? 'ModemManager' : change.backend === 'qmodem' ? 'QModem' : _('automatic selection')), 'run');
		}
		if (expected && state === 'connected') {
			this.ownerStage(_('The managing service was changed and the modem is connected.'), 'ok');
			this.ownerSwitch = null;
		} else if (expected && Date.now() - (change.since || change.at) >= 120000) {
			this.ownerStage(_('The managing service is selected, but the modem has not connected: %s').format(
				why((d.session || {}).reason) || _('check Diagnostics and the network interface')), 'warn');
			this.ownerSwitch = null;
		} else if (!expected && elapsed >= 120000) {
			this.ownerStage(_('The managing service was saved, but the modem is still managed by %s. Check Diagnostics and ModemManager inhibition.').format(
				ctl || _('another service')), 'err');
			this.ownerSwitch = null;
		}
	},



	/* «Базовый» shows the main rows, «Подробный» everything; the empty rows («—») only at «Подробный» */
	gate: function() {
		var lv = this.level, lr = this.rowLevel;
		Object.keys(lr).forEach(function(k) {
			/* 'issue': at «Базовый» only while it says something is wrong (the owner, 2026-10-26: «Стабильно · в норме» is noise there) */
			var need = lr[k][2], said = lr[k][1].textContent !== '—';
			var show = lv === 'expert' || (!need || need === 'issue' && lr[k][1].getAttribute('data-fine') !== '1') && said;
			lr[k][0].hidden = lr[k][1].hidden = !show;
		});
		(this.rowCards || []).forEach(function(c) { c[0].hidden = c[1].every(function(k) { return lr[k][1].hidden; }); });
		/* «Базовый» hides the engineer's words and controls marked mm-x, «Подробный» the plain ones marked mm-b */
		if (this.root) this.root.classList.toggle('is-basic', lv === 'normal');
		if (this.smsUi) this.smsCount();
		if (this.hist && this.hist.last && this.histLevel !== lv) { this.histLevel = lv; this.applyHistory(this.hist.last); }
		/* USSD from «Подробный» (spec 38); a HiLink's SMS go through its web, a QModem one's through QModem: no USSD (r94, r95) */
		/* r117: a HiLink's web sends USSD (caps.sms.ussd «web») */
		var smsCaps = (this.caps || {}).sms || {};
		if (this.smsUi) this.smsUi.ussdCard.hidden = lv === 'normal' || smsCaps.via === 'qmodem' || (smsCaps.via === 'web' && smsCaps.ussd !== 'web') ||
			((this.caps || {}).ussd || {}).ok === false;	/* the firmware never answers USSD (FM350) */
		/* what is known of this model's USSD (field notes, 2026-11-02): said before a request, not after */
		if (this.smsUi) {
			var uk = ((this.data || {}).profile || {}).ussd;
			this.smsUi.ussdNote.hidden = uk !== 'unconfirmed' && uk !== '3g-only';
			text(this.smsUi.ussdNote, uk === 'unconfirmed' ? _('On this model USSD is not confirmed: the modem takes the request, but the network\'s answer may never come.') :
				uk === '3g-only' ? _('This model sends USSD in 3G only: in LTE the network refuses it. Switch the network mode to 3G for the request.') : '');
		}
		if (this.journalBox) this.journalBox.hidden = lv === 'normal';		/* «Базовый»: the 24-hour strip only */
		if (this.tabs && this.tabs.at) {						/* the AT presets too */
			/* what this modem cannot do now is not offered (field 2026-10-13: an FM350 under QModem has no SMS here) */
			var caps = this.caps || {}, noSms = caps.sms && caps.sms.ok === false, noAt = caps.at && caps.at.ok === false;
			this.tabs.at.tab.hidden = lv === 'normal' || noAt;
			this.tabs.sms.tab.hidden = noSms;
			this.esimShow();
			if ((this.tabs.at.tab.hidden && this.current === 'at') || (noSms && this.current === 'sms') ||
			    (this.tabs.esim.tab.hidden && this.current === 'esim')) this.show('overview');
		}
		/* engineer's tools (UI budget, spec 25.9.15): a free AT command and the AT port at «Подробный» */
		if (this.atUi) this.atUi.free.hidden = lv !== 'expert';
		if (this.atPort && this.atPort.card) this.atPort.card.hidden = lv === 'normal';
		if (this.temp && this.temp.card) this.temp.card.hidden = lv === 'normal' || !!(this.caps && this.caps.at && this.caps.at.ok === false);
	},

	/* QMI/MBIM/AT owners (field 2026-10-13): network and signal read over the engine's AT port, at most every 30 s */
	atReading: function(d) {
		var self = this, ctl = (d.owner || {}).control, now = Date.now();
		/* under QModem through its own cell_info (the owner, 2026-10-19: the FM350 showed no operator nor levels) */
		if (d.modemmanager || d.present !== true || ctl === 'modemmanager' || this.atBusy || this.atAsked && now - this.atAsked < 30000) return;
		this.atAsked = now;
		this.atBusy = true;	/* a slow AT port: one request at a time (audit 2026-10-20) */
		getMetrics(this.id, 30000).then(function(r) {
			if (r && r.web) self.applyWeb(r);
			if (!r || !r.metrics) return;
			self.atMm = atAsMm(r.metrics);
			if (self.atMm && r.source === 'qmodem') self.atMm.via_qmodem = true;
			self.atMmAt = { id: self.id, gen: (self.data || {}).generation, at: Date.now() };
			if (self.data) self.apply(self.data);
		}, function() { /* no AT port or no answer: the hero keeps its dashes */ }).finally(function() { self.atBusy = false; });
	},

	hero_: function(d, connected, model) {
		this.heroModel = model;
		this.followConnect(d);
		var h = this.hero, mm = d.modemmanager || this.atMmNow(d) || {}, sess = d.session || {}, net = sess.netifd || {}, up = d.uplink || {},
			tr = up.traffic || {}, rec = d.recovery || {}, health = d.health || {}, present = d.present === true;
		var running = rec.state === 'running', role = up.role, tone, b, say;
		if (running) { tone = 'run'; b = _('A recovery is running.'); say = _('“%s”: the progress is on the “Diagnostics” tab.').format((STAGES[rec.current_level] || [rec.current_level || ''])[0]); }
		else if (!present) { tone = 'off'; b = _('The modem is disconnected from USB.'); say = _('The details below are the last received.'); }
		else if (!connected) { tone = 'err'; b = _('No connection.'); say = why(sess.reason) || why(health.stalled) || _('The modem is not connected to the operator\'s network.'); }
		else if (role === 'limited' && (up.tunnel || {}).ok === true) { tone = 'ok'; b = _('The internet works through the tunnel.');
			say = _('Directly only the operator\'s whitelist opens') + ' · ' + _('the rest goes through the tunnel %s').format([words(up.tunnel.via), words(up.tunnel.outbound)].filter(Boolean).join(', ') || '—'); }
		else if (role === 'limited' || role === 'backup_limited') { tone = 'warn'; b = _('Limited access.'); say = checkWords(up.reach); }
		else if (role === 'no_internet' || role === 'backup_not_ready') { tone = 'warn'; b = _('The internet is unavailable.'); say = checkWords(up.reach) || _('There is a connection, the traffic does not pass.'); }
		else if (up.dns && up.dns.ok === false) { tone = 'warn'; b = _('Sites do not open.'); say = _('The router\'s DNS does not answer (%s).').format(DNSWHY[up.dns.error] || up.dns.error || _('no answer')); }
		else if (role === 'checking') { tone = 'ok'; b = _('Connected.'); say = _('The connection check is running.'); }
		else { tone = 'ok'; b = _('The internet works.'); say = (ROLES[role] || _('Online')) + (up.reach && up.reach.ok ? ' · ' + _('response %d ms').format(up.reach.latency_ms) : ''); }
		if (health.state === 'stalled' && !running) tone = 'err';
		h.node.className = 'mm-card mm-hero mm-header is-' + tone;
		text(h.mark, { ok: '✓', warn: '!', err: '×', off: '–', run: '…' }[tone]);
		text(h.title, words(d.name) || model);
		if (typeof mm.setOwnerBadge === 'function') mm.setOwnerBadge(h.ownerTag, ((d.owner || {}).control));
		else { h.ownerTag.hidden = true; h.ownerTag.textContent = ''; }
		var wc = d.connection || {}, wport = present && wc.web_port;
		h.web.hidden = !wport;
		if (wport) {
			h.web.setAttribute('href', 'http://' + (location.hostname.indexOf(':') >= 0 ? '[' + location.hostname + ']' : location.hostname) + ':' +
				wport + '/' + (/^12d1:/.test(d.vidpid || '') ? 'html/home.html' : ''));
			h.web.dataset.open = wc.web_open_s === -1 || wc.web_open_s > 60 ? '1' : '';
			h.web.title = _('The modem\'s web interface, through the router (port %s)').format(wport);
		}
		text(h.model, [words(d.name) ? model : '', techWords(mm.access_tech)].filter(Boolean).join(' · '));
		h.model.hidden = !h.model.textContent || h.model.textContent === '—';
		text(h.pill, running ? _('Recovery…') : !present ? _('Disconnected from USB') : connected ? ROLES[role] || _('Connected') : _('No internet connection'));
		/* a working backup in its own colour, not the main one's green (the owner, 2026-10-19) */
		h.pill.className = 'mm-pill ' + (tone === 'ok' && role === 'backup_ready' && !running && present ? 'is-backup' : TONE_PILL[tone]);
		text(h.sayB, b); text(h.sayText, say);
		this.applyChain(d.diagnosis);
		var badge = present ? opBadge(mm.operator_code, words(mm.operator), (d.sim_operator || {}).code) : null;
		h.badge.replaceChildren.apply(h.badge, badge ? [badge] : []);
		text(h.net, present ? [badge ? '' : words(mm.operator), techWords(mm.access_tech)].filter(Boolean).join(' · ') : '');
		var icon = present ? signalIcon(mm) : null, sig = mm.signal || {}, c = sig.lte || sig['5g'] || {}, crat = sig.lte ? 'lte' : 'nr', csnr = sinrOf(c);
		setBars(h.bars, icon);
		text(h.sig, typeof c.rsrp !== 'number' || !present ? '' : STRENGTH[zone('rsrp', c.rsrp, crat)] +
			(csnr !== null ? ', ' + NOISE[zone('sinr', csnr, crat)] : '') + ': ' + _('%s dBm').format(numText(c.rsrp)) +
			/* the numbers at every level (the owner, 2026-11-08: «перестали показывать dBm и прочие SNR») */
			(csnr !== null ? ', SINR ' + _('%s dB').format(numText(csnr)) : ''));
		var first = list(net.ipv4_addresses).concat(list(net.ipv6_addresses)).filter(function(a) { return typeof a === 'string'; })[0];
		secret(h.addr, connected ? (first ? first.replace(/\/\d+$/, '') : typeof net.ipv4 === 'string' ? net.ipv4 : typeof net.ipv6 === 'string' ? net.ipv6 :
			net.ipv4 || net.ipv6 ? _('obtained') : '') : '');
		text(h.uptime, connected ? duration(net.uptime) : '');
		h.uptimeDt.hidden = h.uptime.hidden = !h.uptime.textContent || h.uptime.textContent === '—';	/* no «Сеанс —» (the owner, 2026-10-26) */
		text(h.traffic, connected ? totals(tr) : '');
		text(h.rates, connected && typeof tr.rx_rate === 'number' ? '↓ ' + rate(tr.rx_rate) + ' · ↑ ' + rate(tr.tx_rate) : '');
		h.grid.hidden = !present;
		h.check.disabled = !connected;
		h.reconnect.disabled = !present || running;
		/* nothing connects this modem by itself: no interface yet, or it was taken down and kept down */
		/* an interface of ours netifd no longer has (deleted by hand: an FM350 tester, 2026-11-03) is created again by
		 * «Connect», as a missing one: without the button the modem stayed down with «Reconnect» refused */
		var k = d.connection || {}, kept = k.deactivated, gone = !!k.interface && k.managed && net.state === 'missing';
		this.heroConnect = !present || connected || running || sess.state === 'connecting' ? '' : !k.interface || gone ? 'create' : kept ? 'up' : '';
		h.connect.hidden = !this.heroConnect;
		if (this.heroConnect && !connected) text(h.sayText, gone ? _('The modem\'s interface “%s” is gone from the network settings: “Connect” creates it again.').format(k.interface) :
			this.heroConnect === 'create' ? _('The modem has no interface yet: press “Connect”.') :
			kept.by === 'netifd' ? why(sess.reason) || _('The connection failed, the interface is switched off.') : _('The modem is deactivated; “Connect” brings the link back.'));
	},

	apply: function(d) {
		/* a modem that is its own router or a phone has no eSIM the engine can reach */
		if (this.esimNode) this.esimNode.hidden = (d.owner || {}).control === 'hilink' || (d.owner || {}).session === 'device';
		/* a modem ModemManager does not hold (FM350 on «MedveModem напрямую», bench 2026-10-19): «Сеть и сигнал» from
		 * the engine's AT reading, as the hero already was */
		var r = this.refs, mm = d.modemmanager || (d.present === true && this.atMmNow(d)) || {}, sess = d.session || {}, net = sess.netifd || {};
		var up = d.uplink || {}, nw = up.network || {}, tr = up.traffic || {}, rec = d.recovery || {}, health = d.health || {};
		var connected = d.present === true && sess.state === 'connected';
		this.applyHeld(d);
		text(this.fitText, knownLess(d));
		bannerShow(this.fitNote, !!knownLess(d));
		this.applyQmodem(d);
		this.caps = d.capabilities || {};
		this.simLast = d;
		this.gate();
		/* the model ModemManager named last: during a reset the USB name ("Generic Mobile Broadband Adapter") says nothing */
		var model = profileTitle(d, mm) || words(mm.model) || words(d.model) || words(d.product) || _('USB modem'), self = this;
		var running = rec.state === 'running';
		this.data = d;
		recoveryNotes([d], function() { return model; });	/* a step's start and end in the corner (the owner, 2026-10-20) */
		this.applyConnection(d);
		this.followOwnerSwitch(d);
		this.applyNetcheck(d);
		this.applyClash(((d || {}).session || {}).clash);
		this.notice(d, connected);
		this.atReading(d);
		this.hero_(d, connected, model);
		text(r.role, connected ? ROLES[up.role] || _('Connected') : _('No connection') + (words(sess.reason) ? ' — ' + why(sess.reason) : ''));
		text(r.iface, words(sess.interface)); text(r.device, words(up.device));
		/* an engine session says whether an address exists; ModemManager's bearer gives the address */
		/* the addresses themselves (netifd's, with the prefix), else ModemManager's bearer address, else the bare fact */
		var a4 = list(net.ipv4_addresses).filter(function(a) { return typeof a === 'string'; }),
			a6 = list(net.ipv6_addresses).filter(function(a) { return typeof a === 'string'; });
		secret(r.address, !connected ? '' : a4.concat(a6).join(', ') || [net.ipv4 ? (typeof net.ipv4 === 'string' ? 'IPv4 ' + net.ipv4 : _('IPv4 obtained')) : '',
			net.ipv6 ? (typeof net.ipv6 === 'string' ? 'IPv6 ' + net.ipv6 : _('IPv6 obtained')) : ''].filter(Boolean).join(' · '));
		text(r.gateway, !connected ? '' : [words(net.gateway4) || words(nw.gateway) || words(net.gateway), words(net.gateway6)].filter(Boolean)
			.filter(function(g, i, all) { return all.indexOf(g) === i; }).join(', '));
		text(r.dns, !connected ? '' : (list(nw.dns).length ? list(nw.dns) : list(net.dns)).join(', '));
		text(r.mtu, nw.mtu); text(r.route, !connected ? '' : nw.default_route ? _('Yes') : _('No (another interface is used)'));
		text(r.metric, nw.metric);
		/* «Базовый» says the check as a person would, without the device and the address (the owner, 2026-10-26) */
		text(r.check, connected ? ((this.level === 'normal' ? checkPlain(up.reach, up.role) : checkWords(up.reach)) || _('not run')) + (up.dns && up.dns.ok === false ?
			' · ' + _('the router\'s DNS does not answer (%s): sites will not open').format(DNSWHY[up.dns.error] || up.dns.error || _('no answer')) : '') : '');
		text(r.rates, connected && typeof tr.rx_rate === 'number' ? '↓ ' + rate(tr.rx_rate) + ' · ↑ ' + rate(tr.tx_rate) : '');
		text(r.session, connected ? totals(tr) : ''); text(r.day, totals(tr.day)); text(r.month, totals(tr.month));
		text(r.offline, offlineWords(d.downtime, this.level));
		text(r.quota, quotaWords(d.quota));
		r.quota.className = d.quota && d.quota.state === 'exceeded' ? 'mm-q-err' : d.quota && d.quota.state === 'warn' ? 'mm-q-warn' : '';
		text(r.stability, STABILITY[(d.stability || {}).state] || (d.stability || {}).state);
		r.stability.setAttribute('data-fine', (d.stability || {}).state === 'stable' ? '1' : '');
		r.power.setAttribute('data-fine', d.usb_power ? '' : '1');
		r.health.setAttribute('data-fine', !health.state || health.state === 'ok' ? '1' : '');
		text(r.power, d.usb_power ? _('it seems short, the kernel says: “%s”. A hub with its own power or another port is needed').format(words(d.usb_power.last)) : _('fine'));
		text(r.health, (HEALTH[health.state] || health.state || '') + (health.reason ? ': ' + (REASONS[health.reason] || health.reason) : ''));
		text(r.blocked, why(health.stalled) || why(health.blocked));
		/* «Подробный» (field 2026-10-13): the protocols the modem is driven by */
		var roles = d.port_roles || {}, ctl = roles.control || {}, atp = roles.at || {}, own = (d.owner || {}).control;
		var CTLP = { qmi_wwan: 'QMI', cdc_mbim: 'MBIM', cdc_ncm: 'NCM', rndis_host: 'RNDIS', cdc_ether: 'ECM' };
		text(r.p_ctl, [CTLP[ctl.driver] || ctl.driver || '', ctl.node || '', ctl.driver ? _('driver %s').format(ctl.driver) : '',
			OWNERS[own] || own || ''].filter(Boolean).join(' · '));
		text(r.p_sess, [sess.proto ? 'netifd proto ' + sess.proto : '', sess.interface ? _('interface %s').format(sess.interface) : '',
			net.l3_device || ''].filter(Boolean).join(' · '));
		text(r.p_at, atp.node ? atp.node + (atp.source ? ' (' + ({ profile: _('from the profile'), operator: _('chosen by hand'), probe: _('found by a probe'),
			modemmanager: _('from ModemManager'), interface: _('by the interface type') }[atp.source] || atp.source) + ')' : '') : '');
		var sig = mm.signal || {}, lte = sig.lte || {}, nr = sig['5g'] || {};
		function db(v, unit) { return typeof v === 'number' ? numText(v) + ' ' + unit : ''; }
		var plain = this.level === 'normal';
		/* the word and its number at every level (the owner, 2026-11-08: «Основной» had lost the dBm and the SINR) */
		function strength(v, rat) { return typeof v !== 'number' ? '' : STRENGTH[zone('rsrp', v, rat)] + ': ' + db(v, _('dBm')); }
		function noise(v, rat) { return typeof v !== 'number' ? '' : NOISE[zone('sinr', v, rat)] + ': ' + db(v, _('dB')); }
		text(r.r_tech, techWords(mm.access_tech)); text(r.r_operator, words(mm.operator));
		text(r.r_reg, ({ home: _('home network'), roaming: _('roaming'), searching: _('searching for a network'), denied: _('registration refused'), idle: _('no search running') })[mm.registration] || words(mm.registration));
		text(r.r_quality, typeof mm.signal_quality === 'number' ? mm.signal_quality + '%' : '');
		text(r.lte_rsrp, strength(lte.rsrp)); text(r.lte_snr, noise(lte.snr)); text(r.lte_rsrq, db(lte.rsrq, _('dB'))); text(r.lte_rssi, db(lte.rssi, _('dBm')));
		text(r.nr_rsrp, strength(nr.rsrp, 'nr')); text(r.nr_snr, noise(nr.snr, 'nr')); text(r.nr_rsrq, db(nr.rsrq, _('dB')));
		[['lte', lte], ['nr', nr]].forEach(function(p) {
			var m = [['rsrp', p[1].rsrp], ['sinr', p[1].snr], ['rsrq', plain ? null : p[1].rsrq]].filter(function(x) { return typeof x[1] === 'number'; });
			self.meters[p[0]].replaceChildren.apply(self.meters[p[0]], m.map(function(x) { return meter(x[0], x[1], p[0]); }));
			self.radioCards[p[0]].hidden = !m.length && p[0] === 'nr';
		});
		/* the radio settings first: the cells' «Lock» buttons go by them */
		this.present = d.present === true;
		this.applyRadioSettings(d.radio_settings || {});
		this.applyCells(this.withAtCells(mm), d.present === true);
		this.askCells(d, mm);
		this.applyAntennas(this.withAtCells(mm), d.present === true);
		var ctl = (d.owner || {}).control, own = ctl && ctl !== 'modemmanager' && !d.modemmanager;
		var age = this.atMmAt && mm.via_at ? Math.round((Date.now() - this.atMmAt.at) / 1000) : 0;
		this.radioNote.textContent = mm.via_qmodem ? _('The network and the signal are read through QModem, at most once in 30 s.') :
			mm.via_web ? _('The network and the signal are read from the modem\'s web page, at most once in 30 s') + (age > 90 ? ' (' + _('shown %d min ago').format(Math.round(age / 60)) + ')' : '') + '.' :
			mm.via_at ? _('The network and the signal are read through the modem\'s AT port, at most once in 30 s') + (age > 90 ? ' (' + _('shown %d min ago').format(Math.round(age / 60)) + ')' : '') + '.' :
			own && d.present === true ? _('Reading the modem\'s data…') : !mm.model ? _('Measurements are unavailable for this way of managing the modem.') :
			(sig.rate ? _('The signal refreshes every %d s').format(sig.rate) : _('No signal data yet')) +
			(list(mm.cells).length ? '; ' + _('the cells once in 30 s') + (typeof mm.radio_at === 'number' ? ' (' + _('the last at %s').format(clock(mm.radio_at)) + ')' : '') : '') + '.';
		text(r.model, model); text(r.fw, words(mm.firmware) || words(mm.revision)); secret(r.imei, words(mm.imei) || (words(mm.imei_tail) ? '•••• ' + mm.imei_tail : ''));
		secret(r.id, d.device_id); text(r.path, words(d.attachment_id));
		/* the USB descriptor (the owner, 2026-10-20: a HiLink or UFI showed nothing of its USB side) */
		text(r.usbid, words(d.vidpid));
		text(r.usbname, [words(d.manufacturer), words(d.product)].filter(Boolean).join(' · '));
		secret(r.usbserial, words(d.serial) || (d.identity_strength === 'port' ? _('none: the modem is known by its USB port') : ''));
		var drv = {};
		list(d.interfaces).forEach(function(i) { if (i && i.driver) drv[i.driver] = (drv[i.driver] || []).concat(i.number != null ? [String(i.number)] : []); });
		text(r.drivers, Object.keys(drv).map(function(k) { return k + (drv[k].length ? ' (' + drv[k].join(', ') + ')' : ''); }).join(', '));
		/* the SIM as ModemManager reads it: identifiers are shown to the operator (2026-10-09) */
		this.applySim(d, mm);
		this.applyName(d);
		text(r.owner, OWNERS[(d.owner || {}).control] || words((d.owner || {}).control));
		text(r.dial, dialWords((d.connection || {}).dial));
		var cp = d.capabilities || {};
		text(r.nocap, [cp.sms && cp.sms.ok === false ? _('SMS and USSD: %s').format(why(cp.sms.why || '')) : '',
			cp.at && cp.at.ok === false ? _('AT console: %s').format(why(cp.at.why || '')) : '',
			cp.ussd && cp.ussd.ok === false ? _('USSD: %s').format(why(cp.ussd.why || '')) : ''].filter(Boolean).join('; '));
		text(r.ports, list((d.ports || {}).tty).concat(list((d.ports || {}).net), list((d.ports || {}).control)).filter(function(p) { return typeof p === 'string'; }).join(', '));
		text(r.hostfix, d.host_fix ? (HOSTFIX[d.host_fix.result] || words(d.host_fix.result)) + (d.host_fix.detail ? ' — ' + d.host_fix.detail : '') :
			_('fine'));
		text(r.rec_state, (REC_STATE[rec.state] || rec.state || '') + (rec.current_level ? ' ' + _('“%s”').format((STAGES[rec.current_level] || [rec.current_level])[0]) : ''));
		var last = rec.last_steps || list(rec.recent).slice(-1)[0], lastName = last && (typeof last.level === 'string' ? last.level : last.name);
		text(r.rec_last, last ? _('“%s”').format((STAGES[lastName] || [lastName])[0]) + (last.at || last.t ? ', ' + clock(last.at || last.t) : '') +
			' — ' + (RESULTS[last.result] || last.result) + (typeof last.elapsed_s === 'number' ? ' (' + _('%d s').format(last.elapsed_s) + ')' : '') +
			(last.result !== 'succeeded' && last.detail ? ': ' + why(last.detail) : '') +
			(last.result === 'succeeded' && last.internet === false ? ', ' + _('but there is no internet: the session is up, the connection check failed') : '') +
			(last.result === 'succeeded' && list(d.drivers_missing).length ? ', ' + _('but the modem came back without the needed driver (see “Drivers and services”)') : '') : '');
		text(r.rec_hold, why(rec.blocked_reason));
		this.applyProgress(rec.progress, rec.current_level);
		this.busyNote.hidden = !running;
		if (this.chainBtn) this.chainBtn.disabled = running || d.present !== true;
		list(rec.levels).forEach(function(level) {
			var row = self.stage(level), busy = running || d.present !== true;
			text(row.status, level.supported ? level.automatic ? _('Automatically on a failure and by hand') : _('By hand only') :
				_('Unavailable: %s').format(why(level.why) || _('not supported')));
			row.button.disabled = !level.supported || busy;
			row.node.classList.toggle('is-off', !level.supported);
			row.node.classList.toggle('mm-off', !level.supported);
		});
		this.gate();
	},

	/* serving cells as the mockup's aggregation bar and carrier cards; neighbours as a table (spec 25.9, stage 2) */
	/* from «Подробный»: what the modem may use and how it was read; why when nothing can be read */
	/* «История приёма» (spec 25.9.13; the owner's design 2026-10-21), from «Подробный» */
	histCard: function() {
		var self = this, hs = this.hist = { range: '1h', mode: 'level' };
		try { if (HIST_MODES.some(function(m) { return m[0] === localStorage.getItem('mm:hist-mode'); })) hs.mode = localStorage.getItem('mm:hist-mode'); } catch (e) { /* no storage */ }
		var press = function(box, attr, v) {
			Array.prototype.forEach.call(box.children, function(b) { b.setAttribute('aria-pressed', String(b.getAttribute(attr) === v)); });
		};
		/* what is drawn: one measure at a time over the same ribbon and time axis */
		hs.modes = E('div', { 'class': 'mm-seg mm-hist-modes mm-x', role: 'group', 'aria-label': _('What to show') }, HIST_MODES.map(function(m) {
			return E('button', { type: 'button', 'data-mode': m[0], 'aria-pressed': String(m[0] === hs.mode), click: function() {
				hs.mode = m[0];
				try { localStorage.setItem('mm:hist-mode', hs.mode); } catch (e) { /* no storage */ }
				press(hs.modes, 'data-mode', hs.mode);
				if (hs.last) self.applyHistory(hs.last);
			} }, m[1]);
		}));
		hs.seg = E('div', { 'class': 'mm-seg', role: 'group', 'aria-label': _('Period') }, HIST_RANGES.map(function(r) {
			return E('button', { type: 'button', 'data-range': r[0], 'aria-pressed': String(r[0] === hs.range), click: function() {
				hs.range = r[0]; hs.at = 0;
				press(hs.seg, 'data-range', hs.range);
				self.loadHistory(true);
			} }, r[1]);
		}));
		hs.now = E('strong', { 'class': 'mm-hist-now' });
		hs.nowSub = E('span', { 'class': 'mm-faint mm-hist-now-sub' });
		hs.charts = E('div', { 'class': 'mm-hist-charts' });
		hs.events = E('ul', { 'class': 'mm-hist-events' });
		hs.table = E('tbody');
		hs.note = E('p', { 'class': 'mm-faint' });
		this.histNode = this.sec('hist', _('Reception history'), [
			E('div', { 'class': 'mm-hist-head' }, [hs.modes, E('div', { 'class': 'mm-hist-side' }, [hs.seg, hs.now, hs.nowSub])]),
			hs.charts, hs.events, hs.note,
			E('details', { 'class': 'mm-hist-tablebox mm-x' }, [E('summary', {}, _('Table of values')),
				E('div', { 'class': 'mm-tablewrap' }, E('table', { 'class': 'mm-table mm-hist-table' }, [
					E('thead', {}, E('tr', {}, [E('th', {}, _('Time')), E('th', {}, _('Network')), E('th', { 'class': 'num' }, 'RSRP LTE'), E('th', { 'class': 'num' }, 'SINR LTE'),
						E('th', { 'class': 'num' }, 'RSRP 5G'), E('th', { 'class': 'num' }, 'SINR 5G'), E('th', {}, _('Carriers (RSRP)'))])), hs.table]))])]);
		this.histNode.classList.add('mm-hist');
		return this.histNode;
	},

	/* asked while «Радио» is open: the 5-minute view with every poll, the longer ones once a minute */
	loadHistory: function(force) {
		var self = this, hs = this.hist, now = Date.now();
		if (!this.id || this.current !== 'radio' || this.histNode.hidden || hs.pending) return Promise.resolve();
		if (!force && hs.at && now - hs.at < (hs.range === '5m' ? 4000 : 55000)) return Promise.resolve();
		/* audit M01: the answer is for the range asked; one for a range left meanwhile is dropped and the one now
		 * chosen is asked at once */
		var asked = hs.range, id = this.id;
		hs.pending = true;
		return signalHistory(id, asked).then(function(r) {
			if (asked !== hs.range || id !== self.id) return;
			hs.at = Date.now();
			self.applyHistory(r || {});
		}, function(e) {
			/* «not in this version» only for a method the engine does not have; a slow first answer is retried */
			if (asked !== hs.range) return;
			var why = String((e && e.message) || e);
			if (/Method not found|code 3\b/i.test(why)) text(hs.note, _('The signal history is unavailable in this version of the MedveModem service.'));
			/* said apart (the owner, 2026-10-31: an EP06 under ModemManager showed only «retrying»): no such modem now, a
			 * service that does not answer, or a slow first answer */
			else { text(hs.note, /code 4\b|Not ?found/i.test(why) && !/timed? ?out/i.test(why) ?
				_('The service has no modem with this address now (plugged into another USB port or removed): open it again from “Modems”.') :
				/timed? ?out|code 7\b/i.test(why) ? _('The MedveModem service does not answer, retrying…') : _('The history is loading… retrying')); hs.at = 0; }
		}).finally(function() {
			hs.pending = false;
			if (asked !== hs.range) return self.loadHistory(true);
		});
	},

	applyHistory: function(r) {
		this.histEmpty = !list(r.points).length;
		if (this.rsLast) this.histNode.hidden = false;
		/* «Базовый»: the signal level only, no choice of what to show (the owner, 2026-10-26) */
		var hs = this.hist, mode = this.level === 'normal' ? 'level' : hs.mode, range = HIST_RANGES.filter(function(x) { return x[0] === (r.range || hs.range); })[0];
		hs.last = r;
		var radioEv = list(this.lastEvents).filter(function(e) { return RADIO_KINDS[e.kind] && typeof e.t === 'number'; }).map(function(e) {
			return { t: e.t, kind: 'radio', detail: eventWords(e)[0].replace(/^⚠ /, '') }; });
		var h = { range: range[0], span: range[2], now: Math.floor(Date.now() / 1000), step_s: r.step_s, points: list(r.points),
			events: list(r.events).concat(radioEv).sort(function(a, b) { return a.t - b.t; }) };
		var p = reliefPanel(h, mode, Math.max(280, hs.charts.clientWidth || 640));
		/* one card on hover: the time, the technology, the value and its grade in words */
		var card = E('div', { 'class': 'mm-hist-card', hidden: '', role: 'status' });
		hs.charts.replaceChildren(E('div', { 'class': 'mm-relief-wrap' }, [p.node, card]));
		var last = p.qs[p.qs.length - 1];
		text(hs.now, !last ? '' : mode === 'tech' ? _('Now · %s').format(TECH_LV[last.lv] || '—') + (last.tech ? ' (' + techSay(last.tech) + ')' : '') :
			[histGrade(mode, last), histValue(mode, last)].filter(Boolean).join(' · ') || _('No value now'));
		text(hs.nowSub, mode === 'tech' ? _('Network technology, not speed') : mode === 'level' ? _('A grade with the technology in mind') : _('The higher, the cleaner the signal'));
		if (p.hit) {
			p.hit.addEventListener('mousemove', function(ev) {
				var box = p.node.getBoundingClientRect(), px = ev.clientX - box.left, best = null;
				p.qs.forEach(function(q) { if (!best || Math.abs(p.x(q.t) - px) < Math.abs(p.x(best.t) - px)) best = q; });
				if (!best) return;
				var bx = p.x(best.t), by = p.y(best);
				p.cross.setAttribute('x1', bx.toFixed(1)); p.cross.setAttribute('x2', bx.toFixed(1)); p.cross.setAttribute('visibility', 'visible');
				if (by !== null) { p.dot.setAttribute('cx', bx.toFixed(1)); p.dot.setAttribute('cy', by.toFixed(1)); }
				p.dot.setAttribute('visibility', by === null ? 'hidden' : 'visible');
				var grade = histGrade(mode, best), value = histValue(mode, best);
				var said = techSay(best.tech), named = TECH_LV[best.lv] || said;
				card.replaceChildren(E('b', {}, hhmm(best.t) + (named ? ' · ' + named + (said && said !== named ? ' (' + said + ')' : '') : '')),
					E('span', {}, mode === 'tech' ? _('Network technology') : value || _('No comparable data')),
					E('span', {}, mode === 'level' ? (grade ? _('%s signal').format(grade) : '') : mode === 'quality' ? grade : ''));
				card.hidden = false;
				/* beside the point, inside the chart */
				var cw = card.offsetWidth || 160;
				card.style.left = Math.max(0, Math.min(box.width - cw, bx + 12 > box.width - cw ? bx - cw - 12 : bx + 12)) + 'px';
				card.style.top = Math.max(0, (by === null ? 60 : by) - 52) + 'px';
			});
			p.hit.addEventListener('mouseleave', function() {
				p.cross.setAttribute('visibility', 'hidden'); p.dot.setAttribute('visibility', 'hidden'); card.hidden = true;
			});
		}
		var evs = h.events.filter(function(e) { return e.t >= h.now - h.span; }).slice(-8).reverse();
		hs.events.replaceChildren.apply(hs.events, evs.map(function(e) {
			return E('li', {}, [E('time', { 'class': 'mm-t' }, hhmm(e.t)), ' ' + (HIST_EVENT[e.kind] || e.kind) + ': ' + eventSay(e)]); }));
		hs.events.hidden = !evs.length;
		text(hs.note, h.points.length ? (h.range === '5m' ? _('Every measurement for 5 minutes') : h.range === '1h' ? _('Averages per minute') : _('Averages per 5 minutes')) +
			'. ' + _('A dashed line is a change of technology (labelled) or of cell; a gap means no measurements, not bad reception. The history is kept in the router\'s memory and reset at a restart.') :
			_('No measurements yet: they appear when the modem reports the signal level.'));
		var fmt = function(v) { return typeof v === 'number' ? num(v) : '—'; };
		hs.table.replaceChildren.apply(hs.table, h.points.slice(-60).reverse().map(function(q) {
			return E('tr', {}, [E('td', {}, hhmm(q.t)), E('td', {}, techSay(q.tech)), E('td', { 'class': 'num' }, fmt(q.lte_rsrp)),
				E('td', { 'class': 'num' }, fmt(q.lte_sinr)), E('td', { 'class': 'num' }, fmt(q.nr_rsrp)), E('td', { 'class': 'num' }, fmt(q.nr_sinr)),
				E('td', {}, Object.keys(q.carriers || {}).sort(carrierOrder).map(function(k) { return carrierName(k) + ' ' + fmt(q.carriers[k]); }).join(', ') || '—')]);
		}));
	},
	/* «Трафик и скорость» (spec §42, the owner 2026-11-02): the router's own counters of the modem's
	 * network device, so every modem with one, ModemManager or not; «Базовый» the chart only */
	trafCard: function() {
		var self = this, ts = this.traf = { range: '1h', dir: 'rx' };
		try { if (localStorage.getItem('mm:traf-dir') === 'tx') ts.dir = 'tx'; } catch (e) { /* no storage */ }
		ts.dirs = E('div', { 'class': 'mm-seg mm-traf-dirs', role: 'group', 'aria-label': _('What to show') }, TRAF_DIRS.map(function(d) {
			return E('button', { type: 'button', 'data-dir': d[0], 'aria-pressed': String(d[0] === ts.dir), click: function() {
				ts.dir = d[0];
				try { localStorage.setItem('mm:traf-dir', ts.dir); } catch (e) { /* no storage */ }
				Array.prototype.forEach.call(ts.dirs.children, function(b) { b.setAttribute('aria-pressed', String(b.getAttribute('data-dir') === ts.dir)); });
				if (ts.last) self.applyTraffic(ts.last);
			} }, d[1]);
		}));
		ts.seg = E('div', { 'class': 'mm-seg', role: 'group', 'aria-label': _('Period') }, HIST_RANGES.map(function(r) {
			return E('button', { type: 'button', 'data-range': r[0], 'aria-pressed': String(r[0] === ts.range), click: function() {
				ts.range = r[0]; ts.at = 0;
				Array.prototype.forEach.call(ts.seg.children, function(b) { b.setAttribute('aria-pressed', String(b.getAttribute('data-range') === ts.range)); });
				self.loadTraffic(true);
			} }, r[1]);
		}));
		ts.peak = E('strong', { 'class': 'mm-traf-now' });
		ts.bytes = E('span', { 'class': 'mm-faint mm-traf-sub mm-x' });
		ts.charts = E('div', { 'class': 'mm-hist-charts' });
		ts.note = E('p', { 'class': 'mm-faint' });
		ts.table = E('tbody');
		this.trafNode = this.sec('traf', _('Traffic and speed'), [
			E('div', { 'class': 'mm-hist-head' }, [ts.dirs, E('div', { 'class': 'mm-hist-side' }, [ts.seg, ts.peak, ts.bytes])]),
			ts.charts, ts.note,
			E('details', { 'class': 'mm-hist-tablebox mm-x' }, [E('summary', {}, _('Table of values')),
				E('div', { 'class': 'mm-tablewrap' }, E('table', { 'class': 'mm-table mm-traf-table' }, [
					E('thead', {}, E('tr', {}, [E('th', {}, _('Time')), E('th', { 'class': 'num' }, _('Received, average')), E('th', { 'class': 'num' }, _('Received, peak')),
						E('th', { 'class': 'num' }, _('Sent, average')), E('th', { 'class': 'num' }, _('Sent, peak')), E('th', { 'class': 'num' }, _('Traffic'))])), ts.table]))])]);
		this.trafNode.classList.add('mm-traf');
		return this.trafNode;
	},

	/* as the signal history: the 5-minute view with every poll, the longer ones once a minute; the answer for a range
	 * left meanwhile is dropped */
	loadTraffic: function(force) {
		var self = this, ts = this.traf, now = Date.now();
		if (!ts || !this.id || this.current !== 'radio' || ts.pending || ts.gone) return Promise.resolve();
		if (!force && ts.at && now - ts.at < (ts.range === '5m' ? 4000 : 55000)) return Promise.resolve();
		var asked = ts.range, id = this.id;
		ts.pending = true;
		return trafficHistory(id, asked).then(function(r) {
			if (asked !== ts.range || id !== self.id) return;
			ts.at = Date.now();
			self.applyTraffic(r || {});
		}, function(e) {
			if (asked !== ts.range) return;
			/* an engine without the method: the card goes, as on a version before it */
			if (/Method not found|code 3\b/i.test(String((e && e.message) || e))) { ts.gone = true; self.trafNode.hidden = true; return; }
			text(ts.note, _('The history is loading… retrying'));
			ts.at = 0;
		}).finally(function() {
			ts.pending = false;
			if (asked !== ts.range) return self.loadTraffic(true);
		});
	},

	applyTraffic: function(r) {
		var ts = this.traf, k = ts.dir, range = HIST_RANGES.filter(function(x) { return x[0] === (r.range || ts.range); })[0];
		ts.last = r;
		var h = { span: range[2], now: Math.floor(Date.now() / 1000), step_s: r.step_s, points: list(r.points) };
		var p = trafficPanel(h, Math.max(280, ts.charts.clientWidth || 640), k);
		var card = E('div', { 'class': 'mm-hist-card', hidden: '', role: 'status' });
		ts.charts.replaceChildren(E('div', { 'class': 'mm-relief-wrap' }, [p.node, card]));
		var pk = r.peak || {}, many = h.step_s > 5;
		text(ts.peak, typeof pk[k + '_bps'] === 'number' ? _('Peak %s at %s').format(bitrate(pk[k + '_bps']), hhmm(pk[k + '_at'])) : '');
		text(ts.bytes, r.bytes && h.points.length ? _('Over the period: received %s, sent %s').format(bytes(r.bytes.rx), bytes(r.bytes.tx)) : '');
		if (p.hit) {
			p.hit.addEventListener('mousemove', function(ev) {
				var box = p.node.getBoundingClientRect(), px = ev.clientX - box.left, best = null;
				p.pts.forEach(function(q) { if (!best || Math.abs(p.x(q.t) - px) < Math.abs(p.x(best.t) - px)) best = q; });
				if (!best) return;
				var bx = p.x(best.t), by = p.y(best[k + '_bps'] || 0);
				p.cross.setAttribute('x1', bx.toFixed(1)); p.cross.setAttribute('x2', bx.toFixed(1)); p.cross.setAttribute('visibility', 'visible');
				p.dot.setAttribute('cx', bx.toFixed(1)); p.dot.setAttribute('cy', by.toFixed(1)); p.dot.setAttribute('visibility', 'visible');
				card.replaceChildren(E('b', {}, hhmm(best.t)),
					E('span', {}, (many ? _('average %s').format(bitrate(best[k + '_bps'])) : bitrate(best[k + '_bps']))),
					many ? E('span', {}, _('peak %s').format(bitrate(best[k + '_peak_bps']))) : '');
				card.hidden = false;
				var cw = card.offsetWidth || 160;
				card.style.left = Math.max(0, Math.min(box.width - cw, bx + 12 > box.width - cw ? bx - cw - 12 : bx + 12)) + 'px';
				card.style.top = Math.max(0, by - 52) + 'px';
			});
			p.hit.addEventListener('mouseleave', function() { p.cross.setAttribute('visibility', 'hidden'); p.dot.setAttribute('visibility', 'hidden'); card.hidden = true; });
		}
		/* in plain words (the owner, 2026-11-03: the first text read as a translation) */
		text(ts.note, h.points.length ? (range[0] === '5m' ? _('A point every 5 seconds.') : range[0] === '1h' ?
			_('Filled: the average speed of each minute; line: its fastest 5 seconds.') : _('Filled: the average speed of each 5 minutes; line: their fastest 5 seconds.')) +
			' ' + _('The router counts it on the modem\'s interface; a break in the chart is time without a connection. The history lives in the router\'s memory and starts over after a reboot.') :
			_('No measurements yet: they appear while the modem is connected.'));
		ts.table.replaceChildren.apply(ts.table, h.points.slice(-60).reverse().map(function(q) {
			return E('tr', {}, [E('td', {}, hhmm(q.t)), E('td', { 'class': 'num' }, bitrate(q.rx_bps)), E('td', { 'class': 'num' }, bitrate(q.rx_peak_bps)),
				E('td', { 'class': 'num' }, bitrate(q.tx_bps)), E('td', { 'class': 'num' }, bitrate(q.tx_peak_bps)),
				E('td', { 'class': 'num' }, typeof q.rx_bytes === 'number' ? _('received %s, sent %s').format(bytes(q.rx_bytes), bytes(q.tx_bytes)) : '—')]);
		}));
	},
	applyRadioSettings: function(rs) {
		var r = this.refs, why = String(rs.why || ''), unread = RS_UNREAD.test(why), read = rs.via === 'qmodem' || rs.via === 'at';
		this.rsLast = rs;
		RS_WHY.forEach(function(w) { if (w[0].test(why)) why = w[1]; });
		if (this.rsError) why = this.rsError;
		/* a modem that is its own router (HiLink, UFI) keeps the network mode and bands on its own web page: nothing to
		 * show here, the card goes (the owner, 2026-10-20: «если не поддерживается — скрывать») */
		var selfRouter = (((this.data || {}).owner) || {}).control === 'hilink';
		this.rsCard.hidden = this.level === 'normal' || selfRouter;
		/* the history from «Подробный», a HiLink's too: hidden «while empty» it was never asked for, so it stayed hidden
		 * (the owner, 2026-10-21: «нет истории приема у huawei»); the engine keeps it from the modem's web readings */
		this.histNode.hidden = false;
		text(r.rs_modes, modesText(rs.modes));
		text(r.rs_pref, rs.modes ? (MODE_WORDS[rs.modes.preferred] || rs.modes.preferred || _('none')) : '');
		text(r.rs_bands, rs.bands_any ? _('all the modem supports') : bandsText(rs.bands));
		text(r.rs_supported, bandsText(rs.supported_bands));
		/* r94: the cell lock where the modem has one (FM350: LTE; T99W175: LTE and 5G); set from a cell's «Lock» */
		var lockable = list(rs.cell_lock_rats).length > 0, cl = rs.cell_lock, ch = rs.change || {}, wl = (ch.want || {}).cell_lock;
		/* a lock on its way: said in its row at once (the owner, 2026-11-03: the row kept «none» while the T99 restarted
		 * to take it); the reading after the change shows what the modem holds */
		var lockGoing = wl !== undefined && (ch.state === 'applying' || ch.state === 'waiting' || ch.state === 'rolling_back');
		if (lockable && lockGoing) r.rs_lock.replaceChildren(E('span', { 'class': 'mm-pill is-run' },
			ch.state === 'rolling_back' ? _('the previous lock is being put back…') : (wl && typeof wl === 'object' ?
				_('being written: %s').format(lockText(wl)) : _('the removal is being written')) + ' · ' +
				(rs.dialect === 'foxconn-sdx' ? _('the modem takes it after its restart, about a minute') : _('the modem takes it after flight mode'))));
		else if (!lockable) text(r.rs_lock, rs.dialect === 'quectel-ep06' && rs.via === 'modemmanager' ?
			_('For EP06 cell locking, manually select MedveModem native QMI management. ModemManager reserves its AT port.') : '');
		else r.rs_lock.replaceChildren.apply(r.rs_lock, cl ? [E('span', {}, lockText(cl) + (cl.count > 1 ? ' · ' + _('Locked cells: %d').format(cl.count) : '')), ' ',
			E('button', Object.assign({ type: 'button', 'class': 'mm-link mm-rs-unlock', click: mm.busy(this.lockCell.bind(this, null)) },
				rs.writable && this.present ? {} : { disabled: '' }), _('Remove'))] :
			[E('span', { 'class': 'mm-faint' }, _('none: the modem picks its cells itself. «Lock» beside a cell holds the modem on it.'))]);
		text(r.rs_via, (VIA_WORDS[rs.via] || '—') + (read && typeof rs.age_s === 'number' ? ', ' +
			_('read %s').format(rs.age_s < 60 ? _('just now') : _('%d min ago').format(Math.round(rs.age_s / 60))) : ''));
		/* ModemManager narrowed to the one mode the modem stored: said, and another mode is written
		 * to the modem's own preference (the owner, 2026-11-02: no re-plug into a PC) */
		var nw = rs.narrowed && !why ? _('ModemManager offers only %s: that mode is stored in the modem itself. Another mode is written to the modem directly (QMI), with no re-plug.')
			.format(modesText({ allowed: list(rs.narrowed.offered) })) : '';
		text(this.rsWhy, unread && !this.rsError ? _('Reading the settings: %s…').format(why) : why ? _('Unavailable: %s.').format(why) : nw);
		this.rsWhy.hidden = !why && !nw;
		this.rsRead.hidden = !read || this.level === 'normal';
		this.rsRead.disabled = !!this.rsReading;
		if (this.rsGist) text(this.rsGist, modesText(rs.modes));
		/* the same setting in a line on «Сеть и сигнал», where the bands are chosen by the signal (review 2026-10-17) */
		if (r.r_rs) {
			var self = this, what = rs.modes || rs.bands || rs.bands_any ? modesText(rs.modes) + (rs.bands_any ? ' ' + _('· all bands') :
				list(rs.bands).length ? ' · ' + bandsText(rs.bands) : '') : '';
			r.r_rs.replaceChildren.apply(r.r_rs, what ? [E('span', {}, what), ' ', E('button', { type: 'button', 'class': 'mm-link mm-rs-goto',
				click: function() { self.goTo(self.rsCard); } }, _('Change'))] : []);
		}
		this.applyRadioChange(rs);
		/* QModem's and the AT readers answer on request: once per page when the card is in view; a T99 under
		 * ModemManager too, for its cell lock that ModemManager has not (the engine reads it over AT) */
		var lockUnread = rs.via === 'modemmanager' && rs.dialect === 'foxconn-sdx' && !rs.cell_lock_rats;
		if ((unread || lockUnread) && !this.rsCard.hidden && this.rsAsked !== rs.via + '/' + rs.dialect && this.present) {
			this.rsAsked = rs.via + '/' + rs.dialect;
			this.readRadioSettings(false);
		}
	},

	/* r34 (F18): ModemManager's cell info is off by default; the Foxconn modems' cells come over AT^DEBUG?, an FM350's over AT+GTCCINFO? (r94) while
	 * «Радио» is open, at most every 30 s (the engine holds the same period) */
	/* audit M03: the AT readings belong to one attachment of one modem and age: another generation or 2 min drops them */
	withAtCells: function(mm) {
		var at = this.atFresh(), d = this.data || {}, ctl = (d.owner || {}).control;
		/* Native EP06 QENG reports belong to the current engine-owned modem, even if an
		 * older ModemManager snapshot still contains carriers. */
		if (at && ['netifd-qmi', 'netifd-mbim', 'medvemodem-at'].indexOf(ctl) >= 0 &&
			(d.radio_settings || {}).dialect === 'quectel-ep06')
			return Object.assign({}, mm, { cells: at.cells });
		return list(mm.cells).length || !at ? mm : Object.assign({}, mm, { cells: at.cells });
	},

	atFresh: function() {
		var at = this.atCells, d = this.data || {};
		return !at || at.id !== this.id || at.gen !== d.generation || Date.now() - at.at > 120000 ? null : at;
	},

	/* spec 26.1 step 3: QMI's CA info lists an SDX55's secondaries without levels; its AT^DEBUG? has them */
	wantsLevels: function(d, mm) {
		return ['foxconn-sdx', 'quectel', 'quectel-ep06'].indexOf((d.radio_settings || {}).dialect) >= 0 && list((mm.ca || {}).cells).some(function(c) {
			return c && c.role === 'secondary' && typeof c.rsrp !== 'number'; });
	},

	/* the last reading of a modem ModemManager does not hold stays 10 min and outlives a reload (review 2026-10-22: after
	 * 2 min, or on a new page, a working HiLink read «Сеть —», «Сигнал —» until the next reading) */
	atMmNow: function(d) {
		if (!this.atMm) this.loadAtMm();
		var a = this.atMmAt;
		return this.atMm && a && a.id === this.id && a.gen === (d || {}).generation && Date.now() - a.at <= 600000 ? this.atMm : null;
	},

	saveAtMm: function() {
		try { sessionStorage.setItem('mm:reading', JSON.stringify({ mm: this.atMm, at: this.atMmAt })); } catch (e) {}
	},

	loadAtMm: function() {
		if (this.atMmLoaded) return;
		this.atMmLoaded = true;
		try {
			var s = JSON.parse(sessionStorage.getItem('mm:reading') || 'null');
			if (s && s.mm && s.at && s.at.id === this.id) { this.atMm = s.mm; this.atMmAt = s.at; }
		} catch (e) {}
	},

	applyHeat: function(h) {
		var parts = [];
		if (h && typeof h.modem_c === 'number') parts.push(_('Modem temperature: %s °C').format(mm.num(h.modem_c)) +
			(typeof h.pa_c === 'number' ? ', ' + _('amplifier %s °C').format(mm.num(h.pa_c)) : ''));
		if (h && typeof h.throttle === 'number')
			parts.push(h.throttle > 0 ? _('The modem lowers its power because of heat: level %s of 3.').format(mm.num(h.throttle)) :
				_('No throttling because of heat.'));
		this.heatLine.hidden = !parts.length;
		text(this.heatLine, parts.join(' '));
	},

	askCells: function(d, mm) {
		var self = this, dialect = (d.radio_settings || {}).dialect, now = Date.now();
		var levels = list(mm.cells).length > 0 && this.wantsLevels(d, mm);
		if (this.current !== 'radio' || d.present !== true || (dialect === 'quectel-ep06' && ['netifd-qmi', 'netifd-mbim', 'medvemodem-at'].indexOf((d.owner || {}).control) < 0) ||
			(list(mm.cells).length && !levels && dialect !== 'quectel-ep06') || ['foxconn-sdx', 'foxconn-t77', 'fibocom-mtk', 'quectel', 'quectel-ep06'].indexOf(dialect) < 0 ||
			this.cellsBusy || (this.cellsAsked && now - this.cellsAsked < 30000)) return;
		this.cellsAsked = now; this.cellsBusy = true;
		(levels ? getCells(this.id, true) : getCells(this.id)).then(function(r) {
			if (r && Array.isArray(r.cells)) self.applyHeat(r.heat);
			if (r && Array.isArray(r.cells)) {
				self.atCells = { id: self.id, gen: (self.data || {}).generation, at: Date.now(), cells: r.cells };
				if (self.data) {
					var cur = self.withAtCells(self.data.modemmanager || self.atMmNow(self.data) || {});
					self.applyCells(cur, self.data.present === true);
					self.applyAntennas(cur, self.data.present === true);
				}
			}
		}, function() { /* no AT port or an older engine: the cells stay as they are */ }).finally(function() { self.cellsBusy = false; });
	},

	/* stage 2 (spec 25.9.12): where the last change stands, and the operator's keep / take back */
	radioChangeBox: function() {
		var self = this;
		this.rsChText = E('span');
		this.rsKeep = E('button', { type: 'button', 'class': 'mm-btn mm-rs-keep', click: mm.busy(function() { return self.radioConfirm('keep'); }) }, _('Keep'));
		this.rsBack = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-rs-back', click: mm.busy(function() { return self.radioConfirm('rollback'); }) },
			_('Put back as it was'));
		return E('div', { 'class': 'mm-callout mm-rs-change', hidden: '' }, [this.rsChText, E('div', { 'class': 'mm-chips' }, [this.rsKeep, this.rsBack])]);
	},

	radioConfirm: function(action) {
		var self = this;
		this.rsKeep.disabled = this.rsBack.disabled = true;
		return confirmRadio(this.id, action).then(function(r) {
			self.rsError = r.refused ? why(r.refused) : null;
		}).catch(function(e) { self.rsError = String(failText(e)); }).then(function() {
			self.rsKeep.disabled = self.rsBack.disabled = false;
			return self.update ? self.update() : null;
		});
	},

	/* the editor: technologies (and the preferred one where the owner has it), bands from what the modem has */
	radioEditor: function() {
		var self = this;
		this.rsOpen = E('button', { type: 'button', 'class': 'mm-btn mm-rs-open', click: mm.busy(function() { return self.radioEditOpen(); }) }, _('Change'));
		this.rsModes = E('div', { 'class': 'mm-chips mm-rs-modes' });
		this.rsPref = E('select', { 'class': 'mm-input mm-rs-prefsel', change: function() { self.radioEditCheck(); } });
		this.rsBandsBox = E('div', { 'class': 'mm-chips mm-rs-bands' });
		this.rsAny = E('input', { type: 'checkbox', 'class': 'mm-rs-anyband', change: function() { self.radioEditCheck(); } });
		this.rsAnyLabel = E('label', { 'class': 'mm-check' }, [this.rsAny, ' ' + _('all bands the modem has')]);
		this.rsSay = E('p', { 'class': 'mm-faint mm-rs-say' });
		this.rsApply = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-rs-apply', click: mm.busy(function() { return self.radioApply(false); }) }, _('Apply'));
		this.rsSure = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-rs-sure', hidden: '', click: mm.busy(function() { return self.radioApply(true); }) },
			_('Yes, apply'));
		this.rsCancel = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-rs-cancel', click: mm.busy(function() { return self.radioEditClose(); }) }, _('Cancel'));
		this.rsForm = E('div', { 'class': 'mm-rs-form', hidden: '' }, [
			E('div', { 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Technologies')), this.rsModes]),
			this.rsPrefRow = E('label', { 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Priority')), this.rsPref]),
			E('div', { 'class': 'mm-field' }, [E('span', { 'class': 'mm-dim' }, _('Bands')), this.rsAnyLabel, this.rsBandsBox]),
			this.rsSay, E('div', { 'class': 'mm-chips' }, [this.rsApply, this.rsSure, this.rsCancel])]);
		this.rsRO = E('p', { 'class': 'mm-faint mm-rs-ro', hidden: '' });
		return E('div', { 'class': 'mm-rs-edit' }, [this.rsOpen, this.rsForm, this.rsRO]);
	},

	radioEditOpen: function() {
		var rs = this.rsLast || {}, self = this, modes = rs.modes || {}, combos = list(rs.supported_modes);
		var techs = ['2g', '3g', '4g', '5g'].filter(function(t) {
			return combos.length ? combos.some(function(c) { return list(c.allowed).indexOf(t) >= 0; }) :
				t !== '2g' || list(modes.allowed).indexOf('2g') >= 0;
		});
		this.rsModes.textContent = '';
		techs.forEach(function(t) {
			var box = E('input', { type: 'checkbox', value: t, change: function() { self.radioEditCheck(); } });
			box.checked = list(modes.allowed).indexOf(t) >= 0 || list(modes.allowed).indexOf('any') >= 0;
			self.rsModes.appendChild(E('label', { 'class': 'mm-check' }, [box, ' ' + (MODE_WORDS[t] || t)]));
		});
		/* the preferred technology: ModemManager only, from the combinations it offers */
		this.rsPrefRow.hidden = rs.via !== 'modemmanager' || !combos.some(function(c) { return c.preferred && c.preferred !== 'none'; });
		this.rsPref.textContent = '';
		this.rsPref.appendChild(E('option', { value: '' }, _('none')));
		techs.forEach(function(t) { self.rsPref.appendChild(E('option', { value: t }, MODE_WORDS[t] || t)); });
		this.rsPref.value = modes.preferred && modes.preferred !== 'none' ? modes.preferred : '';
		var known = list(rs.supported_bands).length ? list(rs.supported_bands) : list(rs.bands);
		this.rsBandsBox.textContent = '';
		known.forEach(function(b) {
			var box = E('input', { type: 'checkbox', value: b, change: function() { self.radioEditCheck(); } });
			box.checked = rs.bands_any || list(rs.bands).indexOf(b) >= 0;
			self.rsBandsBox.appendChild(E('label', { 'class': 'mm-check' }, [box, ' ' + bandsText([b]).replace(/^[^:]*:\s*/, '')]));
		});
		/* «all bands» where the owner can write it: ModemManager, or QModem / the Foxconn AT dialects that listed them */
		this.rsAnyLabel.hidden = !(rs.via === 'modemmanager' || ((rs.via === 'qmodem' || (rs.via === 'at' && /^(foxconn-|fibocom-mtk$)/.test(rs.dialect || ''))) &&
			list(rs.supported_bands).length));
		this.rsAny.checked = !!rs.bands_any;
		this.rsForm.hidden = false;
		this.rsOpen.hidden = true;
		this.rsSure.hidden = true;
		this.rsApply.hidden = false;
		this.radioEditCheck();
	},

	radioEditClose: function() {
		this.rsForm.hidden = true;
		this.rsOpen.hidden = false;
	},

	radioEditWant: function() {
		var modes = [].slice.call(this.rsModes.querySelectorAll('input:checked')).map(function(b) { return b.value; });
		var bands = this.rsAny.checked && !this.rsAnyLabel.hidden ? 'any' :
			[].slice.call(this.rsBandsBox.querySelectorAll('input:checked')).map(function(b) { return b.value; });
		var want = { modes: { allowed: modes } };
		if (!this.rsPrefRow.hidden) want.modes.preferred = this.rsPref.value || null;
		if (bands === 'any' || this.rsBandsBox.children.length) want.bands = bands;
		return want;
	},

	radioEditCheck: function() {
		var w = this.radioEditWant(), say = '';
		this.rsBandsBox.classList.toggle('mm-off', w.bands === 'any');
		if (!w.modes.allowed.length) say = _('Mark at least one technology.');
		else if (w.modes.preferred && (w.modes.allowed.indexOf(w.modes.preferred) < 0 || w.modes.allowed.length < 2)) say = _('A priority can be set only when two technologies or more are marked, and only for one of them.');
		else if (Array.isArray(w.bands) && !w.bands.length) say = _('Mark at least one band.');
		text(this.rsSay, say || _('The modem re-registers in the network: the link through it is gone for 10–60 seconds. If the internet through the modem does not come back in 2 minutes, the previous settings come back by themselves.'));
		this.rsApply.disabled = !!say;
		this.rsSure.hidden = true;
		this.rsApply.hidden = false;
	},

	/* two clicks: «Применить», then «Да, применить» */
	radioApply: function(sure) {
		var self = this, w = this.radioEditWant();
		if (!sure) {
			this.rsApply.hidden = true;
			this.rsSure.hidden = false;
			return Promise.resolve();
		}
		this.rsSure.disabled = true;
		return setRadio(this.id, w.modes, w.bands, null).then(function(r) {
			self.rsError = r.refused || r.error ? _('not changed: %s').format(why(r.refused || r.error)) : null;
			if (!self.rsError) self.radioEditClose();
			if (r.radio_settings) self.applyRadioSettings(r.radio_settings);
		}).catch(function(e) { self.rsError = _('not changed: %s').format(failText(e)); }).then(function() {
			self.rsSure.disabled = false;
			return self.update ? self.update() : null;
		});
	},

	/* «Lock» beside a cell: the modem stays on it (or «Remove»: picks its cells again); a change with its way back */
	canLock: function(c) {
		var rs = this.rsLast || {}, rat = c.type === '5gnr' ? 'nr' : c.type === 'lte' ? 'lte' : null, ch = rat === 'nr' ? c.nrarfcn : c.earfcn;
		var st = rs.change && rs.change.state;
		return !!(rat && this.present && rs.writable && list(rs.cell_lock_rats).indexOf(rat) >= 0 && ch != null && ch !== '' &&
			(c.pci != null && c.pci !== '' || rs.dialect === 'fibocom-mtk') && ['applying', 'waiting', 'rolling_back'].indexOf(st) < 0);
	},

	lockCell: function(c) {
		var self = this, rs = this.rsLast || {}, restart = rs.dialect === 'foxconn-sdx', lock = 'none';
		if (c) {
			lock = { rat: c.type === '5gnr' ? 'nr' : 'lte', arfcn: Number(c.type === '5gnr' ? c.nrarfcn : c.earfcn) };
			if (c.pci != null && c.pci !== '') lock.pci = Number(c.pci);
		}
		var ask = (c ? _('Lock the modem to the cell %s?').format(lockText(lock)) + ' ' +
			_('The modem stays on it even when another cell is better, until the lock is removed.') :
			_('Remove the cell lock? The modem picks its cells itself again.')) + ' ' +
			(restart ? _('The modem restarts to take it: the link through it is gone for about a minute.') :
				_('The modem goes through flight mode: the link through it is gone for 10–60 seconds.')) + ' ' +
			_('If the internet through the modem does not come back, the previous setting comes back by itself.');
		if (!window.confirm(ask)) return Promise.resolve();
		var id = this.id, said = function(line, tone) { self.say(line); mm.toast('lock', line, tone, tone === 'err' ? 12000 : 8000); };
		said(c ? _('Locking the modem to the cell…') : _('Removing the cell lock…'), 'run');
		/* the engine takes a change only from a reading under 10 minutes old, and a T99's lock only from its AT
		 * reading: a page open longer was refused with «read the settings first», said where nobody looked (the
		 * owner's T99, 2026-11-03). Read again first, then write */
		return readRadio(id, true).then(function(rr) {
			if (rr && rr.radio_settings) self.applyRadioSettings(rr.radio_settings);
		}, function() { /* the write says what is wrong */ }).then(function() {
			return setRadio(id, undefined, undefined, undefined, lock);
		}).then(function(r) {
			r = r || {};
			self.rsError = r.refused || r.error ? _('not changed: %s').format(why(r.refused || r.error)) : null;
			if (self.rsError) said(_('The cell lock is %s').format(self.rsError), 'err');
			else said((c ? _('The lock is written to the modem.') : _('The lock removal is written to the modem.')) + ' ' +
				(restart ? _('The modem restarts to take it; the result is shown here when it is back.') : _('The result is shown here when the link is checked.')), 'run');
			if (r.radio_settings) self.applyRadioSettings(r.radio_settings);
		}).catch(function(e) { self.rsError = _('not changed: %s').format(failText(e)); said(self.rsError, 'err'); }).then(function() {
			return self.update ? self.update() : null;
		});
	},

	lockButton: function(c) {
		if (!this.canLock(c)) return '';
		var rs = this.rsLast || {}, cl = rs.cell_lock, ch = c.type === '5gnr' ? c.nrarfcn : c.earfcn;
		if (cl && (!cl.count || cl.count === 1) && Number(cl.arfcn) === Number(ch) && (cl.pci == null || String(cl.pci) === String(c.pci)))
			return E('span', { 'class': 'mm-tag mm-locked' }, _('locked'));
		return E('button', { type: 'button', 'class': 'mm-link mm-lock', click: mm.busy(this.lockCell.bind(this, c)) }, _('Lock'));
	},

	applyRadioChange: function(rs) {
		var ch = rs.change, st = ch && ch.state, words_ = {
			applying: _('The change is being applied…'),
			waiting: _('The change is applied. Waiting whether the internet comes back through the modem') + (typeof ch === 'object' && typeof ch.left_s === 'number' ? ': ' + _('%d s more').format(ch.left_s) : '') + '. ' + _('If it does not, the previous settings come back by themselves.'),
			applied: _('The change is applied. There was no link through the modem before it either: nothing to check.'),
			kept: _('The change is kept.'),
			rolling_back: _('Putting the previous settings back…'),
			rolled_back: _('The previous settings are back') + (ch && ch.why ? ': ' + why(ch.why) : '') + '.',
			failed: _('The change failed') + (ch && ch.why ? ': ' + why(ch.why) : '') + '.',
			rollback_failed: _('The previous settings could not be restored') + (ch && ch.why ? ': ' + why(ch.why) : '') + '. ' + _('Check the modem\'s settings by hand.')
		};
		this.rsChange.hidden = !st;
		text(this.rsChText, st ? words_[st] || st : '');
		/* the end of a change said where the operator is, not only in the card (the owner, 2026-11-03: «нет
		 * всплывающих сообщений»), and the settings read again: a T99's lock shows only after its restart */
		var done = { kept: 'ok', applied: 'ok', rolled_back: 'warn', failed: 'err', rollback_failed: 'err' };
		if (this.rsChangeState && this.rsChangeState !== st && done[st]) {
			mm.toast('lock', words_[st], done[st], done[st] === 'ok' ? 8000 : 15000);
			if (this.present) this.readRadioSettings(true);
		}
		this.rsChangeState = st;
		this.rsChange.className = 'mm-callout mm-rs-change' + (st === 'rollback_failed' || st === 'failed' ? ' is-err' : st === 'waiting' || st === 'rolling_back' ? ' is-warn' : '');
		this.rsKeep.hidden = !(st === 'waiting' || st === 'applied');
		this.rsBack.hidden = !(st === 'waiting' || st === 'applied' || st === 'kept');
		var busy = st === 'applying' || st === 'waiting' || st === 'rolling_back';
		this.rsOpen.hidden = !rs.writable || !!rs.cell_lock_only || !this.rsForm.hidden;
		this.rsOpen.disabled = busy || !this.present;
		text(this.rsRO, rs.cell_lock_only && rs.writable ? _('Only cell locking is available on EP06-E. Network modes and band masks are read-only.') :
			(rs.writable === false && rs.via !== 'none' && rs.why_read_only ? _('The change is unavailable: %s.').format(why(rs.why_read_only)) : ''));
		this.rsRO.hidden = !this.rsRO.textContent;
		if ((!rs.writable || rs.cell_lock_only) && !this.rsForm.hidden) this.radioEditClose();
	},

	readRadioSettings: function(refresh) {
		var self = this;
		this.rsReading = true;
		this.rsRead.disabled = true;
		return readRadio(this.id, refresh).then(function(r) {
			self.rsError = r.refused || r.error ? _('could not read: %s').format(why(r.refused || r.error)) : null;
			self.rsReading = false;
			self.applyRadioSettings(r.radio_settings || self.rsLast || {});
		}).catch(function(e) {
			self.rsError = _('could not read: %s').format(failText(e));
			self.rsReading = false;
			self.applyRadioSettings(self.rsLast || {});
		});
	},

	applyCells: function(mm, present) {
		var cells = present ? list(mm.cells) : [], serving = cells.filter(function(c) { return c.serving; }),
			neigh = cells.filter(function(c) { return !c.serving; }), r = this.refs, loc = mm.location || {}, self = this;
		/* LTE carriers from QMI's carrier aggregation info when the engine has it (ModemManager's cell info carries the
		 * primary only), measured by ModemManager where the same cell is in its list; 5G from ModemManager. An FM350's
		 * carriers (AT+GTCAINFO?) name their kind, a 5G one among them */
		var ca = list((present && mm.ca || {}).cells).filter(function(c) { return c && (c.role === 'primary' || c.role === 'secondary'); });
		if (ca.length) {
			/* ModemManager's measurement first, then the AT reading's (an SDX55's secondaries, step 3) */
			var atNow = this.atFresh(), measured = serving.concat(atNow && list(mm.cells) !== atNow.cells ?
				list(atNow.cells).filter(function(c) { return c.serving; }) : []), caNr = ca.some(function(c) { return c.type === '5gnr'; });
			serving = ca.map(function(c) {
				/* by cell and channel; an NR carrier (an FM350's NSA leg, step 4) by its ARFCN: +GTCCINFO?'s serving row */
				var nr = c.type === '5gnr', m = measured.filter(function(x) { return (x.type === '5gnr') === nr && String(x.pci) === String(c.pci) &&
					(nr ? x.nrarfcn : x.earfcn) === c.earfcn; })[0] ||
					(c.role === 'primary' && !nr ? measured.filter(function(x) { return x.type === 'lte' && (x.role === 'primary' || x.role === 'serving'); })[0] : null) || {};
				/* a source that measures each carrier itself (AT^DEBUG?, +GTCCINFO?, +QCAINFO) wins over ModemManager's match */
				var own = {};
				['rsrp', 'rsrq', 'rssi', 'sinr', 'mimo_layers', 'modulation'].forEach(function(k) { if (c[k] != null) own[k] = c[k]; });
				return Object.assign({}, m, own, { nrarfcn: nr ? c.earfcn : undefined, type: c.type || 'lte', serving: true, role: c.role, band: c.band || m.band, earfcn: nr ? undefined : c.earfcn,
					pci: c.pci != null ? String(c.pci) : m.pci, bandwidth: c.bandwidth_mhz || m.bandwidth, state: c.state });
			}).concat(serving.filter(function(c) { return c.type !== 'lte' && !(caNr && c.type === '5gnr'); }));
		}
		var off = function(c) { return c.state === 'deactivated' || c.state === 'deconfigured'; };
		var allowed = list(mm.current_bands).map(function(b) { return String(b).toLowerCase(); });
		serving = serving.map(function(c) { return Object.assign({}, c, { doubt: bandDoubt(c, allowed) }); });
		var doubt = serving.filter(function(c) { return c.doubt; });
		this.caDoubt.hidden = !doubt.length;
		this.caDoubt.replaceChildren.apply(this.caDoubt, doubt.length ? [
			E('strong', {}, _('The band %s does not match the modem\'s settings').format(doubt.map(bandName).join(', '))),
			E('span', {}, _('The band is computed from the channel number (EARFCN), and the modem says only %s are allowed. The error may be in the modem\'s firmware or in MedveModem: please send the output of mmcli -m %s --get-cell-info and mmcli -m %s.')
				.format(allowedText(allowed, doubt[0].type), mm.index != null ? mm.index : 'N', mm.index != null ? mm.index : 'N'))] : []);
		var on = serving.filter(function(c) { return !off(c); }), idle = serving.filter(off);
		var lteCount = on.filter(function(c) { return c.type === 'lte'; }).length;
		text(r.r_band, serving.length ? on.map(bandName).join(' + ') + (lteCount > 1 ? ' (' + _('aggregation of %d carriers').format(lteCount) + ')' : '') +
			(idle.length ? '; ' + _('set up, but not used now: %s').format(idle.map(bandName).join(', ')) : '') : '');
		text(r.r_plmn, loc.mcc && loc.mnc ? loc.mcc + '-' + loc.mnc : words(mm.operator_code));
		secret(r.r_cell, loc.tac || loc.lac || loc.cid ? [loc.tac || loc.lac, loc.cid].filter(Boolean).join(' / ') : '');
		var pcell = serving.filter(function(c) { return c.role === 'primary' || c.role === 'serving'; })[0] || serving[0];
		var en = pcell && pcell.type === 'lte' ? enbOf(pcell.ci || loc.cid) : null;
		secret(r.r_enb, en ? _('eNB %s, sector %s').format(en.enb, en.sector) + (pcell.pci ? ' · PCI ' + pcell.pci : '') : '');
		text(r.r_dist, taDistance(pcell));
		/* the map knows where the tower stands; it is an identifier, so «hide the identifiers» hides it too */
		var plmn = String((pcell && pcell.plmn) || ''), mapUrl = cellMapUrl(loc.mcc || plmn.slice(0, 3), loc.mnc || plmn.slice(3),
			pcell && pcell.type, (pcell && pcell.ci) || loc.cid, loc.tac || loc.lac || (pcell && (pcell.tac || pcell.lac)));
		if (!mapUrl) text(r.r_map, '');
		else if (privacy()) text(r.r_map, _('hidden'));
		else if (!r.r_map.firstChild || r.r_map.firstChild.href !== mapUrl)
			r.r_map.replaceChildren(E('a', { 'class': 'mm-cell-map', href: mapUrl, target: '_blank', rel: 'noopener noreferrer' }, '📍 ' + _('Open on 4cells.ru')));
		this.caCard.hidden = !serving.length;
		var total = on.reduce(function(a, c) { return a + (bwMhz(c.bandwidth) || 0); }, 0);
		text(this.caTotal, total ? _('%s MHz in all').format(mhzText(total)) : '');
		this.caTotal.hidden = !total;
		text(this.caGist, on.map(bandName).join('+') + (total ? ' · ' + _('%s MHz').format(mhzText(total)) : ''));
		/* textContent, not text(): nothing is nothing here, not «—» after the signal words (the owner, 2026-10-20) */
		this.hero.ca.textContent = this.level === 'expert' && on.length > 1 ? ' · CA ' + on.map(bandName).join('+') + (total ? ', ' + _('%s MHz').format(mhzText(total)) : '') : '';
		this.caBar.setAttribute('aria-label', _('Carriers: %s').format(serving.map(bandName).join(', ')));
		this.caBar.replaceChildren.apply(this.caBar, serving.map(function(c) {
			var bw = bwMhz(c.bandwidth);
			return E('div', { 'class': 'mm-ca-seg' + (c.role === 'primary' || (serving.length === 1) ? ' is-pcc' : '') + (c.type === '5gnr' ? ' is-nr' : '') + (off(c) ? ' is-off' : ''),
				style: 'flex:' + (bw || 10), title: (CELL_ROLE[c.role] || '') + ' ' + bandName(c) + (bw ? ', ' + _('%s MHz').format(mhzText(bw)) : '') + (off(c) ? ', ' + _('not used now') : '') },
				[E('b', {}, bandName(c)), bw ? E('span', { 'class': 'mm-mono mm-faint' }, _('%s MHz').format(mhzText(bw))) : '']);
		}));
		this.carrierList.replaceChildren.apply(this.carrierList, serving.map(function(c) {
			var ch = c.earfcn != null ? 'EARFCN ' + c.earfcn : c.nrarfcn != null ? 'ARFCN ' + c.nrarfcn : '', f = bandMhz(c);
			return E('div', { 'class': 'mm-carrier' + (off(c) ? ' is-off' : '') }, [
				E('div', { 'class': 'mm-carrier-h' }, [E('b', {}, [bandName(c) + (f ? ' (' + _('%s MHz').format(mhzText(f)) + ')' : '') + ' ', E('span', { 'class': 'mm-faint' },
					(CELL_ROLE[c.role] || '') + (off(c) ? _(', set up, not used') : ''))]),
					E('span', { 'class': 'mm-mono mm-faint' }, [c.pci ? 'PCI ' + c.pci : '', self.level !== 'normal' && ch ? ' · ' + ch + (channelText(c) ? ' (' + channelText(c) + ')' : '') : '',
						self.level !== 'normal' && typeof c.mimo_layers === 'number' ? ' · MIMO ' + c.mimo_layers : '',
						self.level !== 'normal' && /^[0-9A-Z]{3,8}$/.test(c.modulation || '') ? ' · ' + c.modulation : ''].join('').replace(/^ · /, '')),
					c.role === 'primary' || c.role === 'serving' || serving.length === 1 ? self.lockButton(c) : ''])
			].concat([['rsrp', c.rsrp], ['rsrq', c.rsrq], ['sinr', c.sinr]].filter(function(x) { return typeof x[1] === 'number'; })
				.map(function(x) { return meter(x[0], x[1], ratOf(c.type)); }))
				.concat(self.level !== 'normal' && typeof c.rssi === 'number' ? [E('div', { 'class': 'mm-faint mm-mono' }, 'RSSI ' + _('%s dBm').format(numText(c.rssi)))] : []));
		}));
		/* with the carriers drawn, the per-technology cards would say the same twice */
		this.radioCards.lte.hidden = this.radioCards.lte.hidden || serving.length > 0;
		this.radioCards.nr.hidden = this.radioCards.nr.hidden || serving.length > 0;
		this.neighCard.hidden = !neigh.length || this.level === 'normal';
		text(this.neighGist, N_(neigh.length, '%d cell', '%d cells').format(neigh.length));
		this.neighBody.replaceChildren.apply(this.neighBody, neigh.slice().sort(function(a, b) { return (b.rsrp || -999) - (a.rsrp || -999); }).slice(0, 16).map(function(c) {
			var z = typeof c.rsrp === 'number' ? zone('rsrp', c.rsrp, ratOf(c.type)) : null;
			return E('tr', {}, [E('td', { 'class': 'mm-mono' }, bandName(c)), E('td', { 'class': 'num' }, c.pci || '—'),
				E('td', { 'class': 'num' }, c.earfcn != null ? String(c.earfcn) : c.nrarfcn != null ? String(c.nrarfcn) : '—'),
				E('td', { 'class': 'num' + (z != null ? ' mm-q-word z' + z : '') }, typeof c.rsrp === 'number' ? numText(c.rsrp) : '—'),
				E('td', { 'class': 'num' }, typeof c.rsrq === 'number' ? numText(c.rsrq) : '—'), E('td', {}, self.lockButton(c))]);
		}));
	},

	/* receive chains from QMI (NAS tx-rx info, read through the proxy beside ModemManager, every 30 s) */
	applyAntennas: function(mm, present) {
		var ant = present && mm.antennas && typeof mm.antennas === 'object' ? mm.antennas : {}, self = this;
		/* no chains from QMI: a T77's AT^DEBUG? gives them with the primary carrier (rx_diversity, spec 26.1 step 2) */
		var atp = list(mm.cells).filter(function(c) { return c.serving && c.role === 'primary' && Array.isArray(c.chains); })[0];
		if (present && atp && !list((ant.lte || {}).chains).length) ant = Object.assign({}, ant, { lte: { chains: atp.chains }, why: null });
		var groups = [['lte', 'LTE'], ['nr', '5G']].map(function(g) {
			var chains = list((ant[g[0]] || {}).chains).filter(function(c) { return typeof c.chain === 'number'; });
			return { kind: g[0], name: g[1], chains: chains, tx: (ant[g[0]] || {}).tx,
				used: chains.filter(chainLive) };
		}).filter(function(g) { return g.used.length; });
		var why = { 'no-qmicli': _('To see the antennas, install the qmi-utils package (the qmicli program).'),
			'no-control-port': _('The modem gave no QMI or MBIM channel the antennas are read through.'),
			'failed': _('The modem did not answer the antenna request.') }[ant.why] || '';
		this.antCard.hidden = !groups.length && !(why && this.level === 'expert');
		/* not a request for a utility without the way to it (the owner, 2026-10-20): the install is one click away */
		this.antWhy.replaceChildren(groups.length ? '' : why, !groups.length && ant.why === 'no-qmicli' ? ' ' : '',
			!groups.length && ant.why === 'no-qmicli' ? E('a', { href: L.url('admin/modem/medvemodem/settings') + '#components' }, _('Install: Settings → Drivers and ModemManager')) : '');
		this.antWhy.hidden = !!groups.length || !why;
		var weak = null, spread = 0;
		groups.forEach(function(g) {
			var vals = g.used.map(function(c) { return c.rsrp; }), best = Math.max.apply(null, vals);
			g.best = best;
			spread = Math.max(spread, best - Math.min.apply(null, vals));
			g.used.forEach(function(c) { if (best - c.rsrp >= 10 && (!weak || best - c.rsrp > weak.gap)) weak = { g: g, c: c, gap: best - c.rsrp }; });
		});
		this.antBody.replaceChildren.apply(this.antBody, groups.map(function(g) {
			/* the phase only when a chain reports one (bench 2026-10-15: SDX55 says 0° on every chain, which tells nothing) */
			var phased = g.chains.some(function(c) { return typeof c.phase === 'number' && c.phase !== 0; });
			return E('div', { 'class': 'mm-ant-group' }, [groups.length > 1 ? E('div', { 'class': 'mm-dim mm-ant-h' }, g.name) : '',
				E('div', { 'class': 'mm-ant', role: 'list' }, g.chains.map(function(c) {
					var on = chainLive(c), isWeak = on && g.best - c.rsrp >= 10;
					var title = _('Receive chain %s').format(c.chain) + ': ' + (on ? _('RSRP %s dBm').format(numText(c.rsrp)) + (typeof c.power === 'number' ? ', RSSI ' + _('%s dBm').format(numText(c.power)) : '') : _('not used now'));
					return E('div', { 'class': 'mm-ant-col' + (isWeak ? ' is-weak' : '') + (on ? '' : ' is-off'), role: 'listitem', title: title }, [
						E('div', { 'class': 'mm-ant-bar' }, on ? E('i', { style: 'height:' + pct('rsrp', c.rsrp) + '%' }) : ''),
						E('span', { 'class': 'mm-mono' }, on ? numText(c.rsrp) : _('unused')),
						E('span', { 'class': 'mm-faint' }, 'RX' + c.chain),
						self.level !== 'normal' && on && typeof c.power === 'number' ? E('span', { 'class': 'mm-faint mm-mono mm-ant-sub' }, 'RSSI ' + numText(c.power)) : '',
						self.level === 'expert' && on && phased && typeof c.phase === 'number' ? E('span', { 'class': 'mm-faint mm-mono mm-ant-sub' }, numText(c.phase) + '°') : ''
					]);
				}))]);
		}));
		var several = groups.some(function(g) { return g.used.length > 1; });
		this.antCall.hidden = !several;
		this.antCall.className = 'mm-callout' + (weak ? '' : ' is-ok');
		/* a chain at the floor hears nothing: a cable off, not a weaker antenna (-130 dBm and below) */
		var dead = weak && weak.c.rsrp <= -130;
		this.antCall.replaceChildren.apply(this.antCall, !several ? [] : dead ? [
			E('strong', {}, (groups.length > 1 ? weak.g.name + ': ' : '') + _('RX%s receives almost nothing: the antenna seems not connected').format(weak.c.chain)),
			E('span', {}, _('Check that this antenna\'s cable is in the modem\'s socket and not broken.'))] : weak ? [
			E('strong', {}, (groups.length > 1 ? weak.g.name + ': ' : '') + _('RX%s is %s dB weaker than the best antenna').format(weak.c.chain, numText(weak.gap))),
			E('span', {}, _('Check this antenna\'s cable and socket. If the antenna is external, its direction and mounting.'))] : [
			E('strong', {}, _('The antennas are balanced')), E('span', {}, _('Spread %s dB.').format(numText(spread)))]);
		text(this.antGist, !groups.length ? '' : weak ? _('RX%s is weaker').format(weak.c.chain) : several ? _('balanced') :
			g1(groups[0].used.length));
		function g1(n) { return N_(n, '%d chain', '%d chains').format(n); }
		var tx = groups.map(function(g) { return g.tx; }).filter(function(t) { return t && typeof t.power === 'number'; })[0];
		text(this.antTx, tx ? _('Transmit power: %s dBm. A high value may point to a hard uplink from the modem to the base station.').format(numText(tx.power)) : '');
		this.antTx.hidden = !tx || this.level !== 'expert';
	},

	/* the running step: what was done, for how long, what it waits for */
	applyProgress: function(p, level) {
		var box = this.progress, done = !p;
		/* the last run's steps stay until the page is reloaded (the owner, 2026-11-04: «не убирать результаты процесса
		 * после попытки, где галочки») */
		if (p) this.lastProgress = { p: p, level: level };
		else if (this.lastProgress) { p = this.lastProgress.p; level = this.lastProgress.level; }
		box.hidden = !p;
		if (!p) return;
		var stage = (STAGES[level] || [level])[0], steps = list(p.steps);
		var items = steps.map(function(s, i) {
			var fail = s.step === 'failed', run = !done && i === steps.length - 1 && s.step !== 'succeeded' && !fail;
			return E('li', { 'class': fail ? 'fail' : run ? 'run' : 'ok' }, [E('span', { 'class': 'ic' }, fail ? '×' : run ? '…' : '✓'),
				E('span', {}, (STEPS[s.step] || s.step) + (s.detail && s.step !== 'started' ? ' (' + why(s.detail) + ')' : '')),
				E('span', { 'class': 'det mm-t' }, _('%d s').format(Math.round((s.ms || 0) / 1000)))]);
		});
		box.replaceChildren(E('strong', {}, done ? _('The step “%s” has ended; what it did:').format(stage) :
			_('The step “%s” is running: %d s (at most %d s)').format(stage, p.elapsed_s, p.limit_s)),
			E('ul', { 'class': 'mm-steps' }, items),
			p.waiting ? E('span', { 'class': 'mm-faint' }, _('A condition is not met: %s').format(why(p.waiting))) : '');
	},

	/* asked once per page, and only for a model whose profile prefers QModem */
	applyQmodem: function(d) {
		var self = this;
		if (((d.profile || {}).prefer_owner) !== 'qmodem' || this.qmodemAsked) return;
		this.qmodemAsked = true;
		fs.exec('/usr/libexec/medvemodem/qmodem-install', ['status']).then(function(r) {
			var j = {};
			try { j = JSON.parse((r && r.stdout) || '{}'); } catch (e) { /* no word, no note */ }
			bannerShow(self.qmodemNote, !!(j.ok && !j.installed && j.supported && j.source));
		}, function() {});
	},

	/* what changed since the last look, said once, with the time (modem gone/back, internet lost/back) */
	applyHeld: function(d) {
		var qmodemOwns = (d.owner || {}).control === 'qmodem';
		/* tom_modem is QModem's own AT worker. It is a legitimate holder when QModem is the modem owner;
		 * preserve the port lock itself, but do not warn that the operator installed a foreign SMS service. */
		var held = list(((d.port_roles || {}).candidates)).filter(function(c) {
			return typeof c.held_by === 'string' && !(qmodemOwns && /^tom_modem(?:\s|\(|$)/.test(c.held_by));
		});
		if (!held.length) { this.heldNote.hidden = true; return; }
		var ports = held.map(function(c) { return String(c.node).replace(/^\/dev\//, '') + ' (' + c.held_by + ')'; }).join(', ');
		text(this.heldText, (held.length > 1 ? _('Ports %s are busy with other programs') : _('Port %s is busy with another program')).format(ports) + '. ' +
			_('MedveModem does not interfere: the AT console, SMS and USSD through this port are unavailable while the program holds it. If it is an old SMS service (smstools3 and the like), stop it so the port passes to MedveModem.'));
		bannerShow(this.heldNote, true);
	},

	notice: function(d, connected) {
		var prev = this.prevState, now = { present: d.present === true, connected: connected, ok: !!((d.uplink || {}).reach || {}).ok };
		this.prevState = now;
		if (!prev) return;
		var lines = [];
		if (prev.present && !now.present) lines.push([_('Modem disconnected from USB'), 'warn']);
		if (!prev.present && now.present) lines.push([_('Modem connected to USB'), 'ok']);
		if (prev.connected && !now.connected) lines.push([_('The connection through the modem is lost'), 'warn']);
		if (!prev.connected && now.connected) lines.push([_('The connection through the modem is restored'), 'ok']);
		if (prev.connected && now.connected && prev.ok && !now.ok) lines.push([_('The internet access check failed'), 'warn']);
		var t = new Date().toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit', second: '2-digit' }), box = this.notices;
		lines.forEach(function(l) {
			var n = E('div', { 'class': 'mm-callout mm-row mm-' + l[1] + (l[1] === 'ok' ? ' is-ok' : '') }, [E('span', { 'class': 'mm-t mm-mono' }, t), E('span', {}, l[0]),
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-notice-x', 'aria-label': _('Hide'), click: function() { n.remove(); } }, '×')]);
			box.insertBefore(n, box.firstChild);
		});
		while (box.children.length > 5) box.lastChild.remove();
	},

	/* the journal (newest first) and the mockup's 24-hour strip: one mark per event, coloured by its kind */
	applyJournal: function(events) {
		this.lastEvents = events;
		var ev = list(events).slice().sort(function(a, b) { return (b.t || 0) - (a.t || 0); }), now = Date.now() / 1000, W = 480, H = 22, NS = 'http://www.w3.org/2000/svg';
		/* one line for a burst of the same event (a driver binds each interface of the modem in the same second) */
		var rows = [];
		ev.forEach(function(e) {
			var w = eventWords(e), last = rows[rows.length - 1];
			if (last && last.w[0] === w[0] && Math.abs(last.e.t - e.t) <= 2) last.n++;
			else rows.push({ e: e, w: w, n: 1 });
		});
		var self = this;
		/* the engine's own record under the words at «Подробный» and «Эксперт» (the owner, 2026-10-31: «в детальном режиме
		 * подробностей из лога»): its kind and detail as the log has them */
		var raw = this.level !== 'normal' ? function(e) { return e.kind + (e.detail ? ' · ' + e.detail : '') + (e.rebind ? ' · rebind' : '') + (e.requested ? ' · requested' : ''); } : null;
		/* a line as copied: the full date, the kind, the words */
		var line = function(r) {
			var w = r.n > 1 ? r.w[0] + ' ×' + r.n : r.w[0];
			return new Date(r.e.t * 1000).toLocaleString(LANG) + ' · ' + kindOf(r.e.kind)[1] + ' · ' + w + (raw ? ' [' + raw(r.e) + ']' : '');
		};
		this.journalText = rows.map(line);
		/* 30 lines, then «Show earlier» adds 30 at a time (the owner, 2026-10-31: «ушедшие за сутки события как пролистать?») */
		this.journalShown = Math.max(30, this.journalShown || 30);
		var items = rows.slice(0, this.journalShown).map(function(r) {
			var e = r.e, w = r.n > 1 ? [r.w[0] + ' ×' + r.n, r.w[1]] : r.w, k = kindOf(e.kind), age = now - e.t;
			return E('li', { 'class': w[1] ? 'mm-' + w[1] : '' }, [
				E('span', { 'class': 't' }, [E('time', { 'class': 'mm-t' }, clock(e.t)), E('span', { 'class': 'mm-faint' }, age >= 0 ? agoLong(age) : '')]),
				E('span', { 'class': 'k ' + k[0] }, k[1]), E('span', { 'class': 'd' }, [w[0], ' ',
					E('button', { type: 'button', 'class': 'mm-copy', title: _('Copy the line'), 'aria-label': _('Copy the line'), click: function() {
						copyText(line(r)).then(function() { self.say(_('The line is copied.')); },
							function() { self.say(_('The browser did not let copy: select the line and copy it by hand.')); });
					} }, '⧉')].concat(raw ? [E('div', { 'class': 'mm-faint mm-mono mm-ev-raw' }, raw(e))] : []))]);
		});
		if (rows.length > this.journalShown)
			items.push(E('li', { 'class': 'mm-journal-more' }, [E('span', { 'class': 't' }), E('span'), E('span', { 'class': 'd' },
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-journal-older', click: function() {
					self.journalShown += 30;
					self.applyJournal(self.lastEvents);
				} }, N_(rows.length - this.journalShown, 'Show earlier (%d more)', 'Show earlier (%d more)').format(rows.length - this.journalShown)))]));
		/* what the journal reaches back to: it lives in the router's memory, a router restart begins it anew */
		if (rows.length && rows.length <= this.journalShown)
			items.push(E('li', { 'class': 'mm-journal-end mm-muted' }, [E('span', { 'class': 't' }), E('span'), E('span', { 'class': 'd mm-faint' },
				_('Nothing earlier: the journal keeps the modem\'s last 200 events in the router\'s memory, and a router restart begins it anew.'))]));
		/* the banners closed with × on this page, newest first, each with «Show again» */
		var hidden = mm.dismissedFor((this.id || '-') + ':').map(function(h) {
			return E('li', { 'class': 'mm-dismissed-row' }, [
				E('span', { 'class': 't' }, [E('time', { 'class': 'mm-t' }, clock(h.at)), E('span', { 'class': 'mm-faint' }, now - h.at >= 0 ? agoLong(now - h.at) : '')]),
				E('span', { 'class': 'k dim' }, _('notice')), E('span', { 'class': 'd' }, [_('Hidden: %s').format(h.text), ' ',
					E('button', { type: 'button', 'class': 'mm-link', click: function() {
						mm.undismiss(h.key);
						if (self.data) self.apply(self.data);
						if (self.qmodemNote && h.key.slice(-7) === ':qmodem') { self.qmodemAsked = false; if (self.data) self.applyQmodem(self.data); }
						self.applyJournal(self.lastEvents);
					} }, _('Show again'))])]);
		});
		items = hidden.concat(items);
		this.journal.replaceChildren.apply(this.journal, items.length ? items : [E('li', { 'class': 'mm-muted' }, [E('span', { 'class': 't' }), E('span'), E('span', { 'class': 'd' }, _('No events.'))])]);
		var day = ev.filter(function(e) { return typeof e.t === 'number' && now - e.t <= 86400 && now - e.t >= 0; });
		var svg = document.createElementNS(NS, 'svg');
		svg.setAttribute('viewBox', '0 0 ' + W + ' ' + H); svg.setAttribute('preserveAspectRatio', 'none');
		svg.setAttribute('role', 'img'); svg.setAttribute('aria-label', _('Events for 24 hours'));
		function el(name, attrs) { var n = document.createElementNS(NS, name); Object.keys(attrs).forEach(function(a) { n.setAttribute(a, attrs[a]); }); svg.appendChild(n); return n; }
		el('rect', { x: 0, y: 10, width: W, height: 2, 'class': 'mm-strip-line' });
		[0, 6, 12, 18, 24].forEach(function(hh) { el('line', { x1: W - hh / 24 * W, x2: W - hh / 24 * W, y1: 0, y2: H, 'class': 'mm-strip-grid' }); });
		day.forEach(function(e) {
			var x = W - (now - e.t) / 86400 * W, r = el('rect', { x: Math.max(0, x - 1.5).toFixed(1), y: 3, width: 3, height: 16, rx: 1.5, 'class': 'mm-strip-m ' + kindOf(e.kind)[0] });
			var t = document.createElementNS(NS, 'title'); t.textContent = clock(e.t) + ' · ' + eventWords(e)[0]; r.appendChild(t);
		});
		var usb = day.filter(function(e) { return e.kind === 'usb-remove' || e.kind === 'removed'; }).length,
			recs = day.filter(function(e) { return e.kind === 'recovery-start'; }).length;
		var gist = !day.length ? _('no events in 24 h') : [usb ? _('USB disconnects: %d').format(usb) : '', recs ? _('recoveries: %d').format(recs) : '',
			!usb && !recs ? _('events: %d').format(day.length) : ''].filter(Boolean).join(' · ');
		text(this.stripGist, gist);
		var hm = function(hh) { return new Date((now - hh * 3600) * 1000).toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit' }); };
		this.strip.replaceChildren(E('div', { 'class': 'mm-q-head' }, [E('span', { 'class': 'mm-dim' }, _('Stability for 24 h')), E('span', { 'class': 'mm-faint' }, gist)]),
			svg, E('div', { 'class': 'mm-q-scale mm-strip-x' }, [24, 18, 12, 6].map(function(hh) { return E('span', {}, hm(hh)); }).concat([E('span', {}, _('now'))])));
	},

	update: function() {
		var self = this;
		if (!this.id) {
			this.error.hidden = false;
			text(this.error, _('Choose a modem on MedveModem\'s “Modems” page.'));
			return Promise.resolve();
		}
		if (this.pending) return this.pending;
		/* a hidden tab asks nothing; the next poll after it shows again does (the owner, 2026-10-19: background refresh) */
		/* a first answer that failed counts too, as on the dashboard (second audit 2026-10-21: a hidden tab of a modem whose
		 * engine did not answer kept asking every 5 s) */
		if (document.hidden && (this.lastSuccess || this.failing)) return Promise.resolve();
		if (!this.lastSuccess && !this.cacheShown) {
			this.cacheShown = true;
			var cached = cacheGet('mm:dev:' + this.id);
			if (cached && cached.device_id === this.id) {
				try { this.applyAll(cached, true); } catch (e) { /* a cache from an older page is only a head start */ }
				/* the journal comes only with a live answer: said, not left blank (the owner, 2026-10-31: «event журнал тоже
				 * пропал или не было») */
				if (this.journal && !this.journal.children.length)
					this.journal.replaceChildren(E('li', { 'class': 'mm-muted' }, [E('span', { 'class': 't' }), E('span'),
						E('span', { 'class': 'd' }, _('The journal comes when the MedveModem service answers.'))]));
			}
		}
		/* the engine's failure apart from the page's own (bench 2026-10-22, r82: «Модем не найден, или служба MedveModem не
		 * отвечает» for seconds while both were fine — a slow first answer or a render error read as both); «not found»
		 * only for the engine's own code, a slow one is said as loading, then as retried */
		this.pending = getDevice(this.id).then(function(d) {
			if (!d || d.device_id !== self.id) throw new Error('Unexpected engine response');
			self.failing = false; self.failCount = 0;
			self.error.hidden = true; self.root.classList.remove('mm-stale');
			cacheSet('mm:dev:' + self.id, d);
			try { self.applyAll(d, false); } catch (e) {
				self.error.hidden = false;
				self.error.className = 'mm-callout is-err mm-notice';
				text(self.error, _('Unable to display modem data: %s').format(failText(e)));
				self.root.classList.remove('mm-wait');
				if (window.console) console.error('MedveModem page:', e);
			}
		}, function(e) {
			var why = String((e && e.message) || e), gone = /code 4\b|Not ?found/i.test(why) && !/timed? ?out/i.test(why);
			self.failing = true;
			self.failCount = (self.failCount || 0) + 1;
			self.error.hidden = false; self.root.classList.add('mm-stale'); self.root.classList.remove('mm-wait');
			self.error.className = 'mm-callout ' + (self.lastSuccess || self.failCount < 3 ? 'is-info' : 'is-err') + ' mm-notice';
			text(self.error, self.lastSuccess ? _('Refresh error. The last received data is shown, retrying…') :
				gone ? _('The modem is not found: the MedveModem service has no such modem (disconnected from USB or removed).') :
				self.cacheShown && cacheGet('mm:dev:' + self.id) ? _('The data of the last visit is shown, refreshing…') :
				self.failCount < 3 ? _('Loading the modem\'s data…') : _('The MedveModem service does not answer, retrying…'));
		}).finally(function() { self.pending = null; });
		return this.pending;
	},

	/* a collapsed card's header says its state and counters in a few words (the owner, 2026-10-19) */
	gist: function(key, line) {
		var n = (this.secs || {})[key];
		if (n && n.mmGist) n.mmGist(line);
	},

	/* a card of rows says its first two filled rows when collapsed (bench 2026-10-19: «Соединение», «Надёжность» and the
	 * others said nothing); the cards with a gist of their own set it after this */
	applyRowGists: function() {
		var lv = this.rowLevel || {};
		(this.rowCards || []).forEach(function(p) {
			var node = p[0], said = p[1].filter(function(k) {
				var row = lv[k];
				return row && !row[0].hidden && row[1].textContent && row[1].textContent !== '—' && !row[1].classList.contains('mm-secret');
			}).slice(0, 2).map(function(k) { return lv[k][1].textContent.trim(); });
			var line = said.join(' · ');
			if (node.mmGist) node.mmGist(line.length > 70 ? line.slice(0, 69) + '…' : line);
		});
	},

	applyGists: function(d) {
		var mm = d.modemmanager || {}, sig = mm.signal || {}, k = d.connection || {}, sess = d.session || {}, rec = d.recovery || {};
		var f = d.fcc_unlock, sms = d.sms || {}, atp = (d.port_roles || {}).at || {};
		function num(v, unit) { return typeof v === 'number' ? numText(v) + ' ' + unit : ''; }
		function radio(x) { return [num(x.rsrp, _('dBm')), typeof x.snr === 'number' ? 'SINR ' + num(x.snr, _('dB')) : ''].filter(Boolean).join(' · '); }
		var STATE = { connected: _('linked'), connecting: _('connecting…'), configuring: _('configuring…'), failed: _('error'), disconnected: _('no link'), idle: _('no link') };
		this.gist('conn', [words(k.interface), d.frozen ? _('management paused') : STATE[sess.state] || '', words(k.apn_in_use || k.apn)].filter(Boolean).join(' · '));
		this.gist('lte', radio(sig.lte || {}));
		this.gist('nr', radio(sig['5g'] || {}));
		this.gist('rec', (REC_STATE[rec.state] || '') + (typeof rec.attempts_1h === 'number' && rec.attempts_1h ? ' · ' + _('in an hour: %d').format(rec.attempts_1h) : ''));
		this.gist('fcc', f ? (f.enabled ? _('switched on') : f.foreign ? _('another handler') : _('switched off')) : '');
		this.gist('freeze', d.frozen ? _('management paused') : _('management not paused'));
		this.gist('sms', (sms.unread | 0) ? _('new: %s').format(sms.unread | 0) : '');
		this.gist('atport', words(atp.node).replace(/^\/dev\//, ''));
	},

	/*
	 * Everything from one device reply. The page opens with this tab's last reply at once (the owner, 2026-10-19:
	 * «иногда долго открывается»), the fresh one replaces it; the journal is asked every 15 s, at once when something
	 * moved or the operator did something.
	 */
	applyAll: function(d, cached) {
		var self = this, now = Date.now();
		this.apply(d);
		this.applyMSW(d);
		this.applyFcc(d);
		this.applyMmlog(d);
		this.applyFreeze(d);
		this.webIdle(d);
		this.applySvc();
		/* from the dashboard's «Нужно ваше действие» (#svc:<id>): once, straight to the card that fixes it */
		if (!cached && !this.svcJumped && wantsRescue() && this.rescue) {
			this.svcJumped = true;
			this.rescue.box.open = true;
			this.loadRescue();
			this.rescue.box.scrollIntoView({ block: 'start' });
		}
		if (!cached && !this.svcJumped && wantsService() && this.alertTarget) { this.svcJumped = true; this.goAlert(); }
		this.applyAtPort(d);
		this.applyRowGists();
		this.applyGists(d);
		if (!cached) this.tempTick();
		/* unread messages (spec 38): counted on the tab until the list is opened */
		var unread = (d.sms || {}).unread | 0, smsTab = this.tabs.sms.tab;
		smsTab.textContent = unread ? 'SMS · ' + unread : 'SMS';
		smsTab.setAttribute('aria-label', unread ? _('SMS, unread: %d').format(unread) : 'SMS');
		this.root.classList.remove('mm-wait');
		if (cached) {
			text(this.headSub, _('The data of the last visit is shown, refreshing…'));
			return;
		}
		this.loadHistory(false);
		this.loadTraffic(false);
		this.lastSuccess = new Date();
		text(this.headSub, _('Updated at %s').format(this.lastSuccess.toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit', second: '2-digit' })));
		/* something moved (a step runs, the session or the attachment changed, a new event): the journal and the status now */
		var key = [(d.recovery || {}).state, (d.session || {}).state, d.present, (d.last_event || {}).t, (d.last_event || {}).kind].join('|');
		if (key !== this.moveKey || (d.recovery || {}).state === 'running') { this.moveKey = key; this.askAgain = true; }
		/* the journal follows; an engine without it leaves the block as it was */
		/* audit M02: one request of each in flight; an answer is applied to the data of now, not of when it was asked,
		 * and only to the modem it was asked for */
		var id = this.id;
		if ((!this.evAt || now - this.evAt >= 15000 || this.askAgain) && !this.evBusy) {
			this.evAt = now; this.evBusy = true;
			getEvents(id, 200).then(function(r) { if (id === self.id) self.applyJournal(r && r.events); }, function() {})
				.finally(function() { self.evBusy = false; });
		}
		if (!this.stBusy) {
			this.stBusy = true;
			getStatus().then(function(st) {
				var now_ = self.data;
				if (id !== self.id || !now_) return;
				self.applyMMR(st, now_); self.applyPkg(st, now_); self.applyOwnerWhy(st, now_);
			}, function() {}).finally(function() { self.stBusy = false; });
		}
		this.askAgain = false;
		if (this.current === 'sms') this.loadSms(false);
	}
});
