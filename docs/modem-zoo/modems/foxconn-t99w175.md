# Foxconn T99W175 / Qualcomm SDX55 family

> **Status:** `multi-source-with-hazards` · encyclopedia page; command presence is not a recommendation.

T99W175 should not be reduced to “an MBIM modem”. The family is seen in separate MBIM and QMI/RMNET USB compositions, while QMI services can also be tunneled over an MBIM WDM control node.

## Identity and common compositions

| VID:PID | Composition / role |
|---|---|
| `1e2d:00b3` | MBIM |
| `1e2d:00b7` | QMI/RMNET |
| `05c6:90d5` | MBIM |
| `05c6:9025` | QMI/RMNET |

Historical another modem manager tty hints exist for all four IDs, but they are observations only.

a field report also documents a critical ID collision: Compal RXM-G1 / SG500M2-X can reuse some of these IDs with a different firmware and AT command set. VID:PID alone is not model identity.

## QMI, MBIM and QMI over MBIM

### Direct QMI
A QMI composition is identified from the live attachment by `qmi_wwan`. Typical observed IDs are `1e2d:00b7` and `05c6:9025`.

### MBIM
An MBIM composition is identified by `cdc_mbim`. Typical observed IDs are `1e2d:00b3` and `05c6:90d5`.

### QMI over MBIM
This is a **control subtransport**, not another USB composition. libqmi can send QMI services through the MBIM WDM node with `qmicli --device-open-mbim`.

Current a field report code uses that path for QMI extras/eSIM-related work when the WDM node is owned by `cdc_mbim`. The Foxconn FCC unlock path in ModemManager/libqmi uses the same principle.

The modeminfo lineage used around another modem manager explicitly exposes two QMI telemetry choices: **Direct QMI** and **QMI over MBIM**. Our a router maker's firmware package audit separately records another modem manager configuring the T99W175 stack with ModemManager ownership plus modeminfo `qmi_mode`/`qmi_proxy` on the WDM device. The exact `qmi_trap` choice is therefore a modeminfo control-path setting, not the data protocol name. Zoo separates:

1. network owner/data protocol;
2. telemetry/control protocol.

another modem manager may hand networking to ModemManager while companion modeminfo reads QMI-style telemetry through the WDM device. Where that WDM node is `cdc_mbim`, libqmi's MBIM-QMUX path is QMI-over-MBIM. Calling the whole stack merely “MBIM” or merely “QMI” loses the distinction between data ownership and control telemetry.

## Composition switching

Known field commands are firmware/customer dependent:

- `AT+USBSWITCH=00B3` — MBIM;
- `AT+USBSWITCH=00B7` — QMI/RMNET;
- `AT+USBSWITCH=90D5` — Qualcomm MBIM;
- `AT+USBSWITCH=9025` — Qualcomm QMI/RMNET;
- `AT^SETCONFIG=0` — newer-firmware MBIM selector reported in field documentation;
- `AT^SETCONFIG=1` — newer-firmware QMI selector;
- `AT^CUSTOMER=<n>` changes OEM/customer personality and can alter available/persistent compositions.

These operations can re-enumerate the modem or remove expected control ports.

Bench 2026-10-19 (our T99W175, DELL, `05c6:9025` QMI, firmware `T99W175.F0.6.0.0.6.GC.004.056`, Jan 19 2024,
`^CUSTOMER: 5`, `^PCIEMODE: 0`): `AT^USBSWITCH?`, `AT^USBSWITCH=?` and `AT^SETCONFIG?` all answer `ERROR`. No AT
composition selector is exposed on this firmware; the owner declined a blind `AT^SETCONFIG=0`. MedveModem offers no
composition switch for it (one USB configuration in its descriptors). `AT^CUSTOMER?` -> `^CUSTOMER: 5`;
`AT^CUSTOMER=?` -> `^CUSTOMER: 0~36,73,74` (the range the firmware accepts, not a list of safe values: 4PDA 291 names
3/7/9/10/11/13/15/17/22/24/26/30/31/35 as boot-loop values on GC.004 047). Nothing written.

