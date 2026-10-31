#!/bin/sh
set -eu

PACKAGE_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SCRIPT="$PACKAGE_DIR/files/usr/sbin/xm14pro-openclash-compat"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT INT TERM

mkdir -p "$TEST_DIR/bin" "$TEST_DIR/state" "$TEST_DIR/sys/nwrt-ap"
: >"$TEST_DIR/openclash"

cat >"$TEST_DIR/bin/uci" <<'EOF'
#!/bin/sh
[ "$1" != -q ] || shift
command="$1"
shift
case "$command" in
	get)
		case "$1" in
			xm14openclash.main.enabled) echo 1 ;;
			xm14openclash.main.network) echo wifi_lan ;;
			xm14openclash.main.local_networks) echo 'wifi_lan usb_lan' ;;
			xm14openclash.main.fallback_device) echo nwrt-ap ;;
			xm14openclash.main.wait_timeout) echo 2 ;;
			xm14openclash.main.managed_interface) cat "$MOCK_STATE_DIR/managed" 2>/dev/null || true ;;
			openclash.config.lan_interface_name) cat "$MOCK_STATE_DIR/current" 2>/dev/null || true ;;
			network.wifi_lan.device) return 1 ;;
			network.wifi_lan.ipaddr) echo 192.168.67.1 ;;
			network.usb_lan.ipaddr) echo 192.168.66.1 ;;
			*) return 1 ;;
		esac
		;;
	set)
		printf '%s\n' "$1" >>"$MOCK_STATE_DIR/actions"
		case "$1" in
			openclash.config.lan_interface_name=*) printf '%s\n' "${1#*=}" >"$MOCK_STATE_DIR/current" ;;
			xm14openclash.main.managed_interface=*) printf '%s\n' "${1#*=}" >"$MOCK_STATE_DIR/managed" ;;
		esac
		;;
	commit) printf 'commit %s\n' "$1" >>"$MOCK_STATE_DIR/actions" ;;
esac
EOF

cat >"$TEST_DIR/bin/ubus" <<'EOF'
#!/bin/sh
printf '{"up":%s}\n' "${MOCK_UP:-true}"
EOF

cat >"$TEST_DIR/bin/jsonfilter" <<'EOF'
#!/bin/sh
while [ "$#" -gt 0 ]; do
	case "$1" in
		-e) expression="$2"; shift 2 ;;
		*) shift ;;
	esac
done
case "$expression" in
	'@.up') printf '%s\n' "${MOCK_UP:-true}" ;;
	'@.l3_device') printf '%s\n' "${MOCK_DEVICE:-nwrt-ap}" ;;
	'@.device') : ;;
	'@["ipv4-address"][0].address') printf '%s\n' "${MOCK_ADDRESS:-192.168.67.1}" ;;
esac
EOF

cat >"$TEST_DIR/bin/logger" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$MOCK_STATE_DIR/log"
EOF

cat >"$TEST_DIR/bin/ip" <<'EOF'
#!/bin/sh
exit 0
EOF

cat >"$TEST_DIR/bin/iptables" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"$MOCK_STATE_DIR/iptables-actions"
case " $* " in
	*' -C '*) exit 1 ;;
esac
exit 0
EOF

cat >"$TEST_DIR/bin/init-uci" <<'EOF'
#!/bin/sh
case "$*" in
	*'xm14openclash.main.enabled') echo 1 ;;
	*'openclash.config.enable') printf '%s\n' "${MOCK_OPENCLASH_ENABLE:-0}" ;;
esac
EOF

cat >"$TEST_DIR/bin/init-compat" <<'EOF'
#!/bin/sh
printf '%s\n' "$1" >>"$MOCK_STATE_DIR/init-actions"
EOF

