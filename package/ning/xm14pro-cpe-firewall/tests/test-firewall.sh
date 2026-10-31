#!/bin/sh
set -eu

TEST_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PACKAGE_DIR="$(dirname "$TEST_DIR")"
SCRIPT="$PACKAGE_DIR/files/usr/sbin/xm14pro-cpe-firewall"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM

mkdir -p "$TMP/net/rmnet_data0" "$TMP/net/wlan0" "$TMP/net/usb0" "$TMP/lock-parent"
: >"$TMP/iptables.log"

export XM14PRO_TEST_LOG="$TMP/iptables.log"
export XM14PRO_IPTABLES="$TEST_DIR/mock-bin/iptables"
export XM14PRO_SYS_CLASS_NET="$TMP/net"
export XM14PRO_FIREWALL_LOCK="$TMP/lock-parent/firewall.lock"
export PATH="$TEST_DIR/mock-bin:$PATH"

sh "$SCRIPT" apply

assert_log() {
	grep -F -- "$1" "$XM14PRO_TEST_LOG" >/dev/null || {
		echo "missing firewall operation: $1" >&2
		exit 1
	}
}

assert_log '-t nat -A XMCPE_NAT -o rmnet_data0 -j MASQUERADE'
assert_log '-A XMCPE_FWD -i wlan0 -o rmnet_data0 -j ACCEPT'
assert_log '-A XMCPE_FWD -i usb0 -o rmnet_data0 -j ACCEPT'
assert_log '-A XMCPE_IN -i rmnet_data0 -j DROP'
assert_log '-t mangle -A XMCPE_MSS -o rmnet_data0 -p tcp --tcp-flags SYN,RST SYN -j TCPMSS --clamp-mss-to-pmtu'
assert_log 'sysctl -qw net.ipv4.ip_forward=1'

sh "$SCRIPT" clear
assert_log '-t filter -F XMCPE_FWD'
assert_log '-t nat -X XMCPE_NAT'

test ! -e "$XM14PRO_FIREWALL_LOCK"
echo 'xm14pro CPE firewall mock test: PASS'
