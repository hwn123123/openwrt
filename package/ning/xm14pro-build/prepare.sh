#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
SEED="$SCRIPT_DIR/configs/shennong-rootfs.config"
PLATFORM_INSTALLER="$SCRIPT_DIR/../xm14pro-platform/install-target.sh"
MODEMMANAGER_COMPAT_INSTALLER="$SCRIPT_DIR/install-modemmanager-compat.sh"
LUCI_MENU_COMPAT_INSTALLER="$SCRIPT_DIR/install-luci-menu-compat.sh"
LUCI_WIFI_COMPAT_INSTALLER="$SCRIPT_DIR/install-luci-wifi-compat.py"
LUCI_LOG_COMPAT_INSTALLER="$SCRIPT_DIR/install-luci-log-compat.py"
LUCI_IDENTITY_COMPAT_INSTALLER="$SCRIPT_DIR/install-luci-identity-compat.py"
OPENCLASH_COMPAT_INSTALLER="$SCRIPT_DIR/install-openclash-compat.sh"
STOCK_NETWORK_COMPAT_INSTALLER="$SCRIPT_DIR/install-stock-network-compat.sh"
DOCKER_COMPAT_INSTALLER="$SCRIPT_DIR/install-docker-compat.sh"
KMSCON_MAKEFILE="$TOPDIR/feeds/video/utils/kmscon/Makefile"
KMSCON_PATCH_SOURCE="$SCRIPT_DIR/patches/kmscon/900-ignore-virtual-connectors-when-requested.patch"
KMSCON_PATCH_DIR="$TOPDIR/feeds/video/utils/kmscon/patches"
CONFIG="$TOPDIR/.config"
BACKUP="$TOPDIR/.config.before-xm14pro"
STOCK_MODULE_DIR="$TOPDIR/package/ning/xm14pro-stock-modules/files/usr/lib/xm14pro/stock-modules"
VENDOR_MODULE_SOURCE="${XM14_VENDOR_MODULE_SOURCE:-/home/ning/xiangmu/xm14-input/vendor-modules}"

[[ -f "$TOPDIR/rules.mk" && -x "$TOPDIR/scripts/config/conf" ]] || {
    printf 'not an initialized OpenWrt tree: %s\n' "$TOPDIR" >&2
    exit 2
}
[[ -f "$SEED" ]] || { printf 'missing seed config: %s\n' "$SEED" >&2; exit 2; }
[[ -x "$PLATFORM_INSTALLER" ]] || { printf 'missing platform installer: %s\n' "$PLATFORM_INSTALLER" >&2; exit 2; }
[[ -x "$MODEMMANAGER_COMPAT_INSTALLER" ]] || { printf 'missing ModemManager compatibility installer: %s\n' "$MODEMMANAGER_COMPAT_INSTALLER" >&2; exit 2; }
[[ -x "$LUCI_MENU_COMPAT_INSTALLER" ]] || { printf 'missing LuCI menu compatibility installer: %s\n' "$LUCI_MENU_COMPAT_INSTALLER" >&2; exit 2; }
[[ -f "$LUCI_WIFI_COMPAT_INSTALLER" ]] || { printf 'missing LuCI Wi-Fi compatibility installer: %s\n' "$LUCI_WIFI_COMPAT_INSTALLER" >&2; exit 2; }
[[ -f "$LUCI_LOG_COMPAT_INSTALLER" ]] || { printf 'missing LuCI log compatibility installer: %s\n' "$LUCI_LOG_COMPAT_INSTALLER" >&2; exit 2; }
[[ -f "$LUCI_IDENTITY_COMPAT_INSTALLER" ]] || { printf 'missing LuCI identity compatibility installer: %s\n' "$LUCI_IDENTITY_COMPAT_INSTALLER" >&2; exit 2; }
[[ -x "$OPENCLASH_COMPAT_INSTALLER" ]] || { printf 'missing OpenClash compatibility installer: %s\n' "$OPENCLASH_COMPAT_INSTALLER" >&2; exit 2; }
[[ -x "$STOCK_NETWORK_COMPAT_INSTALLER" ]] || { printf 'missing stock-network compatibility installer: %s\n' "$STOCK_NETWORK_COMPAT_INSTALLER" >&2; exit 2; }
[[ -x "$DOCKER_COMPAT_INSTALLER" ]] || { printf 'missing Docker compatibility installer: %s\n' "$DOCKER_COMPAT_INSTALLER" >&2; exit 2; }
[[ -f "$KMSCON_PATCH_SOURCE" ]] || { printf 'missing kmscon connector filter patch: %s\n' "$KMSCON_PATCH_SOURCE" >&2; exit 2; }

