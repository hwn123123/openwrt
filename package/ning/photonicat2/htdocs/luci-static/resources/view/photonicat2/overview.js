'use strict';
'require view';
'require poll';
'require photonicat2.common as pcat';

var HISTORY_SIZE = 40;

function deep(obj, path, fallback) {
	for (var i = 0; obj != null && i < path.length; i++)
		obj = obj[path[i]];
	return obj == null ? fallback : obj;
}

function numeric(value, fallback) {
	value = Number(value);
	return isFinite(value) ? value : (fallback == null ? 0 : fallback);
}

function clamp(value, min, max) {
	return Math.max(min, Math.min(max, numeric(value)));
}

function liveValue(key, value) {
	return E('span', { 'data-value': key }, value == null ? '—' : String(value));
}

function liveMetric(label, key, value, unit, cls) {
	return pcat.metric(label, liveValue(key, value), unit, cls);
}

function formatBytes(bytes, rate) {
	var value = Math.max(0, numeric(bytes));
	var units = [ 'B', 'KB', 'MB', 'GB', 'TB' ];
	var index = 0;
	while (value >= 1024 && index < units.length - 1) {
		value /= 1024;
		index++;
	}
	return (value >= 100 || index === 0 ? value.toFixed(0) : value >= 10 ? value.toFixed(1) : value.toFixed(2)) + ' ' + units[index] + (rate ? '/s' : '');
}

function formatCapacity(mb) {
	mb = numeric(mb, NaN);
	if (!isFinite(mb))
		return '—';
	return mb >= 1024 ? (mb / 1024).toFixed(mb >= 10240 ? 1 : 2) + ' GB' : mb.toFixed(0) + ' MB';
}

function formatDuration(seconds) {
	seconds = Math.max(0, numeric(seconds));
	var days = Math.floor(seconds / 86400);
	var hours = Math.floor(seconds % 86400 / 3600);
	var mins = Math.floor(seconds % 3600 / 60);
	return (days ? days + '天 ' : '') + hours + '小时 ' + mins + '分';
}

function formatMinutes(minutes) {
	minutes = numeric(minutes, NaN);
	if (!isFinite(minutes) || minutes < 0)
		return '—';
	return Math.floor(minutes / 60) + '小时 ' + Math.round(minutes % 60) + '分';
}

function batteryStatus(status, charging) {
	var labels = { Charging: '充电中', Discharging: '放电中', Full: '已充满', 'Not charging': '未充电', Unknown: '未知' };
	return labels[status] || (charging ? '充电中' : (status || '—'));
}

function fanMode(mode) {
	var labels = { smart_cool: '智能冷却', smart_quiet: '智能静音', manual: '手动', off: '关闭' };
	return labels[mode] || mode || '—';
}

function boolText(value) {
	return value === true || value === 1 || value === '1' ? '已启用' : value === false || value === 0 || value === '0' ? '已关闭' : '—';
}

function zoneName(name) {
	var labels = {
		'package-thermal': '芯片封装',
		'bigcore-thermal': 'CPU 大核',
		'littlecore-thermal': 'CPU 小核',
		'gpu-thermal': 'GPU',
		'npu-thermal': 'NPU',
		'ddr-thermal': 'DDR 内存'
	};
	return labels[name] || name || '温度传感器';
}

function progressRow(label, id, value, color) {
	return E('div', { 'class': 'pcat-progress-row', 'data-progress': id }, [
		E('div', { 'class': 'pcat-progress-head' }, [
			E('span', {}, label),
			E('strong', {}, [ liveValue(id + '_progress', pcat.format(value, 0)), E('small', {}, ' %') ])
		]),
		E('div', { 'class': 'pcat-progress-track' }, [
			E('i', { 'style': 'width:%s%%;background:%s'.format(clamp(value, 0, 100), color || 'var(--pcat-accent)') })
		])
	]);
}

