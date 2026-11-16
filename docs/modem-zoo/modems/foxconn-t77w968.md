# Foxconn T77W968 / Dell DW5821e family

> **Status:** `multi-source-reference` · Qualcomm Snapdragon X20 / LTE Cat.16 family.

This family supports both QMI and MBIM and has its own Foxconn AT semantics. It must not be treated as a smaller T99W175.

## Identity / variants

Current sources associate this command family with Dell/Foxconn identities including:

- `413c:81d7` — Dell DW5821e;
- `413c:81e0` — Dell DW5821e eSIM;
- `0489:e0b4` / `0489:e0b5` — Foxconn IDs in current another modem manager profiles.

other scripts tables also group `1bc7:1910/1911` with this broad family/AT heritage; OEM naming must be treated cautiously.

## QMI / MBIM / ECM

QModem explicitly records T77W968 modes:

- QMI;
- MBIM.

a field report's profile determines the live transport from the bound driver:

- `qmi_wwan` → QMI;
- `cdc_mbim` → MBIM;
- `cdc_ether` → ECM.

That live-driver evidence is stronger than fixed tty numbering. Historical a router maker's firmware user reports also show why the distinction matters: some T77W968 setups came up in MBIM while others were later observed in QMI after stack changes; the Zoo records capability and observed composition rather than declaring one universal default.

## FCC unlock

Affected Dell/Foxconn variants may need Foxconn FCC authentication before radio enable.

- with ModemManager owner: MM FCC unlock hook;
- with direct owner: QMI FCC operation;
- do not race both.

## Known AT command corpus

| Command | Category | Risk | Purpose | Source |
|---|---|---|---|---|
| `AT+TEMP` | telemetry | **READ** | Read temperature on firmware that supports +TEMP. | a field report 413c81d7 profile |
| `AT^TEMP?` | telemetry | **READ** | Read PA/TSENS/XO temperature sensors. | a field report 413c81d7 profile |
| `AT^CA_INFO?;^DEBUG?` | telemetry | **READ** | Combined CA and serving/secondary-cell diagnostics. | a field report 413c81d7 profile |
| `AT^VERSION?` | identity | **READ** | Read full firmware version. | a field report 413c81d7 profile |
| `ATI` | identity | **READ** | Generic identity/revision fallback. | a field report 413c81d7 profile |
| `AT+GSN` | identity | **READ** | IMEI. | a field report 413c81d7 profile |
| `AT+CIMI` | sim | **READ** | IMSI. | a field report 413c81d7 profile |
| `AT+ICCID` | sim | **READ** | ICCID. | a field report 413c81d7 profile |
| `AT^SLMODE?` | radio.mode | **READ** | Read RAT mode. T77W968 encoding differs from SDX55/T99W175. | a field report _foxconn_t77w968 |
| `AT^SLMODE=1,4` | radio.mode | **MUTATING** | Auto on T77W968. | a field report _foxconn_t77w968 |
| `AT^SLMODE=1,14` | radio.mode | **MUTATING** | WCDMA-only on T77W968. | a field report _foxconn_t77w968 |
| `AT^SLMODE=1,30` | radio.mode | **MUTATING** | LTE-only on T77W968. | a field report _foxconn_t77w968 |
| `AT^SLMODE=1,35` | radio.mode | **MUTATING** | WCDMA+LTE on T77W968. | a field report _foxconn_t77w968 |
| `AT^SLBAND=?` | radio.band | **READ** | Read supported LTE band set from current firmware. | a field report _foxconn_t77w968 |
| `AT^SLBAND?` | radio.band | **READ** | Read enabled/disabled WCDMA/LTE bands. | a field report _foxconn_common1 |
| `AT^SLBAND=LTE,2,<bands>` | radio.band | **MUTATING** | Replace LTE enabled-band set. | a field report _foxconn_common1 |
| `AT^ULCA?` | radio.ulca | **READ** | Read uplink carrier aggregation state. | a field report _foxconn_t77w968 |
| `AT^ULCA` | radio.ulca | **MUTATING** | Enable ULCA. No reliable disable form was found in the cited field corpus. | a field report _foxconn_t77w968 |
| `AT^GETLTECAT?` | radio.capability | **READ** | Read LTE category; cited profile notes Cat.16 on T77W968. | a field report _foxconn_t77w968 notes |


## Family-specific traps

### SLMODE encoding
T77W968 uses:

- `1,4` → Auto;
- `1,14` → WCDMA;
- `1,30` → LTE;
- `1,35` → WCDMA+LTE.

These values are not T99W175 masks.

### Band control
`AT^SLBAND=?` discovers the supported set from the actual firmware. `AT^SLBAND?` reads enabled/disabled bands. `AT^SLBAND=LTE,2,<bands>` changes the LTE enabled set.

### ULCA
The field corpus establishes `AT^ULCA?` and `AT^ULCA` (enable). It does not establish a reliable disable form, so Zoo does not invent one.

### Telemetry
The current another modem manager profile parses `AT^CA_INFO?;^DEBUG?` for serving and secondary carriers, PCI, EARFCN, RSRP/RSRQ/RSSI/SINR, RI, CQI, Tx power and antenna-chain data, with WCDMA fallbacks.

## Provenance

- a field report: current profile, band/mode/telemetry logic;
- QModem: QMI+MBIM model evidence;
- other scripts/another modem manager: historical IDs/port observations and MM-oriented bring-up;
- ModemManager/libqmi: owner-aware FCC path.

## Telegram field evidence — firmware profiles, transport rebinding and RFNV

