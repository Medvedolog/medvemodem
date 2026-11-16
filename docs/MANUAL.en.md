# MedveModem Manual — English Translation (Part 1 of 2)

[Русский оригинал, разделы 1–11](MANUAL.md) · [Quick user guide](USER-GUIDE.en.md) · [Documentation](README.md)

> [!IMPORTANT]
> **Translation in progress.** This document currently covers chapters **1–6** of the Russian manual. Chapters 7–11 remain available in the [Russian original](MANUAL.md); the short [English user guide](USER-GUIDE.en.md) covers many of their operational topics. Do not mistake this partial translation for the full reference.
>
> The underlying manual was written mainly against r151; its headings and navigation were compared to `0.15.0-r197` on 2026-11-16, **but not every LuCI page was retested**. Check current behavior and limitations against the code, [CHANGELOG](CHANGELOG.md), and the [ubus API](UBUS-API.md). Historical requirements in the technical specification are not proof that every feature ships.

Quoted interface labels reflect the Russian LuCI translation; equivalent English labels may differ slightly. LuCI follows the interface language; the Telegram bot has its own language setting.

## Contents

1. [Introduction and packages](#1-introduction-and-packages)
2. [How MedveModem works](#2-how-medvemodem-works)
3. [General LuCI conventions](#3-general-luci-conventions)
4. [Modems home page](#4-modems-home-page)
5. [Individual modem page](#5-individual-modem-page)
6. [Global settings](#6-global-settings)

## 1. Introduction and packages

MedveModem manages USB modems on OpenWrt 24.10 and 25.12. It discovers modems, selects their control owner, establishes connections, checks that the Internet is actually reachable, switches the router to a backup on failure, and recovers devices step by step, from session reconnection to USB reset. Supported device classes include tethered Android/iPhone devices and router-mode dongles with their own web UI (Huawei HiLink, ZTE, MegaFon M150-4, UFI).

| Package | Contents |
|---|---|
| `medvemodem` | C daemon `medvemodemd`, CLI/TUI `medvemodem`, helpers under `/usr/libexec/medvemodem/`, profiles under `/usr/share/medvemodem/profiles`, APN database |
| `luci-app-medvemodem` | English/Russian LuCI pages under Modem → MedveModem, an OpenWrt Status → Overview panel, and Network → Interfaces protocol integration |
| `medvemodem-bot` | ucode Telegram bot, ubus services `medvemodem.bot` and `medvemodem.bot.config` |
| `luci-app-medvemodem-bot` | LuCI Overview, Setup Wizard and Settings for the bot |
| `medvemodem-lpac`, `medvemodem-lpac-uqmi` | Optional eSIM/lpac integration; profile reading and supported switching; also offered under Diagnostics and components |

Install the engine and LuCI frontend at matching versions. The page checks its frontend version against the engine and prompts for a refresh when they differ.

## 2. How MedveModem works

### 2.1 Layers

```text
 LuCI · CLI/TUI · Telegram bot               clients: ubus only
                     |
             ubus "medvemodem"               one API
                     |
       medvemodemd (single uloop process)
         core: device registry, ownership, sessions,
               AT port leases, connectivity checks,
               failover, DNS and recovery
         mgr_* modules: SMS, radio, signal, modem web,
               quotas, USB tree, SIM, eSIM, AT history,
               whitelist mode, host/router faults
         helpers: AT jobs, ModemManager queries,
               SMS forwarding, lpac, TTL/routing/web,
               speed tests and whitelist-mode tasks
                     |
       exactly one modem executor:
         native AT + QMI/MBIM/NCM via netifd
         OR ModemManager OR QModem OR modem web API
```

Clients never access modem control ports directly. LuCI, CLI and the bot submit requests to the daemon, which decides whether the operation is permitted and queues it. The underlying state is common to the LuCI page, Telegram and `medvemodem show <id>`.

### 2.2 Identification and ownership

- **Profiles** identify a modem by VID:PID, USB interface classes and drivers. The baseline has more than 80 profiles under `profiles/`, with preferred owner, AT dialect, port roles, quirks (such as LTE USSD causing a temporary switch to 3G) and authorized recovery stages. Unknown models receive a generic manufacturer/driver-family profile and an **incompletely known** notice with an exportable report.
- **One connection owner:** ModemManager, QModem, native MedveModem or the router-mode modem web interface. Other managers must not interfere. The selection depends on profile and installed components; it can be overridden in **Connection → Who controls the modem**.
- **Stable names/settings:** linked to USB serial if present, otherwise to a hash of IMEI without storing the IMEI itself; identical IMEIs are disambiguated by USB port. iPhone/Android tethering devices are associated with the port, since their serial is a personal identifier.
- **USB generation:** results from previous enumerations are discarded after reconnection. A reused `ttyUSB2` name does not necessarily mean the same modem/port.
- **Freeze / Suspend management:** stops MedveModem, ModemManager and QModem from operating on the selected modem.

### 2.3 AT port arbitration

Every AT request—SMS, USSD, radio measurements, console commands or cell lock—goes through the daemon and its sequential **port lease**. No client opens a tty independently, and replies cannot be mixed between concurrent callers. If ModemManager or QModem holds the AT port, the AT tab is hidden and the AT-port settings card explains who owns the port. On Foxconn T99/T77, a generated udev rule lets ModemManager use the control port while MedveModem retains the designated AT ports.

### 2.4 Connectivity checks and channels

- **Reachability:** probes run explicitly through the modem's network interface, independently of the router's default route, against multiple configured primary addresses. On TCP port 443, the check verifies a genuine TLS response; an operator captive page presented at zero balance must not count as Internet access.
- **Whitelist-only access:** if only configured operator/government allowlisted destinations work, the channel is marked as restricted (abbreviated *WL* / *БС* in the bot). When possible, user traffic switches to a fully reachable channel. If a podkop/forkop/clash tunnel provides Internet access under these conditions, that is reported separately.
- **Channels and priority:** every modem is a channel with a route metric; a wired WAN is also a channel. Order them under **Settings → Internet and backup → Channel priority** or with arrows in the main channel table. Route metrics can be edited in that table. Automatic failover selects the next healthy channel and reverts when the preferred one recovers; the engine changes its own interface metrics rather than restarting the entire network.
- **Whitelist mode (r149+):** when an operator introduces restrictions, the daemon can pin a working tunnel server through podkop 0.8 or forkop 2, optionally directing all LAN/router traffic into the tunnel. On restriction removal, it restores earlier settings using a persistent change journal.

### 2.5 Recovery

Escalation: observe → reconnect session → restart control path → toggle radio → soft modem reboot → driver rebind → USB reconnect → port power cycle. Only stages authorized by the profile are automated; the process stops once Internet access is restored and increases the delay between successive attempts. On HiLink/ZTE router-mode devices, the radio stage may reconnect the mobile data session via their web API, and reboot may invoke the web reboot endpoint.

See [User Guide §6](USER-GUIDE.en.md#6-recovery-ladder) and the modem's **Connection recovery** panel. Potentially disruptive configuration changes (network mode, bands, cell lock or interface settings) are transactional: restore the previous state if connection verification fails.

### 2.6 Router-mode modem web interfaces

HiLink, ZTE, MegaFon M150-4 and UZ801 expose their functions through the modem's web UI rather than a MedveModem AT port. The engine uses the configured modem `web_password` in `/etc/config/medvemodem`; it may try the factory `admin` default. Login attempts are limited to at most once per five minutes and avoided when an operator is actively using the modem UI. It can read signal, cell, operator and SMS; supported buttons expose modem data, USSD, network mode, reconnect and reboot.

To open a modem UI from a client whose proxy incorrectly tunnels `192.168.8.1`, MedveModem exposes a temporary router-side `808x` port via `medvemodem-webproxy`. It closes automatically after the configured interval.

### 2.7 SMS

SMS are read through the selected owner: ModemManager, the engine's leased AT port, QModem or the modem web API. When modem SMS storage reaches roughly three quarters, old incoming messages are copied into the router archive. Modem messages are not deleted automatically. Forwarding supports Telegram, ntfy/webhook and relay SMS to other numbers. SMS text, IMSI and ICCID are not logged.

### 2.8 Telegram bot

The bot is an independent ucode service, reading modem data only through the engine's ubus API. For Telegram transport it may use a direct route, detected mixed/SOCKS proxies from podkop, forkop, NetShift, SSClash or nikki, manually configured proxies, other modems or emergency IP endpoints. Candidate paths are tested and a working one is remembered. See [the English bot guide](../packages/medvemodem-bot/README.en.md).

## 3. General LuCI conventions

Navigation: **Modem → MedveModem → Modems / Settings / Telegram bot**. Open a modem-specific page from its card.

- **Basic / Detailed** toggles engineering information, metrics, interfaces, raw event records and the AT console. The preference is remembered in the browser.
- **Hide identifiers** masks IMEI, ICCID, phone numbers, addresses and map links for screenshots.
- **Top banners** can be dismissed; the event journal retains them and offers **Show again**.
- **Themes:** Bootstrap, footstrap and Aurora; on mobile, tables collapse into cards, tabs scroll and controls are touch-friendly.
- **Package update:** if the installed frontend changes while a page is open, it reports that the router-side page has been updated and reloads it.

## 4. Modems home page

From top to bottom:

- **Connectivity status strip:** Internet availability, active channel and ordered backups. Can collapse to one line. If the primary is down but backup access exists while failover is disabled, the UI says so and offers an **Enable failover** action.
- **Action required:** missing modem driver, manually deleted interface or a modem web interface requesting a password.
- **Router fault:** USB-controller crash, kernel panic, overheating, power drop, low memory or storage errors; after reboot, faults from the previous boot are shown.
- **Other modem tools:** alerts about potentially conflicting installed managers, sms-tool, 3ginfo and similar tools.
- **Channels table** (Detailed mode): priority number, channel, device, status, editable route metric, today's traffic, peak throughput, connectivity latency and explanation. Arrow controls reorder channels; a secondary line shows the last check time.
- **Modems:** card/table toggle. A card contains model or custom name, operator, RAT and signal bars, role (main, backup, manual-only), traffic and throughput, Internet check result with stale-data marking, a modem-web shortcut, and number of recoveries per hour. Detailed mode additionally shows RSRP/SINR, aggregation, serving-cell MIMO, temperature, APN (r168), resettable recovery counter (r177), and SIM state (r175: inserted/operator, missing, PIN/PUK required, unknown because not registered). Controls: Connect/Disconnect, Check. Menu: Make primary, Disable connection, Suspend management, Run speed test (~20 s / 30–100 MB of SIM traffic), Remove from list (unplugged devices only).
- **Unknown modem:** incomplete profile notice and report export.
- **Ports and devices:** USB hubs, modems, interfaces/drivers and connection event history.
- **All SMS** (collapsed in Detailed mode): combined messages from all modems.
- **Looks like a modem:** a USB device with modem-like ports/vendor that did not match the classification rules. The administrator may classify it as modem / not modem; ModemManager-owned devices are accepted without asking (r179).

## 5. Individual modem page

The header shows custom name (editable), model, operator, signal, address, session, traffic and speed. Buttons offer Connect and Check connection plus an action menu. Tabs: Overview, Network and signal, SMS, AT (Detailed mode if the AT port belongs to the engine), Diagnostics, and Settings. Collapsed/expanded cards retain their state.

### 5.1 Overview

- **Connection:** protocol, IP addresses, interface, network device, gateway, operator DNS, MTU and route metric (Detailed mode).
- **Connectivity and traffic:** link health, current throughput, traffic for this session, today/month, downtime today and data quota.
- **Connection reliability:** USB health, port power, state monitoring, restrictions following frequent failures; Detailed mode shows owner, session and AT port.
- **Event history:** last 24 hours plus earlier events, including disconnects, reconnects, recovery, configuration changes, modem web login and MM/QModem restarts. Copy and earlier-history controls; up to 200 events per modem.

### 5.2 Network and signal

- RAT, operator, bands, registration, PLMN/network code, TAC/CID, base station/map, timing-advance distance estimate, and plain-language signal description.
- LTE/5G RSRP, RSRQ, SINR and RSSI with scales and explanations.
- **Carrier aggregation:** each carrier's band, uplink/downlink channel frequencies, bandwidth, PCI and signal levels.
- **Neighboring cells:** Lock action only for supported devices.
- **Antennas / RSRP:** per-chain metrics and missing-reading indicators. Modem/amplifier temperature; Compal may expose thermal power reduction.
- **Network mode and bands:** allowed technologies, preference, bands, cell lock; transactional changes are rolled back when connectivity does not return. A long-lived failed cell lock may automatically be removed after the configured timeout.
- **Signal history:** RSRP/SINR plots for 5 min, 1 h and 24 h annotated with cell locks/reconnects.
- **Traffic:** Rx/Tx plots at the same ranges; fill represents average and dashed line a 5 s peak. Based on router network counters, not active modem polling.

### 5.3 SMS

- Conversations grouped by number, newest first, unread highlighting/count, send/delete actions, **Code** extractor; modem storage and archived router SMS appear together.
- Address book on the router: up to 20 contacts; `.vcf` import; names are used by SMS views, bot and forwarding.
- USSD with operator preset buttons and dialog replies. The UI warns about device restrictions: FM350 no USSD, EP06 unconfirmed, LM960 only on 3G, T99/T77 may move temporarily to 3G. HiLink/ZTE/M150-4 send USSD through modem web APIs.

### 5.3.1 eSIM

Shown only for an eSIM-capable device, whether the chip is reported by ModemManager or found by **Check eSIM** under Diagnostics (including removable cards such as 9eSIM). **Read eSIM** exposes EID (masked), free storage and installed profiles/activation state, usually without disrupting the modem session. **Enable** switches to an installed disabled profile after confirmation, interrupting connectivity for approximately 1–3 minutes while the modem re-reads the SIM; Foxconn SDX55 devices such as T99W175/DW5930e may require an explicit modem reboot. Buttons allow session restart, re-registration and reboot with confirmation. Downloading or deleting profiles is deferred; use a compatible phone application or EasyEUICC/OpenEUICC on Android. On dual-slot modems, the active slot is read; switch SIM slots before inspecting the other.

### 5.4 AT

Available in Detailed mode only if the engine controls an AT port. **AT console** provides model-specific presets, command entry, daily history and copying. Port selection is in Settings.

### 5.5 Diagnostics

- **Modem and status:** model, firmware, IMEI, USB path, VID:PID, interface drivers, controlling manager and reason, last connection attempt and ports; temperature; missing drivers/services with an Install action and equivalent SSH command.
- **Device-specific service actions** with confirmation: usb-modeswitch rules, storage mode, Foxconn/Dell FCC unlock, USB composition (for example, DW5821e).
- **Connection recovery:** current stage/status, last attempt, profile limits and eligible manual/automatic stages. Concurrent actions are disabled while recovery runs.
- **Tests and support data:** site availability through this modem, operator whitelist restrictions, DNS interception; traffic-consuming speed test (~30–100 MB), per-second curve and possible throttle/tethering/radio interpretations; optional eSIM probe; ModemManager port logs; redacted developer report containing profile, driver state and AT/web response information.
- **Suspend management:** last and most intrusive control, freezing operations on that modem.

### 5.6 Modem settings

- **Connection:** owner selection first, APN (SIM database/custom), IP type, APN username/password, DNS, TTL 64, autoconnect. Hardware NAT acceleration can bypass TTL rules and is explicitly warned about. Changes are transactional.
- **SIM card:** status/operator/number, PIN and switching between physical/eSIM slots.
- **Modem web:** read method, password/Forget password, router-forwarded UI link, carrier address, session/month/lifetime traffic, firmware, RAT, LTE bands and HiLink counter reset.
- **AT port:** active port, owner, automatic probe and manual selection.

## 6. Global settings

Shared across all modems. Tabs except Service expose Save and Discard changes controls.

### 6.1 Service

Process table with daemon, Telegram bot, ModemManager and QModem, including PID, RSS, uptime, daily restarts and daemon crashes since boot. **Restart MedveModem**; **Emergency QModem restart** with warning (normally QModem only reloads settings).

### 6.2 Internet and backup

- **Channel priority:** modem and wired WAN ordering; Apply order.
- **Auto failover:** enable/disable, policy on preferred-channel recovery and manual-only links.
- **Connectivity checks:** primary and whitelist-test addresses, TLS validation.
- **Whitelist mode:** select podkop 0.8 or forkop 2, section and tunnel servers (fixed or first healthy among selected), optional routing of all LAN/router traffic into the tunnel, activation/deactivation delays, community-list checks, manual On/Off and operation journal. Older podkop (pre-0.8) and forkop 1.x cannot switch servers live.
- **Services on connectivity restoration:** restart selected services after a connection comes up. ZeroBlock is selected by default when installed (r167), because it may start before the modem and otherwise wait up to ten minutes for its own check. The UI explains that zapret-like workarounds may be ineffective over mobile Internet/whitelist-only access.

### 6.3 Modems

Since r174, all modem-related global settings share this tab.

- **Modem maintenance:** connection and recovery defaults, automatic handling of newly detected devices, service ports; network/firewall/DNS interception checks (DNS check disabled by default); modem-web forwarding and OpenWrt Overview integration.
- **Detailed radio data:** per-antenna levels, neighboring-cell and location queries via ModemManager (disabled by default because these previously hung some low-power routers); timeout for removing a failed cell/band lock.
- **Modem names:** rename all known devices from one place.

### 6.4 Notifications

- **SMS alerts:** send outage/other events to phone numbers in the address book; Send test SMS.
- **SMS forwarding:** ntfy/webhook; relay SMS to up to five numbers via a selected or any healthy modem except the receiver; source modem selection and hourly rate limit.
- **Router SMS archive:** incoming SMS copied from a modem. **Keep all SMS on router** (r169; disabled by default) immediately copies every incoming message without deleting it from the modem. Copies accumulate in memory and are flushed to flash every ten minutes and during service shutdown (r170).
- **Telegram bot:** shortcut to bot settings.

### 6.5 Diagnostics and components

- **Check services** through every active channel.
- **MedveModem updater** (early implementation, r193): installed/latest GitHub release, update check, Update action and progress log. Downloads matching firmware/architecture archives and verifies their `SHA256SUMS`; updates engine/LuCI and bot if installed, preserving settings. Supports configured local proxies for blocked GitHub access. Options: `medvemodem.core.update_repo` (default `Medvedolog/medvemodem`), `medvemodem.core.update_channel` (`bench` or `release`).
- **APN database:** check and update.
- **QModem:** installation from the build archive with log.
- **Drivers and ModemManager:** installed/missing components, assisted installation.
- **Emergency: ModemManager:** restart with warning that connections managed by MM may drop for approximately one minute.
