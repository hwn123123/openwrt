#!/system/bin/sh
set -eu

root=/dev/block/by-name/linux_root
expected_size=214748364800
bootctl=/nwrt/bin/bootctl
log=/multiboot-slot.log

exec >>"$log" 2>&1
echo "slot-b: begin"

if [ ! -b "$root" ]; then
    echo "linux_root is missing; refusing slot B" >&2
    exit 64
fi
size=$(/system/bin/blockdev --getsize64 "$root" 2>/dev/null || true)
if [ "$size" != "$expected_size" ]; then
    echo "linux_root has unexpected size: $size; refusing slot B" >&2
    exit 65
fi
if ! /system/bin/tune2fs -l "$root" 2>/dev/null |
    /system/bin/grep -Eq '^Filesystem magic number:[[:space:]]+0xEF53$'; then
    echo "linux_root is not an ext4 filesystem; refusing slot B" >&2
    exit 65
fi

if [ ! -x "$bootctl" ]; then
    echo "bootctl is missing; refusing slot B" >&2
    exit 66
fi

attempt=0
while ! "$bootctl" get-current-slot >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 20 ]; then
        echo "boot HAL is unavailable; refusing slot B" >&2
        exit 67
    fi
    sleep 1
done

"$bootctl" set-active-boot-slot 1
if [ "$("$bootctl" is-slot-bootable 1)" != 1 ]; then
    echo "slot B is not bootable after selection" >&2
    exit 68
fi

sync
echo "slot-b: selected"
if ! /nwrt/bin/xm14-clear-recovery-request; then
    echo "slot-b: failed to clear recovery request" >&2
    exit 69
fi
if ! setprop sys.powerctl reboot; then
    echo "slot-b: reboot request was rejected" >&2
    exit 70
fi
echo "slot-b: reboot requested"
exit 0
