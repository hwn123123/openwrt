#!/system/bin/sh
set -eu

# AOSP fastbootd is entered by placing "boot-fastboot" in the bootloader
# message and booting recovery.  The NWRT selector does not understand that
# message itself, so hand this one mode back to the untouched PixelOS recovery
# binary.  Normal boots continue into the NWRT/OpenWrt selector.
case " $* " in
    *" --fastboot "*) exec /nwrt/bin/recovery.stock "$@" ;;
esac

if [ "$(getprop ro.boot.fastboot 2>/dev/null || true)" = 1 ]; then
    exec /nwrt/bin/recovery.stock "$@"
fi

misc=/dev/block/by-name/misc
if [ -b "$misc" ]; then
    if /system/bin/dd if="$misc" bs=2048 count=1 2>/dev/null |
        /system/bin/grep -a -q 'boot-fastboot'; then
        exec /nwrt/bin/recovery.stock "$@"
    fi
fi

exec /nwrt/bin/recovery.selector "$@"
