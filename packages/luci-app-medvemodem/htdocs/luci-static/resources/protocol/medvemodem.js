// SPDX-License-Identifier: GPL-3.0-only
'use strict';
'require form';
'require network';

/*
 * The proto 'medvemodem' interface in Network → Interfaces: the owner, 2026-10 («Unsupported protocol type» beside mm0).
 * The engine makes and dials these interfaces itself; the page shows what it set and says where it is changed.
 */
return network.registerProtocol('medvemodem', {
	getI18n: function() {
		return _('MedveModem modem');
	},

	getIfname: function() {
		return this._ubus('l3_device') || this.sid;
	},

	getPackageName: function() {
		return 'medvemodem';
	},

	isFloating: function() {
		return true;
	},

	isVirtual: function() {
		return true;
	},

	getDevices: function() {
		return null;
	},

	containsDevice: function(ifname) {
		return (network.getIfnameOf(ifname) == this.getIfname());
	},

	renderFormOptions: function(s) {
		var o;

		o = s.taboption('general', form.DummyValue, '_mm_note', _('Managed by'));
		o.rawhtml = false;
		o.cfgvalue = function() {
			return _('Managed by MedveModem. Configure modem, APN, and connection under Modem → MedveModem.');
		};

		o = s.taboption('general', form.Value, 'device_id', _('Modem'));
		o.readonly = true;

		o = s.taboption('general', form.Value, 'apn', _('APN'));
		o.readonly = true;
		o.placeholder = _('from the modem\'s settings');

		o = s.taboption('general', form.Value, 'pdptype', _('IP type'));
		o.readonly = true;
		o.placeholder = _('from the modem\'s settings');
	}
});
