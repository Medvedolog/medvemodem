# HANDOFF — MedveModem Alpha 1 r7 release

**Date:** 2026-11-19  
**Release branch:** `release/alpha1-r7` — PR #12 to main.  
**Status:** r7 published as prerelease on 2026-11-18 22:21 UTC from `9a1f28f`. Later `main` commits are post-r7 source changes; they are **not** included in the published r7 packages.

## Changes

- Synchronized `0.16.0_alpha1-r7` in engine, LuCI, Telegram bot and bot LuCI packages and `0.16.0-alpha1-r7` in the shared/browser JavaScript.
- Corrected r6 blank modem page when a cached common.js lacks `mm.setOwnerBadge`: optional badge fails safely; RPC and rendering failures produce visible messages.
- Added shared frontend API/version contracts and Chromium smoke tests before OpenWrt SDK builds and publication.
- Added `medvemodem-install.sh` in the repository root and an SSH one-liner plus safety comments in the main README.
- Installer selects newest *published* MedveModem v* release, stable OR prerelease, filtering out QModem releases and drafts by publication timestamp. Explicit `--tag` pins a version. No hard-coded older-version fallback.
- Installer verifies archive SHA256, package architecture and dependencies before changing packages; warns about disconnects and checks foreign modem controllers.
- Shared LuCI/installer `neighbours` treats orphaned `/etc/config/5gmodem` as a warning rather than an installed manager; live hotplug and packages remain conflicts.
- Added release-specific English highlights; GitHub Actions publishes only explicit version tags, not main pushes.

- Installer lock ownership regression: an earlier installer wrote a literal placeholder instead of its process PID into lock owner files. The current shell writes `$$` (its PID); `tests/test-installer-locks.sh` exercises live/contended/stale/release locks as a release gate. Corrected to `$`; `tests/test-installer-locks.sh` executes live/contended/stale/release lock flows as a release gate.

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

## Post-release field incident: BusyBox `paste` missing (2026-11-19)

- R3S-Himki / OpenWrt 25.12.6 aarch64_generic: root installed the SSH installer from `main`, chose 1 (install/update); after GitHub API selection started, shell reported `line 296: paste: not found` and exited before any package transaction. The existing modem installation was unchanged.
- Cause: `resolve_latest_release` used the coreutils `paste` command, not guaranteed on minimal OpenWrt. Source/static CI tests imitated the selection algorithm but did not execute the installer's actual command with OpenWrt tool availability; this was a validation gap.
- Fix: `main` commit `5126a9f` replaces `paste|sort|awk` with BusyBox-compatible `awk` using `getline` on the three JSON-filtered data files, comparing `published_at` values. Fail closed when no valid v* published MedveModem release exists (no fallback to old tags).
- Regression tests: `tests/test-installer-release-runtime.py` extracts and executes the *actual* awk program with fixtures for unrelated QModem release, drafts, newer stable and prerelease, and empty selection; wired into `tests/test-installer-release-selection.sh` and the release CI gate.
- Workaround with an old installer already present: `sh /tmp/medvemodem-install.sh --tag v0.16.0-alpha1-r7 --install` bypasses automatic release selection while preserving checksums and preflight. To test the fixed auto-selector, re-download the script from `main` and start again.
- Post-release script changes do not overwrite published r7 assets. Verify on actual BusyBox ash and jsonfilter before declaring hardware acceptance; the CI fixture is still not a live router test.

## Branch cleanup register (2026-11-19)

Branch policy: continue on `main`; do not create a working branch for EP06-E. The following seven refs were checked against `main` through GitHub comparison: **ahead=0**, with the branch tip already an ancestor of `main`. They are **candidates for deletion after one final ref/PR check**, not deleted by this handoff:

| Branch | Tip checked | Deletion criterion |
| --- | --- | --- |
| `ci/publish-r7-after-verified-build` | `22ce061` | merged / ancestor of `main` |
| `ci/qmodem-persistent-release` | `80386fb` | merged / ancestor of `main` |
| `fix/modem-view-version-owner-badges` | `d8ff453` | merged / ancestor of `main` |
| `fix/qmodem-owner-failover-alerts` | `ebde82a` | merged / ancestor of `main` |
| `fix/r4-whitelist-backup-labels` | `95388fd` | merged / ancestor of `main` |
| `release/alpha1-r7` | `d25dec9` | merged / ancestor of `main` |
| `release/r6` | `539c43d` | merged / ancestor of `main` |

**Do not mark as merged or delete without reconciliation:** `ci/alpha1-qmodem-matrix`, `feat/owner-badges-optional-dashboard`, `feat/ssh-installer`, `fix/native-qmi-mbim-selector`, `fix/r6-luci-browser-ci`, `fix/r6-update-install-locks`, `fix/self-update-release-check`, `release/0.16.0-alpha1-r2`. These eight refs have unique commits relative to `main`, even when their functionality may already have been superseded. Review unique patches individually; do not merge old LuCI/installer code wholesale. Never delete `main`.

## EP06-E follow-up in main (2026-11-19, post-r7)

- Native QMI selection remains strictly opt-in. Do not change working ModemManager priority or claim that changing owners has been tested on the physical EP06-E.
- Native, engine-owned AT cell reads now pair `QCAINFO` with read-only `QTEMP` for profile `quectel-ep06`. Maximum valid signed temperature is published as `heat.modem_c`. Invalid/unpopulated sensors are rejected; `tests/test-ep06-temperature.py` compiles the actual C parser and tests nine fixture replies before SDK jobs.
- Existing `QCAINFO` parser already distinguishes SCC state=1 (configured/deactivated) from state=2 (activated). Keep the reporting semantic: idle SCC is not an active carrier.
- ModemManager-owned AT ports remain off limits to native commands; no `QTEMP`, `QNWLOCK` or other background direct AT queries while MM owns EP06-E. MM telemetry remains the supported read path in that mode.
- `QNWLOCK` is still a **nonimplemented, firmware-probe-required cell lock**. Do not expose a write control without exact read syntax, previous-state snapshot, exclusive AT ownership and rollback testing. `QRSRP` / `QENG` detailed metrics, SMS/PDU edge cases, and QMI UIM slot selection still require integration and hardware verification.
- No release tag or package publication was created for these post-r7 commits. Confirm the newest `main` CI, then validate on physical EP06-E with independent recovery access.
