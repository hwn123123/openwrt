'use strict';
'require view';
'require fs';
'require ui';

var CONTROL = '/usr/libexec/xm14pro-camera/control';

function query(action) {
	return fs.exec(CONTROL, [ action ]).then(function(result) {
		var data;
		try { data = JSON.parse((result && result.stdout) || '{}'); }
		catch (e) { throw new Error('摄像头后端返回了无效数据'); }
		if (!data.ok) throw new Error(data.error || '摄像头操作失败');
		return data;
	});
}

function row(label, value) {
	return E('div', { 'class': 'cbi-value' }, [
		E('label', { 'class': 'cbi-value-title' }, label),
		E('div', { 'class': 'cbi-value-field' }, value)
	]);
}

function content(data, update) {
	var loaded = !!data.driver_loaded;
	var capture = !!data.capture_ready;
	var loadButton = E('button', {
		'class': 'btn cbi-button cbi-button-action',
		'type': 'button',
		'disabled': loaded ? 'disabled' : null,
		'click': function() {
			loadButton.disabled = true;
			query('load').then(update, function(error) {
				loadButton.disabled = false;
				ui.addNotification(null, E('p', {}, error.message || String(error)), 'error');
			});
		}
	}, loaded ? '驱动已加载' : '加载摄像头驱动');
	var refreshButton = E('button', {
		'class': 'btn cbi-button',
		'type': 'button',
		'click': function() {
			refreshButton.disabled = true;
			query('status').then(update, function(error) {
				ui.addNotification(null, E('p', {}, error.message || String(error)), 'error');
			}).finally(function() { refreshButton.disabled = false; });
		}
	}, '刷新状态');
	return [
		E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, '硬件状态'),
			row('内核驱动', loaded ? '已加载' : '未加载'),
			row('媒体设备', String(data.media_nodes || 0)),
			row('视频设备', String(data.video_nodes || 0)),
			row('摄像头子设备', String(data.subdevices || 0)),
			E('div', { 'class': 'cbi-page-actions' }, [ loadButton, ' ', refreshButton ])
		]),
		E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, '拍摄服务'),
			row('取流与拍照', capture ? '可用' : '待适配'),
			E('p', { 'class': 'cbi-section-descr' }, data.capture_detail || '')
		]),
		data.last_kernel_error ? E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, '最近的摄像头内核报错'),
			E('pre', {}, data.last_kernel_error)
		]) : null
	].filter(Boolean);
}

return view.extend({
	load: function() { return query('status'); },
	render: function(initial) {
		var body = E('div');
		function update(data) { body.replaceChildren.apply(body, content(data, update)); }
		update(initial);
		return E('div', {}, [ E('h2', {}, '摄像头'), body ]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
