'use strict';
'require view';
'require fs';
'require ui';

var CONTROL = '/usr/libexec/xm14pro-camera/control';
var READ_CONTROL = '/usr/libexec/xm14pro-camera/read-control';
var LIVE_PATH = '/mnt/nwrt-data/camera-android-root/data/local/tmp/nwrt-live.jpg';
var PHOTO_PATH = /^\/mnt\/nwrt-data\/camera\/photos\/photo-[A-Za-z0-9_.-]+\.jpg$/;
var VIDEO_PATH = /^\/mnt\/nwrt-data\/camera\/videos\/video-[A-Za-z0-9_.-]+\.mp4$/;

function query(action, first, second) {
	var args = [ action ];
	if (first != null) args.push(String(first));
	if (second != null) args.push(String(second));
	var readOnly = action === 'status' || action === 'list' ||
		action === 'preview-status' || action === 'video-list';
	return fs.exec(readOnly ? READ_CONTROL : CONTROL, args).then(function(result) {
		var data;
		try { data = JSON.parse((result && result.stdout) || '{}'); }
		catch (e) { throw new Error('摄像头后端返回了无效数据'); }
		if (!data.ok) throw new Error(data.error || '摄像头操作失败');
		return data;
	});
}

function wait(ms) {
	return new Promise(function(resolve) { setTimeout(resolve, ms); });
}

function sizeText(bytes) {
	return bytes >= 1048576 ? (bytes / 1048576).toFixed(1) + ' MB' : Math.ceil(bytes / 1024) + ' KB';
}

function release(url) {
	if (url && url.indexOf('blob:') === 0) URL.revokeObjectURL(url);
}

function downloadBlob(blob, name) {
	var url = URL.createObjectURL(blob);
	var link = E('a', { 'href': url, 'download': name });
	document.body.appendChild(link);
	link.click();
	link.remove();
	setTimeout(function() { release(url); }, 60000);
}

function row(label, value) {
	return E('div', { 'class': 'cbi-value' }, [
		E('label', { 'class': 'cbi-value-title' }, label),
		E('div', { 'class': 'cbi-value-field' }, value)
	]);
}

