# HANDOFF — r6 LuCI blank modem page / regression-proof release QA

**Date:** 2026-11-19
**Scope:** standalone fix independent from installer PR #6, starting from released r6 main.
**Status:** PR pending merge. Published `v0.16.0-alpha1-r6` assets have not been changed.

## Incident and evidence

- On R3S-Himki / OpenWrt 25.12.6 aarch64_generic, the modem detail page was empty even though the engine returned valid devices.
- `ubus call medvemodem device` returned `EP06-E`, `health.state=ok`.
- SHA256 for router `common.js`, `dashboard.js`, `modem.js` exactly matched the files in the r6 Git tag. This rules out mismatched *on-disk* source revisions.
- After enabling visible render errors, LuCI displayed `Unable to display modem data: mm.setOwnerBadge is not a function`.
- r6 `modem.js` invokes `mm.setOwnerBadge`; r6 `common.js` exports it. The most plausible cause is an already cached old `common.js` object in the browser. This is a supported *hypothesis*, not proof of which browser cache entry was loaded.
- The first manual source-file replacement from the installer branch was obsolete (r1), an additional process failure; do not use arbitrary branch files for hotfixes. Preserve compatibility and pin exact commits or release artifacts.
- The owner confirmed the modem card reopened after the guarded badge update.

## Fix

- `modem.js`: display the layout and loading notice immediately; surface errors thrown while applying modem data; fallback when `mm.setOwnerBadge` is not a function.
- Static contracts: `tests/test-luci-frontend.py` verifies shared module version, exposed `mm.*` functions, optional badge fallback, initial loading and error display.
- Browser smoke: `tests/test-luci-browser.cjs` loads the actual LuCI modem view in headless Chromium with mocked router RPC; covers RPC failure, rendering failure and cached shared module without owner badge API. It does not cover all tabs or live modem hardware.
- `.github/workflows/build.yml`: `packages` job depends on both `luci-contracts` and `luci-browser`; tagged releases depend on packages. A failing frontend check therefore prevents package publication. Pin Chromium runner to `ubuntu-24.04` because Playwright 1.58.2 rejected `ubuntu-26.04`.

## Acceptance before merge/release

1. All new static and Chromium CI checks green on the PR, including clean compilation, RPC-error and render-error paths.
2. Confirm the PR merges cleanly into main; do not pull older LuCI files from PR #6.
3. Re-run real R3S-Himki modem detail in normal and private browser sessions with a stale common.js cache; check all tabs and the optional owner badge on/off.
4. Test available modems beyond EP06-E, including QModem, ModemManager, native QMI/MBIM, HiLink.
5. Verify APK/IPK packaging for each target and publish only after release gate passes; update release notes with the regression fix.
6. Do not claim full browser E2E coverage: router RPCs and LuCI framework are stubbed in this smoke test.
7. Keep `docs/CHANGELOG.md` and `docs/CHANGELOG.en.md` updated for any further correction; carry changes into the release handoff.

## Commands

```sh
python3 tests/test-luci-frontend.py
npm install --no-save --no-package-lock playwright@1.58.2
npx playwright install chromium
node tests/test-luci-browser.cjs
```
