#!/bin/sh
# medvemodem stand: a long look at a live router, the way an owner leaves it running.
#
#   medvemodem-stand.sh ro
#       read only: every read the pages make (status, devices, uplink, and per modem device, metrics,
#       signal_history, events, sms_list peek) answers valid JSON in time
#   medvemodem-stand.sh soak <minutes>
#       a sample every 30 s while a LuCI page is left open: memory, processes, zombies, the RSS of the engine,
#       rpcd and the bot, /tmp, each modem interface's uptime, the default routes, and the engine's events.
#       A flap, a zombie seen twice, a recovery step or an interface repair is a failure; growth is a warning
#   medvemodem-stand.sh whitelist <netdev> <minutes> --yes [--block]
#       an operator that lets only a few Russian sites and DNS through, on <netdev> only: a temporary nftables
#       table of its own (inet mmstand), removed at the end, on Ctrl-C, and by a timer of its own even if this
#       shell dies. The engine must call it limited and take no step: no recovery, no repair; only <netdev>'s own
#       route may go behind a link with the whole internet (the failover rule), no other route moves.
#       --block drops everything on <netdev> instead: then the engine must notice
#
# Every mode compares `uci show` of network, firewall and medvemodem before and after. Output: /tmp/mm-stand-<time>/
# (report.txt, samples.tsv); the exit status is the number of failures. Run it on the router (ssh), from LAN or
# Wi-Fi: whitelist takes the internet of <netdev> away for its minutes.
#
# Part of medvemodem; GPL-3.0-only.

set -u

R="${MM_STAND_ROOT:-}"				# a root prefix for /proc and /tmp in tests; empty on the router
EVERY="${MM_STAND_EVERY:-30}"
OUT="${MM_STAND_OUT:-/tmp}/mm-stand-$(date +%Y%m%d-%H%M%S)"
TABLE=mmstand
GRACE="${MM_STAND_GRACE:-300}"		# the table's own timer: this much after the run's end
# the engine's events that mean it acted on a modem; under a working network none of them should come
RISKY="recovery-start iface-repair ifup interface-retry mm-kick usb-config modeswitch-run hostfix-start"
FAILS=0
WARNS=0

say() { printf '%s\n' "$*"; printf '%s\n' "$*" >>"$OUT/report.txt"; }
step() { say "== $*"; }
ok() { say "   ok   $*"; }
bad() { say "   FAIL $*"; FAILS=$((FAILS + 1)); }
warn() { say "   warn $*"; WARNS=$((WARNS + 1)); }
note() { say "   ..   $*"; }

jget() { printf '%s' "$1" | jsonfilter -e "$2" 2>/dev/null; }

ids() { jget "$(ubus call medvemodem devices 2>/dev/null)" '@.devices[*].device_id'; }

configs() { for c in network firewall medvemodem; do uci -q show "$c"; done 2>/dev/null; }

unchanged() {
	configs >"$OUT/uci-after.txt"
	if cmp -s "$OUT/uci-before.txt" "$OUT/uci-after.txt"; then
		ok "network, firewall and medvemodem are as they were"
	else
		diff -u "$OUT/uci-before.txt" "$OUT/uci-after.txt" >"$OUT/uci.diff" 2>&1
		bad "the configuration changed during the run (uci.diff)"
	fi
}

# one call: valid JSON, and in time (5 s: a page's rpc gives about 20)
timed() {
	what=$1; shift
	t0=$(date +%s)
	out=$(ubus call medvemodem "$@" 2>&1)
	rc=$?
	dt=$(( $(date +%s) - t0 ))
	if [ "$rc" != 0 ]; then bad "$what: ubus exit $rc: $(printf '%s' "$out" | head -c 160)"; return; fi
	if ! printf '%s' "$out" | jsonfilter -e '@' >/dev/null 2>&1; then bad "$what: not JSON"; return; fi
	if [ "$dt" -gt 5 ]; then warn "$what: slow, ${dt} s"; else ok "$what"; fi
}

