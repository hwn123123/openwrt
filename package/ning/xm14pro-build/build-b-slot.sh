#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
ROOTFS_DIR="${1:-$TOPDIR/bin/targets/shennong/rootfs}"
OUTPUT_DIR="${XM14_BOOT_CHAIN_OUTPUT_DIR:-$TOPDIR/bin/targets/shennong/boot-chain}"
INPUT_DIR="${XM14_BOOT_CHAIN_INPUT_DIR:-/home/ning/xiangmu/xm14-input}"
TOOLS_DIR="${XM14_RECOVERY_TOOLS_DIR:-$TOPDIR/dl/xm14pro-recovery-tools}"
MAGISKBOOT="${MAGISKBOOT:-$TOOLS_DIR/magiskboot}"
MAGISK_APK="${MAGISK_APK:-$TOOLS_DIR/Magisk-v30.7.apk}"
MKBOOTIMG_DIR="$TOOLS_DIR/mkbootimg"
AVBTOOL="$TOOLS_DIR/avb/avbtool.py"
WIFI_CONFIG="$SCRIPT_DIR/files/kiwi_v2/WCNSS_qcom_cfg.ini"
VENDOR_MODULE_SOURCE="${XM14_VENDOR_MODULE_SOURCE:-$INPUT_DIR/vendor-modules}"

BOOT_SIZE=100663296
INIT_BOOT_SIZE=8388608
VENDOR_BOOT_SIZE=100663296
DTBO_SIZE=25165824
DTBO_ORIGINAL_SIZE=717291
VBMETA_SIZE=131072
AVB_SALT='ab614a37b99ce6eef3903ef481d5c6af42a539f4e6dfa631979c7eacf39b609a'
BOOT_A_SHA256='1dab6b5799ed7db7e84648e761981b2001e5b5b22dee1fac040a97b2c337e923'
INIT_BOOT_A_SHA256='d0bb4390db0bcc54014e985743076a01ba896d43d66d1d5921e10cafff3f16da'
VENDOR_BOOT_A_SHA256='07ec90a770c41ea5a0a7a03698c26773a15426dcf87fd4b2c4eac350f1ec7acc'
DTBO_A_SHA256='2300821865c3dff04c2e25f0ec0cd6bc460c03e1176202ad2b87623a92c6eaf1'
MAGISK_APK_SHA256='e0d32d2123532860f97123d927b1bb86c4e08e6fd8a48bfc6b5bee0afae9ebd5'
RESCUE_BUSYBOX_SHA256='4d60ab3f5a59ebb2ca863f2f514e6924401b581e9b64f602665c008177626651'

BOOT_A="$INPUT_DIR/boot_a.img"
INIT_BOOT_A="$INPUT_DIR/init_boot_a.img"
VENDOR_BOOT_A="$INPUT_DIR/vendor_boot_a.img"
DTBO_A="$INPUT_DIR/dtbo_a.img"

die() { printf 'xm14 B-slot packer: %s\n' "$*" >&2; exit 2; }

require_hash() {
    local path="$1" expected="$2"
    [[ -f "$path" ]] || die "missing input: $path"
    [[ "$(sha256sum "$path" | awk '{print $1}')" == "$expected" ]] ||
        die "hash mismatch: $path"
}

require_size() {
    local path="$1" expected="$2"
    [[ "$(stat -c %s "$path")" -eq "$expected" ]] ||
        die "partition image has wrong size: $path"
}

require_hash "$BOOT_A" "$BOOT_A_SHA256"
require_hash "$INIT_BOOT_A" "$INIT_BOOT_A_SHA256"
require_hash "$VENDOR_BOOT_A" "$VENDOR_BOOT_A_SHA256"
require_hash "$DTBO_A" "$DTBO_A_SHA256"
require_size "$BOOT_A" "$BOOT_SIZE"
require_size "$INIT_BOOT_A" "$INIT_BOOT_SIZE"
require_size "$VENDOR_BOOT_A" "$VENDOR_BOOT_SIZE"
require_size "$DTBO_A" "$DTBO_SIZE"
require_hash "$MAGISK_APK" "$MAGISK_APK_SHA256"
[[ -x "$MAGISKBOOT" ]] || die "missing magiskboot: $MAGISKBOOT"
[[ -f "$MKBOOTIMG_DIR/mkbootimg.py" ]] || die "missing mkbootimg.py"
[[ -f "$MKBOOTIMG_DIR/unpack_bootimg.py" ]] || die "missing unpack_bootimg.py"
[[ -f "$AVBTOOL" ]] || die "missing avbtool.py"
[[ -f "$WIFI_CONFIG" ]] || die "missing Xiaomi kiwi_v2 Wi-Fi configuration"

