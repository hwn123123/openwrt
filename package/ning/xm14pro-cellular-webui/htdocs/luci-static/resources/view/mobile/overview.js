'use strict';
'require view';
'require poll';
'require fs';
'require uci';
'require mobile.common as mobile';

var DISPLAY_CONTROL = '/usr/libexec/xm14pro-display';

var lastTraffic = null;
var liveHistory = { cpu: [], memory: [], storage: [], signal: [], rx: [], tx: [], cpuTemp: [], modemTemp: [] };

function number(value) {
	var match = String(value == null ? '' : value).match(/-?[0-9]+(?:\.[0-9]+)?/);
	return match ? Number(match[0]) : null;
}

function clamp(value) { return Math.max(0, Math.min(100, Math.round(value))); }
function signalValue(s) { return s.nr_rsrp || s.rsrp || s.rssi || null; }

function signalPercent(s) {
	var quality = number(s.signal_quality);
	if (quality != null && quality >= 0 && quality <= 100) return clamp(quality);
	var raw = number(s.nr_rsrp || s.rsrp);
	if (raw != null) return clamp((raw + 120) * 2.5);
	raw = number(s.rssi);
	return raw == null ? null : clamp((raw + 110) * (100 / 60));
}

function traffic(s) {
	var now = Date.now(), rx = number(s.rx_bytes), tx = number(s.tx_bytes), result = { rx: null, tx: null };
	if (lastTraffic && rx != null && tx != null && rx >= lastTraffic.rx && tx >= lastTraffic.tx && now > lastTraffic.time) {
		var seconds = (now - lastTraffic.time) / 1000;
		result.rx = (rx - lastTraffic.rx) / seconds;
		result.tx = (tx - lastTraffic.tx) / seconds;
	}
	if (rx != null && tx != null) lastTraffic = { rx: rx, tx: tx, time: now };
	return result;
}

function rate(value) { return value == null ? '—' : mobile.bytes(value) + '/s'; }
function tag(text, kind) { return E('span', { 'class': 'ui-tag ' + (kind || '') }, text); }
function row(label, value) { return mobile.missing(value) ? null : mobile.row(label, value); }
function remember(name, value) {
	var n = number(value);
	if (n == null) return;
	liveHistory[name].push(n);
	if (liveHistory[name].length > 32) liveHistory[name].shift();
}

function sparkline(values, tone) {
	if (!values || values.length < 2) return '';
	var width = 180, height = 36, min = Math.min.apply(Math, values), max = Math.max.apply(Math, values);
	if (max === min) { max += 1; min -= 1; }
	var points = values.map(function(value, index) {
		var x = values.length === 1 ? width : index * width / (values.length - 1);
		var y = height - 3 - ((value - min) / (max - min)) * (height - 8);
		return x.toFixed(1) + ',' + y.toFixed(1);
	}).join(' ');
	var svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
	var line = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
	svg.setAttribute('viewBox', '0 0 ' + width + ' ' + height);
	svg.setAttribute('preserveAspectRatio', 'none');
	svg.setAttribute('aria-hidden', 'true');
	line.setAttribute('points', points);
	line.setAttribute('vector-effect', 'non-scaling-stroke');
	svg.appendChild(line);
	return E('div', { 'class': 'xm14-sparkline ' + (tone ? 'trend-' + tone : '') }, svg);
}

function kpi(label, value, detail, icon, tone, progress, trend) {
	var p = Number(progress), progressNode = '';
	if (progress !== null && progress !== undefined && isFinite(p)) {
		p = clamp(p);
		progressNode = E('div', { 'class': 'xm14-progress ' + (p < 35 ? 'bad' : p < 60 ? 'warn' : '') }, E('span', { 'style': 'width:' + p + '%' }));
	}
	return E('article', { 'class': 'xm14-kpi' }, [
		mobile.icon(icon, tone || 'blue', 'art-stat-icon'),
		E('span', { 'class': 'xm14-kpi-label' }, label),
		E('strong', {}, mobile.missing(value) ? '—' : String(value)),
		E('small', {}, mobile.missing(detail) ? '\u00a0' : String(detail)),
		progressNode,
		sparkline(trend, tone)
	]);
}

function reading(label, value) {
	return mobile.missing(value) ? null : E('div', { 'class': 'xm14-reading' }, [ E('span', {}, label), E('strong', {}, String(value)) ]);
}

function resource(label, value, detail, trend) {
	var n = number(value);
	if (n == null) return null;
	n = clamp(n);
	return E('div', { 'class': 'xm14-health-resource' }, [
		E('div', { 'class': 'xm14-resource-head' }, [ E('span', {}, label), E('strong', {}, n + '%') ]),
		E('div', { 'class': 'xm14-progress ' + (n >= 90 ? 'bad' : n >= 75 ? 'warn' : '') }, E('span', { 'style': 'width:' + n + '%' })),
		mobile.missing(detail) ? '' : E('small', {}, detail),
		sparkline(trend, '')
	]);
}

