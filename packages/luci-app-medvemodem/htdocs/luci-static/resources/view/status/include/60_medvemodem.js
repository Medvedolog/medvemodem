'use strict';
'require baseclass';
'require rpc';
'require uci';
'require medvemodem.common as mm';

/* «Status» → «Overview» (the owner, 2026-10-17, as in the mockup): one line per modem and a link to its page; the engine's
 * own data only, read every time the overview refreshes; nothing when the engine has no modems or does not answer */
var getDevices = rpc.declare({ object: 'medvemodem', method: 'devices', params: ['absent'], reject: true });
var getStatus = rpc.declare({ object: 'medvemodem', method: 'status', reject: true });
var getMetrics = rpc.declare({ object: 'medvemodem', method: 'metrics', params: ['id', 'max_age_ms'], reject: true });
var getUplink = rpc.declare({ object: 'medvemodem', method: 'uplink', reject: true });

/* a modem ModemManager does not read (HiLink's web, QModem, the engine's own AT) gets its operator and signal as on the
 * dashboard (the owner, 2026-10-21: «у хуавей не показывает ничего по приёму и оператору» on the overview): the engine's
 * reading, asked at most once a minute (QModem's: every 2 min), waited for 2.5 s at most, kept up to 10 min */
var readings = {};
function readable(d) {
	var ctl = (d.owner || {}).control;
	return !d.modemmanager && d.present === true && !d.frozen && ctl && ctl !== 'modemmanager' && ctl !== 'none' && ctl !== 'unresolved';
}
function ask(d) {
	var id = d.device_id, every = (d.owner || {}).control === 'qmodem' ? 120000 : 60000;
	var c = readings[id] || (readings[id] = {});
	if (c.busy || (c.asked && Date.now() - c.asked <= (c.failed ? 20000 : every))) return c.busy || null;
	c.asked = Date.now();
	return c.busy = getMetrics(id, every).then(function(r) {
		if (r && r.metrics) { c.mm = mm.atAsMm(r.metrics); c.at = Date.now(); c.failed = false; } else c.failed = true;
	}, function() { c.failed = true; }).finally(function() { c.busy = null; });
}
function withReading(d) {
	var c = readable(d) && readings[d.device_id];
	if (c && c.mm && Date.now() - c.at > 600000) c.mm = null;
	return c && c.mm ? Object.assign({}, d, { modemmanager: c.mm, reading_fresh: true }) : d;
}

/* a backup's words with its place in the priority third (the owner, 2026-10-19: «Резерв 2, готов») */
var ROLE = { carrying: [_('Main channel'), 'is-ok'], backup_ready: [_('Backup, ready'), 'is-backup', _('Backup %d, ready')],
	backup_not_ready: [_('Backup, not ready'), 'is-warn', _('Backup %d, not ready')],
	backup_limited: [_('Backup, limited access'), 'is-warn', _('Backup %d, limited access')], limited: [_('Limited access'), 'is-warn'],
	no_internet: [_('No internet'), 'is-warn'] };
function words(v) { return typeof v === 'string' && v !== '--' ? v : ''; }
function list(v) { return Array.isArray(v) ? v : []; }

/* the address as the modem page finds it (bench 2026-10-19: «—» while both modems were online): netifd's addresses
 * (ipv4_addresses, ipv6_addresses, with the prefix), else ModemManager's bearer address (a string), else the bare fact */
function addressOf(net) {
	var first = list(net.ipv4_addresses).concat(list(net.ipv6_addresses)).filter(function(a) { return typeof a === 'string'; })[0];
	return first ? first.replace(/\/\d+$/, '') : typeof net.ipv4 === 'string' ? net.ipv4 : typeof net.ipv6 === 'string' ? net.ipv6 :
		net.ipv4 || net.ipv6 ? _('obtained') : '';
}

/* the line above the table (the owner, 2026-10-30: «не показываем сразу наглядно что инет мобильный есть и через какой
 * модем»): the internet or not, through which modem (or the wired channel, or the tunnel), how many backups are ready */
