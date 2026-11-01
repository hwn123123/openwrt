#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
output=${1:-$root/output}

exec "$root/tools/build-multiboot-recovery.sh" \
    "$root/base-recovery.img" \
    "$root/selector-base.img" \
    "$root/assets" \
    "$output" \
    "$root/tools/magiskboot" \
    "$root/tools/mkbootimg.py" \
    "$root/tools/avbtool.py" \
    "$root/tools/patch-multiboot-recovery.py"