function healthValue(label, value, detail, state) {
	if (mobile.missing(value)) return null;
	return E('div', { 'class': 'xm14-health-value' }, [
		E('span', {}, label), E('strong', { 'class': state || '' }, String(value)),
		mobile.missing(detail) ? '' : E('small', {}, detail)
	]);
}

function temperatureGauge(label, value, detail, trend) {
	var n = number(value);
	if (n == null) return null;
	var p = clamp(n), tone = n >= 85 ? 'is-hot' : n >= 70 ? 'is-warm' : 'is-normal';
	return E('article', {
		'class': 'xm14-temp-gauge ' + tone,
		'style': '--temperature:' + p,
		'role': 'meter',
		'aria-valuemin': '0',
		'aria-valuemax': '100',
		'aria-valuenow': String(n.toFixed(1)),
		'aria-label': label
	}, [
		E('span', { 'class': 'xm14-temp-label' }, label),
		E('div', { 'class': 'xm14-temp-ring' }, [
			E('div', { 'class': 'xm14-temp-center' }, [ E('strong', {}, n.toFixed(1)), E('small', {}, '°C') ])
		]),
		E('div', { 'class': 'xm14-temp-scale' }, [ E('span', {}, '0°'), E('span', {}, mobile.missing(detail) ? _('Live') : detail), E('span', {}, '100°') ]),
		sparkline(trend, tone === 'is-hot' ? 'red' : tone === 'is-warm' ? 'orange' : 'green')
	]);
}

function card(title, rows) {
	rows = rows.filter(Boolean);
	return rows.length ? mobile.card(title, rows) : null;
}

function simState(ready, present) {
	if (ready === true) return tag(_('Ready'), 'is-good');
	if (present === false) return tag(_('Absent'), '');
	return present === true ? tag(_('Not ready'), 'is-warn') : null;
}

function aggregationLabel(s) {
	var count = number(s.carrier_count);
	switch (s.carrier_aggregation_state || s.carrier_aggregation) {
	case 'active': return count && count > 1 ? _('Active · %s CC').format(count) : _('Active');
	case 'single': return _('Single carrier now');
	case 'unavailable': return _('Aggregation not verified');
	default: return _('Waiting for radio data');
	}
}

function aggregationCapability(s) {
	if (s.carrier_aggregation_capability === 'supported' && number(s.carrier_aggregation_max_sub6_cc) > 0)
		return _('X75 chipset limit: %s CC').format(s.carrier_aggregation_max_sub6_cc);
	return _('Capability not verified');
}

function bondingPhase(bonding) {
	bonding = bonding || {};
	if (!bonding.enabled) return '已关闭';
	return ({
		disabled: '已关闭', starting: '正在启动', waiting_primary: '等待主卡',
		waiting_secondary: '正在连接辅卡', configuring: '正在建立负载均衡',
		online: '双卡聚合运行中', stopped: '已停止', error: '运行错误'
	})[bonding.phase] || '等待状态';
}

function displaySetting(label, detail, option, settings, save) {
	var id = 'xm14-cell-display-' + option;
	var input = E('input', {
		'id': id,
		'type': 'checkbox',
		'checked': settings[option] !== '0' ? '' : null,
		'change': function() {
			var previous = settings[option] !== '0';
			var next = input.checked;
			input.disabled = true;
			save(option, next ? '1' : '0').then(function() {
				settings[option] = next ? '1' : '0';
				input.disabled = false;
				mobile.notice(_('Screen display setting applied'), 'success');
			}).catch(function(error) {
				input.checked = previous;
				input.disabled = false;
				mobile.notifyError(error);
			});
		}
	});
	return E('div', { 'class': 'xm14-field xm14-cell-display-setting' }, [
		E('label', { 'for': id }, [ E('strong', {}, label), E('small', {}, detail) ]),
		input
	]);
}

