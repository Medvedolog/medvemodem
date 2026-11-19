# Foxconn T99W640 / Dell DW5934e: SDX72 AT, FTM, QCDM and NV/EFS field reference

**Evidence:** user-supplied 35-page technical study, revision **2.4, 2026-11-18**, compared against Retail F03X000_000_054_A00.054 / FDE2.F0.0.0.1.2 (2025-06-04), with **read-only commands tested on ENG FDE2.F0.0.0.1.2.DF.001**. The dangerous/write forms below are **reverse-engineering observations, not hardware-tested procedures**. This is firmware-dependent research, **not a MedveModem feature contract** or a 100%-complete command inventory. See the parent [T99W640 profile](foxconn-t99w640.md).

## Ownership, architecture and transport (reference pp. 4–5, 11, 26, 34)

| Endpoint | Where / protocol | Implication |
| --- | --- | --- |
| PCIe MHI: \`/dev/wwan0mbim0\`; USB: \`/dev/cdc-wdm0\` | MBIM, vendor QMI-over-MBIM AT gateway (Foxconn service \`0xE3\`, command \`0x555D\`, TLV \`0x10\`, subscriptions/indications) | **Not** a raw serial AT port. Needs correct transport/framing and single owning process; the documented vendor routing is **not implemented** in MedveModem. |
| PCIe \`/dev/wwan0at0\`, USB e.g. \`/dev/ttyUSB2\` | MHI DUN / USB ACM | Retail PCIe may lack \`mhi_pipe_32\`, so a visible device node is not proof that AT works. Direct AT only when the manager has released the port. |
| PCIe \`/dev/wwan0qcdm0\` / \`/dev/mhi_DIAG\`, USB e.g. \`/dev/ttyUSB0\` | QCDM binary Async HDLC | Separate from AT, high risk of disrupting data. PCIe DIAG may be initially dormant because the AP's diag-router starts in USB transport mode. **MedveModem does not activate DIAG, write NV/EFS or start FTM.** |
| Processor AP | ARM64 Linux and \`atfwd-daemon\` | USB/PCIe composition, GPIO, GNSS, temperature, SDC, FOTA/LwM2M, OEM flags. 41 AP commands + interception of \`AT+CFUN\` and \`AT$QCPWRDN\` in the source analysis. |
| Processor CP | Qualcomm Hexagon | 3GPP/Qualcomm/vendor RF/CA/bands/locks, UIM, NV/EFS, 34-command \`ftm\` dispatcher. |

**Production policy:** the existing USB MBIM profile still prefers **ModemManager** and does not claim the vendor QMI-AT gateway, an accessible serial AT channel, PCIe support, or working AT lock writes. Shared USB PID \`05c6:90d5\` also identifies other modems; require an accurate model match. Do not automatically probe the AT/DIAG interface, change USB compositions or inhibit/restart ModemManager.

## Read-only catalogue (ENG observed; Retail support can differ)

The *exact suffix matters*. A bare command is **not** presumed to be a read.

| Topic | Query/form | Reported fields and caveats |
| --- | --- | --- |
| Thermals | \`AT+TEMP?\` | Four AP temperatures **TSENS**, **PA**, **Skin Sensor**, **Ambient Sensor**, Celsius. \`AT+TEMP=?\` produces ERROR. Can compare with \`AT$QCTEMP\` (CP temperature). Do not silently discard the existing false PA thermal fault. |
| Chip supply, hardware | \`AT$QCVOLT\`, \`AT+BOOT_VER?\`, \`AT+SECBOOT_STATUS?\`, \`AT+USBTYPE?\` | mV, SBL/UEFI, secure boot, USB speed (UNKNOWN on PCIe). \`AT+RFPIN_STATUS?\`: W_DISABLE# pin, 1=RF enabled. |
| Aggregation | \`AT+CA_INFO?\`, \`AT+CA_ENABLE?\`, \`AT+ULCA?\` | PCC + SCC with LTE band and MHz; CA enabled flag. **CA_ENABLE is inverted: value 0 enables CA, 1..255 disables CA**. \`AT+ULCA\` **without ?** clears a feature-disable file: never use as a read. |
| LTE/NR locks | \`AT+LTE_LOCK?\`, \`AT+NR5G_LOCK?\`, \`AT+WCDMA_LOCK?\` | Read lock state. LTE takes **PCI first**, EARFCN second; the naked \`AT+LTE_LOCK\` clears the lock and is not a probe. NR write format in the static analysis is band,SCS,downlink NR-ARFCN,PCI count,PCI entries; do not infer a working write from a query. |
| Bands / RAT | \`AT+SLBAND?\`, \`AT+BAND_PREF?\`, \`AT+BAND_PRI?\`, \`AT+SLMODE?\`, \`AT+NR5G_MODE?\`, \`AT+UEBAND?\` | Band sets, priority and supported bands. \`SLMODE\` uses a two-argument mode: first argument 0=temporary/1=NV; second **0..7 enumerated**, not the bitmask used by earlier SDX generations. |
| Radio / reception | \`AT+MIMOCONFIG?\`, \`AT+MODULATION?\`, \`AT+DEBUG\`, \`AT$QCSQ\`, \`AT$QCRSRP\`, \`AT$QCRSRQ\`, \`AT$QCSYSMODE\` | PCC/SCC MIMO, DL/UL modulation, cell and Rx0–Rx3, RSSI/RSRP/RSRQ/SINR, active RAT. \`AT+RSSI=LTE,ALL\` is a read-like **parameterized** request; \`AT+RSSI?\` by itself gives a parameter error. Mask invalid/absent Rx values; never assume four populated chains. |
| Antenna / TX | \`AT+RX_CHAINS?\`, \`AT+SWITCHANT?\`, \`AT+ARD_ENABLE?\`, \`AT+QTUNER_ENABLE?\`, \`AT+FDIS_LTEBW?\`, \`AT+GNSS_BLANK?\` | Receive masks/selection, diversity, tuner and LTE bandwidth. \`AT+QTUNER?\` is **wrong**; correct spelling is \`QTUNER_ENABLE\`. \`AT+TX_PWR?\` requires FTM and should not be polled. |
| Power reduction | \`AT+DPR_ACTIVE?\`, \`AT+DPR=check\`, \`AT+DPR_ENABLE?\`, \`AT+DPR_CONTROL_MODE?\`, \`AT+SMTP=<tech>,<band>\` | FCC/CE/ISED SAR mode, AP DPR daemon, per-band TX ceilings. **Never** send \`AT+DPR=rm_all\`; erases the FoxNV DPR tables. |
| UIM / eSIM | \`AT+SWITCH_SLOT?\`, \`AT+SIMDECPIN?\`, \`AT+SBFS?\`, \`AT+WMS_ROUTE?\`, \`AT+MCFG?\`, \`AT+MCFG=?\` | SIM1=0 / SIM2-eSIM=1, card detection, MBN auto-selection, incoming SMS routing. ENG has 24 MCFG profiles; Retail static listing has ~30: **do not hard-code indexes** for on-router writes. |
| IMS / 5G | \`AT+IMS_ENABLE?\`, \`AT+IMSTESTMODE?\`, \`AT+DSS_ENABLE?\`, \`AT+NR5G_MODE?\` | Some reads may return ERROR even on supported firmware. Several corresponding writes trigger an automatic reboot. |
| AP owner / security | \`AT+CUSTOMER?\`, \`AT+FEATURE?\`, \`AT+FCC_LOCK?\`, \`AT+DIAG_ENABLE?\`, \`AT+ODIS?\`, \`AT+PDSHFLG?\` | OEM 0/32/33/34/35/36, eSIM/FCC flag, DIAG status, ODIS identity, ramdump flag. \`FCC_LOCK?\` may return **without trailing OK**; no automatic timeout/retry storm. \`DIAG_ENABLE?\` may return ERROR under locked SDC. |
| GNSS | \`AT+FGPS?\`, \`AT+FGPS_OPER_MODE?\`, \`AT+FGPS_SUPL?\`, \`AT+FGPS_XTRA?\`, \`AT+FGPS_PIN_STATUS?\` | GPS hardware on/off/status; session commands change operating state and are not background probes. |
| Baseline 3GPP | \`AT+CSQ\`, \`AT+CEREG?\`, \`AT+C5GREG?\`, \`AT+CGATT?\`, \`AT+CGDCONT?\`, \`AT+CGPADDR\`, \`AT+CGCONTRDP\`, \`AT+CPIN?\`, \`AT+CSCA?\`, \`AT+CPMS?\`, \`AT+CGSMS?\` | Generic MM/3GPP fallback, not evidence of native transport availability. Avoid collecting ICCID/IMSI/IMEI unredacted. \`AT+COPS=?\` is a **30–90 s network scan**, not a cheap probe. |

## Cell/band lock semantics (reference pp. 9, 19–21, 23)

| Operation | Studied syntax | Safety |
| --- | --- | --- |
| LTE read | \`AT+LTE_LOCK?\` | Read-only, **only** with \`?\`. |
| LTE set | \`AT+LTE_LOCK=<PCI>,<EARFCN>[,<PCI>,<EARFCN> ...]\` (at most 8 pairs, PCI 0..503) | **Unverified write**, firmware guard and rollback required; PCI precedes EARFCN. |
| LTE clear | **bare** \`AT+LTE_LOCK\` | **Mutates** NV \`/nv/item_files/modem/lte/rrc/efs/cell_lock_list\`. Never offer as a query. |
| NR read | \`AT+NR5G_LOCK?\` | Read-only. |
| NR set | \`AT+NR5G_LOCK=<band>,<scs>,<dl_arfcn>,<count>,<pci1>...\` | **Unverified write**; SCS 0=15 kHz, 1=30 kHz; count 1..9 and PCI 0..1007. Writes \`.../nr5g/RRC/pci_lock_info\`. |
| LTE/NR bands | \`AT+SLBAND="<tech>",<1|2>,<bands...>\` | 1=disable, **2=enable**; do not confuse with \`BAND_PREF\` or blindly reset other RATs. |
| Mode set | \`AT+SLMODE=<term>,<pref_mode>\` | \`term=0\` transient / \`1\` in NV; second argument: 0=auto, 1=3G, 2=LTE, 3=3G+LTE, 4=NR, 5=3G+NR, 6=LTE+NR, 7=all (as reported). Avoid copying SDX55 mask logic. |

For a future implementation: **detect this exact modem and firmware**, require confirmed control-port ownership and a non-disruptive transport, snapshot the *complete original lock state* and write back only after separate hardware validation. This is not the existing Foxconn SDX55 radio dialect.

## Non-probe traps and dangerous modes (reference pp. 5–9, 12–16, 19–25)

| Exact command or family | Risk / rule |
| --- | --- |
| \`AT+VZWRSRP?\`, \`AT+VZWRSRQ?\` | **Known CP MHI SSR crash on ENG**. Retail parser looked similar by static inspection. **Hard blacklist**, including AT console presets/automatic probes; use \`AT$QCSQ\` and \`AT$QCRSRP\` instead. |
| \`AT+ODIS\` without \`?\` | **Resets ODIS to factory values** (observed handler). Only \`AT+ODIS?\` is read; \`AT+ODIS=?\` is a help query, not useful for reading contents. |
| \`AT+ULCA\` bare, \`AT+LTE_LOCK\` bare | Changes EFS / clears lock; no implicit "test" calls. |
| \`AT+FPIN=4\` / \`=5\` | Changes/restores a **fixed set of 17 GPIOs**, including W_DISABLE#; reported \`=?\` syntax for modes 0..3 is obsolete. Not a generic GPIO utility. |
| \`AT+USBSWITCH=<PID>\` | **Reboots module** and changes its USB composition; 62 reported profiles including 90D5 MBIM and 9091 QMI. **Never auto-switch**, especially while PCIe remains connected. |
| \`AT+CUSTOMER\`, \`AT+FEATURE\`, \`AT+DIAG_ENABLE\`, \`AT+URRXTX\`, \`AT+SDC_UNLOCK\` | OEM/NV/USB/security operations; many need two-step SDC authorization, reload settings or reboot. No automatic SDC unlocking or writes. |
| \`AT+FCC_LOCK=0\`, \`ftm unlock_fcc\` | Change/clear an internal flag, **not a valid substitute for authenticated host-side QMI FCC unlock**. No "one-command universal unlock" claims. |
| \`AT+CA_ENABLE=<value>\`, \`AT+MCFG=<index>\`, \`AT+SWITCH_SLOT=<0|1>\`, \`AT+SIMDECPIN\`, \`AT+DPR=rm_all\` | CA flag inversion, MBN/SIM operational changes, or destructive DPR erase. Read their own current states first; no profile-driven writes. |
| \`AT+EDL\`, \`AT+PCIE_EDL\`, \`AT+FASTBOOT\`, \`AT+EFS_ERASE\`, \`AT+ERASE_IMEI\`, \`AT+CRASH\`, \`AT+RESET\` | Bootloader/EDL/erase/crash/reboot hazards. No "recovery" or diagnostic preset may invoke them automatically. |
| \`AT+NV\`, \`AT+EFS\`, \`AT+FOXNV\`, \`AT+NV_UNLOCK\` | Raw NV/EFS; arguments and read/write switches are **not interchangeable**, access may be protected. Keep reference-only, never automatic. |

## Factory \`ftm\` subcommands (reference pp. 26–27)

The study enumerates **34** subcommands of a text dispatcher conveyed over the **vendor QMI AT_OVER_MBIM path**, sent as \`ftm <subcommand>\` **without** an \`AT\` prefix. This is **not** a serial-console instruction and not a ModemManager API. *Read* means "classified read-only in the ENG study", not enabled on routers.

| # | Subcommand | Classification / reported behaviour |
| ---: | --- | --- |
| 1 | \`ftm check_customer\` | Read OEM ID |
| 2 | \`ftm check_fw_version\` | Read SW + MCFG versions |
| 3 | \`ftm check_ati\` | Read manufacturer/model/revision identifiers; privacy-mask |
| 4 | \`ftm write_imei\` | **Destructive identity modification, prohibited** |
| 5 | \`ftm check_imei\` | Read IMEI; privacy-mask |
| 6 | \`ftm check_foxnv_imei\` | Read backup IMEI; privacy-mask |
| 7 | \`ftm write_hw_ver\` | **Dangerous** HW revision NV write |
| 8 | \`ftm check_hw_ver\` | Read HW revision |
| 9 | \`ftm set_ftm_mode\` | **Dangerous** CFUN=5 factory radio-off state |
| 10 | \`ftm set_online_mode\` | Mutating return to online, not read-only |
| 11 | \`ftm check_cfun\` | Read ONLINE/LPM/FTM state |
| 12 | \`ftm enable_sbfs\` | Mutating MBN/SIM-auto selection |
| 13 | \`ftm disable_sbfs\` | Mutating MBN/SIM-auto selection |
| 14 | \`ftm check_sbfs\` | Read MBN auto-selection |
| 15 | \`ftm enable_gpsl5\` | Mutating GNSS EFS |
| 16 | \`ftm disable_gpsl5\` | Mutating GNSS EFS |
| 17 | \`ftm check_gpsl5\` | Read GNSS L5 state |
| 18 | \`ftm enable_force_use_gpsl5\` | Mutating GNSS EFS |
| 19 | \`ftm disable_force_use_gpsl5\` | Mutating GNSS EFS |
| 20 | \`ftm check_force_use_gpsl5\` | Read forced GNSS L5 |
| 21 | \`ftm write_sn\` | **Dangerous** device identity NV write |
| 22 | \`ftm check_sn\` | Read serial, privacy-mask |
| 23 | \`ftm delete_is_ftm_mode\` | **Dangerous** EFS deletion |
| 24 | \`ftm nv_unlock\` | **Dangerous** permission change |
| 25 | \`ftm check_nv569\` | Read calibration marker |
| 26 | \`ftm unlock_fcc\` | Mutating internal FCC flag; **does not replace host QMI authentication** |
| 27 | \`ftm check_feature\` | Read eSIM / FCC flags |
| 28 | \`ftm enable_foxnv_backup\` | Mutating force-backup of calibration store |
| 29 | \`ftm disable_foxnv_backup\` | **Dangerous** disable backup/restore protection |
| 30 | \`ftm check_slot\` | Read UIM slot |
| 31 | \`ftm check_cpin\` | Read SIM PIN readiness (not the PIN value) |
| 32 | \`ftm check_cimi\` | Read IMSI, privacy-mask |
| 33 | \`ftm usbtype\` | Read bus speed |
| 34 | \`ftm calnvck\` | Read calibration consistency |

## DIAG / QCDM inventory, **reference-only** (reference pp. 34–35)

**Framing:** asynchronous HDLC, CRC-16-CCITT (little-endian CRC bytes), escape 0x7e -> 0x7d 0x5e and 0x7d -> 0x7d 0x5d; frame delimiter 0x7e. Study describes a PCIe DIAG wakeup dependency on the AP's diag-router. **No transport implementation or automatic activation in MedveModem**.

| Code | Name | Scope / risk |
| --- | --- | --- |
| \`0x00\` | \`DIAG_VERNO_F\` | Firmware build time/board ID, read |
| \`0x0c\` | \`DIAG_STATUS_F\` | Online/LPM/FTM state, read |
| \`0x1c\` | \`DIAG_DIAG_VER_F\` | Protocol revision, read |
| \`0x7c\` | \`DIAG_EXT_BUILD_ID_F\` | Extended build ID, read |
| \`0x26\` | \`DIAG_NV_READ_F\` | 128-byte NV item read, sensitive |
| \`0x27\` | \`DIAG_NV_WRITE_F\` | **Dangerous** NV write |
| \`0x4b 0x13\` | \`SUBSYS_FS\` (EFS2) | File browsing/read/write; **write prohibited** |
| \`0x29\` | \`DIAG_CONTROL_F\` | Radio state / reset: mutating |
| \`0x3a\` | \`DIAG_DLOAD_F\` | **Dangerous** EDL entry |
| \`0x60,0x73,0x7d,0x82\` | OTA/NAS/F3/events masks | Diagnostic logs; can be costly or privacy-sensitive |

Important items **for diagnosis, not modification**: NV 453 (FTM boot), NV 550 (IMEI), NV 6828 (LTE band mask); EFS \`/nv/item_files/modem/mmode/ue_usage_setting\` (voice/data-centric), \`sms_only\`, \`sms_mandatory\`; \`/nv/item_files/modem/lte/common/ca_disable\`, \`/nv/item_files/modem/lte/rrc/cap/disable_ca\` (NV 70251), \`/nv/item_files/ims/qp_ims_reg_config\`, \`/nv/item_files/mcfg/mcfg_autoselect_by_uim\` (NV 71546), \`/policyman/carrier_policy.xml\`. Reading raw NV/EFS may expose identity/calibration/carrier configuration; no unsupervised access.

## Source boundaries and next verification

1. Commands in this note are **firmware-specific**; not all have direct USB AT access, some are exposed only via private PCIe MBIM routing, and some differ on Retail versus ENG.
2. **Source-observed:** non-destructive read queries on ENG; crash of \`VZWRSRP/VZWRSRQ\` on ENG; argument parsing and dangerous effects generally recovered from ENG/Retail binary static analysis. **Not verified:** a working native QMI vendor gateway in MedveModem; lock writes; FCC unlock on Retail; DIAG/FTM operations on actual consumer devices.
3. On hardware: first identify board, firmware and **exclusive owner**, then validate a single *whitelisted* read command with bounded timeouts. Store parsed telemetry only; scrub IMEI, IMSI, ICCID, raw NV/EFS and serials. No dangerous requests for "capability probing".
4. Preserve the existing profile defaults: ModemManager, no direct AT when MM owns the modem, no rebind/re-enumeration, no automatic composition change or destructive recovery. A separate **opt-in + hardware-tested implementation** is required for any writable feature.
