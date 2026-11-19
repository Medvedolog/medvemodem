'use strict';
'require baseclass';

/*
 * What the three MedveModem pages share (spec 25.9.8: one shared module and small views):
 * the version, the words for technologies, operators and levels, the page head, the signal
 * bars and the small formatters. One copy: a version or an operator changed in one place.
 */

/* the stylesheet carries the package version: LuCI versions its own scripts, not an app's
 * <link>, and a browser keeps an old dashboard.css after an upgrade (seen on the bench, 0.7.2) */
var VERSION = '0.16.0-alpha1-r10';	/* = the engine's version */

/* a value as a list: the module has its own (a view's list is not in its scope; r26 broke the icon on this) */
function list(v) { return Array.isArray(v) ? v : v == null || v === '' ? [] : [v]; }
/*
 * The page's words (the owner, 2026-10-23: «исходный английский»): English in _() and N_(), the Russian in
 * po/ru/medvemodem.po, LuCI's language picks it (tools/i18n.py keeps the catalog). Numbers and dates follow the same
 * language: LuCI writes it in <html lang>.
 */
var LANG = (typeof document !== 'undefined' && document.documentElement && document.documentElement.lang) || 'en';
var DEC = (1.5).toLocaleString(LANG).charAt(1) === ',' ? ',' : '.';
/* what the router writes for the operator, in the page's language: the install logs (pkg-install, qmodem-install:
 * --lang=ru|en), the forwarded SMS's heading (sms_forward.lang) */
function langCode() { return String(LANG).split(/[-_]/)[0] === 'ru' ? 'ru' : 'en'; }
function langArg() { return '--lang=' + langCode(); }
/* a number with the language's decimal mark: num(2.5) -> «2,5» in Russian; digits fixes the decimals */
function num(v, digits) {
	var s = digits == null ? String(v) : Number(v).toFixed(digits);
	return DEC === ',' ? s.replace('.', ',') : s;
}
/* Russian plural, kept for the views not yet in _(): plural(2, 'сота', 'соты', 'сот') -> 'соты' */
/* the engine's DNS check says what it found in English (the console stays English, the owner 2026-10-17); the page in
 * the page's language */
function dnsDetail(d) {
	return String(d || '')
		.replace(/^(\S+) does not answer as Cloudflare: (.*)$/, function(m0, a, w) {
			return _('%s does not answer as Cloudflare: %s').format(a, w === 'refused' ? _('refusal') : w === 'empty answer' ? _('empty answer') : w); })
		.replace(/^(\S+) answers in (\d+) ms, TCP to it takes (\d+) ms: the answer comes from closer than the server$/, function(m0, a, ms, tcp) {
			return _('%s answers in %s ms, TCP to it takes %s ms: an intermediate DNS answer is possible, a further check is needed').format(a, ms, tcp); })
		.replace(/^(\S+) via (\S+) → (\S+), DoH says (.*)$/, function(m0, n, v, a, t) {
			return _('%s via %s → %s, and DoH says %s').format(n, v, a, t === 'no address' ? _('no address') : t); });
}
/* the engine's reach error (English, for its log) in the page's language; an unknown one as it is */
function reachError(e) {
	e = String(e || '');
	return /^answered in under 4 ms/.test(e) ? _('the modem answered by itself, there is no network behind it (no SIM or no service)') :
		/^an answer that is not TLS/.test(e) ? _('the operator answers instead of the internet: its own page (no money on the SIM or a block)') :
		/^the connection was cut right after it opened/.test(e) ? _('the connection is cut right after it opens: the operator\'s page or a block (no money on the SIM?)') : e;
}
function plural(n, one, few, many) {
	var a = Math.abs(n) % 100, b = a % 10;
	return a > 10 && a < 20 ? many : b === 1 ? one : b >= 2 && b <= 4 ? few : many;
}
function text(node, value) {
	/* «—» stands for a missing value in a field; a status line with nothing to say stays empty and takes no room
	 * (the owner, 2026-11-09: lone dashes under the settings' buttons) */
	var status = node.getAttribute && node.getAttribute('role') === 'status';
	var next = value == null || value === '' ? (status ? '' : '—') : String(value);
	if (node.textContent !== next) node.textContent = next;
}
/* the privacy switch (spec 25.9.19): identifiers say «hidden» for a screenshot; per browser, the API keeps them
 * (the report is sanitized by the engine, not here) */
function privacy() {
	try { return localStorage.getItem('mm:private') === '1'; } catch (e) { return false; }
}
function secret(node, value) {
	node._mmSecret = value;
	node.classList.add('mm-secret');
	text(node, value && privacy() ? _('hidden') : value);
}
function setPrivacy(on) {
	try { localStorage.setItem('mm:private', on ? '1' : '0'); } catch (e) { /* this page only */ }
	document.documentElement.classList.toggle('mm-private', on);
	Array.prototype.forEach.call(document.querySelectorAll('.mm-secret'), function(n) { text(n, n._mmSecret && on ? _('hidden') : n._mmSecret); });
}
/* ---- the service check and the DNS check, shown alike for a modem and for any channel (spec 25.9.18, 25.9.20) ---- */
var NC_WORD = { ok: _('works'), blocked: _('access denied'), timeout: _('no answer'), dns: _('name not found'), refused: _('connection refused'),
	reset: _('connection reset'), tls: _('TLS error'), cert: _('certificate failed verification'),
	tunnel: _('sent to the router\'s tunnel'), other: _('answer') };
var NC_TONE = { ok: 'ok', blocked: 'err', timeout: 'err', reset: 'err', cert: 'err' };
/* the service names of netcheck.list are the brands; one is said differently in Russian */
var NC_NAME = { wikipedia: _('Wikipedia'), yandex: _('Yandex'), mts: _('MTS'), t2: _('T2 (Tele2)'), megafon: _('MegaFon'), beeline: _('Beeline') };
function ncRows(res) {
	return list(res).map(function(x) {
		var w = NC_WORD[x.verdict] || x.verdict;
		if ((x.verdict === 'other' || x.verdict === 'blocked') && x.code) w += ' (HTTP ' + x.code + ')';
		return E('tr', { 'class': 'mm-nc-' + (NC_TONE[x.verdict] || 'warn') }, [E('td', {}, (NC_NAME[x.id] || x.name) + (x.group === 'list' ? ' · ' + _('for comparison') : x.group === 'known' ? ' · ' + _('usually closed in Russia') : '')),
			E('td', {}, w), E('td', {}, x.verdict === 'timeout' ? '—' : _('%d ms').format(x.ms || 0))]);
	});
}
/* one service's result as a cell of a table over several channels: {text, tone} («Белые списки», 2026-11-12) */
function ncCell(x) {
	if (!x) return { text: '—', tone: '' };
	var w = NC_WORD[x.verdict] || x.verdict;
	if ((x.verdict === 'other' || x.verdict === 'blocked') && x.code) w += ' (HTTP ' + x.code + ')';
	return { text: w, tone: NC_TONE[x.verdict] || 'warn' };
}
function ncName(x) { return (NC_NAME[x.id] || x.name) + (x.group === 'list' ? ' · ' + _('for comparison') : x.group === 'known' ? ' · ' + _('usually closed in Russia') : ''); }
/* where: «this modem», «the wan channel», as the views pass it */
function ncSummary(res, where) {
	res = list(res);
	/* known blocks (the regulator's, a service closed to Russia) and a name the tunnel takes say nothing of whitelists
	 * (the owner, 2026-11-15) */
	res = res.filter(function(x) { return x.verdict !== 'tunnel'; });
	var open = res.filter(function(x) { return x.group === 'open'; }), lst = res.filter(function(x) { return x.group === 'list'; });
	var okOpen = open.filter(function(x) { return x.verdict === 'ok'; }).length, okList = lst.filter(function(x) { return x.verdict === 'ok'; }).length;
	var say = !res.length ? '' : okOpen === open.length && okList === lst.length ? _('All services open through %s.').format(where) :
		!okOpen && okList ? _('Only the services for comparison open: this looks like whitelist mode.') :
		!okOpen && !okList ? _('No service opened: there is no internet through %s now.').format(where) :
		_('Some services do not open through %s: %s.').format(where, open.filter(function(x) { return x.verdict !== 'ok'; })
			.map(function(x) { return x.name; }).join(', '));
	var known = res.filter(function(x) { return x.group === 'known'; });
	if (known.length && open.length && okOpen)
		say += ' ' + _('Of the services usually closed in Russia %d of %d answered: this is no sign of whitelists.')
			.format(known.filter(function(x) { return x.verdict === 'ok'; }).length, known.length);
	if (res.some(function(x) { return x.verdict === 'cert'; }))
		say += ' ' + _('A certificate failed verification: someone on the way replaces the secure connection.');
	return say;
}
function dnsRows(c) {
	var VERDICT = { ok: _('own'), intercepted: _('spoofed'), local: _('the router itself'), unclear: '—' };
	return list((c || {}).resolvers).map(function(r) {
		return E('tr', { 'class': r.verdict === 'intercepted' ? 'mm-nc-err' : '' }, [E('td', { 'class': 'mm-mono' }, r.address),
			E('td', {}, String(r.identity || '')), E('td', { 'class': 'num' }, r.udp_ms >= 0 ? _('%d ms').format(r.udp_ms) : '—'),
			E('td', { 'class': 'num' }, r.tcp_ms >= 0 ? _('%d ms').format(r.tcp_ms) : '—'), E('td', {}, VERDICT[r.verdict] || r.verdict)]);
	});
}
function dnsSummary(c) {
	var stub = (c.blocked || {}).state === 'stub', nx = (c.nxdomain || {}).state === 'rewritten';
	return _('Checked %s.').format(new Date((c.at || 0) * 1000).toLocaleString(LANG)) + ' ' +
		((c.intercept || {}).state === 'local' ? _('The router itself takes the queries to public DNS and the connections to them (in 1–3 ms; so works a proxy or a DNS redirect on the router): the operator cannot be checked this way, and this is not the operator intercepting.') + ' ' :
		(c.intercept || {}).state === 'intercepted' ? _('The answers of public DNS are replaced on the way: %s.').format(dnsDetail(c.intercept.detail)) + ' ' : _('Public DNS answer themselves.') + ' ') +
		(stub ? _('Blocked sites: a stub (%s).').format(dnsDetail(c.blocked.detail)) + ' ' : (c.blocked || {}).state === 'ok' ? _('Blocked sites: the addresses match DoH.') + ' ' : '') +
		(nx ? _('The provider\'s DNS replaces names that do not exist.') : '') +
		' ' + _('“DNS” is the time to answer a query, “TCP” the time to connect to the same address: an answer much faster than the connection did not come from the server.');
}
function dnsTable(body) {
	return E('div', { 'class': 'mm-tablewrap' }, E('table', { 'class': 'mm-table mm-dns-table' }, [
		E('thead', {}, E('tr', {}, [E('th', {}, _('Server')), E('th', {}, _('Who answered')), E('th', { 'class': 'num' }, 'DNS'),
			E('th', { 'class': 'num' }, 'TCP'), E('th', {}, _('Verdict'))])), body]));
}
function ncTable(body) {
	return E('div', { 'class': 'mm-tablewrap', hidden: '' }, E('table', { 'class': 'mm-table mm-nc' }, [
		E('thead', {}, E('tr', {}, [E('th', {}, _('Service')), E('th', {}, _('Result')), E('th', {}, _('Time'))])), body]));
}
/* the helpers' refusals in words */
var NC_ERROR = { busy: _('a check is already running (another tab?)'), 'no curl': _('the router has no curl: install the curl package'),
	'no ca': _('the router has no root certificates: install the ca-bundle package'), 'bad device': _('wrong device name') };
