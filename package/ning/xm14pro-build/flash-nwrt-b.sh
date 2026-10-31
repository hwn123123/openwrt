#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

usage() {
    printf '%s\n' \
        'usage: flash-nwrt-b.sh ERASE-LINUX-ROOT BOOT_CHAIN_DIR' \
        '' \
        'Writes only linux_root and boot_b/init_boot_b/vendor_boot_b/dtbo_b/vbmeta_b.' \
        'It never writes slot A, recovery, userdata or Android logical partitions.' >&2
    exit 2
}

[[ "$#" -eq 2 ]] || usage
[[ "$1" == ERASE-LINUX-ROOT ]] || {
    printf 'refusing to overwrite linux_root without ERASE-LINUX-ROOT\n' >&2
    exit 2
}
CHAIN_DIR="$(cd -- "$2" && pwd -P)"

for tool in fastboot python3; do
    command -v "$tool" >/dev/null || { printf 'missing host tool: %s\n' "$tool" >&2; exit 2; }
done

required=(
    boot_b.img init_boot_b.img vendor_boot_b.img dtbo_b.img vbmeta_b.img
    nwrt-linux-root-8GiB.sparse.img MANIFEST.txt SHA256SUMS
)
for name in "${required[@]}"; do
    [[ -f "$CHAIN_DIR/$name" ]] || { printf 'missing artifact: %s\n' "$name" >&2; exit 2; }
done

if command -v sha256sum >/dev/null; then
    (cd "$CHAIN_DIR" && sha256sum -c SHA256SUMS)
else
    (cd "$CHAIN_DIR" && shasum -a 256 -c SHA256SUMS)
fi

python3 - "$CHAIN_DIR" <<'PY'
import os
import struct
import sys

base = sys.argv[1]
sizes = {
    "boot_b.img": 100_663_296,
    "init_boot_b.img": 8_388_608,
    "vendor_boot_b.img": 100_663_296,
    "dtbo_b.img": 25_165_824,
    "vbmeta_b.img": 131_072,
}
for name, expected in sizes.items():
    actual = os.path.getsize(os.path.join(base, name))
    if actual != expected:
        raise SystemExit(f"{name}: {actual} bytes, expected {expected}")

sparse_path = os.path.join(base, "nwrt-linux-root-8GiB.sparse.img")
with open(sparse_path, "rb") as stream:
    header = stream.read(28)
if len(header) != 28:
    raise SystemExit("linux_root sparse header is truncated")
magic, major, minor, file_header, chunk_header, block_size, blocks, chunks, checksum = \
    struct.unpack("<IHHHHIIII", header)
if magic != 0xED26FF3A or major != 1 or file_header != 28 or chunk_header != 12:
    raise SystemExit("linux_root image is not supported Android sparse format")
logical_size = block_size * blocks
if logical_size != 8_589_934_592:
    raise SystemExit(f"linux_root staging image is {logical_size} bytes, expected 8 GiB")

with open(os.path.join(base, "vbmeta_b.img"), "rb") as stream:
    vbmeta = stream.read(124)
if len(vbmeta) != 124 or vbmeta[:4] != b"AVB0":
    raise SystemExit("vbmeta_b does not contain an AVB header")
flags = struct.unpack_from(">I", vbmeta, 120)[0]
if flags & 3 != 3:
    raise SystemExit(f"vbmeta_b flags are {flags}, expected verification+verity disabled")

print(f"validated linux_root sparse geometry: {block_size} x {blocks}, {chunks} chunks")
print(f"validated vbmeta_b flags: {flags}")
PY

device_list="$(fastboot devices | awk 'NF {print $1}')"
device_count="$(printf '%s\n' "$device_list" | awk 'NF {count++} END {print count + 0}')"
[[ "$device_count" -eq 1 ]] || {
    printf 'expected exactly one fastboot device, found %s\n' "$device_count" >&2
    exit 2
}

getvar() {
    fastboot getvar "$1" 2>&1 | sed -nE "s/.*$1:[[:space:]]*([^[:space:]]+).*/\1/p" | tail -n 1
}

[[ "$(getvar product)" == shennong ]] || { printf 'device is not shennong\n' >&2; exit 2; }
unlocked="$(getvar unlocked)"
[[ "$unlocked" == yes || "$unlocked" == true ]] || {
    printf 'bootloader is not unlocked\n' >&2
    exit 2
}

check_partition_size() {
    local partition="$1" expected="$2" raw actual
    raw="$(getvar "partition-size:$partition")"
    [[ -n "$raw" ]] || { printf 'cannot read %s size\n' "$partition" >&2; exit 2; }
    actual=$((raw))
    [[ "$actual" -eq "$expected" ]] || {
        printf '%s size is %s, expected %s\n' "$partition" "$actual" "$expected" >&2
        exit 2
    }
}

check_partition_size linux_root 214748364800
check_partition_size boot_b 100663296
check_partition_size init_boot_b 8388608
check_partition_size vendor_boot_b 100663296
check_partition_size dtbo_b 25165824
check_partition_size vbmeta_b 131072

fastboot flash linux_root "$CHAIN_DIR/nwrt-linux-root-8GiB.sparse.img"
fastboot flash boot_b "$CHAIN_DIR/boot_b.img"
fastboot flash init_boot_b "$CHAIN_DIR/init_boot_b.img"
fastboot flash vendor_boot_b "$CHAIN_DIR/vendor_boot_b.img"
fastboot flash dtbo_b "$CHAIN_DIR/dtbo_b.img"
fastboot flash vbmeta_b "$CHAIN_DIR/vbmeta_b.img"
fastboot set_active b

printf '%s\n' \
    'NWRT was written only to linux_root and the B-slot boot chain.' \
    'The phone remains in fastboot; slot A and recovery were not modified.'