function verdict(uplink, nameOf) {
	if (!uplink || typeof uplink.internet !== 'string') return null;
	var carrier = uplink.carrier || {}, paths = list(uplink.paths);
	var path = paths.filter(function(p) { return carrier.device && p.device === carrier.device; })[0] || {};
	var via = carrier.device_id ? _('through the modem “%s”').format(nameOf(carrier.device_id) || words(carrier.name) || _('modem')) :
		carrier.device ? _('through the wired channel “%s”').format(words(path.interface) || carrier.device) : '';
	var tun = uplink.internet === 'limited' && uplink.tunnel && uplink.tunnel.ok === true;
	var ready = paths.filter(function(p) { return p.device_id && p.role === 'backup_ready' && p.device_id !== carrier.device_id; }).length;
	var head = tun ? _('The internet works through the tunnel') + (via ? ' (' + _('the operator\'s whitelist %s').format(via) + ')' : '') :
		uplink.internet === 'available' ? _('The internet works') + (via ? ' ' + via : '') :
		uplink.internet === 'limited' ? _('Limited access') + (via ? ' ' + via : '') :
		uplink.internet === 'unavailable' ? _('No internet access') : _('Checking the connection');
	var tone = tun || uplink.internet === 'available' ? 'is-ok' : uplink.internet === 'unavailable' ? 'is-err' : 'is-warn';
	return [head + (ready ? ' · ' + (ready === 1 ? _('the backup channel is ready') : _('backup channels ready: %d').format(ready)) : ''), tone];
}

/* the pill: what a person needs from the overview, nothing more */
function stateOf(d, mmFresh) {
	var mmd = d.modemmanager || {}, session = d.session || {}, uplink = d.uplink || {};
	if (d.present !== true) return [_('Disconnected from USB'), 'is-off'];
	if ((d.recovery || {}).state === 'running') return [_('Recovery…'), 'is-run'];
	if (session.state === 'connected' && uplink.role === 'limited' && (uplink.tunnel || {}).ok === true)
		return [_('Whitelist, the internet through the tunnel'), 'is-ok'];
	if (session.state === 'connected') return ROLE[uplink.role] || [_('Connected'), 'is-ok'];
	if (mmFresh && mmd.failed_reason === 'sim-missing') return [_('No SIM card'), 'is-err'];
	if ((d.connection || {}).deactivated) return [_('Deactivated'), 'is-paused'];
	if (session.state === 'connecting') return [_('Connecting…'), 'is-run'];
	return [_('No link'), 'is-warn'];
}

