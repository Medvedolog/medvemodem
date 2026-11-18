# MedveModem: installation over SSH

The interactive installer is [`medvemodem-install.sh`](../medvemodem-install.sh), modeled after BearGuard's standalone SSH installer.

Supported: **OpenWrt 24.10.x (opkg/IPK)** and **25.12.x (apk/APK)** on `aarch64_cortex-a53`, `aarch64_generic`, `x86_64`. Published archives currently use SDK patches 24.10.8 and 25.12.6. A different firmware patch level is reported explicitly; dependencies are never forced.

## Install from SSH

Download the script, inspect it, and execute it interactively:

```sh
wget -O /tmp/medvemodem-install.sh https://raw.githubusercontent.com/Medvedolog/medvemodem/main/medvemodem-install.sh
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

The default release tag is `v0.16.0-alpha1-r5`. Change it with `--tag` once a newer release is available; this script **does not** use the GitHub API `latest` endpoint because prereleases may not appear there.

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
