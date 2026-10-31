'use strict';
'require baseclass';
'require fs';

var CONTROL = '/usr/libexec/xm14pro-modem/control';
var BONDING_CONTROL = '/usr/libexec/xm14pro-bonding/control';
var noticeTimer = null;
var notificationWatcher = null;

var ICONS = {
	activity: [ 'M3 12h4l2-7 4 14 2-7h6' ],
	audio: [ 'M12 3v10', 'M8 9v3a4 4 0 0 0 8 0V9', 'M6 12a6 6 0 0 0 12 0', 'M12 18v3', 'M9 21h6' ],
	arrowDown: [ 'M12 3v15', 'M6 12l6 6 6-6' ],
	arrowUp: [ 'M12 21V6', 'M6 12l6-6 6 6' ],
	antenna: [ 'M5 12.5a10 10 0 0 1 14 0', 'M8 15.5a6 6 0 0 1 8 0', 'M11 18.5a2 2 0 0 1 2 0', 'M12 4v5' ],
	chip: [ 'M8 5h8a3 3 0 0 1 3 3v8a3 3 0 0 1-3 3H8a3 3 0 0 1-3-3V8a3 3 0 0 1 3-3Z', 'M9 9h6v6H9z', 'M9 2v3m6-3v3M9 19v3m6-3v3M2 9h3m-3 6h3m14-6h3m-3 6h3' ],
	cpu: [ 'M9 9h6v6H9z', 'M9 2v3m6-3v3M9 19v3m6-3v3M2 9h3m-3 6h3m14-6h3m-3 6h3' ],
	globe: [ 'M12 21a9 9 0 1 0 0-18 9 9 0 0 0 0 18Z', 'M3 12h18', 'M12 3c2.2 2.4 3.3 5.4 3.3 9S14.2 18.6 12 21c-2.2-2.4-3.3-5.4-3.3-9S9.8 5.4 12 3Z' ],
	link: [ 'M10 13a5 5 0 0 0 7.1.1l2-2a5 5 0 0 0-7.1-7.1l-1.1 1.1', 'M14 11a5 5 0 0 0-7.1-.1l-2 2A5 5 0 0 0 12 20l1.1-1.1' ],
	phone: [ 'M7 2h10a2 2 0 0 1 2 2v16a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2Z', 'M10 18h4' ],
	power: [ 'M12 2v10', 'M6.3 5.7a8 8 0 1 0 11.4 0' ],
	refresh: [ 'M20 6v5h-5', 'M4 18v-5h5', 'M18.5 9a7 7 0 0 0-12-2.5L4 11', 'M5.5 15a7 7 0 0 0 12 2.5L20 13' ],
	sim: [ 'M7 2h7l5 5v15H7a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2Z', 'M9 11h2v3H9zm4 0h2v3h-2zM9 16h2v2H9zm4 0h2v2h-2z' ],
	message: [ 'M4 4h16a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H9l-5 4v-4a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2Z', 'M7 9h10M7 13h7' ],
	memory: [ 'M6 5h12a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V7a2 2 0 0 1 2-2Z', 'M8 9v6m4-6v6m4-6v6M4 8H2m2 4H2m2 4H2m20-8h-2m2 4h-2m2 4h-2' ],
	route: [ 'M6 19a2 2 0 1 0 0-4 2 2 0 0 0 0 4Zm12-10a2 2 0 1 0 0-4 2 2 0 0 0 0 4Z', 'M8 17h4a6 6 0 0 0 6-6M6 15V9a4 4 0 0 1 4-4h6' ],
	usb: [ 'M12 2v14', 'M9 5l3-3 3 3', 'M12 10l4-2', 'M16 8h3v3h-3z', 'M12 13l-4 2', 'M8 13v4', 'M6 17h4v4H6z', 'M12 16a3 3 0 1 1 0 6 3 3 0 0 1 0-6Z' ],
	volume: [ 'M5 9v6h4l5 4V5L9 9H5Z', 'M17 9a4 4 0 0 1 0 6', 'M19.5 6.5a8 8 0 0 1 0 11' ],
	thermometer: [ 'M14 14.8V5a2 2 0 0 0-4 0v9.8a4 4 0 1 0 4 0Z', 'M12 9v7' ],
	wifi: [ 'M4 9a12 12 0 0 1 16 0', 'M7 12a8 8 0 0 1 10 0', 'M10 15a4 4 0 0 1 4 0', 'M12 19h.01' ]
};

function installStyle() {
	if (document.getElementById('xm14-mobile-style'))
		return;
	var link = document.createElement('link');
	link.id = 'xm14-mobile-style';
	link.rel = 'stylesheet';
	link.href = '/luci-static/resources/mobile/mobile.css?v=59';
	document.head.appendChild(link);
}

