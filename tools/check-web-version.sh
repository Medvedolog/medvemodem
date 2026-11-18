#!/bin/sh
# Refuse an SDK package whose browser version differs from the release's engine version.
# This is intentionally a fast, dependency-free source check.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
rev=$(sed -n 's/^PKG_RELEASE:=\([0-9][0-9]*\)$/\1/p' "$root/Makefile" | head -1)
ver=$(sed -n 's/^PKG_VERSION:=\(.*\)$/\1/p' "$root/Makefile" | head -1)
[ -n "$ver" ] && [ -n "$rev" ] || { echo "Cannot read package version" >&2; exit 1; }
browser="$(printf '%s' "$ver" | tr '_' '-')-r$rev"
for makefile in packages/luci-app-medvemodem/Makefile packages/medvemodem-bot/Makefile packages/luci-app-medvemodem-bot/Makefile; do
  grep -Fxq "PKG_RELEASE:=$rev" "$root/$makefile" || { echo "$makefile: different release counter" >&2; exit 1; }
done
common="$root/packages/luci-app-medvemodem/htdocs/luci-static/resources/medvemodem/common.js"
grep -Fqx "var VERSION = '$browser';	/* = the engine's version */" "$common" || {
  grep -Fq "var VERSION = '$browser';" "$common" || { echo "common.js: expected $browser" >&2; exit 1; }
}
for page in dashboard modem settings; do
  path="$root/packages/luci-app-medvemodem/htdocs/luci-static/resources/view/medvemodem/$page.js"
  grep -Fq "var VIEW_VERSION = '$browser';" "$path" || { echo "$path: expected $browser" >&2; exit 1; }
done
python3 "$root/tools/check-whitelist-i18n.py" || exit 1
sh "$root/tests/test-bot-update-versions.sh" || exit 1
echo "LuCI version matches engine: $browser"
