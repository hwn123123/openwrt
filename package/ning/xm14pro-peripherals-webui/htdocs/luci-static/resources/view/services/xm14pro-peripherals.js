'use strict';
'require view';
'require fs';
'require poll';
'require dom';

var CONTROL = '/usr/libexec/xm14pro-peripherals/control';
var noticeTimer;

function addStyle() {
	if (document.getElementById('xm14-peripherals-style')) return;
	var link = document.createElement('link');
	link.id = 'xm14-peripherals-style';
	link.rel = 'stylesheet';
	link.href = '/luci-static/resources/xm14pro/peripherals.css?v=8';
	document.head.appendChild(link);
}

function parse(result) {
	var data;
	try { data = JSON.parse((result && result.stdout) || '{}'); }
	catch (e) { throw new Error('服务返回了无效数据'); }
	if (data.ok === false) throw new Error(data.error || '操作失败');
	return data;
}

function run(args) {
	return fs.exec(CONTROL, args || []).then(parse);
}

function notice(message, error) {
	var node = document.getElementById('xm14-peripherals-notice');
	if (!node) {
		node = E('button', { 'id': 'xm14-peripherals-notice', 'type': 'button', 'click': function() { node.classList.remove('show'); } }, [ E('i'), E('span') ]);
		document.body.appendChild(node);
	}
	node.className = error ? 'is-error' : 'is-success';
	node.querySelector('span').textContent = String(message || '');
	window.clearTimeout(noticeTimer);
	window.requestAnimationFrame(function() { node.classList.add('show'); });
	noticeTimer = window.setTimeout(function() { node.classList.remove('show'); }, error ? 5200 : 2600);
}

function statusDot(ready) {
	return E('i', { 'class': ready ? 'ready' : 'down' });
}

function metric(label, value, detail, ready) {
	return E('div', { 'class': 'peripheral-metric' }, [
		statusDot(ready),
		E('div', {}, [ E('small', {}, label), E('strong', {}, value), E('span', {}, detail) ])
	]);
}

function toggle(label, detail, checked, handler) {
	var input = E('input', { 'type': 'checkbox', 'checked': checked ? '' : null });
	var control = E('label', { 'class': 'peripheral-switch' }, [ input, E('span') ]);
	input.addEventListener('change', function() {
		var requested = input.checked;
		input.disabled = true;
		Promise.resolve(handler(requested)).then(function() {
			input.disabled = false;
		}, function(e) {
			input.checked = !requested;
			input.disabled = false;
			notice(e.message || String(e), true);
		});
	});
	return E('div', { 'class': 'peripheral-toggle-row' }, [
		E('div', {}, [ E('strong', {}, label), E('small', {}, detail) ]), control
	]);
}

function actionButton(label, className, handler, disabled) {
	var button = E('button', { 'type': 'button', 'class': 'peripheral-action ' + (className || ''), 'disabled': disabled ? '' : null }, label);
	button.addEventListener('click', function() {
		button.disabled = true;
		button.classList.add('busy');
		Promise.resolve(handler()).finally(function() {
			button.disabled = !!disabled;
			button.classList.remove('busy');
		});
	});
	return button;
}

function categoryName(value) {
	return ({
		'TVs': '电视', 'A_V_receiver': '影音设备', 'Air_Purifiers': '空气净化器',
		'Box': '机顶盒', 'Camera': '相机', 'DVD': 'DVD', 'Fan': '风扇',
		'Projector': '投影仪', 'Climate': '空调', 'Air_Conditioners': '空调'
	})[value] || value;
}

function unique(values) {
	return values.filter(function(value, index) { return value && values.indexOf(value) === index; }).sort();
}

function option(value, label, selected) {
	return E('option', { 'value': value, 'selected': selected ? '' : null }, label);
}

function nfcKind(value) {
	return ({ text: '文本', url: '网址', wifi: 'Wi-Fi', contact: '联系人' })[value] || value;
}

