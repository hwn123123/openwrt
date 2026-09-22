'use strict';
'require view';
'require form';
'require uci';
'require poll';
'require ui';
'require photonicat2.common as pcat';

function text(value, empty) {
	return value == null || value === '' ? (empty || '—') : String(value);
}

function bytes(value) {
	var n = Number(value || 0), units = [ 'B', 'KB', 'MB', 'GB', 'TB' ], i = 0;
	while (n >= 1024 && i < units.length - 1) {
		n /= 1024;
		i++;
	}
	return '%s %s'.format(n.toFixed(i ? 1 : 0), units[i]);
}

function stageLabel(stage) {
	switch (stage) {
	case 'ready': return '固件已加载';
	case 'bootloader': return '等待驱动加载固件';
	case 'absent': return '未检测到硬件';
	default: return '设备状态未知';
	}
}

function field(name, value, hint) {
	return E('div', { 'class': 'pcat-wireless-field' }, [
		E('span', {}, name),
		E('strong', { 'data-wifi-field': value[0] === '@' ? value.substring(1) : null }, value[0] === '@' ? '—' : value),
		hint ? E('small', {}, hint) : ''
	]);
}

function statusStep(id, label, hint) {
	return E('div', { 'class': 'pcat-wireless-step', 'data-wifi-step': id }, [
		E('i'), E('strong', {}, label), E('small', {}, hint)
	]);
}

function sparkline(id, color) {
	return E('svg', { 'class': 'pcat-wireless-spark', 'viewBox': '0 0 360 96', 'preserveAspectRatio': 'none', 'data-wifi-chart': id }, [
		E('line', { x1: 0, y1: 24, x2: 360, y2: 24 }),
		E('line', { x1: 0, y1: 48, x2: 360, y2: 48 }),
		E('line', { x1: 0, y1: 72, x2: 360, y2: 72 }),
		E('path', { 'class': 'area', fill: color }),
		E('polyline', { 'class': 'line', stroke: color })
	]);
}

function setNode(root, name, value) {
	var node = root.querySelector('[data-wifi-field="%s"]'.format(name));
	if (node && node.textContent !== value) {
		node.textContent = value;
		node.classList.remove('pcat-value-changed');
		void node.offsetWidth;
		node.classList.add('pcat-value-changed');
	}
}

function setStep(root, name, state) {
	var node = root.querySelector('[data-wifi-step="%s"]'.format(name));
	if (!node)
		return;
	node.classList.toggle('is-ok', !!state);
	node.classList.toggle('is-wait', !state);
}

function points(values) {
	var max = Math.max.apply(Math, values.concat([ 1 ]));
	return values.map(function(value, index) {
		var x = values.length > 1 ? index * 360 / (values.length - 1) : 360;
		var y = 90 - Math.min(1, value / max) * 78;
		return '%s,%s'.format(x.toFixed(1), y.toFixed(1));
	}).join(' ');
}

function updateChart(root, name, values) {
	var chart = root.querySelector('[data-wifi-chart="%s"]'.format(name));
	if (!chart)
		return;
	var line = points(values);
	chart.querySelector('.line').setAttribute('points', line);
	chart.querySelector('.area').setAttribute('d', 'M 0 96 L %s L 360 96 Z'.format(line));
}