function imeiInputField(settings) {
	var currentReal = settings._realImei || '';
	var inputEl = E('input', {
		'id': 'xm14-custom-imei-input',
		'type': 'text',
		'placeholder': _('Enter custom IMEI (15 digits), or leave blank'),
		'maxlength': '15',
		'pattern': '[0-9]{0,15}',
		'value': settings.cell_custom_imei || '',
		'style': 'flex:1;min-width:0;font-family:monospace;font-size:1em;letter-spacing:.05em'
	});
	var applyBtn = E('button', {
		'class': 'btn cbi-button cbi-button-apply',
		'type': 'button',
		'click': function() {
			var val = inputEl.value.trim();
			if (val && !/^[0-9]{15}$/.test(val)) {
				mobile.notice('请输入 15 位数字；此设置仅用于 LuCI 显示', 'error');
				return;
			}
			applyBtn.disabled = true;
			clearBtn.disabled = true;
			mobile.action('set-imei-display', [ val ]).then(function() {
				settings.cell_custom_imei = val;
				applyBtn.disabled = false;
				clearBtn.disabled = false;
				hintEl.textContent = val
					? _('Showing: ') + val
					: _('Showing real IMEI');
				mobile.notice(val ? _('Custom IMEI saved') : _('Restored to real IMEI'), 'success');
			}).catch(function(err) {
				applyBtn.disabled = false;
				clearBtn.disabled = false;
				mobile.notifyError(err);
			});
		}
	}, _('Apply'));
	var clearBtn = E('button', {
		'class': 'btn cbi-button cbi-button-reset',
		'type': 'button',
		'style': 'margin-left:.4em',
		'click': function() {
			inputEl.value = '';
			applyBtn.click();
		}
	}, _('Clear'));
	var hintEl = E('div', {
		'style': 'font-size:.78em;color:var(--text-muted,#888);margin-top:.3em'
	}, settings.cell_custom_imei
		? _('Showing: ') + settings.cell_custom_imei
		: _('Showing real IMEI'));
	return E('div', {
		'class': 'xm14-field xm14-cell-display-setting',
		'style': 'display:block;padding:.6em 0'
	}, [
		E('div', { 'style': 'margin-bottom:.45em' }, [
			E('strong', {}, _('Custom IMEI (display only)')),
			E('small', { 'style': 'display:block;margin-top:.15em;color:var(--text-muted,#888)' },
				_('Overrides the IMEI shown on this page. Does not affect the real modem IMEI.'))
		]),
		E('div', { 'style': 'display:flex;align-items:center;gap:.4em;width:100%' }, [
			inputEl, applyBtn, clearBtn
		]),
		hintEl,
		E('small', { 'style': 'display:block;margin-top:.3em;color:var(--text-muted,#888)' }, '真实基带 IMEI：' + mobile.text(currentReal, '—'))
	]);
}

function cellularDisplayPanel(settings, save) {
	return mobile.panel(_('Physical screen cellular details'), _('Choose the data shown on the device cellular page'), 'phone', [
		displaySetting(_('Show IMEI'), _('Complete device identifier'), 'cell_show_imei', settings, save),
		imeiInputField(settings),
		displaySetting(_('Show serving cell'), _('Cell ID / NCI, PCI, TAC and ARFCN'), 'cell_show_cell', settings, save),
		displaySetting(_('Show carrier aggregation'), _('Observable radio entries and aggregation status'), 'cell_show_aggregation', settings, save),
		displaySetting(_('Show signal details'), _('Live RSSI, RSRP, RSRQ and SINR'), 'cell_show_signal', settings, save)
	], '', 'xm14-cell-display-panel');
}

function healthPanel(s) {
	var cpu = number(s.cpu_usage_percent), mem = number(s.memory_usage_percent), storage = number(s.storage_usage_percent);
	var usedMemory = s.memory_used_kb && s.memory_total_kb ? '%s / %s'.format(mobile.bytes(Number(s.memory_used_kb) * 1024), mobile.bytes(Number(s.memory_total_kb) * 1024)) : null;
	var usedStorage = s.storage_used_kb && s.storage_total_kb ? '%s / %s'.format(mobile.bytes(Number(s.storage_used_kb) * 1024), mobile.bytes(Number(s.storage_total_kb) * 1024)) : null;
	var items = [
		resource(_('CPU usage'), cpu, [ s.cpu_cores ? _('%s cores').format(s.cpu_cores) : '', s.load_1m && s.load_5m && s.load_15m ? _('Load %s / %s / %s').format(s.load_1m, s.load_5m, s.load_15m) : '' ].filter(Boolean).join(' · '), liveHistory.cpu),
		resource(_('Memory usage'), mem, usedMemory, liveHistory.memory),
		resource(_('Storage usage'), storage, usedStorage, liveHistory.storage),
		temperatureGauge(_('CPU temperature'), s.cpu_temperature_c, _('Live'), liveHistory.cpuTemp),
		temperatureGauge(_('Modem temperature'), s.modem_temperature_c, _('Live'), liveHistory.modemTemp),
		healthValue(_('System uptime'), mobile.uptime(s.uptime)),
		healthValue(_('USB network'), s.usb_up ? _('Active') : _('Inactive'), s.usb_up ? [ s.usb_interface, s.usb_ipv4 ].filter(Boolean).join(' · ') : null, s.usb_up ? 'metric-good' : 'metric-warn'),
		healthValue(_('ALSA sound cards'), Number(s.audio_card_count) > 0 ? String(s.audio_card_count) : _('Not ready'), s.voice_audio_bridge ? _('Voice audio endpoint') : null, Number(s.audio_card_count) > 0 ? 'metric-good' : 'metric-warn')
	].filter(Boolean);
	return E('section', { 'class': 'xm14-section' }, [
		E('div', { 'class': 'xm14-section-title' }, E('h2', {}, _('Device health'))),
		E('div', { 'class': 'ui-card xm14-health-strip' }, items)
	]);
}

