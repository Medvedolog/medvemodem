# MedveModem — Release History (English)

[Original complete Russian changelog](CHANGELOG.md) · [Documentation](README.md) · [GitHub Releases](https://github.com/Medvedolog/medvemodem/releases)

> [!NOTE]
> Complete English translation of the release-by-release entries from the [original Russian changelog](CHANGELOG.md). Release names, dates, commit IDs and technical identifiers are preserved. Historical entries describe capabilities **as of that version**; consult current documentation for supported features.

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

## 0.15.0-r109 — 2026-10-27

Bench build: `bench-v0.15.0-r109-bf339a8`.

- **Modem downtime today:** modem page shows total time without Internet, count of outages at least 30 seconds, duration of current outage, and time in whitelist-only mode. Startup link activation does not count as an outage. Measurements come from reachability checks, previous day is retained, and daemon restart does not reset the current day's counters.
- **ModemManager port diagnostics:** show interpretable warnings such as control-port ownership loss, cannot open port, MBIM session failed or port disconnected, which may indicate competing umbim/another modem manager. Display as diagnostic hints only, without automatic restarts or retaining raw MM log lines.
- **Dual-SIM slot selection:** show current physical/eSIM slot and a confirmation-required Switch slot action via ModemManager; connectivity typically drops for about one minute.

## 0.15.0-r108 — 2026-10-27

Bench build: `bench-v0.15.0-r108-f93d0e0`.

- **Static libraries for broader firmware ABI compatibility:** build libubox, libubus, libblobmsg-json and libuci into the engine, avoiding failures such as `libubox20260213 (no such package)` across 25.12.x/snapshots/24.10.x. Only json-c remains dynamic among these. Install the appropriate SDK archive, for example `aarch64_cortex-a53-25.12.6` on a matching snapshot.
- **Install prerequisites from router feeds:** refresh `apk`/`opkg` package indexes first so MM, qmi-utils and jsonfilter can be resolved without compiling them in the project SDK.
- **Recognize operator whitelists:** blocked destinations may reply with an operator rejection mistaken for a real server. Treat it as restricted access (WL/БС) in UI, bot and notifications, not full Internet.
- **Failover from restricted modem:** prefer another modem with full Internet. If all channels are restricted, avoid unnecessary switching so VLESS through podkop on a permitted endpoint continues to work.
- **Tunnel reachability detection:** beyond podkop, detect forkop/forkop X, podkop-plus, NetShift mixed proxies (default port 2080), sing-box HomeProxy and clash/mihomo/nikki; support custom `medvemodem.core.tunnel_proxy`, e.g. `socks5h://127.0.0.1:2080`.
- **T77/DW5821e MBIM AT availability:** reserved ports 02/03 (Telit composition `1bc7:1910/1911`: 04/03) are no longer mistaken for non-AT/service interfaces after MM restart. Probe and manual selection include all eligible ports, enabling console, temperature, neighboring cells and cell lock.
- **Lock-cell responsive layout:** Lock button no longer breaks into vertical letters in narrow carrier cards.
- **Native connection choices restricted to tested/provisioned drivers:** show MedveModem direct mode only for FM350/Intel XMM families where the native code supports dialing, with “not yet verified” where applicable; do not offer an unproven path for T77, T99 and other modems. Previously selected unsupported paths appear disabled until changed.
- **Respect pre-existing hand-configured QMI/MBIM/NCM interfaces:** do not let MM and native netifd own one device simultaneously. Show “Controlled by interface lte” and offer explicit Take over: shut down that interface, wait for MM, and adapt the same named UCI interface transactionally, with Restore original option. No automatic takeover.
- **Diagnostic redaction:** mask ICCID with trailing F digits found in umbim logs.
- **SDK:** upgrade OpenWrt 25.12.5 builds to 25.12.6; stop compiling MM, qmi-utils, jsonfilter and uqmi as project dependencies, shortening build time.

## 0.15.0-r107 — 2026-10-27

Bench build: `bench-v0.15.0-r107-fb261ab`.

- **Automatic recovery for field-reported modems:** after reconnection fails to restore registration/Internet, progress through radio restart, soft modem reset and USB reconnect based on profile authorization. Previously these stages required manual action.
- **Strong safeguards remain:** driver rebind and port power cycle are manual; at most three hard attempts per five minutes, six per hour, growing backoff and manual-required after three consecutive failures. Let a modem searching for the network wait up to twelve minutes; do not reset every modem when all channels fail at once.

## 0.15.0-r106 — 2026-10-26

Bench build: `bench-v0.15.0-r106-ec39d7c`.

- **NR signal in MBIM under ModemManager:** MM may report 5G RSRP as −32768. Read NR RSRP, RSRQ and SNR through QMI inside MBIM using the same safe read-only path as antenna metrics.
- **Discard invalid signal sentinels** such as −32768 from cards and history.

## 0.15.0-r105 — 2026-10-26

Bench build: `bench-v0.15.0-r105-80e35cd`.

- **T77 in Telit QMI composition (`1bc7:1910`) auto-binds:** add dynamic driver IDs missing from Linux tables, binding the data interface to `qmi_wwan` and vendor interfaces 2–4 to a serial driver for AT/SMS.
- **T77 `1bc7:1911` and Dell `413c:81d8`:** dynamically bind serial drivers to vendor-specific ports except ADB/data, using `new_id … ff`.
- **Rescue accidentally claimed data interface:** undo serial-driver binding when it captured the network interface.
- **Field-reported driver recipes:** permit automatic dynamic binding based on the hardware catalog, rather than only test-bench-validated recipes.

## 0.15.0-r104 — 2026-10-26

Bench build: `bench-v0.15.0-r104-b5781f5`.

- **T77/DW5821e AT tab restored:** devices without USB serial had not received generated MM port rules, allowing MM to claim AT ports. Generate the rule by VID:PID, regain reserved AT port once an MM session is down; takes effect after restarting MM/router.
- **Router-forwarded modem web UI:** rewrite modem redirects from `192.168.8.1` to a router-side webproxy endpoint so clients with aggressive proxy routing can open HiLink/ZTE pages.
- **T77 USSD LTE warning:** test showed a request caused 3G fallback, failed USSD and ~90 s Internet outage; warn before sending.
- **SMS layout:** name above number in conversation list; stable timestamp and address-book columns.
- **Channel metric heading:** shorten to “Metric”; show configured/kernel metric explanation in tooltip.

## 0.15.0-r103 — 2026-10-26

Bench build: `bench-v0.15.0-r103-5341aa0`.

- **T99W175 MM-owned cell lock:** add Lock actions to serving/neighboring LTE cells, using native `AT^LTE_LOCK` through a reserved modem AT port. Requires roughly a minute-long modem restart; restore previous lock if Internet does not return. Network mode and bands remain managed by MM.
- **Neighbor PCI encoding:** convert hex PCI such as `1CF` to decimal `463`.
- **Per-carrier channel frequency:** show `EARFCN 2850 (DL 2630.0 / UL 2510.0 MHz)` in Detailed mode.
- **Cell on map:** open 4cells.ru by LTE eNB or 3G cell/area; mask the link with private identifiers.

## 0.15.0-r102 — 2026-10-26

Bench build: `bench-v0.15.0-r102-9623f2a`. r101 audit corrections:

- **Bot restart quiet-period accounting:** an outage beginning during the two-minute suppression is still announced afterward. Repeated failures before restart do not falsely become a flapping alarm. Preserve the start timestamp for prolonged downtime.
- **Critical-only alerts:** restrict to total router Internet loss and exhausted traffic quota, not an individual backup modem failure.
- **Consistent bot localization:** Connected/Restored terminology, quota names and recovery reason; only suggest cables/USB hub power when the device actually disappeared from USB. Move failover exclusions to Channels instead of the main menu.
- **Stale reachability presentation:** compact “Internet · 57 ms”; results older than 3 minutes (11 for backup) appear grey and “Data stale”, not green.
- **USB diagnostics:** complain about missing drivers only when a modem really expects one, name the required `kmod-*` package; an unused ADB interface is not a fault.
- **VCF address book:** support Windows-1251 contacts and name-component order; enforce 20 entries and duplicates across concurrent tabs, block writes when the book cannot be read instead of wiping it.
- **SMS archive durability:** synchronize directory changes before deleting an archived modem SMS, preventing loss on power failure.
- **LuCI wording:** ~40 audit glossary corrections, avoid asserting unproven causes. Make admin binding guidance point to Setup wizard → Administrator; clarify that wired priority is preserved by Make primary.
- **USB port numbering:** explain controller-order numbering may differ from the case; bound service-card and journal lengths.

## 0.15.0-r101 — 2026-10-26

Bench build: `bench-v0.15.0-r101-4298809`.

- **One bot alert per situation instead of repeating engine events:** omit spammy Session up/down/Recovery started lines; include whether another route supplies Internet and whether repair is underway. On recovery, send a silent summary with outage duration and successful recovery step.
- **Backup modem policy:** do not disturb the owner while another channel works, except summarize frequent physical outages or downtime over ten minutes.
- **Restart summary:** after first installation or daemon restart, issue one silent summary of engine version, modem count and active route rather than multiple connection notifications.
- **Friendly names:** use exact model labels, disambiguate identical modems by USB port and avoid internal “Enabled / QMI Composition” descriptions.
- **Critical-only mode:** no Internet and exhausted quota only.
- **Per-carrier display:** labeled carrier bands and frequencies, with RSSI in Detailed mode; other radio and device detail refinements.
- **Address-book and USSD improvements:** contact names in conversations, compose from contacts, exclude nonfunctional FM350 USSD while using a meaningful immediate error in CLI, and add router summary to bot Status.
- **UI corrections:** preserve Rx/Tx arrows beside values, localize USB event details, ensure a radio-change Undo result does not remain indefinitely after reboot.

## 0.15.0-r100 — 2026-10-26

Bench build: `bench-v0.15.0-r100-35c7edb`.

- **Basic mode without engineering clutter:** hide IPv6/gateway/DNS details; present human-readable reachability latency; show USB/health debug only on problems; use descriptive signal wording rather than raw dBm while keeping scales; offer only Reconnect Internet and Reboot modem recovery buttons plus short help for SMS/report/freeze.
- **Advanced-only IP/DNS choices:** connection owner and dial backend remain available in Basic.
- **Signal history in Basic mode:** single signal-strength plot without advanced view/table selectors.
- **Basic Ports and devices:** explain router, USB port order, hubs and modem paths in plain language with actionable cable/power warnings; compact mobile tree.
- **Mobile status banner:** compact header with folding arrow; render channel table as name/value blocks rather than eight overflowing columns.
- **Localization:** remove English fallback “no answer” from connectivity errors.

## 0.15.0-r99 — 2026-10-25

Bench build: `bench-v0.15.0-r99-1b793dd`.

- **ModemManager/qmi-utils dependency installation:** clean R3S routers previously lacked packages to discover QMI/MBIM devices.
- **Post-install/restart:** restart engine/rpcd and clear LuCI caches so new packages work immediately; the user may need to log into LuCI again.
- **`medvemodem version`:** show CLI and running daemon versions.
- **Human-readable channel names:** wired WAN, PPPoE, tunnel `tun0`; clearer traffic and censorship-bypass terminology.
- **Preserve wired WAN priority on manually connecting a modem:** avoid giving the new modem metric 0 and stealing the default route; initial modem metric 100 until configured otherwise.
- **Connection-card labels and modem ownership explanations** updated to avoid jargon.
- **Ports and devices tree:** diagram-style lines, badges for owner/protocol/driver/path/network/AT/speed; dotted empty USB ports.
- **Status banner collapses in Detailed mode** as well.
- **Settings regrouped:** rename Recovery tab to Modems and Automatic recovery panel to Modem maintenance.

## 0.15.0-r98 — 2026-10-24

Bench build: `bench-v0.15.0-r98-bdb930d`.

- **Experimental native dial for Fibocom L850/L860 (Intel XMM)** (`8087:095a`, `2cb7:000b`) where MM falls back to PPP without an IP address. Must be selected manually under Connection → MedveModem direct; not yet tested on the project's physical modems. MM remains the default owner.
- **Router SMS archive switch** added under Settings → Notifications.

## 0.15.0-r97 — 2026-10-24

Bench build: `bench-v0.15.0-r97-e6edad8`.

- **Avoid full modem SMS storage:** when SIM/modem memory reaches ~75%, copy oldest read incoming SMS to router storage (retain last 500 per modem), then remove them from modem storage to keep incoming messages flowing. Show archived entries in the SMS tab and allow deletion there. Applies to AT/MM-backed SMS; switch follows in the next release. On r97 use `uci set medvemodem.core.sms_archive=0` to disable it.

## 0.15.0-r96 — 2026-10-24

Bench build: `bench-v0.15.0-r96-8ba5214`.

- **Bot recognizes Internet through tunnel under whitelists:** show a green active Internet state and latency when podkop/other tunnel works on a restricted uplink rather than generic restricted access.
- **DW5821e/T77W968 CNMI repair:** restore disabled incoming-SMS storage if modem spontaneously sets `CNMI=0,0`.
- **ZTE web variant `reqproc`:** read signal and SMS on more firmware families.
- **FM350 neighboring cells under external manager:** read via that manager.
- **Mobile registration parser:** accept whitespace within `+CEREG: 2, 1, …` replies.

## 0.15.0-r95 — 2026-10-24

Bench build: `bench-v0.15.0-r95-4f326d5`, including r94.

- **ZTE router-mode SMS:** MF79/MF833 and similar can read/send/delete SMS through modem web API, like HiLink.
- **SMS under an external modem manager:** e.g. FM350 supports reading/sending via that manager, combining multipart messages and deleting a complete long SMS rather than individual segments.

## 0.15.0-r94 — 2026-10-24

No standalone build: x86_64 infrastructure failure, included in r95.

- **Cell locking:** Lock/Unlock on serving and neighboring LTE/5G cells from Network and signal. FM350 LTE lock uses an airplane-mode interval of ~10–60 s; T99W175 LTE/5G lock may reboot the modem for roughly one minute. Auto-rollback if Internet does not return.
- **FM350 native radio control without external manager:** apply RAT/band selection through native AT and reconnect the session; “all bands” restores the modem's default set and carrier aggregation. Previously IP persisted after band changes while real connectivity failed.
- **FM350 neighboring cells:** show serving/neighboring cells with signal and support locking to them.
- **HiLink E3372 SMS:** read/send/delete through modem web, forward new messages through bot/ntfy/webhook; explain if web password is needed.

## 0.15.0-r93 — 2026-10-24

Bench build: `bench-v0.15.0-r93-e9fdfba`, first bot-archive release since r89.

- **Internet works through tunnel while operator whitelists direct traffic:** show green “Internet through tunnel” and latency instead of yellow restricted access when podkop VLESS (reachable endpoint) or another tunnel carries unrestricted traffic. Probe tunnel through its Clash API, sing-box/xray local proxy or a more expensive `sing-box tools fetch` fallback (at most every five minutes).
- **Firewall-interference check includes podkop** even without a mixed proxy; do not reset a modem just because direct checks fail while the tunnel works.

## 0.15.0-r92 — 2026-10-23

Bench build: `bench-v0.15.0-r92-3b9ab68`. No bot artifacts due to OpenWrt package server HTTP 502.

- **Custom DNS supplements operator DNS by default**, rather than replacing it. External DNS (8.8.8.8/1.1.1.1) may be blocked under carrier whitelists, breaking resolution if carrier DNS is removed. Optional Replace operator DNS remains available. If podkop or another tunnel owns router DNS, explain that modem DNS will not be used for names.
- **Services on Internet restoration:** select podkop, zapret, passwall and others to restart on modem connectivity following boot or outages of a minute or more. Whitelist-only channels count as suitable for tunnels. Avoid waiting ten minutes for tunnel Internet after reboot.
- **Subnet routes follow default-route metric changes** on failover/priority edits, instead of retaining old metrics until interface restart.
- **Priority metrics changed to 100/110/120…** from 10/20/30 so tunnels such as WireGuard/AmneziaWG with metrics below 100 retain precedence. Apply after the next priority operation.

## 0.15.0-r91 — 2026-10-23

Bench build: `bench-v0.15.0-r91-190febe`. No bot artifacts due to OpenWrt server HTTP 502.

- **Wired WAN in channel priorities:** assign positions to ethernet/PPPoE and modems; Make primary on a modem no longer shifts the wired channel.
- **Conntrack cleanup on failover:** when traffic moves, drop stale flows on the previous uplink so they immediately reconnect on the new one. With `conntrack` installed, flush only moved channel flows; otherwise the whole conntrack table (as in mwan3). Configurable off.
- **User event hooks** under `/etc/hotplug.d/medvemodem/` are invoked on failover, route changes, session up/down, recovery and USB presence changes, with `ACTION`, `DEVICE`, `DEVICE_ID`, `STATE`, etc.
- **Manual modem DNS:** up to four DNS addresses configured per modem under Connection; in this revision they replaced operator DNS.

## 0.15.0-r90 — 2026-10-23

Bench build: `bench-v0.15.0-r90-f9197af`. No bot artifacts due to OpenWrt server HTTP 502.

- **FM350 carrier aggregation:** both with/without an external modem manager, show CA groups such as `B3+B7, 30 MHz` and detailed carrier bands/bandwidth, including 5G carrier where applicable. Previously only the primary band was available.

## 0.15.0-r89 — 2026-10-23

Bench build: `bench-v0.15.0-r89-2503d71`.

- **Settings opens on the Service tab:** engine status, versions, memory, CPU load and start/stop controls apply immediately without a Save button.
- **Custom USB port names:** name a physical port under Ports and devices; the label remains even when unplugged.
- **Carrier aggregation beyond QMI:** include MM serving cells, NSA 5G and HiLink/ZTE web data in homepage modem cards.

## 0.15.0-r88 — 2026-10-23

Bench build: `bench-v0.15.0-r88-bbc800b`.

- **Bilingual Telegram bot:** use the administrator's Telegram language (Russian until their first message) or explicit LuCI bot-language setting. Old chat keyboards continue to work after switching.
- **Localized AT/USSD presets, installer logs and refusal reasons:** use the LuCI user's selected language when starting operations; ntfy forwarded-SMS headings use the language of the configuring page.
- **Bot traffic formatting:** retain one decimal place (4.8 MB, not 4.0).

## 0.15.0-r87 — 2026-10-23

Bench build: `bench-v0.15.0-r87-d096bf4`.

- **Bilingual LuCI:** all MedveModem and bot pages authored in English with a complete Russian translation catalog, selected by the LuCI system language. Add Russian to LuCI language selection even on routers lacking its package when possible.
- **HiLink signal sampling:** update every 30 seconds instead of once a minute.
- **Signal-history safety:** handle very long RAT lists without writing beyond allocated buffers.

## 0.15.0-r86 — 2026-10-23

Bench build: `bench-v0.15.0-r86-29d7e7f`.

- **Two UI detail levels:** Basic and Detailed replace Basic/Advanced/Expert. Detailed unifies signal values, raw log, AT console with presets/manual command, AT port choice, USSD, temperature, USB device tree and channel table. Existing Advanced/Expert preferences map to Detailed.

## 0.15.0-r85 — 2026-10-23

Bench build: `bench-v0.15.0-r85-788652f`.

- **Automatic new-SMS notifications:** optional Send new SMS (requires Show SMS) forwards each incoming message with sender, time, modem/SIM, text and Code/Copy button; leaves modem SMS unread and does not persist copies in the bot.
- **Bot LuCI divided into Overview, Setup wizard and Settings:** process controls/tests, step-based wizard with Back/Next and auto-resume at the first incomplete stage, grouped configuration and collapsed Danger zone.
- **Proxy setup avoids duplicates:** reuse an existing endpoint and show Saved — check proxy rather than falsely claiming untested.
- **r83 queue regression fixed:** restart during a pending send no longer discards the rest of the outgoing queue.

## 0.15.0-r84 — 2026-10-22

Bench build: `bench-v0.15.0-r84-f2fff7e`.

- **Ports and devices engineering panel:** collapsible USB tree (controller, hubs, ports, modems and other devices), empty/occupied port markers, modem owner/protocol/drivers/netdev/AT/USB speed; warn when USB3 modem runs at 480 Mbps.
- **USB fault history:** detect hourly reconnection, resets, errors −71/−110, overcurrent and NETDEV WATCHDOG; display recently detached devices as dotted outlines in their old ports, and contextual messages for hub loss, new bus number and missing data driver. Read-only: these observations do not modify USB.
- **Detailed channel table:** priority/route metrics from config vs kernel, last reachability probe and reason for being active or demoted.
- **Optional diagnostics:** install `usbutils`/`ethtool`; do not poll install logs when the browser tab is hidden.

## 0.15.0-r83 — 2026-10-22

Bench build: `bench-v0.15.0-r83-bedadbf`. Regression fixes reported after r82:

- **Prevent SIGSEGV on USB hotplug while external modem manager edits configuration:** previously the C daemon read freed manager data.
- **Make primary actually moves the route:** setting priority alone did not replace competing kernel routes with equal metrics. Apply live route changes, wait for traffic handoff and explicitly show “first in priority, traffic still via …” while pending.
- **Bot transport:** busy is shown as “retrying” rather than dead; retry messages that might have been lost by a proxy, shorten long polling through unstable proxies and send menu after startup.
- **HiLink/native cached metrics:** show Loading instead of empty values; retain readings up to ten minutes with age label. Refresh HiLink web signal every 30 seconds rather than five minutes for continuous history.
- **Distinct LuCI loading states:** loading, unknown modem and unavailable service are not red errors on startup.
- **E3372-325 (`12d1:155e`) exact HiLink profile**, persistent model identification.
- **Preserve Frozen state** across USB port moves and show it on disconnected cards.
- **Recovery result accuracy:** connected session without working Internet is not “Internet recovered”.
- **Avoid repeated interface repairs** and avoid offering older external-manager versions as upgrades.
- **Bot proxy wizard:** enable a proxy only after successful verification.
- **Mobile mode selector, status banner and warnings:** fix overflow, shorten headline and preserve appropriate warning colors.
- **Privacy/security:** do not pass SMS forwarding tokens in process arguments; handle proxy passwords containing newlines safely; validate external manager helper arguments.

## 0.15.0-r82 — 2026-10-22

Bench build: `bench-v0.15.0-r82-86c7708`.

- **External modem manager pinned stable 3.3.0 (2026-10-19), packaged as `3.3.0_p20261019`:** on startup it no longer restarts the entire network to handle a missing modem, never opens undefined AT ports, and can `reconfigure` one modem without disturbing others.
- **Replace hardcoded AliDNS/DNSPod resolver defaults** `223.5.5.5` and `119.29.29.29` with Yandex `77.88.8.8` and Cloudflare `1.1.1.1`, including IPv6.
- **FM350 gateway workaround:** an external manager guessed `x.x.x.1` as a gateway even though FM350 does not answer, hanging RNDIS with NETDEV WATCHDOG and USB resets. The project package uses an on-link default route directly to the FM350 interface, matching stable native mode.
- **Upgrade external manager** through Settings while keeping its modem sections and preferences.
- **Per-modem change isolation:** enable/disable dial and change interface name through a single-modem reconfigure, without touching others.
- **Native FM350 remains the default,** external manager optional with the routing fix.

## 0.15.0-r81 — 2026-10-22

Bench build: `bench-v0.15.0-r81-0c4e5be`.

- **FM350 native connection default:** avoids an external manager's invalid x.x.x.1 gateway and recurrent RNDIS hangs/USB resets; native on-link route remains stable with low CPU overhead.
- **SMS forwarding injection hardening:** refuse URLs/tokens containing CR/LF or control characters before they reach root-run curl configuration; previously a crafted ubus value could inject curl directives such as `upload-file` and disclose router files.
- **Interface repair reason** appears in journal; note that some repairs refresh networking and should be minimized.
- **SMS storage mismatch warning** when new messages land in a different store than the selected inbox.
- **Native FM350 retains SMS/AT ownership** even when an external manager independently creates a configuration section; report it as a conflict, not takeover.
- **Router-forwarded modem web exposure:** open web access on demand for 15 minutes (`core.web_forward_minutes`; zero means always), retry nftables rules after failure and warn about two HiLink devices sharing `192.168.8.1`.
- **Store SMS forwarding token separately** in a 0600 file and show only Saved in LuCI.
- **Expire SMS-seen history** for devices absent 90 days; cap at 32 devices.
- **AT console validates each semicolon-separated command** to prevent dangerous secondary commands; wait one second when a leased port preempts another operation, avoiding stale replies.
- **HiLink web parser hardening** and cleanup of caches for missing devices.

## 0.15.0-r80 — 2026-10-21

Bench build: `bench-v0.15.0-r80-ac0cf48`.

- **Background web polling of router-mode HiLink/ZTE modems:** read every five minutes even when LuCI is closed to avoid holes in signal history; do not read frozen modems.
- **Preserve external manager ownership through FM350 reboot:** after USB reset FM350 can spend roughly a minute in MediaTek bootloader mode. Previously the engine considered the external-manager section orphaned, disabled it and removed interface `mm3`. Retain the delegated modem for ten minutes after disappearance and reactivate its external-manager section if that manager disabled it after USB removal.
- **Find qmicli reliably** through absolute executable paths rather than relying on shell lookup.
- **Temperature history chart:** aggregate into ten-minute blocks (maximum within each block), render filled area, preserve honest gaps while page was not measuring; hover shows time interval and temperature.
- **Dangerous manager restart control** is now an outlined red button, not a link among ordinary blue actions.
- **Bot setup step 1:** Saved-token masked display stays above input and no longer shifts button layout.
- **Modem table signal units:** use RSRP dBm consistently where available, percentage only without RSRP.
- **OpenWrt Status → Overview** gains operator, technology and signal bars for Huawei HiLink and externally managed modems.
- **Bot startup regression fixed:** the `why` preflight helper was missing from bot packages, preventing bot startup since r77; include it and check packaging in tests.

## 0.15.0-r79 — 2026-10-21

Bench build: `bench-v0.15.0-r79-9ffffc2`.

- **Redesigned signal history:** filled area chart with Signal/Interference and quality/Network technology selector, qualitative y-axis labels, RAT transitions such as “4G → 5G” and a single hover card; gaps represent missing readings.
- **Reject invalid signal sentinel** −32768 from history/scales, preventing T99 plots from collapsing into grey fill.
- **HiLink signal chart restored**, previously hidden due to a never-fetched empty cache.
- **Keep cached HiLink/FM350 metrics visible** when MM answers slowly; show RSRP dBm if percentage is absent.
- **Long table warnings** wrap into two lines with ellipsis.
- **AT port probe** shows progress and result inline.
- **Repair for external-manager modems** enables their dial connection/configuration rather than only restarting the service.

## 0.15.0-r78 — 2026-10-21

Bench build: `bench-v0.15.0-r78-c9b282a`.

- **Partially known modems visibly marked:** family/generic/multiple matches/no profile display an informational banner/card line plus Generate report and GitHub New modem issue actions; not an error warning.
- **12-hour temperature chart** with time/temperature axes, labeled minimum/maximum and hover details; no samples while the page was closed.
- **Freeze operations are placed last** in page, bulk actions, menus, bot and TUI to reflect severity.
- **Developer profile report** includes model, firmware and USB mode answers only when MedveModem legitimately owns the AT port; never probe an MM/external-managed or frozen modem.
- **CLI:** `medvemodem tui` aliases `top`; keys `z` for Freeze and `x` for failover exclusions.
- **Profiles:** record match quality `fit` (vendor family vs generic).

## 0.15.0-r77 — 2026-10-20–21

Bench builds: `bench-v0.15.0-r77-ef4b549`, `…-e51543e`, `…-fc4aaba`.

- **Freeze really isolates the modem:** prevent external-manager dial and new MM ownership after restarts/USB re-enumeration; revoke returning ports, skip MM-backed SMS and HiLink web reads.
- **Reload external modem manager without disrupting all links:** Repair/Restart use reload; full restart is an explicit Emergency action with warning.
- **Make primary changes routes live** instead of restarting all netifd interfaces.
- **Recovery loop guard:** after one failed retry remember the failure for 15 minutes and request manual intervention instead of reconnecting UFI/FM350 every 2–3 minutes.
- **NETDEV WATCHDOG escalation:** on stuck USB network driver, try USB reset after session reconnect, including externally managed modems; advise checking autosuspend, cable and hub power.
- **Disable modem USB autosuspend by default**; `core.usb_autosuspend=1` restores kernel behavior.
- **MM session vs data mismatch:** avoid repeated radio toggles after false connected state.
- **FM350 owner/AT repair and history:** repair foreign AT-port claims, record readings through external manager and AT.
- **Brovi E3372-325 recognized as HiLink**, with protection against sending credentials to unrelated web servers.
- **Daemon last-shutdown reason** logged (signal/clean stop/unclean SIGKILL/OOM).
- **LuCI:** full card/table view, multi-select/bulk modem actions, inline recovery notifications, background work for long operations, cached signal readings and reliable HiLink link.
- **Telegram bot remote control** behind an off-by-default permission and confirmations; buttons expire after 30 minutes. HTML menus/modem paging, network restriction handling, short proxy retry, 25 s long poll, periodic preferred-route probes, SMS inbox, log export, failover exclusions, and improved startup errors.
- **Package documentation:** generated mockups of UI/bot menus from source.

## 0.15.0-r76 — 2026-10-20

Bench build: `bench-v0.15.0-r76-6096c8f`.

- **Router-mode interface follows its USB device** after a port swap, repairing `usb0`/`usb1` renumbering and releasing orphaned interfaces (`iface-repair`).
- **Fix repeated +10000 metric growth** when a device has multiple default routes.
- **FM350 external manager AT-port repair** automatically fixes a stale foreign port and restarts only that modem's dial service; Repair button triggers immediately.
- **Manage external modem manager separately:** service settings have Start/Restart, and Repair starts its stopped dial subsystem.
- **Homepage:** green outline for active primary channel, equal-height cards, expand further issues, and Cards/Table toggle ordered by priority.

## 0.15.0-r75 — 2026-10-20

Bench build: `bench-v0.15.0-r75-ae30f05`.

- **LuCI package version mismatch refresh:** detect stale browser-cached JS after upgrade and reload once with new assets; avoids incorrect E3372 empty-field pages until incognito was used.
- **Signal bar fix** for HiLink, external-manager and AT devices; remove superfluous dash.
- **HiLink UI:** hide unsupported native Network mode/bands, keep signal history populated via modem web.
- **Alpha 1 feature scope approved:** bot, SMS forwarding, router-forwarded modem web and functionality introduced in r63–r75.

## 0.15.0-r74 — 2026-10-20

No standalone bench release.

- **HiLink/UFI settings follow a moved USB modem** by unique USB product/vendor/VID:PID when only one such device is present. Do not migrate ambiguous identical units.
- **Diagnostics → Device:** show USB ID, manufacturer/product, USB serial or an explanation of port identity; Expert mode includes interface drivers.
- **HiLink radio options** point users to the modem's own web UI rather than unsupported native controls.

## 0.15.0-r73 — 2026-10-20

No standalone bench release.

- **Clear stale “install qmi-utils to see antennas” warning** after two minutes when the package/qmicli is already available but no new antenna reading arrived.

## 0.15.0-r72 — 2026-10-20

No standalone bench release.

- **Router-side modem web access** for HiLink/UFI under a client VPN/TUN proxy: each device gets a LAN-only router port (8081–8099); browser accesses that endpoint instead of the modem's tunneled private IP. Own nftables table `medvemodem_web` does not alter fw4, removed on service shutdown. Enabled by default, configurable from Modem maintenance.

## 0.15.0-r71 — 2026-10-20

No standalone bench release.

- **Homepage signal/operator** for HiLink, externally managed and native AT modems, reading at most once a minute (external manager every two minutes).
- **Compact Internet strip** uses card names, and frozen/user-disabled modems are not counted as backup faults.
- **HiLink router-mode is expected:** no warning or Fix button, with clear owner label.
- **DNS interception checks distinguish local router/proxy responses** faster than possible modem RTT; do not incorrectly blame the carrier.
- **Serialize package installation:** concurrent installers cannot both start.

## 0.15.0-r70 — 2026-10-20

No standalone bench release.

- **Make primary works with metric-zero devices:** a modem interface lacking explicit metric defaults to 0 and may outrank configured priorities; explicitly assign a metric when promoting it.
- **Signal card and qmicli lookup:** remove placeholder dashes; search executable across PATH.
- **FM350 external-manager connection service:** warn and offer Repair when its dial daemon is stopped; service view distinguishes dial and discovery subsystems.

## 0.15.0-r69 — 2026-10-20

No separate build (failed CI; included in r70).

- **Bot token can be saved:** standard OpenWrt jq lacks regex support and incorrectly rejected valid tokens. Rework the bot setup page as Token → Administrator → Start → Proxy with state lights, Check token/Check proxy actions and one Enable and start button.
- **LuCI service view:** highlight the active Internet modem in green; one Service panel for MedveModem, MM and external manager shows state, versions, memory and Start/Stop/Restart. Remove a false version-mismatch warning from homepage.
- **MM handover retry bounded:** after three failed attempts to release an FM350 used by another manager, wait until MM rediscovers it instead of repeating every five minutes.
- **Suppress repetitive foreign-port log noise** caused by the same external `tom_modem` command changing PID on each run.

## 0.15.0-r68 — 2026-10-20

Bench build: `bench-v0.15.0-r68-6801c62`.

- **Telegram bot can use any connected modem:** if the primary uplink cannot reach Telegram due to whitelist restrictions or outage, test healthy backup modems by binding only bot requests to their interfaces. Try the next path after about three seconds, without moving the router's default route and with normal TLS verification.
- **Bot routing preference:** show “Telegram: via modem …”, and add the enabled-by-default **Through other modems** option in LuCI. No C engine changes.

## 0.15.0-r67 — 2026-10-20

No standalone bench release.

- **SIM identity in Telegram bot:** show the SIM label and number from LuCI or, if absent, the number reported by MM. Other phone numbers remain hidden; never show IMEI/IMSI/ICCID or log the number. Engine unchanged.

## 0.15.0-r66 — 2026-10-20

Bench build: `bench-v0.15.0-r66-8cf0254`.

- **Bot packaged with every bench build:** separate `medvemodem-bot-…-25.12.tar.gz` APK and `…-24.10.tar.gz` IPK archives, architecture-independent. Merge bot branch into main source.
- **Bot reports operator whitelist-only access separately** from complete Internet loss and full recovery. Show restricted, not offline, on channel/modem cards.
- No engine changes since r65.

## 0.15.0-r65 — 2026-10-20

Bench build: `bench-v0.15.0-r65-63701c8`.

- **Valgrind review of whole C service:** eliminate memory leaks and avoid unnecessary loops.
- **MM release refusal / SIM polling:** stop asking at every refresh; retry failed SIM reads with backoff from one to thirty minutes instead of every 18 seconds, fixing a leak on each attempt.
- **Lower CPU/memory churn:** avoid rebuilding full modem descriptions during every five-second reachability tick, drop unplugged-device data, and avoid repeatedly writing failed MM rules.
- **Timeout stuck external-manager restarts,** notice vanished AT ports quickly, start SMS forwarding only when enabled, and continue the queue after individual send failures.
- **Rate-limit netifd and LuCI retries** when service is unavailable, including hidden browser tabs.

## 0.15.0-r64 — 2026-10-19

Bench build: `bench-v0.15.0-r64-af2088a`. Includes unpublished r63.

- **Read SIM identity without ModemManager:** for externally managed or native modems, query IMSI/ICCID via the external manager's `sim_info`/`send_at` or the engine's own leased AT port, once at connection and every 30 minutes. Enable SIM labels and per-SIM quota accounting. Do not log or persist raw numbers.
- **Respect foreign port ownership:** never directly access the external manager's control port.

## 0.15.0-r63 — 2026-10-19

Not released separately; included in r64.

- **Assign new modems a safe route metric** immediately after existing channels (+10), avoiding default-route theft by UFI devices with metric 0.
- **Freeze stops external-manager dialing** and restores its prior setting on unfreeze.
- **Forward incoming SMS to ntfy or webhook**, with correct escaping for quoted text and no phone number/body in logs.
- **Temperature:** poll every two minutes, show on homepage and a 12-hour min/max plot; carrier aggregation appears in Expert radio details and backup channels receive numbers/colors.
- **USB composition labels:** show number, QMI/MBIM function and model database name. Service status reports PID/memory/daily load.
- **Assisted packages:** qmi-utils/qmicli, ipheth+usbmuxd; uninstall external manager with log and correctly recognize usb-modeswitch already installed.
- **Devices:** UFI `05c6:f00e` router-mode, Android USB RNDIS and iPhone ipheth; suppress APN/owner settings on phone/HiLink. FM350 external-manager radio metrics through `cell_info`.
- **Code audit:** fix races/cache invalidation, prevent hung netifd from blocking ubus status, remove histories when device disappears.

## 0.15.0-r62 — 2026-10-19

Bench build: `bench-v0.15.0-r62-7080699`.

- **Large CPU reduction through caching:** MM modem refresh previously reparsed all hardware profiles (~19,000 file opens in ten seconds), consuming ~57% of one CPU core. Cache profile directory/catalog and selected profile until they change; bench measured `medvemodemd` at under 0.7% CPU afterward.
- **Lower flash writes:** persist SIM traffic counters every 30 minutes, not ten, and on service shutdown.
- **CI reliability:** retry artifact uploads after runner network interruptions.

## 0.15.0-r61 — 2026-10-19

No standalone bench release.

- **FM350 fixed AT interface:** profile defines interface 6, allowing discovery independent of `ttyUSB` numbering and without probing.
- **Native non-MM signal reading:** use AT every 30 seconds to show operator, RAT, RSRP/RSRQ/SINR for FM350 and directly managed QMI/MBIM devices.

## 0.15.0-r60 — 2026-10-19

Bench build: `bench-v0.15.0-r60-e5ad80c`.

- **Prevent a foreign manager using another modem's AT port:** stale FM350 manager config pointed at T99 `ttyUSB5`. Before invoking `tom_modem`, verify the AT port physically belongs to that modem; otherwise refuse and show a conflict.
- **Fast page loads:** respond immediately with MM readings younger than two minutes instead of holding requests for up to two seconds; refresh asynchronously.
- **USB configurations:** show protocols (QMI/MBIM/RNDIS/NCM), COM-port counts and USB IDs from descriptors.
- **OpenWrt Overview formatting:** proportional font, aligned badges and 360px fit.

## 0.15.0-r59 — 2026-10-19

Bench build: `bench-v0.15.0-r59-6821155`.

- **Native FM350 takeover really releases the external manager:** Save and connect asks the manager to release the modem before MedveModem owns SMS/AT/recovery. Do not destroy a user-created external-manager section; report that it must be removed through that manager.
- **Freeze modem:** stop native polling/connect/recovery, release MM, disconnect session, persist state across reboot; CLI `medvemodem freeze <id> on|off`.
- **Global service switch:** optionally stop/start MM and external manager alongside MedveModem, restoring only those stopped by this action.
- **Reduce logs:** suppress repetitive “MM runs but no modem object” for phones/routers/unmanaged devices; rate-limit failed inhibit messages to five minutes.
- **Immediate cached UI:** show last known values first, refresh later, suspend hidden tabs and slow journal polling to 15 s. Avoid flickering signal bars; collapsed cards show concise state.

## 0.15.0-r58 — 2026-10-19

Bench build: `bench-v0.15.0-r58-57ee437`.

- **Installed driver/component versions in LuCI:** read MM, usb-modeswitch and kernel package versions directly from APK/opkg package DB without invoking the manager.
- **Driver state:** distinguish installed-and-loaded from installed-but-not-loaded.
- **Homepage modem card:** display actual IPv4 address rather than a generic address-obtained status.

## 0.15.0-r57 — 2026-10-19

Bench build: `bench-v0.15.0-r57-1a8a4db`.

- **OpenWrt Status → Overview IP address:** fix lookup in netifd, show real connected-modem address and respect Hide identifiers; add signal bars beside operator.

## 0.15.0-r56 — 2026-10-19

Bench build: `bench-v0.15.0-r56-e46dba0`.

- **Manual FCC unlock for Dell DW5821e/Foxconn T77W968:** modem may enumerate but refuse radio enable, reconnecting periodically. Enable/Disable hook in Diagnostics tells MM to send the FCC unlock when needed; never enable automatically or alter another MM hook. Requires qmi-utils; CLI `medvemodem fcc-unlock <id> on|off`.
- **Service actions grouped in Diagnostics:** FCC, usb-modeswitch rule repair, storage-mode switch and USB composition (moved out of Connection with Apply/confirmation), with explanations and rollback guidance.
- **Action required banner:** show missing driver/MM, bad modeswitch rule, storage mode, SIM PIN/PUK or FCC radio lock, with a link to the exact modem card instead of executing an action from homepage.

## 0.15.0-r55 — 2026-10-19

Bench build: `bench-v0.15.0-r55-52aa578`.

- **Profile audit fixes:** HiLink in debug composition and ZTE router-mode units with extra serial ports remain router-mode DHCP devices rather than being erroneously sent to MM. Covers Huawei `12d1:1566`, `12d1:1442` and ZTE `19d2:1404/0581/1557`.
- **FM350 APN credentials:** native AT dialing includes `AT+CGAUTH` and hides passwords in UI/logs; only reuse a persistent session when its credentials match.
- **FM350 recovery:** no unsafe `CFUN=1,1` reboot because radio did not return afterward; use session/USB recovery.
- **Compal RXM-G1** sharing `1e2d:00b7` with Foxconn T99 is matched by model and avoids Foxconn AT commands. Disable unsupported native-QMI fallbacks for Telit FN990/LM960 and SIMCom 9001.
- **Additional IDs and profile corrections:** Sierra `1199:9078/907b`, Dell `413c:81c2`, T99 `1e2d:00b8/00b9`, HiLink `12d1:1442`, HP lt4112 `03f0:581d`, SIMCom `1e0e:9011/9003`; fix FM350 RNDIS, Intel XMM NCM and Sierra descriptions.

## 0.15.0-r54 — 2026-10-19

Bench build: `bench-v0.15.0-r54-097fe85`.

- **Native FM350 AT dialing without external manager:** automatic owner priority became external manager if installed → native engine AT dial → MM, because MM alone could not establish FM350 data sessions. Apply only to explicitly profiled FM350 `0e8d:7127/7126`; manual owner preference still overrides. Explain the choice and installation advice in UI.
- **FM350 bench probe is read-only:** include FCC lock, network mode, SIM, operator and registration queries with commands `AT+GTFCCLOCKMODE?`, `GTFCCLOCKSTATE?`, `GTFCCEFFSTATUS?`, `AT+GTACT?`.

## 0.15.0-r53 — 2026-10-18

Bench build: `bench-v0.15.0-r53-1443420`.

- **Huawei/ZTE profile expansion** from hardware zoo, catalog and community behavior (not bench-verified): Huawei HiLink E3372h/E8372h `12d1:14db/14dc`, E3372h Gateway NCM `12d1:155a` under MM; ZTE modem-mode MF821/MF28D/MF290/P685M/MF286 family/MF289F/MF823 NCM/MF831 MBIM under MM; web-router ZTE `19d2:1405/1403/1404/1244/0557/0558/0581` and Tenda MF6 `1557`.
- **HiLink model from modem web API:** distinguish E3372h from E8372h sharing a USB ID.
- **Diagnostics label:** rename to Last connection attempt.
- **Separate Telegram bot branch prototype:** `/start`/`/menu` dashboard, Status/Modems/Channels/Quota/Events/Help buttons, modem card and Refresh, sanitized IDs in inline callbacks; chronological event log, accurate primary changes/outage durations; fix Cyrillic UTF-8 truncation, mixed engine language, proxy password setup, ubus ACL grant and BusyBox salt/link-code generation.
- **Bot packaging and tests:** build noarch packages for 24.10 IPK and 25.12 APK, test joint installation, use fake Telegram test endpoints and check token/phone-number redaction.

## 0.15.0-r52 — 2026-10-17–18

Bench build: `bench-v0.15.0-r52-c346e06`.

- **Last AT dial shown in modem Diagnostics:** time, outcome and localized step sequence or failure reason for FM350/native AT-connected modems, without exposing IP addresses.
- **Package descriptions** rewritten for Alpha.
- **Huawei HiLink/ZTE profiles and web-model reading** prepared for r53.

## 0.15.0-r51 — 2026-10-17

Bench build: `bench-v0.15.0-r51-0485bc4`.

- **Valgrind/C audit for r44–r50:** fix ~5 KB leak on every native connection/config change due to retained transaction journal; fix use-after-free when an external manager rewrites its configuration while the engine reads old section names, preventing daemon crash or bogus script arguments.
- **USB composition retry bound** from r50 included; Telegram bot not yet in this bench release.

## 0.15.0-r50 — 2026-10-17

Bench build: `bench-v0.15.0-r50-dfacbd5`.

- **Latest event in homepage modem card:** show `last_event` with timestamp as used in Diagnostics, avoiding an extra ubus call per device.
- **Stop endless USB composition switching:** after two failures, suspend retries until the administrator changes the requested composition instead of repeating every ten minutes.
- **Add Telegram bot sources** `medvemodem-bot` and `luci-app-medvemodem-bot` on ucode for OpenWrt 24.10/25.12, without including it in this bench artifact.

## 0.15.0-r49 — 2026-10-17

No standalone bench release.

- **OpenWrt Status → Overview modem rows:** linked modem name, operator/RAT, address and status (Primary, Backup ready, No SIM, Disconnected from USB). Hide block when no modems or daemon unavailable.
- **Clearer homepage:** Internet availability first, then active modem and ready backup by name.
- **SIM missing is not zero signal:** red No SIM and not registered state instead of generic No Internet/0%.
- **Card details:** display address and interface; hide LTE+NR NSA/NR SA engineering notation except in Expert mode.

## 0.15.0-r48 — 2026-10-17

Bench build: `bench-v0.15.0-r48-eb9dbaa`.

- **Inline modem renaming:** pencil beside title, Esc cancels; remove separate Name card.
- **Modem Settings layout:** Connection and SIM adjacent, two columns when space permits.
- **Advanced radio settings preview:** show current RAT/band and Change shortcut on Network and signal.
- **Russian UI copyedit:** clearer pause, connection toggle, USB service mode and plurals.
- **USB configuration:** default option renamed Automatic and actionable failure messages. Engine unchanged.

## 0.15.0-r47 — 2026-10-17

Bench build: `bench-v0.15.0-r47-1104afd`.

- **Native FM350 3GPP dial:** independently implemented behavior informed by real-world projects: check network registration and restore automatic operator search after another program leaves `+COPS: 2`; reuse existing PDP context when available, avoiding 30–60 s disruption on priority changes; use `AT+CGACT=0` before activation only as needed, since repeated activation could drop FM350; retry with IPV4V6 if IPv4-only fails (observed on Tele2); obtain gateway/DNS from `AT+CGCONTRDP` and `AT+GTDNS`, and use a dhcpv6 child interface for IPv6.
- **LuCI terminology reviewed:** “USB composition” and “administrator” replace ambiguous wording.
- **Alpha 1 README** drafted primarily in Russian with a short English overview.

## 0.15.0-r46 — 2026-10-17

Bench build: `bench-v0.15.0-r46-1964998`.

- **Strict frontend/engine version dependency:** `luci-app-medvemodem` requires the exact matching `medvemodem` version to prevent an API mismatch.
- **Original release scheme:** bench `0.15.0-rN`, planned Alpha 1 `0.16.0-r1`, Alpha 2 `0.17.0-r1`; “alpha” originally intended for tag/title only. Later public alpha packaging uses `0.16.0_alpha1-r1`.
- **Protect Foxconn T77 from `^SETMODE=`:** this AT command changes USB composition across HP/Dell/Telit/Foxconn IDs.
- **T99/T77 AT presets:** neighboring cell RSRP/RSRQ, LTE category, band priority; FM350 reboot `AT+CFUN=15`.
- **APN addition:** Rostelecom MCC/MNC 250-03, `internet.rtk.ru`; bench probe includes USB `bcdDevice`.

## 0.15.0-r45 — 2026-10-17

Bench build: `bench-v0.15.0-r45-53b9cb6`.

- **Manual USB composition selection:** Connection → USB configuration, for devices such as DW5821e with separate QMI and MBIM configurations. Changes only the kernel's selected configuration, not modem firmware; limit to two attempts per ten minutes and block during dial/recovery or incompatible `usb-mode.json` rule. Without a saved choice, kernel default applies after reconnect.
- **T99W175 MBIM/1e2d profiles** and read-only `medvemodem-bench probe [--at]` enter test build.

## 0.15.0-r44 — 2026-10-17

Bench build: `bench-v0.15.0-r44-e837029`.

- **Modem identity follows IMEI hash:** clones may share USB serial or have none; using USB port alone loses settings when moving devices. Use salted IMEI hashes to migrate modem/device/eSIM sections without storing/logging IMEI; defer migration if old port is occupied, and refuse ambiguous moves for two devices sharing IMEI. Show name anchor (serial/IMEI/port) in LuCI.
- **T99 profiles:** `05c6:90d5` MBIM and `1e2d:00b3` MBIM/`00b7` QMI; some interface indices remained unknown.
- **Alpha bench checklist:** T99/T77 QMI/MBIM in MM/native modes, FM350 via external manager, E3372; bench USB composition/driver/modeswitch/AT probe.

## 0.15.0-r43 — 2026-10-17

Bench build: `bench-v0.15.0-r43-4ef78ea`.

- **Signal history and carrier aggregation expanded by default** again under Network and signal; r41–r42 collapse made them appear missing.
- **Events instead of connection-only history:** recovery, USB and configuration actions included.
- **Connection-result notices no longer overwrite newer actions** such as SMS refresh or SIM save after a delayed response.

## 0.15.0-r42 — 2026-10-17

Bench build: `bench-v0.15.0-r42-5e0d245`.

- **Dedicated Settings tab per modem:** name, Connection (APN, IP mode, owner, TTL, autoconnect), SIM label/number/quota/SMSC/PIN, modem web password, radio mode/bands and AT port moved into one place. Overview becomes read-only.
- **Grouped Diagnostics:** Modem and device, Recovery, Tests/support information.
- **Contextual alerts above tabs:** link directly to PIN or web password card.
- **Three-phase connection UI:** Connecting modem → Registered, checking Internet → Internet available, or exact failure reason; starting a task is not reported as success.

## 0.15.0-r41 — 2026-10-17

No standalone bench release.

- **Unified version across engine/LuCI:** frontend moves from `0.7.42` to `0.15.0-r41`; show one version and a mismatch warning.
- **Persist collapsible panel state** for modem detail/history/rare tools; auto-open Connection while disconnected.
- **Terminology:** Network and signal, Diagnostics, Signal clarity (SINR), IP version, Quota, Connection history, Site availability check, Disconnect/Connect.
- **Homepage:** one primary card action, advanced actions in ⋯ menu, shorter banner for three+ modems, wired WAN precedence stated in Make primary confirmation.
- **Settings grouped:** modem operation, Internet backup, checks/monitoring and components; cell queries that hung the test router are hidden under Detailed radio data.
- **SMS browser privacy:** retain only a hash, not sender/text snippets, to remember send ordering.

## 0.15.0-r40 — 2026-10-17

Bench build: `bench-v0.15.0-r40-38448eb`.

- **HiLink/ZTE radio via web APIs:** router-mode Huawei E3372h/E8372 and ZTE MF79U/MF833/MF971 have no usable AT port. Read signal, RAT, band and serving cell through each modem's own web session/address, keep LTE/5G NSA metrics separate, and do not query IMEI/IMSI/ICCID/number.
- **Modem web password handling:** try factory `admin`, then ask in a modem web card when required. Save only on router, never in logs/reports; rate-limit wrong-password retries to once per five minutes, accept new password immediately and respect active user session/lockout. Forget password removes it.

## 0.15.0-r39 — 2026-10-17

Bench build: `bench-v0.15.0-r39-95f5172`.

- **SIM PIN/PUK UI:** SIM card panel has PIN entry, attempts remaining, Unlock with last-attempt confirmation; PUK requires new PIN twice. Optional Remember PIN enables post-reboot unlock; Forget PIN removes it. Do not log or export secrets.
- **Script environment isolation:** LuCI-called helpers do not inherit arbitrary caller environment. Serialize package installations.
- **Safer autoconnect:** new modem interfaces use metric 100 rather than stealing wired WAN route, per-modem autoconnect toggle, no automatic action before network config has been loaded.
- **APN suggestion for new interfaces only:** respect already established sessions and explicit APN.
- **Legacy Huawei radio via `AT^SYSCFG`** and profile additions; Delete all SMS remains visible on empty list.

## 0.15.0-r38 — 2026-10-17

Bench build: `bench-v0.15.0-r38-d175eea`.

- **Delete all SMS really clears storage:** native AT erases complete modem/SIM store, including hidden multipart fragments; MM path deletes the complete visible list.
- **T99 LTE USSD confirmation:** warn about temporary 3G switch and possible Internet interruption.
- **eSIM over MBIM:** read currently active SIM slot without switching to the alternate one.
- **Legacy Huawei radio:** fall back to `AT^SYSCFG` if `AT^SYSCFGEX?` is unsupported.
- **Security:** isolate environment for root-run LuCI helpers (previous read-only callers could inject commands); serialize installers.
- **Eleven hardware-family profiles** from vendor references, not bench-tested, including Foxconn T77W676, Sierra, Telit, SIMCom, Dell DW5820e, Quectel EP06, HP lt4132, E3372 debug and Brovi E3372-325; recommend qmi_wwan driver for unsupported Qualcomm/Quectel IDs.
- **Actions CI updated to Node 24.**

## 0.15.0-r37 — 2026-10-16–17

Bench build: `bench-v0.15.0-r37-29484aa`.

- **Autoconnect new devices:** after MM/native ownership becomes ready, create modem interface and apply SIM-selected APN once per USB attachment. Respect hand-managed and external-manager interfaces; optional global switch.
- **Automatic APN by SIM** when APN field is empty, manual value always takes priority.
- **SMS full-store clear button** removes all multipart components to restore receiving on small-memory devices such as E171.
- **English CLI defaults:** `medvemodem top` uses English; use `--lang ru`, `MEDVEMODEM_LANG=ru` or `L` for Russian.
- **T99 USSD and MBIM eSIM:** warn about 3G fallback, do not switch dual-SIM slot to read profiles; custom lpac libraries under `/usr/lib/lpac` are discoverable.

## 0.15.0-r36 — 2026-10-16

Bench build: `bench-v0.15.0-r36-7c3dc21`.

- **Detect missing kernel modules:** name required package such as `kmod-usb-serial-option` on modem cards and in Diagnostics; banner warns above tabs.
- **One-click component installer:** check feed availability before installing drivers, MM, usb-modeswitch; report missing packages instead of partial install. Load driver without router reboot, do not restart an active MM.
- **Modem Connect action** appears when interface is absent/down; avoid marking port-power recovery failed merely because MM intentionally keeps modem disabled until dial.
- **New-device auto-connect and full SMS-store clear** prepared for r37.
- **English CLI events and dns-check outputs**, with localized LuCI explanations.

## 0.15.0-r35 — 2026-10-16

Bench build: `bench-v0.15.0-r35-249c977`.

- **Recover netifd interface after MM gives up:** T99 USSD caused network change to 3G, MM dial failed and disabled the interface; retry on registration after 30 s, 1/2/5 minutes then every 10 minutes.
- **User deactivation tracked distinctly** from automatic errors, persisted across daemon restarts.
- **Homepage recovery indicator** includes latest automatic fix/time and hourly count.
- **SMS reliability:** delete unsent drafts older than ten minutes, which can block inbound SMS; warn on incomplete/unreadable/full stores; messenger-style thread and export CSV; Refresh waits for running read and reports new count; T99 SMS storage switch action.
- **Antenna polling:** restored via qmicli while page open, configurable with `medvemodem.core.antennas`; signal-card click opens radio tab.
- **UI prevents repeated actions** while running and does not re-enable SMS Send prematurely.
- **eSIM SM-DP+ TLS warning:** a historical project decision allowed downloads without certificate verification due to reported incompatibilities; the user is warned before download and codes remain out of logs. This is a historical state, not a recommendation to disable TLS verification.

## 0.15.0-r34 — 2026-10-16

Bench build: `bench-v0.15.0-r34-4ac3488`.

- **Repeated priority changes work** (r33 previously applied only first change); Make primary button; user/automatic deactivation labels distinguished.
- **Deactivate Internet interface, not radio:** disconnect data while allowing incoming SMS/USSD; may temporarily reconnect existing interfaces.
- **External manager cleanup:** standardized `mm0`/`mm1` names, remove orphaned DHCP and firewall zones, preserve active manager-owned records.
- **SIM APN selection:** only when field empty and enabled.
- **SMS threads:** normalize phone formats, stable chronological display and IDs across MM restarts; multipart timeout five minutes, SMSC read/write.
- **USSD session safety:** close old requests, one-minute interactive timeout, pause conflicting SMS/AT requests.
- **T99/T77 serving cells through `AT^DEBUG?`** every 30 seconds with MM location polling disabled by default, plus gentle MM poll and load counters; recognize Huawei E171 storage mode.
- **TUI improvements:** radio tab, activate/priority keys, safe control characters and redacted identifiers under `--once` unless explicitly shown. Avoid unnecessary post-DNS-check delays and inherited mmcli file descriptors.

## 0.15.0-r33 — 2026-10-15–16

Bench builds: `bench-v0.15.0-r33-79e705c`, `…-ba7d8ab`.

- **Modem cards fit three-across layout** with signal/RSRP/SINR/throughput; themed buttons and readable warnings, custom names rendered exactly. Rename Stability to Connection status.
- **Modem priorities:** arrows in Settings → Internet backup, Apply order, route metrics 10/20/30, cards sorted by priority.
- **Deactivate/Activate per modem** until next action or router reboot.
- **MM cell/location queries disabled by default** because Orange Pi Zero 3 with T99W175 froze; optional `medvemodem.core.cell_info=1`.
- **AT port settings moved to Service**, where accessible even if no AT tab; manual temperature probe; suppress zero-phase antenna values.
- **SMS:** strict time order, timestamps, multi-select delete with preserved checkboxes.
- **Documentation:** initial bot specification and dangerous AT-command knowledge.

## 0.15.0-r32 — 2026-10-14–15

Bench build: `bench-v0.15.0-r32-eaf2c9f`.

- **Second-generation operator DNS interception test:** per-modem probes to 1.1.1.1, 8.8.8.8, 9.9.9.9, comparing DNS RTT to TCP RTT to detect impossible local replies; compare blocked-domain answers against DoH to identify stub/private NXDOMAIN changes without flagging legitimate CDN diversity.
- **Service checks UI:** DNS through this modem matrix with resolver, apparent responder, DNS/TCP RTT and verdict.
- **Manually test any channel:** select WAN/Wi-Fi/modem for service/DNS checks; CLI `medvemodem channels` and `dns-check <device>`.
- **Device-scoped DNS/TTL** follows the current netifd egress interface, not a temporary MM port.

## 0.15.0-r31 — 2026-10-14

No separate build; included in r32.

- **Per-modem service availability tests:** Telegram, GitHub, YouTube, Google, Claude, ChatGPT, WhatsApp, Discord versus Yandex/VK/Wildberries directly over each modem, regardless of main route. Requires curl and CA bundle; summarize full/whitelist/no access.
- **Operator DNS interception:** test every session and every six hours whether network devices spoof public resolvers or rewrite nonexistent names, flagging DNS chain while keeping the link usable.
- **LTE cell location:** eNB/sector and timing-advance estimated distance (±80 m).
- **Hide identifiers** browser-persistent control masks IMEI/ICCID/IMSI, phone, addresses and cell IDs.

## 0.15.0-r30 — 2026-10-13–14

Bench build: `bench-v0.15.0-r30-6b91dec`.

- **Per-modem TTL 64 fix** for smartphone tariffs, disabled by default. Set IPv4 TTL=64 and IPv6 hop-limit=64 on egress, excluding ICMPv6, through dedicated nftables tables and a netdev egress hook. Show status/packet counts.
- **Per-modem service checks and LTE cell/timing advance** introduced, expanded in r31.
- **Hide identifiers** for safe screenshots.
- **External manager installer status** no longer flashes Not running immediately after Start.
- **GPL-3.0 license and code provenance** documented; bench artifacts carry external-manager license/source/commit metadata.

## 0.15.0-r29 — 2026-10-13

No standalone bench release.

- **Layered diagnostic chain:** modem header shows Device → SIM and network → Session → Internet → DNS, identifying the first failing link and its cause. Visible only on failures in Basic and always in Advanced; `medvemodem top` has a Chain row. Unchecked links show “?” rather than a failure.
- **One-click modem report:** Service → Modem report → Collect support report generates JSON with hardware inventory, engine decisions/diagnosis, session, event history, kernel and last 300 modem-related syslog lines (MM/netifd/uqmi/umbim/external manager). Redact IMEI/IMSI/ICCID, phone numbers, PIN/passwords and SMS content; hash USB serial.
- **External-manager installer PID race:** replace the PID file atomically to avoid a momentary “not running” status after launch.

## 0.15.0-r28 — 2026-10-13

Bench build: `bench-v0.15.0-r28-e184a96`. **Important fix:** r26/r27 LuCI homepage and signal icon failed for MM-owned devices because a shared frontend module invoked a missing function; use r28 instead.

- **One signal quality scale:** labels, color, bar fill and icon use identical thresholds. 5G RSRP thresholds −115/−105/−95 dBm, LTE −110/−100/−90. Quarter-step fill never rates Good higher than Excellent.
- **Four-bar signal icon:** interference with SINR below 0 dB reduces to one bar and warns about interference. SINR exactly zero with good RSRQ is treated as unmeasured rather than bad interference.
- **Build/test:** test the shared JS module independently under LuCI; a package that succeeds on a serial retry is marked built even if parallel build failed.

## 0.15.0-r27 — 2026-10-13

No standalone bench release.

- **Per-carrier signal timeline:** Radio → Signal history in Advanced mode has parallel rows for LTE, 5G and each active carrier (B3/B7/n78), colored by RSRP quality. Empty cell means no measurement; hover shows time, value and quality; carrier levels also appear in Values table.
- **Engine per-carrier RSRP history:** while page open update every 30 s, in background every five minutes fetch only cell data (not location), for MM-owned modems only.

## 0.15.0-r26 — 2026-10-13

Bench build: `bench-v0.15.0-r26-2f27b0b`.

- **Immediate high-visibility critical warnings:** destructive usb-modeswitch rule or stuck MM is highlighted red above tabs with a Fix link rather than buried in Service.
- **Connect/Disconnect through engine** with audit attribution “manually (LuCI)”.
- **External manager unavailable:** selecting it under Connection shows a missing package and direct installation link.
- **Service → Why this owner:** explain manual selection, an external-manager configuration section or the Auto policy.
- **Basic Settings:** toggles only; check targets, manual channels, failback and APN database available from Advanced. A single modem's homepage card starts expanded.
- **Localization:** correct plurals and standardize “firewall”; README updated to match current engine.

## 0.15.0-r25 — 2026-10-13

Bench build: `bench-v0.15.0-r25-fbc9bdd`.

- **Foxconn T99W175/T77W968 native radio settings without MM:** via leased AT, read `AT^SLMODE?` and `AT^BAND_PREF?` on T99 or `AT^SLBAND?` on T77; allow network-mode/band changes with two-minute Internet-dependent rollback. T99 supports 3G/4G/5G and 3G/LTE/NR bands, T77 3G/4G and LTE bands. Write only changed values; never send bare `AT^BAND_PREF` that resets factory masks.
- **Recognize a T99 profile** when MM previously saw the modem even under shared Qualcomm X55 ID `05c6:9025`.
- **LuCI integration:** high-priority warnings above tabs, immediate external-manager missing warning, Why this owner, simplified Basic Settings, common detail-level selector and expanded single-modem card.
- **CI:** shared frontend version/text/format module and RPC failure/multi-modem ownership tests; r25 failed when its version changed in only one location.

## 0.15.0-r24 — 2026-10-13

No standalone bench release.

- **One-day AT history:** collapsible AT tab lists console/CLI commands, responses, ports, times and rejected dangerous commands; click to reuse and Clear. Stored only in RAM and visible to engineer access group.
- **Native QMI/MBIM/AT status:** show operator, technology and signal from leased AT instead of placeholders, no more than once every 30 s.
- **Expert Stability card:** management backend, protocol/netdev/driver, session interface and leased AT port.
- **MM selection works after native QMI:** bring down uqmi, release/modulate ownership and reconnect through MM rather than leaving the modem offline; no unsupported SMS/AT tabs, with explanations under Service → Unavailable.

## 0.15.0-r23 — 2026-10-13

No standalone bench release.

- **Messenger-style SMS:** left conversation list and right bubble thread/reply, New message above the list, mobile one-column view with Back.
- **AT-sent SMS in conversations:** `AT+CMGS` may not persist sent messages, so hold last 50 per modem in daemon RAM until restart; label failed sends explicitly.
- **USSD progress:** show elapsed seconds and clear timeout/status-read failures instead of indefinite Waiting for network.
- **T99 native QMI vs MM race:** Auto previously chose uqmi while MM was still initializing, then both contended for `cdc-wdm0`. Wait for installed MM and hand back inactive native QMI to MM, preserving working sessions and operator-selected Direct QMI.
- **Settings grouped** into Interface, Modem recovery, Internet redundancy and Components.
- **Fix SMS-list typo** that broke the page and localize MM handback/quota events.

## 0.15.0-r22 — 2026-10-13

Bench build: `bench-v0.15.0-r22-bd856cf`.

- **Prevent external manager dialing foreign modem ports:** disable external-manager entries not explicitly assigned to it, including unplugged or renumbered FM350 after OPi Zero 3 re-enumeration. Previously stale `tom_modem` could send AT into T99 DIAG and hang the kernel.
- **Modem event journal:** Basic shows last 24 h; Advanced adds complete history with Copy log and Copy entry actions.

## 0.15.0-r21 — 2026-10-13

Bench build: `bench-v0.15.0-r21-3769168`.

- **Modularize SMS/USSD/eSIM:** reduce core from ~11.3k to 8.6k lines without behavioral changes.
- **Shutdown ownership correctness:** release modems to MM before tearing down session objects, avoiding freed-data access.
- **Fix memory leak on MM restart invocation.**
- **SDK/CI:** share host threads across concurrent builds, retry failed parallel packages serially with clear summary, install-test external manager after project packages.

## 0.15.0-r20 — 2026-10-13

No standalone bench release.

- **Split modules:** radio configuration transactions, SIM quota, SMS seen marks and signal history each own their lifecycle; no user-visible changes.
- **Fix use-after-free** when loading a saved radio-change journal after daemon restart, introduced in r18 and found with Valgrind.

## 0.15.0-r19 — 2026-10-13

No standalone bench release.

- **Feature freeze** before more hardware validation; only bug fixes, tests and core refactoring.
- **AT by detail level:** Advanced allows safe preset commands, Expert exposes raw AT input and manual AT port.
- **Engineer ACL group** `luci-app-medvemodem-engineer` for AT, port probing, MM restart, modeswitch-rule repair and external-manager installation. Root has access; unauthorized users get an explicit denial.
- **Three build architectures:** `aarch64_cortex-a53`, `aarch64_generic` (Rockchip NanoPi family) and `x86_64`, with external-manager packages for all. Find arch via `. /etc/openwrt_release; echo $DISTRIB_ARCH`.
- **Installer errors surfaced**, including permission-related start failures.

## 0.15.0-r18 — 2026-10-13

No standalone bench release.

- **DIAG/QDL service ports never used for AT**, whether profile, manual selection or auto-probe; show disabled entries. On T99 sending AT to interface 00 could hang the modem.
- **SIM quota isolation:** remove quota-based route penalties when modem/SIM disappears or changes.
- **Radio rollback survives daemon restart:** interrupted mode/band changes resume or roll back correctly.
- **Clock rollback safety:** NTP adjustments backwards do not reset SIM monthly quota.
- **Signal history:** duplicate samples in a second do not skew averages, cell change with same PCI is still an event, disabled history holds no timer.
- **CI main branch** builds and publishes bench artifacts.

## 0.15.0-r17 — 2026-10-13

No standalone bench release.

- **Installer download failure messages:** surface uclient-fetch output, TLS/libustream-mbedtls hints and offline-file steps, fallback to curl; previously install button silently failed.
- **DIAG/QDL protected regardless of profile/operator AT choice** (expanded r18).
- **SIM quota only for currently observed channels**, not a device reassigned to a different modem/SIM.
- **Radio changes recorded and resumed/rolled back** across daemon restart.

## 0.15.0-r16 — 2026-10-13

No standalone bench release.

- **Unread SMS badge** on modem homepage cards opens SMS tab; only opening a message list marks items seen, old stored SMS aren't counted as new.
- **Simpler Basic card:** role and uptime, qualitative signal, SIM label, today's usage or remaining quota bar.
- **Show primary fault first**, put details in tooltip/full page; avoid USB paths/address clutter and empty lines.

## 0.15.0-r15 — 2026-10-13

No standalone bench release.

- **Signal history:** Radio → Signal history in Advanced mode, RSRP and SINR over 5 min/1 h/24 h, separate LTE/5G lines and technology/cell-change markers, hover tooltip and table, all RAM; MM devices only.
- **Reduce active modem queries:** request MM cell/location details only while LuCI or TUI is open rather than every 40 s, and in background poll MM properties once a minute for signal history. Disable with `uci set medvemodem.core.signal_history=0`.

## 0.15.0-r14 — 2026-10-13

No standalone bench release.

- **Per-SIM traffic quota:** Service → SIM card; monthly GB limit, billing-cycle start day, warning percentage and action (warn only or demote to backup on exhaustion). Counts a modem's traffic while the SIM is inserted and survives reboots; stores only hashed ICCID.
- **Quota progress:** modem/homepage show usage such as “12 of 50 GB (24%)” and exhaustion. Journal warning/exhaustion only once per cycle.
- **Demotion policy:** on exhausted quota and enabled failover, select another healthy channel; if none exists, preserve the current connection.

## 0.15.0-r13 — 2026-10-13

No standalone bench release.

- **T99W175 AT console restored:** profile specifies interface 02 in USB composition 9025. Previously MM did not use this AT port but the engine refused to probe it beside MM.
- **Manual AT port selection and probe** in the Advanced AT tab; never probe DIAG service ports.
- **Control owner selector:** Auto, ModemManager, MedveModem or external manager, with confirmation.
- **Fix LuCI tests broken in r12** and add AT-port/modeswitch rule UI tests.

## 0.15.0-r12 — 2026-10-13

No standalone bench release.

- **Failed modem connection no longer reloads the whole network:** previously two `network reload` operations restarted hostapd/wpa_supplicant, disconnecting Wi-Fi clients and Wi-Fi uplinks. Now bring down only the unsuccessful modem interface, retaining configuration for next retry; Release explicitly removes it.
- **Stop connection attempt immediately** when netifd reports a terminal failure such as `MM_CONNECT_FAILED`, rather than waiting 120 seconds.
- **Dell DW5821e usb-modeswitch rule safety:** detect `/etc/usb-mode.json` rules clearing USB configuration (produced a kernel Oops on the bench); show a Disable rule control and save backup as `/etc/usb-mode.json.medvemodem-backup`, without changing the rule automatically.

## 0.15.0-r11 — 2026-10-11

Bench build: `bench-v0.15.0-r11-5109f08`.

- **Edit RAT and bands:** Radio → Network mode and bands → Change in Advanced, selecting 3G/LTE/5G, preferred RAT where supported and device-supported bands; confirm in two steps.
- **Automatic rollback:** if modem Internet existed before change but does not return within two minutes, restore previous settings; user can Keep/Undo any time. Reject unsupported writes and avoid irreversible changes.
- **Backends:** ModemManager, external modem manager and native Quectel `AT+QNWPREFCFG`; Fibocom/Intel XMM/Huawei/older Quectel are read-only with explanations.

## 0.15.0-r10 — 2026-10-11

Bench build: `bench-v0.15.0-r10-602515b`.

- **DNS follows traffic:** remove offline link's DNS resolvers from router until connectivity returns, without leaving the resolver list empty.
- **IPv6 failover** follows IPv4 routing policy.
- **Manual-only channels:** smartphone tethering and other optional paths never auto-take traffic.
- **Keep recovered unreliable link last:** do not auto-restore its priority until user selects Restore priority.
- **Firewall/proxy-aware checks:** do not reset a healthy modem merely because direct probes are blocked while local clash/mihomo proxy provides connectivity.
- **Backoff on route-helper failures:** 60 s instead of repeating an ineffective action continuously.

## 0.15.0-r9 — 2026-10-10–11

Bench build: `bench-v0.15.0-r9-96a557e`.

- **Failover avoids equal metric conflicts:** kernel cannot hold two default routes with equal metric. Assign a free penalty metric to failed channel and wait when another channel owns the intended metric rather than retrying every five seconds.
- **Missing netifd protocol means no destructive resets:** when qmi/mbim/ncm package is absent, report missing protocol handler rather than rebooting the modem.
- **CI optimization:** self-hosted runners by default, rebuild external manager only when version/build scripts change, parallel SDK builds with persisted SDK trees, fallback Chrome for LuCI tests.

## 0.15.0-r8 — 2026-10-10

No standalone bench release.

- **Automatic failover to healthy channel**, disabled by default under Settings → Connection recovery. Check each default-route device (wired, Wi-Fi, modem) through its own interface, add 10000 route metric penalty when offline, restore when healthy. Do not change persistent network settings; restore metrics on daemon shutdown.
- **mwan3 coexistence:** disable this engine's failover when mwan3 is installed and explain incompatibility.
- **Ubus uplink.failover** reports reroutes; optional self-hosted WSL2 CI setup documented.

## 0.15.0-r7 — 2026-10-10

No standalone bench release.

- **Read radio settings from external/native backends:** open Network mode and bands reads via external `modem_ctrl.sh`, native model AT (`AT+GTACT?`, Quectel `QNWPREFCFG/QCFG`, XMM `AT+XACT?`, Huawei `AT^SYSCFGEX?`); show source/read time, Refresh, all-supported when unrestricted.
- **New `radio` ubus method.**

## 0.15.0-r6 — 2026-10-10

Bench build: `bench-v0.15.0-r6-5b67c3b`.

- **Read-only Network mode and bands card** in Advanced Radio with allowed/preferred technologies and bands; Expert lists capabilities/source. MM-owned modems only at this stage, others report why unavailable.
- **Intel XMM family profiles** for Fibocom L850/L860 (`8087:095a`, `2cb7:0007/000b`) and Huawei modem-mode USB NCM/serial/QMI/MBIM; prefer MM. HiLink remains Ethernet/router-mode.

## 0.15.0-r5 — 2026-10-10

Bench build: `bench-v0.15.0-r5-b2df8bc`.

- **Disable external manager auto-probe before installation:** otherwise its hooks may grab any USB networking hardware (Ethernet/Wi-Fi) during package setup; persist `block_auto_probe` and remove temporary config on failed install.
- **Compatibility:** verified the manager does not conflict with the firmware's MM/uqmi/umbim and QMI/MBIM/NCM driver packages when correctly configured.

## 0.15.0-r4 — 2026-10-10

Bench build: `bench-v0.15.0-r4-78f70bc`.

- **Install external modem manager core via LuCI:** only its 3.3.0 backend, without its LuCI UI, built by project CI for 24.10.8 and 25.12.5, published alongside MedveModem packages.
- **Preflight and integrity:** verify checksum and dry-run dependencies; if unavailable in router feeds, install nothing and explain in log.
- **Auto-probe disabled after install** so external manager cannot take T99 or every USB modem; delegate FM350/Quectel by profile.
- **Missing external manager** results in direct installation link on modem page if selected owner requires it.

## 0.15.0-r3 — 2026-10-10

Bench build: `bench-v0.15.0-r3-7dc593d`.

- **Busy AT port visible everywhere:** prominent modem warning, journal when claimed/released, localized console denial.
- **Localized modem journal:** received/sent/failed SMS, stuck multipart cleanup, MM restart outcome and port release; previously many entries appeared as internal names.
- **Fix crash during initial port scan.**
- **External manager packaging study:** vendor binaries built for OpenWrt master libraries could not install on stock 24.10/25.12.

## 0.15.0-r2 — 2026-10-10

Bench build: `bench-v0.15.0-r2-cb37317`.

- **USSD priority over background SMS/AT console** to avoid interrupting an interactive network menu; session and recovery remain higher priority.
- **Profile AT port selection:** use declared interface number immediately, without `medvemodem probe`.
- **Foreign port holder safety:** refuse to open tty held by smstools3/other managers/sms_tool and report program name/PID in command reply and `held_by` field.

## 0.15.0-r1 — 2026-10-10

Bench build: `bench-v0.15.0-r1-91d9b99`.

- **Start of 0.15.0:** functional code same as `0.14.0-r24`, aggregating prior 0.14 changes.
- **AT tab:** Advanced modem page has command entry and 72 model-specific safe presets (T99/T77, Quectel, FM350, etc.); dangerous commands shown as non-executable reference with reason. If MM owns AT port, explain why unavailable.
- **USSD operator presets:** Balance, My number, Allowance for MTS, MegaFon, Beeline, T2, Yota, plus user shortcuts per SIM; sourced publicly, unverified on physical SIM at this point.

## 0.14.0-r24 — 2026-10-09

Bench build: `bench-v0.14.0-r24-e956da4`.

- **SMS/USSD native AT fallback:** first try ModemManager; if listing, sending or USSD fails, retry through the engine-owned AT port and explain the fallback in LuCI.
- **Non-MM modems get SMS/USSD through AT**, with background polling only after first opening SMS.
- **AT storage capacity and multipart assembly:** show e.g. SIM 49/50 and wait for missing parts. Text containing both quote characters now transmits.
- **CI SDK Docker image caching:** prefer Actions cache before Docker Hub to avoid anonymous pull rate limits seen in r23/r24.

## 0.14.0-r23 — 2026-10-09

No standalone bench build.

- **ModemManager SMS/USSD:** new modem SMS tab with number-based conversations, reply, delete and compose; GSM-7 160 vs UCS-2 70 characters and multipart notice. Advanced USSD card supports request, network reply, interactive response and End session.
- **Background SMS poll:** every minute by default (`core.sms_poll=0` disables), log counts but no numbers/text; drop multipart fragments stuck for over two minutes.
- **CLI:** `sms`, `sms send`, `sms delete`, `ussd`, `ussd reply/status/cancel`.
- **Unsupported modes** without MM or for external-manager devices return a reason (native fallback arrives r24).

## 0.14.0-r22 — 2026-10-09

Bench builds: `bench-v0.14.0-r22-310bb2b`, `…-0793e81`, `…-365cda9`.

- **Keep MM from claiming T99/T77 tty on startup:** generate `/lib/udev/rules.d/78-mm-medvemodem.rules` with `ID_MM_PORT_IGNORE=1` only for matching modem serial/interface, not all devices sharing Qualcomm `05c6:9025`. Effective after MM restart.
- **USB bus renumbering retry:** interface repair rejected while MM was still acquiring a modem no longer counts as exhausted; retry when MM updates.
- **Don't rebind ADB on T99:** detaching it while MM probed neighboring ports was suspected of triggering kernel crashes/watchdog resets; preserve ADB binding for `mm_ports=control`.
- **Dangerous AT composition/storage commands** tagged from r21.

## 0.14.0-r21 — 2026-10-09

Bench build: `bench-v0.14.0-r21-38056a0`.

- **Never race ongoing MM probing:** MM/qmi-proxy/mbim-proxy holding a modem port means no re-reporting ownership or recovery; wait up to five minutes. Previously port handoff every 31 seconds made MM discard an almost initialized FM350 twice. `show.mm_probing=true` reports this state.
- **Dangerous AT list:** `AT^USBSWITCH=`, `AT+USBSWITCH=`, `AT+SETCONFIG=`, `AT^PCIEMODE=` alter USB mode; `AT^CUSTOMER=` persists modem settings. Require expert context/authorization.
- **Research:** service checks and speed tests from other software studied as behavioral references; replace Gosuslugi with Wildberries in restriction-test sample list.

## 0.14.0-r20 — 2026-10-09

Bench build: `bench-v0.14.0-r20-60fc9d8`.

- **Carrier aggregation SCells:** MM exposes primary cell but not secondaries; read secondary CA through QMI `--nas-get-lte-cphy-ca-info` via proxy, render inactive SCells dim and derive band from EARFCN. Requires qmi-utils.
- **T99 port claim race:** if MM reclaims reserved tty during startup, revoke once rather than disturbing an active session.
- **Avoid duplicate dynamic port binds** and ADB flicker on boot.
- **TUI stage 2:** Overview/Radio/Service tabs; confirmation-based `c` health check, 1–8 recovery stages and `M` MM restart.
- **Bench release notes** describe changes/testing in Russian.

## 0.14.0-r19 — 2026-10-09

Bench build: `bench-v0.14.0-r19-cb7e18e`.

- **Correct session uptime** for MM netifd child interfaces; stop displaying 0 min.
- **IPv4/IPv6 prefix, gateway and DNS** shown in Basic connection card.
- **Fix UCI save on empty list** (ubus code 4) and rollback failed save.
- **TUI stage 1:** read-only modem list and individual connection, radio, antennas, SIM, recovery and events.
- **LuCI 0.7.3:** versioned styles avoid hard refresh, readable dark themes, grey offline indicator, untruncated fields and correct SIM PIN2 state.

## 0.14.0-r18 — 2026-10-09

Bench build: `bench-v0.14.0-r18-54b98fe`.

- **Manual emergency ModemManager restart:** Service tab, explicit confirmation, at most once every five minutes and never during recovery; affects all modems and never automatic. Wait for old process exit, then restart and suppress auto-recovery for three minutes.
- **Sensitive IDs visible in authorized UI** (full IMEI in Advanced, USB serial, IMSI, ICCID, SIM number), not in logs/persistent state and redacted in diagnostics.
- **SIM card status:** present/PIN/PUK/missing, SIM operator and number.
- **SIM label** (number/description) follows ICCID across modems.
- **CI MM restart test** no longer waits for GitHub runner's own ModemManager.

## 0.14.0-r17 — 2026-10-08

Bench build: `bench-v0.14.0-r17-e96ba0d`.

- **Per-chain antenna RSRP/TX** through read-only QMI proxy beside MM (`qmicli -p --nas-get-tx-rx-info`), not AT. Requires qmi-utils. Radio card highlights a chain 10 dB below best, unavailable chains and Expert phase/TX power.
- **Band consistency** against modem-reported available bands, flag ambiguous `B7?`.
- **ubus API reference** for read-only external clients including Android.

## 0.14.0-r16 — 2026-10-08

Bench build: `bench-v0.14.0-r16-e265d6c`.

- **MM radio data without AT:** bands/aggregation, serving and neighbor cells, network code and TAC/CID at higher detail; refresh at most every 30 s, no GPS read.
- **Device page** adds firmware and last IMEI digits.
- **Band inconsistency warning** when reported current band isn't in MM's allowed list; request mmcli evidence.
- **External manager preferred for FM350/Quectel** starting r15.

## 0.14.0-r15 — 2026-10-08

Bench build: `bench-v0.14.0-r15-23ca471`.

- **Reserve T77 tty from MM** using VID/PID and early boot policy; MM handles T77 over cdc-wdm only.
- **External modem manager preferred for FM350/Quectel** when installed and backend Auto; manual backend remains higher priority. Add Quectel USB `2c7c` family profile.
- **Design note:** Telegram proxy chain specified but not yet implemented.

## 0.14.0-r14 — 2026-10-08

Bench build: `bench-v0.14.0-r14-e634d1e`.

- **LuCI 0.6.1 UI revisions:** Settings cards, modem names in heading, clearer labels, Modems menu; daemon unchanged since r13.
- **Foxconn T77 family** covering Dell/Foxconn/Telit LN960: MM only, no simultaneous AT; recovery radio reset and USB reauthorization, not modem soft reboot.
- **Build job timeout increased** to 75 minutes after r14 reached a previous 45-minute limit.

## 0.14.0-r13 — 2026-10-08

Bench build: `bench-v0.14.0-r13-db96992`.

- **LuCI 0.6.0 dashboard layout:** overall verdict, modem cards with operator icons, detail-level switch and Modems/Settings navigation. Per-modem Overview/Radio/Service tabs, 24-hour event band, joint RSRP/SINR/RSRQ scales and recovery ladder.
- **New ubus `check`** immediately probes link and DNS.
- **Single validated Settings Save** commits changes atomically.
- **Readable modem names/status** at top of homepage and Russian driver events coalesced within two seconds.
- **Detail labels** Basic/Advanced/Expert and approachable recovery actions.

## 0.14.0-r12 — 2026-10-08

Bench build: `bench-v0.14.0-r12-0a45a81`.

- **T99 tty not sent to MM:** after USB reset, MM spent over a minute probing four tty (including DIAG/ADB) and failed to identify cdc-wdm0 as QMI. Early hotplug policy `mm_ports=control` removes TTY, reports only control/network ports.
- **Persist MM-observed model** in `identity.json`, enabling early profile/port policies before MM reconnect.
- **USB hub power recovery** only by explicit action; USB reset counts as successful only after actual disappearance.
- **LuCI 0.5.0**.

## 0.14.0-r11 — 2026-10-07–08

Bench build: `bench-v0.14.0-r11-45d0021`.

- **Early operator whitelist detection:** if Internet checks fail but known allowed sites (`ya.ru`, `vk.com` via `reach_restricted`) answer, label Restricted and avoid recovery, using hysteresis.
- **DNS health check** of router resolver on otherwise working channel; warn after two failures.
- **Recovery policy:** one reconnect after traffic stalls, then suspect operator restriction instead of endless resets (2G may escalate); do not reset all modems when all channels fail simultaneously; wait up to 12 min for network registration; single re-registration after carrier denial; reload firewall if modem interface absent from fw4.
- **USB power errors** such as −71/over-current displayed with powered-hub advice, not a reset trigger.
- **Manual port power cycle** for six seconds, where supported; require actual bus disappearance.
- **Reject unrelated LAN/WAN/tunnel UCI interfaces** as modem interfaces.
- **LuCI 0.4.1:** formal service language, qualitative RSRP/SINR, version footer.
- **Research on MM restoration/stuck modems** summarized in development notes.

## 0.14.0-r10 — 2026-10-07

Bench build: `bench-v0.14.0-r10-ccd5b43`.

- **Visible recovery progress:** after USB reset, MM missed one kernel event and abandoned the modem. Re-report ports up to three times at 30/60/120 s; on `mm_not_holding`, report ports rather than reconnect an absent session.
- **Bind ports before evaluating recovery**, restoring ADB before MM probes.
- **Recovery UI/log:** steps, elapsed time, limits, wait, prior outcome and initiator (automatic, LuCI, CLI, operator). History includes configuration apply/rollback and interface repair.
- **Known modem model display** rather than a raw MM product string.
- **LuCI 0.4.0:** progress, hazardous-event logging, connect/disconnect notifications and localized language.

## 0.14.0-r9 — 2026-10-06–07

Bench build: `bench-v0.14.0-r9-0508ceb`.

- **APN matching from SIM:** ship GNOME MBPI + Android AOSP operator data with Russian MVNO corrections. Select most specific IMSI/GID1/SPN/PLMN match; keep raw IMSI/ICCID internal.
- **`apn-update`** securely downloads/validates newer operator database to `/etc/medvemodem` with TLS, available from LuCI Settings.
- **Versioned bench archive names** such as `medvemodem-0.14.0-r9-…tar.gz`.

## 0.14.0-r8 — 2026-10-06

No standalone bench release.

- **MM detailed signal:** RSRP/RSRQ/RSSI/SINR, enabling polling as needed, signal bars and Radio page.
- **LuCI modem connection controls:** APN, IP type, operator suggestion and Connect/Disconnect; `adopt=1` converts an existing user interface into engine management while preserving other options and firewall zone.
- **Repair interface device path after USB bus renumbering** (H618 EHCI/OHCI startup order), restore `wan` zone if missing, optional `repair_interfaces`.
- **Prevent double counting `wwan0`** before MM identifies physical modem; fix null traffic aggregates.
- **Human descriptions of recovery steps** and expected outage/manual vs automatic policy.

## 0.14.0-r7 — 2026-10-02–06

Bench build: `bench-v0.14.0-r7-d71e783`.

- **LuCI 0.2.0:** homepage with Internet state/operator, backup readiness, modem names/roles, throughput/traffic and active/stuck automatic recovery.
- **Settings page** for recovery, port binding, reachability targets and modem names.
- **Modem page** covering connection, health, traffic, stability, ports, recovery actions, explanations, authorization and confirmation.
- **Localized recovery reasons** in UI.

## 0.14.0-r6 — 2026-10-02

Bench build: `bench-v0.14.0-r6-fc54fe5`.

- **Recover MM ownership after a modem soft reset:** a rebooting T99W175 missed MM probe, leaving netifd disabled. If MM fails to reacquire within 30 s, re-report kernel ports via `modemmanager.common` without restarting MM; then re-enable failed netifd proto interfaces.
- **Auto recovery ladder** for stalled sessions/registration/firmware and ownership conflicts; hard steps limited to bench-validated profiles; UCI `auto_recovery`, `health` in state.
- **Uplink status** includes active/backup role, interface-specific TCP probe, traffic/speed/daily/monthly usage and names; `uplink` via ubus/CLI.
- **Recovery snapshot/history** via ubus/diagnostic export, redacted.
- **Valgrind CI regression testing** manually available.

## 0.14.0-r5 — 2026-10-02

Bench build: `bench-v0.14.0-r5-eb5b71e`.

- **Auto-bind tty interfaces by profile:** T99W175 needs dynamic `option new_id` for interfaces 00/02/03 after reboot. Apply on reconnect and verify binding.
- **Don't claim ADB or foreign-driver ports:** restore wrongly attached extras; at most two attempts/device; only bench-validated policies; UCI `core.host_fixes` disable switch.

## 0.14.0-r4 — 2026-10-02

No standalone bench release.

- **Avoid profile oscillation** between generic QMI/T99W175 while MM probes a released modem for ~15 s; refresh state after 5/15/45 s.
- **Never recover on stale MM data:** ask for a fresh query, with CLI retry.

## 0.14.0-r3 — 2026-10-02

Bench build: `bench-v0.14.0-r3-d213274`.

- **Return a released modem to MM on udev-less OpenWrt:** re-report kernel port events with `mmcli --report-kernel-event` after unblocking or daemon shutdown, following MM hotplug practice.
- **Back off failed inhibit attempts** when MM has no active device instead of repeating every five seconds.

## 0.14.0-r2 — 2026-10-02

No standalone bench release.

- **Restart daemon after package upgrade:** procd otherwise left old `medvemodemd 0.13.0` running with the unchanged command line, breaking new `probe`/`recover`.
- **CLI missing-method diagnostics** and bench version mismatch refusal; for proto qmi inspect child `<iface>_4` for IP/DNS.

## 0.14.0-r1 — 2026-10-02

No standalone bench release.

- **On-demand modem recovery** via `ubus recover` / `medvemodem recover`: MM session/radio reset, driver rebind and USB reauthorization only on that modem, then validate return to service.
- **Serialize recovery** with dial, configuration, eSIM and AT; capture state and release leases before destructive stages.
- **Persistent bench pre-releases** `bench-v<version>-<commit>` with aarch64 archives, independent of 14-day CI artifacts; production releases remain manual.

## 0.13.0-r3 — 2026-10-02

No standalone bench release.

- **Profile-governed L0–L7 recovery policy:** causes, eligible actions based on profile evidence (field-reported strong actions manual-only, bench-validated stages automatic), hard limits, exponential backoff and Manual intervention required state.
- **Do not count self-inflicted instability:** events during recovery and for 30 s afterward are retained but marked `requested` and excluded from spontaneous-failure metrics.
- **Recovery capabilities** added to profiles; T99W175 radio restart/reset marked field-reported.
- **CI:** run lightweight host tests on push, full package builds on demand at this historical revision.

## 0.13.0-r2 — 2026-10-02

No standalone bench release.

- **MM bearer IP session:** IP, DNS and uptime from the active ModemManager bearer in modem record.
- **Port probing beside MM:** a modem explicitly released from MM to native backend can still be probed; previously presence of MM globally caused refusal.
- **Bench `--only-uplink`:** permit testing when the test modem is the router's only Internet access but not carrying the SSH session; probe AT/ATI in native mode with redaction.
- **Bench observations F4/F5:** SIM PIN2 despite connected session, and generic USB strings shared by T99W175 `05c6:9025`.

## 0.13.0-r1 — 2026-10-01–02

No standalone bench release.

- **Native FM350 3GPP AT dial without MM or external manager:** profiles `fibocom-fm350` `0e8d:7127` and `fibocom-fm350-rndis` `0e8d:7126`, netifd `proto medvemodem` with IP/route/DNS, respecting `defaultroute=0`, route metric and `peerdns=0`; hangup on shutdown. CLI `dial`/`hangup`; reject APNs that would inject AT syntax.
- **First LuCI read-only dashboard:** modem cards with session and faults, stale-data indication after RPC error, USB-port disambiguation, friendly no-SIM status; menu under Modem.
- **PIN2 is not a full SIM lock**; modem with PIN2 can still connect.
- **MM-assisted profile specificity:** QMI T99W175 identified by MM model rather than shared USB strings; normalize MM session state.
- **Design:** network DNS checks, failover, telemetry documented as planned.

## 0.12.0-r1 — 2026-10-01

No standalone bench release.

- **eSIM through engine AT APDU bridge:** `esim-apdu` passes lpac APDU through `AT+CCHO/CGLA/CCHC` using one leased tty; fix long-response chain hang that killed FM350 eSIM downloads after exactly 240 s.
- **Find eUICC port:** briefly probe CCHO on eligible tty ports, remember matched port for current USB generation, close leaked logical channels. Skip this path on Sierra, whose CCHO hangs SIM interface.
- **Pinned `medvemodem-lpac` build:** upstream lpac 2.3.0 stdio APDU driver was broken; build fixed git commit and optional uqmi/mbim/qmi drivers under `/usr/lib/medvemodem/lpac` without colliding with system lpac.
- **`medvemodem-bench` test harness:** sequential MM/native/external-owner session/IP/route/ping/DNS tests, redacted diagnostics and before/after config comparison. Refuse if SSH session crosses test modem/default route.
- **Planned LuCI mockups:** eSIM, SMS, operator/SIM, contacts, risk-ranked AT presets, diagnostics/repair, dashboard and settings. Technical design discussed SIM cooling and whitelist handling; not yet functionality.

## 0.11.0-r1 — 2026-10-01

No standalone bench release.

- **eSIM phase 1 (read only):** CLI `medvemodem esim`, ubus `esim_status`/`esim_action` for chip probe/version/profiles through lpac.
- **Transport respects owner:** unused AT port when free, MBIM via mbim-proxy under MM; refuse QMI owned by MM/native/external with `owner_busy`.
- **One exclusive eSIM task at a time**, pause connect operations, reject replies from previous USB generation; timeout is not equivalent to no eUICC.
- **Documentation:** migration from `/etc/config/lpac-esim` and more zoo pages for T99/T77/FM350/L850.

## 0.10.0-r1 — 2026-10-01

No standalone bench release.

- **One control backend per modem:** `option backend` auto/modemmanager/native/external; T99 via MM and FM350 via external manager can coexist.
- **MM inhibition:** `mmcli --inhibit-device` for native/external-owned modems.
- **External manager auto-probe temporarily disabled** while it owns at least one delegated modem; restore previous setting when no longer used; use its own scripts for handoff and preserve user-created sections in Auto mode.
- **No native AT/metrics/probe** into externally managed ports.

## 0.9.0-r1 — 2026-10-01

No standalone bench release.

- **Leased AT console, metrics and port probe:** `medvemodem at`, `metrics`, `probe`.
- **Safe command policy:** forbid dangerous classes, dialing or online transitions; restore tty flags after access.
- **Cancel stale AT replies** when a higher-priority operation preempts the worker.
- **Respect MM-owned ports**, read its metrics instead, and use probes to refine port roles.

## 0.8.0-r1 — 2026-10-01

No standalone bench release.

- **`medvemodem bringup <id>`:** configure netifd, reload as needed, ifup and wait for IP/default route; roll back only the owned changes on failure.
- **Native QMI/MBIM only without active MM** and with uqmi/umbim available; never run simultaneously with MM.
- **Session states from netifd:** connecting/connected/disconnected, IPv4/IPv6, routes and error codes via events; flag MM takeover as an ownership conflict.

## 0.7.0-r1 — 2026-10-01

No standalone bench release.

- **Transactional UCI/network edits:** persist an on-flash journal before commit, roll back interrupted actions at startup, expose manual rollback for last device transaction.
- **Preserve user edits:** only treat a field as engine-owned while it still contains the engine's value; do not adopt or override pre-existing user-configured interfaces.
- **Respect LuCI's uncommitted changes** using dedicated engine transaction directory.
- **Choose netifd protocol:** modemmanager for MM, DHCP for router-mode modem; use UCI administrator preferences.
- **`medvemodem config` and ubus plan/apply/release/rollback/state** APIs, followed by network/firewall reload.

## 0.6.0-r1 — 2026-10-01

No standalone bench release.

- **Redacted diagnostic snapshots:** `medvemodem diag quick`, `diag profile <id>`, `diag diff`, stored under `/tmp/medvemodem/diag`.
- **Device-scoped report:** USB composition, profile decision/reason, history and kernel lines; hash serial with per-router salt and remove IMEI/IMSI/ICCID, phone numbers and secrets.
- **H1–H18/F1–F3 field reports** formalized as verifiable evidence and tests.

## 0.5.0-r1 — 2026-10-01

No standalone bench release.

- **Port-role resolution** from MM and kernel drivers, not tty numbering. Unknown vendor ports remain unknown.
- **Exclusive modem owner and port leases:** higher priority recovery > session > operator > SMS/USSD > diagnostics > metrics, max 120 s; reset leases after USB reconnect, never lease MM-owned or data ports.
- **ubus lease APIs** `lease_acquire/release/renew/leases` and `medvemodem.lease` event.

## 0.4.0-r1 — 2026-10-01

No standalone bench release.

- **Correlate MM modem objects to physical USB** through sysfs and port matching. In ambiguous conflict don't map either, preventing cross-modem data leakage.
- **Async bounded mmcli:** child process with 15 s timeout, event/request driven without idle polling; failures never block daemon.
- **SIM lock/radio off** reported; repeated MM object recreation on same USB becomes an instability/ownership-race event.
- **Publish `medvemodem.device` and `medvemodem.stability` ubus events.**

## 0.3.0-r1 — 2026-10-01

No standalone bench release.

- **Per-modem event history and stability assessment:** stable/degraded/flapping, disconnect and driver rebind counts, probable USB vs driver cause. CLI `events`, ubus `events`, RAM history survives daemon restart within same boot.
- **Suppress hotplug noise:** don't log every tty/WDM/netdev appeared/disappeared; coalesce multi-interface rebind into one episode; hourly counters for 24 hours.
- **Driver health:** `unbound`, `incomplete` and `suboptimal` statuses; exempt serial-only PPP modems.
- **Design:** SIM/APN selection, dual slots, eSIM, SMS alerts.

## 0.2.0-r1 — 2026-09-30–10-01

No standalone bench release.

- **First `medvemodemd` procd service and ubus object** with status/devices/device/lookup/rescan, event-driven idle.
- **Kernel uevent settling:** debounce by `settle_ms`, bound repeated hotplug, ignore virtual noise; UCI logging/settle options.
- **USB generations, first/last appearance and limited disconnected-device history.**
- **Knowledge ingestion:** normalize modem-manager observations and port recipes, classify AT risk and forbid IMEI writing; create Modem Zoo pages for FM350/T99/T77/RM520N/Quectel/Fibocom and field cases.

## 0.1.0-r1 — 2026-09-30

No standalone bench release.

- **Initial OpenWrt medvemodem package** for 24.10/25.12 including aarch64_cortex-a53; configurable default engine.
- **USB modem registry and weighted profile matching** across ModemManager/QMI/MBIM/NCM/HiLink plus known USB/PCI IDs/compositions.
- **Stable identity without serial:** bind to physical USB topology rather than changing bus number; reject obvious fake serials.
- **Show storage-mode and service-mode devices**, detect usb_modeswitch configuration-0 anomalies; distinguish EDL/crash dump/fastboot and don't assign live modem profiles.
- **Foundational safety design:** 18 unsafe first-connection actions (NV/EFS writes, USB composition switches, FCC locks), classification by risk, stepped recovery, low-resource ubus/uloop daemon and UI/TUI concepts.

## Before 0.1.0 — 2026-09-30

No standalone bench release.

- **Project initiated:** studied other modem stacks for OpenWrt 24.10/25.12 to identify hardware behavior worth independently implementing.
- **Initial architecture/design:** device profiles, MM-first ownership, stable data sessions, third-party projects as knowledge sources, diagnostics and community profile submission workflow.


