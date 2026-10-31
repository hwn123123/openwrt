'use strict';
'require view';
'require fs';
'require poll';
'require uci';

var CONTROL = '/usr/libexec/xm14pro-battery';
var noticeTimer;

function loadStyle() {
	if (document.getElementById('xm14-battery-style')) return;
	var link = document.createElement('link');
	link.id = 'xm14-battery-style';
	link.rel = 'stylesheet';
	link.href = '/luci-static/resources/xm14pro/battery.css?v=7';
	document.head.appendChild(link);
}

function parse(result) {
	var data;
	try { data = JSON.parse((result && result.stdout) || '{}'); }
	catch (e) { throw new Error(_('Battery backend returned invalid data')); }
	if (data.ok === false) throw new Error(data.error || _('Battery operation failed'));
	return data;
}

function run(action, args) {
	return fs.exec(CONTROL, [ action ].concat(args || [])).then(parse);
}

function notice(message, error) {
	var node = document.getElementById('xm14-battery-notice');
	if (!node) {
		node = E('button', { 'id': 'xm14-battery-notice', 'type': 'button', 'click': function() { node.classList.remove('show'); } }, [ E('i'), E('span') ]);
		document.body.appendChild(node);
	}
	node.className = error ? 'is-error' : 'is-success';
	node.querySelector('span').textContent = String(message || '');
	window.clearTimeout(noticeTimer);
	window.requestAnimationFrame(function() { node.classList.add('show'); });
	noticeTimer = window.setTimeout(function() { node.classList.remove('show'); }, error ? 5000 : 2600);
}

function num(value) {
	var n = Number(value);
	return isFinite(n) ? n : null;
}

function pickBattery(data) {
	var supplies = data.supplies || [], i;
	for (i = 0; i < supplies.length; i++)
		if (String(supplies[i].type || '').toLowerCase() === 'battery') return supplies[i];
	return null;
}

function firstSupply(data, types) {
	var supplies = data.supplies || [], i;
	for (i = 0; i < supplies.length; i++)
		if (types.indexOf(String(supplies[i].type || '').toLowerCase()) >= 0) return supplies[i];
	return null;
}

function supplyNamed(data, name) {
	var supplies = data.supplies || [], i;
	for (i = 0; i < supplies.length; i++)
		if (String(supplies[i].name || '') === name) return supplies[i];
	return null;
}

function tempC(value) {
	var n = num(value);
	if (n == null) return null;
	if (Math.abs(n) > 1000) return n / 1000;
	if (Math.abs(n) > 150) return n / 10;
	return n;
}

function volts(value) { var n = num(value); return n == null ? null : (Math.abs(n) > 10000 ? n / 1000000 : n).toFixed(3) + ' V'; }
function amps(value) { var n = num(value); return n == null ? null : (Math.abs(n) > 10000 ? n / 1000000 : n).toFixed(3) + ' A'; }
function watts(value) { var n = num(value); return n == null ? null : (Math.abs(n) > 100000 ? n / 1000000 : n).toFixed(2) + ' W'; }
function capacity(value) { var n = num(value); return n == null ? null : (Math.abs(n) > 10000 ? n / 1000 : n).toFixed(0) + ' mAh'; }

function text(value) { return value === null || value === undefined || value === '' ? '—' : String(value); }

function stateText(value) {
	var raw = text(value), key = raw.toLowerCase(), values = {
		'full': _('已充满'), 'charging': _('充电中'), 'discharging': _('放电中'),
		'not charging': _('未充电'), 'unknown': _('未知'), 'good': _('良好'),
		'overheat': _('过热'), 'dead': _('故障'), 'cold': _('温度过低'),
		'li-poly': _('锂聚合物'), 'li-ion': _('锂离子'),
		'fast': _('快速充电'), 'taper': _('涓流充电'), 'none': _('无'),
		'online': _('在线'), 'offline': _('离线'), 'source': _('供电端'),
		'sink': _('受电端'), 'host': _('主机'), 'device': _('设备')
	};
	return values[key] || raw;
}