import_vendor_module() {
    local name="$1" expected="$2" source destination
    source="$VENDOR_MODULE_SOURCE/$name"
    destination="$STOCK_MODULE_DIR/$name"
    if [[ -f "$destination" ]] && [[ "$(sha256sum "$destination" | awk '{print $1}')" == "$expected" ]]; then
        return 0
    fi
    [[ -f "$source" ]] || {
        printf 'missing Xiaomi OS3.0.308.0 vendor module: %s\n' "$source" >&2
        exit 2
    }
    [[ "$(sha256sum "$source" | awk '{print $1}')" == "$expected" ]] || {
        printf 'hash mismatch for Xiaomi vendor module: %s\n' "$source" >&2
        exit 2
    }
    mkdir -p -- "$STOCK_MODULE_DIR"
    install -m 0644 "$source" "$destination"
}

# Proprietary phone modules stay outside the source tree and are imported only
# into the local build tree after exact firmware-version hash verification.
import_vendor_module xiaomi_touch.ko \
    3d856a4fc48ee6c0a79141b2f22e826e6c1ac46e177c9107992130a7c7372ae2
import_vendor_module synaptics_tcm2.ko \
    f9c53a043eb505e16a9e2e6da9e035325e336eda4510e4848938635037fb250b
import_vendor_module q6_pdr_dlkm.ko \
    0155f219c7df3957dc8505fe5b7321134c26c16fc474be67e80f73f102b4a500
import_vendor_module q6_notifier_dlkm.ko \
    bdfb6072d0f347430a31f2cee9277ba9229c09eb6aadc1c06db526add1d42a4e
import_vendor_module snd_event_dlkm.ko \
    ddfa0578434700890fd0af93a08fab9d389830e10afa7af266e99fbb5839082f
import_vendor_module gpr_dlkm.ko \
    ee51794912ea97398d9bc5fd19ae020434da612c4f6a2d5c9d1b26e39ebab61e
import_vendor_module spf_core_dlkm.ko \
    47e09e0bc78752d0a6624443daed09eafa57d30693e2343695891aa6aa875e7c
import_vendor_module adsp_loader_dlkm.ko \
    f6cdc42131ad252ccce36f859180b4a83d0990ebf28686be2c628b851d5bed4f

# Audio is a separate, opt-in closure layered on top of the already proven
# ADSP loader. Keep the stock OS3.0.308.0 load order in the manifest and verify
# every proprietary module before it enters the OpenWrt package tree.
while read -r audio_module audio_hash; do
    case "$audio_module" in
        ''|'#'*) continue ;;
    esac
    import_vendor_module "$audio_module" "$audio_hash"
done < "$SCRIPT_DIR/audio-modules.sha256"

"$PLATFORM_INSTALLER"
"$MODEMMANAGER_COMPAT_INSTALLER"
"$LUCI_MENU_COMPAT_INSTALLER"
python3 "$LUCI_WIFI_COMPAT_INSTALLER"
python3 "$LUCI_LOG_COMPAT_INSTALLER"
python3 "$LUCI_IDENTITY_COMPAT_INSTALLER"
"$OPENCLASH_COMPAT_INSTALLER"
"$STOCK_NETWORK_COMPAT_INSTALLER"
"$DOCKER_COMPAT_INSTALLER"

# kmscon normally pulls OpenWrt's kmod-input-evdev.  The Xiaomi target already
# boots the exact stock 6.1 input stack, so that 6.18 module is an ABI mismatch.
# Keep the upstream dependency for every other target and suppress it only for
# xm14pro; input-support, libudev and the rest of kmscon remain unchanged.
if [[ -f "$KMSCON_MAKEFILE" ]] && grep -q 'input-support +kmod-input-evdev' "$KMSCON_MAKEFILE"; then
    compatibility_file="$(mktemp "$TOPDIR/.kmscon-xm14pro.XXXXXX")"
    sed 's/input-support +kmod-input-evdev/input-support +!TARGET_xm14pro:kmod-input-evdev/' \
        "$KMSCON_MAKEFILE" > "$compatibility_file"
    mv -- "$compatibility_file" "$KMSCON_MAKEFILE"
fi
grep -q 'input-support +!TARGET_xm14pro:kmod-input-evdev' "$KMSCON_MAKEFILE" || {
    printf 'kmscon Xiaomi compatibility dependency was not applied\n' >&2
    exit 3
}

