#!/bin/sh
# Build the MedveModem packages in the official OpenWrt SDK container.
#
#   tools/build.sh <arch> <release> [qmodem-source]   e.g. tools/build.sh aarch64_cortex-a53 25.12.6
#
# Run from the repository root; needs Docker. The packages land in dist/<arch>-<release>/:
# medvemodem, luci-app-medvemodem, medvemodem-bot, luci-app-medvemodem-bot and medvemodem-lpac
# (.apk for OpenWrt 25.12 and newer, .ipk for 24.10). The SDK image is openwrt/sdk:<arch>-<release>;
# the architectures are OpenWrt's package architectures (aarch64_cortex-a53, aarch64_generic, x86_64, ...).
# JOBS overrides the number of make jobs (default: the host's threads); DOCKER_ARGS adds arguments to docker run
# (a proxy, say: DOCKER_ARGS="-e https_proxy=http://proxy:3128"); FEEDS_GITHUB=1 takes OpenWrt's feeds from their GitHub
# mirrors when git.openwrt.org does not answer.
# With a QModem source tree (.github/scripts/qmodem-src.sh <dir>: the commit the Makefile pins, our changes applied),
# QModem's core packages, the set the LuCI installer takes, land in dist-qmodem/<arch>-<release>/.
# MEDVEMODEM_DOWNLOAD_BASE is the release the router downloads the QModem archives from (a release build sets it).
set -eu

