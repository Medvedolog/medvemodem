# MedveModem — installation and upgrade

[Русский оригинал](INSTALL.md) · [Documentation index](README.md)

**Documentation baseline:** public `0.16.0-alpha1-r1`, exported from `0.15.0-r197` on 2026-11-16. This project is in alpha. Source availability does **not** prove that a matching release artifact has passed CI; consult [Releases](https://github.com/Medvedolog/medvemodem/releases) and [Actions](https://github.com/Medvedolog/medvemodem/actions).

**Before installing**, ensure you have an independent connection to the router (Ethernet, Wi-Fi or local serial console). Do not run two independent managers against the same modem.

## 1. Select the correct packages

On the router:

```sh
. /etc/openwrt_release
echo "OpenWrt: $DISTRIB_RELEASE; package architecture: $DISTRIB_ARCH"
df -h /overlay
```

Select the published archive for your exact OpenWrt version and package architecture; see the release layout below.

The daemon statically includes libubox/libubus/libuci; other dependencies (ModemManager, qmi-utils, jsonfilter, libjson-c, etc.) are installed from your firmware's package feeds.

## Release layout: required versus optional packages

## APK / OPKG package dependencies

For **all package-level runtime dependencies** (`DEPENDS` and `EXTRA_DEPENDS`), including QModem and its helpers, see the **[complete dependency matrix](DEPENDENCIES.md)**. The dependency names come from the pinned package Makefiles. They are resolved by the firmware's `apk` (OpenWrt 25.12.6) or `opkg` (24.10.8), not bundled wholesale into the GitHub Release.

| Installed component | Declared dependencies |
| --- | --- |
| `medvemodem` | `libjson-c`, `jsonfilter`, `modemmanager`, `qmi-utils` |
| `luci-app-medvemodem` | `luci-base`, exact matching `medvemodem` version |
| Telegram bot | `medvemodem`, `ucode`, `ucode-mod-{ubus,uloop,fs,uci,log,digest}`, `curl`, `ca-bundle`, `jq` |
| Bot LuCI | `luci-base`, `medvemodem-bot`, `luci-app-medvemodem` |
| lpac/eSIM | `libcurl`; optional APDU plugins require `uqmi`, `libmbim` or `libqmi` |
| Separate QModem core | USB `kmod-*` drivers, `tom_modem`, `ubus-at-daemon`, `modem_scan`, `sms-tool_q`, `jq`, `flock`, `bc`, `coreutils`, `xxd`, `usbutils` and more (full matrix linked above) |

The pinned QModem Makefile even declares `kmod-usb3` on devices with no USB 3.0 port. All `kmod-*` packages must match the router's **exact running kernel**; do not bypass missing dependencies with force flags. Run `apk update` **or** `opkg update` first, then install only your selected components and let the package manager resolve their dependencies.



Open **[GitHub Releases](https://github.com/Medvedolog/medvemodem/releases)** rather than Source code ZIPs or temporary GitHub Actions artifacts. Choose **one** MedveModem archive matching the router's `DISTRIB_ARCH` and OpenWrt version: `medvemodem-<version>-<architecture>-<24.10.8|25.12.6>.tar.gz`. The archive includes required and optional packages; **do not install all of them by default**.

| Package | Purpose | Requirement |
| --- | --- | --- |
| `medvemodem` | Daemon and CLI/TUI | **Required** |
| `luci-app-medvemodem` | LuCI frontend | **Required for LuCI**, same version as the daemon |
| `medvemodem-bot` + `luci-app-medvemodem-bot` | Telegram bot and UI | Optional; install **both together** with versions compatible with the daemon |
| `medvemodem-lpac` and relevant APDU components | Extra eSIM operations | Optional, hardware-dependent; do not install every transport |
| QModem core | Alternative modem executor | Optional and **separately installed** only when using QModem ownership; QModem LuCI is not bundled |
| ModemManager, `uqmi`/`umbim`, drivers, `qmi-utils` | Modem-specific dependencies | As required by the modem; the package manager resolves declared dependencies from the firmware feeds |

**Separate release families.** MedveModem releases (`v0.16.0-alpha1-r1`, later `v0.16.0-alpha1-r2`, etc.) provide target-specific archives plus `SHA256SUMS`. The separate permanent [QModem `qmodem-3.3.0_p20261108-r1` release](https://github.com/Medvedolog/medvemodem/releases/tag/qmodem-3.3.0_p20261108-r1) provides six different archives, one per architecture (`aarch64_cortex-a53`, `aarch64_generic`, `x86_64`) and OpenWrt line (`24.10`, `25.12`), plus its own `SHA256SUMS`. The QModem naming pattern is `qmodem-3.3.0_p20261108-<architecture>-<24.10|25.12>.tar.gz`. QModem is **not required** to run MedveModem.

**Checksums and updates.** Verify the selected archive against `SHA256SUMS` in the *same release* with `sha256sum filename.tar.gz`. The MedveModem LuCI updater checks GitHub Releases and upgrades the daemon and LuCI together; it upgrades the bot and bot LuCI **only when the bot is already installed**, never installs the bot as a surprise. The `bench` channel selects `bench-v*` releases; `release` selects `v*` tags. Select **release** for `v0.16.0-alpha1-r*`. The updater performs user-initiated checks and installs; do not assume unattended automatic upgrades. QModem installation/update is a **separate** LuCI components action using a version-pinned release URL and SHA256 verification, not arbitrary upstream latest.

## 2. Transfer and install

From your computer (replace the placeholder filename):

```sh
scp ./downloaded-archive.tar.gz root@192.168.1.1:/tmp/
```

On the router:

```sh
mkdir -p /tmp/medvemodem-install
cd /tmp/medvemodem-install
tar -xzf /tmp/downloaded-archive.tar.gz
ls -l *.apk *.ipk 2>/dev/null
```

Choose **one** command set:

```sh
# OpenWrt 25.12 / APK:
apk update
apk add --allow-untrusted ./medvemodem-0*.apk ./luci-app-medvemodem-*.apk

# OR OpenWrt 24.10 / OPKG:
opkg update
opkg install ./medvemodem_*.ipk ./luci-app-medvemodem_*.ipk
```

Do not run both blocks. Avoid installing every file in the directory with a wildcard; optional eSIM transports are packaged separately. The engine and LuCI frontend must have matching versions. Stop and save any package-manager error before retrying.

## 3. Optional components

In LuCI navigate to **Modem → MedveModem → Settings → Diagnostics and components** (exact labels depend on UI language). The drivers/ModemManager card checks what is already installed and offers assisted installation from the router's repositories: ModemManager; kernel modules such as `option`, `qmi_wwan`, `cdc_mbim`, `cdc_ncm`, `cdc_ether`, `rndis_host`; `umbim`, `uqmi`, `usb-modeswitch` and `qmi-utils`.

The interface performs a preliminary availability check. If packages are missing from the feeds, it reports the problem instead of making a partial install. It does not restart a working ModemManager automatically. QModem can be installed from the dedicated build archive or through the components panel.

The optional Telegram bot requires `medvemodem-bot` and `luci-app-medvemodem-bot` at a version compatible with the engine. eSIM supports **detection, reading EID and installed profiles, and switching among profiles already stored on the chip**. On the modem page use **Diagnostics → Check for an eSIM**, then the **eSIM** tab. Until **Alpha 2**, load new profiles onto a built-in eUICC or removable UICC/9eSIM **using a compatible phone** (the card manufacturer's app or EasyEUICC/OpenEUICC on Android), not MedveModem. Install `medvemodem-lpac` and only the required APDU transport packages. Do not blindly replace the system's lpac.

## 4. Verify and upgrade

```sh
/etc/init.d/medvemodemd status
ubus call medvemodem status
logread -e medvemodem
```

Open the MedveModem LuCI page. After a LuCI upgrade the installation scripts may restart rpcd and end the current browser session. Upgrade with a coherent, matching package set, back up `/etc/config/medvemodem`, and maintain alternate router access: modem connectivity may be interrupted temporarily.

To remove the main components, use `apk del luci-app-medvemodem medvemodem` or `opkg remove luci-app-medvemodem medvemodem`, depending on the firmware.

To disable automatic recovery:

```sh
uci set medvemodem.core.auto_recovery='0'
uci commit medvemodem
/etc/init.d/medvemodemd restart
```

## 5. Troubleshooting an undetected modem

Collect `medvemodem diag quick`, `ubus call medvemodem status`, `logread -e medvemodem`, `logread -e ModemManager` and the exact firmware version. Never publish raw logs with SIM identifiers, credentials or private numbers. For field diagnostics see the bench tools and the [modem zoo](modem-zoo/README.md).
