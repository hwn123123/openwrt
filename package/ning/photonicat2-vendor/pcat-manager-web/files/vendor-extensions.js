(function () {
  'use strict';

  function addThermalNavigation() {
    if (document.querySelector('[data-pcat-thermal-nav]')) return;
    const active = location.pathname === '/thermal' || location.pathname.indexOf('/thermal/') === 0;
    const desktopMarkup = '<a href="/thermal" class="nav flex h-full items-center justify-center space-x-2' +
      (active ? ' nav-active' : '') + '" title="温度与性能">' +
      '<img src="/static/temperature.svg" class="max-w-6 max-h-6 w-auto h-auto" alt="温度"></a>';
    const mobileMarkup = '<a href="/thermal" class="nav flex items-center justify-center space-x-2 py-2' +
      (active ? ' nav-active' : '') + '" title="温度与性能">' +
      '<img src="/static/temperature.svg" class="max-w-6 max-h-6 w-auto h-auto" alt="温度"></a>';

    const desktop = document.querySelector('#nav-content ul');
    if (desktop) {
      const item = document.createElement('li');
      item.className = 'w-16 sm:w-24';
      item.style.height = '50px';
      item.dataset.pcatThermalNav = '1';
      item.innerHTML = desktopMarkup;
      desktop.appendChild(item);
    }
    const mobile = document.querySelector('#bottom-nav-content ul');
    if (mobile) {
      const item = document.createElement('li');
      item.className = 'w-24';
      item.dataset.pcatThermalNav = '1';
      item.innerHTML = mobileMarkup;
      mobile.appendChild(item);
    }
  }

  function addNavigation(items) {
    if (!items || !items.length || document.querySelector('[data-pcat-extensions-nav]'))
      return;

    const active = location.pathname === '/apps' || location.pathname.indexOf('/apps/') === 0;
    const markup = '<a href="/apps" class="nav flex items-center justify-center space-x-2 py-2' +
      (active ? ' nav-active' : '') + '" title="应用中心">' +
      '<img src="/static/apps.svg" class="max-w-6 max-h-6 w-auto h-auto" alt="应用"></a>';

    const desktop = document.querySelector('#nav-content ul');
    if (desktop) {
      const item = document.createElement('li');
      item.className = 'w-24';
      item.dataset.pcatExtensionsNav = '1';
      item.innerHTML = markup;
      desktop.appendChild(item);
    }

    const mobile = document.querySelector('#bottom-nav-content ul');
    if (mobile) {
      const item = document.createElement('li');
      item.className = 'w-24';
      item.dataset.pcatExtensionsNav = '1';
      item.innerHTML = markup;
      mobile.appendChild(item);
    }
  }

  function load() {
    addThermalNavigation();
    fetch('/api/v1/extensions.json', { credentials: 'same-origin' })
      .then((response) => response.ok ? response.json() : null)
      .then((data) => { if (data) addNavigation(data.extensions); })
      .catch(() => {});
  }

  if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', load);
  else
    load();
}());
