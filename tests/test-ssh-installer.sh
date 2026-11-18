#!/bin/sh
# Static/contract checks for the standalone SSH installer; no router mutation.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
script="$root/medvemodem-install.sh"
sh -n "$script"
grep -Fq '24.10.*) PM=opkg; EXT=ipk; SDK_RELEASE=24.10.8' "$script"
grep -Fq '25.12.*) PM=apk; EXT=apk; SDK_RELEASE=25.12.6' "$script"
grep -Fq 'no_proxy_env "$PM" update' "$script"
grep -Fq '"$PM" update || fail' "$script"
grep -Fq 'export http_proxy="$PROXY" https_proxy="$PROXY"' "$script"
grep -Fq 'SHA256 mismatch' "$script"
grep -Fq 'apk add --simulate --allow-untrusted' "$script"
grep -Fq 'opkg install --noaction' "$script"
grep -Fq 'opkg install --force-reinstall' "$script"
grep -Fq 'apk add --force-reinstall --allow-untrusted' "$script"
grep -Fq 'remove_packages luci-app-medvemodem-bot medvemodem-bot' "$script"
grep -Fq 'remove_packages luci-app-medvemodem-bot medvemodem-bot luci-app-medvemodem medvemodem' "$script"
grep -Fq 'ask '\''Install/update Telegram bot and its LuCI too?'\'' no' "$script"
grep -Fq 'Available release: $TAG (MedveModem + LuCI)' "$script"
grep -Fq 'RELEASE HIGHLIGHTS (English)' "$script"
grep -Fq 'HIGHLIGHTS.txt' "$script"
grep -Fq 'docs/RELEASE-HIGHLIGHTS-ALPHA1-R5.md' "$script"
grep -Fq 'Install $TAG packages now?' "$script"
grep -Fq '"$name"-[0-9]*."$EXT"' "$script"
# Avoid the broad medvemodem-* glob: it catches medvemodem-bot APK.
if grep -Fq '"$name"-*."$EXT"' "$script"; then
    echo 'FAIL: ambiguous APK package selector still present' >&2
    exit 1
fi

"$script" --help | grep -q -- '--reinstall'
echo 'PASS: SSH installer syntax and feature contracts'
