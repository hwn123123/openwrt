#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
SOURCE_DIR="$SCRIPT_DIR/target"
TARGET_DIR="$TOPDIR/target/linux/xm14pro"
MARKER="$TARGET_DIR/.managed-by-package-ning-xm14pro-platform"

[[ -f "$TOPDIR/rules.mk" ]] || { printf 'invalid OpenWrt tree: %s\n' "$TOPDIR" >&2; exit 2; }
[[ -f "$SOURCE_DIR/Makefile" ]] || { printf 'missing target template: %s\n' "$SOURCE_DIR" >&2; exit 2; }
[[ -f "$SOURCE_DIR/config-6.18.extra" ]] || { printf 'missing kernel config additions: %s\n' "$SOURCE_DIR/config-6.18.extra" >&2; exit 2; }
[[ -f "$TOPDIR/target/linux/armsr/armv8/config-6.18" ]] || {
    printf 'missing upstream armsr/armv8 kernel config\n' >&2
    exit 2
}

if [[ -e "$TARGET_DIR" && ! -f "$MARKER" ]]; then
    printf 'refusing to replace unmanaged target: %s\n' "$TARGET_DIR" >&2
    exit 3
fi

mkdir -p -- "$TARGET_DIR/armv8" "$TARGET_DIR/image" "$TARGET_DIR/base-files/etc"
install -m 0644 "$SOURCE_DIR/Makefile" "$TARGET_DIR/Makefile"
install -m 0644 "$SOURCE_DIR/armv8/target.mk" "$TARGET_DIR/armv8/target.mk"
install -m 0644 "$SOURCE_DIR/image/Makefile" "$TARGET_DIR/image/Makefile"
install -m 0644 "$SOURCE_DIR/base-files/etc/inittab" "$TARGET_DIR/base-files/etc/inittab"
install -m 0644 "$SOURCE_DIR/managed-marker" "$MARKER"

# Start from the current upstream ARM64 build-time configuration, then add only
# xm14pro-specific answers. The resulting OpenWrt kernel is a build dependency;
# the B-slot image continues to use the exact Xiaomi stock kernel.
rm -f -- "$TARGET_DIR/config-6.18"
install -m 0644 "$TOPDIR/target/linux/armsr/config-6.18" "$TARGET_DIR/config-6.18"
printf '\n' >> "$TARGET_DIR/config-6.18"
cat "$SOURCE_DIR/config-6.18.extra" >> "$TARGET_DIR/config-6.18"
rm -f -- "$TARGET_DIR/armv8/config-6.18"
# This kernel is only an OpenWrt build-system prerequisite and is never put on
# the phone. Follow the config belonging to the checked-out OpenWrt 6.18 tree
# so new Kconfig choices never become interactive; the deployed boot image
# remains the exact hash-gated Xiaomi 6.1.138 kernel.
install -m 0644 "$TOPDIR/target/linux/armsr/armv8/config-6.18" \
    "$TARGET_DIR/armv8/config-6.18"
ln -sfn -- ../armsr/patches-6.18 "$TARGET_DIR/patches-6.18"

printf 'installed managed OpenWrt target: xm14pro/armv8\n'
