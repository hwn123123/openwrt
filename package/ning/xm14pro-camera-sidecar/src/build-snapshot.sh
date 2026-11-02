#!/bin/sh
set -eu

# Set these to an Android NDK and a read-only extracted stock PixelOS system
# library directory. This build produces only our camera client, not stock
# Android binaries or firmware.
: "${ANDROID_NDK:?set ANDROID_NDK}"
: "${ANDROID_SYSTEM_LIB64:?set ANDROID_SYSTEM_LIB64}"
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:-"$here/../files/usr/libexec/xm14pro-camera/camera-snapshot"}

if [ -x "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android29-clang" ]; then
	clang="$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android29-clang"
elif [ -x "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang" ]; then
	clang="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang"
else
	echo 'Android NDK arm64 clang not found' >&2
	exit 1
fi

mkdir -p "$(dirname -- "$out")"
"$clang" -O2 -Wall -Wextra -Wl,--allow-shlib-undefined \
	-I "$here/jpeg" -L "$ANDROID_SYSTEM_LIB64" \
	-o "$out" "$here/camera-snapshot.c" \
	-lcamera2ndk -lmediandk -lnativewindow -ljpeg -lbinder_ndk