function chartCard(id, title, subtitle, series) {
	var grid = [];
	for (var i = 0; i < 5; i++)
		grid.push(E('line', { 'x1': '10', 'x2': '590', 'y1': String(10 + i * 37.5), 'y2': String(10 + i * 37.5) }));

	var paths = [];
	series.forEach(function(item) {
		paths.push(E('path', { 'class': 'pcat-live-chart-area', 'data-chart-area': item.key, 'style': 'fill:' + item.color }));
		paths.push(E('path', { 'class': 'pcat-live-chart-line', 'data-chart-line': item.key, 'style': 'stroke:' + item.color }));
		paths.push(E('circle', { 'class': 'pcat-live-chart-dot', 'data-chart-dot': item.key, 'r': '4', 'style': 'fill:' + item.color }));
	});

	return E('div', { 'class': 'pcat-card pcat-live-chart-card', 'data-chart': id }, [
		E('div', { 'class': 'pcat-chart-heading' }, [
			E('div', {}, [ E('h3', {}, title), E('span', {}, subtitle) ]),
			E('span', { 'class': 'pcat-live-badge' }, [ E('i'), '实时' ])
		]),
		E('div', { 'class': 'pcat-chart-legend' }, series.map(function(item) {
			return E('span', {}, [ E('i', { 'style': 'background:' + item.color }), item.label + ' ', E('strong', { 'data-value': item.valueKey }, '—') ]);
		})),
		E('svg', { 'class': 'pcat-live-chart', 'viewBox': '0 0 600 170', 'preserveAspectRatio': 'none', 'aria-hidden': 'true' }, [
			E('g', { 'class': 'pcat-live-chart-grid' }, grid)
		].concat(paths)),
		E('div', { 'class': 'pcat-chart-scale' }, [
			E('span', { 'data-chart-min': id }, '0'),
			E('span', {}, '最近约 2 分钟'),
			E('span', { 'data-chart-max': id }, '100')
		])
	]);
}

function sectionTitle(icon, title, text) {
	return E('div', { 'class': 'pcat-section-title' }, [
		E('span', { 'class': 'pcat-section-icon' }, icon),
		E('div', {}, [ E('h3', {}, title), E('p', {}, text) ])
	]);
}

function stateChip(id, ok, text) {
	return E('span', { 'class': 'pcat-status-chip ' + (ok ? 'is-ok' : 'is-off'), 'data-state': id }, [
		E('i'), E('span', { 'data-state-text': id }, text)
	]);
}

function thermalZones(st) {
	var zones = deep(st, [ 'temperature', 'zones' ], []);
	return E('div', { 'class': 'pcat-thermal-grid' }, zones.map(function(zone, index) {
		var value = numeric(zone.temperature_c);
		return E('div', { 'class': 'pcat-thermal-sensor', 'data-zone': String(index) }, [
			E('span', {}, zoneName(zone.name)),
			E('strong', {}, [ E('span', { 'data-zone-value': String(index) }, pcat.format(value, 1)), E('small', {}, ' °C') ]),
			E('i', { 'style': 'height:%s%%'.format(clamp(value / 90 * 100, 3, 100)) })
		]);
	}));
}

