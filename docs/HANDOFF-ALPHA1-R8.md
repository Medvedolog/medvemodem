# HANDOFF — MedveModem 0.16.0 Alpha 1 r8

**Date:** 2026-11-19  
**Working branch:** `main` only. No r8 feature/release branches were created.  
**Status:** r8 source/release candidate; GitHub Release and six SDK artifacts require the explicit, gated manual workflow. Do not claim publication or on-router acceptance until the release URL and jobs confirm it.

## R8 scope

- EP06/EG06 (USB 2c7c:0306) uses an isolated `quectel-ep06` AT radio dialect. The automatic control owner stays ModemManager on the existing working router. Native QMI is manually selectable.
- The native, engine-owned AT radio reader queries `QCFG="nwscanmode"`, `QCFG="band"`, both `QNWLOCK` forms. The parser accepts only complete, numeric replies, rejects the unfinished `common/lte` status, and treats unreadable dual-form state as **no lock write permission**.
- `common/4g`: number of cells then complete EARFCN/PCI pairs (multi-cell supported). `common/lte`: action 0=unlock, 1=EARFCN only, 2=EARFCN+PCI, followed by status. A user lock writes one selected EARFCN+PCI pair. The original locking configuration (including full /4g multi-cell lists) is saved for rollback; /lte query-only completion STATUS is deliberately **not** sent as a write argument. No automatic `save_ctrl` or undocumented QCFG/USB composition writes.
- `radio_set` retains its read-before-write (recent AT reading), AT-port lease, journal, connectivity guard and rollback. Native EP06 writes cannot combine lock and network-mode/band edits; the LTE QCFG controls remain read-only. The LuCI editor is intentionally hidden for those native controls, but ModemManager's own supported network-mode editor remains available under MM ownership.
- `cells` uses `QCAINFO` plus `QENG="servingcell"` and `QENG="neighbourcell"`. QENG LTE neighbour fields are EARFCN/PCI/RSRQ/RSRP/RSSI. Filter out the serving PCI/EARFCN pair and avoid treating QCA records from a different serving snapshot as the current one. The native EP06 display prefers fresh engine AT cells over obsolete ModemManager snapshots.
- `QTEMP` reports the maximum plausible signed sensor reading on a native, engine-owned port only. Out-of-range/unused sensors are ignored. Existing `QCAINFO` LTE SCC activity semantics are preserved; no direct AT polling of an MM- or QModem-owned port.
- Russian LuCI strings describe manual owner selection, native EP06 lock-only editing and the count of locked cells.
- Versions aligned to `0.16.0_alpha1-r8` for the engine and all dependent OpenWrt packages; browser JS uses `0.16.0-alpha1-r8`.
- SDK packages are `workflow_dispatch` only. Release publishing requires opt-in `publish_release=true`, a version-matching `release_tag=v0.16.0-alpha1-r8`, successful frontend/Chromium gates, six SDK jobs, valid SHA256 sums and verified GitHub assets.
- Include the BusyBox SSH installer fix for `paste: not found` added to `main` after r7.

## Seven archived branches — examined 2026-11-19

All have `ahead=0` against the current `main`: all unique commits are already ancestors of `main`. **Do not merge any of these again.** Mark as safe candidates for deletion, pending one final PR/ref check immediately before deletion.

| Branch | Merge evidence | Next action |
| --- | --- | --- |
| `ci/publish-r7-after-verified-build` | PR #13 merged; ahead=0 | candidate to delete |
| `ci/qmodem-persistent-release` | PR #4 merged; ahead=0 | candidate to delete |
| `fix/modem-view-version-owner-badges` | PR #8 merged; ahead=0 | candidate to delete |
| `fix/qmodem-owner-failover-alerts` | PR #5 merged; ahead=0 | candidate to delete |
| `fix/r4-whitelist-backup-labels` | current tip ancestor of main; ahead=0 | candidate to delete |
| `release/alpha1-r7` | PR #12 merged; ahead=0 | candidate to delete |
| `release/r6` | PR #9 merged; ahead=0 | candidate to delete |

