'use strict';
'require view';
'require ui';
'require photonicat2.common as pcat';

function clone(obj) {
	return JSON.parse(JSON.stringify(obj || {}));
}

function asInt(id, fallback) {
	var node = document.getElementById(id);
	var value = node ? parseInt(node.value, 10) : fallback;
	return isFinite(value) ? value : fallback;
}

function b64utf8(text) {
	var bytes = new TextEncoder().encode(text);
	var binary = '';
	for (var i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i]);
	return btoa(binary);
}

function rgbToHex(rgb) {
	if (!Array.isArray(rgb) || rgb.length < 3) return '#ffe500';
	return '#' + rgb.slice(0, 3).map(function(v) {
		return Math.max(0, Math.min(255, Number(v) || 0)).toString(16).padStart(2, '0');
	}).join('');
}

function hexToRgb(hex) {
	var m = String(hex || '').match(/^#([0-9a-f]{6})$/i);
	return m ? [ parseInt(m[1].slice(0, 2), 16), parseInt(m[1].slice(2, 4), 16), parseInt(m[1].slice(4, 6), 16) ] : [ 255, 229, 0 ];
}

function control(label, node, hint) {
	return E('div', { 'class': 'pcat-control' }, [
		E('label', {}, label),
		E('div', {}, [ node, hint ? E('div', { 'class': 'pcat-hint' }, hint) : '' ])
	]);
}

return view.extend({
	config: {},
	pageName: '',
	elementIndex: 0,

	load: function() {
		return L.resolveDefault(pcat.call('screen-get'), { user: {}, effective: {} });
	},

	pages: function() {
		var elements = this.config.display_template && this.config.display_template.elements;
		return elements && typeof elements === 'object' ? elements : {};
	},

	syncJson: function() {
		var editor = document.getElementById('pcat-screen-json');
		if (editor) editor.value = JSON.stringify(this.config, null, 2);
	},

	syncBasic: function() {
		this.config.show_sms = document.getElementById('pcat-screen-sms').checked;
		this.config.sms_limit_for_screen = asInt('pcat-screen-sms-count', 5);
		this.config.screen_max_brightness = asInt('pcat-screen-max', 100);
		this.config.screen_min_brightness = asInt('pcat-screen-min', 0);
		this.config.screen_dimmer_time_on_battery_seconds = asInt('pcat-screen-battery-dim', 60);
		this.config.screen_dimmer_time_on_dc_seconds = asInt('pcat-screen-dc-dim', 300);
		this.config.ping_site0 = document.getElementById('pcat-screen-ping0').value.trim();
		this.config.ping_site1 = document.getElementById('pcat-screen-ping1').value.trim();
	},

	showTab: function(name) {
		document.querySelectorAll('.pcat-tab').forEach(function(tab) {
			tab.classList.toggle('active', tab.getAttribute('data-tab') === name);
		});
		document.querySelectorAll('[data-panel]').forEach(function(panel) {
			panel.classList.toggle('pcat-hidden', panel.getAttribute('data-panel') !== name);
		});
		if (name === 'preview') this.refreshPreview();
	},

	rebuildPageSelect: function() {
		var select = document.getElementById('pcat-layout-page');
		if (!select) return;
		var names = Object.keys(this.pages()).sort(function(a, b) {
			return (parseInt(a.replace(/\D/g, ''), 10) || 0) - (parseInt(b.replace(/\D/g, ''), 10) || 0);
		});
		if (!this.pageName || names.indexOf(this.pageName) < 0) this.pageName = names[0] || 'page0';
		select.innerHTML = '';
		names.forEach(L.bind(function(name) {
			select.appendChild(E('option', { 'value': name, 'selected': name === this.pageName }, name));
		}, this));
		this.rebuildElementSelect();
	},

	rebuildElementSelect: function() {
		var select = document.getElementById('pcat-layout-element');
		if (!select) return;
		var list = this.pages()[this.pageName] || [];
		if (this.elementIndex >= list.length) this.elementIndex = Math.max(0, list.length - 1);
		select.innerHTML = '';
		list.forEach(L.bind(function(el, index) {
			var name = el.label || el.data_key || el.icon_path || el.type || '元素';
			select.appendChild(E('option', { 'value': String(index), 'selected': index === this.elementIndex }, '%d · %s'.format(index + 1, name)));
		}, this));
		this.fillElementForm();
		this.renderStage();
	},

	selectedElement: function() {
		var list = this.pages()[this.pageName] || [];
		return list[this.elementIndex] || null;
	},

	fillElementForm: function() {
		var el = this.selectedElement();
		var disabled = !el;
		var values = {
			'pcat-el-enable': el ? el.enable !== 0 : false,
			'pcat-el-type': el ? el.type || 'text' : 'text',
			'pcat-el-label': el ? el.label || el.fixed_text || '' : '',
			'pcat-el-key': el ? el.data_key || '' : '',
			'pcat-el-x': el && el.position ? Number(el.position.x) || 0 : 0,
			'pcat-el-y': el && el.position ? Number(el.position.y) || 0 : 0,
			'pcat-el-font': el ? el.font || 'reg' : 'reg',
			'pcat-el-units': el ? el.units || '' : '',
			'pcat-el-color': el ? rgbToHex(el.color) : '#ffe500'
		};
		Object.keys(values).forEach(function(id) {
			var node = document.getElementById(id);
			if (!node) return;
			if (node.type === 'checkbox') node.checked = values[id];
			else node.value = values[id];
			node.disabled = disabled;
		});
	},

	applyElementForm: function() {
		var el = this.selectedElement();
		if (!el) return;
		el.enable = document.getElementById('pcat-el-enable').checked ? 1 : 0;
		el.type = document.getElementById('pcat-el-type').value;
		el.label = document.getElementById('pcat-el-label').value;
		el.data_key = document.getElementById('pcat-el-key').value.trim();
		el.position = el.position || {};
		el.position.x = asInt('pcat-el-x', 0);
		el.position.y = asInt('pcat-el-y', 0);
		el.font = document.getElementById('pcat-el-font').value;
		el.units = document.getElementById('pcat-el-units').value;
		el.color = hexToRgb(document.getElementById('pcat-el-color').value);
		this.syncJson();
		this.rebuildElementSelect();
	},

	addElement: function() {
		var pages = this.pages();
		if (!pages[this.pageName]) pages[this.pageName] = [];
		pages[this.pageName].push({
			type: 'text', label: '新元素', data_key: 'CPUUsage', units: '%', font: 'reg',
			position: { x: 10, y: 100 }, color: [ 255, 229, 0 ], enable: 1
		});
		this.elementIndex = pages[this.pageName].length - 1;
		this.syncJson();
		this.rebuildElementSelect();
	},

	deleteElement: function() {
		var list = this.pages()[this.pageName] || [];
		if (!list.length) return;
		list.splice(this.elementIndex, 1);
		this.elementIndex = Math.max(0, this.elementIndex - 1);
		this.syncJson();
		this.rebuildElementSelect();
	},

	addPage: function() {
		var pages = this.pages();
		var index = 0;
		while (pages['page' + index]) index++;
		pages['page' + index] = [];
		this.pageName = 'page' + index;
		this.elementIndex = 0;
		this.syncJson();
		this.rebuildPageSelect();
	},

	deletePage: function() {
		var names = Object.keys(this.pages());
		if (names.length <= 1) {
			pcat.notify('至少需要保留一个屏幕页面', 'error');
			return;
		}
		delete this.pages()[this.pageName];
		this.pageName = '';
		this.elementIndex = 0;
		this.syncJson();
		this.rebuildPageSelect();
	},

	renderStage: function() {
		var stage = document.getElementById('pcat-screen-stage');
		if (!stage) return;
		stage.innerHTML = '';
		var bg = this.config.display_template && this.config.display_template.bg_color;
		stage.style.backgroundColor = rgbToHex(bg || [ 0, 0, 0 ]);
		(this.pages()[this.pageName] || []).forEach(L.bind(function(el, index) {
			if (el.enable === 0) return;
			var pos = el.position || {};
			var fonts = { tiny: 8, small: 10, reg: 12, big: 17, huge: 22 };
			var label = el.type === 'icon' ? '◇ ' + (el.icon_path || 'icon') : (el.label || el.data_key || el.type || '元素');
			var node = E('div', {
				'class': 'pcat-screen-el' + (index === this.elementIndex ? ' selected' : ''),
				'style': 'left:%spx;top:%spx;color:%s;font-size:%spx'.format(Number(pos.x) || 0, Number(pos.y) || 0, rgbToHex(el.color), fonts[el.font] || 12),
				'click': L.bind(function(ev) {
					ev.stopPropagation();
					this.elementIndex = index;
					document.getElementById('pcat-layout-element').value = String(index);
					this.fillElementForm();
					this.renderStage();
				}, this)
			}, label + (el.units || ''));
			stage.appendChild(node);
		}, this));
	},

	applyJson: function() {
		try {
			var parsed = JSON.parse(document.getElementById('pcat-screen-json').value);
			if (!parsed || Array.isArray(parsed) || typeof parsed !== 'object') throw new Error('顶层必须是对象');
			this.config = parsed;
			this.config.display_template = this.config.display_template || { elements: { page0: [] } };
			this.config.display_template.elements = this.config.display_template.elements || { page0: [] };
			this.pageName = '';
			this.elementIndex = 0;
			this.rebuildPageSelect();
			pcat.notify('JSON 已载入可视编辑器，尚未保存到设备', 'info');
		}
		catch (err) {
			pcat.notify('JSON 无效：' + err.message, 'error');
		}
	},

	save: function() {
		try {
			this.syncBasic();
			var editor = document.getElementById('pcat-screen-json');
			if (document.querySelector('[data-panel="json"]:not(.pcat-hidden)')) {
				this.config = JSON.parse(editor.value);
				this.syncBasic();
			}
			if (this.config.screen_min_brightness < 0 || this.config.screen_max_brightness > 100 || this.config.screen_min_brightness > this.config.screen_max_brightness)
				throw new Error('亮度范围无效');
		}
		catch (err) {
			pcat.notify('无法保存：' + err.message, 'error');
			return;
		}
		var button = document.getElementById('pcat-screen-save');
		if (button) button.disabled = true;
		return pcat.call('screen-save', [ b64utf8(JSON.stringify(this.config)) ])
			.then(function() { pcat.notify('屏幕配置已保存，显示服务已重启', 'info'); })
			.catch(function(err) { pcat.notify(err.message, 'error'); })
			.finally(function() { if (button) button.disabled = false; });
	},

	reset: function() {
		L.showModal('恢复迷你屏幕默认布局？', [
			E('p', {}, '这会清除当前用户布局和颜色设置，并重启显示服务。'),
			E('div', { 'class': 'right' }, [
				E('button', { 'class': 'btn', 'click': L.hideModal }, '取消'), ' ',
				E('button', { 'class': 'btn cbi-button-negative', 'click': ui.createHandlerFn(this, function() {
					return pcat.call('screen-reset').then(function() { L.hideModal(); window.location.reload(); });
				}) }, '恢复默认')
			])
		]);
	},

	refreshPreview: function() {
		var img = document.getElementById('pcat-screen-frame');
		var message = document.getElementById('pcat-screen-preview-state');
		if (message) message.textContent = '正在读取实际屏幕帧…';
		return pcat.call('screen-frame').then(function(data) {
			if (img) img.src = 'data:' + data.mime + ';base64,' + data.data;
			if (message) message.textContent = '实际 LCD 帧 · ' + new Date().toLocaleTimeString();
		}).catch(function(err) {
			if (message) message.textContent = err.message;
		});
	},

	screenAction: function(action) {
		return pcat.call(action).then(L.bind(function() {
			pcat.notify(action === 'screen-sms' ? '已切换到短信页面' : '已切换下一页', 'info');
			setTimeout(L.bind(this.refreshPreview, this), 450);
		}, this)).catch(function(err) { pcat.notify(err.message, 'error'); });
	},

	loadLiveData: function() {
		var pre = document.getElementById('pcat-screen-live');
		return pcat.call('screen-data').then(function(data) {
			if (pre) pre.textContent = JSON.stringify(data, null, 2);
		}).catch(function(err) { if (pre) pre.textContent = err.message; });
	},

	render: function(data) {
		pcat.loadStyle();
		this.config = clone(data.effective && Object.keys(data.effective).length ? data.effective : data.user);
		this.config.display_template = this.config.display_template || { elements: { page0: [] } };
		this.config.display_template.elements = this.config.display_template.elements || { page0: [] };
		var pages = Object.keys(this.pages());
		if (!pages.length) this.config.display_template.elements.page0 = [];
		this.pageName = Object.keys(this.pages())[0];
		this.elementIndex = 0;
		var cfg = this.config;

		var tabs = [ ['basic', '基本设置'], ['layout', '可视布局编辑'], ['json', '高级 JSON'], ['preview', '实际屏幕预览'], ['data', '屏幕实时参数'] ];
		var tabNodes = tabs.map(L.bind(function(tab, index) {
			return E('button', {
				'class': 'btn pcat-tab' + (index === 0 ? ' active' : ''),
				'data-tab': tab[0],
				'click': ui.createHandlerFn(this, function() { this.showTab(tab[0]); })
			}, tab[1]);
		}, this));

		var basicPanel = E('div', { 'class': 'pcat-card', 'data-panel': 'basic' }, [
			E('h3', {}, '亮度、休眠与短信'),
			control('短信接入屏幕', E('input', { 'id': 'pcat-screen-sms', 'type': 'checkbox', 'checked': cfg.show_sms !== false }), '短信由 QModem/ModemManager 提供，收到短信时显示服务可自动唤醒并跳转。'),
			control('屏幕短信数量', E('input', { 'id': 'pcat-screen-sms-count', 'type': 'number', 'min': '1', 'max': '100', 'value': String(cfg.sms_limit_for_screen || 5) })),
			control('最高亮度', E('input', { 'id': 'pcat-screen-max', 'type': 'range', 'min': '1', 'max': '100', 'value': String(cfg.screen_max_brightness == null ? 100 : cfg.screen_max_brightness) })),
			control('最低亮度', E('input', { 'id': 'pcat-screen-min', 'type': 'range', 'min': '0', 'max': '100', 'value': String(cfg.screen_min_brightness || 0) })),
			control('电池供电息屏（秒）', E('input', { 'id': 'pcat-screen-battery-dim', 'type': 'number', 'min': '0', 'max': '86400', 'value': String(cfg.screen_dimmer_time_on_battery_seconds || 60) })),
			control('外部供电息屏（秒）', E('input', { 'id': 'pcat-screen-dc-dim', 'type': 'number', 'min': '0', 'max': '604800', 'value': String(cfg.screen_dimmer_time_on_dc_seconds || 300) })),
			control('延迟检测地址 1', E('input', { 'id': 'pcat-screen-ping0', 'value': cfg.ping_site0 || '1.1.1.1' })),
			control('延迟检测地址 2', E('input', { 'id': 'pcat-screen-ping1', 'value': cfg.ping_site1 || 'photonicat.com' }))
		]);

		var elementControls = E('div', {}, [
			control('启用元素', E('input', { 'id': 'pcat-el-enable', 'type': 'checkbox' })),
			control('元素类型', E('select', { 'id': 'pcat-el-type' }, [
				E('option', { 'value': 'text' }, '动态数据文字'),
				E('option', { 'value': 'fixed_text' }, '固定文字'),
				E('option', { 'value': 'icon' }, '图标'),
				E('option', { 'value': 'bar' }, '进度条'),
				E('option', { 'value': 'graph' }, '曲线图')
			])),
			control('标题 / 固定文字', E('input', { 'id': 'pcat-el-label' })),
			control('数据键', E('input', { 'id': 'pcat-el-key', 'placeholder': '例如 CPUUsage、BatterySOC' })),
			control('X 坐标', E('input', { 'id': 'pcat-el-x', 'type': 'number', 'min': '-172', 'max': '344' })),
			control('Y 坐标', E('input', { 'id': 'pcat-el-y', 'type': 'number', 'min': '-320', 'max': '640' })),
			control('字体', E('select', { 'id': 'pcat-el-font' }, [
				E('option', { 'value': 'tiny' }, 'tiny'), E('option', { 'value': 'small' }, 'small'),
				E('option', { 'value': 'reg' }, 'reg'), E('option', { 'value': 'big' }, 'big'), E('option', { 'value': 'huge' }, 'huge')
			])),
			control('单位', E('input', { 'id': 'pcat-el-units' })),
			control('颜色', E('input', { 'id': 'pcat-el-color', 'type': 'color' })),
			E('div', { 'class': 'pcat-actions' }, [
				pcat.button('更新元素', 'cbi-button-positive', L.bind(this.applyElementForm, this)),
				pcat.button('新增元素', '', L.bind(this.addElement, this)),
				pcat.button('删除元素', 'cbi-button-negative', L.bind(this.deleteElement, this))
			])
		]);

		var layoutPanel = E('div', { 'class': 'pcat-card pcat-hidden', 'data-panel': 'layout' }, [
			E('h3', {}, '172 × 320 布局编辑器'),
			E('div', { 'class': 'pcat-inline' }, [
				E('label', {}, '页面'),
				E('select', { 'id': 'pcat-layout-page', 'change': L.bind(function(ev) { this.pageName = ev.target.value; this.elementIndex = 0; this.rebuildElementSelect(); }, this) }),
				pcat.button('新增页面', '', L.bind(this.addPage, this)),
				pcat.button('删除页面', 'cbi-button-negative', L.bind(this.deletePage, this))
			]),
			E('div', { 'class': 'pcat-editor', 'style': 'margin-top:1rem' }, [
				E('div', { 'class': 'pcat-screen-stage-wrap' }, E('div', { 'id': 'pcat-screen-stage', 'class': 'pcat-screen-stage' })),
				E('div', {}, [
					control('当前元素', E('select', { 'id': 'pcat-layout-element', 'change': L.bind(function(ev) { this.elementIndex = Number(ev.target.value) || 0; this.fillElementForm(); this.renderStage(); }, this) })),
					elementControls
				])
			])
		]);

		var jsonPanel = E('div', { 'class': 'pcat-card pcat-hidden', 'data-panel': 'json' }, [
			E('h3', {}, '高级 JSON 配置'),
			E('p', { 'class': 'pcat-hint' }, '支持完整 display_template、自定义指标和公开 IP 数据源。保存前会校验 JSON，最大 128 KiB。'),
			E('textarea', { 'id': 'pcat-screen-json', 'class': 'pcat-json' }, JSON.stringify(this.config, null, 2)),
			E('div', { 'class': 'pcat-actions' }, [ pcat.button('载入可视编辑器', '', L.bind(this.applyJson, this)) ])
		]);

		var previewPanel = E('div', { 'class': 'pcat-card pcat-hidden', 'data-panel': 'preview' }, [
			E('h3', {}, '设备实际画面'),
			E('img', { 'id': 'pcat-screen-frame', 'class': 'pcat-screen-preview', 'alt': '迷你屏幕预览' }),
			E('div', { 'id': 'pcat-screen-preview-state', 'class': 'pcat-hint', 'style': 'text-align:center;margin-top:.6rem' }, '点击刷新读取实际 LCD 帧'),
			E('div', { 'class': 'pcat-actions', 'style': 'justify-content:center' }, [
				pcat.button('刷新预览', '', L.bind(this.refreshPreview, this)),
				pcat.button('切换下一页', '', L.bind(this.screenAction, this, 'screen-click')),
				pcat.button('显示短信页', 'cbi-button-positive', L.bind(this.screenAction, this, 'screen-sms'))
			])
		]);

		var dataPanel = E('div', { 'class': 'pcat-card pcat-hidden', 'data-panel': 'data' }, [
			E('h3', {}, '显示服务实时 data_key'),
			E('pre', { 'id': 'pcat-screen-live', 'style': 'max-height:520px;overflow:auto' }, '点击刷新读取数据'),
			E('div', { 'class': 'pcat-actions' }, [ pcat.button('刷新参数', '', L.bind(this.loadLiveData, this)) ])
		]);

		var root = E('div', { 'class': 'pcat-page' }, [
			E('h2', {}, '迷你屏幕设置与编辑'),
			E('p', { 'class': 'pcat-subtitle' }, '直接管理 pcat2-display-mini，不依赖厂家 Web 端口。可视编辑、完整 JSON、实时数据和实际 LCD 帧集中在同一原生 LuCI 页面。'),
			E('div', { 'class': 'pcat-tabs' }, tabNodes),
			basicPanel, layoutPanel, jsonPanel, previewPanel, dataPanel,
			E('div', { 'class': 'cbi-page-actions' }, [
				E('button', { 'class': 'btn cbi-button-negative', 'click': ui.createHandlerFn(this, 'reset') }, '恢复默认'), ' ',
				E('button', { 'id': 'pcat-screen-save', 'class': 'btn cbi-button-positive important', 'click': ui.createHandlerFn(this, 'save') }, '保存并重启屏幕')
			])
		]);

		window.requestAnimationFrame(L.bind(function() {
			this.rebuildPageSelect();
			this.syncJson();
		}, this));
		return root;
	},

	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
