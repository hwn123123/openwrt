'use strict';
'require view';
'require form';
'require poll';
'require uci';
'require photonicat2.common as pcat';

var DEFAULT_SEQUENCE = [
	{ hz: 1047, ms: 160 },
	{ hz: 1319, ms: 160 },
	{ hz: 1568, ms: 180 },
	{ hz: 2093, ms: 320 }
];

var PRESETS = {
	short: { label: '短音', hint: '单次确认', tones: [ { hz: 2700, ms: 220 } ] },
	double: { label: '双音', hint: '按键反馈', tones: [ { hz: 2600, ms: 150 }, { hz: 0, ms: 90 }, { hz: 3200, ms: 190 } ] },
	notify: { label: '通知音', hint: '新事件提醒', tones: [ { hz: 1800, ms: 110 }, { hz: 2400, ms: 110 }, { hz: 3200, ms: 250 } ] },
	success: { label: '完成音', hint: '操作完成', tones: [ { hz: 1568, ms: 120 }, { hz: 2093, ms: 120 }, { hz: 2637, ms: 260 } ] },
	chime: { label: '上升音', hint: '启动提示', tones: DEFAULT_SEQUENCE },
	down: { label: '下降音', hint: '结束提示', tones: [ { hz: 2093, ms: 150 }, { hz: 1568, ms: 150 }, { hz: 1319, ms: 180 }, { hz: 1047, ms: 300 } ] },
	warning: { label: '警告音', hint: '需要注意', tones: [ { hz: 3000, ms: 180 }, { hz: 0, ms: 90 }, { hz: 3000, ms: 180 } ] },
	alarm: { label: '警报音', hint: '高优先级', tones: [ { hz: 3200, ms: 220 }, { hz: 1800, ms: 180 }, { hz: 3200, ms: 220 }, { hz: 1800, ms: 180 } ] }
};

var SOUND_CHOICES = [
	[ 'off', '不提示' ], [ 'short', '短音' ], [ 'double', '双音' ], [ 'notify', '通知音' ],
	[ 'success', '完成音' ], [ 'chime', '上升音' ], [ 'down', '下降音' ],
	[ 'warning', '警告音' ], [ 'alarm', '警报音' ], [ 'custom', '自定义音序' ]
];

var EVENT_DEFS = [
	{ key: 'boot', option: 'beeper_event_boot', label: '系统启动成功', hint: '系统服务启动完成后提示', icon: '⏻', defaultSound: 'success' },
	{ key: 'shutdown', option: 'beeper_event_shutdown', label: '设备关机', hint: '正常关机流程开始时提示', icon: '◉', defaultSound: 'down' },
	{ key: 'charging', option: 'beeper_event_charging', label: '接入充电', hint: '检测到外部电源开始供电', icon: 'ϟ', defaultSound: 'notify' },
	{ key: 'low_battery', option: 'beeper_event_low_battery', label: '低电量', hint: '达到电池低电量阈值时提示一次', icon: '▱', defaultSound: 'warning' },
	{ key: 'cellular', option: 'beeper_event_cellular', label: '5G 连接成功', hint: '蜂窝接口取得默认路由后提示', icon: '5G', defaultSound: 'chime' },
	{ key: 'lan', option: 'beeper_event_lan', label: 'LAN 连接成功', hint: '有线 LAN 链路由断开变为连接', icon: '↔', defaultSound: 'short' },
	{ key: 'wan', option: 'beeper_event_wan', label: 'WAN 连接成功', hint: 'WAN 逻辑接口成功上线', icon: '◎', defaultSound: 'double' },
	{ key: 'wifi_start', option: 'beeper_event_wifi_start', label: 'Wi-Fi 启动成功', hint: '无线接入点开始工作', icon: '⌁', defaultSound: 'success' },
	{ key: 'wifi_client', option: 'beeper_event_wifi_client', label: 'Wi-Fi 新设备接入', hint: '无线客户端数量增加时提示', icon: '+', defaultSound: 'short' }
];

var SAMPLES = {
	twinkle: { label: '小星星', tones: melody([ 60, 60, 67, 67, 69, 69, 67, null, 65, 65, 64, 64, 62, 62, 60 ], 260) },
	ode: { label: '欢乐颂', tones: melody([ 64, 64, 65, 67, 67, 65, 64, 62, 60, 60, 62, 64, 64, 62, 62 ], 230) }
};

function clamp(value, min, max, fallback) {
	value = Number(value);
	if (!isFinite(value))
		value = fallback;
	return Math.max(min, Math.min(max, Math.round(value)));
}

function midiHz(note) {
	return Math.max(50, Math.min(12000, Math.round(440 * Math.pow(2, (note - 69) / 12))));
}

function melody(notes, duration) {
	var tones = [];
	for (var i = 0; i < notes.length; i++) {
		if (notes[i] == null)
			tones.push({ hz: 0, ms: duration });
		else {
			tones.push({ hz: midiHz(notes[i] + 12), ms: duration - 28 });
			tones.push({ hz: 0, ms: 28 });
		}
	}
	return tones;
}

