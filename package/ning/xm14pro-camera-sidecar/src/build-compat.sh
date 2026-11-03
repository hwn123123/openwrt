#!/bin/sh
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out=${1:-"$here/../files/usr/libexec/xm14pro-camera"}
: "${ANDROID_NDK:?Set ANDROID_NDK to the Android NDK root}"
: "${ANDROID_SYSTEM_LIB64:?Set ANDROID_SYSTEM_LIB64 to a matching stock system lib64 directory}"
case "$(uname -s)" in
    Darwin) host=darwin-x86_64 ;;
    Linux) host=linux-x86_64 ;;
    *) echo 'Unsupported Android NDK host' >&2; exit 1 ;;
esac
cc="$ANDROID_NDK/toolchains/llvm/prebuilt/$host/bin/aarch64-linux-android35-clang"
[ -x "$cc" ] || { echo "Missing Android arm64 compiler: $cc" >&2; exit 1; }
[ -s "$ANDROID_SYSTEM_LIB64/libbinder_ndk.so" ] || {
    echo 'Missing stock libbinder_ndk.so' >&2; exit 1;
}
mkdir -p "$out"
for name in proxy sensor-privacy processinfo flinger; do
    "$cc" "$here/camera-$name-compat.c" -o "$out/camera-$name-compat" \
        -L"$ANDROID_SYSTEM_LIB64" -lbinder_ndk -Wl,--allow-shlib-undefined
done
"$cc" "$here/camera-list.c" -o "$out/camera-list" -lcamera2ndk
for name in activity permission hwsm-property abi; do
    "$cc" -shared -fPIC "$here/camera-$name-compat.c" -o "$out/camera-$name-compat.so" \
        -Wl,--allow-shlib-undefined
done
chmod 755 "$out"/camera-*-compat "$out"/camera-*-compat.so "$out/camera-list"
(
    cd "$out"
    sha256sum camera-*-compat camera-*-compat.so camera-list > COMPAT_SHA256SUMS
)
