#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLS_ROOT_DEFAULT="$(cd "$SCRIPT_DIR/../../../../../.." && pwd)/构建工具"
TOOLS_ROOT="${XM14_ANDROID_TOOLS:-$TOOLS_ROOT_DEFAULT}"
NDK_ROOT="${ANDROID_NDK_ROOT:-$TOOLS_ROOT/android-ndk-r30}"
ANDROID_API="${ANDROID_API:-35}"
NDK_HOST="${ANDROID_NDK_HOST:-darwin-x86_64}"
PACKAGE_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SYSTEM_ROOT="$PACKAGE_ROOT/xm14pro-qwes/files/android-root"
BINDER_HEADERS="$PACKAGE_ROOT/xm14pro-telephony-sidecar/android-src/radio-bridge/binder-headers"
OUTPUT="$SCRIPT_DIR/../files/android-root/vendor/bin/nwrt-power-policy"
CLANG="$NDK_ROOT/toolchains/llvm/prebuilt/$NDK_HOST/bin/aarch64-linux-android${ANDROID_API}-clang++"
test -x "$CLANG" || { echo "missing Android NDK compiler: $CLANG" >&2; exit 2; }
mkdir -p "$(dirname "$OUTPUT")"
"$CLANG" -std=c++17 -Wall -Wextra -Werror -O2 -fPIE -pie -fno-rtti -nostdlib++ \
    -I "$BINDER_HEADERS/include_cpp" -I "$BINDER_HEADERS/include_platform" \
    -L "$SYSTEM_ROOT/system/lib64" \
    -Wl,--dynamic-linker,/system/bin/bootstrap/linker64 \
    -Wl,-rpath,/system/lib64 -Wl,-rpath,/vendor/lib64 \
    -Wl,-rpath-link,"$SYSTEM_ROOT/system/lib64" \
    -o "$OUTPUT" "$SCRIPT_DIR/power-policy.cpp" -Wl,--no-as-needed \
    "$SYSTEM_ROOT/system/lib64/libbinder_ndk.so" \
    "$SYSTEM_ROOT/system/lib64/libbinder.so" \
    "$SYSTEM_ROOT/system/lib64/libc++.so" \
    "$SYSTEM_ROOT/system/lib64/liblog.so" -ldl -lm -lc
chmod 0755 "$OUTPUT"
echo "built: $OUTPUT"
