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
