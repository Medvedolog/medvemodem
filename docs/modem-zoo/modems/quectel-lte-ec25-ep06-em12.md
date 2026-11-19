# Quectel EC25 / EG25 / EP06 / EM12 LTE family

> **Status:** `multi-source-reference`.

These generations share a recognizable Quectel command vocabulary, but **not one universal mask table or mode encoding**. Zoo therefore documents the common grammar and the important per-model differences.

## Historical and current evidence

### EP06

other scripts's imported composition table records `2c7c:0306` with historical hints:

- AT: `ttyUSB2`;
- SMS: `ttyUSB3`.

They are only historical observations.

a field report's live EP06-E profile establishes:

- WCDMA B1/B5/B8;
- no GSM radio;
- `nwscanmode 0` = Auto;
- `nwscanmode 2` = WCDMA;
- `nwscanmode 3` = LTE.

### EM12-G

Current profile uses `QCFG="band"` with its own larger LTE mask and writes it as:

`AT+QCFG="band",0,<lte-mask>,0,1`

That form must not be blindly copied to every Quectel generation.

### EC25 / EG25

They belong to the same broad QENG/QCFG telemetry tradition, but field layouts and available CA/band functions differ by firmware. Use family knowledge as a candidate set, not as a proof of support.

## EP06-E parser and ownership boundaries (19 November 2026)

- `AT+QTEMP` is now queried beside `AT+QCAINFO` **only** for the exact `quectel-ep06` profile through an engine-owned AT port. The returned sensor array may contain three or four signed values; the highest valid temperature becomes `heat.modem_c`. Missing sensors (`255`, `-140`, `-32768`), errors and nonnumeric values do not become temperatures. The production parser has nine host-compiled fixtures.
- Existing `QCAINFO` parsing distinguishes configured/deactivated LTE SCC (`state=1`) from activated (`state=2`). A response containing only `OK` does **not** prove the modem lacks CA.
- On EP06-E `QENG=\"servingcell\"` SINR is already in dB, and the LTE bandwidth index converts `0..5` to `1.4/3/5/10/15/20 MHz`. Neighbour entries put RSRQ before RSRP. The two physical receive chains are not the same as the four positions in `QRSRP`.
- Since Alpha 1 r8, native EP06-E includes **QNWLOCK read/write/rollback in the actual engine**. `common/4g` is `count,EARFCN,PCI,...`, while `common/lte` is `action,EARFCN,PCI,status` (action 2 means EARFCN+PCI). Both forms are probed read-only; a malformed/pending/ambiguous state disables writing. Full multi-cell snapshots are kept for rollback, and an exclusive engine-owned AT lease plus link guard are mandatory. The feature is **not** accessible while ModemManager/QModem owns AT, and live EP06-E firmware validation is still outstanding.
- ModemManager remains the automatic owner on the known working EP06-E configuration. While it owns the modem, no direct AT polling (including temperature) is attempted; use MM-owned telemetry, and treat optional AT-only fields as unavailable. Changing ownership is a manual opt-in with connectivity risk.
- EP06-E SIM-slot reports are to be verified via the current owner's QMI UIM interface. Never infer that `AT+QUIMSLOT?` works from other Quectel generations.

## Native QMI (EP06-E)

The EP06-E profile explicitly offers MedveModem native QMI when `qmi_wwan` exposes the control node and network device. This is a **manual opt-in**, not a change to the automatic QModem/ModemManager preference. Use the existing ownership handover and netifd QMI path; never share QMI control with ModemManager. Changing backend can disrupt an active connection and must keep recovery access available. Verify the resulting interface and bearer after switching; this profile declaration is not a field-test result.

## Telemetry

Commonly useful commands include `QENG`, `QCAINFO`, `QNWINFO`, `QRSRP` and `QTEMP`. Even identical command names can return different field layouts, so parsers should be tied to model/firmware evidence.

## SMS / USSD

other scripts contributes useful generic Quectel SMS/PDU/storage experience, while our a field report evidence gives an important model-specific negative result:

**EP06-E / EG06-E USSD is field-recorded as not working**: the modem accepts `AT+CUSD` but no network result arrives. This is deliberately **not** generalized to EP06-A.

## Known AT command corpus