mapfile -t rootfs_tarballs < <(find "$ROOTFS_DIR" -maxdepth 1 -type f -name '*rootfs.tar.gz' -print)
mapfile -t linux_root_images < <(find "$ROOTFS_DIR" -maxdepth 1 -type f -name 'nwrt-linux-root-8GiB.sparse.img' -print)
(( ${#rootfs_tarballs[@]} == 1 )) || die "expected exactly one rootfs.tar.gz in $ROOTFS_DIR"
(( ${#linux_root_images[@]} == 1 )) || die "expected exactly one linux_root sparse image in $ROOTFS_DIR"
ROOTFS_TARBALL="${rootfs_tarballs[0]}"
LINUX_ROOT_IMAGE="${linux_root_images[0]}"

tar -tzf "$ROOTFS_TARBALL" | grep -E '^\.?/?sbin/init$' >/dev/null ||
    die "persistent rootfs lacks /sbin/init"
simg_dump "$LINUX_ROOT_IMAGE" >/dev/null ||
    die "persistent rootfs image is not a valid Android sparse image"
if [[ -n "${XM14_BOOT_SHIM:-}" ]]; then
    SHIM="$XM14_BOOT_SHIM"
else
    # Isolated boot-shim builds do not refresh the copy cached beside a prior
    # full rootfs. Prefer the just-compiled build_dir artifact so a B-slot
    # repack can never silently reuse stale PID-1 code.
    SHIM="$(find "$TOPDIR/build_dir/target-aarch64_generic_musl" -type f \
        -path '*/xm14pro-boot-shim-*/xm14pro-boot-shim' -perm -111 -print | sort -V | tail -n 1)"
    if [[ -z "$SHIM" || ! -x "$SHIM" ]]; then
        SHIM="$ROOTFS_DIR/xm14pro-boot-shim"
    fi
fi
[[ -n "$SHIM" && -f "$SHIM" ]] || die "compiled xm14pro-boot-shim not found"
file "$SHIM" | grep -F 'ARM aarch64' >/dev/null || die "boot-shim is not AArch64"
file "$SHIM" | grep -F 'statically linked' >/dev/null || die "boot-shim is not static"

if [[ -n "${XM14_EARLY_SPLASH:-}" ]]; then
    EARLY_SPLASH="$XM14_EARLY_SPLASH"
else
    EARLY_SPLASH="$(find "$TOPDIR/build_dir/target-aarch64_generic_musl" -type f \
        -path '*/xm14pro-display-*/xm14pro-early-splash' -perm -111 -print | \
        sort -V | tail -n 1)"
    if [[ -z "$EARLY_SPLASH" || ! -x "$EARLY_SPLASH" ]]; then
        EARLY_SPLASH="$ROOTFS_DIR/xm14pro-early-splash"
    fi
fi
[[ -n "$EARLY_SPLASH" && -f "$EARLY_SPLASH" ]] ||
    die "compiled xm14pro-early-splash not found"
file "$EARLY_SPLASH" | grep -F 'ARM aarch64' >/dev/null ||
    die "early splash is not AArch64"
file "$EARLY_SPLASH" | grep -F 'statically linked' >/dev/null ||
    die "early splash is not static"

WORK_DIR="$(mktemp -d /tmp/xm14-b-slot.XXXXXX)"
cleanup() {
    if [[ "$WORK_DIR" == /tmp/xm14-b-slot.* && -d "$WORK_DIR" && ! -L "$WORK_DIR" ]]; then
        find "$WORK_DIR" -mindepth 1 -delete 2>/dev/null || true
        rmdir "$WORK_DIR" 2>/dev/null || true
    fi
}
trap cleanup EXIT

INIT_ROOT="$WORK_DIR/init-root"
RESCUE_OUTER="$WORK_DIR/rescue-outer"
VENDOR_UNPACK="$WORK_DIR/vendor-unpack"
mkdir -p -- "$INIT_ROOT" "$RESCUE_OUTER/nwrt-rescue" "$VENDOR_UNPACK"
install -m 0755 "$SHIM" "$INIT_ROOT/init"
install -m 0755 "$EARLY_SPLASH" "$INIT_ROOT/early-splash"
mkdir -p -- "$INIT_ROOT/dev" "$INIT_ROOT/proc" "$INIT_ROOT/sys" "$INIT_ROOT/run" \
    "$INIT_ROOT/mnt" "$INIT_ROOT/newroot" "$INIT_ROOT/rom"

# The stock WLAN module asks the kernel firmware loader for this configuration
# before the persistent nwrt root has been entered. Start from the exact
# OS3.0.308.0 file and disable AP bus suspend for sustained CPE throughput;
# firmware binaries remain on the original read-only modem partition.
mkdir -p -- "$RESCUE_OUTER/vendor/firmware/wlan/qca_cld/kiwi_v2"
install -m 0644 "$WIFI_CONFIG" \
    "$RESCUE_OUTER/vendor/firmware/wlan/qca_cld/kiwi_v2/WCNSS_qcom_cfg.ini"

# The B slot must remain a boot chain, so its rescue fragment is deliberately
# bounded and never contains the full OpenWrt rootfs. The static Magisk BusyBox
# is a local, exact-hash-gated build input and provides only an emergency shell.
RESCUE_ROOT="$RESCUE_OUTER/nwrt-rescue"
mkdir -p -- "$RESCUE_ROOT/bin" "$RESCUE_ROOT/sbin" "$RESCUE_ROOT/dev" \
    "$RESCUE_ROOT/proc" "$RESCUE_ROOT/sys" "$RESCUE_ROOT/run" "$RESCUE_ROOT/tmp"
unzip -p "$MAGISK_APK" lib/arm64-v8a/libbusybox.so > "$RESCUE_ROOT/bin/busybox"
chmod 0755 "$RESCUE_ROOT/bin/busybox"
install -m 0755 "$SHIM" "$RESCUE_ROOT/bin/xm14pro-boot-shim"
[[ "$(sha256sum "$RESCUE_ROOT/bin/busybox" | awk '{print $1}')" == \
    "$RESCUE_BUSYBOX_SHA256" ]] || die "rescue BusyBox hash mismatch"
file "$RESCUE_ROOT/bin/busybox" | grep -F 'ARM aarch64' >/dev/null ||
    die "rescue BusyBox is not AArch64"
file "$RESCUE_ROOT/bin/busybox" | grep -F 'statically linked' >/dev/null ||
    die "rescue BusyBox is not static"
ln -s busybox "$RESCUE_ROOT/bin/sh"
cat > "$RESCUE_ROOT/sbin/init" <<'EOF'
#!/bin/busybox sh
export PATH=/bin:/sbin
exec </dev/console >/dev/console 2>&1
echo "Xiaomi 14 Pro nwrt rescue shell"
echo "Persistent nwrt rootfs was not usable; storage was not formatted."
echo "Returning to Fastboot automatically in 10 seconds."
/bin/busybox sleep 10
/bin/xm14pro-boot-shim --reboot-bootloader
echo "Automatic Fastboot return failed; starting the rescue shell."
while true; do
    /bin/busybox sh
    /bin/busybox sleep 1
done
EOF
chmod 0755 "$RESCUE_ROOT/sbin/init"

# cpio preserves mtimes. Normalize the generated ramdisk trees so rebuilding
# the same source produces byte-identical init_boot and vendor_boot images.
source_epoch="${SOURCE_DATE_EPOCH:-$(git -c safe.directory="$TOPDIR" -C "$TOPDIR" show -s --format=%ct HEAD)}"
[[ "$source_epoch" =~ ^[0-9]+$ ]] || die "invalid SOURCE_DATE_EPOCH: $source_epoch"
find "$INIT_ROOT" "$RESCUE_OUTER" -exec \
    touch -h -d "@$source_epoch" -- {} +

(
    cd "$INIT_ROOT"
    find . -xdev -print0 | sort -z |
        cpio --null --create --format=newc --owner=0:0 --reproducible \
        > "$WORK_DIR/init.cpio"
)
(
    cd "$RESCUE_OUTER"
    find . -xdev -print0 | sort -z |
        cpio --null --create --format=newc --owner=0:0 --reproducible \
        > "$WORK_DIR/rescue.cpio"
)
"$MAGISKBOOT" compress=lz4_legacy "$WORK_DIR/init.cpio" "$WORK_DIR/init.cpio.lz4"
"$MAGISKBOOT" compress=lz4_legacy "$WORK_DIR/rescue.cpio" "$WORK_DIR/rescue.cpio.lz4"

touch "$WORK_DIR/empty-kernel"
python3 "$MKBOOTIMG_DIR/mkbootimg.py" \
    --header_version 4 \
    --kernel "$WORK_DIR/empty-kernel" \
    --ramdisk "$WORK_DIR/init.cpio.lz4" \
    --cmdline '' \
    --output "$WORK_DIR/init_boot_b.raw.img"
cp -- "$WORK_DIR/init_boot_b.raw.img" "$WORK_DIR/init_boot_b.img"
python3 "$AVBTOOL" add_hash_footer \
    --image "$WORK_DIR/init_boot_b.img" \
    --partition_size "$INIT_BOOT_SIZE" \
    --partition_name init_boot \
    --hash_algorithm sha256 \
    --salt "$AVB_SALT" \
    --algorithm NONE \
    --append_to_release_string ' xm14pro-nwrt-b-slot'

mapfile -d '' -t vendor_args < <(
    python3 "$MKBOOTIMG_DIR/unpack_bootimg.py" \
        --boot_img "$VENDOR_BOOT_A" --out "$VENDOR_UNPACK" \
        --format=mkbootimg -0
)
python3 "$MKBOOTIMG_DIR/mkbootimg.py" \
    "${vendor_args[@]}" \
    --ramdisk_type platform \
    --ramdisk_name nwrt-rescue \
    --vendor_ramdisk_fragment "$WORK_DIR/rescue.cpio.lz4" \
    --vendor_boot "$WORK_DIR/vendor_boot_b.raw.img"
cp -- "$WORK_DIR/vendor_boot_b.raw.img" "$WORK_DIR/vendor_boot_b.img"
python3 "$AVBTOOL" add_hash_footer \
    --image "$WORK_DIR/vendor_boot_b.img" \
    --partition_size "$VENDOR_BOOT_SIZE" \
    --partition_name vendor_boot \
    --hash_algorithm sha256 \
    --salt "$AVB_SALT" \
    --algorithm NONE \
    --append_to_release_string ' xm14pro-nwrt-b-slot'

# Build an AVB descriptor source for the unchanged stock DTBO. The stock top
# level vbmeta records only the meaningful 717291-byte payload, not the padded
# partition image.
head -c "$DTBO_ORIGINAL_SIZE" "$DTBO_A" > "$WORK_DIR/dtbo_descriptor.img"
python3 "$AVBTOOL" add_hash_footer \
    --image "$WORK_DIR/dtbo_descriptor.img" \
    --partition_size "$DTBO_SIZE" \
    --partition_name dtbo \
    --hash_algorithm sha256 \
    --salt "$AVB_SALT" \
    --algorithm NONE

# This Linux slot must not depend on Android recovery, vbmeta_system or dynamic
# B-slot partitions. Keep only the four images the bootloader needs before PID
# 1 starts, while disabling verification on this unlocked development device.
python3 "$AVBTOOL" make_vbmeta_image \
    --output "$WORK_DIR/vbmeta_b.img" \
    --algorithm NONE \
    --flags 3 \
    --padding_size "$VBMETA_SIZE" \
    --include_descriptors_from_image "$BOOT_A" \
    --include_descriptors_from_image "$WORK_DIR/init_boot_b.img" \
    --include_descriptors_from_image "$WORK_DIR/vendor_boot_b.img" \
    --include_descriptors_from_image "$WORK_DIR/dtbo_descriptor.img" \
    --append_to_release_string ' xm14pro-nwrt-minimal-b-slot'

mkdir -p -- "$OUTPUT_DIR"
find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -delete
install -m 0644 "$BOOT_A" "$OUTPUT_DIR/boot_b.img"
install -m 0644 "$WORK_DIR/init_boot_b.img" "$OUTPUT_DIR/init_boot_b.img"
install -m 0644 "$WORK_DIR/vendor_boot_b.img" "$OUTPUT_DIR/vendor_boot_b.img"
install -m 0644 "$DTBO_A" "$OUTPUT_DIR/dtbo_b.img"
install -m 0644 "$WORK_DIR/vbmeta_b.img" "$OUTPUT_DIR/vbmeta_b.img"
install -m 0644 "$LINUX_ROOT_IMAGE" "$OUTPUT_DIR/nwrt-linux-root-8GiB.sparse.img"
install -m 0755 "$SHIM" "$OUTPUT_DIR/xm14pro-boot-shim"
install -m 0755 "$EARLY_SPLASH" "$OUTPUT_DIR/xm14pro-early-splash"

require_size "$OUTPUT_DIR/boot_b.img" "$BOOT_SIZE"
require_size "$OUTPUT_DIR/init_boot_b.img" "$INIT_BOOT_SIZE"
require_size "$OUTPUT_DIR/vendor_boot_b.img" "$VENDOR_BOOT_SIZE"
require_size "$OUTPUT_DIR/dtbo_b.img" "$DTBO_SIZE"
require_size "$OUTPUT_DIR/vbmeta_b.img" "$VBMETA_SIZE"
ln -s -- init_boot_b.img "$WORK_DIR/init_boot.img"
ln -s -- vendor_boot_b.img "$WORK_DIR/vendor_boot.img"
python3 "$AVBTOOL" verify_image --image "$WORK_DIR/init_boot.img" >/dev/null
python3 "$AVBTOOL" verify_image --image "$WORK_DIR/vendor_boot.img" >/dev/null
python3 "$AVBTOOL" info_image --image "$OUTPUT_DIR/vbmeta_b.img" |
    grep -E '^Flags:[[:space:]]+3$' >/dev/null || die "vbmeta_b verification flags are not 3"
vbmeta_info="$(python3 "$AVBTOOL" info_image --image "$OUTPUT_DIR/vbmeta_b.img")"
if grep -E 'Chain Partition descriptor|Hashtree descriptor' <<< "$vbmeta_info" >/dev/null; then
    die "vbmeta_b unexpectedly depends on Android partitions"
fi
mapfile -t vbmeta_partitions < <(
    sed -n 's/^[[:space:]]*Partition Name:[[:space:]]*//p' <<< "$vbmeta_info" | sort
)
[[ "${vbmeta_partitions[*]}" == 'boot dtbo init_boot vendor_boot' ]] ||
    die "vbmeta_b does not contain the exact minimal boot descriptors"

commit="$(git -c safe.directory="$TOPDIR" -C "$TOPDIR" rev-parse HEAD)"
{
    printf 'device=shennong\n'
    printf 'artifact_kind=nwrt-b-slot-production\n'
    printf 'release_status=production\n'
    printf 'flashable=yes\n'
    printf 'source_commit=%s\n' "$commit"
    printf 'boot_b_source=exact-stock-boot_a\n'
    printf 'dtbo_b_source=exact-stock-dtbo_a\n'
    printf 'init_boot_b=static-xm14pro-boot-shim-plus-fail-open-drm-splash\n'
    printf 'boot_shim_version=0.25-linux-root-ext4-size-guard\n'
    printf 'boot_shim_sha256=%s\n' "$(sha256sum "$SHIM" | awk '{print $1}')"
    printf 'early_splash_sha256=%s\n' "$(sha256sum "$EARLY_SPLASH" | awk '{print $1}')"
    printf 'vendor_boot_b=stock-platform-ramdisk-plus-bounded-static-rescue-fragment\n'
    printf 'rescue_rootfs=exact-hash-magisk-busybox-emergency-shell\n'
    printf 'vbmeta_b=minimal-boot-descriptors-algorithm-none-flags-3-unlocked-device-only\n'
    printf 'persistent_rootfs=nwrt-linux-root-8GiB.sparse.img-to-linux_root\n'
    printf 'persistent_rootfs_growth=resize2fs-to-exact-200GiB-partition-on-first-boot\n'
    printf 'stock_boot_a_sha256=%s\n' "$BOOT_A_SHA256"
    printf 'stock_init_boot_a_sha256=%s\n' "$INIT_BOOT_A_SHA256"
    printf 'stock_vendor_boot_a_sha256=%s\n' "$VENDOR_BOOT_A_SHA256"
    printf 'stock_dtbo_a_sha256=%s\n' "$DTBO_A_SHA256"
} > "$OUTPUT_DIR/MANIFEST.txt"
(
    cd "$OUTPUT_DIR"
    find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 |
        sort -z | xargs -0 sha256sum > SHA256SUMS
)
printf 'nwrt B-slot production boot chain built: %s\n' "$OUTPUT_DIR"
