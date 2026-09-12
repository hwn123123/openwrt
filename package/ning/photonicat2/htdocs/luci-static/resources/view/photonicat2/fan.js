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
			var root = document.getElementById('pcat-fan-page');
			if (!root || !data) return;
			pcat.updateGauge(root, 'fan-temp', value(data, 'temperature', 'board'), 80);
			pcat.updateGauge(root, 'fan-rpm', value(data, 'fan', 'rpm'), 7000);
			var level = root.querySelector('[data-live="fan-level"]');
			var fan = root.querySelector('.pcat-fan');
			if (level) level.textContent = pcat.format(value(data, 'fan', 'level'), 0) + '/9';
			if (fan) fan.className = 'pcat-fan' + (Number(value(data, 'fan', 'level')) > 0 ? '' : ' stopped');
		});
	},

	render: function(data) {
		pcat.loadStyle();
		var status = data[1] || {};
		var m = new form.Map('photonicat2', '智能风扇',
			'提供手动、安静和冷却三种独立策略；温度超过 60°C 时启用硬件保护转速。');
		var s = m.section(form.NamedSection, 'main', 'core', '风扇策略');
		s.addremove = false;
		var o = s.option(form.ListValue, 'fan_mode', '风扇模式');
		o.value('manual', '手动转速');
		o.value('smart_quiet', '智能安静（充电 45°C / 电池 52°C）');
		o.value('smart_cool', '智能冷却（充电 43°C / 电池 48°C）');
		o.default = 'smart_quiet';
		o.rmempty = false;
		o = s.option(form.Value, 'fan_max', '转速档位 / 智能上限');
		o.datatype = 'range(0,9)';
		o.default = '5';
		o.rmempty = false;

		return m.render().then(L.bind(function(formNode) {
			var root = E('div', { 'class': 'pcat-page', 'id': 'pcat-fan-page' }, [
				E('div', { 'class': 'pcat-card pcat-card-glow' }, [
					E('div', { 'class': 'pcat-gauges' }, [
						pcat.gauge('fan-temp', '机身温度', value(status, 'temperature', 'board'), '°C', 80, '#f7a928'),
						pcat.gauge('fan-rpm', '风扇转速', value(status, 'fan', 'rpm'), 'RPM', 7000, '#21c77a')
					]),
					E('div', { 'class': 'pcat-metrics' }, [
						pcat.metric('当前档位', E('span', { 'data-live': 'fan-level' }, pcat.format(value(status, 'fan', 'level'), 0) + '/9')),
						pcat.metric('动态风扇', E('span', { 'class': 'pcat-fan' + (Number(value(status, 'fan', 'level')) > 0 ? '' : ' stopped') }, '✣'))
					])
				]),
				formNode
			]);
			poll.add(L.bind(this.update, this), 5);
			return root;
		}, this));
	}
});
