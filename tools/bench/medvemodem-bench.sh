#!/bin/sh
# medvemodem bench run: one modem through one or more backends on a real router.
#
#   medvemodem-bench.sh info
#   medvemodem-bench.sh probe [--at]
#       read only: each modem's USB configurations and the drivers of its
#       interfaces, a usb-mode.json rule for its ID, ModemManager's view, QMI
#       through ModemManager's proxy (on an MBIM port: QMI inside MBIM);
#       --at adds the composition and radio queries through the engine's AT
#       path (the engine refuses where AT would disturb the modem's owner)
#   medvemodem-bench.sh run <device-id> <backend>... [--yes] [--keep] [--only-uplink]
#       backend: modemmanager | native | qmodem
#       --only-uplink: the modem is the router's only uplink; the router has
#       no internet while the run takes it down, so connect over LAN or Wi-Fi
#
# For each backend: pick it for this modem only, bring the session up, check
# address, route, ping and DNS through the modem's own interface (on native
# also probe for the AT port and ask ATI), take a
# sanitized diagnostic profile, then release and verify the router's
# configuration is back to what it was. Nothing is restored by copying files:
# the engine takes back its own changes, and any difference left over is a
# finding written to the report.
#
# Output: /tmp/mm-bench-<time>/ and a .tar.gz of it; IMEI/ICCID/IMSI and phone
# numbers are masked. Run it from LAN or Wi-Fi, not through the modem under
# test: the script refuses when the default route uses that modem, unless
# --only-uplink is given and this shell is not reached through it.
#
# Part of medvemodem; GPL-3.0-only.

set -u

# R: a root prefix for tests on a host; empty on the router
R="${MM_BENCH_ROOT:-}"
OUT="${MM_BENCH_OUT:-/tmp}/mm-bench-$(date +%Y%m%d-%H%M%S)"
PING_TARGET="${MM_BENCH_PING:-1.1.1.1}"
DNS_NAME="${MM_BENCH_DNS:-ya.ru}"
CONFIGS="network firewall medvemodem qmodem"
YES=0
KEEP=0
ONLY_UPLINK=0
FAILS=0

say() { printf '%s\n' "$*"; printf '%s\n' "$*" >>"$OUT/report.txt"; }
step() { say "== $*"; }
ok() { say "   ok   $*"; }
bad() { say "   FAIL $*"; FAILS=$((FAILS + 1)); }
note() { say "   ..   $*"; }

# mask what identifies a person or a SIM: 14-20 digit runs, +7 numbers
mask() { sed -E 's/([0-9]{4})[0-9]{6,12}([0-9]{4})/\1••••\2/g; s/(\+?7|8)[0-9]{6}([0-9]{4})/\1••••••\2/g'; }

jget() { printf '%s' "$1" | jsonfilter -e "$2" 2>/dev/null; }

have() { command -v "$1" >/dev/null 2>&1; }

snapshot() {
	mkdir -p "$OUT/$1"
	for c in $CONFIGS; do
		[ -f "$R/etc/config/$c" ] && cp "$R/etc/config/$c" "$OUT/$1/$c"
	done
}

compare() {
	same=1
	for c in $CONFIGS; do
		a="$OUT/$1/$c"; b="$OUT/$2/$c"
		if [ -f "$a" ] || [ -f "$b" ]; then
			if ! cmp -s "$a" "$b" 2>/dev/null; then
				same=0
				diff -u "$a" "$b" >"$OUT/$2-$c.diff" 2>&1
				bad "/etc/config/$c differs from $1 (see $2-$c.diff)"
			fi
		fi
	done
	[ "$same" = 1 ] && ok "configuration is back to $1"
}