function parse(result) {
	var data;
	try {
		data = JSON.parse((result && result.stdout) || '{}');
	}
	catch (e) {
		throw new Error(_('Service returned invalid data: %s').format(e.message));
	}
	if (data.ok === false)
		throw new Error(data.error || _('Operation failed'));
	return data;
}

function run(action, args) {
	return fs.exec(CONTROL, [ action ].concat(args || [])).then(parse);
}

function bondingStatus() {
	return fs.exec(BONDING_CONTROL, [ 'status' ]).then(parse).catch(function(error) {
		return { ok: false, available: false, enabled: false, phase: 'unavailable', error: error && error.message ? error.message : String(error) };
	});
}

function operatorName(status) {
	status = status || {};
	var name = String(status.operator_name || '').trim();
	var upper = name.toUpperCase();
	var code = String(status.operator_code || '').trim();
	if (upper.indexOf('MOBILE') >= 0 || upper === 'CMCC' || /^(460)?(00|02|04|07|08)$/.test(code)) return '中国移动';
	if (upper.indexOf('UNICOM') >= 0 || upper === 'CUCC' || /^(460)?(01|06|09)$/.test(code)) return '中国联通';
	if (upper.indexOf('TELECOM') >= 0 || upper === 'CTCC' || /^(460)?(03|05|11)$/.test(code)) return '中国电信';
	if (upper.indexOf('BROADNET') >= 0 || upper === 'CBN' || /^(460)?15$/.test(code)) return '中国广电';
	return name || code || '等待网络';
}

function notice(message, kind) {
	var node = document.getElementById('xm14-mobile-notice');
	if (!node) {
		node = E('button', {
			'id': 'xm14-mobile-notice',
			'type': 'button',
			'role': 'status',
			'click': function() { node.classList.remove('show'); }
		}, [ E('i'), E('span') ]);
		document.body.appendChild(node);
	}
	node.className = 'xm14-float-notice ' + (kind === 'error' ? 'is-error' : kind === 'success' ? 'is-success' : 'is-info');
	node.querySelector('span').textContent = String(message || '');
	window.clearTimeout(noticeTimer);
	window.requestAnimationFrame(function() { node.classList.add('show'); });
	noticeTimer = window.setTimeout(function() { node.classList.remove('show'); }, kind === 'error' ? 5200 : 3000);
}

function notifyError(err) {
	notice(err && err.message ? err.message : String(err), 'error');
}

function startTelephonyNotifications() {
	if (notificationWatcher || /\/admin\/mobile\/sms\/?$/.test(window.location.pathname))
		return;
	if (window.localStorage.getItem('xm14pro.notifications') !== '1')
		return;

	var seenMessages = {}, previousCall = '', initialized = false;
	function key(message) {
		return [ message.direction || '', message.number || '', message.timestamp || '', message.text || '', message.concat_ref || '', message.concat_part || '' ].join('|');
	}
	function push(title, body, tag, persistent) {
		notice(title + (body ? '\n' + body : ''), 'info');
		if (!('Notification' in window) || !window.isSecureContext || window.Notification.permission !== 'granted')
			return;
		var item = new window.Notification(title, { body: body || '', tag: tag, requireInteraction: !!persistent, lang: 'zh-CN' });
		item.onclick = function() {
			window.focus();
			window.location.href = '/cgi-bin/luci/admin/mobile/sms';
			item.close();
		};
	}
	function refresh() {
		if (window.localStorage.getItem('xm14pro.notifications') !== '1')
			return;
		Promise.all([ run('status'), run('messages') ]).then(function(result) {
			var status = result[0], messages = result[1].messages || [];
			var state = String(status.active_call_state || '').toLowerCase();
			var incoming = Number(status.active_call_count || 0) > 0 && [ 'incoming', 'waiting' ].indexOf(state) >= 0;
			var call = incoming ? [ status.active_call_id, status.active_call_number ].join('|') : '';
			if (initialized && call && call !== previousCall)
				push(_('Incoming call'), status.active_call_number || _('Private number'), 'xm14-call-' + call, true);
			previousCall = call;
			messages.forEach(function(message) {
				var signature = key(message);
				if (initialized && !seenMessages[signature] && message.direction === 'in' && message.read !== true)
					push(_('New message'), (message.number || _('Unknown sender')) + '\n' + (message.text || ''), 'xm14-sms-' + signature, false);
				seenMessages[signature] = true;
			});
			initialized = true;
		}).catch(function() {});
	}
	refresh();
	notificationWatcher = window.setInterval(refresh, 3000);
}

function missing(value) {
	if (value === null || value === undefined)
		return true;
	var valueText = String(value).trim().toLowerCase();
	return valueText === '' || valueText === '-' || valueText === '--' || valueText === 'unknown' || valueText === 'n/a';
}

function textValue(value, fallback) {
	return missing(value) ? (fallback || _('Not reported')) : String(value);
}

