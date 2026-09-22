(function () {
  'use strict';

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
