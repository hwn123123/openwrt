'use strict';
'require view';
'require poll';
'require ui';
'require photonicat2.common as pcat';

function number(data, group, key, fallback) {
	return data && data[group] && data[group][key] != null ? data[group][key] : fallback;
}

return view.extend({
	load: function() {
		return Promise.all([ L.resolveDefault(pcat.call('power-get'), {}), L.resolveDefault(pcat.call('status'), {}) ]);
	},

	read: function() {
		return Promise.all([ pcat.call('power-get'), pcat.call('status') ]).then(function(data) {
			var input = document.getElementById('pcat-charge-value');
			var limit = data[0] && data[0].charge ? data[0].charge.value : 100;
			if (input) input.value = limit == null ? 100 : limit;
			var text = document.getElementById('pcat-charge-live');
			if (text) text.textContent = pcat.format(number(data[1], 'battery', 'percent'), 0) + '% / 上限 ' + pcat.format(limit, 0) + '%';
			var gauge = document.getElementById('pcat-charge-gauge');
			if (gauge) pcat.updateGauge(gauge, 'charge', limit, 100);
		}).catch(function(err) { pcat.notify(err.message, 'error'); });
	},

	save: function() {
		var input = document.getElementById('pcat-charge-value');
		var value = input ? parseInt(input.value, 10) : NaN;
		if (!isFinite(value) || value < 50 || value > 100) {
			pcat.notify('充电停止阈值必须为 50–100%', 'error');
			return;
		}
		var button = document.getElementById('pcat-charge-save');
		if (button) button.disabled = true;
		return pcat.call('charge-set', [ String(value) ]).then(function() {
			pcat.notify('充电上限已写入 PMU', 'info');
			return this.read();
		}.bind(this)).catch(function(err) {
			pcat.notify(err.message, 'error');
		}).finally(function() { if (button) button.disabled = false; });
	},

	update: function() {
		return L.resolveDefault(pcat.call('status'), null).then(function(data) {
			var text = document.getElementById('pcat-charge-live');
			if (text && data) text.textContent = pcat.format(number(data, 'battery', 'percent'), 0) + '%';
		});
	},

	render: function(data) {
		pcat.loadStyle();
		var power = data[0] || {};
		var status = data[1] || {};
		var limit = power.charge && power.charge.value != null ? power.charge.value : 100;
		var root = E('div', { 'class': 'pcat-page' }, [
			E('h2', {}, '充电限制'),
			E('p', { 'class': 'pcat-subtitle' }, '直接调用 pcat-manager 的 PMU 充电阈值接口，不经过厂家主题或外部 Web 端口。'),
			E('div', { 'class': 'pcat-card pcat-card-glow', 'id': 'pcat-charge-gauge' }, [
				E('div', { 'class': 'pcat-gauges' }, [ pcat.gauge('charge', '停止充电阈值', limit, '%', 100, '#21c77a') ]),
				E('div', { 'id': 'pcat-charge-live', 'class': 'pcat-hint', 'style': 'text-align:center' },
					pcat.format(number(status, 'battery', 'percent'), 0) + '% / 上限 ' + pcat.format(limit, 0) + '%')
			]),
			E('div', { 'class': 'pcat-card' }, [
				E('h3', {}, 'PMU 充电停止阈值'),
				E('p', { 'class': 'pcat-hint' }, '范围 50–100%。达到阈值后由 PMU 停止充电，低于阈值后自动恢复。'),
				E('div', { 'class': 'pcat-control' }, [
					E('label', {}, '充电上限 (%)'),
					E('input', { 'id': 'pcat-charge-value', 'type': 'number', 'min': '50', 'max': '100', 'value': String(limit) })
				])
			]),
			E('div', { 'class': 'cbi-page-actions' }, [
				E('button', { 'id': 'pcat-charge-save', 'class': 'btn cbi-button-positive important', 'click': ui.createHandlerFn(this, 'save') }, '保存充电上限')
			])
		]);
		poll.add(L.bind(this.update, this), 10);
		return root;
	}
});
