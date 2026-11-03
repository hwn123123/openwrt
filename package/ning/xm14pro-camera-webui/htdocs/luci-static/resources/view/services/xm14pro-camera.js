'use strict';
'require view';
'require fs';
'require ui';

var CONTROL = '/usr/libexec/xm14pro-camera/control';
var READ_CONTROL = '/usr/libexec/xm14pro-camera/read-control';

function query(action, first, second) {
	var args = [ action ];
	if (first != null) args.push(String(first));
	if (second != null) args.push(String(second));
	return fs.exec(action === 'status' || action === 'list' ? READ_CONTROL : CONTROL, args).then(function(result) {
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
			row('取流与拍照', capture ? '已验证可用' : data.capture_possible ? '待验证' : '待适配'),
			E('p', { 'class': 'cbi-section-descr' }, data.capture_detail || '')
		]),
		data.last_kernel_error ? E('div', { 'class': 'cbi-section' }, [
			E('h3', {}, '最近的摄像头内核报错'),
			E('pre', {}, data.last_kernel_error)
		]) : null
	].filter(Boolean);
}

function imageUrl(data) {
	if (data.path && (/^\/tmp\/xm14pro-camera\/[A-Za-z0-9_.-]+\.jpg$/.test(data.path) ||
		/^\/mnt\/nwrt-data\/camera\/photos\/photo-[A-Za-z0-9_.-]+\.jpg$/.test(data.path)))
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
		var state = { data: initial, camera: '2', busy: false, url: null, photo: null, rotation: 0, mirrored: false };
		var hardware = E('div');
		var picker = E('div', { 'class': 'xm-camera-picker' });
		var preview = E('div', { 'class': 'xm-camera-preview' }, [
			E('div', { 'class': 'xm-camera-placeholder' }, '选择镜头后拍摄一帧查看画面')
		]);
		var message = E('p', { 'class': 'xm-camera-message', 'role': 'status' });
		var gallery = E('div', {}, '正在读取设备相册…');
		var count = E('span');
		var timer = E('select', { 'class': 'cbi-input-select' }, [
			E('option', { 'value': '0' }, '立即'),
			E('option', { 'value': '3' }, '3 秒'),
			E('option', { 'value': '10' }, '10 秒')
		]);
		var refreshImage, saveImage, downloadImage, loadDriver, captureActions, previewActions, captureHelp;

		function sizeText(bytes) {
			return bytes >= 1048576 ? (bytes / 1048576).toFixed(1) + ' MB' : Math.ceil(bytes / 1024) + ' KB';
		}
		function applyTransform() {
			var img = preview.querySelector('img');
			if (img) img.style.transform = 'rotate(' + state.rotation + 'deg) scaleX(' + (state.mirrored ? -1 : 1) + ')';
		}
		function showImage(url, name) {
			var old = state.url;
			state.url = url;
			state.photo = name || null;
			state.rotation = 0;
			state.mirrored = false;
			preview.replaceChildren(E('img', { 'src': url, 'alt': '摄像头照片' }));
			releaseImage(old);
			buttons();
		}
		function download(url, name) {
			var link = E('a', { 'href': url, 'download': name });
			document.body.appendChild(link);
			link.click();
			link.remove();
		}

		function errorText(error) {
			var value = error && error.message ? error.message : String(error);
			message.textContent = value;
			ui.addNotification(null, E('p', {}, value), 'error');
		}
		function buttons() {
			refreshImage.disabled = state.busy || !state.data.capture_possible;
			saveImage.disabled = state.busy || !state.data.capture_possible;
			downloadImage.disabled = state.busy || !state.url;
			downloadImage.style.display = state.url ? '' : 'none';
			loadDriver.disabled = state.busy || (!!state.data.driver_loaded && !!state.data.cvp_ready);
			loadDriver.textContent = state.data.driver_loaded && state.data.cvp_ready ? '驱动已加载' : '加载摄像头驱动';
			timer.disabled = state.busy;
			picker.querySelectorAll('button').forEach(function(button) { button.disabled = state.busy || !state.data.capture_possible; });
			picker.style.display = state.data.capture_possible ? '' : 'none';
			if (captureActions) captureActions.style.display = state.data.capture_possible ? '' : 'none';
			if (previewActions) previewActions.style.display = state.url ? '' : 'none';
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
						if (state.data.capture_possible) message.textContent = '已选择' + camera.name + '。';
					}
				}, camera.name || ('镜头 ' + id));
			}));
			buttons();
		}
		function refreshGallery() {
			return query('list').then(function(data) {
				var photos = Array.isArray(data.photos) ? data.photos : [];
				photos.sort(function(a, b) { return (b.modified || 0) - (a.modified || 0); });
				count.textContent = photos.length + ' 张照片';
				if (!photos.length) {
					gallery.replaceChildren(E('p', { 'class': 'cbi-section-descr' }, '设备上还没有保存的照片。'));
					return;
				}
				gallery.replaceChildren.apply(gallery, photos.map(function(photo) {
					var name = String(photo.name || '');
					var when = photo.modified ? new Date(photo.modified * 1000).toLocaleString() : '';
					return E('div', { 'class': 'xm-camera-photo' }, [
						E('span', { 'class': 'xm-camera-photo-name' }, [
							E('strong', {}, name),
							E('small', {}, when + ' · ' + sizeText(Number(photo.size) || 0))
						]),
						E('button', { 'type': 'button', 'class': 'btn', 'click': function() {
							imageUrl(photo).then(function(url) {
								showImage(url, name);
								message.textContent = '正在查看设备上保存的照片。';
							}, errorText);
						} }, '查看'),
						E('button', { 'type': 'button', 'class': 'btn', 'click': function() {
							imageUrl(photo).then(function(url) {
								download(url, name);
								setTimeout(function() { releaseImage(url); }, 60000);
							}, errorText);
						} }, '下载'),
						E('button', { 'type': 'button', 'class': 'btn cbi-button-negative', 'click': function() {
							if (!window.confirm('永久删除设备上的照片“' + name + '”？')) return;
							query('delete', name).then(function() {
								if (state.photo === name) {
									releaseImage(state.url);
									state.url = null;
									state.photo = null;
									preview.replaceChildren(E('div', { 'class': 'xm-camera-placeholder' }, '照片已删除'));
									buttons();
								}
								refreshGallery();
							}, errorText);
						} }, '删除')
					]);
				}));
			}, errorText);
		}
		function capture(save) {
			if (state.busy || !state.data.capture_possible) return;
			state.busy = true;
			buttons();
			var seconds = Number(timer.value) || 0;
			message.textContent = seconds ? '等待 ' + seconds + ' 秒后拍摄…' : '正在取景，单帧约需 10 秒…';
			var camera = state.camera;
			return new Promise(function(resolve) { setTimeout(resolve, seconds * 1000); }).then(function() {
				message.textContent = '正在取景，单帧约需 10 秒…';
				return query('snapshot', camera, save ? 'save' : 'preview');
			}).then(function(data) {
				return imageUrl(data).then(function(url) { return { data: data, url: url }; });
			}).then(function(result) {
				showImage(result.url, result.data.name);
				message.textContent = save ? '照片已保存到设备相册。' : '画面已更新；这是单帧预览。';
				if (save) refreshGallery();
				query('status').then(function(data) { update(data, true); }, function() {});
			}, errorText).finally(function() {
				state.busy = false;
				buttons();
			});
		}
		function update(data, keepMessage) {
			state.data = data;
			drawPicker();
			buttons();
			if (!keepMessage) message.textContent = data.capture_detail || '摄像头取流尚不可用';
			if (!state.url) preview.replaceChildren(E('div', { 'class': 'xm-camera-placeholder' },
				data.capture_possible ? '点击“拍一帧预览”查看画面' : '摄像头服务未就绪，暂时无法预览和拍照'));
			captureHelp.textContent = data.capture_possible ? '选择镜头后拍摄一帧；连续实时预览尚未适配。' :
				'相机服务尚未就绪。预览和拍照功能会在服务恢复后开放。';
			hardware.replaceChildren.apply(hardware, hardwareContent(data, update));
		}

		loadDriver = E('button', {
			'type': 'button', 'class': 'btn cbi-button-action',
			'click': function() {
				state.busy = true;
				buttons();
				query('load').then(function(data) { update(data); }, errorText).finally(function() {
					state.busy = false;
					buttons();
				});
			}
		}, '加载摄像头驱动');
		refreshImage = E('button', {
			'type': 'button', 'class': 'btn cbi-button',
			'click': function() { capture(false); }
		}, '拍一帧预览');
		saveImage = E('button', {
			'type': 'button', 'class': 'btn cbi-button cbi-button-action',
			'click': function() { capture(true); }
		}, '拍照保存到设备');
		downloadImage = E('button', {
			'type': 'button', 'class': 'btn',
			'click': function() {
				if (state.url) download(state.url, state.photo || 'nwrt-camera-' + new Date().toISOString().replace(/[:.]/g, '-') + '.jpg');
			}
		}, '下载当前照片');
		captureHelp = E('p', { 'class': 'cbi-section-descr' });
		captureActions = E('div', { 'class': 'xm-camera-actions' }, [ '倒计时 ', timer, refreshImage, saveImage ]);
		previewActions = E('div', { 'class': 'xm-camera-actions' }, [
			downloadImage,
			E('button', { 'type': 'button', 'class': 'btn', 'click': function() { state.rotation = (state.rotation + 90) % 360; applyTransform(); } }, '预览旋转 90°'),
			E('button', { 'type': 'button', 'class': 'btn', 'click': function() { state.mirrored = !state.mirrored; applyTransform(); } }, '预览镜像')
		]);
		update(initial);
		refreshGallery();
		return E('div', {}, [
			E('style', {}, '.xm-camera-picker,.xm-camera-actions{display:flex;flex-wrap:wrap;align-items:center;gap:.6rem;margin:1rem 0}.xm-camera-lens.selected{background:#5da9a0;color:#fff}.xm-camera-preview{display:flex;align-items:center;justify-content:center;min-height:240px;max-height:62vh;background:#121719;border-radius:12px;overflow:hidden}.xm-camera-preview img{display:block;max-width:100%;max-height:62vh;object-fit:contain}.xm-camera-placeholder{color:#b5c3c6;text-align:center;padding:2rem}.xm-camera-message{min-height:1.5em}.xm-camera-photo{display:flex;flex-wrap:wrap;align-items:center;gap:.5rem;border-top:1px solid #555;padding:.55rem 0}.xm-camera-photo-name{flex:1 1 240px;overflow-wrap:anywhere}.xm-camera-photo-name small{display:block;opacity:.7}'),
			E('h2', {}, '摄像头'),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, '相机状态'),
				message,
				E('div', { 'class': 'xm-camera-actions' }, [
					loadDriver,
					E('button', { 'type': 'button', 'class': 'btn', 'click': function() { query('status').then(update, errorText); } }, '检查状态')
				])
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, '拍照'),
				captureHelp,
				picker,
				preview,
				captureActions,
				previewActions,
				E('p', { 'class': 'cbi-section-descr' }, '旋转和镜像只改变网页预览，不修改设备上保存的原图。')
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, [ '设备相册 · ', count ]),
				E('p', { 'class': 'cbi-section-descr' }, '照片默认保存在 B 槽 /mnt/nwrt-data/camera/photos。点击“下载”可保存到当前浏览器的下载目录；“删除”会永久移除设备上的原图。'),
				E('button', { 'type': 'button', 'class': 'btn', 'click': refreshGallery }, '刷新相册'),
				gallery
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, '录像'),
				E('p', { 'class': 'cbi-section-descr' }, '当前相机后端只支持单帧 JPEG；连续取流和视频编码尚未适配，因此还不能录像或保存视频文件。')
			]),
			E('details', {}, [ E('summary', {}, '硬件与诊断'), hardware ])
		]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