return view.extend({
	load: function() {
		return Promise.all([
			uci.load('wireless'),
			pcat.call('onboard-wifi-status')
		]);
	},

	makeDashboard: function(data) {
		var hw = data.hardware || {}, driver = data.driver || {};
		var runtime = data.runtime || {}, power = data.power || {};
		var stateGood = !!runtime.running;
		var stateText = stateGood ? '板载热点运行中' : (!power.requested ? '板载无线已断电' : (data.usable ? '板载无线已关闭' : stageLabel(hw.stage)));
		var hardwareText = !power.enabled ? '板载芯片已断电' : (hw.present ? '板载芯片在线' : '板载芯片启动中');

		return E('div', { 'class': 'pcat-page pcat-dashboard pcat-wireless-dashboard' }, [
			E('div', { 'class': 'pcat-dashboard-header' }, [
				E('div', {}, [
					E('h2', {}, 'Photonicat 2 板载无线'),
					E('p', { 'class': 'pcat-subtitle' }, 'AIC8800 内置 USB 无线的实时状态与专属设置')
				]),
				E('div', { 'class': 'pcat-header-status' }, [
					E('span', { 'class': 'pcat-status-chip ' + (hw.present ? 'is-ok' : 'is-off'), 'data-wifi-chip': 'hardware' }, [ E('i'), hardwareText ]),
					E('span', { 'class': 'pcat-status-chip ' + (stateGood ? 'is-ok' : 'is-off'), 'data-wifi-chip': 'runtime' }, [ E('i'), stateText ]),
					E('span', { 'class': 'pcat-update-time', 'data-wifi-field': 'time' }, '更新 --:--:--')
				])
			]),

			E('div', { 'class': 'pcat-card pcat-wireless-hero' }, [
				E('div', { 'class': 'pcat-wireless-radio ' + (stateGood ? 'is-active' : '') }, [
					E('i'), E('i'), E('i'), E('i'),
					E('span', { 'data-wifi-field': 'hero' }, stateGood ? 'ON' : 'OFF')
				]),
				E('div', { 'class': 'pcat-wireless-identity' }, [
					E('span', {}, '板载芯片'),
					E('h3', {}, hw.name || 'AIC8800 板载无线'),
					E('strong', { 'data-wifi-field': 'state' }, stateText),
					E('small', { 'data-wifi-field': 'device' }, hw.product_id ? '%s:%s · USB %s'.format(hw.vendor_id, hw.product_id, hw.usb_name || '1-1.3') : 'USB 1-1.3')
				]),
				E('div', { 'class': 'pcat-wireless-primary' }, [
					field('SSID', '@ssid'),
					field('无线接口', '@interface'),
					field('接入设备', '@clients')
				]),
				E('div', { 'class': 'pcat-wave' })
			]),

			E('div', { 'class': 'pcat-card pcat-wireless-chain' }, [
				E('div', { 'class': 'pcat-chart-heading' }, [
					E('div', {}, [ E('h3', {}, '板载无线启动链'), E('span', {}, '严格按内部 USB 物理路径识别') ]),
					E('span', { 'class': 'pcat-live-badge' }, [ E('i'), '实时检测' ])
				]),
				E('div', { 'class': 'pcat-wireless-steps' }, [
					statusStep('usb', 'AIC USB', 'a69c · 1-1.3'),
					statusStep('firmware', '设备固件', '启动固件与校准数据'),
					statusStep('driver', '内核驱动', 'aic_load_fw + fdrv'),
					statusStep('phy', '无线 PHY', 'mac80211 射频设备'),
					statusStep('ap', '板载热点', '仅 AIC 接口')
				])
			]),

			E('div', { 'class': 'pcat-grid wide pcat-wireless-grid' }, [
				E('div', { 'class': 'pcat-card' }, [
					E('div', { 'class': 'pcat-chart-heading' }, [ E('div', {}, [ E('h3', {}, '硬件与驱动'), E('span', {}, '厂家固件硬件定义') ]), E('span', { 'class': 'pcat-wireless-usb-badge' }, 'USB') ]),
					E('div', { 'class': 'pcat-wireless-fields' }, [
						field('USB 阶段', '@stage'), field('设备 ID', '@vidpid'),
						field('当前内核', text(driver.kernel)), field('绑定驱动', '@driver'),
						field('固件文件', '@firmware'), field('无线 PHY', '@phy')
					])
				]),
				E('div', { 'class': 'pcat-card' }, [
					E('div', { 'class': 'pcat-chart-heading' }, [ E('div', {}, [ E('h3', {}, '热点实时状态'), E('span', {}, '不包含 PCIe MT7927') ]), E('div', { 'class': 'pcat-network-flow ' + (stateGood ? 'is-active' : ''), 'data-wifi-flow': 'traffic' }, [ E('i'), E('i'), E('i'), E('i'), E('i') ]) ]),
					E('div', { 'class': 'pcat-wireless-fields' }, [
						field('运行状态', '@running'), field('MAC 地址', '@mac'),
						field('频段 / 频宽', '@mode'), field('信道 / 频率', '@channel'),
						field('发射功率', '@txpower'), field('加密方式', '@encryption')
					])
				])
			]),

			E('div', { 'class': 'pcat-grid wide pcat-wireless-traffic' }, [
				E('div', { 'class': 'pcat-card pcat-live-chart-card' }, [
					E('div', { 'class': 'pcat-chart-heading' }, [ E('div', {}, [ E('h3', {}, '接收流量'), E('span', {}, '板载接口累计字节变化') ]), E('strong', { 'data-wifi-field': 'rxrate' }, '0 B/s') ]),
					sparkline('rx', 'var(--pcat-good)'),
					E('div', { 'class': 'pcat-chart-scale' }, [ E('span', { 'data-wifi-field': 'rxbytes' }, '0 B'), E('span', {}, '最近约 1 分钟') ])
				]),
				E('div', { 'class': 'pcat-card pcat-live-chart-card' }, [
					E('div', { 'class': 'pcat-chart-heading' }, [ E('div', {}, [ E('h3', {}, '发送流量'), E('span', {}, '板载接口累计字节变化') ]), E('strong', { 'data-wifi-field': 'txrate' }, '0 B/s') ]),
					sparkline('tx', '#35a7ff'),
					E('div', { 'class': 'pcat-chart-scale' }, [ E('span', { 'data-wifi-field': 'txbytes' }, '0 B'), E('span', {}, '最近约 1 分钟') ])
				])
			])
		]);
	},

	makeForm: function(data) {
		var self = this, config = data.config || {}, power = data.power || {};
		var radio = config.radio, ifaces = config.ifaces || [];
		if (!radio)
			return null;

		var m = new form.Map('wireless');
		var s, o;
		this.map = m;
		this.onboardPowerRequested = power.requested ? '1' : '0';

		s = m.section(form.TypedSection, 'wifi-device', '板载 AIC8800 射频');
		s.anonymous = true;
		s.addremove = false;
		s.nodescriptions = true;
		s.cfgsections = function() { return [ radio ]; };

		o = s.option(form.Flag, '_pcat_enabled', '启用板载无线');
		o.enabled = '1';
		o.disabled = '0';
		o.default = '0';
		o.rmempty = false;
		o.cfgvalue = function(section_id) {
			if (!power.requested)
				return '0';
			if (uci.get('wireless', section_id, 'disabled') === '1')
				return '0';
			for (var i = 0; i < ifaces.length; i++)
				if (uci.get('wireless', ifaces[i], 'disabled') === '1')
					return '0';
			return '1';
		};
		o.write = function(section_id, value) {
			self.onboardPowerRequested = value;
			var disabled = value === '1' ? '0' : '1';
			uci.set('wireless', section_id, 'disabled', disabled);
			for (var i = 0; i < ifaces.length; i++)
				uci.set('wireless', ifaces[i], 'disabled', disabled);
		};

		o = s.option(form.Value, 'channel', '2.4 GHz 信道');
		o.placeholder = 'auto';
		o.datatype = 'or(range(1,14),"auto")';

		o = s.option(form.ListValue, 'htmode', '频宽 / 模式');
		o.value('HT20', 'Wi-Fi 4 · 20 MHz');
		o.value('HT40', 'Wi-Fi 4 · 40 MHz');
		o.value('HE20', 'Wi-Fi 6 · 20 MHz');
		o.value('HE40', 'Wi-Fi 6 · 40 MHz');
		o.optional = true;

		o = s.option(form.Value, 'country', '国家代码');
		o.placeholder = 'CN';
		o.datatype = 'and(string,length(2))';

		o = s.option(form.Value, 'txpower', '发射功率 (dBm)');
		o.datatype = 'uinteger';
		o.optional = true;

		s = m.section(form.TypedSection, 'wifi-iface', '板载无线接入点');
		s.anonymous = true;
		s.addremove = false;
		s.nodescriptions = true;
		s.cfgsections = function() { return ifaces.slice(); };

		o = s.option(form.Value, 'ssid', 'Wi-Fi 名称 (SSID)');
		o.rmempty = false;

		o = s.option(form.ListValue, 'encryption', '加密方式');
		o.value('psk2', 'WPA2-PSK');
		o.value('sae-mixed', 'WPA2/WPA3 混合');
		o.value('sae', 'WPA3-SAE');
		o.value('none', '无加密');
		o.rmempty = false;

		o = s.option(form.Value, 'key', '无线密码');
		o.password = true;
		o.datatype = 'wpakey';
		o.depends('encryption', 'psk2');
		o.depends('encryption', 'sae-mixed');
		o.depends('encryption', 'sae');

		o = s.option(form.Flag, 'hidden', '隐藏 SSID');
		o.default = '0';

		o = s.option(form.Flag, 'isolate', '客户端隔离');
		o.default = '0';

		o = s.option(form.Value, 'maxassoc', '最大客户端数');
		o.datatype = 'range(1,256)';
		o.optional = true;

	return m;
	},

	updateDashboard: function(data) {
		if (!this.root)
			return;
		var root = this.root, hw = data.hardware || {}, driver = data.driver || {}, phy = data.phy || {};
		var config = data.config || {}, runtime = data.runtime || {}, power = data.power || {};
		var now = new Date((data.timestamp || Date.now() / 1000) * 1000);
		var running = !!runtime.running;
		var state = running ? '板载热点运行中' : (!power.requested ? '板载无线已断电' : (data.usable ? '板载无线已关闭' : stageLabel(hw.stage)));

		setNode(root, 'time', '更新 ' + now.toLocaleTimeString());
		setNode(root, 'hero', running ? 'ON' : 'OFF');
		setNode(root, 'state', state);
		setNode(root, 'device', hw.product_id ? '%s:%s · USB %s'.format(hw.vendor_id, hw.product_id, hw.usb_name || '1-1.3') : 'USB 1-1.3');
		setNode(root, 'ssid', text(config.ssid, '尚未配置'));
		setNode(root, 'interface', text(runtime.interface, '未创建'));
		setNode(root, 'clients', '%d 台'.format(Number(runtime.clients || 0)));
		setNode(root, 'stage', stageLabel(hw.stage));
		setNode(root, 'vidpid', hw.product_id ? '%s:%s'.format(hw.vendor_id, hw.product_id) : 'a69c:—');
		setNode(root, 'driver', text(hw.bound_driver, driver.loaded ? '已加载' : '未加载'));
		setNode(root, 'firmware', driver.firmware_present ? '%d 个文件'.format(driver.firmware_files || 0) : '未安装');
		setNode(root, 'phy', text(phy.name, '未创建'));
		setNode(root, 'running', running ? '运行中' : (config.enabled ? '正在启动 / 未就绪' : '已关闭'));
		setNode(root, 'mac', text(runtime.mac));
		setNode(root, 'mode', '%s / %s'.format(text(config.band, '2g'), text(config.htmode)));
		setNode(root, 'channel', '%s / %s MHz'.format(text(config.channel), text(runtime.frequency_mhz)));
		setNode(root, 'txpower', config.txpower_dbm == null ? '—' : config.txpower_dbm + ' dBm');
		setNode(root, 'encryption', text(config.encryption));

		setStep(root, 'usb', hw.present);
		setStep(root, 'firmware', driver.firmware_present && hw.stage === 'ready');
		setStep(root, 'driver', driver.loaded);
		setStep(root, 'phy', phy.ready);
		setStep(root, 'ap', running);

		var hero = root.querySelector('.pcat-wireless-radio');
		if (hero)
			hero.classList.toggle('is-active', running);
		var flow = root.querySelector('[data-wifi-flow="traffic"]');
		if (flow)
			flow.classList.toggle('is-active', running && ((runtime.rx_bytes || 0) !== this.lastRx || (runtime.tx_bytes || 0) !== this.lastTx));

		var rx = Number(runtime.rx_bytes || 0), tx = Number(runtime.tx_bytes || 0);
		var stamp = Number(data.timestamp || Date.now() / 1000), elapsed = this.lastStamp ? Math.max(1, stamp - this.lastStamp) : 3;
		var rxRate = this.lastRx == null ? 0 : Math.max(0, (rx - this.lastRx) / elapsed);
		var txRate = this.lastTx == null ? 0 : Math.max(0, (tx - this.lastTx) / elapsed);
		this.rxHistory.push(rxRate);
		this.txHistory.push(txRate);
		if (this.rxHistory.length > 20) this.rxHistory.shift();
		if (this.txHistory.length > 20) this.txHistory.shift();
		setNode(root, 'rxrate', bytes(rxRate) + '/s');
		setNode(root, 'txrate', bytes(txRate) + '/s');
		setNode(root, 'rxbytes', '累计 ' + bytes(rx));
		setNode(root, 'txbytes', '累计 ' + bytes(tx));
		updateChart(root, 'rx', this.rxHistory);
		updateChart(root, 'tx', this.txHistory);
		this.lastRx = rx;
		this.lastTx = tx;
		this.lastStamp = stamp;

		var hardwareChip = root.querySelector('[data-wifi-chip="hardware"]');
		if (hardwareChip) {
			hardwareChip.className = 'pcat-status-chip ' + (hw.present ? 'is-ok' : 'is-off');
			hardwareChip.lastChild.data = !power.enabled ? '板载芯片已断电' : (hw.present ? '板载芯片在线' : '板载芯片启动中');
		}
		var runtimeChip = root.querySelector('[data-wifi-chip="runtime"]');
		if (runtimeChip) {
			runtimeChip.className = 'pcat-status-chip ' + (running ? 'is-ok' : 'is-off');
			runtimeChip.lastChild.data = state;
		}
	},

	render: function(results) {
		var self = this, data = results[1];
		pcat.loadStyle();
		this.rxHistory = [];
		this.txHistory = [];
		this.lastRx = null;
		this.lastTx = null;
		this.lastStamp = null;
		this.root = this.makeDashboard(data);
		this.updateDashboard(data);

		var m = this.makeForm(data);
		var done = m ? m.render().then(function(node) {
			self.root.appendChild(E('div', { 'class': 'pcat-section-title pcat-wireless-settings-title' }, [
				E('span', { 'class': 'pcat-section-icon' }, '⌁'),
				E('div', {}, [ E('h3', {}, '板载无线设置'), E('p', {}, '仅保存到与 AIC8800 物理路径匹配的配置') ])
			]));
			self.root.appendChild(E('div', { 'class': 'pcat-card pcat-wireless-form' }, node));
			return self.root;
		}) : Promise.resolve((function() {
			self.root.appendChild(E('div', { 'class': 'pcat-card pcat-wireless-unavailable' }, [
				E('span', { 'class': 'pcat-wireless-alert-icon' }, '!'),
				E('div', {}, [
					E('h3', {}, data.driver && data.driver.files_present ? '板载射频尚未生成配置' : '板载无线驱动尚未安装'),
					E('p', {}, data.driver && data.driver.files_present ? '驱动文件存在，但还没有识别出 AIC 无线 PHY。设置区会在板载射频创建成功后出现。' : '已检测到 AIC USB 硬件，但当前内核没有对应的 AIC8800 驱动与固件，因此不会错误显示或修改 PCIe MT7927。')
				])
			]));
			return self.root;
		})());

		poll.add(function() {
			return pcat.call('onboard-wifi-status').then(function(fresh) {
				self.updateDashboard(fresh);
			});
		}, 3);

		return done;
	},

	handleSaveApply: function(ev, mode) {
		var self = this;
		return this.map.save().then(function() {
			return pcat.call('onboard-wifi-set', [ self.onboardPowerRequested ]);
		}).then(function(result) {
			pcat.notify(result.enabled ? '板载 Wi-Fi 已上电并启动' : '板载 Wi-Fi 已关闭并断电');
			return ui.changes.apply(mode == '0');
		});
	}
});
