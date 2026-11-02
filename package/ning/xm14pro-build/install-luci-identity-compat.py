#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Use a friendly NWRT platform name in LuCI without changing board identity."""

from pathlib import Path


top = Path(__file__).resolve().parents[3]
view = top / "feeds/luci/modules/luci-mod-status/htdocs/luci-static/resources/view/status/include/10_system.js"
source = view.read_text(encoding="utf-8")


def patch(old: str, new: str, marker: str = "") -> None:
    global source
    if (marker or new) in source:
        return
    if source.count(old) != 1:
        raise SystemExit(f"refusing to patch changed LuCI system view: {view}")
    source = source.replace(old, new, 1)


patch(
    "\t\tvar fields = [",
    "\t\tvar target = L.isObject(boardinfo.release) ? boardinfo.release.target : '';\n"
    "\t\tvar displayedTarget = target === 'xm14pro/armv8' ? 'NWRT · Xiaomi 14 Pro' : target;\n\n"
    "\t\tvar fields = [",
    "var displayedTarget = target === 'xm14pro/armv8'",
)
patch(
    "\t\tvar fields = [",
    "\t\tvar stockKernel = target === 'xm14pro/armv8' && /^6\\.1\\.138-android14-/.test(boardinfo.kernel || '');\n"
    "\t\tvar displayedKernel = stockKernel ? E('details', { 'title': boardinfo.kernel }, [\n"
    "\t\t\tE('summary', {}, '6.1.138 · 小米原厂内核（NWRT 适配）'),\n"
    "\t\t\tE('code', {}, boardinfo.kernel)\n"
    "\t\t]) : boardinfo.kernel;\n\n"
    "\t\tvar fields = [",
    "var displayedKernel = stockKernel",
)
patch(
    "\t\t\t_('Target Platform'),  (L.isObject(boardinfo.release) ? boardinfo.release.target : ''),",
    "\t\t\t_('Target Platform'),  displayedTarget,",
)
patch(
    "\t\t\t_('Kernel Version'),   boardinfo.kernel,",
    "\t\t\t_('Kernel Version'),   displayedKernel,",
)

view.write_text(source, encoding="utf-8")
print("installed LuCI NWRT platform label; board target and running kernel remain unchanged")
