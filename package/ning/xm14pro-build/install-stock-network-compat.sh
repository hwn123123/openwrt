#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
DNSMASQ_MAKEFILE="$TOPDIR/package/network/services/dnsmasq/Makefile"
CONNTRACK_MAKEFILE="$TOPDIR/package/libs/libnetfilter-conntrack/Makefile"

[[ -f "$DNSMASQ_MAKEFILE" ]] || { printf 'missing dnsmasq Makefile\n' >&2; exit 2; }
[[ -f "$CONNTRACK_MAKEFILE" ]] || { printf 'missing libnetfilter-conntrack Makefile\n' >&2; exit 2; }

# dnsmasq-full enables nftset by default, which selects nftables and a complete
# OpenWrt kernel-module closure.  xm14pro deliberately uses Xiaomi's stock
# iptables-capable 6.1.138 network stack, so keep nftset as the default on every
# regular OpenWrt target but not on this phone target.
if ! grep -q 'default y if ! TARGET_xm14pro' "$DNSMASQ_MAKEFILE"; then
    sed -i '/config PACKAGE_dnsmasq_full_nftset/,/default y/ s/default y/default y if ! TARGET_xm14pro/' \
        "$DNSMASQ_MAKEFILE"
fi

# The userspace conntrack library works with the stock kernel netlink ABI.  It
# must not pull in a 6.18 kmod merely to satisfy build metadata on xm14pro.
if grep -q 'DEPENDS:=+libnfnetlink +kmod-nf-conntrack-netlink +libmnl' "$CONNTRACK_MAKEFILE"; then
    sed -i 's/+kmod-nf-conntrack-netlink/+!TARGET_xm14pro:kmod-nf-conntrack-netlink/' \
        "$CONNTRACK_MAKEFILE"
fi

grep -A2 'config PACKAGE_dnsmasq_full_nftset' "$DNSMASQ_MAKEFILE" | \
    grep -q 'default y if ! TARGET_xm14pro' || {
        printf 'dnsmasq xm14pro nftset default compatibility was not applied\n' >&2
        exit 3
    }
grep -q '+!TARGET_xm14pro:kmod-nf-conntrack-netlink' "$CONNTRACK_MAKEFILE" || {
    printf 'libnetfilter-conntrack xm14pro dependency compatibility was not applied\n' >&2
    exit 3
}

printf 'installed Xiaomi stock-kernel network compatibility\n'
