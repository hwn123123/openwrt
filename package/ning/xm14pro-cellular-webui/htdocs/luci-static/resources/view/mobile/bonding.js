'use strict';
'require view';
'require fs';
'require poll';
'require mobile.common as mobile';

var CONTROL = '/usr/libexec/xm14pro-bonding/control';

function run(action, args) {
	return fs.exec(CONTROL, [ action ].concat(args || [])).then(function(result) {
		var data;
		try { data = JSON.parse((result && result.stdout) || '{}'); }
		catch (error) { throw new Error('双卡聚合服务返回了无效数据'); }
		if (data.ok === false) throw new Error(data.error || '操作失败');
		return data;
	});
}

function field(label, node, hint) {
	return E('div', { 'class': 'xm14-field' }, [
		E('label', {}, [ label, hint ? E('small', {}, hint) : '' ]), node
	]);
}

function phaseText(phase) {
	return ({
		disabled: '未启用', starting: '正在启动', waiting_primary: '等待主卡',
		waiting_secondary: '正在连接辅卡', configuring: '正在建立负载均衡',
		online: '双卡聚合运行中', stopped: '已停止', error: '运行错误'
	})[phase] || '等待状态';
}

function linkState(sim, active) {
	if (!sim || !sim.connected) return '未连接';
	return active ? '参与聚合' : '数据链路就绪';
}

function rate(value) {
	var n = Number(value);
	return isFinite(n) && n >= 0 ? mobile.bytes(n) + '/s' : '—';
}

function packets(value) {
	var n = Number(value);
	return isFinite(n) && n >= 0 ? n.toLocaleString() : '—';
}

function started(value) {
	var n = Number(value);
	return isFinite(n) && n > 0 ? new Date(n * 1000).toLocaleString() : '—';
}

function compareValue(value, detail, tone) {
	return E('div', { 'class': 'xm14-bond-compare-value ' + (tone || '') }, [
		E('strong', {}, value === '' || value == null ? '—' : String(value)),
		detail ? E('small', {}, detail) : ''
	]);
}

function compareRow(label, sim1Value, sim2Value) {
	return E('div', { 'class': 'xm14-bond-compare-row' }, [
		E('div', { 'class': 'xm14-bond-compare-label' }, label), sim1Value, sim2Value
	]);
}

function compareSection(label) {
	return E('div', { 'class': 'xm14-bond-compare-section' }, label);
}

function cellFor(cells, slot) {
	for (var i = 0; i < cells.length; i++)
		if (Number(cells[i].slot) === slot) return cells[i];
	return {};
}

function cellValue(cell, key) {
	var value = cell && cell[key];
	return value === '' || value == null ? '—' : String(value);
}

function simTone(sim, active) {
	return sim && sim.connected ? (active ? 'is-good' : 'is-warn') : 'is-bad';
}

