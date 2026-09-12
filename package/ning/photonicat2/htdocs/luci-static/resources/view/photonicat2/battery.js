'use strict';
'require view';
'require poll';
'require ui';
'require photonicat2.common as pcat';

function deep(obj, path, fallback) {
	for (var i = 0; obj != null && i < path.length; i++) obj = obj[path[i]];
	return obj == null ? fallback : obj;
}

function historyChart(samples) {
	var valid = (samples || []).filter(function(s) { return isFinite(Number(s.percent)); });
	var points = [];
	for (var i = 0; i < valid.length; i++) {
		var x = valid.length > 1 ? 10 + i * 580 / (valid.length - 1) : 300;
		var y = 210 - Math.max(0, Math.min(100, Number(valid[i].percent))) * 1.8;
		points.push('%s,%s'.format(x.toFixed(1), y.toFixed(1)));
	}
	var area = points.length ? '10,210 ' + points.join(' ') + ' 590,210' : '';
	return E('svg', { 'class': 'pcat-chart', 'viewBox': '0 0 600 230', 'preserveAspectRatio': 'none' }, [
		E('defs', {}, E('linearGradient', { 'id': 'pcatChartFill', 'x1': '0', 'y1': '0', 'x2': '0', 'y2': '1' }, [
			E('stop', { 'offset': '0%', 'stop-color': 'var(--pcat-accent)', 'stop-opacity': '.7' }),
			E('stop', { 'offset': '100%', 'stop-color': 'var(--pcat-accent)', 'stop-opacity': '0' })
		])),
		E('line', { 'class': 'pcat-chart-grid', 'x1': '10', 'y1': '30', 'x2': '590', 'y2': '30' }),
		E('line', { 'class': 'pcat-chart-grid', 'x1': '10', 'y1': '120', 'x2': '590', 'y2': '120' }),
		E('line', { 'class': 'pcat-chart-grid', 'x1': '10', 'y1': '210', 'x2': '590', 'y2': '210' }),
		area ? E('polygon', { 'class': 'pcat-chart-area', 'points': area }) : '',
		points.length ? E('polyline', { 'class': 'pcat-chart-line', 'points': points.join(' ') }) : '',
		E('text', { 'x': '12', 'y': '25', 'fill': 'currentColor', 'opacity': '.55', 'font-size': '11' }, '100%'),
		E('text', { 'x': '12', 'y': '115', 'fill': 'currentColor', 'opacity': '.55', 'font-size': '11' }, '50%'),
		E('text', { 'x': '12', 'y': '205', 'fill': 'currentColor', 'opacity': '.55', 'font-size': '11' }, '0%')
	]);
}

function rangeStats(samples, key) {
	var values = (samples || []).map(function(s) { return Number(s[key]); }).filter(isFinite);
	if (!values.length) return null;
	return {
		min: Math.min.apply(Math, values),
		max: Math.max.apply(Math, values),
		avg: values.reduce(function(a, b) { return a + b; }, 0) / values.length
	};
}

