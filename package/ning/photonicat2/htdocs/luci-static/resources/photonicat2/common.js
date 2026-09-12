'use strict';
'require baseclass';
'require fs';
'require ui';

var CTL = '/usr/libexec/photonicat2ctl';

function loadStyle() {
	if (document.getElementById('photonicat2-native-css'))
		return;

	var link = E('link', {
		'id': 'photonicat2-native-css',
		'rel': 'stylesheet',
		'href': L.resource('photonicat2/photonicat2.css') + '?v=20260906-toast'
	});
	document.head.appendChild(link);
}

function call(action, args) {
	return fs.exec(CTL, [ action ].concat(args || [])).then(function(res) {
		var data;
		try {
			data = JSON.parse((res.stdout || '').trim() || '{}');
		}
		catch (e) {
			throw new Error('后端返回的 JSON 无效');
		}
		if (res.code !== 0 || data.ok === false || data.error || (data.code != null && data.code !== 0))
			throw new Error(data.error || (res.stderr || '操作失败').trim());
		return data;
	});
}

function notify(message, type) {
	var stack = document.getElementById('pcat-toast-stack');
	if (!stack) {
		stack = E('div', { 'id': 'pcat-toast-stack', 'class': 'pcat-toast-stack', 'aria-live': 'polite' });
		document.body.appendChild(stack);
	}

	stack.textContent = '';
	var toast = E('div', {
		'class': 'pcat-toast ' + (type === 'error' ? 'is-error' : 'is-success'),
		'role': type === 'error' ? 'alert' : 'status'
	}, [ E('i', { 'aria-hidden': 'true' }), E('span', {}, [ String(message || (type === 'error' ? '操作失败' : '操作成功')) ]) ]);
	stack.appendChild(toast);
	window.requestAnimationFrame(function() { toast.classList.add('is-visible'); });

	var close = function() {
		if (!toast.parentNode)
			return;
		toast.classList.remove('is-visible');
		window.setTimeout(function() { if (toast.parentNode) toast.parentNode.removeChild(toast); }, 180);
	};
	toast.addEventListener('click', close);
	window.setTimeout(close, type === 'error' ? 4200 : 2400);
}

function finite(value, fallback) {
	value = Number(value);
	return isFinite(value) ? value : (fallback == null ? 0 : fallback);
}

function format(value, digits, empty) {
	if (value == null || value === '' || !isFinite(Number(value)))
		return empty == null ? '—' : empty;
	return Number(value).toFixed(digits == null ? 0 : digits);
}

function gauge(id, label, value, unit, max, color) {
	var pct = Math.max(0, Math.min(1, finite(value) / max));
	var dash = 157 * (1 - pct);
	var angle = -90 + 180 * pct;
	return E('div', { 'class': 'pcat-gauge', 'data-gauge': id }, [
		E('svg', { 'viewBox': '0 0 120 78', 'aria-hidden': 'true' }, [
			E('path', { 'class': 'pcat-gauge-track', 'd': 'M10 62 A50 50 0 0 1 110 62' }),
			E('path', {
				'class': 'pcat-gauge-fill', 'd': 'M10 62 A50 50 0 0 1 110 62',
				'style': 'stroke:%s;stroke-dashoffset:%s'.format(color || 'var(--pcat-accent)', dash)
			}),
			E('line', {
				'class': 'pcat-needle', 'x1': '60', 'y1': '62', 'x2': '60', 'y2': '22',
				'style': 'transform:rotate(%sdeg)'.format(angle)
			}),
			E('circle', { 'class': 'pcat-needle-pin', 'cx': '60', 'cy': '62', 'r': '4' })
		]),
		E('div', { 'class': 'pcat-gauge-value' }, [
			E('span', { 'class': 'value' }, format(value, value != null && Number(value) < 10 ? 1 : 0)),
			E('small', {}, unit || '')
		]),
		E('div', { 'class': 'pcat-gauge-label' }, label)
	]);
}

function updateGauge(root, id, value, max) {
	var node = root.querySelector('[data-gauge="%s"]'.format(id));
	if (!node)
		return;
	var pct = Math.max(0, Math.min(1, finite(value) / max));
	var fill = node.querySelector('.pcat-gauge-fill');
	var needle = node.querySelector('.pcat-needle');
	var text = node.querySelector('.value');
	if (fill)
		fill.style.strokeDashoffset = 157 * (1 - pct);
	if (needle)
		needle.style.transform = 'rotate(%sdeg)'.format(-90 + 180 * pct);
	if (text)
		text.textContent = format(value, value != null && Number(value) < 10 ? 1 : 0);
}

function metric(label, value, unit, cls) {
	var rendered = (value != null && typeof(value) === 'object') ? value : (value == null ? '—' : String(value));
	return E('div', { 'class': 'pcat-metric ' + (cls || '') }, [
		E('span', { 'class': 'pcat-metric-label' }, label),
		E('strong', { 'class': 'pcat-metric-value' }, [ rendered, unit ? E('small', {}, unit) : '' ])
	]);
}

function serviceState(ok, text) {
	return E('span', { 'class': 'pcat-state ' + (ok ? 'is-ok' : 'is-off') }, [
		E('i', { 'class': 'pcat-pulse' }), text
	]);
}

function button(label, cls, handler) {
	return E('button', {
		'class': 'btn cbi-button ' + (cls || 'cbi-button-action'),
		'click': ui.createHandlerFn(null, handler)
	}, label);
}

return baseclass.extend({
	call: call,
	loadStyle: loadStyle,
	notify: notify,
	finite: finite,
	format: format,
	gauge: gauge,
	updateGauge: updateGauge,
	metric: metric,
	serviceState: serviceState,
	button: button
});