/* an rpcd refusal: the user lacks the ACL group (engineer's tools: luci-app-medvemodem-engineer) */
function failText(e) {
	var m = String(e && e.message || e);
	return /Access denied|Permission denied/i.test(m) ? _('no rights: this LuCI user has no luci-app-medvemodem-engineer access group') : m;
}
/* ---- shared with dashboard.js (the mockup's pieces) ---- */
function duration(seconds) {
	if (typeof seconds !== 'number' || seconds < 0) return '—';
	var minutes = Math.floor(seconds / 60);
	return minutes < 60 ? _('%d min').format(minutes) : _('%d h %d min').format(Math.floor(minutes / 60), minutes % 60);
}
function bytes(n) {
	if (typeof n !== 'number' || n < 0) return '—';
	var units = [_('B'), _('KB'), _('MB'), _('GB'), _('TB')], i = 0;
	while (n >= 1024 && i < units.length - 1) { n /= 1024; i++; }
	return (i && n < 10 ? num(n, 1) : Math.round(n)) + ' ' + units[i];
}
var TECH_WORDS = { gsm: '2G', gprs: '2G', edge: '2G', umts: '3G', hsdpa: 'H', hsupa: 'H', hspa: 'H', 'hspa-plus': 'H+',
	lte: 'LTE', 'lte-cat-m': 'LTE-M', 'lte-nb-iot': 'NB-IoT', '5gnr': '5G' };
/* ModemManager's access technologies in a phone's words, without repeats: "lte, 5gnr" -> "LTE / 5G" */
function techWords(list_) {
	var out = [];
	(Array.isArray(list_) ? list_ : []).forEach(function(t) { var w = TECH_WORDS[t] || String(t).toUpperCase(); if (out.indexOf(w) < 0) out.push(w); });
	return out.join(' / ');
}
/* the expert's words (the owner, 2026-10-17): LTE with an NR carrier is NSA, NR alone is SA */
function techExact(list_) {
	var t = Array.isArray(list_) ? list_ : [];
	if (t.indexOf('5gnr') >= 0) return t.indexOf('lte') >= 0 ? 'LTE+NR NSA' : 'NR SA';
	return techWords(t);
}
var TECH = [['5gnr', '5G'], ['lte', 'LTE'], ['hspa-plus', 'H+'], ['hspa', 'H'], ['hsupa', 'H'], ['hsdpa', 'H'],
	['umts', '3G'], ['edge', '2G'], ['gprs', '2G'], ['gsm', '2G']];
/*
 * One scale for the signal (r28: a web, a back end and LEDs that disagree help nobody): four zones
 * per measure and technology — words, colour, a meter's fill (a quarter per zone, linear inside it, so the fill
 * never says more than the colour) and the bars of the icon all come from here. NR has its own RSRP cuts (an n78
 * cell at 3.5 GHz reads lower for the same service); RSRQ and SINR are shared.
 */
var CUTS = {
	lte: { rsrp: [-110, -100, -90], rsrq: [-15, -10, -7], sinr: [0, 13, 20] },
	nr: { rsrp: [-115, -105, -95], rsrq: [-15, -10, -7], sinr: [0, 13, 20] }
};
var SPAN = { rsrp: [-140, -60], rsrq: [-20, -3], sinr: [-10, 30] };	/* a meter's two ends */
function cutsOf(kind, rat) { return (CUTS[rat] || CUTS.lte)[kind]; }
function zoneOf(kind, v, rat) { var c = cutsOf(kind, rat); return v < c[0] ? 0 : v < c[1] ? 1 : v < c[2] ? 2 : 3; }
/* a meter's fill in percent: zone z spans [25z, 25z + 25] */
function fillOf(kind, v, rat) {
	var c = cutsOf(kind, rat), s = SPAN[kind], pts = [Math.min(s[0], c[0] - 1), c[0], c[1], c[2], Math.max(s[1], c[2] + 1)];
	if (v <= pts[0]) return 2;
	if (v >= pts[4]) return 100;
	for (var i = 0; i < 4; i++)
		if (v < pts[i + 1]) return Math.max(2, Math.round(25 * i + 25 * (v - pts[i]) / (pts[i + 1] - pts[i])));
	return 100;
}
/* a SINR of exactly 0 next to a decent RSRQ is a firmware's «not measured» (a field finding), not a value */
function sinrOf(carrier) {
	var v = carrier && carrier.snr;
	return typeof v !== 'number' || (v === 0 && typeof carrier.rsrq === 'number' && carrier.rsrq >= -14) ? null : v;
}
/*
 * The icon: four bars, one per RSRP zone (as a phone counts LTE bars by default), and one bar only when the noise
 * is strong (SINR below 0 dB, «заметные помехи»): a strong cell that cannot be heard is no link. Not the worse of
 * the two zones: SINR 0…13 dB («умеренные помехи») is most real links and would cap every icon at two bars.
 * The LTE anchor in NSA, the NR cell in SA.
 */
function signalIcon(mm) {
	var sig = mm.signal || {}, carrier = sig.lte || sig['5g'] || null, rat = sig.lte ? 'lte' : 'nr', tech = list(mm.access_tech), label = '';
	for (var i = 0; i < TECH.length && !label; i++)
		if (tech.indexOf(TECH[i][0]) >= 0) label = TECH[i][1];
	if (sig['5g'] && typeof sig['5g'].rsrp === 'number') label = '5G';	/* an NR carrier, NSA or SA */
	if (!carrier || typeof carrier.rsrp !== 'number') return null;
	var registered = ['home', 'roaming'].indexOf(mm.registration) >= 0 || ['registered', 'connecting', 'connected'].indexOf(mm.state) >= 0;
	var snr = sinrOf(carrier), noisy = snr !== null && zoneOf('sinr', snr, rat) === 0, z = noisy ? 0 : zoneOf('rsrp', carrier.rsrp, rat);
	return { label: registered ? label : '', bars: registered ? z + 1 : 0, zone: z, registered: registered, rsrp: carrier.rsrp, snr: snr, rat: rat,
		noisy: noisy };
}
/* the signal in percent for the basic level (the owner, 2026-11-12: «в базовом не надо писать RSRP/SINR, лучше словом и
 * проценты»): the meters' own fills, the power weighing 0.6 and the noise 0.4, kept inside the quarter of the bars and the
 * words (zone z: 25z…25z+25), so «отличный сигнал» is never 61 % and heavy noise stays in the lowest quarter */
function signalPct(icon) {
	if (!icon || !icon.registered || typeof icon.rsrp !== 'number') return null;
	var p = icon.snr === null ? fillOf('rsrp', icon.rsrp, icon.rat) : 0.6 * fillOf('rsrp', icon.rsrp, icon.rat) + 0.4 * fillOf('sinr', icon.snr, icon.rat);
	var z = icon.noisy ? 0 : icon.zone;
	return Math.max(1, Math.round(Math.min(Math.max(p, 25 * z), 25 * z + 25)));
}
/* the operators' own names: Latin in English, as the Russian ones write them in Russian */
var OPS = { '25001': [_('MTS'), '#e30611', '#fff'], '25002': [_('MegaFon'), '#00b956', '#fff'], '25099': [_('Beeline'), '#ffcc00', '#000'],
	'25020': ['T2', '#111', '#fff'], '25011': ['Yota', '#00aeef', '#fff'], '25035': [_('Motiv'), '#e4007c', '#fff'],
	'25039': [_('Rostelecom'), '#7700ff', '#fff'], '25062': [_('T-Mobile'), '#ffdd2d', '#000'], '25050': [_('SberMobile'), '#21a038', '#fff'],
	'25027': [_('Letai'), '#00a0e3', '#fff'], '25008': [_('Vainah'), '#2e7d32', '#fff'], '25701': ['A1', '#e30613', '#fff'],
	'25702': [_('MTS'), '#e30611', '#fff'], '25704': ['life:)', '#ffcd00', '#000'], '40101': ['Beeline', '#ffcc00', '#000'],
	'40102': ['Kcell', '#7b2a90', '#fff'], '40177': ['Tele2', '#111', '#fff'], '40107': ['Altel', '#e2001a', '#fff'],
	'43404': ['Beeline', '#ffcc00', '#000'], '43405': ['Ucell', '#7d2483', '#fff'], '43407': ['Mobiuz', '#e30613', '#fff'] };
/* simCode: the SIM's own operator (the engine's sim_operator.code). An MVNO's SIM is on another network (the owner,
 * 2026-11-05: Yota's SIM in MegaFon's network, the HiLink said «МегаФон»): the badge names the SIM's operator and a
 * small «in the MegaFon network» beside it */
function opBadge(code, name, simCode) {
	var o = OPS[String(code || '')], so = simCode && String(simCode) !== String(code || '') ? OPS[String(simCode)] : null;
	if (so) {
		var net = o ? o[0] : name || String(code || '');
		return E('span', { 'class': 'mm-op-pair', title: _('SIM: %s · network: %s').format(so[0] + ' (' + simCode + ')',
			net + (code ? ' (' + code + ')' : '')) }, [
			E('span', { 'class': 'mm-op', style: '--op-bg:' + so[1] + ';--op-fg:' + so[2] }, so[0]),
			net ? E('span', { 'class': 'mm-op-net' }, _('in the %s network').format(net)) : '' ]);
	}
	if (!o) return null;
	return E('span', { 'class': 'mm-op', style: '--op-bg:' + o[1] + ';--op-fg:' + o[2], title: (name || o[0]) + ' · ' + code }, o[0]);
}
/* two levels (the owner, 2026-10-23: the middle one was one step too many); a stored «pro» opens as «Detailed» */
var LEVELS = { normal: _('Basic'), expert: _('Detailed') };
var LEVEL_HINT = { normal: _('Only the main things: the network state and hints.'),
	expert: _('Everything: signal parameters, the recovery progress, connection details, the AT console, services and identifiers.') };
