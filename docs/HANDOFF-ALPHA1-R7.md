# HANDOFF — MedveModem Alpha 1 r7 release

**Date:** 2026-11-19  
**Release branch:** `release/alpha1-r7` — PR #12 to main.  
**Status:** Release candidate, NOT published. Do not tag or announce r7 until CI is green, packages are built and assets have been verified.

## Changes

- Synchronized `0.16.0_alpha1-r7` in engine, LuCI, Telegram bot and bot LuCI packages and `0.16.0-alpha1-r7` in the shared/browser JavaScript.
- Corrected r6 blank modem page when a cached common.js lacks `mm.setOwnerBadge`: optional badge fails safely; RPC and rendering failures produce visible messages.
- Added shared frontend API/version contracts and Chromium smoke tests before OpenWrt SDK builds and publication.
- Added `medvemodem-install.sh` in the repository root and an SSH one-liner plus safety comments in the main README.
- Installer selects newest *published* MedveModem v* release, stable OR prerelease, filtering out QModem releases and drafts by publication timestamp. Explicit `--tag` pins a version. No hard-coded older-version fallback.
- Installer verifies archive SHA256, package architecture and dependencies before changing packages; warns about disconnects and checks foreign modem controllers.
- Shared LuCI/installer `neighbours` treats orphaned `/etc/config/5gmodem` as a warning rather than an installed manager; live hotplug and packages remain conflicts.
- Added release-specific English highlights; GitHub Actions publishes only explicit version tags, not main pushes.

- Installer lock ownership regression: earlier root installer wrote literal `# HANDOFF — MedveModem Alpha 1 r7 release

**Date:** 2026-11-19  
**Release branch:** `release/alpha1-r7` — PR #12 to main.  
**Status:** Release candidate, NOT published. Do not tag or announce r7 until CI is green, packages are built and assets have been verified.

## Changes

- Synchronized `0.16.0_alpha1-r7` in engine, LuCI, Telegram bot and bot LuCI packages and `0.16.0-alpha1-r7` in the shared/browser JavaScript.
- Corrected r6 blank modem page when a cached common.js lacks `mm.setOwnerBadge`: optional badge fails safely; RPC and rendering failures produce visible messages.
- Added shared frontend API/version contracts and Chromium smoke tests before OpenWrt SDK builds and publication.
- Added `medvemodem-install.sh` in the repository root and an SSH one-liner plus safety comments in the main README.
- Installer selects newest *published* MedveModem v* release, stable OR prerelease, filtering out QModem releases and drafts by publication timestamp. Explicit `--tag` pins a version. No hard-coded older-version fallback.
- Installer verifies archive SHA256, package architecture and dependencies before changing packages; warns about disconnects and checks foreign modem controllers.
- Shared LuCI/installer `neighbours` treats orphaned `/etc/config/5gmodem` as a warning rather than an installed manager; live hotplug and packages remain conflicts.
- Added release-specific English highlights; GitHub Actions publishes only explicit version tags, not main pushes.

 rather than the shell PID into lock owner files. Corrected to `$`; `tests/test-installer-locks.sh` executes live/contended/stale/release lock flows as a release gate.

## Release gates

- [ ] `sh tools/check-web-version.sh`.
- [ ] `python3 tests/test-luci-frontend.py`.
- [ ] `sh tests/test-installer-release-selection.sh`.
- [ ] Chromium browser smoke against the real LuCI view JavaScript, including RPC errors, render failures and stale common.js.
- [ ] All six OpenWrt SDK archives built for aarch64_cortex-a53, aarch64_generic and x86_64 on 24.10.8 and 25.12.6.
- [ ] Verify `SHA256SUMS`, `HIGHLIGHTS.txt`, release notes, package versions and downloads before calling r7 released.
- [ ] Real router check of modem page, all LuCI tabs, and installer upgrade/reinstall with independent WAN/SSH access.

## Critical lessons from r6

On R3S-Himki the engine returned valid modem RPC responses (EP06-E, health ok), but the LuCI page failed `mm.setOwnerBadge is not a function`. The three on-router LuCI JavaScript files matched the r6 tag byte-for-byte: the likely trigger was browser module caching, but that cache hypothesis was not conclusively proven. A guarded badge call fixed the field case. Static version checks alone never substitute for browser tests or defensive optional UI components.

The earlier installer PR #6 diverged from main (r5 shared JS alongside r6 page). Do not merge it wholesale or overwrite newer LuCI files. PR #12 starts from released r6 plus the r6 regression fixes.

## Installer operation

One-line command after merge to main:

```sh
wget -O /tmp/medvemodem-install.sh https://raw.githubusercontent.com/Medvedolog/medvemodem/main/medvemodem-install.sh && sh /tmp/medvemodem-install.sh
```

`--tag v0.16.0-alpha1-r7` opts out of automatic latest selection. GitHub's `/releases/latest` excludes prereleases and must not be used.

## Important caveats

Do not hard-code r6 or r7 as a default release tag. Do not automatically remove third-party modem ownership scripts. Do not claim successful CI merely because tests are in source; inspect GitHub Actions. The user requires changelog, handoff and release highlights for every release and a test for every regression.

## Automatic r7 publication after checks

The main `build` workflow performs LuCI module contract checks and Chromium smoke checks, then builds six SDK targets. `.github/workflows/publish-alpha1-r7-after-build.yml` receives only successful completed `build` runs on `main`. It checks that the exact successful commit is the current main head and has PKG_RELEASE=7, then downloads the original six build artifacts by `workflow_run.id`.

The publisher verifies that every target contains the engine, LuCI, Telegram bot and bot LuCI; repacks six archives, generates SHA256SUMS, creates a **draft** r7 release and downloads all published assets again to verify hashes. Only then does it mark the release public as prerelease. It refuses to overwrite any existing r7 GitHub Release and ignores main builds with newer revisions.

Creating the tag via `gh release create` inside this same workflow is necessary: GitHub Actions releases/tags created with `GITHUB_TOKEN` do not trigger downstream tag-push workflows. Never declare the release complete until the final `Publish ... r7` workflow reports success and the actual Release assets are present.
