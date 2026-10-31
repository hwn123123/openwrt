#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
PATCH_FILE="$SCRIPT_DIR/patches/openclash/0001-xm14pro-use-stock-kernel-network-stack.patch"
OPENCLASH_MAKEFILE="$TOPDIR/feeds/openclash/luci-app-openclash/Makefile"

[[ -f "$OPENCLASH_MAKEFILE" ]] || {
    printf 'OpenClash feed is not installed: %s\n' "$OPENCLASH_MAKEFILE" >&2
    exit 2
}
[[ -f "$PATCH_FILE" ]] || {
    printf 'missing OpenClash Xiaomi compatibility patch: %s\n' "$PATCH_FILE" >&2
    exit 2
}

# OpenClash normally selects OpenWrt-built netfilter/TUN modules.  The phone
# boots Xiaomi's exact 6.1.138 kernel and already provides those facilities;
# loading OpenWrt 6.18 modules would be an ABI error.  Keep all upstream
# dependencies on normal targets and suppress only those module selectors on
# xm14pro.
if ! grep -q '+!TARGET_xm14pro:kmod-tun' "$OPENCLASH_MAKEFILE"; then
    patch -d "$TOPDIR" -p1 --forward < "$PATCH_FILE"
fi

grep -q 'default y if PACKAGE_$(PKG_NAME) && ! TARGET_xm14pro' "$OPENCLASH_MAKEFILE" || {
    printf 'OpenClash xm14pro Kconfig compatibility was not applied\n' >&2
    exit 3
}
grep -q '+!TARGET_xm14pro:kmod-tun' "$OPENCLASH_MAKEFILE" || {
    printf 'OpenClash xm14pro TUN dependency compatibility was not applied\n' >&2
    exit 3
}

printf 'installed OpenClash compatibility for Xiaomi stock kernel\n'
