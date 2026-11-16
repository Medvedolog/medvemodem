#!/bin/sh
# Build the MedveModem packages in the official OpenWrt SDK container.
#
#   tools/build.sh <arch> <release>        e.g. tools/build.sh aarch64_cortex-a53 25.12.6
#
# Run from the repository root; needs Docker. The packages land in dist/<arch>-<release>/:
# medvemodem, luci-app-medvemodem, medvemodem-bot, luci-app-medvemodem-bot and medvemodem-lpac
# (.apk for OpenWrt 25.12 and newer, .ipk for 24.10). The SDK image is openwrt/sdk:<arch>-<release>;
# the architectures are OpenWrt's package architectures (aarch64_cortex-a53, aarch64_generic, x86_64, ...).
# JOBS overrides the number of make jobs (default: the host's threads); DOCKER_ARGS adds arguments to docker run
# (a proxy, say: DOCKER_ARGS="-e https_proxy=http://proxy:3128"); FEEDS_GITHUB=1 takes OpenWrt's feeds from their GitHub
# mirrors when git.openwrt.org does not answer.
set -eu

[ $# -eq 2 ] || { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
arch=$1
release=$2
root=$(pwd)
[ -f "$root/Makefile" ] && [ -d "$root/packages/luci-app-medvemodem" ] || { echo "run it from the repository root" >&2; exit 2; }
out="$root/dist/$arch-$release"
mkdir -p "$out"
chmod 0777 "$out"

docker run --rm \
	-v "$root:/feed/medvemodem:ro" \
	-v "$out:/out" \
	-e JOBS="${JOBS:-$(nproc)}" -e FEEDS_GITHUB="${FEEDS_GITHUB:-}" ${DOCKER_ARGS:-} \
	"openwrt/sdk:$arch-$release" /bin/bash -c '
set -eu
trap "chmod -R a+rwX /out 2>/dev/null || true" EXIT
cd /builder
[ -d ./scripts ] || ./setup.sh
# the feed is the directory above the checkout: its root Makefile is a package of its own (the engine)
printf "src-link medvemodem /feed\n" > feeds.conf
if [ -n "$FEEDS_GITHUB" ]; then
	sed -e "s|https://git.openwrt.org/openwrt/openwrt.git|https://github.com/openwrt/openwrt.git|" \
	    -e "s|https://git.openwrt.org/feed/|https://github.com/openwrt/|" \
	    -e "s|https://git.openwrt.org/project/luci.git|https://github.com/openwrt/luci.git|" feeds.conf.default >> feeds.conf
else
	cat feeds.conf.default >> feeds.conf
fi
./scripts/feeds update -a >/dev/null
./scripts/feeds install -a -p medvemodem >/dev/null
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
'
echo "packages in $out"
