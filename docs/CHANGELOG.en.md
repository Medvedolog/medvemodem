# MedveModem — Release History (English)

[Original complete Russian changelog](CHANGELOG.md) · [Documentation](README.md) · [GitHub Releases](https://github.com/Medvedolog/medvemodem/releases)

> [!IMPORTANT]
> **English translation in progress.** This page currently covers the public alpha and source revisions **r197–r195**. The original [Russian CHANGELOG.md](CHANGELOG.md) contains the complete historical record, including all older revisions. This partial English translation must not be used as a replacement for the original release history.

## 0.16.0-alpha1-r1 — 2026-11-16

Public Alpha 1, published from the `0.15.0-r197` source snapshot without functional changes. The package release counter `r1` belongs to the renamed package version; `r197` remains the source baseline in the historical log.

CI builds in `.github/workflows/build.yml` require independent verification. The packaging issue in the first published commit was corrected subsequently by switching OpenWrt APK package versions to `0.16.0_alpha1-r1`; this is a packaging correction rather than a feature change.

## 0.15.0-r197 — 2026-11-16

Source baseline labeled “bench build: pending” in the original changelog.

- **All SMS on the Modems homepage:** an initially collapsed section in Detailed mode combines messages from all modems, newest first, with filtering by phone number, body and modem. When collapsed it makes no queries; when opened it reads cached daemon data without marking messages seen.
- **Modem SMS search:** search conversations by number, address-book contact or message text.
- **Clickable URLs in SMS:** show the full address and open it in a separate browser tab.
- **Operator-originated messages:** distinguish alphanumeric senders belonging to MegaFon, Beeline, MTS, T2, Yota and others, in both combined and conversation views.
- **Signal for modems without ModemManager:** the bot now reports signal percentages, radio network and operator for devices such as FM350 controlled by MedveModem and HiLink/E3372.
- **Connection wording:** bot status uses consistent neutral wording for established/not-established connections.
- **Primary modem unplug notifications:** announce a primary uplink change even if there is a moment with no active primary between the old and new channels. A just-unplugged primary is identified as primary rather than backup.
- **24-hour statistics in `/status`:** include primary-channel switch count and USB modem-disconnect count. Earlier, a day with physically unplugged modems could appear to have zero failures.
- **Speed test timeout:** correct background launch to avoid a LuCI `XHR request timed out` failure caused by the previous synchronous request's ~20-second limit.
- **eSIM profile switching:** refresh SIM/operator information immediately after the change instead of showing the former profile for up to half an hour (observed on YESIM, with misleading Beeline/T2 operator labels). Add “eSIM profile switched” to modem history.
- **FM350 control ownership:** failed connection no longer causes repeated handoff to an unsupported ModemManager and back to native MedveModem on every recovery.
- **User-deactivated status:** clear the mark once a connection is successfully re-established; do not transfer the mark to a different modem on the same USB port.
- **eSIM diagnostics:** move Check eSIM into a persistent Diagnostics card with an Open eSIM tab action; the previous Settings button disappeared after testing.
- **Bot connection diagnostics:** display the modem's model if it has no custom name; keep path-testing table rows, column widths and expanded detail stable during checks.
- **Whitelist → Service checks:** identify links with readable modem names, such as `FM350-GL (mm3 · eth4)`.
- **Modem home table:** use a single traffic-light status per row, show problems below the main status rather than in a mostly empty extra column, and avoid horizontal overflow.
- **Whitelist → Current:** show primary channel first, followed by backup 1, backup 2 and so on, with a separate Status column.
- **Copy and terminology:** rename “Fix” to “Edit” for local proxies; clarify the **Allow sending SMS and USSD from Telegram** permission and help text.

## 0.15.0-r196 — 2026-11-15

Bench build: `bench-v0.15.0-r196-31f1329`. Fixes from an external review of the C service, without new user features.

- **Router-side DNS checks:** correctly pass link latency to the test helper. Responses arriving faster than the expected link delay can now be identified as router-local rather than operator-originated.
- **Identity across USB ports:** merge old modem settings into the new record and remove the stale record when the same modem is moved.
- **Downtime accounting:** a router powered off overnight is not counted as Internet downtime; outages spanning midnight are counted from the first measurement on the new day.
- **SMS forwarding:** avoid a leftover byte sequence after truncating multi-byte message text. Remove spurious trailing `@` from alphanumeric sender names (e.g., `MegaFon`).
- **ZTE SMS:** receive incoming SIM-stored messages even when sent messages are also present in modem storage.
- **Concatenated SMS:** repeated segment numbers from the same sender no longer incorrectly merge unrelated long messages read by another modem manager.
- **Diagnostic redaction:** conceal username and password embedded in a webhook URL.
- **USB controller hangs:** prevent the hung controller/port from permanently blocking the daemon's connection to the system; LuCI becomes available after service restart. Stuck DNS/firewall probes are limited to five minutes.
- **Simultaneous subnet conflicts:** recover both modems when two affected devices reconnect at the same time.
- **Minor:** support half-hour time zones, kernel fault messages that include a CPU number, and Delete/PgUp/PgDn keys in `medvemodem top`.

## 0.15.0-r195 — 2026-11-15

Bench build: `bench-v0.15.0-r195-99878f8`.

- **Terminology and localization:** two rounds of text editing across bot, UI and installers. Standardize phrases for router, MedveModem service, USSD session, Mixed Port, authorization, interference descriptions, uptime, tunnel servers, form labels, re-registration action, SIM quota navigation and installer logs.
- **Sierra signal:** fix RSRP reporting for EM7565 and EM9291 with per-antenna measurements; show 5G NSA carrier separately from LTE on EM9190.
- **Temperature support:** add Fibocom FM150/FM160 and L850/L860, Telit FN990 and MeiG families. Sierra temperature may appear next to cell metrics without an additional query.

---

**Older source revisions:** [Complete Russian history](CHANGELOG.md). Field-specific evidence and modem zoo material remain untranslated by design.
