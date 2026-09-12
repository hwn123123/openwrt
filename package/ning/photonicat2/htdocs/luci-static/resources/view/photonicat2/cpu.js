'use strict';
'require view';
'require form';
'require poll';
'require uci';
'require photonicat2.common as pcat';

function value(data, group, key) {
	return data && data[group] && data[group][key] != null ? data[group][key] : null;
}

return view.extend({
	load: function() {
		return Promise.all([ uci.load('photonicat2'), L.resolveDefault(pcat.call('status'), {}) ]);
	},

	update: function() {
		return L.resolveDefault(pcat.call('status'), null).then(function(data) {
			var root = document.getElementById('pcat-cpu-page');
			if (!root || !data) return;
			pcat.updateGauge(root, 'cpu-load', value(data, 'cpu', 'usage'), 100);
			pcat.updateGauge(root, 'cpu-temp', value(data, 'temperature', 'cpu'), 100);
			var freq = root.querySelector('[data-live="cpu-freq"]');
			var state = root.querySelector('[data-live="cpu-save"]');
			if (freq) freq.textContent = pcat.format(value(data, 'cpu', 'frequency_mhz'), 0) + ' MHz';
			if (state) {
				var active = !!value(data, 'cpu', 'powersave_active');
				state.textContent = active ? '已生效' : '未生效';
				state.className = 'pcat-state ' + (active ? 'is-ok' : 'is-off');
			}
		});
	},

	render: function(data) {
		pcat.loadStyle();
		var status = data[1] || {};
		var m = new form.Map('photonicat2', 'CPU 节能',
			'启用后优先关闭最高性能核心；硬件不支持核心热插拔时改用 powersave 调速器。');
		var s = m.section(form.NamedSection, 'main', 'core', '节能策略');
		s.addremove = false;
		var o = s.option(form.Flag, 'cpu_powersave', '启用 CPU 节能');
		o.default = '0';
		o.rmempty = false;

		return m.render().then(L.bind(function(formNode) {
			var active = !!value(status, 'cpu', 'powersave_active');
			var root = E('div', { 'class': 'pcat-page', 'id': 'pcat-cpu-page' }, [
				E('div', { 'class': 'pcat-card pcat-card-glow' }, [
					E('div', { 'class': 'pcat-gauges' }, [
						pcat.gauge('cpu-load', 'CPU 负载', value(status, 'cpu', 'usage'), '%', 100, '#00a8ff'),
						pcat.gauge('cpu-temp', 'CPU 温度', value(status, 'temperature', 'cpu'), '°C', 100, '#f7a928')
					]),
					E('div', { 'class': 'pcat-metrics' }, [
						pcat.metric('当前频率', E('span', { 'data-live': 'cpu-freq' }, pcat.format(value(status, 'cpu', 'frequency_mhz'), 0) + ' MHz')),
						pcat.metric('节能状态', E('span', { 'data-live': 'cpu-save', 'class': 'pcat-state ' + (active ? 'is-ok' : 'is-off') }, active ? '已生效' : '未生效'))
					])
				]),
				formNode
			]);
			poll.add(L.bind(this.update, this), 5);
			return root;
		}, this));
	}
});