return view.extend({
	load: function() { return query('status'); },
	render: function(initial) {
		var state = {
			data: initial, camera: '2', epoch: 0, closed: false, starting: false,
			streaming: false, previewReady: false, recording: false, busy: false,
			frameUrl: null, viewUrl: null,
			frameTimer: null, heartbeatTimer: null, stopPromise: null, missingFrames: 0,
			heartbeatErrors: 0, wasMounted: false
		};
		var root, picture = E('img', { 'alt': '实时摄像头画面' });
		var placeholder = E('div', { 'class': 'xm-camera-placeholder' }, '正在准备相机…');
		var preview = E('div', { 'class': 'xm-camera-preview' }, placeholder);
		var message = E('p', { 'class': 'xm-camera-message', 'role': 'status' });
		var picker = E('div', { 'class': 'xm-camera-picker' });
		var hardware = E('div');
		var photos = E('div', {}, '正在读取照片…');
		var photoViewer = E('div', { 'class': 'xm-camera-viewer' });
		var videos = E('div', {}, '正在读取视频…');
		var photoCount = E('span'), videoCount = E('span');
		var startButton, photoButton, recordButton, stopButton, reloadButton;
		var observer;

		function say(error) {
			var value = error && error.message ? error.message : String(error);
			message.textContent = value;
			ui.addNotification(null, E('p', {}, value), 'error');
		}

		function buttons() {
			startButton.disabled = state.starting || state.streaming || state.busy || state.closed;
			startButton.style.display = state.streaming ? 'none' : '';
			photoButton.disabled = !state.streaming || state.starting || state.busy;
			recordButton.disabled = !state.streaming || state.starting || state.busy;
			recordButton.textContent = state.recording ? '停止录像' : '开始录像';
			stopButton.disabled = !state.streaming && !state.starting;
			reloadButton.disabled = state.busy;
			picker.querySelectorAll('button').forEach(function(button) {
				button.disabled = state.starting || state.busy || state.recording;
			});
		}

		function renderHardware() {
			var data = state.data;
			hardware.replaceChildren(
				row('内核驱动', data.driver_loaded ? '已加载' : '未加载'),
				row('CVP 处理器', data.cvp_ready ? '已就绪' : '未就绪'),
				row('相机服务', data.service_ready ? '已就绪' : '未启动'),
				row('媒体设备', String(data.media_nodes || 0)),
				row('摄像头子设备', String(data.subdevices || 0)),
				data.last_kernel_error ? E('pre', {}, data.last_kernel_error) : E('span')
			);
		}

		function drawPicker() {
			var cameras = Array.isArray(state.data.cameras) ? state.data.cameras : [];
			picker.replaceChildren.apply(picker, cameras.map(function(camera) {
				var id = String(camera.id);
				return E('button', {
					'type': 'button',
					'class': 'btn xm-camera-lens' + (id === state.camera ? ' selected' : ''),
					'click': function() {
						if (id === state.camera || state.starting || state.recording) return;
						state.camera = id;
						drawPicker();
						startPreview(true);
					}
				}, camera.name || ('镜头 ' + id));
			}));
			buttons();
		}

		function clearImage() {
			picture.removeAttribute('src');
			release(state.frameUrl);
			state.frameUrl = null;
			preview.replaceChildren(placeholder);
		}

		function stopPreview(final) {
			state.epoch++;
			state.starting = false;
			state.streaming = false;
			state.previewReady = false;
			state.recording = false;
			if (state.frameTimer) clearTimeout(state.frameTimer);
			if (state.heartbeatTimer) clearTimeout(state.heartbeatTimer);
			state.frameTimer = null;
			state.heartbeatTimer = null;
			clearImage();
			placeholder.textContent = final ? '相机已关闭' : '相机已停止';
			if (final) state.closed = true;
			if (final) release(state.viewUrl);
			buttons();
			state.stopPromise = (state.stopPromise || Promise.resolve()).then(function() {
				return query('preview-stop').catch(function() {});
			});
			return state.stopPromise;
		}

		function frameLoop(token) {
			if (token !== state.epoch || !state.streaming || document.hidden) return;
			if (!state.previewReady) {
				state.frameTimer = setTimeout(function() { frameLoop(token); }, 400);
				return;
			}
			fs.read_direct(LIVE_PATH, 'blob').then(function(blob) {
				if (token !== state.epoch || !state.streaming) return;
				if (!blob || !blob.size) {
					state.missingFrames++;
					return;
				}
				var url = URL.createObjectURL(blob);
				var old = state.frameUrl;
				state.frameUrl = url;
				picture.onload = function() { release(old); };
				picture.onerror = function() { release(old); };
				setTimeout(function() { release(old); }, 3000);
				picture.src = url;
				if (preview.firstChild !== picture) preview.replaceChildren(picture);
				state.missingFrames = 0;
			}, function() {
				state.missingFrames++;
				if (state.missingFrames > 8) message.textContent = '取景暂时中断，正在重试…';
			}).finally(function() {
				if (token === state.epoch && state.streaming && !document.hidden)
					state.frameTimer = setTimeout(function() { frameLoop(token); }, 150);
			});
		}

		function heartbeat(token) {
			if (token !== state.epoch || !state.streaming || document.hidden) return;
			query('preview-heartbeat').then(function(data) {
				if (token !== state.epoch) return;
				state.heartbeatErrors = 0;
				if (!data.preview_active) {
					message.textContent = '相机取景已停止';
					stopPreview(false);
					return;
				}
				state.previewReady = !!data.preview_ready && data.frame_age <= 4;
				if (!state.previewReady) {
					clearImage();
					placeholder.textContent = '正在等待新画面…';
					message.textContent = '正在等待新画面…';
				}
			}, function(error) {
				if (token === state.epoch) {
					state.heartbeatErrors++;
					if (state.heartbeatErrors >= 3) {
						say(error);
						stopPreview(false);
					}
				}
			}).finally(function() {
				if (token === state.epoch && state.streaming)
					state.heartbeatTimer = setTimeout(function() { heartbeat(token); }, 5000);
			});
		}

		async function waitForService(token) {
			for (var attempt = 0; attempt < 75; attempt++) {
				if (token !== state.epoch || state.closed || document.hidden) return false;
				var data = await query('status');
				state.data = data;
				renderHardware();
				if (data.service_ready) return true;
				if (attempt > 2 && !data.service_starting)
					throw new Error(data.capture_detail || '相机服务启动失败');
				await wait(2000);
			}
			throw new Error('相机服务启动超时');
		}

		async function startPreview(switchLens) {
			if (state.closed || document.hidden || state.starting || state.busy) return;
			if (state.streaming && !switchLens) return;
			var token = ++state.epoch;
			state.starting = true;
			state.streaming = false;
			if (state.frameTimer) clearTimeout(state.frameTimer);
			if (state.heartbeatTimer) clearTimeout(state.heartbeatTimer);
			buttons();
			message.textContent = '正在开启摄像头，首次启动约需一分钟…';
			try {
				if (state.stopPromise) await state.stopPromise;
				if (token !== state.epoch) return;
				if (!state.data.driver_loaded || !state.data.cvp_ready) {
					state.data = await query('load');
					renderHardware();
				}
				if (token !== state.epoch) return;
				if (!state.data.service_ready) {
					await query('start');
					if (!await waitForService(token)) return;
				}
				if (token !== state.epoch) return;
				await query('preview-start', state.camera);
				for (var attempt = 0; attempt < 25; attempt++) {
					if (token !== state.epoch || document.hidden) return;
					var previewStatus = await query('preview-status');
					if (previewStatus.preview_ready && previewStatus.preview_camera === state.camera) break;
					if (!previewStatus.preview_active && attempt > 2)
						throw new Error('相机取景进程已退出');
					await wait(1000);
				}
				if (attempt === 25) throw new Error('等待实时画面超时');
				if (token !== state.epoch) return;
				state.streaming = true;
				state.previewReady = true;
				state.missingFrames = 0;
				state.heartbeatErrors = 0;
				message.textContent = '实时取景中';
				frameLoop(token);
				heartbeat(token);
			} catch (error) {
				if (token === state.epoch) {
					say(error);
					stopPreview(false);
				}
			} finally {
				if (token === state.epoch) {
					state.starting = false;
					buttons();
				}
			}
		}

		function photo() {
			if (!state.streaming || state.busy) return;
			state.busy = true;
			buttons();
			query('preview-photo', state.camera).then(function() {
				message.textContent = '照片已保存到设备照片目录';
				refreshPhotos();
			}, say).finally(function() {
				state.busy = false;
				buttons();
			});
		}

		function toggleRecord() {
			if (!state.streaming || state.busy) return;
			state.busy = true;
			buttons();
			query(state.recording ? 'record-stop' : 'record-start', state.camera).then(function(data) {
				state.recording = !!data.recording;
				message.textContent = state.recording ? '正在录像，离开页面会自动停止' : '录像已保存';
				if (!state.recording) refreshVideos();
			}, say).finally(function() {
				state.busy = false;
				buttons();
			});
		}

		function refreshPhotos() {
			return query('list').then(function(data) {
				var entries = Array.isArray(data.photos) ? data.photos : [];
				entries.sort(function(a, b) { return (b.modified || 0) - (a.modified || 0); });
				photoCount.textContent = entries.length + ' 张';
				photos.replaceChildren.apply(photos, entries.map(function(item) {
					var name = String(item.name || '');
					if (!PHOTO_PATH.test(item.path || '')) return E('span');
					return E('div', { 'class': 'xm-camera-file' }, [
						E('span', {}, name + ' · ' + sizeText(Number(item.size) || 0)),
						E('button', { 'class': 'btn', 'click': function() {
							fs.read_direct(item.path, 'blob').then(function(blob) {
								release(state.viewUrl);
								state.viewUrl = URL.createObjectURL(blob);
								photoViewer.replaceChildren(
									E('img', { 'src': state.viewUrl, 'alt': name }),
									E('button', { 'class': 'btn', 'click': function() {
										release(state.viewUrl);
										state.viewUrl = null;
										photoViewer.replaceChildren();
									} }, '关闭照片')
								);
							}, say);
						} }, '查看'),
						E('button', { 'class': 'btn', 'click': function() {
							fs.read_direct(item.path, 'blob').then(function(blob) { downloadBlob(blob, name); }, say);
						} }, '下载'),
						E('button', { 'class': 'btn cbi-button-negative', 'click': function() {
							if (window.confirm('删除照片“' + name + '”？'))
								query('delete', name).then(refreshPhotos, say);
						} }, '删除')
					]);
				}));
				if (!entries.length) photos.textContent = '还没有照片';
			}, say);
		}

		function refreshVideos() {
			return query('video-list').then(function(data) {
				var entries = Array.isArray(data.videos) ? data.videos : [];
				entries.sort(function(a, b) { return (b.modified || 0) - (a.modified || 0); });
				videoCount.textContent = entries.length + ' 段';
				videos.replaceChildren.apply(videos, entries.map(function(item) {
					var name = String(item.name || '');
					if (!VIDEO_PATH.test(item.path || '')) return E('span');
					return E('div', { 'class': 'xm-camera-file' }, [
						E('span', {}, name + ' · ' + sizeText(Number(item.size) || 0)),
						E('button', { 'class': 'btn', 'click': function() {
							fs.read_direct(item.path, 'blob').then(function(blob) { downloadBlob(blob, name); }, say);
						} }, '下载'),
						E('button', { 'class': 'btn cbi-button-negative', 'click': function() {
							if (window.confirm('删除录像“' + name + '”？'))
								query('video-delete', name).then(refreshVideos, say);
						} }, '删除')
					]);
				}));
				if (!entries.length) videos.textContent = '还没有录像';
			}, function(error) { videos.textContent = error.message || String(error); });
		}

		function onVisibility() {
			if (document.hidden) stopPreview(false);
			else if (!state.closed) {
				refreshVideos();
				startPreview(false);
			}
		}
		function onPageHide() { stopPreview(true); }
		function onPageShow(event) {
			if (event.persisted && root && document.body.contains(root)) {
				state.closed = false;
				buttons();
				if (!document.hidden) startPreview(false);
			}
		}
		function onNavigation() {
			if (root && document.body.contains(root)) state.wasMounted = true;
			if (state.wasMounted && !document.body.contains(root)) {
				stopPreview(true);
				observer.disconnect();
				document.removeEventListener('visibilitychange', onVisibility);
				window.removeEventListener('pagehide', onPageHide);
				window.removeEventListener('pageshow', onPageShow);
			}
		}

		startButton = E('button', { 'type': 'button', 'class': 'btn cbi-button-action',
			'click': function() { startPreview(false); } }, '开启取景');
		photoButton = E('button', { 'type': 'button', 'class': 'btn cbi-button-action',
			'click': photo }, '拍照');
		recordButton = E('button', { 'type': 'button', 'class': 'btn cbi-button-negative',
			'click': toggleRecord }, '开始录像');
		stopButton = E('button', { 'type': 'button', 'class': 'btn',
			'click': function() { stopPreview(false); } }, '关闭摄像头');
		reloadButton = E('button', { 'type': 'button', 'class': 'btn',
			'click': function() { query('status').then(function(data) {
				state.data = data;
				renderHardware();
			}, say); } }, '刷新状态');
		root = E('div', {}, [
			E('style', {}, '.xm-camera-picker,.xm-camera-actions{display:flex;flex-wrap:wrap;align-items:center;gap:.6rem;margin:1rem 0}.xm-camera-lens.selected{background:#5da9a0;color:#fff}.xm-camera-preview{display:flex;align-items:center;justify-content:center;min-height:260px;max-height:66vh;background:#121719;border-radius:12px;overflow:hidden}.xm-camera-preview img,.xm-camera-viewer img{display:block;max-width:100%;max-height:66vh;object-fit:contain}.xm-camera-placeholder{color:#b5c3c6;text-align:center;padding:2rem}.xm-camera-message{min-height:1.5em}.xm-camera-file{display:flex;align-items:center;gap:.7rem;border-top:1px solid #555;padding:.55rem 0}.xm-camera-file span{flex:1;overflow-wrap:anywhere}.xm-camera-viewer{display:grid;justify-items:start;gap:.5rem;margin:1rem 0}'),
			E('h2', {}, '摄像头'),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, '实时取景'),
				picker, preview, message,
				E('div', { 'class': 'xm-camera-actions' }, [
					startButton, photoButton, recordButton, stopButton
				]),
				E('p', { 'class': 'cbi-section-descr' }, '进入页面自动开启取景。切换页面或锁屏时自动关闭；也可手动关闭。')
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, [ '设备照片 · ', photoCount ]),
				E('p', { 'class': 'cbi-section-descr' }, '照片保存在 B 槽 /mnt/nwrt-data/camera/photos。'),
				E('button', { 'class': 'btn', 'click': refreshPhotos }, '刷新照片'),
				photoViewer,
				photos
			]),
			E('div', { 'class': 'cbi-section' }, [
				E('h3', {}, [ '设备录像 · ', videoCount ]),
				E('p', { 'class': 'cbi-section-descr' }, '录像保存在 B 槽 /mnt/nwrt-data/camera/videos。当前录像无声音。'),
				E('button', { 'class': 'btn', 'click': refreshVideos }, '刷新录像'),
				videos
			]),
			E('details', {}, [ E('summary', {}, '硬件与诊断'), reloadButton, hardware ])
		]);
		buttons();
		drawPicker();
		renderHardware();
		refreshPhotos();
		refreshVideos();
		document.addEventListener('visibilitychange', onVisibility);
		window.addEventListener('pagehide', onPageHide);
		window.addEventListener('pageshow', onPageShow);
		observer = new MutationObserver(onNavigation);
		observer.observe(document.body, { childList: true, subtree: true });
		setTimeout(function() { if (!state.closed && !document.hidden) startPreview(false); }, 0);
		return root;
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