**Not part of that deletion set:** `ci/alpha1-qmodem-matrix`, `feat/owner-badges-optional-dashboard`, `feat/ssh-installer`, `fix/native-qmi-mbim-selector`, `fix/r6-luci-browser-ci`, `fix/r6-update-install-locks`, `fix/self-update-release-check`, `release/0.16.0-alpha1-r2`. Their tips have commits not present on `main`; reconcile each patch, do not indiscriminately merge or delete. No branches were removed in this operation.

## Verification gates and hardware test

- [ ] `python3 tests/test-ep06-cell-lock.py`: compile actual C QNWLOCK parser, validate eleven firmware-shape/malformed/pending fixtures.
- [ ] `python3 tests/test-ep06-temperature.py`: compile actual C QTEMP parser, validate nine fixtures.
- [ ] `python3 tests/test-luci-frontend.py`, `node tests/test-luci-browser.cjs`, `sh tools/check-web-version.sh`.
- [ ] All six OpenWrt SDK packages built: aarch64_cortex-a53 / aarch64_generic / x86_64 across 24.10.8 (IPK) and 25.12.6 (APK).
- [ ] SHA256SUMS, HIGHLIGHTS.txt, tag `v0.16.0-alpha1-r8`, package versions and artifact downloads confirmed; only then release can be marked published.
- [ ] Physical EP06-E on OpenWrt: ModemManager first; verify no takeover, no AT contention and unchanged WAN. Manually switch the owner to native QMI with independent SSH/WAN recovery access, verify a working data session.
- [ ] Native EP06: `ubus call medvemodem radio '{"id":"<device_id>","refresh":true}'` shows `cell_lock_rats:["lte"]` only with valid QNWLOCK query; list current and neighbouring cells with correct EARFCN/PCI. Lock one, verify `QNWLOCK` readback; remove it and verify auto selection.
- [ ] Negative tests: an unsupported QNWLOCK form must disable Lock, dual active forms must refuse writes, a pending status must refuse writes, expired state must re-read, and loss of connectivity must trigger rollback to the exact previous multi-cell list.
- [ ] Validate temperature, CA active/deactivated, SMS and AT console under each owner; never run raw AT simultaneously with ModemManager.

## Operating policy

Do **not** add new branches for EP06 work. Update **handoff + highlights + Russian and English changelogs** for each release. User authorized r8 publication, but release jobs and physical testing must not be falsely reported as successful before evidence. Unsolicited push/PR builds are disabled; further SDK runs require explicit action.

## One-shot owner command dispatch for r8

