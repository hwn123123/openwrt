(function () {
  "use strict";

  const soundOrder = ["off", "short", "double", "notify", "success", "chime", "down", "warning", "alarm", "custom"];
  const soundLabels = {
    off: "不提示", short: "短音", double: "双音", notify: "通知音",
    success: "完成音", chime: "上升音", down: "下降音",
    warning: "警告音", alarm: "警报音", custom: "该事件的自定义音序"
  };
  const categoryOrder = ["系统", "SIM 与短信", "供电与电池", "温度安全", "蜂窝与出口", "局域网与 Wi-Fi", "外接存储"];
  const eventOrder = [
    "boot", "shutdown", "sms_received", "sim_ready", "sim_lost",
    "charging", "power_removed", "charge_full", "low_battery", "critical_battery",
    "board_hot", "board_normal", "modem_hot", "modem_normal",
    "cellular", "cellular_lost", "wan", "wan_lost", "uplink_cellular", "uplink_wan",
    "lan", "lan_lost", "wifi_start", "wifi_stop", "wifi_client", "wifi_client_left",
    "sd_inserted", "sd_removed", "nvme_inserted", "nvme_removed"
  ];
  let state = null;
  let config = null;
  let dirty = false;
  let expandedEvent = null;
  let toastTimer = null;
  let scheduleTimer = null;

  function one(selector, root) {
    return (root || document).querySelector(selector);
  }

  function all(selector, root) {
    return Array.from((root || document).querySelectorAll(selector));
  }

  function escapeHtml(value) {
    return String(value == null ? "" : value).replace(/[&<>"']/g, function (char) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[char];
    });
  }

  function cloneTones(tones) {
    return (Array.isArray(tones) ? tones : []).map(function (tone) {
      return { hz: Number(tone.hz) || 0, ms: Number(tone.ms) || 1 };
    });
  }

  async function jsonFetch(url, options) {
    const response = await fetch(url, options || {});
    let payload = {};
    try { payload = await response.json(); } catch (error) { payload = {}; }
    if (!response.ok || payload.status === "error") {
      throw new Error(payload.message || ("请求失败（HTTP " + response.status + "）"));
    }
    return payload;
  }

  function toast(message, type) {
    const node = one("[data-sound-toast]");
    if (!node) return;
    node.textContent = message;
    node.className = "pcat-sound-toast is-visible " + (type === "error" ? "is-error" : "is-success");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { node.className = "pcat-sound-toast"; }, 3200);
  }

  function setPlaying(duration) {
    const speaker = one("[data-sound-speaker]");
    if (!speaker) return;
    speaker.classList.add("is-playing");
    setTimeout(function () { speaker.classList.remove("is-playing"); }, Math.max(450, Math.min(duration || 900, 5000)));
  }

  function toneDuration(tones) {
    return (tones || []).reduce(function (sum, tone) { return sum + Number(tone.ms || 0); }, 0);
  }

  function formatDuration(ms) {
    return ms >= 1000 ? (ms / 1000).toFixed(2).replace(/0+$/, "").replace(/\.$/, "") + " 秒" : ms + " 毫秒";
  }

  function formatTime(epoch) {
    if (!epoch) return "—";
    return new Date(epoch * 1000).toLocaleString("zh-CN", { hour12: false });
  }

  function markDirty() {
    dirty = true;
    one("[data-save-events]").classList.add("is-dirty");
  }

  function eventSetting(name) {
    const definition = (state.event_definitions || {})[name] || { default: "off" };
    let setting = config.events[name];
    if (typeof setting === "string") setting = { sound: setting, tones: [] };
    if (!setting || typeof setting !== "object") setting = { sound: definition.default || "off", tones: [] };
    if (!soundOrder.includes(setting.sound)) setting.sound = definition.default || "off";
    if (!Array.isArray(setting.tones) || !setting.tones.length) {
      setting.tones = cloneTones(((state.presets || {})[setting.sound] || {}).tones || [{ hz: 2700, ms: 200 }]);
    }
    config.events[name] = setting;
    return setting;
  }

  function renderMode(data) {
    const mode = data.mode || "on";
    all("[data-beeper-mode]").forEach(function (button) {
      button.classList.toggle("is-active", button.dataset.beeperMode === mode);
    });
    const schedule = one("[data-sound-schedule]");
    schedule.hidden = mode !== "timed";
    one("[data-beeper-start]").value = data.start_time || "08:00";
    one("[data-beeper-end]").value = data.end_time || "22:00";
    one("[data-mode-summary]").textContent = mode === "off" ? "始终静音" : mode === "timed" ? "按时段允许" : "始终允许";
  }

  function renderBeeperStatus(data) {
    const enabled = !!data.current_enabled;
    const node = one("[data-beeper-state]");
    node.textContent = enabled ? "当前允许响铃" : "当前保持静音";
    node.classList.toggle("is-off", !enabled);
    one("[data-sound-speaker]").classList.toggle("is-disabled", !enabled);
  }

  function setRuntime(name, label, active) {
    const node = one('[data-runtime="' + name + '"]');
    if (!node) return;
    node.textContent = label;
    node.className = active == null ? "" : active ? "is-up" : "is-down";
  }

  function renderRuntime(runtime) {
    runtime = runtime || {};
    const monitor = !!runtime.monitor_alive;
    one("[data-monitor-dot]").classList.toggle("is-off", !monitor);
    one("[data-monitor-text]").textContent = monitor ? "事件监测运行中" : "事件监测未运行";
    ["cellular", "lan", "wan", "wifi"].forEach(function (key) {
      const value = runtime[key];
      setRuntime(key, value == null ? "未知" : value ? "已连接" : "未连接", value == null ? null : value);
    });
    setRuntime("wifi_clients", runtime.wifi_clients == null ? "未知" : runtime.wifi_clients + " 台", runtime.wifi_clients > 0);
    const battery = runtime.battery_percent == null ? "电量未知" : runtime.battery_percent + "%";
    setRuntime("battery", battery + " · " + (runtime.charging ? "外部供电" : "电池供电"), runtime.charging);
    const simLabels = { ready: "已就绪", absent: "未插卡", "not-ready": "未就绪", "need-pin": "需要 PIN", "need-puk": "需要 PUK", bad: "SIM 异常" };
    setRuntime("sim", simLabels[runtime.sim_state] || runtime.sim_state || "未知", runtime.sim_state === "ready");
    const uplinkLabels = { cellular: "5G 蜂窝", wan: "有线 WAN", other: "其他接口" };
    setRuntime("uplink", uplinkLabels[runtime.default_uplink] || "无默认路由", !!runtime.default_uplink);
    setRuntime("board_temperature", runtime.board_temperature == null ? "暂无数据" : runtime.board_temperature + " °C", runtime.board_hot === false);
    setRuntime("modem_temperature", runtime.modem_temperature == null ? "等待模组缓存" : runtime.modem_temperature + " °C", runtime.modem_hot === false);
    setRuntime("sd", runtime.sd_present ? "已接入" : "未接入", runtime.sd_present);
    setRuntime("nvme", runtime.nvme_present ? "已接入" : "未接入", runtime.nvme_present);
    const last = runtime.last_event;
    one("[data-last-event]").textContent = last ? "最近：" + ((state.event_definitions[last.event] || {}).label || last.event) + " · " + formatTime(last.time) : "还没有触发记录";
  }

  function renderHistory(history) {
    const list = one("[data-history-list]");
    const entries = (history || []).slice().reverse();
    one("[data-history-count]").textContent = entries.length + " 条";
    if (!entries.length) {
      list.innerHTML = "<p>本次运行期间暂无触发记录</p>";
      return;
    }
    list.innerHTML = entries.slice(0, 12).map(function (entry) {
      const definition = state.event_definitions[entry.event] || {};
      return '<div><b>' + escapeHtml(definition.icon || "•") + '</b><span><strong>' + escapeHtml(definition.label || entry.event) + '</strong><small>' + escapeHtml(soundLabels[entry.sound] || entry.sound) + '</small></span><time>' + escapeHtml(formatTime(entry.time)) + '</time></div>';
    }).join("");
  }

  function soundOptions(selected) {
    return soundOrder.map(function (sound) {
      return '<option value="' + sound + '"' + (sound === selected ? " selected" : "") + '>' + soundLabels[sound] + '</option>';
    }).join("");
  }

  function renderEventEditor(card, name) {
    const setting = eventSetting(name);
    const editor = one("[data-event-editor]", card);
    const steps = one("[data-event-steps]", card);
    editor.hidden = setting.sound !== "custom" || expandedEvent !== name;
    if (editor.hidden) return;
    steps.innerHTML = setting.tones.map(function (tone, index) {
      return '<div class="pcat-sound-step" data-tone-step><b>' + String(index + 1).padStart(2, "0") + '</b>' +
        '<label><span>频率</span><input type="number" min="0" max="12000" value="' + Number(tone.hz) + '" data-tone-hz><i>Hz</i></label>' +
        '<label><span>时长</span><input type="number" min="1" max="65535" value="' + Number(tone.ms) + '" data-tone-ms><i>ms</i></label>' +
        '<button type="button" data-tone-remove title="删除此音调"' + (setting.tones.length <= 1 ? " disabled" : "") + '>×</button></div>';
    }).join("");
    one("[data-event-duration]", card).textContent = "总时长 " + formatDuration(toneDuration(setting.tones));
    all("input", steps).forEach(function (input) {
      input.addEventListener("input", function () {
        const row = input.closest("[data-tone-step]");
        const index = all("[data-tone-step]", steps).indexOf(row);
        setting.tones[index] = {
          hz: Math.max(0, Math.min(12000, Number(one("[data-tone-hz]", row).value) || 0)),
          ms: Math.max(1, Math.min(65535, Number(one("[data-tone-ms]", row).value) || 1))
        };
        one("[data-event-duration]", card).textContent = "总时长 " + formatDuration(toneDuration(setting.tones));
        markDirty();
      });
    });
    all("[data-tone-remove]", steps).forEach(function (button, index) {
      button.addEventListener("click", function () {
        if (setting.tones.length <= 1) return;
        setting.tones.splice(index, 1);
        markDirty();
        renderEventEditor(card, name);
      });
    });
  }

  function updateEventCard(card, name) {
    const setting = eventSetting(name);
    one("select", card).value = setting.sound;
    card.classList.toggle("is-disabled", setting.sound === "off");
    card.classList.toggle("is-editing", setting.sound === "custom" && expandedEvent === name);
    one("[data-event-preview]", card).disabled = setting.sound === "off";
    one("[data-event-custom]", card).classList.toggle("is-active", setting.sound === "custom");
    renderEventEditor(card, name);
  }

  function renderEvents() {
    const grid = one("[data-event-grid]");
    const definitions = state.event_definitions || {};
    const categories = categoryOrder.concat(Object.keys(definitions).map(function (name) { return definitions[name].category || "其他"; }))
      .filter(function (category, index, items) { return items.indexOf(category) === index; });
    grid.innerHTML = categories.map(function (category) {
      const names = eventOrder.concat(Object.keys(definitions)).filter(function (name, index, items) {
        return items.indexOf(name) === index && definitions[name] && (definitions[name].category || "其他") === category;
      });
      if (!names.length) return "";
      return '<section class="pcat-sound-event-group"><header><strong>' + escapeHtml(category) + '</strong><small>' + names.length + ' 个事件</small></header><div>' + names.map(function (name) {
        const item = definitions[name];
        const setting = eventSetting(name);
        return '<article class="pcat-sound-event' + (setting.sound === "off" ? " is-disabled" : "") + '" data-event="' + name + '">' +
          '<div class="pcat-sound-event-main"><b class="pcat-sound-event-icon">' + escapeHtml(item.icon) + '</b>' +
          '<div class="pcat-sound-event-copy"><strong>' + escapeHtml(item.label) + '</strong><small>' + escapeHtml(item.hint) + '</small></div>' +
          '<select aria-label="' + escapeHtml(item.label) + '提示音">' + soundOptions(setting.sound) + '</select>' +
          '<div class="pcat-sound-event-actions"><button type="button" data-event-preview' + (setting.sound === "off" ? " disabled" : "") + '>试听</button><button type="button" data-event-custom>自定义</button></div></div>' +
          '<div class="pcat-sound-event-editor" data-event-editor hidden><header><span>“' + escapeHtml(item.label) + '”的独立音序</span><small data-event-duration>—</small></header><div class="pcat-sound-event-steps" data-event-steps></div><footer><button type="button" data-event-add>＋ 添加音调</button><button type="button" data-event-reset>恢复该事件默认</button><span>0 Hz 为静音间隔，最多 20 步</span></footer></div></article>';
      }).join("") + '</div></section>';
    }).join("");

    all("[data-event]", grid).forEach(function (card) {
      const name = card.dataset.event;
      const select = one("select", card);
      select.addEventListener("change", function () {
        const setting = eventSetting(name);
        const previousSound = setting.sound;
        setting.sound = select.value;
        if (setting.sound === "custom") {
          if (!setting.tones.length) setting.tones = cloneTones(((state.presets || {})[previousSound] || {}).tones);
          expandedEvent = name;
        } else if (expandedEvent === name) {
          expandedEvent = null;
        }
        markDirty();
        updateEventCard(card, name);
      });
      one("[data-event-preview]", card).addEventListener("click", function () {
        const setting = eventSetting(name);
        preview(setting.sound, setting.tones);
      });
      one("[data-event-custom]", card).addEventListener("click", function () {
        const setting = eventSetting(name);
        if (setting.sound !== "custom") {
          setting.tones = cloneTones(((state.presets || {})[setting.sound] || {}).tones || setting.tones);
          setting.sound = "custom";
          expandedEvent = name;
          markDirty();
        } else {
          expandedEvent = expandedEvent === name ? null : name;
        }
        updateEventCard(card, name);
      });
      one("[data-event-add]", card).addEventListener("click", function () {
        const setting = eventSetting(name);
        if (setting.tones.length >= 20) return toast("每个事件最多 20 个音调步骤", "error");
        setting.tones.push({ hz: 2700, ms: 200 });
        markDirty();
        renderEventEditor(card, name);
      });
      one("[data-event-reset]", card).addEventListener("click", function () {
        const sound = definitions[name].default || "off";
        config.events[name] = { sound: sound, tones: cloneTones(((state.presets || {})[sound] || {}).tones || [{ hz: 2700, ms: 200 }]) };
        if (expandedEvent === name) expandedEvent = null;
        markDirty();
        updateEventCard(card, name);
      });
      updateEventCard(card, name);
    });
  }

  function syncThresholds() {
    all("[data-threshold]").forEach(function (input) {
      let value = Number(input.value);
      value = Math.max(Number(input.min), Math.min(Number(input.max), isFinite(value) ? Math.round(value) : Number(input.min)));
      input.value = value;
      config[input.dataset.threshold] = value;
    });
    if (config.critical_battery_threshold >= config.low_battery_threshold) {
      config.critical_battery_threshold = Math.max(2, config.low_battery_threshold - 5);
      one('[data-threshold="critical_battery_threshold"]').value = config.critical_battery_threshold;
    }
  }

  async function preview(sound, tones) {
    try {
      const selected = sound === "custom" ? tones : ((state.presets[sound] || {}).tones || []);
      await jsonFetch("/api/v1/sound_events/preview.json", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ sound: sound, tones: sound === "custom" ? tones : undefined })
      });
      setPlaying(toneDuration(selected));
      toast("正在试听“" + soundLabels[sound] + "”", "success");
    } catch (error) {
      toast("试听失败：" + error.message, "error");
    }
  }

  function renderThresholds() {
    all("[data-threshold]").forEach(function (input) {
      if (config[input.dataset.threshold] != null) input.value = config[input.dataset.threshold];
    });
  }

  async function loadSound(first) {
    try {
      const data = await jsonFetch("/api/v1/sound_events.json");
      state = data;
      if (first || !dirty) {
        config = JSON.parse(JSON.stringify(data.config));
        renderEvents();
        renderThresholds();
      }
      renderRuntime(data.runtime);
      renderHistory(data.history);
    } catch (error) {
      toast("事件状态读取失败：" + error.message, "error");
    }
  }

  async function loadBeeper() {
    try {
      const results = await Promise.all([
        jsonFetch("/api/v1/beeper_settings.json"),
        jsonFetch("/api/v1/beeper_status.json")
      ]);
      renderMode(results[0]);
      renderBeeperStatus(results[1]);
    } catch (error) {
      toast("蜂鸣器状态读取失败：" + error.message, "error");
    }
  }

  async function saveMode(mode) {
    try {
      await jsonFetch("/api/v1/beeper_settings.json", {
        method: "POST", headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ mode: mode, start_time: one("[data-beeper-start]").value, end_time: one("[data-beeper-end]").value })
      });
      await loadBeeper();
      toast("蜂鸣器工作策略已保存", "success");
    } catch (error) {
      toast("保存失败：" + error.message, "error");
    }
  }

  async function saveEvents() {
    try {
      syncThresholds();
      const result = await jsonFetch("/api/v1/sound_events.json", {
        method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(config)
      });
      state = result;
      config = JSON.parse(JSON.stringify(result.config));
      dirty = false;
      expandedEvent = null;
      one("[data-save-events]").classList.remove("is-dirty");
      renderEvents(); renderThresholds(); renderRuntime(result.runtime); renderHistory(result.history);
      toast("全部事件的独立音效与阈值已保存", "success");
    } catch (error) {
      toast("保存失败：" + error.message, "error");
    }
  }

  function bind() {
    all("[data-beeper-mode]").forEach(function (button) {
      button.addEventListener("click", function () { saveMode(button.dataset.beeperMode); });
    });
    [one("[data-beeper-start]"), one("[data-beeper-end]")].forEach(function (input) {
      input.addEventListener("change", function () {
        clearTimeout(scheduleTimer);
        scheduleTimer = setTimeout(function () { saveMode("timed"); }, 250);
      });
    });
    one("[data-save-events]").addEventListener("click", saveEvents);
    one("[data-reset-events]").addEventListener("click", function () {
      config = JSON.parse(JSON.stringify(state.defaults));
      expandedEvent = null;
      markDirty();
      renderEvents();
      renderThresholds();
      toast("已载入默认方案，点击保存后生效", "success");
    });
    all("[data-threshold]").forEach(function (input) {
      input.addEventListener("change", function () { syncThresholds(); markDirty(); });
    });
    one("[data-sound-refresh]").addEventListener("click", function () { loadSound(false); loadBeeper(); });
  }

  async function init() {
    if (!one('[data-settings-page="sound_events"]')) return;
    bind();
    await Promise.all([loadSound(true), loadBeeper()]);
    setInterval(function () { loadSound(false); loadBeeper(); }, 5000);
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init);
  else init();
}());
