'use strict';
'require view';
'require fs';
'require uci';

var CONTROL = '/usr/libexec/xm14pro-display';
var noticeTimer;
var PAGE_META = {
	overview: [ _('Overview'), _('Connection, signal and live traffic') ],
	network: [ _('Mobile network'), _('Operator, radio and cell details') ],
	speedtest: [ _('Aggregate speed test'), _('Domestic multi-server, multi-thread IPv4 throughput') ],
	traffic: [ _('Traffic'), _('Live rate, totals and link quality') ],
	device: [ _('Device'), _('Load, memory, temperature, battery and sharing') ],
	connect: [ _('Quick connect'), _('Real Wi-Fi QR code and management addresses') ],
	battery: [ _('Battery'), _('Capacity, charging, Type-C and protection status') ],
	nfc: [ _('NFC'), _('Share Wi-Fi and manage card-copy mode') ],
	infrared: [ _('Infrared'), _('Saved remotes and complete air-conditioner control') ],
	cellular: [ _('Cellular details'), _('Serving cell, carrier aggregation and device identity') ],
	sms: [ _('Messages'), _('Read, compose, send and inspect delivery receipts') ],
	phone: [ _('Phone'), _('Dialpad, calls, microphone, earpiece and speaker') ]
};

function style() {
	if (document.getElementById('xm14-display-style')) return;
	var link = document.createElement('link');
	link.id = 'xm14-display-style';
	link.rel = 'stylesheet';
	link.href = '/luci-static/resources/xm14pro/display.css?v=7';
	document.head.appendChild(link);
}

function parse(result) {
	var data;
	try { data = JSON.parse((result && result.stdout) || '{}'); }
	catch (e) { throw new Error(_('Screen backend returned invalid data')); }
	if (data.ok === false) throw new Error(data.error || _('Screen operation failed'));
	return data;
}

function run(action, args) {
	return fs.exec(CONTROL, [ action ].concat(args || [])).then(parse);
}

function notice(message, error) {
	var node = document.getElementById('xm14-display-notice');
	if (!node) {
		node = E('button', { 'id': 'xm14-display-notice', 'type': 'button', 'click': function() { node.classList.remove('show'); } }, [ E('i'), E('span') ]);
		document.body.appendChild(node);
	}
	node.className = error ? 'is-error' : 'is-success';
	node.querySelector('span').textContent = String(message || '');
	window.clearTimeout(noticeTimer);
	window.requestAnimationFrame(function() { node.classList.add('show'); });
	noticeTimer = window.setTimeout(function() { node.classList.remove('show'); }, error ? 5000 : 2600);
}

function toggle(label, detail, value, change, available) {
	var current = !!value;
	available = available !== false;
	var state = E('b', { 'class': 'screen-switch-state' });
	var button = E('button', {
		'type': 'button',
		'class': 'screen-switch',
		'role': 'switch',
		'disabled': available ? null : '',
		'click': function(ev) {
			ev.preventDefault();
			if (!available) return;
			var previous = current;
			current = !current;
			paint();
			button.disabled = true;
			var operation;
			try { operation = change(current); }
			catch (e) { operation = Promise.reject(e); }
			Promise.resolve(operation).then(function() {
				notice(_('Setting applied'));
				button.disabled = !available;
			}, function(e) {
				current = previous;
				paint();
				button.disabled = !available;
				notice(e.message || String(e), true);
			});
		}
	}, E('i'));
	function paint() {
		button.classList.toggle('is-on', current);
		button.setAttribute('aria-checked', current ? 'true' : 'false');
		button.setAttribute('aria-label', current ? _('Enabled') : _('Disabled'));
		state.classList.toggle('is-on', current && available);
		state.textContent = available ? (current ? _('Enabled') : _('Disabled')) : _('Unavailable');
	}
	paint();
	return E('div', { 'class': 'screen-toggle' }, [
		E('div', {}, [ E('span', {}, label), E('small', {}, detail) ]),
		E('div', { 'class': 'screen-switch-wrap' }, [ state, button ])
	]);
}