function level() {
	try { var v = localStorage.getItem('mm:level'); return v === 'pro' ? 'expert' : LEVELS[v] ? v : 'normal'; } catch (e) { return 'normal'; }
}
function setLevel(v) {
	try { localStorage.setItem('mm:level', v); } catch (e) { /* the page works without storage */ }
}
var CHEV = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4 6l4 4 4-4"/></svg>';
function chev() { var s = E('span', { 'class': 'mm-blk-c' }); s.innerHTML = CHEV; return s; }
/* «Modems · Basic ▾»: the head of every MedveModem page */
function pageHead(title, sub, onLevel, here) {
	var lv = level(), menu = E('details', { 'class': 'mm-menu', id: 'mm-level' }), pop = E('div', { 'class': 'mm-pop' });
	var summary = E('summary', { 'class': 'mm-btn', title: _('Interface detail') }, [LEVELS[lv] + ' ']);
	summary.appendChild(chev());
	/* the button sits right on a wide page and left on a phone: the open menu is shifted to stay on screen */
	menu.addEventListener('toggle', function() {
		pop.style.left = '';
		if (!menu.open) return;
		var r = pop.getBoundingClientRect(), room = document.documentElement.clientWidth - 8, shift = 0;
		if (r.right > room) shift = room - r.right;
		if (r.left + shift < 8) shift = 8 - r.left;
		if (shift) pop.style.left = shift + 'px';
	});
	Object.keys(LEVELS).forEach(function(k) {
		pop.appendChild(E('button', { type: 'button', 'class': 'mm-btn is-quiet' + (k === lv ? ' is-on' : ''), 'aria-pressed': String(k === lv),
			click: function() { setLevel(k); menu.removeAttribute('open'); summary.firstChild.textContent = LEVELS[k] + ' ';
				Array.prototype.forEach.call(pop.children, function(b, i) { var on = Object.keys(LEVELS)[i] === k;
					b.classList.toggle('is-on', on); b.setAttribute('aria-pressed', String(on)); });
				if (onLevel) onLevel(k); } },
			[E('b', {}, LEVELS[k]), E('span', { 'class': 'mm-faint' }, LEVEL_HINT[k])]));
	});
	menu.appendChild(summary); menu.appendChild(pop);
	/* «Modems» and «Settings» are LuCI's own menu tabs: no second switch here (bench 2026-10-15) */
	var on = privacy(), priv = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-privacy', 'aria-pressed': String(on),
		title: _('IMEI, ICCID, IMSI, numbers, addresses and cells read “hidden”, for a screenshot for example'),
		click: function() { on = !on; priv.setAttribute('aria-pressed', String(on)); text(priv, on ? _('Identifiers hidden') : _('Hide identifiers'));
			setPrivacy(on); if (onLevel) onLevel(level()); } }, on ? _('Identifiers hidden') : _('Hide identifiers'));
	document.documentElement.classList.toggle('mm-private', on);
	var head = E('div', { 'class': 'mm-head' }, [E('div', {}, [E('h2', {}, title), sub]), E('div', { 'class': 'mm-row' }, [priv, menu])]);
	head.syncLevel = function(k) {
		summary.firstChild.textContent = LEVELS[k] + ' ';
		Array.prototype.forEach.call(pop.children, function(b, i) { var on = Object.keys(LEVELS)[i] === k;
			b.classList.toggle('is-on', on); b.setAttribute('aria-pressed', String(on)); });
	};
	return head;
}
/* four bars with the phone's network label (spec 25.9.10): one bar per zone of the scale above */
function barsNode() {
	var svg = '<svg viewBox="0 0 19 15" aria-hidden="true">' + [0, 1, 2, 3].map(function(i) {
		var h = (i + 1) * 3.75;
		return '<rect class="mm-bar mm-bar-' + (i + 1) + '" x="' + i * 5 + '" y="' + (15 - h) + '" width="3.5" height="' + h + '" rx="1"/>';
	}).join('') + '</svg>';
	var node = E('span', { 'class': 'mm-bars mm-icon', role: 'img', hidden: true }, [E('b', { 'class': 'mm-icon-label' })]);
	node.insertAdjacentHTML('beforeend', svg);
	node.appendChild(E('i', { 'aria-hidden': 'true', hidden: true }, '×'));
	return node;
}
/* hysteresis (the owner, 2026-10-19): a one-bar step shows only when the next reading agrees; a jump of two or more, or
 * losing the network, at once — a signal on the edge of a zone no longer flickers between two pictures */
function setBars(node, icon) {
	node.hidden = !icon;
	if (!icon) return;
	if (icon.registered && typeof node.mmBars === 'number' && Math.abs(icon.bars - node.mmBars) === 1 && node.mmNext !== icon.bars) {
		node.mmNext = icon.bars;
		icon = Object.assign({}, icon, { bars: node.mmBars });
	} else {
		node.mmNext = null;
	}
	node.mmBars = icon.registered ? icon.bars : null;
	text(node.firstChild, icon.label);
	if (!icon.label) node.firstChild.textContent = '';
	node.classList.toggle('mm-unreg', !icon.registered);
	node.classList.toggle('is-low', icon.bars <= 1);
	Array.prototype.forEach.call(node.querySelectorAll('rect'), function(r, i) { r.classList.toggle('on', i < icon.bars); });
	node.lastChild.hidden = icon.registered;
	node.setAttribute('aria-label', icon.registered ? _('Signal: %d of 4').format(icon.bars) : _('Not registered in a network'));
	node.setAttribute('title', _('RSRP %s dBm').format(icon.rsrp) + (icon.snr !== null ? ', ' + _('SINR %s dB').format(icon.snr) : ''));
}
/* «backup»: a working backup channel, told apart from the main one's green (the owner, 2026-10-19) */
var TONE_PILL = { ok: 'is-ok', backup: 'is-backup', warn: 'is-warn', neutral: 'is-idle', err: 'is-err', run: 'is-run', off: 'is-off', paused: 'is-paused' };
/* a section the operator opened or closed stays so; one never touched follows the page's default (UX audit 2026-10-17:
 * not everything open at first) */
function secSaved(key) { try { var v = localStorage.getItem('mm:sec:' + key); return v === 'open' || v === 'closed' ? v : null; } catch (e) { return null; } }
/* tabs wider than the screen (mobile audit r117): «›» at the right edge while more of them is to the right, and the
 * chosen tab scrolled into view; wrap is .mm-tabs-wrap, strip its .mm-tabs */
function tabsHint(wrap, strip) {
	var update = function() { wrap.classList.toggle('is-more', strip.scrollLeft + strip.clientWidth < strip.scrollWidth - 2); };
	strip.addEventListener('scroll', update, { passive: true });
	window.addEventListener('resize', update);
	return function(tab) {
		if (tab && tab.scrollIntoView && strip.scrollWidth > strip.clientWidth) tab.scrollIntoView({ block: 'nearest', inline: 'nearest' });
		update();
		window.setTimeout(update, 0);
	};
}
function secOpen(key, dflt) { var v = secSaved(key); return v ? v === 'open' : dflt !== false; }
function secRemember(key, open) { try { localStorage.setItem('mm:sec:' + key, open ? 'open' : 'closed'); } catch (e) { /* storage is optional */ } }
/* a collapsible card: the whole header opens it, the chevron is its button; node.mmOpen(open) opens or closes it without
 * remembering (the page's own choice: a problem it points at), node.mmSaved says whether the operator chose */
/* the last reply of this browser tab, for an instant first picture while the fresh one comes (the owner, 2026-10-19);
 * sessionStorage: gone with the tab, never shared; a page renders the same without it */
function cacheGet(key) { try { var v = sessionStorage.getItem(key); return v ? JSON.parse(v) : null; } catch (e) { return null; } }
function cacheSet(key, value) { try { sessionStorage.setItem(key, JSON.stringify(value)); } catch (e) { /* storage is optional */ } }
function section(key, title, body, gist, dflt) {
	var open = secOpen(key, dflt), b = E('div', { 'class': 'mm-section mm-sec-b' }, body), g = E('span', { 'class': 'mm-faint mm-sec-gist' }, gist || '');
	var c = E('button', { type: 'button', 'class': 'mm-chev' }), node;
	var show = function(o) { open = o; b.hidden = !open; g.hidden = open; c.setAttribute('aria-expanded', String(open));
		c.setAttribute('aria-label', (open ? _('Collapse: %s') : _('Expand: %s')).format(title));
		/* a card that reads something only when it is looked at (the whitelist mode asks the tunnel managers) */
		if (open && node && node.mmOnOpen) node.mmOnOpen(); };
	var toggle = function() { show(!open); secRemember(key, open); };
	c.addEventListener('click', function(ev) { ev.stopPropagation(); toggle(); });
	c.innerHTML = CHEV;
	show(open);
	var h = E('div', { 'class': 'mm-sec-h mm-sec-click' }, [E('h3', {}, title), E('span', { 'class': 'mm-row' }, [g, c])]);
	h.addEventListener('click', function(ev) { if (!ev.target.closest('button, a, input, select, textarea, label')) toggle(); });
	node = E('section', { 'class': 'mm-card mm-pad mm-section mm-block' }, [h, b]);
	node.mmOpen = function(o) { if (o !== open) show(o); };
	node.mmIsOpen = function() { return open; };
	/* the collapsed header's short state and counters (the owner, 2026-10-19) */
	node.mmGist = function(t) { if (g.textContent !== (t || '')) g.textContent = t || ''; };
	node.mmSaved = function() { return secSaved(key) !== null; };
	return node;
}

/* the diagnosis chain (served "diagnosis", mm_chain): its links and reasons as people say them; the modem page and the dashboard */
var CHAIN_LINKS = { device: _('Device'), network: _('SIM and network'), session: _('Session'), internet: _('Internet'), dns: 'DNS' };
var CHAIN_WHY = { 'dns-intercepted': _('the operator answers the queries to other DNS servers in their place (DNS interception)'),
	'dns-rewritten': _('the operator\'s DNS answers names that do not exist with an address (rewritten answers)'),
	'dns-stub': _('DNS answers blocked sites with the address of a stub, not the real one'), absent: _('the modem is not seen on USB'),
	composition: _('the modem was not fully recognised (no driver or some of its ports)'),
	'service-mode': _('the modem is in service mode'), 'modeswitch-rule': _('a usb-modeswitch rule resets the modem\'s configuration'),
	'sim-missing': _('no SIM card'), 'sim-locked': _('the SIM card waits for its PIN'), 'registration-denied': _('the operator refused the registration'),
	searching: _('the modem searches for a network'), 'not-registered': _('the modem is not registered in a network'), 'no-session': _('no session with the operator'),
	'session-failed': _('the session with the operator does not come up'), whitelist: _('only the sites on the operator\'s whitelist work'),
	'no-answer': _('the check addresses do not answer through this modem'),
	restricted: _('the operator restricted access and sends to its own page: check the balance or the operator\'s restrictions'), 'dns-failed': _('the router\'s DNS does not answer') };
/* one line for the dashboard's card: «all links fine» or the first broken link and why */
function chainLine(dg) {
	if (!dg || !dg.links) return '';
	var w = dg.warning;
	return dg.ok ? (w ? _('works, a remark on the link “%s”: %s').format(CHAIN_LINKS[w.link] || w.link, CHAIN_WHY[w.reason] || w.reason) :
		dg.standby ? _('all links fine, the modem is on standby') : _('all links fine')) :
		_('a problem at the link “%s”: %s').format(CHAIN_LINKS[dg.link] || dg.link, CHAIN_WHY[dg.reason] || dg.reason);
}
/*
 * Every action button says it is working (the owner, 2026-10-16: «кнопка обновить … вообще не
 * понятно отрабатывает или нет»): a click handler that returns a promise marks its button busy —
 * a spinner, aria-busy, a second click ignored — until the promise settles. The page's own message
 * line says the outcome. The handler's own disabling is left alone.
 */
function busy(fn) {
	return function(ev) {
		var b = ev && ev.currentTarget, r;
		if (b && b.classList && b.classList.contains('is-busy')) return;
		r = fn.apply(this, arguments);
		if (b && b.classList && r && typeof r.then === 'function') {
			b.classList.add('is-busy');
			b.setAttribute('aria-busy', 'true');
			var done = function() { b.classList.remove('is-busy'); b.removeAttribute('aria-busy'); };
			r.then(done, done);
		}
		return r;
	};
}

