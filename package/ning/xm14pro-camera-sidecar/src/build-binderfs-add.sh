#!/bin/sh
set -eu
: "${ANDROID_NDK:?set ANDROID_NDK}"
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
package_dir=$(dirname "$script_dir")
if [ -x "$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android29-clang" ]; then
	clang="$ANDROID_NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android29-clang"
elif [ -x "$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang" ]; then
	clang="$ANDROID_NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/aarch64-linux-android29-clang"
else
	echo 'Android NDK arm64 compiler not found' >&2
	exit 1
fi
"$clang" -O2 -Wall -Wextra "$script_dir/binderfs-add.c" -o \
	"$package_dir/files/usr/libexec/xm14pro-camera/binderfs-add"
