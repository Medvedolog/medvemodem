#!/bin/sh
# Build the pinned QModem core for one OpenWrt SDK target.
# Output: dist/qmodem-<arch>-<release>/qmodem-<version>-<arch>-<line>.tar.gz
set -eu
[ "$#" -eq 2 ] || { echo "Usage: $0 ARCH RELEASE" >&2; exit 2; }
arch=$1
release=$2
case "$arch" in aarch64_cortex-a53|aarch64_generic|x86_64) ;; *) echo "unsupported architecture: $arch" >&2; exit 2;; esac
case "$release" in 24.10.*|25.12.*) ;; *) echo "unsupported OpenWrt: $release" >&2; exit 2;; esac
root=$(pwd)
[ -f "$root/Makefile" ] || { echo "run from MedveModem root" >&2; exit 2; }
version=$(sed -n 's/^QMODEM_VERSION:=//p' Makefile | head -1)
commit=$(sed -n 's/^QMODEM_COMMIT:=//p' Makefile | head -1)
[ -n "$version" ] && [ -n "$commit" ] || exit 2
line=${release%.*}
out="$root/dist/qmodem-$arch-$release"
mkdir -p "$out"
chmod 0777 "$out"
docker run --rm \
  -v "$out:/out" \
  -e JOBS="${JOBS:-$(nproc)}" -e QMODEM_VERSION="$version" -e QMODEM_COMMIT="$commit" \
  -e FEEDS_GITHUB="${FEEDS_GITHUB:-}" ${DOCKER_ARGS:-} \
  "openwrt/sdk:$arch-$release" /bin/bash -c '
set -euo pipefail
trap "chmod -R a+rwX /out 2>/dev/null || true" EXIT
cd /builder
[ -d ./scripts ] || ./setup.sh
git clone -q https://github.com/FUjr/QModem.git qmodem-src
git -C qmodem-src checkout -q --detach "$QMODEM_COMMIT"
[ "$(git -C qmodem-src rev-parse HEAD)" = "$QMODEM_COMMIT" ]
# Upstream application Makefiles include ../../version.mk. Feed symlinks change
# their apparent location, so point them at the checked-out, pinned version.
sed -i "s/^QMODEM_VERSION:=.*/QMODEM_VERSION:=$QMODEM_VERSION/;s/^QMODEM_RELEASE:=.*/QMODEM_RELEASE:=1/" qmodem-src/version.mk
find qmodem-src/application -name Makefile -exec sed -i "s@include ../../version.mk@include \$(TOPDIR)/qmodem-src/version.mk@" {} +
printf "src-link qmodem /builder/qmodem-src/application\n" > feeds.conf
if [ -n "$FEEDS_GITHUB" ]; then
  sed -e "s|https://git.openwrt.org/openwrt/openwrt.git|https://github.com/openwrt/openwrt.git|" \
      -e "s|https://git.openwrt.org/feed/|https://github.com/openwrt/|" \
      -e "s|https://git.openwrt.org/project/luci.git|https://github.com/openwrt/luci.git|" feeds.conf.default >> feeds.conf
else
  cat feeds.conf.default >> feeds.conf
fi
./scripts/feeds update -a >/dev/null
./scripts/feeds install -a -p qmodem >/dev/null
make defconfig >/dev/null
cat >> .config <<EOF
CONFIG_PACKAGE_qmodem=m
CONFIG_PACKAGE_qmodem-seal=m
CONFIG_PACKAGE_tom_modem=m
CONFIG_PACKAGE_ubus-at-daemon=m
CONFIG_PACKAGE_sms-tool_q=m
CONFIG_PACKAGE_modem_scan=m
CONFIG_PACKAGE_quectel-CM-5G-M=m
EOF
make defconfig >/dev/null
# Build targets use upstream source directory names, not binary package names.
# In particular, ubus-at-daemon lives in ubus_at_daemon and
# quectel-CM-5G-M in quectel_CM_5G_M.
for src in qmodem-seal tom_modem ubus_at_daemon sms-tool_q modem_scan quectel_CM_5G_M qmodem; do
  target="package/feeds/qmodem/$src/compile"
  [ -f "package/feeds/qmodem/$src/Makefile" ] || { echo "Missing feed source: $src" >&2; exit 1; }
  make -j"$JOBS" "$target" V=s || make -j1 "$target" V=s
done
for pkg in qmodem qmodem-seal tom_modem ubus-at-daemon sms-tool_q modem_scan quectel-CM-5G-M; do
  matches=$(find bin/packages -type f \( -name "$pkg-*.apk" -o -name "${pkg}_*.ipk" \) | wc -l)
  [ "$matches" -gt 0 ] || { echo "Missing QModem package: $pkg" >&2; exit 1; }
  find bin/packages -type f \( -name "$pkg-*.apk" -o -name "${pkg}_*.ipk" \) -exec cp {} /out/ \;
done
# ndisc6 is optional; if the pinned source built it, bundle it too.
find bin/packages -type f \( -name "ndisc6-*.apk" -o -name "ndisc6_*.ipk" \) -exec cp {} /out/ \;
ls -l /out
'
asset="qmodem-$version-$arch-$line.tar.gz"
tar -C "$out" -czf "$out/$asset.tmp" --exclude="$asset.tmp" .
mv "$out/$asset.tmp" "$out/$asset"
# Only the target archive is uploaded; never upload a naked unverified package directory.
find "$out" -maxdepth 1 -type f ! -name "$asset" -delete
echo "Built $out/$asset"