function quickTile(iconName, title, value, detail, tone, progress) {
	var p = Number(progress), ring = '';
	if (progress !== null && progress !== undefined && isFinite(p)) {
		p = clamp(p);
		ring = E('div', { 'class': 'xm14-quick-ring ' + (tone || ''), 'style': '--value:' + p }, E('strong', {}, p + '%'));
	}
	return E('article', { 'class': 'xm14-quick-tile' }, [
		E('div', { 'class': 'xm14-quick-title' }, [ mobile.icon(iconName, tone || 'green', 'art-stat-icon'), E('span', {}, title) ]),
		ring || E('strong', { 'class': 'xm14-quick-value' }, mobile.missing(value) ? '—' : String(value)),
		ring ? E('strong', { 'class': 'xm14-quick-value' }, mobile.missing(value) ? '—' : String(value)) : '',
		E('small', {}, mobile.missing(detail) ? ' ' : String(detail))
	]);
}

function copyText(label, value) {
	if (mobile.missing(value)) return '';
	return E('button', {
		'class': 'xm14-copy-button',
		'type': 'button',
		'title': _('Copy %s').format(label),
		'click': function(event) {
			event.preventDefault();
			event.stopPropagation();
			var text = String(value);
			function done() { mobile.notice(_('%s copied').format(label), 'success'); }
			function fallback() {
				var input = E('textarea', { 'readonly': '', 'style': 'position:fixed;left:-9999px;top:-9999px' }, text);
				document.body.appendChild(input);
				input.select();
				try { document.execCommand('copy'); done(); }
				catch (error) { mobile.notifyError(error); }
				input.remove();
			}
			if (navigator.clipboard && window.isSecureContext)
				navigator.clipboard.writeText(text).then(done).catch(fallback);
			else
				fallback();
		}
	}, mobile.icon('link', 'plain'));
}

function cellularDatum(label, value, detail, tone, copy) {
	return E('div', { 'class': 'xm14-cellular-datum ' + (tone || '') }, [
		E('div', { 'class': 'xm14-cellular-datum-head' }, [
			E('span', {}, label),
			copy ? copyText(label, value) : ''
		]),
		E('strong', {}, mobile.missing(value) ? '—' : String(value)),
		mobile.missing(detail) ? '' : E('small', {}, String(detail))
	]);
}

function cellularDetailPanel(s, customImei) {
	var channel = s.nr_arfcn || s.lte_arfcn || s.arfcn;
	var network = [ s.operator_name, s.operator_code, s.access_technologies ].filter(function(v) { return !mobile.missing(v); }).join(' · ');
	var band = [ s.active_bands, s.channel_bandwidths ].filter(function(v) { return !mobile.missing(v); }).join(' · ');
	var cell = [ !mobile.missing(s.pci) ? 'PCI ' + s.pci : '', !mobile.missing(s.tac) ? 'TAC ' + s.tac : '', !mobile.missing(channel) ? 'ARFCN ' + channel : '' ].filter(Boolean).join(' · ');
	var signal = [ !mobile.missing(s.nr_rsrq) ? 'RSRQ ' + s.nr_rsrq : '', !mobile.missing(s.nr_sinr) ? 'SINR ' + s.nr_sinr : '', !mobile.missing(s.rssi) ? 'RSSI ' + s.rssi : '' ].filter(Boolean).join(' · ');
	return mobile.panel(_('Cellular details'), _('Live network, serving cell, signal and device identity'), 'antenna', [
		E('div', { 'class': 'xm14-cellular-detail-grid' }, [
			cellularDatum(_('Current network'), network, band, s.connected ? 'is-good' : 'is-warn'),
			cellularDatum(_('Serving cell'), s.cell_id, cell, 'is-good'),
			cellularDatum(_('Signal measurements'), s.nr_rsrp || s.rsrp || s.rssi, signal, ''),
			cellularDatum(customImei ? 'IMEI · 自定义显示' : _('IMEI'), customImei || s.imei,
				customImei ? '真实基带 IMEI：' + mobile.text(s.imei, '—') : _('IMEI SV %s · MEID %s').format(mobile.text(s.imei_sv, '—'), mobile.text(s.meid, '—')), '', true)
		]),
		E('div', { 'class': 'xm14-cell-selection-strip' }, [
			cellularDatum(_('Current serving'), s.cell_id, cell),
			cellularDatum(_('Band and channel'), s.active_bands, mobile.missing(channel) ? '' : 'ARFCN ' + channel),
			cellularDatum(_('Network selection'), _('Automatic'), s.operator_name),
			E('div', { 'class': 'xm14-cell-selection-action' }, mobile.button(_('Refresh radio details'), '', function(event) {
				var button = event.currentTarget;
				button.disabled = true;
				mobile.action('refresh-radio').then(function() {
					mobile.notice(_('Radio details refreshed'), 'success');
				}).catch(mobile.notifyError).finally(function() { button.disabled = false; });
			}, false, 'refresh'))
		])
	], '', 'xm14-cellular-detail-panel');
}