chmod +x "$TEST_DIR/bin/"*
export MOCK_STATE_DIR="$TEST_DIR/state"
export XM14_OPENCLASH_UCI="$TEST_DIR/bin/uci"
export XM14_OPENCLASH_UBUS="$TEST_DIR/bin/ubus"
export XM14_OPENCLASH_JSONFILTER="$TEST_DIR/bin/jsonfilter"
export XM14_OPENCLASH_IP="$TEST_DIR/bin/ip"
export XM14_OPENCLASH_IPTABLES="$TEST_DIR/bin/iptables"
export XM14_OPENCLASH_LOGGER="$TEST_DIR/bin/logger"
export XM14_OPENCLASH_SYS_CLASS_NET="$TEST_DIR/sys"
export XM14_OPENCLASH_CONFIG="$TEST_DIR/openclash"

assert_equal() {
	[ "$1" = "$2" ] || {
		echo "assertion failed: expected [$2], got [$1]" >&2
		exit 1
	}
}

printf '0\n' >"$TEST_DIR/state/current"
"$SCRIPT" apply
assert_equal "$(cat "$TEST_DIR/state/current")" nwrt-ap
assert_equal "$(cat "$TEST_DIR/state/managed")" nwrt-ap
grep -q '^commit openclash$' "$TEST_DIR/state/actions"
grep -q -- '-t nat -A XMOC_LOCAL -d 192.168.67.1/32 -j ACCEPT' "$TEST_DIR/state/iptables-actions"
grep -q -- '-t nat -A XMOC_LOCAL -d 192.168.66.1/32 -j ACCEPT' "$TEST_DIR/state/iptables-actions"
grep -q -- '-t nat -A XMOC_LOCAL -d 127.0.0.0/8 -j ACCEPT' "$TEST_DIR/state/iptables-actions"
grep -q -- '-t nat -I OUTPUT 1 -j XMOC_LOCAL' "$TEST_DIR/state/iptables-actions"
grep -q -- '-t mangle -I PREROUTING 1 -j XMOC_LOCAL' "$TEST_DIR/state/iptables-actions"
grep -q -- '-t mangle -I OUTPUT 1 -j XMOC_LOCAL' "$TEST_DIR/state/iptables-actions"

: >"$TEST_DIR/state/actions"
: >"$TEST_DIR/state/iptables-actions"
"$SCRIPT" apply
[ ! -s "$TEST_DIR/state/actions" ]

printf 'usb0\n' >"$TEST_DIR/state/current"
: >"$TEST_DIR/state/managed"
: >"$TEST_DIR/state/actions"
"$SCRIPT" apply
assert_equal "$(cat "$TEST_DIR/state/current")" usb0
[ ! -s "$TEST_DIR/state/actions" ]
grep -q 'keeping user-selected' "$TEST_DIR/state/log"

printf 'old-ap\n' >"$TEST_DIR/state/current"
printf 'old-ap\n' >"$TEST_DIR/state/managed"
: >"$TEST_DIR/state/actions"
"$SCRIPT" apply
assert_equal "$(cat "$TEST_DIR/state/current")" nwrt-ap
assert_equal "$(cat "$TEST_DIR/state/managed")" nwrt-ap

export MOCK_UP=false
printf '0\n' >"$TEST_DIR/state/current"
if "$SCRIPT" apply; then
	echo 'down interface unexpectedly accepted' >&2
	exit 1
fi
assert_equal "$(cat "$TEST_DIR/state/current")" 0

INIT_SCRIPT="$PACKAGE_DIR/files/etc/init.d/xm14pro-openclash-compat"
export XM14_OPENCLASH_UCI="$TEST_DIR/bin/init-uci"
export XM14_OPENCLASH_COMPAT="$TEST_DIR/bin/init-compat"
. "$INIT_SCRIPT"

: >"$TEST_DIR/state/init-actions"
export MOCK_OPENCLASH_ENABLE=0
boot
assert_equal "$(cat "$TEST_DIR/state/init-actions")" apply

: >"$TEST_DIR/state/init-actions"
export MOCK_OPENCLASH_ENABLE=1
boot
assert_equal "$(cat "$TEST_DIR/state/init-actions")" wait

echo 'openclash compatibility tests: PASS'
