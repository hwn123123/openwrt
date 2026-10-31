#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLS_ROOT_DEFAULT="$(cd "$SCRIPT_DIR/../../../../../.." && pwd)/构建工具"
TOOLS_ROOT="${XM14_ANDROID_TOOLS:-$TOOLS_ROOT_DEFAULT}"
NDK_ROOT="${ANDROID_NDK_ROOT:-$TOOLS_ROOT/android-ndk-r30}"
ANDROID_API="${ANDROID_API:-35}"
SIDECAR_ROOT="$(cd "$SCRIPT_DIR/../files/android-root" && pwd)"
SYSTEM_ROOT="$(cd "$SCRIPT_DIR/../../xm14pro-qwes/files/android-root" && pwd)"
BINDER_HEADERS="$(cd "$SCRIPT_DIR/../../xm14pro-telephony-sidecar/android-src/radio-bridge/binder-headers" && pwd)"
NFC_HEADERS="$SCRIPT_DIR/nfc-headers"
CONTROL_OUTPUT="$SIDECAR_ROOT/vendor/bin/xm14pro-peripheral-control"
NFC_PROBE_OUTPUT="$SIDECAR_ROOT/vendor/bin/xm14pro-nfc-stack-probe"
NFC_SERVICE_OUTPUT="$SIDECAR_ROOT/vendor/bin/xm14pro-nfc-service"
NFC_STACK_LIB="$SIDECAR_ROOT/vendor/lib64/libsn100nfc-nci.so"
CLANG="$NDK_ROOT/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android${ANDROID_API}-clang++"

test -x "$CLANG" || { echo "missing Android NDK compiler: $CLANG" >&2; exit 2; }
test -f "$NFC_STACK_LIB" || { echo "missing NFC stack library: $NFC_STACK_LIB" >&2; exit 2; }
mkdir -p "$(dirname "$CONTROL_OUTPUT")"

"$CLANG" -std=c++17 -O2 -fPIE -pie -fvisibility=hidden -fno-rtti -nostdlib++ \
	-I "$BINDER_HEADERS/include_cpp" \
	-I "$BINDER_HEADERS/include_platform" \
	-L "$SYSTEM_ROOT/system/lib64" \
	-Wl,--dynamic-linker,/system/bin/bootstrap/linker64 \
	-Wl,-rpath,/system/lib64 -Wl,-rpath,/vendor/lib64 \
	-Wl,-rpath-link,"$SYSTEM_ROOT/system/lib64" \
	-o "$CONTROL_OUTPUT" "$SCRIPT_DIR/peripheral-control.cpp" \
	-Wl,--no-as-needed \
	"$SYSTEM_ROOT/system/lib64/libbinder_ndk.so" \
	"$SYSTEM_ROOT/system/lib64/libbinder.so" \
	"$SYSTEM_ROOT/system/lib64/libc++.so" \
	"$SYSTEM_ROOT/system/lib64/liblog.so" -ldl -lm -lc

"$CLANG" -std=c++17 -O2 -fPIE -pie -fvisibility=hidden -fno-rtti -nostdlib++ \
	-I "$BINDER_HEADERS/include_cpp" \
	-I "$BINDER_HEADERS/include_platform" \
	-I "$NFC_HEADERS/src/nfa/include" \
	-I "$NFC_HEADERS/src/nfc/include" \
	-I "$NFC_HEADERS/src/include" \
	-I "$NFC_HEADERS/src/gki/common" \
	-I "$NFC_HEADERS/src/gki/ulinux" \
	-L "$SIDECAR_ROOT/vendor/lib64" \
	-L "$SYSTEM_ROOT/system/lib64" \
	-Wl,--dynamic-linker,/system/bin/bootstrap/linker64 \
	-Wl,-rpath,/system/lib64 -Wl,-rpath,/vendor/lib64 \
	-Wl,-rpath-link,"$SYSTEM_ROOT/system/lib64" \
	-Wl,-rpath-link,"$SIDECAR_ROOT/vendor/lib64" \
	-o "$NFC_PROBE_OUTPUT" "$SCRIPT_DIR/nfc-stack-probe.cpp" \
	-Wl,--no-as-needed \
	"$NFC_STACK_LIB" \
	"$SYSTEM_ROOT/system/lib64/libbinder_ndk.so" \
	"$SYSTEM_ROOT/system/lib64/libbinder.so" \
	"$SYSTEM_ROOT/system/lib64/libc++.so" \
	"$SYSTEM_ROOT/system/lib64/liblog.so" -ldl -lm -lc

"$CLANG" -std=c++17 -O2 -fPIE -pie -fvisibility=hidden -fno-rtti -nostdlib++ \
	-I "$BINDER_HEADERS/include_cpp" \
	-I "$BINDER_HEADERS/include_platform" \
	-I "$NFC_HEADERS/src/nfa/include" \
	-I "$NFC_HEADERS/src/nfc/include" \
	-I "$NFC_HEADERS/src/include" \
	-I "$NFC_HEADERS/src/gki/common" \
	-I "$NFC_HEADERS/src/gki/ulinux" \
	-L "$SIDECAR_ROOT/vendor/lib64" \
	-L "$SYSTEM_ROOT/system/lib64" \
	-Wl,--dynamic-linker,/system/bin/bootstrap/linker64 \
	-Wl,-rpath,/system/lib64 -Wl,-rpath,/vendor/lib64 \
	-Wl,-rpath-link,"$SYSTEM_ROOT/system/lib64" \
	-Wl,-rpath-link,"$SIDECAR_ROOT/vendor/lib64" \
	-o "$NFC_SERVICE_OUTPUT" "$SCRIPT_DIR/nfc-service.cpp" \
	-Wl,--no-as-needed \
	"$NFC_STACK_LIB" \
	"$SYSTEM_ROOT/system/lib64/libbinder_ndk.so" \
	"$SYSTEM_ROOT/system/lib64/libbinder.so" \
	"$SYSTEM_ROOT/system/lib64/libc++.so" \
	"$SYSTEM_ROOT/system/lib64/liblog.so" -ldl -lm -lc

chmod 0755 "$CONTROL_OUTPUT" "$NFC_PROBE_OUTPUT" "$NFC_SERVICE_OUTPUT"
echo "built: $CONTROL_OUTPUT"
file "$CONTROL_OUTPUT"
echo "built: $NFC_PROBE_OUTPUT"
file "$NFC_PROBE_OUTPUT"
echo "built: $NFC_SERVICE_OUTPUT"
file "$NFC_SERVICE_OUTPUT"