function listValues(value) {
	if (mobile.missing(value)) return [];
	return String(value).split(/\s*,\s*/).filter(Boolean);
}

function carrierAggregationPanel(s, aggregationStatus) {
	var count = number(s.carrier_count) || 0;
	var maximum = number(s.carrier_aggregation_max_sub6_cc);
	var components = listValues(s.carrier_components);
	var channels = listValues(s.active_channels);
	var bandwidths = listValues(s.channel_bandwidths);
	var componentCount = Math.max(count, components.length, channels.length, bandwidths.length);
	var componentNodes = [];
	var stateTone = s.carrier_aggregation_state === 'active' ? 'is-good' : 'is-info';
	for (var i = 0; i < componentCount; i++) {
		var component = components[i] || '';
		var parts = component.split('@');
		var band = parts[0] || listValues(s.active_bands)[i] || '—';
		var channel = parts[1] || channels[i] || '—';
		componentNodes.push(E('article', { 'class': 'xm14-ca-component ' + (i === 0 ? 'is-primary' : '') }, [
			E('div', { 'class': 'xm14-ca-component-head' }, [
				E('span', {}, '射频记录 ' + (i + 1)),
				E('b', {}, i === 0 ? '当前网络' : '其他记录')
			]),
			E('strong', {}, band),
			E('small', {}, 'ARFCN ' + channel),
			E('em', {}, bandwidths[i] || '带宽未报告')
		]));
	}
	if (!componentNodes.length)
		componentNodes.push(E('div', { 'class': 'xm14-ca-empty' }, '正在等待基带上报射频记录'));

	return mobile.panel('5G 射频与聚合', '基带上报的射频记录；聚合状态需独立验证', 'antenna', [
		E('div', { 'class': 'xm14-ca-summary' }, [
			cellularDatum('实时状态', aggregationStatus, s.access_technologies, stateTone),
			cellularDatum('可见射频记录', count ? String(count) : '—', components.join(' · '), stateTone),
			cellularDatum('可见带宽', bandwidths.join(' + ') || '—', '不代表聚合总带宽', stateTone),
			cellularDatum('芯片规格上限', maximum > 0 ? '最高 ' + maximum + ' CC' : '未验证', 'X75 Sub-6；实际组合取决于设备与网络', 'is-info')
		]),
		E('div', { 'class': 'xm14-ca-components' }, componentNodes),
		E('div', { 'class': 'xm14-ca-radio-row' }, [
			cellularDatum('当前频段', s.active_bands, s.nr_bands || s.lte_bands),
			cellularDatum('活动信道', s.active_channels, !mobile.missing(s.nr_arfcn) ? 'NR ARFCN ' + s.nr_arfcn : !mobile.missing(s.lte_arfcn) ? 'LTE ARFCN ' + s.lte_arfcn : ''),
			cellularDatum('网络架构', s.nr_mode || s.access_technologies, s.operator_name),
			cellularDatum('分配方式', '网络实时调度', '业务负载变化时分量数会动态变化')
		])
	], '', 'xm14-ca-panel');
}

function trendPanel(title, value, detail, history, tone, iconName) {
	return mobile.panel(title, detail, iconName, [
		E('div', { 'class': 'xm14-chart-value' }, mobile.missing(value) ? '—' : String(value)),
		E('div', { 'class': 'xm14-chart-stage' }, [
			sparkline(history, tone),
			E('div', { 'class': 'xm14-chart-grid' })
		])
	], '', 'xm14-trend-panel');
}

function deviceOverview(s, connectedLabel, networkDetail, bonding) {
	var slot = mobile.missing(s.active_data_slot) ? '尚未连接' : '卡槽 ' + s.active_data_slot;
	var aggregation = bondingPhase(bonding);
	return E('section', { 'class': 'xm14-device-overview' }, [
		E('div', { 'class': 'xm14-device-brand' }, [
			E('div', { 'class': 'xm14-cpe-mark ' + (s.connected ? '' : 'is-offline') }, [ E('i'), E('i'), E('b', {}, 'N') ]),
			E('div', {}, [
				E('small', {}, _('Device overall status')),
				E('h1', {}, 'nwrt CPE'),
				E('p', {}, networkDetail || _('Mobile network unavailable'))
			])
		]),
		E('div', { 'class': 'xm14-device-facts' }, [
			mobile.datum('实际上网卡', slot, s.data_slot === 'auto' ? '默认：自动' : '默认：卡槽 ' + s.data_slot, s.connected ? 'is-good' : 'is-bad'),
			mobile.datum('当前运营商', mobile.operatorName(s), s.operator_code || '', s.connected ? 'is-good' : 'is-warn'),
			mobile.datum('双卡聚合', aggregation, bonding && bonding.balance_active ? '两张 SIM 同时拨号 · 多连接负载均衡' : '未接管', bonding && bonding.balance_active && bonding.phase === 'online' ? 'is-good' : 'is-warn'),
			mobile.datum(_('Mobile link'), connectedLabel, s.interface, s.connected ? 'is-good' : 'is-bad')
		])
	]);
}

