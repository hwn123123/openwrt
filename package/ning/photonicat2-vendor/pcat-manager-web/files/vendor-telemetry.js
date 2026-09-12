(function () {
  "use strict";

  document.documentElement.lang = "zh-CN";
  document.documentElement.classList.add("pcat-black-theme");

  const MAX_POINTS = 1500;
  const colors = {
    green: "#27dda6",
    blue: "#58aaff",
    purple: "#a98bff",
    amber: "#f2b94b",
    grid: "rgba(224,255,244,.075)"
  };

  const history = {
    signal: [[]],
    traffic: [[], []]
  };

  const charts = new Map();
  let dashboardRoot = null;
  let ui = null;
  let lastSampleAt = 0;
  let unsubscribe = null;
  let modemRuntimeTimer = null;
  let cellularRefreshTimer = null;
  let modemRefreshTimer = null;
  let modemPresentState = null;
  let modemDisplayReady = false;
  let historyRange = "3h";

  function isDashboard() {
    return (location.pathname === "/" || location.pathname === "/index" ||
      location.pathname === "/index.html") &&
      document.querySelector('[x-data="dashboard"]');
  }

  function finite(value) {
    if (value === null || value === undefined || value === "") return null;
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  }

  function text(value, fallback) {
    return value === null || value === undefined || value === "" ? (fallback || "—") : String(value);
  }

  function format(value, digits, unit) {
    return value === null || !Number.isFinite(value) ? "—" : value.toFixed(digits) + unit;
  }

  function formatRate(mbps) {
    if (mbps === null || !Number.isFinite(mbps)) return "—";
    if (mbps < 1) return (mbps * 1000).toFixed(0) + " Kbps";
    return mbps.toFixed(mbps < 10 ? 2 : 1) + " Mbps";
  }

  function formatGB(bytes) {
    const value = finite(bytes);
    if (value === null || value <= 0) return "—";
    const gb = value / 1e9;
    return gb.toFixed(gb >= 100 ? 0 : 1) + " GB";
  }

  function formatMemory(usedBytes, totalBytes) {
    const used = finite(usedBytes);
    const total = finite(totalBytes);
    if (total === null || total <= 0) return "—";
    const gib = 1073741824;
    const totalText = (total / gib).toFixed(total / gib >= 10 ? 1 : 2);
    if (used === null) return totalText + " GiB";
    return (used / gib).toFixed(2) + " / " + totalText + " GiB";
  }

  function chineseOperator(value) {
    const names = {
      "China Mobile": "中国移动",
      "China Unicom": "中国联通",
      "China Telecom": "中国电信",
      "Chunghwa Telecom": "中华电信",
      "Taiwan Mobile Telecom": "台湾大哥大",
      "Far EasTone Telecom": "远传电信"
    };
    return names[value] || value || "";
  }

  function chineseUptime(value) {
    if (!value || typeof value !== "string") return value || "—";
    return value
      .replace(/^up\s+/i, "已运行 ")
      .replace(/\bdays?\b/gi, "天")
      .replace(/\bhours?\b/gi, "小时")
      .replace(/\bminutes?\b/gi, "分钟")
      .replace(/\bseconds?\b/gi, "秒")
      .replace(/,\s*/g, " ");
  }

  function cellularGeneration(value) {
    const normalized = String(value || "").toUpperCase();
    if (normalized.includes("NR") || normalized.includes("5G")) return "5G";
    if (normalized.includes("LTE") || normalized.includes("4G")) return "4G";
    if (normalized.includes("WCDMA") || normalized.includes("3G")) return "3G";
    if (normalized.includes("GSM") || normalized.includes("2G")) return "2G";
    return "蜂窝";
  }

  function push(series, value) {
    series.push(finite(value));
    if (series.length > MAX_POINTS) series.shift();
  }

  function setText(selector, value) {
    const node = ui.root.querySelector(selector);
    if (node) node.textContent = value;
  }

  function setGauge(selector, percent, state) {
    const node = ui.root.querySelector(selector);
    if (!node) return;
    const value = Math.max(0, Math.min(100, finite(percent) || 0));
    const gauge = node.classList.contains("pcat-small-gauge") ? node.firstElementChild : node;
    if (gauge) gauge.style.setProperty("--gauge-angle", (value * 3.6) + "deg");
    node.classList.toggle("is-warning", state === "warning");
    node.classList.toggle("is-critical", state === "critical");
  }

  function moduleHeader(icon, title, subtitle, extra) {
    return '<header class="pcat-module-header">' +
      '<span class="pcat-module-icon">' + icon + '</span>' +
      '<div><h2>' + title + '</h2><p>' + subtitle + '</p></div>' +
      (extra || "") + '</header>';
  }

  function buildDashboard() {
    const root = document.createElement("div");
    root.className = "pcat-module-grid";
    root.innerHTML =
      '<article class="pcat-module pcat-module-device">' +
        moduleHeader("硬", "设备硬件", "处理器、内存与主板实时状态", '<span class="pcat-module-badge" data-device-health>实时监测</span>') +
        '<div class="pcat-device-body">' +
          '<div class="pcat-live-metrics" aria-label="设备实时状态">' +
            '<div class="pcat-live-metric is-cpu" data-system-metric="cpu"><span>CPU 占用</span><strong><b>—</b><small>%</small></strong><div><i></i></div></div>' +
            '<div class="pcat-live-metric is-memory" data-system-metric="memory"><span>内存占用</span><strong><b>—</b><small>%</small></strong><div><i></i></div></div>' +
            '<div class="pcat-live-metric is-temperature" data-system-metric="temperature"><span>主板温度</span><strong><b>—</b><small>°C</small></strong><div><i></i></div></div>' +
          '</div>' +
          '<div class="pcat-device-facts">' +
            '<div><span>设备型号</span><strong data-model>—</strong></div>' +
            '<div><span>处理器</span><strong data-cpu-model>—</strong></div>' +
            '<div><span>CPU 核心</span><strong data-cpu-cores>—</strong></div>' +
            '<div><span>系统负载</span><strong data-load-average>—</strong></div>' +
            '<div><span>物理内存</span><strong data-memory-size>—</strong></div>' +
            '<div><span>内核版本</span><strong data-kernel>—</strong></div>' +
            '<div><span>系统版本</span><strong data-openwrt>—</strong></div>' +
            '<div><span>运行时间</span><strong data-uptime>—</strong></div>' +
          '</div>' +
        '</div>' +
      '</article>' +

      '<article class="pcat-module pcat-module-cellular">' +
        moduleHeader('<span data-cell-icon-tech>蜂窝</span>', "蜂窝网络", "模组驻网与无线信号", '<span class="pcat-status-chip" data-cell-status>检测中</span>') +
        '<div class="pcat-cellular-body">' +
          '<div class="pcat-main-gauge" data-gauge="signal"><div><strong data-signal-value>—</strong><span>%</span><small data-signal-grade>读取中</small></div></div>' +
          '<div class="pcat-cellular-copy"><strong data-cell-tech>—</strong><span data-cell-operator>等待网络注册</span><small data-cell-band>频段信息获取中</small></div>' +
          '<div class="pcat-cellular-metrics">' +
            '<div class="pcat-modem-runtime" aria-label="5G 模组实时状态">' +
              '<div class="is-cpu" data-modem-metric="cpu"><span>模组 CPU</span><strong><b>—</b><small>%</small></strong><div><i></i></div></div>' +
              '<div class="is-memory" data-modem-metric="memory"><span>模组内存</span><strong><b>—</b><small>%</small></strong><div><i></i></div></div>' +
              '<div class="is-temperature" data-modem-metric="temperature"><span>模组温度</span><strong><b>—</b><small>°C</small></strong><div><i></i></div></div>' +
            '</div>' +
            '<div class="pcat-radio-metrics">' +
              '<div><span>RSRP 信号</span><strong data-rsrp>—</strong></div>' +
              '<div><span>RSRQ 质量</span><strong data-rsrq>—</strong></div>' +
              '<div><span>SINR 信噪比</span><strong data-sinr>—</strong></div>' +
            '</div>' +
          '</div>' +
        '</div>' +
        '<div class="pcat-chart-heading"><span>无线信号历史</span><small data-signal-history-range>最近 3 小时 · 后台记录</small></div>' +
        '<canvas class="pcat-chart pcat-chart-signal" data-chart="signal" aria-label="5G 信号实时曲线"></canvas>' +
      '</article>' +

      '<article class="pcat-module pcat-module-battery">' +
        moduleHeader("电", "电池与供电", "电量、充放电与续航状态", '<span class="pcat-module-badge" data-power-state>读取中</span>') +
        '<div class="pcat-gauge-row">' +
          '<div class="pcat-small-gauge is-green" data-gauge="battery"><div><strong><b data-battery>—</b><span>%</span></strong></div><small>电池电量</small></div>' +
          '<div class="pcat-small-gauge is-purple" data-gauge="power"><div><strong><b data-power>—</b><span>W</span></strong></div><small>当前功耗</small></div>' +
          '<div class="pcat-small-gauge is-blue" data-gauge="current"><div><strong><b data-current-gauge>—</b><span>A</span></strong></div><small data-current-label>电池电流</small></div>' +
        '</div>' +
        '<div class="pcat-battery-facts">' +
          '<div><span>电池电压</span><strong data-battery-voltage>—</strong></div>' +
          '<div><span>充电输入</span><strong data-charge-voltage>—</strong></div>' +
          '<div><span>供电状态</span><strong data-supply-mode>—</strong></div>' +
          '<div><span>预计剩余时间</span><strong data-remaining>—</strong></div>' +
        '</div>' +
      '</article>' +

      '<article class="pcat-module pcat-module-ethernet">' +
        moduleHeader("网", "以太网", "WAN 与 LAN 物理链路", '<span class="pcat-module-badge" data-ethernet-egress>非活动出口</span>') +
        '<div class="pcat-interface-list">' +
          '<div class="pcat-interface" data-interface="wan"><span class="pcat-interface-dot"></span><div><strong>WAN 端口</strong><small data-wan-state>检测中</small></div><b data-wan-speed>—</b></div>' +
          '<div class="pcat-interface" data-interface="lan"><span class="pcat-interface-dot"></span><div><strong>LAN 端口</strong><small data-lan-state>检测中</small></div><b data-lan-speed>—</b></div>' +
        '</div>' +
        '<div class="pcat-address-list"><div><span>公网地址</span><strong data-public-ip>—</strong></div><div><span>接口地址</span><strong data-local-ip>—</strong></div></div>' +
      '</article>' +

      '<article class="pcat-module pcat-module-wifi">' +
        moduleHeader("Wi", "Wi‑Fi 无线网络", "本机无线硬件的真实运行状态", '<div class="pcat-client-count"><strong data-wifi-clients>0</strong><span>台终端</span></div>') +
        '<div class="pcat-wifi-list" data-wifi-list></div>' +
      '</article>' +

      '<article class="pcat-module pcat-module-traffic">' +
        moduleHeader("⇅", "实时网络吞吐", "后台持续记录，刷新页面不会从头开始", '<div class="pcat-range-switch" data-dashboard-range><button data-range="3h" class="is-active">3 小时</button><button data-range="24h">24 小时</button><button data-range="7d">7 天</button></div>') +
        '<div class="pcat-traffic-values">' +
          '<div class="is-down"><span>实时下载</span><strong data-down-rate>—</strong></div>' +
          '<div class="is-up"><span>实时上传</span><strong data-up-rate>—</strong></div>' +
          '<div class="is-route"><span>当前出口</span><strong data-active-route>—</strong></div>' +
        '</div>' +
        '<canvas class="pcat-chart pcat-chart-traffic" data-chart="traffic" aria-label="网络吞吐实时曲线"></canvas>' +
        '<div class="pcat-chart-legend"><span class="is-green">下载速率</span><span class="is-blue">上传速率</span></div>' +
      '</article>' +

      '<article class="pcat-module pcat-module-storage">' +
        moduleHeader("存", "设备存储", "内置 eMMC、SD 卡与 NVMe 真实检测", '') +
        '<div class="pcat-storage-list">' +
          '<div class="pcat-storage" data-storage="internal"><div><span>内置 eMMC</span><b data-internal-state>检测中</b></div><strong data-internal-detail>—</strong><div class="pcat-storage-bar"><i data-internal-bar></i></div></div>' +
          '<div class="pcat-storage" data-storage="sd"><div><span>SD 卡</span><b data-sd-state>未检测到</b></div><strong data-sd-detail>—</strong><div class="pcat-storage-bar"><i data-sd-bar></i></div></div>' +
          '<div class="pcat-storage" data-storage="nvme"><div><span>NVMe 固态硬盘</span><b data-nvme-state>未检测到</b></div><strong data-nvme-detail>—</strong><div class="pcat-storage-bar"><i data-nvme-bar></i></div></div>' +
        '</div>' +
      '</article>';
    return root;
  }

  function prepareTitle() {
    const container = dashboardRoot.parentElement;
    const header = container && container.querySelector(":scope > .flex.items-center.justify-between");
    if (!header) return;
    header.className = "pcat-compact-titlebar";
    header.innerHTML = '<h1>仪表盘</h1><span><i></i>设备实时数据</span>';
  }

  function mount() {
    dashboardRoot = document.querySelector('[x-data="dashboard"]');
    if (!dashboardRoot || document.querySelector(".pcat-module-grid")) return;
    document.body.classList.add("pcat-modern-dashboard");
    dashboardRoot.classList.add("pcat-dashboard-root");
    Array.from(dashboardRoot.children).forEach((card) => card.classList.add("pcat-native-source"));
    prepareTitle();
    const modules = buildDashboard();
    dashboardRoot.insertBefore(modules, dashboardRoot.firstChild);
    ui = { root: modules };

    modules.querySelectorAll("canvas[data-chart]").forEach((canvas) => {
      charts.set(canvas.dataset.chart, canvas);
      if (window.ResizeObserver) new ResizeObserver(drawAll).observe(canvas);
    });
    modules.querySelectorAll("[data-dashboard-range] button").forEach((button) => {
      button.addEventListener("click", function () {
        loadHistory(button.dataset.range);
      });
    });
    window.addEventListener("resize", drawAll, { passive: true });
  }

  function canvasContext(canvas) {
    const rect = canvas.getBoundingClientRect();
    const ratio = Math.min(window.devicePixelRatio || 1, 2);
    const width = Math.max(1, Math.round(rect.width * ratio));
    const height = Math.max(1, Math.round(rect.height * ratio));
    if (canvas.width !== width || canvas.height !== height) {
      canvas.width = width;
      canvas.height = height;
    }
    const ctx = canvas.getContext("2d");
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    return { ctx, width: rect.width, height: rect.height };
  }

  function rangeFor(seriesList, fixedRange) {
    if (fixedRange) return fixedRange;
    const values = seriesList.flat().filter((value) => value !== null && Number.isFinite(value));
    if (!values.length) return [0, 1];
    let min = Math.min.apply(null, values);
    let max = Math.max.apply(null, values);
    min = Math.min(0, min);
    if (min === max) max = min + 1;
    else max += (max - min) * .18;
    return [min, max];
  }

  function drawChart(id, seriesList, seriesColors, fixedRange) {
    const canvas = charts.get(id);
    if (!canvas) return;
    const { ctx, width, height } = canvasContext(canvas);
    ctx.clearRect(0, 0, width, height);
    const pad = { left: 5, right: 6, top: 9, bottom: 9 };
    const plotW = Math.max(1, width - pad.left - pad.right);
    const plotH = Math.max(1, height - pad.top - pad.bottom);
    ctx.strokeStyle = colors.grid;
    ctx.lineWidth = 1;
    for (let i = 0; i < 4; i += 1) {
      const y = pad.top + plotH / 3 * i;
      ctx.beginPath();
      ctx.moveTo(pad.left, y);
      ctx.lineTo(width - pad.right, y);
      ctx.stroke();
    }
    const range = rangeFor(seriesList, fixedRange);
    seriesList.forEach((series, seriesIndex) => {
      const points = [];
      series.forEach((value, index) => {
        if (value === null) return;
        points.push({
          x: pad.left + (index / Math.max(1, series.length - 1)) * plotW,
          y: pad.top + (1 - (value - range[0]) / (range[1] - range[0])) * plotH
        });
      });
      if (points.length < 2) return;
      const color = seriesColors[seriesIndex];
      ctx.beginPath();
      ctx.moveTo(points[0].x, points[0].y);
      points.slice(1).forEach((point) => ctx.lineTo(point.x, point.y));
      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.lineJoin = "round";
      ctx.lineCap = "round";
      ctx.shadowColor = color;
      ctx.shadowBlur = 6;
      ctx.stroke();
      ctx.shadowBlur = 0;
      if (seriesIndex === 0) {
        const gradient = ctx.createLinearGradient(0, pad.top, 0, height);
        gradient.addColorStop(0, color + "30");
        gradient.addColorStop(1, color + "00");
        ctx.lineTo(points[points.length - 1].x, height - pad.bottom);
        ctx.lineTo(points[0].x, height - pad.bottom);
        ctx.closePath();
        ctx.fillStyle = gradient;
        ctx.fill();
      }
      const last = points[points.length - 1];
      ctx.beginPath();
      ctx.arc(last.x, last.y, 2.7, 0, Math.PI * 2);
      ctx.fillStyle = color;
      ctx.fill();
    });
  }

  function drawAll() {
    drawChart("signal", history.signal, [colors.green], [0, 100]);
    drawChart("traffic", history.traffic, [colors.green, colors.blue]);
  }

  function loadHistory(rangeName) {
    if (!["3h", "24h", "7d"].includes(rangeName)) return Promise.resolve();
    historyRange = rangeName;
    const rangeLabels = { "3h": "最近 3 小时 · 后台记录", "24h": "最近 24 小时 · 后台记录", "7d": "最近 7 天 · 后台记录" };
    setText("[data-signal-history-range]", rangeLabels[rangeName]);
    ui.root.querySelectorAll("[data-dashboard-range] button").forEach((button) => {
      button.classList.toggle("is-active", button.dataset.range === rangeName);
      button.disabled = true;
    });
    return fetch("/api/v1/telemetry/history.json?range=" + encodeURIComponent(rangeName), {
      cache: "no-store"
    })
      .then((response) => {
        if (!response.ok) throw new Error("history unavailable");
        return response.json();
      })
      .then((payload) => {
        if (historyRange !== rangeName || !Array.isArray(payload.points) || !payload.points.length) return;
        history.signal[0] = payload.points.map((point) => finite(point.rsrp_percent));
        history.traffic[0] = payload.points.map((point) => {
          const value = finite(point.down_speed);
          return value === null ? null : value * 8 / 1048576;
        });
        history.traffic[1] = payload.points.map((point) => {
          const value = finite(point.up_speed);
          return value === null ? null : value * 8 / 1048576;
        });
        drawAll();
      })
      .catch(function () {
        // Keep the live in-memory samples when history has not started yet.
      })
      .finally(() => {
        if (!ui) return;
        ui.root.querySelectorAll("[data-dashboard-range] button").forEach((button) => {
          button.disabled = false;
        });
      });
  }

  function setInterface(kind, enabled, carrier, speed, active) {
    const row = ui.root.querySelector('[data-interface="' + kind + '"]');
    if (!row) return;
    row.classList.toggle("is-up", Boolean(enabled && carrier));
    row.classList.toggle("is-active", Boolean(active));
    setText("[data-" + kind + "-state]", !enabled ? "接口已关闭" : carrier ? (active ? "已连接 · 当前出口" : "已连接") : "未连接");
    setText("[data-" + kind + "-speed]", carrier ? text(speed) : "—");
  }

  function setSystemMetric(name, value, maximum) {
    const card = ui.root.querySelector('[data-system-metric="' + name + '"]');
    if (!card) return;
    const reading = finite(value);
    const level = reading === null ? 0 : Math.max(0, Math.min(100, reading / maximum * 100));
    const number = card.querySelector("strong b");
    if (number) number.textContent = reading === null ? "—" : reading.toFixed(1);
    card.style.setProperty("--system-level", level + "%");
    card.classList.toggle("is-warning", reading !== null && (
      name === "temperature" ? reading >= 65 : reading >= 80
    ));
    card.classList.toggle("is-critical", reading !== null && (
      name === "temperature" ? reading >= 78 : reading >= 92
    ));
  }

  function renderWifi(interfaces) {
    const box = ui.root.querySelector("[data-wifi-list]");
    box.replaceChildren();
    const radios = Array.isArray(interfaces) ? interfaces : [];
    if (!radios.length) {
      const empty = document.createElement("div");
      empty.className = "pcat-empty-state";
      empty.textContent = "未检测到无线接口";
      box.appendChild(empty);
      return;
    }
    radios.forEach((radio) => {
      const hardware = radio.hardware || {};
      const current = radio.current || radio;
      const runtime = radio.runtime || {};
      const running = runtime.up === undefined ? Boolean(radio.enabled) : Boolean(runtime.up);
      const row = document.createElement("div");
      row.className = "pcat-wifi-row" + (running ? " is-up" : "") + (hardware.kind === "pcie" ? " is-pcie" : "");
      const dot = document.createElement("span");
      dot.className = "pcat-interface-dot";
      const copy = document.createElement("div");
      const title = document.createElement("strong");
      const detail = document.createElement("small");
      const radioName = hardware.model || radio.device_type || radio.device || "无线模块";
      title.textContent = radioName + (hardware.kind === "pcie" ? " PCIe" : hardware.kind === "onboard" ? " 板载" : "");
      const standards = Array.isArray(radio.standards) ? radio.standards.slice(-1)[0] : "";
      detail.textContent = [current.ssid, current.band && current.band.toUpperCase(), standards, current.htmode, hardware.driver].filter(Boolean).join(" · ") || "无线接口";
      copy.append(title, detail);
      const status = document.createElement("b");
      status.textContent = !radio.enabled ? "已关闭" : runtime.pending ? "启动中" : running ? "在线" : "已启用 · 未运行";
      row.append(dot, copy, status);
      box.appendChild(row);
    });
  }

  function renderStorage(prefix, info) {
    const present = Boolean(info && info.present);
    const size = finite(info && info.size);
    const free = finite(info && info.free);
    const used = size !== null && free !== null ? Math.max(0, size - free) : null;
    const percent = size && used !== null ? used / size * 100 : 0;
    setText("[data-" + prefix + "-state]", present ? (prefix === "internal" ? "系统内置" : "已连接") : "未检测到");
    setText("[data-" + prefix + "-detail]", present
      ? [text(info.model, "存储设备"), used === null ? formatGB(size) : formatGB(used) + " / " + formatGB(size)].join(" · ")
      : "—");
    const bar = ui.root.querySelector("[data-" + prefix + "-bar]");
    if (bar) bar.style.width = Math.max(0, Math.min(100, percent)) + "%";
    const card = ui.root.querySelector('[data-storage="' + prefix + '"]');
    if (card) card.classList.toggle("is-present", present);
  }

  function update(data) {
    if (!data || !ui) return;
    const now = Date.now();
    if (now - lastSampleAt < 2500) return;
    lastSampleAt = now;

    const downRaw = finite(data.down_speed);
    const upRaw = finite(data.up_speed);
    const downMbps = downRaw === null ? null : downRaw * 8 / 1048576;
    const upMbps = upRaw === null ? null : upRaw * 8 / 1048576;
    const modemPresent = Boolean(data.modem_valid && data.wwan_powered);
    const previousModemState = modemPresentState;
    modemPresentState = modemPresent;
    if (!modemPresent) modemDisplayReady = false;
    if (modemPresent && previousModemState !== true) {
      modemDisplayReady = false;
      refreshModemAfterOnline();
    }
    const showModem = modemPresent && modemDisplayReady;
    const signal = showModem ? (finite(data.cell_signal_percent_qrsrp) ?? finite(data.modem_signal_strength)) : null;
    const rsrp = showModem ? finite(data.cell_signal_percent_qrsrp) : null;
    const rsrq = showModem ? finite(data.cell_signal_percent_qrsrq) : null;
    const sinr = showModem ? finite(data.cell_signal_percent_sinr) : null;
    const battery = finite(data.charge_percent) ?? finite(data.battery_soc);
    const voltage = finite(data.battery_voltage_v);
    const wattage = finite(data.battery_wattage_w);
    const current = finite(data.battery_current_a);
    const temperature = finite(data.board_temperature);
    const chargeVoltage = finite(data.charge_voltage);
    const operator = showModem ? chineseOperator(data.cell_isp_native_name || data.enhanced_isp_full_cn || data.isp_name) : "";
    const online = showModem && data.sim_interface_enabled && signal !== null;
    const routeNames = { mobile: "5G 蜂窝网络", wan: "WAN 以太网", wired: "有线网络", wifi: "Wi‑Fi 中继", lan: "LAN 局域网" };

    if (showModem) push(history.signal[0], signal);
    else history.signal[0] = [];
    push(history.traffic[0], downMbps);
    push(history.traffic[1], upMbps);

    const cellular = ui.root.querySelector(".pcat-module-cellular");
    cellular.classList.toggle("is-online", online);
    setText("[data-cell-status]", !modemPresent ? "模组离线" : !modemDisplayReady ? "正在读取" : online ? "已驻网" : "未驻网");
    setText("[data-signal-value]", signal === null ? "—" : signal.toFixed(0));
    setText("[data-signal-grade]", signal === null ? "暂无信号" : signal >= 80 ? "信号优秀" : signal >= 60 ? "信号良好" : signal >= 35 ? "信号一般" : "信号较弱");
    const cellGeneration = showModem ? cellularGeneration(data.cell_tech || data.modem_mode) : "—";
    setText("[data-cell-icon-tech]", showModem ? cellGeneration : "蜂窝");
    setText("[data-cell-tech]", cellGeneration);
    setText("[data-cell-operator]", showModem ? text(operator, "等待网络注册") : modemPresent ? "正在读取模组" : "等待模组上线");
    setText("[data-cell-band]", showModem ? [text(data.modem_model, "蜂窝模组"), data.cell_band ? "当前频段 " + data.cell_band : "频段信息获取中"].join(" · ") : "—");
    setText("[data-rsrp]", format(rsrp, 0, "%"));
    setText("[data-rsrq]", format(rsrq, 0, "%"));
    setText("[data-sinr]", format(sinr, 0, "%"));
    setGauge('[data-gauge="signal"]', signal);
    setSystemMetric("cpu", data.cpu_usage, 100);
    setSystemMetric("memory", data.memory_usage, 100);
    setSystemMetric("temperature", temperature, 90);
    const deviceHealth = temperature !== null && temperature >= 78 ? "温度过高" :
      temperature !== null && temperature >= 65 ? "温度偏高" : "运行正常";
    setText("[data-device-health]", deviceHealth);
    setText("[data-cpu-model]", text(data.cpu_model));
    setText("[data-cpu-cores]", data.cpu_cores ? data.cpu_cores + " 核" : "—");
    setText("[data-load-average]", data.load_average === null || data.load_average === undefined ? "—" : Number(data.load_average).toFixed(2));
    setText("[data-memory-size]", formatMemory(data.memory_used_bytes, data.memory_total_bytes));

    setText("[data-down-rate]", formatRate(downMbps));
    setText("[data-up-rate]", formatRate(upMbps));
    setText("[data-active-route]", routeNames[data.active_egress] || routeNames[data.connection] || "未确定");

    setInterface("wan", data.wan_interface_enabled, data.wan_carrier, data.wan_ethernet_speed, data.active_egress === "wan");
    setInterface("lan", data.lan_interface_enabled, data.lan_carrier, data.lan_ethernet_speed, data.active_egress === "lan");
    setText("[data-ethernet-egress]", data.active_egress === "wan" || data.active_egress === "lan" ? "当前网络出口" : "备用链路");
    setText("[data-public-ip]", text(data.wan_ip));
    setText("[data-local-ip]", text(data.local_wan_ip));

    setText("[data-wifi-clients]", String(finite(data.wifi_clients_count) || 0));
    renderWifi(data.wifi_interfaces);

    setText("[data-power-state]", data.on_charging ? "正在充电" : "电池供电");
    setText("[data-battery]", battery === null ? "—" : battery.toFixed(0));
    setText("[data-power]", wattage === null ? "—" : Math.abs(wattage).toFixed(1));
    setText("[data-current-gauge]", current === null ? "—" : Math.abs(current).toFixed(2));
    setText("[data-current-label]", current === null ? "电池电流" : current > 0 ? "充电电流" : current < 0 ? "放电电流" : "电池电流");
    setText("[data-supply-mode]", data.on_charging ? "外部供电 · 正在充电" : "电池放电");
    setGauge('[data-gauge="battery"]', battery, battery !== null && battery <= 15 ? "critical" : battery !== null && battery <= 35 ? "warning" : "normal");
    setGauge('[data-gauge="power"]', wattage === null ? 0 : Math.abs(wattage) / 15 * 100, "normal");
    setGauge('[data-gauge="current"]', current === null ? 0 : Math.abs(current) / 3 * 100, "normal");
    setText("[data-battery-voltage]", format(voltage, 3, " V"));
    setText("[data-charge-voltage]", chargeVoltage === null ? "—" : format(chargeVoltage > 100 ? chargeVoltage / 1000 : chargeVoltage, 2, " V"));
    setText("[data-remaining]", text(data.battery_remaining_time));

    setText("[data-model]", text(data.model));
    setText("[data-openwrt]", text(data.openwrt_version));
    setText("[data-kernel]", text(data.kernel));
    setText("[data-uptime]", chineseUptime(data.uptime));

    renderStorage("internal", data.internal_storage || {});
    renderStorage("sd", data.sd_info || {});
    renderStorage("nvme", data.nvme || {});
    drawAll();

    if (dashboardRoot && window.Alpine && typeof Alpine.$data === "function") {
      window.setTimeout(function () {
        const state = Alpine.$data(dashboardRoot);
        if (!state) return;
        state.uptime = chineseUptime(data.uptime);
        state.isp_name = chineseOperator(data.isp_name);
        state.enhanced_isp_full_cn = chineseOperator(data.enhanced_isp_full_cn);
        state.enhanced_isp_full_en = chineseOperator(data.enhanced_isp_full_en);
      }, 0);
    }
  }

  function connect() {
    if (window.DashboardService && typeof window.DashboardService.subscribe === "function") {
      unsubscribe = window.DashboardService.subscribe(update);
      window.DashboardService.fetchDashboard().then(update).catch(function () {});
      return;
    }
    const poll = function () {
      if (document.hidden) return;
      fetch("/api/v1/dashboard.json")
        .then((response) => response.json())
        .then(update)
        .catch(function () {});
    };
    poll();
    window.setInterval(poll, 4000);
  }

  function setModemMetric(name, value, maximum) {
    if (!ui) return;
    const metric = ui.root.querySelector('[data-modem-metric="' + name + '"]');
    if (!metric) return;
    const parsed = finite(value);
    const percent = parsed === null ? 0 : Math.max(0, Math.min(100, parsed / maximum * 100));
    metric.style.setProperty("--modem-level", percent + "%");
    metric.classList.toggle("is-warning", name === "temperature" && parsed !== null && parsed >= 70);
    metric.classList.toggle("is-critical", name === "temperature" && parsed !== null && parsed >= 82);
    metric.querySelector("b").textContent = parsed === null ? "—" : parsed.toFixed(1);
  }

  function updateModemRuntime(data) {
    if (!data || !ui) return;
    const available = modemDisplayReady && modemPresentState && data.modem_present !== false;
    setModemMetric("cpu", available ? data.cpu_usage : null, 100);
    setModemMetric("memory", available ? data.memory_usage : null, 100);
    setModemMetric("temperature", available ? data.temperature : null, 90);
  }

  function fetchModemRuntime() {
    if (document.hidden || !ui) return;
    fetch("/api/v1/modem/runtime.json", { cache: "no-store" })
      .then((response) => {
        if (!response.ok) throw new Error("modem runtime unavailable");
        return response.json();
      })
      .then(updateModemRuntime)
      .catch(function () {});
  }

  function connectModemRuntime() {
    fetchModemRuntime();
    modemRuntimeTimer = window.setInterval(fetchModemRuntime, 5000);
  }

  function requestCellularRefresh() {
    if (document.hidden || !ui) return;
    fetch("/api/v1/modem/basic.json", {
      method: "POST",
      cache: "no-store",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ refresh: "cellular_overview" })
    }).catch(function () {});
  }

  function refreshModemAfterOnline() {
    if (document.hidden || !ui || !modemPresentState) return;
    requestCellularRefresh();
    if (modemRefreshTimer !== null) window.clearTimeout(modemRefreshTimer);
    const check = function () {
      if (!modemPresentState || document.hidden || !ui) return;
      fetch("/api/v1/modem/basic.json", { cache: "no-store" })
        .then((response) => response.json())
        .then((basic) => {
          if (!basic.modem_valid) return;
          if (basic.querying) {
            modemRefreshTimer = window.setTimeout(check, 1000);
            return;
          }
          modemDisplayReady = true;
          return fetch("/api/v1/dashboard.json", { cache: "no-store" })
            .then((response) => response.json())
            .then(update)
            .then(fetchModemRuntime)
            .then(() => loadHistory(historyRange));
        })
        .catch(function () {
          modemRefreshTimer = window.setTimeout(check, 2000);
        });
    };
    modemRefreshTimer = window.setTimeout(check, 1200);
  }

  function connectCellularRefresh() {
    requestCellularRefresh();
    window.setTimeout(requestCellularRefresh, 5000);
    cellularRefreshTimer = window.setInterval(requestCellularRefresh, 15000);
  }

  function installNasNavigation() {
    if (document.querySelector('a[href="/nas"]')) return;
    const active = location.pathname.replace(/\/+$/, "") === "/nas";
    const makeItem = function (mobile) {
      const li = document.createElement("li");
      li.className = mobile ? "w-24" : "w-16 sm:w-24";
      if (!mobile) li.style.height = "50px";
      li.innerHTML = '<a href="/nas" title="网络存储" class="' +
        (active ? "nav-active" : "nav") +
        ' flex items-center justify-center space-x-2 ' + (mobile ? "py-2" : "h-full") + '">' +
        '<img src="/static/nas.svg" class="max-w-6 max-h-6 w-auto h-auto" alt="NAS"></a>';
      return li;
    };
    const desktop = document.querySelector("#nav-content > ul");
    if (desktop) desktop.insertBefore(makeItem(false), desktop.lastElementChild);
    const mobile = document.querySelector("#bottom-nav-content > ul");
    if (mobile) mobile.insertBefore(makeItem(true), mobile.lastElementChild);
  }

  document.addEventListener("DOMContentLoaded", function () {
    installNasNavigation();
    if (!isDashboard()) return;
    mount();
    connectModemRuntime();
    connectCellularRefresh();
    loadHistory(historyRange).finally(connect);
  });

  window.addEventListener("pagehide", function () {
    if (typeof unsubscribe === "function") unsubscribe();
    if (modemRuntimeTimer !== null) window.clearInterval(modemRuntimeTimer);
    if (cellularRefreshTimer !== null) window.clearInterval(cellularRefreshTimer);
    if (modemRefreshTimer !== null) window.clearTimeout(modemRefreshTimer);
  });

}());
