'use strict';
'require view';
'require form';
'require poll';
'require uci';
'require photonicat2.common as pcat';

function input(name) {
	var id = 'cbid.photonicat2.main.' + name;
	return document.getElementById('widget.' + id) || document.getElementById(id);
}

function validateTime(section_id, value) {
	return /^([01]\d|2[0-3]):[0-5]\d$/.test(value) ? true : '请输入 HH:MM 格式时间';
}

function modeLabel(mode) {
	return mode === 'off' ? '始终关闭' : mode === 'timed' ? '按时间开启' : '始终开启';
}

function minutes(value) {
	var match = String(value || '').match(/^(\d{2}):(\d{2})$/);
	return match ? Number(match[1]) * 60 + Number(match[2]) : 0;
}

function inWindow(now, start, end) {
	if (start === end)
		return true;
	return start < end ? now >= start && now < end : now >= start || now < end;
}

function durationLabel(value) {
	value = Math.max(0, Math.round(value));
	var hours = Math.floor(value / 60);
	var mins = value % 60;
	if (!hours)
		return mins + ' 分钟';
	return hours + ' 小时' + (mins ? ' ' + mins + ' 分钟' : '');
}

return view.extend({
	setText: function(root, name, value) {
		var node = root.querySelector('[data-led-value="' + name + '"]');
		if (node)
			node.textContent = value;
	},

	policy: function(root) {
		var modeNode = input('led_mode');
		var mode = modeNode && modeNode.value;
		var startNode = input('led_start');
		var endNode = input('led_end');
		if (mode !== 'on' && mode !== 'off' && mode !== 'timed')
			mode = uci.get('photonicat2', 'main', 'led_mode') || 'on';
		return {
			mode: mode,
			start: startNode && /^([01]\d|2[0-3]):[0-5]\d$/.test(startNode.value) ? startNode.value : (uci.get('photonicat2', 'main', 'led_start') || '07:00'),
			end: endNode && /^([01]\d|2[0-3]):[0-5]\d$/.test(endNode.value) ? endNode.value : (uci.get('photonicat2', 'main', 'led_end') || '23:00')
		};
	},

	syncModeButtons: function(root, mode) {
		var buttons = root.querySelectorAll('[data-led-mode-button]');
		for (var i = 0; i < buttons.length; i++) {
			var active = buttons[i].getAttribute('data-led-mode-button') === mode;
			buttons[i].classList.toggle('is-active', active);
			buttons[i].setAttribute('aria-pressed', active ? 'true' : 'false');
		}
	},

	setupModeButtons: function(formNode) {
		var frame = formNode.querySelector('[id="cbid.photonicat2.main.led_mode"]');
		var mode = formNode.querySelector('[id="widget.cbid.photonicat2.main.led_mode"]');
		if (!frame || !mode || !frame.parentNode)
			return;

		var field = frame.parentNode;
		field.classList.add('pcat-led-mode-field');
		frame.classList.add('pcat-led-mode-native');
		mode.setAttribute('aria-hidden', 'true');
		mode.tabIndex = -1;

		var choices = [
			[ 'on', '始终开启', '不受时间限制，保持亮起' ],
			[ 'off', '始终关闭', '不点亮状态灯' ],
			[ 'timed', '按时间开启', '按设定时段自动切换' ]
		];
		var buttons = E('div', { 'class': 'pcat-led-mode-buttons', 'role': 'group', 'aria-label': '工作模式' });

		function sync() {
			var items = buttons.querySelectorAll('[data-led-mode-button]');
			for (var i = 0; i < items.length; i++) {
				var active = items[i].getAttribute('data-led-mode-button') === mode.value;
				items[i].classList.toggle('is-active', active);
				items[i].setAttribute('aria-pressed', active ? 'true' : 'false');
			}
		}

		for (var i = 0; i < choices.length; i++) {
			var choice = choices[i];
			var button = E('button', {
				'type': 'button',
				'class': 'pcat-led-mode-button',
				'data-led-mode-button': choice[0],
				'aria-pressed': 'false'
			}, [ E('strong', {}, choice[1]), E('small', {}, choice[2]) ]);
			button.addEventListener('click', (function(value) {
				return function() {
					mode.value = value;
					var event = document.createEvent('HTMLEvents');
					event.initEvent('change', true, false);
					mode.dispatchEvent(event);
					sync();
				};
			})(choice[0]));
			buttons.appendChild(button);
		}

		field.insertBefore(buttons, frame);
		mode.addEventListener('change', sync);
		sync();
	},

	updateTimeline: function(root, policy, now) {
		var start = minutes(policy.start);
		var end = minutes(policy.end);
		var spans = root.querySelectorAll('[data-led-window]');
		var marker = root.querySelector('[data-led-now]');

		for (var i = 0; i < spans.length; i++) {
			spans[i].style.left = '0';
			spans[i].style.width = '0';
			spans[i].style.opacity = '0';
		}
		if (policy.mode === 'on' || (policy.mode === 'timed' && start === end)) {
			spans[0].style.width = '100%';
			spans[0].style.opacity = '1';
		}
		else if (policy.mode === 'timed' && start < end) {
			spans[0].style.left = (start / 14.4) + '%';
			spans[0].style.width = ((end - start) / 14.4) + '%';
			spans[0].style.opacity = '1';
		}
		else if (policy.mode === 'timed') {
			spans[0].style.left = (start / 14.4) + '%';
			spans[0].style.width = ((1440 - start) / 14.4) + '%';
			spans[0].style.opacity = '1';
			spans[1].style.width = (end / 14.4) + '%';
			spans[1].style.opacity = '1';
		}
		if (marker)
			marker.style.left = (now / 14.4) + '%';
	},

	updatePolicy: function(root) {
		var policy = this.policy(root);
		var date = new Date();
		var now = date.getHours() * 60 + date.getMinutes();
		var start = minutes(policy.start);
		var end = minutes(policy.end);
		var expected = policy.mode === 'on' || (policy.mode === 'timed' && inWindow(now, start, end));
		var next;
		if (policy.mode === 'on')
			next = '持续亮起';
		else if (policy.mode === 'off')
			next = '保持熄灭';
		else {
			var target = expected ? end : start;
			var delta = (target - now + 1440) % 1440;
			if (!delta)
				delta = 1440;
			next = durationLabel(delta) + '后' + (expected ? '关闭' : '开启');
		}

		this.expected = expected;
		this.setText(root, 'mode', modeLabel(policy.mode));
		this.setText(root, 'window', policy.mode === 'timed' ? policy.start + ' — ' + policy.end : (policy.mode === 'on' ? '全天亮起' : '全天熄灭'));
		this.setText(root, 'next', next);
		this.setText(root, 'expected', expected ? '策略要求亮起' : '策略要求熄灭');
		this.setText(root, 'clock', ('0' + date.getHours()).slice(-2) + ':' + ('0' + date.getMinutes()).slice(-2));
		this.syncModeButtons(root, policy.mode);
		this.updateTimeline(root, policy, now);
	},

	updateHardware: function(root, data) {
		var raw = data && data['status-led-v2-enabled'];
		var known = Number(raw) === 0 || Number(raw) === 1;
		var enabled = Number(raw) === 1;
		var lamp = root.querySelector('.pcat-led-lamp');
		var state = root.querySelector('[data-led-state]');
		var stateText = root.querySelector('[data-led-state-text]');
		var sync = root.querySelector('[data-led-sync]');
		var syncText = root.querySelector('[data-led-sync-text]');

		if (lamp)
			lamp.className = 'pcat-led-lamp ' + (enabled ? 'is-on' : 'is-off');
		if (state) {
			state.className = 'pcat-status-chip ' + (enabled ? 'is-ok' : 'is-off');
			if (stateText)
				stateText.textContent = known ? (enabled ? '指示灯已开启' : '指示灯已关闭') : '状态未知';
		}
		if (sync) {
			sync.className = 'pcat-led-sync ' + (known && enabled === this.expected ? 'is-ok' : 'is-waiting');
			if (syncText)
				syncText.textContent = !known ? '等待硬件数据' : enabled === this.expected ? '硬件与策略一致' : '等待策略同步';
		}
		this.setText(root, 'hardware', known ? (enabled ? '正在发光' : '当前熄灭') : '暂未读取');
		this.setText(root, 'updated', new Date().toLocaleTimeString([], { hour12: false }));
	},

	refresh: function() {
		var root = document.getElementById('pcat-led-dashboard');
		if (!root)
			return Promise.resolve();
		this.updatePolicy(root);
		return L.resolveDefault(pcat.call('io-get'), null).then(L.bind(function(data) {
			this.updateHardware(root, data);
		}, this));
	},

	load: function() {
		return Promise.all([ uci.load('photonicat2'), L.resolveDefault(pcat.call('io-get'), {}) ]);
	},

	handleSave: function() {
		if (!this.map)
			return Promise.resolve();

		return this.map.save(null, true)
			.then(L.bind(function() {
				var formNode = document.querySelector('#pcat-led-dashboard .pcat-led-form .cbi-map');
				if (formNode)
					this.setupModeButtons(formNode);
			}, this))
			.then(function() { return pcat.call('apply'); })
			.then(L.bind(function() {
				return new Promise(function(resolve) { window.setTimeout(resolve, 500); });
			}, this))
			.then(L.bind(this.refresh, this))
			.then(function() { pcat.notify('指示灯设置已保存并生效', 'info'); })
			.catch(function(error) {
				pcat.notify(error.message || String(error), 'error');
			});
	},

	handleSaveApply: function() {
		return this.handleSave();
	},

	handleReset: function() {
		if (!this.map)
			return Promise.resolve();
		return this.map.reset().then(L.bind(function() {
			var root = document.getElementById('pcat-led-dashboard');
			var formNode = root && root.querySelector('.pcat-led-form .cbi-map');
			if (formNode)
				this.setupModeButtons(formNode);
			if (root)
				this.updatePolicy(root);
		}, this));
	},

	render: function(data) {
		pcat.loadStyle();
		var stylesheet = document.getElementById('photonicat2-native-css');
		if (stylesheet && stylesheet.href.indexOf('20260906-led-toast') < 0)
			stylesheet.href = L.resource('photonicat2/photonicat2.css') + '?v=20260906-led-toast';

		var live = data[1] || {};
		var enabled = Number(live['status-led-v2-enabled']) === 1;
		var mode = uci.get('photonicat2', 'main', 'led_mode') || 'on';
		var m = new form.Map('photonicat2');
		this.map = m;
		var s = m.section(form.NamedSection, 'main', 'core', '指示灯策略');
		s.addremove = false;
		var o = s.option(form.ListValue, 'led_mode', '工作模式');
		o.value('on', '始终开启');
		o.value('off', '始终关闭');
		o.value('timed', '按时间开启');
		o.default = 'on';
		o.rmempty = false;
		o = s.option(form.Value, 'led_start', '开启时间');
		o.validate = validateTime;
		o.depends('led_mode', 'timed');
		o.default = '07:00';
		o.rmempty = false;
		o = s.option(form.Value, 'led_end', '关闭时间');
		o.validate = validateTime;
		o.depends('led_mode', 'timed');
		o.default = '23:00';
		o.rmempty = false;

		return m.render().then(L.bind(function(formNode) {
			this.setupModeButtons(formNode);
			var root = E('div', { 'id': 'pcat-led-dashboard', 'class': 'pcat-page pcat-dashboard pcat-led-dashboard' }, [
				E('div', { 'class': 'pcat-dashboard-header' }, [
					E('div', {}, [ E('h2', {}, '电源指示灯'), E('p', { 'class': 'pcat-subtitle' }, 'Photonicat 2 PMU 状态灯实时监测与配置') ]),
					E('div', { 'class': 'pcat-header-status' }, [
						E('span', { 'data-led-state': '', 'class': 'pcat-status-chip ' + (enabled ? 'is-ok' : 'is-off') }, [ E('i'), E('span', { 'data-led-state-text': '' }, enabled ? '指示灯已开启' : '指示灯已关闭') ]),
						E('span', { 'class': 'pcat-update-time' }, [ '更新 ', E('b', { 'data-led-value': 'updated' }, '—') ])
					])
				]),
				E('section', { 'class': 'pcat-card pcat-led-hero' }, [
					E('div', { 'class': 'pcat-led-visual', 'aria-hidden': 'true' }, [ E('div', { 'class': 'pcat-led-lamp ' + (enabled ? 'is-on' : 'is-off') }, [ E('i', { 'class': 'pcat-led-halo halo-one' }), E('i', { 'class': 'pcat-led-halo halo-two' }), E('i', { 'class': 'pcat-led-halo halo-three' }), E('i', { 'class': 'pcat-led-core' }), E('i', { 'class': 'pcat-led-base' }) ]) ]),
					E('div', { 'class': 'pcat-led-identity' }, [ E('span', {}, 'PMU POWER INDICATOR'), E('h3', { 'data-led-value': 'hardware' }, enabled ? '正在发光' : '当前熄灭'), E('p', {}, '状态每 3 秒从设备 PMU 接口刷新'), E('span', { 'data-led-sync': '', 'class': 'pcat-led-sync' }, [ E('i'), E('span', { 'data-led-sync-text': '' }, '检查策略状态') ]) ]),
					E('div', { 'class': 'pcat-led-facts' }, [ E('div', {}, [ E('span', {}, '当前策略'), E('strong', { 'data-led-value': 'mode' }, '—'), E('small', { 'data-led-value': 'expected' }, '—') ]), E('div', {}, [ E('span', {}, '有效时段'), E('strong', { 'data-led-value': 'window' }, '—'), E('small', {}, '支持跨越午夜') ]), E('div', {}, [ E('span', {}, '下一动作'), E('strong', { 'data-led-value': 'next' }, '—'), E('small', {}, '依据设备本地时间') ]) ])
				]),
				E('section', { 'class': 'pcat-card pcat-led-timeline-card' }, [ E('div', { 'class': 'pcat-led-timeline-heading' }, [ E('div', {}, [ E('h3', {}, '24 小时运行时间轴'), E('span', {}, '亮色区间表示指示灯开启时段') ]), E('span', { 'class': 'pcat-led-policy-state' }, [ '当前 ', E('strong', { 'data-led-value': 'clock' }, '—') ]) ]), E('div', { 'class': 'pcat-led-track' }, [ E('i', { 'data-led-window': '' }), E('i', { 'data-led-window': '' }), E('b', { 'data-led-now': '', 'title': '当前时间' }) ]), E('div', { 'class': 'pcat-led-time-labels' }, [ E('span', {}, '00:00'), E('span', {}, '06:00'), E('span', {}, '12:00'), E('span', {}, '18:00'), E('span', {}, '24:00') ]) ]),
				E('div', { 'class': 'pcat-section-title pcat-led-settings-title' }, [ E('span', { 'class': 'pcat-section-icon' }, '◉'), E('div', {}, [ E('h3', {}, '指示灯控制'), E('p', {}, '先选择工作模式，再点击下方保存即可生效') ]) ]),
				E('section', { 'class': 'pcat-card pcat-led-form' }, formNode)
			]);

			root.addEventListener('change', L.bind(function(event) {
				if (event.target && event.target.id && event.target.id.indexOf('cbid.photonicat2.main.led_') >= 0)
					window.setTimeout(L.bind(function() { this.updatePolicy(root); }, this), 0);
			}, this));
			root.addEventListener('input', L.bind(function(event) {
				if (event.target && event.target.id && event.target.id.indexOf('cbid.photonicat2.main.led_') >= 0)
					this.updatePolicy(root);
			}, this));
			window.setTimeout(L.bind(function() {
				this.updatePolicy(root);
				this.updateHardware(root, live);
			}, this), 0);
			poll.add(L.bind(this.refresh, this), 3);
			return root;
		}, this));
	}
});