function stateText(value) {
	if (missing(value))
		return _('Not reported');
	var states = {
		connected: _('Connected'),
		disconnected: _('Disconnected'),
		attached: _('Attached'),
		'not-registered': _('Not registered'),
		searching: _('Searching'),
		available: _('Available'),
		unavailable: _('Unavailable'),
		ready: _('Ready'),
		running: _('Running'),
		online: _('Online'),
		enabled: _('Enabled'),
		disabled: _('Disabled'),
		registered: _('Registered'),
		home: _('Home network'),
		roaming: _('Roaming'),
		on: _('Enabled'),
		off: _('Disabled'),
		incoming: _('Incoming'),
		waiting: _('Waiting'),
		originating: _('Dialing'),
		alerting: _('Ringing'),
		conversation: _('In call'),
		hold: _('On hold'),
		end: _('Ended')
	};
	return states[String(value).toLowerCase()] || String(value);
}

function icon(name, tone, className) {
	var paths = ICONS[name] || ICONS.activity;
	var body = paths.map(function(d) { return '<path d="' + d + '"/>'; }).join('');
	var stroke = tone === 'plain' ? '#0a84ff' : '#ffffff';
	var source = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="' + stroke + '" stroke-width="2.15" stroke-linecap="round" stroke-linejoin="round">' + body + '</svg>';
	return E('span', { 'class': (className || 'ui-row-icon') + ' ' + (tone || 'blue') },
		E('img', { 'src': 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(source), 'alt': '', 'aria-hidden': 'true' })
	);
}

function head(title, subtitle, buttons) {
	installStyle();
	return buttons && buttons.length ?
		E('div', { 'class': 'ui-actions is-end xm14-toolbar' }, buttons) : '';
}

function button(label, style, handler, disabled, iconName) {
	return E('button', {
		'class': 'btn xm14-button ' + (style || ''),
		'disabled': disabled ? '' : null,
		'click': handler
	}, iconName ? [ icon(iconName, 'plain', 'ui-button-icon'), E('span', {}, label) ] : label);
}

function row(label, value) {
	if (missing(value))
		value = E('span', { 'class': 'text-muted' }, _('Not reported'));
	return E('div', { 'class': 'ui-row' }, [
		E('span', { 'class': 'ui-row-label' }, label),
		E('span', { 'class': 'ui-row-value' }, value)
	]);
}

function card(title, children, className, iconName, tone) {
	children = (children || []).filter(function(child) { return child !== '' && child !== null && child !== undefined; });
	return E('section', { 'class': 'ui-group ' + (className || '') }, [
		E('div', { 'class': 'ui-group-head' }, [
			E('h2', {}, title)
		]),
		E('div', { 'class': 'ui-card' }, children || [])
	]);
}

function statusBand(title, subtitle, items, iconName, online) {
	items = (items || []).filter(function(item) { return item && !missing(item.value); });
	return E('section', { 'class': 'xm14-console-band' }, [
		E('div', { 'class': 'xm14-console-brand' }, [
			E('div', { 'class': 'xm14-console-orb ' + (online === false ? 'is-offline' : '') }, [
				E('i'), E('i'), icon(iconName || 'antenna', 'plain', 'xm14-console-icon')
			]),
			E('div', { 'class': 'xm14-console-copy' }, [
				E('small', {}, subtitle || _('Live device status')),
				E('strong', {}, title)
			])
		]),
		E('div', { 'class': 'xm14-console-facts' }, items.map(function(item) {
			return E('div', { 'class': 'xm14-console-fact' }, [
				E('span', {}, item.label),
				E('strong', { 'class': item.good === true ? 'metric-good' : item.good === false ? 'metric-warn' : '' }, String(item.value))
			]);
		}))
	]);
}

function panel(title, subtitle, iconName, children, accessory, className) {
	children = (children || []).filter(function(child) { return child !== '' && child !== null && child !== undefined; });
	return E('section', { 'class': 'xm14-panel ' + (className || '') }, [
		E('div', { 'class': 'xm14-panel-header' }, [
			E('div', { 'class': 'xm14-panel-title' }, [
				icon(iconName || 'activity', 'plain', 'xm14-panel-icon'),
				E('div', {}, [ E('h2', {}, title), subtitle ? E('p', {}, subtitle) : '' ])
			]),
			accessory || ''
		]),
		E('div', { 'class': 'xm14-panel-body' }, children)
	]);
}

function datum(label, value, detail, tone) {
	return E('div', { 'class': 'xm14-datum ' + (tone || '') }, [
		E('span', {}, label),
		E('strong', {}, missing(value) ? '—' : String(value)),
		detail ? E('small', {}, detail) : ''
	]);
}