function cleanSequence(value, limit) {
	var source = value;
	if (typeof source === 'string') {
		try { source = JSON.parse(source); }
		catch (e) { source = null; }
	}
	if (!Array.isArray(source) || !source.length)
		source = DEFAULT_SEQUENCE;
	var out = [];
	for (var i = 0; i < source.length && i < (limit || 20); i++) {
		if (!source[i] || typeof source[i] !== 'object')
			continue;
		out.push({ hz: clamp(source[i].hz, 0, 12000, 2700), ms: clamp(source[i].ms, 1, 65535, 200) });
	}
	return out.length ? out : DEFAULT_SEQUENCE.map(function(step) { return { hz: step.hz, ms: step.ms }; });
}

function sequenceValid(sectionId, value) {
	var parsed;
	try { parsed = JSON.parse(value); }
	catch (e) { return '自定义音序不是有效 JSON'; }
	if (!Array.isArray(parsed) || !parsed.length || parsed.length > 20)
		return '自定义音序需要包含 1–20 个步骤';
	for (var i = 0; i < parsed.length; i++) {
		if (!parsed[i] || !isFinite(Number(parsed[i].hz)) || !isFinite(Number(parsed[i].ms)) ||
			Number(parsed[i].hz) < 0 || Number(parsed[i].hz) > 12000 ||
			Number(parsed[i].ms) < 1 || Number(parsed[i].ms) > 65535)
			return '第 ' + (i + 1) + ' 步参数超出范围';
	}
	return true;
}

function validateTime(sectionId, value) {
	return /^([01]\d|2[0-3]):[0-5]\d$/.test(value) ? true : '请输入 HH:MM 格式时间';
}

function formInput(name) {
	var id = 'cbid.photonicat2.main.' + name;
	return document.getElementById('widget.' + id) || document.getElementById(id);
}

function modeLabel(mode) {
	return mode === 'off' ? '始终静音' : mode === 'timed' ? '按时段允许' : '始终允许';
}

function totalMs(tones) {
	var total = 0;
	for (var i = 0; i < tones.length; i++)
		total += clamp(tones[i].ms, 1, 65535, 1);
	return total;
}

function durationLabel(ms) {
	if (ms >= 60000)
		return (ms / 60000).toFixed(ms >= 600000 ? 0 : 1) + ' 分钟';
	return ms >= 1000 ? (ms / 1000).toFixed(1) + ' 秒' : ms + ' 毫秒';
}

function midiToSteps(buffer, octaveShift) {
	var bytes = new Uint8Array(buffer);
	var start = -1;
	for (var i = 0; i + 3 < bytes.length; i++) {
		if (bytes[i] === 0x4d && bytes[i + 1] === 0x54 && bytes[i + 2] === 0x68 && bytes[i + 3] === 0x64) {
			start = i;
			break;
		}
	}
	if (start < 0)
		throw new Error('not midi');
	if (start > 0) {
		buffer = buffer.slice(start);
		bytes = new Uint8Array(buffer);
	}

	var view = new DataView(buffer);
	var pos = 0;
	function need(count) {
		if (pos + count > bytes.length)
			throw new Error('truncated midi');
	}
	function str4() {
		need(4);
		var text = String.fromCharCode(bytes[pos], bytes[pos + 1], bytes[pos + 2], bytes[pos + 3]);
		pos += 4;
		return text;
	}
	function u32() { need(4); var value = view.getUint32(pos); pos += 4; return value; }
	function u16() { need(2); var value = view.getUint16(pos); pos += 2; return value; }
	function variable(end) {
		var value = 0;
		var count = 0;
		var byte;
		do {
			if (pos >= end || ++count > 4)
				throw new Error('bad variable length');
			byte = bytes[pos++];
			value = (value << 7) | (byte & 0x7f);
		} while (byte & 0x80);
		return value;
	}

	if (str4() !== 'MThd')
		throw new Error('bad midi header');
	var headerLength = u32();
	if (headerLength < 6)
		throw new Error('bad midi header');
	u16();
	var trackCount = u16();
	var division = u16();
	pos += headerLength - 6;

	var smpteMs = null;
	if (division & 0x8000) {
		var fps = 256 - (division >> 8);
		var ticksPerFrame = division & 0xff;
		if (!fps || !ticksPerFrame)
			throw new Error('bad midi division');
		smpteMs = 1000 / (fps * ticksPerFrame);
	}
	else if (!division)
		throw new Error('bad midi division');

	var events = [];
	var parsedTracks = 0;
	while (parsedTracks < trackCount && pos + 8 <= bytes.length) {
		var chunkId = str4();
		var chunkLength = u32();
		var end = Math.min(pos + chunkLength, bytes.length);
		if (chunkId !== 'MTrk') {
			pos = end;
			continue;
		}
		parsedTracks++;
		var tick = 0;
		var status = 0;
		while (pos < end) {
			try { tick += variable(end); }
			catch (e) { pos = end; break; }
			if (pos >= end)
				break;
			if (bytes[pos] & 0x80)
				status = bytes[pos++];
			if (!status)
				break;
			var type = status & 0xf0;
			if (type === 0x90 || type === 0x80) {
				if (pos + 2 > end) break;
				var note = bytes[pos++];
				var velocity = bytes[pos++];
				events.push({ tick: tick, priority: type === 0x90 && velocity ? 2 : 1, on: type === 0x90 && velocity > 0, note: note });
			}
			else if (type === 0xa0 || type === 0xb0 || type === 0xe0)
				pos += 2;
			else if (type === 0xc0 || type === 0xd0)
				pos += 1;
			else if (status === 0xff) {
				if (pos >= end) break;
				var meta = bytes[pos++];
				var metaLength;
				try { metaLength = variable(end); }
				catch (e2) { break; }
				if (meta === 0x51 && metaLength === 3 && pos + 3 <= end) {
					var tempo = (bytes[pos] << 16) | (bytes[pos + 1] << 8) | bytes[pos + 2];
					events.push({ tick: tick, priority: 0, tempo: tempo });
				}
				pos += metaLength;
			}
			else if (status === 0xf0 || status === 0xf7) {
				try { pos += variable(end); }
				catch (e3) { break; }
			}
			else if (status === 0xf2)
				pos += 2;
			else if (status === 0xf1 || status === 0xf3)
				pos += 1;
			else if (status < 0xf4 || status > 0xfe)
				break;
			if (pos > end)
				pos = end;
		}
		pos = end;
	}

	events.sort(function(a, b) { return a.tick - b.tick || a.priority - b.priority; });
	var microsecondsPerQuarter = 500000;
	var anchorTick = 0;
	var anchorMs = 0;
	function tickToMs(value) {
		return smpteMs != null ? value * smpteMs : anchorMs + ((value - anchorTick) * microsecondsPerQuarter) / 1000 / division;
	}
	var segments = [];
	var active = [];
	var current = null;
	var segmentStart = 0;
	for (var j = 0; j < events.length; j++) {
		var event = events[j];
		var time = tickToMs(event.tick);
		if (event.tempo != null) {
			anchorMs = time;
			anchorTick = event.tick;
			microsecondsPerQuarter = event.tempo;
			continue;
		}
		if (event.on)
			active.push(event.note);
		else {
			var index = active.indexOf(event.note);
			if (index >= 0)
				active.splice(index, 1);
		}
		var top = active.length ? Math.max.apply(null, active) : null;
		if (top !== current) {
			if (time > segmentStart)
				segments.push({ note: current, ms: time - segmentStart });
			current = top;
			segmentStart = time;
		}
	}

	var steps = [];
	function addRest(value) {
		if (steps.length && steps[steps.length - 1].hz === 0)
			steps[steps.length - 1].ms += value;
		else
			steps.push({ hz: 0, ms: value });
	}
	for (var k = 0; k < segments.length; k++) {
		var duration = Math.min(65535, Math.round(segments[k].ms));
		if (!isFinite(duration) || duration <= 0)
			continue;
		if (segments[k].note == null) {
			if (steps.length)
				addRest(duration);
			continue;
		}
		var gap = duration > 80 ? Math.min(40, Math.round(duration * .1)) : 0;
		steps.push({ hz: midiHz(segments[k].note + 12 * octaveShift), ms: duration - gap });
		if (gap)
			addRest(gap);
		if (steps.length >= 800)
			break;
	}
	while (steps.length && steps[steps.length - 1].hz === 0)
		steps.pop();
	return steps;
}

