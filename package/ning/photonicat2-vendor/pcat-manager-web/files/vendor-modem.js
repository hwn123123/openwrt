(function () {
  "use strict";

  const colors = ["#27dda6", "#58aaff", "#a98bff", "#f2b94b"];
  const modeLabels = { AUTO: "自动选择", NR5G: "仅使用 5G", LTE: "仅使用 4G" };
  const sensorLabels = {
    soc_max: "模组 SoC", cpu_little0: "CPU 核心 0", cpu_little1: "CPU 核心 1",
    cpu_little2: "CPU 核心 2", cpu_little3: "CPU 核心 3", gpu0: "GPU 传感器 0",
    gpu1: "GPU 传感器 1", dramc: "内存控制器", mmsys: "多媒体系统",
    md_5g: "5G 基带", md_4g: "4G 基带", md_3g: "3G 基带",
    soc_dram_ntc: "SoC / 内存 NTC", ltepa_ntc: "4G 功放 NTC",
    nrpa_ntc: "5G 功放 NTC", rf_ntc: "射频 NTC", md_rf: "基带射频",
    conn_gps: "连接 / GNSS", pmic: "电源管理", pmic_vcore: "核心电源",
    pmic_vproc: "处理器电源", pmic_vgpu: "GPU 电源"
  };

  let root = null;
  let panel = null;
  let nativeShell = null;
  let currentBasic = {};
  let currentDashboard = {};
  let currentApn = {};
  let currentStats = {};
  let currentInterface = {};
  let historyRange = "3h";
  let historyPayload = null;
  let refreshTimer = null;
  let viewTimer = null;
  let hardwareTimer = null;
  let modemOnlineRefreshTimer = null;
  let modemPresentState = null;
  let modemRefreshPending = false;
  let currentView = "overview";
  let smsMessages = [];
  let smsStorage = "SM";
  let selectedThread = "";
  let smsDraftRecipient = null;
  let settingsDirty = false;
  let selectedMode = "AUTO";
  let selectedLte = new Set();
  let selectedNr = new Set();
  let currentRadio = {};
  let radioDraftDirty = false;
  let dialFilter = "all";
  let lastDialPayload = null;
  let esimStatus = null;
  let esimProfiles = [];
  let esimJobTimer = null;

  const views = {
    overview: { label: "概览", nativeTab: "" },
    network: { label: "网络", nativeTab: "" },
    sms: { label: "短信", nativeTab: "" },
    temperature: { label: "模组温度", nativeTab: "" },
    "dial-log": { label: "拨号日志", nativeTab: "" },
    settings: { label: "网络设置", nativeTab: "" },
    automation: { label: "短信转发", nativeTab: "modem_hook" },
    esim: { label: "eSIM 管理", nativeTab: "" }
  };

  function finite(input) {
    if (input === null || input === undefined || input === "") return null;
    const parsed = Number(input);
    return Number.isFinite(parsed) ? parsed : null;
  }

  function numberFrom(input) {
    const match = String(input || "").match(/[-+]?\d+(?:\.\d+)?/);
    return match ? Number(match[0]) : null;
  }

  function value(input, fallback) {
    return input === null || input === undefined || input === "" ? (fallback || "—") : String(input);
  }

  function setText(name, content, fallback) {
    const node = panel.querySelector("[data-cell-" + name + "]");
    if (node) node.textContent = value(content, fallback);
  }

  function confirmAction(message, title) {
    return new Promise((resolve) => {
      const old = document.querySelector(".pcat-confirm-overlay");
      if (old) old.remove();
      const overlay = document.createElement("div");
      overlay.className = "pcat-confirm-overlay";
      overlay.setAttribute("role", "dialog");
      overlay.setAttribute("aria-modal", "true");
      overlay.innerHTML = '<section class="pcat-confirm-dialog"><div><h3></h3><p></p></div>' +
        '<div class="pcat-confirm-actions"><button type="button" data-confirm-cancel>取消</button>' +
        '<button type="button" class="is-confirm" data-confirm-ok>确定</button></div></section>';
      overlay.querySelector("h3").textContent = title || "请确认";
      overlay.querySelector("p").textContent = message;
      const finish = function (answer) {
        document.removeEventListener("keydown", onKeydown);
        overlay.remove();
        resolve(answer);
      };
      const onKeydown = function (event) {
        if (event.key === "Escape") finish(false);
        if (event.key === "Enter") finish(true);
      };
      overlay.querySelector("[data-confirm-cancel]").addEventListener("click", () => finish(false));
      overlay.querySelector("[data-confirm-ok]").addEventListener("click", () => finish(true));
      overlay.addEventListener("click", (event) => {
        if (event.target === overlay) finish(false);
      });
      document.addEventListener("keydown", onKeydown);
      document.body.appendChild(overlay);
      overlay.querySelector("[data-confirm-ok]").focus();
    });
  }

  function formatBytes(input) {
    let bytes = finite(input);
    if (bytes === null || bytes < 0) return "—";
    const units = ["B", "KB", "MB", "GB", "TB"];
    let index = 0;
    while (bytes >= 1024 && index < units.length - 1) {
      bytes /= 1024;
      index += 1;
    }
    const digits = index === 0 ? 0 : bytes >= 100 ? 0 : bytes >= 10 ? 1 : 2;
    return bytes.toFixed(digits) + " " + units[index];
  }

  function formatRate(input) {
    const bytes = finite(input);
    if (bytes === null) return "—";
    const bits = Math.max(0, bytes) * 8;
    if (bits >= 1000000000) return (bits / 1000000000).toFixed(2) + " Gbps";
    if (bits >= 1000000) return (bits / 1000000).toFixed(2) + " Mbps";
    if (bits >= 1000) return (bits / 1000).toFixed(0) + " Kbps";
    return bits.toFixed(0) + " bps";
  }

  function formatDuration(input) {
    let seconds = Math.max(0, Math.floor(finite(input) || 0));
    const days = Math.floor(seconds / 86400);
    seconds %= 86400;
    const hours = Math.floor(seconds / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    return (days ? days + " 天 " : "") + (hours ? hours + " 小时 " : "") + minutes + " 分钟";
  }

  async function fetchJSON(url, options) {
    const response = await fetch(url, Object.assign({ cache: "no-store" }, options || {}));
    const payload = await response.json().catch(() => ({}));
    if (!response.ok) {
      const error = new Error(payload.message || (url + " HTTP " + response.status));
      error.status = response.status;
      error.payload = payload;
      throw error;
    }
    return payload;
  }

  function postJSON(url, body) {
    return fetchJSON(url, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body)
    });
  }

  function moduleCard(title, subtitle, bodyClass, body, sections) {
    return '<article class="pcat-cell-card ' + bodyClass + '" data-cell-section="' + sections + '">' +
      '<header><div><h3>' + title + '</h3><p>' + subtitle + '</p></div></header>' + body + '</article>';
  }

  function buildPanel() {
    const section = document.createElement("section");
    section.className = "pcat-cell-center";
    section.innerHTML =
      '<nav class="pcat-cell-subnav" aria-label="蜂窝网络二级菜单">' +
        '<button type="button" class="pcat-cell-menu-toggle" data-cell-menu-toggle aria-expanded="false"><span aria-hidden="true">☰</span><small>蜂窝通信</small><strong data-cell-view-label>概览</strong></button>' +
        '<div class="pcat-cell-nav-list" data-cell-nav-list>' +
          '<button type="button" data-cell-view="overview">概览</button>' +
          '<button type="button" data-cell-view="network">网络</button>' +
          '<button type="button" data-cell-view="sms">短信</button>' +
          '<button type="button" data-cell-view="temperature">模组温度</button>' +
          '<button type="button" data-cell-view="dial-log">拨号日志</button>' +
          '<button type="button" data-cell-view="settings">网络设置</button>' +
          '<button type="button" data-cell-view="automation">短信转发</button>' +
          '<button type="button" data-cell-view="esim">eSIM 管理</button>' +
        '</div>' +
        '<button type="button" class="pcat-cell-refresh" data-cell-refresh title="刷新当前数据"><span aria-hidden="true">↻</span><span>刷新</span></button>' +
      '</nav>' +
      '<div class="pcat-cell-grid">' +
        '<article class="pcat-cell-hero" data-cell-section="overview">' +
          '<div class="pcat-cell-device-mark"><span>5G</span><i></i></div>' +
          '<div class="pcat-cell-identity"><small>CELLULAR RADIO MODULE</small><h2 data-cell-model>正在检测模组</h2><p><span data-cell-operator>等待运营商</span><b data-cell-network>—</b><b data-cell-band>—</b></p></div>' +
          '<div class="pcat-cell-state"><span data-cell-online><i></i>检测中</span><span data-cell-sim-state>SIM 检测中</span><span><b data-cell-temperature>—</b> °C</span></div>' +
        '</article>' +
        moduleCard("无线信号质量", "真实无线测量，数值随模组状态刷新", "pcat-cell-signal",
          '<div class="pcat-cell-signal-body"><div class="pcat-cell-gauge" data-cell-gauge><div><strong data-cell-signal>—</strong><span>%</span><small data-cell-signal-grade>读取中</small></div></div>' +
          '<div class="pcat-cell-metrics"><div data-cell-metric="rsrp"><span>SS-RSRP</span><strong>—</strong><small>—</small><div><i></i></div></div><div data-cell-metric="rsrq"><span>SS-RSRQ</span><strong>—</strong><small>—</small><div><i></i></div></div><div data-cell-metric="sinr"><span>SS-SINR</span><strong>—</strong><small>—</small><div><i></i></div></div></div></div>', "overview") +
        moduleCard("SIM 与数据会话", "当前用户身份、接入点与地址", "pcat-cell-session",
          '<div class="pcat-cell-detail-grid"><div><span>SIM 状态</span><strong data-cell-sim>—</strong></div><div><span>运营商代码</span><strong data-cell-mccmnc>—</strong></div><div><span>ICCID</span><strong data-cell-iccid>—</strong></div><div><span>IMSI</span><strong data-cell-imsi>—</strong></div><div><span>接入点 APN</span><strong data-cell-apn>自动</strong></div><div><span>接口地址</span><strong data-cell-local-ip>—</strong></div><div class="is-wide"><span>公网地址</span><strong data-cell-public-ip>—</strong></div></div>', "overview") +
        moduleCard("模组概览", "硬件身份、固件和当前工作方式", "pcat-cell-module-info",
          '<div class="pcat-cell-detail-grid"><div><span>模组型号</span><strong data-cell-info-model>—</strong></div><div><span>固件版本</span><strong data-cell-firmware>—</strong></div><div><span>IMEI</span><strong data-cell-imei>—</strong></div><div><span>网络偏好</span><strong data-cell-tech-pref>—</strong></div><div><span>网络选择</span><strong data-cell-roam>—</strong></div><div><span>USB 网络</span><strong data-cell-usbnet>—</strong></div><div><span>首选制式顺序</span><strong data-cell-rat-order>—</strong></div><div><span>当前载波</span><strong data-cell-carrier>—</strong></div></div>', "overview") +

        moduleCard("实时上传与下载", "页面关闭后仍由设备后台采样，刷新不会丢失", "pcat-cell-network-live",
          '<div class="pcat-network-kpis"><div class="is-down"><span>实时下载</span><strong data-cell-down-rate>—</strong></div><div class="is-up"><span>实时上传</span><strong data-cell-up-rate>—</strong></div><div><span>本次会话接收</span><strong data-cell-session-rx>—</strong></div><div><span>本次会话发送</span><strong data-cell-session-tx>—</strong></div></div>' +
          '<div class="pcat-cell-card-tools" data-cell-range><button data-range="3h" class="is-active">3 小时</button><button data-range="24h">24 小时</button><button data-range="7d">7 天</button></div>' +
          '<canvas data-cell-chart="traffic"></canvas><div class="pcat-cell-chart-key"><span class="is-green">下载</span><span class="is-blue">上传</span></div>', "network") +
        moduleCard("蜂窝流量统计", "厂家统计库累计，不依赖浏览器页面", "pcat-cell-usage",
          '<div class="pcat-usage-grid"><div><span>今日</span><strong data-cell-usage-today>—</strong></div><div><span>本周</span><strong data-cell-usage-week>—</strong></div><div><span>本月</span><strong data-cell-usage-month>—</strong></div><div><span>上月</span><strong data-cell-usage-last-month>—</strong></div></div>', "network") +
        moduleCard("数据连接详情", "从 OpenWrt 当前 WWAN 接口实时读取", "pcat-cell-interface",
          '<div class="pcat-cell-detail-grid"><div><span>连接状态</span><strong data-cell-if-state>—</strong></div><div><span>系统接口</span><strong data-cell-if-device>—</strong></div><div><span>IPv4 地址</span><strong data-cell-if-ip>—</strong></div><div><span>默认网关</span><strong data-cell-if-gateway>—</strong></div><div><span>DNS 服务器</span><strong data-cell-if-dns>—</strong></div><div><span>在线时长</span><strong data-cell-if-uptime>—</strong></div></div>', "network") +
        moduleCard("当前服务小区", "基站与无线接入参数", "pcat-cell-serving",
          '<div class="pcat-radio-scene"><div class="pcat-radio-tower"><i></i><i></i><i></i><b></b></div><span data-cell-scene-label>正在搜索网络</span></div>' +
          '<div class="pcat-serving-facts"><div><span>双工方式</span><strong data-cell-duplex>—</strong></div><div><span>频段</span><strong data-cell-serving-band>—</strong></div><div><span>PCI</span><strong data-cell-pci>—</strong></div><div><span>NR-ARFCN</span><strong data-cell-arfcn>—</strong></div><div><span>带宽</span><strong data-cell-bandwidth>—</strong></div><div><span>TAC</span><strong data-cell-tac>—</strong></div><div class="is-wide"><span>小区 ID</span><strong data-cell-cell-id>—</strong></div></div>', "network") +
        moduleCard("载波聚合", "主载波与辅载波的真实状态", "pcat-cell-ca",
          '<div class="pcat-ca-list" data-cell-ca-list><div class="pcat-cell-empty">正在读取载波信息</div></div>', "network") +
        moduleCard("射频质量历史", "RSRP、RSRQ 与 SINR 的长期变化", "pcat-cell-history",
          '<canvas data-cell-chart="signal"></canvas><div class="pcat-cell-chart-key"><span class="is-green">RSRP</span><span class="is-blue">RSRQ</span><span class="is-purple">SINR</span></div>', "network") +

        moduleCard("温度运行趋势", "模组主温度与主板温度的后台记录", "pcat-cell-thermal",
          '<div class="pcat-thermal-readings"><div><span>模组主温度</span><strong><b data-cell-modem-temp>—</b> °C</strong></div><div><span>主板温度</span><strong><b data-cell-board-temp>—</b> °C</strong></div><div><span>模组最高温度</span><strong><b data-cell-hottest-temp>—</b> °C</strong></div><div><span>传感器数量</span><strong><b data-cell-sensor-count>—</b> 个</strong></div></div><canvas data-cell-chart="temperature"></canvas><div class="pcat-cell-chart-key"><span class="is-amber">模组</span><span class="is-blue">主板</span></div>', "temperature") +
        moduleCard("模组内部温度传感器", "根据当前模组实际支持的命令读取全部可用传感器", "pcat-cell-sensors",
          '<div class="pcat-sensor-summary"><span data-cell-sensor-state>等待模组返回温度数据</span><small data-cell-sensor-updated>—</small></div><div class="pcat-sensor-grid" data-cell-sensor-grid></div>', "temperature") +

        moduleCard("短信中心", "网页同步 SIM 卡与模组存储；不会再自动删除原短信", "pcat-cell-sms",
          '<div class="pcat-sms-summary"><div><span>全部短信</span><strong data-sms-total>—</strong></div><div><span>收到</span><strong data-sms-received>—</strong></div><div><span>已发送</span><strong data-sms-sent>—</strong></div><div><span>SIM 状态</span><strong data-sms-sim>—</strong></div>' +
          '<div class="pcat-sms-toolbar"><label><span>新短信存储</span><select data-sms-storage><option value="SM">SIM 卡（默认）</option><option value="ME">模组设备</option></select></label><small>切换后只影响新收到的短信</small><button type="button" data-sms-refresh>刷新短信</button><button type="button" class="is-danger" data-sms-clear>全部删除</button></div></div>' +
          '<div class="pcat-sms-workspace"><aside><div class="pcat-sms-aside-head"><strong>会话</strong><small data-sms-sync>尚未同步</small></div><div class="pcat-sms-threads" data-sms-threads><div class="pcat-cell-empty">正在读取短信</div></div></aside>' +
          '<section><header><div><strong data-sms-title>选择一个会话</strong><small data-sms-subtitle>读取 SIM 卡与模组设备中的短信</small></div></header><div class="pcat-sms-messages" data-sms-messages><div class="pcat-cell-empty">选择左侧会话查看全部短信</div></div>' +
          '<form class="pcat-sms-compose" data-sms-form><input data-sms-number inputmode="tel" autocomplete="tel" placeholder="接收号码"><textarea data-sms-text rows="2" maxlength="670" placeholder="输入短信内容"></textarea><div><small><b data-sms-count>0</b> / 670</small><span data-sms-status></span><button type="submit">发送短信</button></div></form></section></div>', "sms") +

        moduleCard("真实拨号过程", "直接读取 pcat-manager 当前模组拨号日志环", "pcat-cell-dial-log",
          '<div class="pcat-dial-status-grid"><div><span>拨号管理</span><strong data-cell-dial-manager>检测中</strong></div><div><span>模组拨号器</span><strong data-cell-dial-helper>检测中</strong></div><div><span>WWAN 接口</span><strong data-cell-dial-interface>检测中</strong></div><div><span>IPv4 地址</span><strong data-cell-dial-address>—</strong></div><div><span>拨号程序</span><strong data-cell-dial-exec>—</strong></div><div><span>USB 设备</span><strong data-cell-dial-usb>—</strong></div><div><span>日志条数</span><strong data-cell-dial-count>—</strong></div><div><span>错误事件</span><strong data-cell-dial-errors>—</strong></div></div>' +
          '<div class="pcat-dial-log-toolbar"><span data-cell-dial-updated>等待读取 · 最新日志在上</span><div><button type="button" data-log-filter="all" class="is-active">完整过程</button><button type="button" data-log-filter="errors">仅错误</button><button type="button" data-cell-log-copy>复制日志</button><button type="button" class="is-danger" data-cell-log-clear>清空日志</button><button type="button" data-cell-log-refresh>立即刷新</button></div></div><pre data-cell-dial-log>正在读取真实拨号日志…</pre>', "dial-log") +

        moduleCard("移动网络设置", "自动识别当前配置；只有点击保存后才会写入模组", "pcat-cell-settings",
          '<div class="pcat-settings-state"><span>当前读取状态</span><strong data-settings-read-state>等待模组数据</strong><small>保存网络制式或频段会让蜂窝网络短暂断开并重新驻网。</small></div>' +
          '<div class="pcat-settings-block"><div class="pcat-settings-title"><div><strong>网络制式</strong><small>默认选中模组当前实际设置</small></div></div><div class="pcat-settings-modes"><button type="button" data-setting-mode="AUTO">自动</button><button type="button" data-setting-mode="NR5G">仅 5G</button><button type="button" data-setting-mode="LTE">仅 4G</button></div></div>' +
          '<div class="pcat-settings-block"><div class="pcat-settings-title"><div><strong>LTE 频段</strong><small data-lte-selection>—</small></div><div><button type="button" data-band-action="all" data-band-type="lte">全选</button><button type="button" data-band-action="clear" data-band-type="lte">清空</button></div></div><div class="pcat-band-grid" data-setting-band-list="lte"></div></div>' +
          '<div class="pcat-settings-block"><div class="pcat-settings-title"><div><strong>5G NR 频段</strong><small data-nr-selection>—</small></div><div><button type="button" data-band-action="all" data-band-type="nr">全选</button><button type="button" data-band-action="clear" data-band-type="nr">清空</button></div></div><div class="pcat-band-grid" data-setting-band-list="nr"></div></div>' +
          '<div class="pcat-settings-actions"><span data-settings-status>未修改</span><button type="button" data-settings-reload>重新读取</button><button type="button" class="is-primary" data-settings-save>保存网络配置</button></div>', "settings") +
        moduleCard("APN 接入点", "空 APN 表示继续由厂家拨号程序自动选择", "pcat-cell-apn",
          '<div class="pcat-apn-form"><label><span>APN</span><input data-apn-address autocomplete="off" placeholder="留空为自动"></label><label><span>用户名</span><input data-apn-user autocomplete="username" placeholder="通常留空"></label><label><span>密码</span><input data-apn-password type="password" autocomplete="current-password" placeholder="通常留空"></label><label><span>认证方式</span><select data-apn-auth><option value="">自动 / 无</option><option value="pap">PAP</option><option value="chap">CHAP</option><option value="MsChapV2">MS-CHAP v2</option></select></label></div>' +
          '<div class="pcat-imei-panel"><div><span>当前 IMEI</span><strong data-imei-current>正在读取</strong></div><label><span>模组标签上的原始 IMEI</span><input data-imei-input inputmode="numeric" autocomplete="off" maxlength="15" placeholder="15 位数字"></label><div class="pcat-imei-actions"><span data-imei-status>写入后自动回读校验，重启模组或设备后完全生效</span><button type="button" data-imei-write>写入 IMEI</button></div></div>', "settings") +
        moduleCard("高级射频控制", "扫描邻区，按频点＋PCI 锁定小区；失败后自动解锁", "pcat-cell-radio-control",
          '<div class="pcat-radio-state"><div><span>锁定状态</span><strong data-radio-lock-state>正在读取</strong><small data-radio-lock-message>—</small></div><div><span>当前运营商</span><strong data-radio-current-operator>—</strong><small data-radio-current-rat>—</small></div><button type="button" data-radio-refresh>刷新邻区</button></div>' +
          '<div class="pcat-radio-layout"><section class="pcat-radio-form"><h4>小区锁定</h4><div class="pcat-radio-fields"><label><span>制式</span><select data-radio-rat><option value="LTE">LTE</option><option value="NR5G">5G NR</option></select></label><label><span>EARFCN / NR-ARFCN</span><input data-radio-arfcn inputmode="numeric" autocomplete="off" placeholder="例如 627264"></label><label><span>PCI</span><input data-radio-pci inputmode="numeric" autocomplete="off" placeholder="例如 280"></label></div><label class="pcat-radio-check"><input type="checkbox" data-radio-reapply checked><span>模组重新上线后自动应用</span></label><p>频段限制使用上方 LTE/NR 频段设置；这里按频点和 PCI 锁定具体小区。90 秒未驻留到目标会自动解锁。</p><div class="pcat-radio-actions"><span data-radio-action-status>未操作</span><button type="button" class="is-danger" data-radio-unlock>解除锁定</button><button type="button" class="is-primary" data-radio-lock>应用锁定</button></div></section>' +
          '<section class="pcat-radio-form"><h4>运营商选择</h4><div class="pcat-radio-fields is-operator"><label><span>选择方式</span><select data-radio-operator-mode><option value="auto">自动选择</option><option value="manual">锁定 PLMN</option></select></label><label><span>PLMN（MCC+MNC）</span><input data-radio-plmn inputmode="numeric" autocomplete="off" maxlength="6" placeholder="例如 46000"></label></div><p>运营商锁定只限制 PLMN，不等同于锁定基站或小区。</p><div class="pcat-radio-actions"><span data-radio-operator-status>未操作</span><button type="button" class="is-primary" data-radio-operator-save>应用运营商设置</button></div></section></div>' +
          '<div class="pcat-radio-table-block"><div class="pcat-settings-title"><div><strong>当前与邻近小区</strong><small data-radio-cells-summary>等待扫描</small></div></div><div class="pcat-radio-table-wrap"><table class="pcat-radio-table"><thead><tr><th>状态</th><th>制式</th><th>PLMN</th><th>TAC</th><th>Cell ID</th><th>频段</th><th>频点</th><th>PCI</th><th>RSRP</th><th>RSRQ</th><th>SINR</th><th>操作</th></tr></thead><tbody data-radio-cells><tr><td colspan="12">正在读取小区信息</td></tr></tbody></table></div></div>' +
          '<div class="pcat-radio-ca"><div class="pcat-settings-title"><div><strong>载波聚合详情</strong><small>主载波、辅载波、带宽、MIMO 与调制方式</small></div></div><div data-radio-ca-list><div class="pcat-cell-empty">正在读取载波信息</div></div></div>', "settings") +

        moduleCard("eSIM 与卡槽", "FM350 内置 eUICC 状态和 SIM1 / SIM2 切换", "pcat-cell-esim-status",
          '<div class="pcat-esim-content"><div class="pcat-cell-detail-grid"><div><span>模组</span><strong data-esim-model>读取中</strong></div><div><span>当前卡槽</span><strong data-esim-slot>读取中</strong></div><div><span>SIM 类型</span><strong data-esim-type>读取中</strong></div><div><span>eUICC 管理器</span><strong data-esim-lpac>检测中</strong></div><div class="is-wide"><span>EID</span><strong data-esim-eid>切换到 SIM2 后读取</strong></div></div>' +
          '<p class="pcat-esim-notice" data-esim-message>正在查询模组支持情况</p><div class="pcat-esim-actions"><button type="button" data-esim-slot="0">使用实体卡 SIM1</button><button type="button" data-esim-slot="1">使用 eSIM SIM2</button><button type="button" data-esim-refresh>刷新 eSIM 信息</button></div><p class="pcat-esim-hint">切换卡槽或启停套餐会暂时断开蜂窝连接。下载套餐时请确保设备有其他可用的互联网连接。</p></div>', "esim") +
        moduleCard("eUICC 芯片信息", "芯片版本、存储空间与默认服务器", "pcat-cell-esim-chip",
          '<div class="pcat-esim-content"><div class="pcat-cell-detail-grid"><div><span>芯片固件</span><strong data-esim-firmware>—</strong></div><div><span>已安装应用</span><strong data-esim-installed>—</strong></div><div><span>剩余非易失存储</span><strong data-esim-memory>—</strong></div><div><span>默认 SM-DP+</span><strong data-esim-smdp>—</strong></div><div class="is-wide"><span>根 SM-DS</span><strong data-esim-smds>—</strong></div></div><div class="pcat-esim-form"><label><span>修改默认 SM-DP+ 地址</span><input data-esim-smdp-input autocomplete="off" placeholder="例如 rsp.example.com"></label><button type="button" data-esim-default-smdp>保存默认地址</button></div></div>', "esim") +
        moduleCard("eSIM 套餐", "查看、启用、停用、命名与删除已安装套餐", "pcat-cell-esim-profiles",
          '<div class="pcat-esim-content"><div class="pcat-esim-toolbar"><span data-esim-profile-count>尚未读取套餐</span><button type="button" data-esim-profiles-refresh>刷新套餐</button></div><div class="pcat-esim-list" data-esim-profiles><div class="pcat-cell-empty">切换至 SIM2 后读取套餐</div></div></div>', "esim") +
        moduleCard("下载新套餐", "粘贴运营商二维码中的 LPA:1$ 激活码", "pcat-cell-esim-download",
          '<div class="pcat-esim-content"><div class="pcat-esim-form"><label><span>激活码</span><input data-esim-activation autocomplete="off" spellcheck="false" placeholder="LPA:1$服务器$匹配码"></label><label><span>确认码（如运营商要求）</span><input data-esim-confirm-code autocomplete="off" type="password" placeholder="可选"></label><button type="button" class="is-primary" data-esim-download>下载并安装</button></div><p class="pcat-esim-hint">激活码只用于本次下载，不会保存到设备配置。安装后请在上方启用套餐。</p></div>', "esim") +
        moduleCard("发现待领取套餐", "从 SM-DS 查询运营商分配给本设备的套餐", "pcat-cell-esim-discovery",
          '<div class="pcat-esim-content"><div class="pcat-esim-form"><label><span>SM-DS 服务器（留空使用默认）</span><input data-esim-discovery-server autocomplete="off" placeholder="lpa.ds.gsma.com"></label><button type="button" data-esim-discovery>查询待领取套餐</button></div><pre class="pcat-esim-output" data-esim-discovery-output>尚未查询</pre></div>', "esim") +
        moduleCard("运营商通知", "查看、发送和移除 eUICC 待处理通知", "pcat-cell-esim-notifications",
          '<div class="pcat-esim-content"><div class="pcat-esim-toolbar"><span>套餐操作后应及时发送待处理通知</span><button type="button" data-esim-notifications-refresh>刷新通知</button></div><div class="pcat-esim-list" data-esim-notifications><div class="pcat-cell-empty">尚未读取通知</div></div></div>', "esim") +
        moduleCard("高级维护", "清空 eUICC 中的全部套餐", "pcat-cell-esim-danger",
          '<div class="pcat-esim-content"><p class="pcat-esim-hint">清空操作不可撤销，会删除所有已安装套餐；实体 SIM 卡不受影响。</p><div class="pcat-esim-form"><label><span>输入完整 EID 确认</span><input data-esim-purge-eid autocomplete="off" inputmode="numeric" placeholder="32 位 EID"></label><button type="button" class="is-danger" data-esim-purge>清空 eUICC</button></div></div>', "esim") +
      '</div>';
    return section;
  }

  function openNativeTab(name) {
    if (window.Alpine && typeof Alpine.$data === "function") {
      const state = Alpine.$data(root);
      if (state) {
        state.tab_page = name;
        state.bands_editing = false;
      }
    }
  }

  function viewFromLocation() {
    const raw = location.hash.replace(/^#/, "");
    const aliases = { sms_inbox: "sms", modem_basic: "overview", module: "overview", modem_adv: "settings", modem_hook: "automation" };
    const name = aliases[raw] || raw;
    return views[name] ? name : "overview";
  }

  function hardwareRefreshType() {
    if (currentView === "temperature") return "cellular_temperature";
    if (currentView === "settings") return "cellular_settings";
    if (currentView === "overview" || currentView === "network") return "cellular_overview";
    return "";
  }

  async function requestHardwareRefresh() {
    const refresh = hardwareRefreshType();
    if (!refresh || document.hidden) return;
    try {
      await postJSON("/api/v1/modem/basic.json", { refresh: refresh });
      window.setTimeout(() => refreshCurrent(currentView === "settings"), 1400);
    } catch (error) {}
  }

  function switchView(name, updateAddress) {
    currentView = views[name] ? name : "overview";
    panel.dataset.cellView = currentView;
    panel.classList.remove("is-menu-open");
    panel.querySelector("[data-cell-menu-toggle]").setAttribute("aria-expanded", "false");
    setText("view-label", views[currentView].label);
    let visible = 0;
    panel.querySelectorAll("[data-cell-section]").forEach((card) => {
      const show = card.dataset.cellSection.split(/\s+/).includes(currentView);
      card.hidden = !show;
      if (show) visible += 1;
    });
    panel.querySelector(".pcat-cell-grid").hidden = visible === 0;
    panel.querySelectorAll("[data-cell-view]").forEach((button) => {
      const active = button.dataset.cellView === currentView;
      button.classList.toggle("is-active", active);
      button.setAttribute("aria-current", active ? "page" : "false");
    });
    const nativeTab = views[currentView].nativeTab;
    if (nativeShell) nativeShell.hidden = !nativeTab;
    if (nativeTab) openNativeTab(nativeTab);
    if (viewTimer) {
      window.clearInterval(viewTimer);
      viewTimer = null;
    }
    if (hardwareTimer) {
      window.clearInterval(hardwareTimer);
      hardwareTimer = null;
    }
    if (currentView === "sms") {
      // Opening the inbox performs one modem synchronization. Subsequent
      // refreshes are SQLite-only; +CMTI handles new arrivals in the backend.
      loadSms(true);
      viewTimer = window.setInterval(() => loadSms(false), 3000);
    } else if (currentView === "dial-log") {
      loadDialLog(true);
      viewTimer = window.setInterval(() => loadDialLog(false), 3500);
    } else if (currentView === "network") {
      loadInterface();
      loadHistory(historyRange);
      viewTimer = window.setInterval(loadInterface, 5000);
    } else if (currentView === "temperature") {
      loadHistory(historyRange);
    } else if (currentView === "settings") {
      loadRadio(true);
      loadIdentity(true);
      viewTimer = window.setInterval(() => loadRadio(false), 4000);
    } else if (currentView === "esim") {
      loadEsimStatus(true);
    }
    const hardwareInterval = currentView === "temperature" ? 5000 :
      (currentView === "overview" || currentView === "network" ? 10000 : 0);
    if (hardwareRefreshType()) requestHardwareRefresh();
    if (hardwareInterval) hardwareTimer = window.setInterval(requestHardwareRefresh, hardwareInterval);
    window.requestAnimationFrame(drawCharts);
    if (updateAddress && location.hash !== "#" + currentView) {
      history.pushState({ cellularView: currentView }, "", "#" + currentView);
    }
  }

  function setGauge(percent) {
    const gauge = panel.querySelector("[data-cell-gauge]");
    const safe = Math.max(0, Math.min(100, finite(percent) || 0));
    if (gauge) gauge.style.setProperty("--cell-gauge", (safe * 3.6) + "deg");
  }

  function setMetric(name, rawValue, percent, unit) {
    const metric = panel.querySelector('[data-cell-metric="' + name + '"]');
    if (!metric) return;
    const numeric = finite(rawValue);
    metric.querySelector("strong").textContent = numeric === null ? "—" : numeric.toFixed(name === "rsrp" ? 0 : 1) + unit;
    metric.querySelector("i").style.width = Math.max(0, Math.min(100, finite(percent) || 0)) + "%";
    metric.querySelector("small").textContent = finite(percent) === null ? "暂无数据" : Math.round(percent) + "%";
  }

  function qualityValues(basic) {
    const serving = basic.serving || {};
    const result = { sinr: finite(serving.sinr_db), rsrq: finite(serving.rsrq_db), rsrp: finite(serving.rsrp_dbm) };
    if (result.sinr === null && result.rsrq === null && result.rsrp === null) {
      const parts = String(basic.modem_serving_quality || "").split(",");
      if (parts.length >= 3) {
        result.sinr = numberFrom(parts[0]);
        result.rsrq = numberFrom(parts[1]);
        result.rsrp = numberFrom(parts[2]);
      }
    }
    return result;
  }

  function servingValues(basic, dashboard) {
    const serving = Object.assign({}, basic.serving || {});
    const info = String(basic.modem_serving_info || "");
    const head = info.match(/^([^\s,]+)\s+(FDD|TDD)\s+([^,]+)/i);
    const patterns = { cell_id: /Cell ID\s*=\s*([^,]+)/i, tac: /TAC\s*=\s*([^,]+)/i, pci: /PCI\s*=\s*([^,]+)/i };
    if (!serving.rat && head) serving.rat = head[1];
    if (!serving.duplex && head) serving.duplex = head[2].toUpperCase();
    if (!serving.band && head) serving.band = head[3];
    Object.keys(patterns).forEach((key) => {
      const match = info.match(patterns[key]);
      if (!serving[key] && match) serving[key] = match[1].trim();
    });
    serving.rat = serving.rat || dashboard.cell_tech || dashboard.modem_mode;
    serving.band = serving.band || dashboard.cell_band;
    serving.cell_id = serving.cell_id || dashboard.cell_id;
    serving.tac = serving.tac || dashboard.cell_tac;
    return serving;
  }

  function mainTemperature(basic) {
    const decimal = finite(basic.modem_temperature_decimal);
    if (decimal !== null) return decimal;
    if (basic.modem_temperature && typeof basic.modem_temperature === "object") {
      for (const item of Object.values(basic.modem_temperature)) {
        const parsed = finite(item);
        if (parsed !== null) return parsed;
      }
    }
    return finite(basic.modem_temperature);
  }

  function renderCA(items, serving) {
    const list = panel.querySelector("[data-cell-ca-list]");
    list.replaceChildren();
    let carriers = Array.isArray(items) ? items : [];
    if (!carriers.length && serving.band) {
      carriers = [{ role: "PCC", state: "主载波", band: serving.band, pci: serving.pci, arfcn: serving.arfcn, dl_bandwidth: serving.bandwidth }];
    }
    if (!carriers.length) {
      const empty = document.createElement("div");
      empty.className = "pcat-cell-empty";
      empty.textContent = "当前未返回载波聚合信息";
      list.appendChild(empty);
      return;
    }
    carriers.forEach((carrier) => {
      const row = document.createElement("div");
      row.className = "pcat-ca-row" + (carrier.role === "PCC" ? " is-pcc" : "");
      const role = document.createElement("b");
      role.textContent = carrier.role || "载波";
      const copy = document.createElement("div");
      const band = document.createElement("strong");
      band.textContent = carrier.band || "未知频段";
      const detail = document.createElement("small");
      detail.textContent = [carrier.dl_bandwidth, carrier.pci ? "PCI " + carrier.pci : "", carrier.arfcn ? "ARFCN " + carrier.arfcn : "", carrier.dl_mimo ? carrier.dl_mimo + "×MIMO" : ""].filter(Boolean).join(" · ") || "等待详细参数";
      copy.append(band, detail);
      const state = document.createElement("span");
      state.textContent = carrier.state || "已配置";
      row.append(role, copy, state);
      list.appendChild(row);
    });
  }

  function renderSensors(sensors) {
    const grid = panel.querySelector("[data-cell-sensor-grid]");
    const entries = Object.entries(sensors || {}).map(([key, raw]) => [key, finite(raw)]).filter((item) => item[1] !== null);
    entries.sort((a, b) => b[1] - a[1]);
    grid.replaceChildren();
    setText("sensor-count", entries.length || "—");
    if (!entries.length) {
      setText("hottest-temp", "—");
      setText("sensor-state", "等待当前模组返回传感器数据");
      setText("sensor-updated", currentBasic.querying ? "模组正在查询" : "可点击右上角刷新");
      const empty = document.createElement("div");
      empty.className = "pcat-cell-empty";
      empty.textContent = "尚未读取到内部温度。本页不会用主板温度或估算值替代。";
      grid.appendChild(empty);
      return;
    }
    setText("hottest-temp", entries[0][1].toFixed(1));
    setText("sensor-state", "已读取 " + entries.length + " 个真实传感器");
    setText("sensor-updated", "实时刷新 · " + new Date().toLocaleTimeString("zh-CN", { hour12: false }));
    entries.forEach(([key, temp]) => {
      const item = document.createElement("div");
      item.className = "pcat-sensor-item" + (temp >= 75 ? " is-hot" : temp >= 60 ? " is-warm" : "");
      const name = document.createElement("span");
      name.textContent = sensorLabels[key] || key;
      const code = document.createElement("small");
      code.textContent = key;
      const reading = document.createElement("strong");
      reading.textContent = temp.toFixed(1) + " °C";
      const bar = document.createElement("i");
      bar.style.width = Math.max(2, Math.min(100, temp)) + "%";
      item.append(name, code, reading, bar);
      grid.appendChild(item);
    });
  }

  function normalizeBands(input) {
    const raw = Array.isArray(input) ? input : String(input || "").split(":");
    return raw.map((item) => parseInt(item, 10)).filter(Number.isFinite).sort((a, b) => a - b);
  }

  function renderBandList(type, policy, selected) {
    const container = panel.querySelector('[data-setting-band-list="' + type + '"]');
    const selection = type === "lte" ? selectedLte : selectedNr;
    const bands = Array.from(new Set(normalizeBands(policy).concat(normalizeBands(selected)))).sort((a, b) => a - b);
    container.replaceChildren();
    bands.forEach((band) => {
      const label = document.createElement("label");
      const input = document.createElement("input");
      input.type = "checkbox";
      input.checked = selection.has(band);
      input.value = String(band);
      input.addEventListener("change", () => {
        if (input.checked) selection.add(band);
        else selection.delete(band);
        markSettingsDirty();
        updateBandSummary();
      });
      const span = document.createElement("span");
      span.textContent = (type === "nr" ? "n" : "B") + band;
      label.append(input, span);
      container.appendChild(label);
    });
  }

  function updateBandSummary() {
    const lte = panel.querySelector("[data-lte-selection]");
    const nr = panel.querySelector("[data-nr-selection]");
    if (lte) lte.textContent = "已选择 " + selectedLte.size + " 个频段";
    if (nr) nr.textContent = "已选择 " + selectedNr.size + " 个频段";
  }

  function renderModeButtons() {
    panel.querySelectorAll("[data-setting-mode]").forEach((button) => {
      button.classList.toggle("is-active", button.dataset.settingMode === selectedMode);
    });
  }

  function markSettingsDirty() {
    settingsDirty = true;
    const status = panel.querySelector("[data-settings-status]");
    if (status) status.textContent = "有尚未保存的修改";
  }

  function renderSettings(force) {
    if (settingsDirty && !force) return;
    selectedMode = ["AUTO", "NR5G", "LTE"].includes(currentBasic.modem_tech_pref) ? currentBasic.modem_tech_pref : "AUTO";
    selectedLte = new Set(normalizeBands(currentBasic.selected_lte_bands));
    selectedNr = new Set(normalizeBands(currentBasic.selected_nr5g_bands));
    renderModeButtons();
    renderBandList("lte", currentBasic.policy_lte_bands, currentBasic.selected_lte_bands);
    renderBandList("nr", currentBasic.policy_nr5g_bands, currentBasic.selected_nr5g_bands);
    updateBandSummary();
    panel.querySelector("[data-apn-address]").value = currentApn.apn || "";
    panel.querySelector("[data-apn-user]").value = currentApn.user || "";
    panel.querySelector("[data-apn-password]").value = currentApn.password || "";
    panel.querySelector("[data-apn-auth]").value = currentApn.auth || "";
    setText("settings-read-state", currentBasic.querying ? "正在读取模组设置" : "已识别 · " + (modeLabels[selectedMode] || selectedMode));
    const status = panel.querySelector("[data-settings-status]");
    if (status) status.textContent = "当前配置已同步";
    settingsDirty = false;
  }

  function radioStatusText(lock) {
    const states = {
      off: "未锁定",
      pending: "等待驻网确认",
      active: lock && lock.pci ? "小区锁定已生效" : "频点锁定已生效",
      auto_unlocked: "保护解锁"
    };
    return states[String(lock && lock.verification || "off")] || "状态未知";
  }

  function setRadioText(name, content, fallback) {
    const node = panel.querySelector("[data-radio-" + name + "]");
    if (node) node.textContent = value(content, fallback);
  }

  function renderRadioCells(cells) {
    const body = panel.querySelector("[data-radio-cells]");
    if (!body) return;
    body.replaceChildren();
    const rows = Array.isArray(cells) ? cells : [];
    setRadioText("cells-summary", rows.length ? "读取到 " + rows.length + " 个小区，点击可填入锁定参数" : "暂未读取到小区");
    if (!rows.length) {
      const row = document.createElement("tr");
      const cell = document.createElement("td");
      cell.colSpan = 12;
      cell.textContent = currentRadio.online ? "当前没有返回邻区；解除锁定后再刷新可看到更多小区" : "模组离线";
      row.appendChild(cell);
      body.appendChild(row);
      return;
    }
    rows.forEach((item) => {
      const row = document.createElement("tr");
      if (item.serving) row.className = "is-serving";
      const values = [
        item.serving ? "当前" : "邻区",
        item.rat,
        [item.mcc, item.mnc].filter(Boolean).join(""),
        item.tac,
        item.cell_id,
        item.band || "—",
        item.arfcn,
        item.pci,
        finite(item.rsrp_dbm) === null ? "—" : Number(item.rsrp_dbm).toFixed(0) + " dBm",
        finite(item.rsrq_db) === null ? "—" : Number(item.rsrq_db).toFixed(1) + " dB",
        finite(item.sinr_db) === null ? "—" : Number(item.sinr_db).toFixed(1) + " dB"
      ];
      values.forEach((content) => {
        const cell = document.createElement("td");
        cell.textContent = value(content);
        row.appendChild(cell);
      });
      const actionCell = document.createElement("td");
      const button = document.createElement("button");
      button.type = "button";
      button.textContent = "填入";
      button.disabled = !item.arfcn || !item.pci || !["LTE", "NR5G"].includes(item.rat);
      button.addEventListener("click", () => {
        panel.querySelector("[data-radio-rat]").value = item.rat;
        panel.querySelector("[data-radio-arfcn]").value = item.arfcn || "";
        panel.querySelector("[data-radio-pci]").value = item.pci || "";
        panel.querySelector("[data-radio-action-status]").textContent = "已填入 " + (item.serving ? "当前小区" : "邻区") + "，点击应用后生效";
        radioDraftDirty = true;
      });
      actionCell.appendChild(button);
      row.appendChild(actionCell);
      body.appendChild(row);
    });
  }

  function renderRadioCA(items) {
    const list = panel.querySelector("[data-radio-ca-list]");
    if (!list) return;
    list.replaceChildren();
    const carriers = Array.isArray(items) ? items : [];
    if (!carriers.length) {
      const empty = document.createElement("div");
      empty.className = "pcat-cell-empty";
      empty.textContent = currentRadio.online ? "当前未返回载波聚合信息" : "模组离线";
      list.appendChild(empty);
      return;
    }
    carriers.forEach((carrier) => {
      const row = document.createElement("div");
      row.className = "pcat-radio-ca-row" + (carrier.role === "PCC" ? " is-pcc" : "");
      const role = document.createElement("b");
      role.textContent = carrier.role || "载波";
      const band = document.createElement("strong");
      band.textContent = carrier.band || "未知频段";
      const detail = document.createElement("small");
      detail.textContent = [
        carrier.state,
        carrier.arfcn ? "ARFCN " + carrier.arfcn : "",
        carrier.pci ? "PCI " + carrier.pci : "",
        carrier.dl_bandwidth ? "下行 " + carrier.dl_bandwidth : "",
        carrier.ul_bandwidth ? "上行 " + carrier.ul_bandwidth : "",
        carrier.dl_mimo ? "下行 " + carrier.dl_mimo + "×MIMO" : "",
        carrier.dl_modulation || ""
      ].filter(Boolean).join(" · ");
      row.append(role, band, detail);
      list.appendChild(row);
    });
  }

  function renderRadio(force) {
    const lock = currentRadio.cell_lock || {};
    const operator = currentRadio.operator || {};
    setRadioText("lock-state", radioStatusText(lock));
    setRadioText("lock-message", lock.message || "未锁定");
    const operatorName = operator.current_name || operator.current_plmn || "等待驻网";
    setRadioText("current-operator", operatorName + (operator.current_plmn && operatorName !== operator.current_plmn ? " · " + operator.current_plmn : ""));
    setRadioText("current-rat", operator.current_rat || "—");
    if (!radioDraftDirty || force) {
      panel.querySelector("[data-radio-rat]").value = lock.rat === "NR5G" ? "NR5G" : "LTE";
      panel.querySelector("[data-radio-arfcn]").value = lock.arfcn || "";
      panel.querySelector("[data-radio-pci]").value = lock.pci || "";
      panel.querySelector("[data-radio-reapply]").checked = lock.reapply !== false;
      panel.querySelector("[data-radio-operator-mode]").value = operator.mode === "manual" ? "manual" : "auto";
      panel.querySelector("[data-radio-plmn]").value = operator.plmn || "";
      radioDraftDirty = false;
    }
    const manual = panel.querySelector("[data-radio-operator-mode]").value === "manual";
    const available = currentRadio.supported !== false && currentRadio.online !== false;
    panel.querySelector("[data-radio-plmn]").disabled = !manual || !available;
    panel.querySelector("[data-radio-lock]").disabled = !available;
    panel.querySelector("[data-radio-unlock]").disabled = !available;
    panel.querySelector("[data-radio-operator-save]").disabled = !available;
    if (!available) {
      setRadioText("action-status", currentRadio.supported === false ? "当前模组不是 FM350" : "模组离线，等待重新上线");
    }
    renderRadioCells(currentRadio.cells);
    renderRadioCA(currentRadio.carrier_aggregation);
  }

  async function loadRadio(force) {
    if (currentView !== "settings") return;
    const refreshButton = panel.querySelector("[data-radio-refresh]");
    if (force && refreshButton) refreshButton.disabled = true;
    try {
      if (force) {
        try {
          await postJSON("/api/v1/modem/radio/refresh.json", {});
        } catch (refreshError) {
          setRadioText("action-status", "刷新未启动：" + refreshError.message);
        }
      }
      currentRadio = await fetchJSON("/api/v1/modem/radio.json");
      renderRadio(false);
      if (force) {
        window.setTimeout(() => loadRadio(false), 1600);
        window.setTimeout(() => loadRadio(false), 4200);
      }
    } catch (error) {
      panel.querySelector("[data-radio-action-status]").textContent = "读取失败：" + error.message;
    } finally {
      if (force && refreshButton) refreshButton.disabled = false;
    }
  }

  async function applyRadioLock() {
    const rat = panel.querySelector("[data-radio-rat]").value;
    const arfcn = panel.querySelector("[data-radio-arfcn]").value.trim();
    const pci = panel.querySelector("[data-radio-pci]").value.trim();
    const status = panel.querySelector("[data-radio-action-status]");
    if (!/^\d{1,7}$/.test(arfcn)) {
      status.textContent = "请填写有效频点";
      return;
    }
    if (!/^\d{1,4}$/.test(pci) || Number(pci) > 1007) {
      status.textContent = "锁定小区必须填写 0–1007 之间的 PCI";
      return;
    }
    const target = rat + " 频点 " + arfcn + (pci ? "、PCI " + pci : "");
    if (!(await confirmAction("确定锁定 " + target + " 吗？模组会短暂离线；90 秒不能驻网将自动解锁。", "应用射频锁定"))) return;
    const button = panel.querySelector("[data-radio-lock]");
    button.disabled = true;
    status.textContent = "正在切换模组并应用锁定…";
    try {
      const payload = await postJSON("/api/v1/modem/radio/cell-lock.json", {
        action: "lock", rat: rat, arfcn: arfcn, pci: pci,
        reapply: panel.querySelector("[data-radio-reapply]").checked
      });
      status.textContent = payload.message || "已提交锁定";
      radioDraftDirty = false;
      window.setTimeout(() => loadRadio(false), 2500);
    } catch (error) {
      status.textContent = "锁定失败：" + error.message;
    } finally {
      button.disabled = false;
    }
  }

  async function unlockRadio() {
    if (!(await confirmAction("确定解除频点和小区锁定并重新自动驻网吗？", "解除射频锁定"))) return;
    const button = panel.querySelector("[data-radio-unlock]");
    const status = panel.querySelector("[data-radio-action-status]");
    button.disabled = true;
    status.textContent = "正在解除锁定…";
    try {
      const payload = await postJSON("/api/v1/modem/radio/cell-lock.json", { action: "unlock" });
      status.textContent = payload.message || "已解除锁定";
      radioDraftDirty = false;
      window.setTimeout(() => loadRadio(false), 2500);
    } catch (error) {
      status.textContent = "解锁失败：" + error.message;
    } finally {
      button.disabled = false;
    }
  }

  async function applyOperator() {
    const mode = panel.querySelector("[data-radio-operator-mode]").value;
    const plmn = panel.querySelector("[data-radio-plmn]").value.trim();
    const status = panel.querySelector("[data-radio-operator-status]");
    if (mode === "manual" && !/^\d{5,6}$/.test(plmn)) {
      status.textContent = "PLMN 应为 5 或 6 位 MCC+MNC";
      return;
    }
    const description = mode === "manual" ? "锁定运营商 " + plmn : "恢复自动选择运营商";
    if (!(await confirmAction("确定" + description + "吗？重新注册可能需要一到两分钟。", "运营商设置"))) return;
    const button = panel.querySelector("[data-radio-operator-save]");
    button.disabled = true;
    status.textContent = "已提交，等待网络注册…";
    try {
      const payload = await postJSON("/api/v1/modem/radio/operator.json", { mode: mode, plmn: plmn });
      status.textContent = payload.message || "设置已提交";
      radioDraftDirty = false;
      window.setTimeout(() => loadRadio(false), 3500);
    } catch (error) {
      status.textContent = "设置失败：" + error.message;
    } finally {
      button.disabled = false;
    }
  }

  function validImei(imei) {
    if (!/^\d{15}$/.test(imei)) return false;
    let total = 0;
    for (let index = 0; index < imei.length; index += 1) {
      let digit = Number(imei[index]);
      if (index % 2 === 1) {
        digit *= 2;
        digit = Math.floor(digit / 10) + (digit % 10);
      }
      total += digit;
    }
    return total % 10 === 0;
  }

  function renderIdentity(identity) {
    const current = panel.querySelector("[data-imei-current]");
    const status = panel.querySelector("[data-imei-status]");
    if (current) current.textContent = identity.imei || currentBasic.imei_num || "未写入 / 未读取到";
    if (status && identity.message) status.textContent = identity.message;
  }

  async function loadIdentity(refresh) {
    try {
      const identity = await fetchJSON("/api/v1/modem/identity.json" + (refresh ? "?refresh=1" : ""));
      renderIdentity(identity);
      if (refresh && (!identity.supported || !identity.online)) {
        window.setTimeout(() => loadIdentity(false), 1200);
      }
      return identity;
    } catch (error) {
      const status = panel.querySelector("[data-imei-status]");
      if (status) status.textContent = "IMEI 状态读取失败：" + error.message;
      return null;
    }
  }

  async function writeImei() {
    const input = panel.querySelector("[data-imei-input]");
    const button = panel.querySelector("[data-imei-write]");
    const status = panel.querySelector("[data-imei-status]");
    const imei = input.value.replace(/\D/g, "");
    input.value = imei;
    if (imei.length !== 15) {
      status.textContent = "IMEI 必须是 15 位数字";
      return;
    }
    if (!validImei(imei)) {
      status.textContent = "IMEI 校验位不正确，请重新核对模组标签";
      return;
    }
    if (!(await confirmAction("确认将模组 IMEI 写入为 " + imei + " 吗？请只填写该模组标签上的原始号码。", "写入模组 IMEI"))) return;

    button.disabled = true;
    status.textContent = "正在写入并回读校验…";
    try {
      let identity = await postJSON("/api/v1/modem/identity.json", { imei: imei });
      renderIdentity(identity);
      for (let attempt = 0; attempt < 20 && ["pending", "written"].includes(identity.write_status); attempt += 1) {
        await new Promise((resolve) => window.setTimeout(resolve, 500));
        identity = await fetchJSON("/api/v1/modem/identity.json");
        renderIdentity(identity);
      }
      if (identity.write_status === "verified") {
        input.value = "";
        await refreshCurrent(false);
      } else if (["pending", "written"].includes(identity.write_status)) {
        status.textContent = "等待回读超时，请重新读取后核对";
      }
    } catch (error) {
      status.textContent = "写入失败：" + error.message;
    } finally {
      button.disabled = false;
    }
  }

  async function saveSettings() {
    if (!selectedLte.size || !selectedNr.size) {
      panel.querySelector("[data-settings-status]").textContent = "LTE 和 5G 均须至少保留一个频段";
      return;
    }
    if (!(await confirmAction("确定保存网络制式、频段和 APN 吗？蜂窝连接会短暂中断并重新拨号。", "保存网络配置"))) return;
    const button = panel.querySelector("[data-settings-save]");
    const status = panel.querySelector("[data-settings-status]");
    button.disabled = true;
    status.textContent = "正在提交厂家拨号配置…";
    const modemBody = {
      modem_cpin: currentBasic.modem_cpin || "",
      modem_tech_pref: selectedMode,
      modem_roam_pref: currentBasic.modem_roam_pref || "any",
      modem_usb_speed: currentBasic.modem_usb_speed || "",
      desired_lte_bands: Array.from(selectedLte).sort((a, b) => a - b).join(":"),
      desired_nr5g_bands: Array.from(selectedNr).sort((a, b) => a - b).join(":")
    };
    const apnBody = {
      apn_addr: panel.querySelector("[data-apn-address]").value.trim(),
      user: panel.querySelector("[data-apn-user]").value.trim(),
      password: panel.querySelector("[data-apn-password]").value,
      auth: panel.querySelector("[data-apn-auth]").value
    };
    try {
      await postJSON("/api/v2/modem_advanced.json", modemBody);
      await postJSON("/api/v1/apn_settings.json", apnBody);
      settingsDirty = false;
      status.textContent = "已提交，等待模组重新驻网";
      window.setTimeout(() => refreshCurrent(true), 6000);
    } catch (error) {
      status.textContent = "保存失败：" + error.message;
    } finally {
      button.disabled = false;
    }
  }

  function isSentSms(message) {
    const direction = String(message.direction === undefined ? "" : message.direction).toLowerCase();
    return direction === "sent" || direction === "1";
  }

  function smsParty(message) {
    return isSentSms(message)
      ? value(message.to || message.sender || message.from, "未知号码")
      : value(message.sender || message.from || message.to, "未知号码");
  }

  function smsContent(message) {
    return value(message.content || message.msg || message.message, "（空短信）");
  }

  function smsTime(message) {
    return value(message.timestamp || message.send_at || message.date, "时间未知");
  }

  function normalizedSmsRecipient(input) {
    let digits = String(input || "").replace(/\D/g, "");
    if (digits.length === 13 && digits.startsWith("86")) digits = digits.slice(2);
    return digits;
  }

  function sameSmsRecipient(left, right) {
    const leftDigits = normalizedSmsRecipient(left);
    const rightDigits = normalizedSmsRecipient(right);
    if (leftDigits && rightDigits) return leftDigits === rightDigits;
    return String(left || "").trim() === String(right || "").trim();
  }

  function createSmsThreadRow(party, messages, draft) {
    const item = document.createElement("div");
    item.className = "pcat-sms-thread" +
      ((!draft && party === selectedThread) || (draft && smsDraftRecipient !== null) ? " is-active" : "") +
      (draft ? " is-draft" : "");
    const button = document.createElement("button");
    button.type = "button";
    button.className = "pcat-sms-thread-main";
    const head = document.createElement("div");
    const name = document.createElement("strong");
    name.textContent = party || "新短信";
    const time = document.createElement("small");
    time.textContent = draft ? "新会话" : smsTime(messages[0]);
    head.append(name, time);
    const preview = document.createElement("span");
    preview.textContent = draft ? "等待发送第一条短信" : smsContent(messages[0]);
    button.append(head, preview);
    if (!draft) {
      const count = document.createElement("b");
      count.textContent = String(messages.length);
      button.appendChild(count);
    }
    button.addEventListener("click", () => {
      if (draft) {
        selectedThread = "";
        smsDraftRecipient = party;
      } else {
        selectedThread = party;
        smsDraftRecipient = null;
      }
      const number = panel.querySelector("[data-sms-number]");
      if (number) number.value = party;
      renderSms();
    });
    item.appendChild(button);
    if (!draft) {
      const remove = document.createElement("button");
      remove.type = "button";
      remove.className = "pcat-sms-thread-delete";
      remove.title = "删除这个联系人的全部短信";
      remove.setAttribute("aria-label", "删除与 " + party + " 的全部短信");
      remove.textContent = "删除";
      remove.addEventListener("click", (event) => {
        event.stopPropagation();
        deleteSmsThread(party, messages, remove);
      });
      item.appendChild(remove);
    }
    return item;
  }

  function renderSms() {
    const received = smsMessages.filter((item) => !isSentSms(item)).length;
    const sent = smsMessages.length - received;
    const setSmsText = (name, content) => {
      const node = panel.querySelector("[data-sms-" + name + "]");
      if (node) node.textContent = value(content);
    };
    setSmsText("total", smsMessages.length);
    setSmsText("received", received);
    setSmsText("sent", sent);
    setSmsText("sim", currentBasic.sim_state === "ready" ? "已就绪" : value(currentBasic.sim_state, "未知"));
    setSmsText("sync", "同步于 " + new Date().toLocaleTimeString("zh-CN", { hour12: false }));
    const storage = panel.querySelector("[data-sms-storage]");
    if (storage && !storage.disabled) storage.value = smsStorage;
    const groups = new Map();
    smsMessages.forEach((message) => {
      const party = smsParty(message);
      if (!groups.has(party)) groups.set(party, []);
      groups.get(party).push(message);
    });
    const threads = Array.from(groups.entries()).sort((a, b) => {
      return String(smsTime(b[1][0])).localeCompare(String(smsTime(a[1][0])));
    });
    const list = panel.querySelector("[data-sms-threads]");
    list.replaceChildren();
    if (selectedThread && !groups.has(selectedThread)) selectedThread = "";
    if (smsDraftRecipient) {
      const match = threads.find(([party]) => sameSmsRecipient(party, smsDraftRecipient));
      if (match) {
        selectedThread = match[0];
        smsDraftRecipient = null;
      }
    }
    if (!selectedThread && smsDraftRecipient === null && threads.length) selectedThread = threads[0][0];
    if (smsDraftRecipient !== null && smsDraftRecipient) {
      list.appendChild(createSmsThreadRow(smsDraftRecipient, [], true));
    }
    if (!threads.length && smsDraftRecipient === null) {
      const empty = document.createElement("div");
      empty.className = "pcat-cell-empty";
      empty.textContent = "当前短信数据库中没有短信";
      list.appendChild(empty);
      selectedThread = "";
    }
    threads.forEach(([party, messages]) => {
      list.appendChild(createSmsThreadRow(party, messages, false));
    });
    renderConversation(groups.get(selectedThread) || []);
  }

  function renderConversation(messages) {
    const title = panel.querySelector("[data-sms-title]");
    const number = panel.querySelector("[data-sms-number]");
    const recipient = selectedThread || smsDraftRecipient || "";
    if (title) title.textContent = recipient || "新短信";
    if (number && selectedThread && smsDraftRecipient === null && !number.matches(":focus")) number.value = selectedThread;
    const area = panel.querySelector("[data-sms-messages]");
    const previousScrollTop = area.scrollTop;
    const threadKey = selectedThread || (smsDraftRecipient !== null ? "draft:" + smsDraftRecipient : "");
    const threadChanged = area.dataset.smsThread !== threadKey;
    const nearBottom = area.scrollHeight - area.scrollTop - area.clientHeight < 48;
    area.replaceChildren();
    area.dataset.smsThread = threadKey;
    const ordered = messages.slice().sort((a, b) => String(smsTime(a)).localeCompare(String(smsTime(b))));
    if (!ordered.length) {
      const empty = document.createElement("div");
      empty.className = "pcat-cell-empty";
      empty.textContent = smsDraftRecipient !== null ? "输入内容后即可发送第一条短信" :
        (selectedThread ? "这个会话暂时没有短信" : "可直接输入号码和内容发送短信");
      area.appendChild(empty);
      return;
    }
    ordered.forEach((message) => {
      const sent = isSentSms(message);
      const bubble = document.createElement("article");
      bubble.className = "pcat-sms-bubble " + (sent ? "is-sent" : "is-received");
      const content = document.createElement("p");
      content.textContent = smsContent(message);
      const meta = document.createElement("small");
      meta.textContent = (sent ? "已发送" : "已接收") + " · " + smsTime(message) + (message.status ? " · " + message.status : "");
      const footer = document.createElement("div");
      footer.className = "pcat-sms-bubble-footer";
      const remove = document.createElement("button");
      remove.type = "button";
      remove.textContent = "删除";
      remove.addEventListener("click", () => deleteSms(message, remove));
      footer.append(meta, remove);
      bubble.append(content, footer);
      area.appendChild(bubble);
    });
    if (threadChanged || nearBottom) area.scrollTop = area.scrollHeight;
    else area.scrollTop = previousScrollTop;
  }

  async function loadSms(force) {
    if (currentView !== "sms") return;
    const button = panel.querySelector("[data-sms-refresh]");
    if (button) button.disabled = true;
    try {
      if (force) {
        const storagePayload = await fetchJSON("/api/v1/modem/sms/storage.json");
        smsStorage = storagePayload.storage === "ME" ? "ME" : "SM";
      }
      const payload = await fetchJSON("/api/v2/sms/list.json?n=200" + (force ? "&force=true" : ""));
      smsMessages = Array.isArray(payload.msg) ? payload.msg : Array.isArray(payload.messages) ? payload.messages : [];
      renderSms();
    } catch (error) {
      panel.querySelector("[data-sms-status]").textContent = "读取失败：" + error.message;
    } finally {
      if (button) button.disabled = false;
    }
  }

  async function setSmsStorage(event) {
    const select = event.currentTarget;
    const requested = select.value === "ME" ? "ME" : "SM";
    const status = panel.querySelector("[data-sms-status]");
    select.disabled = true;
    status.textContent = "正在设置新短信存储…";
    try {
      const result = await postJSON("/api/v1/modem/sms/storage.json", { storage: requested });
      smsStorage = result.storage === "ME" ? "ME" : "SM";
      select.value = smsStorage;
      status.textContent = "新短信将存储到" + (smsStorage === "SM" ? " SIM 卡" : "模组设备");
    } catch (error) {
      select.value = smsStorage;
      status.textContent = "设置失败：" + error.message;
    } finally {
      select.disabled = false;
    }
  }

  async function deleteSms(message, button) {
    if (!(await confirmAction("确定删除这条短信吗？此操作会同时删除真实存储中的原短信。", "删除短信"))) return;
    const status = panel.querySelector("[data-sms-status]");
    button.disabled = true;
    status.textContent = "正在删除短信…";
    try {
      const result = await postJSON("/api/v1/sms/delete.json", { msg_id: message.id });
      if (result.status !== "ok") throw new Error(result.message || "模组删除失败");
      smsMessages = smsMessages.filter((item) => String(item.id) !== String(message.id));
      renderSms();
      status.textContent = "短信已删除";
    } catch (error) {
      status.textContent = "删除失败：" + error.message;
      button.disabled = false;
    }
  }

  async function deleteSmsThread(party, messages, button) {
    const ids = Array.from(new Set(messages.map((message) => message.id).filter((id) => id !== null && id !== undefined)));
    if (!ids.length) return;
    if (!(await confirmAction("确定删除与 " + party + " 的全部 " + ids.length + " 条短信吗？真实存储中的原短信也会删除。", "删除整个会话"))) return;
    const status = panel.querySelector("[data-sms-status]");
    button.disabled = true;
    let deleted = 0;
    try {
      for (const id of ids) {
        status.textContent = "正在删除 " + party + " 的短信 " + (deleted + 1) + " / " + ids.length + "…";
        const result = await postJSON("/api/v1/sms/delete.json", { msg_id: id });
        if (result.status !== "ok") throw new Error(result.message || "模组删除失败");
        deleted += 1;
      }
      const idSet = new Set(ids.map(String));
      smsMessages = smsMessages.filter((message) => !idSet.has(String(message.id)));
      if (sameSmsRecipient(selectedThread, party)) selectedThread = "";
      smsDraftRecipient = null;
      renderSms();
      status.textContent = "已删除与 " + party + " 的全部短信";
    } catch (error) {
      status.textContent = "已删除 " + deleted + " / " + ids.length + " 条，随后失败：" + error.message;
      await loadSms(true);
    } finally {
      button.disabled = false;
    }
  }

  async function clearAllSms() {
    if (!(await confirmAction("确定删除全部短信吗？SIM 卡、模组设备和网页记录都会被清空，此操作不能撤销。", "全部删除短信"))) return;
    const button = panel.querySelector("[data-sms-clear]");
    const status = panel.querySelector("[data-sms-status]");
    button.disabled = true;
    status.textContent = "正在删除全部短信…";
    try {
      const result = await postJSON("/api/v2/sms/clear_all.json", {});
      if (result.status !== "ok") throw new Error(result.message || "模组删除失败");
      smsMessages = [];
      selectedThread = "";
      renderSms();
      status.textContent = "全部短信已删除";
    } catch (error) {
      status.textContent = "全部删除失败：" + error.message;
    } finally {
      button.disabled = false;
    }
  }

  async function sendSms(event) {
    event.preventDefault();
    const number = panel.querySelector("[data-sms-number]").value.trim();
    const text = panel.querySelector("[data-sms-text]").value;
    const button = panel.querySelector("[data-sms-form] button");
    const status = panel.querySelector("[data-sms-status]");
    if (!/^\+?[0-9]{5,20}$/.test(number)) {
      status.textContent = "请输入正确的接收号码";
      return;
    }
    if (!text.trim()) {
      status.textContent = "短信内容不能为空";
      return;
    }
    button.disabled = true;
    status.textContent = "正在通过模组发送…";
    try {
      const result = await postJSON("/api/v2/modem/sms/send.json", { send_to: number, msg: text });
      if (result.status !== "ok") throw new Error(result.message || "模组返回发送失败");
      panel.querySelector("[data-sms-text]").value = "";
      panel.querySelector("[data-sms-count]").textContent = "0";
      selectedThread = "";
      smsDraftRecipient = number;
      status.textContent = "发送成功";
      renderSms();
      window.setTimeout(() => loadSms(true), 1200);
    } catch (error) {
      status.textContent = "发送失败：" + error.message;
    } finally {
      button.disabled = false;
    }
  }

  function renderDialLog(resetScroll) {
    if (!lastDialPayload) return;
    const payload = lastDialPayload;
    const iface = payload.interface || {};
    const meta = payload.log || {};
    setText("dial-manager", payload.manager_pid ? "运行中 · PID " + payload.manager_pid : "未运行");
    setText("dial-helper", payload.dialer_pid ? "运行中 · PID " + payload.dialer_pid : "未运行");
    setText("dial-interface", iface.up ? "已连接 · " + value(iface.device, "WWAN") : iface.pending ? "正在连接" : "未连接");
    setText("dial-address", iface.ipv4);
    setText("dial-exec", meta.exec);
    setText("dial-usb", meta.usb_id);
    setText("dial-count", payload.count);
    setText("dial-errors", Array.isArray(payload.errors) ? payload.errors.length : 0);
    setText("dial-updated", "实时读取 · " + value(payload.generated_at, "刚刚") + " · 最新日志在上");
    const lines = dialFilter === "errors" ? payload.errors : payload.lines;
    const output = panel.querySelector("[data-cell-dial-log]");
    const previousScrollTop = output.scrollTop;
    output.textContent = Array.isArray(lines) && lines.length ? lines.slice().reverse().join("\n") : (dialFilter === "errors" ? "当前日志中没有识别到错误事件。" : "拨号日志环中暂时没有可显示记录。");
    if (resetScroll || output.dataset.rendered !== "true") output.scrollTop = 0;
    else output.scrollTop = Math.min(previousScrollTop, Math.max(0, output.scrollHeight - output.clientHeight));
    output.dataset.rendered = "true";
  }

  async function loadDialLog(resetScroll) {
    if (currentView !== "dial-log") return;
    const refresh = panel.querySelector("[data-cell-log-refresh]");
    if (refresh) refresh.disabled = true;
    try {
      lastDialPayload = await fetchJSON("/api/v1/modem/dial-log.json?limit=500");
      currentInterface = lastDialPayload.interface || currentInterface;
      renderInterface();
      renderDialLog(!!resetScroll);
    } catch (error) {
      panel.querySelector("[data-cell-dial-log]").textContent = "拨号日志读取失败：" + error.message;
      setText("dial-updated", "读取失败");
    } finally {
      if (refresh) refresh.disabled = false;
    }
  }

  async function copyDialLog() {
    const button = panel.querySelector("[data-cell-log-copy]");
    const output = panel.querySelector("[data-cell-dial-log]");
    const text = output.textContent.trim();
    if (!text) return;
    button.disabled = true;
    try {
      if (navigator.clipboard && window.isSecureContext) {
        await navigator.clipboard.writeText(text);
      } else {
        const helper = document.createElement("textarea");
        helper.value = text;
        helper.setAttribute("readonly", "");
        helper.className = "pcat-clipboard-helper";
        document.body.appendChild(helper);
        helper.select();
        const copied = document.execCommand("copy");
        helper.remove();
        if (!copied) throw new Error("浏览器拒绝访问剪贴板");
      }
      button.textContent = "已复制";
      button.classList.add("is-success");
    } catch (error) {
      button.textContent = "复制失败";
      button.classList.add("is-error");
    }
    window.setTimeout(() => {
      button.textContent = "复制日志";
      button.classList.remove("is-success", "is-error");
      button.disabled = false;
    }, 1600);
  }

  async function clearDialLog() {
    const button = panel.querySelector("[data-cell-log-clear]");
    const offset = Number(lastDialPayload && lastDialPayload.log && lastDialPayload.log.total_offset);
    if (!Number.isFinite(offset)) return;
    if (!(await confirmAction("确定清空当前拨号日志吗？只隐藏清理前的记录，不会停止或重启拨号。", "清空拨号日志"))) return;
    button.disabled = true;
    try {
      await postJSON("/api/v1/modem/dial-log/clear.json", {
        offset: offset,
        manager_pid: String(lastDialPayload.manager_pid || "")
      });
      const output = panel.querySelector("[data-cell-dial-log]");
      output.textContent = "日志已清空，正在等待新的拨号记录…";
      output.scrollTop = 0;
      output.dataset.rendered = "false";
      await loadDialLog(true);
    } catch (error) {
      window.alert("清空日志失败：" + error.message);
    } finally {
      button.disabled = false;
    }
  }

  function renderInterface() {
    if (modemPresentState !== true || modemRefreshPending) {
      setText("if-state", modemRefreshPending ? "正在读取" : "模组离线");
      ["if-device", "if-ip", "if-gateway", "if-dns", "if-uptime", "session-rx", "session-tx"].forEach((name) => setText(name, "—"));
      return;
    }
    const iface = currentInterface || {};
    setText("if-state", iface.up ? "已连接" : iface.pending ? "正在连接" : "未连接");
    setText("if-device", iface.device);
    setText("if-ip", iface.ipv4 || currentDashboard.local_wan_ip);
    setText("if-gateway", iface.gateway);
    setText("if-dns", Array.isArray(iface.dns) ? iface.dns.join(" · ") : iface.dns);
    setText("if-uptime", formatDuration(iface.uptime));
    setText("session-rx", formatBytes(iface.rx_bytes));
    setText("session-tx", formatBytes(iface.tx_bytes));
  }

  async function loadInterface() {
    if (currentView !== "network") return;
    try {
      const payload = await fetchJSON("/api/v1/modem/dial-log.json?include_log=0");
      currentInterface = payload.interface || {};
      renderInterface();
    } catch (error) {}
  }

  function clearModemCharts() {
    panel.querySelectorAll("canvas[data-cell-chart]").forEach((canvas) => {
      const context = canvas.getContext("2d");
      if (context) context.clearRect(0, 0, canvas.width, canvas.height);
    });
  }

  function clearModemDisplay(reading) {
    panel.classList.remove("is-online");
    setText("model", reading ? "正在重新读取模组" : "模组离线");
    setText("online", reading ? "正在读取" : "模组离线");
    setText("sim-state", "SIM —");
    ["operator", "network", "band", "temperature", "signal", "duplex",
      "serving-band", "pci", "arfcn", "bandwidth", "tac", "cell-id",
      "modem-temp", "sim", "mccmnc", "iccid", "imsi", "apn", "local-ip",
      "public-ip", "info-model", "firmware", "imei", "tech-pref", "roam",
      "usbnet", "rat-order", "carrier", "down-rate", "up-rate"].forEach((name) => setText(name, "—"));
    setText("signal-grade", reading ? "正在读取" : "暂无信号");
    setText("scene-label", reading ? "正在重新读取模组" : "等待模组上线");
    setGauge(null);
    setMetric("rsrp", null, null, " dBm");
    setMetric("rsrq", null, null, " dB");
    setMetric("sinr", null, null, " dB");
    renderCA([], {});
    renderSensors({});
    setText("sensor-state", reading ? "正在重新读取模组" : "模组离线");
    setText("sensor-updated", reading ? "请稍候" : "等待模组上线");
    currentInterface = {};
    renderInterface();
    historyPayload = null;
    clearModemCharts();
  }

  function refreshAfterModemOnline() {
    postJSON("/api/v1/modem/basic.json", { refresh: "cellular_overview" }).catch(function () {});
    if (modemOnlineRefreshTimer) window.clearTimeout(modemOnlineRefreshTimer);
    modemOnlineRefreshTimer = window.setTimeout(() => refreshCurrent(true), 1600);
  }

  function update(basic, dashboard, apn, stats) {
    currentBasic = basic || currentBasic;
    currentDashboard = dashboard || currentDashboard;
    currentApn = apn || currentApn;
    currentStats = stats || currentStats;
    const modemPresent = Boolean(currentBasic.modem_valid && currentBasic.wwan_powered);
    const previousModemState = modemPresentState;
    modemPresentState = modemPresent;
    const boardTemp = finite(currentDashboard.board_temperature);
    setText("board-temp", boardTemp === null ? "—" : boardTemp.toFixed(0));
    if (!modemPresent) {
      modemRefreshPending = false;
      clearModemDisplay(false);
      if (currentView === "sms") renderSms();
      return;
    }
    if (previousModemState !== true) {
      modemRefreshPending = true;
      clearModemDisplay(true);
      refreshAfterModemOnline();
      return;
    }
    if (modemRefreshPending && currentBasic.querying) {
      clearModemDisplay(true);
      return;
    }
    if (modemRefreshPending) {
      modemRefreshPending = false;
      loadHistory(historyRange);
    }
    const serving = servingValues(currentBasic, currentDashboard);
    const quality = qualityValues(currentBasic);
    const online = Boolean(currentBasic.modem_valid && currentBasic.wwan_powered && currentBasic.sim_state === "ready");
    const signal = finite(currentDashboard.cell_signal_percent_qrsrp) ?? finite(currentDashboard.modem_signal_strength);
    const modemTemp = mainTemperature(currentBasic);
    panel.classList.toggle("is-online", online);
    setText("model", currentBasic.modem_model || currentDashboard.modem_model);
    setText("operator", currentDashboard.cell_isp_native_name || currentBasic.modem_isp_details || "等待运营商");
    setText("network", currentDashboard.cell_tech || currentBasic.modem_network_info);
    setText("band", currentDashboard.cell_band || serving.band);
    setText("online", online ? "已驻网" : currentBasic.wwan_powered ? "等待驻网" : "模组已关闭");
    const onlineNode = panel.querySelector("[data-cell-online]");
    if (onlineNode && !onlineNode.querySelector("i")) onlineNode.insertAdjacentHTML("afterbegin", "<i></i>");
    setText("sim-state", currentBasic.sim_state === "ready" ? "SIM 已就绪" : "SIM " + value(currentBasic.sim_state, "未知"));
    setText("temperature", modemTemp === null ? "—" : modemTemp.toFixed(1));
    setText("signal", signal === null ? "—" : Math.round(signal));
    setText("signal-grade", signal === null ? "暂无信号" : signal >= 75 ? "信号优秀" : signal >= 55 ? "信号良好" : signal >= 35 ? "信号一般" : "信号较弱");
    setGauge(signal);
    setMetric("rsrp", quality.rsrp, currentDashboard.cell_signal_percent_qrsrp, " dBm");
    setMetric("rsrq", quality.rsrq, currentDashboard.cell_signal_percent_qrsrq, " dB");
    setMetric("sinr", quality.sinr, currentDashboard.cell_signal_percent_sinr, " dB");
    setText("duplex", serving.duplex);
    setText("serving-band", serving.band || currentDashboard.cell_band);
    setText("pci", serving.pci);
    setText("arfcn", serving.arfcn);
    setText("bandwidth", serving.bandwidth);
    setText("tac", serving.tac || currentDashboard.cell_tac);
    setText("cell-id", serving.cell_id || currentDashboard.cell_id);
    setText("scene-label", online ? [serving.rat || currentDashboard.cell_tech, serving.band || currentDashboard.cell_band].filter(Boolean).join(" · ") : "正在搜索网络");
    setText("modem-temp", modemTemp === null ? "—" : modemTemp.toFixed(1));
    renderCA(currentBasic.carrier_aggregation, serving);
    renderSensors(currentBasic.thermal_sensors);
    setText("sim", currentBasic.sim_state === "ready" ? "已就绪" : currentBasic.sim_state);
    setText("mccmnc", currentDashboard.cell_mccmnc);
    setText("iccid", currentBasic.sim_iccid || currentDashboard.sim_ccid);
    setText("imsi", currentBasic.sim_imsi || currentDashboard.sim_cimi);
    setText("apn", currentApn.apn || "自动获取");
    setText("local-ip", currentDashboard.local_wan_ip);
    setText("public-ip", currentDashboard.wan_ip);
    setText("info-model", currentBasic.modem_model);
    setText("firmware", currentBasic.firmware_version);
    setText("imei", currentBasic.imei_num || currentDashboard.imei_number);
    const imeiCurrent = panel.querySelector("[data-imei-current]");
    if (imeiCurrent) imeiCurrent.textContent = currentBasic.imei_num || currentDashboard.imei_number || "未写入 / 未读取到";
    setText("tech-pref", modeLabels[currentBasic.modem_tech_pref] || currentBasic.modem_tech_pref);
    setText("roam", currentBasic.modem_roam_pref === "any" ? "自动选择运营商" : currentBasic.modem_roam_pref);
    setText("usbnet", currentBasic.modem_usbnet_mode || "系统默认");
    setText("rat-order", Array.isArray(currentBasic.rat_preference_order) ? currentBasic.rat_preference_order.join(" → ") : currentBasic.rat_preference_order);
    setText("carrier", currentBasic.cell_carrier_info);
    setText("down-rate", formatRate(currentDashboard.down_speed));
    setText("up-rate", formatRate(currentDashboard.up_speed));
    setText("usage-today", formatBytes(currentStats.today_used));
    setText("usage-week", formatBytes(currentStats.week_used));
    setText("usage-month", formatBytes(currentStats.month_used));
    setText("usage-last-month", formatBytes(currentStats.last_month_used));
    renderInterface();
    renderSettings(false);
    if (currentView === "sms") renderSms();
  }

  async function refreshCurrent(forceSettings) {
    if (document.hidden) return;
    try {
      const items = await Promise.all([
        fetchJSON("/api/v1/modem/basic.json"),
        fetchJSON("/api/v1/dashboard.json"),
        fetchJSON("/api/v1/apn_settings.json").catch(() => ({})),
        fetchJSON("/api/v1/data_stats.json?network_type=mobile").catch(() => ({}))
      ]);
      update(items[0], items[1], items[2], items[3]);
      if (forceSettings) renderSettings(true);
    } catch (error) {}
  }

  function canvasContext(canvas) {
    const rect = canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) return null;
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    canvas.width = Math.max(1, Math.round(rect.width * ratio));
    canvas.height = Math.max(1, Math.round(rect.height * ratio));
    const ctx = canvas.getContext("2d");
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    return { ctx: ctx, width: rect.width, height: rect.height };
  }

  function draw(canvas, series, palette, fixedRange) {
    if (!canvas) return;
    const context = canvasContext(canvas);
    if (!context) return;
    const ctx = context.ctx;
    const width = context.width;
    const height = context.height;
    const pad = 8;
    ctx.clearRect(0, 0, width, height);
    ctx.strokeStyle = "rgba(224,255,244,.075)";
    ctx.lineWidth = 1;
    for (let index = 0; index < 4; index += 1) {
      const y = pad + (height - pad * 2) / 3 * index;
      ctx.beginPath();
      ctx.moveTo(pad, y);
      ctx.lineTo(width - pad, y);
      ctx.stroke();
    }
    const values = series.flat().filter((item) => item !== null && Number.isFinite(item));
    let min = fixedRange ? fixedRange[0] : (values.length ? Math.min.apply(null, values) : 0);
    let max = fixedRange ? fixedRange[1] : (values.length ? Math.max.apply(null, values) : 1);
    if (!fixedRange) min = Math.min(0, min);
    if (max === min) max = min + 1;
    series.forEach((items, seriesIndex) => {
      const points = [];
      items.forEach((item, index) => {
        if (item === null || !Number.isFinite(item)) return;
        points.push({
          x: pad + index / Math.max(1, items.length - 1) * (width - pad * 2),
          y: pad + (1 - (item - min) / (max - min)) * (height - pad * 2)
        });
      });
      if (points.length < 2) return;
      ctx.beginPath();
      ctx.moveTo(points[0].x, points[0].y);
      points.slice(1).forEach((point) => ctx.lineTo(point.x, point.y));
      ctx.strokeStyle = palette[seriesIndex];
      ctx.lineWidth = 2;
      ctx.lineJoin = "round";
      ctx.shadowColor = palette[seriesIndex];
      ctx.shadowBlur = 7;
      ctx.stroke();
      ctx.shadowBlur = 0;
    });
  }

  function drawCharts() {
    if (!historyPayload || !Array.isArray(historyPayload.points)) return;
    const points = historyPayload.points;
    draw(panel.querySelector('[data-cell-chart="signal"]'), [
      points.map((item) => finite(item.rsrp_percent)),
      points.map((item) => finite(item.rsrq_percent)),
      points.map((item) => finite(item.sinr_percent))
    ], colors, [0, 100]);
    draw(panel.querySelector('[data-cell-chart="temperature"]'), [
      points.map((item) => finite(item.modem_temperature)),
      points.map((item) => finite(item.board_temperature))
    ], [colors[3], colors[1]], [0, 90]);
    draw(panel.querySelector('[data-cell-chart="traffic"]'), [
      points.map((item) => {
        const reading = finite(item.down_speed);
        return reading === null ? null : reading * 8 / 1000000;
      }),
      points.map((item) => {
        const reading = finite(item.up_speed);
        return reading === null ? null : reading * 8 / 1000000;
      })
    ], [colors[0], colors[1]]);
  }

  async function loadHistory(rangeName) {
    if (!["3h", "24h", "7d"].includes(rangeName)) return;
    historyRange = rangeName;
    panel.querySelectorAll("[data-cell-range] button").forEach((button) => {
      button.classList.toggle("is-active", button.dataset.range === rangeName);
      button.disabled = true;
    });
    try {
      const payload = await fetchJSON("/api/v1/telemetry/history.json?range=" + encodeURIComponent(rangeName));
      if (historyRange === rangeName) {
        historyPayload = payload;
        drawCharts();
      }
    } catch (error) {
    } finally {
      panel.querySelectorAll("[data-cell-range] button").forEach((button) => { button.disabled = false; });
    }
  }

  function esimText(name, content) {
    const node = panel.querySelector("[data-esim-" + name + "]");
    if (node) node.textContent = content === null || content === undefined || content === "" ? "—" : String(content);
  }

  function esimReady() {
    return Boolean(esimStatus && esimStatus.euicc_ready && esimStatus.lpac_available);
  }

  function setEsimMessage(message, error) {
    const node = panel.querySelector("[data-esim-message]");
    node.textContent = message;
    node.classList.toggle("is-error", Boolean(error));
  }

  async function loadEsimStatus(withDetails) {
    if (document.hidden) return;
    try {
      const data = await fetchJSON("/api/v1/modem/esim/status.json");
      esimStatus = data;
      esimText("model", data.model);
      esimText("slot", data.slot === 0 ? "SIM1 · 实体卡" : data.slot === 1 ? "SIM2 · eSIM" : "未知");
      esimText("type", data.sim_type === 1 ? "eSIM" : data.sim_type === 0 ? "USIM" : "未知");
      esimText("eid", data.eid || (data.slot === 1 ? "未检测到 EID" : "切换到 SIM2 后读取"));
      esimText("lpac", data.lpac_available ? "已安装" : "未安装");
      setEsimMessage(data.message, false);
      panel.querySelectorAll("[data-esim-slot]").forEach((button) => {
        button.classList.toggle("is-active", Number(button.dataset.esimSlot) === data.slot);
        button.disabled = (Number(button.dataset.esimSlot) === data.slot) ||
          (button.dataset.esimSlot === "1" && !data.sim2_available);
      });
      panel.querySelectorAll(".pcat-cell-esim-chip button,.pcat-cell-esim-download button,.pcat-cell-esim-discovery button,.pcat-cell-esim-danger button").forEach((button) => {
        button.disabled = !esimReady();
      });
      if (withDetails && esimReady()) {
        await loadEsimRead("chip");
        await loadEsimRead("profiles");
        await loadEsimRead("notifications");
      }
      if (!esimReady()) {
        esimProfiles = [];
        renderEsimProfiles();
        renderEsimNotifications([]);
      }
    } catch (error) {
      esimStatus = null;
      setEsimMessage("读取 eSIM 状态失败：" + error.message, true);
    }
  }

  function renderEsimChip(data) {
    const info = data && data.EUICCInfo2 || {};
    const addresses = data && data.EuiccConfiguredAddresses || {};
    const resources = info.extCardResource || {};
    esimText("firmware", info.euiccFirmwareVer || info.profileVersion);
    esimText("installed", resources.installedApplication);
    esimText("memory", resources.freeNonVolatileMemory === undefined ? "—" :
      formatBytes(resources.freeNonVolatileMemory));
    esimText("smdp", addresses.defaultDpAddress);
    esimText("smds", addresses.rootDsAddress);
  }

  function makeEsimButton(label, action, value, danger) {
    const button = document.createElement("button");
    button.type = "button";
    button.textContent = label;
    button.dataset.esimProfileAction = action;
    button.dataset.esimValue = value;
    if (danger) button.classList.add("is-danger");
    return button;
  }

  function renderEsimProfiles() {
    const list = panel.querySelector("[data-esim-profiles]");
    list.replaceChildren();
    esimText("profile-count", esimReady() ? esimProfiles.length + " 个已安装套餐" : "切换到 SIM2 后管理套餐");
    if (!esimProfiles.length) {
      const empty = document.createElement("div");
      empty.className = "pcat-cell-empty";
      empty.textContent = esimReady() ? "尚无已安装套餐" : "当前卡槽无法读取 eSIM 套餐";
      list.appendChild(empty);
      return;
    }
    esimProfiles.forEach((profile) => {
      const row = document.createElement("div");
      row.className = "pcat-esim-row";
      const heading = document.createElement("div");
      const name = document.createElement("strong");
      name.textContent = profile.profileNickname || profile.profileName || profile.serviceProviderName || "未命名套餐";
      const detail = document.createElement("small");
      detail.textContent = [profile.serviceProviderName, profile.iccid, profile.profileState].filter(Boolean).join(" · ");
      heading.append(name, detail);
      const actions = document.createElement("div");
      actions.className = "pcat-esim-row-actions";
      const enabled = String(profile.profileState || "").toLowerCase() === "enabled";
      actions.appendChild(makeEsimButton(enabled ? "停用" : "启用", enabled ? "disable" : "enable", profile.iccid));
      const nickname = document.createElement("input");
      nickname.placeholder = "套餐昵称";
      nickname.maxLength = 64;
      nickname.value = profile.profileNickname || "";
      nickname.dataset.esimNickname = profile.iccid;
      actions.append(nickname, makeEsimButton("保存昵称", "nickname", profile.iccid));
      actions.appendChild(makeEsimButton("删除", "delete", profile.iccid, true));
      row.append(heading, actions);
      list.appendChild(row);
    });
  }

  function renderEsimNotifications(items) {
    const list = panel.querySelector("[data-esim-notifications]");
    list.replaceChildren();
    if (!Array.isArray(items) || !items.length) {
      const empty = document.createElement("div");
      empty.className = "pcat-cell-empty";
      empty.textContent = "没有待处理通知";
      list.appendChild(empty);
      return;
    }
    items.forEach((item) => {
      const row = document.createElement("div");
      row.className = "pcat-esim-row";
      const heading = document.createElement("div");
      const name = document.createElement("strong");
      name.textContent = "通知 #" + item.seqNumber + " · " + (item.profileManagementOperation || "套餐操作");
      const detail = document.createElement("small");
      detail.textContent = [item.iccid, item.notificationAddress].filter(Boolean).join(" · ");
      heading.append(name, detail);
      const actions = document.createElement("div");
      actions.className = "pcat-esim-row-actions";
      const send = makeEsimButton("发送", "process_notification", String(item.seqNumber));
      const remove = makeEsimButton("移除", "remove_notification", String(item.seqNumber), true);
      actions.append(send, remove);
      row.append(heading, actions);
      list.appendChild(row);
    });
  }

  async function loadEsimRead(kind) {
    if (!esimReady()) return;
    try {
      const payload = await fetchJSON("/api/v1/modem/esim/read.json?kind=" + encodeURIComponent(kind));
      if (kind === "chip") renderEsimChip(payload.data);
      if (kind === "profiles") {
        esimProfiles = Array.isArray(payload.data) ? payload.data : [];
        renderEsimProfiles();
      }
      if (kind === "notifications") renderEsimNotifications(payload.data);
    } catch (error) {
      setEsimMessage("读取" + ({ chip: "芯片", profiles: "套餐", notifications: "通知" }[kind] || "eSIM") + "失败：" + error.message, true);
    }
  }

  async function pollEsimJob(jobId) {
    try {
      const job = await fetchJSON("/api/v1/modem/esim/job.json?id=" + encodeURIComponent(jobId));
      if (job.state === "running") {
        setEsimMessage("eSIM 操作正在进行，已运行 " + Math.max(0, Math.floor(Date.now() / 1000 - job.started)) + " 秒", false);
        return true;
      }
      if (esimJobTimer) window.clearInterval(esimJobTimer);
      esimJobTimer = null;
      setEsimMessage(job.result && job.result.message || "操作已结束", job.state !== "done");
      if (job.state === "done") {
        if (job.action === "discovery") {
          panel.querySelector("[data-esim-discovery-output]").textContent = JSON.stringify(job.result.data || [], null, 2);
        }
        if (job.action === "download") {
          panel.querySelector("[data-esim-activation]").value = "";
          panel.querySelector("[data-esim-confirm-code]").value = "";
        }
        await loadEsimStatus(true);
        setEsimMessage(job.result.message || "操作已完成", false);
      }
      return false;
    } catch (error) {
      if (esimJobTimer) window.clearInterval(esimJobTimer);
      esimJobTimer = null;
      setEsimMessage("无法读取操作结果：" + error.message, true);
      return false;
    }
  }

  async function startEsimJob(action, body) {
    if (!esimReady()) {
      setEsimMessage("请先切换至 SIM2，并确认 EID 与管理器可用", true);
      return;
    }
    try {
      const result = await postJSON("/api/v1/modem/esim/action.json", Object.assign({ action: action }, body || {}));
      setEsimMessage("操作已提交，正在等待模组与 eUICC 响应", false);
      if (esimJobTimer) window.clearInterval(esimJobTimer);
      if (await pollEsimJob(result.job_id)) {
        esimJobTimer = window.setInterval(() => pollEsimJob(result.job_id), 2000);
      }
    } catch (error) {
      setEsimMessage("操作失败：" + error.message, true);
    }
  }

  async function switchEsimSlot(slot) {
    if (esimStatus && esimStatus.slot === slot) return;
    const label = slot === 1 ? "eSIM SIM2" : "实体卡 SIM1";
    if (!(await confirmAction("切换到" + label + "会暂时断开蜂窝数据连接。确定继续吗？", "切换 SIM 卡槽"))) return;
    try {
      setEsimMessage("正在切换到" + label, false);
      await postJSON("/api/v1/modem/esim/slot.json", { slot: slot });
      window.setTimeout(() => loadEsimStatus(true), 1800);
    } catch (error) {
      setEsimMessage("切换失败：" + error.message, true);
    }
  }

  function bind() {
    panel.querySelector("[data-cell-menu-toggle]").addEventListener("click", function () {
      const open = panel.classList.toggle("is-menu-open");
      this.setAttribute("aria-expanded", String(open));
    });
    panel.querySelectorAll("[data-cell-view]").forEach((button) => {
      button.addEventListener("click", () => switchView(button.dataset.cellView, true));
    });
    panel.querySelector("[data-cell-refresh]").addEventListener("click", async function () {
      const button = this;
      const label = button.querySelector("span:last-child");
      button.disabled = true;
      if (label) label.textContent = "读取中";
      if (currentView === "esim") {
        await loadEsimStatus(true);
        button.disabled = false;
        if (label) label.textContent = "刷新";
        return;
      }
      try {
        await postJSON("/api/v1/modem/basic.json", {
          refresh: hardwareRefreshType() || "cellular_overview"
        });
      } catch (error) {}
      window.setTimeout(() => refreshCurrent(currentView === "settings"), 1800);
      window.setTimeout(() => {
        button.disabled = false;
        if (label) label.textContent = "刷新";
      }, 2600);
    });
    panel.querySelectorAll("[data-cell-range] button").forEach((button) => {
      button.addEventListener("click", () => loadHistory(button.dataset.range));
    });
    panel.querySelector("[data-sms-refresh]").addEventListener("click", () => loadSms(true));
    panel.querySelector("[data-sms-storage]").addEventListener("change", setSmsStorage);
    panel.querySelector("[data-sms-clear]").addEventListener("click", clearAllSms);
    panel.querySelector("[data-sms-form]").addEventListener("submit", sendSms);
    panel.querySelector("[data-sms-text]").addEventListener("input", function () {
      panel.querySelector("[data-sms-count]").textContent = String(this.value.length);
    });
    panel.querySelector("[data-sms-number]").addEventListener("input", function () {
      const recipient = this.value.trim();
      const parties = Array.from(new Set(smsMessages.map(smsParty)));
      const match = recipient ? parties.find((party) => sameSmsRecipient(party, recipient)) : "";
      if (match) {
        selectedThread = match;
        smsDraftRecipient = null;
      } else {
        selectedThread = "";
        smsDraftRecipient = recipient;
      }
      renderSms();
    });
    panel.querySelectorAll("[data-setting-mode]").forEach((button) => {
      button.addEventListener("click", () => {
        selectedMode = button.dataset.settingMode;
        markSettingsDirty();
        renderModeButtons();
      });
    });
    panel.querySelectorAll("[data-apn-address],[data-apn-user],[data-apn-password],[data-apn-auth]").forEach((input) => {
      input.addEventListener("input", markSettingsDirty);
      input.addEventListener("change", markSettingsDirty);
    });
    panel.querySelectorAll("[data-band-action]").forEach((button) => {
      button.addEventListener("click", () => {
        const type = button.dataset.bandType;
        const selection = type === "lte" ? selectedLte : selectedNr;
        const inputs = panel.querySelectorAll('[data-setting-band-list="' + type + '"] input');
        selection.clear();
        if (button.dataset.bandAction === "all") {
          inputs.forEach((input) => selection.add(Number(input.value)));
        }
        inputs.forEach((input) => { input.checked = selection.has(Number(input.value)); });
        markSettingsDirty();
        updateBandSummary();
      });
    });
    panel.querySelector("[data-settings-save]").addEventListener("click", saveSettings);
    panel.querySelector("[data-settings-reload]").addEventListener("click", async () => {
      settingsDirty = false;
      await refreshCurrent(true);
    });
    panel.querySelector("[data-radio-refresh]").addEventListener("click", () => loadRadio(true));
    panel.querySelector("[data-radio-lock]").addEventListener("click", applyRadioLock);
    panel.querySelector("[data-radio-unlock]").addEventListener("click", unlockRadio);
    panel.querySelector("[data-radio-operator-save]").addEventListener("click", applyOperator);
    panel.querySelector("[data-imei-input]").addEventListener("input", function () {
      this.value = this.value.replace(/\D/g, "").slice(0, 15);
    });
    panel.querySelector("[data-imei-write]").addEventListener("click", writeImei);
    panel.querySelector("[data-esim-refresh]").addEventListener("click", () => loadEsimStatus(true));
    panel.querySelectorAll("[data-esim-slot]").forEach((button) => {
      button.addEventListener("click", () => switchEsimSlot(Number(button.dataset.esimSlot)));
    });
    panel.querySelector("[data-esim-profiles-refresh]").addEventListener("click", () => loadEsimRead("profiles"));
    panel.querySelector("[data-esim-notifications-refresh]").addEventListener("click", () => loadEsimRead("notifications"));
    panel.querySelector("[data-esim-default-smdp]").addEventListener("click", () => {
      startEsimJob("default_smdp", { address: panel.querySelector("[data-esim-smdp-input]").value.trim() });
    });
    panel.querySelector("[data-esim-download]").addEventListener("click", () => {
      const activation = panel.querySelector("[data-esim-activation]").value.trim();
      if (!/^LPA:1\$\S+/.test(activation)) {
        setEsimMessage("请填写运营商提供的 LPA:1$ 激活码", true);
        return;
      }
      startEsimJob("download", {
        activation_code: activation,
        confirmation_code: panel.querySelector("[data-esim-confirm-code]").value.trim()
      });
    });
    panel.querySelector("[data-esim-discovery]").addEventListener("click", () => {
      startEsimJob("discovery", { server: panel.querySelector("[data-esim-discovery-server]").value.trim() });
    });
    panel.querySelector("[data-esim-purge]").addEventListener("click", async () => {
      const eid = panel.querySelector("[data-esim-purge-eid]").value.trim();
      if (!esimStatus || eid !== esimStatus.eid) {
        setEsimMessage("请先输入页面显示的完整 EID", true);
        return;
      }
      if (!(await confirmAction("将删除 eUICC 中的所有套餐，此操作无法恢复。确定清空吗？", "清空 eUICC"))) return;
      startEsimJob("purge", { confirm_eid: eid });
    });
    panel.querySelector("[data-esim-profiles]").addEventListener("click", async (event) => {
      const button = event.target.closest("button[data-esim-profile-action]");
      if (!button) return;
      const action = button.dataset.esimProfileAction;
      const iccid = button.dataset.esimValue;
      if (action === "nickname") {
        const input = button.parentElement.querySelector("input[data-esim-nickname]");
        startEsimJob("nickname", { iccid: iccid, nickname: input.value.trim() });
        return;
      }
      if (!(await confirmAction("确定" + ({ enable: "启用", disable: "停用", delete: "删除" }[action] || action) + "套餐 " + iccid + " 吗？", "eSIM 套餐操作"))) return;
      startEsimJob(action, { iccid: iccid });
    });
    panel.querySelector("[data-esim-notifications]").addEventListener("click", async (event) => {
      const button = event.target.closest("button[data-esim-profile-action]");
      if (!button) return;
      const action = button.dataset.esimProfileAction;
      if (action === "remove_notification" && !(await confirmAction("确定移除这条待处理通知吗？", "移除运营商通知"))) return;
      startEsimJob(action, { sequence: button.dataset.esimValue });
    });
    panel.querySelectorAll("[data-radio-rat],[data-radio-arfcn],[data-radio-pci],[data-radio-reapply],[data-radio-plmn]").forEach((input) => {
      input.addEventListener("input", () => { radioDraftDirty = true; });
      input.addEventListener("change", () => { radioDraftDirty = true; });
    });
    panel.querySelector("[data-radio-operator-mode]").addEventListener("change", function () {
      radioDraftDirty = true;
      panel.querySelector("[data-radio-plmn]").disabled = this.value !== "manual";
    });
    panel.querySelector("[data-cell-log-copy]").addEventListener("click", copyDialLog);
    panel.querySelector("[data-cell-log-clear]").addEventListener("click", clearDialLog);
    panel.querySelector("[data-cell-log-refresh]").addEventListener("click", () => loadDialLog(true));
    panel.querySelectorAll("[data-log-filter]").forEach((button) => {
      button.addEventListener("click", () => {
        dialFilter = button.dataset.logFilter;
        panel.querySelectorAll("[data-log-filter]").forEach((item) => item.classList.toggle("is-active", item === button));
        renderDialLog(true);
      });
    });
    window.addEventListener("resize", drawCharts, { passive: true });
    window.addEventListener("popstate", () => switchView(viewFromLocation(), false));
    window.addEventListener("hashchange", () => switchView(viewFromLocation(), false));
    document.addEventListener("click", (event) => {
      if (!panel.contains(event.target)) {
        panel.classList.remove("is-menu-open");
        panel.querySelector("[data-cell-menu-toggle]").setAttribute("aria-expanded", "false");
      }
    });
    document.addEventListener("visibilitychange", () => {
      if (document.hidden) {
        if (refreshTimer) window.clearInterval(refreshTimer);
        if (viewTimer) window.clearInterval(viewTimer);
        if (hardwareTimer) window.clearInterval(hardwareTimer);
        refreshTimer = null;
        viewTimer = null;
        hardwareTimer = null;
        return;
      }
      refreshCurrent(true);
      switchView(currentView, false);
      refreshTimer = window.setInterval(() => refreshCurrent(false), 5000);
    });
  }

  function mount() {
    root = document.querySelector('[x-data^="adv_settings"]');
    if (!root || document.querySelector(".pcat-cell-center")) return;
    document.documentElement.lang = "zh-CN";
    document.documentElement.classList.add("pcat-black-theme");
    document.body.classList.add("pcat-modern-modem");
    const pageTitle = root.querySelector(":scope > h1") ||
      root.querySelector(":scope > div:first-child > h1");
    const pageHeading = pageTitle && pageTitle.parentElement !== root
      ? pageTitle.parentElement
      : pageTitle;
    if (pageHeading) {
      pageHeading.hidden = true;
      pageHeading.classList.add("pcat-native-heading");
    }
    panel = buildPanel();
    if (pageHeading) pageHeading.insertAdjacentElement("afterend", panel);
    else root.insertBefore(panel, root.firstChild);
    const nativePanel = root.querySelector(".grow.rounded.border.bg-white");
    if (nativePanel) {
      nativePanel.classList.add("pcat-native-panel");
      nativeShell = nativePanel.parentElement;
      if (nativeShell) nativeShell.classList.add("pcat-native-shell");
      if (nativePanel.firstElementChild) nativePanel.firstElementChild.classList.add("pcat-native-tabs");
    }
    bind();
    refreshCurrent(true);
    loadHistory(historyRange);
    switchView(viewFromLocation(), false);
    if (!location.hash) history.replaceState({ cellularView: currentView }, "", "#overview");
    refreshTimer = window.setInterval(() => refreshCurrent(false), 5000);
  }

  function start() {
    if (location.pathname !== "/modem" && location.pathname !== "/modem/") return;
    mount();
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", start);
  else start();

  window.addEventListener("pagehide", () => {
    if (refreshTimer) window.clearInterval(refreshTimer);
    if (viewTimer) window.clearInterval(viewTimer);
    if (hardwareTimer) window.clearInterval(hardwareTimer);
    if (modemOnlineRefreshTimer) window.clearTimeout(modemOnlineRefreshTimer);
    if (esimJobTimer) window.clearInterval(esimJobTimer);
  });
}());