info() {
	step "system"
	cat "$R/etc/openwrt_release" 2>/dev/null | sed 's/^/   /' >>"$OUT/report.txt"
	say "   kernel $(uname -r), $(uname -m)"
	step "packages"
	for p in medvemodem medvemodem-lpac modemmanager uqmi umbim qmodem luci-app-qmodem luci-app-qmodem-next \
		kmod-usb-net-qmi-wwan kmod-usb-net-cdc-mbim kmod-usb-net-cdc-ncm kmod-usb-net-rndis kmod-usb-serial-option; do
		v=$(apk list --installed "$p" 2>/dev/null | head -n1)
		[ -z "$v" ] && v=$(opkg status "$p" 2>/dev/null | sed -n 's/^Version: //p')
		say "   $(printf '%-28s' "$p") ${v:-—}"
	done
	step "services"
	for s in medvemodemd modemmanager qmodem_init; do
		if [ -x "$R/etc/init.d/$s" ]; then
			if "$R/etc/init.d/$s" running >/dev/null 2>&1; then st=running; else st=stopped; fi
			say "   $(printf '%-16s' "$s") $st"
		fi
	done
	[ -x "$R/lib/netifd/proto/qmi.sh" ] && say "   netifd proto qmi  present" || say "   netifd proto qmi  —"
	[ -x "$R/lib/netifd/proto/mbim.sh" ] && say "   netifd proto mbim present" || say "   netifd proto mbim —"
	[ -x "$R/lib/netifd/proto/medvemodem.sh" ] && say "   netifd proto medvemodem present" || say "   netifd proto medvemodem —"
	[ -x "$R/lib/netifd/proto/modemmanager.sh" ] && say "   netifd proto modemmanager present" || say "   netifd proto modemmanager —"
	step "usb"
	for d in "$R"/sys/bus/usb/devices/*; do
		[ -f "$d/idVendor" ] || continue
		say "   $(basename "$d") $(cat "$d/idVendor"):$(cat "$d/idProduct") $(cat "$d/product" 2>/dev/null)"
	done
	step "engine"
	medvemodem list 2>&1 | mask >"$OUT/list.json"
	medvemodem status 2>&1 | mask >"$OUT/status.json"
	# an upgrade that did not restart the daemon leaves the old engine running
	running=$(jsonfilter -i "$OUT/status.json" -e '@.version' 2>/dev/null)
	# the whole version and release on both sides: the engine says 0.15.0-r135, apk lists medvemodem-0.15.0-r135 and
	# opkg 0.15.0-r135 or 0.15.0-135 (audit of r135: the release was cut from the package's only, every run stopped)
	pkg=$(apk list --installed medvemodem 2>/dev/null | head -n1 | sed -n 's/^medvemodem-\([0-9][^ ]*\).*/\1/p')
	[ -z "$pkg" ] && pkg=$(opkg status medvemodem 2>/dev/null | sed -n 's/^Version: \([^ ]*\).*/\1/p')
	running=$(printf '%s' "$running" | sed 's/-r\{0,1\}\([0-9][0-9]*\)$/-r\1/')
	pkg=$(printf '%s' "$pkg" | sed 's/-r\{0,1\}\([0-9][0-9]*\)$/-r\1/')
	if [ -n "$running" ] && [ -n "$pkg" ] && [ "$running" != "$pkg" ]; then
		bad "the running engine is $running but the installed package is $pkg: /etc/init.d/medvemodemd restart"
		ENGINE_STALE=1
	fi
	medvemodem diag quick 2>&1 | mask >"$OUT/diag-quick.txt"
	say "   list, status and diag quick saved"
	have mmcli && mmcli -L 2>&1 | mask >"$OUT/mmcli-L.txt"
}

# a command under a time limit where the router has `timeout`
tmo() { if have timeout; then timeout "$@"; else shift; "$@"; fi; }

# the modems' USB side: configurations, interfaces with their drivers, a usb-mode.json rule for the ID
usb_probe() {
	step "usb configurations"
	for d in "$R"/sys/bus/usb/devices/*; do
		[ -f "$d/idVendor" ] || continue
		v=$(cat "$d/idVendor"); p=$(cat "$d/idProduct"); b=$(basename "$d")
		case $v in 05c6|1e2d|413c|0489|1bc7|0e8d|12d1|2cb7|19d2|2c7c|1199|03f0|3566|8087) ;; *) continue ;; esac
		# the revision tells products apart that share an ID (12d1:1506 rev 0 is an E392, others the E3372 stick)
		say "   $b $v:$p rev $(cat "$d/bcdDevice" 2>/dev/null) $(cat "$d/product" 2>/dev/null) — configuration $(cat "$d/bConfigurationValue" 2>/dev/null) of $(cat "$d/bNumConfigurations" 2>/dev/null)"
		for i in "$d/$b":*; do
			[ -d "$i" ] || continue
			drv=—
			[ -L "$i/driver" ] && drv=$(basename "$(readlink "$i/driver")")
			say "     interface $(cat "$i/bInterfaceNumber" 2>/dev/null) class $(cat "$i/bInterfaceClass" 2>/dev/null)/$(cat "$i/bInterfaceSubClass" 2>/dev/null)/$(cat "$i/bInterfaceProtocol" 2>/dev/null) $drv"
		done
		if grep -qi "\"$v:$p\"" "$R/etc/usb-mode.json" 2>/dev/null; then
			note "usb-mode.json has a rule for $v:$p:"
			grep -i -A4 "\"$v:$p\"" "$R/etc/usb-mode.json" | sed 's/^/        /' >>"$OUT/report.txt"
		fi
	done
	if have mmcli; then
		for m in $(mmcli -L 2>/dev/null | sed -n 's|.*/Modem/\([0-9]*\).*|\1|p'); do
			mmcli -m "$m" 2>&1 | mask >"$OUT/mmcli-m$m.txt"
		done
	fi
}