/* the engine's journal in words: one wording for the modem page and the dashboard's «Event» (2026-10-17) */
function words(value) { return typeof value === 'string' && value !== '--' ? value : ''; }
var REASONS = { session_stuck: _('the connection does not come up'), mm_bearer_stuck: _('ModemManager holds a broken connection'),
	proto_missing: _('the connection protocol\'s package is not installed (resets will not help)'), registration_stuck: _('the modem does not register in the network'),
	modem_firmware_hang: _('the modem does not answer'), owner_conflict: _('a conflict of managing services'),
	configuration: _('setup needed'), sim: _('SIM card error'), mm_not_holding: _('ModemManager does not manage the modem'), user_requested: _('started by hand'),
	no_internet: _('connected, but no internet'), no_data_2g: _('the modem fell back to 2G, no data is possible'),
	registration_denied: _('the network refused the registration'), usb_power: _('the modem lacks USB power') };
/* spec 25.9.11: stages named by how hard they hit; the L-number only for logs and the CLI */
/* each step said by what it does and how long the internet is away (user review 2026-10-06: «мягкий»
 * meant nothing); [title, what happens, the pause, asks first]; the L-number stays in logs and the CLI */
var STAGES = {
	observe: [_('Refresh the modem\'s data'), _('The modem\'s state is read again.'), _('without breaking the link'), false],
	session: [_('Reconnect the internet'), _('The connection with the operator is set up again.'), _('a break of a few seconds'), false],
	control: [_('Free stuck ports'), _('Stuck port locks are lifted and the modem is handed to the managing service.'), _('a break of up to a few seconds'), false],
	radio: [_('Restart the network search'), _('The modem leaves the operator\'s base station and searches for the network again (like airplane mode).'), _('a break of 10–30 s'), true],
	'soft-reset': [_('Restart the modem'), _('The modem is restarted by a command.'), _('a break of 1–2 min'), true],
	rebind: [_('Restart the modem\'s driver'), _('The router unbinds the modem\'s driver and binds it again.'), _('a break of up to 1 min'), true],
	'usb-reset': [_('Reconnect the modem over USB'), _('The modem is disconnected from the USB bus by software and connected again (like pulling it out of the port).'), _('a break of 1–3 min'), true],
	power: [_('Reset the USB power'), _('The modem\'s USB port is switched off and on again (on a hub with power control: fully unpowered).'), _('a break of 1–3 min'), true]
};
/* the device's history (medvemodemd.c, history.c) as a journal line; ⚠ marks what changed the modem or the settings */
var EVENTS = {
	added: [_('Modem found'), ''], reattached: [_('The modem reconnected to USB'), ''], changed: [_('The modem\'s USB composition changed'), ''],
	removed: [_('Modem disconnected from USB'), 'warn'], 'usb-add': [_('Modem connected to USB'), ''], 'usb-remove': [_('Modem disconnected from USB'), 'warn'],
	'driver-bind': [_('The modem\'s driver bound'), ''], 'driver-unbind': [_('The modem\'s driver unbound'), ''], 'mm-add': [_('ModemManager took over the modem'), ''], 'mm-remove': [_('ModemManager released the modem'), 'warn'],
	'mm-state': [_('The modem\'s state changed'), ''], 'mm-kick': [_('The ports handed to ModemManager again'), ''], 'registration-denied': ['⚠ ' + _('The network refused the registration'), ''],
	'port-held': [_('A modem port is busy with another program'), 'warn'], 'port-released': [_('Another program released a modem port'), ''],
	'mm-ports-withdrawn': [_('Serial ports withdrawn from ModemManager'), ''],
	'mm-restart': ['⚠ ' + _('ModemManager restart'), 'act'], 'mm-restart-end': [_('ModemManager restart finished'), ''],
	'sim-unlock': ['⚠ ' + _('SIM card unlock'), 'act'],
	'sms-received': [_('SMS received'), 'ok'], 'sms-sent': [_('SMS sending'), ''], 'sms-cnmi-fixed': [_('Incoming SMS were not stored (CNMI 0,0): fixed'), 'warn'],
	'subnet-clash': [_('The modem\'s network clashes with the router\'s or another modem\'s'), 'warn'], 'subnet-moved': [_('The modem moved to a free subnet'), 'ok'],
		'esim-switch': [_('eSIM profile switched: the modem registers in the network again'), 'ok'],
	'subnet-move-failed': [_('The modem\'s web did not take the new subnet'), 'warn'], 'sms-stuck-dropped': [_('A stuck part of a long SMS deleted'), 'warn'], 'at-port-stuck': [_('The AT port hung in the system'), 'warn'], 'at-port-rebind': [_('The hung AT port re-attached to its driver'), 'act'], 'usb-rescue': [_('Last resort by hand: the USB controller or the power line'), 'warn'],
	'session-up': [_('Connection up'), 'ok'], 'session-down': [_('Connection down'), 'warn'], 'session-failed': [_('Connection setup error'), 'warn'],
	'recovery-start': ['⚠ ' + _('Recovery step started'), 'act'], 'recovery-end': [_('Recovery step finished'), ''],
	'hostfix-start': ['⚠ ' + _('Service ports being bound'), 'act'], 'hostfix-end': [_('Service ports bound'), ''],
	'radio-change': ['⚠ ' + _('Network mode and bands changed'), 'act'], 'radio-kept': [_('The network mode change kept'), 'ok'],
	'radio-rollback': ['⚠ ' + _('The previous network mode and bands restored'), 'warn'],
	'radio-guard': ['⚠ ' + _('Cell lock guard'), 'warn'],
	'band-guard': ['⚠ ' + _('Band lock guard'), 'warn'],
	reregister: ['⚠ ' + _('Re-register in the network'), 'act'], 'band-change': [_('Band changed'), ''], 'cell-change': [_('Serving cell changed'), ''], 'ca-change': [_('Carrier aggregation changed'), ''],
	'backend-return': ['⚠ ' + _('The modem returned to ModemManager'), 'act'],
	'quota-warn': [_('Traffic limit almost used up'), 'warn'], 'quota-exceeded': [_('Traffic limit used up'), 'warn'],
	ifup: [_('Interface brought up again'), ''], 'settings-follow': [_('The name and settings followed the modem'), ''], 'usb-config': ['⚠ ' + _('USB configuration switched'), 'act'],
	'fcc-unlock-on': ['⚠ ' + _('FCC unlock switched on'), 'act'], 'fcc-unlock-off': [_('FCC unlock switched off'), 'act'],
	frozen: ['⚠ ' + _('Modem management paused by the operator'), 'act'], unfrozen: [_('Modem management resumed'), 'act'],
	'qmodem-release': [_('The modem taken from QModem by the operator\'s choice'), 'act'], 'iface-repair': ['⚠ ' + _('Interface settings repaired'), 'act'],
	config: ['⚠ ' + _('Network settings changed'), 'act'],
	internet: [_('Internet through the modem'), ''], failover: [_('Traffic switching'), 'warn']
};
/* why a modem was found or came back (the owner, 2026-10-28: «писать причину пере-детекта модема») */
var FOUND_WHY = { start: _('at the engine\'s start'), uevent: _('a USB event'), 'rescan request': _('a rescan'), returned: _('it was gone and came back') };
var INTERNET = { ok: [_('the whole internet answers'), 'ok'], limited: [_('only the operator\'s whitelist answers'), 'warn'], down: [_('no answer'), 'warn'] };
var FAILOVER_WHY = { down: _('has no internet, the traffic moved to another channel'), limited: _('has the whitelist only, another channel carries the traffic'),
	quota: _('spent its traffic limit, kept behind the others'), manual: _('is a manual channel, kept behind'), demoted: _('answers again, kept last'),
	back: _('answers again, its priority restored') };