Web reports on CUSTOMER and MBIM (2026-10-19, read through a web search summary, **unverified**, not on our bench):
- 4PDA T99 topic: `00B3` (MBIM, VID 1e2d) is accepted only after `AT^CUSTOMER=14` (Thales); the same thread says
  `00B3` works on `F0.1.0.0.9.GC.004` and **does not on `F0.6.0.0.6.GC.004`** — the branch of our unit (056).
  [4PDA 1086317](https://4pda.to/forum/index.php?showtopic=1086317&st=20)
- CUSTOMER 14 + `00B3` reported working in Windows/OpenWrt/MikroTik on one WH3000 board (HUA #303).
  [4PDA 1102595](https://4pda.to/forum/index.php?showtopic=1102595&st=300)
- An OpenWrt wiki gives `AT+USBSWITCH=90D5` (MBIM) / `=9025` (QMI) and `CUSTOMER=0` first when the default
  CUSTOMER 14 does not let the composition change. [wiki.vps-server.ru](https://wiki.vps-server.ru/doku.php/wiki:openwrt:t77w175)
- another modem manager issue #40: MBIM + ModemManager unstable (connections drop) on `F0.6.0.0.6.GC.004057`.
  GitHub issue 40
- Wireless Joint X55/T99W175 threads: AT enabling and composition notes for other hosts.
  [WJ 3267](https://wirelessjoint.com/viewtopic.php?t=3267&start=150), [WJ 3777](https://wirelessjoint.com/viewtopic.php?t=3777)

Reading for F0.6.0.0.6.GC.004.0xx with CUSTOMER 5: the documented MBIM route (CUSTOMER 14 + `00B3`) is reported not to
work on this branch, `90D5` tends to come without serial ports, MBIM + ModemManager is reported unstable on 057. Keep
CUSTOMER 5 + `9025` (QMI); MedveModem offers no CUSTOMER or composition write for it.

## Known AT command corpus

Commands are kept here even when destructive or unsuitable for automatic OpenWrt use.

| Command | Category | Risk | Purpose | Source |
|---|---|---|---|---|
| `AT^TEMP?` | telemetry | **READ** | Read TSENS/PA/Skin temperatures. | a field report T99W175 USB profiles |
| `AT^DEBUG?` | telemetry | **READ** | Serving/secondary LTE/NR cell, signal and CA diagnostics. | a field report T99W175 USB profiles |
| `AT+CGMI` | identity | **READ** | Manufacturer. | a field report profiles |
| `AT+CGMM` | identity | **READ** | Model. | a field report profiles |
| `AT+CGMR` | identity | **READ** | Firmware revision. | a field report profiles |
| `AT+CGSN` | identity | **READ** | IMEI. | a field report profiles |
| `AT+CIMI` | sim | **READ** | IMSI. | a field report profiles |
| `AT+ICCID` | sim | **READ** | ICCID. | a field report profiles |
| `AT^BAND_PREF?` | radio.band | **READ** | Read enabled/disabled WCDMA/LTE/NR5G band sets. | a field report _qualcomm_sdx55_common |
| `AT^BAND_PREF` | radio.band | **MUTATING** | Reset band masks to defaults across RATs; dangerous as an implicit pre-step because it changes other RAT masks. | a field report _qualcomm_sdx55_common |
| `AT^BAND_PREF=LTE,2,<bands>` | radio.band | **MUTATING** | Replace LTE enabled-band set. | a field report _qualcomm_sdx55_common |
| `AT^BAND_PREF=WCDMA,2,<bands>` | radio.band | **MUTATING** | Replace WCDMA enabled-band set. | a field report _qualcomm_sdx55_common |
| `AT^BAND_PREF=NR5G,2,<bands>` | radio.band | **MUTATING** | Replace NR5G enabled-band set. | a field report _qualcomm_sdx55_common |
| `AT^SLMODE?` | radio.mode | **READ** | Read RAT mode. | a field report _qualcomm_sdx55_common |
| `AT^SLMODE=1,<mask>` | radio.mode | **MUTATING** | Set RAT mask; T99W175 semantics use 1=WCDMA, 2=LTE, 4=NR5G bits. | a field report _qualcomm_sdx55_common |
| `AT^LTE_LOCK?` | cell.lock | **READ** | Read LTE cell lock. | a field report _qualcomm_sdx55_common |
| `AT^LTE_LOCK=<PCI>,<EARFCN>` | cell.lock | **MUTATING** | Set LTE cell lock. Argument order is PCI first, EARFCN second. Requires modem restart to take effect. | a field report _qualcomm_sdx55_common |
| `AT^LTE_LOCK` | cell.lock | **MUTATING** | Clear LTE cell lock. | a field report _qualcomm_sdx55_common |
| `AT^WCDMA_LOCK?` | cell.lock | **READ** | Read WCDMA lock state. | a field report _qualcomm_sdx55_common |
| `AT^WCDMA_LOCK` | cell.lock | **MUTATING** | Clear WCDMA lock. | a field report _qualcomm_sdx55_common |
| `AT^NR5G_LOCK?` | cell.lock | **READ** | Read NR5G lock state. | a field report _qualcomm_sdx55_common |
| `AT^NR5G_LOCK=<PCI>,<NR-ARFCN>` | cell.lock | **EXPERIMENTAL** | Proposed/set form used by another modem manager by symmetry with LTE; write form was explicitly noted there as lacking a live forum dump and must remain unverified. | a field report _qualcomm_sdx55_common |
| `AT^NR5G_LOCK` | cell.lock | **MUTATING** | Clear NR5G lock. | a field report _qualcomm_sdx55_common |
| `AT^CA_ENABLE?` | radio.ca | **READ** | Read CA enable flag. | a field report _qualcomm_sdx55_common |
| `AT+CFUN=1,1` | reset | **MUTATING** | Restart modem; also required after some cell-lock changes. | a field report and other scripts |
| `AT^CUSTOMER?` | firmware.personality | **READ** | Read customer/personality. | 4PDA T99W175 field documentation |
| `AT^CUSTOMER=<n>` | firmware.personality | **HIGH** | Change OEM/customer personality; may alter VID:PID, available compositions and firmware behavior. | 4PDA T99W175 field documentation |
| `AT+USBSWITCH=00B3` | transport.composition | **HIGH** | Select MBIM composition on documented firmware/customer combinations. | 4PDA T99W175 field documentation |
| `AT+USBSWITCH=00B7` | transport.composition | **HIGH** | Select QMI/RMNET composition on documented firmware/customer combinations. | 4PDA T99W175 field documentation |
| `AT+USBSWITCH=9025` | transport.composition | **HIGH** | Select Qualcomm QMI/RMNET composition with DIAG/ADB/MODEM/NMEA on documented firmware. | 4PDA T99W175 field documentation |
| `AT+USBSWITCH=90D5` | transport.composition | **HIGH** | Select Qualcomm MBIM composition on documented firmware. | 4PDA T99W175 field documentation |
| `AT^SETCONFIG=0` | transport.composition | **HIGH** | Newer-firmware MBIM selector reported by community documentation. | 4PDA T99W175 field documentation |
| `AT^SETCONFIG=1` | transport.composition | **HIGH** | Newer-firmware QMI selector reported by community documentation. | 4PDA T99W175 field documentation |
| `AT^EFS="/policyman/carrier_policy.xml",0` | efs.policy | **DESTRUCTIVE** | Delete carrier policy file. Recovery may require firmware restore/reflash. | other scripts/t99w175 observation + 4PDA |
| `AT^EFS="/policyman/policies.xml",0` | efs.policy | **DESTRUCTIVE** | Delete policies.xml if present. | other scripts/t99w175 observation + 4PDA |
| `AT^EFS="/policyman/lte_feature_restrictions.xml",0` | efs.policy | **DESTRUCTIVE** | Delete LTE feature restriction policy. | 4PDA T99W175 field recipes |
| `AT^EFS="/policyman/us_fcc_band_restrictions.xml",0` | efs.policy | **DESTRUCTIVE** | Delete FCC band restriction policy. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/rrc/lte_feature_ca.xml",0` | efs.radio | **DESTRUCTIVE** | Delete LTE CA feature file. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/rrc/PC2_WHITELIST.xml",0` | efs.radio | **DESTRUCTIVE** | Delete PC2 whitelist. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/rrc/PC2_WHITELIST.xml_Subscription01",0` | efs.radio | **DESTRUCTIVE** | Delete subscription-specific PC2 whitelist. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/rrc/cap/disable_gap_less_meas",0` | efs.radio | **DESTRUCTIVE** | Delete gap-less measurement disable item. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/common/ca_disable",0` | efs.radio | **DESTRUCTIVE** | Delete CA disable item. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/rrc/efs/lte_feature_enable"` | efs.radio | **READ** | Read LTE feature-enable item. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/rrc/efs/lte_feature_enable_Subscription01"` | efs.radio | **READ** | Read subscription-specific LTE feature-enable item. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/rrc/efs/lte_feature_enable",32,"FF,..."` | efs.radio | **DESTRUCTIVE** | Overwrite 32-byte LTE feature-enable payload with FF pattern; exact effect is firmware-dependent. | 4PDA T99W175 field recipes |
| `AT^EFS="/nv/item_files/modem/lte/rrc/efs/lte_feature_enable_Subscription01",32,"FF,..."` | efs.radio | **DESTRUCTIVE** | Overwrite subscription-specific feature-enable payload. | 4PDA T99W175 field recipes |
| `AT^SBFS=0` | firmware | **HIGH** | Community patch sequence command reported to trigger modem reboot on some firmware. | 4PDA T99W175 field recipes |
| `AT+FOTA_SWITCH=0` | firmware | **HIGH** | Community patch sequence command affecting FOTA behavior. | 4PDA T99W175 field recipes |


## Destructive EFS/NV recipes

The `AT^EFS` delete/write entries are intentionally retained. They explain real field-modified T99W175 devices and some otherwise mysterious behavior.

They modify modem filesystem/NV policy and can require firmware recovery. **Documentation does not make them MedveModem presets.**

## Radio-control notes

The SDX55 family profile provides `BAND_PREF`, `SLMODE`, LTE/WCDMA/NR5G cell lock and CA diagnostics.

Important: the T99W175 `AT^SLMODE` mask is different from T77W968. Reusing mode values between those families is wrong.

## Provenance

- a field report / another modem manager: live profile/transport logic;
- other scripts / another modem manager + modeminfo: historical compositions, QMI telemetry integration, operator presets;
- QModem: USB MBIM/RMNET and PCIe MBIM model evidence;
- ModemManager/libqmi: QMI-over-MBIM control path;
- community field corpus: firmware/customer/composition and destructive EFS observations.

## Telegram field evidence — composition is not endpoint health

- A live `05c6:9025` diagnostic on `T99W175.F0.6.0.0.6.GC.004` shows `cdc-wdm0`, `ttyUSB0..3`, bound `qmi_wwan`, and a connected ModemManager object.
- other scripts roughly ten T99W175 units in `00B7` with working SMS. This is useful multi-device field evidence, but not enough to turn `00B7` into a universal guarantee.
- Other reports show why: one unit used `00B3` with ttyUSB while `90D5` lacked serial ports; another firmware accepted imported `00B7` composition functions but its AT ports remained unresponsive. The selected composition label is not proof that the control endpoint is healthy.
- Discussion around qmi-proto, qmicli and ModemManager is best read as an **ownership** problem: duplicate session/network ownership can break a stack, while coordinated MM + qmicli + COM usage is also reported working.
- A live `05c6:9025` repro showed `atinout` receiving fragments/echoes of other commands and intermittent errors when several consumers shared the T99 command port. a router maker's firmware describes T99W175 as effectively having one shared AT/COM path and recommends serialized access. This is direct evidence for a central AT lease/broker rather than independent status/SMS/console readers.
- a router maker's firmware also documents a firmware-scoped SMS-storage quirk from at least `F0.6.0.0.6`: after firmware flashing, briefly selecting `AT+CPMS="SM","SM","SM"` and then returning to `ME` (smstools3 does the return automatically) can make SMS reception persist across reboot, power cycle, composition and customer changes until the next firmware update. Treat this as one-shot initialization for matching firmware, not a generic periodic preset.

See [Telegram field notes](../TELEGRAM-FIELD-NOTES.md).

<!-- 4pda-field-2026-10-15:start -->
## 4PDA — дополнительные модельные полевые факты

**Reference only; not hardware-verified.** Данные из пользовательских HTML-архивов; связь `message-N` — только с локальным архивом. Нет новых runtime presets/AT operations. Все кейсы и SHA256: [4PDA field notes](../4PDA-FIELD-NOTES.md).

- **`4PDA-1086317-291`** (danger): Закреплённая инструкция указывает, что CUSTOMER 3/7/9/10/11/13/15/17/22/24/26/30/31/35 на F0.6.0.0.6.GC.004 047 способен вызвать циклическую перезагрузку; отдельно предупреждение, что инструкция не для REV.3 V305 USB3. **MedveModem:** Внести blacklist опасных CUSTOMER с привязкой FW; любые изменения CUSTOMER только с ручным подтверждением. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317).
- **`4PDA-1086317-7316`** (danger): После неудачных прошивок и отсутствия сети автор восстановил устройство через EDL/fastboot. Сообщил, что запись раздела sec поменяла USB VID:PID, потребовалось повторно сопоставлять WinUSB; далее fastboot обнаруживался как 18d1:d00d. **MedveModem:** Этап восстановления может менять USB-identity несколько раз. Ни EDL-запись разделов, ни работа с boot/recovery не являются автоматическим recovery. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317).
- **`4PDA-1086317-8180`** (write): На сборке T99W175.F0.6.0.0.6.GC.004 025 автор перевёл CUSTOMER 18 + 90D5 в CUSTOMER 5 + 9025, после чего a router maker's firmware заработал; аппаратный pin3 модифицировать не пришлось. **MedveModem:** Физическая доработка не является обязательной для всех адаптеров и ревизий; фиксировать адаптер/пины в профиле отдельно. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317).
- **`4PDA-1086317-5961`** (read): При композиции 9025 пользователь установил одновременно QMI, MBIM и ModemManager пакеты на OpenWrt 24.10.1, но сообщение описывает нерешённую проблему запуска. **MedveModem:** Не считать просто наличие пакетов доказательством протокола и портов; resolver должен выбрать одного владельца. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1086317).

<!-- 4pda-field-2026-10-15:end -->


<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### t99-mbimex-payload-contract

**Observation:** MBIMEx response payload truncated (64<68/76<80); competing MBIM/MM owners cause drops; MM-only observed >14 h. Firmware 004057 alone did not cure it.

- **Scope:** model=T99W175 / MV31-W; usb_ids=05c6:90d5, 05c6:9025; protocol=mbimex / modemmanager
- **Type / confidence:** `control-backend` / `external-field-report`
- **Regression contracts:** classify protocol-contract fault separately from hardware loss; do not trigger CFUN or USB reset solely on ParameterTooLong; enforce one cdc-wdm control owner
- **Safety:** Do not infer Foxconn from 05c6 alone; Compal shares IDs.
- **Original source:** issue / release / pinned source

### Composition and boot-mode recipes (reference only)

Exact operation stages, exit checks and prerequisites: [composition matrix](../COMPOSITION-AND-BOOT-MODES.md).

- **`foxconn-t99-qmi-mbim`** — `PERSISTENT_COMPOSITION_WRITE`; T99W175 QMI 05c6:9025/1e2d:00b7 or MBIM 05c6:90d5/1e2d:00b3 → firmware/personality-dependent; explicit approval required; Do not infer a valid numeric command from the VID/PID alone.

<!-- upstream-another modem manager-v3.5.2:end -->
