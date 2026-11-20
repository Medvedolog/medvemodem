# ZTE MF79 / MF286 / Tenda MF6 / OLAX

> **Status:** `external-reference-unvalidated` · ZTE and ODMs, ZTE cellular router / USB modem.

USB storage eject, goform/web debug API, model-specific persistence; do not mistake Wi-Fi router for AT modem.

## Identity and owner

- Model aliases: `mf79`, `mf286d`, `tenda mf6`, `mf922`, `olax f90`.
- USB/PCI IDs are **candidates**, not physical identity or proof of supported transport.
- Resolve with sysfs topology, USB interface descriptors, driver binding, identity from authorised control owner and firmware.
- Any composition, APDU, reset or bootloader operations are reference-only until validated on a matching physical device.

## Profiles in the build (2026-10-18)

| USB ID | What | Owner | Profile |
|---|---|---|---|
| 19d2:0542, 0548, 0564 | CD-ROM; StandardEject → 19d2:0565 (rule may be missing from OpenWrt's usb-mode.json) | — | none: the engine switches nothing |
| 19d2:0167 | MF821 | ModemManager | `zte` (family) |
| 19d2:0189 | MF28D / MF290 | ModemManager | `zte` |
| 19d2:1275 | P685M / ZM8630A | ModemManager | `zte` |
| 19d2:1432 | MF286 / MF286A | ModemManager | `zte` |
| 19d2:1485 | MF286D / MF289F (SMS in storage MT) | ModemManager | `zte` |
| 19d2:1489 | MF286R | ModemManager | `zte` |
| 19d2:1402 | MF823 in NCM | ModemManager | `zte` |
| 19d2:9994 | MF831 in MBIM | ModemManager | `zte` |
| 19d2:1405, 1403, 1404, 1244 | MF823 / MF667-class in the web (RNDIS) composition | web (goform) | `zte-web-19d2-<pid>` |
| 19d2:0557, 0558 | ZTE web sticks (MF79 and relatives) | web | `zte-web-19d2-<pid>` |
| 19d2:0581 | MF79 family with serial functions added; signal still from its web | web | `zte-web-19d2-0581` |
| 19d2:1557 | Tenda MF6 (web; its debug survives a reboot, never switched on by us) | web | `zte-web-19d2-1557` |
| 19d2:1218 | MF192 (MTS 3G; «MF652» in its USB product string, «MF192» from ModemManager): interfaces 1–8 CDC ACM (ttyACM), 9 storage, no network function; a tester's OPiZ3, 2026-11-09 | ModemManager (PPP) | `zte`; detected from r178 (an all-ACM modem was dropped before) |

What the engine does through a ZTE goform web (r95 SMS, r96 the reqproc paths, 2026-11-03 the rest; none of it on
our bench): the signal, SMS (list, send, delete), and since r151 the actions a HiLink's web had: «Modem data» (the
operator's address, the session's and the month's traffic, the model and firmware, the network mode), USSD
(USSD_PROCESS, ussd_write_flag 16, ussd_data_info in UCS-2 hex), the network mode (SET_BEARER_PREFERENCE NETWORK_auto
/ Only_GSM / Only_WCDMA / Only_LTE, the stick's net_priority after a line break, as another modem manager's zte_setmode),
the mobile session again (DISCONNECT_NETWORK, CONNECT_NETWORK: the recovery ladder's radio step) and the restart
(REBOOT_DEVICE: its reboot step). A write the web refuses goes once more with AD = md5(md5(wa_inner_version +
cr_version) + RD), which newer MF79U/MF833-class firmwares ask; an older one never gets it. Login: sha256(sha256(pw)
+ LD) where the web gives LD, the base64 password otherwise (another modem manager sends only the base64 one). Never: the
debug mode (SET_DEVICE_MODE), the factory reset, the IMEI/IMSI/phone number.

ZTE AT set (AT+ZRSSI, ZCELLINFO?, ZCAINFO?) has no engine dialect yet: radio through ModemManager. Sources:
another modem manager 3.5.8 (modem/usb/19d2*, hilink.sh goform, msw/hilink.sh, lib.sh, usbports.sh, collect.sh,
usb-mode-extra.json), the imported catalog, HANDOFF 126/162/164. Not on our bench.

## MTS 8810FT (19d2:1557, ZXIC), a tester's OPiZ3, 2026-11-11

Field report; no identifier kept. USB «ZXIC,Incorporated» / «MTS Mobile Boardband» (sic), cdc_ether, a router inside
(192.168.1.1), firmware `XFNJ_B03P14_F232E_V3.0_RR_FX_MTS_D_P019`, web server «Sanechips-Webs» — the ZX297520 platform of
Sanechips (ZTE's chip arm), likely an ODM build for MTS; the same USB ID as the Tenda MF6.

- The ZTE goform web: `GET /goform/goform_get_cmd_process?isTest=false&multi_data=1&cmd=…` answers without a login and
  without a Referer; `reqproc/proc_get` is not there.
- Its HTTP headers end in a bare LF LF and carry no Content-Length (curl reads it; the engine read it from r181).
- Standard fields: `network_type`, `network_provider`, `rssi`, `lte_rsrp`, `cell_id` (decimal: 1703969 = eNB 6656 cell
  33), `wa_inner_version`; `lte_rsrq`, `lte_snr`, `lte_pci`, band and channel empty, `lac_code` 0. The ZXIC firmwares keep
  them in `nv_*` (another modem manager's Olax MT80 notes): asked apart from r181, to be confirmed on this stick.
- Through the engine's web forward its page showed «modem_removed» while the stick was online: to look at.

## ZTE MF192 (19d2:1218), a tester's OPiZ3, 2026-11-09

Field report (rank 1 in SOURCE-HIERARCHY.md); no identifier kept. MTS-branded 3G stick, firmware `BD_MF192V1.0.0B02`
(«Eagle_Raptor_R2.4.1»), USB product string «MF652», ModemManager names it MF192.

- USB: interfaces 1–8 bound by `cdc_acm` (ttyACM0…), 9 `usb-storage` (the MTS installer and, by its description, a
  microSD slot); no network function: PPP. Before r178 the engine dropped it (no modem driver, only ACM).
- ModemManager 1.24: plugin `zte`, ports ttyACM0, ttyACM1 (primary), ttyACM3 all AT; power on, state `disabled` until
  someone enables it.
- Modes: 2G only, 3G only, 2G+3G preferring 2G or 3G (current: 3G preferred). Bands egsm, dcs, pcs, g850, utran-1,
  utran-8. IPv4 only.
- **`enabled locks: net-pers`**: locked to its operator (MTS); another operator's SIM is not taken. To be said on the card.
- Not tried yet: `AT+ZSNT` (mode), `AT+ZPAS?` (network type), `AT+ZCDRUN=8` (no installer disk at start) — ZTE commands
  from the family, to check on this stick before a profile uses them.

## Related reference

- [USB composition and boot mode recipes](../COMPOSITION-AND-BOOT-MODES.md)
- another modem manager source map

<!-- 4pda-field-2026-10-15:start -->
## 4PDA — дополнительные модельные полевые факты

**Reference only; not hardware-verified.** Данные из пользовательских HTML-архивов; связь `message-N` — только с локальным архивом. Нет новых runtime presets/AT operations. Все кейсы и SHA256: [4PDA field notes](../4PDA-FIELD-NOTES.md).

- **`4PDA-PRIMARY-MM-P15`** (read): MF833R firmware did not accept ZCDRUN commands used by other ZTE variants. **MedveModem:** No vendor-family blanket AT command permission. [4PDA topic](https://4pda.to/forum/index.php?showtopic=968586&st=180).

<!-- 4pda-field-2026-10-15:end -->


<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### zte-cdrom-eject

**Observation:** ZTE 19d2:0542/0548/0564 boot as storage and can eject to modem mode with USB modeswitch; 19d2:0565 may appear afterward.

- **Scope:** pre_usb_ids=19d2:0542, 19d2:0548, 19d2:0564; post_usb_ids=19d2:0565
- **Type / confidence:** `composition-switch` / `external-field-report`
- **Regression contracts:** mode switch only on storage-class composition; allow detach/re-enumeration without declaring firmware crash
- **Safety:** Class and actual sysfs driver must be checked before ejection.
- **Original source:** issue / release / pinned source

### zte-mf6-debug-persistence

**Observation:** Tenda MF6 19d2:1557 debug enabled via web API and survives reboot; its band changes use AT+ZLTEBAND.

- **Scope:** model=Tenda MF6; usb_ids=19d2:1557; firmware=device-dependent
- **Type / confidence:** `composition-switch` / `external-field-report`
- **Regression contracts:** web debug is a state mutation, not a passive metric read; do not assume Huawei-style debug persistence
- **Safety:** Web UI credentials/authorization required; do not auto-enable debug.
- **Original source:** issue / release / pinned source

### Composition and boot-mode recipes (reference only)

Exact operation stages, exit checks and prerequisites: [composition matrix](../COMPOSITION-AND-BOOT-MODES.md).

- **`zte-mf6-web-debug`** — `COMPOSITION_MUTATION`; Tenda MF6 19d2:1557 web/API only → web-managed router with AT debug enabled; explicit approval required; Unlike Huawei E3372, MF6 debug persists.
- **`zte-cdrom-start-eject`** — `COMPOSITION_MUTATION`; 19d2:0542 / 0548 / 0564 USB mass storage → working modem composition (possibly 19d2:0565); explicit approval required; Never eject a modem already in active data composition.

<!-- upstream-another modem manager-v3.5.2:end -->


<!-- another modem manager-delta-v3.5.5:start -->
## another modem manager–v3.5.5 — release delta (2026-10-16)

**Reference only; not bench-validated.** Read from the diff v3.5.2..v3.5.5; no upstream code, no runtime preset. All records: another modem manager delta.

- **`…-v353-sms-ucs2-byte-wise-locale`** (`upstream-code-reference`, upstream-code-reference): The project's UTF-8 to UCS2 conversion for outgoing SMS (AT PDU and the ZTE web API) and its repair of received text walk the text byte by byte. Under gawk in a UTF-8 locale a multi-byte character is one character, so the byte walk broke: Cyrillic SMS were sent empty, and the repair pass re-encoded already valid UTF-8 a second time (doubled UTF-8 in the SMS header). v3.5.3 forces the C locale for every such conversion. Busybox awk is byte-based and was never affected. Source: a field report/another modem manager (another modem manager/root/usr/share/another modem manager/smspdu.sh, hilink.sh, lib.sh).

<!-- another modem manager-delta-v3.5.5:end -->


<!-- another modem manager-delta-v3.5.16:start -->
## another modem manager–v3.5.16 — release delta (2026-10-21)

**Reference only; not bench-validated.** Read from the diff v3.5.5..v3.5.16 at `a22590c108cc`; no upstream code, no runtime preset. Full review: another modem manager delta.

- **`…-v3514-mf286d-qmiraw-modes-bands`** (`19d2:1485`) — `upstream-code-reference`, not-bench-validated: v3.5.14 moves the MF286D to their own QMI proto because the stock qmi.sh power-cycles a SIM that is still initialising: before bring-up it waits up to 30 s for the SIM (PIN status / UIM SIM state no longer 'not_initialized'; both answering 'Not supported' ends the wait) and skips network registration when the serving system is already registered or searching. Network mode is AT+ZSNT (0 auto, 1 2G, 2 3G, 6 4G, written as AT+ZSNT=&lt;n&gt;,0,0). Writing LTE bands with at+znlockband=1,0,&lt;hex&gt;,0 forces the modem to LTE-only, so the previous mode is read before and restored after unless it was 6. 2G/3G bands go through AT$QCBANDPREF index lists. Source: a field report/another modem manager @ `a22590c` (root/usr/share/another modem manager/mkiface.sh:862, root/lib/netifd/proto/qmiraw.sh:136-155, root/lib/netifd/proto/qmiraw.sh:345-348, root/usr/share/another modem manager/modemband/19d21485:17-112, root/etc/another modem manager/modem/atcmmds/zte.user).
  - AT$QCBANDPREF indices used: GSM 1800 -&gt; 8, GSM 900 -&gt; 9 and 10, GSM 850 -&gt; 24, PCS 1900 -&gt; 26, UMTS B1 -&gt; 27, UMTS B8 -&gt; 34; a write keeps indices it does not manage.
  - Model and firmware strings were cut when the modem gives no echo; the parse now strips the reply prefix by command, not by a character count (same fix for 19d2:1432, 2c7c:0512, 05c6:6000).
  - Their AT template set for MF28x: AT+ZRSSI, AT+ZCELLINFO?, AT+ZCAINFO?, AT$QCRSRP?, AT+ZPAS?, AT+ZSNT?, AT+ZULCA?, AT+CFUN=1,1 (the last one resets the module).
  - SMS storage for 19d2:1485 is always MT (see the storage record).
- **`…-v3515-mf286d-adb-temperature-chains`** (`19d2:1485`) — `upstream-code-reference`, not-bench-validated: MF286D temperature and per-antenna RSRP are available only from inside the module over adb: when adb is installed, they find the module by its USB serial (/sys/bus/usb/devices/&lt;path&gt;/serial) and run a shell with a 5 s timeout, reading cfg values lte_rsrp_1..4 (RSRP of each of the four receive chains) and, at most once a minute, the thermal zone whose type is mdm_case_therm (cached per USB path; accepted only when 0 &lt; t &lt; 150 C). Chains are shown only in LTE. Source: a field report/another modem manager @ `a22590c` (root/usr/share/another modem manager/modem/usb/19d21485:127-148).
  - Their full package now depends on adb because of this one model.
  - No AT or QMI command on this module gives the same temperature or per-chain values in their code.

<!-- another modem manager-delta-v3.5.16:end -->
