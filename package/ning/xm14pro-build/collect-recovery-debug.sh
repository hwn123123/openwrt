#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Temporary development helper.  It does not modify Android or recovery.
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
PROJECT_DIR="$(cd -- "$SCRIPT_DIR/../../../../.." && pwd -P)"
STAMP="$(date +%Y%m%d-%H%M%S)"
OUTPUT_DIR="${1:-$PROJECT_DIR/实体机日志/rec-debug-$STAMP}"

command -v adb >/dev/null 2>&1 || {
    printf 'adb is required\n' >&2
    exit 2
}
ADB_STATE="$(adb get-state 2>/dev/null || true)"
[[ "$ADB_STATE" == recovery ]] || {
    printf 'device is not in adb recovery mode (state=%s)\n' "$ADB_STATE" >&2
    exit 2
}

mkdir -p -- "$OUTPUT_DIR"

adb shell 'getprop' > "$OUTPUT_DIR/recovery-getprop.txt"
adb shell 'cat /proc/cmdline; cat /proc/bootconfig 2>/dev/null || true' \
    > "$OUTPUT_DIR/recovery-boot-config.txt"
adb shell 'dmesg' > "$OUTPUT_DIR/recovery-dmesg.txt"
adb shell 'mount' > "$OUTPUT_DIR/recovery-mounts.txt"
adb shell 'ls -la /sys/fs/pstore 2>/dev/null || true' \
    > "$OUTPUT_DIR/pstore-list.txt"

adb shell '
mkdir -p /mnt/linux-root
mountpoint -q /mnt/linux-root 2>/dev/null || \
    mount -t ext4 -o ro /dev/block/by-name/linux_root /mnt/linux-root
true
'

adb shell 'find /mnt/linux-root/root/nwrt-logs -maxdepth 3 -type f -print 2>/dev/null || true' \
    > "$OUTPUT_DIR/persistent-file-list.txt"
adb shell 'tail -n 2000 /mnt/linux-root/root/nwrt-logs/boot-shim-last.log 2>/dev/null || true' \
    > "$OUTPUT_DIR/boot-shim-last.log"
adb shell 'cat /mnt/linux-root/root/nwrt-logs/pre-adsp-kmsg-last.log 2>/dev/null || true' \
    > "$OUTPUT_DIR/pre-adsp-kmsg-last.log"
adb shell 'cat /mnt/linux-root/root/nwrt-logs/adsp-kmsg-last.log 2>/dev/null || true' \
    > "$OUTPUT_DIR/adsp-kmsg-last.log"
adb shell 'dmesg | grep -Ei "panic|oops|watchdog|remoteproc|adsp|power_supply|usb|typec|xm14" || true' \
    > "$OUTPUT_DIR/recovery-kernel-focus.txt"
adb shell '
script=/mnt/linux-root/etc/init.d/xm14pro-stock-modules
if [ -f "$script" ]; then
    sha256sum "$script"
    sed -n "1,220p" "$script"
else
    echo "linux_root stock-module service is absent"
fi
' > "$OUTPUT_DIR/linux-root-stock-modules.txt"

adb shell 'for file in /sys/fs/pstore/*; do [ -f "$file" ] && echo "$file"; done; true' |
while IFS= read -r remote; do
    [[ -n "$remote" ]] || continue
    adb pull "$remote" "$OUTPUT_DIR/" >/dev/null
done

if adb shell 'test -b /dev/block/by-name/oops'; then
    adb exec-out 'dd if=/dev/block/by-name/oops bs=1048576 2>/dev/null' \
        > "$OUTPUT_DIR/oops.img"
fi

(
    cd "$OUTPUT_DIR"
    find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 |
        sort -z | xargs -0 shasum -a 256 > SHA256SUMS
)
printf 'recovery diagnostic bundle: %s\n' "$OUTPUT_DIR"
