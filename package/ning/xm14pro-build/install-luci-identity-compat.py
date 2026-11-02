#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Keep hardware identity out of the LuCI overview without changing ubus."""

from pathlib import Path
import re


top = Path(__file__).resolve().parents[3]
view = top / "feeds/luci/modules/luci-mod-status/htdocs/luci-static/resources/view/status/include/10_system.js"
source = view.read_text(encoding="utf-8")

kernel_code = (
    "\t\tvar kernelMatch = String(boardinfo.kernel || '').match(/^\\d+\\.\\d+\\.\\d+/);\n"
    "\t\tvar displayedKernel = kernelMatch ? 'Linux ' + kernelMatch[0] : _('unknown');\n\n"
    "\t\tvar fields = ["
)

if "var kernelMatch = String(boardinfo.kernel || '')" not in source:
    # Replace the older patch as a whole so its model and Android wording
    # cannot survive when this installer is rerun on an existing build tree.
    source, legacy_count = re.subn(
        r"\t\tvar target = L\.isObject\(boardinfo\.release\).*?\t\tvar fields = \[",
        lambda _: kernel_code,
        source,
        count=1,
        flags=re.DOTALL,
    )
    if not legacy_count:
        old = "\t\tvar fields = ["
        if source.count(old) != 1:
            raise SystemExit(f"refusing to patch changed LuCI system view: {view}")
        source = source.replace(old, kernel_code, 1)

source = source.replace("\t\t\t_('Model'),            boardinfo.model,\n", "")

target_row = "\t\t\t_('Target Platform'),  'nwrt/openwrt',\n"
for row in (
    "\t\t\t_('Target Platform'),  displayedTarget,\n",
    "\t\t\t_('Target Platform'),  (L.isObject(boardinfo.release) ? boardinfo.release.target : ''),\n",
):
    source = source.replace(row, target_row)
if target_row not in source:
    architecture_row = "\t\t\t_('Architecture'),     boardinfo.system,\n"
    if source.count(architecture_row) != 1:
        raise SystemExit(f"refusing to patch changed LuCI architecture row: {view}")
    source = source.replace(architecture_row, architecture_row + target_row, 1)

old_kernel = "\t\t\t_('Kernel Version'),   boardinfo.kernel,"
new_kernel = "\t\t\t_('Kernel Version'),   displayedKernel,"
if old_kernel in source:
    source = source.replace(old_kernel, new_kernel, 1)
elif new_kernel not in source:
    raise SystemExit(f"refusing to patch changed LuCI kernel row: {view}")

if source.count(target_row) != 1 or "_('Model')" in source:
    raise SystemExit(f"unexpected LuCI identity rows: {view}")

view.write_text(source, encoding="utf-8")
print("LuCI target label is nwrt/openwrt; board target and running kernel remain unchanged")