return view.extend({
	load: function() {
		return Promise.all([
			L.resolveDefault(pcat.call('status'), {}),
			L.resolveDefault(pcat.call('history'), { samples: [] })
		]);
	},

	updateLive: function() {
		return L.resolveDefault(pcat.call('status'), null).then(function(st) {
			var root = document.getElementById('pcat-battery');
			if (!root || !st) return;
			pcat.updateGauge(root, 'soc', deep(st, ['battery', 'percent']), 100);
			pcat.updateGauge(root, 'voltage', deep(st, ['battery', 'voltage']), 5);
			pcat.updateGauge(root, 'power', Math.abs(deep(st, ['battery', 'power'], 0)), 20);
			pcat.updateGauge(root, 'btemp', deep(st, ['temperature', 'board']), 70);
			var map = {
				current: pcat.format(deep(st, ['battery', 'current']), 2) + ' A',
				state: deep(st, ['battery', 'charging'], false) ? '正在充电' : '使用电池',
				limit: pcat.format(deep(st, ['battery', 'threshold']), 0) + '%'
			};
			Object.keys(map).forEach(function(k) {
				var el = root.querySelector('[data-live="%s"]'.format(k));
				if (el) el.textContent = map[k];
			});
		});
	},

	clearHistory: function() {
		L.showModal('清空本次开机的电池历史？', [
			E('p', {}, '图表采样位于内存中，清空后无法恢复，但不会影响 PMU 保存的循环次数和容量信息。'),
			E('div', { 'class': 'right' }, [
				E('button', { 'class': 'btn', 'click': L.hideModal }, '取消'), ' ',
				E('button', {
					'class': 'btn cbi-button-negative',
					'click': ui.createHandlerFn(this, function() {
						return pcat.call('clear-history').then(function() { L.hideModal(); window.location.reload(); });
					})
				}, '清空')
			])
		]);
	},

	render: function(data) {
		pcat.loadStyle();
		var st = data[0] || {};
		var samples = data[1].samples || [];
		var info = deep(st, ['battery', 'info'], {});
		var voltageStats = rangeStats(samples, 'voltage');
		var tempStats = rangeStats(samples, 'temperature');
		var nominal = Number(info['nominal-capacity']);
		var full = Number(info['full-capacity']);
		var health = isFinite(nominal) && nominal > 0 && isFinite(full) ? 100 * full / nominal : null;
		var oldest = samples.length ? new Date(Number(samples[0].time) * 1000).toLocaleString() : '本次开机暂无样本';

		var page = E('div', { 'class': 'pcat-page', 'id': 'pcat-battery' }, [
			E('h2', {}, '电池数据统计'),
			E('p', { 'class': 'pcat-subtitle' }, '每分钟在 /tmp 中采样，不写闪存；重启后重新统计。PMU 学习容量与循环次数由硬件自身持久保存。'),
			E('div', { 'class': 'pcat-card pcat-card-glow' }, [
				E('div', { 'class': 'pcat-gauges' }, [
					pcat.gauge('soc', '当前电量', deep(st, ['battery', 'percent']), '%', 100, '#21c77a'),
					pcat.gauge('voltage', '电池电压', deep(st, ['battery', 'voltage']), 'V', 5, '#00a8ff'),
					pcat.gauge('power', '充放电功率', Math.abs(deep(st, ['battery', 'power'], 0)), 'W', 20, '#9b72ff'),
					pcat.gauge('btemp', '电池/机身温度', deep(st, ['temperature', 'board']), '°C', 70, '#f7a928')
				]),
				E('div', { 'class': 'pcat-metrics' }, [
					pcat.metric('实时电流', E('span', { 'data-live': 'current' }, pcat.format(deep(st, ['battery', 'current']), 2) + ' A')),
					pcat.metric('供电状态', E('span', { 'data-live': 'state' }, deep(st, ['battery', 'charging'], false) ? '正在充电' : '使用电池')),
					pcat.metric('充电限制', E('span', { 'data-live': 'limit' }, pcat.format(deep(st, ['battery', 'threshold']), 0) + '%')),
					pcat.metric('记录样本', String(samples.length), ' 条')
				])
			]),
			E('div', { 'class': 'pcat-grid wide' }, [
				E('div', { 'class': 'pcat-card' }, [
					E('h3', {}, '本次开机电量曲线'),
					historyChart(samples),
					E('div', { 'class': 'pcat-hint' }, '起始采样：' + oldest)
				]),
				E('div', { 'class': 'pcat-card' }, [
					E('h3', {}, '电池健康与区间'),
					E('div', { 'class': 'pcat-metrics' }, [
						pcat.metric('学习容量', pcat.format(full, 0), ' mAh'),
						pcat.metric('标称容量', pcat.format(nominal, 0), ' mAh'),
						pcat.metric('估算健康度', pcat.format(health, 1), '%'),
						pcat.metric('充放电循环', pcat.format(info['cycle-count'], 0), ' 次'),
						pcat.metric('电压区间', voltageStats ? pcat.format(voltageStats.min, 2) + '–' + pcat.format(voltageStats.max, 2) : '—', ' V'),
						pcat.metric('平均温度', tempStats ? pcat.format(tempStats.avg, 1) : '—', ' °C')
					]),
					E('div', { 'class': 'pcat-actions' }, [
						E('button', { 'class': 'btn cbi-button-negative', 'click': ui.createHandlerFn(this, 'clearHistory') }, '清空历史')
					])
				])
			])
		]);
		poll.add(L.bind(this.updateLive, this), 5);
		return page;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
