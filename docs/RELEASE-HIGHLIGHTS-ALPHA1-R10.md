# MedveModem 0.16.0 Alpha 1 r10 — Release Highlights

## 0.16.0-alpha1-r10

- **Owner badge placement:** move MM / QM / MV ownership badges to the lower-right corner of each modem dashboard card and the individual modem hero card. Remove the badge from the title row, top action icons and modem overview table.
- **No duplicate WEB owner badge:** remove the HiLink / WEB owner chip. The modem's existing web-interface link already identifies and opens its web UI; ownership badges now describe only ModemManager, QModem and MedveModem control.
- **Keep the preference optional:** the existing browser-local badge preference remains off by default and is saved through the common Save/Revert control.
- **Release consistency:** bump engine, LuCI, bot and bot LuCI package counters and all LuCI browser module versions to r10. Add frontend regression contracts for the lower-right placement and absence of the redundant WEB chip.

**Validation:** JavaScript syntax and source-level layout contracts are checked before the six-target SDK matrix. No release archive is published until the manual build workflow completes all six targets and verifies SHA256SUMS.
