#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
TOPDIR="$(cd -- "$SCRIPT_DIR/../../.." && pwd -P)"
DOCKERD_MAKEFILE="$TOPDIR/feeds/packages/utils/dockerd/Makefile"
CONTAINERD_MAKEFILE="$TOPDIR/feeds/packages/utils/containerd/Makefile"
RUNC_MAKEFILE="$TOPDIR/feeds/packages/utils/runc/Makefile"
CONTAINERD_PATCH_SOURCE="$SCRIPT_DIR/patches/docker/containerd/010-xm14pro-no-pidns-introspection.patch"
CONTAINERD_PATCH_DIR="$TOPDIR/feeds/packages/utils/containerd/patches"
RUNC_PATCH_SOURCE="$SCRIPT_DIR/patches/docker/runc/010-xm14pro-no-userns-setgroups.patch"
RUNC_WRAPPER_SOURCE="$SCRIPT_DIR/patches/docker/runc/runc-wrapper.sh"
RUNC_COMPAT_SOURCE="$SCRIPT_DIR/patches/docker/runc/xm14pro-runc-compat.uc"
RUNC_PATCH_DIR="$TOPDIR/feeds/packages/utils/runc/patches"
RUNC_FILES_DIR="$TOPDIR/feeds/packages/utils/runc/files"

[[ -f "$DOCKERD_MAKEFILE" ]] || {
    printf 'missing dockerd feed Makefile: %s\n' "$DOCKERD_MAKEFILE" >&2
    exit 2
}
[[ -f "$CONTAINERD_MAKEFILE" ]] || {
    printf 'missing containerd feed Makefile: %s\n' "$CONTAINERD_MAKEFILE" >&2
    exit 2
}
[[ -f "$RUNC_MAKEFILE" ]] || {
    printf 'missing runc feed Makefile: %s\n' "$RUNC_MAKEFILE" >&2
    exit 2
}
for source in \
    "$CONTAINERD_PATCH_SOURCE" "$RUNC_PATCH_SOURCE" \
    "$RUNC_WRAPPER_SOURCE" "$RUNC_COMPAT_SOURCE"; do
    [[ -f "$source" ]] || {
        printf 'missing Docker compatibility source: %s\n' "$source" >&2
        exit 2
    }
done

# shennong boots Xiaomi's exact stock 6.1 kernel.  The required Docker
# facilities (namespaces, cgroup v2, seccomp, veth, bridge, conntrack, NAT and
# overlayfs) are built into that kernel, so OpenWrt 6.18 kmods must never be
# selected or installed.  Keep every upstream dependency on normal targets and
# suppress only the redundant kernel/firewall packages on xm14pro.
python3 - "$DOCKERD_MAKEFILE" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
replacements = {
    "+iptables-mod-extra": "+!TARGET_xm14pro:iptables-mod-extra",
    "+IPV6:ip6tables": "+(IPV6&&!TARGET_xm14pro):ip6tables",
    "+IPV6:kmod-ipt-nat6": "+(IPV6&&!TARGET_xm14pro):kmod-ipt-nat6",
    "+kmod-ipt-nat": "+!TARGET_xm14pro:kmod-ipt-nat",
    "+kmod-ipt-physdev": "+!TARGET_xm14pro:kmod-ipt-physdev",
    "+kmod-nf-ipvs": "+!TARGET_xm14pro:kmod-nf-ipvs",
    "+kmod-veth": "+!TARGET_xm14pro:kmod-veth",
    "+uci-firewall": "+!TARGET_xm14pro:uci-firewall",
}
for old, new in replacements.items():
    if new in text:
        continue
    if old not in text:
        raise SystemExit(f"dockerd dependency anchor missing: {old}")
    text = text.replace(old, new, 1)
path.write_text(text)
PY

for dependency in \
    iptables-mod-extra ip6tables kmod-ipt-nat6 kmod-ipt-nat \
    kmod-ipt-physdev kmod-nf-ipvs kmod-veth uci-firewall; do
    grep -q "!TARGET_xm14pro.*:$dependency" "$DOCKERD_MAKEFILE" || {
        printf 'dockerd Xiaomi compatibility missing for %s\n' "$dependency" >&2
        exit 3
    }
done

