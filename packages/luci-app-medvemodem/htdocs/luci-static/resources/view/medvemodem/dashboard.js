'use strict';
'require view';
'require rpc';
'require poll';
'require fs';
'require medvemodem.common as mm';

var CSS_VERSION = mm.VERSION, privacy = mm.privacy, cutsOf = mm.cutsOf, zoneOf = mm.zoneOf, fillOf = mm.fillOf, sinrOf = mm.sinrOf, APP_VERSION = mm.VERSION, text = mm.text, duration = mm.duration, bytes = mm.bytes, techWords = mm.techWords, techExact = mm.techExact, eventWords = mm.eventWords, signalIcon = mm.signalIcon, opBadge = mm.opBadge, level = mm.level, CHEV = mm.CHEV, chev = mm.chev, pageHead = mm.pageHead, barsNode = mm.barsNode, setBars = mm.setBars, TONE_PILL = mm.TONE_PILL, chainLine = mm.chainLine, cacheGet = mm.cacheGet, cacheSet = mm.cacheSet, num = mm.num, LANG = mm.LANG, mmc = mm;

var getStatus = rpc.declare({ object: 'medvemodem', method: 'status', reject: true });
var getDevices = rpc.declare({ object: 'medvemodem', method: 'devices', params: ['absent'], reject: true });
/* disconnected modems taken off the list in this browser: mm:hidden-gone [device ids]; set: true adds, false removes */
/* the «! N» dismissed by hand (the owner, 2026-11-09: «сбрасывать этот бейдж надо разрешить вручную»): the time of the
 * last repair it showed, per modem, in this browser; a newer repair shows it again */
function fixSeen(id, at) {
	var v = {};
	try { v = JSON.parse(localStorage.getItem('mm:fix-seen') || '{}') || {}; } catch (e) { v = {}; }
	if (at === undefined) return typeof v[id] === 'number' ? v[id] : 0;
	v[id] = at;
	var keys = Object.keys(v);
	if (keys.length > 64) delete v[keys[0]];
	try { localStorage.setItem('mm:fix-seen', JSON.stringify(v)); } catch (e) { /* this page only */ }
	return at;
}
function hiddenGone(id, set) {
	var v = [];
	try { v = JSON.parse(localStorage.getItem('mm:hidden-gone') || '[]'); if (!Array.isArray(v)) v = []; } catch (e) { v = []; }
	var has = v.indexOf(id) >= 0;
	if (set === undefined) return has;
	if (set === has) return has;
	v = set ? v.concat([id]).slice(-64) : v.filter(function(x) { return x !== id; });
	try { localStorage.setItem('mm:hidden-gone', JSON.stringify(v)); } catch (e) { /* this page only */ }
	return set;
}
var getUplink = rpc.declare({ object: 'medvemodem', method: 'uplink', reject: true });
/* «Порты и устройства» (r84, docs/PORTS-AND-DEVICES.md): asked only while the card is shown */
var getUsbTree = rpc.declare({ object: 'medvemodem', method: 'usbtree', reject: true });
var getNeighbours = rpc.declare({ object: 'medvemodem', method: 'neighbours', params: ['refresh'], reject: true });
var getUsbDetail = rpc.declare({ object: 'medvemodem', method: 'usbtree_detail', params: ['path'], reject: true });
/* «Это модем / не модем» by hand (the owner, 2026-11-09), by VID:PID: modem, not_modem or auto */
var setUsbClass = rpc.declare({ object: 'medvemodem', method: 'usb_class', params: ['vidpid', 'class'], reject: true });
/* a port's name: config usb_port, option path and option name, which the engine shows as the node's label */
var uciGet = rpc.declare({ object: 'uci', method: 'get', params: ['config', 'type'], reject: true });
var uciAdd = rpc.declare({ object: 'uci', method: 'add', params: ['config', 'type', 'values'], reject: true });
var uciSet = rpc.declare({ object: 'uci', method: 'set', params: ['config', 'section', 'values'], reject: true });
var uciDelete = rpc.declare({ object: 'uci', method: 'delete', params: ['config', 'section'], reject: true });
var uciCommit = rpc.declare({ object: 'uci', method: 'commit', params: ['config'], reject: true });
var forgive = rpc.declare({ object: 'medvemodem', method: 'failover_forgive', params: ['device'], reject: true });
var connect = rpc.declare({ object: 'medvemodem', method: 'connect', params: ['id', 'up', 'who'], reject: true });
var bringup = rpc.declare({ object: 'medvemodem', method: 'bringup', params: ['id'], reject: true });
var setPriority = rpc.declare({ object: 'medvemodem', method: 'priority', params: ['order', 'set'], reject: true });
var freeze = rpc.declare({ object: 'medvemodem', method: 'freeze', params: ['id', 'on', 'who'], reject: true });
var getMetrics = rpc.declare({ object: 'medvemodem', method: 'metrics', params: ['id', 'max_age_ms'], reject: true });
var getPresets = rpc.declare({ object: 'medvemodem', method: 'presets', params: ['id'], reject: true });
var atCommand = rpc.declare({ object: 'medvemodem', method: 'at_command', params: ['id', 'command'], reject: true });
var webOpen = rpc.declare({ object: 'medvemodem', method: 'web_open', params: ['id'], reject: true });
/* «Все SMS»: the engine's kept list of each modem, a peek (nothing marked seen: the modem page keeps its unread marks) */
var smsPeek = rpc.declare({ object: 'medvemodem', method: 'sms_list', params: ['id', 'refresh', 'peek'], reject: true });

/* the driver of the modem's control function -> its protocol */
/* what the engine repaired by itself (device.autofix: the newest of the last hour), said on the card */
var AUTOFIX = { 'interface-retry': _('the interface brought up again after a failed connection'),
	'sms-draft-dropped': _('a stuck unsent SMS deleted, it blocked receiving'),
	'sms-stuck-dropped': _('an incomplete SMS deleted, it blocked receiving'),
	'iface-repair': _('the network interface settings repaired'), ifup: _('the interface brought up when ModemManager took the modem again'),
	'mm-kick': _('the modem handed to ModemManager again'), 'recovery-end': _('the link restored'),
	'recovery-no-internet': _('a recovery brought the session up, but there is no internet') };
/* the kernel's trouble in words (the owner, 2026-10-31, after the R3S hung: «HC died и failed to set voltage — да обязательно
 * и не только для r3s»): what it means for the modems, and who said it */
var KFAULT = { 'hc-died': _('the USB controller %s died: the modems on it are gone until the router restarts'),
	voltage: _('the board could not set a voltage (%s): usually the power supply or the power chip'),
	undervoltage: _('the supply voltage sags (%s): a weak power supply or cable'),
	stall: _('a processor core hung (%s)'), lockup: _('a processor core hung (%s)'), oops: _('a kernel error (oops)%s'),
	panic: _('a kernel panic%s'), overheat: _('critical temperature (%s)'), 'hung-task': _('a task hung for minutes (%s)'),
	oom: _('out of memory: the kernel stopped %s'), 'fs-error': _('storage errors (%s)') };
