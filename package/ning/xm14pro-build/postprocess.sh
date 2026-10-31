#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
SOURCE_DIR="$TOPDIR/bin/targets/xm14pro/armv8"
OUTPUT_DIR="$TOPDIR/bin/targets/shennong/rootfs"
LAYOUT="$SCRIPT_DIR/layout/linux-root-layout-v1.json"

[[ -d "$SOURCE_DIR" ]] || { printf 'missing xm14pro output: %s\n' "$SOURCE_DIR" >&2; exit 2; }
[[ -f "$LAYOUT" ]] || { printf 'missing partition layout: %s\n' "$LAYOUT" >&2; exit 2; }
mkdir -p -- "$OUTPUT_DIR"
find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -delete

# Keep previous successful images until the new build is known to be complete.
# Select the newest EROFS image, then only accept tar aliases carrying the same
# build prefix.  Older builds in bin/targets must not make post-processing fail.
erofs_image="$(
    find "$SOURCE_DIR" -maxdepth 1 -type f -name '*-erofs-rootfs.erofs' \
        -printf '%T@ %p\n' |
        sort -nr | head -n 1 | cut -d ' ' -f 2-
)"
[[ -n "$erofs_image" && -f "$erofs_image" ]] || {
    printf 'no rootfs.erofs payload found in %s\n' "$SOURCE_DIR" >&2
    exit 3
}

artifact_prefix="${erofs_image%-erofs-rootfs.erofs}"
tarball="${artifact_prefix}-rootfs.tar.gz"
if [[ ! -f "$tarball" ]]; then
    tarball="${artifact_prefix}-targz-rootfs.tar.gz"
fi
[[ -f "$tarball" ]] || {
    printf 'no matching rootfs.tar.gz payload found for %s\n' "$erofs_image" >&2
    exit 3
}

install -m 0644 "$tarball" "$OUTPUT_DIR/nwrt-xm14pro-rootfs.tar.gz"
"$SCRIPT_DIR/build-linux-root-staging-image.sh" \
    "$OUTPUT_DIR/nwrt-xm14pro-rootfs.tar.gz" "$OUTPUT_DIR"

shim="${XM14_BOOT_SHIM:-}"
if [[ -z "$shim" ]]; then
    shim="$(find "$TOPDIR/staging_dir" -type f -path '*/root-*/usr/libexec/xm14/boot-shim' -print -quit)"
fi
[[ -n "$shim" && -f "$shim" ]] || { printf 'built boot shim not found\n' >&2; exit 3; }
file "$shim" | grep -F 'ARM aarch64' >/dev/null
file "$shim" | grep -F 'statically linked' >/dev/null
install -m 0755 "$shim" "$OUTPUT_DIR/xm14pro-boot-shim"

install -m 0644 "$LAYOUT" "$OUTPUT_DIR/linux-root-layout-v1.json"
commit="$(git -c safe.directory="$TOPDIR" -C "$TOPDIR" rev-parse HEAD)"
branch="$(git -c safe.directory="$TOPDIR" -C "$TOPDIR" branch --show-current)"
{
    printf 'device=shennong\n'
    printf 'artifact_kind=nwrt-production-userspace-and-boot-shim\n'
    printf 'release_status=production\n'
    printf 'flashable=yes\n'
    printf 'source_branch=%s\n' "$branch"
    printf 'source_commit=%s\n' "$commit"
    printf 'userspace_target=xm14pro/armv8\n'
    printf 'userspace_libc=musl\n'
    printf 'device_kernel=stock-xiaomi-6.1.138-android14-11\n'
    printf 'rootfs_install_image=nwrt-linux-root-8GiB.sparse.img\n'
    printf 'openwrt_kernel_artifacts_exported=no\n'
    printf 'rootfs_location=linux_root\n'
    printf 'persistent_data_location=linux_root\n'
    printf 'rootfs_filesystem=ext4-direct\n'
    printf 'rootfs_first_boot_growth=8GiB-to-200GiB-after-exact-size-guard\n'
    printf 'b_slot_content=boot-chain-and-rescue-only\n'
    printf 'partition_layout_status=single-linux-root-200GiB\n'
} > "$OUTPUT_DIR/MANIFEST.txt"
(
    cd "$OUTPUT_DIR"
    find . -maxdepth 1 -type f ! -name SHA256SUMS -print0 |
        sort -z | xargs -0 sha256sum > SHA256SUMS
)
printf 'shennong nwrt production userspace artifacts exported: %s\n' "$OUTPUT_DIR"

"$SCRIPT_DIR/build-b-slot.sh" "$OUTPUT_DIR"