var FOLLOW_WHAT = { modem: _('name'), device: _('connection settings'), esim: _('eSIM settings') };
var WHO = { luci: _('from the web interface'), cli: _('from the command line'), automatic: _('automatically'), operator: _('by the administrator') };
function eventWords(e) {
	var d = words(e.detail), k = EVENTS[e.kind] || [e.kind, ''], m;
	if ((e.kind === 'recovery-start' || e.kind === 'recovery-end') && (m = /^(\S+)\s+(\S+)(?:\s+(\S+))?/.exec(d))) {
		var stage = (STAGES[m[1]] || [m[1]])[0];
		d = e.kind === 'recovery-start' ? _('“%s”, %s').format(stage, WHO[m[3]] || _('by hand')) + (m[2] !== 'user_requested' ? ' (' + (REASONS[m[2]] || m[2]) + ')' : '')
			: _('“%s” — %s').format(stage, RESULTS[m[2]] || m[2]);
	} else if (e.kind === 'mm-kick') d = d.replace(/^ports reported again \((\d+) of (\d+)\)/, function(m0, a, b) { return _('attempt %s of %s').format(a, b); });
	else if (e.kind === 'config') d = d.replace(/^firewall reloaded: its rules missed (\S+)$/, function(m0, dev) { return _('the firewall reloaded: its rules had no device %s').format(dev); })
		.replace(/^(\S+ \S+) (\S+)$/, function(m0, a, b) { return _('%s (interface %s)').format(a, b); }).replace(/^bringup/, _('bring-up')).replace(/^apply/, _('applying'))
		.replace(/^release/, _('giving up control')).replace(/^rollback/, _('rollback')).replace(/^repair/, _('repair')).replace(/committed/, _('applied'))
		.replace(/rolled-back/, _('cancelled'));
	else if (e.kind === 'interface-down' || e.kind === 'interface-up') d = d.replace(/^(\S+) by the operator \((.*)\)$/, function(m0, a, b) { return _('%s, by hand (%s)').format(a, b); });
	else if (e.kind === 'settings-follow') d = d.replace(/^(\S+): /, function(m0, t) { return (FOLLOW_WHAT[t] || t) + ': '; })
		.replace(/: from parked$/, ': ' + _('the modem came back')).replace(/: from (\S+)$/, function(m0, p) { return ': ' + _('from port %s').format(p); })
		.replace(/: parked, another modem is in this port$/, ': ' + _('put aside, another modem is in this port'));
	else if (e.kind === 'iface-repair') d = d.replace(/device path of this boot/, _('the path to the modem updated')).replace(/joined the wan zone/, _('added to the wan zone'));
	else if (e.kind === 'session-up' || e.kind === 'session-down' || e.kind === 'session-failed') d = d ? _('interface %s').format(d) : '';
	else if (e.kind === 'sms-received') d = d.replace(/^(\d+) new$/, function(m0, n) { return _('new: %s').format(n); });
	else if (e.kind === 'quota-warn' || e.kind === 'quota-exceeded') d = d.replace(/^([\d.]+) of ([\d.]+) GB$/, function(m0, a, b) { return _('%s of %s GB').format(a, b); });
	else if (e.kind === 'backend-return') d = d.replace(/^native interface down, ModemManager may take the modem$/, _('the direct QMI connection stopped, the modem handed to ModemManager'))
		.replace(/^ModemManager took the modem$/, _('ModemManager took the modem, the connection goes through it'))
		.replace(/^ModemManager did not take the modem$/, _('ModemManager did not take the modem: the direct connection again')).replace(/^bring-up refused$/, _('the connection was not started'));
	else if (e.kind === 'sms-sent') { k = [/^failed/.test(d) ? _('SMS not sent') : _('SMS sent'), /^failed/.test(d) ? 'warn' : 'ok'];
		d = / at$/.test(d) ? _('through the modem\'s AT port') : ''; }
	else if (e.kind === 'service-restart' || e.kind === 'service-stop') {
		var sv = /^qmodem/.test(d) ? 'QModem' : 'ModemManager', ours = / medvemodem$/.test(d);
		k = e.kind === 'service-stop' ? [_('%s stopped').format(sv), ours ? '' : 'warn'] :
			[ours ? _('%s restarted by MedveModem').format(sv) : _('%s restarted by itself (a crash, or by hand)').format(sv), ours ? '' : 'warn'];
		d = ''; }
	else if (e.kind === 'web-login') { k = [d === 'ok' ? _('Logged in to the modem\'s web') : d === 'locked' ? _('The modem\'s web blocked logins for a while') :
			d === 'wrong' ? _('The saved password did not fit the modem\'s web') : _('The modem\'s web asks for a password'), d === 'ok' ? 'ok' : 'warn']; d = ''; }
	else if (e.kind === 'sim-unlock') { k = [d === 'done' ? _('SIM card unlocked') : _('SIM card not unlocked'), d === 'done' ? 'ok' : 'warn']; d = ''; }
	else if (e.kind === 'sms-stuck-dropped') d = d ? _('message No. %s (an incomplete part blocked receiving)').format(d) : '';
	else if (e.kind === 'mm-restart') d = d.replace(/^operator(: )?/, function(m0, sep) { return _('by the administrator') + (sep || ''); });
	else if (e.kind === 'mm-restart-end') d = d.replace(/^started again$/, _('the service started')).replace(/^init script status (\d+)$/, function(m0, c) { return _('start error (code %s)').format(c); });
	else if ((e.kind === 'added' || e.kind === 'reattached') && d) d = FOUND_WHY[d] || (/attachment/.test(d) ? _('its USB place changed') : /ports/.test(d) ? _('its ports changed') : '');
	else if (e.kind === 'internet') { k = [k[0], (INTERNET[d] || ['', ''])[1]]; d = (INTERNET[d] || [d])[0]; }
	else if (e.kind === 'radio-change' && /cell lock/.test(d)) {
		/* a cell lock is no «network mode and bands» change (the owner, 2026-11-03: the lock was not seen in the journal) */
		k = [_('Cell lock'), 'act'];
		d = d.replace(/cell lock (\w+) (\d+) pci (\S+)/, function(m0, rat, ch, pci) { return _('%s, channel %s, PCI %s').format(rat === 'nr' ? '5G' : 'LTE', ch, pci === 'any' ? '—' : pci); })
			.replace(/cell lock off/, _('removed')).replace(/^modes /, _('modes') + ' ').replace(/bands /, _('bands') + ' ');
	}
	else if (e.kind === 'reregister') d = d === 'done' ? _('done') : d === 'failed' ? _('not done') : d === 'at' ? _('over AT') : d;
	else if (/^(band|cell|ca)-change$/.test(e.kind)) d = d.replace(/one carrier/g, _('one carrier'));
	else if (e.kind === 'radio-guard' || e.kind === 'band-guard')
		d = (m = /^no network for (\d+) min$/.exec(d)) ? _('no network for %s min, the settings before the lock go back').format(m[1]) :
			d === 'done' ? _('the settings before the lock are back') : _('the old settings could not be written, it gave up');
	else if (e.kind === 'failover' && (m = /^(\S+) (\S+)$/.exec(d))) d = _('%s: %s').format(m[2], FAILOVER_WHY[m[1]] || _('its priority restored'));
	else if (/^(added|removed|reattached|changed|usb-add|usb-remove|driver-bind|driver-unbind|mm-add|mm-remove|hostfix-)/.test(e.kind)) d = '';
	return [k[0] + (d ? ': ' + d : ''), k[1]];
}
var RESULTS = { succeeded: _('done'), failed: _('not done'), refused: _('refused'), busy: _('busy'), retry: _('try again later'), started: _('started') };

/* an open menu («⋯», «Ещё», the level) closes on a click or a tap outside it and on Escape (bench 2026-10-19: they stayed
 * open over the cards); one listener for the page, bound once */
if (typeof document !== 'undefined' && document.addEventListener && !window.mmMenuCloser) {
	window.mmMenuCloser = true;
	var closeMenus = function(keep) {
		Array.prototype.forEach.call(document.querySelectorAll('details.mm-menu[open], details#mm-level[open]'), function(m) {
			if (!keep || !m.contains(keep)) m.removeAttribute('open');
		});
	};
	document.addEventListener('click', function(ev) { closeMenus(ev.target); }, true);
	document.addEventListener('keydown', function(ev) { if (ev.key === 'Escape') closeMenus(null); });
}

/* the hottest sensor in a temperature answer: whole degrees, tenths (Huawei ^CHIPTEMP) or thousandths (Fibocom
 * GTSENRDTEMP) are told by size; the command echo and sensor indices below a degree that matters are left out.
 * A sensor's placeholder is no reading (the FM350's ETHERMAL during a reinit gives 32767, -127, -28479; a field
 * report): it read as «32.8 °C» once divided. «+GTSENRDTEMP: <sensor>,<value>» is the value only, its
 * sensor number never a temperature (a placeholder there left «1 °C») */
var TEMP_PLACEHOLDERS = [32767, -32767, -32768, 65535, -127, -28479];
function tempOf(lines) {
	var best = null;
	lines.forEach(function(l) {
		if (/^AT/i.test(l)) return;
		var body = String(l).replace(/^[^:]*:/, '');
		if (/^\s*\+GTSENRDTEMP\s*:/i.test(l)) body = body.replace(/^\s*\d+\s*,/, '');
		(body.match(/(?<![\w.])-?\d+(?:\.\d+)?(?![\d.])/g) || []).forEach(function(x) {
			var v = parseFloat(x);
			if (v >= 65000 || TEMP_PLACEHOLDERS.indexOf(v) >= 0) return;	/* 65535: a sensor that is not there */
			/* a flat 0 is a sensor that gave nothing, not a working modem at the freezing point (a tester's FM350,
			 * 2026-11-11: «0 °C» for a day); the next command of the model is asked instead */
			if (v === 0) return;
			if (Math.abs(v) >= 1000) v /= 1000; else if (Math.abs(v) >= 150) v /= 10;
			if (v >= -40 && v <= 125 && (best === null || v > best)) best = Math.round(v * 10) / 10;
		});
	});
	return best;
}
/* the model's temperature presets, from the engine's AT presets, in their order: the first that gives a reading wins
 * (the FM350: GTSENRDTEMP, then ETHERMAL — ETHERMAL is silent on a firmware) */
function tempCommands(presets) {
	var out = [];
	list(((presets || {}).at || {}).sets).forEach(function(set) { list(set.presets).forEach(function(p) {
		if (p.allowed && /TEMP|THERMAL/i.test(p.command || '') && out.indexOf(p.command) < 0) out.push(p.command); }); });
	return out;
}
function tempCommand(presets) { return tempCommands(presets)[0] || null; }
/* ask each command in turn until one answers a temperature: { c, cmd, lines, r } of the last asked */
function tempAsk(cmds, ask) {
	var i = 0, last = { c: null, cmd: cmds[0], lines: [], r: null };
	function next() {
		if (i >= cmds.length) return Promise.resolve(last);
		var cmd = cmds[i++];
		return Promise.resolve(ask(cmd)).then(function(r) {
			r = r || {};
			last = { c: !r.refused && r.final ? tempOf(list(r.lines)) : null, cmd: cmd, lines: list(r.lines), r: r };
			return last.c !== null || r.refused ? last : next();
		});
	}
	return next();
}
/* «Температура» automatically (the owner, 2026-10-19): per modem in this browser, { auto, dash } and the last reading
 * { c, t }; the dashboard shows it at «Подробный» and measures it again when it is older than TEMP_EVERY_S */
var TEMP_EVERY_S = 120;
/* a reading: the last one and the 12 h series for the card's small chart (the owner, 2026-10-19), at most a point a
 * minute; the series lives in this browser like the rest, so it has gaps while no page measured */
var TEMP_SPAN_S = 12 * 3600;
/* a lone jump is a sensor's slip (the FM350 gave one 25 within 53–57; three readings
 * in a row settle it): more than 15 °C from a reading under 2 min old is kept only when three in a row agree; false while held */
var TEMP_JUMP = 15, TEMP_JUMP_S = 120, TEMP_CONFIRM = 3;
function tempRecord(id, c) {
	var pr = tempPrefs(id), now0 = Math.floor(Date.now() / 1000);
	if (typeof pr.c === 'number' && pr.t && now0 - pr.t <= TEMP_JUMP_S && Math.abs(c - pr.c) > TEMP_JUMP) {
		var j = pr.j && Math.abs(pr.j.c - c) <= TEMP_JUMP ? pr.j.n + 1 : 1;
		if (j < TEMP_CONFIRM) { tempPrefs(id, { j: { c: c, n: j } }); return false; }
	}
	if (pr.j) tempPrefs(id, { j: null });
	var now = now0, h = list(tempPrefs(id).h).filter(function(p) {
		return Array.isArray(p) && typeof p[0] === 'number' && now - p[0] <= TEMP_SPAN_S; });
	if (!h.length || now - h[h.length - 1][0] >= 60) h.push([now, c]);
	return tempPrefs(id, { c: c, t: now, h: h.slice(-720) });
}
/*
 * The series as a chart (the owner, 2026-10-21: «огрызок», then «графики битые»: a reading every 1–2 min jumps a
 * degree or two between sensors and drew a saw): 10-minute buckets, the hottest reading of each (the card shows the
 * hottest sensor too), drawn as the reception history is — a filled step relief, a gap where no page measured. The
 * card's width, a degree axis, the time every 3 h to «сейчас», the lowest and the highest bucket marked, hover names
 * the bucket. null without a reading.
 */
