# MedveModem — Release History (English)

[Original complete Russian changelog](CHANGELOG.md) · [Documentation](README.md) · [GitHub Releases](https://github.com/Medvedolog/medvemodem/releases)

> [!IMPORTANT]
> **English translation in progress.** This page currently covers the public alpha and source revisions **r197–r110**. The original [Russian CHANGELOG.md](CHANGELOG.md) contains the complete historical record, including all older revisions. This partial English translation must not be used as a replacement for the original release history.

## 0.16.0-alpha1-r1 — 2026-11-16

Public Alpha 1, published from the `0.15.0-r197` source snapshot without functional changes. The package release counter `r1` belongs to the renamed package version; `r197` remains the source baseline in the historical log.

CI builds in `.github/workflows/build.yml` require independent verification. The packaging issue in the first published commit was corrected subsequently by switching OpenWrt APK package versions to `0.16.0_alpha1-r1`; this is a packaging correction rather than a feature change.

## 0.15.0-r197 — 2026-11-16

Source baseline labeled “bench build: pending” in the original changelog.

- **All SMS on the Modems homepage:** an initially collapsed section in Detailed mode combines messages from all modems, newest first, with filtering by phone number, body and modem. When collapsed it makes no queries; when opened it reads cached daemon data without marking messages seen.
- **Modem SMS search:** search conversations by number, address-book contact or message text.
- **Clickable URLs in SMS:** show the full address and open it in a separate browser tab.
- **Operator-originated messages:** distinguish alphanumeric senders belonging to MegaFon, Beeline, MTS, T2, Yota and others, in both combined and conversation views.
- **Signal for modems without ModemManager:** the bot now reports signal percentages, radio network and operator for devices such as FM350 controlled by MedveModem and HiLink/E3372.
- **Connection wording:** bot status uses consistent neutral wording for established/not-established connections.
- **Primary modem unplug notifications:** announce a primary uplink change even if there is a moment with no active primary between the old and new channels. A just-unplugged primary is identified as primary rather than backup.
- **24-hour statistics in `/status`:** include primary-channel switch count and USB modem-disconnect count. Earlier, a day with physically unplugged modems could appear to have zero failures.
- **Speed test timeout:** correct background launch to avoid a LuCI `XHR request timed out` failure caused by the previous synchronous request's ~20-second limit.
- **eSIM profile switching:** refresh SIM/operator information immediately after the change instead of showing the former profile for up to half an hour (observed on YESIM, with misleading Beeline/T2 operator labels). Add “eSIM profile switched” to modem history.
- **FM350 control ownership:** failed connection no longer causes repeated handoff to an unsupported ModemManager and back to native MedveModem on every recovery.
- **User-deactivated status:** clear the mark once a connection is successfully re-established; do not transfer the mark to a different modem on the same USB port.
- **eSIM diagnostics:** move Check eSIM into a persistent Diagnostics card with an Open eSIM tab action; the previous Settings button disappeared after testing.
- **Bot connection diagnostics:** display the modem's model if it has no custom name; keep path-testing table rows, column widths and expanded detail stable during checks.
- **Whitelist → Service checks:** identify links with readable modem names, such as `FM350-GL (mm3 · eth4)`.
- **Modem home table:** use a single traffic-light status per row, show problems below the main status rather than in a mostly empty extra column, and avoid horizontal overflow.
- **Whitelist → Current:** show primary channel first, followed by backup 1, backup 2 and so on, with a separate Status column.
- **Copy and terminology:** rename “Fix” to “Edit” for local proxies; clarify the **Allow sending SMS and USSD from Telegram** permission and help text.

## 0.15.0-r196 — 2026-11-15

Bench build: `bench-v0.15.0-r196-31f1329`. Fixes from an external review of the C service, without new user features.

- **Router-side DNS checks:** correctly pass link latency to the test helper. Responses arriving faster than the expected link delay can now be identified as router-local rather than operator-originated.
- **Identity across USB ports:** merge old modem settings into the new record and remove the stale record when the same modem is moved.
- **Downtime accounting:** a router powered off overnight is not counted as Internet downtime; outages spanning midnight are counted from the first measurement on the new day.
- **SMS forwarding:** avoid a leftover byte sequence after truncating multi-byte message text. Remove spurious trailing `@` from alphanumeric sender names (e.g., `MegaFon`).
- **ZTE SMS:** receive incoming SIM-stored messages even when sent messages are also present in modem storage.
- **Concatenated SMS:** repeated segment numbers from the same sender no longer incorrectly merge unrelated long messages read by another modem manager.
- **Diagnostic redaction:** conceal username and password embedded in a webhook URL.
- **USB controller hangs:** prevent the hung controller/port from permanently blocking the daemon's connection to the system; LuCI becomes available after service restart. Stuck DNS/firewall probes are limited to five minutes.
- **Simultaneous subnet conflicts:** recover both modems when two affected devices reconnect at the same time.
- **Minor:** support half-hour time zones, kernel fault messages that include a CPU number, and Delete/PgUp/PgDn keys in `medvemodem top`.

## 0.15.0-r195 — 2026-11-15

Bench build: `bench-v0.15.0-r195-99878f8`.

- **Terminology and localization:** two rounds of text editing across bot, UI and installers. Standardize phrases for router, MedveModem service, USSD session, Mixed Port, authorization, interference descriptions, uptime, tunnel servers, form labels, re-registration action, SIM quota navigation and installer logs.
- **Sierra signal:** fix RSRP reporting for EM7565 and EM9291 with per-antenna measurements; show 5G NSA carrier separately from LTE on EM9190.
- **Temperature support:** add Fibocom FM150/FM160 and L850/L860, Telit FN990 and MeiG families. Sierra temperature may appear next to cell metrics without an additional query.

## 0.15.0-r194 — 2026-11-15

Bench build: `bench-v0.15.0-r194-db50af1`. Alpha 1 feature freeze begins at this revision: subsequent work is limited to bug fixes, localization, polish and the bot.

- **eSIM tab:** available in Basic and Detailed modes, but only for a modem with an embedded eSIM chip or removable card such as 9eSIM discovered by the Check eSIM action. Shows EID, available storage and profiles. **Enable** switches to a profile already present on the chip after confirmation, with an estimated 1–3-minute connection interruption. T99W175 and DW5930e may need a modem reboot before the newly activated profile is used; the UI also offers session restart and network re-registration. Profile download/deletion should still be performed from a compatible phone application. Dual-SIM modems show the active slot.
- **lpac in Diagnostics and components:** install the project's pinned lpac build from the MedveModem GitHub release, verifying its SHA256 checksum. The original source note warns that this could not succeed while the repository was private.
- **Bot Network page:** two lines per uplink, cleaned device names without an extraneous “- QMI Composition”, no duplicate main route, one correctly sized service-test button instead of eight truncated buttons, and service probes grouped as reachable, no response or normally blocked in Russia.
- **Bot modem card:** Add Confirm-required Re-register and Reboot modem actions.
- **Traffic quota in bot:** edit an individual modem's monthly limit from 1 to 200 GB or Remove limit after confirmation; use the same UCI setting as LuCI. Start day and warning threshold remain configured in LuCI.
- **Whitelist terminology:** use “Full Internet”/“No Internet” instead of “Open”/“No response”; show channel names based on modem model.
- **Self-update transport:** prefer curl for GitHub release downloads; keep uclient-fetch as fallback because it could hang on long GitHub response headers.

## 0.15.0-r193 — 2026-11-15

Bench build: `bench-v0.15.0-r193-cec7e30`.

- **Blocking test avoids false whitelist positives:** Telegram, YouTube, WhatsApp, Discord, Claude and ChatGPT can be blocked in Russia independently of operator whitelist mode. They are now classified as “normally blocked in Russia” and excluded from the score. General-reachability sites are Google, GitHub, Wikipedia, Cloudflare and Microsoft. Router DNS answers routed through a tunnel (FakeIP) are flagged and do not count as an operator block.
- **podkop 0.8 in bot:** fix an incorrect “sing-box not running / DNS via sing-box unavailable” report: podkop 0.8 embeds sing-box. Report the embedded process as active, and do not misrepresent the operator's public IP as a tunnel exit address.
- **Bot Network page:** label each uplink primary or backup N and show latency of the last probe or “no connection”; refresh tunnel probes older than ten minutes on opening; mark tests sent “Directly through modem, without tunnel”.
- **Whitelist page terminology and labels:** replace Open/No response with Full Internet/No Internet, name channels by modem rather than `eth1`, and remove empty placeholder rows.
- **AT preset hazard classes:** tell the user which commands disrupt a link, write persistent settings or require restart. Require confirmation with an explanation of consequences; high-risk commands such as reboot or USB composition changes remain reference-only and cannot be sent by confirmation.
- **Multipart SMS collision:** avoid joining distinct long SMS messages from the same sender when their 8-bit concatenation reference is reused after 256 messages.
- **LuCI/bot updater prototype:** detect GitHub updates (direct or via a local tunnel proxy), show a short “What's new”, verify SHA256 before installation; bot reports new versions every 12 hours. At this point the updater remained a prototype because the source repository was private.

## 0.15.0-r192 — 2026-11-14

Bench build: `bench-v0.15.0-r192-f6d5847`.

- **2G/3G and PPP health checks:** a slow working EDGE link could be incorrectly declared dead after a three-second timeout and endlessly reconnected. Increase the check to ten seconds for 2G/3G and PPP; LTE/5G keep their previous timeout.
- **ModemManager reconnection race:** wait for ModemManager to finish disconnecting the old session before bringing up a new one. Previously an early request could receive `WrongState` and leave the modem disconnected.

## 0.15.0-r191 — 2026-11-14

Bench build: `bench-v0.15.0-r191-fcfbbe6`.

- **Detected proxies in bot:** display every discovered proxy with address, username but not password, currently selected route, last probe time/status and an individual Test button. Previously all auto-detected proxies were compressed into one list entry.
- **JustClash:** discover its mixed proxy port and login credentials; explain how to enable the port if disabled.
- **Proxy text:** more readable localized labels, for example `podkop, section main — 192.168.1.1:2080` and `responded in 230 ms, checked just now`.

## 0.15.0-r190 — 2026-11-13

Bench build: `bench-v0.15.0-r190-241005a`.

- **Vendor-family fallback:** approximately 100 Telit, Sierra, Fibocom, SIMCom and MeiG models without an exact profile are now matched to a manufacturer family, with ModemManager as preferred owner, instead of a generic profile. Model-specific profiles continue to override the family.
- **Radio readings without ModemManager:** MedveModem reads network mode and serving-cell information for SIMCom, Telit and Sierra modems, plus permitted LTE bands for SIMCom and 5G cell readings where available; useful on memory-constrained routers. This is read-only.
- **JustClash in local proxies:** detect the configured mixed port and authentication for proxy-assisted downloads.

## 0.15.0-r189 — 2026-11-13

Bench build: `bench-v0.15.0-r189-54f8588`.

- **Local proxies card on the Whitelist page:** detect tunnel proxies installed on the router (podkop, forkop, NetShift, SSClash, nikki); test whether GitHub and OpenWrt package servers are reachable through them; override host, port, login and password or add a manual proxy. These routes are used for downloads and package installation when direct access is blocked. The external applications' configurations remain untouched: overrides live in MedveModem settings.

## 0.15.0-r188 — 2026-11-12

Bench build: `bench-v0.15.0-r188-5214d6f`.

- **Install drivers, ModemManager and dependencies when direct OpenWrt repositories are blocked:** if package-index refresh fails, retry through a local proxy discovered in podkop, forkop, SSClash or nikki, and install packages through it. Log which proxy was used without logging its credentials.
- **ZTE MF100 detection before driver installation:** recognize the correct profile even without `kmod-usb-serial-option`, recommending only the required driver rather than incorrectly requesting `kmod-usb-net-qmi-wwan`. Applies to other models with a dedicated profile.
- **OpenWrt 24.10 PPP:** avoid reporting native PPP service `pppd` as an unrelated process holding a busy port.

## 0.15.0-r187 — 2026-11-12

Bench build: `bench-v0.15.0-r187-0a6926f`.

- **Component installs and APN updates through local proxies:** when direct GitHub downloads are blocked, try local mixed proxies from podkop, forkop, NetShift, SSClash or nikki. Record which route was used in the installation log without changing those programs' settings.
- **Diagnostic URL redaction:** `medvemodem diag` retains only the website host; URL paths/query parameters are redacted, protecting ntfy/Telegram and other notification tokens.

## 0.15.0-r186 — 2026-11-12

Bench build: `bench-v0.15.0-r186-32e41e7`.

- **Whitelist text cleanup:** more concise labels for primary, backup and direct uplinks, and for bot notifications: “Restricted access (whitelist)”, “Only operator-approved websites available” and “Whitelist with Internet through tunnel”. Improve explanations of service checks and tunnel-service restarts.
- **Whitelist configuration labels:** “Routing package” instead of “Program”, “Proxy” instead of “Tunnel servers”.
- **Whitelist current-state table:** visually highlight the primary channel.

## 0.15.0-r185 — 2026-11-12

Bench build: `bench-v0.15.0-r185-995f683`.

- **Dedicated Whitelist restrictions page** under Modem → MedveModem: per-channel unrestricted/whitelist-only/tunnel state; service checks across individual or all channels in a service × channel matrix; relocated whitelist-mode controls, whitelist check addresses and SMS alerts; service-restart options also shown in Settings; links to Telegram bot notifications and DNS/DPI guidance.
- **Telegram bot Whitelist restrictions button:** display channels, the mode switch and blocking test; the Network page keeps addresses, tunnel state and general service probes.
- **Yandex speed test server:** use Yandex Internetometer on a nearby CDN as primary server, potentially reachable even under operator whitelists. Retain older servers as fallbacks; replace the Obninsk upload fallback with Penza because the former failed TLS validation.

## 0.15.0-r184 — 2026-11-12

Bench build: `bench-v0.15.0-r184-e8c3ab1`.

- **SMS and USSD from Telegram:** new disabled-by-default permission “Send SMS and USSD from Telegram”. The `/send` flow or contextual Write SMS/Reply buttons select a modem, number/address-book contact and text, then show full details with an explicit Send confirmation. Each confirmation permits exactly one send. USSD via `/ussd` supports operator code buttons and replies to interactive menus. Phone numbers, text and codes are not logged.
- **Basic-mode signal:** signal strength displayed as words and percentage (“Good signal · 54%”) rather than raw RSRP/SINR, with percentages constrained to the UI scale (“Excellent” starts at 75%). Numeric metrics and carrier aggregation remain in Detailed mode.
- **Internet latency indicator:** show “Internet · 31 ms” in Detailed mode and beside modem state in table view.
- **Primary modem label:** use “Primary channel” rather than simply “Primary”.
- **Speed test upload repaired:** the upload endpoint rejected an endless body without a known length. Replace a server that answered 403 and upload in 1 MB chunks, falling back among configured servers. Show whichever direction succeeded if the other failed; localize error messages and fit them within cards.
- **r183 included:** those packages compiled, but the pre-release publish step failed after a GitHub connection drop on the runner.

## 0.15.0-r183 — 2026-11-12

No published bench build: package compilation passed but release publication failed; changes were included in r184.

- **Registration denied is immediately visible:** display the network registration rejection and retry time on the modem card, rather than hiding it after MM disables the modem. Persist the reason until registration succeeds, log the rejection only once, and back off retry attempts to once per 30 minutes instead of an endless ten-minute loop (observed on a tester's ZTE MF192).
- **SIM present state is evidence-based:** only report “SIM inserted” when IMSI, ICCID, operator code or successful network registration confirms it. Older devices may claim “SIM ready” despite no card; display a specific “Cannot read IMSI” warning instead in LuCI and `medvemodem top`.

## 0.15.0-r182 — 2026-11-12

Bench build: `bench-v0.15.0-r182-01c425c`.

Corrections identified during an external review of modem detection:

- **Duplicate USB serial IDs:** if two modems with identical serial numbers were ever present at the same time, bind them permanently to distinct USB ports. Previously, unplugging one changed the other's `device_id`, custom name, settings and network interface.
- **Per-case interface grace periods:** a modem reinserted hours after startup can still reclaim its existing interface after a brief unplug.
- **Retry transient device-name correction failures** after one minute, up to five attempts, rather than waiting for another hotplug.
- **Firmware/update modes** for Sierra EM74xx/EM7565 and MediaTek bootloaders are marked as service devices instead of live modems.

## 0.15.0-r181 — 2026-11-11

Bench build: `bench-v0.15.0-r181-5f1c16d`.

Fixes following tester reports since r180:

- **Moved modem reconnects:** ZTE MF192 moved between USB hub ports, but its interface remained bound to the old port, resulting in 129 failed connection attempts. The interface now follows the modem.
- **No mistaken interface inheritance:** a replacement M150-4 in the original USB port no longer inherits the moved modem's interface; its Connect button and modem web link appear correctly.
- **FM350 temperature:** a zero-degree reading means an unresponsive temperature sensor, not a real 0 °C. Try the next modem sensor and do not display false zero values.
- **Unnamed card:** show manufacturer and port under the model name instead of the raw USB path alone.
- **MTS 8810FT modem web:** tolerate nonstandard HTTP response-header termination so operator, signal, technology and cell data can be read.
- **ZXIC stick metrics:** expose stored RSRQ, SINR, PCI, channel and TAC; calculate cell identifier correctly.
- **Release orphaned network interfaces:** the startup grace period introduced in r176 could expire without a new event, leaving two interfaces and two default routes for one modem. Schedule a recheck when the grace period ends; use a monotonic timer unaffected by initial clock synchronization.
- **Respect user-managed and foreign-managed interfaces:** device-name repair only touches interfaces MedveModem owns.
- **Avoid rewriting valid manually specified devices:** correct only entries that point to a nonexistent device or another modem; preserve custom bridge/VLAN assignments.
- **MegaFon M150-4:** do not label the link offline merely because modem web fields are empty while the modem reports a connected session, which could otherwise demote a functioning modem.
- **SIM card status:** clear a stale “SIM not detected” indicator when the SIM returns.
- **Modem-like device prompts:** ignore ordinary USB Ethernet, UART adapters, Wi-Fi, keyboards and hubs; only prompt for devices with CDC ACM/modem functions or a modem-maker hint.
- **Emergency USB recovery safety:** block controller reset or power cycling while **any** USB disk is mounted, not only extroot/system storage. The disk must be unmounted first.

## 0.15.0-r180 — 2026-11-09

Bench build: `bench-v0.15.0-r180-fd08e0a`.

- **Manual last-resort modem recovery:** collapsed “Last resort: USB controller and power” panel under modem Diagnostics → Connection recovery for devices that remain down after softer steps. **Restart USB controller** temporarily disconnects all devices sharing the controller; list affected devices first, and do not offer the action when the controller carries the router's extroot disk. **Power cycle** uses named USB/modem GPIO power lines exposed in `/sys/class/gpio` when available. These operations are never automatic and require explicit confirmation; affected devices receive journal entries.
- **Actionable escalation notice on modem cards:** after recovery exhausts its options, or USB reset does not help, a Fix shortcut opens that panel. It is not offered for SIM-card problems or operator registration denial, where power cycling would not help.

## 0.15.0-r179 — 2026-11-09

Bench build: `bench-v0.15.0-r179-13e0a04`.

- **Always detect ModemManager-owned devices:** a modem already controlled by MM must remain visible regardless of driver or how its ports are classified.
- **Looks like a modem:** when a device has modem-like ports or a modem manufacturer ID but does not match detection rules, offer a separate dashboard card with **Treat as modem** and **Not a modem**. The latter permanently dismisses the card.
- **Is this a modem?** in Detailed → Ports and devices: Auto / Modem / Not a modem per device. Classification is saved by VID:PID across upgrades.

## 0.15.0-r178 — 2026-11-09

Bench build: `bench-v0.15.0-r178-324f790`.

- **Older 3G modems with only ttyACM ports:** detect ZTE MF192 and similar devices that MM recognizes but MedveModem had filtered out as non-modems to avoid Arduino/USB-UART false positives. Two or more ttyACM ports now qualify as modem evidence.
- **Ports and devices:** do not label a multifunction modem merely “Mass storage” when it also exposes a driver CD or storage interface.

## 0.15.0-r177 — 2026-11-09

Bench build: `bench-v0.15.0-r177-8ca4f98`.

- **Hourly recovery counter `! N`:** display in both Basic/Detailed modes and in table view rather than only Basic cards. Clicking the indicator dismisses it until that modem undergoes another recovery in the current browser.

## 0.15.0-r176 — 2026-11-09

Bench build: `bench-v0.15.0-r176-29b52ae`.

- **Repair stale interface device names after reboot even if manually entered:** a tester's FM350 moved from `eth2` to `eth3`; MedveModem previously treated `eth2` as an intentional manual choice and failed to reconnect.
- **Do not prematurely release a late modem's interface:** after reboot FM350 appeared first and took the interface name previously used by M150-4; M150-4 appeared seconds later after its network interface had been released as belonging to an unplugged modem. Keep foreign interfaces for a three-minute grace period after service startup.
- **MegaFon M150-4 with no SIM:** correctly report “modem not registered with operator” even when the web API supplies empty fields rather than `-1`.

## 0.15.0-r175 — 2026-11-09

Bench build: `bench-v0.15.0-r175-aa809d6`.

- **Repair network and control interface names after reboot:** `eth1` became `eth2` on a tester's FM350 and the old `eth1` belonged to another modem. MedveModem now compares configured and observed `eth`/`usb`, `/dev/cdc-wdm` and dialing device paths for every modem and fixes mismatches automatically. A `repair interface` event is recorded; previously only router-mode devices were covered.
- **Updated supported external modem manager:** a stable release dated Nov 8 adds better FM350 SIM readiness states, fewer erroneous AT-port timeouts, correct recovery after USB port relocation and return of managed interfaces to firewall zone `wan`. An Update button is shown when that manager is installed; nothing changes otherwise.
- **Phone-like SMS conversations:** newest messages first, unread highlights and per-conversation unread counts such as `MegaFon (2)`. Opening the conversation clears the list count, while message highlights remain until the user leaves.
- **Router-mode SIM/network status:** detect missing SIM and missing operator network in HiLink/MegaFon M150-4 even when its web fields are empty. Avoid displaying obsolete signal data, avoid unnecessary reconnections, and show SIM inserted/operator, missing, awaiting PIN/PUK or unknown in Detailed mode.

## 0.15.0-r174 — 2026-11-09

Bench build: `bench-v0.15.0-r174-afb10d8`.

- **Settings organized by function:** the Modems tab now groups maintenance, detailed radio data and names. Subgroups include Connection and recovery, Checks, Modem web and overview, Switching, Advanced settings, From/To for SMS forwarding, Recipients/Events/Sending for SMS alerts, and dedicated whitelist-mode groups.
- **UI cleanup:** stop rendering lone dash placeholders beneath buttons when the status line is empty.

## 0.15.0-r173 — 2026-11-08

Bench build: `bench-v0.15.0-r173-f10387a`.

- **Automatically repair a stuck AT port:** detach and rebind only the affected modem interface driver, normally without interrupting the data connection. Limit to two attempts at five-minute intervals; if the port hangs again, escalate to USB reset with the normal recovery safeguards.
- **ZTE router-mode device moved to another USB port:** carry settings and takeover status to the new identity by reading its IMEI through ZTE's web API, as for HiLink, without duplicating the modem card.
- **Externally managed interface visibility:** show an existing unmanaged modem interface even when specified by device name such as `usb0`, and link the Fix action to the handover workflow.

## 0.15.0-r172 — 2026-11-08

Bench build: `bench-v0.15.0-r172-7ceebe2`.

- **A kernel-stuck AT worker no longer blocks all work indefinitely:** FM350 reports described an AT port silent for hours while Internet service continued; previously every AT request, signal read, SMS and recovery attempt failed with “AT task in progress.” Five seconds after an unsuccessful worker termination, mark the task failed, log the explicit kernel-stuck port diagnosis and refuse new requests until the process exits or USB is re-enumerated.

## 0.15.0-r171 — 2026-11-08

No standalone bench build; all changes shipped in r172.

- **Whitelist/tunnel checks on OpenWrt 25.12 BusyBox:** replace missing `od`/`hexdump` dependency with `awk` to avoid five “od: not found” messages per cycle and empty proxy names.
- **Bot first-run secret initialization:** generate salt and linking code through `ucode` when `hexdump` is unavailable, preventing initial setup failure on the affected BusyBox builds.

## 0.15.0-r170 — 2026-11-08

Bench build: `bench-v0.15.0-r170-427fec7`.

- **Write-efficient “Keep all SMS on router”:** copy newly received messages to RAM (`/var/run/medvemodem`) immediately, then batch writes to flash at most once every ten minutes and at service shutdown. A power failure during that interval does not permanently lose messages because they remain on the modem and are recopied later. Moving messages out of full modem memory and explicit deletions continue to persist immediately.

## 0.15.0-r169 — 2026-11-08

Bench build: `bench-v0.15.0-r169-a68ec46`.

- **Keep all SMS on router** (Settings → Router SMS, disabled by default): immediately copy every incoming SMS to persistent router storage without deleting it from the modem. While both copies exist, show only one conversation message. If the modem loses the original (some do on reboot), retain the router copy; do not forward it again. Deleting it through the SMS tab also deletes the local copy. Supports AT, ModemManager, HiLink/ZTE web APIs and external managers.

## 0.15.0-r168 — 2026-11-08

Bench build: `bench-v0.15.0-r168-35db3ec`.

- **MIMO and APN on Detailed modem cards:** for example, primary-carrier `MIMO 2` and `APN internet.beeline.ru`.
- **Speed test from the homepage:** Run speed test from a modem's ⋯ menu, with Rx/Tx result retained on the card; detailed carrier-throttling diagnosis remains on the modem page.
- **FM350 temperature sanity checks:** discard sensor placeholders such as 32767, do not confuse the `+GTSENRDTEMP: 1,…` sensor index with a 1 °C reading; fall back to `AT+ETHERMAL?` if `AT+GTSENRDTEMP=1` times out. Require three consecutive occurrences before showing a >15 °C temperature jump.

## 0.15.0-r167 — 2026-11-08

Bench build: `bench-v0.15.0-r167-d93689e`.

- **Automatically restart ZeroBlock after Internet connectivity returns:** ZeroBlock may start before the modem and wait up to ten minutes for its initial check. When installed, enable it in “Services on Internet restoration” by default, but preserve user opt-out.
- **Bypass-tool guidance:** if zapret, zapret2, byedpi or youtubeUnblock is installed, explain that such tools are unreliable over mobile data and ineffective against operator whitelists; they may interfere with the tunnel.

## 0.15.0-r166 — 2026-11-08

No standalone bench build; its changes entered r167 after a false-positive build test.

- **Yota modem signal via web API:** dedicated profiles for Yota LU150 (`1076:8002`) and Gemtek WLTUBA (`15a9:002d`, `15a9:003a`) read signal, serving cell and operator from unauthenticated `/status`. These devices previously connected but were misidentified as ZTE for web querying. Yota MF922 based on ZTE (`19d2:0565`) is tried as Yota first, then ZTE. Not yet verified on the project's physical modems.
- **Show numeric signal readings in Basic mode again:** RSRP (dBm) and SINR (dB) alongside qualitative labels on cards, individual pages and signal scales; they had been hidden since October 26.

## 0.15.0-r165 — 2026-11-06

No standalone bench build; its changes were included in r166.

- **ZTE MF100 auto-connects on Nokia:** a tester's modem reached Internet access 38 seconds after insertion without manual intervention. A spurious “Port used by other program: pppd” warning was subsequently removed for PPP launched by OpenWrt for the modem interface; an independently configured PPP process remains foreign.
- **Tested hardware:** add the modem to the README list of bench-validated devices.

## 0.15.0-r164 — 2026-11-06

Bench build: `bench-v0.15.0-r164-406d87c`.

- **Operator restricted-access detection for exhausted Tele2 balance:** probe `http://1.1.1.1/` rather than a general connectivity test address. Even without a carrier restriction, `8.8.8.8` could redirect to `dns.google` and mimic an operator captive page. Log the actual response; the Check button now reruns the operator-page test immediately.

## 0.15.0-r163 — 2026-11-06

Bench build: `bench-v0.15.0-r163-25d355b`.

- **Continuous FM350 signal history:** sample a modem managed by a third-party manager or through the native AT port once per minute, even with its LuCI page closed, as with MM-owned devices.
- **FM350 SINR and 5G readings:** retrieve SINR and 5G metrics through `AT+GTCCINFO?` rather than standard commands that omit them.
- **HiLink SIM-stored SMS:** read messages on the SIM as well as the modem and beyond the first 50, without duplicates; messages discovered long after arrival are neither marked new nor forwarded again.
- **Yota registered on MegaFon:** display both the SIM operator and visited network on the homepage, rather than the visited network alone.
- **Temperature plot:** interpolate across one or two missing readings instead of breaking the filled plot; a gap remains if the page was closed.
- **Whitelist-mode rollback:** if podkop/forkop rejects deactivation, preserve the pending rollback journal and report an error, so the next Off action completes the restoration rather than forgetting a LAN prefix. Switching to another program/section first restores the previous one; turning off “all LAN” or “router” removes the corresponding setting immediately.
- **Relocated USB interface:** repair a stale device path even when it was previously entered manually.
- **Band guard:** restore only the original band selection, without undoing a network-mode change made later.
- **Speed test:** show server/DNS/connect/TLS/HTTP errors instead of reporting zero throughput.
- **Remove unused configuration-template fields** (`profile_dir`, `owner_preference`; bot `state_dir`, `default_transport`, `require_private_chat`, `schema_version`); synchronize the manual's configuration chapter with actual code.
- **Include r162:** its build was blocked by an English settings-page test.

## 0.15.0-r162 — 2026-11-06

No published bench build.

- **SMS relay by SMS:** default maximum of three concatenated message parts (~201 Cyrillic characters); phones assemble them into one message. A one-part 70-character option remains available under Length.
- **Whitelist-mode UI refinements:** show activation status, selected tunnel server and manual/automatic trigger on the homepage. The mode card displays podkop's automatic selection and effective server or an explicitly selected one. Server lists are grouped by their source label (for example `Akenai_Priests`) rather than an opaque `src1` paragraph. Missing rule-to-source associations include guidance. The Activate button waits for completion and reports the result rather than a perpetual “Activating…”. podkop 0.8.9 Off restores URL-test auto-selection to `<rule>-urltest-out`.
- **SMS alert sender label:** use the model rather than USB path for unnamed modems.
- **Technical manual:** chapter 11 documents UCI layout, state files, modem profiles and package architecture.

## 0.15.0-r161 — 2026-11-06

Bench build: `bench-v0.15.0-r161-23f7133`.

- **Use modem model in forwarded SMS:** without a custom name, a long `usb-port:platform/…@1.2` identifier consumed the entire 70-character SMS. Use a concise model label such as `E3372h-320` or `USB 1.2` if unknown; apply the same naming to connectivity alerts, ntfy and webhook messages.

## 0.15.0-r160 — 2026-11-06

Bench build: `bench-v0.15.0-r160-f4e4903`.

- **Network re-registration:** new gentlest manual recovery step, shown as a separate button and first operation of Recover in order. Disconnect from the carrier and register again through ModemManager or AT. Manual only for now.
- **Forbidden network list on SIM:** in Detailed mode, read and clear previously rejected networks from the SIM's forbidden PLMN list.
- **Band-lock guard:** automatically release band restrictions when they prevent registration, matching the existing cell-lock guard.
- **Cell/band/carrier aggregation history:** journal changes such as `B3 → B7` at most once every two minutes.

## 0.15.0-r159 — 2026-11-06

Bench build: `bench-v0.15.0-r159-7aa8f79`.

- **Experimental Foxconn T99W373 and Thales MV32-W (X62):** recognize USB IDs `0489:e0f0` and `1e2d:00f1`–`00f4`; bring up data through ModemManager/MBIM, without AT commands; recovery remains manual. Enable the Foxconn FCC unlock hook from the modem page for `0489:e0f0`.

## 0.15.0-r158 — 2026-11-06

Bench build: `bench-v0.15.0-r158-2f484fe`.

- **Experimental Dell DW5934e / Foxconn T99W640 (X72) via USB:** identify using the ModemManager model; operate exclusively through ModemManager/MBIM without AT commands, with manual-only recovery. Known quirks (false amplifier overtemperature, hangs after base-station denial, USB becoming stable only after a prior PCIe boot) are recorded in the modem knowledge base.

## 0.15.0-r157 — 2026-11-06

Bench build: `bench-v0.15.0-r157-f25c0ee`.

- **Telegram bot setup wizard no longer pops up after restart:** while the bot rechecks command polling, LuCI must not mistake transient status for an incomplete first-time setup when notifications are already being delivered.

## 0.15.0-r156 — 2026-11-05

Bench build: `bench-v0.15.0-r156-92c2af7`.

- **“Operator restricted access” instead of unexplained “No Internet”:** when TLS connectivity fails, perform one ordinary HTTP probe to see whether the operator redirects to a captive payment/balance page (Tele2 zero-balance behavior). Show that diagnosis and the destination address in LuCI and bot.
- **ZTE MF100 and other devices without physdev auto-connect after boot:** netifd may mark an interface unavailable before MM starts and never re-enable it. The engine now restores availability before initiating the connection.
- **Yota vs MegaFon:** distinguish the SIM's virtual operator (Yota, T-Mobile, SberMobile) from the visited mobile network; display “Yota · on MegaFon network” and “SIM: Yota”.
- **SIM-anchored identity across USB ports:** relocate a modem's name and settings even when two devices share one IMEI or have none. Only salted hashes, not raw IMSI, are stored on the router.
- **Russian copyediting:** a professional reviewed 846 UI/bot/helper phrases, standardizing terminology (“router”, “username”, “notifications”, “whitelist”, “USB composition”), verb-led buttons and neutral language.

## 0.15.0-r155 — 2026-11-05

Bench build: `bench-v0.15.0-r155-5c54044`.

- **Recover after reboot despite manually edited interface paths:** Orange Pi Zero 3 can renumber the USB bus at boot. After one manual interface edit, the engine previously treated the path as user-owned and left a now-invalid device path unchanged; it can now repair the stale path.

## 0.15.0-r154 — 2026-11-05

Bench build: `bench-v0.15.0-r154-c0e2e82`.

- **Restore a lost route:** after recovering one modem, OpenWrt could remove another modem's default route, shown as `100 / —` or “no default route”; the engine now detects and restores it so failover can use that modem.
- **HiLink/UFI without a SIM cannot remain primary:** if the modem web API reports no SIM/network, disregard its misleading positive local connectivity check, fail over to another link and avoid pointless stick reboots.
- **Recover in order:** one button runs permitted recovery stages from least disruptive until connectivity returns, preserving per-stage results on the page.
- **Bot status restructured:** separate sections for Internet/primary channel, ordered backup modems with explicit unavailable reasons, Telegram path, daily outages and daemon resource usage. Warn when bot/engine versions differ.
- **UI polish:** remove `-1` operator label for SIM-less M150-4, rename “serving” cell to “primary”, and fix overlapping controls in the whitelist-mode card.

## 0.15.0-r153 — 2026-11-04

Bench build: `bench-v0.15.0-r153-3fbf6b5`. Earlier `bench-v0.15.0-r153-c373f02` lacks the missing-physdev fix and must not be used.

- **MM-owned modem autoconnects after boot, including Nokia/ZTE MF100:** even if netifd kept interface autostart enabled but never retried after MM became ready, MedveModem now raises it 30 seconds after MM acquires a previously disabled modem.
- **ModemManager 1.24 without physdev:** on Nokia snapshot builds, ZTE MF100 lacked a physical USB device path so OpenWrt rejected connections with “Device not managed by ModemManager”. The engine writes the corresponding ModemManager object path instead and refreshes it after MM restarts.
- **False busy-port alert:** ignore `pppd` launched for the modem's own ModemManager-managed PPP interface.

## 0.15.0-r152 — 2026-11-04

Bench build: `bench-v0.15.0-r152-6d1375c`.

- **Bot SMS forwarding respects general forwarding selection:** if Settings → Notifications → SMS forwarding is enabled for specific modems, the Telegram bot forwards only from them. If general forwarding is off or all modems are selected, retain forwarding from all.
- **MM-owned modem no longer stuck “Connecting…” after boot:** when netifd tries an interface before MM starts, the attempt may fail and MM may later find the modem without activating it. Once MM acquires the modem, the engine raises its interface unless the owner explicitly disabled it.
- **ZTE MF100 exact profile:** the legacy 3G stick `19d2:0031` is recognized by model rather than “known family, unknown model”.

## 0.15.0-r151 — 2026-11-04

Bench build: `bench-v0.15.0-r151-8350e04`. Labeled as the final 0.15 bench release; the next planned line was Alpha 1 candidate 1 (`0.16.0-r1`).

- **ZTE router-mode dongles** (MF79, MF823, MF833 and related) gain Huawei HiLink-like support: modem web data (carrier address, per-session/month traffic, model, firmware), USSD, network-mode changes (auto/2G/3G/4G), session reconnect and modem reboot, both by button and as recovery actions. New ZTE firmware requires an AD command signature, added only when needed. Not tested against a physical ZTE device on the project bench; field reports requested.
- **Intel L830-EB** (`2cb7:0210`) matched by a Fibocom XMM family profile under ModemManager; formerly the last unknown modem in an external manager's list.
- **Bot Channels:** show model names such as FM350-GL/T77W968, not internal strings like “Enabled” or “MBIM Composition”; show wired channel as e.g. `wan (eth2)`.
- **Modem diagnostics:** use the cached radio/operator/signal/band/channel information from any owner (MM, modem web, external manager or native AT) without sending commands just to generate a report; do not export cell IDs or private identifiers. For web modems also include SIM, network and session status.
- **HiLink self-reported connection:** distinguish good SIM/network but disabled data transfer, autoconnect off, unauthorized roaming, carrier rejection, and no Internet/balance from radio signal alone.
- **Manual:** document the architecture, every LuCI page and bot capabilities.

## 0.15.0-r150 — 2026-11-03

Bench build: `bench-v0.15.0-r150-d61ec3b`.

- **SSClash and nikki Telegram proxies:** auto-detect configured mixed ports and authentication, and use them for the bot's Telegram connection without modifying either application. If disabled, advise how to enable: SSClash `mixed-port: 7890` in `/opt/clash/config.yaml`; nikki **Mixin → Mixed Port**.

## 0.15.0-r149 — 2026-11-03

Bench build: `bench-v0.15.0-r149-38afc92`.

- **Whitelist mode** (Settings → Internet and backup): when the carrier restricts Internet to approved websites, pin a working tunnel server using podkop 0.8 or forkop 2 (specified server or first healthy one from selected names/patterns), optionally route all LAN/router traffic through that tunnel, and restore the original automatic selection and routing after restrictions end, including after crash/reboot. Manual On/Off via LuCI or bot Network page when control is enabled; bot announces state/server changes. Community whitelist hints can preselect tunnel servers whose endpoints remain reachable. Older podkop and forkop 1.x cannot change server live.
- **forkop 2.0 proxy discovery:** its mixed proxy is a server; preserve section-based detection for forkop 1.x/podkop 0.7.
- **TTL vs hardware NAT offload:** warn that an enabled MediaTek-like hardware acceleration path bypasses TTL fixing and may reveal tethering to the operator, with instructions to disable offload.
- **netifd protocol label:** modem interfaces no longer display “Unsupported protocol type” in Network → Interfaces.
- **Modem speed test and throttle diagnosis:** on-demand download/upload test with data-usage warning and a throughput-shape analysis: flat speed cap, cutoff after initial megabytes, or signal-dependent variability, followed by TTL, hardware offload, tariff and IMEI guidance.
- **Bot command menu:** remove permanent keyboard below the text box, retaining Telegram's ☰ command menu and proactively removing old reply keyboards in existing chats.

## 0.15.0-r148 — 2026-11-03

Bench build: `bench-v0.15.0-r148-adf2e53`.

- **Zero balance is no longer “Internet available”:** some operators accept connections solely to serve a top-up/captive portal. Require the real TLS response from the expected server; report “No Internet: operator responds instead of Internet” and switch to a backup rather than oscillating between false-positive links.
- **Disabled failover visibility:** if the primary has no Internet while a backup is healthy, prominently show this with an Enable failover button. The bot's No Internet notice offers the same advice.
- **Localization:** translate the “Modem answered itself” homepage diagnostic.

## 0.15.0-r147 — 2026-11-03

No published bench artifact: build/tests passed but package upload failed; included in r148.

- **MegaFon M150-4 web authentication:** an empty random login challenge and missing Referer caused a 403 even with the correct password. Reproduce the browser's actual authentication flow so operator, signal and SMS become readable again.
- **Huawei HiLink disconnected from cellular network:** stale last-cell values must not appear as excellent green signal and valid operator while the device itself reports no mobile network.

## 0.15.0-r146 — 2026-11-03

Bench build: `bench-v0.15.0-r146-1926513`.

- **Huawei HiLink persistent HTTP responses:** newer E3372-325 WEBUI 3.0.2.61 does not close the TCP connection after replying. Read the declared response length instead of waiting six seconds for EOF and reporting modem web timeout.
- **MegaFon M150-4 / Mobile.Router password fallback:** after a saved password fails, try `admin`, empty and the modem's own password.
- **Modem report:** include web backend type, login state and refusal reason without exposing secrets.
- **Bench test correction:** an uplink's own route may be steered via another fully connected channel under whitelist-only restrictions by design; changing unrelated channels' routes remains an error.

## 0.15.0-r145 — 2026-11-03

Bench build: `bench-v0.15.0-r145-1944fc3`.

- **Fast response to a hung network driver:** when Linux reports `NETDEV WATCHDOG` transmit queue timeout, reset the affected modem over USB immediately rather than waste minutes retrying a session that cannot recover (DW5821e on MBIM).
- **Successful USB rebind is recognized:** count the recovery as successful when the device remains enumerated but its drivers detach and reattach.
- **FCC unlock on OpenWrt without `/etc/ModemManager`:** create the missing directories before linking a hook.
- **DNS interception explanation:** localize the modem-card text.
- **Telegram path diagnostics:** label modem paths by name, interface in parentheses, show latency after OK and reason on failure; allow another path check to queue while one runs and display progress with controls disabled until safe.

## 0.15.0-r144 — 2026-11-03

Bench build: `bench-v0.15.0-r144-79769e6`.

- **Restored LAN access through a modem:** deleting the modem interface in LuCI also removed it from firewall zone `wan`. A recreated interface then gave the router Internet while LAN computers/phones remained offline. Re-add a modem interface to `wan` if it belongs to no zone; preserve an intentional assignment to a different zone.
- **Live-router bench tools:** `medvemodem-stand` performs read-only observation and whitelist simulation on the router; `tools/bench/stand-pages.cjs` checks LuCI pages from Chrome on a LAN computer, with notes under the bench test documentation.

## 0.15.0-r143 — 2026-11-03

Bench build: `bench-v0.15.0-r143-acdb30f`.

- **Forward SMS by SMS:** configure up to five forwarding recipients from contacts or manually entered numbers; choose a specific modem or any healthy modem (prefer one other than the receiver), and select all or specific source modems. Prefix forwarded messages with modem and sender; choose 1–3 SMS parts and hourly rate limit. Avoid forwarding relayed messages again. ntfy/webhook forwarding remains independently configurable. Do not log numbers/text.
- **DNS interception check disabled by default:** under TSPU, interception warnings from most carriers were noisy and alarming. It can still be enabled under modem maintenance or run manually on a modem page.
- **AT-console timeout messages:** translate the no-answer timeout and guide users to AT port selection.

## 0.15.0-r142 — 2026-11-03

Bench build: `bench-v0.15.0-r142-57f1234`.

- **Traffic chart layout:** one plot with Received/Sent selector, clearer captioning; place antenna metrics above carrier aggregation.
- **Homepage channel table:** simplify peak and check columns, move last check time into a single line below the table, and improve the daily-statistics label.
- **Bot proxy credentials:** one shared Username and password card for manually specified proxies (proxies added in Telegram keep individual credentials); action results use plain text rather than traffic lights.
- **Foxconn T77 in Telit composition (`1bc7:1910`):** select the AT interface according to profile priority, not enumeration order. Interface 03 works; 04 may remain silent.

## 0.15.0-r141 — 2026-11-03

Bench build: `bench-v0.15.0-r141-4b7b867`.

- **Duplicate IMEIs:** modems whose manufacturer or owner set the same IMEI must retain their own settings by USB port when USB serials are missing. Previously unplugging one could move its name/settings onto the other; log a duplicate IMEI warning.
- **Cell-lock progress visibility:** an open page older than ten minutes could fail with “read settings first” shown only at page top. Refresh current radio settings immediately before a write; show begin/refusal/keep/rollback toasts and an in-progress cell-lock description, then journal the band/channel/PCI and annotate the signal-history timeline.

## 0.15.0-r140 — 2026-11-03

Bench build: `bench-v0.15.0-r140-a792aac`.

- **eSIM read-only in LuCI:** in Detailed modem Settings, show EID, available eUICC storage and installed profiles (name, operator, active state and masked ICCID). Switching, removal and download are deferred. If reading fails due to MM/another owner's control port or missing lpac, explain the reason.
- **Missing SIM diagnostics:** show clear Russian ModemManager missing SIM/PIN/error messages instead of raw netifd/MM errors. Do not spam “reading SMS via AT” every minute for a SIM-less modem; journal a reading-path change once. A collapsed temperature card reads “not measured yet”.

## 0.15.0-r139 — 2026-11-03

Bench build: `bench-v0.15.0-r139-09fc8df`.

- **FM350 without RNDIS driver:** detect its network interface class `02/02/ff` as RNDIS and offer Install `kmod-usb-net-rndis` instead of leaving an MM-managed modem without a session.
- **Recreate manually deleted modem interfaces:** replace a confusing netifd rejection with a clear “no interface” diagnosis; Connect recreates the interface using its former name.

## 0.15.0-r138 — 2026-11-03

Bench build: `bench-v0.15.0-r138-c94331a`.

- **Per-modem traffic/throughput history:** Rx/Tx plots for 5 minutes, hour and day on Network and signal; fill represents average throughput, dashed line the five-second maximum, with peak/time in header and total bytes/table in Detailed mode. Read router interface counters rather than polling the modem.
- **Peak speed on channel table:** maximum upload/download throughput since boot, narrower columns with full tooltips.
- **Modem Settings tab:** last tab, visually separated and themed.
- **Native backend troubleshooting:** show actionable reasons for failed connection (missing RNDIS/other network driver, missing AT port) instead of raw English `backend native: …`.

## 0.15.0-r137 — 2026-11-02

Bench build: `bench-v0.15.0-r137-7cfebc5`.

- **Install dependencies via button or SSH:** for native non-MM paths, offer `umbim` for MBIM, `uqmi` for QMI and `comgt-ncm` for NCM, alongside commands for the actual router package manager (`apk add`/`opkg install`). Restart the router once after adding a protocol package.
- **Audit follow-ups:** do not USB-rebind while ModemManager still holds/queries the modem; restore already-unbound interfaces after a failed rebind; do not reject a correctly version-matched `medvemodem-bench` as obsolete; verify QMI network-mode changes against the precise expected technology set.

## 0.15.0-r136 — 2026-11-02

Bench build: `bench-v0.15.0-r136-370fb52`. OpenWrt 24.10 install test failed (“Cannot install package luci-app-medvemodem”), but 25.12 passed; 24.10 passed in r137.

- **Compact Telegram bot LuCI page (“UI v2”):** collapsible route-test summary, one line per path with separate command-receive/notification-send states, short OK/Error/Unchecked labels and expandable details. Show conflict 409 only when present; prevent duplicate path-test submissions.

## 0.15.0-r135 — 2026-11-02

Bench build: `bench-v0.15.0-r135-1dfc402`.

- **Model-specific USSD warnings:** Quectel EP06 accepts requests but may never deliver network replies; label as unconfirmed. Telit LM960 only supports USSD on 3G; tell the user to change mode.
- **Remove “ghost 5G” on T99:** `AT^DEBUG?` 5G cells without a band are firmware placeholders, not real carriers.
- **Antenna measurements:** treat RSRP ≤ −140 dBm as “not measured” on Quectel/QMI instead of a weak antenna.
- **Safer recovery:** never USB-rebind while MM is still probing the modem; reports showed this could leave it unconfigured until a power cycle.

## 0.15.0-r134 — 2026-11-02

Bench build: `bench-v0.15.0-r134-796582e`.

- **Restore Auto radio mode without reconnecting to a PC:** if a modem previously stored LTE-only, MM may advertise only LTE and refuse expansion. MedveModem explicitly writes 3G/LTE/5G/Auto over QMI and checks that the modem retained the selected mode.
- **Temperature in Network and signal → Antennas:** show T99 modem and amplifier temperatures, or a 0–3 thermal power-backoff level for devices such as Compal without temperature readings.

## 0.15.0-r133 — 2026-11-02

Bench build: `bench-v0.15.0-r133-7b3f1aa`.

- **T77 USSD in LTE through native AT:** MegaFon could reject MM-mediated USSD only after ~40 seconds; directly use the leased modem AT port for a quicker result. Use MM in 2G/3G or if there is no AT port.
- **USSD Send button:** prevent SMS polling from re-enabling it while a USSD request is pending.
- **Tethered phones and HiLink/UFI diagnostics:** classify their behavior as expected rather than suboptimal.
- **USSD terminated by network:** explicitly suggest checking incoming SMS because some operators send the result there.
- **Proxy failure explanations in bot:** distinguish refused socket, eight-second timeout, proxy rejection and Telegram rejecting the token. When the configured proxy is `127.0.0.1` but podkop binds to the router's LAN address, suggest the correct host.
- **podkop 0.8 mixed proxy:** discover its port from the active router setup even if sing-box configuration is outside `/etc/sing-box`.

## 0.15.0-r132 — 2026-11-02

Bench build: `bench-v0.15.0-r132-374a764`.

- **No SIM is not an operator whitelist:** a SIM-less E3372/HiLink can answer local TCP probes as if it were the Internet, falsely matching whitelist sites. If any check is answered by the modem itself, mark the whole Internet check failed.
- **MegaFon M150-4 / Mobile.Router web:** for modems with passwordless access, read the web without login. If needed, try configured password, then `admin/admin`, then `admin` with no password; distinguish an incorrect saved password from a required login.

## 0.15.0-r131 — 2026-11-02

Bench build: `bench-v0.15.0-r131-0f1ade0`.

- **Audit administrator actions in syslog:** log only action names such as `admin_cmd_status`, `admin_view_journal`, `admin_action_rc` and `admin_settings_answer`, never SMS/USSD content, PINs or proxy links.
- **Full router log export:** bot button and `/logall` return up to 20,000 lines, redacting most phone numbers, long identifiers, proxy URLs (`vless://`, `ss://`, `trojan://` etc.) and bot tokens.
- **Per-modem blocking test:** Telegram Network → Blocking test warns first, then probes services directly over each modem without switching the main router route; classifies unrestricted, partial blocks, whitelist-only or offline from weighted service responses. Service list is not exposed in the result.
- **Stop periodic bot restarts:** an OpenWrt 24.10/25.12 ucode file descriptor leak left one handle open per Telegram request; after ~7.5 hours the bot exhausted descriptors and restarted. Close them correctly.
- **Mobile signal-history axis:** size captions by the longest translated word under footstrap.
- **Mobile modem table:** in expanded rows show each label above its value, with values occupying the full width rather than a narrow right column.

## 0.15.0-r130 — 2026-11-01

Bench build: `bench-v0.15.0-r130-7dc855e`.

- **iPhone/iPad USB identity:** bind tethered Apple devices by USB port rather than serial, because that serial is their personal UDID and must not be stored. Previously identity changes could orphan a modem interface such as `mm2`. After this upgrade, an iPhone appears as a new device once.
- **Phone tethering cards:** replace absent cellular metrics with clear “Phone shares Internet over USB” or “Not sharing Internet” and guidance to enable Personal Hotspot/Trust This Computer on iOS or USB tethering on Android.
- **Broader USB tethering vendor recognition:** add Honor, ASUS, Lenovo, LG and HTC alongside Google, Samsung, Xiaomi, OPPO/Realme/OnePlus, vivo, Motorola and Sony. ASUS/Lenovo require RNDIS evidence because the same vendor IDs occur in laptop modems.

## 0.15.0-r129 — 2026-11-01

Not published separately; included in r130. Router faults, Aurora mobile layout, HiLink E3372 and a ModemMon author report.

- **Bot starts again:** r128's preflight demanded mode 0640 for the token file while r127's installer enforced 0600, causing an endless startup/restart loop on every router. Standardize on 0600 and add a matching test.
- **Detect faults of the router itself** on any board: dead USB controller (`HC died`), failed voltage regulation, CPU/kernel hangs, kernel errors/panics, overheating, undervoltage, OOM and storage failures. Display a Router fault banner with advice; send important bot alerts even in quiet hours. After reboot show what errors terminated the previous boot.
- **USB port relocation:** remove a stale disconnected duplicate modem card once its name/settings follow it into a new port.
- **Mobile layout under Bootstrap/Aurora:** Status → Overview uses one compact card per modem, tabs no longer overlap, the primary uplink is one green block, and signal-graph timestamp labels do not collide.
- **HiLink:** bind names/settings by salted IMEI read from the modem web page to distinguish identical E3372h sticks without saving raw IMEI; add mixed radio priorities 4G→3G, 4G→2G and 3G→2G; expose band, EARFCN, bandwidth and TAC when modified E3372h firmware supplies them.
- **ModemMon field report:** repair ModemManager interfaces incorrectly pointing at a USB hub instead of the modem behind it; suppress an empty LTE-device “5G” placeholder without a signal reading.

## 0.15.0-r128 — 2026-10-31

Bench build: `bench-v0.15.0-r128-ccb88f5`. LuCI polish after testing on R3S/EP06.

- **Consistent signal wording:** strong RSRP but poor SINR (e.g. −84 dBm, −7.4 dB) must be described as “strong signal but severe interference” rather than “excellent signal” with one red bar. Match bot interference thresholds.
- **Dismissible banners:** every top banner has a close button; dismissed ones remain in event history with Show again.
- **Remove disconnected modem:** a Remove from list action explains that the entry is retained until service restart.
- **Who controls the modem:** rename Connect through, move it to the top of connection settings and explain which devices benefit from an external manager (FM350, Quectel) versus generic QMI/MBIM, HiLink and tethered phones.
- **Event history:** Show older action, up to 200 entries per modem instead of 64, with raw engine event under each row in Detailed mode.
- **Signal history:** explain why no chart is drawn when the modem is missing or the service is unavailable.
- **Service status:** process table for engine, bot, ModemManager and external manager showing PID, memory, uptime, daily restarts and engine crashes since boot; bot also reports restarts.
- **Service checks:** add MTS, T2, MegaFon and Beeline sites, reachable during carrier whitelist restrictions.

## 0.15.0-r127 — 2026-10-31

Bench build: `bench-v0.15.0-r127-1c73ef8`. Bot settings in Telegram; no separate Unix user.

- **Bot installs/upgrades no longer signal ubusd:** running under a dedicated user required a permissions reload that unexpectedly restarted ubusd and broke LuCI login in r124. Bot now runs as root, like MedveModem/OpenWrt management services; token/proxy credentials remain root-only.
- **Telegram Settings menu (`/settings`):** choose smart, important-only, all or disabled alerts; enable separate categories (backup devices, MM/third-party manager restarts, quotas, restart summary, new SMS).
- **Quiet hours:** silently deliver notifications without losing them; No Internet and exhausted quota can still be audible unless disabled. Configure by a message such as `23:00-07:00`.
- **Telegram proxy management:** submit `socks5h://user:password@host:port#Name` or a `t.me/socks` link, then enable, disable, test or remove it. Test before use, disable unreachable proxies, delete messages containing credentials immediately and store secrets in protected files. Prevent removal of the bot's last working Telegram route.
- **Bot code split into modules** from a monolithic ~1900-line file without changing user-visible behavior.
- **LuCI bot Settings:** matching alert types, quiet hours, proxy labels and the Allow changing settings from Telegram switch.

## 0.15.0-r126 — 2026-10-31

Bench build: `bench-v0.15.0-r126-217a29b`. Bot audit fixes following r124.

- **Authenticated Podkop/Forkop proxies:** discovered local proxy inputs now use their manager's own credentials, and an explicitly configured proxy at the same host/port is not discarded.
- **Telegram routes in LuCI Overview:** display every actual candidate route including detected proxies; each has a Test action. “Working” means the Telegram Bot API accepted the token, not merely that some HTTP server replied.
- **Setup wizard validation:** Test without proxy bypasses all proxies; arbitrary HTTP 200 is not success; test at most three proxies by priority and label the others untested.
- **Failback to primary route:** polling and sending independently return to their preferred routes. A second empty task result no longer overwrites a successful test.
- **Evidence-based bot status:** report the specific failed proxy and when, or call it a backup route; before a first successful check, label it untested.
- **Modem control outcomes:** done, in progress, done with warning, failed, with accurate current traffic channel when a modem is made first. Start an action after the Telegram acknowledgement is sent instead of a fixed 1.5-second delay.
- **24-hour report:** ongoing outages lasting over a day are not reported as zero incidents; bound event-log memory.
- **Telegram markup:** avoid corrupting long HTML replies midway through a tag or after escaping `&`.
- **Modem card:** show stale-as-of time and disable actions if the engine is unreachable; add SINR/interference text, reachability latency, session uptime, quota reset date, required operator intervention, unknown-reachability white dot and clear Backup line.
- **Failover exclusions:** toggles stay bound to the intended channel.
- **Commands:** `/sms`, `/network`, `/channels`; unify Help/menu metadata and standardize Russian terminology.

## 0.15.0-r125 — 2026-10-30

Bench build: `bench-v0.15.0-r125-d489093`.

- **LuCI login survives bot upgrades:** the bot package's ubusd signal could restart the bus on 25.12 and leave rpcd answering “Object not found” until reboot. Only signal ubusd when its ACL actually changes; explicitly recommend reboot if a restart still occurred.
- **New modem cannot preempt wired WAN:** assigning metric 100 to a new backup conflicted with a WAN also at 100. Place modem after wired WAN (+10), shifting an existing matching interface when needed.
- **Editable channel metrics:** click the metric in any table row, including wired WAN, and apply route order immediately without a full network restart.
- **Huawei E171 incoming SMS:** MM owned all ports and inbound responses disappeared after sending. Reserve the AT port for MedveModem while MM retains control/data, verify CNMI after sends and provide recovery for its small SIM storage (five slots/multipart fragments).
- **Manufacturer QMI drivers** such as `qmi_wwan_q/f/s` for Quectel/Fibocom/SIMCom are recognized as QMI.
- **MM/external-manager lifecycle** now appears in each modem's event history.
- **Bot wording and actions:** Make primary becomes “First among modems” (wired WAN remains above if configured); freeze confirmation warns of connection interruption. Quotas use decimal GB consistently (50 GB no longer displayed as 47).
- **Bot service memory/status:** show engine, MM, external manager and bot memory usage plus daily restart counters.
- **Bot manager restart alerts:** poll both MM and external manager every 30 s, distinguish self/manual restarts from MedveModem-initiated MM restarts; alert on stoppages and show daily restarts/stops in the daily summary.

## 0.15.0-r124 — 2026-10-30

Bench build: `bench-v0.15.0-r124-90a48a7`.

- **Two modems sharing a factory subnet:** MF150-4 and UZ801 both default to `192.168.100.x`, leaving the second with Network unreachable and broken modem web UI. Detect modem-to-modem collisions, not just conflicts with router LAN; report them and relocate MF150-4 to a free subnet such as `192.168.101.1`.
- **HiLink without SIM:** stop displaying stale operator/T2/LTE/signal and a false 2 ms Internet check when E3372 locally answers everything from its No network page. Read SIM state through the web API and disregard unrealistically fast (<4 ms) modem-supplied Internet replies.
- **MF150-4 web forward:** preserve the router host/port across redirects to `status.megafon.ru` and rewrite login cookies appropriately, avoiding an endless Wait screen.
- **Event history:** record modem web login outcome (success/password needed/wrong/locked), subnet conflict and relocation.
- **T77W968/DW5821e receive SMS after sending:** outgoing SMS reset `CNMI` to `0,0`; check after each send and at least every ten minutes while reading, restoring `2,1` via the reserved AT port and journaling the repair.
- **Bot contact names:** forward SMS as “from Mom (+7…)” using MedveModem address-book entries; display names in Inbox too.
- **Bot ordering:** active Internet modem first, then priority order; primary indicated by arrow rather than star. An established session without full Internet is yellow, not green.
- **Proxy diagnostics:** identify by name which Telegram proxy failed and why the bot bypassed it.
- **Detailed channel table:** avoid global warning when another healthy channel provides Internet; highlight active traffic row; replace opaque metric-based “Why” with priority arrows and show explanations only for problems.

## 0.15.0-r123 — 2026-10-30

Bench build: `bench-v0.15.0-r123-c735fc3`.

- **MegaFon MF150-4 radio details:** operator, signal and cell are now available after logging into the modem web UI (the anonymous API returned empty fields). Use the browser-equivalent admin or modem-provided password flow at most once every five minutes.
- **Router-mode modem fields:** display band, TAC/CID, cell map and firmware already exposed by Huawei HiLink/MF150-4/UZ801 web backends; show RSSI even when it is the only available UZ801 signal metric.

## 0.15.0-r122 — 2026-10-30

Bench build: `bench-v0.15.0-r122-cd8b850`.

- **MF150-4 under redirected host:** the modem redirects all direct IP requests to `status.megafon.ru`; use that host consistently when fetching data and forwarding the browser, rather than mistaking the device for UZ801.
- **HiLink status while SMS is being read:** display the most recent cached radio and firmware data rather than an empty “Reading modem…” placeholder while its web interface is busy.

## 0.15.0-r121 — 2026-10-30

Bench build: `bench-v0.15.0-r121-0c0b945`.

- **Avoid engine crash on HiLink USB removal:** a modem unplugged or power-cycled during recovery could crash the daemon and leave USB power off. Restore any port power left off by an interrupted operation on the next start.
- **Modem subnet conflicts with router LAN:** show a prominent warning and journal event when a modem assigns itself an address overlapping the router LAN (e.g. MF150-4 `192.168.1.10` with router `192.168.1.1`). Relocate MF150-4 automatically to a free `192.168.100.1` subnet and restart its interface.
- **MegaFon MF150-4 web backend** (`05c6:f00e`): retrieve RSRP/RSRQ/SINR, cell, PCI, TAC, operator, traffic and versions, read/send/delete SMS without marking them read, perform USSD and trigger recovery reboot, all through an unauthenticated web API.
- **Huawei HiLink recovery:** Radio action reconnects mobile data through the modem web UI without reboot; this can run automatically. Modem web card adds traffic for current day/billing cycle and quota, transmit power, bandwidth/transmission mode, Auto/4G/3G and supported LTE bands, and traffic-counter reset.
- **UZ801 UFI 4G stick** (`05c6:f00e`): web-based operator/signal/firmware, traffic and recovery reboot through throttled `admin/admin` auth; show Web shortcut even if the SIM has no data and the stick supplies no default gateway.
- **OpenWrt Status → Overview:** top line shows Internet state and active modem/wired/tunnel route plus ready backups; highlight carrier row and display interface protocol (QMI/MBIM/NCM/RNDIS/ECM/PPP). Dashboard integration is enabled by default but can be disabled under Modem maintenance.
- **Bot task failure handling:** if router cannot start a send task, log the cause and restart the bot after five minutes instead of remaining indefinitely “task unavailable”.

## 0.15.0-r120 — 2026-10-30

Bench build: `bench-v0.15.0-r120-b7120f5`.

- **Much lower memory usage:** compress signal-history storage; previously the daemon grew by about 1 MB/modem over the first day (five modems 3.9→6.5 MB). Twenty-four hours for five modems now occupy roughly 0.2 MB, without changing charts or UI.
- **SMS alerts independent of data Internet:** send radio SMS to contact-book recipients after sustained whitelist restriction (15 min), total outage (5 min), or failover to another modem/tunnel/adapter, and send a restoration notice with duration. Configurable hysteresis and at most six alerts per hour prevent spam. If the selected modem cannot send, use another one. Include Send test SMS button.
- **HiLink reboot in recovery ladder:** Reboot modem now invokes its web API and waits for it to return; remove the redundant standalone reboot button from the web card.

## 0.15.0-r119 — 2026-10-29

Bench build: `bench-v0.15.0-r119-e3a5c83`.

- **Mobile Status → Overview:** replace four-column modem table that broke IP addresses and labels into tiny fragments with one modem block containing name/state followed by network/address.
- **Mobile home table:** wrap long operator names; keep selection checkbox, number and modem name together.
- **Mobile tabs:** show a right arrow while more tabs are off-screen and scroll the selected tab into view.
- **Touch-friendly controls:** enlarge the rename pencil, Back to all modems, Hide identifiers and short AT tab.

## 0.15.0-r118 — 2026-10-29

Bench build: `bench-v0.15.0-r118-bfbb6e0`.

- **No lost Telegram alerts on hanging proxies:** when podkop/sing-box restarts after accepting but before completing a send, detect that the message was not delivered and retry on another route.
- **Clear red send status promptly:** recover status as soon as Telegram is reachable again, rather than waiting for the next notification or an expanding five-minute retry interval.
- **Deduplicate quota notifications.**
- **ModemManager stopped:** warn about unavailable MM control, not necessarily lost data sessions; already established links may continue.
- **Proxy-only bot mode:** detect newly started proxies every 15 seconds rather than every five minutes.
- **External IP and service checks:** force per-modem direct routing, avoiding router proxies.
- **Bot text:** clearer whitelist, traffic, USB reconnect and connection restored wording.
- **Remove 12 nonfunctional bot settings** such as unused daily/weekly reports and event filters.
- **T99W175 MBIM composition** (`05c6:90d5`): detect exact T99 profile and bind an AT port despite missing ttyUSB ports; do not touch ADB.
- **Router helper false-positive:** stop labeling Orange Pi Zero 3 Wi-Fi `sprdwl-delay` as a modem helper.

## 0.15.0-r117 — 2026-10-28

Bench build: `bench-v0.15.0-r117-474b1b7`.

- **Bot daily statistics:** count outages and total downtime, whitelist periods, MedveModem restarts/unavailability and MM stops/restarts. During the first day show “Since HH:MM”, preserve across bot restarts and reset after router reboot.
- **ModemManager lifecycle alerts:** notify when MM stops and quietly when it restarts or resumes, regardless of who performed the restart.
- **Huawei HiLink details:** Read modem data provides carrier IP, per-session/month/lifetime traffic and durations, firmware/WebUI versions without requesting IMEI/IMSI/number.
- **HiLink USSD:** use modem web API for codes such as `*100#` in the shared USSD card.
- **HiLink reboot:** confirmation-required action interrupting connection for approximately one to two minutes.

## 0.15.0-r116 — 2026-10-28

Bench build: `bench-v0.15.0-r116-02d4835`.

- **Web shortcut on modem cards:** one-click router-forwarded web access for HiLink/UFI.
- **Telegram path correctly labeled:** when podkop intercepts Telegram, show “through router tunnel (sing-box)” in LuCI rather than “direct (no proxy)”.
- **No false foreign-helper alerts:** exclude standard OpenWrt `usb-modeswitch`, `wwan` hotplug and dropbear authorized-keys content.
- **T77W968/DW5821e AT port ownership:** `ttyUSB1` marked “ignored” by MM due to MedveModem's rule is not owned by MM; allow native AT queries, metrics and AT tab.
- **Main card border:** restore visual primary-modem selection under whitelist-only access; color yellow when restricted, green when fully reachable or tunneled.
- **T77W968/DW5821e:** the source changelog repeats the AT-port fix in two entries; the semantics are the same.

## 0.15.0-r115 — 2026-10-28

Bench build: `bench-v0.15.0-r115-6ae1d52`.

- **Cell-lock safety guard:** after ten minutes without registration following a MedveModem cell lock, restore the radio settings from before the lock, not unrestricted factory defaults. Configurable to 5/10/15 minutes or never; journal `Cell lock guard`. Frozen modems are untouched.
- **T77W968/DW5821e cold start:** surface MM's “unusable” timeout/refusal in modem's MM messages, with advice on next steps.

## 0.15.0-r114 — 2026-10-28

Bench build: `bench-v0.15.0-r114-7131216`.

- **Bot layout:** eliminate long separators that look poor in chat previews; split tunnel Internet into “through tunnel” and “Direct: only whitelist”.
- **Whitelist wording:** avoid redundant `WL`/restricted labels on backup channels.
- **Detailed bot event journal:** include why a modem was detected, connection interface, recovery step, unrestricted/whitelist/offline state transitions and traffic handoff; avoid duplicates after daemon restart. Put timestamp/modem above each event body.
- **Bot modem-log export:** `/log` and a button below events produce up to 1000 lines.

## 0.15.0-r113 — 2026-10-28

Bench build: `bench-v0.15.0-r113-3d3cabc`.

- **Event history in LuCI, bot and CLI:** explain device detection causes (service startup, USB hotplug, rescan, disappearance/reappearance, USB port move), reachability transitions and failover decisions.
- **Whitelist plus functioning tunnel:** when podkop restores real Internet through a restricted modem, label it “Whitelist, Internet through tunnel” rather than simply restricted.
- **Modem web controls:** explain missing buttons for frozen or disconnected HiLink/UFI devices.
- **podkop/forkop mixed proxy detection:** find proxies within VPN sections and nonstandard sing-box paths; preserve a temporarily restarting proxy in the candidate list.
- **Bot direct mode under FakeIP:** when podkop intercepts `api.telegram.org` and routes it through sing-box, correctly label the path as router tunnel rather than direct.

## 0.15.0-r112 — 2026-10-28

Bench build: `bench-v0.15.0-r112-5d4e1a2`.

- **Nearby modem tools diagnostic:** detect installed modem managers, sms-tool, 3ginfo, modemband, lite-watchdog, smstools3, USB reset scripts and port-holding applications; explain possible interference without automatically disabling them. Expose the result on homepage, device page, diagnostic report and bot Network page.
- **r108 audit fixes:** validate APN/DNS on takeover; keep old HiLink webproxy available until the replacement starts; never terminate an unrelated process based on stale PID; avoid empty IPv4 address in IPv6-only native mode; find correct IPv6 firewall zone under firewall4.

## 0.15.0-r111 — 2026-10-28

Bench build: `bench-v0.15.0-r111-fb2e3ca`.

- **Bot Network view:** main-route and modem IPs, optionally probe external IP through each channel.
- **podkop/forkop status:** installation/version/enabled state, sing-box status, router DNS tunnel routing and tunnel exit IP.
- **Service checks:** test monitored destinations through a selected modem or primary route, like LuCI.
- **Modem cards:** today's Internet downtime, outage count and whitelist-only duration.

## 0.15.0-r110 — 2026-10-28

No standalone bench build; changes included in r111.

- **Bot Status:** display load averages and bot version; a privacy scrubber previously hid three load numbers as if they were a phone number.
- **Telegram route diagnosis:** if proxies are configured but the bot connects directly, say “proxies not responding”.
- **Whitelist awareness:** separately list restricted links even if another modem supplies unrestricted Internet.

---

**Older source revisions:** [Complete Russian history](CHANGELOG.md). Field-specific evidence and modem zoo material remain untranslated by design.
