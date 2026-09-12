'use strict';
'require baseclass';
'require fs';
'require network';
'require rpc';

const callSystemInfo = rpc.declare({
	object: 'system',
	method: 'info'
});

const SVG_NS = 'http://www.w3.org/2000/svg';

function S(name, attributes, children) {
	const node = document.createElementNS(SVG_NS, name);
	Object.keys(attributes || {}).forEach(function(key) {
		node.setAttribute(key, attributes[key]);
	});
	(children || []).forEach(function(child) {
		node.appendChild(child);
	});
	return node;
}

function clamp(value, min, max) {
	return Math.max(min, Math.min(max, Number(value) || 0));
}

function number(value, fallback) {
	const n = Number(value);
	return Number.isFinite(n) ? n : (fallback == null ? 0 : fallback);
}

function formatBytes(value, rate) {
	let n = Math.max(0, number(value));
	const units = [ 'B', 'KB', 'MB', 'GB', 'TB' ];
	let unit = 0;
	while (n >= 1024 && unit < units.length - 1) {
		n /= 1024;
		unit++;
	}
	const digits = n >= 100 || unit === 0 ? 0 : (n >= 10 ? 1 : 2);
	return n.toFixed(digits) + ' ' + units[unit] + (rate ? '/s' : '');
}

function formatFrequency(khz) {
	const mhz = number(khz) / 1000;
	if (!mhz)
		return '—';
	return mhz >= 1000 ? (mhz / 1000).toFixed(2) + ' GHz' : Math.round(mhz) + ' MHz';
}

function parseCpu(raw) {
	const line = String(raw || '').split('\n')[0].trim().split(/\s+/);
	if (line[0] !== 'cpu')
		return null;
	const values = line.slice(1).map(function(value) { return number(value); });
	const total = values.reduce(function(sum, value) { return sum + value; }, 0);
	return { total: total, idle: (values[3] || 0) + (values[4] || 0) };
}

function parseCoreCount(raw) {
	let total = 0;
	String(raw || '').trim().split(',').forEach(function(part) {
		const range = part.split('-').map(Number);
		if (range.length === 2 && Number.isFinite(range[0]) && Number.isFinite(range[1]))
			total += Math.max(0, range[1] - range[0] + 1);
		else if (Number.isFinite(range[0]))
			total++;
	});
	return total || 1;
}

function qualityColor(value) {
	return value >= 78 ? '#f25f70' : value >= 58 ? '#ffad3d' : '#18cf8b';
}

function gauge(label, value, unit, percent, previousPercent, note, color) {
	const pct = clamp(percent, 0, 100);
	const previousPct = previousPercent == null ? pct : clamp(previousPercent, 0, 100);
	const angle = -90 + pct * 1.8;
	const previousAngle = -90 + previousPct * 1.8;
	const style = [
		'--dov-meter:' + color,
		'--dov-angle:' + angle.toFixed(1) + 'deg',
		'--dov-previous-angle:' + previousAngle.toFixed(1) + 'deg',
		'--dov-offset:' + (100 - pct).toFixed(1),
		'--dov-previous-offset:' + (100 - previousPct).toFixed(1)
	].join(';');
	return E('article', { 'class': 'dov-gauge-card', 'style': style }, [
		E('div', { 'class': 'dov-gauge-label' }, label),
		E('div', { 'class': 'dov-gauge' }, [
			S('svg', { 'viewBox': '0 0 120 78', 'aria-hidden': 'true' }, [
				S('path', { 'class': 'dov-gauge-track', 'd': 'M 10 66 A 50 50 0 0 1 110 66', 'pathLength': '100' }),
				S('path', { 'class': 'dov-gauge-fill', 'd': 'M 10 66 A 50 50 0 0 1 110 66', 'pathLength': '100' }),
				S('line', { 'class': 'dov-gauge-needle', 'x1': '60', 'y1': '66', 'x2': '60', 'y2': '30' }),
				S('circle', { 'class': 'dov-gauge-pin', 'cx': '60', 'cy': '66', 'r': '4' })
			]),
			E('div', { 'class': 'dov-gauge-value' }, [
				E('strong', String(value)),
				E('span', unit)
			])
		]),
		E('div', { 'class': 'dov-gauge-note' }, note)
	]);
}

