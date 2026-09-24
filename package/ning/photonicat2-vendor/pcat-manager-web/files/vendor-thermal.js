(function () {
  'use strict';

  const CATEGORY_ORDER = [
    ['processor', '处理器', true],
    ['accelerator', '计算单元', true],
    ['memory', '内存', true],
    ['network', '网络设备', true],
    ['modem', '5G 蜂窝模组', true],
    ['board', '主板与机身', true],
    ['storage', '存储设备', false],
    ['power', '电源系统', false],
    ['other', '其他传感器', false]
  ];

  const STATE_TEXT = {
    normal: '正常', warning: '偏高', critical: '过热', unavailable: '不可用'
  };

  let inFlight = false;
  let timer = null;

  function byId(id) { return document.getElementById(id); }
  function finite(value) {
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  }
  function clamp(value, low, high) { return Math.min(high, Math.max(low, value)); }
  function text(node, value) { if (node) node.textContent = value; }
  function empty(node) { while (node && node.firstChild) node.removeChild(node.firstChild); }
  function element(tag, className, value) {
    const node = document.createElement(tag);
    if (className) node.className = className;
    if (value !== undefined && value !== null) node.textContent = value;
    return node;
  }
  function valueOrDash(value, digits, suffix) {
    const number = finite(value);
    return number === null ? '—' : number.toFixed(digits) + (suffix || '');
  }
  function setRing(id, value, maximum) {
    const node = byId(id);
    const number = finite(value);
    if (node) node.style.setProperty('--meter', number === null ? 0 : clamp(number / maximum * 100, 0, 100));
  }

  function renderTemperatures(items) {
    const root = byId('thermal-groups');
    if (!root) return;
    empty(root);
    const grouped = {};
    (items || []).forEach((item) => {
      const category = item.category || 'other';
      if (!grouped[category]) grouped[category] = [];
      grouped[category].push(item);
    });

    CATEGORY_ORDER.forEach(([key, title, always]) => {
      const sensors = grouped[key] || [];
      if (!always && !sensors.length) return;
      const group = element('section', 'pcat-temperature-group');
      const heading = element('header');
      heading.appendChild(element('strong', '', title));
      heading.appendChild(element('small', '', sensors.length ? sensors.length + ' 个传感器' : '暂无可用数据'));
      group.appendChild(heading);
      const list = element('div', 'pcat-temperature-list');
      if (!sensors.length) {
        list.appendChild(element('div', 'pcat-thermal-empty', key === 'modem' ? '等待厂家服务缓存 FM350 温度数据' : '当前硬件未提供此类温度读数'));
      }
      sensors.forEach((sensor) => {
        const state = sensor.state || 'unavailable';
        const card = element('article', 'pcat-sensor-card is-' + state);
        const top = element('div');
        const strong = element('strong');
        const temperature = finite(sensor.temperature_c);
        strong.appendChild(document.createTextNode(temperature === null ? '—' : temperature.toFixed(1)));
        strong.appendChild(element('small', '', temperature === null ? '' : ' °C'));
        const dot = element('span', 'pcat-sensor-state');
        dot.title = STATE_TEXT[state] || state;
        top.appendChild(strong);
        top.appendChild(dot);
        card.appendChild(top);
        card.appendChild(element('span', 'pcat-sensor-label', sensor.label || sensor.id));
        card.appendChild(element('small', 'pcat-sensor-source', sensor.source || '系统传感器'));
        const limits = [];
        if (finite(sensor.warning_c) !== null) limits.push('警告 ' + Number(sensor.warning_c).toFixed(0) + '°C');
        if (finite(sensor.critical_c) !== null) limits.push('临界 ' + Number(sensor.critical_c).toFixed(0) + '°C');
        card.title = (sensor.label || sensor.id) + (limits.length ? ' · ' + limits.join(' · ') : '');
        list.appendChild(card);
      });
      group.appendChild(list);
      root.appendChild(group);
    });
  }

  function renderCores(cpu) {
    const root = byId('thermal-cpu-cores');
    if (!root) return;
    empty(root);
    const cores = (cpu && cpu.cores) || [];
    if (!cores.length) {
      root.appendChild(element('div', 'pcat-thermal-empty', '暂无核心采样数据'));
      return;
    }
    cores.forEach((core) => {
      const usage = finite(core.usage);
      const card = element('article', 'pcat-core-card');
      const row = element('div');
      row.appendChild(element('span', '', 'CPU ' + core.id));
      row.appendChild(element('strong', '', usage === null ? '—' : usage.toFixed(0) + '%'));
      const meter = element('div', 'pcat-meter');
      const fill = element('span');
      fill.style.setProperty('--value', (usage === null ? 0 : clamp(usage, 0, 100)) + '%');
      meter.appendChild(fill);
      card.appendChild(row);
      card.appendChild(meter);
      root.appendChild(card);
    });
  }

  function renderPolicies(cpu) {
    const root = byId('thermal-policies');
    if (!root) return;
    empty(root);
    const policies = (cpu && cpu.policies) || [];
    if (!policies.length) {
      root.appendChild(element('div', 'pcat-thermal-empty', 'CPU 未提供 cpufreq 策略'));
      return;
    }
    policies.forEach((policy) => {
      const card = element('article', 'pcat-policy-card');
      const cores = (policy.cores || []).map((item) => 'CPU' + item).join(' · ');
      card.appendChild(element('span', '', cores || policy.name));
      card.appendChild(element('strong', '', valueOrDash(policy.current_mhz, 0, ' MHz')));
      card.appendChild(element('small', '', policy.governor || '未知调速器'));
      const range = finite(policy.minimum_mhz) === null || finite(policy.maximum_mhz) === null
        ? '频率范围不可用'
        : Number(policy.minimum_mhz).toFixed(0) + '–' + Number(policy.maximum_mhz).toFixed(0) + ' MHz';
      card.appendChild(element('small', '', range));
      root.appendChild(card);
    });
  }

  function metric(label, value, note) {
    const card = element('article', 'pcat-metric-card');
    card.appendChild(element('span', '', label));
    card.appendChild(element('strong', '', value));
    if (note) card.appendChild(element('small', '', note));
    return card;
  }

  function powerStatus(raw) {
    const names = {
      Charging: '正在充电', Discharging: '电池供电', Full: '电池已充满',
      'Not charging': '未充电', Unknown: '状态未知'
    };
    return names[raw] || raw || '状态未知';
  }

  function renderPower(power) {
    const root = byId('thermal-power-items');
    if (!root) return;
    empty(root);
    power = power || {};
    root.appendChild(metric('电池电压', valueOrDash(power.battery_voltage_v, 3, ' V'), powerStatus(power.battery_status)));
    root.appendChild(metric('电池电流', valueOrDash(power.battery_current_a, 3, ' A'), '正负方向由电源管理芯片定义'));
    root.appendChild(metric('电池侧功率', valueOrDash(power.battery_power_w, 2, ' W'), '不含无法测量的输入损耗'));
    root.appendChild(metric('电池电量', valueOrDash(power.battery_capacity, 0, '%'), powerStatus(power.battery_status)));
    root.appendChild(metric('充电输入', valueOrDash(power.charger_voltage_v, 3, ' V'), power.charger_online ? '外部电源在线' : '未连接外部电源'));
    root.appendChild(metric('供电来源', power.charger_online ? '外部供电' : '内部电池', power.charger_online ? '充电口已连接' : '当前由电池供电'));
  }

  function renderFans(fans) {
    const root = byId('thermal-fans');
    if (!root) return;
    empty(root);
    if (!fans || !fans.length) {
      root.appendChild(metric('风扇', '不可用', '未发现转速传感器'));
      return;
    }
    fans.forEach((fan) => {
      const rpm = finite(fan.rpm);
      root.appendChild(metric(fan.name || '散热风扇', rpm === null ? '—' : rpm.toFixed(0) + ' RPM',
        rpm === 0 ? '当前停转 · 由智能温控决定' : (fan.source || '硬件监控')));
    });
  }

  function render(data) {
    const cpu = data.cpu || {};
    const summary = data.temperature_summary || {};
    const power = data.power || {};
    const usage = finite(cpu.usage);
    const highest = finite(summary.highest_c);
    const watts = finite(power.battery_power_w);

    text(byId('thermal-cpu'), usage === null ? '—' : usage.toFixed(1) + '%');
    text(byId('thermal-load'), cpu.load && finite(cpu.load[0]) !== null ? '负载 ' + Number(cpu.load[0]).toFixed(2) : '负载 —');
    text(byId('thermal-core-count'), (cpu.online_cores || 0) + ' 个在线核心');
    text(byId('thermal-highest'), highest === null ? '—' : highest.toFixed(1) + '°');
    text(byId('thermal-average'), finite(summary.average_c) === null ? '平均温度 —' : '平均 ' + Number(summary.average_c).toFixed(1) + ' °C');
    text(byId('thermal-sensor-count'), (summary.count || 0) + ' 个有效温度读数');
    text(byId('thermal-power'), watts === null ? '—' : watts.toFixed(2) + ' W');
    text(byId('thermal-power-state'), powerStatus(power.battery_status));
    text(byId('thermal-power-detail'), valueOrDash(power.battery_voltage_v, 3, ' V') + ' · ' + valueOrDash(power.battery_capacity, 0, '%'));

    setRing('thermal-cpu-ring', usage, 100);
    setRing('thermal-temp-ring', highest, 100);
    setRing('thermal-power-ring', watts, 20);
    renderTemperatures(data.temperatures);
    renderCores(cpu);
    renderPolicies(cpu);
    renderPower(power);
    renderFans(data.fans);

    const stamp = finite(data.generated_at);
    text(byId('thermal-updated'), stamp === null ? '刚刚更新' : new Date(stamp * 1000).toLocaleTimeString([], {hour12: false}));
    text(byId('thermal-live-text'), '设备实时数据');
    const live = document.querySelector('.pcat-thermal-live');
    if (live) live.classList.remove('is-error');
    const error = byId('thermal-error');
    if (error) error.hidden = true;
  }

  async function refresh() {
    if (inFlight || document.hidden) return;
    inFlight = true;
    try {
      const response = await fetch('/api/v1/thermal.json', { credentials: 'same-origin', cache: 'no-store' });
      if (!response.ok) throw new Error('HTTP ' + response.status);
      const data = await response.json();
      if (!data || data.status !== 'ok') throw new Error('invalid response');
      render(data);
    } catch (error) {
      const notice = byId('thermal-error');
      if (notice) {
        notice.hidden = false;
        notice.textContent = '暂时无法读取设备监控数据，将自动重试。';
      }
      text(byId('thermal-live-text'), '数据暂不可用');
      const live = document.querySelector('.pcat-thermal-live');
      if (live) live.classList.add('is-error');
    } finally {
      inFlight = false;
    }
  }

  function start() {
    if (!byId('pcat-thermal-page')) return;
    refresh();
    timer = window.setInterval(refresh, 2000);
    document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(); });
    window.addEventListener('pagehide', () => { if (timer) window.clearInterval(timer); }, { once: true });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
}());
