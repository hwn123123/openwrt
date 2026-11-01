#!/system/bin/sh
set -eu

bootctl=/nwrt/bin/bootctl
log=/multiboot-slot.log

exec >>"$log" 2>&1
echo "slot-a: begin"

if [ ! -x "$bootctl" ]; then
    echo "slot-a: bootctl is missing"
    exit 70
fi

attempt=0
while ! "$bootctl" get-current-slot >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 20 ]; then
        echo "slot-a: boot HAL is unavailable"
        exit 71
    fi
    sleep 1
done

"$bootctl" set-active-boot-slot 0
if [ "$("$bootctl" is-slot-bootable 0)" != 1 ]; then
    echo "slot-a: slot A is not bootable after selection"
    exit 72
fi

sync
echo "slot-a: selected"
if ! /nwrt/bin/xm14-clear-recovery-request; then
    echo "slot-a: failed to clear recovery request"
    exit 73
fi
if ! setprop sys.powerctl reboot; then
    echo "slot-a: reboot request was rejected" >&2
    exit 74
fi
echo "slot-a: reboot requested"
exit 0