function metric(title, value, detail, iconName, tone, progress, key) {
	var known = !missing(value);
	var p = Number(progress);
	var showProgress = progress !== null && progress !== undefined && isFinite(p);
	if (showProgress)
		p = Math.max(0, Math.min(100, p));

	var toneClass = tone === 'red' ? 'metric-bad' : tone === 'orange' ? 'metric-warn' : 'metric-good';
	return E('article', {
		'class': 'art-health-item',
		'data-xm14-metric': key || null
	}, [
		E('div', { 'class': 'art-health-label' }, [ icon(iconName, tone === 'orange' ? 'amber' : tone, 'art-stat-icon'), E('span', {}, title) ]),
		E('strong', { 'class': known ? toneClass : 'text-muted' }, known ? String(value) : _('Not reported')),
		E('small', {}, textValue(detail, ' ')),
		showProgress ? E('div', {
			'class': 'art-resource-track ' + toneClass,
			'role': 'progressbar',
			'aria-valuemin': '0',
			'aria-valuemax': '100',
			'aria-valuenow': String(Math.round(p))
		}, E('span', { 'style': 'width:' + p + '%' })) : ''
	]);
}

function sectionHeading(title, accessory) {
	return E('div', { 'class': 'art-overview-heading' }, [ E('h2', {}, title), E('span', {}, accessory || '') ]);
}

function boolValue(value, yes, no) {
	if (value === true)
		return E('span', { 'class': 'ui-tag is-good' }, yes || _('Yes'));
	if (value === false)
		return E('span', { 'class': 'ui-tag is-warn' }, no || _('No'));
	return E('span', { 'class': 'ui-tag' }, _('Not reported'));
}

function state(value, goodValues) {
	if (missing(value))
		return E('span', { 'class': 'ui-tag' }, _('Not reported'));
	var good = (goodValues || []).indexOf(String(value)) >= 0;
	return E('span', { 'class': 'ui-tag ' + (good ? 'is-good' : 'is-warn') }, stateText(value));
}

function unavailable(title, detail) {
	return E('div', { 'class': 'ui-card ui-note' }, [
		icon('activity', 'orange'),
		E('div', {}, [ E('strong', {}, title), E('p', { 'class': 'text-muted' }, detail) ])
	]);
}

function uptime(seconds) {
	var n = Number(seconds);
	if (!isFinite(n) || n < 0)
		return _('Not reported');
	var days = Math.floor(n / 86400);
	var hours = Math.floor((n % 86400) / 3600);
	var minutes = Math.floor((n % 3600) / 60);
	if (days > 0)
		return _('%d d %d h %d min').format(days, hours, minutes);
	if (hours > 0)
		return _('%d h %d min').format(hours, minutes);
	return _('%d min').format(minutes);
}

function bytes(value) {
	var n = Number(value);
	if (!isFinite(n) || n < 0)
		return _('Not reported');
	var units = [ 'B', 'KiB', 'MiB', 'GiB', 'TiB' ];
	var unit = 0;
	while (n >= 1024 && unit < units.length - 1) {
		n /= 1024;
		unit++;
	}
	return (unit === 0 ? n.toFixed(0) : n.toFixed(n >= 100 ? 0 : n >= 10 ? 1 : 2)) + ' ' + units[unit];
}

function slot(value) {
	if (missing(value) || String(value) === 'auto')
		return _('Automatic');
	return _('SIM slot %s').format(value);
}

function servicePair(status, technology) {
	if (missing(status) && missing(technology))
		return _('Not reported');
	if (missing(technology))
		return textValue(status);
	if (missing(status))
		return textValue(technology);
	return '%s · %s'.format(status, technology);
}

return baseclass.extend({
	status: function() { return run('status'); },
	bondingStatus: bondingStatus,
	operatorName: operatorName,
	logs: function() { return run('logs'); },
	action: run,
	notice: notice,
	notifyError: notifyError,
	startTelephonyNotifications: startTelephonyNotifications,
	head: head,
	button: button,
	row: row,
	card: card,
	metric: metric,
	icon: icon,
	text: textValue,
	stateText: stateText,
	missing: missing,
	uptime: uptime,
	bytes: bytes,
	slot: slot,
	servicePair: servicePair,
	sectionHeading: sectionHeading,
	boolValue: boolValue,
	state: state,
	unavailable: unavailable,
	statusBand: statusBand,
	panel: panel,
	datum: datum,
	page: function(children, className) { installStyle(); startTelephonyNotifications(); return E('div', { 'class': 'xm14-page sa-page-app ' + (className || '') }, children || []); },
	grid: function(children, className) { return E('div', { 'class': 'xm14-grid ' + (className || '') }, children); },
	columns: function(left, right) { return E('div', { 'class': 'xm14-columns' }, [ E('div', { 'class': 'xm14-column' }, left), E('div', { 'class': 'xm14-column' }, right) ]); },
	refresh: function() { window.location.reload(); }
});
