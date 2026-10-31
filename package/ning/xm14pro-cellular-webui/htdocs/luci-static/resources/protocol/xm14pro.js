'use strict';
'require form';
'require network';

network.registerPatternVirtual(/^xm14cell$/);
network.registerErrorCode('XM14PRO_DSI_CALL_FAILED', _('The vendor DSI data call failed.'));
network.registerErrorCode('XM14PRO_DSI_TIMEOUT', _('The vendor DSI data call timed out.'));
network.registerErrorCode('XM14PRO_DSI_BAD_DEVICE', _('The vendor DSI data device is invalid.'));
network.registerErrorCode('XM14PRO_DSI_DEVICE_MISSING', _('The vendor DSI data device is missing.'));
network.registerErrorCode('XM14PRO_DSI_IPV4_ROUTE_FAILED', _('Unable to install the mobile IPv4 route.'));
network.registerErrorCode('XM14PRO_DSI_CONTROL_DEVICE_FAILED', _('Unable to create the mobile control interface.'));

return network.registerProtocol('xm14pro', {
	getI18n: function() {
		return _('Xiaomi vendor DSI');
	},

	getIfname: function() {
		return this._ubus('l3_device') || 'xm14cell';
	},

	getPackageName: function() {
		return 'xm14pro-cellular';
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
		return network.getIfnameOf(ifname) === this.getIfname();
	},

	renderFormOptions: function(s) {
		var o;

		o = s.taboption('general', form.Value, 'apn', _('APN'));
		o.placeholder = 'CMNET';

		o = s.taboption('general', form.ListValue, 'iptype', _('IP type'));
		o.value('ipv4', _('IPv4 only'));
		o.value('ipv6', _('IPv6 only'));
		o.value('ipv4v6', _('IPv4/IPv6'));
		o.default = 'ipv4v6';

		o = s.taboption('general', form.Value, 'timeout', _('Connection timeout'));
		o.datatype = 'range(10,300)';
		o.default = '120';

		o = s.taboption('advanced', form.Value, 'metric', _('Gateway metric'));
		o.datatype = 'uinteger';
		o.default = '10';

		o = s.taboption('advanced', form.Flag, 'defaultroute', _('Use default gateway'));
		o.default = o.enabled;

		o = s.taboption('advanced', form.Flag, 'peerdns', _('Use DNS servers advertised by peer'));
		o.default = o.enabled;
	}
});
