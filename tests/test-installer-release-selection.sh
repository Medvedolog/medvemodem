#!/bin/sh
# Regression contracts for standalone MedveModem SSH installer.
set -eu
cd "$(dirname "$0")/.."
sh -n medvemodem-install.sh
sh -n files/usr/libexec/medvemodem/neighbours
grep -Fq 'TAG=${MEDVEMODEM_TAG:-}' medvemodem-install.sh
grep -Fq "releases?per_page=100" medvemodem-install.sh
grep -Fq "'@[*].published_at'" medvemodem-install.sh
grep -Fq "'@[*].draft'" medvemodem-install.sh
grep -Fq "getline stamp < ARGV[1]" medvemodem-install.sh
! grep -Eq "^[[:space:]]*TAG=\\$\\(paste " medvemodem-install.sh
grep -Fq 'resolve_latest_release' medvemodem-install.sh
grep -Fq 'SHA256 mismatch' medvemodem-install.sh
grep -Fq 'apk add --simulate --allow-untrusted' medvemodem-install.sh
grep -Fq 'opkg install --noaction' medvemodem-install.sh
grep -Fq 'take_package_lock' medvemodem-install.sh
grep -Fq 'audit_modem_owners' medvemodem-install.sh
! grep -q 'TAG=${MEDVEMODEM_TAG:-v0.16' medvemodem-install.sh
! grep -Fq 'fetch "$REPO/releases/latest"' medvemodem-install.sh
! grep -Fq 'RELEASE-HIGHLIGHTS-ALPHA1-R6.md' medvemodem-install.sh
grep -Fq 'raw.githubusercontent.com/Medvedolog/medvemodem/main/medvemodem-install.sh' README.md

# Exercise the actual awk program embedded in the installer with mocked API
# arrays, rather than copying its filtering logic into the regression test.
python3 tests/test-installer-release-runtime.py

# Existing bot packages are carried forward automatically; only first-time
# optional bot installs need a separate choice.
grep -Fq 'if installed medvemodem-bot || installed luci-app-medvemodem-bot; then' medvemodem-install.sh
grep -Fq 'Existing Telegram bot detected; it will be upgraded together with MedveModem.' medvemodem-install.sh
grep -Fq "ask 'Install Telegram bot and its LuCI too? (optional, first-time install)' no" medvemodem-install.sh

# One final confirmation includes the running services and disconnect warning.
grep -Fq 'if ! confirm_installation; then' medvemodem-install.sh
grep -Fq 'warning="This will temporarily stop:$SERVICE_RESTORE. SSH/mobile internet may disconnect."' medvemodem-install.sh
! grep -Fq "ask 'Upgrading stops MedveModem" medvemodem-install.sh
! grep -Fq 'ask "Install $TAG packages now?' medvemodem-install.sh
awk '
    /apk add --simulate --allow-untrusted \$files/ { dry = NR }
    /if ! confirm_installation/ { confirm = NR }
    /^    quiesce_for_upgrade$/ { stop = NR }
    END { if (!dry || !confirm || !stop || dry >= confirm || confirm >= stop) exit 1 }
' medvemodem-install.sh



# A repeat run against the same selected release is a no-op unless forced.
grep -Fq 'installed_version() {' medvemodem-install.sh
grep -Fq '/^P:/ { pkg = substr($0, 3) }' medvemodem-install.sh
grep -Fq '/^V:/ && pkg == name { print substr($0, 3); exit }' medvemodem-install.sh
grep -Fq "sed 's/-alpha/_alpha/'" medvemodem-install.sh
grep -Fq 'All selected packages already match $TAG; no package transaction is needed.' medvemodem-install.sh
awk '
    /expected_version=.*TAG/ { version = NR }
    /All selected packages already match/ { noop = NR }
    /Downloading \$archive/ { download = NR }
    END { if (!version || !noop || !download || version >= noop || noop >= download) exit 1 }
' medvemodem-install.sh
grep -Fq '[ "$all_current" = 1 ] && [ "$FORCE" != 1 ]' medvemodem-install.sh

echo 'PASS: installer release selection, idempotency and confirmation-flow contracts'