return view.extend({
	load: function() {
		return Promise.all([ uci.load('photonicat2'), L.resolveDefault(pcat.call('io-get'), {}) ]);
	},

	policy: function() {
		var modeNode = formInput('beeper_mode');
		var startNode = formInput('beeper_start');
		var endNode = formInput('beeper_end');
		return {
			mode: modeNode ? modeNode.value : (uci.get('photonicat2', 'main', 'beeper_mode') || 'on'),
			start: startNode ? startNode.value : (uci.get('photonicat2', 'main', 'beeper_start') || '08:00'),
			end: endNode ? endNode.value : (uci.get('photonicat2', 'main', 'beeper_end') || '22:00')
		};
	},

	setText: function(name, value) {
		var node = this.root && this.root.querySelector('[data-buzzer-value="' + name + '"]');
		if (node)
			node.textContent = value;
	},

	updatePolicy: function() {
		var policy = this.policy();
		this.setText('mode', modeLabel(policy.mode));
		this.setText('window', policy.mode === 'timed' ? policy.start + ' — ' + policy.end : (policy.mode === 'on' ? '全天允许发声' : '全天保持静音'));
		var buttons = this.root ? this.root.querySelectorAll('[data-buzzer-mode]') : [];
		for (var i = 0; i < buttons.length; i++) {
			var active = buttons[i].getAttribute('data-buzzer-mode') === policy.mode;
			buttons[i].classList.toggle('is-active', active);
			buttons[i].setAttribute('aria-pressed', active ? 'true' : 'false');
		}
	},

	setupForm: function(formNode) {
		var frame = formNode.querySelector('[id="cbid.photonicat2.main.beeper_mode"]');
		var mode = formNode.querySelector('[id="widget.cbid.photonicat2.main.beeper_mode"]');
		var custom = formNode.querySelector('[id="cbid.photonicat2.main.beeper_custom"]');
		var actions = formNode.querySelector('.cbi-page-actions');
		var actionHolder = this.root && this.root.querySelector('[data-buzzer-save-actions]');
		if (actions && actionHolder)
			actionHolder.appendChild(actions);
		if (custom) {
			var customRow = custom.closest ? custom.closest('.cbi-value') : null;
			(customRow || custom).classList.add('pcat-buzzer-native-hidden');
		}
		if (!frame || !mode || !frame.parentNode)
			return;
		var field = frame.parentNode;
		var old = field.querySelector('.pcat-buzzer-mode-buttons');
		if (old)
			old.parentNode.removeChild(old);
		frame.classList.add('pcat-buzzer-native-hidden');
		mode.setAttribute('aria-hidden', 'true');
		mode.tabIndex = -1;
		var choices = [
			[ 'on', '始终允许', '全天允许设备发声' ],
			[ 'off', '始终静音', '关闭所有蜂鸣器声音' ],
			[ 'timed', '按时段允许', '仅在设置时间内发声' ]
		];
		var group = E('div', { 'class': 'pcat-buzzer-mode-buttons', 'role': 'group', 'aria-label': '蜂鸣器模式' });
		for (var i = 0; i < choices.length; i++) {
			var item = choices[i];
			var button = E('button', { 'type': 'button', 'class': 'pcat-buzzer-mode-button', 'data-buzzer-mode': item[0], 'aria-pressed': 'false' }, [
				E('strong', {}, item[1]), E('small', {}, item[2])
			]);
			button.addEventListener('click', (function(value, self) {
				return function() {
					mode.value = value;
					var event = document.createEvent('HTMLEvents');
					event.initEvent('change', true, false);
					mode.dispatchEvent(event);
					self.updatePolicy();
				};
			})(item[0], this));
			group.appendChild(button);
		}
		field.insertBefore(group, frame);
		this.updatePolicy();
	},

	setupEventMappings: function(formNode) {
		this.eventInputs = {};
		for (var i = 0; i < EVENT_DEFS.length; i++) {
			var eventDef = EVENT_DEFS[i];
			var id = 'cbid.photonicat2.main.' + eventDef.option;
			var frame = formNode.querySelector('[id="' + id + '"]');
			var select = formNode.querySelector('[id="widget.' + id + '"]');
			if (!frame || !select)
				continue;
			var row = frame.closest ? frame.closest('.cbi-value') : null;
			(row || frame).classList.add('pcat-buzzer-native-hidden');
			this.eventInputs[eventDef.key] = select;
		}

		var eventPicker = this.root && this.root.querySelector('[data-buzzer-event-choice]');
		var soundPicker = this.root && this.root.querySelector('[data-buzzer-sound-choice]');
		if (eventPicker)
			eventPicker.onchange = L.bind(this.syncCompactEvent, this);
		if (soundPicker)
			soundPicker.onchange = L.bind(this.setCompactSound, this);
		this.syncCompactEvent();
	},

	selectedEvent: function() {
		var picker = this.root && this.root.querySelector('[data-buzzer-event-choice]');
		var key = picker ? picker.value : EVENT_DEFS[0].key;
		for (var i = 0; i < EVENT_DEFS.length; i++)
			if (EVENT_DEFS[i].key === key)
				return EVENT_DEFS[i];
		return EVENT_DEFS[0];
	},

	syncCompactEvent: function() {
		if (!this.root)
			return;
		var eventDef = this.selectedEvent();
		var input = this.eventInputs && this.eventInputs[eventDef.key];
		var sound = input ? input.value : (uci.get('photonicat2', 'main', eventDef.option) || eventDef.defaultSound);
		var soundPicker = this.root.querySelector('[data-buzzer-sound-choice]');
		var preview = this.root.querySelector('[data-buzzer-event-test]');
		if (soundPicker)
			soundPicker.value = sound;
		if (preview)
			preview.disabled = sound === 'off';
		this.setText('event-name', eventDef.label);
		this.setText('event-hint', eventDef.hint);
	},

	setCompactSound: function() {
		var eventDef = this.selectedEvent();
		var input = this.eventInputs && this.eventInputs[eventDef.key];
		var soundPicker = this.root && this.root.querySelector('[data-buzzer-sound-choice]');
		if (!input || !soundPicker)
			return;
		input.value = soundPicker.value;
		var event = document.createEvent('HTMLEvents');
		event.initEvent('change', true, false);
		input.dispatchEvent(event);
		this.syncCompactEvent();
	},

	previewEvent: function(key) {
		var event;
		key = key || this.selectedEvent().key;
		for (var i = 0; i < EVENT_DEFS.length; i++) {
			if (EVENT_DEFS[i].key === key) {
				event = EVENT_DEFS[i];
				break;
			}
		}
		if (!event)
			return;
		var input = (this.eventInputs && this.eventInputs[event.key]) || formInput(event.option);
		var sound = input ? input.value : event.defaultSound;
		if (sound === 'off')
			return;
		var tones = sound === 'custom' ? this.sequence : (PRESETS[sound] && PRESETS[sound].tones);
		if (tones)
			return this.playTones(tones, false, event.label);
	},

	syncSequenceInput: function() {
		var input = formInput('beeper_custom');
		if (!input)
			return;
		input.value = JSON.stringify(this.sequence);
		var event = document.createEvent('HTMLEvents');
		event.initEvent('change', true, false);
		input.dispatchEvent(event);
	},

	updateSequenceSummary: function() {
		var duration = totalMs(this.sequence);
		this.setText('steps', this.sequence.length + ' 步');
		this.setText('duration', durationLabel(duration));
		this.setText('sequence', this.sequence.length + ' 步 · ' + durationLabel(duration));
	},

	renderSequence: function() {
		var container = this.root && this.root.querySelector('[data-buzzer-sequence]');
		if (!container)
			return;
		container.textContent = '';
		for (var i = 0; i < this.sequence.length; i++) {
			var step = this.sequence[i];
			var hz = E('input', { 'type': 'number', 'min': '0', 'max': '12000', 'step': '1', 'value': String(step.hz), 'inputmode': 'numeric' });
			var ms = E('input', { 'type': 'number', 'min': '1', 'max': '65535', 'step': '10', 'value': String(step.ms), 'inputmode': 'numeric' });
			var kind = E('span', { 'class': 'pcat-buzzer-step-kind' }, step.hz === 0 ? '停顿' : '音符');
			var preview = E('button', { 'type': 'button', 'class': 'pcat-buzzer-icon-button', 'title': '试听此步骤' }, '▶');
			var up = E('button', { 'type': 'button', 'class': 'pcat-buzzer-icon-button', 'title': '上移', 'disabled': i === 0 ? 'disabled' : null }, '↑');
			var down = E('button', { 'type': 'button', 'class': 'pcat-buzzer-icon-button', 'title': '下移', 'disabled': i === this.sequence.length - 1 ? 'disabled' : null }, '↓');
			var remove = E('button', { 'type': 'button', 'class': 'pcat-buzzer-icon-button is-danger', 'title': '删除' }, '×');
			var row = E('div', { 'class': 'pcat-buzzer-step' }, [
				E('div', { 'class': 'pcat-buzzer-step-number' }, [ E('strong', {}, String(i + 1)), kind ]),
				E('label', {}, [ E('span', {}, '频率 Hz（0 为停顿）'), hz ]),
				E('label', {}, [ E('span', {}, '持续时间 ms'), ms ]),
				E('div', { 'class': 'pcat-buzzer-step-actions' }, [ preview, up, down, remove ])
			]);
			(function(index, hzInput, msInput, kindNode, previewButton, upButton, downButton, removeButton, self) {
				function changed() {
					self.sequence[index].hz = clamp(hzInput.value, 0, 12000, self.sequence[index].hz);
					self.sequence[index].ms = clamp(msInput.value, 1, 65535, self.sequence[index].ms);
					hzInput.value = String(self.sequence[index].hz);
					msInput.value = String(self.sequence[index].ms);
					kindNode.textContent = self.sequence[index].hz === 0 ? '停顿' : '音符';
					self.syncSequenceInput();
					self.updateSequenceSummary();
				}
				hzInput.addEventListener('change', changed);
				msInput.addEventListener('change', changed);
				previewButton.addEventListener('click', function() { changed(); self.playTones([ self.sequence[index] ], false, '单步试听'); });
				upButton.addEventListener('click', function() { self.moveStep(index, -1); });
				downButton.addEventListener('click', function() { self.moveStep(index, 1); });
				removeButton.addEventListener('click', function() { self.removeStep(index); });
			})(i, hz, ms, kind, preview, up, down, remove, this);
			container.appendChild(row);
		}
		this.updateSequenceSummary();
	},

	addStep: function(rest) {
		if (this.sequence.length >= 20) {
			pcat.notify('自定义音序最多 20 步', 'error');
			return;
		}
		this.sequence.push(rest ? { hz: 0, ms: 150 } : { hz: 2700, ms: 200 });
		this.syncSequenceInput();
		this.renderSequence();
	},

	removeStep: function(index) {
		if (this.sequence.length <= 1) {
			pcat.notify('自定义音序至少保留一个步骤', 'error');
			return;
		}
		this.sequence.splice(index, 1);
		this.syncSequenceInput();
		this.renderSequence();
	},

	moveStep: function(index, offset) {
		var target = index + offset;
		if (target < 0 || target >= this.sequence.length)
			return;
		var step = this.sequence[index];
		this.sequence[index] = this.sequence[target];
		this.sequence[target] = step;
		this.syncSequenceInput();
		this.renderSequence();
	},

	loadSequence: function(tones) {
		this.sequence = cleanSequence(tones, 20);
		this.syncSequenceInput();
		this.renderSequence();
	},

	clearPlayback: function() {
		this.playToken = (this.playToken || 0) + 1;
		if (this.chunkTimers) {
			for (var i = 0; i < this.chunkTimers.length; i++)
				window.clearTimeout(this.chunkTimers[i]);
		}
		this.chunkTimers = [];
		if (this.progressTimer)
			window.clearInterval(this.progressTimer);
		if (this.finishTimer)
			window.clearTimeout(this.finishTimer);
		this.progressTimer = null;
		this.finishTimer = null;
		this.playing = false;
		this.updatePlaybackVisual();
	},

	startProgress: function(label, duration, loop) {
		this.playing = true;
		this.playStarted = Date.now();
		this.playDuration = Math.max(1, duration);
		this.playLoop = !!loop;
		this.setText('playing', label);
		this.updatePlaybackVisual();
		this.progressTimer = window.setInterval(L.bind(function() {
			var elapsed = Date.now() - this.playStarted;
			var progress = this.playLoop ? (elapsed % this.playDuration) / this.playDuration : Math.min(1, elapsed / this.playDuration);
			var bar = this.root && this.root.querySelector('[data-buzzer-progress]');
			if (bar)
				bar.style.width = (progress * 100) + '%';
			this.setText('elapsed', durationLabel(this.playLoop ? elapsed % this.playDuration : Math.min(elapsed, this.playDuration)));
		}, this), 120);
		if (!loop)
			this.finishTimer = window.setTimeout(L.bind(this.clearPlayback, this), duration + 180);
	},

	updatePlaybackVisual: function() {
		if (!this.root)
			return;
		var speaker = this.root.querySelector('.pcat-buzzer-speaker');
		var panel = this.root.querySelector('.pcat-buzzer-now-playing');
		if (speaker)
			speaker.classList.toggle('is-playing', !!this.playing);
		if (panel)
			panel.classList.toggle('is-playing', !!this.playing);
		if (!this.playing) {
			this.setText('playing', '等待试听');
			this.setText('elapsed', '0 毫秒');
			var bar = this.root.querySelector('[data-buzzer-progress]');
			if (bar)
				bar.style.width = '0%';
		}
	},

	sendTones: function(tones, loop) {
		return pcat.call('beep-play', [ JSON.stringify(tones), loop ? '1' : '0' ]);
	},

	playTones: function(tones, loop, label) {
		tones = cleanSequence(tones, 20);
		this.clearPlayback();
		this.startProgress(label || '正在试听', totalMs(tones), loop);
		return this.sendTones(tones, loop).catch(L.bind(function(error) {
			this.clearPlayback();
			pcat.notify(error.message || String(error), 'error');
		}, this));
	},

	playLongSequence: function(tones, label) {
		var cleaned = cleanSequence(tones, 800);
		var chunks = [];
		for (var i = 0; i < cleaned.length; i += 20)
			chunks.push(cleaned.slice(i, i + 20));
		this.clearPlayback();
		var token = this.playToken;
		var offset = 0;
		this.startProgress(label || '正在播放音序', totalMs(cleaned), false);
		for (var j = 0; j < chunks.length; j++) {
			(function(chunk, delay, self, expectedToken) {
				function send() {
					if (self.playToken !== expectedToken)
						return;
					self.sendTones(chunk, false).catch(function(error) {
						self.clearPlayback();
						pcat.notify(error.message || String(error), 'error');
					});
				}
				if (!delay)
					send();
				else
					self.chunkTimers.push(window.setTimeout(send, delay + 15));
			})(chunks[j], offset, this, token);
			offset += totalMs(chunks[j]);
		}
	},

	stopPlayback: function() {
		this.clearPlayback();
		return pcat.call('beep-stop').catch(function(error) { pcat.notify(error.message || String(error), 'error'); });
	},

	playPreset: function(name) {
		var preset = PRESETS[name];
		if (preset)
			return this.playTones(preset.tones, false, preset.label);
	},

	playCustomTone: function() {
		var hz = this.root.querySelector('[data-buzzer-custom-hz]');
		var ms = this.root.querySelector('[data-buzzer-custom-ms]');
		var step = { hz: clamp(hz.value, 50, 12000, 2700), ms: clamp(ms.value, 10, 5000, 300) };
		hz.value = String(step.hz);
		ms.value = String(step.ms);
		return this.playTones([ step ], false, '自定义单音');
	},

	readMidi: function(event) {
		var file = event.target.files && event.target.files[0];
		if (!file)
			return;
		var reader = new FileReader();
		reader.onload = L.bind(function(e) { this.midiBuffer = e.target.result; this.parseMidi(); }, this);
		reader.onerror = function() { pcat.notify('无法读取 MIDI 文件', 'error'); };
		reader.readAsArrayBuffer(file);
	},

	parseMidi: function() {
		if (!this.midiBuffer)
			return;
		var octave = Number(this.root.querySelector('[data-buzzer-octave]').value || 0);
		try {
			this.midiSequence = midiToSteps(this.midiBuffer, octave);
			if (!this.midiSequence.length)
				throw new Error('empty');
			this.setText('midi', this.midiSequence.length + ' 步 · ' + durationLabel(totalMs(this.midiSequence)));
			var actions = this.root.querySelector('[data-buzzer-midi-actions]');
			if (actions)
				actions.classList.add('is-ready');
		}
		catch (e) {
			this.midiSequence = [];
			this.setText('midi', '无法解析此 MIDI 文件');
			pcat.notify('MIDI 文件无有效音符或格式不受支持', 'error');
		}
	},

	updateHardware: function(data) {
		var raw = data && data['beeper-enabled'];
		var known = Number(raw) === 0 || Number(raw) === 1;
		var enabled = Number(raw) === 1;
		var speaker = this.root && this.root.querySelector('.pcat-buzzer-speaker');
		var state = this.root && this.root.querySelector('[data-buzzer-state]');
		if (speaker) {
			speaker.classList.toggle('is-enabled', enabled);
			speaker.classList.toggle('is-disabled', !enabled);
		}
		if (state) {
			state.className = 'pcat-status-chip ' + (enabled ? 'is-ok' : 'is-off');
			state.lastChild.textContent = known ? (enabled ? '蜂鸣器允许发声' : '蜂鸣器当前静音') : '状态未知';
		}
		this.setText('hardware', known ? (enabled ? '声音已启用' : '声音已关闭') : '等待设备数据');
		this.setText('updated', new Date().toLocaleTimeString([], { hour12: false }));
	},

	refresh: function() {
		if (!this.root)
			return Promise.resolve();
		this.updatePolicy();
		return L.resolveDefault(pcat.call('io-get'), null).then(L.bind(function(data) { if (data) this.updateHardware(data); }, this));
	},

	handleSave: function() {
		if (!this.map)
			return Promise.resolve();
		this.syncSequenceInput();
		return this.map.save(null, true)
				.then(L.bind(function() {
					var formNode = this.root && this.root.querySelector('.pcat-buzzer-form .cbi-map');
					if (formNode) {
						this.setupForm(formNode);
						this.setupEventMappings(formNode);
					}
				}, this))
			.then(function() { return pcat.call('apply'); })
			.then(L.bind(function() { return new Promise(function(resolve) { window.setTimeout(resolve, 500); }); }, this))
			.then(L.bind(this.refresh, this))
			.then(function() { pcat.notify('蜂鸣器设置已保存并生效', 'info'); })
			.catch(function(error) { pcat.notify(error.message || String(error), 'error'); });
	},

	handleSaveApply: function() { return this.handleSave(); },

	handleReset: function() {
		if (!this.map)
			return Promise.resolve();
		return this.map.reset().then(L.bind(function() {
			var formNode = this.root && this.root.querySelector('.pcat-buzzer-form .cbi-map');
			if (formNode)
				this.setupForm(formNode);
			var custom = formInput('beeper_custom');
				this.sequence = cleanSequence(custom ? custom.value : uci.get('photonicat2', 'main', 'beeper_custom'), 20);
				this.renderSequence();
				this.updatePolicy();
				this.setupEventMappings(formNode);
			}, this));
	},

	render: function(data) {
		pcat.loadStyle();
		var stylesheet = document.getElementById('photonicat2-native-css');
			if (stylesheet && stylesheet.href.indexOf('20260906-buzzer-toast') < 0)
				stylesheet.href = L.resource('photonicat2/photonicat2.css') + '?v=20260906-buzzer-toast';

		var live = data[1] || {};
		var enabled = Number(live['beeper-enabled']) === 1;
		this.sequence = cleanSequence(uci.get('photonicat2', 'main', 'beeper_custom'), 20);
		this.midiSequence = [];
		this.chunkTimers = [];
		this.playing = false;
		this.playToken = 0;

		var m = new form.Map('photonicat2');
		this.map = m;
		var s = m.section(form.NamedSection, 'main', 'core', '蜂鸣器策略');
		s.addremove = false;
		var o = s.option(form.ListValue, 'beeper_mode', '工作模式');
		o.value('on', '始终允许');
		o.value('off', '始终静音');
		o.value('timed', '按时间允许');
		o.default = 'on';
		o.rmempty = false;
		o = s.option(form.Value, 'beeper_start', '允许发声时间');
		o.validate = validateTime;
		o.depends('beeper_mode', 'timed');
		o.default = '08:00';
		o.rmempty = false;
		o = s.option(form.Value, 'beeper_end', '开始静音时间');
		o.validate = validateTime;
		o.depends('beeper_mode', 'timed');
		o.default = '22:00';
		o.rmempty = false;
		o = s.option(form.Value, 'beeper_custom', '自定义音序');
		o.default = JSON.stringify(DEFAULT_SEQUENCE);
		o.validate = sequenceValid;
		o.rmempty = false;
		for (var eventIndex = 0; eventIndex < EVENT_DEFS.length; eventIndex++) {
			var eventDef = EVENT_DEFS[eventIndex];
			o = s.option(form.ListValue, eventDef.option, eventDef.label);
			for (var soundIndex = 0; soundIndex < SOUND_CHOICES.length; soundIndex++)
				o.value(SOUND_CHOICES[soundIndex][0], SOUND_CHOICES[soundIndex][1]);
			o.default = eventDef.defaultSound;
			o.rmempty = false;
		}

			return m.render().then(L.bind(function(formNode) {
				var eventOptions = [];
				for (var eventIndex = 0; eventIndex < EVENT_DEFS.length; eventIndex++)
					eventOptions.push(E('option', { 'value': EVENT_DEFS[eventIndex].key }, EVENT_DEFS[eventIndex].label));
				var soundOptions = [];
				for (var soundIndex = 0; soundIndex < SOUND_CHOICES.length; soundIndex++)
					soundOptions.push(E('option', { 'value': SOUND_CHOICES[soundIndex][0] }, SOUND_CHOICES[soundIndex][1]));

				var eventPicker = E('select', { 'data-buzzer-event-choice': '', 'aria-label': '选择设备事件' }, eventOptions);
				var soundPicker = E('select', { 'data-buzzer-sound-choice': '', 'aria-label': '选择提示声音' }, soundOptions);
				var previewButton = pcat.button('试听', '', L.bind(this.previewEvent, this, null));
				previewButton.setAttribute('data-buzzer-event-test', '');

				var root = E('div', { 'id': 'pcat-buzzer-dashboard', 'class': 'pcat-page pcat-dashboard pcat-buzzer-dashboard is-compact' }, [
					E('div', { 'class': 'pcat-dashboard-header' }, [
						E('div', {}, [ E('h2', {}, '蜂鸣器控制'), E('p', { 'class': 'pcat-subtitle' }, '设备事件提示音与蜂鸣器策略') ]),
						E('div', { 'class': 'pcat-header-status' }, [
							E('span', { 'data-buzzer-state': '', 'class': 'pcat-status-chip ' + (enabled ? 'is-ok' : 'is-off') }, [ E('i'), enabled ? '允许发声' : '当前静音' ]),
							E('span', { 'class': 'pcat-update-time' }, [ '更新 ', E('b', { 'data-buzzer-value': 'updated' }, '—') ])
						])
					]),
					E('section', { 'class': 'pcat-card pcat-buzzer-compact-hero' }, [
						E('div', { 'class': 'pcat-buzzer-speaker ' + (enabled ? 'is-enabled' : 'is-disabled'), 'aria-hidden': 'true' }, [
							E('i', { 'class': 'ring-one' }), E('i', { 'class': 'ring-two' }), E('i', { 'class': 'ring-three' }), E('span', { 'class': 'pcat-buzzer-cone' }),
							E('div', { 'class': 'pcat-buzzer-bars' }, [ E('b'), E('b'), E('b'), E('b'), E('b') ])
						]),
						E('div', { 'class': 'pcat-buzzer-compact-state' }, [
							E('span', {}, 'PMU BUZZER'), E('h3', { 'data-buzzer-value': 'hardware' }, enabled ? '声音已启用' : '声音已关闭'),
							E('div', { 'class': 'pcat-buzzer-now-playing' }, [
								E('span', { 'data-buzzer-value': 'playing' }, '等待试听'), E('small', { 'data-buzzer-value': 'elapsed' }, '0 毫秒'),
								E('div', {}, E('i', { 'data-buzzer-progress': '' }))
							])
						]),
						E('div', { 'class': 'pcat-buzzer-compact-facts' }, [
							E('div', {}, [ E('span', {}, '当前策略'), E('strong', { 'data-buzzer-value': 'mode' }, '—') ]),
							E('div', {}, [ E('span', {}, '允许时段'), E('strong', { 'data-buzzer-value': 'window' }, '—') ]),
							E('div', {}, [ E('span', {}, '自定义音序'), E('strong', { 'data-buzzer-value': 'sequence' }, '—') ])
						])
					]),
					E('section', { 'class': 'pcat-card pcat-buzzer-control-card' }, [
						E('div', { 'class': 'pcat-buzzer-compact-heading' }, [ E('h3', {}, '提示音设置'), E('span', {}, '选择一项，设置声音后保存') ]),
						E('div', { 'class': 'pcat-buzzer-form' }, formNode),
						E('div', { 'class': 'pcat-buzzer-picker-grid' }, [
							E('label', {}, [ E('span', {}, '设备事件'), eventPicker ]),
							E('label', {}, [ E('span', {}, '提示声音'), soundPicker ]),
							E('div', { 'class': 'pcat-buzzer-picker-actions' }, [ previewButton, pcat.button('停止', 'cbi-button-negative', L.bind(this.stopPlayback, this)) ])
						]),
						E('p', { 'class': 'pcat-buzzer-event-summary' }, [ E('strong', { 'data-buzzer-value': 'event-name' }, '—'), E('span', {}, ' · '), E('span', { 'data-buzzer-value': 'event-hint' }, '—') ]),
						E('div', { 'class': 'pcat-buzzer-save-actions', 'data-buzzer-save-actions': '' })
					]),
					E('details', { 'class': 'pcat-card pcat-buzzer-advanced' }, [
						E('summary', {}, [ E('span', {}, '自定义音序（可选）'), E('small', {}, '仅在提示声音选择“自定义音序”时使用') ]),
						E('div', { 'class': 'pcat-buzzer-advanced-body' }, [
							E('div', { 'class': 'pcat-buzzer-card-heading' }, [
								E('div', {}, [ E('h3', {}, '音序步骤'), E('span', {}, [ E('b', { 'data-buzzer-value': 'steps' }, '—'), ' · ', E('b', { 'data-buzzer-value': 'duration' }, '—') ]) ]),
								E('div', { 'class': 'pcat-actions' }, [
									pcat.button('增加音符', '', L.bind(this.addStep, this, false)), pcat.button('增加停顿', '', L.bind(this.addStep, this, true)),
									pcat.button('试听', '', L.bind(function() { return this.playTones(this.sequence, false, '自定义音序'); }, this)),
									pcat.button('停止', 'cbi-button-negative', L.bind(this.stopPlayback, this))
								])
							]),
							E('div', { 'class': 'pcat-buzzer-sequence', 'data-buzzer-sequence': '' })
						])
					])
				]);

			this.root = root;
			this.setupForm(formNode);
			this.setupEventMappings(formNode);
			this.renderSequence();
			this.updatePolicy();
			this.updateHardware(live);
			root.addEventListener('change', L.bind(function(event) {
				if (event.target && event.target.id && event.target.id.indexOf('cbid.photonicat2.main.beeper_') >= 0)
					window.setTimeout(L.bind(this.updatePolicy, this), 0);
			}, this));
			poll.add(L.bind(this.refresh, this), 3);
			return root;
		}, this));
	}
});
