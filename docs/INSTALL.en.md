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

From the matching **tested build** (not a source ZIP), select the packages for the exact OpenWrt release family and `DISTRIB_ARCH`. Bench archives may be named `medvemodem-<version>-<architecture>-<release>.tar.gz`; separate archives may contain the Telegram bot or QModem. OpenWrt 25.12/snapshots use APK, while 24.10 uses IPK. Do not guess the package architecture from the CPU model.

The daemon statically includes libubox/libubus/libuci; other dependencies (ModemManager, qmi-utils, jsonfilter, libjson-c, etc.) are installed from your firmware's package feeds.

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