function comparisonPanel(status, cells) {
	var sim1 = status.sim1 || {}, sim2 = status.sim2 || {};
	var cell1 = cellFor(cells, 1), cell2 = cellFor(cells, 2);
	var active = !!status.balance_active;
	function simHeader(sim, slot, cell) {
		return E('div', { 'class': 'xm14-bond-compare-sim' }, [
			E('div', {}, [ E('small', {}, '卡槽 ' + slot), E('strong', {}, 'SIM ' + slot) ]),
			E('span', { 'class': 'ui-tag ' + simTone(sim, active) }, linkState(sim, active)),
			E('small', { 'class': 'xm14-bond-compare-role' }, [
				sim.role === 'primary' ? '当前主卡会话' : '同时拨号会话', cell.available ? ' · 已驻留' : ''
			])
		]);
	}

	return mobile.panel('双卡实时对比', '两张 SIM 的独立链路、无线状态与实时流量', 'antenna', [
		E('div', { 'class': 'xm14-bond-compare' }, [
			E('div', { 'class': 'xm14-bond-compare-head' }, [
				E('div', { 'class': 'xm14-bond-compare-axis' }, '对比项目'),
				simHeader(sim1, 1, cell1), simHeader(sim2, 2, cell2)
			]),
			compareSection('数据链路'),
			compareRow('链路状态',
				compareValue(linkState(sim1, active), sim1.role === 'primary' ? '主卡数据会话' : '辅卡独立拨号', simTone(sim1, active)),
				compareValue(linkState(sim2, active), sim2.role === 'primary' ? '主卡数据会话' : '辅卡独立拨号', simTone(sim2, active))),
			compareRow('接口与地址', compareValue(sim1.device, sim1.address), compareValue(sim2.device, sim2.address)),
			compareRow('实际 APN', compareValue(sim1.apn, '当前数据会话'), compareValue(sim2.apn, '当前数据会话')),
			compareRow('网关与路由表', compareValue(sim1.gateway, sim1.route_table ? '路由表 ' + sim1.route_table : ''), compareValue(sim2.gateway, sim2.route_table ? '路由表 ' + sim2.route_table : '')),
			compareRow('连接分配比例', compareValue((status.sim1_percent || 0) + '%', '权重 ' + (sim1.weight || 1)), compareValue((status.sim2_percent || 0) + '%', '权重 ' + (sim2.weight || 1))),

			compareSection('服务小区'),
			compareRow('网络与运营商', compareValue(cellValue(cell1, 'radio'), cell1.plmn ? 'PLMN ' + cell1.plmn : ''), compareValue(cellValue(cell2, 'radio'), cell2.plmn ? 'PLMN ' + cell2.plmn : '')),
			compareRow('频段与带宽', compareValue(cellValue(cell1, 'bands'), cell1.bandwidths || ''), compareValue(cellValue(cell2, 'bands'), cell2.bandwidths || '')),
			compareRow('小区 ID / NCI', compareValue(cellValue(cell1, 'cell_id'), cell1.channels ? '信道 ' + cell1.channels : ''), compareValue(cellValue(cell2, 'cell_id'), cell2.channels ? '信道 ' + cell2.channels : '')),
			compareRow('PCI / TAC / ARFCN',
				compareValue(cellValue(cell1, 'pci'), 'TAC ' + cellValue(cell1, 'tac') + ' · ARFCN ' + cellValue(cell1, 'arfcn')),
				compareValue(cellValue(cell2, 'pci'), 'TAC ' + cellValue(cell2, 'tac') + ' · ARFCN ' + cellValue(cell2, 'arfcn'))),

			compareSection('无线质量'),
			compareRow('RSRP 信号强度', compareValue(cellValue(cell1, 'rsrp'), '数值越接近 0 越好'), compareValue(cellValue(cell2, 'rsrp'), '数值越接近 0 越好')),
			compareRow('RSRQ 信号质量', compareValue(cellValue(cell1, 'rsrq'), '数值越接近 0 越好'), compareValue(cellValue(cell2, 'rsrq'), '数值越接近 0 越好')),
			compareRow('SINR 信噪比', compareValue(cellValue(cell1, 'sinr'), '数值越高越好'), compareValue(cellValue(cell2, 'sinr'), '数值越高越好')),

			compareSection('实时传输'),
			compareRow('上传速率', compareValue(rate(sim1.tx_bytes_per_second), '累计 ' + mobile.bytes(sim1.tx_bytes)), compareValue(rate(sim2.tx_bytes_per_second), '累计 ' + mobile.bytes(sim2.tx_bytes))),
			compareRow('下载速率', compareValue(rate(sim1.rx_bytes_per_second), '累计 ' + mobile.bytes(sim1.rx_bytes)), compareValue(rate(sim2.rx_bytes_per_second), '累计 ' + mobile.bytes(sim2.rx_bytes))),
			compareRow('数据包', compareValue('↑ ' + packets(sim1.tx_packets), '↓ ' + packets(sim1.rx_packets)), compareValue('↑ ' + packets(sim2.tx_packets), '↓ ' + packets(sim2.rx_packets))),
			compareRow('链路错误', compareValue('收 ' + packets(sim1.rx_errors), '发 ' + packets(sim1.tx_errors)), compareValue('收 ' + packets(sim2.rx_errors), '发 ' + packets(sim2.tx_errors)))
		])
	], '', 'xm14-bond-comparison');
}

function statusView(status) {
	var activePaths = Number(status.active_paths || 0);
	return E('div', {}, [
		mobile.statusBand('双卡聚合', '两张 SIM 同时拨号，本机多连接负载均衡', [
			{ label: '运行状态', value: phaseText(status.phase), good: status.phase === 'online' },
			{ label: '在线链路', value: activePaths + ' / 2', good: activePaths === 2 },
			{ label: '聚合模式', value: status.balance_active ? '本机多连接负载均衡' : '未接管', good: !!status.balance_active },
			{ label: '分配比例', value: (status.sim1_percent || 0) + '% / ' + (status.sim2_percent || 0) + '%', good: !!status.balance_active }
		], 'link', status.phase === 'online'),
		status.error ? E('div', { 'class': 'xm14-bond-alert' }, status.error) : ''
	]);
}

