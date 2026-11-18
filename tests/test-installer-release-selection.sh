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
echo 'PASS: BusyBox syntax, latest stable/prerelease selection, manifest and README contracts'
