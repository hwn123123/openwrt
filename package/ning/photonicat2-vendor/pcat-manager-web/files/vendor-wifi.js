(function () {
  "use strict";

  const root = document.getElementById("pcat-wifi-page");
  if (!root) return;

  const cards = document.getElementById("pcat-wifi-cards");
  const summary = document.getElementById("pcat-wifi-summary");
  const notice = document.getElementById("pcat-wifi-notice");
  let state = { interfaces: [], countries: [] };

  function esc(value) {
    return String(value == null ? "" : value)
      .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
      .replace(/"/g, "&quot;").replace(/'/g, "&#39;");
  }

  function notify(message, type) {
    notice.textContent = message;
    notice.className = "pcat-wifi-notice is-" + (type || "info");
    notice.hidden = false;
    clearTimeout(notice._timer);
    notice._timer = setTimeout(function () { notice.hidden = true; }, type === "error" ? 7000 : 4200);
  }

  function bandLabel(band) {
    return { "2g": "2.4 GHz", "5g": "5 GHz", "6g": "6 GHz" }[band] || band.toUpperCase();
  }

  function modeLabel(mode) {
    if (mode === "NOHT") return "传统模式 · 20 MHz";
    const width = (mode.match(/[0-9]+$/) || ["20"])[0];
    const family = mode.replace(/[0-9]+$/, "");
    const standard = { HT: "Wi-Fi 4", VHT: "Wi-Fi 5", HE: "Wi-Fi 6", EHT: "Wi-Fi 7" }[family] || family;
    return standard + " · " + mode + " · " + width + " MHz";
  }

  function radioTitle(radio) {
    const model = radio.hardware.model || "未知无线模块";
    const location = radio.hardware.kind === "pcie" ? "PCIe 无线" :
      radio.hardware.kind === "onboard" ? "板载无线" : "无线";
    return model + " · " + location;
  }

  function countryOptions(current) {
    const list = state.countries.slice();
    if (!list.some(function (item) { return item.code === current; })) {
      list.unshift({ code: current || "00", name: current === "CN" ? "China" : "当前配置" });
    }
    return list.map(function (item) {
      return '<option value="' + esc(item.code) + '"' + (item.code === current ? " selected" : "") + '>' +
        esc(item.code + " · " + item.name) + "</option>";
    }).join("");
  }

  function option(value, label, current, disabled) {
    return '<option value="' + esc(value) + '"' + (String(value) === String(current) ? " selected" : "") +
      (disabled ? " disabled" : "") + ">" + esc(label) + "</option>";
  }

  function capabilityBadges(radio) {
    const bands = Object.keys(radio.capabilities.bands || {});
    return [
      radio.standards.join(" / "),
      bands.map(bandLabel).join(" / "),
      radio.capabilities.tx_streams + "×" + radio.capabilities.rx_streams + " MIMO",
      radio.hardware.driver
    ].filter(Boolean).map(function (text) { return "<span>" + esc(text) + "</span>"; }).join("");
  }

  function supportedRows(radio) {
    return Object.keys(radio.capabilities.bands || {}).map(function (band) {
      const info = radio.capabilities.bands[band];
      const modes = (info.modes || []).filter(function (name) { return name !== "NOHT"; });
      return '<div class="pcat-wifi-cap-row"><strong>' + esc(bandLabel(band)) + '</strong>' +
        '<span>' + esc(modes.join(" / ")) + '</span><em>最大 ' + esc(info.max_width) + ' MHz</em></div>';
    }).join("");
  }

  function renderRadio(radio) {
    const current = radio.current;
    const bands = Object.keys(radio.capabilities.bands || {});
    const statusClass = radio.enabled ? (radio.runtime.up ? "is-on" : "is-wait") : "is-off";
    const statusText = radio.enabled ? (radio.runtime.up ? "已开启" : "已启用 / 待启动") : "已关闭";
    const maxStandard = radio.standards[radio.standards.length - 1] || "Wi-Fi";
    const accent = radio.hardware.kind === "pcie" ? "pcie" : "onboard";
    return '<article class="pcat-wifi-card is-' + accent + '" data-radio="' + esc(radio.device) + '">' +
      '<header class="pcat-wifi-card-head">' +
        '<div class="pcat-wifi-radio-mark">' + (radio.hardware.kind === "pcie" ? "7" : "6") + '</div>' +
        '<div class="pcat-wifi-card-title"><small>' + esc(radio.device + " · " + radio.phy) + '</small>' +
          '<h2>' + esc(radioTitle(radio)) + '</h2><div class="pcat-wifi-badges">' + capabilityBadges(radio) + '</div></div>' +
        '<div class="pcat-wifi-power-state ' + statusClass + '"><i></i><strong>' + statusText + '</strong>' +
          '<span>开关请到设备控制</span></div>' +
      '</header>' +
      '<div class="pcat-wifi-current">' +
        '<div><span>当前用途</span><strong>' + (current.mode === "sta" ? "无线客户端 / 上联网" : "无线接入点 / AP") + '</strong></div>' +
        '<div><span>当前制式</span><strong>' + esc(current.htmode) + '</strong></div>' +
        '<div><span>频段 / 信道</span><strong>' + esc(bandLabel(current.band) + " / " + current.channel) + '</strong></div>' +
        '<div><span>硬件等级</span><strong>' + esc(maxStandard) + '</strong></div>' +
      '</div>' +
      '<form class="pcat-wifi-form" autocomplete="off">' +
        '<div class="pcat-wifi-section-title"><div><strong>工作方式与身份</strong><span>AP 用于给设备提供无线网络，客户端用于连接其他热点上网</span></div>' +
          '<button type="button" class="pcat-wifi-preset" data-action="preset">推荐高性能参数</button></div>' +
        '<div class="pcat-wifi-fields">' +
          '<label><span>工作模式</span><select data-field="mode">' +
            option("ap", "无线接入点（AP）", current.mode) + option("sta", "无线客户端（上联网）", current.mode) + '</select></label>' +
          '<label><span>SSID</span><input data-field="ssid" maxlength="32" value="' + esc(current.ssid) + '" placeholder="无线网络名称"></label>' +
          '<label><span>加密方式</span><select data-field="encryption">' +
            option("none", "开放网络（不加密）", current.encryption) +
            option("psk2", "WPA2-PSK", current.encryption) +
            option("sae-mixed", "WPA2/WPA3 混合", current.encryption) +
            option("sae", "WPA3-SAE", current.encryption) + '</select></label>' +
          '<label><span>无线密码</span><div class="pcat-wifi-password"><input data-field="password" type="password" maxlength="63" value="' + esc(current.password) + '" placeholder="8 至 63 个字符"><button type="button" data-action="password">显示</button></div></label>' +
          '<label class="pcat-wifi-sta-only"><span>指定 BSSID（可选）</span><input data-field="bssid" value="' + esc(current.bssid) + '" placeholder="AA:BB:CC:DD:EE:FF"></label>' +
          '<label class="pcat-wifi-ap-only pcat-wifi-check"><span>广播设置</span><span><input data-field="hidden" type="checkbox"' + (current.hidden ? " checked" : "") + '> 隐藏 SSID</span></label>' +
        '</div>' +
        '<div class="pcat-wifi-section-title"><div><strong>射频与法规参数</strong><span>选项来自本机 board.json、无线驱动与 regulatory database</span></div></div>' +
        '<div class="pcat-wifi-fields">' +
          '<label><span>频段</span><select data-field="band">' + bands.map(function (band) { return option(band, bandLabel(band), current.band); }).join("") + '</select></label>' +
          '<label><span>制式与信道宽度</span><select data-field="htmode"></select></label>' +
          '<label><span>信道</span><select data-field="channel"></select></label>' +
          '<label><span>国家 / 地区</span><select data-field="country">' + countryOptions(current.country) + '</select></label>' +
          '<label><span>发射功率</span><select data-field="txpower"></select></label>' +
          '<label><span>接口归属</span><input readonly value="' + esc(current.mode === "sta" ? "wifiwan（DHCP 上联网）" : "lan（局域网桥）") + '" data-role="network-view"></label>' +
        '</div>' +
        '<div class="pcat-wifi-sta-tools"><button type="button" data-action="scan">扫描附近热点</button><span>扫描会短暂占用当前无线模块，不会改变电源开关。</span></div>' +
        '<div class="pcat-wifi-scan" data-role="scan-results" hidden></div>' +
        '<details class="pcat-wifi-capabilities"><summary>查看全部硬件能力、频段与带宽</summary>' + supportedRows(radio) +
          '<div class="pcat-wifi-hardware-grid"><span>总线<strong>' + esc(radio.hardware.bus.toUpperCase()) + '</strong></span>' +
          '<span>内核驱动<strong>' + esc(radio.hardware.driver) + '</strong></span><span>物理接口<strong>' + esc((radio.interfaces || []).join(", ") || "未创建") + '</strong></span>' +
          '<span>配置接口<strong>' + esc(radio.interface_section) + '</strong></span></div></details>' +
        '<footer class="pcat-wifi-actions"><span>保存时不会开启或关闭此无线模块</span>' +
          '<button type="submit">保存 ' + esc(radio.hardware.model) + ' 参数</button></footer>' +
      '</form>' +
    '</article>';
  }

  function populateModes(card, radio, preferred) {
    const band = card.querySelector('[data-field="band"]').value;
    const select = card.querySelector('[data-field="htmode"]');
    const modes = ((radio.capabilities.bands[band] || {}).modes || []);
    const current = preferred || select.value || radio.current.htmode;
    select.innerHTML = modes.map(function (mode) { return option(mode, modeLabel(mode), current); }).join("");
    if (!select.value && modes.length) select.value = modes[modes.length - 1];
  }

  function populateChannels(card, radio, preferred) {
    const band = card.querySelector('[data-field="band"]').value;
    const mode = card.querySelector('[data-field="mode"]').value;
    const select = card.querySelector('[data-field="channel"]');
    const current = String(preferred || select.value || radio.current.channel || "auto");
    let list = (radio.channels[band] || []).slice();
    if (!list.length) {
      const fallbacks = band === "2g" ? [1, 6, 11] : band === "5g" ? [36, 40, 44, 48, 149, 153, 157, 161] : [5, 37, 69, 101, 133, 165, 197, 229];
      list = fallbacks.map(function (channel) { return { channel: channel, mhz: 0, no_ir: false, radar: false }; });
    }
    select.innerHTML = option("auto", "自动选择", current) + list.map(function (item) {
      const notes = [];
      if (item.radar) notes.push("DFS");
      if (item.no_ir) notes.push("仅被动扫描");
      const label = "信道 " + item.channel + (item.mhz ? " · " + item.mhz + " MHz" : "") + (notes.length ? " · " + notes.join("/") : "");
      return option(item.channel, label, current, mode === "ap" && item.no_ir);
    }).join("");
    if (!select.value) select.value = "auto";
  }

  function populatePowers(card, radio, preferred) {
    const select = card.querySelector('[data-field="txpower"]');
    const current = String(preferred || radio.current.txpower || "auto");
    const powers = (radio.txpowers || []).filter(function (item, index, all) {
      return all.findIndex(function (other) { return other.dbm === item.dbm; }) === index;
    });
    select.innerHTML = option("auto", "自动（遵循国家法规）", current) + powers.map(function (item) {
      return option(item.dbm, item.dbm + " dBm · " + item.mw + " mW", current);
    }).join("");
  }

  function syncMode(card) {
    const isSta = card.querySelector('[data-field="mode"]').value === "sta";
    card.querySelectorAll(".pcat-wifi-sta-only, .pcat-wifi-sta-tools").forEach(function (node) { node.hidden = !isSta; });
    card.querySelectorAll(".pcat-wifi-ap-only").forEach(function (node) { node.hidden = isSta; });
    card.querySelector('[data-role="network-view"]').value = isSta ? "wifiwan（DHCP 上联网）" : "lan（局域网桥）";
  }

  function findRadio(card) {
    return state.interfaces.find(function (radio) { return radio.device === card.dataset.radio; });
  }

  function recommended(card, radio) {
    const bands = radio.capabilities.bands || {};
    const band = bands["5g"] ? "5g" : bands["2g"] ? "2g" : Object.keys(bands)[0];
    card.querySelector('[data-field="band"]').value = band;
    const modes = (bands[band] || {}).modes || [];
    const preferred = ["HE80", "EHT80", "VHT80", "HE40", "EHT40", "HT40"].find(function (mode) { return modes.indexOf(mode) >= 0; }) || modes[modes.length - 1];
    populateModes(card, radio, preferred);
    const desiredChannel = band === "5g" ? "149" : band === "2g" ? "6" : "auto";
    populateChannels(card, radio, desiredChannel);
    const powers = radio.txpowers || [];
    if (powers.length) card.querySelector('[data-field="txpower"]').value = String(Math.max.apply(null, powers.map(function (item) { return item.dbm; })));
    notify("已载入稳定高性能参数，确认国家/地区与信道后再保存。", "info");
  }

  function scan(card, radio, button) {
    const panel = card.querySelector('[data-role="scan-results"]');
    button.disabled = true;
    button.textContent = "正在扫描…";
    panel.hidden = false;
    panel.innerHTML = "<p>扫描附近热点通常需要 5–20 秒…</p>";
    fetch("/api/v2/wireless/scan.json?device=" + encodeURIComponent(radio.device))
      .then(function (response) { return response.json().then(function (data) { return { ok: response.ok, data: data }; }); })
      .then(function (result) {
        if (!result.ok) throw new Error(result.data.message || "扫描失败");
        if (!result.data.results.length) {
          panel.innerHTML = "<p>没有扫描到热点。</p>";
          return;
        }
        panel.innerHTML = result.data.results.map(function (network) {
          return '<button type="button" data-scan-ssid="' + esc(network.ssid) + '" data-scan-bssid="' + esc(network.bssid) + '" data-scan-security="' + esc(network.encryption) + '">' +
            '<strong>' + esc(network.ssid) + '</strong><span>' + esc((network.signal || "--") + " dBm · 信道 " + (network.channel || "--")) + '</span><em>' + esc(network.encryption.toUpperCase()) + '</em></button>';
        }).join("");
      }).catch(function (error) {
        panel.innerHTML = "<p class=\"is-error\">" + esc(error.message) + "</p>";
      }).finally(function () {
        button.disabled = false;
        button.textContent = "扫描附近热点";
      });
  }

  function save(card, radio) {
    const value = function (name) { return card.querySelector('[data-field="' + name + '"]').value; };
    const payload = {
      device: radio.device,
      mode: value("mode"),
      ssid: value("ssid"),
      encryption: value("encryption"),
      password: value("password"),
      band: value("band"),
      htmode: value("htmode"),
      channel: value("channel"),
      country: value("country"),
      txpower: value("txpower"),
      bssid: value("bssid"),
      hidden: card.querySelector('[data-field="hidden"]').checked
    };
    const button = card.querySelector('button[type="submit"]');
    button.disabled = true;
    button.textContent = "正在保存…";
    fetch("/api/v1/wireless.json", {
      method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(payload)
    }).then(function (response) {
      return response.json().then(function (data) { return { ok: response.ok, data: data }; });
    }).then(function (result) {
      if (!result.ok) throw new Error(result.data.message || "保存失败");
      notify(result.data.message || "无线参数已保存", "success");
      setTimeout(load, 3500);
    }).catch(function (error) {
      notify(error.message, "error");
    }).finally(function () {
      button.disabled = false;
      button.textContent = "保存 " + radio.hardware.model + " 参数";
    });
  }

  cards.addEventListener("change", function (event) {
    const card = event.target.closest("[data-radio]");
    if (!card) return;
    const radio = findRadio(card);
    if (event.target.matches('[data-field="band"]')) {
      populateModes(card, radio);
      populateChannels(card, radio);
      if (event.target.value === "6g") card.querySelector('[data-field="encryption"]').value = "sae";
    } else if (event.target.matches('[data-field="mode"]')) {
      syncMode(card);
      populateChannels(card, radio);
    }
  });

  cards.addEventListener("click", function (event) {
    const card = event.target.closest("[data-radio]");
    if (!card) return;
    const radio = findRadio(card);
    const action = event.target.closest("[data-action]");
    const scanChoice = event.target.closest("[data-scan-ssid]");
    if (scanChoice) {
      card.querySelector('[data-field="ssid"]').value = scanChoice.dataset.scanSsid;
      card.querySelector('[data-field="bssid"]').value = scanChoice.dataset.scanBssid;
      card.querySelector('[data-field="encryption"]').value = scanChoice.dataset.scanSecurity;
      notify("已选择热点 “" + scanChoice.dataset.scanSsid + "”，请输入密码后保存。", "info");
      return;
    }
    if (!action) return;
    if (action.dataset.action === "preset") recommended(card, radio);
    if (action.dataset.action === "scan") scan(card, radio, action);
    if (action.dataset.action === "password") {
      const input = card.querySelector('[data-field="password"]');
      input.type = input.type === "password" ? "text" : "password";
      action.textContent = input.type === "password" ? "显示" : "隐藏";
    }
  });

  cards.addEventListener("submit", function (event) {
    event.preventDefault();
    const card = event.target.closest("[data-radio]");
    if (card) save(card, findRadio(card));
  });

  function render() {
    const pcie = state.interfaces.find(function (radio) { return radio.hardware.kind === "pcie"; });
    const onboard = state.interfaces.find(function (radio) { return radio.hardware.kind === "onboard"; });
    summary.innerHTML = '<div><span>无线设备</span><strong>' + state.interfaces.length + ' 块已识别</strong></div>' +
      '<div><span>' + esc(pcie ? pcie.hardware.model : "PCIe 无线") + '</span><strong>' + (pcie ? (pcie.enabled ? "已开启 · 主力无线" : "已关闭") : "未检测到") + '</strong></div>' +
      '<div><span>' + esc(onboard ? onboard.hardware.model : "板载无线") + '</span><strong>' + (onboard ? (onboard.enabled ? "已开启" : "已关闭") : "未检测到") + '</strong></div>';
    cards.innerHTML = state.interfaces.map(renderRadio).join("") || '<div class="pcat-wifi-loading is-error">没有检测到可配置的无线模块。</div>';
    cards.querySelectorAll("[data-radio]").forEach(function (card) {
      const radio = findRadio(card);
      populateModes(card, radio, radio.current.htmode);
      populateChannels(card, radio, radio.current.channel);
      populatePowers(card, radio, radio.current.txpower);
      syncMode(card);
    });
  }

  function load() {
    fetch("/api/v2/wireless.json", { cache: "no-store" })
      .then(function (response) { if (!response.ok) throw new Error("读取无线配置失败"); return response.json(); })
      .then(function (data) { state = data; render(); })
      .catch(function (error) { cards.innerHTML = '<div class="pcat-wifi-loading is-error">' + esc(error.message) + '</div>'; });
  }

  load();
}());
