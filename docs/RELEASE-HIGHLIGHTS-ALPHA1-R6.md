# MedveModem 0.16.0 Alpha 1 r6 — Release Highlights

## 0.16.0-alpha1-r6
- Fix the individual modem LuCI page reporting page r1 while its shared module runs r5. Enforce version consistency for all three LuCI views at build time.
- Add optional MM, QM, MV and WEB owner badges on dashboard cards, fleet table and individual modem pages. Enable them in Settings → Modems; disabled by default.
- Translate 20 missing Russian labels and descriptions on the Whitelists page, with a build-time Russian catalog completeness check.
- Fix Telegram bot update notices to recognize OpenWrt package versions with underscores, such as 0.16.0_alpha1-r5.
- Show English release highlights in the SSH installer; keep engine, LuCI, bot and bot LuCI package revisions synchronized.
- Keep eSIM operations unavailable when QModem owns a modem by design; native-owner and supported safe ModemManager transports remain eligible.
- Alpha release: verify connectivity and modem continuity after upgrading. QModem is a separately managed package and is not updated by MedveModem.

Release: https://github.com/Medvedolog/medvemodem/releases/tag/v0.16.0-alpha1-r6
