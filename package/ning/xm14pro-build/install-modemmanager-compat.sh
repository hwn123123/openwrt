#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
SOURCE_DIR="$SCRIPT_DIR/patches/modemmanager"
PATCH_DIR="$TOPDIR/feeds/packages/net/modemmanager/patches"
LIBQMI_SOURCE_DIR="$SCRIPT_DIR/patches/libqmi"
LIBQMI_PATCH_DIR="$TOPDIR/feeds/packages/libs/libqmi/patches"
MODEMMANAGER_MAKEFILE="$TOPDIR/feeds/packages/net/modemmanager/Makefile"
LIBQMI_MAKEFILE="$TOPDIR/feeds/packages/libs/libqmi/Makefile"
MODEMMANAGER_RELEASE=46
LIBQMI_RELEASE=9

# These were experiments from the superseded ModemManager/WDS dialing path.
# Mobile data is now owned by Xiaomi's stock NICM/DSI runtime, so keeping any
# of them would rebuild an unused second data-plane implementation into nwrt.
OBSOLETE_MODEMMANAGER_PATCHES=(
    0010-qcom-soc-activate-primary-gw-in-uim-client.patch
    0011-qcom-soc-register-xiaomi-ipa-mux.patch
    0012-qcom-soc-read-xiaomi-ipa-dpm-endpoints.patch
    0017-qcom-soc-configure-xiaomi-ipa-data-formats.patch
    0018-qcom-soc-keep-xiaomi-ipa-parent-up.patch
    0019-qcom-soc-bind-xiaomi-wds-client-to-mux.patch
    0020-qcom-soc-bind-xiaomi-wds-subscription.patch
    0022-qcom-soc-skip-phone-packet-attach.patch
    0023-qcom-soc-reuse-stock-xiaomi-rmnet-topology.patch
    0024-qcom-soc-open-xiaomi-dpm-port-once.patch
    0025-qcom-soc-match-xiaomi-phone-start-network.patch
    0026-qcom-soc-open-xiaomi-low-latency-dpm-port.patch
    0027-qcom-soc-configure-xiaomi-normal-and-low-latency-wda.patch
    0028-qcom-soc-bind-xiaomi-qos-client.patch
    0029-qcom-soc-start-xiaomi-dual-stack-together.patch
    0030-qcom-soc-run-wds-route-lookup-before-data-call.patch
    0031-qcom-soc-run-xiaomi-nicm-wds-preflight.patch
    0032-qcom-soc-prepare-xiaomi-nicm-aux-path.patch
    0033-qcom-soc-sync-xiaomi-runtime-data-profile.patch
    0034-qcom-soc-match-xiaomi-data-client-order.patch
)
OBSOLETE_LIBQMI_PATCHES=(
    0002-wds-add-phone-start-network-fields.patch
)

[[ -f "$TOPDIR/rules.mk" ]] || { printf 'invalid OpenWrt tree: %s\n' "$TOPDIR" >&2; exit 2; }
compgen -G "$SOURCE_DIR/*.patch" >/dev/null || {
    printf 'missing ModemManager compatibility patches: %s\n' "$SOURCE_DIR" >&2
    exit 2
}
[[ -d "$PATCH_DIR" ]] || {
    printf 'ModemManager feed is not installed: %s\n' "$PATCH_DIR" >&2
    exit 2
}
compgen -G "$LIBQMI_SOURCE_DIR/*.patch" >/dev/null || {
    printf 'missing libqmi compatibility patches: %s\n' "$LIBQMI_SOURCE_DIR" >&2
    exit 2
}
mkdir -p -- "$LIBQMI_PATCH_DIR"
# Keep both the canonical package/ning patch set and the generated feed patch
# directory free of retired data-call experiments.
for NAME in "${OBSOLETE_MODEMMANAGER_PATCHES[@]}"; do
    for OBSOLETE in "$SOURCE_DIR/$NAME" "$PATCH_DIR/$NAME"; do
        if [[ -e "$OBSOLETE" ]]; then
            rm -f -- "$OBSOLETE"
            printf 'removed obsolete ModemManager compatibility patch: %s\n' "$OBSOLETE"
        fi
    done
done

for NAME in "${OBSOLETE_LIBQMI_PATCHES[@]}"; do
    for OBSOLETE in "$LIBQMI_SOURCE_DIR/$NAME" "$LIBQMI_PATCH_DIR/$NAME"; do
        if [[ -e "$OBSOLETE" ]]; then
            rm -f -- "$OBSOLETE"
            printf 'removed obsolete libqmi compatibility patch: %s\n' "$OBSOLETE"
        fi
    done
done

for SOURCE in "$SOURCE_DIR"/*.patch; do
    TARGET="$PATCH_DIR/$(basename -- "$SOURCE")"
    install -m 0644 -- "$SOURCE" "$TARGET"
    printf 'installed ModemManager compatibility patch: %s\n' "$TARGET"
done

# These device compatibility patches change the ModemManager binary. Keep a
# distinct package revision so a hot update cannot be mistaken for the
# previous patched build with the same upstream version.
sed -i -E "s/^PKG_RELEASE:=.*/PKG_RELEASE:=$MODEMMANAGER_RELEASE/" \
    "$MODEMMANAGER_MAKEFILE"
printf 'set ModemManager package release: %s\n' "$MODEMMANAGER_RELEASE"

# PPP is useful for USB dongles on normal OpenWrt targets, but the Xiaomi phone
# modem uses the stock QRTR/QMI/NICM data path.  Selecting ppp here would pull
# kmod-ppp built for OpenWrt's 6.18 kernel into a 6.1.138 Android boot chain.
# Keep the upstream dependency everywhere except the xm14pro target.
if grep -Eq '^[[:space:]]*\+ppp[[:space:]]*\\$' "$MODEMMANAGER_MAKEFILE"; then
    sed -i -E 's/^([[:space:]]*)\+ppp([[:space:]]*\\)$/\1+!TARGET_xm14pro:ppp\2/' \
        "$MODEMMANAGER_MAKEFILE"
fi
grep -q '+!TARGET_xm14pro:ppp' "$MODEMMANAGER_MAKEFILE" || {
    printf 'ModemManager Xiaomi PPP dependency compatibility was not applied\n' >&2
    exit 3
}

sed -i -E "s/^PKG_RELEASE:=.*/PKG_RELEASE:=$LIBQMI_RELEASE/" \
    "$LIBQMI_MAKEFILE"
printf 'set libqmi package release: %s\n' "$LIBQMI_RELEASE"

for SOURCE in "$LIBQMI_SOURCE_DIR"/*.patch; do
    TARGET="$LIBQMI_PATCH_DIR/$(basename -- "$SOURCE")"
    install -m 0644 -- "$SOURCE" "$TARGET"
    printf 'installed libqmi compatibility patch: %s\n' "$TARGET"
done

