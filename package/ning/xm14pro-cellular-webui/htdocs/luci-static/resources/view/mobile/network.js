'use strict';
'require view';
'require poll';
'require mobile.common as mobile';

function field(label, node) { return E('div', { 'class': 'xm14-field' }, [ E('label', {}, label), node ]); }
function row(label, value) { return mobile.missing(value) ? null : mobile.row(label, value); }

function option(value, label, selected) {
	return E('option', { 'value': value, 'selected': value === selected ? '' : null }, label);
}

function number(value) {
	var match = String(value == null ? '' : value).match(/-?[0-9]+(?:\.[0-9]+)?/);
	return match ? Number(match[0]) : null;
}

function signalPercent(s) {
	var n = number(s.nr_rsrp || s.rsrp);
	if (n != null) return Math.max(0, Math.min(100, Math.round((n + 120) * 2.5)));
	n = number(s.rssi);
	return n == null ? 0 : Math.max(0, Math.min(100, Math.round((n + 110) * (100 / 60))));
}

function signalDatum(label, value) {
	return E('div', { 'class': 'xm14-signal-datum' }, [ E('span', {}, label), E('strong', {}, mobile.text(value, '—')) ]);
}

function bondingPhase(phase) {
	return ({
		disabled: '已关闭', starting: '正在启动', waiting_primary: '等待主卡',
		waiting_secondary: '正在连接辅卡', configuring: '正在建立负载均衡',
		online: '双卡聚合运行中', stopped: '已停止', error: '运行错误', unavailable: '状态不可用'
	})[phase] || '等待状态';
}

function runtimeBand(s, bonding) {
	var active = String(s.active_data_slot || '');
	var configured = String(s.data_slot || 'auto');
	var applied = configured === 'auto' ? !!active : active === configured;
	var aggregation = bonding && bonding.enabled ? bondingPhase(bonding.phase) : '已关闭';
	return mobile.statusBand('当前联网状态', '读取设备实际运行状态，每 2 秒更新', [
		{ label: '实际上网卡', value: active ? '卡槽 ' + active : '尚未连接', good: !!active && !!s.connected },
		{ label: '当前运营商', value: mobile.operatorName(s) + (s.operator_code ? ' · ' + s.operator_code : ''), good: !!s.connected },
		{ label: '双卡聚合', value: aggregation, good: !!(bonding && bonding.balance_active && bonding.phase === 'online') },
		{ label: '默认上网卡', value: configured === 'auto' ? '自动' : '卡槽 ' + configured + (applied ? ' · 已生效' : ' · 等待切换'), good: applied }
	], 'antenna', !!s.connected);
}

function simCard(s, slot) {
	var ready = s['sim' + slot + '_ready'];
	var present = s['sim' + slot + '_present'];
	var roles = [];
	var currentData = String(s.active_data_slot || '') === String(slot);
	var preferredData = String(s.data_slot || '') === String(slot);
	if (currentData) roles.push(_('Current mobile data'));
	else if (preferredData) roles.push('默认上网卡 · 等待切换');
	if (String(s.active_voice_slot || '') === String(slot)) roles.push(_('Current calls'));
	if (String(s.active_sms_slot || '') === String(slot)) roles.push(_('Current messages'));
	return E('div', { 'class': 'xm14-sim-card ' + (ready ? 'is-ready ' : '') + (currentData ? 'is-active ' : '') + (preferredData ? 'is-preferred' : '') }, [
		E('b', {}, 'SIM ' + slot),
		E('span', {}, ready ? (s['sim' + slot + '_ims_ready'] ? _('Ready · Calls and messages available') : _('Ready')) : present === false ? _('Absent') : _('Not ready')),
		roles.length ? E('em', {}, roles.join(' · ')) : null
	]);
}