var KFAULT_ORDER = ['hc-died', 'voltage', 'undervoltage', 'overheat', 'stall', 'lockup', 'oops', 'panic', 'hung-task', 'oom', 'fs-error'];
function kfaultLines(kinds, when) {
	return KFAULT_ORDER.filter(function(k) { return kinds && kinds[k]; }).map(function(k) {
		var x = kinds[k], w = KFAULT[k].format(x.who || '').replace(' ()', '').replace(/\s+/g, ' ').replace(/: $/, ''), at = typeof x.first === 'number' ? when(x.first) : '';
		/* the count only in the hint: a closed banner stays closed while a stall repeats */
		return E('li', { title: x.count > 1 ? N_(x.count, '%d time', '%d times').format(x.count) : '' }, w.charAt(0).toUpperCase() + w.slice(1) + (at ? ' — ' + at : ''));
	});
}
function hhmm(t) { return new Date(t * 1000).toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit' }); }
/* [the card's line, the badge's last one] */
function autofixLine(a) {
	/* HANDOFF 200 item 10: a recovery brought the session back but not the internet */
	if (a && a.kind === 'recovery-no-internet') {
		var said = _('The recovery at %s brought the session up, but there is no internet.').format(hhmm(a.at));
		return [said, said];
	}
	if (!a || !AUTOFIX[a.kind]) return ['', ''];
	return [_('Restored automatically at %s: %s.').format(hhmm(a.at), AUTOFIX[a.kind]), _('at %s: %s.').format(hhmm(a.at), AUTOFIX[a.kind])];
}

/* why netifd gave up on an interface (proto modemmanager's error codes); the engine brings it up again */
var NETIFD_WHY = { MM_CONNECT_FAILED: _('the modem could not connect'), MM_INTERFACE_RESTART: _('reconnecting'),
	MM_FAILED_REASON_SIM_MISSING: _('no SIM card'), MM_PINCODE_REQUIRED: _('a PIN is needed'), MM_CHECK_UNLOCK_PIN: _('the SIM card is locked') };
var PAUSE_WHY = { 'the modem has no network interface yet': _('the modem has no network interface yet'), 'a recovery is running on this modem': _('a recovery is running'),
	'a bring-up is running': _('a bring-up is running'), 'a configuration change is being applied': _('settings are being applied'), 'netifd did not take the request': _('netifd did not take the command'),
	'an eSIM operation holds this modem': _('an eSIM operation is running'), 'the modem is frozen by the operator': _('the modem management is paused: resume it first'),
	'a bring-up is running on this modem': _('this modem is being brought up'), 'device not present': _('the modem is not plugged in'),
	'another configuration change is being applied': _('other settings are being applied now, try again in a few seconds'),
	'the modem is not known to the registry': _('the engine does not know this modem (unplugged or removed)') };
var CTL_PROTO = { qmi_wwan: 'QMI', cdc_mbim: 'MBIM', cdc_ncm: 'NCM', huawei_cdc_ncm: 'NCM', rndis_host: 'RNDIS', cdc_ether: 'ECM', option: 'AT', cdc_acm: 'AT' };
function words(value) {
	return typeof value === 'string' && value !== '--' ? value : '';
}
function list(value) { return Array.isArray(value) ? value : []; }
/* a channel that is not a modem by what it is (the owner, 2026-10-25: «проводной wan, pppoe, wan1, tun»): the interface's
 * protocol, its name when it is not plain «wan»; a tunnel's device by its own name */
function wireName(p, dev) {
	var iface = words(p && p.interface), proto = words(p && p.proto), d = words(dev || (p && p.device));
	var KIND = { pppoe: 'PPPoE', pppoa: 'PPPoA', l2tp: 'L2TP', pptp: 'PPTP' };
	if (iface) {
		if (KIND[proto]) return iface === 'wan' ? KIND[proto] : _('%s “%s”').format(KIND[proto], iface);
		return iface === 'wan' ? _('wired WAN') : _('wired WAN “%s”').format(iface);
	}
	if (/^(tun|tap|wg|utun)/.test(d)) return _('tunnel %s').format(d);
	return d || _('not a modem');
}
/* the carriers a modem uses now, for the cards' CA line (the owner, 2026-10-23: «агрегацию прямо на карточке»), from what
 * there is: QMI's carrier aggregation (with ModemManager's 5G cell beside it), else ModemManager's serving cells, else the
 * bands a modem's own web names. [{name: «B3», mhz}] */
function carriers(mm) {
	var off = function(x) { return x.state === 'deactivated' || x.state === 'deconfigured'; };
	var mhz = function(v) { return typeof v !== 'number' || v <= 0 ? 0 : v >= 1000 ? v / 1000 : v <= 100 ? v : 0; };
	var cell = function(x) { return { name: x.band ? (x.type === '5gnr' ? 'n' : 'B') + x.band : x.type === '5gnr' ? '5G' : 'LTE', mhz: mhz(x.bandwidth) }; };
	var serving = list(mm.cells).filter(function(x) { return x && x.serving && !off(x); });
	var ca = list((mm.ca || {}).cells).filter(function(x) { return x && (x.role === 'primary' || x.role === 'secondary') && !off(x); });
	/* QMI's cells are LTE; the FM350's (AT+GTCAINFO?) name their kind and carry a 5G one themselves */
	if (ca.length)
		return ca.map(function(x) { return { name: x.band ? (x.type === '5gnr' ? 'n' : 'B') + x.band : x.type === '5gnr' ? '5G' : 'LTE', mhz: mhz(x.bandwidth_mhz) }; })
			.concat(ca.some(function(x) { return x.type === '5gnr'; }) ? [] : serving.filter(function(x) { return x.type === '5gnr'; }).map(cell));
	if (serving.length) return serving.map(cell);
	return list(mm.bands).map(function(b, i) { return { name: b, mhz: i === 0 && mm.band_mhz || 0 }; });
}
/* what only the operator can fix (the engine's action_needed, HANDOFF 176): said on the dashboard, done on the modem page */
var ACTIONS = { drivers: _('a driver is missing'), modemmanager: _('ModemManager is not installed'),
	'modeswitch-rule': _('a usb-modeswitch rule resets the modem'), 'modeswitch-pending': _('the modem is in flash drive mode, it needs switching'),
	'sim-pin': _('the SIM card waits for its PIN'), 'sim-puk': _('the SIM card is locked, a PUK is needed'),
	'fcc-unlock': _('the radio is off, an FCC unlock is needed'),
	'foreign-interface': _('an interface set up outside MedveModem drives it: hand it over on the modem page') };
/* the packages the neighbours helper knows: the name and what it does to a modem (read in their sources, 2026-10-28) */
var NEIGHBOUR = {
	'5gmodem': ['luci-app-5gmodem', _('its hotplug scripts switch USB modes and rebind drivers, its services restart interfaces')],
	'usb-reinit': [_('USB re-init scripts'), _('they reset the USB bus or the modem when it is plugged in')],
	'modeminfo': ['modeminfo', _('polls the modem over AT')],
	'smstools3': ['smstools3', _('keeps a modem port open for SMS')],
	'sms-tool-js': ['luci-app-sms-tool-js', _('its call log service keeps the AT port open; it reads SMS over AT')],
	'sms-tool': ['sms-tool', _('reads SMS and sends AT over the modem port')],
	'3ginfo-lite': ['luci-app-3ginfo-lite', _('polls the modem over AT while its page is open')],
	'3ginfo': ['3ginfo', _('polls the modem over AT while its page is open')],
	'modemband': ['modemband', _('writes the bands to the modem directly')],
	'modemdata': ['luci-app-modemdata', _('polls the modem over AT')],
	'lite-watchdog': ['luci-app-lite-watchdog', _('its own ping watchdog restarts the interface or reboots the router')],
	'easyconfig': ['easyconfig', _('its own modem detection and watchdog')],
	'rooter': ['ROOter', _('a modem stack of its own')],
	'luci-app-modem': ['luci-app-modem', _('a modem stack of its own')] };
function rate(bps) {
	if (typeof bps !== 'number' || bps < 0) return '';
	var bits = bps * 8;
	if (bits >= 1e6) return _('%s Mbit/s').format(num(bits / 1e6, bits >= 1e7 ? 0 : 1));
	return _('%s kbit/s').format(Math.round(bits / 1e3));
}
/* the peak since the router booted, «7,0 / 1,2 Мбит/с» (spec §42.2.1): one unit, by the larger of the two */
function peakText(pk) {
	if (!pk || typeof pk.rx_bps !== 'number') return '—';
	var big = Math.max(pk.rx_bps, pk.tx_bps || 0), mb = big >= 1e6, f = function(b) { return mb ? num(b / 1e6, b >= 1e7 ? 0 : 1) : num(Math.round(b / 1e3)); };
	return (mb ? _('%s / %s Mbit/s') : _('%s / %s kbit/s')).format(f(pk.rx_bps), f(pk.tx_bps || 0));
}
/* the arrow stays with its number: a narrow column breaks between the two pairs only (the owner, 2026-10-26) */
function keep(v) { return String(v).replace(/ /g, '\u00a0'); }
function rates(traffic) {
	var t = traffic || {};
	return typeof t.rx_rate === 'number' || typeof t.tx_rate === 'number' ? '↓\u00a0' + keep(rate(t.rx_rate) || '—') + ' · ↑\u00a0' + keep(rate(t.tx_rate) || '—') : '';
}
function totals(bucket) {
	return bucket && typeof bucket.rx_bytes === 'number' ? '↓\u00a0' + keep(bytes(bucket.rx_bytes)) + ' · ↑\u00a0' + keep(bytes(bucket.tx_bytes)) : '—';
}
/* the SIM's monthly quota (spec 20.9), in the operator's decimal gigabytes */
function gb(n) { return _('%s GB').format(n / 1e9 < 10 ? num(n / 1e9, 1) : String(Math.round(n / 1e9))); }
function quotaWords(q) {
	return !q || q.state === 'none' || typeof q.limit_bytes !== 'number' ? '' : _('%s of %s (%s %%)').format(gb(q.used_bytes), gb(q.limit_bytes), q.percent);
}
function ago(seconds) {
	if (typeof seconds !== 'number' || seconds < 0) return '';
	return seconds < 60 ? _('%d s ago').format(seconds) : _('%d min ago').format(Math.floor(seconds / 60));
}
/* ---- «Порты и устройства» ---- */
/* the USB link speed, never traffic: «5 Гбит/с», «480 Мбит/с» */
function usbSpeed(mbps) {
	if (typeof mbps !== 'number' || mbps <= 0) return '';
	return mbps >= 1000 ? _('%s Gbit/s').format(num(mbps / 1000)) : _('%s Mbit/s').format(num(mbps));
}
var USB_KIND = { storage: _('Storage'), net: _('Network adapter'), wifi: 'Wi-Fi', hub: _('Hub'), input: _('Input device'), other: _('USB device') };
var USB_CONTROL = { modemmanager: 'ModemManager', qmodem: 'QModem', native: 'MedveModem', medvemodem: 'MedveModem', hilink: _('the modem\'s web'),
	none: _('nobody'), unresolved: _('not determined') };
var USB_BOUNCE = [['reconnects', _('reconnects in an hour')], ['reset', _('resets')], ['err71', _('errors -71')], ['err110', _('no answer (-110)')],
	['err32', _('errors -32')], ['enum', _('did not enumerate')], ['overcurrent', _('over-current protection')], ['watchdog', _('queue hangs (NETDEV WATCHDOG)')],
	['hc', _('controller failures')]];
function usbBounce(b) {
	if (!b || !b.hour) return _('no data');
	var said = USB_BOUNCE.filter(function(k) { return b.hour[k[0]] > 0; }).map(function(k) {
		return k[0] === 'reconnects' ? N_(b.hour.reconnects, '%d reconnect in an hour', '%d reconnects in an hour').format(b.hour.reconnects) :
			k[1] + ': ' + b.hour[k[0]]; });
	return said.length ? said.join(' · ') + (b.state === 'unstable' ? ' — ' + _('unstable') : '') : _('stable');
}
function usbName(n) {
	var last = n.last || n;
	return words(n.name || last.name) || words(n.product || last.product) || words(n.vidpid || last.vidpid) || n.path;
}
function usbWhen(t) {
	return typeof t === 'number' && t > 0 ? hhmm(t) : '';
}
/* a channel's role in words: the channels table and the plain USB tree */
var CHAN_ROLE = { carrying: _('carries the traffic'), backup_ready: _('backup ready'), backup_not_ready: _('backup not ready'), backup_limited: _('backup, limited access'),
	limited: _('limited access'), limited_tunnel: _('whitelist, the rest through the tunnel'), no_internet: _('no internet'), checking: _('being checked'), down: _('no session') };
function usbEventWords(e, place) {
	var who = place ? place(e.path) : e.path || '', p = words(e.product);
	switch (e.kind) {
	/* whole sentences, the place in brackets (the owner, 2026-10-26: «Подключено: UB82…», «устройство найдено, драйвера
	 * данных нет» — «не по-русски звучит») */
	case 'connected': return p ? (e.modem ? _('Modem “%s” connected (%s)') : _('Device “%s” plugged in (%s)')).format(p, who) :
		(e.modem ? _('A modem connected (%s)') : _('A device plugged in (%s)')).format(who);
	case 'disconnected': return p ? (e.modem ? _('Modem “%s” disconnected (%s)') : _('Device “%s” unplugged (%s)')).format(p, who) :
		(e.modem ? _('A modem disconnected (%s)') : _('A device unplugged (%s)')).format(who);
	case 'reconnected': return p ? _('Device “%s” (%s) was unplugged and came back between two checks').format(p, who) :
		_('The device (%s) was unplugged and came back between two checks').format(who);
	case 'hub-gone': return N_(e.devices, 'After hub %s was disconnected, %d device disappeared', 'After hub %s was disconnected, %d devices disappeared').format(who, e.devices) +
		(e.modems ? ', ' + _('of them modems: %d').format(e.modems) : '');
	case 'speed-down': return (e.from ? _('The USB link %s fell from %s to %s').format(who, usbSpeed(e.from), usbSpeed(e.to)) : _('The USB link %s fell to %s').format(who, usbSpeed(e.to)));
	/* the kernel driver the modem waits for and its package (audit r101 R04: not any unbound interface) */
	case 'no-driver': return e.driver ? _('The modem has no driver %s (%s)').format(e.driver, who) + (e.package ? '; ' + _('install %s').format(e.package) : '') :
		_('A device without a data driver (%s)').format(who);
	case 'moved': return _('The modem came back on another bus: %s → %s').format(e.from, who);
	case 'bounce': return N_(e.count, '%s reconnected %d time in %d min', '%s reconnected %d times in %d min').format(who, e.count, e.minutes);
	case 'usb-errors': return _('%s: kernel log — %s').format(who.replace(/^hc:/, _('controller') + ' ').replace(/^net:/, _('interface') + ' '),
		USB_BOUNCE.filter(function(k) { return e[k[0]]; }).map(function(k) { return k[1] + ' ' + e[k[0]]; }).join(', '));
	}
	return e.kind + (who ? ': ' + who : '');
}

/* a check worded as a check, not a proof: «connection check (wwan0, 1.1.1.1): 31 ms (updated 3 s ago)»; bare: without
 * the «connection check» words (the card's row says them) */
function checkWords(reach, bare) {
	if (!reach || typeof reach.ok !== 'boolean') return '';
	var target = String(reach.target || '').replace(/^\[|\]?:\d+$/g, '');
	var head = function(w) { return bare ? w : _('connection check (%s)').format(w); };
	if (reach.limited) return _('%s: access to the internet is limited: only %s from the second group of addresses answers; the operator may restrict access')
		.format(head(reach.device), target) + (ago(reach.age_s) ? ' (' + _('updated %s').format(ago(reach.age_s)) + ')' : '');
	return head(reach.device + ', ' + target) + ': ' + (reach.ok ? _('%d ms').format(reach.latency_ms) :
		_('no answer') + (reach.error && reach.error !== 'no answer' ? ' (' + words(mm.reachError(reach.error)) + ')' : '')) + (ago(reach.age_s) ? (reach.ok ? ' (' + _('updated %s').format(ago(reach.age_s)) + ')' : '; ' + _('updated %s').format(ago(reach.age_s))) : '');
}
/* the basic level says the check as a person would: «The internet answers, 31 ms» */
/* «ограниченный доступ», not «белый список» for sure: a part of the addresses fails for other reasons too (second audit);
 * the words and their age in common.js (audit r101 R12) */
function checkSimple(reach, role) {
	var s = mmc.checkState(reach, role);
	return s ? s.text : '';
}
/* a measured direction's speed: the mean after its first second (the modem page's speedShape), null when too short */
function speedMean(kbps) {
	var all = list(kbps).filter(function(x) { return typeof x === 'number' && x >= 0; }).slice(1);
	return all.length < 2 ? null : Math.round(all.reduce(function(s, x) { return s + x; }, 0) / all.length);
}
function speedWords(k) { return k >= 1000 ? _('%s Mbit/s').format(num(Math.round(k / 100) / 10)) : _('%d kbit/s').format(Math.round(k)); }
/* how long ago the engine checked (second audit 2026-10-21: a card says how fresh its verdict is) */
function agoWords(s) {
	if (typeof s !== 'number' || s < 0) return '';
	return s < 60 ? _('just now') : s < 3600 ? _('%d min ago').format(Math.round(s / 60)) : _('%d h ago').format(Math.round(s / 3600));
}
/* the signal as a word, from the same bars the icon shows */
function signalWord(icon) {
	if (!icon || !icon.registered) return '';
	/* SINR below 0 dB: the noise is stronger than the signal and the link is poor whatever the RSRP says; the words
	 * follow the bars, never «excellent» beside one red bar (the owner, 2026-10-31: RSRP -84, SINR -7.4) */
	var z = zoneOf('rsrp', icon.rsrp, icon.rat);
	if (icon.noisy)
		return z >= 2 ? _('strong signal, but the noise is stronger') : _('weak signal and heavy noise');
	return [_('weak signal'), _('fair signal'), _('good signal'), _('excellent signal')][z];
}
/* a problem in the card at the basic level: its first sentence; the whole of it in the title and on the modem page */
function firstSentence(t) { var m = /^(.*?[.!?])(\s|$)/.exec(String(t || '')); return m ? m[1] : String(t || ''); }
var ENVELOPE = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round" aria-hidden="true">' +
	'<rect x="1.5" y="3.5" width="13" height="9" rx="1.5"/><path d="M2 4.5l6 4.5 6-4.5"/></svg>';
function cap(s) { return s ? s.charAt(0).toUpperCase() + s.slice(1) : s;
}
/* [the pill, its tone, the pill with the backup's place, the table's short word, the short word with the place] (the owner,
 * 2026-10-19: a backup says its place in the priority) */
var ROLES = { carrying: [_('Main channel'), 'ok', null, _('Main channel')],
	backup_ready: [_('Backup channel, ready'), 'backup', _('Backup channel %d, ready'), _('Backup, ready'), _('Backup %d, ready')],
	backup_not_ready: [_('Backup channel, not ready'), 'warn', _('Backup channel %d, not ready'), _('Backup, not ready'), _('Backup %d, not ready')],
	no_internet: [_('No internet access'), 'warn'], checking: [_('Connection check'), 'neutral'],
	limited: [_('Limited access'), 'warn'],
	/* the whitelist with a tunnel over it that answers (the owner, 2026-10-28): the traffic passes, through the tunnel */
	limited_tunnel: [_('Whitelist, the internet through the tunnel'), 'ok', null, _('Main channel, through the tunnel')],
	backup_limited: [_('Backup channel, limited access'), 'warn', _('Backup channel %d, limited access'), _('Backup, limited access'), _('Backup %d, limited access')] };
var REASONS = mm.REASONS;
/* a tile folded or open as the person left it; never touched: open when it is the only modem (spec 25.9.7) */
function stored(id, alone) {
	try { var v = localStorage.getItem('mm:' + location.host + ':dashboard:' + id); return v ? v === 'open' : !!alone; }
	catch (e) { return !!alone; }
}
function remember(id, open) {
	try { localStorage.setItem('mm:' + location.host + ':dashboard:' + id, open ? 'open' : 'closed'); }
	catch (e) { /* The dashboard also works with browser storage disabled. */ }
}
function describe(device, status) {
	var mm = device.modemmanager || {}, session = device.session || {}, net = session.netifd || {};
	var owner = device.owner || {}, config = device.configuration || {}, stability = device.stability || {};
	var uplink = device.uplink || {}, traffic = uplink.traffic || {}, health = device.health || {};
	var mmAge = (status.modemmanager || {}).age_s;
	/* ModemManager's view goes stale with its read; a modem it does not hold (HiLink web, QModem, AT) has its own reading,
	 * kept by withReading: that one must not vanish whenever ModemManager is slow (the owner, 2026-10-21: «периодически
	 * показания пропадают» on the E3372) */
	/* ModemManager's freshness says nothing about a modem it does not hold (review 2026-10-22: a HiLink with no reading yet
	 * read «Оператор пока неизвестен» and «Сигнал: нет данных» because ModemManager was fresh); before its first reading
	 * such a modem says it is being read */
	var present = device.present === true, state = present ? session.state : 'absent';
	/* Only this modem's owner can establish reading freshness; the global
	 * ModemManager timestamp does not validate QModem/native AT telemetry. */
	var ownReading = !!owner.control && owner.control !== 'modemmanager' && !device.reading_fresh;
	var mmFresh = !!device.reading_fresh ||
		(!ownReading && owner.control === 'modemmanager' && (status.modemmanager || {}).backend === 'available' &&
		 typeof mmAge === 'number' && mmAge <= 30);
	var readingPending = present && ownReading && !device.reading_failed;
	/* taken down and kept down: by the operator («Деактивировать»), or netifd gave up (the owner, 2026-10-16) */
	var paused = present && state !== 'connected' && (device.connection || {}).deactivated || null;
	/* «Приостановить управление» (the owner, 2026-10-19): no service touches it until «Возобновить управление» */
	var frozen = present && device.frozen || null;
	/* nothing connects it: the engine has no interface for it yet (field 2026-10-17, an E171) */
	/* an interface the user set up already serves it (a tester's MF833V, 2026-11-08: «no interface yet» while mm1 carried
	 * the traffic): it is handed over on the modem page, not made again */
	var userIf = present && device.connection && !device.connection.interface ? words(device.connection.user_interface) : '';
	var noIface = present && state !== 'connected' && state !== 'connecting' && !!device.connection && !device.connection.interface &&
		!userIf && owner.control !== 'qmodem' && !list(device.drivers_missing).length;
	/* a phone in tethering (an iPhone over ipheth, an Android over RNDIS/NCM): no operator, signal or SIM of its own
	 * here, they live on the phone; its network device appears only while it shares (the owner, 2026-11-01) */
	var phone = /^phone-/.test((device.profile || {}).id || '');
	var sharing = phone && list((device.ports || {}).net).length > 0;
	var names = { connected: _('Connected'), connecting: _('Connecting…'), configuring: _('Configuring…'),
		disconnected: _('No internet connection'), failed: _('Connection error'), idle: _('No internet connection'),
		absent: _('Modem disconnected from USB'), recovering: _('Recovery…') };
	/* the freeze stays with an unplugged modem and follows it into another port (HANDOFF 200 item 9) */
	if (!present && device.frozen) names.absent = _('Modem disconnected from USB') + ' · ' + _('management paused');
	/* no SIM, or a SIM the network does not take: said in the pill, not as «no internet» (mockup, the owner 2026-10-17) */
	var noSim = present && mmFresh && (mm.failed_reason === 'sim-missing' || mm.web_sim === 'absent');
	var unregistered = present && mmFresh && (noSim || (!!words(mm.registration) && ['home', 'roaming'].indexOf(mm.registration) < 0 && !words(mm.operator)));
	/* the network refused the registration: ModemManager disables the modem after the failed connect and then reports
	 * no registration at all, so the engine keeps the refusal until a registration comes (a tester's MF192, 2026-11-11) */
	var deniedAt = (device.connection || {}).registration_denied;
	var denied = present && !(mmFresh && ['home', 'roaming'].indexOf(mm.registration) >= 0) && ((mmFresh && mm.registration === 'denied') || !!deniedAt);
	/* a problem marked err colours the card's line red when it comes first (a SIM, a failed connection, a network error) */
	var problems = [], errs = [], retune = false, rescue = false, prob = function(t, err) { problems.push(t); errs.push(!!err); };
	if (device.usb_power)
		prob(_('USB power failure (kernel error: “%s”). A restart will not help: connect the modem through a USB hub with its own power or into another port.').format(words(device.usb_power.last)));
	if (!present) prob(_('The modem is not found. Check its connection and power.'));
	else {
		retune = !!owner.conflict || config.state === 'locked' || !!(config.state && ['unknown', 'ready', 'ok', 'expected'].indexOf(config.state) < 0) ||
			(state === 'connected' && uplink.dns && uplink.dns.ok === false);
		/* field 2026-10-17: a Huawei waited for drivers the router lacked; the modem page installs them */
		if (list(device.drivers_missing).length)
			prob(_('The router lacks a driver for this modem (%s). It can be installed on the modem\'s page, the “Diagnostics” tab.')
				.format(list(device.drivers_missing).map(function(m) { return (m && (m.package || m.driver)) || ''; }).join(', ')));
		if (phone && !sharing) prob(/^phone-iphone/.test(device.profile.id) ?
			_('The iPhone does not share its internet: switch on “Personal Hotspot” on it and keep the screen unlocked; the first time it asks “Trust this computer?”.') :
			_('The phone does not share its internet: switch on “USB tethering” in its settings (Hotspot and tethering).'));
		else if (noIface && phone) prob(_('The phone shares its internet, but the router has no interface for it: “Connect” creates one (the address from the phone, by DHCP).'));
		else if (noIface) prob(_('The modem is not connected: it has no interface yet. “Connect” creates one with the modem\'s settings.'));
		else if (userIf && !(device.connection || {}).foreign) prob(_('The interface “%s” set up outside MedveModem serves this modem: hand it over to MedveModem on the modem page.').format(userIf));
		/* bench 2026-10-20 (FM350): handed to QModem while QModem's services were stopped — no one drove it, and the card
		 * said only «нет подключения», no operator, no signal, no «Исправить» */
		var qmodemDown = owner.control === 'qmodem' && state !== 'connected' && !frozen && status.qmodemRunning === false;
		if (qmodemDown)
			prob(_('QModem should manage the modem, but its modem connection service (qmodem_network) is not running: nobody connects the modem. “Fix” starts it (or Settings → MedveModem service → “Start QModem”); another way of management can also be chosen on the modem\'s page.'));
		if (owner.conflict) prob(_('Management conflict: several services manage the modem.'));
		if (mmFresh && mm.failed_reason === 'sim-missing') prob(_('The SIM card is not found: insert it until it clicks.'), true);
		if (list(device.action_needed).indexOf('fcc-unlock') >= 0)
			prob(_('The modem\'s radio is off: this model does so without an FCC unlock. It can be switched on on the modem\'s page, the “Diagnostics” tab.'));
		else if (config.state === 'locked') prob(_('The SIM is locked or the radio is off.'), true);
		/* a router inside the device that has no other mode is how it works, not a problem (UFI stick, bench 2026-10-19) */
		else if (config.state && ['unknown', 'ready', 'ok', 'expected'].indexOf(config.state) < 0 &&
			!(config.state === 'suboptimal' && (device.profile || {}).router_mode === 'only'))
			prob(({ unbound: _('The modem\'s driver is not found.'), incomplete: _('Not all the modem\'s ports are available.'), suboptimal: _('The modem works in router mode: management is limited.'), 'service-mode': _('The modem is in service mode.'), 'modeswitch-rule': _('A conflict with usb-modeswitch rules.') })[config.state] || _('The configuration needs a check: %s').format(config.state));
		if (device.quota && device.quota.state === 'exceeded')
			prob(_('The SIM\'s traffic limit is used up: %s.').format(quotaWords(device.quota)) + (device.quota.action === 'backup' ? ' ' + _('The channel is kept as a backup until the next period.') : ''), true);
		if (stability.state === 'flapping') prob(_('The modem reconnects often.'));
		else if (stability.state === 'degraded') prob(_('Failures of the modem were recorded.'));
		if (state === 'failed' && !denied) prob(_('The connection could not be set up.'), true);
		if (state === 'disconnected' && !paused && !noSim && !denied && !(phone && !sharing)) prob(_('The connection is broken.'));
		if (state === 'connected' && uplink.dns && uplink.dns.ok === false)
			prob(_('The router\'s DNS does not answer: there is a link, but sites will not open. If the DNS servers are set by hand, use 77.88.8.8 and 77.88.8.1 under whitelists.'));
		if (list(net.errors).length) prob(_('Network error: %s').format(net.errors.join(', ')), true);
		if (state === 'connected' && uplink.role === 'no_internet') prob(_('The connection is up, but there is no internet access.') + ' ' + cap(checkWords(uplink.reach)) + '.');
		if (health.state === 'stalled' && /^carrier restriction/.test(health.stalled || ''))
			prob(_('Reconnecting did not help. Check the balance, the traffic limit and the operator\'s network.'));
		else if (denied || (health.state === 'stalled' && /^registration denied/.test(health.stalled || '')))
			prob(_('Registration denied: the SIM card is blocked, there is no roaming, or the modem\'s firmware is for another region.') +
				(deniedAt && deniedAt.retry_in_s > 0 ? ' ' + _('The next connection attempt in %s min.').format(num(Math.ceil(deniedAt.retry_in_s / 60))) : ''), true);
		else if (health.state === 'stalled')
			prob(_('The link could not be restored automatically (%s). A check by hand is needed.').format(REASONS[health.reason] || health.reason), true);
		/* the ladder spent, or its USB step failed: the last resort is on the modem's page (the owner, 2026-11-09: «иногда
		 * оправдано направить пользователя»); not for a SIM or an operator's refusal, where it would not help */
		var lastStep = ((device.recovery || {}).last_steps) || {};
		rescue = (health.state === 'stalled' && !/^(carrier restriction|registration denied)/.test(health.stalled || '')) ||
			(['usb-reset', 'power-cycle'].indexOf(lastStep.level) >= 0 && lastStep.result === 'failed');
		if (rescue) prob(_('If the modem does not answer at all, the last resort is on its page: restarting the USB controller or its power.'));
	}
	var roleKey = uplink.role === 'limited' && (uplink.tunnel || {}).ok === true ? 'limited_tunnel' : uplink.role;
	var role = present && state === 'connected' ? ROLES[roleKey] : null;
	/* the model ModemManager named last, also while it does not hold the modem (a reset) */
	var titled = mmc.profileTitle(device, mm), model = titled || words(mm.model) || words(device.model) || words(device.product) || _('USB modem');
	var own = /^\S+$/.test(words(device.model)) && !/^Generic/i.test(device.model) ? device.model : '';
	var running = present && (device.recovery || {}).state === 'running';
	var owners = { modemmanager: 'ModemManager', 'netifd-qmi': 'MedveModem · QMI',
		'netifd-mbim': 'MedveModem · MBIM', 'medvemodem-at': 'MedveModem · AT',
		qmodem: 'QModem', hilink: _('The modem itself (a router inside it)'), unresolved: _('Not determined'), none: _('Not assigned') };
	var quality = mmFresh && present && typeof mm.signal_quality === 'number' ? mm.signal_quality : null;
	return {
		name: words(device.name) || model,
		/* the operator's own name as written (bench 2026-10-15: no model after it); a short model when the modem has no name */
		label: words(device.name) || (function() {
			/* a one-word model the engine knows comes first: a HiLink stick's web names it (E8372h-320), while its
			 * family profile lists several (2026-10-18); then the matched profile knows the module's name (T99W175
			 * for «DELL Snapdragon X55») */
			if (own) return own;
			var prof = /\b([A-Z]{1,4}\d{2,}[A-Z0-9-]*)\b/.exec(words((device.profile || {}).description) || '');
			var short = titled || (prof && prof[1]) || [mm.model, device.model, device.product].map(words).filter(function(m) {
				return m && !/\s/.test(m) && !/^Generic/i.test(m); })[0] || model;
			return short;
		})(),
		retune: retune,
		/* the hardware's name for the header (the owner, 2026-10-16: not the operator's description) */
		hw: (function() {
			if (own) return own;
			var prof = /\b([A-Z]{1,4}\d{2,}[A-Z0-9-]*)\b/.exec(words((device.profile || {}).description) || '');
			return titled || (prof && prof[1]) || [mm.model, device.model, device.product].map(words).filter(function(m) {
				return m && !/\s/.test(m) && !/^Generic/i.test(m); })[0] || model;
		})(),
		/* without a name of its own the model is the title; under it who made it, then where it is plugged (a tester,
		 * 2026-11-11: «в описании карточки только usb путь») */
		subtitle: words(device.name) ? model : [(function(mk) {
			mk = mk.replace(/_/g, ' ').replace(/\s+(Incorporated|Inc\.?|Technologies|Co\.?,? ?Ltd\.?)$/i, '').trim();
			return mk && model.toLowerCase().indexOf(mk.toLowerCase().split(' ')[0]) < 0 ? mk : '';
		})(words(mm.manufacturer) || words(device.manufacturer)), words(device.attachment_id)].filter(Boolean).join(' · '),
		/* a working channel says for how long; a failing one only what is wrong */
		state: frozen ? _('Management paused') + (words(frozen.who) ? ' (' + words(frozen.who) + ')' : '') + ': ' + _('services do not touch the modem') :
			running ? names.recovering : paused ? (paused.by === 'netifd' ? _('Switched off automatically') +
			(paused.why ? ': ' + (NETIFD_WHY[paused.why] || words(paused.why)) : '') +
			(typeof paused.retry_in_s === 'number' ? ' · ' + (paused.retry_in_s > 0 ? _('retry in %d s').format(paused.retry_in_s) : _('retry now')) : '') :
			_('Deactivated by the user') + (words(paused.who) ? ' (' + words(paused.who) + ')' : '')) :
			role ? role[0] + ((uplink.role === 'carrying' || uplink.role === 'backup_ready') &&
			typeof net.uptime === 'number' && net.uptime > 0 ? ' · ' + duration(net.uptime) : '') :
			phone && present && !sharing ? _('Not sharing the internet') :
			noSim && state !== 'connected' ? _('No SIM card') : names[state] || _('No internet connection'),
		roleKey: role ? roleKey : null,
		/* the default route's link, whatever answers through it: under a whitelist it is «limited» and still carries
		 * the traffic (the owner, 2026-10-28: the main card lost its green frame) */
		carrying: !frozen && !!role && ['carrying', 'limited', 'no_internet'].indexOf(uplink.role) >= 0,
		tone: frozen ? 'paused' : noSim && !running && !paused && state !== 'connected' ? 'err' : paused ? 'paused' : problems.length ? 'warn' : role ? role[1] : state === 'connected' ? 'ok' : 'neutral',
		rescue: rescue, unregistered: unregistered, paused: !!paused || !!frozen, frozen: !!frozen, pausedAuto: !!(paused && paused.by === 'netifd'), noIface: noIface, qmodemDown: !!(present && qmodemDown),
		rates: present && state === 'connected' ? rates(traffic) : '',
		recovering: present && health.state === 'recovering' ? _('Recovery: %s…').format(REASONS[health.reason] || health.reason) : '',
		check: present && state === 'connected' ? checkWords(uplink.reach, true) || '—' : '—',
		session_traffic: present && state === 'connected' ? totals({ rx_bytes: traffic.rx_bytes, tx_bytes: traffic.tx_bytes }) : '—',
		today: totals(traffic.day),
		quota: quotaWords(device.quota) || '—',
		/* the journal's newest line (the owner, 2026-10-17, as in the mockup): «19:00 · Connection up» */
		event: device.last_event && typeof device.last_event.t === 'number' ? (function(e) {
			var at = new Date(e.t * 1000), hm = hhmm(e.t);
			return (at.toDateString() === new Date().toDateString() ? hm : at.toLocaleDateString(LANG, { day: 'numeric', month: 'short' }) + ' ' + hm) +
				' · ' + eventWords(e)[0];
		})(device.last_event) : '—',
		/* the usage line of the basic card: today's bytes, or what is left of the SIM's quota */
		usage: device.quota && typeof device.quota.limit_bytes === 'number' && device.quota.state !== 'none' ?
			(device.quota.state === 'exceeded' ? _('Limit used up: %s of %s').format(gb(device.quota.used_bytes), gb(device.quota.limit_bytes)) :
				_('%s of %s left').format(gb(Math.max(0, device.quota.limit_bytes - device.quota.used_bytes)), gb(device.quota.limit_bytes))) :
			traffic.day && typeof traffic.day.rx_bytes === 'number' ? _('Today %s').format(bytes(traffic.day.rx_bytes + traffic.day.tx_bytes)) : '',
		usage_pct: device.quota && typeof device.quota.percent === 'number' ? Math.min(100, device.quota.percent) : null,
		usage_tone: device.quota ? device.quota.state : '',
		sim: device.sim_label ? [words(device.sim_label.description), words(device.sim_label.number) && privacy() ? _('number hidden') :
			words(device.sim_label.number)].filter(Boolean).join(' · ') : '',
		/* the SIM as it is known now, on the expert card (the tester, 2026-11-09: «на карточке в подробном логично
		 * показывать статус сим») */
		simstate: !present ? '—' : (function() {
			var lock = words(mm.unlock_required), code = (device.sim_operator || {}).code || (mm.sim_card || {}).operator_code;
			if (mmFresh && (noSim || mm.web_sim === 'absent')) return _('Not found');
			if (mmFresh && mm.web_sim) return _('Not usable');
			if (lock === 'sim-pin') return _('The PIN is needed');
			if (lock === 'sim-puk') return _('Locked, the PUK is needed');
			/* «inserted» only for a SIM that reads: an old modem answers +CPIN: READY with no SIM in it, and ModemManager
			 * makes a SIM object of that (a tester's MF192, 2026-11-11) */
			var card = mm.sim_card || {}, sim = device.sim || {};
			if (code || card.imsi || card.iccid || sim.imsi || sim.iccid || (mmFresh && ['home', 'roaming'].indexOf(mm.registration) >= 0))
				return _('Inserted') + (code ? ' · ' + ((mmc.OPS[code] || [])[0] || code) : '');
			if (mm.sim || mm.sim_card) return mmFresh ? _('Not read: the modem reports it ready, but gives no IMSI') : '—';
			if (mmFresh && mm.registration === 'idle') return _('Unknown: the modem is not in a network');
			return '—';
		})(),
		sms: device.sms && device.sms.unread > 0 ? device.sms.unread : 0,
		/* what stops reception (the owner, 2026-10-16): an unsent message left in the storage, a full storage */
		smsWarn: (function(h) {
			if (!h) return '';
			if (h.full === true) return _('The SMS storage is full: new messages are not received');
			if (h.stuck && h.stuck.length) return _('An unsent message in the modem\'s storage blocks receiving SMS');
			if (h.full === false) return _('The SMS storage is almost full');
			return '';
		})(device.sms && device.sms.health),
		reach: uplink.reach,
		connected: present && state === 'connected', present: present, problems: problems, probErr: !!errs[0],
		phone: phone,
		network: phone ? (sharing ? _('The phone shares its internet over USB') : _('Phone over USB')) :
			readingPending ? _('Reading the modem\'s data…') : present && mmFresh ? [words(mm.operator), techWords(mm.access_tech)].filter(Boolean).join(' · ') ||
			(denied ? _('registration refused') : unregistered ? _('not registered in a network') : _('The operator is not known yet')) :
			!present ? _('Modem disconnected from USB') :
			owner.control === 'qmodem' ? _('QModem telemetry is unavailable') : _('The operator\'s data is unavailable'),
		readingPending: readingPending,
		readingAge: device.reading_age,
		quality: quality !== null && quality >= 0 && quality <= 100 ? quality : null,
		icon: mmFresh && present ? signalIcon(mm) : null,
		/* how the modem is driven (bench 2026-10-15): the protocol of its control function and the service, «QMI · ModemManager» */
		owner: [CTL_PROTO[((device.port_roles || {}).control || {}).driver] || '', owners[owner.control] || words(owner.control) || ''].filter(Boolean).join(' · ') || _('Not determined'),
		iface: words(session.interface), proto: words(session.proto),
		/* the address itself and the interface, «10.187.44.19 · wwan0» (mockup): netifd's list (with the prefix), else
		 * ModemManager's bearer address; a flag only says it was given (bench 2026-10-19: «IPv4 получен» with the list there) */
		address: !present ? '—' : net.ipv4 || net.ipv6 ? [
			list(net.ipv4_addresses).filter(function(a) { return typeof a === 'string'; }).map(function(a) { return a.replace(/\/\d+$/, ''); })[0] ||
				(typeof net.ipv4 === 'string' ? net.ipv4 : net.ipv4 ? _('IPv4 obtained') : ''),
			list(net.ipv6_addresses).filter(function(a) { return typeof a === 'string'; }).map(function(a) { return a.replace(/\/\d+$/, ''); })[0] ||
				(typeof net.ipv6 === 'string' ? net.ipv6 : net.ipv6 ? _('IPv6 obtained') : ''),
			words(session.interface)].filter(Boolean).join(' · ') : _('No data'),
		uptime: present && state === 'connected' ? duration(net.uptime) : '—',
		path: words(device.attachment_id), id: device.device_id,
		/* the engine's evidence for a configuration finding; "unknown" only means nothing is validated yet */
		reason: words(session.reason) || (['unknown', 'ready', 'ok', 'expected'].indexOf(config.state) < 0 ? list(config.evidence).join('; ') : '') ||
			chainLine(device.diagnosis) || '—',
		generation: device.generation,
		freshness: present && device.reading_stale ?
			(owner.control === 'qmodem' ? _('Last QModem reading is stale.') : _('Last modem reading is stale.')) :
			present && owner.control === 'modemmanager' && mm.model && !mmFresh ? _('ModemManager\'s data is stale or unavailable.') : ''
	};
}

/*
 * The browser keeps LuCI's scripts by LuCI's own version, not this package's: after an update a page met an old
 * common.js and showed dashes (the owner, 2026-10-20, fixed by a private window). This file knows its version; on a
 * mismatch both files are fetched past the cache and the page reloads once.
 */
var VIEW_VERSION = '0.16.0-alpha1-r5';
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
		this.cards = new Map();
		this.cardSerial = 0;
		this.speedRes = {};	/* the speed measured from a card in this page, by device id */
		this.lastSuccess = null;
		this.level = level();
		this.counts = E('span', {}, _('Loading…'));
		this.updated = E('span', { 'class': 'mm-faint' });
		this.summary = E('h3', { 'class': 'mm-verdict-title' });
		this.internet = E('div', { 'class': 'mm-verdict-sub' }, _('The internet availability is not checked yet.'));
		/* «Базовый» says the backups as a count, the names behind «Подробнее» (HANDOFF 200 item 16: the plate was ~180 px) */
		this.backupText = E('span');
		this.backupMore = E('button', { type: 'button', 'class': 'mm-link', hidden: true, 'aria-expanded': 'false', click: function() {
			self.backupOpen = !self.backupOpen;
			self.backupMore.setAttribute('aria-expanded', String(self.backupOpen));
			text(self.backupMore, self.backupOpen ? _('Hide') : _('More'));
			text(self.backupText, self.backupOpen ? self.backupLong : self.backupShort);
		} }, _('More'));
		this.backup = E('div', { 'class': 'mm-verdict-sub', hidden: true }, [this.backupText, ' ', this.backupMore]);
		this.foText = E('span');
		this.foBack = E('button', { type: 'button', 'class': 'mm-btn mm-fo-back', hidden: true, click: mm.busy(function() {
			var b = this;
			b.disabled = true;
			return forgive().then(function() { return self.update(); }).catch(function() {}).then(function() { b.disabled = false; });
		}) }, _('Restore the priority'));
		this.failover = E('div', { 'class': 'mm-verdict-sub mm-failover', hidden: true }, [this.foText, ' ', this.foBack]);
		/* switching off while the carrier has no internet and a backup answers (the tester's T99 with no money, 2026-11-03:
		 * the traffic stayed on the dead channel, nobody said why): said with a button that turns it on */
		this.foOffText = E('span');
		this.foOn = E('button', { type: 'button', 'class': 'mm-btn mm-fo-on', click: mm.busy(function() {
			var b = this;
			b.disabled = true;
			return uciSet('medvemodem', 'core', { failover: '1' }).then(function() { return uciCommit('medvemodem'); })
				.then(function() { mm.toast('fo', _('Channel switching is on.'), 'ok', 8000); return self.update(); })
				.catch(function() {}).then(function() { b.disabled = false; });
		}) }, _('Turn switching on'));
		this.foOff = E('div', { 'class': 'mm-verdict-sub mm-fo-off', hidden: true }, [this.foOffText, ' ', this.foOn]);
		/* the whitelist mode on (the owner, 2026-11-06: «в медвемодем показывать статус включенности обхода и какая выбрана
		 * прокси»): the server the tunnel manager uses, what goes through it, how it was turned on */
		this.wlLine = E('div', { 'class': 'mm-verdict-sub mm-wl-line', hidden: true });
		this.dpiLine = E('div', { 'class': 'mm-verdict-sub mm-dpi-line', hidden: true });
		/* «Подробный»: every channel with its configured and kernel metric and why it is not the one carrying (HANDOFF 200 item 16) */
		this.chanBody = E('tbody');
		/* the heads also label the cells: a phone shows each channel as a block of «head: value» lines (the owner, 2026-10-26) */
		/* «Статус», one metric column «setting / kernel» (the owner, 2026-10-26: «не роль, а статус», «метрики два раза зачем») */
		/* today's bytes per channel and their sum (the owner, 2026-10-26: «колонку трафика … и ИТОГО … с какого времени») */
		/* the owner, 2026-10-30: «метрика больше меньше — лишне», «сделать редактируемой», «помечать кто сейчас передает
		 * трафик зеленым»: the order by ↑ ↓ in place of «why so»; a reason only when something is off, beside the status */
		/* spec §42.2.1: «Пик ↓/↑» since the router's boot; the other heads short so the table does not grow, the full words
		 * in their titles */
		this.chanHeads = ['№', _('Channel'), _('Device'), _('Status'), _('Metric'), _('Today'), _('Peak'), _('Check'), _('Order')];
		var chanTitles = { 4: _('Metric (setting / kernel)'), 5: _('Traffic today'), 6: _('The highest speed over 5 seconds since the router booted, download / upload'),
			7: _('The internet check through this channel'), 8: _('Order of the channels') };
		this.chanTotal = E('p', { 'class': 'mm-faint mm-chans-total' });
		this.chans = E('div', { 'class': 'mm-chans', hidden: true }, [E('div', { 'class': 'mm-tablewrap' }, E('table', { 'class': 'mm-table' }, [
			E('thead', {}, E('tr', {}, this.chanHeads.map(function(h, i) {
				/* one word, the cell is a number (the owner, 2026-10-26: «слишком много места ест»); what it is, on hover */
				return E('th', Object.assign(i === 0 || i === 4 || i === 5 || i === 6 ? { 'class': 'num' } : {}, chanTitles[i] ? { title: chanTitles[i] } : {}), h); }))), this.chanBody])), this.chanTotal]);
		this.mark = E('span', { 'class': 'mm-verdict-mark', 'aria-hidden': 'true' }, '…');
		/* one line on request (the owner, 2026-10-20: «не убирается или не скрывается до одной строки»), kept per browser */
		this.fold = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-verdict-fold', 'aria-label': _('Collapse'), title: _('Collapse to one line'), click: function() {
			var short = !self.verdict.classList.contains('is-short');
			self.verdict.classList.toggle('is-short', short);
			self.fold.setAttribute('aria-expanded', String(!short));
			self.fold.title = short ? _('Expand') : _('Collapse to one line');
			try { localStorage.setItem('mm:verdict-short', short ? '1' : '0'); } catch (e) {}
		} }, chev());
		this.verdict = E('section', { 'class': 'mm-verdict mm-summary', 'aria-label': _('Modem state') },
			[this.mark, E('div', { 'class': 'mm-verdict-body' }, [this.summary, this.internet, this.backup, this.failover, this.foOff, this.wlLine, this.dpiLine, this.chans]), this.fold]);
		try { if (localStorage.getItem('mm:verdict-short') === '1') { this.verdict.classList.add('is-short'); this.fold.setAttribute('aria-expanded', 'false'); this.fold.title = _('Expand'); } } catch (e) {}
		this.error = E('div', { 'class': 'mm-callout is-err mm-notice', role: 'status', hidden: true });
		/* «Нужно ваше действие» (the owner, 2026-10-19): one line a modem, a link to its card; nothing runs from here */
		this.actList = E('ul', { 'class': 'mm-act-list' });
		this.actions = E('section', { 'class': 'mm-callout is-warn mm-act-needed', role: 'status', 'aria-label': _('Your action is needed'), hidden: true },
			[E('strong', {}, _('Your action is needed')), this.actList]);
		/* other modem helpers on the router (the owner, 2026-10-28): said, never stopped from here */
		this.nbList = E('ul', { 'class': 'mm-act-list' });
		this.nbAgain = E('button', { type: 'button', 'class': 'mm-btn is-quiet', click: mm.busy(function() {
			var b = this;
			b.disabled = true;
			return getNeighbours(true).then(function() { return new Promise(function(r) { setTimeout(r, 4000); }); })
				.then(function() { return self.neighboursShow(); }).catch(function() {}).then(function() { b.disabled = false; });
		}) }, _('Check again'));
		this.nb = E('section', { 'class': 'mm-callout is-warn mm-neighbours', role: 'status', 'aria-label': _('Other modem helpers on this router'), hidden: true }, [
			E('strong', {}, _('Other modem helpers on this router')),
			E('p', { 'class': 'mm-faint' }, _('They may talk to the same modems: answers mixed with ours, USB resets, a second dial, a watchdog of their own. MedveModem does not stop them; switch off what you do not need.')),
			this.nbList, this.nbAgain]);
		/* the router's health in the kernel's words: this boot, and what the last troubled boot ended with */
		this.kList = E('ul', { 'class': 'mm-act-list' });
		this.kern = E('section', { 'class': 'mm-callout is-err mm-kernel', role: 'alert', hidden: true }, [
			E('strong', {}, _('The router itself is in trouble')), this.kList,
			E('p', { 'class': 'mm-faint' }, _('This is the hardware or the power, not the modems: MedveModem cannot fix it with a restart of a modem. Save the logs (logread, dmesg) and restart the router. If it comes back: a power supply with a margin, a USB hub with its own power for the modems, cooling.'))]);
		this.kPrevList = E('ul', { 'class': 'mm-act-list' });
		this.kPrev = E('section', { 'class': 'mm-callout is-warn mm-kernel-prev', role: 'status', hidden: true }, [
			E('strong', {}, _('Before the last restart the router was in trouble')), this.kPrevList,
			E('p', { 'class': 'mm-faint' }, _('Told by the kernel before the router was restarted. If the router hung, check its power and the USB hub.'))]);
		/* the banners close with ×; the closed ones are counted under them with «Show again» (the owner, 2026-10-31) */
		this.hiddenLine = E('p', { 'class': 'mm-faint mm-hidden-notices', hidden: '' });
		var hiddenShow = function() {
			var h = mm.dismissedFor('router:');
			self.hiddenLine.hidden = !h.length;
			self.hiddenLine.replaceChildren(_('Hidden notices: %d').format(h.length), ' · ', E('button', { type: 'button', 'class': 'mm-link', click: function() {
				h.forEach(function(x) { mm.undismiss(x.key); });
				self.actions.hidden = !self.actList.children.length;
				self.nb.hidden = !self.nbList.children.length;
				self.kern.hidden = !self.kList.children.length;
				self.kPrev.hidden = !self.kPrevList.children.length;
				hiddenShow();
			} }, _('Show again')));
		};
		mm.dismissible(this.actions, 'router:actions', hiddenShow);
		mm.dismissible(this.nb, 'router:neighbours', hiddenShow);
		mm.dismissible(this.kern, 'router:kernel', hiddenShow);
		mm.dismissible(this.kPrev, 'router:kernel-prev', hiddenShow);
		hiddenShow();
		this.grid = E('section', { 'class': 'mm-fleet mm-grid' });
		/* the cards or one numbered table (the owner, 2026-10-20: «переключатель … в виде таблицы пронумерованной»); per browser */
		try { this.view = localStorage.getItem('mm:dash-view') === 'table' ? 'table' : 'cards'; } catch (e) { this.view = 'cards'; }
		this.tableBody = E('tbody');
		this.table = E('div', { 'class': 'mm-card mm-tablewrap mm-fleet-table', hidden: true }, E('table', { 'class': 'mm-table mm-cards' }, [
			E('thead', {}, E('tr', {}, [this.tableAll = E('input', { type: 'checkbox', 'aria-label': _('Select all modems'), change: function() {
				var on = self.tableAll.checked;
				self.tableSel = new Set(on ? Array.prototype.map.call(self.grid.children, function(n) { return n.getAttribute('data-device-id'); }) : []);
				self.applyTable();
			} }), '№', _('Modem'), _('State'), _('Operator'), _('Signal'), _('Today'), ''].map(function(h, i) {
				return E('th', i === 1 ? { 'class': 'num' } : {}, h); }))), this.tableBody]));
		/* the selected rows' actions (the owner, 2026-10-20: «управление базовое, с галочками например») */
		this.tableSel = new Set();
		this.tableOpen = new Set();
		this.bulk = E('div', { 'class': 'mm-row mm-bulk', hidden: true });
		this.table.insertBefore(this.bulk, this.table.firstChild);
		this.viewBtns = ['cards', 'table'].map(function(k) {
			return E('button', { type: 'button', 'data-view': k, 'aria-pressed': String(self.view === k), click: function() {
				self.view = k;
				try { localStorage.setItem('mm:dash-view', k); } catch (e) {}
				self.viewBtns.forEach(function(b) { b.setAttribute('aria-pressed', String(b.getAttribute('data-view') === k)); });
				if (self.lastStatus) self.apply(self.lastStatus, self.lastDevices, self.lastUplink);
			} }, k === 'cards' ? _('Cards') : _('Table'));
		});
		this.viewSwitch = E('div', { 'class': 'mm-seg mm-view-switch', role: 'group', 'aria-label': _('Modem list view') }, this.viewBtns);
		/* a device the rules dropped that looks like a modem: asked about at every level (the owner, 2026-11-09) */
		this.suspects = E('section', { 'class': 'mm-suspects', hidden: true, 'aria-label': _('Possibly a modem') });
		this.empty = E('div', { 'class': 'mm-card mm-pad mm-empty', hidden: true }, [
			E('h3', {}, _('No modems found')),
			E('p', { 'class': 'mm-dim' }, _('Connect a modem and check its power. The list refreshes by itself.'))
		]);
		/* «Порты и устройства» (r84): Expert only, off by default, kept per browser; one line folded */
		try { this.usbOn = localStorage.getItem('mm:usbtree') === '1'; this.usbOpen = localStorage.getItem('mm:usbtree-open') === '1'; } catch (e) { this.usbOn = this.usbOpen = false; }
		this.usbShow = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-usb-show', hidden: true, click: function() { self.usbSwitch(true); } },
			_('Show “Ports and devices”'));
		this.usbSum = E('span', { 'class': 'mm-faint' }, _('loading…'));
		this.usbFold = E('button', { type: 'button', 'class': 'mm-btn is-quiet', 'aria-expanded': String(this.usbOpen), click: function() {
			self.usbOpen = !self.usbOpen;
			try { localStorage.setItem('mm:usbtree-open', self.usbOpen ? '1' : '0'); } catch (e) {}
			self.usbFold.setAttribute('aria-expanded', String(self.usbOpen));
			self.usbRefresh(true);
		} }, _('Expand'));
		this.usbTree = E('ul', { 'class': 'mm-usb-tree', role: 'tree', 'aria-label': _('USB tree') });
		this.usbDetail = E('div', { 'class': 'mm-usb-detail' });
		/* the selected port's name: its own block, kept while the 5 s refresh redraws the details (typing is not lost) */
		this.usbName = { path: null, input: E('input', { type: 'text', 'class': 'mm-input mm-usb-name', id: 'mm-usb-name', maxlength: 32,
			placeholder: _('for example: upper port') }), out: E('span', { 'class': 'mm-faint mm-usb-name-out', role: 'status' }) };
		this.usbName.save = E('button', { type: 'button', 'class': 'mm-btn mm-usb-name-save', click: mm.busy(function() { return self.usbNameSave(); }) }, _('Save the name'));
		this.usbName.box = E('div', { 'class': 'mm-usb-name-box', hidden: true }, [
			E('label', { 'for': 'mm-usb-name' }, _('Port name')), ' ', this.usbName.input, ' ', this.usbName.save, ' ', this.usbName.out,
			E('p', { 'class': 'mm-faint' }, _('The name belongs to the port: another device in it is shown with the same name. An empty field removes it.'))]);
		this.usbEvents = E('ol', { 'class': 'mm-usb-events' });
		this.usbNote = E('p', { 'class': 'mm-faint mm-usb-note', hidden: true });
		this.usbBody = E('div', { 'class': 'mm-usb-body', hidden: true }, [
			E('div', { 'class': 'mm-usb-grid' }, [this.usbTree, E('div', {}, [this.usbDetail, this.usbName.box])]),
			E('h4', {}, _('Events')), this.usbEvents, this.usbNote]);
		this.usbCard = E('section', { 'class': 'mm-card mm-pad mm-usb', hidden: true, 'aria-label': _('Ports and devices') }, [
			E('div', { 'class': 'mm-row mm-usb-head' }, [E('h3', {}, _('Ports and devices')), this.usbSum, E('span', { 'class': 'mm-grow' }), this.usbFold,
				E('button', { type: 'button', 'class': 'mm-btn is-quiet', click: function() { self.usbSwitch(false); } }, _('Hide'))]),
			this.usbBody]);
		/* «Все SMS» (the owner, 2026-11-16: «в экспертном режиме отдельную скрытую простыню на главной»): every modem's
		 * messages in one feed, the newest first, with a search; Detailed only, folded by default, kept per browser.
		 * Folded it reads nothing; open, the engine's kept lists every 30 s (no modem is asked for them), as a peek */
		try { this.feedOpen = localStorage.getItem('mm:smsfeed-open') === '1'; } catch (e) { this.feedOpen = false; }
		this.feedSum = E('span', { 'class': 'mm-faint mm-feed-sum' });
		this.feedSearch = E('input', { type: 'search', 'class': 'mm-input mm-sms-search', id: 'mm-feed-search', autocomplete: 'off',
			placeholder: _('Search the messages'), 'aria-label': _('Search the messages'), input: function() { self.feedRender(); } });
		this.feedNote = E('p', { 'class': 'mm-faint mm-feed-note', hidden: '' });
		this.feedList = E('div', { 'class': 'mm-feed-list' });
		this.feedFold = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-feed-fold', 'aria-expanded': String(this.feedOpen), click: function() {
			self.feedOpen = !self.feedOpen;
			try { localStorage.setItem('mm:smsfeed-open', self.feedOpen ? '1' : '0'); } catch (e) {}
			self.feedFold.setAttribute('aria-expanded', String(self.feedOpen));
			self.feedRefresh(true);
		} }, _('Expand'));
		this.feedBody = E('div', { 'class': 'mm-feed-body', hidden: '' }, [this.feedSearch, this.feedNote, this.feedList]);
		this.feedCard = E('section', { 'class': 'mm-card mm-pad mm-feed', hidden: '', 'aria-label': _('All SMS') }, [
			E('div', { 'class': 'mm-row mm-feed-top' }, [E('h3', {}, _('All SMS')), this.feedSum, E('span', { 'class': 'mm-grow' }), this.feedFold]),
			this.feedBody]);
		this.version = E('span');
		this.refresh = E('button', { 'class': 'mm-btn is-quiet mm-refresh', type: 'button', click: mm.busy(function() { return self.update(); }) }, _('Refresh'));
		this.root = E('div', { 'class': 'mm-root', 'aria-busy': 'true' }, [
			E('link', { rel: 'stylesheet', href: L.resource('medvemodem/dashboard.css') + '?v=' + CSS_VERSION }),
			pageHead(_('Modems'), E('p', {}, [this.counts, ' · ', this.updated]), function(k) { self.level = k; self.update(); }, 'modems'),
			this.error, this.verdict, this.kern, this.kPrev, this.actions, this.nb, this.hiddenLine, this.viewRow = E('div', { 'class': 'mm-row mm-view-row' }, this.viewSwitch), this.grid, this.table, this.suspects, this.empty,
			this.feedCard, this.usbCard, E('div', { 'class': 'mm-row' }, this.usbShow),
			E('footer', { 'class': 'mm-footer mm-faint mm-row' }, [this.version, this.refresh])
		]);
		text(this.summary, _('Getting the data…'));
		this.pollFn = this.update.bind(this);
		poll.add(this.pollFn, 5);
		this.update();
		return this.root;
	},

	/* the table view: one row a modem in the cards' order (the priority), from what the cards just showed; a traffic light
	 * with the words on hover, the operator's badge, bars and percent, the card's actions behind «⋯», a check box for the
	 * actions on several, a row opening into the card's details (the owner, 2026-10-20) */
	/* the operator's word on a device; the dashboard and the USB tree refresh after it */
	usbClass: function(vidpid, cls) {
		var self = this;
		return setUsbClass(vidpid, cls).then(function(r) {
			if (r && r.error) throw new Error(r.error);
			mm.toast('usb-class', cls === 'modem' ? _('%s is taken as a modem').format(vidpid) : cls === 'not_modem' ?
				_('%s is not a modem: it will not be asked about again').format(vidpid) : _('%s is decided by the rules again').format(vidpid), 'ok', 5000);
			if (self.usbOn) self.usbRefresh(true);
			return self.update();
		}).catch(function(e) { mm.toast('usb-class', _('Not saved: %s').format(e.message || e), 'err', 8000); });
	},

	applySuspects: function(uc) {
		var self = this, list = (uc && Array.isArray(uc.suspects)) ? uc.suspects : [];
		this.usbClassNow = uc || null;
		this.suspects.hidden = !list.length;
		this.suspects.replaceChildren.apply(this.suspects, list.map(function(s) {
			var name = [words(s.manufacturer), words(s.product)].filter(Boolean).join(' ') || _('USB device');
			return E('div', { 'class': 'mm-card mm-pad mm-suspect', 'data-vidpid': s.vidpid }, [
				E('h3', {}, _('Possibly a modem: %s').format(name)),
				E('p', { 'class': 'mm-dim' }, [s.vidpid, ' · ', _('USB %s').format(s.usb), ' · ',
					s.why === 'ports' ? _('it has serial or network ports, but MedveModem does not know it as a modem') :
						_('its maker makes modems, but MedveModem does not know this device')]),
				E('div', { 'class': 'mm-row' }, [
					E('button', { type: 'button', 'class': 'mm-btn is-primary mm-suspect-yes', click: mm.busy(function() { return self.usbClass(s.vidpid, 'modem'); }) },
						_('Treat as a modem')),
					E('button', { type: 'button', 'class': 'mm-btn mm-suspect-no', click: mm.busy(function() { return self.usbClass(s.vidpid, 'not_modem'); }) },
						_('Not a modem'))])]);
		}));
	},

	applyTable: function() {
		/* one modem: its card, no switch (the owner, 2026-10-21: a table of one row says less than the card); the choice is
		 * kept for when a second one comes */
		var self = this, many = this.grid.children.length > 1, table = this.view === 'table' && many;
		if (this.viewRow) this.viewRow.hidden = !many;
		this.grid.hidden = table;
		this.table.hidden = !table || !this.grid.children.length;
		if (!table) return;
		/* the rows are built anew each poll: not while a row's «⋯» is open, and the focus comes back to the same control of
		 * the same row (second audit 2026-10-21: the 5 s refresh closed the menu and dropped the keyboard's place) */
		if (this.tableBody.querySelector('details.mm-more[open]')) { this.tableDirty = true; return; }
		this.tableDirty = false;
		var focus = null, fa = document.activeElement;
		if (fa && this.tableBody.contains(fa)) {
			var frow = fa.closest('tr[data-device-id]'), fcells = frow ? Array.prototype.slice.call(frow.querySelectorAll('button, a, input, summary')) : [];
			if (frow) focus = { id: frow.getAttribute('data-device-id'), at: fcells.indexOf(fa) };
		}
		var ids = Array.prototype.map.call(this.grid.children, function(n) { return n.getAttribute('data-device-id'); });
		this.tableSel.forEach(function(id) { if (ids.indexOf(id) < 0) self.tableSel.delete(id); });
		var rows = [];
		ids.forEach(function(id, i) {
			var card = self.cards.get(id), data = card && card.data;
			if (!data) return;
			var href = card.link.getAttribute('href'), tone = (card.state.className.match(/\bis-(\w+)/) || [])[1] || 'idle';
			var short = data.short || card.state.textContent.split(' · ')[0];
			var prob = card.problem.hidden ? '' : card.problem.title || card.problem.textContent;
			/* one light a row (the owner, 2026-11-16: «два светофора не красиво»): the worse of the channel and its internet,
			 * the internet's words beside it, the problem under it (no column of dashes of its own) */
			var hTone = card.health.hidden ? '' : (card.health.className.match(/\bis-(\w+)/) || [])[1] || 'idle';
			var SEV = { err: 4, warn: 3, paused: 3, run: 2, idle: 1 };
			var lit = (SEV[hTone] || 0) > (SEV[tone] || 0) ? hTone : tone;
			var light = E('span', { 'class': 'mm-light is-' + lit, title: card.state.textContent + (card.health.hidden ? '' : '\n' + card.health.textContent) +
				(prob ? '\n' + prob : '') }, [E('i'), short]);
			var net = card.health.hidden ? '' : E('span', { 'class': 'mm-faint mm-table-net' }, card.health.textContent);
			var badge = card.badge.hidden ? null : card.badge.firstChild && card.badge.firstChild.cloneNode(true);
			var op = card.network.textContent && card.network.textContent !== '—' ? card.network.textContent : '';
			var bars = barsNode(); setBars(bars, data.icon);
			/* one unit in the column (the owner, 2026-10-21: «где-то дБм, где-то проценты»): RSRP when any reading has it, the percent of
			 * ModemManager only without */
			var pct = data.icon && typeof data.icon.rsrp === 'number' ? _('%s dBm').format(data.icon.rsrp) : data.quality !== null ? data.quality + '%' : '';
			var sigWords = card.signal.hidden ? '' : card.sigWords || '';
			/* the card's own actions, by its buttons: one behaviour, one confirmation */
			var acts = [];
			if (!card.fix.hidden) acts.push(E('a', { 'class': 'mm-btn is-primary mm-fix-btn', href: card.fix.getAttribute('href'),
				click: function(ev) { if (card.qmodemDown) { ev.preventDefault(); card.fix.click(); } } }, _('Fix')));
			if (!card.connect.hidden) acts.push(E('button', { type: 'button', 'class': 'mm-btn is-primary', click: function() { card.connect.click(); } }, card.connect.textContent));
			var menuItems = [card.main, card.pause, card.freeze, card.speed].filter(function(b) { return !b.hidden; }).map(function(b) {
				return E('button', { type: 'button', 'class': 'mm-btn is-quiet', click: function() { menu.open = false; b.click(); } }, b.textContent);
			});
			var menu = E('details', { 'class': 'mm-menu mm-more', toggle: function() { if (!menu.open && self.tableDirty) self.applyTable(); } }, [
				E('summary', { 'class': 'mm-btn is-quiet', title: _('Other actions'), 'aria-label': _('Other actions') }, '⋯'), E('div', { 'class': 'mm-pop' }, menuItems)]);
			menu.hidden = !menuItems.length;	/* a property: LuCI's E() would write «hidden="false"» */
			acts.push(menu);	/* the modem's name opens its page: no «Открыть» button to widen the row */
			var open = self.tableOpen.has(id);
			var chev = E('button', { type: 'button', 'class': 'mm-chev mm-row-toggle', 'aria-expanded': String(open), 'aria-label': open ? _('Collapse') : _('More'),
				click: function() { open ? self.tableOpen.delete(id) : self.tableOpen.add(id); self.applyTable(); } });
			chev.innerHTML = CHEV;
			var tr = E('tr', { 'class': [card.node.classList.contains('is-carrying') ? 'is-carrying' : '', card.node.classList.contains('is-stale') ? 'is-stale' : '',
				self.tableSel.has(id) ? 'is-selected' : ''].join(' ').trim(), 'data-device-id': id }, [
				E('td', {}, (function(box) { box.checked = self.tableSel.has(id); return box; })(E('input', { type: 'checkbox', 'class': 'mm-row-check',
					'aria-label': _('Select “%s”').format(card.titleLink.textContent),
					change: function(ev) { ev.target.checked ? self.tableSel.add(id) : self.tableSel.delete(id); self.applyTable(); } }))),
				E('td', { 'class': 'num', 'data-h': '№', title: self.ranked.indexOf(id) >= 0 ? _('Place in the channel priority') : _('No interface: outside the priority') }, String(i + 1)),
								E('td', { 'data-h': _('Modem') }, [E('a', { href: href }, card.titleLink.textContent),
					card.ownerTag.hidden ? '' : card.ownerTag.cloneNode(true),
					card.subtitle.hidden ? '' : E('div', { 'class': 'mm-faint' }, card.subtitle.textContent)]),
				E('td', { 'data-h': _('State') }, [E('span', { 'class': 'mm-row mm-table-state' }, [light, net,
					card.autofixBadge.hidden ? '' : (function(b) { b.addEventListener('click', function() { card.autofixBadge.click(); self.applyTable(); }); return b; })(card.autofixBadge.cloneNode(true))]),
					/* two lines at most: the whole of it in the title and in the row opened (the owner, 2026-10-21: «ломают таблицу») */
					prob ? E('div', { 'class': 'mm-table-prob mm-clamp2', title: prob },
						firstSentence(data.problems[0]) + (data.problems.length > 1 ? ' (+' + (data.problems.length - 1) + ')' : '')) : '']),
				E('td', { 'data-h': _('Operator') }, E('span', { 'class': 'mm-row mm-table-op' }, [badge || '', op || (badge ? '' : '—')])),
				E('td', { 'data-h': _('Signal'), title: sigWords }, E('span', { 'class': 'mm-row mm-table-sig' }, [bars, pct || (bars.hidden ? sigWords || '—' : '')])),
				E('td', { 'data-h': _('Today'), 'class': 'mm-table-today' }, (card.today.textContent || '—').replace(/ · /g, ' ')),
				E('td', {}, E('div', { 'class': 'mm-row mm-table-acts' }, acts.concat([chev])))]);
			rows.push(tr);
			if (open) {
				/* the card's details as two columns, only the rows the card shows at this level */
				var kv = [];
				for (var dt = card.details.firstElementChild; dt; dt = dt.nextElementSibling ? dt.nextElementSibling.nextElementSibling : null) {
					var dd = dt.nextElementSibling;
					if (!dd || dt.hidden || dd.hidden || !dd.textContent || dd.textContent === '—') continue;
					kv.push(E('tr', {}, [E('th', {}, dt.textContent), E('td', {}, dd.textContent)]));
				}
				rows.push(E('tr', { 'class': 'mm-table-more' }, E('td', { colspan: '8' }, kv.length ? E('table', { 'class': 'mm-table mm-kvtable' }, E('tbody', {}, kv)) :
					E('p', { 'class': 'mm-faint' }, _('No details yet.')))));
			}
		});
		this.tableBody.replaceChildren.apply(this.tableBody, rows);
		if (focus) {
			var row = Array.prototype.find.call(this.tableBody.querySelectorAll('tr[data-device-id]'), function(r) { return r.getAttribute('data-device-id') === focus.id; });
			var el = row && row.querySelectorAll('button, a, input, summary')[focus.at];
			if (el) el.focus();
		}
		this.tableAll.checked = ids.length > 0 && this.tableSel.size === ids.length;
		this.tableAll.indeterminate = this.tableSel.size > 0 && this.tableSel.size < ids.length;
		this.applyBulk();
	},

	/* the actions on the checked modems, each through the engine as its card does; one confirmation for all */
	applyBulk: function() {
		var self = this, sel = Array.from(this.tableSel), cards = sel.map(function(id) { return self.cards.get(id); }).filter(Boolean);
		this.bulk.hidden = !cards.length;
		if (!cards.length) { this.bulk.replaceChildren(); return; }
		var names = cards.map(function(c) { return _('“%s”').format(c.titleLink.textContent); }).join(', ');
		/* once: one call for the whole list (the priority), its one result for all; not one call per checked modem (second
		 * audit 2026-10-21: three checked gave three identical priority calls and a partial-failure message after a success) */
		var run = function(what, ask, fn, once) {
			return E('button', { type: 'button', 'class': 'mm-btn', click: mm.busy(function() {
				if (ask && !window.confirm(_('%s %s?').format(ask, names))) return;
				mm.toast('bulk', what + ': ' + names + '…', 'run', 0);
				if (once) return fn().then(function(r) { return r; }, function(e) { return { result: 'error', reason: (e && e.message) || _('no answer') }; })
					.then(function(r) {
						var ok = !r || r.result === 'done' || r.result === 'unchanged';
						mm.toast('bulk', what + ': ' + (ok ? _('done') : _('not done (%s)').format(PAUSE_WHY[r.reason] || r.reason || r.result)), ok ? 'ok' : 'warn', ok ? 8000 : 20000);
						return self.update();
					});
				return Promise.all(cards.map(function(c) { return fn(c).then(function(r) { return [c, r]; }, function(e) { return [c, { result: 'error', reason: (e && e.message) || _('no answer') }]; }); }))
					.then(function(res) {
						var bad = res.filter(function(x) { return x[1] && x[1].result !== 'done'; });
						mm.toast('bulk', what + ': ' + (bad.length ? _('not done for %s').format(bad.map(function(x) { return _('“%s” (%s)').format(x[0].titleLink.textContent,
							PAUSE_WHY[x[1].reason] || x[1].reason || x[1].result); }).join(', ')) : _('done')), bad.length ? 'warn' : 'ok', bad.length ? 20000 : 8000);
						return self.update();
					});
			}) }, what);
		};
		var items = [E('strong', {}, _('Selected: %d').format(cards.length))];
		items.push(run(_('Switch the connection on'), null, function(c) { return connect(c.device.device_id, true, 'LuCI'); }));
		items.push(run(_('Switch the connection off'), _('Switch the connection off for'), function(c) { return connect(c.device.device_id, false, 'LuCI'); }));
		if (cards.length === 1) items.push(run(_('Make main'), _('Make main'), function(c) {
			var id = c.device.device_id;
			return setPriority([id].concat((self.ranked || []).filter(function(x) { return x !== id; }))).then(function(r) {
				if (r && r.result === 'done') self.followMain(id, c.titleLink.textContent, r);
				return r;
			});
		}));
		else items.push(run(_('First in the priority'), _('Put first in the priority'), function() {
			/* the checked ones first in their current order, then the rest: one call for all */
			var first = (self.ranked || []).filter(function(x) { return self.tableSel.has(x); }).concat(sel.filter(function(x) { return (self.ranked || []).indexOf(x) < 0; }));
			return setPriority(first.concat((self.ranked || []).filter(function(x) { return !self.tableSel.has(x); })));
		}, true));
		/* the freeze last, everywhere (the owner, 2026-10-21) */
		items.push(run(_('Pause management'), _('Pause management'), function(c) { return freeze(c.device.device_id, true, 'LuCI'); }));
		items.push(run(_('Resume management'), null, function(c) { return freeze(c.device.device_id, false, 'LuCI'); }));
		items.push(E('button', { type: 'button', 'class': 'mm-btn is-quiet', click: function() { self.tableSel.clear(); self.applyTable(); } }, _('Clear the selection')));
		this.bulk.replaceChildren.apply(this.bulk, items);
	},

	/* measure a modem's temperature again when the last reading is older than mm.TEMP_EVERY_S, one at a time per modem */
	tempMeasure: function(id, tp) {
		var self = this, busy = this.tempBusy = this.tempBusy || {};
		if (busy[id] || document.hidden || (tp.t && Date.now() / 1000 - tp.t < mmc.TEMP_EVERY_S)) return;
		busy[id] = true;
		getPresets(id).then(function(p) {
			var cmds = mmc.tempCommands(p);
			if (!cmds.length) return;
			return mmc.tempAsk(cmds, function(cmd) { return atCommand(id, cmd); }).then(function(a) {
				if (a.c !== null) mmc.tempRecord(id, a.c);
			});
		}).catch(function() {}).finally(function() {
			/* a failed try waits as long as a reading would */
			if (mmc.tempPrefs(id).t === tp.t) mmc.tempPrefs(id, { t: Math.floor(Date.now() / 1000) });
			busy[id] = false;
		});
	},

	makeCard: function(id, alone) {
		var refs = {}, detailsId = 'mm-details-' + ++this.cardSerial, href = L.url('admin/modem/medvemodem/modem') + '#' + encodeURIComponent(id);
		refs.title = E('h3', { 'class': 'mm-mcard-title' }); refs.subtitle = E('span', { 'class': 'mm-faint mm-tile-sub mm-muted' });
		refs.state = E('span', { 'class': 'mm-pill' });
		refs.ownerTag = E('span', { 'class': 'mm-owner-tag', hidden: true });
		refs.badge = E('span', { 'class': 'mm-badge-slot' });
		refs.network = E('span'); refs.signal = E('span', { 'class': 'mm-signal mm-q-word' });
		refs.icon = barsNode(); refs.rates = E('span', { 'class': 'mm-rates mm-faint', hidden: true });
		refs.numbers = E('span', { 'class': 'mm-mono mm-faint', hidden: true });
		refs.ca = E('span', { 'class': 'mm-mono mm-faint mm-ca', hidden: true });
		refs.temp = E('span', { 'class': 'mm-mono mm-faint mm-temp', hidden: true });
		refs.mimo = E('span', { 'class': 'mm-mono mm-faint mm-mimo', hidden: true });
		refs.apn = E('span', { 'class': 'mm-mono mm-faint mm-apn', hidden: true });
		refs.recovering = E('div', { 'class': 'mm-jobline mm-recovering', hidden: true });
		refs.problem = E('div', { 'class': 'mm-tile-prob mm-problem', hidden: true });
		refs.freshness = E('p', { 'class': 'mm-faint', hidden: true });
		/* the basic level: the internet through this modem as checked, apart from its role in the pill (second audit) */
		refs.health = E('p', { 'class': 'mm-health', hidden: true });
		/* first by priority but not carrying (bench 2026-10-22, r82: the chosen card went first and read «резервный»
		 * while the green frame stayed below): said apart */
		refs.firstNote = E('p', { 'class': 'mm-faint mm-first-note', hidden: true });
		/* not a problem: the model is not fully known; the modem page's «Диагностика» collects the report */
		refs.known = E('p', { 'class': 'mm-faint mm-known', hidden: true }, [E('span'), ' ', E('a', { href: href.replace('#', '#svc:') }, _('Collect a report →'))]);
		refs.autofix = E('p', { 'class': 'mm-faint mm-autofix', hidden: true });
		/* the base level: «! N» next to the envelope, the sentence in its tooltip (the owner, 2026-10-16) */
		refs.autofixBadge = E('button', { type: 'button', 'class': 'mm-autofix-badge', hidden: true, click: function(ev) {
			var b = ev.currentTarget; fixSeen(b.getAttribute('data-id'), +b.getAttribute('data-at') || 0); b.hidden = true; } });
		refs.sim = E('div', { 'class': 'mm-faint mm-tile-sim', hidden: true });
		refs.usageText = E('span');
		refs.usageBar = E('span', { 'class': 'mm-usage-bar', hidden: true }, E('i'));
		refs.usage = E('div', { 'class': 'mm-usage', hidden: true }, [refs.usageText, refs.usageBar]);
		refs.sms = E('a', { 'class': 'mm-sms-badge', href: href.replace('#', '#sms:'), hidden: true });
		/* a modem with its own web page (HiLink, UFI): a chip that opens it through the router (the owner, 2026-10-28);
		 * the forward opens on asking, as the modem page's link does (audit 2026-10-21) */
		refs.web = E('a', { 'class': 'mm-web-chip', target: '_blank', rel: 'noopener noreferrer', hidden: true }, _('🌐 Web'));
		refs.web.addEventListener('click', function(ev) {
			var url = refs.web.getAttribute('href');
			if (!url || refs.web.dataset.open === '1') return;
			ev.preventDefault();
			var win = window.open('about:blank', '_blank');
			webOpen(id).then(function() {
				setTimeout(function() { if (win) win.location = url; else location.href = url; }, 500);
			}, function(e) {
				if (win) win.close();
				mm.toast('web', _('The web interface forward did not open: %s').format(mm.failText(e)), 'err', 8000);
			});
		});
		refs.details = E('dl', { id: detailsId, 'class': 'mm-kv mm-tile-more' });
		refs.rows = {};
		[['owner', _('Management'), 'expert'], ['iface', _('Interface'), 'expert'], ['proto', _('Protocol'), 'expert'], ['address', _('Address'), 'expert'],
		 ['simstate', _('SIM card'), 'expert'], ['uptime', _('Session length')], ['check', _('Connection check')], ['session_traffic', _('Session traffic')], ['today', _('For today')], ['quota', _('SIM limit')], ['event', _('Event')],
		 ['path', _('USB connection'), 'expert'], ['id', _('Identifier'), 'expert'], ['generation', _('USB connections since start'), 'expert'],
		 ['reason', _('Diagnostics'), 'expert']].forEach(function(row) {
			var dt = E('dt', {}, row[1]);
			refs[row[0]] = E('dd'); refs.details.appendChild(dt); refs.details.appendChild(refs[row[0]]);
			refs.rows[row[0]] = [dt, refs[row[0]], row[2]];
		});
		refs.open = stored(id, alone); refs.details.hidden = !refs.open;
		refs.toggle = E('button', { type: 'button', 'class': 'mm-chev mm-toggle', 'aria-controls': detailsId,
			'aria-expanded': String(refs.open), 'aria-label': _('More'), click: function() {
				refs.open = !refs.open; refs.details.hidden = !refs.open;
				refs.toggle.setAttribute('aria-expanded', String(refs.open));
				refs.toggle.setAttribute('aria-label', refs.open ? _('Collapse') : _('More')); remember(id, refs.open);
			} });
		refs.toggle.innerHTML = CHEV;
		/* a button, not a bare link (the owner, 2026-10-16) */
		refs.link = E('a', { 'class': 'mm-btn mm-open', href: href, role: 'button' }, _('Open the modem →'));
		refs.fix = E('a', { 'class': 'mm-btn is-primary mm-fix-btn', href: href.replace('#', '#svc:'), hidden: true, click: function(ev) {
			/* QModem's dial stopped: started again here, one call (field 2026-10-20: «qmodem не перезапускается») */
			if (!refs.qmodemDown) return;
			ev.preventDefault();
			if (refs.fix.getAttribute('aria-busy')) return;
			refs.fix.setAttribute('aria-busy', 'true');
			mm.toast('qmodem', _('Starting QModem\'s connection…'), 'run', 0);
			/* QModem's reload, never its restart: that hangs every dial up and takes the network down (bench 2026-10-20) */
			/* its section on too (state, enable_dial): a reload alone left QModem with nothing to dial (the owner, 2026-10-21) */
			var sec = ((refs.device || {}).qmodem || {}).section;
			fs.exec('/usr/libexec/medvemodem/service', sec ? ['qmodem', 'enable', sec] : ['qmodem', 'reload']).then(function(r) {
				var j = {}; try { j = JSON.parse((r && r.stdout) || '{}'); } catch (e) {}
				var on = !!(((j.others || {}).qmodem_network) || {}).running;
				mm.toast('qmodem', j.error ? _('QModem not started: %s').format(j.error) : on ? _('QModem started: the modem is connecting.') :
					_('QModem started, but it has nothing to connect: the modem is not switched on in its settings.'), j.error || !on ? 'warn' : 'ok', 10000);
			}, function(e) { mm.toast('qmodem', _('QModem not started: %s').format((e && e.message) || e), 'err', 15000); }).then(function() {
				refs.fix.removeAttribute('aria-busy');
				self.qmAskedAt = 0;
				return self.update();
			});
		} }, _('Fix'));
		/* the fix itself when it is one call (the owner, 2026-10-17): the interface made and brought up */
		refs.connect = E('button', { type: 'button', 'class': 'mm-btn is-primary mm-connect-btn', hidden: true, click: mm.busy(function() {
			return bringup(id).then(function(r) {
				r = r || {};
				if (['connected', 'started', 'running'].indexOf(r.result) < 0) window.alert(_('Not connected: %s').format(PAUSE_WHY[r.reason] || r.reason || r.result || _('no answer')));
			}, function() { window.alert(_('The MedveModem service does not answer.')); }).finally(function() { return self.update(); });
		}) }, _('Connect'));
		/* «Деактивировать» (owner's request 2026-10-15): the interface down through the engine until «Активировать»
		 * or a reboot; netifd keeps it down and the engine leaves it alone (m_connect) */
		var self = this;
		refs.pause = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-pause', hidden: true, click: mm.busy(function() {
			var up = refs.pause.getAttribute('data-up') !== '1';
			if (!up && !window.confirm(_('Switch the connection “%s” off? The link through this modem is gone until “Switch the connection on” or a router restart.').format(refs.titleLink.textContent)))
				return;
			refs.pause.disabled = true;
			return connect(id, up, 'LuCI').then(function(r) {
				if (r && r.result !== 'done') window.alert((up ? _('Not activated: %s') : _('Not deactivated: %s')).format(PAUSE_WHY[r.reason] || r.reason || r.result));
			}, function() { window.alert(_('The MedveModem service does not answer.')); })
				.finally(function() { refs.pause.disabled = false; return self.update(); });
		}) });
		/* the rare actions behind «⋯» (UX audit 2026-10-17): one main button on a card; the menu closes after a choice */
		refs.morePop = E('div', { 'class': 'mm-pop' });
		refs.more = E('details', { 'class': 'mm-menu mm-more', hidden: true }, [
			E('summary', { 'class': 'mm-btn is-quiet', title: _('Other actions'), 'aria-label': _('Other actions') }, '⋯'), refs.morePop]);
		refs.morePop.addEventListener('click', function(ev) { if (ev.target.closest('button')) refs.more.open = false; });
		/* «Сделать основным» (the owner, 2026-10-16): this modem first in the priority, the rest in their order */
		refs.main = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-make-main', hidden: true, click: mm.busy(function() {
			var order = [id].concat((self.ranked || []).filter(function(x) { return x !== id; }));
			var name = refs.titleLink.textContent;
			/* the wired channels keep their place in the common order, before or after the modems (audit r101 R07: the
			 * text said a WAN always stays above, the engine keeps «WAN second» second) */
			if (!window.confirm(_('Put “%s” first among the modems? When it has internet, the modems\' traffic goes through it, the rest stay as backups. The wired channels keep their place in the common order.').format(name)))
				return;
			refs.main.disabled = true;
			return setPriority(order).then(function(r) {
				if (r && r.result !== 'done') window.alert(_('Not put first: %s').format(PAUSE_WHY[r.reason] || r.reason || r.result));
				else self.followMain(id, name, r);
			}, function() { window.alert(_('The MedveModem service does not answer.')); })
				.finally(function() { refs.main.disabled = false; return self.update(); });
		}) }, _('Make the main modem'));
		/* «Приостановить управление» (the owner, 2026-10-19): not only the interface down — no service touches the modem on USB:
		 * MedveModem starts nothing on it, ModemManager is kept off it, QModem's dial of it stops */
		refs.freeze = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-freeze', hidden: true, click: mm.busy(function() {
			var on = refs.freeze.getAttribute('data-frozen') !== '1';
			if (on && !window.confirm(_('Pause the management of the modem “%s”? The connection through it goes off, MedveModem stops reading, connecting and recovering it, ModemManager lets it go, QModem stops dialling. The modem stays untouched until you unfreeze it (also after a router restart).').format(refs.titleLink.textContent)))
				return;
			refs.freeze.disabled = true;
			return freeze(id, on, 'LuCI').then(function(r) {
				r = r || {};
				if (r.result !== 'done' && r.result !== 'unchanged') window.alert((on ? _('Management not paused: %s') : _('Management not resumed: %s')).format(PAUSE_WHY[r.reason] || r.reason || r.result));
				else if (r.qmodem_warning) window.alert(on ? _('The modem management is paused, but QModem\'s dialling is not stopped: QModem is busy, try again in a minute.') :
					_('The modem management is resumed, but QModem\'s dialling is not switched on: QModem is busy, try again in a minute.'));
			}, function() { window.alert(_('The MedveModem service does not answer or cannot freeze the modem.')); })
				.finally(function() { refs.freeze.disabled = false; return self.update(); });
		}) });
		/* «Замерить скорость» from the card (the owner, 2026-11-08): the modem page's
		 * measurement through this modem's own device, its result in the card's line; the shape and the advice stay on the
		 * modem page */
		refs.speed = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-speed', hidden: true, click: mm.busy(function() {
			var dev = refs.speed.getAttribute('data-dev');
			if (!dev) return;
			if (!window.confirm(_('Measure the speed through “%s”? It takes about 20 seconds and 30–100 MB of the SIM\'s traffic.').format(dev))) return;
			refs.speed.disabled = true;
			self.speedRes[id] = { running: true };
			self.update();
			return mm.execLong(fs, 'speedtest', '/usr/libexec/medvemodem/speedtest', [dev], 120000).then(function(r) {
				var j = null;
				try { j = JSON.parse((r && r.stdout) || ''); } catch (e) {}
				if (!j) throw new Error(_('no answer'));
				/* one way measured is a result: the other one said as not measured (r184) */
				var dOk = j.down && !j.down.error, uOk = j.up && !j.up.error;
				if (!dOk && !uOk) throw new Error(mmc.speedErr(j));
				self.speedRes[id] = { down: dOk ? speedMean(j.down.kbps) : null, up: uOk ? speedMean(j.up.kbps) : null, at: Date.now(),
					note: j.error ? mmc.speedErr(j) : '' };
			}).catch(function(e) {
				self.speedRes[id] = { error: (e && e.message) || String(e) };
			}).finally(function() { refs.speed.disabled = false; return self.update(); });
		}) }, _('Measure the speed'));
		refs.speedSaid = E('span', { 'class': 'mm-mono mm-faint mm-speed-said', hidden: true });
		refs.morePop.append(refs.main, refs.pause, refs.freeze, refs.speed);
		/* a modem pulled out of USB: nothing to fix, it can only be taken off the list (the owner, 2026-10-31). The engine keeps
		 * it until the router or the service restarts, so its name and settings come back with it; here it is hidden in this
		 * browser until it is plugged in again */
		refs.goneNote = E('p', { 'class': 'mm-faint mm-gone-note', hidden: '' },
			_('A disconnected modem stays on the list until the router or the MedveModem service restarts: then its name and settings come back when it is plugged in again.'));
		refs.forget = E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-forget', hidden: '', click: function() {
			hiddenGone(id, true);
			refs.node.hidden = true;
			mm.toast('gone-' + id, _('The modem is taken off the list. It comes back by itself when it is plugged in again.'), 'ok', 6000);
		} }, _('Remove from the list'));
		refs.titleLink = E('a', { href: href });
		refs.title.appendChild(refs.titleLink);
		refs.node = E('article', { 'class': 'mm-card mm-mcard mm-tile', 'data-device-id': id }, [
			E('div', { 'class': 'mm-mcard-top mm-card-head' }, [E('div', { 'class': 'mm-mcard-id' }, [refs.title, refs.subtitle]),
				E('div', { 'class': 'mm-row mm-mcard-icons' }, [refs.ownerTag, refs.autofixBadge, refs.web, refs.sms, refs.toggle])]),
			E('div', {}, refs.state),
			refs.sum = E('div', { 'class': 'mm-sum mm-network' }, [E('a', { 'class': 'mm-to-radio', href: href.replace('#', '#radio:'),
				title: _('Network and signal: cells, bands') }, refs.icon), refs.badge, refs.network]),
			refs.sub = E('a', { 'class': 'mm-sum-sub mm-dots mm-to-radio', href: href.replace('#', '#radio:'), title: _('Network and signal: cells, bands') }),
			refs.health, refs.firstNote, refs.sim, refs.usage,
			refs.recovering, refs.problem, refs.autofix, refs.freshness, refs.known, refs.details, refs.goneNote,
			E('div', { 'class': 'mm-actions' }, [refs.connect, refs.fix, refs.forget, refs.link, refs.more])
		]);
		this.grid.appendChild(refs.node);
		return refs;
	},

	kernelShow: function(k) {
		var now = kfaultLines((k || {}).kinds, hhmm), prev = kfaultLines(((k || {}).previous_boot || {}).kinds, function(t) { return new Date(t * 1000).toLocaleString(LANG); });
		this.kList.replaceChildren.apply(this.kList, now);
		this.kPrevList.replaceChildren.apply(this.kPrevList, prev);
		var crit = Object.keys((k || {}).kinds || {}).some(function(x) { return k.kinds[x].critical; });
		this.kern.className = 'mm-callout mm-kernel mm-dismissible ' + (crit ? 'is-err' : 'is-warn');
		mmc.bannerShow(this.kern, now.length > 0);
		mmc.bannerShow(this.kPrev, prev.length > 0);
	},

	apply: function(status, devices, uplink) {
		this.kernelShow(status && status.kernel);
		var self = this, seen = new Set(), present = 0, connected = 0, up = [], off = [], retune = [], check = [], lv = this.level;
		/* QModem's services, asked only while a present modem is QModem's, at most once a minute (a dozen processes each) */
		if (devices.some(function(d) { return d.present === true && (d.owner || {}).control === 'qmodem'; })) {
			if (!this.qmAskedAt || Date.now() - this.qmAskedAt > 60000) {
				this.qmAskedAt = Date.now();
				fs.exec('/usr/libexec/medvemodem/service', ['status']).then(function(r) {
					var j = {}, o;
					try { j = JSON.parse((r && r.stdout) || '{}'); } catch (e) {}
					o = j.others || {};
					/* QModem's dial only: its boot scan (qmodem_init) is off by design and tells nothing (the owner, 2026-10-20) */
					var was = self.qmodemRunning;
					self.qmodemRunning = (o.qmodem_network || {}).installed ? !!o.qmodem_network.running : null;
					if (was !== self.qmodemRunning && self.lastStatus) self.apply(self.lastStatus, self.lastDevices, self.lastUplink);
				}, function() {});
			}
			status = Object.assign({}, status, { qmodemRunning: this.qmodemRunning });
		}
		this.lastStatus = status; this.lastDevices = devices; this.lastUplink = uplink;
		this.applySuspects((status || {}).usb_class);
		/* a modem ModemManager does not hold (HiLink, QModem, AT): its network and signal as its own page reads them
		 * (the owner, 2026-10-20: the E3372 showed no operator nor signal here, its page did) */
		devices = devices.map(function(d) { return self.withReading(d); });
		/* a recovery step said in the corner as it starts and ends (the owner, 2026-10-20: nothing showed after the button) */
		mm.recoveryNotes(devices, function(d) { return words(d.name) || words((d.modemmanager || {}).model) || words(d.product) || _('Modem'); });
		/* the operator's priority (the interface's route metric): the modems with an interface, first = main */
		var metricOf = function(d) { var m = (d.connection || {}).metric; return typeof m === 'number' ? m : 1e9; };
		this.ranked = devices.map(function(d, i) { return [d, i]; }).filter(function(p) { return words((p[0].connection || {}).interface) || words((p[0].session || {}).interface); })
			.sort(function(a, b) { return metricOf(a[0]) - metricOf(b[0]) || a[1] - b[1]; }).map(function(p) { return p[0].device_id; });
		var parts = [];
		devices.forEach(function(device) {
			var id = device.device_id, data = describe(device, status), card = self.cards.get(id), mm = device.modemmanager || {};
			if (!card) { card = self.makeCard(id, devices.length === 1); self.cards.set(id, card); }
			card.data = data; card.device = device;	/* the table view reads them */
			seen.add(id); present += data.present ? 1 : 0; connected += data.connected ? 1 : 0;
			(!data.present ? off : data.connected ? up : []).push(data.label);
			if (data.present && data.problems.length) (data.retune ? retune : check).push(data.name);
			/* the USB path is no news at the basic level: the model, or nothing */
			text(card.titleLink, data.name); text(card.subtitle, lv === 'normal' && !words(device.name) ? '' : data.subtitle); text(card.state, data.state);
			card.subtitle.hidden = !card.subtitle.textContent || card.subtitle.textContent === '—';
			mmc.setOwnerBadge(card.ownerTag, ((device.owner || {}).control));
			/* the modem's own web through the router's address (web-forward), the HiLink home by its path */
			var wc = device.connection || {}, wport = data.present && wc.web_port;
			card.web.hidden = !wport;
			if (wport) {
				var whost = location.hostname.indexOf(':') >= 0 ? '[' + location.hostname + ']' : location.hostname;
				card.web.setAttribute('href', 'http://' + whost + ':' + wport + '/' + (/^12d1:/.test(device.vidpid || '') ? 'html/home.html' : ''));
				card.web.dataset.open = wc.web_open_s === -1 || wc.web_open_s > 60 ? '1' : '';
				card.web.title = _('The modem\'s web interface, through the router (port %s)').format(wport);
			}
			/* a backup says its place in the priority (the owner, 2026-10-19: «нумеровать в соответствии с позицией») */
			var place = self.ranked.indexOf(id);
			var rk = data.roleKey && ROLES[data.roleKey];
			if (place > 0 && rk && rk[2]) data.state = data.state.replace(rk[0], rk[2].format(place));
			/* the table's short word: «Main», «Backup 2, ready» */
			data.short = rk && rk[3] ? (place > 0 && rk[4] ? rk[4].format(place) : rk[3]) : '';
			text(card.state, data.state);
			card.state.className = 'mm-pill ' + (!data.present ? 'is-off' : data.recovering || (device.recovery || {}).state === 'running' ? 'is-run' :
				TONE_PILL[data.tone] || 'is-idle') + ' mm-' + data.tone;
			card.node.classList.toggle('is-carrying', !!data.carrying);	/* the owner, 2026-10-20: the active card framed green, no fill */
			/* framed in its pill's colour when the main one answers only the whitelist (the owner, 2026-10-28) */
			card.node.classList.toggle('is-carrying-warn', !!data.carrying && !!rk && rk[1] === 'warn');
			card.node.classList.toggle('is-stale', !data.present);
			card.node.classList.toggle('mm-absent', !data.present);
			var badge = data.present ? opBadge(mm.operator_code, words(mm.operator), (device.sim_operator || {}).code) : null;
			card.badge.replaceChildren.apply(card.badge, badge ? [badge] : []);
			card.badge.hidden = !badge;
			/* the badge names the operator: the line keeps the technology only */
			/* «LTE+NR NSA» is the expert's (the owner, 2026-10-17); the others read «LTE / 5G» */
			var tech = (lv === 'expert' ? techExact : techWords)(mm.access_tech);
			text(card.network, badge ? tech || data.network : data.network.replace(techWords(mm.access_tech), tech));
			setBars(card.icon, data.icon);
			var iface = words((device.connection || {}).interface);
			card.pause.hidden = !data.present || !iface || data.frozen;
			card.freeze.hidden = !data.present;
			card.freeze.setAttribute('data-frozen', data.frozen ? '1' : '0');
			text(card.freeze, data.frozen ? _('Resume management') : _('Pause management'));
			card.pause.setAttribute('data-up', data.connected ? '1' : '0');
			text(card.pause, data.connected ? _('Switch the connection off') : _('Switch the connection on'));
			var rank = self.ranked.indexOf(id);
			card.main.hidden = !data.present || rank <= 0 || data.paused;
			var carrier = (uplink && uplink.carrier) || {}, carrierCard = carrier.device_id && self.cards.get(carrier.device_id);
			var notFirst = rank === 0 && data.present && !data.carrying && !data.frozen && carrier.device_id && carrier.device_id !== id;
			text(card.firstNote, notFirst ? _('First in the priority · the traffic still goes through “%s”').format(carrierCard ? carrierCard.titleLink.textContent : words(carrier.name) || carrier.device) +
				((data.reach || {}).ok === false ? ': ' + _('this modem has no internet') : '') : '');
			card.firstNote.hidden = !notFirst;
			var spDev = data.present && data.connected && !data.frozen ? words((device.uplink || {}).device) : '';
			card.speed.hidden = !spDev; card.speed.setAttribute('data-dev', spDev);
			var spr = self.speedRes[id];
			text(card.speedSaid, !spr ? '' : spr.running ? _('measuring the speed…') : spr.error ? _('speed not measured: %s').format(spr.error) :
				_('speed ↓ %s ↑ %s').format(spr.down === null ? '—' : speedWords(spr.down), spr.up === null ? '—' : speedWords(spr.up)));
			card.speedSaid.title = spr && spr.note || '';
			card.speedSaid.hidden = !spr || !data.present;
			card.more.hidden = card.main.hidden && card.pause.hidden && card.freeze.hidden && card.speed.hidden;
			/* the header: the hardware's name, its place in the priority or why it carries nothing */
			parts.push([rank < 0 ? 1e9 : rank, data.hw + ' (' + (!data.present ? _('disconnected from USB') + (device.frozen ? ', ' + _('management paused') : '') : data.frozen ? _('management paused') : data.pausedAuto ? _('switched off, reconnecting') : data.paused ? _('deactivated') :
				(rank === 0 ? (data.carrying ? _('main') : _('first in the priority')) : rank > 0 ? (self.ranked.length > 2 ? _('backup %d').format(rank) : _('backup')) : _('no interface')) +
				(data.connected ? '' : ', ' + _('no link'))) + ')']);
			/* the normal level names only bad noise; the experienced one the numbers too */
			/* a phone's signal is on the phone: nothing said here rather than «нет данных» */
			var sigPct = lv === 'normal' ? mmc.signalPct(data.icon) : null;
			var sigWords = data.phone && !data.icon ? '' : data.icon ? signalWord(data.icon) + (sigPct !== null ? ' · ' + sigPct + '%' : '') +
				(data.readingAge > 120 ? ' · ' + agoWords(data.readingAge) : '') :
				data.unregistered || data.readingPending ? '' : data.quality === null ? _('no data') : data.quality + '%';
			var sigSaid = data.icon || !sigWords ? sigWords : _('Signal: %s').format(sigWords);
			card.sigWords = sigWords;	/* the table's column: the words without «Signal:» */
			text(card.signal, sigSaid); card.signal.hidden = !sigSaid;
			card.signal.classList.toggle('z1', !!(data.icon && data.icon.noisy));
			var sig = mm.signal || {}, c = sig.lte || sig['5g'] || {};
			/* the carriers aggregated now (the owner, 2026-10-19), from the engine's CA cells; every level since 2026-10-20
			 * («в карточках до сих пор нет агрегации») */
			var caOn = data.present ? carriers(mm) : [];
			var caMhz = caOn.every(function(x) { return x.mhz; }) ? caOn.reduce(function(a, x) { return a + x.mhz; }, 0) : 0;
			var csnr = sinrOf(c);
			text(card.numbers, typeof c.rsrp === 'number' ? 'RSRP ' + _('%s dBm').format(num(c.rsrp)) + (csnr !== null ? ' · SINR ' + _('%s dB').format(num(csnr)) : '') : '');
			text(card.ca, caOn.length > 1 ? 'CA ' + caOn.map(function(x) { return x.name; }).join('+') + (caMhz ? ', ' + _('%s MHz').format(num(caMhz)) : '') : '');
			/* text() writes «—» for nothing: never a part of the line. The basic level says the signal in words and percent,
			 * the numbers and the aggregation from «Подробный» (the owner, 2026-11-12, after 2026-11-08 had put them back) */
			card.ca.hidden = lv === 'normal' || !(caOn.length > 1);
			card.numbers.hidden = lv === 'normal' || !data.present || typeof c.rsrp !== 'number';
			text(card.rates, data.rates); card.rates.hidden = !data.rates;
			/* «Подробный»: the main carrier's MIMO layers and the APN in use (the owner, 2026-11-08:
			 * «MIMO 1×1», «APN: internet.beeline.ru») */
			var prim = data.present ? list(mm.cells).filter(function(x) { return x && x.serving && typeof x.mimo_layers === 'number'; })
				.sort(function(a, b) { return (a.role === 'primary' ? 0 : 1) - (b.role === 'primary' ? 0 : 1); })[0] : null;
			text(card.mimo, prim ? 'MIMO ' + prim.mimo_layers : '');
			card.mimo.hidden = lv === 'normal' || !prim;
			var apnNow = data.present && data.connected ? words(device.connection && (device.connection.apn_in_use || device.connection.apn)) : '';
			text(card.apn, apnNow ? 'APN ' + apnNow : '');
			card.apn.hidden = lv === 'normal' || !apnNow;
			/* the modem's temperature, when its page's «Температура» asks for it (the owner, 2026-10-19): from «Подробный» */
			var tp = lv !== 'normal' && data.present ? mmc.tempPrefs(id) : {};
			text(card.temp, tp.dash && tp.c != null ? num(tp.c) + ' °C' : '');
			card.temp.hidden = !(tp.dash && tp.c != null);
			if (tp.dash && !data.frozen) self.tempMeasure(id, tp);
			/* the second line: only what is there, so the dots separate real parts */
			card.sub.replaceChildren.apply(card.sub, [card.signal, card.numbers, card.ca, card.mimo, card.temp, card.apn, card.rates, card.speedSaid].filter(function(n) { return !n.hidden && n.textContent && n.textContent !== '—'; }));
			card.sum.hidden = card.sub.hidden = !data.present;
			/* the basic level: the first problem's first sentence, the rest on the modem page («Исправить») */
			/* «И ещё 3» opens the rest in place (the owner, 2026-10-20: «3. - события??») */
			var probAll = data.problems.join(' '), probShort = lv === 'normal' && data.problems.length ? firstSentence(data.problems[0]) : probAll;
			var probMore = probShort !== probAll && data.problems.length > 1 ? _('And %d more').format(data.problems.length - 1) : '';
			card.problem.replaceChildren(card.probOpen && probMore ? probAll : probShort);
			if (probMore)
				card.problem.append(' ', E('button', { type: 'button', 'class': 'mm-linkbtn mm-prob-more', 'aria-expanded': String(!!card.probOpen),
					click: function() { card.probOpen = !card.probOpen; if (self.lastStatus) self.apply(self.lastStatus, self.lastDevices, self.lastUplink); } },
					card.probOpen ? _('Collapse') : probMore + ' →'));
			card.problem.title = data.problems.join(' ');
			card.problem.hidden = !data.problems.length;
			text(card.sim, data.sim ? 'SIM: ' + data.sim : ''); card.sim.hidden = !data.present || !data.sim;
			text(card.usageText, data.usage); card.usage.hidden = !data.present || !data.usage;
			card.usageBar.hidden = data.usage_pct === null;
			if (data.usage_pct !== null) card.usageBar.firstChild.style.width = data.usage_pct + '%';
			card.usage.className = 'mm-usage' + (data.usage_tone === 'exceeded' ? ' is-err' : data.usage_tone === 'warn' ? ' is-warn' : '');
			var fixedBoth = autofixLine(device.autofix), fixed = fixedBoth[0], fixN = (device.autofix && device.autofix.count) || 1;
			text(card.autofix, fixed); card.autofix.hidden = !fixed || lv === 'normal';
			/* the «! N» of the last hour's automatic repairs at every level and in the table (the owner, 2026-11-09: «в подробном
			 * виде на карточках модемов и в режиме таблицы нет отображения ошибок самодиагностики как в базовом») */
			var fixAt = fixed && typeof device.autofix.at === 'number' ? device.autofix.at : 0;
			card.autofixBadge.hidden = !fixed || (fixAt > 0 && fixSeen(id) >= fixAt);
			if (fixed) {
				text(card.autofixBadge, '! ' + fixN);
				card.autofixBadge.setAttribute('data-id', id); card.autofixBadge.setAttribute('data-at', String(fixAt));
				card.autofixBadge.title = _('Restored automatically in the last hour: %d. The last one — %s').format(fixN, fixedBoth[1]) + ' ' +
					_('Click to hide it until the next repair.');
				card.autofixBadge.setAttribute('aria-label', card.autofixBadge.title);
			}
			card.sms.hidden = !data.sms && !data.smsWarn;
			card.sms.classList.toggle('is-warn', !!data.smsWarn);
			if (data.sms || data.smsWarn) {
				var said = [data.sms ? _('Unread SMS: %d').format(data.sms) : '', data.smsWarn].filter(Boolean).join('. ');
				card.sms.innerHTML = ENVELOPE;
				card.sms.appendChild(E('span', {}, data.sms ? String(data.sms) : '!'));
				card.sms.setAttribute('aria-label', said);
				card.sms.title = said;
			}
			card.problem.className = 'mm-tile-prob mm-problem' + (!data.present || data.probErr ? ' is-err' : '');
			card.connect.hidden = !data.noIface || (device.recovery || {}).state === 'running';
			card.qmodemDown = data.qmodemDown;
			card.fix.hidden = !data.present || !data.problems.length || data.noIface || (device.recovery || {}).state === 'running';
			/* the ladder spent: «Исправить» opens the last resort on the modem's page */
			card.fix.setAttribute('href', card.link.getAttribute('href').replace('#', data.rescue ? '#rescue:' : '#svc:'));
			card.forget.hidden = card.goneNote.hidden = !!data.present;
			/* taken off the list while away; back by itself once plugged in */
			if (data.present) hiddenGone(id, false);
			card.node.hidden = !data.present && hiddenGone(id);
			text(card.recovering, data.recovering); card.recovering.hidden = !data.recovering;
			text(card.freshness, data.freshness); card.freshness.hidden = !data.freshness;
			/* the internet lamp at every level and in the table (the owner, 2026-11-12: «и в подробном режиме карточки тоже») */
			var role = (device.uplink || {}).role, st = data.present && !data.frozen ? mmc.checkState(data.reach, role) : null;
			text(card.health, st ? st.text : ''); card.health.hidden = !st;
			card.health.className = 'mm-health ' + (st ? 'is-' + st.tone : '');
			/* one action on a basic card: «Подключить» or «Исправить» when there is one; the name opens the modem anyway */
			card.link.hidden = lv === 'normal' && (!card.connect.hidden || !card.fix.hidden);
			text(card.known.firstChild, mmc.knownLess(device)); card.known.hidden = !mmc.knownLess(device);
			['owner', 'iface', 'proto', 'address', 'simstate', 'uptime', 'check', 'session_traffic', 'today', 'quota', 'event', 'path', 'id', 'generation', 'reason'].forEach(function(key) {
				var r = card.rows[key], show = (!r[2] || lv === 'expert') && !(key === 'quota' && data.quota === '—');
				text(card[key], key === 'check' && lv === 'normal' && data.present ? checkSimple(data.reach, (device.uplink || {}).role) || data.check : data[key]);
				/* the basic level: no empty rows, and the quota is said by the usage line above */
				if (lv === 'normal' && (card[key].textContent === '—' || key === 'quota')) show = false;
				r[0].hidden = r[1].hidden = !show;
			});
		});
		this.cards.forEach(function(card, id) { if (!seen.has(id)) { card.node.remove(); self.cards.delete(id); } });
		/* the operator's priority first (the interface's route metric, «Настройки» → «Приоритет модемов»); without one, as listed */
		var rank = function(d) { var m = (d.connection || {}).metric; return typeof m === 'number' ? m : 1e9; };
		/* the card being used keeps its place on the screen and its focus (bench 2026-10-22: «Сделать основным» threw the
		 * page to another card); moving a node drops the focus inside it */
		var focus = document.activeElement, held = focus && self.grid.contains(focus) ? focus.closest('.mm-tile') : null;
		var top = held ? held.getBoundingClientRect().top : 0;
		devices.map(function(d, i) { return [d, i]; }).sort(function(a, b) { return rank(a[0]) - rank(b[0]) || a[1] - b[1]; })
			.forEach(function(p, i) { var node = self.cards.get(p[0].device_id).node;
				if (self.grid.children[i] !== node) self.grid.insertBefore(node, self.grid.children[i] || null); });
		if (held) {
			var moved = held.getBoundingClientRect().top - top;
			if (moved) window.scrollBy(0, moved);
			if (document.activeElement !== focus && document.contains(focus)) try { focus.focus({ preventScroll: true }); } catch (e) {}
		}
		this.applyTable();
		var n = devices.length;
		/* said as a person would (user review 2026-10-08): «Подключено: Стенд (T99W175). Всё в порядке.»,
		 * «Отключён от USB: Дача (EP06-E).», «Требуется перенастройка: Дача.» — names, not counts */
		/* short (the owner, 2026-10-16): how many, by the hardware's name, who is main and who backs it up; the
		 * problems stay on the cards */
		parts.sort(function(a, b) { return a[0] - b[0]; });
		/* from three modems a count (UX audit 2026-10-17: the names fill a phone's header); the cards name them */
		text(this.counts, !n ? _('No modems found.') : n < 3 ? _('Modems: %d — %s').format(n, parts.map(function(p) { return p[1]; }).join(', ')) :
			_('Modems: %d · connected: %d').format(n, connected) + (n - connected ? ' · ' + _('without a link: %d').format(n - connected) : ''));
		text(this.summary, !present ? _('No connected modems') : connected ? _('Active connections: %d').format(connected) : _('No confirmed connection'));
		this.applyUplink(uplink, devices, connected, present);
		this.applyActions(devices);
		this.empty.hidden = devices.length !== 0;
		/* one version for both; a page left from another build says so */
		/* the engine knows its version without the release (0.15.0), the page with it (0.15.0-r69): one version when they agree */
		var engineVersion = String(status.version || '').replace(/_/g, '-');
		var same = engineVersion === APP_VERSION || engineVersion === String(APP_VERSION).replace(/-r\d+$/, '');
		text(this.version, 'MedveModem ' + (same ? APP_VERSION : _('%s · web interface %s (another version)').format(status.version, APP_VERSION)));
		this.lastSuccess = new Date();
		text(this.updated, _('updated at %s').format(this.lastSuccess.toLocaleTimeString(LANG)));
		this.error.hidden = true; this.root.classList.remove('mm-stale');
	},

	applyActions: function(devices) {
		var rows = devices.filter(function(d) { return d.present !== false && list(d.action_needed).length; }).map(function(d) {
			var mm = d.modemmanager || {}, name = words(d.name) || words(mm.model) || words(d.model) || words(d.product) || _('USB modem');
			var href = L.url('admin/modem/medvemodem/modem') + '#svc:' + encodeURIComponent(d.device_id);
			return E('li', { 'class': 'mm-act-row' }, [E('span', {}, [E('b', {}, name), ': ' + list(d.action_needed).map(function(k) { return ACTIONS[k] || k; }).join('; ')]),
				E('a', { 'class': 'mm-btn mm-act-go', href: href }, _('Open →'))]);
		});
		this.actList.replaceChildren.apply(this.actList, rows);
		mmc.bannerShow(this.actions, rows.length > 0);
	},

	withReading: function(d) {
		var self = this, ctl = (d.owner || {}).control, id = d.device_id;
		if (d.modemmanager || d.present !== true || d.frozen || !ctl || ctl === 'modemmanager' || ctl === 'none' || ctl === 'unresolved') return d;
		if (!this.readings) this.loadReadings();
		var c = this.readings[id] || (this.readings[id] = {});
		/* USB re-enumeration or an owner change invalidates prior AT readings;
		 * an async response from the old generation must not replace them. */
		if (c.generation !== d.generation || c.owner !== ctl) {
			c = this.readings[id] = { generation: d.generation, owner: ctl };
		}
		/* at most once a minute (QModem's own tool on its port: every 2 min), one request at a time; a refusal (the port
		 * busy with the modem page's own read) is asked again in 20 s and the last good reading kept up to 10 min (the
		 * owner, 2026-10-20: «на дашборде оператор пока неизвестен» while the FM350's page named Билайн) */
		var every = c.failed ? 20000 : ctl === 'qmodem' ? 120000 : 60000;
		if (!c.busy && (!c.asked || Date.now() - c.asked > every)) {
			c.asked = Date.now(); c.busy = true;
			getMetrics(id, ctl === 'qmodem' ? 120000 : 60000).then(function(r) {
				if (self.readings[id] !== c) return;
				if (r && r.metrics) {
					/* field by field over the last one: a reading without the operator does not wipe it (review 2026-10-22) */
					/* a reading made of the web's «no SIM / no network» is not built upon: «SIM not found» stayed after the SIM
					 * came back (regression review 2026-11-11) */
					var fresh = mm.atAsMm(r.metrics), old = c.mm && !c.mm.web_off ? c.mm : {};
					c.mm = Object.assign({}, old, fresh, { signal: Object.keys(fresh.signal || {}).length ? fresh.signal : old.signal || {} });
					['operator', 'operator_code', 'registration'].forEach(function(k) { if (!fresh[k] && old[k]) c.mm[k] = old[k]; });
					if (!(fresh.access_tech || []).length && old.access_tech) c.mm.access_tech = old.access_tech;
					c.at = Date.now() - (typeof r.age_ms === 'number' && r.age_ms >= 0 ? Math.min(600000, r.age_ms) : 0);
					c.failed = false;
					self.saveReadings();
				} else if (r && r.web && (r.web.sim || r.web.network === 'none')) {
					/* the modem's web says it has no SIM or no network (the tester's M150-4, 2026-11-09: the card said only
					 * «оператор пока неизвестен»): said on the card, the last operator and signal dropped */
					c.mm = { web_off: true, failed_reason: r.web.sim === 'absent' ? 'sim-missing' : undefined, web_sim: r.web.sim || '', registration: 'idle',
						signal: {}, access_tech: [], via_at: true };
					c.at = Date.now(); c.failed = false;
					self.saveReadings();
				} else c.failed = true;
				if (self.lastStatus) self.apply(self.lastStatus, self.lastDevices, self.lastUplink);
			}, function() {
				if (self.readings[id] !== c) return;
				c.failed = true;
				if (self.lastStatus) self.apply(self.lastStatus, self.lastDevices, self.lastUplink);
			}).finally(function() { c.busy = false; });
		}
		if (c.mm && Date.now() - c.at > 600000) c.mm = null;
		return c.mm ? Object.assign({}, d, { modemmanager: c.mm, reading_fresh: true,
			reading_stale: Date.now() - c.at > (ctl === 'qmodem' ? 150000 : 90000),
			reading_age: Math.round((Date.now() - c.at) / 1000) }) :
			Object.assign({}, d, { reading_failed: !!c.failed, reading_pending: !!c.busy });
	},

	/* the readings outlive a reload of the page (sessionStorage, like the page's own cache) */
	saveReadings: function() {
		var out = {};
		Object.keys(this.readings || {}).forEach(function(id) { var c = this.readings[id]; if (c.mm) out[id] = { mm: c.mm, at: c.at, generation: c.generation, owner: c.owner }; }, this);
		try { sessionStorage.setItem('mm:readings', JSON.stringify(out)); } catch (e) {}
	},

	loadReadings: function() {
		var self = this;
		this.readings = this.readings || {};
		try {
			var saved = JSON.parse(sessionStorage.getItem('mm:readings') || '{}');
			Object.keys(saved).forEach(function(id) {
				if (saved[id] && saved[id].mm && Date.now() - saved[id].at < 600000)
					self.readings[id] = { mm: saved[id].mm, at: saved[id].at, generation: saved[id].generation, owner: saved[id].owner };
			});
		} catch (e) {}
	},

	/* after «Сделать основным»: the order is saved at once, the traffic follows when the modem has internet — said as it
	 * happens, not promised (bench 2026-10-22, r82: the chosen modem went first while the traffic stayed elsewhere) */
	followMain: function(id, name, r) {
		var self = this, tries = 0;
		if (r && r.routes === 'failed') {
			mm.toast('main', _('The order is saved, but the routes are not moved: the MedveModem service could not move them. Try again; if it repeats, the log is needed.'), 'warn', 30000);
			return;
		}
		mm.toast('main', _('The order is saved: “%s” is first. Switching the traffic…').format(name), 'run', 0);
		(function look() {
			tries++;
			getUplink().then(function(u) {
				var c = (u && u.carrier) || {}, path = list(u && u.paths).filter(function(p) { return p.device_id === id; })[0] || {};
				if (c.device_id === id) { mm.toast('main', _('The traffic goes through “%s”.').format(name), 'ok', 8000); return; }
				if (tries < 7) { window.setTimeout(look, 3000); return; }
				var via = c.device_id && self.cards.get(c.device_id) ? self.cards.get(c.device_id).titleLink.textContent : words(c.name) || c.device || '';
				mm.toast('main', _('The order is saved, but the traffic still goes through “%s”').format(via) +
					((path.reach || {}).ok === false ? ': ' + _('“%s” has no internet, it stays a backup until the link comes.').format(name) :
						'. ' + _('It switches when “%s” passes the connection check.').format(name)), 'warn', 30000);
			}, function() { if (tries < 7) window.setTimeout(look, 3000); });
		})();
	},

	/* one verdict: is the router online, through what, is a backup ready (spec 25.9.10) */
	applyUplink: function(uplink, devices, connected, present) {
		var carrier = uplink && uplink.carrier, paths = list(uplink && uplink.paths), byId = {}, cards = this.cards;
		devices.forEach(function(d) { byId[d.device_id] = d; });
		/* a modem by the name its card shows (the owner, 2026-10-20: «Трафик идёт через «модем»» for an E3372) */
		var titled = function(id, fallback) {
			var c = id && cards && cards.get(id), t = c && c.titleLink && c.titleLink.textContent;
			return t && t !== '—' ? t : fallback;
		};
		var tone = !present ? 'warn' : connected ? 'ok' : 'warn';
		if (!uplink || typeof uplink.internet !== 'string') {
			text(this.internet, _('The internet availability is not checked yet.'));
			this.backup.hidden = true;
		} else {
			var path = carrier && paths.filter(function(p) { return p.device === carrier.device; })[0];
			/* the router's own uplink by its interface: «через проводной канал «wan»» (field request 2026-10-23) */
			var who = !carrier ? '' : carrier.device_id ? words(carrier.name) || titled(carrier.device_id,
				words(((byId[carrier.device_id] || {}).modemmanager || {}).model) || _('modem')) :
				wireName(path, carrier.device);
			/* as the mockup says it (the owner, 2026-10-17): «Интернет работает», then through whom and who backs it up */
			var heads = { available: _('The internet works'), limited: _('Limited access'), unavailable: _('No internet access'), checking: _('Checking the connection') };
			/* an operator's whitelist with a tunnel over it (podkop's VLESS on an allowed address; the owner 2026-10-23):
			 * the carrier is limited, the rest goes through the tunnel, which answers */
			var tun = uplink.internet === 'limited' && uplink.tunnel && uplink.tunnel.ok === true ? uplink.tunnel : null;
			text(this.summary, tun ? _('The internet works through the tunnel') :
				(heads[uplink.internet] || _('State unknown')) + (who && uplink.internet !== 'available' ? ' (' + who + ')' : ''));
			var simple = this.level === 'normal', r0 = path && path.reach;
			text(this.internet, path ? (simple ? [who ? _('Now through “%s”').format(who) : '', !r0 || typeof r0.ok !== 'boolean' ? _('the connection check has not run yet') :
				r0.limited ? _('not all sites open') : r0.ok ? (typeof r0.latency_ms === 'number' ? _('response %d ms').format(r0.latency_ms) : _('the internet answers')) :
				_('the internet does not answer')] : [who ? _('The traffic goes through “%s”').format(who) : '', checkWords(path.reach) || _('the connection check has not run yet')]).filter(Boolean).join(' · ') :
				carrier ? (who ? (simple ? _('Now through “%s”') : _('The traffic goes through “%s”')).format(who) : '') : _('There is no default route.'));
			if (tun) text(this.internet, [who ? _('Directly through “%s” only the operator\'s whitelist opens').format(who) : _('Directly only the operator\'s whitelist opens'),
				_('the rest goes through the tunnel %s').format([words(tun.via), words(tun.outbound)].filter(Boolean).join(', ') || '—') +
				(typeof tun.delay_ms === 'number' ? ', ' + _('response %d ms').format(tun.delay_ms) : '')].join(' · '));
			var modems = paths.filter(function(p) { return p.device_id; });
			/* a frozen or deactivated modem is down on purpose: not a backup that is «not ready» */
			var parked = function(p) { var d = byId[p.device_id] || {}; return !!(d.frozen || (d.connection || {}).deactivated); };
			var spare = modems.filter(function(p) { return (p.role === 'backup_ready' || p.role === 'backup_not_ready') && !parked(p); });
			var self2 = this;
			var ready = spare.filter(function(p) { return p.role === 'backup_ready'; }), notReady = spare.filter(function(p) { return p.role !== 'backup_ready'; });
			this.backup.hidden = !spare.length;
			/* the backups in the priority order, names only: the checks and the states are in the channels table (the owner,
			 * 2026-10-26: «просто перечислить по приоритету: Резервы: 1. FM350 …») */
			var order = function(p) { var n = (self2.ranked || []).indexOf(p.device_id); return n < 0 ? 1e6 : n; };
			this.backupLong = _('Backups: %s').format(spare.slice().sort(function(a, b) { return order(a) - order(b); }).map(function(p, i) {
				var n = _('“%s”').format(words(p.name) || titled(p.device_id, words(((byId[p.device_id] || {}).modemmanager || {}).model) || p.device));
				return (i + 1) + '. ' + (p.role === 'backup_ready' ? n : _('%s (not ready)').format(n)); }).join(', ')) + '.';
			this.backupShort = [ready.length ? (ready.length === 1 ? _('The backup channel is ready') : _('Backup channels ready: %d').format(ready.length)) : '',
				notReady.length ? (ready.length ? _('not ready: %d') : _('Backup channel not ready: %d')).format(notReady.length) : ''].filter(Boolean).join(', ') + '.';
			if (notReady.length === 1 && !ready.length) this.backupShort = _('The backup channel is not ready.');
			this.backupMore.hidden = !simple;
			text(this.backupText, simple && !this.backupOpen ? this.backupShort : this.backupLong);
			/* the modem the operator put first, present and up, but not carrying the traffic yet: amber, said by name */
			var first = (this.ranked || [])[0], fd = first && byId[first];
			var firstIdle = fd && fd.present === true && !fd.frozen && !(fd.connection || {}).deactivated && carrier && carrier.device_id &&
				carrier.device_id !== first && uplink.internet === 'available';
			tone = uplink.internet === 'available' || tun ? (uplink.dns_ok === false || firstIdle ? 'warn' : 'ok') : uplink.internet === 'unavailable' ? 'err' :
				uplink.internet === 'limited' ? 'warn' : 'idle';
			if (firstIdle) text(this.internet, this.internet.textContent + ' · ' + _('the first in the priority “%s” carries no traffic yet').format(titled(first, words(fd.name) || words(fd.product) || _('modem'))));
		}
		/* failover (spec 20.8): which channel it steered traffic away from, by its modem's name when it is a modem;
		 * a manual channel waiting and a revived one kept last are said apart (one line per device, IPv4 and IPv6 alike) */
		var fo = uplink && uplink.failover, routes = fo && fo.active ? list(fo.routes).filter(function(r) { return r && r.steered_away; }) : [];
		var devs = function(f) { return routes.filter(f).map(function(r) { return r.device; }).filter(function(d, i, a) { return a.indexOf(d) === i; }); };
		var idOf = function(dev) { var p = paths.filter(function(x) { return x.device === dev && x.device_id; })[0]; return p && p.device_id; };
		var resting = function(dev) { var d = byId[idOf(dev)] || {}; return !!(d.frozen || (d.connection || {}).deactivated); };
		/* steered away from a modem the operator froze or took down: that is the operator's doing, not a failure */
		var away = devs(function(r) { return !r.manual && !r.demoted && !r.quota && !resting(r.device); }), kept = devs(function(r) { return r.demoted && !r.manual; });
		var spent = devs(function(r) { return r.quota && !r.manual; });
		var name = function(dev) {
			var p = paths.filter(function(x) { return x.device === dev && x.device_id; })[0];
			var q = paths.filter(function(x) { return x.device === dev && words(x.interface); })[0];
			return p ? words(p.name) || titled(p.device_id, words(((byId[p.device_id] || {}).modemmanager || {}).model) || dev) :
				wireName(q, dev);
		};
		var said = [];
		if (away.length) said.push(_('no internet through %s: the traffic goes through another channel.').format(away.map(name).join(', ')));
		if (kept.length) said.push(_('%s answers again, but is kept last by the setting.').format(kept.map(name).join(', ')));
		if (spent.length) said.push(_('%s used up the SIM\'s traffic limit: the channel is kept as a backup.').format(spent.map(name).join(', ')));
		this.failover.hidden = !(fo && fo.enabled && (said.length || !fo.active));
		text(this.foText, !fo || !fo.enabled ? '' : !fo.active ? _('Channel switching does not work: %s.').format(/mwan3/.test(fo.why || '') ? _('mwan3 manages the routes') : words(fo.why) || _('the reason is unknown')) :
			said.length ? _('Switching: %s').format(said.join(' ')) : '');
		this.foBack.hidden = !(fo && fo.active && kept.length);
		var dead = paths.filter(function(p) { return p.role === 'no_internet'; })[0], alive = paths.filter(function(p) { return p.role === 'backup_ready'; })[0];
		var wl = (uplink && uplink.wlmode) || {}, wlWhat = [];
		if (wl.lan) wlWhat.push(_('the whole LAN'));
		if (wl.router) wlWhat.push(_('the router'));
		this.wlLine.hidden = !wl.on;
		text(this.wlLine, wl.on ? _('Whitelist mode on: the tunnel through “%s”').format(wl.label || wl.server || '—') +
			(typeof wl.delay_ms === 'number' ? ', ' + _('%d ms').format(wl.delay_ms) : '') +
			(wlWhat.length ? '; ' + _('through it: %s').format(wlWhat.join(', ')) : '') + '. ' +
			(wl.manual ? _('Turned on by hand.') : _('Turned on by itself under the operator\'s whitelist.')) : '');
		/* under a whitelist a DPI bypass helps nothing and often breaks the mobile channel (the owner's guide, 2026-11-08) */
		var dpi = (uplink && (uplink.internet === 'limited' || wl.on) ? list(uplink.services) : []).filter(function(v) {
			return v && v.installed !== false && ['zapret', 'zapret2', 'byedpi', 'youtubeUnblock'].indexOf(v.name) >= 0; }).map(function(v) { return v.name; });
		this.dpiLine.hidden = !dpi.length;
		text(this.dpiLine, dpi.length ? _('%s is installed: under the operator\'s whitelist it does not help and may break the tunnel; on mobile internet it is better stopped or removed.').format(dpi.join(', ')) : '');
		this.foOff.hidden = !(fo && fo.enabled === false && dead && alive);
		if (!this.foOff.hidden)
			text(this.foOffText, _('No internet through %s, while %s works: channel switching is off, so the traffic stays on the dead channel.')
				.format(words(dead.name) || name(dead.device), words(alive.name) || name(alive.device)));
		/* a backup moved aside while the internet works is said in the line, not by an «!» (the owner, 2026-10-30: «почему
		 * восклицательный знак у сетевой таблицы»): the first in the priority idle or the DNS down still turn it amber */
		/* the tone only: the fold (is-short) stays as the operator left it (the owner, 2026-10-26: it opened by itself) */
		this.verdict.className = 'mm-verdict mm-summary is-' + tone + (this.verdict.classList.contains('is-short') ? ' is-short' : '');
		text(this.mark, tone === 'ok' ? '✓' : tone === 'err' ? '×' : tone === 'idle' ? '…' : '!');
		this.applyChans(paths, byId, routes, fo, titled, uplink && uplink.internet === 'limited' && uplink.tunnel && uplink.tunnel.ok === true);
	},

	applyChans: function(paths, byId, routes, fo, titled, tunnelled) {
		/* the setting and the kernel's: one number when they agree */
		function metricText(m, k) {
			var a = typeof m === 'number' ? String(m) : '—', b = typeof k === 'number' ? String(k) : '—';
			return a === b ? a : a + ' / ' + b;
		}
		/* every modem, a down one too, after the channels that have a route (the owner, 2026-10-30: «только 4 модема, хотя в
		 * системе 6») */
		var self = this, rows = (paths || []).filter(function(p) { return p.device || p.device_id; });
		/* each modem's role, for the plain USB tree too */
		this.roleOf = {};
		var roleOf = function(p) { return p.role === 'limited' && tunnelled ? 'limited_tunnel' : p.role; };
		rows.forEach(function(p) { if (p.device_id) self.roleOf[p.device_id] = roleOf(p); });
		/* switched off in Settings → Internet and backup (the owner, 2026-10-26), per browser */
		var on = true;
		try { on = localStorage.getItem('mm:chans') !== '0'; } catch (e) {}
		this.chans.hidden = this.level !== 'expert' || !rows.length || !on;
		if (this.chans.hidden) return;
		/* by the configured metric, a wired WAN among the modems where its metric puts it (field request 2026-10-23) */
		var rank = function(p) { var n = (self.ranked || []).indexOf(p.device_id);
			return typeof p.metric === 'number' ? p.metric : n < 0 ? 1e6 + (p.kernel_metric || 0) : n; };
		var ROLE = CHAN_ROLE;
		var all = fo && fo.routes ? fo.routes : [];
		rows.sort(function(a, b) { return (a.device ? 0 : 1) - (b.device ? 0 : 1) || rank(a) - rank(b); });
		this.chanBody.replaceChildren.apply(this.chanBody, rows.map(function(p, i) {
			var d = byId[p.device_id] || {}, r = all.filter(function(x) { return x && x.device === p.device; })[0] || {};
			var why = d.frozen ? _('management paused') : (d.connection || {}).deactivated ? _('deactivated') :
				!p.device ? (d.present === false ? _('disconnected from USB') : _('no session: no network device')) :
				r.steered_away && r.quota ? _('SIM limit used up: moved aside') : r.steered_away && r.manual ? _('a manual channel waits') :
				r.demoted ? _('answers again, kept last by the setting') :
				r.steered_away && p.reach && p.reach.limited ? _('whitelist only: moved aside') :
				r.steered_away ? _('no internet: moved aside by switching') :
				typeof p.metric === 'number' && typeof p.kernel_metric === 'number' && p.metric !== p.kernel_metric ? _('the kernel has not moved the route yet') :
				p.role !== 'carrying' && typeof p.kernel_metric !== 'number' ? _('no default route') : '';
			/* the modems' order moved by one place; the wired channels keep theirs in the common order (as «Make main») */
			var order = (self.ranked || []).slice(), at = order.indexOf(p.device_id);
			var move = function(by) {
				return mm.busy(function() {
					var o = order.slice(), j = at + by, t = o[at];
					o[at] = o[j]; o[j] = t;
					return setPriority(o).then(function(res) {
						if (res && res.result !== 'done') window.alert(_('The order is not saved: %s').format(PAUSE_WHY[res.reason] || res.reason || res.result));
					}, function() { window.alert(_('The MedveModem service does not answer.')); }).finally(function() { return self.update(); });
				});
			};
			var moves = at < 0 ? '—' : E('span', { 'class': 'mm-chan-move' }, [
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-chan-up', title: _('Higher in the priority'), 'aria-label': _('Higher in the priority'),
					disabled: at === 0 ? '' : null, click: at > 0 ? move(-1) : null }, '↑'),
				E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-chan-down', title: _('Lower in the priority'), 'aria-label': _('Lower in the priority'),
					disabled: at === order.length - 1 ? '' : null, click: at < order.length - 1 ? move(1) : null }, '↓')]);
			/* the metric typed here, for every channel with an interface, the wire too (the owner, 2026-10-30): the
			 * engine writes it and moves the live route, no network reload (as the order above) */
			var editKey = p.device_id ? ((d.connection || {}).interface ? p.device_id : null) : p.interface ? 'iface:' + p.interface : null;
			var editMetric = mm.busy(function() {
				var name = p.device_id ? words(p.name) || p.device_id : wireName(p);
				var v = window.prompt(_('Metric of “%s”: 0–9999, the lower one carries the traffic first').format(name),
					typeof p.metric === 'number' ? String(p.metric) : '');
				if (v === null) return;
				v = String(v).trim();
				if (!/^[0-9]{1,4}$/.test(v)) { window.alert(_('A metric is a whole number from 0 to 9999.')); return; }
				var set = {};
				set[editKey] = parseInt(v, 10);
				return setPriority([editKey], set).then(function(res) {
					if (res && res.result !== 'done') window.alert(_('The metric is not saved: %s').format(PAUSE_WHY[res.reason] || res.reason || res.result));
				}, function() { window.alert(_('The MedveModem service does not answer.')); }).finally(function() { return self.update(); });
			});
			/* the latency only; when it was measured is said once under the table (the owner, 2026-11-03) */
			var reach = p.reach && typeof p.reach.ok === 'boolean' ? (p.reach.ok ? (p.reach.latency_ms != null ? _('%d ms').format(p.reach.latency_ms) : _('answers')) : _('no answer')) : '—';
			var cells = [E('td', { 'class': 'num' }, p.device_id ? String(i + 1) : '—'),
				E('td', {}, p.device_id ? words(p.name) || titled(p.device_id, words(d.product) || p.device) :
					wireName(p)),
				E('td', { 'class': 'mm-mono' }, p.device || '—'),
				E('td', {}, [ROLE[roleOf(p)] || words(p.role) || '—', why ? E('div', { 'class': 'mm-faint mm-chan-why' }, why) : '']),
				E('td', { 'class': 'num' }, editKey ? E('button', { type: 'button', 'class': 'mm-btn is-quiet mm-chan-metric',
					title: _('Change the metric'), click: editMetric }, [metricText(p.metric, p.kernel_metric), ' ✎']) :
					metricText(p.metric, p.kernel_metric)),
				E('td', { 'class': 'num' }, typeof p.today_bytes === 'number' ? bytes(p.today_bytes) : '—'),
				E('td', { 'class': 'num mm-chan-peak' }, peakText(p.peak)),
				E('td', {}, reach), E('td', {}, moves)];
			cells.forEach(function(c, k) { c.setAttribute('data-label', self.chanHeads[k]); });
			return E('tr', { 'class': p.role === 'carrying' ? 'is-carrying' : '' }, cells);
		}));
		var counted = rows.filter(function(p) { return typeof p.today_bytes === 'number'; });
		var from = counted.map(function(p) { return p.today_from; }).filter(function(v) { return typeof v === 'number'; });
		var since = from.length ? new Date(Math.min.apply(null, from) * 1000).toLocaleTimeString(LANG, { hour: '2-digit', minute: '2-digit' }) : '';
		text(this.chanTotal, counted.length ? (since ? _('Total today: %s, counted since %s.').format(bytes(counted.reduce(function(a, p) { return a + p.today_bytes; }, 0)), since) :
			_('Total today: %s.').format(bytes(counted.reduce(function(a, p) { return a + p.today_bytes; }, 0)))) : '');
		var ages = rows.map(function(p) { return p.reach && typeof p.reach.age_s === 'number' ? p.reach.age_s : null; }).filter(function(a) { return a !== null; });
		if (ages.length && ago(Math.min.apply(null, ages)))
			this.chanTotal.textContent = (this.chanTotal.textContent ? this.chanTotal.textContent + ' ' : '') +
				_('Connection checked %s.').format(ago(Math.min.apply(null, ages)));
		this.chanTotal.hidden = !this.chanTotal.textContent;
	},

	/* «Порты и устройства»: shown at the Expert level when switched on; asked every 5 s while open, once a minute folded,
	 * never on a hidden tab (docs/PORTS-AND-DEVICES.md) */
	/* «Все SMS»: shown at Detailed; read when open, at most every 30 s, the modems the engine reads SMS of */
	feedRefresh: function(now) {
		var self = this, show = this.level === 'expert';
		this.feedCard.hidden = !show;
		this.feedBody.hidden = !show || !this.feedOpen;
		text(this.feedFold, this.feedOpen ? _('Collapse') : _('Expand'));
		if (!show || !this.feedOpen || document.hidden || this.feedPending) return;
		if (!now && this.feedAt && Date.now() - this.feedAt < 30000) return;
		var devs = (this.lastDevices || []).filter(function(d) { return d && d.present === true && !d.frozen && d.capabilities &&
			d.capabilities.sms && d.capabilities.sms.ok === true; });
		this.feedAt = Date.now();
		this.feedPending = Promise.all(devs.map(function(d) {
			return smsPeek(d.device_id, false, true).then(function(r) { return { d: d, r: r }; }, function() { return { d: d, r: null }; });
		})).then(function(all) {
			var items = [], failed = 0;
			all.forEach(function(x) {
				var c = self.cards && self.cards.get(x.d.device_id), name = c && c.titleLink ? c.titleLink.textContent : words(x.d.model) || x.d.device_id;
				if (!x.r || typeof x.r.refused === 'string' || !Array.isArray(x.r.messages)) { failed++; return; }
				x.r.messages.forEach(function(m) { items.push({ m: m, id: x.d.device_id, modem: name }); });
			});
			self.feedItems = items;
			self.feedFailed = failed;
			self.feedModems = devs.length;
			self.feedRender();
		}).catch(function() {}).then(function() { self.feedPending = null; });
	},

	feedRender: function() {
		var self = this, q = String(this.feedSearch.value || '').trim().toLowerCase(), items = this.feedItems || [];
		var shown = items.filter(function(x) {
			return !q || [x.m.number, x.m.text, x.modem].some(function(v) { return String(v || '').toLowerCase().indexOf(q) >= 0; });
		}).sort(function(a, b) { return (b.m.epoch || 0) - (a.m.epoch || 0); }).slice(0, 300);
		text(this.feedSum, this.feedItems ? _('messages: %d · modems: %d').format(items.length, this.feedModems || 0) : '');
		this.feedNote.hidden = !(q && !shown.length) && !this.feedFailed;
		text(this.feedNote, q && !shown.length ? _('Nothing found for “%s”.').format(q) :
			this.feedFailed ? _('The messages of some modems are not read yet (%d): they come in a moment.').format(this.feedFailed) : '');
		var page = L.url('admin/modem/medvemodem/modem');
		this.feedList.replaceChildren.apply(this.feedList, shown.map(function(x) {
			var m = x.m, out = m.type === 'submit', op = !out && mm.smsOperator(m.number), code = out || m.binary ? '' : mm.smsCode(m.text);
			var t = typeof m.epoch === 'number' ? new Date(m.epoch * 1000) : null;
			var when = t ? t.toLocaleString(mm.LANG, { day: '2-digit', month: '2-digit', hour: '2-digit', minute: '2-digit' }) : '';
			return E('div', { 'class': 'mm-feed-msg' + (out ? ' is-out' : '') + (op ? ' is-op' : ''), 'data-device-id': x.id }, [
				E('div', { 'class': 'mm-feed-head' }, [E('b', { 'class': 'mm-mono' }, (out ? '→ ' : '') + (words(m.number) || '—')),
					op ? E('span', { 'class': 'mm-feed-op' }, _('operator')) : '',
					E('span', { 'class': 'mm-faint' }, [when, x.modem].filter(Boolean).join(' · ')),
					E('a', { 'class': 'mm-link mm-feed-open', href: page + '#sms:' + encodeURIComponent(x.id) }, _('Open'))]),
				E('div', { 'class': 'mm-sms-body' }, m.binary ? _('[binary message, no text]') : mm.smsLinked(m.text)),
				code ? E('button', { type: 'button', 'class': 'mm-link mm-sms-code', click: function() {
					mm.copyText(code).then(function() { mm.toast('feed-code', _('The code %s is copied.').format(code), 'ok', 4000); },
						function() { mm.toast('feed-code', _('The browser did not let copy: the code %s.').format(code), 'warn', 8000); }); } },
					_('Code %s').format(code)) : '']);
		}));
	},

	usbSwitch: function(on) {
		this.usbOn = on;
		try { localStorage.setItem('mm:usbtree', on ? '1' : '0'); } catch (e) {}
		this.usbRefresh(true);
	},

	usbRefresh: function(now) {
		/* both levels (the owner, 2026-10-26): «Базовый» draws it in plain words */
		var self = this, show = this.usbOn;
		this.usbShow.hidden = this.usbOn;
		this.usbCard.classList.toggle('is-plain', this.level !== 'expert');
		this.usbCard.hidden = !show;
		this.usbBody.hidden = !show || !this.usbOpen;
		text(this.usbFold, this.usbOpen ? _('Collapse') : _('Expand'));
		if (!show || document.hidden || this.usbPending) return;
		if (!now && !this.usbOpen && this.usbAt && Date.now() - this.usbAt < 60000) return;
		this.usbAt = Date.now();
		this.usbPending = getUsbTree().then(function(t) { self.usbApply(t); })
			.catch(function() { text(self.usbSum, _('the service did not answer')); })
			.then(function() { self.usbPending = null; });
	},

	usbApply: function(t) {
		var self = this, nodes = {}, absent = {}, all = (t && t.nodes) || [], s = (t && t.summary) || {};
		this.usbLast = t;
		all.forEach(function(n) { nodes[n.path] = n; });
		((t && t.absent) || []).forEach(function(a) { absent[a.path] = a; });
		text(this.usbSum, [N_(s.connected, '%d modem connected', '%d modems connected').format(s.connected),
			s.disconnected ? N_(s.disconnected, '%d disconnected', '%d disconnected').format(s.disconnected) : '',
			s.speed_mbps && this.level === 'expert' ? 'USB ' + usbSpeed(s.speed_mbps) : ''].filter(Boolean).join(' · '));
		if (this.usbBody.hidden) return;
		if (this.level !== 'expert') return this.usbPlain(t);
		/* the tree: controllers at the top, each port in order, a device gone drawn dashed in its port */
		var item = function(n, gone) {
			var last = gone ? (n.last || {}) : n, sub = [], cls = 'mm-usb-node', chips = null;
			if (gone) {
				cls += ' is-gone';
				sub.push(_('Disconnected %s').format(agoWords(Math.max(0, (t.at || 0) - (n.disconnected_at || 0))) || '') + (usbName(n) ? ' · ' + _('was: %s').format(usbName(n)) : '') +
					(last.control ? ' · ' + (USB_CONTROL[last.control] || last.control) : '') + (last.speed_mbps ? ' · USB ' + usbSpeed(last.speed_mbps) : ''));
			} else if (n.kind === 'controller') {
				sub.push(['USB', (n.controller || {}).driver || (n.controller || {}).name, _('bus %s').format(n.bus), usbSpeed(n.speed_mbps)].filter(Boolean).join(' · '));
			} else if (n.modem) {
				cls += ' is-modem';
				var ats = (n.roles || []).filter(function(r) { return r.role === 'at'; }).length;
				var nets = [].concat.apply([], (n.interfaces || []).map(function(i) { return i.nodes || []; })).filter(function(x) { return !/^tty|^cdc-wdm/.test(x); });
				var drivers = (n.interfaces || []).map(function(i) { return i.driver; }).filter(function(d, i, a) { return d && a.indexOf(d) === i && d !== 'option'; });
				/* the modem's facts as chips (the owner, 2026-10-25: as in the mock-up): who runs it first, then the wire */
				chips = [[USB_CONTROL[n.control] || n.control, 'is-owner'], [n.protocol, ''], [drivers.join(', '), 'is-mono'], [n.path, 'is-mono'],
					[nets.join(', '), 'is-mono'], [ats ? 'AT ×' + ats : '', ''], [usbSpeed(n.speed_mbps), n.downgraded ? 'is-warn' : '']];
			} else {
				cls += n.kind === 'hub' ? ' is-hub' : ' is-grey';
				sub.push([n.kind === 'hub' ? (n.ports ? _('ports: %d').format(n.ports.length) : '') : USB_KIND[n.class_kind] || _('USB device'),
					n.path, usbSpeed(n.speed_mbps)].filter(Boolean).join(' · '));
			}
			var head = gone ? _('Port %s').format(n.label ? n.label + ' · ' + n.path : n.path) : n.kind === 'controller' ? _('USB controller %s').format(n.bus) : n.kind === 'hub' ? _('Hub %s').format(words(n.product) || n.path) :
				(n.label ? n.label + ': ' : '') + usbName(n);
			var warn = !gone && n.downgraded ? E('span', { 'class': 'mm-usb-warn' }, _('USB 3 at 480 Mbit/s')) :
				!gone && n.bounce && n.bounce.state !== 'stable' ? E('span', { 'class': 'mm-usb-warn' }, n.bounce.state === 'unstable' ? _('unstable') : _('USB errors')) : '';
			/* a glyph by kind: the controller's trident, a hub's fan-out, a modem's antenna, any other device a plug */
			var glyph = gone ? '⊘' : n.kind === 'controller' ? '⏚' : n.kind === 'hub' ? '⑂' : n.modem ? '📶' : '🔌';
			var line = chips ? E('span', { 'class': 'mm-usb-chips' }, chips.filter(function(c) { return c[0]; }).map(function(c) {
				return E('span', { 'class': 'mm-usb-chip ' + c[1] }, c[0]); })) : E('span', { 'class': 'mm-faint' }, sub.join(''));
			var li = E('li', { role: 'treeitem', 'class': cls, 'data-path': n.path, 'aria-selected': String(self.usbSel === n.path) }, [
				E('button', { type: 'button', 'class': 'mm-usb-pick', click: function() { self.usbSel = n.path; self.usbApply(self.usbLast); } },
					[E('span', { 'class': 'mm-usb-glyph', 'aria-hidden': 'true' }, glyph), E('span', { 'class': 'mm-usb-text' },
						[E('strong', {}, head), ' ', warn, E('br'), line])])]);
			if (!gone && n.ports && n.ports.length) {
				var ul = E('ul', { role: 'group' });
				n.ports.forEach(function(p) {
					var kid = p.child && nodes[p.child], away = !kid && Object.keys(absent).filter(function(a) { return absent[a] && absent[a].path === (n.kind === 'controller' ? n.bus + '-' + p.n : n.path + '.' + p.n); })[0];
					if (kid) ul.appendChild(item(kid, false));
					else if (away) { ul.appendChild(item(absent[away], true)); delete absent[away]; }
					else ul.appendChild(E('li', { role: 'treeitem', 'class': 'mm-usb-node is-free' }, E('span', { 'class': 'mm-usb-free' }, _('Port %s — free').format(p.n) +
						(p.over_current ? ' · ' + _('over-current protection: %s').format(p.over_current) : ''))));
				});
				li.appendChild(ul);
			}
			return li;
		};
		var tops = all.filter(function(n) { return !n.parent || !nodes[n.parent]; });
		var list = tops.map(function(n) { return item(n, false); });
		Object.keys(absent).forEach(function(a) { if (absent[a]) list.push(item(absent[a], true)); });
		this.usbTree.replaceChildren.apply(this.usbTree, list.length ? list : [E('li', { 'class': 'mm-faint' }, _('No USB devices found'))]);
		/* the selected node's details */
		var sel = nodes[this.usbSel] || null, gone = !sel && ((t.absent || []).filter(function(a) { return a.path === self.usbSel; })[0] || null);
		var rows = [], row = function(k, v) { if (v) rows.push(E('dt', {}, k), E('dd', {}, v)); };
		if (sel) {
			row(_('Path'), sel.path + (sel.label ? ' (' + sel.label + ')' : ''));
			row(_('Device'), [sel.vidpid, words(sel.manufacturer), words(sel.product)].filter(Boolean).join(' · '));
			row(_('Type'), sel.kind === 'controller' ? _('controller') : sel.kind === 'hub' ? _('hub') : sel.modem ? _('modem') : USB_KIND[sel.class_kind]);
			row(_('USB speed'), usbSpeed(sel.speed_mbps) + (sel.downgraded ? ' — ' + _('a USB 3 device works as USB 2: a cable, a socket or a hub without USB 3') : ''));
			row(_('USB version'), words(sel.version));
			/* the operator's word, Expert only (the owner, 2026-11-09): Auto / Modem / Not a modem, by VID:PID */
			if (this.level === 'expert' && sel.kind !== 'controller' && sel.kind !== 'hub' && /^[0-9a-f]{4}:[0-9a-f]{4}$/.test(sel.vidpid || '')) {
				var uc = this.usbClassNow || {}, now = (uc.modem || []).indexOf(sel.vidpid) >= 0 ? 'modem' : (uc.not_modem || []).indexOf(sel.vidpid) >= 0 ? 'not_modem' : 'auto';
				var pick = E('select', { 'class': 'mm-input mm-usb-class', 'aria-label': _('Is it a modem?'), change: function(ev) { self.usbClass(sel.vidpid, ev.target.value); } },
					[['auto', _('Auto (by the rules)')], ['modem', _('Modem')], ['not_modem', _('Not a modem')]].map(function(o) {
						return E('option', { value: o[0] }, o[1]); }));
				pick.value = now;	/* a property: an attribute on every option would pick the last one */
				rows.push(E('dt', {}, _('Is it a modem?')), E('dd', {}, pick));
			}
			if (sel.modem) {
				row(_('Management'), USB_CONTROL[sel.control] || sel.control);
				row(_('Protocol'), sel.protocol);
				row(_('Ports'), (sel.roles || []).map(function(r) { return r.node + ' — ' + (r.role === 'at' ? 'AT' : r.role || _('port')); }).join(', '));
			}
			row(_('Interfaces'), (sel.interfaces || []).map(function(i) {
				return ('0' + i.n.toString(16)).slice(-2) + ' ' + (i.driver || _('no driver')) + (i.nodes && i.nodes.length ? ' → ' + i.nodes.join(', ') : ''); }).join('; '));
			/* what the configuration asks for, not what the port gives */
			row(_('Power'), typeof sel.max_power_ma === 'number' ? _('asks for up to %d mA').format(sel.max_power_ma) : '');
			row(_('Power saving'), sel.runtime);
			if (sel.kind !== 'controller') row(_('Bounce'), usbBounce(sel.bounce));
			if (sel.history) row(_('History'), [_('first seen %s').format(usbWhen(sel.history.first_seen))].concat((sel.history.speed_changes || []).map(function(c) {
				return usbWhen(c.at) + ': ' + usbSpeed(c.from) + ' → ' + usbSpeed(c.to); })).join(' · '));
		} else if (gone) {
			row(_('Port'), gone.path);
			row(_('Was'), usbName(gone) + ((gone.last || {}).vidpid ? ' · ' + gone.last.vidpid : ''));
			row(_('Disconnected'), usbWhen(gone.disconnected_at) + ' (' + agoWords(Math.max(0, (t.at || 0) - (gone.disconnected_at || 0))) + ')');
			row(_('Management was'), USB_CONTROL[(gone.last || {}).control] || (gone.last || {}).control);
			row(_('Bounce'), usbBounce(gone.bounce));
		}
		/* «по запросу»: kept across the 5 s refresh for the device it was asked for */
		var moreOf = function(d) {
				var extra = [], add = function(k, v) { if (v) extra.push(E('dt', {}, k), E('dd', {}, v)); };
				add(_('Serial number'), d.serial ? (privacy() ? _('hidden') : d.serial) : _('none'));
				add(_('Configuration'), d.configuration ? (d.configurations ? _('%s of %s').format(d.configuration, d.configurations) : d.configuration) : '');
				add(_('Power control'), d.power_control);
				Object.keys(d.net || {}).forEach(function(ifn) {
					var c = d.net[ifn];
					add(_('Counters %s').format(ifn), _('received %s, sent %s · errors %d · dropped %d').format(bytes(c.rx_bytes), bytes(c.tx_bytes),
						(c.rx_errors || 0) + (c.tx_errors || 0), (c.rx_dropped || 0) + (c.tx_dropped || 0)));
				});
				return extra;
		};
		var asked = sel && this.usbMore && this.usbMore.path === sel.path;
		if (asked) rows = rows.concat(moreOf(this.usbMore.d));
		var more = sel && sel.kind !== 'controller' && !asked ? E('button', { type: 'button', 'class': 'mm-btn is-quiet', click: mm.busy(function() {
			return getUsbDetail(sel.path).then(function(d) {
				self.usbMore = { path: sel.path, d: d };
				self.usbApply(self.usbLast);
			}).catch(function() { self.usbDetail.appendChild(E('p', { 'class': 'mm-faint' }, _('The device is already disconnected.'))); });
		}) }, _('Serial number and counters')) : '';
		this.usbDetail.replaceChildren.apply(this.usbDetail, rows.length ? [E('dl', { 'class': 'mm-kv' }, rows), more,
			E('p', { 'class': 'mm-faint' }, _('To compare with lsusb -t, install usbutils in Settings → Components. The speed is the USB link\'s speed, not traffic.'))] :
			[E('p', { 'class': 'mm-faint' }, _('Pick a device in the tree.'))]);
		/* the port's name: a port with or without a device in it, not a controller */
		var named = sel && sel.kind !== 'controller' ? sel : gone, nb = this.usbName;
		nb.box.hidden = !named;
		if (named && nb.path !== named.path) {
			nb.path = named.path;
			nb.input.value = named.label || '';
			text(nb.out, '');
		}
		/* the events, newest first */
		var ev = ((t && t.events) || []).slice(-12).reverse();
		this.usbEvents.replaceChildren.apply(this.usbEvents, ev.length ? ev.map(function(e) {
			return E('li', {}, usbWhen(e.t) + ' · ' + usbEventWords(e)); }) : [E('li', { 'class': 'mm-faint' }, _('No events yet'))]);
		this.usbNote.hidden = !t || t.kernel_log !== false;
		text(this.usbNote, _('The kernel log is unavailable: the bounce is counted by reconnects only.'));
	},

	/* «Базовый» (the owner, 2026-10-26): where the modems are plugged in, in plain words. The router's own USB ports are
	 * numbered in order across its controllers (a controller's number need not match the case); a hub and its ports; a
	 * port's name instead of its number when given. No drivers, buses, speeds or paths; a modem leads to its page */
	usbPlain: function(t) {
		var self = this, nodes = {}, absent = {}, all = (t && t.nodes) || [], rootNo = {}, k = 0;
		all.forEach(function(n) { nodes[n.path] = n; });
		((t && t.absent) || []).forEach(function(a) { absent[a.path] = a; });
		all.filter(function(n) { return n.kind === 'controller'; }).sort(function(a, b) { return a.bus - b.bus; }).forEach(function(c) {
			(c.ports || []).forEach(function(p) { rootNo[c.bus + '-' + p.n] = ++k; });
		});
		var label = function(path) { var n = nodes[path] || absent[path]; return n && words(n.label); };
		/* one port in words: the router's «USB port 2», a hub's «port 4», or the name it was given */
		var portWord = function(path) {
			var m = /^(\d+-\d+)((?:\.\d+)*)$/.exec(path || '');
			if (!m) return path || '';
			return label(path) || (m[2] ? _('port %s').format(m[2].split('.').pop()) : _('USB port %d').format(rootNo[m[1]] || 0));
		};
		/* the whole way: «the router → USB port 2 → hub → port 4» */
		var place = function(path) {
			var m = /^(\d+-\d+)((?:\.\d+)*)$/.exec(path || '');
			if (!m || !rootNo[m[1]]) return path || '';	/* a port gone with its hub: its path as it was */
			var way = [label(m[1]) || _('USB port %d').format(rootNo[m[1]] || 0)], at = m[1];
			m[2].split('.').filter(Boolean).forEach(function(seg) { at += '.' + seg; way.push(_('hub'), label(at) || _('port %s').format(seg)); });
			return way.join(' → ');
		};
		var free = function(list) {
			return E('li', { role: 'treeitem', 'class': 'mm-usb-node is-free' }, E('span', { 'class': 'mm-usb-free' }, list.length > 1 ?
				_('ports %s · free').format(list.join(', ')) : _('%s · free').format(list[0])));
		};
		var item = function(n, gone) {
			var last = gone ? (n.last || {}) : n, cls = 'mm-usb-node', head, sub = [portWord(n.path)], chips = [], warn = '';
			if (gone) {
				cls += ' is-gone';
				head = _('%s: disconnected %s').format(usbName(n), agoWords(Math.max(0, (t.at || 0) - (n.disconnected_at || 0))) || '');
			} else if (n.kind === 'hub') {
				cls += ' is-hub';
				head = _('USB hub');
				if (n.ports) sub.push(N_(n.ports.length, '%d port', '%d ports').format(n.ports.length));
			} else if (n.modem) {
				cls += ' is-modem';
				head = usbName(n);
				var role = (self.roleOf || {})[n.device_id];
				if (CHAN_ROLE[role]) chips.push(CHAN_ROLE[role]);
				sub = [_('the router → %s').format(place(n.path))];
			} else {
				cls += ' is-grey';
				head = usbName(n);
				sub.unshift(USB_KIND[n.class_kind] || _('USB device'));
			}
			/* a problem said with what to do about it */
			if (!gone && n.bounce && n.bounce.state === 'unstable')
				warn = n.bounce.hour && n.bounce.hour.reconnects ? N_(n.bounce.hour.reconnects, 'Disconnected %d time in an hour: check the cable or the hub\'s power',
					'Disconnected %d times in an hour: check the cable or the hub\'s power').format(n.bounce.hour.reconnects) : _('USB errors: check the cable or the hub\'s power');
			else if (!gone && n.downgraded) warn = _('Works slower than it can: another cable or a USB 3 port');
			var glyph = gone ? '⊘' : n.kind === 'hub' ? '⑂' : n.modem ? '📶' : '🔌';
			var body = [E('strong', {}, head), E('br'), E('span', { 'class': 'mm-faint' }, sub.join(' · '))];
			if (chips.length) body.push(E('span', { 'class': 'mm-usb-chips' }, chips.map(function(c) { return E('span', { 'class': 'mm-usb-chip is-owner' }, c); })));
			if (warn) body.push(E('span', { 'class': 'mm-usb-say' }, '⚠ ' + warn));
			var text_ = E('span', { 'class': 'mm-usb-text' }, body), g = E('span', { 'class': 'mm-usb-glyph', 'aria-hidden': 'true' }, glyph);
			var li = E('li', { role: 'treeitem', 'class': cls, 'data-path': n.path }, n.modem && n.device_id && !gone ?
				E('a', { 'class': 'mm-usb-pick', href: L.url('admin/modem/medvemodem/modem') + '#' + encodeURIComponent(n.device_id) }, [g, text_]) :
				E('div', { 'class': 'mm-usb-pick' }, [g, text_]));
			if (!gone && n.ports && n.ports.length) li.appendChild(ports(n));
			return li;
		};
		/* a node's ports in order, the free ones next to each other in one line */
		var ports = function(n) {
			var ul = E('ul', { role: 'group' }), idle = [];
			/* the router's free ports one by one by name, a hub's together: «ports 2, 3 · free» */
			var flush = function() {
				if (n.kind === 'controller') idle.forEach(function(w) { ul.appendChild(free([w])); });
				else if (idle.length) ul.appendChild(free(idle.length > 1 ? idle : [_('port %s').format(idle[0])]));
				idle = [];
			};
			(n.ports || []).forEach(function(p) {
				var path = n.kind === 'controller' ? n.bus + '-' + p.n : n.path + '.' + p.n, kid = p.child && nodes[p.child];
				if (kid) { flush(); ul.appendChild(item(kid, false)); }
				else if (absent[path]) { flush(); ul.appendChild(item(absent[path], true)); delete absent[path]; }
				else idle.push(n.kind === 'controller' ? _('USB port %d').format(rootNo[path] || p.n) : String(p.n));
			});
			flush();
			return ul;
		};
		/* the router on top, its ports from every controller together */
		var router = E('li', { role: 'treeitem', 'class': 'mm-usb-node is-router' }, [
			E('div', { 'class': 'mm-usb-pick' }, [E('span', { 'class': 'mm-usb-glyph', 'aria-hidden': 'true' }, '⌂'), E('span', { 'class': 'mm-usb-text' }, E('strong', {}, _('Router')))])]);
		var rootUl = E('ul', { role: 'group' });
		all.filter(function(n) { return n.kind === 'controller'; }).sort(function(a, b) { return a.bus - b.bus; }).forEach(function(c) {
			Array.prototype.slice.call(ports(c).childNodes).forEach(function(li) { rootUl.appendChild(li); });
		});
		Object.keys(absent).forEach(function(a) { rootUl.appendChild(item(absent[a], true)); });
		router.appendChild(rootUl);
		/* the audit (R11): the numbers are the controllers' order, not a proven map of the case; a name says it best */
		var unnamed = Object.keys(rootNo).some(function(p) { return !label(p); });
		var tree = all.length ? [router] : [E('li', { 'class': 'mm-faint' }, _('No USB devices found'))];
		if (all.length && unnamed) tree.push(E('li', { 'class': 'mm-faint mm-usb-hint' },
			_('The ports are numbered in the order of the router\'s controllers and may not match the case. A port can be given a name in the «Detailed» level.')));
		this.usbTree.replaceChildren.apply(this.usbTree, tree);
		var ev = ((t && t.events) || []).slice(-5).reverse();
		this.usbEvents.replaceChildren.apply(this.usbEvents, ev.length ? ev.map(function(e) {
			return E('li', {}, usbWhen(e.t) + ' · ' + usbEventWords(e, place)); }) : [E('li', { 'class': 'mm-faint' }, _('No events yet'))]);
		this.usbNote.hidden = true;
	},

	/* the name written as config usb_port by its path; an empty one removes the section. Printable, at most 32 characters */
	usbNameSave: function() {
		var self = this, nb = this.usbName, path = nb.path, name = nb.input.value.replace(/[\x00-\x1f\x7f]/g, '').trim().slice(0, 32);
		if (!path) return Promise.resolve();
		return uciGet('medvemodem', 'usb_port').then(function(r) {
			var have = Object.keys((r && r.values) || {}).filter(function(k) { return r.values[k].path === path; });
			var drop = (name ? have.slice(1) : have).map(function(k) { return uciDelete('medvemodem', k); });
			var put = !name ? null : have.length ? uciSet('medvemodem', have[0], { name: name }) : uciAdd('medvemodem', 'usb_port', { path: path, name: name });
			return Promise.all(drop.concat(put ? [put] : []));
		}).then(function() { return uciCommit('medvemodem'); }).then(function() {
			nb.input.value = name;
			text(nb.out, name ? _('Saved.') : _('The name is removed.'));
			self.usbRefresh(true);
		}, function(e) { text(nb.out, _('Not saved: %s.').format((e && e.message) || e)); });
	},

	/* the helper's last answer (the engine runs it every 10 min): known packages, programs on modem ports, scripts */
	neighboursShow: function(devices) {
		var self = this;
		if (devices) this.nbDevices = devices;
		return getNeighbours(false).then(function(n) {
			var rows = [], names = {}, owners = {};
			(self.nbDevices || []).forEach(function(d) {
				names[d.device_id] = d.name || d.model || d.device_id;
				owners[d.device_id] = (d.owner || {}).control || '';
			});
			list(n && n.packages).forEach(function(p) {
				var w = NEIGHBOUR[p.id] || [p.id, ''];
				rows.push(E('li', {}, [E('strong', {}, w[0]), w[1] ? ' — ' + w[1] : '',
					p.running ? ' · ' + _('running') : '', p.cron ? ' · ' + _('runs by cron') : '',
					E('br'), E('span', { 'class': 'mm-faint is-mono' }, list(p.files).join(' '))]));
			});
			list(n && n.holders).forEach(function(h) {
				/* QModem's tom_modem legitimately holds its own modem's AT node. */
				if (h.device_id && owners[h.device_id] === 'qmodem' && h.comm === 'tom_modem') return;
				rows.push(E('li', {}, _('%s (PID %d) holds %s').format(h.comm, h.pid, h.node) +
					(h.device_id ? ' · ' + _('modem “%s”: no AT command goes to that port while it is held').format(names[h.device_id] || h.device_id) : '')));
			});
			list(n && n.scripts).forEach(function(sc) {
				rows.push(E('li', {}, [sc.path === 'cron' ? _('A cron task touches modems') : _('Script %s touches modems').format(sc.path),
					' (' + list(sc.words).join(', ') + ')']));
			});
			self.nbList.replaceChildren.apply(self.nbList, rows);
			mmc.bannerShow(self.nb, rows.length > 0);
		}).catch(function() { self.nb.hidden = true; });
	},

	update: function() {
		var self = this;
		if (this.pending) return this.pending;
		/* a hidden tab asks nothing (the owner, 2026-10-19); the first picture is this tab's last reply, then the fresh one */
		/* and a hidden tab with the engine not answering asks nothing either (audit 2026-10-20: it kept polling) */
		if (document.hidden && (this.lastSuccess || this.failing)) return Promise.resolve();
		if (!this.lastSuccess && !this.cacheShown) {
			this.cacheShown = true;
			var c = cacheGet('mm:dash');
			if (c && c.status && Array.isArray(c.devices)) {
				try { this.apply(c.status, c.devices, c.uplink); text(this.updated, _('the data of the last visit, refreshing…')); } catch (e) { /* only a head start */ }
				this.lastSuccess = null;	/* nothing from the engine yet */
			}
		}
		this.refresh.disabled = true;
		this.root.setAttribute('aria-busy', 'true');
		/* an engine older than 0.14.0-r7 has no uplink method: the page still works without it */
		this.pending = Promise.all([getStatus(), getDevices(true), getUplink().catch(function() { return null; })]).then(function(replies) {
			var status = replies[0], devices = replies[1] && replies[1].devices;
			if (!status || typeof status.version !== 'string' || !Array.isArray(devices) ||
				devices.some(function(d) { return !d || typeof d.device_id !== 'string' || !d.device_id; }) ||
				new Set(devices.map(function(d) { return d.device_id; })).size !== devices.length)
				throw new Error('Unexpected engine response');
			self.apply(status, devices, replies[2]);
			self.usbRefresh(false);
			self.feedRefresh(false);
			/* the engine looks every 10 min: a minute between reads is plenty */
			if (!self.nbAt || Date.now() - self.nbAt > 60000) { self.nbAt = Date.now(); self.neighboursShow(devices); }
			else self.nbDevices = devices;
			self.failing = false;
			self.svcAt = 0;
			cacheSet('mm:dash', { status: status, devices: devices, uplink: replies[2] });
		}).catch(function() {
			self.error.hidden = false; self.root.classList.add('mm-stale'); self.error.className = 'mm-callout is-err mm-notice';
			text(self.error, self.lastSuccess ? _('Refresh error. The last received data is shown; the current state is unknown.') :
				_('The MedveModem service does not answer. Check that the medvemodem package is installed, the service runs and access is allowed.'));
			text(self.summary, _('The current state is unknown'));
			self.verdict.className = 'mm-verdict mm-summary is-warn' + (self.verdict.classList.contains('is-short') ? ' is-short' : ''); text(self.mark, '!');
			if (!self.lastSuccess) { text(self.counts, _('no data received yet')); text(self.updated, _('waiting for the service')); }
			/* the operator's switch turned it off (the owner, 2026-10-19): said so, with the way back. Asked at most every
			 * 30 s while it fails: the helper costs the router a dozen processes, not every 5 s (audit 2026-10-20) */
			self.failing = true;
			if (self.svcAt && Date.now() - self.svcAt < 30000) return;
			self.svcAt = Date.now();
			fs.exec('/usr/libexec/medvemodem/service', ['status']).then(function(r) {
				var j = {};
				try { j = JSON.parse((r && r.stdout) || '{}'); } catch (e) {}
				if (j.enabled !== false) return;
				self.error.className = 'mm-callout is-info mm-notice';
				self.error.replaceChildren((list(j.stopped_services).length ? _('The MedveModem service is switched off together with other modem services') : _('The MedveModem service is switched off')) +
					'. ' + _('Switch on:') + ' ', E('a', { href: L.url('admin/modem/medvemodem/settings') + '#service' }, _('Settings → MedveModem service')), '.');
			}, function() {});
		}).finally(function() {
			self.pending = null; self.refresh.disabled = false; self.root.setAttribute('aria-busy', 'false');
		});
		return this.pending;
	}
});
