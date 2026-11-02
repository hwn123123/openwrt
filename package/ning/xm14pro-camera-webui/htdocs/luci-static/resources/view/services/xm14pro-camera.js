'use strict';
'require view';
'require fs';
'require ui';

var CONTROL = '/usr/libexec/xm14pro-camera/control';

function query(action, camera) {
	return fs.exec(CONTROL, camera == null ? [ action ] : [ action, String(camera) ]).then(function(result) {
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

function hardwareContent(data, update) {
	var driverLoaded = !!data.driver_loaded;
	var cvpReady = !!data.cvp_ready;
	var loaded = driverLoaded && cvpReady;
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
	}, loaded ? '驱动已加载' : '加载缺失的摄像头驱动');
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
			row('内核驱动', driverLoaded ? '已加载' : '未加载'),
			row('CVP 处理器', cvpReady ? '已就绪' : '未就绪'),
			row('ICP 固件', data.firmware_ready ? '已安装' : '缺失'),
			row('EVA 固件', data.eva_firmware_ready ? '已安装' : '缺失'),
			row('防抖固件', data.ois_firmware_ready ? '已安装' : '缺失'),
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

function imageUrl(data) {
	if (data.path && /^\/tmp\/xm14pro-camera\/[A-Za-z0-9_.-]+\.jpg$/.test(data.path))
		return fs.read_direct(data.path, 'blob').then(function(blob) { return URL.createObjectURL(blob); });
	if (data.image && (!data.mime || data.mime === 'image/jpeg'))
		return Promise.resolve('data:image/jpeg;base64,' + data.image);
	return Promise.reject(new Error('摄像头没有返回可读取的 JPEG'));
}

function releaseImage(url) {
	if (url && url.indexOf('blob:') === 0) URL.revokeObjectURL(url);
}

return view.extend({
	load: function() { return query('status'); },
	render: function(initial) {
		var state = { data: initial, camera: '2', busy: false, url: null };
		var hardware = E('div');
		var picker = E('div', { 'class': 'xm-camera-picker' });
		var preview = E('div', { 'class': 'xm-camera-preview' }, [
			E('div', { 'class': 'xm-camera-placeholder' }, '选择镜头后点击“刷新画面”')
		]);
		var message = E('p', { 'class': 'xm-camera-message', 'role': 'status' });
		var refreshImage;
		var saveImage;

		function errorText(error) {
			var value = error && error.message ? error.message : String(error);
			message.textContent = value;
			ui.addNotification(null, E('p', {}, value), 'error');
		}
		function buttons() {
			refreshImage.disabled = state.busy || !state.data.capture_ready;
			saveImage.disabled = state.busy || !state.data.capture_ready;
			picker.querySelectorAll('button').forEach(function(button) { button.disabled = state.busy; });
		}
		function drawPicker() {
			var cameras = Array.isArray(state.data.cameras) ? state.data.cameras : [];
			if (cameras.length && !cameras.some(function(camera) { return String(camera.id) === state.camera; }))
				state.camera = String(cameras[0].id);
			picker.replaceChildren.apply(picker, cameras.map(function(camera) {
				var id = String(camera.id);
				return E('button', {
					'type': 'button',
					'class': 'btn xm-camera-lens' + (id === state.camera ? ' selected' : ''),
					'click': function() {
						state.camera = id;
						drawPicker();
						message.textContent = '已选择' + camera.name + '；点击“刷新画面”拍摄当前画面。';
					}
				}, camera.name || ('镜头 ' + id));
			}));
			buttons();
		}
		function capture(save) {
			if (state.busy || !state.data.capture_ready) return;
			state.busy = true;
			buttons();
			message.textContent = '正在取景，单帧约需 10 秒…';
			var camera = state.camera;
			return query('snapshot', camera).then(imageUrl).then(function(url) {
				var old = state.url;
				state.url = url;
				preview.replaceChildren(E('img', { 'src': url, 'alt': '最近拍摄的画面' }));
				releaseImage(old);
				message.textContent = '画面已更新。此预览为逐帧拍摄。';
				if (save) {
					var link = E('a', {
						'href': url,
						'download': 'nwrt-camera-' + camera + '-' + new Date().toISOString().replace(/[:.]/g, '-') + '.jpg'
					});
					document.body.appendChild(link);
					link.click();
					link.remove();
				}
			}, errorText).finally(function() {
				state.busy = false;
				buttons();
			});
		}
		function update(data) {
			state.data = data;
			drawPicker();
			buttons();
			if (!data.capture_ready) message.textContent = data.capture_detail || '摄像头取流尚不可用';
			else if (!state.url) message.textContent = '摄像头已就绪。逐帧拍摄约需 10 秒。';
			hardware.replaceChildren.apply(hardware, hardwareContent(data, update));
		}

		refreshImage = E('button', {
			'type': 'button', 'class': 'btn cbi-button',
			'click': function() { capture(false); }
		}, '刷新画面');
		saveImage = E('button', {
			'type': 'button', 'class': 'btn cbi-button cbi-button-action',
			'click': function() { capture(true); }
		}, '拍照并保存');
		update(initial);
		return E('div', {}, [
			E('style', {}, '.xm-camera-picker{display:flex;flex-wrap:wrap;gap:.5rem;margin:1rem 0}.xm-camera-lens.selected{background:#5da9a0;color:#fff}.xm-camera-preview{display:flex;align-items:center;justify-content:center;min-height:240px;max-height:62vh;background:#121719;border-radius:12px;overflow:hidden}.xm-camera-preview img{display:block;max-width:100%;max-height:62vh;object-fit:contain}.xm-camera-placeholder{color:#b5c3c6;text-align:center;padding:2rem}.xm-camera-actions{display:flex;flex-wrap:wrap;gap:.6rem;margin:1rem 0}.xm-camera-message{min-height:1.5em}'),
			E('h2', {}, '摄像头'),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, '拍照'),
				E('p', { 'class': 'cbi-section-descr' }, '选择镜头，刷新逐帧画面，或拍照并保存到当前设备。'),
				picker,
				preview,
				E('div', { 'class': 'xm-camera-actions' }, [ refreshImage, saveImage ]),
				message,
				E('p', { 'class': 'cbi-section-descr' }, '连续视频预览和录像仍在适配中。')
			]),
			E('details', {}, [ E('summary', {}, '硬件与诊断'), hardware ])
		]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
