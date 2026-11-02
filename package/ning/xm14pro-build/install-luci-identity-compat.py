#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Use a friendly NWRT platform name in LuCI without changing board identity."""

from pathlib import Path


top = Path(__file__).resolve().parents[3]
view = top / "feeds/luci/modules/luci-mod-status/htdocs/luci-static/resources/view/status/include/10_system.js"
source = view.read_text(encoding="utf-8")


def patch(old: str, new: str) -> None:
    global source
    if new in source:
        return
    if source.count(old) != 1:
        raise SystemExit(f"refusing to patch changed LuCI system view: {view}")
    source = source.replace(old, new, 1)


patch(
    "\t\tvar fields = [",
    "\t\tvar target = L.isObject(boardinfo.release) ? boardinfo.release.target : '';\n"
    "\t\tvar displayedTarget = target === 'xm14pro/armv8' ? 'NWRT · Xiaomi 14 Pro' : target;\n\n"
    "\t\tvar fields = [",
)
patch(
    "\t\t\t_('Target Platform'),  (L.isObject(boardinfo.release) ? boardinfo.release.target : ''),",
    "\t\t\t_('Target Platform'),  displayedTarget,",
)

view.write_text(source, encoding="utf-8")
print("installed LuCI NWRT platform label; board target and running kernel remain unchanged")
