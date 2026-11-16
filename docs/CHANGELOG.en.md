# MedveModem — Release History (English)

[Original complete Russian changelog](CHANGELOG.md) · [Documentation](README.md) · [GitHub Releases](https://github.com/Medvedolog/medvemodem/releases)

> [!IMPORTANT]
> **English translation in progress.** This page currently covers the public alpha and source revisions **r197–r150**. The original [Russian CHANGELOG.md](CHANGELOG.md) contains the complete historical record, including all older revisions. This partial English translation must not be used as a replacement for the original release history.

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

---

**Older source revisions:** [Complete Russian history](CHANGELOG.md). Field-specific evidence and modem zoo material remain untranslated by design.