function boolText(value, on, off) {
	if (value === null || value === undefined || value === '') return '—';
	return String(value) === '1' ? (on || _('已开启')) : String(value) === '0' ? (off || _('已关闭')) : stateText(value);
}

function modeText(value) {
	if (value === null || value === undefined || value === '') return '—';
	return String(value) === '0' ? _('已关闭') : _('模式 %s').format(String(value));
}

function backendError(message) {
	var map = {
		'control is not exposed by the active battery driver': _('当前电池驱动没有提供此控制项'),
		'control exists but is read-only': _('此控制项当前为只读'),
		'value must be an integer': _('设置值必须是整数'),
		'value is outside the safe range': _('设置值超出安全范围'),
		'kernel rejected the requested value': _('内核拒绝了该设置'),
		'kernel did not keep the requested value': _('驱动未能保持该设置')
	};
	return map[String(message || '')] || String(message || _('Battery operation failed'));
}

function metric(label, value, detail, tone) {
	return E('article', { 'class': 'battery-metric ' + (tone || '') }, [
		E('span', {}, label), E('strong', {}, text(value)), E('small', {}, text(detail))
	]);
}

function row(label, value) {
	if (value === null || value === undefined || value === '') return null;
	return E('div', { 'class': 'battery-row' }, [ E('span', {}, label), E('strong', {}, String(value)) ]);
}

function section(title, children, className) {
	children = (children || []).filter(Boolean);
	if (!children.length) return null;
	return E('section', { 'class': 'battery-section ' + (className || '') }, [ E('h2', {}, title), E('div', { 'class': 'battery-card' }, children) ]);
}

function temperature(data, battery) {
	var t = battery ? tempC(battery.temp) : null, thermals = data.thermals || [], i;
	if (t == null) for (i = 0; i < thermals.length; i++) if (thermals[i].kind === 'battery') { t = tempC(thermals[i].temp); break; }
	return t;
}

function controlCard(data, refresh) {
	var controls = data.controls || {}, nodes = [];
	function apply(key, value, done) {
		run('control', [ key, String(value) ]).then(function() {
			notice(_('设置已生效'));
			refresh();
		}).catch(function(e) {
			notice(backendError(e.message), true);
		}).finally(done || function() {});
	}
	function addNumber(key, label, description, unit, divisor, min, max, warning) {
		var ctl = controls[key], value, input;
		if (!ctl || !ctl.writable) return;
		value = num(ctl.value);
		if (value != null && divisor) value /= divisor;
		input = E('input', { 'type': 'number', 'min': min, 'max': max, 'value': value == null ? '' : String(value) });
		nodes.push(E('div', { 'class': 'battery-control' }, [
			E('div', { 'class': 'battery-control-copy' }, [ E('label', {}, label), E('small', {}, description) ]), E('div', {}, [ input, E('span', {}, unit || ''), E('button', { 'class': 'btn cbi-button-action', 'click': function() {
				var requested = Number(input.value); if (!isFinite(requested)) return;
				if (divisor) requested = Math.round(requested * divisor);
				if (warning && !window.confirm(warning)) return;
				apply(key, requested);
			} }, _('应用')) ])
		]));
	}
	function addToggle(key, label, description, warning) {
		var ctl = controls[key], enabled, button;
		if (!ctl || !ctl.writable) return;
		enabled = String(ctl.value) === '1';
		button = E('button', {
			'type': 'button', 'class': 'battery-switch ' + (enabled ? 'is-on' : 'is-off'),
			'aria-pressed': enabled ? 'true' : 'false', 'click': function() {
				if (!enabled && warning && !window.confirm(warning)) return;
				button.disabled = true;
				apply(key, enabled ? '0' : '1', function() { button.disabled = false; });
			}
		}, [
			E('span', { 'class': 'battery-switch-option option-off ' + (!enabled ? 'is-active' : '') }, _('关闭')),
			E('span', { 'class': 'battery-switch-option option-on ' + (enabled ? 'is-active' : '') }, _('开启'))
		]);
		nodes.push(E('div', { 'class': 'battery-control toggle' }, [
			E('div', { 'class': 'battery-control-copy' }, [ E('label', {}, label), E('small', {}, description) ]), button
		]));
	}
	addToggle('reverse_charging', _('反向充电'), _('通过 Type-C 接口为外接设备供电；系统不会自动开启。'), _('开启反向充电会增加耗电和发热，并可能使当前 USB 网络连接中断。确认开启吗？'));
	addToggle('night_charging', _('夜间充电保护'), _('夜间充电保护默认关闭。'));
	addToggle('charging_enabled', _('允许充电'), _('控制电池是否接受充电输入。'), _('关闭充电可能导致设备仅由电池供电。确认继续吗？'));
	addToggle('input_suspended', _('暂停充电输入'), _('立即暂停 USB 供电输入。'), _('暂停输入会停止充电，并可能影响当前 USB 连接。确认继续吗？'));
	addNumber('charge_start_threshold', _('开始充电阈值'), _('电量低于此百分比后允许开始充电。'), '%', 1, 40, 100);
	addNumber('charge_end_threshold', _('停止充电阈值'), _('电量达到此百分比后停止继续充电。'), '%', 1, 40, 100);
	addNumber('input_current_limit', _('输入电流上限'), _('限制充电器输入电流；过低会降低充电速度。'), 'A', 1000000, 0, 20, _('修改输入电流可能影响充电速度和稳定性。确认应用吗？'));
	addNumber('constant_charge_current_max', _('最大充电电流'), _('限制电池恒流充电阶段的最大电流。'), 'A', 1000000, 0, 20, _('修改最大充电电流可能影响温度和充电速度。确认应用吗？'));
	return section(_('电池与充电设置'), nodes.length ? nodes : [ E('div', { 'class': 'battery-empty compact' }, _('当前内核驱动没有提供可安全写入的电池控制项。')) ], 'battery-controls');
}

