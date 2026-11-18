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
grep -Fq 'docs/RELEASE-HIGHLIGHTS-ALPHA1-R6.md' "$script"
grep -Fq 'Install $TAG packages now?' "$script"
grep -Fq '"$name"-[0-9]*."$EXT"' "$script"
# Avoid the broad medvemodem-* glob: it catches medvemodem-bot APK.
if grep -Fq '"$name"-*."$EXT"' "$script"; then
    echo 'FAIL: ambiguous APK package selector still present' >&2
    exit 1
fi

sh "$script" --help | grep -q -- '--reinstall'
sh "$script" --help | grep -q -- '--allow-disconnect'
# A second SSH install or a concurrent LuCI package transaction must be refused.
grep -Fq 'INSTALL_LOCK=' "$script"
grep -Fq 'PM_LOCK=' "$script"
grep -Fq 'take_lock "$INSTALL_LOCK" || fail' "$script"
grep -Fq "another SSH installer is already running" "$script"
grep -Fq "another MedveModem package operation is running" "$script"
grep -Fq 'take_package_lock' "$script"
grep -Fq 'release_lock "$PM_LOCK"' "$script"
grep -Fq 'release_lock "$INSTALL_LOCK"' "$script"
# Competing modem-manager/hotplug audit must run before release downloads,
# leave QModem and ModemManager installed, and never auto-remove conflicts.
grep -Fq 'audit_modem_owners() {' "$script"
grep -Fq 'audit_modem_owners' "$script"
grep -Fq 'luci-app-5gmodem' "$script"
grep -Fq 'xmm-modem' "$script"
grep -Fq 'luci-app-modeminfo' "$script"
grep -Fq 'luci-app-smstools3' "$script"
grep -Fq 'pidof smsd' "$script"
grep -Fq 'sms-tool' "$script"
grep -Fq '/usr/lib/rooter' "$script"
grep -Fq '*santa*' "$script"
grep -Fq '*automodem*' "$script"
grep -Fq 'COMPATIBLE: $name' "$script"
grep -Fq 'NOTICE: $name installed' "$script"
grep -Fq "installation paused due to competing modem automation" "$script"
grep -Fq 'apk del $name' "$script"
grep -Fq 'opkg remove $name' "$script"
awk '
  /audit_modem_owners$/ { audit = NR }
  /^    prepare_network$/ { network = NR }
  END { if (!audit || !network || audit >= network) exit 1 }
' "$script"
grep -Fq 'quiesce_for_upgrade() {' "$script"
grep -Fq 'restore_services() {' "$script"
grep -Fq 'wait_pids_gone medvemodemd' "$script"
grep -Fq 'wait_pids_gone medvemodem-bot' "$script"
grep -Fq 'wait_pids_gone bot-poll' "$script"
grep -Fq 'curl-poll-' "$script"
grep -Fq 'wait_pids_gone qmodem' "$script"
grep -Fq 'say "Stopping $service..."' "$script"
grep -Fq 'kill -KILL "$pid"' "$script"
grep -Fq "SSH/mobile internet may disconnect" "$script"
grep -Fq 'restore_services' "$script"
# It must finish service shutdown before modifying any packages.
awk '
  /quiesce_for_upgrade$/ { seen = 1 }
  /apk add --allow-untrusted \$files/ { if (!seen) exit 1 }
  END { if (!seen) exit 1 }
' "$script"


# Reproduce the OPiZ3 regression with realistic APK/IPK package names.
# The engine selector must NOT include medvemodem-bot or other packages.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
for ext in apk ipk; do
    mkdir -p "$tmp/$ext"
    if [ "$ext" = apk ]; then
        : > "$tmp/$ext/medvemodem-0.16.0_alpha1-r5.apk"
        : > "$tmp/$ext/medvemodem-bot-0.16.0_alpha1-r5.apk"
        : > "$tmp/$ext/luci-app-medvemodem-0.16.0_alpha1-r5.apk"
        : > "$tmp/$ext/luci-app-medvemodem-bot-0.16.0_alpha1-r5.apk"
    else
        : > "$tmp/$ext/medvemodem_0.16.0_alpha1-r5_aarch64_cortex-a53.ipk"
        : > "$tmp/$ext/medvemodem-bot_0.16.0_alpha1-r5_all.ipk"
        : > "$tmp/$ext/luci-app-medvemodem_0.16.0_alpha1-r5_all.ipk"
        : > "$tmp/$ext/luci-app-medvemodem-bot_0.16.0_alpha1-r5_all.ipk"
    fi
    for name in medvemodem luci-app-medvemodem medvemodem-bot luci-app-medvemodem-bot; do
        count=0
        for item in "$tmp/$ext/$name"_[0-9]*."$ext" "$tmp/$ext/$name"-[0-9]*."$ext"; do
            [ -f "$item" ] || continue
            count=$((count + 1))
        done
        [ "$count" -eq 1 ] || {
            echo "FAIL: expected one exact $name.$ext, got $count" >&2
            exit 1
        }
    done
done
echo 'PASS: SSH installer syntax, package selection, and feature contracts'
