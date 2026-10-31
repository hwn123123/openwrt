'use strict';
'require view';
'require fs';
'require uci';
'require mobile.common as mobile';

function field(label, node) { return E('div', { 'class': 'xm14-field' }, [ E('label', {}, label), node ]); }
function flag(value) { return E('input', { 'type': 'checkbox', 'checked': String(value) !== '0' ? '' : null }); }
function option(value, label, selected) { return E('option', { 'value': value, 'selected': value === selected ? '' : null }, label); }

function saveConfig(config, values, command, message) {
	Object.keys(values).forEach(function(key) {
		uci.set(config, 'main', key, typeof values[key] === 'boolean' ? (values[key] ? '1' : '0') : String(values[key]));
	});
	return uci.save().then(function() { return uci.apply(); }).then(function() {
		return command ? fs.exec(command[0], command.slice(1)) : null;
	}).then(function() { mobile.notice(message, 'success'); }).catch(mobile.notifyError);
}

return view.extend({
	load: function() { return Promise.all([ mobile.status(), mobile.logs(), uci.load('xm14usb'), uci.load('xm14cpe') ]); },
	render: function(result) {
		var s = result[0], logs = result[1];
		var usbMode = E('select', {}, [
			option('network', _('USB network and charging'), uci.get('xm14usb', 'main', 'mode') || 'network'),
			option('charge_only', _('Charging only'), uci.get('xm14usb', 'main', 'mode') || 'network')
		]);
		var cpeEnabled = flag(uci.get('xm14cpe', 'main', 'enabled') || '1');
		var usbForward = flag(uci.get('xm14cpe', 'main', 'usb_forward') || '1');
		var blockWan = flag(uci.get('xm14cpe', 'main', 'block_wan_management') || '1');
		var allowPing = flag(uci.get('xm14cpe', 'main', 'allow_wan_ping') || '1');
		var mssClamp = flag(uci.get('xm14cpe', 'main', 'tcp_mss_clamp') || '1');

		function interfaceRow(iconName, name, role, address, online) {
			return E('div', { 'class': 'xm14-interface-row' }, [
				mobile.icon(iconName, online ? 'green' : 'orange', 'art-stat-icon'),
				E('div', { 'class': 'xm14-interface-name' }, [ E('strong', {}, name), E('small', {}, role) ]),
				E('div', { 'class': 'xm14-interface-address' }, [ E('span', {}, _('Address')), E('strong', {}, mobile.text(address, '—')) ]),
				E('span', { 'class': 'ui-tag ' + (online ? 'is-good' : 'is-warn') }, online ? _('Online') : _('Offline'))
			]);
		}
		function service(name, available) {
			return E('div', { 'class': 'xm14-service-tile ' + (available ? 'is-ready' : '') }, [ E('i'), E('span', {}, name), E('strong', {}, available ? _('Ready') : _('Unavailable')) ]);
		}

		return mobile.page([
			mobile.panel(_('Interface overview'), _('Real uplink and sharing interfaces'), 'route', [
				E('div', { 'class': 'xm14-interface-list' }, [
					interfaceRow('antenna', s.interface || _('Mobile uplink'), _('Cellular uplink'), s.ipv4 || s.ipv6, !!s.connected),
					interfaceRow('wifi', s.wifi_interface || _('Wireless AP'), [ s.wifi_ssid_5g, _('%s clients').format(mobile.text(s.wifi_clients, '0')) ].filter(Boolean).join(' · '), s.wifi_ipv4, !!s.wifi_up),
					interfaceRow('usb', s.usb_interface || _('USB network'), _('USB gadget'), s.usb_ipv4, !!s.usb_up)
				])
			], mobile.button(_('Refresh'), '', mobile.refresh, false, 'refresh'), 'xm14-interface-panel'),
			E('div', { 'class': 'xm14-cpe-console-grid' }, [
				mobile.panel(_('CPE forwarding policy'), _('Firewall, NAT and path policy'), 'route', [
					field(_('Enable forwarding'), cpeEnabled), field(_('Share through USB'), usbForward),
					field(_('Block management from mobile network'), blockWan), field(_('Allow mobile-side ping'), allowPing), field(_('TCP MSS clamp'), mssClamp),
					E('div', { 'class': 'xm14-panel-actions' }, mobile.button(_('Save and apply'), 'cbi-button-positive', function() {
						saveConfig('xm14cpe', { enabled:cpeEnabled.checked, usb_forward:usbForward.checked, block_wan_management:blockWan.checked, allow_wan_ping:allowPing.checked, tcp_mss_clamp:mssClamp.checked }, [ '/etc/init.d/xm14pro-cpe-firewall', 'reload' ], _('CPE policy applied.'));
					}))
				]),
				mobile.panel(_('USB sharing'), _('Type-C gadget and charging mode'), 'usb', [
					E('div', { 'class': 'xm14-datum-grid compact' }, [
						mobile.datum(_('Status'), s.usb_up ? _('Active') : _('Inactive'), s.usb_interface, s.usb_up ? 'is-good' : 'is-warn'),
						mobile.datum(_('Controller'), s.usb_udc, s.usb_ipv4)
					]),
					field(_('Type-C mode'), usbMode),
					E('div', { 'class': 'xm14-panel-actions' }, mobile.button(_('Save and apply'), 'cbi-button-positive', function() { saveConfig('xm14usb', { mode:usbMode.value }, [ '/etc/init.d/xm14pro-usb-gadget', 'restart' ], _('USB mode applied.')); }))
				]),
				mobile.panel(_('Device service status'), _('Core service status'), 'chip', [
					E('div', { 'class': 'xm14-service-grid' }, [
						service('NAS', s.nas_service), service('UIM', s.uim_service), service('DPM', s.dpm_service),
						service('WMS', s.wms_service), service('VOICE', s.voice_service), service('IMS', s.ims_service)
					])
				])
			]),
			E('div', { 'class': 'xm14-counter-grid' }, [
				mobile.datum(_('Received'), mobile.bytes(s.rx_bytes), _('Cellular traffic'), 'is-good'),
				mobile.datum(_('Sent'), mobile.bytes(s.tx_bytes), _('Cellular traffic')),
				mobile.datum(_('MTU'), s.mtu, s.interface),
				mobile.datum(_('System uptime'), mobile.uptime(s.uptime), _('nwrt runtime'))
			]),
			mobile.panel(_('System log'), _('Recent modem and CPE events'), 'activity', [ E('pre', { 'class': 'xm14-log xm14-terminal' }, logs.logs || _('No matching log entries.')) ], '', 'xm14-log-panel')
		]);
	},
	handleSaveApply: null, handleSave: null, handleReset: null
});