function slider(label, detail, value, min, max, suffix, change) {
	var committed = Number(value);
	var output = E('strong', {}, value + suffix);
	var row = E('label', { 'class': 'screen-slider' });
	var input = E('input', {
		'type': 'range', 'min': min, 'max': max, 'value': value,
		'input': function() { output.textContent = input.value + suffix; },
		'change': function() {
			var next = Number(input.value);
			if (next === committed) return;
			input.disabled = true;
			row.classList.add('is-applying');
			var operation;
			try { operation = change(next); }
			catch (e) { operation = Promise.reject(e); }
			Promise.resolve(operation).then(function() {
				committed = next;
				input.disabled = false;
				row.classList.remove('is-applying');
				notice(_('Setting applied'));
			}, function(e) {
				input.value = committed;
				output.textContent = committed + suffix;
				input.disabled = false;
				row.classList.remove('is-applying');
				notice(e.message || String(e), true);
			});
		}
	});
	row.appendChild(E('div', {}, [ E('span', {}, [ E('b', {}, label), E('small', {}, detail) ]), output ]));
	row.appendChild(input);
	return row;
}

function statusItem(label, available, detail) {
	return E('div', { 'class': 'screen-status-item' }, [ E('i', { 'class': available ? 'good' : 'bad' }), E('div', {}, [ E('strong', {}, label), E('small', {}, detail || (available ? _('Ready') : _('Unavailable'))) ]) ]);
}