function renderStatus(data, root) {
	var battery = pickBattery(data), input = supplyNamed(data, 'usb') || firstSupply(data, [ 'usb', 'usb-c', 'mains', 'wireless' ]), ucsi = firstSupply(data, [ 'usb' ]), level = battery ? num(battery.capacity) : null;
	var xiaomi = data.xiaomi || {}, typec = data.typec || {};
	var temp = temperature(data, battery), warning = Number(uci.get('xm14battery', 'main', 'temperature_warning') || 45), critical = Number(uci.get('xm14battery', 'main', 'temperature_critical') || 55);
	var tempTone = temp != null && temp >= critical ? 'bad' : temp != null && temp >= warning ? 'warn' : 'good';
	if (!battery) {
		root.replaceChildren(E('div', { 'class': 'battery-page' }, [
			E('div', { 'class': 'battery-unavailable' }, [ E('div', { 'class': 'battery-unavailable-icon' }, '!'), E('strong', {}, _('Battery driver is not registered')), E('p', {}, _('The Xiaomi PMIC battery channel is not available yet. No battery value is being simulated.')) ]),
			section(_('Detected power components'), (data.supplies || []).map(function(s) { return row(s.name, s.type); }) || [])
		]));
		return;
	}
	level = level == null ? 0 : Math.max(0, Math.min(100, level));
	var full = num(battery.charge_full), design = num(battery.charge_full_design), capacityRetention = full != null && design > 0 ? Math.round(full * 100 / design) : null;
	var healthPercent = num(xiaomi.soh), status = String(battery.status || '').toLowerCase();
	if (healthPercent == null || healthPercent < 0 || healthPercent > 100) healthPercent = null;
	var power = watts(battery.power_now);
	if (!power && num(battery.voltage_now) != null && num(battery.current_now) != null) power = ((num(battery.voltage_now) / 1000000) * (num(battery.current_now) / 1000000)).toFixed(2) + ' W';
	var inputPower = num(input && input.voltage_now) != null && num(input && input.current_now) != null ? ((num(input.voltage_now) / 1000000) * (num(input.current_now) / 1000000)).toFixed(2) + ' W' : null;
	var protocol = xiaomi.usb_real_type || xiaomi.real_type || (input && input.type);
	var connectorTemp = tempC(xiaomi.connector_temp);
	var inputOnline = input && String(input.online) === '1';
	var fastCharging = inputOnline && ((num(xiaomi.fastchg_mode) || 0) > 0 || (num(xiaomi.quick_charge_type) || 0) > 0);
	var chargePower = inputOnline ? inputPower : null;
	var refresh = function() { run('status').then(function(next) { renderStatus(next, root); }).catch(function(e) { notice(e.message, true); }); };
	root.replaceChildren(E('div', { 'class': 'battery-page' }, [
		E('section', { 'class': 'battery-hero' }, [
			E('div', { 'class': 'battery-ring', 'style': '--level:' + level }, E('div', {}, [ E('strong', {}, level + '%'), E('span', {}, stateText(battery.status)) ])),
			E('div', { 'class': 'battery-hero-copy' }, [ E('small', {}, _('电源系统')), E('h1', {}, stateText(battery.status)), E('p', {}, [ stateText(battery.health), ' · ', stateText(battery.technology), protocol ? ' · ' + text(protocol) : '' ]) ]),
			E('button', { 'class': 'btn', 'click': refresh }, _('刷新'))
		]),
		E('div', { 'class': 'battery-metrics' }, [
			metric(_('Battery temperature'), temp == null ? null : temp.toFixed(1) + '°C', tempTone === 'good' ? _('Normal') : tempTone === 'warn' ? _('Warm') : _('Hot'), tempTone),
			metric(_('Voltage'), volts(battery.voltage_now), _('Live voltage')),
			metric(_('Current'), amps(battery.current_now), status.indexOf('charg') >= 0 && status.indexOf('dis') < 0 ? _('Charging') : status.indexOf('dis') >= 0 ? _('Discharging') : battery.status),
			metric(_('当前充电功率'), chargePower, inputOnline ? (protocol || _('充电器已连接')) : _('未连接充电器'), inputOnline ? 'good' : ''),
			metric(_('充电模式'), inputOnline ? (protocol || _('未识别协议')) : _('未充电'), _('驱动实时识别')),
			metric(_('快充状态'), inputOnline ? (fastCharging ? _('已激活') : _('待机')) : _('未充电'), _('充电控制器实时状态'), fastCharging ? 'good' : ''),
			metric(_('电池侧功率'), power, _('Kernel-reported power')),
			metric(_('Battery health'), healthPercent == null ? stateText(battery.health) : healthPercent + '%', healthPercent == null ? _('Kernel health state') : _('Fuel-gauge state of health')),
			metric(_('Cycle count'), battery.cycle_count, _('Kernel-reported cycles'))
		]),
		E('div', { 'class': 'battery-grid' }, [
			section(_('Capacity and lifetime'), [ row(_('Current charge'), capacity(battery.charge_now)), row(_('Full charge capacity'), capacity(battery.charge_full)), row(_('Design capacity'), capacity(battery.charge_full_design)), row(_('Full versus design'), capacityRetention == null ? null : capacityRetention + '%'), row(_('Fuel-gauge SOH'), healthPercent == null ? null : healthPercent + '%'), row(_('Time remaining'), xiaomi.remaining_time) ]),
			section(_('Charging input'), [ row(_('Protocol'), protocol), row(_('UCSI source'), ucsi && (ucsi.usb_type || ucsi.type)), row(_('Online'), input && boolText(input.online, _('在线'), _('离线'))), row(_('Input voltage'), input && volts(input.voltage_now)), row(_('Input current'), input && amps(input.current_now)), row(_('Input power'), inputPower), row(_('Current limit'), input && amps(input.input_current_limit)), row(_('Charge type'), stateText(battery.charge_type)) ]),
			section(_('Type-C connection'), [ row(_('Mode'), xiaomi.typec_mode), row(_('Current state'), xiaomi.current_state), row(_('Data role'), typec.data_role), row(_('Power role'), typec.power_role), row(_('Power mode'), typec.power_operation_mode), row(_('PD revision'), typec.usb_power_delivery_revision), row(_('Type-C revision'), typec.usb_typec_revision), row(_('Cable orientation'), xiaomi.cc_orientation) ]),
			section(_('Fast charging'), [ row(_('当前充电功率'), chargePower), row(_('充电模式'), inputOnline ? (protocol || _('未识别协议')) : _('未充电')), row(_('快充状态'), inputOnline ? (fastCharging ? _('已激活') : _('待机')) : _('未充电')), row(_('Fast-charge mode'), xiaomi.fastchg_mode), row(_('Quick-charge type'), xiaomi.quick_charge_type), row(_('PD verified'), xiaomi.pd_verifed), row(_('APDO maximum'), xiaomi.apdo_max), row(_('Power maximum'), xiaomi.power_max), row(_('Adapter ID'), xiaomi.adapter_id), row(_('Adapter SVID'), xiaomi.adapter_svid) ]),
			section(_('Battery identity'), [ row(_('Model'), battery.model_name), row(_('Battery device'), xiaomi.batt_device_name), row(_('Cell vendor'), xiaomi.cell_vendor), row(_('Pack vendor'), xiaomi.pack_vendor_str || xiaomi.pack_vendor), row(_('Technology'), stateText(battery.technology)), row(_('Health'), stateText(battery.health)), row(_('Present'), boolText(battery.present, _('已安装'), _('未安装'))) ]),
			section(_('供电与保护状态'), [ row(_('反向充电支持'), boolText(xiaomi.otg_ui_support, _('支持'), _('不支持'))), row(_('反向充电'), boolText(xiaomi.reverse_chg_mode)), row(_('反向供电状态'), boolText(xiaomi.reverse_chg_state, _('正在供电'), _('未供电'))), row(_('夜间充电保护'), boolText(xiaomi.night_charging)), row(_('USB 输入'), boolText(xiaomi.input_suspend, _('已暂停'), _('正常'))), row(_('智能充电模式'), modeText(xiaomi.smart_chg)), row(_('充电控制'), boolText(xiaomi.charge_control_en)), row(_('低功耗检测保护'), boolText(xiaomi.lpd_control)), row(_('低功耗充电状态'), boolText(xiaomi.lpd_charging, _('充电中'), _('未启用'))) ]),
			section(_('Charging safety'), [ row(_('Connector temperature'), connectorTemp == null ? null : connectorTemp.toFixed(1) + '°C'), row(_('进水检测'), boolText(xiaomi.moisture_detection_en)), row(_('Moisture status'), boolText(xiaomi.moisture_detection_status, _('检测到异常'), _('正常'))), row(_('Anti-burn state'), boolText(xiaomi.anti_burn, _('保护中'), _('正常'))), row(_('CC short-to-VBUS'), boolText(xiaomi.cc_short_vbus, _('异常'), _('正常'))), row(_('Battery connection'), boolText(xiaomi.battcont_online, _('正常'), _('未连接'))), row(_('Charge-pump primary'), boolText(xiaomi.bq2597x_chip_ok, _('正常'), _('异常'))), row(_('Charge-pump secondary'), boolText(xiaomi.bq2597x_slave_chip_ok, _('正常'), _('异常'))) ]),
			section(_('Thermal sensors'), (data.thermals || []).map(function(t) { var value = tempC(t.temp); return row(t.name, value == null ? null : value.toFixed(1) + '°C'); }))
		]),
		controlCard(data, refresh)
	]));
}

return view.extend({
	load: function() { loadStyle(); return Promise.all([ uci.load('xm14battery'), run('status') ]); },
	render: function(data) {
		var root = E('div');
		renderStatus(data[1], root);
		var interval = Math.max(2, Math.min(30, Number(uci.get('xm14battery', 'main', 'refresh_interval') || 3)));
		poll.add(function() { return run('status').then(function(status) { renderStatus(status, root); }); }, interval);
		return root;
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});

