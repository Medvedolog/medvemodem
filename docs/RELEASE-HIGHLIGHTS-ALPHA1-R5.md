# MedveModem 0.16.0 Alpha 1 r5 — Highlights

- FM350-GL operator telemetry via QModem owner-mediated `AT+GTCURCAR?`, with cellular-information fallback. No competing access to the AT port.
- Safer QModem handover and stop/restart: bounded timeouts, targeted orphan dial cleanup and checks of procd/dial process state.
- More accurate LuCI modem telemetry: distinguish stale/pending/failed readings and clear cached owner data after modem re-enumeration or owner change.
- Consistent r5 versions for MedveModem engine, LuCI, Telegram bot and bot LuCI.
- Independent QModem core source updated to `qmodem-3.3.0_p20261108-r2` (installed separately through LuCI).
- Six OpenWrt SDK package builds: 24.10.8 IPK and 25.12.6 APK, architectures aarch64_cortex-a53, aarch64_generic and x86_64.
- **Known limitation:** eSIM via QModem stays blocked by design; only native-owner or supported safe ModemManager access is permitted.
- **Alpha / hardware validation required:** do not uninstall or switch a modem that provides the only remote management route.

Release: https://github.com/Medvedolog/medvemodem/releases/tag/v0.16.0-alpha1-r5
