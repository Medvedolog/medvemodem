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
grep -Fq "sort -r" medvemodem-install.sh
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

# The same awk release policy as the installer: latest published v* tag wins,
# whether stable or prerelease; other projects and drafts must not win.
selector='$1 ~ /^20[0-9][0-9]-/ && $2 ~ /^v[0-9][0-9A-Za-z._-]*$/ && $3 == "false" {print $2; exit}'
result=$(printf '2026-11-19T15:00:00Z\tqmodem-4.0-r1\tfalse\n2026-11-19T14:00:00Z\tv0.16.0-alpha1-r7\tfalse\n2026-11-19T13:00:00Z\tv0.16.0-alpha1-r6\tfalse\n2026-11-19T16:00:00Z\tv0.16.0-alpha1-r8\ttrue\n' |
  LC_ALL=C sort -r | awk -F '\t' "$selector")
[ "$result" = 'v0.16.0-alpha1-r7' ] || {
  echo "FAIL: newest published release selection: $result" >&2
  exit 1
}
echo 'PASS: BusyBox syntax, latest stable/prerelease filtering, manifest and README contracts'