return view.extend({
	load: function() { return Promise.all([ mobile.status(), mobile.bondingStatus() ]); },
	render: function(data) {
		var s = data[0], bonding = data[1] || {};
		var sim1Apn = E('input', { 'value': s.sim1_apn || '', 'placeholder': _('Automatic carrier APN') });
		var sim2Apn = E('input', { 'value': s.sim2_apn || '', 'placeholder': _('Automatic carrier APN') });
		var ipType = E('select', {}, [ option('ipv4', 'IPv4', s.ip_type), option('ipv6', 'IPv6', s.ip_type), option('ipv4v6', 'IPv4/IPv6', s.ip_type) ]);
		var roaming = E('input', { 'type': 'checkbox', 'checked': s.roaming ? '' : null });
		var autoconnect = E('input', { 'type': 'checkbox', 'checked': s.autoconnect ? '' : null });
		var airplane = E('input', { 'type': 'checkbox', 'checked': s.airplane_mode ? '' : null });
		var dataSlot = E('select', {}, [ option('auto', _('Automatic'), s.data_slot || 'auto'), option('1', _('SIM slot 1'), s.data_slot), option('2', _('SIM slot 2'), s.data_slot) ]);
		var voiceSlot = E('select', {}, [ option('auto', _('Automatic'), s.voice_slot || 'auto'), option('1', _('SIM slot 1'), s.voice_slot), option('2', _('SIM slot 2'), s.voice_slot) ]);
		var smsSlot = E('select', {}, [ option('auto', _('Automatic'), s.sms_slot || 'auto'), option('1', _('SIM slot 1'), s.sms_slot), option('2', _('SIM slot 2'), s.sms_slot) ]);
		var slotApply;
		var runtimeHolder = E('div', { 'class': 'xm14-runtime-holder' }, runtimeBand(s, bonding));
		var simCards = E('div', { 'class': 'xm14-sim-cards' }, [ simCard(s, 1), simCard(s, 2) ]);

		function refreshRuntime() {
			return Promise.all([ mobile.status(), mobile.bondingStatus() ]).then(function(result) {
				s = result[0]; bonding = result[1] || {};
				runtimeHolder.replaceChildren(runtimeBand(s, bonding));
				simCards.replaceChildren(simCard(s, 1), simCard(s, 2));
			});
		}

		function done(message) { mobile.notice(message, 'success'); }
		function run(action, args, message) {
			return mobile.action(action, args || []).then(function() { if (message) done(message); else mobile.refresh(); }).catch(mobile.notifyError);
		}
		function saveSlots() {
			slotApply.disabled = true;
			mobile.notice(_('Applying dual-SIM settings...'), 'info');
			return mobile.action('set-slot', [ 'data', dataSlot.value ])
				.then(function() { return mobile.action('set-slot', [ 'voice', voiceSlot.value ]); })
				.then(function() { return mobile.action('set-slot', [ 'sms', smsSlot.value ]); })
				.then(function() { done('卡槽设置已保存并生效。'); return refreshRuntime(); })
				.then(function() { slotApply.disabled = false; })
				.catch(function(error) { slotApply.disabled = false; mobile.notifyError(error); });
		}

		var actions = mobile.head('', '', [
			mobile.button(_('Connect'), 'cbi-button-action', function() { run('connect'); }, !s.modem_available || s.connected),
			mobile.button(_('Disconnect'), 'cbi-button-negative', function() { run('disconnect'); }, !s.connected),
			mobile.button(_('Refresh'), '', function() { run('refresh-radio'); }, !s.radio_refresh_available, 'refresh')
		]);

		var quality = signalPercent(s);
		var modemStatus = [
			mobile.datum(_('Connection'), s.connected ? _('Connected') : _('Disconnected'), s.interface, s.connected ? 'is-good' : 'is-bad'),
			mobile.datum(_('Network mode'), s.access_technologies, s.active_bands),
			mobile.datum(_('Operator'), s.operator_name, s.operator_code),
			mobile.datum(_('Runtime APN'), s.runtime_apn, s.ipv4 || s.ipv6)
		];
		var signalPanel = E('div', { 'class': 'xm14-signal-console' }, [
			E('div', { 'class': 'xm14-signal-ring', 'style': '--value:' + quality }, [ E('strong', {}, quality + '%'), E('small', {}, _('Quality')) ]),
			E('div', { 'class': 'xm14-signal-values' }, [
				signalDatum('RSRP', s.nr_rsrp || s.rsrp), signalDatum('SINR', s.nr_sinr || s.sinr),
				signalDatum('RSSI', s.rssi), signalDatum('RSRQ', s.nr_rsrq || s.rsrq)
			])
		]);

		poll.add(function() { return refreshRuntime().catch(mobile.notifyError); }, 2);

		return mobile.page([
			runtimeHolder,
			E('div', { 'class': 'xm14-network-console' }, [
				mobile.panel(_('Modem status'), _('Native phone baseband'), 'chip', [ E('div', { 'class': 'xm14-datum-grid' }, modemStatus) ], '', 'xm14-modem-panel'),
				mobile.panel(_('APN and data'), _('Each SIM uses its own carrier profile by default'), 'globe', [
					field(_('SIM slot 1 APN override'), sim1Apn), field(_('SIM slot 2 APN override'), sim2Apn),
					field(_('PDP type'), ipType), field(_('Allow roaming'), roaming), field(_('Automatic connection'), autoconnect),
					E('div', { 'class': 'xm14-panel-actions' }, mobile.button(_('Save settings'), 'cbi-button-positive', function() {
						run('set-data', [ sim1Apn.value, sim2Apn.value, ipType.value, roaming.checked ? '1' : '0', autoconnect.checked ? '1' : '0' ], _('Mobile data settings saved.'));
					}))
				], '', 'xm14-apn-panel'),
				mobile.panel(_('Signal and quality'), s.active_bands || '', 'antenna', [ signalPanel ], '', 'xm14-signal-panel')
			]),
			E('div', { 'class': 'xm14-control-grid' }, [
				mobile.panel(_('Connection control'), _('Current session %s').format(s.connected ? mobile.uptime(s.session_uptime) : '—'), 'power', [
					E('div', { 'class': 'xm14-connect-state ' + (s.connected ? 'is-online' : '') }, [ E('i'), E('strong', {}, s.connected ? _('Connected') : _('Disconnected')), E('span', {}, mobile.text(s.interface, '—')) ]),
					actions
				]),
				mobile.panel(_('Dual-SIM selection'), _('Set independent defaults for mobile data, calls and messages'), 'sim', [
					simCards,
					field(_('Mobile data SIM'), dataSlot),
					field(_('Voice SIM'), voiceSlot),
					field(_('Messages SIM'), smsSlot),
					E('div', { 'class': 'xm14-note' }, _('Automatic uses the active mobile-data SIM first and switches to another ready SIM when necessary.')),
					E('div', { 'class': 'xm14-panel-actions' }, slotApply = mobile.button(_('Apply'), 'cbi-button-positive', saveSlots))
				]),
				mobile.panel(_('Radio control'), _('Hardware radio and flight mode'), 'antenna', [
					field(_('Airplane mode'), airplane),
					E('div', { 'class': 'xm14-note' }, _('Turning on airplane mode disconnects mobile data and powers down the radio.')),
					E('div', { 'class': 'xm14-panel-actions' }, mobile.button(_('Apply'), airplane.checked ? 'cbi-button-negative' : 'cbi-button-positive', function() { run('set-airplane', [ airplane.checked ? '1' : '0' ]); }, !s.modem_available))
				])
			])
		]);
	},
	handleSaveApply: null, handleSave: null, handleReset: null
});
