#!/bin/sh
set -eu

TEST_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
PACKAGE_DIR="$(dirname "$TEST_DIR")"
SCRIPT="$PACKAGE_DIR/files/usr/sbin/xm14pro-wifi-profile"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM
: >"$TMP/operations.log"

export XM14PRO_WIFI_TEST_LOG="$TMP/operations.log"
export PATH="$TEST_DIR/mock-bin:$PATH"

sh "$SCRIPT" initial

assert_log() {
	grep -F -- "$1" "$XM14PRO_WIFI_TEST_LOG" >/dev/null || {
		echo "missing Wi-Fi operation: $1" >&2
		exit 1
	}
}

assert_log 'wifi config'
assert_log 'wireless.radio0.country=US'
assert_log 'wireless.radio0.band=5g'
assert_log 'wireless.radio0.channel=149'
assert_log 'wireless.radio0.htmode=HE80'
assert_log 'wireless.nwrt_5g.ssid=nwrt'
assert_log 'wireless.radio1.country=US'
assert_log 'wireless.radio1.band=6g'
assert_log 'wireless.radio1.channel=5'
assert_log 'wireless.radio1.htmode=EHT320'
assert_log 'wireless.nwrt_6g.ssid=nwrt'
assert_log 'wireless.nwrt_6g.key=1234567890'
assert_log 'delete wireless.default_radio0'
assert_log 'commit wireless'

echo 'xm14pro Wi-Fi profile mock test: PASS'
