# MedveModem Manual — English Translation

[Русский оригинал, разделы 1–11](MANUAL.md) · [Quick user guide](USER-GUIDE.en.md) · [Documentation](README.md)

> [!NOTE]
> English translation of all eleven chapters of the Russian manual. Code identifiers, paths, CLI commands and API fields are left untranslated.
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
7. [Other LuCI integration points](#7-other-luci-integration-points)
8. [Telegram bot pages in LuCI](#8-telegram-bot-pages-in-luci)
9. [Telegram bot in Telegram](#9-telegram-bot-in-telegram)
10. [Files, logs and security](#10-files-logs-and-security)
11. [Configuration and hardware profiles](#11-configuration-and-hardware-profiles)

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

Shown only for an eSIM-capable device, whether the chip is reported by ModemManager or found by **Check eSIM** under Diagnostics (including removable cards such as 9eSIM). **Read eSIM** exposes EID (masked), free storage and installed profiles/activation state, usually without disrupting the modem session. **Enable** switches to an installed disabled profile after confirmation, interrupting connectivity for approximately 1–3 minutes while the modem re-reads the SIM; Foxconn SDX55 devices such as T99W175/DW5930e may require an explicit modem reboot. Buttons allow session restart, re-registration and reboot with confirmation. **Alpha 1 already supports eSIM detection, reading and switching between profiles stored on the chip. Until Alpha 2, load new profiles onto the built-in eUICC or removable UICC/9eSIM with a compatible phone**, using the card's app or EasyEUICC/OpenEUICC on Android; deleting profiles through MedveModem is also not available yet. On dual-slot modems, the active slot is read; switch SIM slots before inspecting the other.

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

## 7. Other LuCI integration points

- **Status → Overview** contains a Modems panel showing whether Internet access works and which modem carries traffic, with one line per modem.
- **Network → Interfaces** displays modem interfaces using the `medvemodem` protocol as **MedveModem** (since r149) rather than “Unsupported protocol type”. Prefer editing these interfaces from the modem page so changes receive transaction/rollback protection.

### 7a. Whitelist restrictions

Under **Modem → MedveModem → Whitelist restrictions** (r185), the UI collects tools for operators that allow access only to approved destinations:

- **Current state:** per-channel unrestricted, whitelist-only, Internet via tunnel despite whitelist restrictions, or no response.
- **Service checks:** through a selected channel or each channel in turn; a matrix of service × channel is shown. This is the same test available on the modem page, taking roughly 10 seconds and under 1 MB per channel.
- **Local proxies** (r189): proxies for package downloads when direct GitHub/OpenWrt downloads are blocked. Detects mixed proxy from podkop, podkop-plus, forkop, NetShift, SSClash, nikki and other clash/mihomo implementations. **Test** probes GitHub and downloads.openwrt.org through a proxy. **Fix** creates a local override of host/port/login/password without altering the external application's configuration; an empty password retains the previous password. **Add proxy** creates a user-defined entry tested before autodetected ones. Custom entries are UCI `proxy` sections in `/etc/config/medvemodem`.
- **Whitelist mode** (podkop 0.8, forkop 2) moved here from Settings.
- **Whitelist detection:** whitelist-test addresses moved here; primary connectivity-check addresses remain in Settings.
- **Services on Internet restoration:** restart tunnel-related services on channel recovery, as in Settings.
- **Notifications:** whitelist-only SMS notifications and delay moved here from Settings → Notifications (master SMS enable/recipients remain there); links to bot notifications in LuCI and `/settings` in Telegram.
- **Advice:** DNS `77.88.8.8`, DPI bypass tools, tunnel endpoints hosted at an allowed address.

Earlier cards in Settings show a pointer to the new Whitelist restrictions page.

## 8. Telegram bot pages in LuCI

Go to **Modem → MedveModem → Telegram bot**.

- **Overview:** whether the bot is running, token/admin setup, receiving commands and sending notifications, certificate/clock health and queue status. **Paths to Telegram** lists direct, router-tunnel, proxy, modem and emergency-IP routes with separate receive/send status. **Test** queues a probe and shows its progress. If SSClash/nikki lacks a mixed port, the page advises enabling one. Controls: Refresh, Restart bot, Send test message; warns on Telegram conflict 409 when another process uses the same token.
- **Setup wizard:** BotFather token → bind administrator by Telegram ID after a message to the bot → test. **Test without proxy** really disables proxy for that check; if the direct Telegram endpoint is inaccessible, configure a proxy first.
- **Settings:** enabled/language, allowed operations (show SMS, forward SMS, control modems, edit settings from Telegram), polling/send path policies (automatic/proxy-only/direct-only), fallback through other modems, chosen direct interface, manually configured SOCKS5 (`socks5h://`) and HTTP(S) CONNECT proxies and their order relative to autodetected routes, shared proxy username/password and emergency Telegram IPs with certificate verification.

## 9. Telegram bot in Telegram

### 9.1 General use

Only the bound administrator receives replies. Since r149, the **☰ Menu** command selector is used instead of a permanent reply keyboard beneath the text input. The bot edits one message in place as inline buttons are pressed (Refresh and Back) rather than creating a stream of separate status messages.

| Command | Purpose |
|---|---|
| `/menu` | Dashboard with section buttons |
| `/status` | Router/modem Internet status, downtime and number of failures over 24 h, service CPU/memory and MM/QModem restarts |
| `/modems` | Modem list and per-device cards; active Internet modem is listed first |
| `/channels` / `/uplinks` | Ordered uplinks, connection state and whitelist-only restriction |
| `/sms` | Incoming SMS when permitted |
| `/send` | Compose and confirm SMS using selected modem, number/contact and text; only when SMS/USSD sending is allowed |
| `/ussd` | USSD requests, operator presets and interactive responses; same permission |
| `/network` | Main route and per-modem addresses, external IPs, podkop/forkop tunnel status, service tests and whitelist restriction page |
| `/quota` | Traffic quotas |
| `/events` | Recent events with explanations |
| `/log` | Modem logs as a file |
| `/logall` | Redacted complete router log as a file |
| `/settings` | Alerts, proxy routes and bot settings |
| `/help` | Command help |

### 9.2 Modem card

Shows operator, qualitative/numeric signal, role, today's downtime and outage count and traffic. With `allow_control` enabled in LuCI, buttons provide Reconnect, Disconnect/Connect, Make first among modems (wired WAN can still outrank it), Suspend/Resume management and failover exceptions on the channels page. Connection-disruptive operations ask for **Yes / Cancel** confirmation. The result is one of: completed, in progress, completed with warning, failed.

### 9.3 Network

Lists the main route and each modem's interface addresses; buttons obtain each channel's external IP. Shows whether podkop/forkop is installed, version, whether sing-box is running, router DNS tunnel route and tunnel egress. A service-check action probes a selected channel.

### 9.3a Whitelist restrictions

Separate menu button since r185. Lists unrestricted, whitelist-only, tunnel-reachable or unavailable channels; whitelist mode status and optional On/Off control; blocking test checks each modem after warning and categorizes the outcome as unrestricted (green), partially blocked (yellow), whitelist-only (white) or offline (red).

### 9.4 Notifications

One situation is represented by one message, edited as the condition evolves. Topics: Internet lost/recovered, an uplink becoming whitelist-only, backup modems, MM/QModem restarts or stoppages, data quotas, router faults, new SMS (if permitted), whitelist mode activation/deactivation and server changes. After a MedveModem restart the bot remains quiet for two minutes and sends one silent summary.

In **Settings**, select smart, important-only, all or disabled notifications; choose categories; set quiet hours (silent delivery without losing messages). Internet outage or exhausted quota alerts may remain audible unless disabled.

### 9.5 Telegram proxies

Route order: user proxies configured *before detected*, detected router proxies, user proxies configured *after detected*, direct, other modems, and emergency IPs. Detected proxies include:

- podkop 0.7/0.8, podkop-plus, forkop 1.x/2.x and NetShift, including login authentication when configured.
- **SSClash** (`/opt/clash/config.yaml`) and **nikki** (`/etc/nikki/run/config.yaml` or UCI), reading `mixed-port` and `authentication`. Without a mixed port, Settings/Proxy advises enabling one: for SSClash add `mixed-port: 7890` and restart Clash; for nikki use **Mixin → Mixed Port**. The bot only reads these configurations.

The Proxy view shows the active route and individual failure reasons: refused connection, eight-second timeout, proxy rejection, or Telegram token rejection. Adding a proxy by sending `socks5h://…` or a `t.me/socks?…` link causes the bot to remove that message immediately and test the proxy.

### 9.6 What the bot does not do

It does not mark or delete SMS, log SMS text, USSD replies, PINs or proxy URLs (syslog contains action labels such as `admin_cmd_status`), or control modem hardware when control permission is off.

## 10. Files, logs and security

| Path | Purpose |
|---|---|
| `/etc/config/medvemodem`, `/etc/config/medvemodem-bot` | UCI configuration; LuCI can modify it; for manual edits use package-scoped `uci commit medvemodem` |
| `/etc/medvemodem/` | Persistent modem state, SMS archive, contacts, whitelist-mode rollback journal |
| `/etc/medvemodem-bot/` | Bot salts and persistent state; credentials are stored in restricted configuration files |
| `/var/run/medvemodem/` | Volatile runtime state |
| `logread -e medvemodem`, `logread -e medvemodem-bot` | System logs |

- **Networking:** the engine avoids restarting the entire network or Wi-Fi, operating only its own interfaces with `ifup`/`ifdown`. If one of its modem interfaces loses a firewall zone, it may restore `wan`; interfaces intentionally moved to another zone are left alone.
- **Permissions:** OpenWrt services run as root; `ubusd` is not restarted.
- **Privacy:** IMSI/ICCID and SMS bodies are not logged. Persisted SMS archive requires the owner's choice; diagnostic reports redact identifiers.
- **Other managers:** podkop, forkop, SSClash, nikki and QModem are not installed/reconfigured automatically. The user-enabled whitelist mode is a documented exception: it may change the selected tunnel server and `fully_routed_ips` for a podkop/forkop section, with original values recorded for rollback.

## 11. Configuration and hardware profiles

This section is for administrators editing UCI manually or developing profiles. Prefer LuCI; for manual changes use targeted `uci set …` followed by `uci commit medvemodem` (never a bare global `uci commit`).

### 11.1 `/etc/config/medvemodem`

A single UCI file with sections by type. `modem` and `esim` sections identify a device with `device_id`; `device` sections use `id`, referring to the same identifier.

| Identifier | Meaning |
|---|---|
| `usb-serial:<serial>` | Unique modem USB serial (not one shared by clones) |
| `usb-port:<path>@<port>` | Missing/shared USB serial, or a tethered phone; binds identity to USB topology |

| Section | Data |
|---|---|
| `core 'core'` | Service enablement; auto-connect/recovery, interface repair, `reach_target`, `reach_restricted`, SMS polling/archive, cell information, USB autosuspend |
| `modem` | Device `device_id`, user-defined `name` |
| `device` | Owner, USB composition `usb_config`, `quota_*`, `web_password`, radio and recovery settings |
| `sim` | User-assigned SIM label, number, notes, SMS center and data limit; keyed by a salted ICCID hash |
| `contact` | SMS address book |
| `sms_forward 'sms_forward'` | ntfy/webhook: `web`, `kind`, `url`; forwarding by SMS: `sms`, `sms_number`, `sms_contact`, `sms_parts` (default 3 concatenated parts), `sms_max_per_hour`; eligible devices: `modems_all`, `modem` |
| `alerts` | SMS connectivity alerts and thresholds |
| `ussd` | Stored modem USSD shortcuts |
| `usb_port` | USB port labels |
| `esim` | eSIM profile metadata |
| `wlmode` | Whitelist-mode manager, section, servers, LAN/router rules |

Options of `core` (the ones commented out in the default file show the service's default):

| Option | Default | Effect |
|---|---|---|
| `enabled` | `1` | The service runs |
| `update_channel` | `release` | Update channel: `release` (published releases) or `bench` (bench builds) |
| `settle_ms` | `2500` | How long a USB modem is given to finish appearing before its ports are read |
| `log_level` | `info` | Log detail: `info` or `debug` |
| `host_fixes` | `1` | Driver bindings that bench-validated profiles declare (an AT port the stock driver misses) |
| `auto_connect` | `1` | A new modem without an interface connects by itself, APN from the SIM (once per USB attachment) |
| `auto_recovery` | `1` | Automatic recovery: gentle steps always, disruptive ones only from bench-validated profiles |
| `repair_interfaces` | `1` | The modem's network interface is rewritten for this boot's device; into `wan` when it has no zone |
| `cell_info` | `0` | ModemManager's cell info and 3GPP location (hung an Orange Pi Zero 3 with a T99W175) |
| `antennas` | `1` | Per-antenna levels and carriers (qmicli through ModemManager's proxy while a page is open) |
| `mm_poll_s` | `0` | Read ModemManager at most once in this many seconds while a page is open (`0`: every 15 s) |
| `usb_autosuspend` | off | `1` leaves the kernel's USB autosuspend of the modems as it is (off by default: a suspended usbnet modem hangs) |
| `reach_target` | `1.1.1.1:443`, `8.8.8.8:443` | Reachability targets, checked through each uplink's own device |
| `reach_restricted` | two addresses | Tried when no `reach_target` answers: an answer means an operator whitelist (`none` turns it off) |
| `sms_poll` | `60` | How often SMS are read from ModemManager-owned modems, seconds (min 15; `0`: only when a page asks) |
| `sms_archive` | `1` | A modem's SMS storage three quarters full: the oldest received ones move to the router |
| `sms_keep_all` | `0` | Every received SMS is copied to the router at once (the modem keeps its own) |

Section `sim`: `key` (a salted hash of the ICCID since r12; the ICCID itself is not stored, older entries are converted
by the service), `number`, `description`, `sms_center`, data limit `quota_gb`, `quota_day`, `quota_warn`, `quota_action`
(`warn` or `backup`).

The ntfy/webhook token lives in protected `/etc/config/medvemodem-secret`, not in the ordinary UCI file; the configuration form does not reveal it.

**Identifying a modem after a port change:** `modem`, `device` and `esim` settings associated with `usb-port:` devices can follow the modem through salted hashes rather than storing raw identifiers:

- `imei_key`: salted hash of IMEI.
- `sim_key`: salted hash of `IMEI | IMSI` (or `USB identification | IMSI`), helping distinguish devices with the same IMEI.

Salt: `/etc/medvemodem/id-salt`. If the same IMEI appears simultaneously on multiple modems, IMEI-based tracking is disabled for them via `imei-shared.json`; SIM-based identity remains.

### 11.2 `/etc/config/medvemodem-bot`

| Section | Contents |
|---|---|
| `bot 'main'` | Enabled state, language, summary mode and permissions |
| `administrator 'primary'` | Bound recipient/chat ID |
| `transport 'poll'`, `transport 'send'` | Direct/proxy policy for receiving and sending |
| `notifications 'events'` | Enabled event categories |
| `proxy 'backup1'` etc. | Alternate Telegram routes |

Options:

| Section | Option | Default | Effect |
|---|---|---|---|
| `bot 'main'` | `enabled` | `0` | The bot runs |
| | `snapshot_minutes` | `15` | How often the state snapshot is refreshed |
| | `lang` | `auto` | Message language: `auto` (the admin's Telegram app), `ru`, `en` |
| | `allow_sms_content` | `0` | «Incoming SMS» in Telegram: number and text (nothing is marked read on the router) |
| | `sms_forward` | `0` | Each new SMS as its own message; only with `allow_sms_content` |
| | `allow_control` | `0` | Modem control from Telegram (disruptive actions ask first); `0`: view only |
| | `allow_send` | `0` | Sending SMS and USSD from Telegram (they may cost money) |
| | `allow_settings` | `1` | «⚙️ Settings» in Telegram: notifications and proxies; `0`: read only |
| | `emergency_ip` | — | Owner-supplied IPv4 addresses for Telegram, used only after other routes fail, with strict TLS |
| `administrator 'primary'` | `user_id`, `chat_id` | — | The admin's numeric Telegram ID and chat |
| | `alias`, `role` | `admin1`, `admin` | Entry name and role |
| `transport 'poll'`, `'send'` | `mode` | `auto` | Route to Telegram: direct, a proxy or a modem |
| | `fallback_direct` | `1` | Direct when the proxies did not answer |
| | `timeout_seconds` | `50` | Long-poll length (`poll` only) |
| | `bind_interface` | — | Kernel interface for direct requests (e.g. `wwan0`) |
| `notifications 'events'` | `enabled`, `mode` | `1`, `smart` | Notifications and their mode |
| | `backup`, `services`, `kernel`, `quota`, `restart` | `1` | Kinds: backup modems, ModemManager/QModem restarts, router trouble in the kernel's words, limits, MedveModem restarted |
| | `quiet`, `quiet_from`, `quiet_to` | `0`, `23:00`, `07:00` | Quiet hours: notifications without sound, none dropped |
| | `quiet_urgent` | `1` | «No internet» and a used-up limit still ring in quiet hours |
| `proxy 'backup1'` … | `enabled`, `role`, `priority`, `endpoint` | `0`, `fallback`, `50`, `socks5h://127.0.0.1:2080` | A backup route to Telegram (SOCKS: `socks5h` only) |
| | `proxy_username` | — | Proxy user name; the password is in `medvemodem-bot-secret` |

Bot token and proxy passwords are stored in `/etc/config/medvemodem-bot-secret` (section `secret 'main'`, `token` option), root-only with mode 0600 set/verified by `init-secrets`; the regular form never displays these values. `/etc/medvemodem-bot/` stores a salt and persistent snapshot.

### 11.3 State and storage

Persisted under `/etc/medvemodem/` (relatively infrequent writes, survives reboot):

| File | Purpose |
|---|---|
| `id-salt`, `diag-salt` | Salt for modem identification and diagnostic hashes |
| `identity.json` | Most recently recognized model/profile for early matching before MM appears |
| `imei-shared.json` | Hashed IMEIs observed simultaneously on more than one modem |
| `web-models.json` | Router-mode modem models learned from web APIs |
| `frozen.json`, `deactivated.json` | Suspended and disconnected devices |
| `radio-txn.json`, `radio-guard.json` | In-progress radio changes and cell/band lock guard |
| `traffic.json`, `traffic-peaks.json`, `sim-usage.json` | Traffic totals/peaks and SIM quota state |
| `downtime.json` | Channel downtime |
| `sms-seen.json`, `sms-archive.json`, `sms-alert.json` | Seen SMS identifiers, optional router-side archive and alert queue |
| `dns-check.json` | Most recent DNS interception check |
| `wlmode.json` | Whitelist-mode rollback ledger; deleted only after original settings are restored |
| `managed.json`, `journal.json` | Network/firewall fields owned by MedveModem and any incomplete configuration transaction, allowing user edits to remain untouched and interrupted operations to complete/roll back |
| `neighbours.json`, `tunnel.json` | Neighboring-cell and tunnel state |
| `usbmode-huawei.json` | Huawei USB composition changes |

Volatile state: `/var/run/medvemodem/` for daemon/jobs; `/tmp/medvemodem/history.json` for this boot's event history; `/tmp/medvemodem-bot/` for the bot.

### 11.4 Hardware profiles

A profile is JSON under `/usr/share/medvemodem/profiles/generic/`. Source schema is `profiles/schema.json`, validated by `tools/validate_profiles.py`. Package upgrades overwrite the installed directory, so author custom profiles in the source tree and submit them upstream; a separate user-profile directory is not yet available. The daemon loads updated profiles without restart within about one minute.

**Matching:** each profile receives a score based on its `match` fields. A failed match condition disqualifies that profile; among remaining candidates, the highest score wins.

| `match` condition | Points |
|---|---:|
| `pid` | 100 |
| `vid` | 80 |
| `mm_model_contains` | 80 |
| `mm_manufacturer_contains` | 60 |
| `drivers_any` | 50 |
| `manufacturer_contains`, `product_contains` | 30 each |
| `bus` | 20 for `usb`; 1 for `any` |

This allows multiple profiles for one VID:PID when distinguished by driver or ModemManager model.

**Profile fields:**

- `id`, `description`: identifiers and explanation.
- `extends`: family inheritance; `abstract`: base profile never selected directly.
- `fit` (`family` or `generic`): incomplete identification, prompts LuCI for a report.
- `policy`: `owner_priority`, AT access with MM, `mm_ports`, `fcc_unlock`.
- `hints`: protocols, AT interface numbers, `usb_configurations`, router mode, USSD and a display `title` up to 60 characters.
- `dial`: AT dialing command template using `{cid}`, `{pdp}`, `{apn}`.
- `bind`: host-side driver binding for the AT port.
- `radio`: AT dialect for network/radio configuration.
- `recovery`: approved stages, `settle_ms` and evidence source.

Recovery evidence flags: `bench-validated` means exercised on the project's test bench, permitting automatic recovery; `field-reported` comes from field routers or vendor documentation, allowing automatic execution only for policy-authorized gentle stages, with stronger stages behind an explicit user action.

Other profile directories: `presets/` (AT/USSD presets), `apn/` (operator APN data using IMSI codes), `catalog/` (hardware and USB-composition knowledge for family-profile fallback).

### 11.5 Packages and builds

| Package | Installs |
|---|---|
| `medvemodem` | C daemon, CLI/TUI, helpers, modem profiles, init script and netifd `medvemodem` protocol |
| `luci-app-medvemodem` | LuCI pages, rpcd ACLs, translations |
| `medvemodem-bot`, `luci-app-medvemodem-bot` | ucode Telegram bot and LuCI panels |
| `medvemodem-lpac` | eSIM/lpac integration |

Coordinated packages share the version fields `PKG_VERSION` and `PKG_RELEASE` in their Makefiles. Bench builds are published as `bench-v<version>-<commit>` pre-releases for `aarch64_cortex-a53`, `aarch64_generic`, and `x86_64`; IPK on OpenWrt 24.10, APK on 25.12. See [BUILDING.en.md](BUILDING.en.md) and [INSTALL.en.md](INSTALL.en.md).