return view.extend({
	load: function() {
		style();
		return Promise.all([ uci.load('xm14display'), run('status') ]);
	},
	render: function(data) {
		var status = data[1];
		var state = {
			enabled: uci.get('xm14display', 'main', 'enabled') !== '0',
			always: uci.get('xm14display', 'main', 'always_on') === '1',
			powerButton: uci.get('xm14display', 'main', 'power_button') !== '0',
			touchWake: uci.get('xm14display', 'main', 'touch_wake') === '1',
			raiseWake: uci.get('xm14display', 'main', 'raise_wake') === '1',
			volumeOverlay: uci.get('xm14display', 'main', 'volume_overlay') !== '0',
			telephonyPages: uci.get('xm14display', 'main', 'telephony_pages') === '1',
			brightness: Number(uci.get('xm14display', 'main', 'brightness') || 72),
			timeout: Number(uci.get('xm14display', 'main', 'screen_timeout') || 120),
			wakePresses: Number(uci.get('xm14display', 'main', 'power_wake_presses') || 2),
			wakeWindow: Number(uci.get('xm14display', 'main', 'power_wake_window_ms') || 1000),
			refresh: Number(uci.get('xm14display', 'main', 'refresh_interval') || 2),
			speedDuration: Number(uci.get('xm14display', 'main', 'speed_duration') || 5),
			speedThreads: Number(uci.get('xm14display', 'main', 'speed_threads') || 8)
		};
		var pages = [ 'overview', 'network', 'speedtest', 'traffic', 'device', 'connect', 'battery', 'nfc', 'infrared', 'cellular' ];
		if (state.telephonyPages) pages.push('sms', 'phone');

		function command(action, success) {
			run(action).then(function() { notice(success); }).catch(function(e) { notice(e.message, true); });
		}

		var commitQueue = Promise.resolve();
		function setOption(name, value) {
			var task = commitQueue.catch(function() {}).then(function() {
				uci.set('xm14display', 'main', name, String(value));
				return uci.save();
			}).then(function() {
				return uci.apply();
			}).then(function() {
				if (name === 'enabled') return run(String(value) === '1' ? 'start' : 'stop');
				// uci.apply() already triggers xm14pro-display.reload_service().
				// Calling apply again makes brightness and wake settings race.
				return null;
			});
			commitQueue = task;
			return task;
		}

		var pageList = E('div', { 'class': 'screen-pages' });
		pages.forEach(function(key, index) {
			var buttons = [ E('button', { 'class': 'btn', 'click': function() { run('page', [ String(index) ]).then(function() { notice(_('Physical screen switched')); }).catch(function(e) { notice(e.message, true); }); } }, _('Show on screen')) ];
			if (key === 'speedtest') buttons.push(E('button', { 'class': 'btn cbi-button-action', 'click': function() { run('page', [ String(index) ]).then(function() { return run('speedtest'); }).then(function() { notice(_('Aggregate speed test started')); }).catch(function(e) { notice(e.message, true); }); } }, _('Start test')));
			pageList.appendChild(E('div', { 'class': 'screen-page-row' }, [ E('span', { 'class': 'screen-page-order' }, String(index + 1)), E('div', {}, [ E('strong', {}, PAGE_META[key][0]), E('small', {}, PAGE_META[key][1]) ]), E('div', { 'class': 'screen-page-buttons' }, buttons) ]));
		});

		var mode = status.modes && status.modes.length ? status.modes[0] : status.connector_status;
		return E('div', { 'class': 'screen-manager' }, [
			E('section', { 'class': 'screen-status' }, [
				statusItem(_('DRM display'), status.drm_available && status.connector_status === 'connected', mode),
				statusItem(_('Backlight'), status.backlight_available, status.brightness + '%'),
				statusItem(_('Touch panel'), status.touch_available, status.touch_device || _('Driver pending')),
				statusItem(_('Screen service'), status.service_running, status.service_running ? _('Running') : _('Stopped'))
			]),
			E('section', { 'class': 'screen-panel' }, [
				E('h2', {}, _('Immediate control')),
				E('div', { 'class': 'screen-command-grid' }, [
					E('button', { 'class': 'btn cbi-button-action', 'click': function() { command('wake', _('Screen awakened')); } }, _('Wake screen')),
					E('button', { 'class': 'btn', 'click': function() { command('sleep', _('Screen turned off')); } }, _('Turn off screen')),
					E('button', { 'class': 'btn', 'click': function() { command('start', _('Screen service started')); } }, _('Start service')),
					E('button', { 'class': 'btn', 'click': function() { command('stop', _('Screen service stopped')); } }, _('Stop service')),
					E('button', { 'class': 'btn', 'click': function() { command('restart', _('Screen service restarting')); } }, _('Restart service'))
				])
			]),
			E('div', { 'class': 'screen-workspace' }, [
				E('section', { 'class': 'screen-panel' }, [
					E('h2', {}, _('Display settings')),
					toggle(_('Enable phone screen'), _('Start the native display service at boot'), state.enabled, function(v) { return setOption('enabled', v ? '1' : '0'); }),
					toggle(_('Show messages and phone on screen'), _('Hidden by default; enables local SMS and full call controls on the physical screen'), state.telephonyPages, function(v) { return setOption('telephony_pages', v ? '1' : '0'); }),
					toggle(_('Keep screen always on'), _('Ignore the automatic screen timeout'), state.always, function(v) { return setOption('always_on', v ? '1' : '0'); }),
					slider(_('Brightness'), _('Native panel backlight level'), state.brightness, 1, 100, '%', function(v) { return setOption('brightness', v); }),
					slider(_('Screen timeout'), _('Turn off after the last touch'), state.timeout, 5, 600, _(' s'), function(v) { return setOption('screen_timeout', v); }),
					slider(_('Live refresh'), _('Hardware and network data interval'), state.refresh, 1, 10, _(' s'), function(v) { return setOption('refresh_interval', v); })
				]),
				E('section', { 'class': 'screen-panel' }, [
					E('h2', {}, _('Physical buttons and wake')),
					toggle(_('Power button controls screen'), _('One press turns a lit screen off; consecutive presses wake it'), state.powerButton, function(v) { return setOption('power_button', v ? '1' : '0'); }),
					slider(_('Wake press count'), _('Number of consecutive power-button presses required while the screen is off'), state.wakePresses, 1, 4, _(' times'), function(v) { return setOption('power_wake_presses', v); }),
					slider(_('Wake press interval'), _('Maximum interval allowed between consecutive presses'), state.wakeWindow, 300, 2000, _(' ms'), function(v) { return setOption('power_wake_window_ms', v); }),
					toggle(_('Touch to wake'), _('Allow a new touch to wake an extinguished panel'), state.touchWake, function(v) { return setOption('touch_wake', v ? '1' : '0'); }),
					toggle(_('Raise to wake'), _('Motion sensor is unavailable'), state.raiseWake, function(v) { return setOption('raise_wake', v ? '1' : '0'); }, !!status.raise_wake_available),
					toggle(_('Volume overlay'), _('Show the top-level volume panel for physical volume keys'), state.volumeOverlay, function(v) { return setOption('volume_overlay', v ? '1' : '0'); })
				]),
				E('section', { 'class': 'screen-panel' }, [
					E('h2', {}, _('Speed-test settings')),
					slider(_('Duration'), _('Seconds for each download or upload phase'), state.speedDuration, 3, 15, _(' s/direction'), function(v) { return setOption('speed_duration', v); }),
					slider(_('Parallel threads'), _('Aggregate workers across domestic IPv4 mirrors'), state.speedThreads, 2, 16, '', function(v) { return setOption('speed_threads', v); })
				])
			]),
			E('section', { 'class': 'screen-panel screen-layout' }, [ E('h2', {}, _('Physical screen shortcuts')), pageList ])
		]);
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