function dashboard(s, bonding, customImei) {
	var q = signalPercent(s), rates = traffic(s);
	remember('cpu', s.cpu_usage_percent); remember('memory', s.memory_usage_percent); remember('storage', s.storage_usage_percent);
	remember('signal', q); remember('rx', rates.rx); remember('tx', rates.tx);
	remember('cpuTemp', s.cpu_temperature_c); remember('modemTemp', s.modem_temperature_c);
	var connectedLabel = s.connected ? _('Connected') : _('Disconnected');
	var networkDetail = [ !mobile.missing(s.active_data_slot) ? '卡槽 ' + s.active_data_slot : '', mobile.operatorName(s), s.access_technologies, s.active_bands ].filter(function(v) { return !mobile.missing(v); }).join(' · ');
	var signalDetail = [ signalValue(s), s.nr_mode ].filter(function(v) { return !mobile.missing(v); }).join(' · ');
	var aggregationStatus = aggregationLabel(s);
	var imsVoice = mobile.missing(s.ims_voice_status) && mobile.missing(s.ims_voice_technology) ? null : mobile.servicePair(s.ims_voice_status, s.ims_voice_technology);
	var imsSms = mobile.missing(s.ims_sms_status) && mobile.missing(s.ims_sms_technology) ? null : mobile.servicePair(s.ims_sms_status, s.ims_sms_technology);
	var radioReadings = [
		reading('RSSI', s.rssi), reading('LTE RSRP', s.rsrp), reading('LTE RSRQ', s.rsrq), reading('LTE SINR', s.sinr),
		reading('NR RSRP', s.nr_rsrp), reading('NR RSRQ', s.nr_rsrq), reading('NR SINR', s.nr_sinr),
		reading(_('Cell ID / NCI'), s.cell_id), reading(_('TAC'), s.tac), reading(_('PCI'), s.pci), reading('ARFCN', s.nr_arfcn || s.lte_arfcn || s.arfcn)
	].filter(Boolean);
	var linkErrors = [
		!mobile.missing(s.rx_errors) ? _('RX errors %s').format(s.rx_errors) : '', !mobile.missing(s.tx_errors) ? _('TX errors %s').format(s.tx_errors) : '',
		!mobile.missing(s.rx_dropped) ? _('RX drops %s').format(s.rx_dropped) : '', !mobile.missing(s.tx_dropped) ? _('TX drops %s').format(s.tx_dropped) : ''
	].filter(Boolean).join(' · ');
	var sim1 = simState(s.sim1_ready, s.sim1_present), sim2 = simState(s.sim2_ready, s.sim2_present);
	var displayImei = (customImei && customImei.trim()) ? customImei.trim() : s.imei;

	var columnsLeft = [
		card(_('Network session'), [
			row(_('Interface'), s.interface), row(_('Runtime APN'), s.runtime_apn), row(_('PDP type'), s.ip_type), row(_('MTU'), s.mtu),
			row(_('IPv4 address'), s.ipv4), row(_('IPv4 prefix'), s.ipv4_prefix), row(_('IPv4 gateway'), s.ipv4_gateway),
			row(_('IPv6 address'), s.ipv6), row(_('IPv6 prefix'), s.ipv6_prefix), row(_('IPv6 gateway'), s.ipv6_gateway),
			row(_('DNS servers'), s.dns_servers), row(_('Session uptime'), s.connected ? mobile.uptime(s.session_uptime) : null)
		]),
		card(_('SIM and operator'), [
			row(_('Operator'), s.operator_name), row(_('PLMN'), s.operator_code), row(_('Registration'), mobile.stateText(s.registration_state)),
			row(_('Active data SIM'), mobile.slot(s.active_data_slot)), sim1 ? mobile.row(_('SIM slot 1'), sim1) : null, sim2 ? mobile.row(_('SIM slot 2'), sim2) : null,
			row(_('Own number'), s.own_numbers), row(customImei ? 'IMEI · 自定义显示' : _('IMEI'), displayImei),
			customImei ? row('真实基带 IMEI', s.imei) : null, row(_('IMEI SV'), s.imei_sv), row(_('MEID'), s.meid)
		]),
		card(_('Phone services'), [
			row(_('IMS registration'), s.ims_registration), row(_('IMS access'), s.ims_technology), row(_('IMS voice'), imsVoice), row(_('IMS messages'), imsSms),
			row(_('Voice domain'), s.voice_domain), row(_('Voice privacy'), s.voice_privacy), row(_('SMSC address'), s.smsc_address),
			row(_('Active calls'), s.active_call_count), row(_('Stored messages'), s.sms_stored_total), row(_('Unread messages'), s.sms_unread_total)
		])
	].filter(Boolean);

	var columnsRight = [
		card(_('Modem identity'), [
			row(_('Manufacturer'), s.modem_manufacturer), row(_('Model'), s.modem_model), row(_('Baseband revision'), s.modem_revision),
			row(_('Software version'), s.modem_software_version), row(_('Hardware revision'), s.modem_hardware_revision),
			row(_('Operating mode'), s.modem_operating_mode), row(_('Power source'), s.modem_power_source), row(_('Hardware restricted'), s.modem_hardware_restricted)
		]),
		card(_('Radio capabilities'), [
			row(_('Supported networks'), s.network_capabilities), row(_('Data service'), s.data_service_capability),
			row(_('LTE band capabilities'), s.lte_band_capabilities), row(_('NR band capabilities'), s.nr_band_capabilities),
			row(_('Active bands'), s.active_bands), row(_('LTE bands'), s.lte_bands), row(_('NR bands'), s.nr_bands),
			row(_('Aggregation capability'), aggregationCapability(s)), row(_('Current aggregation'), aggregationStatus),
			row(_('Observable RF entries'), s.carrier_components), row(_('Channel bandwidth'), s.channel_bandwidths), row(_('Active channels'), s.active_channels),
			row(_('DMS maximum RX rate'), s.max_rx_rate), row(_('DMS maximum TX rate'), s.max_tx_rate)
		]),
		card(_('Data counters'), [
			row(_('Received'), mobile.bytes(s.rx_bytes)), row(_('Transmitted'), mobile.bytes(s.tx_bytes)), row(_('RX packets'), s.rx_packets), row(_('TX packets'), s.tx_packets), row(_('Link errors and drops'), linkErrors)
		]),
		card(_('System information'), [
			row(_('Hostname'), s.hostname), row(_('Kernel'), s.kernel_release), row(_('Architecture'), s.architecture), row(_('Boot slot'), s.boot_slot),
			row(_('Storage filesystem'), s.storage_filesystem)
		])
	].filter(Boolean);

	return [
		deviceOverview(s, connectedLabel, networkDetail, bonding),
		E('section', { 'class': 'xm14-section' }, [
			E('div', { 'class': 'xm14-section-title' }, [ E('h2', {}, _('Live links')), E('span', {}, _('Refreshes every 3 seconds')) ]),
			E('div', { 'class': 'xm14-quick-grid' }, [
				quickTile('antenna', _('Cellular link'), signalValue(s), signalDetail, q != null && q < 35 ? 'red' : q != null && q < 60 ? 'orange' : 'green', q),
				quickTile('wifi', _('Wireless AP'), s.wifi_up ? (s.wifi_ssid_5g || _('Active')) : _('Inactive'), _('%s clients · %s').format(mobile.text(s.wifi_clients, '0'), mobile.text(s.wifi_country, '—')), s.wifi_up ? 'green' : 'orange'),
				quickTile('usb', _('USB sharing'), s.usb_up ? _('Active') : _('Inactive'), [ s.usb_interface, s.usb_ipv4 ].filter(Boolean).join(' · '), s.usb_up ? 'blue' : 'orange'),
				quickTile('cpu', _('System resources'), number(s.cpu_usage_percent) == null ? '—' : s.cpu_usage_percent + '% CPU', _('%s memory · %s').format(mobile.text(s.memory_usage_percent, '—') + '%', mobile.uptime(s.uptime)), 'indigo')
			])
		]),
		E('div', { 'data-xm14-cellular-allocation': '1' }, [
			cellularDetailPanel(s, customImei),
			carrierAggregationPanel(s, aggregationStatus)
		]),
		mobile.panel(_('Hardware telemetry'), _('Thermal, storage and voice-audio state'), 'thermometer', [
			E('div', { 'class': 'xm14-hardware-grid' }, [
				temperatureGauge(_('CPU temperature'), s.cpu_temperature_c, _('Live'), liveHistory.cpuTemp),
				temperatureGauge(_('Modem temperature'), s.modem_temperature_c, _('Live'), liveHistory.modemTemp),
				quickTile('memory', _('Storage'), number(s.storage_usage_percent) == null ? '—' : s.storage_usage_percent + '%', s.storage_used_kb && s.storage_total_kb ? '%s / %s · %s'.format(mobile.bytes(Number(s.storage_used_kb) * 1024), mobile.bytes(Number(s.storage_total_kb) * 1024), mobile.text(s.storage_filesystem, '—')) : s.storage_filesystem, 'blue', number(s.storage_usage_percent)),
				quickTile('phone', _('Voice audio'), Number(s.audio_card_count) > 0 ? _('%s sound cards').format(s.audio_card_count) : _('Not ready'), s.voice_audio_bridge ? _('Voice audio endpoint') : _('ALSA endpoint unavailable'), Number(s.audio_card_count) > 0 ? 'green' : 'orange')
			])
		], '', 'xm14-hardware-panel'),
		E('section', { 'class': 'xm14-section' }, [
			E('div', { 'class': 'xm14-section-title' }, [ E('h2', {}, _('Live telemetry')), E('span', {}, _('Real samples from this session')) ]),
			E('div', { 'class': 'xm14-trend-grid' }, [
				trendPanel(_('CPU load'), mobile.text(s.cpu_usage_percent, '—') + '%', _('%s cores · load %s / %s / %s').format(mobile.text(s.cpu_cores, '—'), mobile.text(s.load_1m, '—'), mobile.text(s.load_5m, '—'), mobile.text(s.load_15m, '—')), liveHistory.cpu, 'green', 'cpu'),
				trendPanel(_('Memory'), mobile.text(s.memory_usage_percent, '—') + '%', s.memory_used_kb && s.memory_total_kb ? '%s / %s'.format(mobile.bytes(Number(s.memory_used_kb) * 1024), mobile.bytes(Number(s.memory_total_kb) * 1024)) : '', liveHistory.memory, 'green', 'memory'),
				trendPanel(_('Cellular throughput'), '%s ↓  %s ↑'.format(rate(rates.rx), rate(rates.tx)), _('Total %s / %s').format(mobile.bytes(s.rx_bytes), mobile.bytes(s.tx_bytes)), liveHistory.rx, 'blue', 'activity')
			])
		]),
		E('details', { 'class': 'xm14-section xm14-detail-drawer' }, [
			E('summary', {}, [
				E('div', {}, [ E('strong', {}, _('More network and device details')), E('small', {}, _('Expand to inspect full session, modem, IMS and system information')) ]),
				E('span', { 'aria-hidden': 'true' }, '⌄')
			]),
			E('div', { 'class': 'xm14-detail-drawer-body' }, [
				radioReadings.length ? mobile.panel(_('Radio and serving cell'), s.active_bands || '', 'antenna', [ E('div', { 'class': 'xm14-radio-grid' }, radioReadings) ], '', 'xm14-radio-panel') : '',
				mobile.columns(columnsLeft, columnsRight)
			])
		])
	];
}

