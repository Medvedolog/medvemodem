# Huawei E3372 / HiLink

> **Status:** `external-reference-unvalidated` · Huawei, HiLink embedded router.

Web API/router ownership remains even when debug tty appears; debug may be volatile.

## Identity and owner

- Model aliases: `e3372`, `e3372h`, `hilink`.
- USB/PCI IDs are **candidates**, not physical identity or proof of supported transport.
- Resolve with sysfs topology, USB interface descriptors, driver binding, identity from authorised control owner and firmware.
- Any composition, APDU, reset or bootloader operations are reference-only until validated on a matching physical device.

## Profiles in the build (2026-10-18)

| USB ID | What | Owner | Profile |
|---|---|---|---|
| 12d1:1f01 | CD-ROM before the switch (OpenWrt usb-mode.json → 14db/14dc) | — | none (not a modem yet) |
| 12d1:14db | HiLink web firmware: E3372h, E8372h (Wi-Fi; MTS 8211F) | web | `huawei-hilink-12d1-14db` |
| 12d1:14dc | E3372h, web only | web | `huawei-hilink-12d1-14dc` |
| 12d1:1566 | E3372h debug (AT added, gone after a reboot) | web | `huawei-e3372-hilink-debug-1566` |
| 12d1:155a | E3372h «Gateway NCM» stick mode, NCM + AT | ModemManager | `huawei-12d1-155a` |
| 12d1:1506 | E3372h/E3272/E3276/E392 stick, NCM + AT | ModemManager | `huawei` (family) |
| 12d1:155e, 156c, 1573, 15bb, 15c1, 15c3, 1f1a | E3272/E3372/E3276-class sticks with AT | ModemManager | `huawei` (family) |
| 3566:2001 | Brovi E3372-325 | web | `brovi-e3372-325-hilink` |

One HiLink USB ID covers several models: the card's model comes from the stick's web (`DeviceName`, kept by the
engine), not from the profile. Sources: another modem manager 3.5.8 (msw/hilink.sh: 12d1:14db = E8372 / MTS 8211F;
modemswitch.sh HILINK_IDS; hilink.sh; msw/identity.sh 14dc ↔ 1566 ↔ 1442; modem/usb/12d1*), the imported catalog,
HANDOFF 126/164. Not on our bench.

## E3372-325 web, the owner's HAR (2026-10-30)

An E3372-325 (vendor name «ZOWEE» in `POST /api/device/vendorname`), Tele2 RU, LTE B1. What its web does that the
engine did not read or do yet (the signal block `device/signal` was already read):

- `device/signal`, unread fields: `ulbandwidth`, `txpower` («PPusch:0dBm PPucch:999dBm …», 999 = none), `rrc_status`,
  `transmode` («TM[1]»), `ulfrequency`/`dlfrequency` in kHz, `enodeb_id`, `cqi0`/`cqi1`.
- `net/net-mode` (`NetworkMode` 00 auto / 03 LTE / 02 3G, `NetworkBand`, `LTEBand` hex masks) and `net/net-mode-list`
  (the modes and band masks this unit allows, with names): the radio settings of a HiLink, read and written with
  `POST net/net-mode`.
- `POST net/reconnect` `<reconnect_action>1</reconnect_action>` → OK: the session reconnects without a reboot (a
  session level for a HiLink in the ladder).
- `dialup/mobile-dataswitch` (`dataswitch`), `dialup/connection` (auto dial, roaming auto connect, MTU),
  `dialup/profiles` (the APN list; a `POST` is RSA-encrypted with `webserver/publickey`).
- `monitoring/start_date` (billing day, `DataLimit`, thresholds, data off at the limit), `month_statistics`
  (`CurrentDayUsed`, `CurrentDayDuration`, `MonthLastClearTime`), `POST monitoring/clear-traffic`.
- `pin/status` (`SimState` 257 ready, PIN and PUK tries), `net/csps_state`, `net/register` (manual operator),
  `dhcp/settings` (the stick's own subnet: `DhcpIPAddress`, range; for the LAN clash), `security/dmz`, `sms/config`
  (SMS centre, delivery reports; the centre number is not stored).
- `monitoring/daily-data-limit` answers error 100002 (not supported) on this unit.

## Related reference

- [USB composition and boot mode recipes](../COMPOSITION-AND-BOOT-MODES.md)
- another modem manager source map

<!-- 4pda-field-2026-10-15:start -->
## 4PDA — дополнительные модельные полевые факты

**Reference only; not hardware-verified.** Данные из пользовательских HTML-архивов; связь `message-N` — только с локальным архивом. Нет новых runtime presets/AT operations. Все кейсы и SHA256: [4PDA field notes](../4PDA-FIELD-NOTES.md).

- **`4PDA-PRIMARY-MM-P16`** (danger): Huawei E3372 Stick vs HiLink/flash mode depends on precise variant and firmware. **MedveModem:** No blanket HiLink-to-Stick command migration. [4PDA topic](https://4pda.to/forum/index.php?showtopic=582284).

<!-- 4pda-field-2026-10-15:end -->


<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### huawei-e3372-debug-volatile

**Observation:** Huawei E3372 HiLink debug temporarily creates AT ports but reverts on reboot; for band/RAT use HTTP API rather than AT^SYSCFGEX.

- **Scope:** models=E3372 HiLink; usb_ids=12d1:14dc, 12d1:14db, 12d1:1566
- **Type / confidence:** `composition-switch` / `external-field-report`
- **Regression contracts:** web API must stay data-plane owner after debug appears; no composition switch from AT^SYSCFGEX as band-setting shortcut
- **Safety:** Do not assume all E3372 firmware shares same IP/API.
- **Original source:** issue / release / pinned source

### Composition and boot-mode recipes (reference only)

Exact operation stages, exit checks and prerequisites: [composition matrix](../COMPOSITION-AND-BOOT-MODES.md).

- **`huawei-hilink-debug`** — `COMPOSITION_MUTATION`; E3372 HiLink DHCP/router and web API → HiLink with temporary AT debug interfaces; explicit approval required; Debug disappears on reboot for observed Huawei E3372.
- **`huawei-at-composition-hazards`** — `PERSISTENT_COMPOSITION_WRITE`; E3372 web-debug or modem-mode with tty interfaces → no automatic composition change; explicit approval required; No universal SETPORT/U2DIAG map across Huawei submodels; changing it can lose data/AT ports.

<!-- upstream-another modem manager-v3.5.2:end -->
