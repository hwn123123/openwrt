#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Xiaomi 14 Pro stock DSI data-call protocol. Generic OpenWrt protocols are
# intentionally not modified or inherited here.

[ -x /usr/libexec/xm14pro-nicm/dsi-dial ] || exit 0
[ -x /usr/libexec/xm14pro-modem/dsi-session ] || exit 0

. /lib/functions.sh
. /lib/netifd/netifd-proto.sh

init_proto "$@"

STATE_ROOT=/var/run/xm14pro

state_value() {
	local file="$1" key="$2"
	sed -n "s/^${key}=//p" "$file" | tail -n 1
}

stop_session() {
	local interface="$1"
	: > "$STATE_ROOT/dsi-stop-${interface}"
	proto_kill_command "$interface" TERM
}

proto_xm14pro_init_config() {
	available=1
	no_device=1
	proto_config_add_string apn
	proto_config_add_string iptype
	proto_config_add_int timeout
	proto_config_add_int metric
	proto_config_add_boolean defaultroute
	proto_config_add_boolean peerdns
	proto_config_add_defaults
}

proto_xm14pro_setup() {
	local interface="$1"
	local state error count device control_device metric timeout defaultroute peerdns
	local ipv4_address ipv4_prefix ipv4_gateway ipv4_dns1 ipv4_dns2
	local ipv6_address ipv6_prefix ipv6_gateway ipv6_dns1 ipv6_dns2

	json_get_vars metric timeout defaultroute peerdns
	case "$timeout" in ''|*[!0-9]*) timeout=120 ;; esac
	[ "$timeout" -ge 10 ] || timeout=10
	case "$metric" in ''|*[!0-9]*) metric=10 ;; esac
	[ -n "$defaultroute" ] || defaultroute=1
	[ -n "$peerdns" ] || peerdns=1

	mkdir -p "$STATE_ROOT"
	state="$STATE_ROOT/dsi-${interface}.state"
	error="$STATE_ROOT/dsi-${interface}.error"
	rm -f "$state" "$error" "$STATE_ROOT/dsi-stop-${interface}"

	proto_run_command "$interface" \
		/usr/libexec/xm14pro-modem/dsi-session "$interface"

	count=0
	while [ "$count" -lt "$timeout" ]; do
		[ -s "$state" ] && break
		if [ -s "$error" ]; then
			echo "Xiaomi DSI data call failed: $(cat "$error")"
			proto_notify_error "$interface" XM14PRO_DSI_CALL_FAILED
			proto_block_restart "$interface"
			stop_session "$interface"
			return 1
		fi
		count=$((count + 1))
		sleep 1
	done

	if [ ! -s "$state" ]; then
		echo "Xiaomi DSI data call timed out after ${timeout}s"
		proto_notify_error "$interface" XM14PRO_DSI_TIMEOUT
		proto_block_restart "$interface"
		stop_session "$interface"
		return 1
	fi

	device="$(state_value "$state" device)"
	case "$device" in
		''|*[!A-Za-z0-9_.-]*)
			proto_notify_error "$interface" XM14PRO_DSI_BAD_DEVICE
			proto_block_restart "$interface"
			stop_session "$interface"
			return 1
			;;
	esac
	[ -e "/sys/class/net/$device" ] || {
		proto_notify_error "$interface" XM14PRO_DSI_DEVICE_MISSING
		proto_block_restart "$interface"
		stop_session "$interface"
		return 1
	}

	ipv4_address="$(state_value "$state" ipv4_address)"
	ipv4_prefix="$(state_value "$state" ipv4_prefix)"
	ipv4_gateway="$(state_value "$state" ipv4_gateway)"
	ipv4_dns1="$(state_value "$state" ipv4_dns1)"
	ipv4_dns2="$(state_value "$state" ipv4_dns2)"
	ipv6_address="$(state_value "$state" ipv6_address)"
	ipv6_prefix="$(state_value "$state" ipv6_prefix)"
	ipv6_gateway="$(state_value "$state" ipv6_gateway)"
	ipv6_dns1="$(state_value "$state" ipv6_dns1)"
	ipv6_dns2="$(state_value "$state" ipv6_dns2)"

	# Stock DSI owns the complete vendor rmnet link and installs its assigned IPv4
	# and IPv6 addresses.  Do not attach that link as a netifd device: taking
	# netifd ownership of a live Xiaomi vendor rmnet link triggers a kernel panic.
	# IPv6 receives its default route from the modem RA; only IPv4 needs a route.
	if [ "$defaultroute" != 0 ] && [ -n "$ipv4_address" ] &&
	   [ -n "$ipv4_gateway" ]; then
		if ! ip route replace default via "$ipv4_gateway" dev "$device" \
		     src "$ipv4_address" metric "$metric"; then
			proto_notify_error "$interface" XM14PRO_DSI_IPV4_ROUTE_FAILED
			proto_block_restart "$interface"
			stop_session "$interface"
			return 1
		fi
	fi

	# netifd requires an ifname for a link-up protocol update.  Give it a private
	# addressless dummy device so it never takes ownership of the vendor rmnet
	# link.  The real data interface remains published separately below.
	control_device=xm14cell
	if ! ip link show dev "$control_device" >/dev/null 2>&1; then
		if ! ip link add "$control_device" type dummy; then
			proto_notify_error "$interface" XM14PRO_DSI_CONTROL_DEVICE_FAILED
			proto_block_restart "$interface"
			stop_session "$interface"
			return 1
		fi
	fi
	ip link set dev "$control_device" up || {
		proto_notify_error "$interface" XM14PRO_DSI_CONTROL_DEVICE_FAILED
		proto_block_restart "$interface"
		stop_session "$interface"
		return 1
	}
	proto_init_update "$control_device" 1
	proto_set_keep 1
	if [ "$peerdns" != 0 ]; then
		[ -z "$ipv4_dns1" ] || proto_add_dns_server "$ipv4_dns1"
		[ -z "$ipv4_dns2" ] || proto_add_dns_server "$ipv4_dns2"
		[ -z "$ipv6_dns1" ] || proto_add_dns_server "$ipv6_dns1"
		[ -z "$ipv6_dns2" ] || proto_add_dns_server "$ipv6_dns2"
	fi
	proto_add_data
	json_add_string device "$device"
	[ -z "$ipv4_address" ] || json_add_string ipv4_address "$ipv4_address"
	[ -z "$ipv4_prefix" ] || json_add_int ipv4_prefix "$ipv4_prefix"
	[ -z "$ipv4_gateway" ] || json_add_string ipv4_gateway "$ipv4_gateway"
	[ -z "$ipv6_address" ] || json_add_string ipv6_address "$ipv6_address"
	[ -z "$ipv6_prefix" ] || json_add_int ipv6_prefix "$ipv6_prefix"
	[ -z "$ipv6_gateway" ] || json_add_string ipv6_gateway "$ipv6_gateway"
	proto_close_data
	json_add_int metric "$metric"
	proto_send_update "$interface"
	logger -t xm14-cellular \
		"netifd published stock DSI session on externally managed $device after ${count}s"
}

proto_xm14pro_teardown() {
	local interface="$1"
	stop_session "$interface"
}

add_protocol xm14pro