# QMI through the proxy ModemManager uses; on an MBIM port QMI inside MBIM (Qualcomm's EXT_QMUX). Without
# ModemManager an MBIM port is left alone: a second client beside umbim costs the T99 its control (F24).
qmi_probe() {
	step "QMI (through the proxy; an MBIM port through QMI inside MBIM)"
	have qmicli || { note "qmicli not installed (qmi-utils)"; return; }
	mm=0
	[ -x "$R/etc/init.d/modemmanager" ] && "$R/etc/init.d/modemmanager" running >/dev/null 2>&1 && mm=1
	for w in "$R"/sys/class/usbmisc/cdc-wdm*; do
		[ -e "$w" ] || continue
		n=$(basename "$w"); drv=—
		[ -L "$w/device/driver" ] && drv=$(basename "$(readlink "$w/device/driver")")
		case $drv in
		qmi_wwan) o= ;;
		cdc_mbim)
			o=--device-open-mbim
			[ "$mm" = 1 ] || { note "$n ($drv): skipped, ModemManager is not running (umbim may hold the port)"; continue; }
			;;
		*) continue ;;
		esac
		for q in --dms-get-revision --nas-get-signal-info --nas-get-system-selection-preference; do
			say "   $n ($drv) $q: $(tmo 10 qmicli -p $o -d "/dev/$n" $q 2>&1 | mask | tr '\n' ' ')"
		done
	done
}

# read-only AT queries by family, through the engine
at_probe() {
	step "AT queries through the engine"
	set -f
	for id in $(medvemodem list 2>/dev/null | jsonfilter -e '@[*].device_id' 2>/dev/null); do
		vp=$(jget "$(medvemodem show "$id" 2>/dev/null)" '@.vidpid')
		case $vp in
		05c6:9025|05c6:90d5|1e2d:*) cmds='ATI AT^PCIEMODE? AT^USBSWITCH? AT^CUSTOMER? AT+CFUN?' ;;
		413c:81*|0489:e0b*|1bc7:191*) cmds='ATI AT^CUSTOMER? AT+CFUN?' ;;
		# FM350: the FCC lock, read only (mode 0 off; 1/2 with state 0 keeps the radio off), the SIM, the network
		0e8d:712*) cmds='ATI AT+CGMR AT+CFUN? AT+GTUSBMODE? AT+GTACT=? AT+GTACT? AT+GTFCCLOCKMODE? AT+GTFCCLOCKSTATE? AT+GTFCCEFFSTATUS? AT+CPIN? AT+COPS? AT+CEREG?' ;;
		*) cmds='ATI AT+CFUN?' ;;
		esac
		for c in $cmds; do
			say "   $id $c: $(medvemodem at "$id" "$c" 2>&1 | mask | tr '\n' ' ')"
		done
	done
	set +f
}

default_route_dev() { ip -4 route show default 2>/dev/null | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -n1; }

# the device the SSH client of this shell is reached through; empty on a console
peer_dev() {
	peer=${SSH_CLIENT%% *}
	[ -n "$peer" ] || peer=${SSH_CONNECTION%% *}
	[ -n "$peer" ] || return 0
	ip route get "$peer" 2>/dev/null | sed -n 's/.* dev \([^ ]*\).*/\1/p' | head -n1
}

modem_netdevs() {
	medvemodem show "$1" 2>/dev/null | jsonfilter -e '@.ports.net[*]' 2>/dev/null | sed 's#^/dev/##'
}

# a value from the interface or, failing that, its DHCP child <iface>_4
if_value() {
	v=$(ifstatus "$1" 2>/dev/null | jsonfilter -e "$2" 2>/dev/null)
	[ -n "$v" ] || v=$(ifstatus "${1}_4" 2>/dev/null | jsonfilter -e "$2" 2>/dev/null)
	printf '%s' "$v"
}