Control issue: [#14](https://github.com/Medvedolog/medvemodem/issues/14). The exact comment `/medvemodem publish r8` by GitHub login `Medvedolog` triggers only the `release-command-r8.yml` bridge, which invokes `build.yml` with `workflow_dispatch`, `publish_release=true`, `release_tag=v0.16.0-alpha1-r8`. All other comments and authors skip the dispatch job. The builder itself remains manual-only. No QModem build or PR/push SDK trigger is enabled.

After the explicit owner command, inspect the build run, gate outcomes and release assets. If any test or package fails, **do not** call r8 published. The one-shot bridge should be retired after successful release. This command is a user-authorized exception to the normal lack of automatic builds, not a recurring publishing scheduler.

## Post-r8 incident: EP06-E native QMI choice silently not applied (2026-11-19)

**Field evidence:** after installing the published r8 on the router, the operator selected native management for EP06-E but saw no lower toast, no clear result beside Save and no effective switch away from ModemManager.

**Root causes found in source:** `saveConnection()` and `handOver()` never called the common `mm.toast()` notification for owner changes and treated `bringup: started` as the last UI response; the actual owner release was asynchronous and its failure was not displayed. LuCI's EP06 `withAtCells()` and `askCells()` checked the impossible literal `owner.control === "native"` although the daemon reports `netifd-qmi`, `netifd-mbim` or `medvemodem-at`. Native QMI selection also depended unnecessarily on an exact resolved modem profile even if a genuine `qmi_wwan` + `/dev/cdc-wdm*` control function was present.

**Post-r8 source fixes on main (NOT present in the already published r8 packages):**
- LuCI native choice now accepts a discovered QMI/MBIM control driver and node independently of exact profile matching. This does not change automatic owner priority, and `uqmi` or `umbim` is still required for the selected netifd protocol.
- Both standard Save and foreign-interface handover report progress in three places: top action message, visible status beside Save and shared bottom toast. UCI readback verifies the saved `backend` option before starting `bringup`.
- A `started` response is explicitly pending, **not** success. The next 5-second device polls verify the chosen UCI backend, real `owner.control`, `backend` ledger and session state; detect owner conflicts, incomplete transfer, missing connection or timeout, with explanations.
- EP06 native `QENG` cell readings now recognize `netifd-qmi`, `netifd-mbim` and `medvemodem-at`, instead of a nonexistent `native` control label.
- Russian translations added for all new statuses. `tests/test-luci-frontend.py` statically gates the owner-reporting contracts; the real-view Chromium suite exercises persistence, pending result, native-QMI handover confirmation, toast and fresh EP06 cells.
- A manual-only lightweight frontend QA path `/medvemodem qa luci` on control issue #14 runs two jobs without OpenWrt SDK. It does not start package compilation or publication.

**Important:** Do not report a fixed installed r8; the correction requires a later versioned package/release and on-router acceptance. On the r8 router, inspect `uci show medvemodem | grep backend`, `ubus call medvemodem devices`, and `logread -e medvemodem -e ModemManager` to distinguish unsaved preference, refused bring-up and failed ModemManager release. Do not issue direct AT commands while MM owns the modem.

### Source-only QA result (2026-11-19)

- [x] Final manual frontend contract/JS syntax run: [Actions #44478836837](https://github.com/Medvedolog/medvemodem/actions/runs/44478836837), **success**.
- [x] Final Chromium real LuCI view smoke (including UCI write/readback, progress toast, not treating `started` as success, and `netifd-qmi` cells): same run, **success**.
- [x] Previous manual smoke baseline: [Actions #44478393451](https://github.com/Medvedolog/medvemodem/actions/runs/44478393451), **success**.
- [ ] OpenWrt SDK packaging for **the post-r8 fix**: not started (the published r8 already predates the fix; a new package revision/tag is required).
- [ ] Physical EP06-E owner handover / SMS / QNWLOCK acceptance on router: not yet verified.
- No OpenWrt SDK jobs, release creation, or tag moves were triggered by these QA runs.


## Post-r8 modem-zoo ingestion: Foxconn T99W640 SDX72 (2026-11-19)

**Input:** user-supplied 35-page "AT/DIAG_REFERENCE", revision 2.4 dated 2026-11-18; ENG live-tested **read-only** AT commands, Retail static firmware cross-check; destructive writes/FTM/DIAG were not tested live. Reference is not a verified MedveModem feature implementation.

- [x] Added [SDX72 vendor AT, all 34 ftm commands, QCDM inventory and NV/EFS paths](modem-zoo/modems/foxconn-t99w640-at-diag.md); new anchor from the [existing hardware entry](modem-zoo/modems/foxconn-t99w640.md) and [AT command catalog](modem-zoo/AT-COMMANDS.md).
- [x] Updated experimental USB-MBIM profile `profiles/generic/foxconn-t99w640-mbim.json` with firmware provenance, 26 researched read-only queries, transport differences, LTE **PCI-first** and NR lock argument semantics, inverted CA-enable logic, four temperature sensors, per-antenna signal, FCC/authentication limitations, 34 FTM entries, and DIAG reference flags.
- [x] Recorded **ENG crash on AT+VZWRSRP? and AT+VZWRSRQ?**, bare `AT+ODIS` factory reset, bare `AT+LTE_LOCK` unlock, bare `AT+ULCA` mutation, and write/SDC/EDL/NV/EFS hazards. **The profile's forbid list is documentation metadata, not proof that every raw AT console path globally rejects these commands**; a future raw-console gate would require a separate audited implementation.
- [x] Preserved current owner policy: `modemmanager` first, USB-only `05c6:90d5` with model match, no direct AT while MM owns it, no automatic switching, DIAG/FTM/writes disabled, no driver rebind or USB reset. The private PCIe QMI-over-MBIM AT gateway is **not implemented**.
- [ ] Live testing of the vendor PCIe/USB AT transport, especially Retail behaviour, requires a physical bench and explicit operator consent. No modem write commands or modem-side reset were run.
- [ ] No build or release was requested for this zoo/profile-only knowledge update; published r8 binaries remain unchanged.


## Physical EP06-E diagnostic upload — 2026-11-19 09:22 (post-r8)

**Measured report:** r8 engine `0.16.0_alpha1-r8`, OpenWrt 25.12.6 `aarch64_generic`, Linux 6.12.112, Quectel EP06-E **EP06ELAR04A06M4G**, USB `2c7c:0306`. Exact profile `quectel-ep06`; QMI host driver and nodes `qmi_wwan`, `/dev/cdc-wdm1`, `wwan1`. AT: `/dev/ttyUSB5` (interface 2; previously used by pppd) and `/dev/ttyUSB6` (interface 3, selected by MM). GNSS `ttyUSB4`, MM-ignored `ttyUSB3`. **Do not hard-code ttyUSB numbers: use USB interface role mapping.**

- [x] MM is connected and fully functional (`mm5`, MTS RUS LTE, APN `internet.mts.ru`, data-path `wwan1`); report shows RSRP −82, RSRQ −12, SINR 3.4. Reach probe OK and modem backup-ready.
- [x] Confirmed **failed/nonpersisted native selection at capture**: `connection.backend = auto` and `owner.control/session/at = modemmanager`. No evidence of native `uqmi` invocation. The reported QMI nodes exist, so lack of `qmi_wwan` is not the explanation.
- [x] The 09:08:40 operator freeze inhibited MM and tore down `mm5`; at 09:08:42 the operator released inhibition; MM reacquired and reconnected at 09:08:59. This is **not** a successful native handover. A brief freeze/unfreeze sequence may be the separate freeze control, not the backend Save; do not conflate them.
- [x] Confirmed inherited generic Quectel `qmodem`-first policy for the EP06 profile; added **EP06-specific MM-first owner_priority** with explicit-only native QMI/QModem. Protect existing working MM and prohibit automatic AT/QMI contention.
- [x] Saved measured firmware/topology in `profiles/generic/quectel-ep06.json` and linked analysis in `docs/modem-zoo/modems/quectel-lte-ec25-ep06-em12.md`.
- [ ] **Cause of the failed r8 Save is not proven by the report alone**: no raw LuCI-to-ubus RPC, UCI commit trace, or post-selection live `backend=native` state. Existing source-level corrections on main require a new versioned build.
- [ ] Check exact EP06 firmware `EP06ELAR04A06M4G` on router under explicitly chosen native QMI, with independent Ethernet recovery, before claiming `QNWLOCK` writes or native SMS/AT support.
- [ ] New SDK package not started and published r8 unaffected.

**Safe read-only check on that router**, if an operator repeats the Save test without disturbing MM:
```sh
uci show medvemodem | grep -E 'backend|\.id='
ubus call medvemodem devices
logread -e medvemodemd -e ModemManager | tail -n 90
```
Do **not** issue bare `AT+QNWLOCK`, change modem USB composition or force MM inhibition just to collect evidence.


## Branch cleanup — 2026-11-19 (explicit owner approval)

- **Seven obsolete merged branches deleted** by owner-gated one-shot GitHub Actions run [#44504196840](https://github.com/Medvedolog/medvemodem/actions/runs/44504196840), successful. For every branch the workflow checked that its commit was an ancestor of `main`, no open PR existed, and the remote SHA still matched the fetched SHA, then deleted the exact allowlisted ref.
- Deleted: `ci/publish-r7-after-verified-build`, `ci/qmodem-persistent-release`, `fix/modem-view-version-owner-badges`, `fix/qmodem-owner-failover-alerts`, `fix/r4-whitelist-backup-labels`, `release/alpha1-r7`, `release/r6`.
- Verified afterward using the actual GitHub branches endpoint: **9 branches remain including `main`**. Existing tags and releases were untouched; SDK workflows were not invoked. The one-shot prune workflow is to be removed from `main` after execution.
- **Eight branches retained pending content reconciliation**. Ahead counts below are *history differences*, not proof a feature is missing from current `main`; many changes have already been implemented through separate commits.
  - `ci/alpha1-qmodem-matrix` (ahead 15; open PR #1): QModem workflow and installer — `main` already contains `.github/workflows/qmodem.yml` and `tools/build-qmodem.sh`, published QModem r2; compare tests before closing PR.
  - `feat/owner-badges-optional-dashboard` (ahead 6): per-page badge UI/CSS. `main` already has badge handling; check whether any toggle UX/locale/CSS differences remain.
  - `feat/ssh-installer` (ahead 86; open PR #6): standalone SSH install, tests, docs and various UI changes. `main` has `medvemodem-install.sh` and build artifacts, but `tests/test-ssh-installer.sh` is absent; identify whether tests carry reusable coverage before closing.
  - `fix/native-qmi-mbim-selector` (ahead 1): LuCI option availability. Current `main` has a newer hardware-detected native QMI/MBIM selector; reconcile the isolated fix before pruning.
  - `fix/r6-luci-browser-ci` (ahead 1; open PR #11): build workflow modifications. Current r8 includes passing Chromium smoke; avoid merging older CI rules.
  - `fix/r6-update-install-locks` (ahead 2; open PR #10): `pkg-install` / `self-update` lock logic; both current files include locking, compare race/exit-path cases.
  - `fix/self-update-release-check` (ahead 3; open PR #2): updater latest-tag logic and LuCI UI. Verify current r8 updater before deciding.
  - `release/0.16.0-alpha1-r2` (ahead 19; open PR #3): old version/changelog/UI. **Never merge the stale r2 package versions into r8/main**; review any independent logic as individual commits only.
- **Remaining action:** audit these eight for *functional* differences, close superseded PRs with rationale, and delete their branches only after confirming unique substantive fixes are ported or deliberately rejected. Do not auto-merge obsolete release branches.


## Eight-branch reconciliation progress — 2026-11-19

- Closed obsolete PRs [#1](https://github.com/Medvedolog/medvemodem/pull/1) and [#11](https://github.com/Medvedolog/medvemodem/pull/11), without merge: QModem persistent workflow/build script and LuCI Chromium coverage have newer versions in main.
- Found that the existing SSH installer owns a shared `package-operation.lock`, while current `pkg-install` and `self-update` lacked the corresponding inter-process lock. Ported shared locking to main for component installs, self-update and release add-ons, with exit cleanup and PID-owner checks. Closed obsolete PR [#10](https://github.com/Medvedolog/medvemodem/pull/10) without merging r6 files.
- Added `tests/test-package-locks.sh` (shell syntax, cross-entrypoint exclusion, fresh-ownerless-lock race). **The new test has not yet been run in GitHub Actions or on a router; do not mark the fix accepted until it passes.**
- Release update channel in `self-update`: default is now `release`; `bench` accepts only `bench-v*` tags, not public `v*` tags. Existing explicit `bench` preferences remain usable. PR #2 remains open because some old LuCI display enhancements have not yet been reconciled.
- Current native QMI/MBIM selector recognizes a discovered `qmi_wwan`/`cdc_mbim` control port and has post-r8 owner-handover monitoring, superseding the one-commit old selector. Physical EP06 owner acceptance remains outstanding.
- The `feat/ssh-installer` branch carries an older 185-line installer test and branch-triggered CI workflow absent from main; they need adaptation rather than wholesale copying because release versions and behavior have changed. PR #6 remains open.
- Owner-badges branch and old r2 branch still need individual UI/feature reconciliation. No new build/release was requested or triggered, and **none of the eight retained branches has been deleted**. Their commit histories remain available pending final source and test acceptance.


## SSH installer confirmation UX — 2026-11-19

- Updated `medvemodem-install.sh`: an already installed Telegram bot (either bot package present) is automatically included in the engine/LuCI upgrade set. The optional bot prompt is now reserved for first-time installation; `--bot` / `--no-bot` remain explicit overrides.
- Removed the separate package-install YES and the later service-disruption YES. After SHA256 verification and package-manager dry-run, one final confirmation names the selected package set and, when detectable, the running services that will be stopped and the possibility of losing SSH/mobile connectivity.
- Service shutdown begins only after that confirmation. Noninteractive operation still requires `--allow-disconnect`; first-time optional bot selection defaults to no when no TTY is present.
- Repeat runs are idempotent: when every selected package already has the version represented by the selected release tag, the installer exits successfully before downloading the archive or opening an install confirmation. `--force` / `--reinstall` deliberately bypasses this no-op check. APK versions are read from `/lib/apk/db/installed`; opkg versions from package status.
- Added regression assertions to `tests/test-installer-release-selection.sh` for automatic bot carry-forward, the single final confirmation, warning text and dry-run/confirm/stop ordering.
- This changes the raw `main` SSH installer used by future runs; it does not modify the already published r8 package archive. No SDK build or release was triggered. GitHub Actions execution of the updated test has not yet been observed.


## R9 source changes — updater and settings UX (2026-11-19)

- Fixed the LuCI updater's version model: engine, engine LuCI, Telegram bot and bot LuCI are compared independently against the selected release. A stale bot package now offers an update even when the engine is current. The updater selects the engine/LuCI or bot/bot-LuCI pair independently and installs only the outdated family.
- Added a persistent UCI `core.update_channel` setting with `release` as the default and explicit `bench` opt-in. LuCI offers the choice through the common Save/Revert form. Check cache entries now include their channel; an old-channel result is not presented as current. Error checks are timestamped and stale/channel-mismatched results are retried.
- Updater status now lists all four installed package versions, the available release, channel and check time; known no-build errors are translated into Russian.
- Moved the MM/QM/MV badge preference to the bottom of Diagnostics and components. It now participates in the common Save dirty state; localStorage is updated only after Save succeeds. It remains a browser-local preference and does not change modem ownership or UCI engine behavior.
- Bumped root engine and all three dependent package Makefiles to r9; shared LuCI and all three page view versions are aligned to r9. Added Russian PO/POT entries and regression assertions; the manual build workflow now runs `tests/test-bot-update-versions.sh`.
- Release highlights for r9 are staged in `docs/RELEASE-HIGHLIGHTS-ALPHA1-R9.md`. **No r9 SDK build, release publication or physical router acceptance has been performed.** Published r8 remains immutable.


## R10 source changes — modem owner badge placement (2026-11-19)

- Moved the optional MM/QM/MV owner badge to the lower-right corner of dashboard modem cards and the individual modem hero card. Removed it from the dashboard top icon row, overview table and modem title row.
- Removed the HiLink/WEB owner badge. HiLink's web-interface link remains visible and actionable; ownership chips now represent only ModemManager, QModem and MedveModem.
- Preserved the browser-local preference, off-by-default behavior and common Save/Revert integration.
- Bumped root engine, main LuCI, bot and bot LuCI package release counters to r10; bumped common.js and dashboard/modem/settings view versions to 0.16.0-alpha1-r10.
- Added source-level regression assertions for the bottom-right placement and absence of the duplicate WEB owner badge. Release highlights are in `docs/RELEASE-HIGHLIGHTS-ALPHA1-R10.md`.
- **Source only:** no r10 SDK build, release publication or physical router acceptance has been performed. Published release assets are unchanged.