- A a router maker's firmware recipe scoped to **F1.0.0.5.2** uses `AT^MCFG?` / `AT^MCFG=?` to inspect firmware profiles before any profile selection. Numeric profile IDs are firmware-specific.
- The same field recipe documents active RFNV `/nv/item_files/rfnv/00028874` and stock 2-antenna / 4-antenna variants. This is retained as destructive Expert/recovery knowledge; the calibration blobs themselves are deliberately not copied into Zoo.
- A real `413c:81d7` kernel log shows `cdc_mbim` binding first, then unregistering and being replaced by `qmi_wwan`; `cdc-wdm0`, `wwan0` and `ttyUSB0..3` are recreated around that transition. Live driver state therefore outranks assumptions derived from customer/setmode alone.
- other scripts's hotplug description explicitly stops/restarts `smstools3` so a COM port is not left held across replug — a concrete example of why serial leases must survive frontend/process boundaries.
- Customer 2 + setmode 2 is repeatedly associated with visible serial ports on some USB setups, but an internal-M.2 report behaves differently. Treat customer/setmode as bus/board/firmware-scoped.
- A later F2.0xx two-antenna report is intentionally kept **experimental**: downgrade and RFNV replacement efficacy were not established.
- One separate field report reproduced a lifecycle failure after `AT^RESET`: the T77 rebooted, radio/cell telemetry returned, but the network interface did not. This is only a single-device incident, but it is a useful recovery invariant — successful AT/registration telemetry is not proof that WDM/netdev ownership and the data session recovered.

See [Telegram field notes](../TELEGRAM-FIELD-NOTES.md) for provenance.

<!-- 4pda-field-2026-10-15:start -->
## 4PDA — дополнительные модельные полевые факты

**Reference only; not hardware-verified.** Данные из пользовательских HTML-архивов; связь `message-N` — только с локальным архивом. Нет новых runtime presets/AT operations. Все кейсы и SHA256: [4PDA field notes](../4PDA-FIELD-NOTES.md).

- **`4PDA-1102595-567`** (danger): В WH3000 Pro внутренний M.2 давал 413c:81d6 вместо ожидаемого 413c:81d7; автор сообщил результат после изоляции RESET pin67. ModemManager-интерфейс ломался после reboot, нативный MBIM /dev/cdc-wdm0 затем поднялся. **MedveModem:** Отличать hardware pin-state и драйверную ошибку; pin insulation — только ручная аппаратная процедура, не универсальная рекомендация. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1102595).
- **`4PDA-PRIMARY-MM-P11`** (read): a router maker's firmware AX3000 with DW5821e registered LTE but did not bring up a usable data bearer. **MedveModem:** Distinguish registered/network-data state. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1080524&st=400).

<!-- 4pda-field-2026-10-15:end -->


<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### t77-usbmode-config-zero

**Observation:** usb-mode.json config:0 removed cdc_mbim/wwan; AT remained; deleting harmful rule with backup alone may not restore already unconfigured device.

- **Scope:** usb_ids=413c:81d7, 413c:81e0, 413c:81e4, 413c:81e6, 413c:81d8; sysfs_config=0
- **Type / confidence:** `composition-hazard` / `external-field-report`
- **Regression contracts:** do not call composition 0 a valid data mode; keep last-known-good bConfigurationValue; only reapply config when data interfaces absent, not on healthy link
- **Safety:** Editing /etc/usb-mode.json alters another package; do not apply automatically in MedveModem.
- **Original source:** issue / release / pinned source

### t77-fastboot-pin67

**Observation:** Huasifei WH3000 Pro M.2 pin 67 grounded can force Dell DW5821e into fastboot every boot; a field report only sends reboot, does not flash.

- **Scope:** bootloader_usb_ids=413c:81d6, 413c:81e1, 1199:9070; non_bootloader_ids=413c:81e4, 413c:81e6; signature=ff/42/03 undriven only when single interface
- **Type / confidence:** `bootloader-rescue` / `external-field-report`
- **Regression contracts:** exclude EDL 05c6:9008; do not classify working 81e4/81e6 as bootloader; limit attempts and avoid concurrent flasher
- **Safety:** fastboot reboot is never a flash; manual opt-in in MedveModem; repeated bootloader entry requires fixing wiring.
- **Original source:** issue / release / pinned source

### hp-lt4120-usbmode-config-zero

**Observation:** HP lt4120 03f0:9d1d had four usbmode config:0 deconfigurations, taking away cdc_ether; module had no CD-ROM mode.

- **Scope:** model=HP lt4120 / T77W595; usb_ids=03f0:9d1d
- **Type / confidence:** `composition-hazard` / `external-field-report`
- **Regression contracts:** working-data-interface must veto config:0; missing data after deconfigure must be diagnosed before reset
- **Safety:** Different model than T77W968; share only failure-mode, not vendor AT dialect.
- **Original source:** issue / release / pinned source

### Composition and boot-mode recipes (reference only)

Exact operation stages, exit checks and prerequisites: [composition matrix](../COMPOSITION-AND-BOOT-MODES.md).

- **`dw5821e-usb-config-restoration`** — `DRIVER_COMPOSITION_REPAIR`; 413c:81d7/81e0 config 0 with no cdc-wdm/wwan → last known good USB bConfigurationValue; explicit approval required; Never treat config=0 as a selectable working mode.
- **`foxconn-fastboot-exit`** — `BOOTLOADER_EXIT_ONLY`; DW5821e true fastboot ff/42/03 or 413c:81d6 / 81e1 → normal USB modem, after one reboot; explicit approval required; 413c:81e4 and 413c:81e6 are WORKING DW5829e compositions, not fastboot.

<!-- upstream-another modem manager-v3.5.2:end -->