l3_of() {
	st=$(ifstatus "$1" 2>/dev/null) || return 1
	d=$(jget "$st" '@.l3_device'); [ -n "$d" ] || d=$(jget "$st" '@.device')
	printf '%s' "$d"
}

set_backend() {
	# a device section for this modem, created if missing; only "backend" is touched
	sec=$(uci -q show medvemodem | sed -n "s/^medvemodem\.\([^.]*\)\.id='$1'$/\1/p" | head -n1)
	if [ -z "$sec" ]; then
		sec=$(uci add medvemodem device)
		uci set "medvemodem.$sec.id=$1"
	fi
	uci set "medvemodem.$sec.backend=$2"
	uci commit medvemodem
}

wait_released() {
	i=0
	while [ $i -lt 30 ]; do
		st=$(medvemodem config state "$1" 2>/dev/null)
		[ -z "$(jget "$st" '@.ledger.interface')" ] && return 0
		sleep 1; i=$((i + 1))
	done
	return 1
}

run_backend() {
	id=$1 be=$2
	step "backend $be"
	case $be in
		qmodem) [ -x "$R/usr/share/qmodem/modem_scan.sh" ] || { bad "QModem is not installed"; return; } ;;
		native) [ -x "$R/lib/netifd/proto/qmi.sh" ] || [ -x "$R/lib/netifd/proto/mbim.sh" ] || [ -x "$R/lib/netifd/proto/medvemodem.sh" ] ||
			note "no qmi, mbim or medvemodem netifd proto: the native backend has nothing to drive" ;;
		modemmanager) [ -x "$R/etc/init.d/modemmanager" ] || { bad "ModemManager is not installed"; return; } ;;
		*) bad "unknown backend $be"; return ;;
	esac
	snapshot "before-$be"
	set_backend "$id" "$be"
	t0=$(date +%s)
	r=$(medvemodem bringup "$id" 2>&1)
	printf '%s\n' "$r" | mask >"$OUT/$be-bringup.json"
	res=$(jget "$r" '@.result'); iface=$(jget "$r" '@.interface'); proto=$(jget "$r" '@.proto')
	say "   bringup: ${res:-?} via ${proto:-?} on ${iface:-?} in $(( $(date +%s) - t0 )) s $(jget "$r" '@.reason')"
	say "   steps: $(printf '%s' "$r" | jsonfilter -e '@.steps[*].step' 2>/dev/null | tr '\n' ' ')"
	if [ "$res" = connected ] && [ -n "$iface" ]; then
		ok "session up"
		dev=$(l3_of "$iface")
		# proto qmi/mbim keep the address on the DHCP child <iface>_4
		addr=$(if_value "$iface" '@["ipv4-address"][0].address')
		[ -n "$addr" ] && ok "address on $dev" || bad "no IPv4 address on $iface"
		if ip -4 route show table all 2>/dev/null | grep -q "dev $dev"; then ok "route through $dev"; else bad "no route through $dev"; fi
		if ping -c 3 -W 3 -I "$dev" "$PING_TARGET" >"$OUT/$be-ping.txt" 2>&1; then
			ok "ping $PING_TARGET through $dev: $(tail -n1 "$OUT/$be-ping.txt")"
		else
			bad "ping $PING_TARGET through $dev failed"
		fi
		dns=$(if_value "$iface" '@["dns-server"][0]')
		if [ -n "$dns" ] && nslookup "$DNS_NAME" "$dns" >"$OUT/$be-dns.txt" 2>&1; then ok "DNS $DNS_NAME via $dns"
		elif [ -z "$dns" ]; then note "the interface reports no DNS server"
		else bad "DNS $DNS_NAME via $dns failed"; fi
	else
		bad "bring-up did not connect"
	fi
	if [ "$be" = native ]; then
		# ModemManager has let go of this modem: look for its AT port (AT, AT+CGMI) and ask ATI
		p=$(medvemodem probe "$id" 2>&1)
		printf '%s\n' "$p" | mask >"$OUT/native-probe.json"
		say "   probe: $(printf '%s' "$p" | tr -d '\n\t ' | mask | cut -c1-200)"
		a=$(medvemodem at "$id" ATI 2>&1)
		printf '%s\n' "$a" | mask >"$OUT/native-ati.json"
		l=$(jget "$a" '@.lines[*]' | tr '\n' ' ')
		[ -n "$l" ] && say "   ATI on $(jget "$a" '@.port'): $(printf '%s' "$l" | mask)" ||
			note "ATI: $(jget "$a" '@.refused')$(jget "$a" '@.error')"
	fi
	medvemodem show "$id" 2>&1 | mask >"$OUT/$be-show.json"
	medvemodem diag profile "$id" --label "bench-$be" --stdout 2>&1 | mask >"$OUT/$be-diag.json"
	if [ "$KEEP" = 1 ]; then
		note "--keep: left running on $be"
		return
	fi
	rel=$(medvemodem config release "$id" 2>&1)
	printf '%s\n' "$rel" | mask >"$OUT/$be-release.json"
	wait_released "$id" && ok "released" || bad "engine still holds the configuration 30 s after release"
	# take our device section back out as we found it
	cp "$OUT/before-$be/medvemodem" "$R/etc/config/medvemodem" 2>/dev/null
	sleep 3
	snapshot "after-$be"
	compare "before-$be" "after-$be"
}