return view.extend({
	setValue: function(root, key, value) {
		var nodes = root.querySelectorAll('[data-value="%s"]'.format(key));
		value = value == null || value === '' ? '—' : String(value);
		for (var i = 0; i < nodes.length; i++) {
			if (nodes[i].textContent === value)
				continue;
			nodes[i].textContent = value;
			nodes[i].classList.remove('pcat-value-changed');
			void nodes[i].offsetWidth;
			nodes[i].classList.add('pcat-value-changed');
		}
	},

	setState: function(root, id, ok, text) {
		var chip = root.querySelector('[data-state="%s"]'.format(id));
		if (!chip)
			return;
		chip.className = 'pcat-status-chip ' + (ok ? 'is-ok' : 'is-off');
		var label = chip.querySelector('[data-state-text="%s"]'.format(id));
		if (label)
			label.textContent = text;
	},

	setProgress: function(root, id, value) {
		var node = root.querySelector('[data-progress="%s"]'.format(id));
		if (node) {
			var fill = node.querySelector('.pcat-progress-track i');
			if (fill)
				fill.style.width = clamp(value, 0, 100) + '%';
		}
		this.setValue(root, id + '_progress', pcat.format(value, 0));
	},

	pushHistory: function(key, value) {
		if (!this.history[key])
			this.history[key] = [];
		this.history[key].push(numeric(value));
		if (this.history[key].length > HISTORY_SIZE)
			this.history[key].shift();
	},

	networkRates: function(st) {
		var now = Date.now();
		var device = deep(st, [ 'network', 'device' ], '');
		var rx = numeric(deep(st, [ 'network', 'rx_bytes' ]));
		var tx = numeric(deep(st, [ 'network', 'tx_bytes' ]));
		var rates = { rx: 0, tx: 0 };
		if (this.previousNetwork && this.previousNetwork.device === device) {
			var elapsed = Math.max(.1, (now - this.previousNetwork.time) / 1000);
			rates.rx = Math.max(0, (rx - this.previousNetwork.rx) / elapsed);
			rates.tx = Math.max(0, (tx - this.previousNetwork.tx) / elapsed);
		}
		this.previousNetwork = { device: device, rx: rx, tx: tx, time: now };
		return rates;
	},

	drawChart: function(root, id, keys, min, max, bytesScale) {
		var card = root.querySelector('[data-chart="%s"]'.format(id));
		if (!card)
			return;
		if (max == null) {
			max = 1;
			keys.forEach(L.bind(function(key) {
				(this.history[key] || []).forEach(function(value) { max = Math.max(max, value); });
			}, this));
			max *= 1.15;
		}
		max = Math.max(max, min + 1);
		var width = 580;
		var height = 150;
		var left = 10;
		var top = 10;
		keys.forEach(L.bind(function(key) {
			var points = (this.history[key] || []).map(function(value, index) {
				return [ left + index / (HISTORY_SIZE - 1) * width, top + height - clamp((value - min) / (max - min), 0, 1) * height ];
			});
			var line = card.querySelector('[data-chart-line="%s"]'.format(key));
			var area = card.querySelector('[data-chart-area="%s"]'.format(key));
			var dot = card.querySelector('[data-chart-dot="%s"]'.format(key));
			var d = points.map(function(point, index) { return (index ? 'L' : 'M') + point[0].toFixed(1) + ' ' + point[1].toFixed(1); }).join(' ');
			if (line)
				line.setAttribute('d', d);
			if (area)
				area.setAttribute('d', points.length ? d + ' L' + points[points.length - 1][0].toFixed(1) + ' 160 L10 160 Z' : '');
			if (dot && points.length) {
				dot.setAttribute('cx', points[points.length - 1][0]);
				dot.setAttribute('cy', points[points.length - 1][1]);
			}
		}, this));
		var minNode = card.querySelector('[data-chart-min="%s"]'.format(id));
		var maxNode = card.querySelector('[data-chart-max="%s"]'.format(id));
		if (minNode)
			minNode.textContent = bytesScale ? formatBytes(min, true) : pcat.format(min, 0);
		if (maxNode)
			maxNode.textContent = bytesScale ? formatBytes(max, true) : pcat.format(max, 0);
	},

	update: function(st) {
		var root = document.getElementById('pcat-overview');
		if (!root)
			return;
		var battery = deep(st, [ 'battery' ], {});
		var info = battery.info || {};
		var cpu = deep(st, [ 'cpu' ], {});
		var memory = deep(st, [ 'memory' ], {});
		var temperature = deep(st, [ 'temperature' ], {});
		var fan = deep(st, [ 'fan' ], {});
		var network = deep(st, [ 'network' ], {});
		var storage = deep(st, [ 'storage' ], {});
		var rates = this.networkRates(st);

		pcat.updateGauge(root, 'battery', battery.percent, 100);
		pcat.updateGauge(root, 'cpu', cpu.usage, 100);
		pcat.updateGauge(root, 'temp', temperature.cpu, 90);
		pcat.updateGauge(root, 'memory', memory.used_percent, 100);

		var values = {
			firmware: deep(st, [ 'pmu', 'firmware' ], '—'),
			updated_at: st.timestamp ? new Date(st.timestamp * 1000).toLocaleTimeString() : '—',
			battery_status: batteryStatus(battery.status, battery.charging),
			battery_voltage: pcat.format(battery.voltage, 2), battery_current: pcat.format(battery.current, 2),
			battery_power: pcat.format(Math.abs(numeric(battery.power)), 2), battery_threshold: pcat.format(battery.threshold, 0),
			battery_health: pcat.format(info['health-pct'], 0), battery_cycles: pcat.format(info['cycle-count'], 0),
			battery_full: pcat.format(info['full-capacity'], 0), battery_nominal: pcat.format(info['nominal-capacity'], 0),
			battery_remaining: pcat.format(info['remain-mah'], 0), battery_resistance: pcat.format(info['internal-resistance-mohm'], 0),
			battery_eta: formatMinutes(info['minutes-to-full']),
			battery_energy_in: pcat.format(numeric(info['charged-mwh']) / 1000, 2), battery_energy_out: pcat.format(numeric(info['consumed-mwh']) / 1000, 2),
			cpu_usage_chart: pcat.format(cpu.usage, 1) + '%', memory_chart: pcat.format(memory.used_percent, 1) + '%',
			cpu_temp_chart: pcat.format(temperature.cpu, 1) + '°C', board_temp_chart: pcat.format(temperature.board, 1) + '°C',
			net_rx_chart: formatBytes(rates.rx, true), net_tx_chart: formatBytes(rates.tx, true),
			cpu_freq: pcat.format(cpu.frequency_mhz, 0), cpu_freq_max: pcat.format(cpu.max_frequency_mhz, 0),
			cpu_cores: pcat.format(cpu.cores_online, 0) + ' / ' + pcat.format(cpu.cores_total, 0),
			cpu_load: (cpu.load || []).map(function(v) { return pcat.format(v, 2); }).join(' / ') || '—',
			cpu_governor: cpu.governor || '—',
			cpu_power_mode: cpu.powersave_active ? '节能运行中' : (cpu.powersave_configured ? '节能已配置' : '正常模式'),
			memory_used: formatCapacity(memory.used_mb), memory_total: formatCapacity(memory.total_mb),
			memory_available: formatCapacity(memory.available_mb), swap_used: formatCapacity(memory.swap_used_mb),
			cpu_temp: pcat.format(temperature.cpu, 1), board_temp: pcat.format(temperature.board, 1),
			fan_rpm: pcat.format(fan.rpm, 0), fan_level: pcat.format(fan.level, 0) + ' / ' + pcat.format(fan.max, 0), fan_mode: fanMode(fan.mode),
			root_used: formatCapacity(deep(storage, [ 'root', 'used_mb' ])), root_total: formatCapacity(deep(storage, [ 'root', 'total_mb' ])),
			overlay_free: formatCapacity(deep(storage, [ 'overlay', 'free_mb' ])),
			tmp_used: formatCapacity(deep(storage, [ 'tmp', 'used_mb' ])), tmp_total: formatCapacity(deep(storage, [ 'tmp', 'total_mb' ])),
			net_device: network.device || '—', net_ipv4: network.ipv4 || '—', lan_ipv4: network.lan_ipv4 || '—', net_gateway: network.gateway || '—',
			net_rx_rate: formatBytes(rates.rx, true), net_tx_rate: formatBytes(rates.tx, true),
			net_rx_total: formatBytes(network.rx_bytes), net_tx_total: formatBytes(network.tx_bytes),
			net_interfaces: pcat.format(network.interfaces_up, 0) + ' / ' + pcat.format(network.interfaces_total, 0),
			net_errors: pcat.format(numeric(network.rx_errors) + numeric(network.tx_errors), 0),
			hostname: deep(st, [ 'system', 'hostname' ], '—'), model: deep(st, [ 'system', 'model' ], '—'),
			board_name: deep(st, [ 'system', 'board_name' ], '—'), architecture: deep(st, [ 'system', 'architecture' ], '—'),
			kernel: deep(st, [ 'system', 'kernel' ], '—'), os_firmware: deep(st, [ 'system', 'firmware' ], '—'),
			revision: deep(st, [ 'system', 'revision' ], '—'), target: deep(st, [ 'system', 'target' ], '—'), rootfs: deep(st, [ 'system', 'rootfs' ], '—'),
			uptime: formatDuration(st.uptime), led_state: boolText(deep(st, [ 'io', 'led' ])), beeper_state: boolText(deep(st, [ 'io', 'beeper' ]))
		};

		Object.keys(values).forEach(L.bind(function(key) { this.setValue(root, key, values[key]); }, this));
		this.setState(root, 'pmu', deep(st, [ 'pmu', 'connected' ], false), deep(st, [ 'pmu', 'connected' ], false) ? '硬件服务在线' : '硬件服务离线');
		this.setState(root, 'network', network.state === 'up', network.state === 'up' ? '上联网卡在线' : '上联网卡离线');
		this.setProgress(root, 'battery_level', battery.percent);
		this.setProgress(root, 'battery_health_bar', info['health-pct']);
		this.setProgress(root, 'memory_bar', memory.used_percent);
		this.setProgress(root, 'storage_bar', deep(storage, [ 'root', 'used_percent' ]));

		var flow = root.querySelector('[data-energy-flow]');
		if (flow) {
			var power = Math.abs(numeric(battery.power));
			flow.className = 'pcat-energy-flow ' + (power < .05 ? 'is-idle' : battery.charging ? 'is-charging' : 'is-discharging');
			flow.style.setProperty('--flow-speed', clamp(3.2 - power / 5, .55, 3.2).toFixed(2) + 's');
		}
		var fanIcon = root.querySelector('[data-live-fan]');
		if (fanIcon) {
			var intensity = Math.max(numeric(fan.rpm) / 1000, numeric(fan.level));
			fanIcon.className = 'pcat-live-fan' + (intensity > 0 ? '' : ' is-stopped');
			fanIcon.style.animationDuration = clamp(2.4 - intensity * .25, .35, 2.4).toFixed(2) + 's';
		}
		var netFlow = root.querySelector('[data-network-flow]');
		if (netFlow) {
			var traffic = rates.rx + rates.tx;
			netFlow.className = 'pcat-network-flow' + (traffic > 8 ? ' is-active' : '');
			netFlow.style.setProperty('--network-speed', clamp(2.5 - Math.log(traffic + 1) / 6, .35, 2.5).toFixed(2) + 's');
		}

		(temperature.zones || []).forEach(function(zone, index) {
			var node = root.querySelector('[data-zone="%s"]'.format(index));
			if (!node)
				return;
			var temp = numeric(zone.temperature_c);
			var valueNode = node.querySelector('[data-zone-value="%s"]'.format(index));
			var level = node.querySelector('i');
			if (valueNode)
				valueNode.textContent = pcat.format(temp, 1);
			if (level)
				level.style.height = clamp(temp / 90 * 100, 3, 100) + '%';
			node.className = 'pcat-thermal-sensor ' + (temp >= 75 ? 'is-hot' : temp >= 60 ? 'is-warm' : 'is-normal');
		});

		this.pushHistory('cpu_usage', cpu.usage);
		this.pushHistory('memory_usage', memory.used_percent);
		this.pushHistory('cpu_temperature', temperature.cpu);
		this.pushHistory('board_temperature', temperature.board);
		this.pushHistory('network_rx', rates.rx);
		this.pushHistory('network_tx', rates.tx);
		this.drawChart(root, 'resources', [ 'cpu_usage', 'memory_usage' ], 0, 100, false);
		this.drawChart(root, 'temperatures', [ 'cpu_temperature', 'board_temperature' ], 20, 90, false);
		this.drawChart(root, 'network', [ 'network_rx', 'network_tx' ], 0, null, true);
	},

	load: function() {
		return L.resolveDefault(pcat.call('status'), {});
	},

	render: function(st) {
		pcat.loadStyle();
		this.history = {};
		this.previousNetwork = null;
		var battery = deep(st, [ 'battery' ], {});
		var info = battery.info || {};
		var cpu = deep(st, [ 'cpu' ], {});
		var memory = deep(st, [ 'memory' ], {});
		var temperature = deep(st, [ 'temperature' ], {});
		var fan = deep(st, [ 'fan' ], {});
		var network = deep(st, [ 'network' ], {});
		var storage = deep(st, [ 'storage' ], {});

		var page = E('div', { 'class': 'pcat-page pcat-dashboard', 'id': 'pcat-overview' }, [
			E('div', { 'class': 'pcat-dashboard-header' }, [
				E('div', {}, [
					E('h2', {}, 'Photonicat 2 硬件总览'),
					E('p', { 'class': 'pcat-subtitle' }, [ liveValue('model', deep(st, [ 'system', 'model' ], 'Photonicat 2')), ' · 全部数据来自 PMU、内核与硬件传感器，本页仅查看' ])
				]),
				E('div', { 'class': 'pcat-header-status' }, [
					stateChip('pmu', deep(st, [ 'pmu', 'connected' ], false), deep(st, [ 'pmu', 'connected' ], false) ? '硬件服务在线' : '硬件服务离线'),
					stateChip('network', network.state === 'up', network.state === 'up' ? '上联网卡在线' : '上联网卡离线'),
					E('span', { 'class': 'pcat-update-time' }, [ '更新 ', liveValue('updated_at', '—') ])
				])
			]),

			E('div', { 'class': 'pcat-card pcat-summary-card' }, [
				E('div', { 'class': 'pcat-gauges' }, [
					pcat.gauge('battery', '电池电量', battery.percent, '%', 100, '#19cf8b'),
					pcat.gauge('cpu', 'CPU 实时使用', cpu.usage, '%', 100, '#35a7ff'),
					pcat.gauge('temp', 'CPU 温度', temperature.cpu, '°C', 90, '#ffae3d'),
					pcat.gauge('memory', '内存使用', memory.used_percent, '%', 100, '#9b7cff')
				]),
				E('div', { 'class': 'pcat-summary-strip' }, [
					E('div', { 'class': 'pcat-energy-flow', 'data-energy-flow': '' }, [ E('i'), E('i'), E('i'), E('i'), E('i') ]),
					E('span', {}, [ '实时功率 ', E('strong', {}, [ liveValue('battery_power', pcat.format(Math.abs(numeric(battery.power)), 2)), ' W' ]) ]),
					E('span', {}, [ 'CPU ', E('strong', {}, [ liveValue('cpu_freq', pcat.format(cpu.frequency_mhz, 0)), ' MHz' ]) ]),
					E('span', {}, [ '风扇 ', E('strong', {}, [ liveValue('fan_rpm', pcat.format(fan.rpm, 0)), ' RPM' ]) ]),
					E('span', {}, [ 'PMU ', E('strong', {}, liveValue('firmware', deep(st, [ 'pmu', 'firmware' ], '—'))) ])
				])
			]),

			sectionTitle('⌁', '实时变化趋势', '每 3 秒读取一次真实传感器与内核计数，曲线仅保存在当前浏览器页面'),
			E('div', { 'class': 'pcat-chart-grid' }, [
				chartCard('resources', 'CPU 与内存', '系统资源实时占用', [
					{ key: 'cpu_usage', label: 'CPU', valueKey: 'cpu_usage_chart', color: '#35a7ff' },
					{ key: 'memory_usage', label: '内存', valueKey: 'memory_chart', color: '#9b7cff' }
				]),
				chartCard('temperatures', '硬件温度', 'CPU 与主板传感器', [
					{ key: 'cpu_temperature', label: 'CPU', valueKey: 'cpu_temp_chart', color: '#ffae3d' },
					{ key: 'board_temperature', label: '主板', valueKey: 'board_temp_chart', color: '#19cf8b' }
				]),
				chartCard('network', '实时网络吞吐', '活动上联网卡的内核计数', [
					{ key: 'network_rx', label: '下载', valueKey: 'net_rx_chart', color: '#19cf8b' },
					{ key: 'network_tx', label: '上传', valueKey: 'net_tx_chart', color: '#35a7ff' }
				])
			]),

			sectionTitle('⚡', '电源与电池', 'PMU 实时供电状态、电芯容量与健康数据'),
			E('div', { 'class': 'pcat-grid pcat-detail-grid' }, [
				E('div', { 'class': 'pcat-card pcat-detail-card' }, [
					E('h3', {}, '实时供电'),
					E('div', { 'class': 'pcat-metrics' }, [
						liveMetric('当前状态', 'battery_status', batteryStatus(battery.status, battery.charging)),
						liveMetric('电池电压', 'battery_voltage', pcat.format(battery.voltage, 2), ' V'),
						liveMetric('充放电电流', 'battery_current', pcat.format(battery.current, 2), ' A'),
						liveMetric('实时功率', 'battery_power', pcat.format(Math.abs(numeric(battery.power)), 2), ' W'),
						liveMetric('充电上限', 'battery_threshold', pcat.format(battery.threshold, 0), ' %'),
						liveMetric('预计充满', 'battery_eta', formatMinutes(info['minutes-to-full']))
					]),
					progressRow('当前电量', 'battery_level', battery.percent, '#19cf8b'),
					progressRow('电池健康度', 'battery_health_bar', info['health-pct'], '#35a7ff')
				]),
				E('div', { 'class': 'pcat-card pcat-detail-card' }, [
					E('h3', {}, '电池统计'),
					E('div', { 'class': 'pcat-metrics' }, [
						liveMetric('健康度', 'battery_health', pcat.format(info['health-pct'], 0), ' %'),
						liveMetric('循环次数', 'battery_cycles', pcat.format(info['cycle-count'], 0), ' 次'),
						liveMetric('当前满充容量', 'battery_full', pcat.format(info['full-capacity'], 0), ' mAh'),
						liveMetric('设计容量', 'battery_nominal', pcat.format(info['nominal-capacity'], 0), ' mAh'),
						liveMetric('剩余容量', 'battery_remaining', pcat.format(info['remain-mah'], 0), ' mAh'),
						liveMetric('内阻', 'battery_resistance', pcat.format(info['internal-resistance-mohm'], 0), ' mΩ'),
						liveMetric('累计充入', 'battery_energy_in', pcat.format(numeric(info['charged-mwh']) / 1000, 2), ' Wh'),
						liveMetric('累计消耗', 'battery_energy_out', pcat.format(numeric(info['consumed-mwh']) / 1000, 2), ' Wh')
					])
				])
			]),

			sectionTitle('◫', '算力、内存与温控', 'CPU 调频、系统负载、内存和全部热区传感器'),
			E('div', { 'class': 'pcat-grid pcat-detail-grid' }, [
				E('div', { 'class': 'pcat-card pcat-detail-card' }, [
					E('h3', {}, 'CPU 与内存'),
					E('div', { 'class': 'pcat-metrics' }, [
						liveMetric('实时频率', 'cpu_freq', pcat.format(cpu.frequency_mhz, 0), ' MHz'),
						liveMetric('最高频率', 'cpu_freq_max', pcat.format(cpu.max_frequency_mhz, 0), ' MHz'),
						liveMetric('在线核心', 'cpu_cores', pcat.format(cpu.cores_online, 0) + ' / ' + pcat.format(cpu.cores_total, 0)),
						liveMetric('1 / 5 / 15 分钟负载', 'cpu_load', (cpu.load || []).join(' / ')),
						liveMetric('调频策略', 'cpu_governor', cpu.governor),
						liveMetric('节能状态', 'cpu_power_mode', cpu.powersave_active ? '节能运行中' : '正常模式'),
						liveMetric('已用内存', 'memory_used', formatCapacity(memory.used_mb)),
						liveMetric('内存总量', 'memory_total', formatCapacity(memory.total_mb)),
						liveMetric('可用内存', 'memory_available', formatCapacity(memory.available_mb)),
						liveMetric('交换空间已用', 'swap_used', formatCapacity(memory.swap_used_mb))
					]),
					progressRow('内存占用', 'memory_bar', memory.used_percent, '#9b7cff')
				]),
				E('div', { 'class': 'pcat-card pcat-detail-card' }, [
					E('div', { 'class': 'pcat-fan-heading' }, [
						E('div', {}, [ E('h3', {}, '温度与散热'), E('span', {}, '风扇动画速度跟随实际档位/转速') ]),
						E('span', { 'class': 'pcat-live-fan is-stopped', 'data-live-fan': '' }, '✣')
					]),
					E('div', { 'class': 'pcat-metrics' }, [
						liveMetric('CPU 温度', 'cpu_temp', pcat.format(temperature.cpu, 1), ' °C'),
						liveMetric('主板温度', 'board_temp', pcat.format(temperature.board, 1), ' °C'),
						liveMetric('风扇转速', 'fan_rpm', pcat.format(fan.rpm, 0), ' RPM'),
						liveMetric('风扇档位', 'fan_level', pcat.format(fan.level, 0) + ' / ' + pcat.format(fan.max, 0)),
						liveMetric('风扇策略', 'fan_mode', fanMode(fan.mode))
					]),
					thermalZones(st)
				])
			]),

			sectionTitle('↕', '网络与存储', '仅从 Linux 内核读取网卡计数和文件系统使用情况'),
			E('div', { 'class': 'pcat-grid pcat-detail-grid' }, [
				E('div', { 'class': 'pcat-card pcat-detail-card' }, [
					E('div', { 'class': 'pcat-network-heading' }, [
						E('h3', {}, '活动网络接口'),
						E('div', { 'class': 'pcat-network-flow', 'data-network-flow': '' }, [ E('i'), E('i'), E('i'), E('i'), E('i') ])
					]),
					E('div', { 'class': 'pcat-metrics' }, [
						liveMetric('接口', 'net_device', network.device), liveMetric('上联 IPv4', 'net_ipv4', network.ipv4),
						liveMetric('LAN IPv4', 'lan_ipv4', network.lan_ipv4), liveMetric('网关', 'net_gateway', network.gateway),
						liveMetric('实时下载', 'net_rx_rate', '0 B/s'), liveMetric('实时上传', 'net_tx_rate', '0 B/s'),
						liveMetric('累计接收', 'net_rx_total', formatBytes(network.rx_bytes)), liveMetric('累计发送', 'net_tx_total', formatBytes(network.tx_bytes)),
						liveMetric('活动 / 总接口', 'net_interfaces', pcat.format(network.interfaces_up, 0) + ' / ' + pcat.format(network.interfaces_total, 0)),
						liveMetric('收发错误', 'net_errors', pcat.format(numeric(network.rx_errors) + numeric(network.tx_errors), 0))
					])
				]),
				E('div', { 'class': 'pcat-card pcat-detail-card' }, [
					E('h3', {}, '存储与临时空间'),
					E('div', { 'class': 'pcat-metrics' }, [
						liveMetric('根分区已用', 'root_used', formatCapacity(deep(storage, [ 'root', 'used_mb' ]))),
						liveMetric('根分区容量', 'root_total', formatCapacity(deep(storage, [ 'root', 'total_mb' ]))),
						liveMetric('可写区可用', 'overlay_free', formatCapacity(deep(storage, [ 'overlay', 'free_mb' ]))),
						liveMetric('临时空间已用', 'tmp_used', formatCapacity(deep(storage, [ 'tmp', 'used_mb' ]))),
						liveMetric('临时空间容量', 'tmp_total', formatCapacity(deep(storage, [ 'tmp', 'total_mb' ]))),
						liveMetric('根文件系统', 'rootfs', deep(st, [ 'system', 'rootfs' ], '—'))
					]),
					progressRow('根分区占用', 'storage_bar', deep(storage, [ 'root', 'used_percent' ]), '#35a7ff')
				])
			]),

			sectionTitle('◇', '设备与固件', '设备身份、系统版本和只读硬件开关状态'),
			E('div', { 'class': 'pcat-grid pcat-detail-grid' }, [
				E('div', { 'class': 'pcat-card pcat-detail-card' }, [
					E('h3', {}, '设备信息'),
					E('div', { 'class': 'pcat-metrics' }, [
						liveMetric('主机名', 'hostname', deep(st, [ 'system', 'hostname' ])), liveMetric('设备型号', 'model', deep(st, [ 'system', 'model' ])),
						liveMetric('板型', 'board_name', deep(st, [ 'system', 'board_name' ])), liveMetric('处理器架构', 'architecture', deep(st, [ 'system', 'architecture' ])),
						liveMetric('运行时间', 'uptime', formatDuration(st.uptime))
					])
				]),
				E('div', { 'class': 'pcat-card pcat-detail-card' }, [
					E('h3', {}, '固件与硬件状态'),
					E('div', { 'class': 'pcat-metrics' }, [
						liveMetric('系统固件', 'os_firmware', deep(st, [ 'system', 'firmware' ])), liveMetric('系统版本', 'revision', deep(st, [ 'system', 'revision' ])),
						liveMetric('内核版本', 'kernel', deep(st, [ 'system', 'kernel' ])), liveMetric('编译目标', 'target', deep(st, [ 'system', 'target' ])),
						liveMetric('PMU 固件', 'firmware', deep(st, [ 'pmu', 'firmware' ])), liveMetric('电源指示灯', 'led_state', boolText(deep(st, [ 'io', 'led' ]))),
						liveMetric('蜂鸣器', 'beeper_state', boolText(deep(st, [ 'io', 'beeper' ])))
					])
				])
			]),
			E('div', { 'class': 'pcat-readonly-note' }, '只读监控页面：不会保存设置、修改 UCI、控制硬件或操作拨号。')
		]);

		window.requestAnimationFrame(L.bind(function() { this.update(st); }, this));
		poll.add(L.bind(function() {
			return L.resolveDefault(pcat.call('status'), null).then(L.bind(function(next) { if (next) this.update(next); }, this));
		}, this), 3);
		return page;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
