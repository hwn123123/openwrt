'use strict';
'require view';
'require uci';
'require ui';
'require photonicat2.common as pcat';

function intValue(id, fallback) {
	var node = document.getElementById(id);
	var value = node ? parseInt(node.value, 10) : fallback;
	return isFinite(value) ? value : fallback;
}

function eventOf(data, action) {
	var list = data && data.schedule && data.schedule['event-list'];
	if (!Array.isArray(list)) return null;
	for (var i = 0; i < list.length; i++)
		if (Number(list[i].action) === action) return list[i];
	return null;
}

function field(label, node, hint) {
	return E('div', { 'class': 'pcat-control' }, [
		E('label', {}, label), E('div', {}, [ node, hint ? E('div', { 'class': 'pcat-hint' }, hint) : '' ])
	]);
}

return view.extend({
	load: function() {
		return Promise.all([ uci.load('photonicat2'), L.resolveDefault(pcat.call('power-get'), {}) ]);
	},

	save: function() {
		var mode = intValue('pcat-power-mode', 0);
		var car = document.getElementById('pcat-car-mode').checked ? 1 : 0;
		var delay = intValue('pcat-car-delay', 10);
		var offTime = (document.getElementById('pcat-off-time').value || '23:00').split(':');
		var onTime = (document.getElementById('pcat-on-time').value || '07:00').split(':');
		var offHour = parseInt(offTime[0], 10), offMinute = parseInt(offTime[1], 10);
		var onHour = parseInt(onTime[0], 10), onMinute = parseInt(onTime[1], 10);
		var dayBits = 0;
		document.querySelectorAll('#pcat-days input').forEach(function(input) {
			if (input.checked) dayBits |= (1 << Number(input.value));
		});
		var lowThreshold = intValue('pcat-low-threshold', 10);
		var idleMinutes = intValue('pcat-idle-minutes', 30);
		if (mode < 0 || mode > 1 || delay < 0 || delay > 3600 ||
			!isFinite(offHour) || offHour < 0 || offHour > 23 || !isFinite(offMinute) || offMinute > 59 ||
			!isFinite(onHour) || onHour < 0 || onHour > 23 || !isFinite(onMinute) || onMinute > 59 ||
			lowThreshold < 3 || lowThreshold > 50 || idleMinutes < 1 || idleMinutes > 1440) {
			pcat.notify('请检查开关机和安全关机参数', 'error');
			return;
		}
		var button = document.getElementById('pcat-save-policy');
		if (button) button.disabled = true;
		return pcat.call('power-mode-set', [ String(mode) ])
			.then(function() { return pcat.call('charger-set', [ String(car), String(delay) ]); })
			.then(function() { return pcat.call('schedule-set', [
				String(document.getElementById('pcat-off-enable').checked ? 1 : 0), String(offHour), String(offMinute),
				String(document.getElementById('pcat-on-enable').checked ? 1 : 0), String(onHour), String(onMinute), String(dayBits)
			]); })
			.then(function() {
				uci.set('photonicat2', 'main', 'low_battery_enable', document.getElementById('pcat-low-enable').checked ? '1' : '0');
				uci.set('photonicat2', 'main', 'low_battery_threshold', String(lowThreshold));
				uci.set('photonicat2', 'main', 'idle_shutdown_enable', document.getElementById('pcat-idle-enable').checked ? '1' : '0');
				uci.set('photonicat2', 'main', 'idle_shutdown_minutes', String(idleMinutes));
				uci.set('photonicat2', 'main', 'idle_battery_only', document.getElementById('pcat-idle-battery').checked ? '1' : '0');
				return uci.save();
			})
				.then(function() { return pcat.call('apply'); })
			.then(function() { pcat.notify('开关机策略已保存', 'info'); })
			.catch(function(err) { pcat.notify(err.message, 'error'); })
			.finally(function() { if (button) button.disabled = false; });
	},

	render: function(data) {
		pcat.loadStyle();
		var power = data[1] || {};
		var off = eventOf(power, 0) || {}, on = eventOf(power, 1) || {};
		var offHour = off.hour == null ? 23 : Number(off.hour), offMinute = off.minute == null ? 0 : Number(off.minute);
		var onHour = on.hour == null ? 7 : Number(on.hour), onMinute = on.minute == null ? 0 : Number(on.minute);
		var bits = Number(off['dow-bits'] != null ? off['dow-bits'] : on['dow-bits']);
		if (!bits) bits = 127;
		var mode = power.power_mode && power.power_mode.mode != null ? Number(power.power_mode.mode) : 0;
		var car = power.charger && Number(power.charger.state) === 1;
		var delay = power.charger && power.charger.timeout != null ? power.charger.timeout : 10;
		var days = [ '日', '一', '二', '三', '四', '五', '六' ];
		var dayNodes = days.map(function(day, index) {
			return E('label', {}, [ E('input', { 'type': 'checkbox', 'value': String(index), 'checked': !!(bits & (1 << index)) }), '周' + day ]);
		});
		var root = E('div', { 'class': 'pcat-page' }, [
			E('h2', {}, '开关机策略'),
			E('p', { 'class': 'pcat-subtitle' }, '上电模式、车载延时和 PMU RTC 定时由硬件接口执行；低电量与无线空闲关机由本机服务执行。'),
			E('div', { 'class': 'pcat-grid wide' }, [
				E('div', { 'class': 'pcat-card' }, [ E('h3', {}, '上电与车载模式'),
					field('外部电源接入方式', E('select', { 'id': 'pcat-power-mode' }, [
						E('option', { 'value': '0', 'selected': mode === 0 }, '手动开机'),
						E('option', { 'value': '1', 'selected': mode === 1 }, '接通电源自动开机')
					]), 'PMU 支持的两种上电行为。'),
					field('车载延时模式', E('input', { 'id': 'pcat-car-mode', 'type': 'checkbox', 'checked': car }), '随车辆供电自动处理断电。'),
					field('断电延时（秒）', E('input', { 'id': 'pcat-car-delay', 'type': 'number', 'min': '0', 'max': '3600', 'value': String(delay) }))
				]),
				E('div', { 'class': 'pcat-card' }, [ E('h3', {}, '安全关机'),
					field('低电量自动关机', E('input', { 'id': 'pcat-low-enable', 'type': 'checkbox', 'checked': uci.get('photonicat2', 'main', 'low_battery_enable') === '1' }), '连续约一分钟低于阈值才关机。'),
					field('低电量阈值（%）', E('input', { 'id': 'pcat-low-threshold', 'type': 'number', 'min': '3', 'max': '50', 'value': uci.get('photonicat2', 'main', 'low_battery_threshold') || '10' })),
					field('无人连接自动关机', E('input', { 'id': 'pcat-idle-enable', 'type': 'checkbox', 'checked': uci.get('photonicat2', 'main', 'idle_shutdown_enable') === '1' }), '所有无线接入点无客户端时计时。'),
					field('空闲时间（分钟）', E('input', { 'id': 'pcat-idle-minutes', 'type': 'number', 'min': '1', 'max': '1440', 'value': uci.get('photonicat2', 'main', 'idle_shutdown_minutes') || '30' })),
					field('仅电池供电时生效', E('input', { 'id': 'pcat-idle-battery', 'type': 'checkbox', 'checked': uci.get('photonicat2', 'main', 'idle_battery_only') !== '0' }))
				])
			]),
			E('div', { 'class': 'pcat-card' }, [ E('h3', {}, 'PMU RTC 每周定时'),
				E('div', { 'class': 'pcat-grid wide' }, [
					E('div', {}, [ field('定时关机', E('input', { 'id': 'pcat-off-enable', 'type': 'checkbox', 'checked': Number(off.enabled) === 1 })), field('关机时间', E('input', { 'id': 'pcat-off-time', 'type': 'time', 'value': '%02d:%02d'.format(offHour, offMinute) })) ]),
					E('div', {}, [ field('定时开机', E('input', { 'id': 'pcat-on-enable', 'type': 'checkbox', 'checked': Number(on.enabled) === 1 })), field('开机时间', E('input', { 'id': 'pcat-on-time', 'type': 'time', 'value': '%02d:%02d'.format(onHour, onMinute) })) ])
				]),
				E('div', { 'id': 'pcat-days', 'class': 'pcat-day-grid' }, dayNodes)
			]),
			E('div', { 'class': 'cbi-page-actions' }, [ E('button', { 'id': 'pcat-save-policy', 'class': 'btn cbi-button-positive important', 'click': ui.createHandlerFn(this, 'save') }, '保存并应用策略') ])
		]);
		return root;
	}
});
