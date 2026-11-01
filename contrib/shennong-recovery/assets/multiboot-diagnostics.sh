#!/system/bin/sh
set -u

sleep 4
log=/linux-multiboot-diagnostics.log
umask 022

{
    echo "linux-multiboot diagnostics v1.5"
    date
    id
    getenforce
    echo "slot_suffix=$(getprop ro.boot.slot_suffix)"
    echo "boot_hal=$(getprop init.svc.vendor.boot-qti)"
    echo "usb_state=$(getprop sys.usb.state)"
    echo "--- fstab ---"
    ls -lZ /fstab.default /fstab.qcom /first_stage_ramdisk/fstab.qcom 2>&1
    echo "--- bootctl ---"
    /nwrt/bin/bootctl get-current-slot
    /nwrt/bin/bootctl is-slot-bootable 0
    /nwrt/bin/bootctl is-slot-bootable 1
    echo "--- partitions ---"
    ls -lZ /dev/block/by-name/misc /dev/block/by-name/linux_root 2>&1
    echo "--- processes ---"
    ps -AZ | grep -E 'boot-service|servicemanager|recovery|adbd' || true
    echo "--- kernel tail ---"
    dmesg | tail -n 300
} >"$log" 2>&1

chmod 0644 "$log"