function pathFor(values, min, max) {
	const list = values.length ? values : [ 0 ];
	const range = Math.max(1, max - min);
	return list.map(function(value, index) {
		const x = list.length === 1 ? 300 : 8 + index * 584 / (list.length - 1);
		const y = 158 - clamp((number(value) - min) / range, 0, 1) * 142;
		return (index ? 'L' : 'M') + x.toFixed(1) + ' ' + y.toFixed(1);
	}).join(' ');
}

function chart(title, subtitle, series, min, max, scaleLeft, scaleRight) {
	const children = [];
	for (let i = 0; i < 5; i++) {
		const y = 16 + i * 35.5;
		children.push(S('line', { 'x1': '8', 'x2': '592', 'y1': y, 'y2': y }));
	}
	const legend = [];
	series.forEach(function(item) {
		const line = pathFor(item.values, min, max);
		if (item.area)
			children.push(S('path', { 'class': 'dov-chart-area', 'd': line + ' L 592 158 L 8 158 Z', 'style': 'fill:' + item.color }));
		children.push(S('path', { 'class': 'dov-chart-line', 'd': line, 'style': 'stroke:' + item.color }));
		legend.push(E('span', {}, [
			E('i', { 'style': 'background:' + item.color }),
			item.name + ' ',
			E('strong', item.current)
		]));
	});
	return E('article', { 'class': 'dov-chart-card' }, [
		E('div', { 'class': 'dov-chart-heading' }, [
			E('div', {}, [ E('h4', title), E('p', subtitle) ]),
			E('span', { 'class': 'dov-live' }, [ E('i'), '实时' ])
		]),
		E('div', { 'class': 'dov-chart-legend' }, legend),
		S('svg', { 'class': 'dov-chart', 'viewBox': '0 0 600 170', 'preserveAspectRatio': 'none', 'aria-hidden': 'true' }, [
			S('g', { 'class': 'dov-chart-lines' }, children.slice(0, 5)),
			S('g', {}, children.slice(5))
		]),
		E('div', { 'class': 'dov-chart-scale' }, [ E('span', scaleLeft), E('span', '最近约 3 分钟'), E('span', scaleRight) ])
	]);
}

function infoCard(icon, label, value, note) {
	return E('article', { 'class': 'dov-info-card' }, [
		E('i', { 'class': 'dov-info-icon' }, icon),
		E('div', {}, [ E('span', label), E('strong', value), E('small', note || '') ])
	]);
}

function decorateSections() {
	window.setTimeout(function() {
		document.querySelectorAll('#view > .cbi-section').forEach(function(section) {
			if (section.querySelector('.dov-dashboard')) {
				section.classList.add('dov-dashboard-section');
				return;
			}
			const title = section.querySelector('.cbi-title h3');
			const text = title ? title.textContent.toLowerCase() : '';
			const classes = [
				[ /system|系统/, 'dov-standard-system' ],
				[ /modem|调制解调器|模组/, 'dov-standard-modem' ],
				[ /memory|内存/, 'dov-standard-memory' ],
				[ /storage|存储/, 'dov-standard-storage' ],
				[ /ethernet|port|端口/, 'dov-standard-ports' ],
				[ /network|网络/, 'dov-standard-network' ],
				[ /dhcp/, 'dov-standard-dhcp' ],
				[ /wireless|无线/, 'dov-standard-wireless' ]
			];
			classes.some(function(rule) {
				if (!rule[0].test(text))
					return false;
				section.classList.add(rule[1]);
				return true;
			});
		});
	}, 0);
}

