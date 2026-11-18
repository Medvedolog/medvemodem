# MedveModem: installation over SSH

The interactive installer is [`medvemodem-install.sh`](../medvemodem-install.sh), modeled after BearGuard's standalone SSH installer.

Supported: **OpenWrt 24.10.x (opkg/IPK)** and **25.12.x (apk/APK)** on `aarch64_cortex-a53`, `aarch64_generic`, `x86_64`. Published archives currently use SDK patches 24.10.8 and 25.12.6. A different firmware patch level is reported explicitly; dependencies are never forced.

## Install from SSH

Download the script, inspect it, and execute it interactively:

```sh
wget -O /tmp/medvemodem-install.sh https://raw.githubusercontent.com/Medvedolog/medvemodem/feat/ssh-installer/medvemodem-install.sh
sh /tmp/medvemodem-install.sh
```

The menu offers install/update, force reinstall, full removal, or bot-only removal.

Noninteractive examples:

```sh
sh /tmp/medvemodem-install.sh --install --no-bot
sh /tmp/medvemodem-install.sh --update --bot
sh /tmp/medvemodem-install.sh --reinstall --no-bot
sh /tmp/medvemodem-install.sh --install --proxy http://192.168.1.1:7890
sh /tmp/medvemodem-install.sh --remove-bot
sh /tmp/medvemodem-install.sh --remove
```

**While this remains a draft PR, use the branch URL above; do not advertise the `main` one-liner.** The default release tag is `v0.16.0-alpha1-r5`. Change it with `--tag` once a newer release is available; this script **does not** use the GitHub API `latest` endpoint because prereleases may not appear there.

## Network behavior

1. Probe the release's `SHA256SUMS` on GitHub directly.
2. Probe `opkg update` or `apk update` directly.
3. If either fails, ask for an HTTP(S) proxy URL (Clash/mihomo **mixed port** speaks HTTP CONNECT). Optionally prompt for username/password with terminal echo disabled.
4. Temporarily pass `http_proxy`/`https_proxy` to downloads and the package manager. No UCI, feed config, resolver, routing or firewall settings are changed.
5. Download the architecture+SDK archive, verify SHA256, check archive member paths/types and run the package manager's dependency dry-run. Only then install the selected packages.

GitHub or official feeds must be reachable directly or through the proxy. An unavailable dependency stops installation; do not use `--force-depends`, `--force-checksum`, `--force-overwrite`, or disable TLS verification.

**Security:** The script is run as root; inspect the downloaded file and verify its source. Proxy credentials are held only in process environment, not saved to disk or UCI. Root can inspect another root process's environment while the installer runs; avoid shared/compromised routers. Credentials containing URL special characters should be percent-encoded in the proxy URL.

## Package changes

The core install includes `medvemodem` and `luci-app-medvemodem`. It asks separately whether to install `medvemodem-bot` and `luci-app-medvemodem-bot`. OpenWrt feeds resolve the remaining dependencies. The independent `qmodem` component is managed separately through MedveModem LuCI.

`--reinstall` and `--force` mean **same-version package reinstall** only; they never bypass dependencies, cryptographic SHA256 checks or architecture restrictions. `opkg --force-reinstall` and OpenWrt 25.12's `apk add --force-reinstall` are used.

`--remove-bot` removes the bot and its LuCI, keeping the engine; `--remove` removes both plus the engine and LuCI. The script does not explicitly purge UCI configuration or secrets. Package-specific hooks and package-manager conffile policy still apply. QModem is **not** removed automatically.

## Tests

```sh
sh -n medvemodem-install.sh
sh tests/test-ssh-installer.sh
```

Tests are static. Before claiming a production-ready installer, verify actual transactions on OpenWrt 24.10.8 and 25.12.6, both with and without an authenticated proxy, plus the failure cases for missing dependencies and bad checksums. Do not test a disruptive uninstall over the modem link that provides SSH connectivity.

## Field finding and release gating — 2026-11-18

**Second OPiZ3 field finding:** The published r5 archive passed SHA256 and official `apk update` succeeded on 25.12.5, but the installer falsely reported `duplicate medvemodem package` because the APK selector matched the optional bot. Branch fix restricts package filenames to exact name followed by a numeric version. The new release-preview screen shows r5 English highlights before the Telegram bot choice, and a final explicit confirmation follows dependency dry-run. The highlights live in `docs/RELEASE-HIGHLIGHTS-ALPHA1-R5.md`, with a dedicated release metadata workflow. **No successful apk installation has yet been demonstrated.**



- First real OPiZ3 trial: OpenWrt **25.12.5**, package architecture `aarch64_cortex-a53`, package manager `apk`. The script correctly identified the OpenWrt line and warned that published packages target the **25.12.6 SDK**.
- The first `--install` attempt failed before package installation: GitHub returned `curl (22)` / **HTTP 404** for `v0.16.0-alpha1-r5/SHA256SUMS` because MedveModem r5 was not yet published. The script incorrectly treated the missing release as a network outage and asked for a proxy. The user did not report a successful installation.
- A subsequent branch fix (`e50f502`) checks the GitHub API and rejects a missing release instead of automatically requesting an unrelated proxy. **This fix still requires a repeat run on OpenWrt**.
- The same CI history shows all six MedveModem r5 SDK builds succeeded, but its initial publish job failed due to missing `actions/checkout`. The release workflow was fixed in `main` (`e53e83d`); QModem r2 did publish. Those are release facts, not installer acceptance tests.

### Acceptance checklist before README / published installation instructions

- [ ] Fresh OpenWrt 24.10.x: IPK, feed refresh, dependencies and installation; verify engine + LuCI.
- [ ] OpenWrt 25.12.5 / 25.12.6: APK with firmware/SDK patch-level mismatch handled safely, all exact dependencies resolvable.
- [ ] Direct GitHub success, real HTTP 404 rejection and direct network failure are differentiated.
- [ ] OpenWrt repository inaccessible directly: temporary HTTP/mixed proxy supports `apk update` / `opkg update` and then restores the operator's shell/network configuration.
- [ ] Proxy authentication works without leaking credentials to logs; test failures and percent-encoded special characters.
- [ ] SHA256 mismatch, invalid archive, missing package and missing feed dependency fail before modifying packages.
- [ ] Normal install, newer-version upgrade, same-version forced reinstall (`--force` / `--reinstall`).
- [ ] Separate opt-in for Telegram bot + LuCI, and unattended `--bot` / `--no-bot`.
- [ ] Remove bot-only and full stack, preserving configuration where supported; QModem stays installed.
- [ ] Run `sh -n medvemodem-install.sh` and `sh tests/test-ssh-installer.sh` on a host, then test with a backup management path on a router.
- [ ] Only after the above: merge PR, add a stable one-liner to `README.md`, `docs/INSTALL.md` and `docs/INSTALL.en.md`.

**Do not run full removal over a management link provided by MedveModem.** The installer and its `--reinstall` path remain alpha until the checklist is completed.