ro() {
	step "read only, as the pages read"
	for m in status devices uplink; do timed "$m" "$m"; done
	for id in $(ids); do
		a="{\"id\":\"$id\"}"
		timed "$id device" device "$a"
		timed "$id metrics" metrics "$a"
		timed "$id signal_history" signal_history "$a"
		timed "$id events" events "$a"
		timed "$id sms_list (peek)" sms_list "{\"id\":\"$id\",\"peek\":true}"
	done
}

# the modem interfaces, from the engine's view
ifaces() {
	devs=$(ubus call medvemodem devices 2>/dev/null)
	i=0
	while id=$(jget "$devs" "@.devices[$i].device_id") && [ -n "$id" ]; do
		f=$(jget "$devs" "@.devices[$i].connection.interface")
		[ -n "$f" ] && printf '%s\n' "$f"
		i=$((i + 1))
	done
}

# the risky events since $1 (unix time), counted by their time: a count of the journal's window missed new ones once
# the window was full (HANDOFF 200 item 17); with no $1, the kinds one a line
risky_since() {
	n=0
	for id in $(ids); do
		ev=$(ubus call medvemodem events "{\"id\":\"$id\"}" 2>/dev/null)
		i=0
		while k=$(jget "$ev" "@.events[$i].kind") && [ -n "$k" ]; do
			t=$(jget "$ev" "@.events[$i].t")
			i=$((i + 1))
			[ "${t:-0}" -ge "${1:-0}" ] 2>/dev/null || continue
			case " $RISKY " in *" $k "*) n=$((n + 1)); [ -n "${2:-}" ] && printf '%s\n' "$k" ;; esac
		done
	done
	[ -n "${2:-}" ] || printf '%s' "$n"
}

# the RSS (kB) of the process named $1 whose command line has $2 too: several ucode processes run (rpcd's plugins, the
# bot), the bot is the one asked for (HANDOFF 200 item 17)
rss_of() {
	for p in "$R"/proc/[0-9]*; do
		[ "$(cat "$p/comm" 2>/dev/null)" = "$1" ] || continue
		[ -z "${2:-}" ] || tr '\0' ' ' <"$p/cmdline" 2>/dev/null | grep -q -- "$2" || continue
		sed -n 's/^VmRSS:[[:space:]]*\([0-9]*\).*/\1/p' "$p/status" 2>/dev/null
		return
	done
	printf 0
}

# one line of samples.tsv
sample() {
	mem=$(sed -n 's/^MemAvailable:[[:space:]]*\([0-9]*\).*/\1/p' "$R/proc/meminfo")
	procs=0; zomb=0
	for p in "$R"/proc/[0-9]*; do
		[ -f "$p/status" ] || continue
		procs=$((procs + 1))
		grep -q '^State:[[:space:]]*Z' "$p/status" 2>/dev/null && zomb=$((zomb + 1))
	done
	tmp=$(du -sk "$R/tmp" 2>/dev/null | cut -f1)
	up=""
	for f in $(ifaces); do up="$up$f=$(jget "$(ifstatus "$f" 2>/dev/null)" '@.uptime'),"; done
	route=$(ip route show default 2>/dev/null | sort | tr '\n' ';' | tr '\t' ' ')
	printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$(date +%s)" "${mem:-0}" "$procs" "$zomb" \
		"$(rss_of medvemodemd)" "$(rss_of rpcd)" "$(rss_of ucode medvemodem-bot)" "${tmp:-0}" "${up:-none}" "${route:-none}" >>"$OUT/samples.tsv"
}

watch_for() {
	n=${MM_STAND_SAMPLES:-$(( $1 * 60 / EVERY ))}; [ "$n" -lt 2 ] && n=2
	i=0
	while [ "$i" -lt "$n" ]; do
		sample
		i=$((i + 1))
		[ "$i" -lt "$n" ] && sleep "$EVERY"
	done
}

# column $1 of the first and the last sample
col() { awk -F'\t' -v c="$1" -v w="$2" 'NR == 1 { a = $c } { b = $c } END { print (w == "first" ? a : b) }' "$OUT/samples.tsv"; }

