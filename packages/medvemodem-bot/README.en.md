# MedveModem Bot — Telegram Interface

[Русский оригинал](README.md) · [MedveModem](../../README.md) · [Documentation](../../docs/README.md) · [Bot architecture and Telegram screen wireframes (RU)](../../docs/BOT-ARCHITECTURE.md)

MedveModem Bot provides modem and uplink alerts, status summaries, incoming SMS and diagnostic events in the administrator's private Telegram chat. It ships as `medvemodem-bot` alongside the optional LuCI panel `luci-app-medvemodem-bot`. Written in ucode, it supports OpenWrt 24.10 and 25.12 on every package architecture (`all` / `noarch`). The bot talks to the modem **only through the MedveModem engine's ubus interface** and can perform no operation that the engine disallows.

Languages: `option lang` accepts `auto`, `ru` or `en`.

## Capabilities

### Alerts

- Connection loss and recovery; prolonged outages and restoration; repeated short outages consolidated into one notification.
- Primary uplink changes, operator whitelist restrictions, SIM data quota, ModemManager/QModem restarts, router kernel problems and failures of monitored services.
- **Quiet hours**, 23:00–07:00 by default. Urgent notices remain enabled. Default *smart* mode suppresses insignificant brief outages; an alternative reports all of them.

### Dashboard and buttons

- A single inline-keyboard message with **Modems**, **Channels**, **Quotas**, **Events**, **SMS**, **Network** and **Settings**. **Refresh** updates the same message on demand.
- Per-modem cards show operator, radio network, signal, address, traffic, connectivity-chain status and today's Internet downtime.
- **Last 24 hours** summarizes outages, failovers, service restarts and traffic.
- **Network** reports external IP addresses per channel, tunnel status and service probes.
- **Whitelist restrictions** (r185) show unrestricted/whitelist-only status by channel, a whitelist-mode toggle and a per-modem blocking test **without switching router routes**.
- Events and downloadable modem/router logs with tokens and links redacted.

### SMS and USSD

- Incoming SMS display sender and source modem. The **Code** button extracts digits. Bot access does not mark received SMS as read.
- `allow_sms_content` controls display of SMS bodies; `sms_forward` controls forwarding.
- With `allow_send '1'` (r184), send SMS via `/send`, **Write SMS** beneath an incoming message or **Reply** to a forwarded message. The workflow selects a modem, recipient number or contact, and message; a confirmation page presents the final details and allows **one send**.
- USSD via `/ussd`, operator-code preset buttons and interactive replies. Numbers and SMS text are not logged.

### Optional control

When `allow_control '1'` is enabled, a bound administrator may connect/disconnect, reconnect, re-register, restart, pause/resume management, make a modem primary and change automatic failover exceptions. Disconnect, reconnect, re-register, restart, make-primary and pause-management require confirmation; connect and resume do not use a separate confirmation page. Administrative actions are recorded in the system log as `admin_*`. When disabled, the bot only reads state.

### Telegram connectivity

The bot supports a direct connection, authenticated SOCKS/HTTP proxies (including podkop/forkop), or a selected modem uplink. If its route becomes blocked, it may move to another path and return to the preferred one once restored. It can discover known proxy managers on the router and suggest a working endpoint.

## Security

- Only the linked administrator in a **private chat** receives responses; unknown users and groups are ignored.
- Telegram token is stored in a separate file with permissions `0600`, not in logs, state files or curl command-line arguments.
- IMEI, ICCID, IMSI, phone numbers and SMS text are not written to bot logs or persistent state.
- The service runs as root, like other OpenWrt management services. It neither restarts `ubusd` nor creates a separate system user.

## Installation

Download `medvemodem-bot-<version>-<release>.tar.gz` from a matching tested [MedveModem release](https://github.com/Medvedolog/medvemodem/releases). The engine version must match.

```sh
# OpenWrt 25.12 / APK
tar xzf medvemodem-bot-*-25.12.tar.gz
apk add --allow-untrusted ./medvemodem-bot-*.apk ./luci-app-medvemodem-bot-*.apk

# OR OpenWrt 24.10 / opkg
tar xzf medvemodem-bot-*-24.10.tar.gz
opkg install ./medvemodem-bot_*.ipk ./luci-app-medvemodem-bot_*.ipk
```

Use only the commands matching the router's package manager. Runtime dependencies are installed by the package manager: `ucode` with the ubus, uloop, fs, uci, log and digest modules; `curl`, `ca-bundle` and `jq`.

## First-time setup

1. Create a Telegram bot with [@BotFather](https://t.me/BotFather) and copy its token.
2. In LuCI open **Modem → MedveModem → Telegram bot**. Follow the wizard: token → enter your numeric Telegram user ID (from @userinfobot) → choose Telegram path (direct, proxy or modem) → **Enable and start bot**.
3. Use **Send test message**, then send `/menu` in Telegram.

Commands:

```text
/menu     /status    /modems    /uplinks
/sms      /send      /ussd      /network
/quota    /events    /log       /logall
/settings /help
```

Telegram's command menu beside the text field provides command hints; there is no permanent reply keyboard beneath the input.

Settings live in `/etc/config/medvemodem-bot` and can be changed from LuCI or the bot's Settings menu when `allow_settings '1'` is enabled. Service control: `/etc/init.d/medvemodem-bot`.

## Troubleshooting

- Check **Telegram bot → Overview** in LuCI for service and Telegram path status.
- Run `logread -e medvemodem-bot` to view status/error codes without private information.
- Export a log from the bot or generate **Diagnostics → Modem report** in LuCI.
- See the [project changelog](../../docs/CHANGELOG.md) for release-by-release changes.