return baseclass.extend({
	title: '实时运行中心',
	history: {},
	previousCpu: null,
	previousNetwork: null,
	gaugeValues: {},

	load: function() {
		return Promise.all([
			L.resolveDefault(callSystemInfo(), {}),
			L.resolveDefault(fs.read('/proc/stat'), ''),
			L.resolveDefault(fs.trimmed('/sys/devices/system/cpu/online'), '0'),
			L.resolveDefault(fs.trimmed('/sys/class/thermal/thermal_zone0/temp'), ''),
			L.resolveDefault(fs.trimmed('/sys/class/thermal/thermal_zone2/temp'), ''),
			L.resolveDefault(fs.trimmed('/sys/devices/system/cpu/cpufreq/policy0/scaling_cur_freq'), ''),
			L.resolveDefault(fs.trimmed('/sys/devices/system/cpu/cpufreq/policy4/scaling_cur_freq'), ''),
			L.resolveDefault(fs.trimmed('/proc/sys/net/netfilter/nf_conntrack_count'), '0'),
			L.resolveDefault(fs.trimmed('/proc/sys/net/netfilter/nf_conntrack_max'), '0'),
			L.resolveDefault(network.getWANNetworks(), []),
			L.resolveDefault(network.getNetworks(), [])
		]);
	},

	push: function(name, value) {
		if (!this.history[name])
			this.history[name] = [];
		this.history[name].push(number(value));
		if (this.history[name].length > 36)
			this.history[name].shift();
	},

	selectUplink: function(wans, networks) {
		let candidates = (wans || []).filter(function(iface) {
			return iface && iface.isUp() && iface.getL3Device();
		});
		if (!candidates.length) {
			candidates = (networks || []).filter(function(iface) {
				if (!iface || !iface.isUp() || !iface.getL3Device())
					return false;
				return !/^(lan|loopback|docker)$/i.test(iface.getName());
			});
			candidates.sort(function(a, b) {
				const score = function(iface) {
					return (iface.getGatewayAddr() ? 8 : 0) + (iface.getIPAddrs().length ? 4 : 0) - (/v6$/i.test(iface.getName()) ? 2 : 0);
				};
				return score(b) - score(a);
			});
		}
		return candidates[0] || null;
	},

	render: function(data) {
		if (!document.body.classList.contains('theme-design'))
			return null;

		const system = data[0] || {};
		const cpuNow = parseCpu(data[1]);
		const cores = parseCoreCount(data[2]);
		const temperatures = [ number(data[3], NaN), number(data[4], NaN) ].filter(Number.isFinite).map(function(v) { return v > 1000 ? v / 1000 : v; });
		const temperature = temperatures.length ? Math.max.apply(Math, temperatures) : 0;
		const frequency = Math.max(number(data[5]), number(data[6]));
		const connCount = number(data[7]);
		const connMax = number(data[8]);
		const uplink = this.selectUplink(data[9], data[10]);
		const device = uplink ? uplink.getL3Device() : null;

		let cpuPercent = Array.isArray(system.load) ? clamp(system.load[0] / 65535 / cores * 100, 0, 100) : 0;
		if (cpuNow && this.previousCpu) {
			const totalDelta = cpuNow.total - this.previousCpu.total;
			const idleDelta = cpuNow.idle - this.previousCpu.idle;
			if (totalDelta > 0)
				cpuPercent = clamp((totalDelta - idleDelta) / totalDelta * 100, 0, 100);
		}
		this.previousCpu = cpuNow;

		const memory = L.isObject(system.memory) ? system.memory : {};
		const root = L.isObject(system.root) ? system.root : {};
		const memoryUsed = Math.max(0, number(memory.total) - number(memory.available || memory.free));
		const memoryPercent = memory.total ? clamp(memoryUsed / memory.total * 100, 0, 100) : 0;
		const rootPercent = root.total ? clamp(number(root.used) / number(root.total) * 100, 0, 100) : 0;
		const now = Date.now();
		const rx = device ? device.getRXBytes() : 0;
		const tx = device ? device.getTXBytes() : 0;
		let rxRate = 0, txRate = 0;
		if (this.previousNetwork && device && this.previousNetwork.device === device.getName()) {
			const seconds = Math.max(.1, (now - this.previousNetwork.time) / 1000);
			rxRate = Math.max(0, (rx - this.previousNetwork.rx) / seconds);
			txRate = Math.max(0, (tx - this.previousNetwork.tx) / seconds);
		}
		this.previousNetwork = device ? { device: device.getName(), rx: rx, tx: tx, time: now } : null;

		this.push('cpu', cpuPercent);
		this.push('memory', memoryPercent);
		this.push('temperature', temperature);
		this.push('rx', rxRate);
		this.push('tx', txRate);

		const netPeak = Math.max(1024, Math.max.apply(Math, this.history.rx.concat(this.history.tx)) * 1.15);
		const tempMin = 20;
		const tempMax = Math.max(90, Math.ceil(Math.max.apply(Math, this.history.temperature) / 10) * 10);
		const load = Array.isArray(system.load) ? system.load.map(function(v) { return (v / 65535).toFixed(2); }) : [ '—', '—', '—' ];
		const address = uplink && uplink.getIPAddrs().length ? uplink.getIPAddrs()[0] : '—';
		const gateway = uplink ? (uplink.getGatewayAddr() || '—') : '—';
		const temperaturePercent = temperature ? clamp((temperature - 20) / 70 * 100, 0, 100) : 0;
		const previousGauges = this.gaugeValues || {};
		this.gaugeValues = {
			cpu: cpuPercent,
			memory: memoryPercent,
			temperature: temperaturePercent,
			storage: rootPercent
		};

		decorateSections();
		return E('div', { 'class': 'dov-dashboard' }, [
			E('div', { 'class': 'dov-gauge-grid' }, [
				gauge('CPU 实时使用率', cpuPercent.toFixed(1), '%', cpuPercent, previousGauges.cpu, cores + ' 核 · ' + formatFrequency(frequency), qualityColor(cpuPercent)),
				gauge('内存使用率', memoryPercent.toFixed(1), '%', memoryPercent, previousGauges.memory, formatBytes(memoryUsed) + ' / ' + formatBytes(memory.total), qualityColor(memoryPercent)),
				gauge('核心温度', temperature ? temperature.toFixed(1) : '—', temperature ? '°C' : '', temperaturePercent, previousGauges.temperature, temperature ? '多传感器最高值' : '传感器不可用', temperature >= 75 ? '#f25f70' : temperature >= 60 ? '#ffad3d' : '#18cf8b'),
				gauge('系统存储', rootPercent.toFixed(1), '%', rootPercent, previousGauges.storage, formatBytes(number(root.used) * 1024) + ' / ' + formatBytes(number(root.total) * 1024), qualityColor(rootPercent))
			]),

			E('div', { 'class': 'dov-info-grid' }, [
				infoCard('⌁', '系统负载', load.join(' / '), '1 / 5 / 15 分钟'),
				infoCard('↟', '运行时间', system.uptime ? '%t'.format(system.uptime) : '—', '本次启动持续时间'),
				infoCard('⇄', '活动连接', String(connCount), connMax ? '上限 ' + connMax : '连接跟踪'),
				infoCard('◎', '上行接口', uplink ? uplink.getName() : '—', device ? device.getName() + ' · ' + (uplink.getI18n() || '已连接') : '未发现可用接口'),
				infoCard('⌂', '上行地址', String(address), '网关 ' + gateway),
				infoCard('↯', '实时吞吐', '↓ ' + formatBytes(rxRate, true), '↑ ' + formatBytes(txRate, true))
			]),

			E('div', { 'class': 'dov-chart-grid' }, [
				chart('系统资源脉冲', 'CPU 与内存真实占用变化', [
					{ name: 'CPU', color: '#36a9ff', values: this.history.cpu, current: cpuPercent.toFixed(1) + '%', area: true },
					{ name: '内存', color: '#9b7cff', values: this.history.memory, current: memoryPercent.toFixed(1) + '%' }
				], 0, 100, '0%', '100%'),
				chart('上行流量波形', device ? device.getName() + ' 实际网卡计数' : '等待上行接口', [
					{ name: '下载', color: '#18cf8b', values: this.history.rx, current: formatBytes(rxRate, true), area: true },
					{ name: '上传', color: '#36a9ff', values: this.history.tx, current: formatBytes(txRate, true) }
				], 0, netPeak, '0 B/s', formatBytes(netPeak, true)),
				chart('温度变化曲线', '核心温度随负载实时变化', [
					{ name: '温度', color: '#ffad3d', values: this.history.temperature, current: temperature ? temperature.toFixed(1) + ' °C' : '—', area: true }
				], tempMin, tempMax, tempMin + '°C', tempMax + '°C')
			])
		]);
	}
});
