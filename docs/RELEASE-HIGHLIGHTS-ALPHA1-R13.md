# MedveModem 0.16.0 Alpha 1 r13 — Release Highlights

## 0.16.0-alpha1-r13

- **A modem interface left without its proto or device repairs itself again:** a T99W175 whose `mm0` kept only a metric
  was brought up as it was for a day (netifd: `NO_DEVICE`). The repair of r11 ran from recovery only, and the
  configuration plan took the missing proto and device for the owner's own edit. The interface is now written again
  from the retry too (at most every 10 minutes), and those two fields are never left out as «user-removed».
- **QModem installs from the public releases again:** the public r12 was built without the QModem package address.
- **The update check asks the right repository:** releases from the public one, bench builds from the working one; a
  GitHub refusal (a private repository, the rate limit) is said as it is, not as «no build for your architecture».
- **Settings:** the «Whitelist mode is on the Whitelists page» note is a plain line, not an empty card.

**Validation:** the full host suite (a new service test removes `proto` and `device` from `mm0` and expects them back),
the bot on both ucode pins, LuCI in Chromium, package installs on OpenWrt 24.10 and 25.12 and the six-target SDK build.
