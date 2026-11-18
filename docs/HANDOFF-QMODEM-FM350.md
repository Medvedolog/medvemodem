# HANDOFF: QModem upstream integration, FM350 recovery and owner-mediated AT telemetry

Date: 2026-11-18. Status: confirmed field diagnosis + proposed work, **not implemented or validated**. Base: MedveModem `main` 41ca3c9 and branch `fix/qmodem-owner-failover-alerts` (10 commits ahead of main before this document). Hardware: OPiZ3/OpenWrt 25.12.5, Fibocom FM350-GL (MediaTek/RNDIS), QModem `6_1_2`, interface `mm7` / `eth3`, QModem AT port `/dev/ttyUSB3`.

**Rules:** Keep MedveModem's existing fixes and local QModem patches. Do not bulk merge upstream branches. No CI builds, release publication, `latest` changes, main merges, or router mutation until explicitly authorized. Distinguish code verified in repository from hypotheses and physical tests.

## 1. Field evidence and outstanding failure

1. Before correction `qmodem.6_1_2.metric=11` while `network.mm7.metric=110`; when QModem took FM350, the default route changed from `wwan0` to `eth3` at 13:49:08. MedveModem restored the intended order at 14:03:08. Manually setting and committing QModem metric 110 gave kernel defaults `wwan0 100, eth3 110, usb0 120, wwan1 130, eth1 140, eth2 150`. Nevertheless `ifstatus mm7` still reported `metric: 11`. Validate netifd live state separately from UCI and kernel routes; do not assume UCI commit alone updates it.
2. `config-reload qmodem-dial 6_1_2 0` hung/was interrupted. UCI `enable_dial=0` persisted while `ifstatus mm7` still said up; dial shells PID 16079 (PPID 1) and PID 16080 (child) survived, and procd `qmodem_network` had triggers but no modem instance. Earlier log: procd sent SIGKILL to `qmodem_network::modem_6_1_2` PID 22845 after SIGTERM timeout. These data indicate lifecycle desynchronization, not necessarily an OOM/crash.
3. QModem `_hang` grabs `event.lock` with unbounded `flock -x 9`, calls `procd_kill`, then synchronously invokes `modem_dial.sh <section> hang`. The event lock was available in a later nonblocking test. `dial.lock` (inode 32416) was busy; `/proc/locks` named PID 23970, absent from `/proc`, while dial shells had FD 7 open on that lock path. Inherited open-file-description locking is plausible but **not proven**. Another lock inode 32384 was unrelated to these two lockfiles. Do not delete lockfiles or kill arbitrary PIDs to 'fix' this.
4. `tom_modem` is a legitimate helper under actual QModem ownership: suppress *third-party conflict* warnings, **not** the genuine busy-port status. Never directly open the AT port concurrently.
5. LuCI's exact string `The operator's data is unavailable` is at `packages/luci-app-medvemodem/htdocs/luci-static/resources/view/medvemodem/dashboard.js` around line 511, selected when `mmFresh` is false. It does **not** establish loss of registration. Root cause and source freshness need owner-aware correction.

## 2. Upstream audit (FUjr/QModem; checked 2026-11-18)

Repositories/refs: https://github.com/FUjr/QModem ; examined main, stable, delivery/m60k63-20261105, 26 listed branches, issue/PR and commit search. This is **candidate triage**, not certification of all commits in every branch. Inventory the other branches using compare/changed-files before selecting patches.

- `main` `application/qmodem/files/etc/init.d/qmodem_network` now uses `hang "$1" || return $?` in `reconfigure`; `stable` and the tested router version ignore the return code. Backport error propagation. All reviewed variants still use unbounded event-lock wait and synchronous hang; none proved to solve the orphaned dial process.
- PR https://github.com/FUjr/QModem/pull/288 (open, not merged): device presence proof, prevent stale slots from dialing, host-only `offline` cleanup, bounded retry and lifecycle tests. Evaluate/test as a selective candidate, especially USB generation handling; do not blindly pull its network/scanner changes.
- PR https://github.com/FUjr/QModem/pull/188: Quectel-CM process ownership by PID files; transfer the **design principle**, not necessarily unrelated Quectel code.
- Issues https://github.com/FUjr/QModem/issues/169 (FM350 DNS/APN/operator, `AT+GTCURCAR?` fallback), https://github.com/FUjr/QModem/issues/179 (FM350 polling/slot/IPv6), https://github.com/FUjr/QModem/issues/198 (FM350 disconnects), https://github.com/FUjr/QModem/issues/280 (slot duplication), and PR https://github.com/FUjr/QModem/pull/285. Check status and applicability against the exact FM350 revision.
- Commit https://github.com/FUjr/QModem/commit/b905654d53dc7f16294055cf1f3635162538114b replaces `fastat` with `at` for ECM hang, **not a proven fix for our RNDIS case**.
- Commit `cc9efb7` adjusts MediaTek/Fibocom GTACT band-lock fields; not relevant to a dial-lock fix, but review separately for our FM350 band support.
- Search across `main`, `stable`, `delivery/*`, `fix/*`, `feat/*`, `feature/*`, `beta/*`, `release/*` and open/closed PRs for runtime fixes. Track each candidate with source SHA, files, behavior, status (already incorporated / cherry-pick / adapt / reject), compatibility, test and rationale. Deduplicate by patch-id/behavior and preserve local modifications, license and attribution.
- Follow up on our QModem persistent-release workflows and pinned source SHA; never silently shift auto-install to a changing upstream head.

## 3. Implement separately: QModem lifecycle, MedveModem supervision, route metrics

**QModem patch set:**
- Determine actual lock ownership and blocking stage with bounded instrumentation before altering semantics. Avoid PID-only assumptions and cross-instance impact. Add bounded waits to `event.lock` and to hang/cleanup, differentiated timeout errors, cleanup that cannot wait on its own inherited `dial.lock`, and SIGTERM propagation to descendants. Keep locks protected against races; never unlink a held lock as an 'unlock'.
- Verify `procd` instance registration, removal and orphan detection for exactly one QModem section. Avoid restarting all `qmodem_network` or unrelated modems. A timeout should report failed/unknown, not falsely report disconnected. Compare targeted lifecycle design with PR #288.
- Add isolated BusyBox ash tests for parent/child lock FD inheritance, long-running `hang`, SIGTERM ignored, missing procd instance, enable=0 with live dial, USB reenumeration, concurrent reload and reconfigure. Preserve idempotence.

**MedveModem patch set:**
- Existing working branch `fix/qmodem-owner-failover-alerts` includes `qm_inherit_metric`, filtering QModem-owned `tom_modem`, and changing `crashes` label to `unclean restarts`. Inspect its 10 commits, retain all previous operator requirements, and **do not equate renaming with diagnosing SIGKILL**.
- Synchronize priorities **before QModem's first dial and each re-create/USB generation**, confirm actual netifd/kernel route (not only UCI); do not overwrite explicit operator priority indiscriminately. No transient `eth3 metric 11` while `wwan0 metric 100`. Implement reconciliation safely without uncontrolled network reload.
- Enforce bounded, asynchronous, per-section QModem operations outside the main daemon uloop; track states queued/running/timeout/failed/verified, and never claim success until observing state. On timeout leave unknown and do not aggressively redial. Detect orphaned dial scripts via process identity + section and procd, but do not auto-kill without a separately designed safe recovery path.
- Use resolved active ownership when suppressing `tom_modem` third-party warnings; keep AT lock/in-use information visible. Preserve other helper-conflict alerts.

## 4. Mandatory: telemetry over owner-mediated QModem AT interface

**Goal:** Under owner `qmodem`, read live FM350 registration, operator, cell, RSRP/RSRQ/SINR, CA, serving RAT/bands, SIM state, firmware and link without MedveModem opening `/dev/ttyUSB3` itself.

Upstream documented APIs (verify availability and JSON schema in pinned installed build):
- https://github.com/FUjr/QModem/blob/main/docs/qmodem-rpcd-interface.md — `ubus call qmodem base_info '{"config_section":"6_1_2"}'`, `network_info`, `cell_info`, `sim_info`, `info`, `get_current_band`, and `send_at` with `params.at` and `params.port`.
- https://github.com/FUjr/QModem/blob/main/docs/rpcd-at-daemon-userguide.md — `ubus call at-daemon sendat '{"at_port":"/dev/ttyUSB3","at_cmd":"AT+CSQ","timeout":5}'`. **Do not invoke from MedveModem merely because it exists:** this is a raw port-facing API, not proven ownership/arbitration, and could race with `tom_modem`.
- Preferred source is QModem's structured/read-only cached `cell_info`, `network_info`, `base_info` through its own RPC, retaining section-ID mapping; for missing fields use a **QModem-coordinated** read-only AT proxy or explicitly serialized `send_at` only after auditing that implementation's locking/ownership and per-port concurrency. Do not assume `send_at` alone is safe; avoid injecting arbitrary AT from periodic polling.
- FM350 `AT+GTCURCAR?` is reported by upstream issue #169 as a potential operator-name source. First test via owner's vendor implementation or safe serialized proxy; fallback `+COPS?`/PLMN code only where documented and safe. Avoid any sensitive IMSI/ICCID/IMEI in logs. Do not change PDP/APN/CFUN or band locks to learn telemetry.
- A dedicated per-modem adapter should map QModem's `config_section` to MedveModem's stable `device_id` and USB generation. Fetch bounded, concurrent-deduplicated snapshots asynchronously, with separate `source=qmodem`, `observed_at`, `age_s`, `fresh/stale/busy/timeout`, and `last_success`. Never treat stale cache or missing MM fields as radio-off; no synchronous ubus/AT calls on the main daemon loop. Prefer vendor-specific QModem parser for AT output, not generic heuristic strings.
- Overlay from the active owner into normalized `device` representation and LuCI dashboard/card. In particular, fix `mmFresh` gate so QModem-owned modems do not show `The operator's data is unavailable` when fresh QModem readings exist. If unavailable, display 'QModem reading unavailable' with source/age/reason; if cached, label it stale. Ensure no direct AT when QModem owns the port.
- **Read-only router capability probe (optional, safe for current state):**
  ```sh
  ubus -v list qmodem
  ubus -v list at-daemon
  ubus -t 5 call qmodem cell_info '{"config_section":"6_1_2"}'
  ```
  The cell_info call may itself initiate hardware reads; only run manually on healthy equipment, not automatically or at high frequency while the current dial lifecycle is wedged. Do not probe `send_at` yet.

## 5. Acceptance gates and publication

- Test with six interfaces: main `wwan0 metric 100`, FM350 backup `eth3 metric 110`; no route takeover on handover, redial, targeted recovery and USB reset. Verify `ifstatus mm7` and kernel route reflect the same metric, not only UCI.
- Targeted dial off/on is bounded, idempotent and verifiably releases/starts the correct instance; no leaked shell descendants/locks; other data links and LAN not disrupted. Simulate held event/dial lock and missed SIGTERM.
- Read all relevant QModem telemetry with the AT port owned by `tom_modem` and no 'foreign program' warning; no second direct port opener. Tested operator-name fallback, legitimate unavailable/stale transitions, correctness of readings and LuCI localization.
- Run static checks, host tests, comparison against upstream and on-device FM350 test **before** CI builds or release. For each upstream candidate record source SHA, rationale and regression coverage; retain our QModem pinned packaging/release/install path.
- Merge only reviewed targeted changes to a development branch. Do not touch `main`, release artifacts or auto-updater until approval.

## 6. Implementation progress (development branch, 2026-11-18)

- `e32d093`: LuCI dashboard evaluates `present` before `readingPending`, gates the global ModemManager freshness by actual ModemManager ownership, and distinguishes unavailable QModem telemetry from a confirmed operator registration failure. **Not yet built or tested on hardware.**
- `27ac9a8`: MedveModem `config-reload` bounds a QModem reconfigure/reload *caller* to 20s (TERM, then KILL after 3s), propagates the actual exit code instead of always exiting zero and logs failure. **Does not prove orphaned grandchildren are gone and does not cure QModem's internal lock ownership.** Must check availability/behavior of `timeout` on both supported BusyBox versions and add process-group regression tests before release.
- Existing FM350 metrics integration was discovered during implementation: `metrics_launch` already calls QModem's `cell_info` via its controlled service path, adds QModem `send_at` for `AT+GTCAINFO?` on Fibocom MTK, and `signal_history_read` schedules readings every minute. `dashboard.js::withReading` already requests `getMetrics` every 120s with a cached 10-minute fallback. **Do not build a second reader:** audit actual QModem `send_at` locking and shape, then correct the existing adapter, provenance and UI flow.
- Upstream QModem runtime source remains pinned at `QMODEM_COMMIT=3196ab787e06f93ef343d0beceadd9758e78ef8b` in Makefile. **No QModem source backports/cherry-picks applied yet**. Before adding them, introduce a tracked overlay/patch manifest and a reproducible application to the pinned source (or an explicitly maintained source fork). Keep source SHA and patch provenance in the release manifest.
- Next critical implementation: select and test minimal upstream `hang` error-propagation and process-lifecycle changes; reproduce both lock states in a harness; enforce true target-specific termination and exact completion verification. Then integrate the FM350 operator-name fallback (issue #169) only after owner-side AT serialization is confirmed. Avoid global QModem reload on UCI commits where practical.



### First pinned-source QModem backport staged (2026-11-18)

- `patches/qmodem/0001-bound-stop-and-propagate-errors.patch` (latest patch commit `5fa8f9a`) applies to the exact pinned FUjr/QModem source `3196ab787e06f93ef343d0beceadd9758e78ef8b`: error propagation from `redial` and `reconfigure`, bounded `event.lock` acquisition (8 seconds, `timeout` TERM then KILL), immediate stop on lock timeout before `procd_kill` or `hang`. Its original/context lines were matched byte-for-byte against the pinned source, and old/new unified hunk counts were validated. **Not tested with `git apply` in a real checkout or on OpenWrt** (network access unavailable in the validation container), and not a complete fix for inherited `dial.lock` or orphaned children.
- `tools/build-qmodem.sh` (`44b844c`) mounts `patches/qmodem` read-only and uses `git apply --check` then `git apply` after pin checkout, before package setup. Builds will fail closed if an overlay becomes incompatible. No build was executed.
- `dashboard.js` additional correction `85bc141`: reads without data distinguish *pending* versus *failed* and display the QModem-specific unavailable wording. Note that full QModem telemetry verification and normalization remain pending.
- After these changes, operator-name fallback, QModem locking/child lifecycle correctness, procd verification and deterministic host/device tests remain **open**. No release should be tagged from these commits without validation.

## eSIM ownership policy — final decision, 2026-11-18

**Scope:** eSIM/EUICC operations are allowed when **MedveModem natively owns the required transport** or when **ModemManager explicitly permits a supported, safely shared transport** (such as MBIM through the correct proxy under existing owner-aware rules). **QModem-owned devices are intentionally excluded:** `src/esim.c:mm_esim_target()` must retain `owner_busy` for `owner.control=qmodem`. Do **not** create a QModem-mediated CCHO/CGLA/CCHC bridge, open QModem/tom_modem tty ports directly, forcibly claim the port, or stop QModem merely to read a profile.

This decision supersedes the earlier proposal for r6. Issue [#7](https://github.com/Medvedolog/medvemodem/issues/7) was closed as **not planned**. The Fildunsky bridge investigation is retained as a technical reference, not an implementation requirement. r5's operator/CA telemetry via QModem does not change eSIM eligibility. Maintain clear eSIM UI explanations of allowed ModemManager access and unavailable QModem access. No unsupported promises in the README or release notes.