mkdir -p "$OUT" || exit 1
: >"$OUT/report.txt"

case "${1:-}" in
info)
	info
	;;
probe)
	info
	usb_probe
	qmi_probe
	[ "${2:-}" = --at ] && at_probe
	;;
run)
	shift
	id=${1:-}; shift 2>/dev/null
	[ -n "$id" ] || { echo "usage: $0 run <device-id> <backend>... [--yes] [--keep] [--only-uplink]" >&2; exit 2; }
	backends=""
	for a in "$@"; do
		case $a in --yes) YES=1 ;; --keep) KEEP=1 ;; --only-uplink) ONLY_UPLINK=1 ;; *) backends="$backends $a" ;; esac
	done
	[ -n "$backends" ] || backends="modemmanager native qmodem"
	have jsonfilter || { echo "jsonfilter is required" >&2; exit 2; }
	medvemodem show "$id" >/dev/null 2>&1 || { echo "no modem $id (see: medvemodem list)" >&2; exit 2; }
	info
	if [ "${ENGINE_STALE:-0}" = 1 ]; then
		echo "The running engine is not the installed one; restart it (/etc/init.d/medvemodemd restart) and run again." >&2
		exit 3
	fi
	rd=$(default_route_dev)
	pd=$(peer_dev)
	for d in $(modem_netdevs "$id"); do
		if [ -n "$pd" ] && [ "$d" = "$pd" ]; then
			echo "This SSH session comes in through $pd, which belongs to this modem." >&2
			echo "Connect to the router from LAN or Wi-Fi, then run again." >&2
			exit 3
		fi
		if [ "$d" = "$rd" ] && [ "$ONLY_UPLINK" != 1 ]; then
			echo "The default route goes through $rd, which belongs to this modem." >&2
			echo "Connect to the router from LAN or Wi-Fi and use another uplink, then run again." >&2
			echo "If this modem is the router's only uplink, add --only-uplink: the router" >&2
			echo "has no internet while the run takes the modem down; LAN access stays." >&2
			exit 3
		fi
		[ "$d" = "$rd" ] && UPLINK_DEV=$d
	done
	if [ -n "${UPLINK_DEV:-}" ]; then
		say "   ..   the default route goes through $UPLINK_DEV of this modem (--only-uplink):"
		say "        the router has no internet while each backend is switched"
	fi
	if [ "$YES" != 1 ]; then
		printf 'Modem %s will be brought up and released on:%s\nIts connection drops several times. Continue? [y/N] ' "$id" "$backends"
		read -r ans
		[ "$ans" = y ] || [ "$ans" = Y ] || exit 1
	fi
	snapshot start
	for be in $backends; do run_backend "$id" "$be"; done
	[ "$KEEP" = 1 ] || { snapshot end; step "whole run"; compare start end; }
	;;
*)
	sed -n '2,/^# Part of/p' "$0" | sed 's/^# \{0,1\}//'
	exit 2
	;;
esac

step "summary"
say "   failures: $FAILS"
logread 2>/dev/null | grep -Ei 'medvemodem|modemmanager|qmodem|netifd' | tail -n 400 | mask >"$OUT/log.txt"
tar -C "$(dirname "$OUT")" -czf "$OUT.tar.gz" "$(basename "$OUT")" && say "   report: $OUT.tar.gz"
[ "$FAILS" = 0 ]
