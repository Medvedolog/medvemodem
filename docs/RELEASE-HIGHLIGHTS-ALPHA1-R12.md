# MedveModem 0.16.0 Alpha 1 r12 — Release Highlights

## 0.16.0-alpha1-r12

- **SIM labels no longer keep the ICCID:** a SIM's number, description, SMS center and data limit are kept under a
  salted hash of its ICCID, as a modem is found by its IMEI. Existing labels are converted at the service's start and
  their ICCID removed from the config; one SIM in another modem (an `F` pad, no check digit) finds the same label.
- **A package upgrade keeps your settings:** `/etc/config/medvemodem` is now a declared config file, so opkg and apk
  no longer replace it with the default. sysupgrade also keeps your hook scripts in `/etc/hotplug.d/medvemodem/` and the
  SMS forwarding secret; the bot built from these sources keeps its token and salt.
- **QModem removed after a modem was given to it:** the modem is managed as under Auto until QModem is back, instead of
  being left without an owner.
- **Stale readings look stale:** the card dims an old reading, and a temperature the modem no longer gives reads
  "(old)", so a hung modem no longer looks alive.
- **Service robustness (audit):** background checks stop with the service; memory no longer grows with a flapping USB
  modem; descriptors no longer leak into child processes; a broken SMS time stamp is no date; qmicli runs without a shell.
- **Every push and pull request is checked** by a fast CI (contracts, installer, documentation links, Chromium) without
  the SDK matrix; release highlights are found by tag for later alphas and betas too.
- **Telegram bot architecture and screen layouts:** a new reference, `docs/BOT-ARCHITECTURE.md` (in Russian); the bot
  README describes the current setup wizard and control.
- **Documentation:** every UCI option of the service and the bot in MANUAL (a test keeps it complete), every Markdown
  link and anchor checked, DEPENDENCIES without stale revision numbers.

**Validation:** the full host suite (service, bot on both ucode pins, LuCI in Chromium), package installs on OpenWrt
24.10 and 25.12 and the six-target SDK build pass before the release. Bench check: upgrade over r11 keeps names,
priorities and SIM labels, and the config holds no ICCID afterwards.
