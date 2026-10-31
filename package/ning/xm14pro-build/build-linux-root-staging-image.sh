#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

ROOTFS_TARBALL="${1:?usage: build-linux-root-staging-image.sh ROOTFS_TARBALL [OUTPUT_DIR]}"
OUTPUT_DIR="${2:-$(dirname -- "$ROOTFS_TARBALL")}"
LOGICAL_SIZE=8589934592
BLOCK_SIZE=4096
OUTPUT_IMAGE="$OUTPUT_DIR/nwrt-linux-root-8GiB.sparse.img"
OUTPUT_MANIFEST="$OUTPUT_DIR/nwrt-linux-root-8GiB.MANIFEST.txt"

die() { printf 'linux_root staging image: %s\n' "$*" >&2; exit 2; }

[[ -f "$ROOTFS_TARBALL" ]] || die "missing rootfs tarball: $ROOTFS_TARBALL"
for tool in fakeroot tar truncate mke2fs e2fsck debugfs e2label img2simg simg_dump sha256sum; do
    command -v "$tool" >/dev/null || die "missing host tool: $tool"
done
tar -tzf "$ROOTFS_TARBALL" | grep -E '^\.?/?sbin/init$' >/dev/null ||
    die 'rootfs tarball lacks /sbin/init'

mkdir -p -- "$OUTPUT_DIR"
WORK_DIR="$(mktemp -d /tmp/xm14-linux-root.XXXXXX)"
cleanup() {
    if [[ "$WORK_DIR" == /tmp/xm14-linux-root.* && -d "$WORK_DIR" && ! -L "$WORK_DIR" ]]; then
        find "$WORK_DIR" -mindepth 1 -delete 2>/dev/null || true
        rmdir "$WORK_DIR" 2>/dev/null || true
    fi
}
trap cleanup EXIT

ROOT_DIR="$WORK_DIR/root"
RAW_IMAGE="$WORK_DIR/linux_root.raw.img"
mkdir -p -- "$ROOT_DIR"

# Keep archive ownership and modes without requiring a root login. mke2fs runs
# in the same fakeroot session so its -d importer sees the archived uid/gid
# values instead of the unprivileged build user's real ownership.
fakeroot -- sh -eu -c '
    archive="$1"
    root="$2"
    image="$3"
    size="$4"
    tar --numeric-owner --same-owner -xzf "$archive" -C "$root"
    test -x "$root/sbin/init"
    truncate -s "$size" "$image"
    mke2fs -q -F -t ext4 -b 4096 -I 256 -m 0 -L linux_root \
        -E lazy_itable_init=0,lazy_journal_init=0 \
        -O ^orphan_file,^metadata_csum_seed -d "$root" "$image"
' sh "$ROOTFS_TARBALL" "$ROOT_DIR" "$RAW_IMAGE" "$LOGICAL_SIZE"

[[ "$(stat -c %s "$RAW_IMAGE")" -eq "$LOGICAL_SIZE" ]] ||
    die 'raw ext4 image has the wrong logical size'
[[ "$(e2label "$RAW_IMAGE")" == linux_root ]] || die 'ext4 label is not linux_root'
e2fsck -fn "$RAW_IMAGE" >/dev/null || die 'ext4 filesystem check failed'
debugfs -R 'stat /sbin/init' "$RAW_IMAGE" 2>/dev/null | grep 'Type: regular' >/dev/null ||
    die 'staging filesystem lacks /sbin/init'

rm -f -- "$OUTPUT_IMAGE"
img2simg "$RAW_IMAGE" "$OUTPUT_IMAGE" "$BLOCK_SIZE"
[[ -s "$OUTPUT_IMAGE" ]] || die 'Android sparse image was not created'
simg_dump "$OUTPUT_IMAGE" > "$WORK_DIR/sparse.dump"
grep -Eq 'Total of [0-9]+ [0-9]+-byte output blocks in [0-9]+ input chunks' \
    "$WORK_DIR/sparse.dump" || die 'Android sparse image metadata is unreadable'

{
    printf 'device=shennong\n'
    printf 'partition=linux_root\n'
    printf 'partition_capacity_bytes=214748364800\n'
    printf 'staging_filesystem=ext4\n'
    printf 'staging_label=linux_root\n'
    printf 'staging_logical_bytes=%s\n' "$LOGICAL_SIZE"
    printf 'flash_format=android-sparse\n'
    printf 'first_boot_growth=resize2fs-after-exact-partition-guard\n'
    printf 'source_rootfs=%s\n' "$(basename -- "$ROOTFS_TARBALL")"
    printf 'image_sha256=%s\n' "$(sha256sum "$OUTPUT_IMAGE" | awk '{print $1}')"
} > "$OUTPUT_MANIFEST"

printf 'linux_root ext4 staging image built: %s\n' "$OUTPUT_IMAGE"