return view.extend({
	load: function() { return Promise.all([ mobile.status(), uci.load('xm14display'), mobile.bondingStatus() ]); },
	render: function(data) {
		var s = data[0];
		var bonding = data[2] || {};
		var settings = {
			cell_show_imei: uci.get('xm14display', 'main', 'cell_show_imei') || '1',
			cell_show_cell: uci.get('xm14display', 'main', 'cell_show_cell') || '1',
			cell_show_aggregation: uci.get('xm14display', 'main', 'cell_show_aggregation') || '1',
			cell_show_signal: uci.get('xm14display', 'main', 'cell_show_signal') || '1',
			cell_custom_imei: s.imei_display_override || '',
			_realImei: s.imei
		};
		var commitQueue = Promise.resolve();
		function saveDisplayOption(name, value) {
			var task = commitQueue.catch(function() {}).then(function() {
				uci.set('xm14display', 'main', name, value);
				return uci.save();
			}).then(function() {
				return uci.apply();
			}).then(function() {
				return fs.exec(DISPLAY_CONTROL, [ 'apply' ]);
			}).then(function(result) {
				var reply;
				try { reply = JSON.parse((result && result.stdout) || '{}'); }
				catch (error) { throw new Error(_('Screen service returned invalid data')); }
				if (reply.ok === false) throw new Error(reply.error || _('Screen operation failed'));
				return reply;
			});
			commitQueue = task;
			return task;
		}
		var displayPanel = cellularDisplayPanel(settings, saveDisplayOption);
		var holder = E('div', { 'id': 'xm14-overview-live' }, dashboard(s, bonding, s.imei_display_override));
		function placeDisplayPanel() {
			var allocation = holder.querySelector('[data-xm14-cellular-allocation]');
			if (allocation) allocation.after(displayPanel);
		}
		placeDisplayPanel();
		poll.add(function() {
			return Promise.all([ mobile.status(), mobile.bondingStatus() ]).then(function(result) {
				var next = result[0], nextBonding = result[1] || {};
				var detailsOpen = !!holder.querySelector('.xm14-detail-drawer[open]');
				holder.replaceChildren.apply(holder, dashboard(next, nextBonding, next.imei_display_override));
				var details = holder.querySelector('.xm14-detail-drawer');
				if (details && detailsOpen) details.open = true;
				placeDisplayPanel();
			}).catch(mobile.notifyError);
		}, 3);
		return mobile.page([ holder ], 'xm14-overview-compact');
	},
	handleSaveApply: null, handleSave: null, handleReset: null
});
