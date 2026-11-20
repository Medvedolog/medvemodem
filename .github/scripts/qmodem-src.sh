#!/bin/sh
# Fetch QModem's source at the commit the Makefile pins (QMODEM_COMMIT), for
# sdk-build.sh. Usage: qmodem-src.sh <dir>
# Only the core packages are built from it;
# nothing of QModem is kept in this repository: our changes are the substitutions below.
set -eu

dir=$1
commit=$(sed -n 's/^QMODEM_COMMIT:=//p' Makefile)
[ -n "$commit" ] || { echo "QMODEM_COMMIT is not set in the Makefile" >&2; exit 1; }
rm -rf "$dir"
git init -q "$dir"
n=0
until git -C "$dir" fetch -q --depth 1 https://github.com/FUjr/QModem.git "$commit"; do
	n=$((n + 1))
	[ "$n" -lt 4 ] || { echo "cannot fetch QModem $commit" >&2; exit 1; }
	sleep $((n * 15))
done
git -C "$dir" -c advice.detachedHead=false checkout -q FETCH_HEAD
[ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ]
upstream=$(sed -n 's/^QMODEM_VERSION:=//p' "$dir/version.mk")

# Our build carries our version (QMODEM_VERSION in the Makefile, upstream's own plus a _p<date> of the pin): a router
# with an earlier build of the same upstream version sees an upgrade (qmodem-install, the LuCI «Обновить QModem»).
ours=$(sed -n 's/^QMODEM_VERSION:=//p' Makefile)
case "$ours" in "$upstream"|"$upstream"_p[0-9]*) ;; *) echo "QMODEM_VERSION $ours is not upstream's $upstream" >&2; exit 1 ;; esac
sed -i "s/^QMODEM_VERSION:=.*/QMODEM_VERSION:=$ours/" "$dir/version.mk"

# The resolvers and probe targets QModem writes in by itself are Chinese (AliDNS 223.5.5.5, DNSPod 119.29.29.29 and
# their IPv6): a modem that gives no DNS got them as the interface's servers, and the monitors pinged them. The owner,
# 2026-10-22: «надо что-то делать с хардкодом китайских днс». Yandex and Cloudflare instead, in the runtime files
# only (not QModem's tests); a build with one left fails. the working notes
files=$(cd "$dir" && grep -rlE '223\.5\.5\.5|119\.29\.29\.29|2400:3200::1|2402:4e00::' application luci |
	grep -v -e '/tests/' -e '/src/log/' || true)
for f in $files; do
	sed -i -e 's/223\.5\.5\.5/77.88.8.8/g' -e 's/119\.29\.29\.29/1.1.1.1/g' \
		-e 's/2400:3200::1/2a02:6b8::feed:0ff/g' -e 's/2402:4e00::/2606:4700:4700::1111/g' "$dir/$f"
	echo "resolvers replaced: $f"
done
left=$(cd "$dir" && grep -rlE '223\.5\.5\.5|119\.29\.29\.29|2400:3200::1|2402:4e00::' application luci |
	grep -v -e '/tests/' -e '/src/log/' || true)
[ -z "$left" ] || { echo "Chinese resolvers left in: $left" >&2; exit 1; }

# The FM350 over USB (RNDIS/NCM, not mtk_pcie): QModem took the first host of the address's /24 as the gateway
# (ip_change_fm350), the modem never answers it, the RNDIS queue hangs (NETDEV WATCHDOG) and USB resets follow — the
# owner's «storm», bench 2026-10-21; unfixed upstream (main and stable, 2026-10-22). Our engine dials it on-link
# (profile fibocom-fm350 addressing.gateway, HANDOFF 174), stable on the bench; netifd's static `gateway 0.0.0.0` is the
# same default route straight into the device (no nexthop, scope link). Exactly one such line, or the build fails.
f=application/qmodem/files/usr/share/qmodem/modem_dial.sh
n=$(grep -c '^[[:space:]]*gateway="\${ipv4_config%\.\*}\.1"$' "$dir/$f" || true)
[ "$n" = 1 ] || { echo "$f: the FM350 first-host gateway line is not there once ($n): look at ip_change_fm350 again" >&2; exit 1; }
sed -i 's/^\([[:space:]]*\)gateway="\${ipv4_config%\.\*}\.1"$/\1gateway="0.0.0.0"	# MedveModem: on-link, the FM350 answers no x.x.x.1/' "$dir/$f"
echo "FM350 gateway on-link: $f"

echo "QModem $upstream as $ours at $commit"