var TEMP_BUCKET_S = 600;
function tempChart(h, width) {
	var now = Math.floor(Date.now() / 1000), start = now - TEMP_SPAN_S, buckets = {};
	list(h).forEach(function(p) {
		if (!Array.isArray(p) || typeof p[1] !== 'number' || now - p[0] > TEMP_SPAN_S) return;
		var k = Math.floor((p[0] - start) / TEMP_BUCKET_S);
		if (!buckets[k] || p[1] > buckets[k].v) buckets[k] = { k: k, v: p[1], t: p[0], n: (buckets[k] ? buckets[k].n : 0) + 1 };
		else buckets[k].n++;
	});
	var bs = Object.keys(buckets).map(function(k) { return buckets[k]; }).sort(function(a, b) { return a.k - b.k; });
	if (!bs.length) return null;
	var NS = 'http://www.w3.org/2000/svg', W = Math.max(260, Math.round(width || 560)), H = 150, L = 40, R = 14, T = 18, B = 22;
	var pw = W - L - R, ph = H - T - B, vals = bs.map(function(b) { return b.v; });
	var mn = Math.min.apply(null, vals), mx = Math.max.apply(null, vals);
	var lo = Math.floor(mn - 2), hi = Math.ceil(mx + 2);
	if (hi - lo < 6) { lo -= Math.ceil((6 - (hi - lo)) / 2); hi = lo + 6; }
	var step = hi - lo > 30 ? 10 : hi - lo > 12 ? 5 : 2;
	var x = function(t) { return L + (t - start) / TEMP_SPAN_S * pw; }, y = function(v) { return T + (hi - v) / (hi - lo) * ph; };
	var bx = function(b) { return x(start + b.k * TEMP_BUCKET_S); }, bw = pw * TEMP_BUCKET_S / TEMP_SPAN_S;
	var svg = document.createElementNS(NS, 'svg');
	svg.setAttribute('width', String(W)); svg.setAttribute('height', String(H));
	svg.setAttribute('class', 'mm-hist-svg mm-temp-chart'); svg.setAttribute('role', 'img');
	svg.setAttribute('aria-label', _('Temperature over 12 hours: from %s to %s °C').format(num(mn), num(mx)));
	function el(name, attrs, txt, parent) {
		var n = document.createElementNS(NS, name);
		Object.keys(attrs).forEach(function(a) { n.setAttribute(a, attrs[a]); });
		if (txt != null) n.textContent = txt;
		(parent || svg).appendChild(n);
		return n;
	}
	var deg = function(v) { return num(Math.round(v * 10) / 10); };
	var hm = function(t) { return new Date(t * 1000).toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit' }); };
	for (var v = Math.ceil(lo / step) * step; v <= hi; v += step) {
		el('line', { x1: L, x2: L + pw, y1: y(v).toFixed(1), y2: y(v).toFixed(1), 'class': 'mm-hist-grid' });
		el('text', { x: L - 6, y: (y(v) + 4).toFixed(1), 'text-anchor': 'end', 'class': 'mm-hist-tick' }, v + '°');
	}
	el('line', { x1: L, x2: L + pw, y1: T + ph, y2: T + ph, 'class': 'mm-hist-base' });
	[0, 0.25, 0.5, 0.75, 1].forEach(function(f) {
		el('text', { x: (L + f * pw).toFixed(1), y: H - 4, 'text-anchor': f === 0 ? 'start' : f === 1 ? 'end' : 'middle', 'class': 'mm-hist-tick' },
			f === 1 ? _('now') : hm(start + f * TEMP_SPAN_S));
	});
	/* runs of buckets: steps filled to the baseline. One or two empty buckets (a reading every 2 min that failed while the
	 * AT port was busy) hold the step before them: the owner's r161 screenshot, «ступеньки заливки рвутся». A longer
	 * gap (the page was closed) stays a gap */
	var runs = [], cur = null;
	bs.forEach(function(b) {
		if (!cur || b.k > cur[cur.length - 1].k + 3) { cur = []; runs.push(cur); }
		cur.push(b);
	});
	runs.forEach(function(r) {
		var top = r.map(function(b, i) { var x0 = bx(b), x1 = Math.min(L + pw, x0 + bw);
			return (i ? 'L' + x0.toFixed(1) + ' ' + y(r[i - 1].v).toFixed(1) + 'L' : 'M') + x0.toFixed(1) + ' ' + y(b.v).toFixed(1) +
				'L' + x1.toFixed(1) + ' ' + y(b.v).toFixed(1); }).join('');
		var last = r[r.length - 1], x1 = Math.min(L + pw, bx(last) + bw);
		el('path', { d: top + 'L' + x1.toFixed(1) + ' ' + (T + ph) + 'L' + bx(r[0]).toFixed(1) + ' ' + (T + ph) + 'Z', 'class': 'mm-temp-area' });
		el('path', { d: top, 'class': 'mm-hist-line mm-temp-line' });
	});
	/* the hottest and the coolest bucket, named; the latest as a dot */
	var pMax = bs.filter(function(b) { return b.v === mx; }).pop(), pMin = bs.filter(function(b) { return b.v === mn; }).pop(), last = bs[bs.length - 1];
	function mark(b, word, above) {
		var cx = Math.min(L + pw, bx(b) + bw / 2), cy = y(b.v), anchor = cx > L + pw - 60 ? 'end' : cx < L + 60 ? 'start' : 'middle';
		el('circle', { cx: cx.toFixed(1), cy: cy.toFixed(1), r: 4, 'class': 'mm-hist-dot mm-temp-dot' });
		el('text', { x: cx.toFixed(1), y: (above ? cy - 8 : cy + 15).toFixed(1), 'text-anchor': anchor, 'class': 'mm-hist-label' }, word + ' ' + deg(b.v) + '°');
	}
	if (mx !== mn) { mark(pMax, _('max'), true); mark(pMin, _('min'), false); }
	else mark(last, _('now'), true);
	/* hover: the bucket under the pointer — its time and its hottest reading */
	var cross = el('line', { y1: T, y2: T + ph, 'class': 'mm-hist-cross', visibility: 'hidden' });
	var tip = el('text', { y: T - 6, 'class': 'mm-hist-label mm-temp-tip', visibility: 'hidden' });
	var hit = el('rect', { x: L, y: T, width: pw, height: ph, fill: 'transparent' });
	var show = function(ev) {
		var r = svg.getBoundingClientRect(), px = (ev.clientX - r.left) * (W / (r.width || W)), best = bs[0];
		bs.forEach(function(b) { if (Math.abs(bx(b) + bw / 2 - px) < Math.abs(bx(best) + bw / 2 - px)) best = b; });
		var cx = Math.min(L + pw, bx(best) + bw / 2);
		cross.setAttribute('x1', cx.toFixed(1)); cross.setAttribute('x2', cx.toFixed(1)); cross.setAttribute('visibility', 'visible');
		tip.textContent = hm(start + best.k * TEMP_BUCKET_S) + '–' + hm(start + (best.k + 1) * TEMP_BUCKET_S) + ' · ' + deg(best.v) + ' °C';
		tip.setAttribute('x', cx.toFixed(1)); tip.setAttribute('text-anchor', cx > L + pw - 90 ? 'end' : cx < L + 90 ? 'start' : 'middle');
		tip.setAttribute('visibility', 'visible');
	};
	hit.addEventListener('mousemove', show);
	hit.addEventListener('mouseleave', function() { cross.setAttribute('visibility', 'hidden'); tip.setAttribute('visibility', 'hidden'); });
	return { node: svg, min: mn, max: mx, minAt: pMin.t, maxAt: pMax.t, last: last, points: bs.reduce(function(a, b) { return a + b.n; }, 0) };
}
function tempPrefs(id, set) {
	var k = 'mm:temp:' + id, v = {};
	try { v = JSON.parse(localStorage.getItem(k) || '{}') || {}; } catch (e) { v = {}; }
	if (set) { v = Object.assign(v, set); try { localStorage.setItem(k, JSON.stringify(v)); } catch (e) { /* no storage */ } }
	return v;
}

/* the profile's name for a device whose USB strings say nothing (a UFI stick says «Android», bench 2026-10-19) */
function profileTitle(d, mmd) {
	var t = ((d || {}).profile || {}).title, w = function(v) { return typeof v === 'string' && v !== '--' ? v : ''; };
	if (typeof t !== 'string' || !t) return '';
	if (w((mmd || {}).model) || w((d || {}).model)) return '';
	return !w(d.product) || /^(android|generic|usb|rndis|cdc)\b/i.test(w(d.product)) ? t : '';
}

/*
 * How well MedveModem knows the model (the owner, 2026-10-21): '' for a model with its own
 * profile, else one sentence for the card and the page, which offer the report a new profile is made from. The engine's
 * profile.fit: exact, family (a vendor's profile), generic (by the driver only), ambiguous (two profiles tie); a modem
 * waiting for its mode switch has its own card and is not said twice.
 */
/* the project's issue form for a new modem: the report goes there (.github/ISSUE_TEMPLATE/new-modem.yml, on main) */
var NEW_MODEM_ISSUE = 'https://github.com/Medvedolog/medvemodem/issues/new?template=new-modem.yml';

/*
 * The connection check in plain words, one place for the card, the table and the modem page (audit r101: «Интернет ·
 * 57 мс», not «Интернет отвечает, 57 мс · проверено только что»). A result older than two of its checks is said as
 * out of date, not green: the main channel is checked every 60 s, a backup every 5 min (UPLINK_PRIMARY_MS,
 * UPLINK_BACKUP_MS). {text, tone: ok|warn|err|idle}, or null when nothing was checked.
 */
function checkState(reach, role) {
	if (!reach || typeof reach.ok !== 'boolean') return null;
	var limit = role === 'carrying' ? 180 : 660;
	if (typeof reach.age_s === 'number' && reach.age_s > limit)
		return { text: _('Data out of date · %s').format(reach.age_s < 3600 ? _('%d min').format(Math.round(reach.age_s / 60)) : _('%d h').format(Math.round(reach.age_s / 3600))), tone: 'idle' };
	if (reach.limited) return { text: _('Access limited'), tone: 'warn' };
	if (!reach.ok) return { text: _('No answer to the check'), tone: 'err' };
	return { text: typeof reach.latency_ms === 'number' ? _('Internet · %d ms').format(reach.latency_ms) : _('Internet available'), tone: 'ok' };
}
function knownLess(d) {
	var p = (d || {}).profile, fit = p && p.fit;
	if (!d || d.present === false || d.detection_status !== 'candidate') return '';
	if (!p) return _('MedveModem does not know this model: it has no profile, only the common things work.');
	if (fit === 'generic') return _('The model is recognised by its connection type only (the generic profile): some functions and recovery steps are not available.');
	if (fit === 'family') return _('The modem family is known, but not this model: some functions may work differently.');
	if (fit === 'ambiguous') return _('Several profiles fit the modem, MedveModem cannot pick one.');
	return '';
}

/* the data protocol (the owner, 2026-10-30: «колонку типа протокола, mbim, ppp, ndis, rndis, qmi»): a dial-up session's
 * PPP, else the driver of the modem's network interface; '' when neither says */
var PROTO_DRIVERS = [['cdc_mbim', 'MBIM'], ['qmi_wwan', 'QMI'], ['qmi_wwan_q', 'QMI'], ['huawei_cdc_ncm', 'NCM'], ['cdc_ncm', 'NCM'],
	['mhi_net', 'MHI'], ['mtk_t7xx', 'T7XX'], ['rndis_host', 'RNDIS'], ['cdc_ether', 'ECM'], ['ipheth', 'iPhone'], ['option', 'PPP']];
function protoOf(d) {
	var sp = String(((d || {}).session || {}).proto || '');
	if (sp === '3g' || sp === 'ppp') return 'PPP';
	var drivers = (Array.isArray((d || {}).interfaces) ? d.interfaces : []).map(function(i) { return i && i.driver; });
	for (var i = 0; i < PROTO_DRIVERS.length; i++)
		if (drivers.indexOf(PROTO_DRIVERS[i][0]) >= 0) return PROTO_DRIVERS[i][1];
	return '';
}
/* the engine's AT reading (metrics) in ModemManager's terms, for the hero of a modem ModemManager does not own */
var AT_TECH = { lte: ['lte'], 'nr-nsa': ['lte', '5gnr'], nr: ['5gnr'], 'nr-sa': ['5gnr'], umts: ['umts'], hspa: ['hspa'], gsm: ['gsm'], edge: ['edge'] };
function atAsMm(m) {
	if (!m) return null;
	var lte = {}, nr = {}, n = m.nr || {}, sig = {};
	if (typeof m.rsrp_dbm === 'number') lte.rsrp = m.rsrp_dbm;
	if (typeof m.rsrq_db === 'number') lte.rsrq = m.rsrq_db;
	/* a modem's own web (HANDOFF 164) gives the SINR and the RSSI too, and an NSA modem's 5G cell apart */
	/* a UZ801 gives only the RSSI (2026-10-30): shown on its own, as LTE when the modem says LTE or nothing */
	if (typeof m.rssi_dbm === 'number' && (Object.keys(lte).length || !m.access_technology || m.access_technology === 'lte')) lte.rssi = m.rssi_dbm;
	if (typeof m.sinr_db === 'number' && Object.keys(lte).length) lte.snr = m.sinr_db;
	if (typeof n.rsrp_dbm === 'number') nr.rsrp = n.rsrp_dbm;
	if (typeof n.rsrq_db === 'number') nr.rsrq = n.rsrq_db;
	if (typeof n.sinr_db === 'number') nr.snr = n.sinr_db;
	if (Object.keys(lte).length) sig.lte = lte;
	if (Object.keys(nr).length) sig['5g'] = nr;
	/* +COPS in numeric form gives the network's code (25099): the badge names it (bench 2026-10-19, the FM350) */
	var code = /^\d{5,6}$/.test(String(m.operator || '')) ? String(m.operator) : m.operator_code;
	var named = code && OPS[code] ? OPS[code][0] : '';
	/* a reading with an operator is a modem in the network: the bars light up (the owner, 2026-10-20: a HiLink's bars
	 * were grey with an «×», nothing in the reading said «registered») */
	/* a web modem's serving cell in ModemManager's cell shape (the owner's E3372, 2026-10-30: bands, cell, base station
	 * and firmware stayed dashes though its web gave them): the band number, the channel, PCI, the ECI in hex, TAC */
	var band = /^[Bn](\d+)$/.exec(String(m.band || '')), tech = m.access_technology === 'umts' ? 'umts' : m.access_technology === 'gsm' ? 'gsm' : 'lte';
	var ci = typeof m.cell_id === 'number' && m.cell_id > 0 ? m.cell_id.toString(16).toUpperCase() : undefined;
	var cells = band || m.earfcn != null || m.pci != null || ci ? [{ type: tech, serving: true, role: 'serving', band: band ? Number(band[1]) : undefined,
		earfcn: typeof m.earfcn === 'number' ? m.earfcn : undefined, pci: m.pci != null ? String(m.pci) : undefined, ci: ci,
		tac: typeof m.tac === 'number' ? m.tac.toString(16).toUpperCase() : undefined, plmn: code || undefined,
		rsrp: m.rsrp_dbm, rsrq: m.rsrq_db, rssi: m.rssi_dbm, sinr: m.sinr_db, bandwidth: m.bandwidth_mhz }] : undefined;
	var location = code || ci || m.tac != null ? { mcc: code ? String(code).slice(0, 3) : undefined, mnc: code ? String(code).slice(3) : undefined,
		tac: typeof m.tac === 'number' ? m.tac.toString(16).toUpperCase() : undefined, cid: ci } : undefined;
	return { operator: code === m.operator ? named || m.operator : m.operator, operator_code: code,
		cells: cells, location: location, firmware: m.firmware, model: m.model,
		access_tech: AT_TECH[m.access_technology] || (m.access_technology ? [m.access_technology] : []),
		registration: m.operator || code ? 'home' : undefined, signal: sig, via_at: true,
		/* a modem's own web names its bands: the primary, the aggregated ones, an NSA modem's 5G (the cards' CA line) */
		bands: [m.band].concat(Array.isArray(m.ca) ? m.ca : [], [n.band]).filter(function(b) { return typeof b === 'string' && b; }),
		band_mhz: typeof m.bandwidth_mhz === 'number' ? m.bandwidth_mhz : undefined,
		/* the FM350's carriers from AT+GTCAINFO? (the engine reads it beside the metrics), in ModemManager's QMI shape */
		ca: m.ca && Array.isArray(m.ca.cells) ? m.ca : undefined };
}
/* A note in the page's corner (the owner, 2026-10-20: «процесс починки … никак не отображается после нажатия кнопки, и
 * вообще всплывало бы»): one per key, replaced in place; tone ok|warn|err|run; ttl ms, 0 keeps it until replaced */
function toast(key, words, tone, ttl) {
	/* plain DOM: this file is also loaded where LuCI's E() is not passed in */
	var k = String(key).replace(/["\\]/g, ''), box = document.querySelector('.mm-toasts');
	if (!box) {
		box = document.createElement('div');
		box.className = 'mm-toasts'; box.setAttribute('role', 'status'); box.setAttribute('aria-live', 'polite');
		/* inside LuCI's #view, not on <body>: a theme that swaps views without a reload (footstrap) drops it with the
		 * page (footstrap's styling guide, §7); never inside .mm-root, a container query root that would hold a fixed box */
		(document.getElementById('view') || document.body).appendChild(box);
	}
	var t = box.querySelector('[data-key="' + k + '"]');
	if (!words) { if (t) t.remove(); return; }
	if (!t) {
		t = document.createElement('div');
		t.setAttribute('data-key', k);
		var span = document.createElement('span'), x = document.createElement('button');
		span.className = 'mm-toast-text';
		x.type = 'button'; x.className = 'mm-toast-x'; x.setAttribute('aria-label', _('Close')); x.textContent = '×';
		x.addEventListener('click', function() { t.remove(); });
		t.appendChild(span); t.appendChild(x);
		box.appendChild(t);
	}
	t.className = 'mm-toast is-' + (tone || 'run');
	t.firstChild.textContent = words;
	clearTimeout(t.mmTimer);
	if (ttl) t.mmTimer = setTimeout(function() { t.remove(); }, ttl);
	return t;
}

/* the page's files still of two versions after the one fresh fetch (a proxy's or the browser's cache): said, not hidden
 * (the old page used to render silently) */
function staleWarn(viewVersion) {
	setTimeout(function() {
		toast('stale-files', _('The page\'s files on the router and in the browser are of different versions (page %s, shared module %s). Reload the page clearing the cache: Ctrl+F5 or Cmd+Shift+R.')
			.format(viewVersion, VERSION), 'err');
	}, 0);
}

/* a recovery step's start and end said in the corner, from the device views a page polls anyway */
var recSeen = {};
function recoveryNotes(devices, nameOf) {
	(devices || []).forEach(function(d) {
		var id = d && d.device_id, r = (d && d.recovery) || {}, name = nameOf ? nameOf(d) : id, was = recSeen[id];
		if (!id) return;
		var running = r.state === 'running', stage = (STAGES[r.current_level] || [r.current_level || _('recovery')])[0];
		var last = list(r.recent).slice(-1)[0] || null, lastKey = last ? last.t + '/' + last.name : '';
		if (running) {
			var p = r.progress || {};
			toast('rec:' + id, _('%s: “%s” — running').format(name, stage) + (p.elapsed_s != null ? ', ' + _('%d s').format(p.elapsed_s) : '') +
				(p.waiting ? ' (' + _('waiting for: %s').format(p.waiting) + ')' : '') + '…', 'run', 0);
		} else if (was && (was.running || was.last !== lastKey) && last && lastKey !== (was && was.last)) {
			var ok = last.result === 'succeeded';
			toast('rec:' + id, _('%s: “%s” — %s').format(name, (STAGES[last.name] || [last.name])[0], ok ? _('done') : _('did not help') +
				(last.detail ? ': ' + last.detail : '')), ok ? 'ok' : 'err', ok ? 8000 : 20000);
		} else if (was && was.running) {
			toast('rec:' + id, _('%s: recovery finished').format(name), 'ok', 8000);
		}
		recSeen[id] = { running: running, last: lastKey };
	});
}

/* A check that may outlast rpcd's ~20 s for a page's call (the service tester, DNS and network checks, a report):
 * started in the background on the router (/usr/libexec/medvemodem/bg), then asked for every 1.5 s. Resolves as
 * fs.exec does, { code, stdout }; an engine without bg gets the plain call. fs is the page's own module. */
var BG = '/usr/libexec/medvemodem/bg';
function execLong(fs, key, cmd, args, maxMs) {
	args = args || [];
	/* a job of its own: a second tab, or a second click before the first answer, no longer takes the first one's
	 * files away (second audit 2026-10-21, R01) */
	key += '-' + (Date.now().toString(36) + Math.random().toString(36).slice(2)).slice(-10).replace(/[^a-z0-9]/g, '0');
	return fs.exec(BG, ['start', key, cmd].concat(args)).then(function(r) {
		if (!r || r.code || String(r.stdout || '').indexOf('started') !== 0)
			return fs.exec(cmd, args);
		var until = Date.now() + (maxMs || 300000);
		return new Promise(function(resolve) {
			(function ask() {
				fs.exec(BG, ['get', key]).then(function(g) {
					var out = String((g && g.stdout) || ''), nl = out.indexOf('\n'), head = nl < 0 ? out : out.slice(0, nl);
					var m = /^done (\d+)(?: (\d+))?/.exec(head), bytes, at;
					if (m) {
						/* the error output comes last, its length in bytes on the first line */
						bytes = new TextEncoder().encode(nl < 0 ? '' : out.slice(nl + 1));
						at = Math.max(0, bytes.length - (+m[2] || 0));
						return resolve({ code: +m[1], stdout: new TextDecoder().decode(bytes.slice(0, at)),
							stderr: new TextDecoder().decode(bytes.slice(at)) });
					}
					if (head === 'none' || Date.now() > until)
						return resolve({ code: 124, stdout: '' });
					window.setTimeout(ask, 1500);
				}, function() {
					if (Date.now() > until)
						return resolve({ code: 124, stdout: '' });
					window.setTimeout(ask, 1500);
				});
			})();
		});
	}, function() { return fs.exec(cmd, args); });
}

/* a banner above the page closes with ×; closed, it stays out of sight while its words stay the same and is listed in
 * the modem's journal with «Show again» (the owner, 2026-10-31: «для всех балунов сверху надо иметь крестик для
 * удаления, пусть они в событиях висят в логе»). Per browser: localStorage mm:dismissed {key: {sig, text, at}} */
var DISMISS_KEY = 'mm:dismissed';
function dismissedAll() {
	try { var v = JSON.parse(localStorage.getItem(DISMISS_KEY) || '{}'); return v && typeof v === 'object' ? v : {}; } catch (e) { return {}; }
}
function dismissedSave(v) { try { localStorage.setItem(DISMISS_KEY, JSON.stringify(v)); } catch (e) { /* the banner just comes back */ } }
/* the banner's words without its × */
function bannerSig(node) {
	var t = '';
	Array.prototype.forEach.call(node.childNodes, function(c) { if (!(c.classList && c.classList.contains('mm-dismiss'))) t += c.textContent; });
	return t.replace(/\s+/g, ' ').trim().slice(0, 400);
}
/* adds the × to a banner node once; key: «<device id or router>:<what>» */
function dismissible(node, key, onChange) {
	if (node.mmDismissKey) return node;
	node.mmDismissKey = key;
	node.classList.add('mm-dismissible');
	node.appendChild(E('button', { type: 'button', 'class': 'mm-dismiss', title: _('Hide this notice'), 'aria-label': _('Hide this notice'), click: function() {
		var v = dismissedAll(), sig = bannerSig(node);
		v[key] = { sig: sig, text: sig, at: Math.floor(Date.now() / 1000) };
		/* at most 50 kept: the oldest go */
		var ks = Object.keys(v).sort(function(a, b) { return (v[a].at || 0) - (v[b].at || 0); });
		while (ks.length > 50) delete v[ks.shift()];
		dismissedSave(v);
		node.hidden = true;
		if (onChange) onChange();
	} }, '×'));
	return node;
}
/* show a banner unless the operator hid these very words */
function bannerShow(node, show) {
	var d = show && node.mmDismissKey ? dismissedAll()[node.mmDismissKey] : null;
	node.hidden = !show || !!(d && d.sig === bannerSig(node));
	return !node.hidden;
}
/* the hidden banners whose key starts with prefix, newest first: [{key, text, at}] */
function dismissedFor(prefix) {
	var v = dismissedAll();
	return Object.keys(v).filter(function(k) { return k.indexOf(prefix) === 0; })
		.map(function(k) { return { key: k, text: v[k].text, at: v[k].at }; }).sort(function(a, b) { return b.at - a.at; });
}
function undismiss(key) { var v = dismissedAll(); delete v[key]; dismissedSave(v); }

/* the speed test's own words (files/usr/libexec/medvemodem/speedtest) in the page's, short: a card shows them on one line
 * (r184: «the upload failed: the server answered with an HTTP error» ran out of the card) */
var SPEED_ERR = [[/could not resolve the server/, _('the server is not found')], [/could not connect to the server/, _('the server does not answer')],
	[/the server answered with an HTTP error/, _('the server refused')], [/the TLS connection failed/, _('the secure connection failed')],
	[/the connection broke/, _('the connection broke')], [/curl is not installed/, _('curl is not installed')],
	[/no such network device/, _('the modem\'s network device is gone')]];
function speedErrWord(e) {
	var s = String(e || '');
	for (var i = 0; i < SPEED_ERR.length; i++) if (SPEED_ERR[i][0].test(s)) return SPEED_ERR[i][1];
	return _('the transfer failed');
}
/* what did not measure, each way: «приём: сервер не ответил», «отдача: …»; '' when both measured */
function speedErr(j) {
	if (!j || typeof j !== 'object') return speedErrWord(j);
	var out = [];
	if ((j.down || {}).error) out.push(_('download: %s').format(speedErrWord(j.down.error)));
	if ((j.up || {}).error) out.push(_('upload: %s').format(speedErrWord(j.up.error)));
	if (!out.length && j.error) out.push(speedErrWord(j.error));
	return out.join('; ');
}

/* a message's text with its web links clickable (the owner, 2026-11-15): http(s) only, the whole address shown as it
 * came (a link in an SMS may be a lure: the reader sees where it goes), a new tab without the page's referrer */
function smsLinked(value) {
	var w = words(value), out = [], re = /https?:\/\/[^\s<>"']+/gi, at = 0, m;
	while ((m = re.exec(w))) {
		var url = m[0].replace(/[.,;:!?»)\]]+$/, '');
		if (m.index > at) out.push(w.slice(at, m.index));
		out.push(E('a', { href: url, target: '_blank', rel: 'noopener noreferrer', 'class': 'mm-link mm-sms-url' }, url));
		at = m.index + url.length;
		re.lastIndex = at;
	}
	if (at < w.length) out.push(w.slice(at));
	return out;
}
/* what a conversation is found by: the number, the contact's name, any message's text (case aside) */
function smsMatches(query, num, name, msgs) {
	var q = String(query || '').trim().toLowerCase();
	if (!q) return true;
	return [num, name].concat(msgs.map(function(m) { return words(m.text); })).some(function(x) {
		return String(x || '').toLowerCase().indexOf(q) >= 0; });
}
/* the one-time code in a message, for the «Код» button (the owner, 2026-10-21, after the second audit: as a phone does):
 * 4–8 digits (also «123-456») next to «код», «code», «пароль», «password»; else the only such group of a short message.
 * An amount (followed by «р», «руб», «₽») or a phone number is not a code. '' for none. */
function smsCode(value) {
	var w = words(value), m, all = [], re = /(^|[^0-9+.,])([0-9]{3}[- ][0-9]{3}|[0-9]{4,8})(?![0-9]|[.,][0-9]|\s*(?:р\b|р\.|руб|₽|rub|%))/gi;
	if (!w) return '';
	while ((m = re.exec(w))) all.push({ code: m[2].replace(/[- ]/g, ''), at: m.index + m[1].length });
	if (!all.length) return '';
	var keyed = all.filter(function(c) {
		return /(код|code|пароль|password|parol|kod)[^0-9]{0,40}$/i.test(w.slice(Math.max(0, c.at - 48), c.at)) ||
			/^[^0-9]{0,24}(код|code)/i.test(w.slice(c.at + c.code.length, c.at + c.code.length + 32));
	});
	if (keyed.length) return keyed[0].code;
	return all.length === 1 && w.length <= 160 && /(код|code|пароль|password|подтвержд|verif|otp)/i.test(w) ? all[0].code : '';
}
/* text to the clipboard; LuCI is mostly opened over http, where navigator.clipboard is missing: a hidden textarea then */
function copyText(t) {
	if (navigator.clipboard && window.isSecureContext)
		return navigator.clipboard.writeText(t);
	return new Promise(function(resolve, reject) {
		var a = E('textarea', { readonly: '', style: 'position:fixed;top:0;left:0;opacity:0' });
		a.value = t;
		document.body.appendChild(a);
		a.select();
		var ok = false;
		try { ok = document.execCommand('copy'); } catch (e) { /* refused */ }
		a.remove();
		ok ? resolve() : reject(new Error('copy refused'));
	});
}
/* an operator's own sender (the owner, 2026-11-15: «подсвечивать чаты/сообщения именно от операторов»): its letter name
 * as the networks write it (letters only: a short number of the operator is not known for sure yet); a bank's or a
 * shop's letter name is not one */
var SMS_OPERATORS = /^(megafon|мегафон|beeline|билайн|mts|мтс|tele2|t2|yota|motiv|мотив|rostelecom|ростелеком|sbermobile|сбермобайл|t-mobile|tinkoff ?mobile|тинькофф ?мобайл|letai|летай|danycom|volna|волна)$/i;
function smsOperator(sender) {
	var s = words(sender).trim();
	return !!s && SMS_OPERATORS.test(s);
}

/* Manager badges are an optional browser presentation preference, off by default.
 * These only describe the resolved control owner; never infer ownership from
 * the presence of a QModem/ModemManager process elsewhere on the router. */
function ownerBadgesEnabled() {
	try { return localStorage.getItem('mm:owner-badges') === '1'; } catch (e) { return false; }
}
function ownerBadgeInfo(owner) {
	var b = {
		modemmanager: ['MM', 'ModemManager', 'mm'],
		qmodem: ['QM', 'QModem', 'qm'],
		'netifd-qmi': ['MV', 'MedveModem (native QMI)', 'native'],
		'netifd-mbim': ['MV', 'MedveModem (native MBIM)', 'native'],
		'medvemodem-at': ['MV', 'MedveModem (native AT)', 'native'],
		native: ['MV', 'MedveModem (native)', 'native'],
		medvemodem: ['MV', 'MedveModem', 'native'],
""	};
	return b[owner] || null;
}
function setOwnerBadge(node, owner) {
	var info = ownerBadgesEnabled() ? ownerBadgeInfo(owner) : null;
	node.hidden = !info;
	if (!info) { node.textContent = ''; node.removeAttribute('title'); return; }
	node.textContent = info[0];
	node.title = info[1];
	node.setAttribute('aria-label', info[1]);
	node.className = 'mm-owner-tag is-' + info[2];
}

return baseclass.extend({
	smsLinked: smsLinked, smsMatches: smsMatches, smsCode: smsCode, copyText: copyText, smsOperator: smsOperator,
	busy: busy, speedErr: speedErr,
	toast: toast, recoveryNotes: recoveryNotes, execLong: execLong, staleWarn: staleWarn,
	ownerBadgesEnabled: ownerBadgesEnabled, ownerBadgeInfo: ownerBadgeInfo, setOwnerBadge: setOwnerBadge,
	VERSION: VERSION, plural: plural, LANG: LANG, langArg: langArg, langCode: langCode, num: num, CHAIN_LINKS: CHAIN_LINKS, CHAIN_WHY: CHAIN_WHY, dnsDetail: dnsDetail, reachError: reachError, chainLine: chainLine,
	text: text,
	failText: failText,
	duration: duration,
	bytes: bytes,
	TECH_WORDS: TECH_WORDS,
	techWords: techWords,
	techExact: techExact,
	REASONS: REASONS, STAGES: STAGES, EVENTS: EVENTS, FOLLOW_WHAT: FOLLOW_WHAT, WHO: WHO, eventWords: eventWords, RESULTS: RESULTS,
	TECH: TECH,
	signalIcon: signalIcon, signalPct: signalPct,
	CUTS: CUTS,
	cutsOf: cutsOf,
	zoneOf: zoneOf,
	fillOf: fillOf,
	sinrOf: sinrOf,
	OPS: OPS,
	opBadge: opBadge,
	atAsMm: atAsMm,
	protoOf: protoOf,
	LEVELS: LEVELS,
	LEVEL_HINT: LEVEL_HINT,
	level: level,
	setLevel: setLevel,
	CHEV: CHEV,
	chev: chev,
	pageHead: pageHead,
	list: list,
	ncRows: ncRows, ncSummary: ncSummary, ncTable: ncTable, NC_ERROR: NC_ERROR, ncCell: ncCell, ncName: ncName,
	dnsRows: dnsRows, dnsSummary: dnsSummary, dnsTable: dnsTable,
	privacy: privacy,
	secret: secret,
	barsNode: barsNode,
	setBars: setBars,
	TONE_PILL: TONE_PILL,
	secOpen: secOpen,
	tabsHint: tabsHint,
	section: section,
	OPS: OPS,
	profileTitle: profileTitle,
	knownLess: knownLess,
	checkState: checkState,
	NEW_MODEM_ISSUE: NEW_MODEM_ISSUE,
	tempOf: tempOf,
	tempCommand: tempCommand,
	tempCommands: tempCommands,
	tempAsk: tempAsk,
	tempPrefs: tempPrefs,
	tempRecord: tempRecord,
	tempChart: tempChart,
	TEMP_EVERY_S: TEMP_EVERY_S,
	cacheGet: cacheGet,
	cacheSet: cacheSet,
	secRemember: secRemember,
	dismissible: dismissible, bannerShow: bannerShow, dismissedFor: dismissedFor, undismiss: undismiss
});