judge() {
	step "verdict over $(wc -l <"$OUT/samples.tsv") samples"
	d=$(( $(col 2 first) - $(col 2 last) ))
	if [ "$d" -gt 8192 ]; then warn "available memory fell by ${d} kB"; else ok "memory (${d} kB less available)"; fi
	d=$(( $(col 3 last) - $(col 3 first) ))
	if [ "$d" -gt 15 ]; then warn "${d} more processes"; else ok "processes (${d})"; fi
	if awk -F'\t' 'p > 0 && $4 > 0 { z = 1 } { p = $4 } END { exit !z }' "$OUT/samples.tsv"; then
		bad "zombies in two samples running"
	else
		ok "no zombie stays"
	fi
	for c in 5:medvemodemd 6:rpcd 7:the-bot; do
		d=$(( $(col "${c%%:*}" last) - $(col "${c%%:*}" first) ))
		if [ "$d" -gt 2048 ]; then warn "${c#*:} grew by ${d} kB"; else ok "${c#*:} (${d} kB)"; fi
	done
	d=$(( $(col 8 last) - $(col 8 first) ))
	if [ "$d" -gt 4096 ]; then warn "/tmp grew by ${d} kB"; else ok "/tmp (${d} kB)"; fi
	# an interface whose uptime went down was brought down and up again
	if awk -F'\t' '{ n = split($9, kv, ","); for (i = 1; i <= n; i++) { split(kv[i], p, "="); if (p[1] != "" && (p[1] in u) && p[2] + 0 < u[p[1]] + 0) f = f " " p[1]; u[p[1]] = p[2] } }
		END { if (f != "") { print f; exit 0 } exit 1 }' "$OUT/samples.tsv" >"$OUT/flaps.txt"; then
		bad "an interface went down and up:$(cat "$OUT/flaps.txt")"
	else
		ok "no modem interface flapped"
	fi
	# under a white list the link itself goes behind a link with the whole internet: the owner's failover rule
	# (failover.c held(), 2026-10-27: «a whitelisted T99 kept the traffic beside a free T77»), and back after it; any
	# other route moved is a failure (the live stand, 2026-11-03: wwan1 120 -> 10120 was this rule, not a fault)
	if [ "$1" = whitelist ]; then
		others() { printf '%s' "$1" | tr ';' '\n' | grep -v " dev $DEV " | tr '\n' ';'; }
		if [ "$(others "$(col 10 first)")" != "$(others "$(col 10 last)")" ]; then
			bad "the default routes of other links changed"
		elif [ "$(col 10 first)" != "$(col 10 last)" ]; then
			ok "$DEV went behind the links with the whole internet (the failover rule for a white-listed link); the others stayed"
		else
			ok "the default routes stayed"
		fi
	elif [ "$(col 10 first)" != "$(col 10 last)" ]; then
		warn "the default routes changed"
	else
		ok "the default routes stayed"
	fi
	after=$(risky_since "$START")
	if [ "$after" -gt 0 ]; then
		bad "the engine acted $after time(s): $(risky_since "$START" kinds | sort | uniq -c | tr '\n' ' ')"
	else
		ok "the engine took no recovery step"
	fi
}

nft_rules() {
	ok_set=$1
	printf 'table inet %s {\n' "$TABLE"
	[ -n "$ok_set" ] && printf '\tset ok { type ipv4_addr; elements = { %s } }\n' "$ok_set"
	for ch in output forward; do
		printf '\tchain %s { type filter hook %s priority -5; policy accept;\n' "$ch" "$ch"
		if [ "$BLOCK" = 0 ]; then
			printf '\t\toifname "%s" udp dport 53 accept\n\t\toifname "%s" tcp dport 53 accept\n' "$DEV" "$DEV"
			[ -n "$ok_set" ] && printf '\t\toifname "%s" ip daddr @ok accept\n' "$DEV"
		fi
		printf '\t\toifname "%s" counter drop\n\t}\n' "$DEV"
	done
	printf '}\n'
}

# the table goes by itself after $1 seconds, whatever happens to this shell
self_removing() {
	( trap '' HUP; sleep "$1"; nft delete table inet "$2" 2>/dev/null ) </dev/null >/dev/null 2>&1 &
}

