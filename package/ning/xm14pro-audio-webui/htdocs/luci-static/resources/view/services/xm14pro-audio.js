'use strict';
'require view';
'require fs';
'require uci';

var STATUS = '/usr/libexec/xm14pro-audio/status';
var CONTROL = '/usr/libexec/xm14pro-audio/control';
var noticeTimer;

function addStyle() {
	if (document.getElementById('xm14-audio-style')) return;
	var link = document.createElement('link');
	link.id = 'xm14-audio-style';
	link.rel = 'stylesheet';
	link.href = '/luci-static/resources/xm14pro/audio.css?v=2';
	document.head.appendChild(link);
}

function parse(result) {
	var data;
	try { data = JSON.parse((result && result.stdout) || '{}'); }
	catch (e) { throw new Error('音频后端返回了无效数据'); }
	if (data.ok === false) throw new Error(data.error || ('操作失败 ' + (data.result == null ? '' : data.result)));
	return data;
}

function run(path, args) {
	return fs.exec(path, args || []).then(parse);
}

function notice(message, error) {
	var node = document.getElementById('xm14-audio-notice');
	if (!node) {
		node = E('button', { 'id': 'xm14-audio-notice', 'type': 'button', 'click': function() { node.classList.remove('show'); } }, [ E('i'), E('span') ]);
		document.body.appendChild(node);
	}
	node.className = error ? 'is-error' : 'is-success';
	node.querySelector('span').textContent = String(message || '');
	window.clearTimeout(noticeTimer);
	window.requestAnimationFrame(function() { node.classList.add('show'); });
	noticeTimer = window.setTimeout(function() { node.classList.remove('show'); }, error ? 5000 : 2600);
}

function service(label, ready, detail) {
	return E('div', { 'class': 'audio-service' }, [
		E('i', { 'class': ready ? 'ready' : 'down' }),
		E('div', {}, [ E('strong', {}, label), E('small', {}, detail) ]),
		E('b', { 'class': ready ? 'ready' : 'down' }, ready ? '正常' : '未就绪')
	]);
}

function slider(label, detail, context, value) {
	var committed = Number(value || 0);
	var output = E('strong', {}, committed + '%');
	var input = E('input', {
		'type': 'range', 'min': 0, 'max': 100, 'step': 1, 'value': committed,
		'input': function() { output.textContent = input.value + '%'; },
		'change': function() {
			var next = Number(input.value);
			if (next === committed) return;
			input.disabled = true;
			run(CONTROL, [ 'volume', context, String(next) ]).then(function() {
				committed = next;
				input.disabled = false;
				notice(label + '已设为 ' + next + '%');
			}, function(e) {
				input.value = committed;
				output.textContent = committed + '%';
				input.disabled = false;
				notice(e.message || String(e), true);
			});
		}
	});
	return E('label', { 'class': 'audio-slider' }, [
		E('div', {}, [ E('span', {}, [ E('b', {}, label), E('small', {}, detail) ]), output ]),
		input
	]);
}

function tone(route, label, description, icon) {
	var button = E('button', {
		'type': 'button', 'class': 'audio-route',
		'click': function() {
			button.disabled = true;
			button.classList.add('testing');
			run(CONTROL, [ 'tone', route, '12', '800' ]).then(function() {
				notice(label + '测试完成');
			}, function(e) {
				notice(e.message || String(e), true);
			}).finally(function() {
				button.disabled = false;
				button.classList.remove('testing');
			});
		}
	}, [ E('span', {}, icon), E('div', {}, [ E('strong', {}, label), E('small', {}, description) ]), E('b', {}, '播放测试音') ]);
	return button;
}

return view.extend({
	load: function() {
		addStyle();
		return Promise.all([ uci.load('xm14audio'), run(STATUS), run(CONTROL, [ 'status' ]) ]);
	},
	render: function(data) {
		var system = data[1], pal = data[2];
		var route = pal.voice_route === 'speaker' ? '扬声器 + 扬声器麦克风' : '听筒 + 内置麦克风';
		return E('div', { 'class': 'audio-page' }, [
			E('section', { 'class': 'audio-hero' }, [
				E('div', { 'class': 'audio-hero-icon' }, '♫'),
				E('div', {}, [ E('small', {}, 'XIAOMI AUDIO'), E('h1', {}, '音频与通话声路'), E('p', {}, '扬声器、听筒、麦克风与通话声路控制。') ]),
				E('b', { 'class': system.hal_running ? 'ready' : 'down' }, pal.pal_ready ? '音频已就绪' : (system.hal_running ? '音频待机' : '音频不可用'))
			]),
			E('section', { 'class': 'audio-services' }, [
				service('声卡', !!system.card_ready, system.pcm_links + ' 条 PCM 链路'),
				service('音频 DSP', !!system.adsprpc_running, 'audioadsprpcd'),
				service('Audio HAL', !!system.hal_running, 'android.hardware.audio.service_64'),
				service('通话声路', !!pal.voice_capable, pal.voice_active ? route : '待机，来电/拨号时自动建立')
			]),
			E('div', { 'class': 'audio-grid' }, [
				E('section', { 'class': 'audio-panel' }, [
					E('h2', {}, '音量'),
					slider('媒体音量', '实体音量键和屏幕浮层使用此音量', 'media', pal.media_volume),
					slider('通话音量', '接听电话时的听筒/扬声器音量', 'call', pal.call_volume)
				]),
				E('section', { 'class': 'audio-panel' }, [
					E('h2', {}, '输出声路检测'),
					E('p', { 'class': 'audio-hint' }, '测试音量 12%，持续 0.8 秒。'),
					E('div', { 'class': 'audio-routes' }, [
						tone('speaker', '扬声器', '上下双扬声器与功放保护链', '◖'),
						tone('earpiece', '听筒', '通话听筒声路', '◗')
					])
				])
			]),
			E('section', { 'class': 'audio-note' }, [
				E('strong', {}, '麦克风与通话'),
				E('span', {}, pal.voice_active ?
					('真实 PAL 语音流已激活：' + route + '；话筒' + (pal.mic_muted ? '已静音。' : '正在采集。')) :
					'电话接通后自动建立双向 PAL 语音流，支持听筒/扬声器切换和话筒静音。')
			])
		]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