[ $# -eq 2 ] || [ $# -eq 3 ] || { sed -n '2,17p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
arch=$1
release=$2
root=$(pwd)
[ -f "$root/Makefile" ] && [ -d "$root/packages/luci-app-medvemodem" ] || { echo "run it from the repository root" >&2; exit 2; }
sh "$root/tools/check-web-version.sh"
out="$root/dist/$arch-$release"
qout="$root/dist-qmodem/$arch-$release"
qmodem=${3:-}
[ -z "$qmodem" ] || qmodem=$(cd "$qmodem" && pwd)
mkdir -p "$out" "$qout"
chmod 0777 "$out" "$qout"

docker run --rm \
	-v "$root:/feed/medvemodem:ro" \
	-v "$out:/out" \
	-v "$qout:/out-qmodem" \
	${qmodem:+-v "$qmodem:/feed-qmodem:ro"} \
	-e JOBS="${JOBS:-$(nproc)}" -e FEEDS_GITHUB="${FEEDS_GITHUB:-}" -e QMODEM="${qmodem:+1}" \
	${MEDVEMODEM_DOWNLOAD_BASE:+-e MEDVEMODEM_DOWNLOAD_BASE="$MEDVEMODEM_DOWNLOAD_BASE"} ${DOCKER_ARGS:-} \
	"openwrt/sdk:$arch-$release" /bin/bash -c '
set -eu
trap "chmod -R a+rwX /out /out-qmodem 2>/dev/null || true" EXIT
cd /builder
[ -d ./scripts ] || ./setup.sh
# the feed is the directory above the checkout: its root Makefile is a package of its own (the engine)
printf "src-link medvemodem /feed\n" > feeds.conf
[ -z "$QMODEM" ] || printf "src-link qmodem /feed-qmodem\n" >> feeds.conf
if [ -n "$FEEDS_GITHUB" ]; then
	sed -e "s|https://git.openwrt.org/openwrt/openwrt.git|https://github.com/openwrt/openwrt.git|" \
	    -e "s|https://git.openwrt.org/feed/|https://github.com/openwrt/|" \
	    -e "s|https://git.openwrt.org/project/luci.git|https://github.com/openwrt/luci.git|" feeds.conf.default >> feeds.conf
else
	cat feeds.conf.default >> feeds.conf
fi
./scripts/feeds update -a >/dev/null
./scripts/feeds install -a -p medvemodem >/dev/null
[ -z "$QMODEM" ] || ./scripts/feeds install -a -p qmodem >/dev/null
make defconfig >/dev/null
cat >> .config <<EOC
CONFIG_PACKAGE_medvemodem=m
CONFIG_PACKAGE_luci-app-medvemodem=m
CONFIG_PACKAGE_medvemodem-bot=m
CONFIG_PACKAGE_luci-app-medvemodem-bot=m
CONFIG_PACKAGE_medvemodem-lpac=m
CONFIG_PACKAGE_medvemodem-lpac-uqmi=m
# CONFIG_PACKAGE_medvemodem-lpac-mbim is not set
# CONFIG_PACKAGE_medvemodem-lpac-qmi is not set
EOC
make defconfig >/dev/null
failed=""
for p in medvemodem luci-app-medvemodem medvemodem-bot luci-app-medvemodem-bot medvemodem-lpac; do
	make -j"$JOBS" "package/$p/compile" V=s && continue
	echo "=== $p failed; again with one job"
	make -j1 "package/$p/compile" V=s || failed="$failed $p"
done
find bin \( -name "*medvemodem*.apk" -o -name "*medvemodem*.ipk" \) -exec cp {} /out/ \;
ls -1 /out
[ -z "$failed" ] || { echo "not built:$failed (see above)" >&2; exit 1; }
[ -n "$QMODEM" ] || exit 0
# QModem core as its own release builds it (generic qmi_wwan from the official feed, its quectel-CM); luci-app-qmodem
# is selected only because its menu keeps the driver choice, it is not shipped: the MedveModem pages are the interface.
# The x86 SDK profile selects gpio-button-hotplug, which a release SDK cannot build (as the QModem CI does).
rm -rf feeds/base/package/kernel/gpio-button-hotplug package/feeds/base/gpio-button-hotplug
sed -i "/^CONFIG_PACKAGE_gpio-button-hotplug=/d" .config
cat >> .config <<EOC
# CONFIG_PACKAGE_gpio-button-hotplug is not set
CONFIG_PACKAGE_luci-app-qmodem=m
CONFIG_PACKAGE_luci-app-qmodem_INCLUDE_generic-qmi-wwan=y
# CONFIG_PACKAGE_luci-app-qmodem_INCLUDE_vendor-qmi-wwan is not set
CONFIG_PACKAGE_luci-app-qmodem_USE_TOM_CUSTOMIZED_QUECTEL_CM=y
CONFIG_PACKAGE_qmodem=m
CONFIG_PACKAGE_tom_modem=m
CONFIG_PACKAGE_tom_modem_EARLY_RETURN=y
CONFIG_PACKAGE_tom_modem_ENABLE_UBUS_DAEMON=y
CONFIG_PACKAGE_ubus-at-daemon=m
CONFIG_PACKAGE_sms-tool_q=m
CONFIG_PACKAGE_modem_scan=m
CONFIG_PACKAGE_qmodem-seal=m
CONFIG_PACKAGE_quectel-CM-5G-M=m
CONFIG_PACKAGE_ndisc6=m
EOC
make defconfig >/dev/null
for p in tom_modem ubus_at_daemon sms-tool_q modem_scan qmodem-seal quectel_CM_5G_M ndisc6 qmodem; do
	make -j"$JOBS" "package/$p/compile" V=s || make -j1 "package/$p/compile" V=s || { echo "QModem package $p not built" >&2; exit 1; }
done
for n in qmodem qmodem-seal tom_modem ubus-at-daemon sms-tool_q modem_scan quectel-CM-5G-M ndisc6; do
	f=$(find bin/packages \( -name "${n}_[0-9]*.ipk" -o -name "${n}-[0-9]*.apk" \) | head -n 1)
	[ -n "$f" ] || { echo "QModem package $n not built" >&2; exit 1; }
	cp "$f" /out-qmodem/
done
cp /feed-qmodem/LICENSE /out-qmodem/QModem-LICENSE
ls -1 /out-qmodem
'
echo "packages in $out"