# containerd can use the kernel's overlayfs snapshotter.  Its unconditional
# btrfs-progs dependency is optional here and would otherwise pull an entire
# OpenWrt Btrfs kmod closure that cannot run on Xiaomi's stock kernel.
if ! grep -q '+!TARGET_xm14pro:btrfs-progs' "$CONTAINERD_MAKEFILE"; then
    sed -i 's/+btrfs-progs/+!TARGET_xm14pro:btrfs-progs/' \
        "$CONTAINERD_MAKEFILE"
fi
grep -q '+!TARGET_xm14pro:btrfs-progs' "$CONTAINERD_MAKEFILE" || {
    printf 'containerd Xiaomi overlayfs compatibility was not applied\n' >&2
    exit 3
}

# The stock kernel has CONFIG_PID_NS disabled. containerd's introspection API
# must report PID namespace inode zero instead of refusing to start Docker.
python3 - "$CONTAINERD_MAKEFILE" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text()
release = re.search(r"^PKG_RELEASE:=(\d+)$", text, re.MULTILINE)
if not release:
    raise SystemExit("containerd PKG_RELEASE is missing")
if int(release.group(1)) < 2:
    text = text[:release.start(1)] + "2" + text[release.end(1):]
path.write_text(text)
PY
mkdir -p -- "$CONTAINERD_PATCH_DIR"
cp -- "$CONTAINERD_PATCH_SOURCE" \
    "$CONTAINERD_PATCH_DIR/010-xm14pro-no-pidns-introspection.patch"

# runc gets a target-specific front end. It removes only the PID, IPC and
# mqueue entries which the stock Xiaomi kernel cannot implement; mount, net,
# UTS and cgroup isolation remain managed by runc. The source patch also avoids
# probing the absent USER_NS setgroups policy file.
python3 - "$RUNC_MAKEFILE" <<'PY'
from pathlib import Path
import re
import sys

path = Path(sys.argv[1])
text = path.read_text()

release = re.search(r"^PKG_RELEASE:=(\d+)$", text, re.MULTILINE)
if not release:
    raise SystemExit("runc PKG_RELEASE is missing")
if int(release.group(1)) < 3:
    text = text[:release.start(1)] + "3" + text[release.end(1):]

depends = "  DEPENDS:=$(ARCH_DEPENDS) +KERNEL_SECCOMP_FILTER:libseccomp"
if depends + " +ucode" not in text:
    if depends not in text:
        raise SystemExit("runc dependency anchor is missing")
    text = text.replace(depends, depends + " +ucode", 1)

old_install = """\t$(INSTALL_DIR) $(1)/usr/sbin/\n\t$(INSTALL_BIN) $(GO_PKG_BUILD_DIR)/src/$(GO_PKG)/runc $(1)/usr/sbin/"""
new_install = """\t$(INSTALL_DIR) $(1)/usr/sbin/ $(1)/usr/libexec/\n\t$(INSTALL_BIN) $(GO_PKG_BUILD_DIR)/src/$(GO_PKG)/runc $(1)/usr/sbin/runc.real\n\t$(INSTALL_BIN) ./files/runc-wrapper.sh $(1)/usr/sbin/runc\n\t$(INSTALL_BIN) ./files/xm14pro-runc-compat.uc $(1)/usr/libexec/xm14pro-runc-compat.uc"""
if new_install not in text:
    if old_install not in text:
        raise SystemExit("runc install anchor is missing")
    text = text.replace(old_install, new_install, 1)

path.write_text(text)
PY
mkdir -p -- "$RUNC_PATCH_DIR" "$RUNC_FILES_DIR"
cp -- "$RUNC_PATCH_SOURCE" \
    "$RUNC_PATCH_DIR/010-xm14pro-no-userns-setgroups.patch"
cp -- "$RUNC_WRAPPER_SOURCE" "$RUNC_FILES_DIR/runc-wrapper.sh"
cp -- "$RUNC_COMPAT_SOURCE" "$RUNC_FILES_DIR/xm14pro-runc-compat.uc"

grep -q 'PKG_RELEASE:=2' "$CONTAINERD_MAKEFILE" || {
    printf 'containerd Xiaomi release was not applied\n' >&2
    exit 3
}
grep -q 'PKG_RELEASE:=3' "$RUNC_MAKEFILE" || {
    printf 'runc Xiaomi release was not applied\n' >&2
    exit 3
}
grep -q 'xm14pro-runc-compat.uc' "$RUNC_MAKEFILE" || {
    printf 'runc Xiaomi wrapper was not applied\n' >&2
    exit 3
}

printf 'dockerd configured to use Xiaomi stock-kernel container facilities\n'
