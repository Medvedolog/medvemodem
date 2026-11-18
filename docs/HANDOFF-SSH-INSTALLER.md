# HANDOFF: SSH installer, connectivity, proxy and release lifecycle

**Date:** 2026-11-18. **Status:** draft implementation, GitHub PR [#6](https://github.com/Medvedolog/medvemodem/pull/6); first successful four-package APK upgrade demonstrated on OPiZ3; service/runtime acceptance, IPK, proxy, reinstall and removal tests remain open. This is separate from QModem's AT/lifecycle handoff. Keep the installer out of the root README and public installation one-liners until acceptance checks pass.

## 1. Scope

- Standalone `medvemodem-install.sh` for the router's SSH console; adapt BearGuard's release-fetch pattern rather than copying its single-package install.
- OpenWrt `24.10.x` → `opkg` + IPK packages from 24.10.8 SDK; `25.12.x` → `apk` + APK packages from 25.12.6 SDK, with explicit patch-version warning. Prebuilt package arches: `aarch64_cortex-a53`, `aarch64_generic`, `x86_64`.
- Default tag `v0.16.0-alpha1-r5` **must exist in GitHub Releases**; never interpret 404 as proof of blocked GitHub. Do not use the GitHub `latest` API for prereleases.
- Install/update engine `medvemodem` and `luci-app-medvemodem`, with a **separate** explicit question for `medvemodem-bot` and `luci-app-medvemodem-bot`.
- Support `--install`, `--update`, `--reinstall`, `--force`, `--remove`, `--remove-bot`, `--bot`, `--no-bot`, `--tag`, and `--proxy`. Force means reinstall the same package version **without** weakening integrity, dependency, or architecture checks.
- QModem is a separate component and must not be removed as a side effect. Before any full removal, warn about losing the connection and require explicit confirmation.

## 2. Observed run and correction

OPiZ3, OpenWrt **25.12.5**, `aarch64_cortex-a53`, `apk`:

```
OpenWrt 25.12.5 / aarch64_cortex-a53 / apk; available release archive built for 25.12.6
WARNING: firmware and package SDK patch version differ; dependency dry-run must succeed.
Checking GitHub directly...
curl: (22) The requested URL returned error: 404
GitHub direct unavailable
HTTP mixed proxy URL (http://host:port):
```

The user stopped at the proxy prompt; no successful installation was reported. The missing `v0.16.0-alpha1-r5` release, not necessarily network censorship, caused this 404. QModem `qmodem-3.3.0_p20261108-r2` had already published. Installer branch commit `e50f502` added a direct GitHub API probe and explicit error on absent tag/manifest. Recheck implementation and test all cases: API auth/rate limiting, GitHub redirects, 404 vs 403 vs connection failures.

## 3. Temporary proxy architecture

- Direct GitHub release and OpenWrt feed checks first. On genuine connection errors, accept HTTP/mixed proxy URL, optional login/password.
- Only export `http_proxy`, `https_proxy` (and upper-case equivalents) to the installer's processes for fetching releases, `opkg update`/`apk update` and package install. Never alter `/etc/opkg/`, `/etc/apk/`, UCI, firewall, netifd, `/etc/profile`, or persistent environment.
- Avoid exposing credentials in command traces/logs; never ask users to share full proxy URLs with authentication. Verify BusyBox curl/wget and both package managers support the chosen transport and credentials on target firmware.
- A GitHub API 404 or missing asset is a **release-content failure**; a proxy cannot repair it.

## 4. Integrity and package behavior

- Check exact firmware line, architecture and package names.
- Download release `SHA256SUMS`, verify selected archive SHA256, inspect archive paths and reject unexpected entries.
- Run package-manager dry-run before installation. No force-depends, allow-unsigned checksums, or mismatched kernel dependencies. For local GitHub APK packages OpenWrt requires explicit `--allow-untrusted` *only* after the GitHub archive SHA256 has passed.
- `opkg install --force-reinstall` / OpenWrt's `apk add --force-reinstall` limited to the requested package files.
- Remove bot UI and core before MedveModem LuCI and engine; preserve existing config as package manager allows. Document package-specific post-remove behavior and prevent loss of management access.

## 5. Release and documentation gates

- [ ] Host shell syntax + static regression test `sh tests/test-ssh-installer.sh` actually executed.
- [ ] Fully simulate 404, failed DNS/connectivity, proxy auth rejection and SHA256 mismatch before any package mutations.
- [x] Validate the APK installer transaction on actual OPiZ3 25.12.5 (r3 → r5, four packages, dependency dry-run successful). Modem continuity and runtime validation still open.
- [ ] Validate 24.10 IPK and 25.12 APK installations, normal upgrade, same-version reinstall, bot optional choice, removals.
- [ ] Confirm mixed HTTP proxy is temporary for BOTH `opkg`/`apk` (no UCI/feed changes), credentials not logged.
- [ ] Confirm successful GitHub release publication of target r5 and distinct QModem r2 artifacts.
- [ ] Update `README.md` title area, `docs/INSTALL.md`, `docs/INSTALL.en.md`, `docs/README.md` and release notes with **verified** one-line SSH install only after gate passes.
- [ ] Keep detailed failure report and compatibility limitations in this handoff and `docs/SSH-INSTALLER.md`; update changelog on every behavior change.

## 6. Current implementation references

- `medvemodem-install.sh` in `feat/ssh-installer`.
- `tests/test-ssh-installer.sh` contract tests.
- `docs/SSH-INSTALLER.md` installer usage and acceptance checklist.
- `docs/CHANGELOG.md` (полный русский журнал сохранён и дополнен) and `docs/CHANGELOG.en.md` (English), both updated for r5 and the installer PR. Keep the two histories synchronized for later fixes.
- `docs/HANDOFF-QMODEM-FM350.md` for QModem lock lifecycle and operator telemetry; do not conflate the two workstreams.

## 7. Second OPiZ3 trial and release highlights (2026-11-18)

- OpenWrt 25.12.5 / aarch64_cortex-a53, apk: GitHub reachable directly, all feeds refreshed (11,060 packages), Telegram bot requested, r5 archive downloaded and SHA256 verified. Before dry-run, package selection failed with `ERROR: duplicate medvemodem package`. Root cause: APK filename selector `medvemodem-*.apk` also matched `medvemodem-bot-*.apk`. Fix `b082728` restricts selected package basename to `NAME-[0-9]*.apk` or `NAME_[0-9]*.ipk`. **The field attempt never executed `apk add`**. Re-test on OPiZ3.
- User requested English-only installer console output and displayed release highlights **before** any yes/confirmation. The script now prints release tag, firmware/package architecture, link and English highlights before asking about installing the bot; after dependency dry-run it asks a final explicit install confirmation. Noninteractive action flags remain usable.
- English r5 notes are tracked in `docs/RELEASE-HIGHLIGHTS-ALPHA1-R5.md` on main. Existing release originally lacked highlights; `publish-r5-highlights.yml` was added to upload HIGHLIGHTS.txt and edit GitHub Release notes. Confirm this workflow succeeded before claiming assets exist.
- No hardware install success yet. Keep main README one-liner and main installation docs gated on real APK/IPK and proxy/rollback verification.

## 8. Successful OPiZ3 APK field installation — 2026-11-18

- User executed the fresh SSH script on OPiZ3 running OpenWrt **25.12.5**, `aarch64_cortex-a53`, using `apk`. Released package archive was SDK **25.12.6**; mismatch warning displayed, dependency simulation proved the package set installable on this router.
- **Direct** GitHub download and all configured feeds succeeded, including the user-maintained WLAN backport feed; `apk update` reported **11,060** available packages. No temporary proxy was necessary (proxy behavior not tested).
- Available r5 release, URL and **English HIGHLIGHTS.txt** appeared **before** the Telegram bot consent prompt. User opted in to bot + bot LuCI.
- `medvemodem-0.16.0-alpha1-r5-aarch64_cortex-a53-25.12.6.tar.gz` downloaded and SHA256 was verified. Exact APK file selection succeeded after numeric-version matching fix. `apk add --simulate --allow-untrusted` reported upgrades for all four packages, `r3 → r5`, no dependency conflicts.
- User approved the final install confirmation. Actual `apk add --allow-untrusted` upgraded `medvemodem`, `luci-app-medvemodem`, `medvemodem-bot`, `luci-app-medvemodem-bot` to `0.16.0_alpha1-r5`; all four package hooks ran; transaction returned `Installed successfully`.
- `apk` wrote **`/etc/config/medvemodem.apk-new`** and **`/etc/config/medvemodem-bot.apk-new`** rather than replacing existing UCI files. Compare configuration differences and test runtime; **do not blindly overwrite live config or apply defaults**.
- **Accepted:** online fetch, release highlights, selection/consent, hash verification, dependency dry-run, APK upgrade. **Not yet accepted:** daemon and LuCI live operation, bot runtime, modem stability, 24.10/opkg, forced reinstall, removals, disconnected feeds/GitHub, temporary authenticated mixed proxy.
- Do not merge PR #6 or claim complete cross-version support until the remaining test matrix passes; root README and install docs remain gated.

## 9. Service quiescence during upgrade — requested after r5 field trial

- The successful r3→r5 APK upgrade was followed by observed duplicated processes: two `medvemodemd` and two bot ucode instances. Their parent/daemon ownership is not yet established: do not assume duplicate workers are always orphaned without PID/procd inspection.
- Draft installer now prepares a quiescent package transaction: after SHA256 and dependency dry-run, **before** package mutation, prompt for consent that QModem internet and SSH may be interrupted; stop `medvemodem-bot`, `medvemodemd`, and installed QModem init services (`qmodem_network`, `qmodem_monitor`, `qmodem_hc_sim`, `qmodem_init`). Record and restore only services previously reported running.
- Wait up to 10 seconds for the engine, bot, and QModem dial processes to disappear. If stop fails, TERM **identified process IDs**, then SIGKILL **only IDs that remain**. Never `killall ucode`, `killall sh`, or kill unrelated ModemManager/other user processes.
- On installer exit or package transaction error, perform best-effort restore; the explicit `--allow-disconnect` is required for unattended installations. Without confirmation/flag, do not touch running services.
- **Not field tested**: service stop/restart sequencing, QModem lock lifetime, procd respawn and SSH connectivity. Testing requires wired/serial or independent non-QModem management access. Once validated, test repeated r5 reinstall and single-instance behavior, including process ancestry and `ubus call service list`.

## 10. r6 candidate and bot release metadata (2026-11-18)

- The SSH installer branch now defaults to `v0.16.0-alpha1-r6` instead of r5. Fallback English highlights point to `docs/RELEASE-HIGHLIGHTS-ALPHA1-R6.md` in the `release/r6` candidate branch. Once the r6 Release exists, it should download the immutable release-owned `HIGHLIGHTS.txt` instead.
- The r6 package candidate is PR #9, combining PR #8 fixes and the bot update-version regex correction. Release CI must publish all six SDK tarballs, `SHA256SUMS` and `HIGHLIGHTS.txt`; until then, an installer 404 is expected.
- User requested verifying Telegram update checks: `update.uc` consumes `self-update status`, checks every 12 hours, then notifies once for a newer version; `updview.uc` did not accept underscores in APK version strings. r6 corrects this. No bot hardware verification for r6 has been completed.
- Never claim the new installer lifecycle is tested because the previous r3→r5 successful upgrade happened **before** quiescence was added.

## 11. r6 field regression: blank LuCI modem detail and mandatory pre-release QA (2026-11-19)

**Field evidence, R3S-Himki / OpenWrt 25.12.6 / aarch64_generic:**
- The published r6 `common.js`, `dashboard.js` and `modem.js` files on the router matched the `v0.16.0-alpha1-r6` source byte for byte (verified by SHA256). `ubus call medvemodem devices` and `ubus call medvemodem device` returned valid objects; the device was `EP06-E` with health `ok`.
- The LuCI detail page was blank until a temporary diagnostic view revealed: `Unable to display modem data: mm.setOwnerBadge is not a function`. The owner badge is an optional feature introduced in r6; an older cached `medvemodem.common` module may omit this method even when the on-disk file is r6. A matching VERSION constant alone does not prove the browser loaded the matching module.
- A guarded `typeof mm.setOwnerBadge === 'function'` fallback restored the modem card on hardware. LuCI must never lose basic controls because a decoration/helper API is missing. The view now leaves panels visible during RPC/render failures and shows diagnostic messages.
- PR #6 was behind main: `common.js` and `dashboard.js` were r5 and `modem.js` was r1/r6 at different points, despite a clean r6 tag. The branch's LuCI files were synchronized to r6; PR remains subject to merge-conflict review.

**Release gate implementation in PR #6:**
- `tests/test-luci-frontend.py`: shared/public `mm.*` API contract, exact view/common version consistency, optional badge fallback and visible error contract. This caught the branch's mixed r5/r6 files and failed CI until corrected.
- `tests/test-luci-browser.cjs`: headless Chromium loads the *actual* LuCI modem view JS with stubbed LuCI/browser dependencies, simulates failed `device` RPC and rendering errors and executes the owner's badge fallback with an older common API. This is a smoke test, not full router or all-modem UI integration.
- `.github/workflows/build.yml` gates every six-architecture OpenWrt build and release publication behind `luci-contracts` AND `luci-browser` jobs. `.github/workflows/ssh-installer-tests.yml` executes both on the development branch. Branch protection should require these checks on main before merging.
- Test instructions locally: `python3 tests/test-luci-frontend.py`; `npm install --no-save --no-package-lock playwright@1.58.2 && npx playwright install chromium && node tests/test-luci-browser.cjs`. CI installs Chromium OS dependencies.
- **Still required before r7:** test branch CI green, merge safely after resolving PR #6 conflicts, inspect the resulting diff for obsolete r5 code, test actual OpenWrt router in normal browser and incognito, APK/IPK clean install/update/reinstall, and verify fallback against older cached common module. Release should not advance merely because package compilation succeeded.

**Installer neighbour false positive also corrected:** an orphaned `/etc/config/5gmodem` UCI file emits `WARN residual-config` for installer preflight, not `BLOCK`, and is excluded as a 5gmodem package from normal LuCI JSON; live 5gmodem hotplug remains a block.