whitelist() {
	DEV=$1 MIN=$2
	command -v nft >/dev/null 2>&1 || { bad "nft is not installed"; return; }
	printf '%s\n' "$DEV" | grep -Eqx '[A-Za-z0-9_.@-]{1,15}' || { bad "not a network device: $DEV"; return; }
	[ -d "$R/sys/class/net/$DEV" ] || { bad "no network device $DEV"; return; }
	# the rollback first, proven on a table of its own
	step "the rollback timer"
	printf 'table inet %s_probe { }\n' "$TABLE" | nft -f - || { bad "nft refused a test table"; return; }
	self_removing 2 "${TABLE}_probe"
	sleep 4
	if nft list table inet "${TABLE}_probe" >/dev/null 2>&1; then
		nft delete table inet "${TABLE}_probe"
		bad "the rollback timer did not remove its table: not starting"
		return
	fi
	ok "a table removes itself"
	ips=""
	if [ "$BLOCK" = 0 ]; then
		for h in ya.ru yandex.ru ozon.ru max.ru; do
			for a in $(nslookup "$h" 2>/dev/null | sed -n 's/^Address[^:]*: *\([0-9.]*\)$/\1/p' | grep -v '^127\.'); do
				ips="${ips:+$ips, }$a"
			done
		done
		[ -n "$ips" ] || { bad "could not resolve the white list"; return; }
	fi
	step "$( [ "$BLOCK" = 1 ] && echo "everything dropped" || echo "only the white list") on $DEV for $MIN min"
	trap 'nft delete table inet $TABLE 2>/dev/null; exit 130' INT TERM HUP
	nft_rules "$ips" >"$OUT/table.nft"
	nft -f "$OUT/table.nft" || { bad "nft refused the table (table.nft)"; return; }
	self_removing $((MIN * 60 + GRACE)) "$TABLE"
	note "table inet $TABLE in place, removed in $MIN min (or $((MIN + 5)) min by its timer)"
	watch_for "$MIN"
	levels=$(for id in $(ids); do
		devs=$(ubus call medvemodem device "{\"id\":\"$id\"}" 2>/dev/null)
		[ "$(jget "$devs" '@.uplink.device')" = "$DEV" ] && jget "$devs" '@.uplink.reach.level'
	done)
	nft delete table inet "$TABLE" 2>/dev/null
	trap - INT TERM HUP
	note "the engine's last word on $DEV: ${levels:-none}"
	if [ "$BLOCK" = 1 ]; then
		case "$levels" in down|limited) ok "the engine noticed ($levels)" ;; *) bad "the engine did not notice the drop (${levels:-no check})" ;; esac
	else
		case "$levels" in limited|ok) ok "the engine did not call it down ($levels)" ;; *) bad "the engine called a white-listed link ${levels:-unchecked}" ;; esac
	fi
}

mkdir -p "$OUT" || exit 1
: >"$OUT/report.txt"
configs >"$OUT/uci-before.txt"
MODE=${1:-}
case "$MODE" in
ro)
	ro
	;;
soak)
	case "${2:-}" in ''|*[!0-9]*) echo "usage: medvemodem-stand.sh soak <minutes>" >&2; exit 2 ;; esac
	START=$(date +%s)
	step "soak for $2 min, a sample every $EVERY s: leave a MedveModem page open in a browser"
	watch_for "$2"
	judge soak
	;;
whitelist)
	case "${3:-}" in ''|*[!0-9]*) echo "usage: medvemodem-stand.sh whitelist <netdev> <minutes> --yes [--block]" >&2; exit 2 ;; esac
	YES=0 BLOCK=0
	for a in "$@"; do case "$a" in --yes) YES=1 ;; --block) BLOCK=1 ;; esac; done
	[ "$YES" = 1 ] || { echo "whitelist takes the internet of $2 away for $3 min: add --yes" >&2; exit 2; }
	START=$(date +%s)
	whitelist "$2" "$3"
	[ -s "$OUT/samples.tsv" ] && judge whitelist
	;;
*)
	sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'
	exit 2
	;;
esac
unchanged
say ""
say "$FAILS failure(s), $WARNS warning(s); report in $OUT"
exit "$FAILS"
