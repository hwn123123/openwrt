#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOLS_ROOT_DEFAULT="$(cd "$SCRIPT_DIR/../../../../../../.." && pwd)/构建工具"
TOOLS_ROOT="${XM14_ANDROID_TOOLS:-$TOOLS_ROOT_DEFAULT}"
NDK_ROOT="${ANDROID_NDK_ROOT:-$TOOLS_ROOT/android-ndk-r30}"
AIDL="${AIDL:-$TOOLS_ROOT/aidl-36}"
ANDROID_API="${ANDROID_API:-35}"
SIDECAR_ROOT="$(cd "$SCRIPT_DIR/../../files/android-root" && pwd)"
SYSTEM_ROOT="$(cd "$SCRIPT_DIR/../../../xm14pro-qwes/files/android-root" && pwd)"
PROBE_OUTPUT="$SIDECAR_ROOT/vendor/bin/xm14pro-radio-probe"
SMS_OUTPUT="$SIDECAR_ROOT/vendor/bin/xm14pro-radio-sms"
FRAMEWORK_OUTPUT="$SIDECAR_ROOT/vendor/bin/xm14pro-radio-framework"
CORE_AIDL="$SCRIPT_DIR/aidl/android.hardware.radio/2"
MESSAGING_AIDL="$SCRIPT_DIR/aidl/android.hardware.radio.messaging/2"
CLANG="$NDK_ROOT/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android${ANDROID_API}-clang++"
WORK="$(mktemp -d /tmp/xm14-radio-bridge.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

test -x "$AIDL" || { echo "missing AIDL compiler: $AIDL" >&2; exit 2; }
test -x "$CLANG" || { echo "missing Android NDK compiler: $CLANG" >&2; exit 2; }

mkdir -p "$WORK/core-include" "$WORK/core-src" \
	"$WORK/messaging-include" "$WORK/messaging-src" "$(dirname "$PROBE_OUTPUT")"

CORE_HASH="$(tr -d '\r\n' < "$CORE_AIDL/.hash")"
MESSAGING_HASH="$(tr -d '\r\n' < "$MESSAGING_AIDL/.hash")"

CORE_FILES=()
while IFS= read -r file; do CORE_FILES+=("$file"); done \
	< <(find "$CORE_AIDL" -name '*.aidl' -type f | sort)
MESSAGING_FILES=()
while IFS= read -r file; do MESSAGING_FILES+=("$file"); done \
	< <(find "$MESSAGING_AIDL" -name '*.aidl' -type f | sort)

"$AIDL" --lang=ndk --structured --stability=vintf --min_sdk_version=29 \
	-v 2 --hash="$CORE_HASH" -I "$CORE_AIDL" \
	-h "$WORK/core-include" -o "$WORK/core-src" "${CORE_FILES[@]}"

"$AIDL" --lang=ndk --structured --stability=vintf --min_sdk_version=29 \
	-v 2 --hash="$MESSAGING_HASH" -I "$MESSAGING_AIDL" -I "$CORE_AIDL" \
	-h "$WORK/messaging-include" -o "$WORK/messaging-src" \
	"${MESSAGING_FILES[@]}"

"$CLANG" -std=c++17 -O2 -fPIE -pie -fvisibility=hidden -fno-rtti -nostdlib++ \
	-I "$WORK/core-include" -I "$WORK/messaging-include" \
	-I "$SCRIPT_DIR/binder-headers/include_cpp" \
	-I "$SCRIPT_DIR/binder-headers/include_platform" \
	-L "$SYSTEM_ROOT/system/lib64" -L "$SIDECAR_ROOT/vendor/lib64" \
	-Wl,--dynamic-linker,/system/bin/bootstrap/linker64 \
	-Wl,-rpath,/system/lib64 -Wl,-rpath,/vendor/lib64 \
	-Wl,-rpath-link,"$SYSTEM_ROOT/system/lib64" \
	-Wl,-rpath-link,"$SIDECAR_ROOT/vendor/lib64" \
	-o "$PROBE_OUTPUT" "$SCRIPT_DIR/radio-probe.cpp" \
	-Wl,--no-as-needed \
	"$SIDECAR_ROOT/vendor/lib64/android.hardware.radio.messaging-V2-ndk.so" \
	"$SIDECAR_ROOT/vendor/lib64/android.hardware.radio-V2-ndk.so" \
	"$SYSTEM_ROOT/system/lib64/libbinder_ndk.so" \
	"$SYSTEM_ROOT/system/lib64/libbinder.so" \
	"$SYSTEM_ROOT/system/lib64/libc++.so" \
	"$SYSTEM_ROOT/system/lib64/liblog.so" -ldl -lm -lc