return baseclass.extend({
	title: _('Modems'),

	load: function() {
		/* switched off in MedveModem's settings (option status_overview '0'; on by default, the owner 2026-10-30) */
		return uci.load('medvemodem').then(function() { return uci.get('medvemodem', 'core', 'status_overview') === '0'; }, function() { return false; }).then(function(off) {
			if (off) return null;
			return Promise.all([getStatus(), getDevices(true), getUplink().catch(function() { return null; })]);
		}).then(function(data) {
			if (!data) return null;
			var asks = (Array.isArray((data[1] || {}).devices) ? data[1].devices : []).filter(readable).map(ask).filter(Boolean);
			return Promise.race([Promise.all(asks), new Promise(function(done) { setTimeout(done, 2500); })]).then(function() { return data; });
		}).catch(function() { return null; });
	},

	render: function(data) {
		var status = data && data[0], devices = data && data[1] && Array.isArray(data[1].devices) ? data[1].devices : [];
		if (!status || !devices.length) return null;
		var age = (status.modemmanager || {}).age_s, mmFresh = (status.modemmanager || {}).backend === 'available' && typeof age === 'number' && age <= 30;
		var expert = mm.level() === 'expert', href = L.url('admin/modem/medvemodem/modem');
		var metric = function(d) { var m = (d.connection || {}).metric; return typeof m === 'number' ? m : 1e9; };
		var sorted = devices.map(function(d, i) { return [d, i]; }).sort(function(a, b) { return metric(a[0]) - metric(b[0]) || a[1] - b[1]; });
		/* a backup says its place in the priority, as on the dashboard (the owner, 2026-10-19): the modems with an interface */
		var ranked = sorted.filter(function(p) { return words((p[0].connection || {}).interface) || words((p[0].session || {}).interface); })
			.map(function(p) { return p[0].device_id; });
		var names = {}, carrierId = ((data[2] || {}).carrier || {}).device_id;
		var rows = sorted.map(function(p) {
			var d = withReading(p[0]), mmd = d.modemmanager || {}, st = stateOf(d, mmFresh), place = ranked.indexOf(d.device_id);
			if (place > 0 && st[2]) st = [st[2].format(place), st[1]];
			var model = mm.profileTitle(d, mmd) || words(mmd.model) || words(d.model) || words(d.product) || _('USB modem');
			names[d.device_id] = words(d.name) || model;
			var on = d.present === true && (mmFresh || !!d.reading_fresh);
			var badge = on ? mm.opBadge(mmd.operator_code, words(mmd.operator), (d.sim_operator || {}).code) : null;
			var tech = on ? (expert ? mm.techExact : mm.techWords)(mmd.access_tech) : '';
			var net_ = on ? [badge ? '' : words(mmd.operator), tech].filter(Boolean).join(' · ') : '';
			/* a connected HiLink often has no address of its own here: its «—» takes no line on a phone either (Bootstrap, 2026-11-02) */
			var conn = (d.session || {}).state === 'connected', addrText = conn ? addressOf((d.session || {}).netifd || {}) : '';
			var addr = E('td', { 'class': 'td left mm-mono' + (addrText ? '' : ' mm-empty'), 'data-title': _('Address') });
			/* the signal bars of the dashboard's card, beside the operator (bench 2026-10-19) */
			var bars = mm.barsNode();
			mm.setBars(bars, on ? mm.signalIcon(mmd) : null);
			mm.secret(addr, addrText || '—');
			/* the modem the internet goes through, marked in its row (the owner, 2026-10-30: «можно прям выделять модем в таблице») */
			var carrying = d.present === true && conn && ((carrierId && carrierId === d.device_id) || (!carrierId && (d.uplink || {}).role === 'carrying'));
			return E('tr', { 'class': 'tr' + (carrying ? ' is-carrying' : '') }, [
				E('td', { 'class': 'td left', 'data-title': _('Modem') }, [E('a', { href: href + '#' + encodeURIComponent(d.device_id) }, words(d.name) || model),
					words(d.name) ? E('span', { 'class': 'mm-faint' }, ' ' + model) : '']),
				E('td', { 'class': 'td left' + (badge || net_ ? '' : ' mm-empty'), 'data-title': _('Network') }, badge || net_ ? [bars, ' ', badge || '', badge && net_ ? ' ' : '', net_] : '—'),
				E('td', { 'class': 'td left mm-mono' + (mm.protoOf(d) ? '' : ' mm-empty'), 'data-title': _('Protocol') }, mm.protoOf(d) || '—'),
				addr,
				E('td', { 'class': 'td right', 'data-title': _('State') }, E('span', { 'class': 'mm-pill ' + st[1] }, st[0]))
			]);
		});
		/* the stylesheet once in the head: the overview redraws this every few seconds (audit 2026-10-20) */
		if (!document.getElementById('mm-status-css'))
			document.head.appendChild(E('link', { id: 'mm-status-css', rel: 'stylesheet', href: L.resource('medvemodem/dashboard.css') + '?v=' + mm.VERSION }));
		var v = verdict(data[2], function(id) { return names[id]; });
		return E('div', { 'class': 'mm-status' }, [
			v ? E('p', { 'class': 'mm-status-verdict' }, E('span', { 'class': 'mm-pill ' + v[1] }, v[0])) : '',
			E('table', { 'class': 'table' }, [E('tr', { 'class': 'tr table-titles' }, [
				E('th', { 'class': 'th left' }, _('Modem')), E('th', { 'class': 'th left' }, _('Network')), E('th', { 'class': 'th left' }, _('Protocol')), E('th', { 'class': 'th left' }, _('Address')),
				E('th', { 'class': 'th right' }, _('State'))])].concat(rows)),
			E('p', {}, E('a', { href: L.url('admin/modem/medvemodem') }, _('All modems →')))
		]);
	}
});
