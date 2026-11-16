#!/bin/sh
# netifd protocol "medvemodem": a data session the engine dials over AT
# (native backend, spec 19.4). The engine runs the modem's dial recipe under
# its AT lease and answers with the address; this handler only puts that on
# the interface. medvemodem writes these sections itself:
#
#   config interface 'mm0'
#       option proto 'medvemodem'
#       option device 'usb0'                    # the modem's network device
#       option device_id 'usb-serial:...'
#       option apn 'internet'                   # optional
#       option pdptype 'ipv4'                   # ipv4 | ipv6 | ipv4v6
#       option auth 'pap'                       # optional: pap | chap | both | none
#       option username 'user'                  # the APN login; the engine reads these
#       option password 'secret'                # three from this section itself, so the
#                                               # password never shows on a command line
#
# Part of medvemodem; GPL-3.0-only.

[ -n "$INCLUDE_ONLY" ] || {
	. /lib/functions.sh
	. ../netifd-proto.sh
	init_proto "$@"
}

proto_medvemodem_init_config() {
	available=1
	no_device=1
	proto_config_add_string "device:device"
	proto_config_add_string device_id
	proto_config_add_string apn
	proto_config_add_string pdptype
	proto_config_add_defaults
}

# the request body, built with jshn so nothing in a value can break the JSON
medvemodem_request() {
	json_init
	json_add_string id "$1"
	[ -n "$2" ] && json_add_string apn "$2"
	[ -n "$3" ] && json_add_string pdptype "$3"
	json_add_string interface "$4"
	json_dump
}

proto_medvemodem_setup() {
	local interface="$1"
	local device device_id apn pdptype reply result ipv4 plen gw why dns pdp zone
	local $PROTO_DEFAULT_OPTIONS
	json_get_vars device device_id apn pdptype $PROTO_DEFAULT_OPTIONS

	if [ -z "$device" ] || [ -z "$device_id" ]; then
		echo "medvemodem: interface $interface needs options device and device_id"
		proto_notify_error "$interface" MISSING_OPTIONS
		proto_block_restart "$interface"
		return 1
	fi

	reply=$(ubus -t 180 call medvemodem dial "$(medvemodem_request "$device_id" "$apn" "$pdptype" "$interface")" 2>&1) || {
		echo "medvemodem: the engine did not answer: $reply"
		proto_notify_error "$interface" ENGINE_UNAVAILABLE
		# the engine stopped (switched off, or procd gave up): an answer comes at once, and netifd would run the
		# setup again straight away; a pause keeps that from becoming a loop (audit 2026-10-20)
		sleep "${MEDVEMODEM_PROTO_PAUSE:-10}"
		return 1
	}
	why=$(printf '%s' "$reply" | jsonfilter -e '@.refused' 2>/dev/null)
	if [ -n "$why" ]; then
		# a configuration problem: retrying the same request cannot help
		echo "medvemodem: dial refused for $device_id: $why"
		proto_notify_error "$interface" DIAL_REFUSED
		proto_block_restart "$interface"
		return 1
	fi
	result=$(printf '%s' "$reply" | jsonfilter -e '@.result' 2>/dev/null)
	if [ "$result" != connected ]; then
		echo "medvemodem: dial failed for $device_id: $(printf '%s' "$reply" | jsonfilter -e '@.error' 2>/dev/null)"
		proto_notify_error "$interface" DIAL_FAILED
		return 1
	fi
	ipv4=$(printf '%s' "$reply" | jsonfilter -e '@.ipv4')
	plen=$(printf '%s' "$reply" | jsonfilter -e '@.prefix_len')
	# no gateway: the default route on the link (a cellular link is point to point; the modem names none)
	gw=$(printf '%s' "$reply" | jsonfilter -e '@.gateway' 2>/dev/null)
	pdp=$(printf '%s' "$reply" | jsonfilter -e '@.pdp' 2>/dev/null)

	# a link that answers no ARP (Intel XMM over NCM): with ARP on, the address carries nothing
	[ "$(printf '%s' "$reply" | jsonfilter -e '@.arp' 2>/dev/null)" = false ] && ip link set dev "$device" arp off

	# an IPv6-only context has no IPv4 address: none is added, no IPv4 default route (audit r108, R108-04)
	case "$ipv4" in
	*.*.*.*) ;;
	*)
		ipv4=
		case "$pdp" in
		IPV6) ;;
		*)
			echo "medvemodem: no IPv4 address for $device_id ($pdp)"
			proto_notify_error "$interface" NO_ADDRESS
			return 1 ;;
		esac ;;
	esac
	case "$plen" in ''|*[!0-9]*) plen=32 ;; esac

	proto_init_update "$device" 1
	if [ -n "$ipv4" ]; then
		proto_add_ipv4_address "$ipv4" "$plen"
		[ "${defaultroute:-1}" = 0 ] || proto_add_ipv4_route "0.0.0.0" 0 "$gw" "" "$metric"
	fi
	if [ "${peerdns:-1}" != 0 ]; then
		for dns in $(printf '%s' "$reply" | jsonfilter -e '@.dns[*]' 2>/dev/null); do
			proto_add_dns_server "$dns"
		done
	fi
	proto_send_update "$interface"

	# IPv6 comes by router advertisement on the link, not from AT+CGPADDR: a dhcpv6 child, as proto qmi does
	case "$pdp" in
	IPV6|IPV4V6)
		json_init
		json_add_string name "${interface}_6"
		json_add_string ifname "@$interface"
		[ "$pdp" = IPV4V6 ] && json_add_string iface_464xlat "0"
		json_add_string proto "dhcpv6"
		json_add_string extendprefix 1
		proto_add_dynamic_defaults
		zone=$(zone_of "$interface")
		[ -n "$zone" ] && json_add_string zone "$zone"
		json_close_object
		ubus call network add_dynamic "$(json_dump)"
		;;
	esac
}

# The data session is left up (HANDOFF 171): netifd takes an interface down and up for a metric change (the modems'
# priority), and a hang-up made each of those a 30-60 s re-dial; the next setup reuses the session the modem holds
# (a recovery asks the engine for a new one). `medvemodem hangup <id>` ends it on purpose.
# the firewall zone that lists the interface: fw3 where it is, else the UCI (firewall4 of 24.10/25.12 has no fw3)
zone_of() {
	local z
	z=$(fw3 -q network "$1" 2>/dev/null) && [ -n "$z" ] && { echo "$z"; return; }
	uci -q show firewall 2>/dev/null | sed -n "s/^firewall\.\([^.]*\)\.network=.*'$1'.*/\1/p" | head -1 |
		while read -r s; do uci -q get "firewall.$s.name"; done
}

proto_medvemodem_teardown() {
	local interface="$1"

	proto_init_update "*" 0
	proto_send_update "$interface"
}

[ -n "$INCLUDE_ONLY" ] || add_protocol medvemodem
