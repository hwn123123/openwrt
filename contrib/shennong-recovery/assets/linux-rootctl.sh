#!/system/bin/sh
set -eu

disk=/dev/block/sda
root=/dev/block/by-name/linux_root
sgdisk=/nwrt/bin/sgdisk

verify_layout() {
    [ -b "$root" ] || return 1
    info=$($sgdisk --info=34 "$disk") || return 1
    echo "$info" | grep -F "Partition unique GUID: C675F892-57A0-4776-A8FD-17C15F0AD78A" >/dev/null
    echo "$info" | grep -F "First sector: 76197882 " >/dev/null
    echo "$info" | grep -F "Last sector: 128626681 " >/dev/null
    echo "$info" | grep -F "Partition name: 'linux_root'" >/dev/null
}

case "${1:-status}" in
    status)
        verify_layout
        $sgdisk --info=34 "$disk"
        /system/bin/blkid "$root" 2>/dev/null || true
        ;;
    prepare-ext4)
        [ "${2:-}" = ERASE-LINUX-ROOT ] || {
            echo "usage: $0 prepare-ext4 ERASE-LINUX-ROOT" >&2
            exit 2
        }
        verify_layout
        if grep -qE '(^| )(/dev/block/by-name/linux_root|/dev/block/sda34)( |$)' /proc/mounts; then
            echo "linux_root is mounted; refusing format" >&2
            exit 3
        fi
        /system/bin/mke2fs -t ext4 -L linux_root -m 0 -I 256 -F "$root"
        /system/bin/e2fsck -f -y "$root"
        ;;
    *)
        echo "usage: $0 {status|prepare-ext4 ERASE-LINUX-ROOT}" >&2
        exit 2
        ;;
esac
