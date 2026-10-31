#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

package_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
project_dir=$(CDPATH= cd -- "$package_dir/../../../../.." && pwd)
ndk_bin="$project_dir/构建工具/android-ndk-r30/toolchains/llvm/prebuilt/darwin-x86_64/bin"
android_root="$package_dir/files/android-root"
output="$android_root/vendor/lib64/libxm14pro_audio_control.so"

"$ndk_bin/aarch64-linux-android34-clang" \
	-Wall -Wextra -Werror -O2 -fPIC -shared \
	-I"$package_dir/src" \
	"$package_dir/src/audio-control-server.c" \
	-ldl -lm \
	-Wl,-soname,libxm14pro_audio_control.so \
	-o "$output"
chmod 0644 "$output"
"$ndk_bin/llvm-strip" --strip-unneeded "$output"
"$ndk_bin/llvm-readelf" -d "$output"
shasum -a 256 "$output"