# Qualcomm's phone DRM driver exposes two always-connected Virtual/writeback
# connectors next to the real DSI panel.  Stock kmscon tries to modeset all
# three and enters a hot loop on the writeback encoders.  Add an opt-in runtime
# filter; only xm14pro-display enables it, so normal kmscon behaviour elsewhere
# remains unchanged.
mkdir -p -- "$KMSCON_PATCH_DIR"
cp -- "$KMSCON_PATCH_SOURCE" "$KMSCON_PATCH_DIR/900-ignore-virtual-connectors-when-requested.patch"

if [[ -s "$CONFIG" ]]; then
    cp -- "$CONFIG" "$BACKUP"
    printf 'previous config saved to %s\n' "$BACKUP"
fi
cp -- "$SEED" "$CONFIG"

# Go 1.4, which OpenWrt uses as its default first bootstrap stage, has no
# linux/arm64 host support.  The ARM64 build VM already provides a current Go
# toolchain; use its documented external-bootstrap path to build OpenWrt's
# pinned host Go instead of attempting the unsupported Go 1.4 stage.
if [[ "$(uname -s)/$(uname -m)" == Linux/aarch64 ]]; then
    command -v go >/dev/null 2>&1 || {
        printf 'native Go toolchain is required on the ARM64 build host\n' >&2
        exit 3
    }
    host_go_root="$(go env GOROOT)"
    [[ -x "$host_go_root/bin/go" && -d "$host_go_root/src" ]] || {
        printf 'invalid native Go root: %s\n' "$host_go_root" >&2
        exit 3
    }
    {
        printf 'CONFIG_GOLANG_EXTERNAL_BOOTSTRAP_ROOT="%s"\n' "$host_go_root"
        printf '# CONFIG_GOLANG_BUILD_BOOTSTRAP is not set\n'
    } >> "$CONFIG"
    printf 'using native ARM64 Go bootstrap: %s\n' "$host_go_root"
fi

make -C "$TOPDIR" defconfig

# armsr/generic selects many OpenWrt 6.18 kmods by default. They are not ABI
# compatible with Xiaomi's 6.1.138 kernel. First turn off everything selected
# by the generic target, then resolve dependencies once to expose only the
# packages that are still forced by target defaults or userspace dependencies.
filtered_config="$(mktemp "$TOPDIR/.config.xm14pro.XXXXXX")"
cleanup() { rm -f -- "$filtered_config"; }
trap cleanup EXIT
awk '
    /^CONFIG_PACKAGE_kmod-[^=]*=[ym]$/ {
        name = $0
        sub(/^CONFIG_/, "", name)
        sub(/=y$/, "", name)
        print "# CONFIG_" name " is not set"
        next
    }
    { print }
' "$CONFIG" > "$filtered_config"
mv -- "$filtered_config" "$CONFIG"
trap - EXIT
make -C "$TOPDIR" defconfig

mapfile -t remaining_kmods < <(sed -n 's/^CONFIG_PACKAGE_\(kmod-[^=]*\)=[ym]$/\1/p' "$CONFIG")
if (( ${#remaining_kmods[@]} > 0 )); then
    printf 'refusing config: Xiaomi profile still forces OpenWrt kernel modules:\n' >&2
    printf '  %s\n' "${remaining_kmods[@]}" >&2
    exit 3
fi

grep -qx 'CONFIG_TARGET_xm14pro=y' "$CONFIG"
grep -qx 'CONFIG_TARGET_xm14pro_armv8=y' "$CONFIG"
grep -qx 'CONFIG_TARGET_xm14pro_armv8_DEVICE_xiaomi_14_pro=y' "$CONFIG"
grep -qx 'CONFIG_PACKAGE_xm14pro-boot-shim=y' "$CONFIG"
grep -qx 'CONFIG_PACKAGE_luci-base=y' "$CONFIG"
if grep -Eq '^CONFIG_PACKAGE_(firewall4|nftables-json)=y$' "$CONFIG"; then
    printf 'refusing config: nftables/firewall4 is incompatible with stock kernel policy\n' >&2
    exit 3
fi

mapfile -t final_kmods < <(sed -n 's/^CONFIG_PACKAGE_\(kmod-[^=]*\)=[ym]$/\1/p' "$CONFIG")
(( ${#final_kmods[@]} == 0 )) || { printf 'refusing config: OpenWrt kmods remain selected\n' >&2; exit 3; }

printf 'shennong rootfs config prepared: %s\n' "$CONFIG"
printf 'target=xm14pro/armv8 shennong userspace; device kernel=stock Xiaomi 6.1.138\n'
printf 'OpenWrt kmods selected for Xiaomi rootfs: 0\n'

