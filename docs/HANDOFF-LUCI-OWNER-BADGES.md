# LuCI ownership badge and modem page version — handoff

Status: proposed next MedveModem release after r5; not included in published r5. Branch `fix/modem-view-version-owner-badges`.

## Field reports (OPiZ3, OpenWrt 25.12.5; MedveModem r5)

- LuCI event: `The page's files on the router and in the browser are of different versions (page 0.16.0-alpha1-r1, shared module 0.16.0-alpha1-r5)`.
- **Root cause confirmed in current GitHub source:** `packages/luci-app-medvemodem/htdocs/luci-static/resources/view/medvemodem/modem.js` still declares `VIEW_VERSION = '0.16.0-alpha1-r1'`. The installed shared `common.js` correctly declares `r5`. The self-check therefore raises a real mismatch; clearing browser cache cannot fix a wrong version embedded in an installed file.
- `tools/check-web-version.sh` only validated `dashboard.js` and `settings.js`, allowing `modem.js` r1 into r5. Fix is to validate **all three pages** before package build.
- User requested a compact manager badge on individual modem header, plus dashboard cards/table and a general setting, **off by default**. Previously owner was visible only in detailed diagnostics.

## Changes made on branch

- Correct `modem.js` VIEW_VERSION to `0.16.0-alpha1-r5` in source, and add the previously missing modem page to `tools/check-web-version.sh` validation. The next package revision must bump versions consistently and be rebuilt; never relabel the existing immutable r5 archive.
- Shared `mm.ownerBadgesEnabled()`, `mm.ownerBadgeInfo()`, `mm.setOwnerBadge()` map resolved `device.owner.control` to **MM / QM / MV / WEB** (ModemManager / QModem / native MedveModem / web modem). Badge is informational; does not infer ownership from another process and does not claim a conflict.
- `Settings → Modems` checkbox `Show modem management badges (MM / QM / MV)` persists `mm:owner-badges` in browser `localStorage`; default off. Applies on next rendering of dashboard/modem page without a daemon restart or UCI commit. Show on dashboard cards and table, and next to the individual modem title. CSS uses compact chips.
- No modifications to QModem, operator telemetry, IP routes or existing service management.

## Remaining release requirements

- Run `sh tools/check-web-version.sh` and JS syntax check on **all** LuCI views.
- Verify browser setting toggles badge visibility on dashboard, list and individual modem for QModem (FM350), ModemManager and native-controlled devices.
- Confirm browser reload no longer produces the false stale-files warning, including with LuCI cache from r5.
- Bump package revision for the next release and change **all** `VIEW_VERSION` / `common.VERSION` values together. Build SDK packages; do not reuse or overwrite r5 assets.
- Validate on OPiZ3. Update Russian and English changelog once accepted.
- The same field log also shows two `medvemodemd` processes and two bot processes after upgrading r3 → r5. Work on safe service quiescence/restore is tracked separately under installer PR #6. Do not indiscriminately run `killall` on a live modem router.

## Whitelist Russian localization defect and correction (2026-11-18)

- Field reproduction in a Russian LuCI session: the separate Whitelists page mixed translated content with original English labels and descriptions (`Checked channels`, `Full internet`, `All services`, `Recent access changes`, `Channels checked`, `Attention`, `Possibly limited`, `Telegram notification settings and delivery status…`, among others).
- Root cause: these source strings already used LuCI `_()`, but **20 entries were missing** from `packages/luci-app-medvemodem/po/ru/medvemodem.po` and `po/templates/medvemodem.pot`. Thus the English originals were rendered by gettext fallback; it is **not a browser cache problem**.
- Added all 20 Russian translations and template entries. Source remains English in `settings.js` per the project's internationalization policy.
- Added `tools/check-whitelist-i18n.py` and invoked it from `tools/check-web-version.sh`, preventing package builds if the whitelist page adds strings without populated Russian catalog entries. Static check only; requires CI PASS and one real Russian LuCI rendering after the next coherent package bump.
- Track PR #8, not published r5. Preserve original `po` history and use gettext compilation into LuCI `.lmo` at build time.

## r6 promotion and bot-update validation (2026-11-18)

- Release branch `release/r6` includes the full PR #8 fix, increments all four package revisions to r6 and all LuCI `VIEW_VERSION` strings to `0.16.0-alpha1-r6`. A draft PR #9 is used for six-target CI and controlled promotion to main.
- Telegram update audit identified an `updview.uc` version regex omitting `_`, although OpenWrt uses `0.16.0_alpha1-r5`. This incorrectly rendered installed versions as `—`; fixed in r6. Bot's `update.uc` reads state from `self-update`, checks twice per day (first at about 5 min after startup), and relays a notification once per available tag. The new r6 `HIGHLIGHTS.txt` uses a `## 0.16.0-alpha1-r6` heading to satisfy `self-update` parsing. Need field verification of real GitHub release detection and one notification; do not claim this is device-tested.
- Installer defaults to r6 on `feat/ssh-installer` and must not be tested for an actual upgrade until r6 release assets exist. Full quiescence tests remain pending and the installer is separately gated in PR #6.