function summaryPanel(status) {
	var sim1 = status.sim1 || {}, sim2 = status.sim2 || {};
	return mobile.panel('实时汇总', '两条独立数据链路的实时合计', 'activity', [
		E('div', { 'class': 'xm14-datum-grid compact' }, [
			mobile.datum('总上传速率', rate(status.total_tx_bytes_per_second), '两路实时合计', Number(status.total_tx_bytes_per_second) > 0 ? 'is-good' : ''),
			mobile.datum('总下载速率', rate(status.total_rx_bytes_per_second), '两路实时合计', Number(status.total_rx_bytes_per_second) > 0 ? 'is-good' : ''),
			mobile.datum('总上传流量', mobile.bytes(status.total_tx_bytes), 'SIM 1 + SIM 2'),
			mobile.datum('总下载流量', mobile.bytes(status.total_rx_bytes), 'SIM 1 + SIM 2'),
			mobile.datum('SIM 1 出口', sim1.device || '—', sim1.address || ''),
			mobile.datum('SIM 2 出口', sim2.device || '—', sim2.address || ''),
			mobile.datum('路由标记', (sim1.route_mark || '—') + ' / ' + (sim2.route_mark || '—'), '仅使用高位标记'),
			mobile.datum('本次启动', started(status.started_at), status.running ? '服务运行中' : '服务未运行')
		])
	]);
}

return view.extend({
	load: function() { return Promise.all([ run('status'), run('cells') ]); },
	render: function(data) {
		var status = data[0] || {};
		var cells = (data[1] && data[1].cells) || [];
		var enabled = E('input', { 'type': 'checkbox', 'checked': status.enabled ? '' : null });
		var sim1Apn = E('input', { 'value': status.sim1_apn || '', 'placeholder': '自动' });
		var sim2Apn = E('input', { 'value': status.sim2_apn || '', 'placeholder': '自动' });
		var sim1Weight = E('input', { 'type': 'number', 'min': '1', 'max': '100', 'value': status.sim1_weight || '1' });
		var sim2Weight = E('input', { 'type': 'number', 'min': '1', 'max': '100', 'value': status.sim2_weight || '1' });
		var saveButton;
		var statusHolder = E('div', {}, statusView(status));
		var comparisonHolder = E('div', {}, comparisonPanel(status, cells));
		var summaryHolder = E('div', {}, summaryPanel(status));

		function save() {
			saveButton.disabled = true;
			mobile.notice('正在应用双卡聚合配置…', 'info');
			return run('save', [
				enabled.checked ? '1' : '0', sim1Apn.value.trim(), sim2Apn.value.trim(),
				sim1Weight.value.trim(), sim2Weight.value.trim()
			]).then(function() {
				mobile.notice(enabled.checked ? '配置已保存，正在建立双卡数据会话。' : '双卡聚合已关闭。', 'success');
				window.setTimeout(mobile.refresh, 1200);
			}).catch(function(error) {
				saveButton.disabled = false;
				mobile.notifyError(error);
			});
		}

		poll.add(function() {
			return Promise.all([ run('status'), run('cells') ]).then(function(result) {
				status = result[0] || {};
				cells = (result[1] && result[1].cells) || [];
				statusHolder.replaceChildren(statusView(status));
				comparisonHolder.replaceChildren(comparisonPanel(status, cells));
				summaryHolder.replaceChildren(summaryPanel(status));
			}).catch(mobile.notifyError);
		}, 2);

		return mobile.page([
			statusHolder,
			comparisonHolder,
			E('div', { 'class': 'xm14-bond-layout' }, [
				mobile.panel('聚合设置', '配置两张 SIM 的拨号参数和连接分配权重', 'route', [
					field('启用双卡聚合', enabled, '启用后同时建立两张 SIM 的独立数据会话'),
					field('SIM 1 APN', sim1Apn, '留空时使用当前蜂窝网络 APN'),
					field('SIM 2 APN', sim2Apn, '不同运营商时可单独填写'),
					field('SIM 1 连接权重', sim1Weight, '与 SIM 2 的权重共同计算新连接分配比例'),
					field('SIM 2 连接权重', sim2Weight, '两者都为 1 时按 50% / 50% 分配'),
					E('div', { 'class': 'xm14-panel-actions' }, [
						mobile.button('刷新状态', '', mobile.refresh, false, 'refresh'),
						saveButton = mobile.button('保存并应用', 'cbi-button-positive', save)
					])
				], '', 'xm14-bond-config'),
				E('div', { 'class': 'xm14-bond-side' }, [
					summaryHolder
				])
			])
		], 'xm14-bond-page');
	},
	handleSaveApply: null,
	handleSave: null,
	handleReset: null
});
