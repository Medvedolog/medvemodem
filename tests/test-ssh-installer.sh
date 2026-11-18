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
"$script" --help | grep -q -- '--reinstall'
echo 'PASS: SSH installer syntax and feature contracts'
