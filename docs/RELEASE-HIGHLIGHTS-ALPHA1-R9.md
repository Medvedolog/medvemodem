# MedveModem 0.16.0 Alpha 1 r9 — Release Highlights

## 0.16.0-alpha1-r9

- **Updater checks all installed packages:** compare engine, LuCI, Telegram bot and bot LuCI versions independently. An outdated bot or bot LuCI now makes an update available even when the engine is current; updating replaces every installed component as a matched release set.
- **Release channel is explicit:** published versioned releases (including tagged prereleases) are the default. LuCI exposes Published releases / Test builds (bench), saves the choice through the common Save action, scopes cached results to the selected channel and retries stale or channel-mismatched checks.
- **Clear updater status:** show the installed version of each of the four packages, selected channel, available release and last successful/failed check. Explain when a channel has no build for the router architecture or OpenWrt line.
- **Owner badges preference:** move the optional MM / QM / MV preference to the bottom of Diagnostics and components, translate it into Russian and include it in Save/Revert dirty-state handling. The choice remains local to this browser; no daemon or modem configuration is changed.
- **Version consistency:** align shared LuCI and all three page modules to r9. Add regression checks for multi-package update detection, channel-scoped cache, updater UI and Save/Apply behavior.

**Validation:** static shell and LuCI contract checks are required before the six-target SDK matrix. No release archive is published until the manual build workflow completes all six targets and verifies SHA256SUMS.
