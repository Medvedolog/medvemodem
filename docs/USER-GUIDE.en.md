# MedveModem: Quick User Guide

[Русский оригинал](USER-GUIDE.md) · [Documentation](README.md) · [Installation](INSTALL.en.md)

This guide describes the `0.15.0-r197` source baseline (2026-11-16), published as the initial `0.16.0-alpha1` line. Refer to the [full manual](MANUAL.md), the [ubus API](UBUS-API.md), and the current project status for details. Hardware-dependent functionality requires validation on the actual modem.

## 1. Overview

MedveModem is an OpenWrt (24.10 and 25.12) service that detects USB modems, assigns control to one owner, maintains a connection, checks Internet access, switches to a backup when necessary and recovers a modem in escalating stages. The LuCI web interface, CLI/TUI and Telegram bot all consume the same state published by the C engine.

## 2. Installation and upgrade

Select the matching architecture and OpenWrt release family from a tested `bench-v<version>-<commit>` build on [GitHub Releases](https://github.com/Medvedolog/medvemodem/releases), if one is available. Verify the actual archive filename. OpenWrt 25.12.x/snapshots use APK; OpenWrt 24.10.x uses IPK/opkg.

Common architectures include `aarch64_cortex-a53` (Orange Pi Zero 3, Filogic, Airoha AN7581 / Nokia XG-040G-MD), `aarch64_generic` (Rockchip, 64-bit Raspberry Pi), and `x86_64`. Confirm the architecture reported by the installed firmware rather than guessing.

```sh
# OpenWrt 25.12 / snapshot (APK):
apk update
apk add --allow-untrusted ./medvemodem-0*.apk ./luci-app-medvemodem-*.apk

# OR OpenWrt 24.10 (opkg):
opkg update
opkg install ./medvemodem_*.ipk ./luci-app-medvemodem_*.ipk
```

Use `medvemodem-lpac*` from the same build only when eSIM support is needed. ModemManager, modem kernel drivers, umbim/uqmi, usb-modeswitch, QModem and the project's lpac build can also be installed from LuCI under **Settings → Diagnostics and components**. Package installation includes a log and availability checks; lpac downloads are SHA256-verified.

After an upgrade, if a manual restart is needed:

```sh
/etc/init.d/medvemodemd restart
/etc/init.d/medvemodem-bot restart
/etc/init.d/rpcd restart
rm -rf /tmp/luci-indexcache /tmp/luci-modulecache
ubus call medvemodem status | grep version
```

Then reload the web page with Ctrl+F5. Upgrades and service restarts **may interrupt modem connectivity**, so retain independent router access. MedveModem is designed to operate on its own interfaces rather than restart all networking/Wi-Fi. See [installation details](INSTALL.en.md).

## 3. LuCI: Modem → MedveModem

### Modems — main page

- **Top strip:** Internet availability, active carrier and ordered backups. Expands into a channel table showing channel number, device, status, route metric, today's traffic, health-check result and explanation.
- **Modem cards or table:** operator, radio signal, main/backup role, traffic, and Internet health (for example, latency in milliseconds; a stale result is shown separately).
- **Device menu:** Make primary, Pause, and Freeze. Freezing stops MedveModem, QModem and ModemManager from acting on that modem.
- **Ports and devices:** USB device tree, connect/disconnect history and port identities.
- **Basic / Detailed:** show or hide engineering fields. **Hide identifiers** masks IMEI, ICCID, phone numbers, addresses and map links for screenshots.

### Individual modem page

- **Overview:** current state, connection, SIM, Internet check and a manual connection-check action.
- **Network and signal:** radio technology, bands, carrier aggregation with frequencies/downlink/uplink, PCI and per-carrier levels; serving cell on a map; timing-advance distance estimates; signal history, antennas and neighboring cells (with a Lock action where supported). Network-mode and band changes roll back automatically if connectivity is not restored.
- **SMS:** conversations, sending, address book (up to 20 contacts, `.vcf` import), USSD with operator presets and an AT console with predefined commands when the engine owns a suitable AT port. When modem SMS storage is roughly 75% full, older incoming messages may be archived to the router (up to 500 retained).
- **Settings:** APN, IP type, DNS, TTL 64, automatic connection, AT port assignment and **Find AT port**.
- **Diagnostics:** recovery stages (section 6), modem event log, support report and **Check for an eSIM** to detect a built-in eUICC or removable UICC/9eSIM card.
- **eSIM:** after detection, **Read eSIM** displays EID and installed profiles; **Enable** switches between profiles already stored on the chip (the connection may briefly drop). **Until Alpha 2, load new profiles onto the eUICC/UICC/9eSIM using a compatible phone** and its card app or EasyEUICC/OpenEUICC on Android. Downloading and deleting profiles from MedveModem are not yet supported.

### General Settings

Tabs: **Service status**, **Internet and backup** (channel priority, failover, checks), **Modems** (default ownership, drivers, ModemManager, QModem), **Notifications**, **Diagnostics and components**.

### Telegram bot page

Tabs: **Overview**, **Setup wizard** (token → Telegram administrator ID → test message), **Settings**. Bot menu includes Status, Modems, Channels, Quotas, Events and Help, with commands `/menu`, `/status`, `/modems`, `/uplinks`, `/quota`, `/events` and `/help`. Incoming SMS can be forwarded. Notifications are consolidated; after a service restart a two-minute quiet period ends in one silent status summary. See the [bot's full guide](../packages/medvemodem-bot/README.en.md) for additional commands and optional controls.

### Router-mode modem web interfaces (HiLink, ZTE)

For a router-mode modem, the home page links to its own UI via a port on the router (for example `192.168.1.1:808x`). The `medvemodem-webproxy` helper forwards requests to the modem's internal address, avoiding conflicts on clients where a proxy routes `192.168.8.1` into a tunnel.

## 4. Command-line interface: `medvemodem`

`<id>` denotes a modem identifier from `medvemodem list` (typically `usb-serial:…` or `usb-port:…`).

| Command | Purpose |
|---|---|
| `medvemodem tui` or `top` | Interactive terminal UI; section 5 |
| `medvemodem list` | Device identifiers, model, owner and interface |
| `medvemodem show <id>` | Detailed modem record (JSON) |
| `medvemodem status` / `uplink` / `channels` | Service, Internet/uplink and all router channels |
| `medvemodem events <id> --text` | Modem event log |
| `medvemodem rescan` | Rescan USB |
| `medvemodem metrics <id>` | Signal measurements |
| `medvemodem at <id> 'AT+CSQ'` | AT command through the engine's arbitration |
| `medvemodem sms <id>` / `sms send <id> <number> <text>` / `sms delete <id> <n>` | SMS operations |
| `medvemodem ussd <id> '*100#'` / `ussd reply` / `ussd cancel` | USSD |
| `medvemodem recover <id> <stage>` | Manual recovery stage |
| `medvemodem bringup <id>` / `hangup <id>` / `dial <id> --apn …` | Manage data session |
| `medvemodem freeze <id> on\|off` | Freeze/unfreeze device |
| `medvemodem config plan\|apply\|rollback <id>` | Transactional network configuration |
| `medvemodem diag quick` / `diag profile <id> --at` | Quick and modem-specific diagnostics |
| `medvemodem dns-check <netdev>` | Detect upstream DNS interception |
| `medvemodem probe <id>` | Detect the AT port |
| `medvemodem esim status <id>` | eSIM status |

The same engine is accessible through `ubus call medvemodem <method> '{…}'`; see the [API reference](UBUS-API.md).

## 5. Terminal UI: `medvemodem tui`

The device list opens per-modem **Overview**, **Radio** and **Service** tabs.

| Key | Action |
|---|---|
| `j`/`k` or arrow keys | Move down/up |
| Enter or `l` | Open |
| `h`, `b`, Escape, Backspace | Back |
| Tab or `1`–`9` | Tab / item |
| `r` | Refresh |
| `q` or Ctrl+C | Exit |

Options: `--once` (single non-interactive snapshot), `--lang ru`, `--ascii`, `--no-color`, `--width N` and `--show-identifiers`.

## 6. Recovery ladder

From least invasive to most invasive, the engine stops as soon as connectivity returns. Stages 1–3 may run automatically for any modem; stages 4, 5 and 7 require explicit profile support; stages 6 and 8 are manual-only.

1. `observe` — observe and check; no changes.
2. `session` — reconnect the data session.
3. `control` — restart the selected control path (ModemManager, QModem or native).
4. `radio` — cycle the radio / repeat network registration.
5. `soft-reset` — modem command to reboot.
6. `rebind` — rebind a kernel driver.
7. `usb-reset` — reconnect the USB device.
8. `power` — cycle USB-port power where the board supports it.

Manual operations: modem page → **Diagnostics**, or `medvemodem recover <id> <stage>`.

Disable automatic recovery:

```sh
uci set medvemodem.core.auto_recovery=0
uci commit medvemodem
/etc/init.d/medvemodemd restart
```

## 7. Architecture

```text
LuCI · CLI/TUI · Telegram bot         clients using ubus
              |
      ubus object: medvemodem         shared API
              |
      medvemodemd (one C process, uloop)
        core: device registry, ownership, sessions,
              AT leases, health checks, failover, DNS, recovery
        mgr_* modules: SMS, radio, eSIM, modem web, signals,
              quotas, USB tree, SIM, AT command history
        helper processes: AT jobs, mmcli worker, SMS forwarding,
              eSIM (lpac), TTL/routes/web helpers
              |
      exactly one executor per modem:
      native AT + QMI/MBIM/ECM via netifd
      OR ModemManager OR QModem OR HiLink/ZTE web backend
```

Key invariants:

- **Exactly one modem owner.** The selected manager controls modem operations; other managers must not interfere. On Foxconn T99/T77, the ownership policy can reserve management ports for ModemManager while leaving safe AT ports to the engine.
- **Stable identity.** The preferred anchor is the USB serial; without one, USB topology helps identify the device. Name and settings may follow a relocated modem via a hash of its IMEI (raw IMEI is not stored). Firmware version is not part of the identity.
- **USB generations.** Responses from a previous enumeration are rejected after reconnect; `ttyUSB2` need not refer to the same modem.
- **AT port leasing.** SMS, USSD, signal checks and the console serialize requests through the engine.
- **Rollback.** Network mode, bands, cell lock and network-interface changes are transactional and revert when connectivity cannot be restored.
- **Profiles.** Under `/usr/share/medvemodem/profiles`: recommended owner, AT dialect, ports, quirks and safe recovery stages.

Important paths: `/etc/config/medvemodem`, `/etc/config/medvemodem-bot` (configuration); `/etc/medvemodem/` (persistent SMS archive/history); `/var/run/medvemodem/` (runtime state); `logread -e medvemodem` (service log).

## 8. FAQ

**No AT tab for this modem.** Another owner (ModemManager or QModem) holds the AT port. Check **Modem settings → AT port**. The Foxconn T77/T99 ModemManager port rule has been generated automatically since r104; it takes effect after a ModemManager restart, which can interrupt its modem sessions.

**USSD breaks Internet access.** Some LTE modems temporarily switch to 3G for USSD (including T99 and T77). LuCI asks for confirmation. SMS balance enquiries are sometimes more reliable.

**Bot outputs a literal array like `["…","…"]`.** Upgrade both `medvemodem-bot` and `luci-app-medvemodem-bot` together.

**Unexpected behavior or failures.** Export **Diagnostics → Modem report** or run `medvemodem diag quick` and include redacted `logread -e medvemodem` and `logread -e ModemManager` output.
