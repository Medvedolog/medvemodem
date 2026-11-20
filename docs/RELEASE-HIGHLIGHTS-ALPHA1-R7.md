# MedveModem 0.16.0 Alpha 1 r7 — Release Highlights

## 0.16.0-alpha1-r7

- Fix the blank individual modem page introduced in r6: the optional modem-owner badge now fails safely when a cached older LuCI common.js lacks its API.
- Keep modem tabs and diagnostic messages visible when the modem RPC request or JavaScript rendering fails; no more silent empty panel.
- Gate OpenWrt package builds and GitHub release publication on LuCI shared-module API/version contracts and a headless Chromium smoke test, including stale-browser-cache and failed-RPC scenarios.
- Fix false-positive "another modem manager" detection from the orphaned /etc/config/another modem manager configuration file; genuinely installed controllers and active hotplug handlers are still detected.
- Add a standalone SSH installer in the repository root. By default, it selects the newest *published* MedveModem GitHub release, including pre-releases, without selecting independent QModem releases or drafts; --tag allows an explicit version.
- Improve SSH installer safety with checksum verification, dependency simulation, serialized package operations and clear warnings about disconnecting the active mobile WAN.
- Update Russian/English changelogs, release handoff and the README one-line installation command.

- Fix installer lock ownership (write the actual shell PID, prevent concurrent install races and recover stale locks); exercise lock behaviour in CI.

**Alpha warning:** back up your router configuration and keep a wired/serial recovery path before upgrading. The installer does not silently remove other modem controllers. QModem builds remain separately managed.

Release: https://github.com/Medvedolog/medvemodem/releases/tag/v0.16.0-alpha1-r7