return view.extend({
	load: function() {
		addStyle();
		return Promise.all([
			run([ 'status' ]),
			run([ 'nfc-library' ]),
			run([ 'infrared-categories' ]),
			run([ 'infrared-library' ])
		]).then(function(base) {
			var categories = base[2].items || [];
			var category = categories[0] && categories[0].name;
			if (!category)
				return [ base[0], base[1], categories, [], [], base[3] ];
			return run([ 'infrared-brands', category ]).then(function(data) {
				var brands = data.items || [];
				var brand = brands[0] && brands[0].name;
				if (!brand)
					return [ base[0], base[1], categories, brands, [], base[3] ];
				return run([ 'infrared-models', category, brand ]).then(function(models) {
					return [ base[0], base[1], categories, brands, models.items || [], base[3] ];
				});
			});
		});
	},

	render: function(initial) {
		var state = initial[0];
		var nfcLibrary = initial[1];
		var irCategories = initial[2] || [];
		var irBrands = initial[3] || [];
		var irModels = initial[4] || [];
		var irLibrary = initial[5].items || [];
		var selectedRemote = initial[5].selected || '';
		var active = 'nfc';
		var selectedNfc = nfcLibrary.selected || (nfcLibrary.items[0] && nfcLibrary.items[0].id) || '';
		var selectedCategory = (irCategories[0] && irCategories[0].name) || '';
		var selectedBrand = (irBrands[0] && irBrands[0].name) || '';
		var selectedSource = (irModels[0] && irModels[0].id) || '';
		var irButtons = [];
		var acState = null;
		var page = E('div', { 'class': 'peripherals-page' });
		var body = E('div');

		function safeRender() {
			var focused = document.activeElement;
			if (!focused || [ 'INPUT', 'TEXTAREA', 'SELECT' ].indexOf(focused.tagName) < 0)
				renderBody();
		}

		function refreshStatus(silent) {
			return run([ 'status' ]).then(function(next) {
				state = next;
				safeRender();
				if (!silent) notice('状态已刷新');
				return next;
			}, function(e) {
				if (!silent) notice(e.message || String(e), true);
				throw e;
			});
		}

		function reloadNfc() {
			return run([ 'nfc-library' ]).then(function(data) {
				nfcLibrary = data;
				if (!selectedNfc && data.items[0]) selectedNfc = data.items[0].id;
				renderBody();
			});
		}

		function reloadIrLibrary() {
			return run([ 'infrared-library' ]).then(function(data) {
				irLibrary = data.items || [];
				selectedRemote = data.selected || '';
				renderBody();
			});
		}

		function loadModels(category, brand, preferredSource) {
			return run([ 'infrared-models', category, brand ]).then(function(data) {
				irModels = data.items || [];
				selectedSource = preferredSource && irModels.some(function(item) { return item.id === preferredSource; })
					? preferredSource : ((irModels[0] && irModels[0].id) || '');
				irButtons = [];
				renderBody();
			});
		}

		function loadBrands(category) {
			return run([ 'infrared-brands', category ]).then(function(data) {
				irBrands = data.items || [];
				selectedBrand = (irBrands[0] && irBrands[0].name) || '';
				if (!selectedBrand) {
					irModels = [];
					selectedSource = '';
					irButtons = [];
					renderBody();
					return;
				}
				return loadModels(category, selectedBrand);
			});
		}

		function openSavedRemote(item) {
			if (item.kind === 'ac') {
				return run([ 'infrared-select', item.id ]).then(function() {
					return run([ 'infrared-ac-state', item.id ]);
				}).then(function(data) {
					selectedRemote = item.id;
					selectedCategory = 'Air_Conditioners';
					selectedSource = item.source;
					acState = data;
					irButtons = [];
					renderBody();
				});
			}
			var parts = item.source.split('/');
			var category = parts[0] || '';
			var brand = parts[1] || '';
			return run([ 'infrared-select', item.id ]).then(function() {
				return Promise.all([
					run([ 'infrared-brands', category ]),
					run([ 'infrared-models', category, brand ]),
					run([ 'infrared-buttons', item.source ])
				]);
			}).then(function(data) {
				selectedRemote = item.id;
				selectedCategory = category;
				selectedBrand = brand;
				selectedSource = item.source;
				irBrands = data[0].items || [];
				irModels = data[1].items || [];
				irButtons = data[2].items || [];
				renderBody();
			});
		}

		function sendAcAction(action) {
			return run([ 'infrared-ac-command', action ]).then(function(data) {
				acState = data;
				notice('空调状态已发送');
				renderBody();
			}, function(e) { notice(e.message || String(e), true); throw e; });
		}

		function acControlPanel(ready) {
			if (!acState) return null;
			var modeNames = { auto: '自动', cool: '制冷', heat: '制热', dry: '除湿', fan: '送风' };
			var fanNames = { auto: '自动', low: '低风', medium: '中风', high: '高风', max: '强风' };
			return E('section', { 'class': 'peripheral-panel ac-control-panel' }, [
				E('div', { 'class': 'panel-heading' }, [
					E('div', {}, [ E('small', {}, acState.protocol), E('h2', {}, acState.name || '空调遥控器') ]),
					E('span', { 'class': 'count-badge' }, acState.power ? '运行中' : '已关机')
				]),
				E('div', { 'class': 'peripheral-metrics' }, [
					metric('温度', String(acState.temp) + '°C', modeNames[acState.mode] || acState.mode, true),
					metric('风速', fanNames[acState.fan] || acState.fan, '完整状态帧', true),
					metric('上下扫风', acState.swingv === 'auto' ? '开启' : '关闭', '摆风', acState.swingv === 'auto'),
					metric('左右扫风', acState.swingh === 'auto' ? '开启' : '关闭', '摆风', acState.swingh === 'auto')
				]),
				E('div', { 'class': 'remote-pad ac-pad' }, [
					actionButton(acState.power ? '关机' : '开机', 'remote-key', function() { return sendAcAction('power'); }, !ready),
					actionButton('模式 · ' + (modeNames[acState.mode] || acState.mode), 'remote-key', function() { return sendAcAction('mode'); }, !ready),
					actionButton('温度 −', 'remote-key', function() { return sendAcAction('temp-'); }, !ready),
					actionButton(String(acState.temp) + '°C', 'remote-key', function() { return Promise.resolve(); }, true),
					actionButton('温度 ＋', 'remote-key', function() { return sendAcAction('temp+'); }, !ready),
					actionButton('风速 · ' + (fanNames[acState.fan] || acState.fan), 'remote-key', function() { return sendAcAction('fan'); }, !ready),
					actionButton('上下扫风', 'remote-key', function() { return sendAcAction('swingv'); }, !ready),
					actionButton('左右扫风', 'remote-key', function() { return sendAcAction('swingh'); }, !ready)
				])
			]);
		}

		function setFeature(feature, enabled) {
			return run([ 'set', feature, enabled ? '1' : '0' ]).then(function() {
				notice((feature === 'nfc' ? 'NFC' : '红外') + (enabled ? '已启用' : '已关闭'));
				return refreshStatus(true);
			});
		}

		function setNfcMode(mode) {
			return run([ 'set-nfc-mode', mode ]).then(function() {
				state.nfc_enabled = true;
				state.nfc_mode = mode;
				state.nfc_stage = 'starting';
				state.nfc_detail = mode === 'share' ? '正在启动热点碰一碰分享' : '正在启动标签复制';
				notice(mode === 'share' ? '已切换到热点分享' : '热点分享已停止，已进入复制标签');
				renderBody();
				window.setTimeout(function() { refreshStatus(true).catch(function() {}); }, 4500);
			});
		}

		function selectedTag() {
			return (nfcLibrary.items || []).filter(function(item) { return item.id === selectedNfc; })[0];
		}

		function createNfcForm() {
			var name = E('input', { 'type': 'text', 'maxlength': 80, 'placeholder': '例如：家庭 Wi-Fi' });
			var kind = E('select', {}, [
				option('wifi', 'Wi-Fi'), option('url', '网址'), option('text', '文本'), option('contact', '联系人')
			]);
			var payload = E('textarea', { 'rows': 5, 'maxlength': 4096, 'placeholder': '输入标签内容' });
			return E('section', { 'class': 'peripheral-panel nfc-editor' }, [
				E('div', { 'class': 'panel-heading' }, [ E('div', {}, [ E('small', {}, 'NDEF'), E('h2', {}, '新建标签') ]), E('span', { 'class': 'count-badge' }, '本地模板') ]),
				E('div', { 'class': 'form-grid' }, [
					E('label', {}, [ E('span', {}, '名称'), name ]),
					E('label', {}, [ E('span', {}, '类型'), kind ])
				]),
				E('label', { 'class': 'wide-field' }, [ E('span', {}, '内容'), payload ]),
				actionButton('保存到标签库', '', function() {
					return run([ 'nfc-create', name.value, kind.value, payload.value ]).then(function(data) {
						selectedNfc = data.id;
						notice('标签已保存');
						return reloadNfc();
					}, function(e) { notice(e.message || String(e), true); throw e; });
				})
			]);
		}

		function tagDetail(tag) {
			if (!tag) return createNfcForm();
			var canWrite = !!state.nfc_ndef_available;
			return E('section', { 'class': 'peripheral-panel nfc-editor' }, [
				E('div', { 'class': 'panel-heading' }, [
					E('div', {}, [ E('small', {}, nfcKind(tag.kind)), E('h2', {}, tag.name) ]),
					E('span', { 'class': 'count-badge' }, nfcLibrary.selected === tag.id ? '已选择' : '已保存')
				]),
				E('div', { 'class': 'payload-preview' }, tag.payload),
				E('div', { 'class': 'button-row' }, [
					actionButton('选择', 'secondary', function() {
						return run([ 'nfc-select', tag.id ]).then(function() { nfcLibrary.selected = tag.id; notice('已选择 ' + tag.name); renderBody(); });
					}),
					actionButton('重命名', 'secondary', function() {
						var value = window.prompt('标签名称', tag.name);
						if (!value || value === tag.name) return Promise.resolve();
						return run([ 'nfc-rename', tag.id, value ]).then(function() { notice('名称已更新'); return reloadNfc(); });
					}),
					actionButton(canWrite ? '写入标签' : '标签写入未就绪', '', function() { return Promise.resolve(); }, !canWrite),
					actionButton('删除', 'danger', function() {
						if (!window.confirm('删除“' + tag.name + '”？')) return Promise.resolve();
						return run([ 'nfc-delete', tag.id ]).then(function() { selectedNfc = ''; notice('标签已删除'); return reloadNfc(); });
					})
				])
			]);
		}

		function nfcPanel() {
			var ready = !!(state.nfc_enabled && state.nfc_node && state.nfc_hal_running && state.nfc_service_running);
			var mode = state.nfc_mode || 'share';
			var share = mode === 'share';
			var stageNames = {
				starting: '启动中', share_active: '分享中', await_source: '等待源标签',
				reading_source: '读取源标签', await_target: '等待目标标签',
				writing_target: '写入目标标签', copy_complete: '复制完成',
				mct_detected: '识别到 MIFARE Classic', mct_reading_source: '读取 Classic 源卡',
				mct_await_target: '等待 Classic 目标卡', mct_writing_target: '写入 Classic 目标卡',
				error: '异常'
			};
			var stage = stageNames[state.nfc_stage] || (ready ? '运行中' : '未就绪');
			return E('div', { 'class': 'peripheral-content' }, [
				E('section', { 'class': 'peripheral-hero nfc' }, [
					E('div', { 'class': 'peripheral-symbol nfc-symbol' }, [ E('b'), E('i'), E('em') ]),
					E('div', { 'class': 'peripheral-hero-copy' }, [ E('small', {}, 'NFC'), E('h1', {}, share ? '热点碰一碰分享' : '复制标签'), E('p', {}, share ? 'Android 使用 NFC 碰一碰；iPhone 请扫描设备屏幕的“连接二维码”。' : '复制模式独占 NFC，热点分享已经停止。') ]),
					E('b', { 'class': ready ? 'hero-ready' : 'hero-off' }, state.nfc_enabled ? (ready ? '服务在线' : '未就绪') : '已关闭')
				]),
				E('section', { 'class': 'peripheral-metrics' }, [
					metric('工作模式', share ? '热点分享' : '复制标签', '两种模式互斥', ready),
					metric('控制器', state.nfc_node ? state.nfc_chip : '未连接', state.nfc_firmware || '--', !!state.nfc_node),
					metric('当前阶段', stage, state.nfc_detail || '等待服务状态', !!state.nfc_ndef_available),
					metric('源数据', String(state.nfc_source_bytes || 0) + ' B', share ? 'Wi-Fi WSC' : '已读取标签', share || state.nfc_source_bytes > 0)
				]),
				E('section', { 'class': 'peripheral-panel feature-switch' }, [
					toggle('启用 NFC', '关闭时不启动 NFC 服务', !!state.nfc_enabled, function(v) { return setFeature('nfc', v); })
				]),
				E('div', { 'class': 'manager-layout nfc-mode-layout' }, [
					E('section', { 'class': 'peripheral-panel' + (share ? ' selected' : '') }, [
						E('div', { 'class': 'panel-heading' }, [ E('div', {}, [ E('small', {}, '默认模式'), E('h2', {}, '热点分享') ]), E('span', { 'class': 'count-badge' }, share ? '当前' : '待机') ]),
						E('p', {}, 'Android 贴近设备读取标准 Wi-Fi WSC NDEF；iOS 不支持该联网记录，请用 iPhone 相机扫描设备屏幕的连接二维码。'),
						actionButton(share ? '热点分享已开启' : '切换到热点分享', share ? 'secondary' : '', function() { return share ? Promise.resolve() : setNfcMode('share'); }, share)
					]),
					E('section', { 'class': 'peripheral-panel' + (!share ? ' selected' : '') }, [
						E('div', { 'class': 'panel-heading' }, [ E('div', {}, [ E('small', {}, '独占模式'), E('h2', {}, '复制标签') ]), E('span', { 'class': 'count-badge' }, !share ? '当前' : '待机') ]),
						E('p', {}, '先贴源标签读取，再贴可写目标标签。支持标准 NDEF；MIFARE Classic 使用 MCT 兼容流程。'),
						E('div', { 'class': 'button-row' }, [
							actionButton(!share ? '复制模式已开启' : '进入复制模式', !share ? 'secondary' : '', function() { return !share ? Promise.resolve() : setNfcMode('copy'); }, !share),
							actionButton('重新读取源标签', 'secondary', function() {
								return run([ 'nfc-copy-reset' ]).then(function() { notice('请重新贴近源标签'); window.setTimeout(function() { refreshStatus(true); }, 4000); });
							}, share)
						])
					])
				])
			]);
		}

		function loadButtons(source) {
			selectedSource = source || '';
			if (!selectedSource) { irButtons = []; renderBody(); return Promise.resolve(); }
			return run([ 'infrared-buttons', selectedSource ]).then(function(data) {
				irButtons = data.items || [];
				acState = null;
				renderBody();
			});
		}

		function infraredPanel() {
			var ready = !!(state.infrared_enabled && state.infrared_node && state.infrared_hal_running);
			var categories = irCategories.map(function(item) { return item.name; });
			if (categories.indexOf(selectedCategory) < 0) selectedCategory = categories[0] || '';
			var brands = irBrands.map(function(item) { return item.name; });
			if (brands.indexOf(selectedBrand) < 0) selectedBrand = brands[0] || '';
			var models = irModels;
			if (!selectedSource && models[0]) selectedSource = models[0].id;
			var selectedModel = models.filter(function(item) { return item.id === selectedSource; })[0];
			var sourceIsAc = !!(selectedSource && selectedSource.indexOf('ac:') === 0);
			var category = E('select', {}, categories.map(function(value) { return option(value, categoryName(value), value === selectedCategory); }));
			var brand = E('select', {}, brands.map(function(value) { return option(value, value, value === selectedBrand); }));
			var model = E('select', {}, models.map(function(item) { return option(item.id, item.model, item.id === selectedSource); }));
			var remoteName = E('input', { 'type': 'text', 'maxlength': 80, 'placeholder': '遥控器名称', 'value': selectedModel ? (selectedModel.brand + ' ' + selectedModel.model) : '' });
			category.addEventListener('change', function() {
				selectedCategory = category.value;
				selectedBrand = '';
				selectedSource = '';
				irButtons = [];
				loadBrands(selectedCategory).catch(function(e) { notice(e.message || String(e), true); });
			});
			brand.addEventListener('change', function() {
				selectedBrand = brand.value;
				selectedSource = '';
				irButtons = [];
				loadModels(selectedCategory, selectedBrand).catch(function(e) { notice(e.message || String(e), true); });
			});
			model.addEventListener('change', function() { loadButtons(model.value).catch(function(e) { notice(e.message || String(e), true); }); });

			var sections = [
				E('section', { 'class': 'peripheral-hero infrared' }, [
					E('div', { 'class': 'peripheral-symbol ir-symbol' }, [ E('b'), E('i'), E('em') ]),
					E('div', { 'class': 'peripheral-hero-copy' }, [ E('small', {}, 'INFRARED'), E('h1', {}, '万能遥控器'), E('p', {}, '按设备类型、品牌和型号管理常用遥控器。') ]),
					E('b', { 'class': ready ? 'hero-ready' : 'hero-off' }, state.infrared_enabled ? (ready ? '发射就绪' : '未就绪') : '已关闭')
				]),
				E('section', { 'class': 'peripheral-metrics' }, [
					metric('红外发射', state.infrared_enabled ? '已开启' : '已关闭', '内置发射器', state.infrared_enabled),
					metric('离线码库', String(state.infrared_catalog || 0), '遥控器型号', state.infrared_catalog > 0),
					metric('我的遥控器', String(irLibrary.length), '已保存设备', irLibrary.length > 0),
					metric('载波范围', '30–56 kHz', '自动匹配', ready)
				]),
				E('section', { 'class': 'peripheral-panel feature-switch' }, [
					toggle('启用红外发射', '关闭时不启动红外服务', !!state.infrared_enabled, function(v) { return setFeature('infrared', v); })
				]),
				acControlPanel(ready),
				E('div', { 'class': 'remote-layout' }, [
					E('section', { 'class': 'peripheral-panel catalog-panel' }, [
						E('div', { 'class': 'panel-heading' }, [ E('h2', {}, '添加设备'), E('span', { 'class': 'count-badge' }, String(state.infrared_catalog || 0) + ' 个型号') ]),
						E('div', { 'class': 'form-grid three' }, [
							E('label', {}, [ E('span', {}, '设备类型'), category ]),
							E('label', {}, [ E('span', {}, '品牌'), brand ]),
							E('label', {}, [ E('span', {}, '型号'), model ])
						]),
						E('div', { 'class': 'save-remote-row' }, [
							remoteName,
							actionButton('保存遥控器', '', function() {
								return run([ 'infrared-save', selectedSource, remoteName.value ]).then(function() { notice('遥控器已保存'); return reloadIrLibrary(); }, function(e) { notice(e.message || String(e), true); throw e; });
							}, !selectedSource)
						]),
						E('div', { 'class': 'remote-pad' }, sourceIsAc ? [ E('div', { 'class': 'empty-state' }, [ E('b', {}, '状态式空调遥控器'), E('span', {}, '先保存遥控器，再设置开关、模式、温度、风速和扫风') ]) ] : (irButtons.length ? irButtons.map(function(button) {
							return actionButton(button, 'remote-key', function() {
								return run([ 'infrared-send', selectedSource, button ]).then(function() { notice(button + ' 已发送'); }, function(e) { notice(e.message || String(e), true); throw e; });
							}, !ready);
						}) : [ actionButton('加载遥控面板', '', function() { return loadButtons(selectedSource); }, !selectedSource) ]))
					]),
					E('section', { 'class': 'peripheral-panel saved-remotes' }, [
						E('div', { 'class': 'panel-heading' }, [ E('h2', {}, '我的遥控器'), E('span', { 'class': 'count-badge' }, String(irLibrary.length)) ]),
						E('div', { 'class': 'library-list' }, irLibrary.length ? irLibrary.map(function(item) {
							return E('div', { 'class': 'saved-remote' + (selectedRemote === item.id ? ' selected' : '') }, [
								E('button', { 'type': 'button', 'click': function() {
									return openSavedRemote(item).catch(function(e) { notice(e.message || String(e), true); });
								} }, [ E('span', { 'class': 'library-icon' }, item.kind === 'ac' ? '❄' : '⌁'), E('span', {}, [ E('b', {}, item.name), E('small', {}, item.kind === 'ac' ? '空调 · 状态式遥控' : item.source.split('/').slice(0, 2).join(' · ')) ]) ]),
								E('button', { 'type': 'button', 'class': 'mini-action', 'title': '重命名', 'click': function() {
									var value = window.prompt('遥控器名称', item.name);
									if (value && value !== item.name) run([ 'infrared-rename', item.id, value ]).then(function() { notice('名称已更新'); reloadIrLibrary(); });
								} }, '✎'),
								E('button', { 'type': 'button', 'class': 'mini-action danger', 'title': '删除', 'click': function() {
									if (window.confirm('删除“' + item.name + '”？')) run([ 'infrared-delete', item.id ]).then(function() { notice('遥控器已删除'); reloadIrLibrary(); });
								} }, '×')
							]);
						}) : [ E('div', { 'class': 'empty-state' }, [ E('b', {}, '暂无遥控器'), E('span', {}, '选择型号并保存到这里') ]) ])
					])
				])
			];
			return E('div', { 'class': 'peripheral-content' }, sections.filter(function(node) { return !!node; }));
		}

		function renderBody() {
			dom.content(body, [ active === 'nfc' ? nfcPanel() : infraredPanel() ]);
			page.querySelectorAll('.peripheral-tab').forEach(function(tab) { tab.classList.toggle('active', tab.dataset.tab === active); });
		}

		var tabs = E('div', { 'class': 'peripheral-tabs' }, [
			E('button', { 'type': 'button', 'class': 'peripheral-tab active', 'data-tab': 'nfc', 'click': function() { active = 'nfc'; renderBody(); } }, [ E('span', {}, 'N'), E('b', {}, 'NFC 标签') ]),
			E('button', { 'type': 'button', 'class': 'peripheral-tab', 'data-tab': 'infrared', 'click': function() { active = 'infrared'; renderBody(); } }, [ E('span', {}, '◎'), E('b', {}, '红外遥控') ])
		]);
		page.appendChild(tabs);
		page.appendChild(body);
		renderBody();
		poll.add(function() { return refreshStatus(true).catch(function() {}); }, 10);
		return page;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