| Command | Category | Risk | Purpose | Source |
|---|---|---|---|---|
| `AT+QENG="servingcell"` | cell.telemetry | **READ** | Serving-cell detail; exact response fields differ across EC/EP/EM firmware. | a field report common Quectel profiles |
| `AT+QENG="neighbourcell"` | cell.telemetry | **READ** | Neighbour-cell scan/report. | a field report/QModem |
| `AT+QCAINFO` | radio.ca | **READ** | Carrier aggregation state and component carriers. | a field report/QModem |
| `AT+QNWINFO` | radio.serving | **READ** | RAT/band/channel/operator summary. | a field report/QModem |
| `AT+QRSRP` | radio.signal | **READ** | Per-branch LTE RSRP on supported firmware. | a field report |
| `AT+QTEMP` | hardware.temperature | **READ** | Temperature sensors on supported Quectel firmware. | a field report/QModem |
| `AT+QCFG="band"` | radio.band | **READ** | Read Quectel legacy band masks. | a field report EP06/EM12 |
| `AT+QCFG="band",<gw-mask>,<lte-mask>,<td-mask>[,<effect>]` | radio.band | **MUTATING** | Write legacy Quectel band masks. Exact argument count/semantics are firmware-specific. | a field report common Quectel profiles |
| `AT+QCFG="band",0,<lte-mask>,0,1` | radio.band | **MUTATING** | EM12-G form used by current archived modem utility profile to replace LTE mask. | a field report EM12-G profile |
| `AT+QCFG="nwscanmode"` | radio.mode | **READ** | Read legacy network scan mode. | a field report/QModem |
| `AT+QCFG="nwscanmode",0` | radio.mode | **MUTATING** | Auto mode on EP06-E field profile. | a field report EP06-E |
| `AT+QCFG="nwscanmode",2` | radio.mode | **MUTATING** | WCDMA-only on EP06-E field profile. | a field report EP06-E |
| `AT+QCFG="nwscanmode",3` | radio.mode | **MUTATING** | LTE-only on EP06-E field profile. | a field report EP06-E |
| `AT+QCFG="usbnet"` | transport.composition | **READ** | Read USB network mode. | a field report/QModem |
| `AT+QCFG="usbnet",<mode>` | transport.composition | **HIGH** | Change USB network mode; exact mode values are model/firmware-specific. | QModem/a field report safety model |
| `AT+QCCID` | sim.iccid | **READ** | ICCID on many Quectel LTE firmwares. | a field report common Quectel |
| `AT+CUSD=1,<code>[,<dcs>]` | ussd | **MUTATING** | Generic USSD request. EP06-E/EG06-E are field-recorded as not returning a network response despite command acceptance; do not generalize to EP06-A. | a field report quirks |
| `AT+CUSD=2` | ussd | **MUTATING** | Cancel USSD session. | generic AT / a field report USSD service |


## Main trap

“Quectel command exists” is not enough. For this family, mask width, parameter count, LTE/WCDMA availability and apply semantics vary enough that command syntax must remain model/firmware-scoped.

## Telegram field evidence — EP06 USSD conflict

A Telegram field script reports working USSD through **ModemManager / `mmcli --3gpp-ussd-*`** on an unspecified EP06 in a QMI setup. This conflicts with the existing EP06-E/EG06-E field evidence where raw `AT+CUSD` produced no useful network response.

Zoo deliberately keeps both observations:

- the negative result is specifically EP06-E/EG06-E and raw-AT oriented;
- the positive report does not identify EP06 variant/firmware and uses the MM owner path.

Until reproduced on identified hardware, USSD here is **variant/control-path dependent**, not a family-wide boolean. The reusable architectural lesson is to use the owner's API when ModemManager already owns the modem rather than opening the AT port in parallel.

See [Telegram field notes](../TELEGRAM-FIELD-NOTES.md).

<!-- 4pda-field-2026-10-15:start -->
## 4PDA — дополнительные модельные полевые факты

**Reference only; not hardware-verified.** Данные из пользовательских HTML-архивов; связь `message-N` — только с локальным архивом. Нет новых runtime presets/AT operations. Все кейсы и SHA256: [4PDA field notes](../4PDA-FIELD-NOTES.md).

- **`4PDA-994474-1530`** (read): На Mikrotik после некоторого времени пропадает B7+B7 CA; AT+QCAINFO возвращает только OK, ручной restart LTE-interface временно помогает. Сообщено для MBIM и QMI. **MedveModem:** Вводить отдельный статус CA lost (без reboot); отслеживать контекст RRC/нагрузка и оценивать soft reconnect до reset. [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474).
- **`4PDA-994474-32204`** (danger): После обновления firmware 04→05/06 автор сообщил исчезновение B3+B7 CA; возврат к 04 дал PCC B3 + SCC B7 и B7+B7 в последующих пробах. Комбинация и скорость менялись со временем. **MedveModem:** Откат FW не автоматизировать. Логировать действующие PCC/SCC, SINR, bandwidth и firmware до/после, а не «поддерживаемые бэнды». [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474).
- **`4PDA-994474-8569`** (write): После смены EP06 на MBIM `AT+QCFG="usbnet",2` модем перестал отвечать через YAT, а Mikrotik не увидел data interface. **MedveModem:** После смены режима прогнозировать изменение USB-узлов и доступности AT, recovery по USB topology. [4PDA topic](https://4pda.to/forum/index.php?showtopic=994474).

<!-- 4pda-field-2026-10-15:end -->


<!-- external-another package-modem5g-2026-10-16:start -->
## another package / another package — external field facts (2026-10-16)

**Reference only; not bench-validated.** Checked against the pinned upstream files and the issue/PR pages; no upstream code, no runtime preset. All cases: another package / another package field notes.

- **`qdc507-2c7c0125-identity-and-sms-archive-pr51`** (`field-reported`, external-unmerged-pr): An open, unmerged PR adds a QDC507 that enumerates as 2c7c:0125, the same VID:PID as the EC25 family, and a local SMS inbox for it. Its rules: a modem copy is deleted only after the archive is written, synced, read back, and the SIM identity and the exact PDU in that slot are re-checked; on a save failure, a SIM change or a reused slot the message stays on the modem. SMS send, USSD and other AT traffic share one serial lock. Source: another package/5G-Modem-Support PR #51 (open, not merged; opened 2026-10-10).

<!-- external-another package-modem5g-2026-10-16:end -->
