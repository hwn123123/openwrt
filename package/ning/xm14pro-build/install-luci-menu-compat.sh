#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
MENU="$TOPDIR/feeds/luci/protocols/luci-proto-modemmanager/root/usr/share/luci/menu.d/luci-proto-modemmanager.json"

# The Xiaomi mobile page uses its own menu and the stock NICM/DSI data path.
# Keep luci-proto-modemmanager installed for its protocol support, but omit its
# empty, duplicate status page from images built for this device.
[[ -d "$(dirname -- "$MENU")" ]] || {
    printf 'LuCI ModemManager feed is missing: %s\n' "$MENU" >&2
    exit 2
}
if [[ -f "$MENU" ]]; then
    python3 - "$MENU" <<'PY'
import json
import pathlib
import sys

menu = pathlib.Path(sys.argv[1])
entries = json.loads(menu.read_text(encoding="utf-8"))
if set(entries) != {"admin/status/modemmanager"}:
    raise SystemExit(f"refusing to remove a changed LuCI menu: {menu}")
menu.unlink()
print(f"removed unused LuCI status menu: {menu}")
PY
fi