"$CLANG" -std=c++17 -O2 -fPIE -pie -fvisibility=hidden -fno-rtti -nostdlib++ \
	-I "$SCRIPT_DIR/binder-headers/include_cpp" \
	-I "$SCRIPT_DIR/binder-headers/include_platform" \
	-L "$SYSTEM_ROOT/system/lib64" -L "$SIDECAR_ROOT/vendor/lib64" \
	-Wl,--dynamic-linker,/system/bin/bootstrap/linker64 \
	-Wl,-rpath,/system/lib64 -Wl,-rpath,/vendor/lib64 \
	-Wl,-rpath-link,"$SYSTEM_ROOT/system/lib64" \
	-Wl,-rpath-link,"$SIDECAR_ROOT/vendor/lib64" \
	-o "$FRAMEWORK_OUTPUT" "$SCRIPT_DIR/radio-framework.cpp" \
	-Wl,--no-as-needed \
	"$SYSTEM_ROOT/system/lib64/libbinder_ndk.so" \
	"$SYSTEM_ROOT/system/lib64/libbinder.so" \
	"$SYSTEM_ROOT/system/lib64/libc++.so" \
	"$SYSTEM_ROOT/system/lib64/liblog.so" -ldl -lm -lc

"$CLANG" -std=c++17 -O2 -fPIE -pie -fvisibility=hidden -fno-rtti -nostdlib++ \
	-I "$WORK/core-include" -I "$WORK/messaging-include" \
	-I "$SCRIPT_DIR/binder-headers/include_cpp" \
	-I "$SCRIPT_DIR/binder-headers/include_platform" \
	-L "$SYSTEM_ROOT/system/lib64" -L "$SIDECAR_ROOT/vendor/lib64" \
	-Wl,--dynamic-linker,/system/bin/bootstrap/linker64 \
	-Wl,-rpath,/system/lib64 -Wl,-rpath,/vendor/lib64 \
	-Wl,-rpath-link,"$SYSTEM_ROOT/system/lib64" \
	-Wl,-rpath-link,"$SIDECAR_ROOT/vendor/lib64" \
	-o "$SMS_OUTPUT" "$SCRIPT_DIR/radio-sms.cpp" \
	-Wl,--no-as-needed \
	"$SIDECAR_ROOT/vendor/lib64/android.hardware.radio.messaging-V2-ndk.so" \
	"$SIDECAR_ROOT/vendor/lib64/android.hardware.radio-V2-ndk.so" \
	"$SYSTEM_ROOT/system/lib64/libbinder_ndk.so" \
	"$SYSTEM_ROOT/system/lib64/libbinder.so" \
	"$SYSTEM_ROOT/system/lib64/libc++.so" \
	"$SYSTEM_ROOT/system/lib64/liblog.so" -ldl -lm -lc

chmod 0755 "$PROBE_OUTPUT" "$SMS_OUTPUT" "$FRAMEWORK_OUTPUT"
echo "built: $PROBE_OUTPUT"
file "$PROBE_OUTPUT"
echo "built: $SMS_OUTPUT"
file "$SMS_OUTPUT"
echo "built: $FRAMEWORK_OUTPUT"
file "$FRAMEWORK_OUTPUT"
