(function () {
  "use strict";

  const pages = [
    { path: "/device_control", label: "设备控制", root: '[x-data="adv_settings"]' },
    { path: "/general_settings", label: "通用设置", root: '[x-data="general_settings"]' },
    { path: "/settings", label: "Wi-Fi", root: '#pcat-wifi-page' },
    { path: "/timer", label: "开关机策略", root: '[x-data="timer_settings"]' },
    { path: "/sound_events", label: "蜂鸣器控制", root: '[data-settings-page="sound_events"]' }
  ];

  function currentPath() {
    return location.pathname.replace(/\/+$/, "") || "/";
  }

  function hideNativeHeading(root) {
    const title = root.querySelector(":scope > h1") ||
      root.querySelector(":scope > div:first-child h1");
    if (!title) return null;
    const heading = title.parentElement !== root ? title.parentElement : title;
    heading.hidden = true;
    heading.classList.add("pcat-native-settings-heading");
    return heading;
  }

  function hideNativeNavigation(root, path) {
    const firstLink = root.querySelector('a[href="/device_control"]');
    const list = firstLink && firstLink.closest("ul");
    if (!list || !list.querySelector('a[href="/general_settings"]') ||
        !list.querySelector('a[href="/settings"]')) return;

    const strip = list.parentElement;
    strip.hidden = true;
    strip.classList.add("pcat-native-settings-nav");

    if (path === "/timer") return;
    let owner = strip;
    while (owner.parentElement && owner.parentElement !== root) owner = owner.parentElement;
    if (owner.parentElement === root) {
      owner.hidden = true;
      owner.classList.add("pcat-native-settings-nav-shell");
    }
  }

  function buildNavigation(current) {
    const shell = document.createElement("section");
    shell.className = "pcat-cell-center pcat-settings-shell";
    shell.innerHTML =
      '<nav class="pcat-cell-subnav pcat-settings-subnav" aria-label="系统设置二级菜单">' +
        '<button type="button" class="pcat-cell-menu-toggle" data-settings-menu-toggle aria-expanded="false">' +
          '<span aria-hidden="true">☰</span><small>系统设置</small><strong>' + current.label + '</strong>' +
        '</button>' +
        '<div class="pcat-cell-nav-list" data-settings-nav-list>' +
          pages.map(function (page) {
            return '<button type="button" data-settings-path="' + page.path + '"' +
              (page.path === current.path ? ' class="is-active" aria-current="page"' : "") +
              '>' + page.label + '</button>';
          }).join("") +
        '</div>' +
        '<button type="button" class="pcat-cell-refresh" data-settings-refresh title="刷新当前设置">' +
          '<span aria-hidden="true">↻</span><span>刷新</span>' +
        '</button>' +
      '</nav>';
    return shell;
  }

  function mount() {
    const path = currentPath();
    const current = pages.find(function (page) { return page.path === path; });
    if (!current) return;

    const root = document.querySelector(current.root);
    if (!root || root.querySelector(":scope > .pcat-settings-shell")) return;

    document.documentElement.classList.add("pcat-black-theme");
    document.body.classList.add("pcat-modern-settings");
    root.classList.add("pcat-settings-root");

    const heading = hideNativeHeading(root);
    hideNativeNavigation(root, path);
    const shell = buildNavigation(current);
    if (heading && heading.parentElement === root) heading.insertAdjacentElement("afterend", shell);
    else root.insertBefore(shell, root.firstChild);

    const toggle = shell.querySelector("[data-settings-menu-toggle]");
    toggle.addEventListener("click", function () {
      const open = shell.classList.toggle("is-menu-open");
      toggle.setAttribute("aria-expanded", String(open));
    });

    shell.querySelector("[data-settings-nav-list]").addEventListener("click", function (event) {
      const button = event.target.closest("[data-settings-path]");
      if (!button) return;
      shell.classList.remove("is-menu-open");
      toggle.setAttribute("aria-expanded", "false");
      if (button.dataset.settingsPath !== path) location.assign(button.dataset.settingsPath);
    });

    shell.querySelector("[data-settings-refresh]").addEventListener("click", function () {
      location.reload();
    });

    document.addEventListener("click", function (event) {
      if (shell.contains(event.target)) return;
      shell.classList.remove("is-menu-open");
      toggle.setAttribute("aria-expanded", "false");
    });
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", mount);
  else mount();
}());
