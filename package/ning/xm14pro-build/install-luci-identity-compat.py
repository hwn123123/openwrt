#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Keep device identity out of the public LuCI overview."""

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

for row in (
    "\t\t\t_('Model'),            boardinfo.model,\n",
    "\t\t\t_('Target Platform'),  displayedTarget,\n",
    "\t\t\t_('Target Platform'),  (L.isObject(boardinfo.release) ? boardinfo.release.target : ''),\n",
):
    source = source.replace(row, "")

old_kernel = "\t\t\t_('Kernel Version'),   boardinfo.kernel,"
new_kernel = "\t\t\t_('Kernel Version'),   displayedKernel,"
if old_kernel in source:
    source = source.replace(old_kernel, new_kernel, 1)
elif new_kernel not in source:
    raise SystemExit(f"refusing to patch changed LuCI kernel row: {view}")

if "_('Target Platform')" in source or "_('Model')" in source:
    raise SystemExit(f"device identity row remains in LuCI system view: {view}")

view.write_text(source, encoding="utf-8")
print("removed LuCI device identity rows; running kernel version remains truthful")
