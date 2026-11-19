# Modem Zoo — AT command corpus

**A documented command is not an approved command.** Zoo keeps read-only, mutating, persistent, destructive, firmware-specific and dangerous commands so field knowledge is not lost.

For larger vendor/script dumps see [Extended QModem/other scripts AT reference](AT-REFERENCE.md).

## Risk legend

- **READ** — query/diagnostic/challenge;
- **MUTATING** — changes runtime/session/radio state;
- **EXPERIMENTAL** — incomplete or indirectly confirmed;
- **HIGH** — persistent/re-enumerating/reset/bootloader operation;
- **DESTRUCTIVE** — EFS/NV/transport operation with difficult recovery;
- **UNSUPPORTED OBSERVED** — syntax/capability may be advertised but field evidence says the requested service does not work.

## Foxconn T99W640 / Dell DW5934e / SDX72: research annex

**Not the SDX55 dialect.** The owner's firmware analysis (v2.4, 2026-11-18) distinguishes ENG read-only live tests from Retail static reverse engineering. Detailed [T99W640 AT + 34 ftm + QCDM/NV/EFS inventory](modems/foxconn-t99w640-at-diag.md). **No commands below are auto-polled or enabled as write capabilities** under the experimental ModemManager-owned USB MBIM profile.

| Command | Type | Risk / firmware rule |
| --- | --- | --- |
| `AT+TEMP?`, `AT$QCTEMP` | telemetry | **READ** ENG: four AP thermal sensors versus CP sensor. |
| `AT+CA_INFO?` | radio.ca | **READ** ENG: PCC and SCC bands/MHz. |
| `AT$QCSQ`, `AT$QCRSRP`, `AT$QCRSRQ` | radio.signal | **READ** ENG: serving signal, per-Rx RSRP/RSRQ. Do not use crashing VZW aliases. |
| `AT+RSSI=LTE,ALL` | radio.signal | **READ-LIKE** parameterized antenna query, while `AT+RSSI?` fails with argument error. |
| `AT+LTE_LOCK?` | cell.lock | **READ** ENG: current LTE lock. |
| `AT+LTE_LOCK=<PCI>,<EARFCN>` | cell.lock | **MUTATING / UNTESTED** write; PCI FIRST; up to eight pairs. Not enabled. |
| Bare `AT+LTE_LOCK` | cell.lock | **MUTATING** removal of an existing lock, not a query. |
| `AT+NR5G_LOCK?` | cell.lock | **READ** ENG; NR write family has `band,scs,dl_arfcn,pci_count,pci...`; not enabled. |
| `AT+SLMODE?` | radio.mode | **READ** ENG; writes take temporary/persistent flag plus enumeration 0..7 (not SDX55 mode mask). |
| `AT+SLBAND?`, `AT+BAND_PREF?` | radio.band | **READ** ENG; `SLBAND` writes distinguish disable=1 / enable=2. |
| `AT+CA_ENABLE?` | radio.ca | **READ**; a write of **0 ENABLES**, 1..255 DISABLES aggregation. |
| `AT+FCC_LOCK?` | security | **READ** but may omit terminating `OK`; internal flag is not host QMI FCC authorization. |
| `AT+ODIS?` | OEM | **READ**; bare `AT+ODIS` **RESETS** profile to defaults. |
| `AT+VZWRSRP?`, `AT+VZWRSRQ?` | radio.signal | **KNOWN CRASH** on tested ENG, CP MHI SSR; do not query. |
| `AT+ULCA`, `AT+DPR=rm_all` | CA / SAR | **MUTATING / DESTRUCTIVE** even without usual write form. |
| `ftm check_...` | factory | **REFERENCE ONLY** over vendor QMI command 0x555D, not a standard AT channel; 34 subcommands documented. No enabled factory path. |
| QCDM / DIAG | diagnostic | **REFERENCE ONLY**: separate HDLC binary channel; no background probe, NV/EFS writes or activation. |

## Foxconn T99W175 / SDX55 command corpus

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
| `AT^NR5G_LOCK=<PCI>,<NR-ARFCN>` | cell.lock | **EXPERIMENTAL** | Proposed/set form described in archived field observations by symmetry with LTE; write form was explicitly noted there as lacking a live forum dump and must remain unverified. | a field report _qualcomm_sdx55_common |
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

### Transport / behavior notes

- 1e2d:00b3 is documented as MBIM; 1e2d:00b7 as QMI/RMNET.
- 05c6:90d5 is documented as MBIM; 05c6:9025 as QMI/RMNET.
- 00b8/00b9 are additional MBIM/QMI-style compositions reported in community composition tables; availability varies by customer/firmware.
- a field report profiles identify actual transport by bound driver (qmi_wwan/cdc_mbim/cdc_ether), not by PID alone.
- QMI-over-MBIM is a distinct control technique: qmicli uses --device-open-mbim when the WDM node is bound to cdc_mbim. a field report uses this for QMI extras/eSIM paths and ModemManager's Foxconn FCC unlock does the same.
- modeminfo exposes Direct QMI vs QMI over MBIM as separate qmi_trap modes. historical tooling configures modeminfo/QMI telemetry separately from the network owner's protocol; document both planes instead of collapsing them into one 'mode'.
- Telegram live diagnostic for 05c6:9025 / F0.6.0.0.6.GC.004 shows cdc-wdm0 + ttyUSB0..3, qmi_wwan and a connected ModemManager object.
- other scripts roughly ten T99W175 units in 00B7 with working SMS, but other field reports show composition artifacts/firmware can still yield unresponsive AT ports. Composition label is a candidate, not proof of endpoint health.
- Field discussion distinguishes duplicate network ownership (qmi-proto competing with ModemManager) from coordinated use of qmicli/MM/COM ports. Arbitration should be per resource and operation.
- eSIM on SDX55 (T99W175 / MV31-W): lpac's APDU transport follows the cdc-wdm driver, not the session protocol: qmi_wwan -> QMI, cdc_mbim -> MBIM. Under ModemManager the node may still be qmi_wwan.
- Opening the eUICC over MBIM can remap the active SIM slot to the (often empty) eSIM and drop the working SIM: keep LPAC_APDU_MBIM_SKIP_SLOT_MAPPING=1 and restore the saved slot mapping.
- Profile downloads on SDX55 fail intermittently on long APDUs (community: several attempts); after a failed download the chip may refuse with euicc_init -1 until the SIM slot is toggled. iccid_already_exists on a retry means the previous attempt installed the profile.
- After enabling a profile SDX55 firmware may keep the old profile until AT+CFUN=1,1 (no REFRESH handling).

## Foxconn T77W968 / Dell DW5821e command corpus

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
| `AT^MCFG?` | firmware.profile | **READ** | Read current carrier/customer firmware profile on T77W968 F1.0.0.5.2 field recipe. | a router maker's firmware Telegram field recipe |
| `AT^MCFG=?` | firmware.profile | **READ** | List available profile IDs before selecting a profile; numeric GC ID is firmware-specific. | a router maker's firmware Telegram field recipe |
| `AT^MCFG=<profile>` | firmware.profile | **HIGH** | Select firmware/carrier profile. Must use an ID discovered on the exact firmware; never hard-code the field example globally. | a router maker's firmware Telegram field recipe |
| `AT^SBFS=0` | firmware.policy | **HIGH** | Field recipe operation used to inhibit SIM-driven profile/policy behavior on T77/T99 firmware; firmware semantics are not generalized. | a router maker's firmware Telegram field recipes |

### Transport / behavior notes

- QModem records T77W968 as supporting both QMI and MBIM.
- a field report's T77W968 profile detects qmi_wwan, cdc_mbim and cdc_ether from the live USB composition.
- Dell IDs 413c:81d7 / 413c:81e0 and Foxconn IDs 0489:e0b4/e0b5 are treated as the same T77W968 command family in archival hardware profiles.
- FCC authentication is required on affected Dell/Foxconn variants; ModemManager hook or direct QMI path must be owner-aware.
- T77W968 AT^SLMODE numeric values are not compatible with T99W175 SDX55 masks. Reusing SDX55 mode values can silently select the wrong RAT mode.
- A captured 413c:81d7 boot log shows a real cdc_mbim -> qmi_wwan transition with cdc-wdm0/wwan0 and ttyUSB0..3 torn down/recreated. Live bound driver outranks assumed customer/setmode mapping.
- Telegram F1.0.0.5.2 recipe documents active RFNV 00028874 replacement from stock 2-ant/4-ant files. Treat as destructive Expert/recovery knowledge; calibration binaries are not mirrored into Zoo.

## Quectel RM500/RM502/RM520/RG520 Qualcomm 5G command corpus

| Command | Category | Risk | Purpose | Source |
|---|---|---|---|---|
| `AT+QENG="servingcell"` | cell.telemetry | **READ** | Serving LTE/NR cell, registration and signal detail. | a field report/QModem |
| `AT+QENG="neighbourcell"` | cell.telemetry | **READ** | Neighbour-cell report; response layout varies by generation/firmware. | a field report/QModem |
| `AT+QCAINFO` | radio.ca | **READ** | PCC/SCC aggregation and band/channel information. | a field report/QModem |
| `AT+QRSRP` | radio.signal | **READ** | Per-branch RSRP where supported. | a field report Quectel templates |
| `AT+QNWINFO` | radio.serving | **READ** | Current access technology/operator/band/channel summary. | a field report/QModem |
| `AT+QTEMP` | hardware.temperature | **READ** | Temperature sensor report. | a field report/QModem |
| `AT+QCFG="usbnet"` | transport.composition | **READ** | Read current USB network composition/mode. | a field report/QModem |
| `AT+QCFG="usbnet",<mode>` | transport.composition | **HIGH** | Change USB network mode; may re-enumerate and change visible control/data interfaces. | QModem + a field report hazard policy |
| `AT+QCFG="usbcfg",...` | transport.composition | **HIGH** | Change USB function composition. Wrong values can make the router lose required ports. | a field report DANGEROUS_AT |
| `AT+QCFG="data_interface",...` | transport.composition | **HIGH** | Change data-interface selection between USB/PCIe-related paths. | a field report DANGEROUS_AT |
| `AT+QCFG="pcie/mode",...` | transport.pcie | **HIGH** | Change PCIe operating mode; can move the modem away from the currently usable host path. | a field report DANGEROUS_AT |
| `AT+QPCIE="adb",1` | transport.pcie | **DESTRUCTIVE** | Field-reported on PCIe RM520N to disable all USB ports persistently; cited recovery required EDL/reflash. | a field report field hazard notes |
| `AT+QFASTBOOT` | bootloader | **HIGH** | Enter fastboot/bootloader path; router may lose the modem until recovery/reboot. | a field report DANGEROUS_AT |
| `AT+QPRTPARA=3` | nv.parameters | **DESTRUCTIVE** | Parameter reset operation; retained as hazardous historical knowledge. | a field report DANGEROUS_AT |
| `AT+QNWPREFCFG="mode_pref"` | radio.mode | **READ** | Read RAT preference on 5G Quectel generations. | QModem/a field report |
| `AT+QNWPREFCFG="mode_pref",<mode>` | radio.mode | **MUTATING** | Set RAT preference. | QModem/a field report |
| `AT+QNWPREFCFG="<band-item>"` | radio.band | **READ** | Read LTE/NR band preference item selected by firmware family. | QModem |
| `AT+QNWPREFCFG="<band-item>",<mask>` | radio.band | **MUTATING** | Change LTE/NR band preference mask. | QModem |
| `AT+QNWLOCK="<type>"` | cell.lock | **READ** | Read Quectel cell-lock state for the requested lock type. | QModem/a field report |
| `AT+QNWLOCK="<type>",<args>` | cell.lock | **MUTATING** | Apply cell lock; exact arguments are generation/RAT-specific. | QModem/a field report |
| `AT+QNWLOCK="<type>",0` | cell.lock | **MUTATING** | Clear matching cell lock. | QModem |
| `AT+QUIMSLOT?` | sim.switch | **READ** | Read active SIM slot where firmware implements Quectel slot control. | QModem/a field report |
| `AT+QUIMSLOT=<slot>` | sim.switch | **MUTATING** | Switch SIM slot; capability is model/firmware-dependent. | QModem |
| `AT+QMBNCFG="List"` | carrier.mbn | **READ** | List carrier MBN profiles. | a field report Quectel templates |
| `AT+CNMI=2,1,0,0,0` | sms.urc | **MUTATING** | Enable +CMTI SMS notifications; QModem RM520N-GL evidence says reapply after reboot. | QModem RM520N-GL record |
| `AT+CFUN=1,1` | reset | **MUTATING** | Restart modem; used after some persistent configuration operations. | common Quectel practice / source profiles |

### Transport / behavior notes

- QModem records RM520N USB with QMI, GobiNet, ECM, MBIM, RNDIS and NCM; PCIe variants with QMI, GobiNet and MBIM.
- a field report RM520N-GLAP PCIe/MHI profile reuses the USB RM520N AT radio profile; the endpoint changes to /dev/wwan<N>at<M> but the AT semantics do not.
- RM520N-GL QModem evidence marks cell lock non-persistent across reboot.
- RM520N-GL QModem evidence records +CMTI SMS URCs after AT+CNMI=2,1,0,0,0; setup must be reapplied after reboot.
- Composition/PCIe commands are knowledge, not safe presets. Model + firmware + current bus must be established before any write.
- Ordering-code evidence matters: RM520NGLAP-M20-SGASA is PCIe-only in Quectel support references, while RM520NGLAA variants are the USB/PCIe-capable branch. Do not infer USB capability from RM520N-GL name alone.
- Quectel support states QNWLOCK common/5g does not support NSA locking and describes QNWLOCK as a test-only command not recommended for commercial use.
- RM520NGLAAR01A07M4G release-note text reproduced on Quectel's forum describes restrictions on normal registration in RUS/IRN and warns against downgrade; scope this to affected firmware, not the whole family.

## Quectel EC25 / EG25 / EP06 / EM12 LTE command corpus

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
| `AT+QCFG="band",0,<lte-mask>,0,1` | radio.band | **MUTATING** | EM12-G form used by archival hardware profile to replace LTE mask. | a field report EM12-G profile |
| `AT+QCFG="nwscanmode"` | radio.mode | **READ** | Read legacy network scan mode. | a field report/QModem |
| `AT+QCFG="nwscanmode",0` | radio.mode | **MUTATING** | Auto mode on EP06-E field profile. | a field report EP06-E |
| `AT+QCFG="nwscanmode",2` | radio.mode | **MUTATING** | WCDMA-only on EP06-E field profile. | a field report EP06-E |
| `AT+QCFG="nwscanmode",3` | radio.mode | **MUTATING** | LTE-only on EP06-E field profile. | a field report EP06-E |
| `AT+QCFG="usbnet"` | transport.composition | **READ** | Read USB network mode. | a field report/QModem |
| `AT+QCFG="usbnet",<mode>` | transport.composition | **HIGH** | Change USB network mode; exact mode values are model/firmware-specific. | QModem/a field report safety model |
| `AT+QCCID` | sim.iccid | **READ** | ICCID on many Quectel LTE firmwares. | a field report common Quectel |
| `AT+CUSD=1,<code>[,<dcs>]` | ussd | **MUTATING** | Generic USSD request. EP06-E/EG06-E are field-recorded as not returning a network response despite command acceptance; do not generalize to EP06-A. | a field report quirks |
| `AT+CUSD=2` | ussd | **MUTATING** | Cancel USSD session. | generic AT / a field report USSD service |

### Transport / behavior notes

- other scripts's historical EP06 2c7c:0306 record carries ttyUSB2 AT and ttyUSB3 SMS hints; these are observations, not stable roles.
- EP06-E field evidence: no GSM radio; nwscanmode 0=Auto, 2=WCDMA, 3=LTE.
- EM12-G uses a different/larger LTE band mask than EP06-E and a QCFG=band write form with the final apply flag.
- Shared Quectel command names do not imply identical masks, field positions or supported modes across EC25/EP06/EM12.
- EP06-E/EG06-E USSD unsupported observation is deliberately scoped to those E variants.
- Telegram reports working ModemManager/mmcli USSD on an unspecified EP06 in QMI mode, conflicting with the existing EP06-E/EG06-E raw-CUSD negative field evidence. Keep capability variant/control-path dependent.

## Fibocom FM350 / RW350 MediaTek command corpus

| Command | Category | Risk | Purpose | Source |
|---|---|---|---|---|
| `AT+GTACT?` | radio.mode_band | **READ** | Read RAT/preference/band selection. | a field report/QModem |
| `AT+GTACT=?` | radio.capability | **READ** | Read accepted RAT and UMTS/LTE/NR band values; used to distinguish 5G-capable and GL-16/RW350 4G-only variants sharing IDs. | a field report FM350 |
| `AT+GTACT=<rat>[,<pref1>,<pref2>[,<bands>]]` | radio.mode_band | **MUTATING** | Change RAT and/or bands. Field tests show active PDP may become dead while IP remains assigned; reconnect is required. | a field report FM350 |
| `AT+GTCCINFO?` | cell.telemetry | **READ** | Serving and neighbour-cell/carry information for FM350 family. | a field report/QModem |
| `AT+GTCAINFO?` | radio.ca | **READ** | Carrier aggregation: PCC:<band>,<pci>,<earfcn>,<dl_bw>,[<ul_bw>,]<dl_mimo>,...,<rsrp> and SCC<n>:<state>,<ul_cfg>,<band>,<pci>,<earfcn>,<dl_bw>,... (state 2 = in use). Band coded 100+B (103 = B3), 5000+n or 500+n (5078 = n78); widths in 200 kHz steps (50 = 10 MHz). RRC idle: no PCC. The PCC field count differs between firmwares (an extra ul_bw); the first four fields agree. MedveModem reads it with the metrics (r90), under QModem through its send_at. | QModem Fibocom wrappers |
| `AT+ECELL` | cell.telemetry | **READ** | Serving cell: the timing advance (TA), cell id, RAT and other radio fields. TA is a coarse round-trip step, not an exact distance; its use is in watching TA and the radio values change over time while an antenna is turned (Alpha 2). Our bench, 2026-11-16 (modems/fibocom-fm350.md): per cell RAT, cell id, TAC, MCC, MNC, PCI, RSRP and RSRQ indexes, RSRP and RSRQ in quarter dB, TA (empty in idle), three more fields, EARFCN. | a router maker's firmware 4.6.4 modeminfo FM350 poll set (an outside audit, 2026-11-14) |
| `AT+GTPKGVER?` | device.firmware | **READ** | The firmware package version, a field of its own beside the revision (AT+CGMR); never used to identify the modem. The parser waits for a real FM350 answer. | a router maker's firmware 4.6.4 modeminfo FM350 poll set (an outside audit, 2026-11-14) |
| `AT+ETHERMAL?` | hardware.temperature | **READ** | Temperature/thermal report used in current FM350 template. | a field report FM350 templates |
| `AT+GTUSBMODE?` | transport.composition | **READ** | Read USB composition. Tested FM350 USB reports modes 40/41. | a field report/QModem |
| `AT+GTUSBMODE=<mode>` | transport.composition | **HIGH** | Change Fibocom USB composition; model-generation-specific and can re-enumerate. | QModem Fibocom wrappers |
| `AT+GTDUALSIM?` | sim.switch | **READ** | Read active SIM slot on tested FM350. | a field report/QModem |
| `AT+GTDUALSIM=<slot>` | sim.switch | **MUTATING** | Switch SIM slot where supported. | QModem/a field report |
| `AT+SIMTYPE?` | sim.type | **READ** | Distinguish physical USIM/eSIM on tested FM350 slot implementation. | a field report quirks/simslot |
| `AT+ICCID` | sim.iccid | **READ** | Read ICCID. | a field report/QModem |
| `AT+GTPKGVER?` | identity.firmware | **READ** | Read Fibocom firmware package version. | a field report FM350 templates |
| `AT+CUSD=?` | ussd | **READ** | Firmware advertises CUSD capability on tested FM350, but that does not imply working supplementary services. | a field report field test |
| `AT+CUSD=1,<code>[,<dcs>]` | ussd | **UNSUPPORTED OBSERVED** | Exhaustively field-tested on 0e8d:7126/7127 without useful response; retained to document the failure mode. | a field report quirks |
| `AT+GTFCCLOCKGEN` | radio.fcc | **READ** | Request Fibocom FCC unlock challenge in other scripts hostless path. | other scripts create_hostless.sh |
| `AT+GTFCCLOCKVER=<response>` | radio.fcc | **MUTATING** | Submit computed FCC unlock response; other scripts treats +GTFCCLOCKVER: 1 as success. | other scripts create_hostless.sh |
| `AT+GTRXPATHEN=<rat>,<mask>` | radio.antenna-path | **EXPERIMENTAL** | Enable selected FM350 RX paths for per-antenna diagnostics; Telegram PoC uses masks 1/2/4/8 and must restore 15 (all paths). | Telegram field attachment fm350_antenna_scan.sh |
| `AT+CESQ` | radio.signal | **READ** | Extended signal query used by the FM350 HSPA parser and per-RX-path diagnostic; community parser maps HSPA RSCP/EcNo from CESQ raw fields. | Telegram field parser + antenna-scan attachments |
| `AT+CCHO="A0000005591010FFFFFFFF8900000100"` | sim.euicc | **READ** | Open a logical channel to the eUICC ISD-R. FM350-GL answers the bare channel number ("1") without the +CCHO: prefix. Only one tty of the modem accepts it and its number changes on re-enumeration; the other AT ports answer AT but swallow CCHO without any reply. A probe must close the channel it opened. | a field report esim.sh / esim-apdu-bridge.sh (v3.2.2) |
| `AT+CGLA=<channel>,<length>,"<apdu>"` | sim.euicc | **MUTATING** | Send one APDU on the open logical channel. <length> counts the hex characters of the APDU (27.007); FM350 does not answer a byte count. An empty answer is a lost exchange (port taken, re-enumeration), not an empty card response. | a field report esim.sh / esim-apdu-bridge.sh (v3.2.2) |
| `AT+CCHC=<channel>` | sim.euicc | **MUTATING** | Close a logical channel. The card has a handful of channels; one leaked by a killed operation makes the next CCHO go unanswered on every port, which reads as "no eSIM". Closing channels 1..4 recovers without a power cycle (verified on FM350). | a field report esim.sh / esim-apdu-bridge.sh (v3.2.2) |
| `AT+GTESIMCFG=0,0,0` | sim.euicc | **HIGH** | Listed as "Unlock ESIM" in QModem's Fibocom command menu. Semantics and persistence are not documented in the reviewed sources; reference only until a vendor manual or hardware test explains it. | QModem at_commands_en.json |

### Transport / behavior notes

- QModem records FM350/FM350R USB as RNDIS and PCIe variants as MBIM. Treat bus as a first-class part of the profile.
- a field report field evidence for USB 0e8d:7126/7127 reports only GTUSBMODE 40/41 compositions, both RNDIS, and uses a dedicated fibocom netifd protocol.
- GTACT band changes can leave L3 configuration looking alive while traffic is dead; reconnect the interface rather than assuming an AT success means a usable session.
- 0e8d:7127 eUICC is field-confirmed; GTDUALSIM/SIMTYPE are used for slot/type handling.
- USSD is field-confirmed non-working on the tested 7126/7127 firmware even after encoding/RAT/port permutations.
- other scripts treats 0e8d:7126/7127 in its hostless/ECM connection path and uses historical fixed comm-port offsets 1/3 plus an FCC challenge-response. QModem/a field report describe the tested USB compositions as RNDIS; preserve the source distinction rather than collapsing the labels.
- Telegram field evidence shows GTUSBMODE 40/41 mapping to 0e8d:7127/7126 on one a router maker's firmware test firmware, with control/AT/SMS sharing one ACM endpoint per composition. This is strong lease/arbitration evidence, not a stable tty map.
- A community antenna-scan PoC actively changes GTRXPATHEN and restores mask 15 on exit; it is an Expert diagnostic, not a background metric.
- eSIM over AT: lpac 2.3.0's native AT driver stalls on FM350-GL (es10b_prepare_download stops at exactly 240 s; the driver's persistent at_expect buffer desynchronizes on long CGLA chains). The only download field-confirmed to finish on FM350 ran lpac with LPAC_APDU=stdio and a bridge sending short CCHO/CGLA/CCHC exchanges.
- lpac's stdio APDU driver is broken in the v2.3.0 release (a request is treated as a failure before the answer is read; upstream fix 977c324, 2025-11-02, after the release). A stdio bridge needs lpac 2.2.x or a build with that fix.
- In the fibocom/RNDIS USB composition FM350 has no cdc-wdm node: eUICC access is AT-only. QModem records PCIe FM350 as MBIM; eUICC access over MBIM on PCIe is not field-verified.
- With an empty eUICC the fibocom dial protocol retries registration every ~70 s and holds the AT port, starving a profile download on the same port; the dialer must be paused for the operation (a profile download also needs uplink to the SM-DP+).
- The FM350 eUICC is slow on Store-Data: a profile download may need up to 10 minutes.
- After a profile is enabled FM350 performs the SGP.22 REFRESH itself; no CFUN reset is needed (unlike SDX55-based T99W175).
- A SIM slot switch re-enumerates the modem and FM350 resets its band mask to all bands: saved bands and APN are re-applied after it.
- eUICC-capable IDs in field use: 0e8d:7127 (eUICC confirmed on hardware) and 0e8d:7126.

## Fibocom L850 / L860 Intel XMM command corpus

| Command | Category | Risk | Purpose | Source |
|---|---|---|---|---|
| `AT+XACT?` | radio.mode_band | **READ** | Read XMM RAT/preference/band selection. | a field report XMM profiles |
| `AT+XACT=?` | radio.capability | **READ** | Read supported XMM modes/bands. | a field report XMM templates |
| `AT+XACT=<rat>,<pref1>,<pref2>,<bands>` | radio.mode_band | **MUTATING** | Change RAT/band selection. Current profile preserves the existing band tail because a shorter form can reset choices. | a field report L850/L860 |
| `AT+XLEC?` | radio.ca | **READ** | LTE aggregation/component-carrier report. | a field report XMM |
| `AT+XMCI=1` | cell.telemetry | **MUTATING** | Enable/request XMM cell information. Kept separate from passive polling because it changes reporting behavior on some stacks. | a field report XMM / a router maker's firmware ownership lessons |
| `AT+RSRP?` | radio.signal | **READ** | Per-branch RSRP. | a field report XMM templates |
| `AT+RSRQ?` | radio.signal | **READ** | Per-branch RSRQ. | a field report XMM templates |
| `AT+XCCINFO?` | cell.telemetry | **READ** | XMM cell information query where firmware supports it. | a field report XMM profiles |
| `at@errc:pcell_scell_measurement_info()` | radio.diagnostic | **READ** | Intel engineering measurement view of primary/secondary cells/antenna branches. | a field report XMM templates |
| `at@nvm:cal_usbmode.num?` | transport.composition | **READ** | Read XMM USB composition NVM value. | a field report XMM |
| `AT+GTUSBMODE=0` | transport.composition | **HIGH** | Legacy Fibocom composition switch attempt; L860 fw 18600/18601 may reject it. | a field report modemswitch |
| `at@nvm:cal_usbmode.num=0` | transport.composition | **HIGH** | Set USB composition NVM on L860 firmware where GTUSBMODE was removed. | a field report modemswitch |
| `at@store_nvm(cal_usbmode)` | nv.parameters | **HIGH** | Persist cal_usbmode; without this store, the selected mode is lost on reboot. | a field report modemswitch |
| `at@sic:freq_lock(0,3,<band>,1,<earfcn>,<pci>)` | cell.lock | **HIGH** | Write-only LTE cell/frequency lock. Requires flight-mode/reset sequence. | a field report L850/L860 |
| `at@sic:freq_lock(0,3,255,0,4294967295,65535)` | cell.lock | **MUTATING** | Clear XMM frequency/cell lock using firmware sentinel values. | a field report L850/L860 |
| `AT+CFUN=4` | radio.power | **MUTATING** | Flight mode used before XMM freq_lock changes. | a field report L850/L860 |
| `AT+CFUN=15` | reset | **HIGH** | Intel full reset used after freq_lock; may re-enumerate USB. | a field report L850/L860 |
| `AT+CFUN=1` | reset | **HIGH** | Normal radio enable, but field evidence on L860-GL-16 fw 18601 shows it can leave endless network search when used to finish freq_lock; use context-specific recipe. | a field report field report |
| `AT+GTDUALSIM?` | sim.switch | **READ** | Fibocom SIM-slot query on supported XMM variants. | a field report templates |

### Transport / behavior notes

- other scripts records 8087:095a and 2cb7:000b as XMM compositions with historical ttyACM0 AT / ttyACM2 SMS hints; 2cb7:0007 is another observed composition.
- XACT values used by current L850/L860 profile: 1=3G, 2=4G, 4=3G+4G/Auto; 3G bands are plain numbers, LTE bands are 100+band.
- L860 fw 18600/18601 may require direct NVM cal_usbmode writes because GTUSBMODE was removed.
- Cell lock is effectively write-only; the profile remembers what it wrote because firmware does not provide a matching read form.
- The CFUN completion sequence is firmware-sensitive: CFUN=15 is a documented field workaround after freq_lock on affected L860.
- Telegram kernel/syslog captures for 8087:095a repeatedly show ttyACM and CDC-NCM functions disappearing and returning during initialization; multiple hotplug/setup passes can belong to one physical attachment lifecycle.
- A live modem-diag capture shows ttyACM0/1/2 plus wwan0/1/2, with only wwan0 active. Multiple USB network functions are not equivalent to multiple usable sessions.

## Quectel RM551E-GL scoped command corpus

| Command | Category | Risk | Purpose | Source |
|---|---|---|---|---|
| `AT+CGMI` | identity | **READ** | Manufacturer identity from the dedicated 2c7c:0122 profile. | a field report RM551E profile |
| `AT+CGMM` | identity | **READ** | Model identity from the dedicated profile. | a field report RM551E profile |
| `AT+CGMR` | identity | **READ** | Firmware revision used by the dedicated profile; exact full-build scope still matters for field quirks. | a field report RM551E profile |
| `AT+QNWINFO` | radio.serving | **READ** | Current RAT/operator/band/channel summary. | a field report RM551E profile |
| `AT+QCAINFO` | radio.ca | **READ** | Carrier aggregation information. | a field report RM551E profile |
| `AT+QENG="servingcell"` | cell.telemetry | **READ** | Serving-cell and radio diagnostics. | a field report RM551E profile |
| `AT+QSPN` | sim.operator | **READ** | Service-provider name in the RM551E telemetry batch. | a field report RM551E profile |
| `AT+QNWPREFCFG="lte_band"` | radio.band | **READ** | Read LTE band preference list. | a field report _quectel_common2 |
| `AT+QNWPREFCFG="lte_band",<bands>` | radio.band | **MUTATING** | Set LTE band list. RM551E issue #24 reports immediate live apply; verify by read-back and keep firmware scope. | a field report issue #24 + _quectel_common2 |
| `AT+QNWPREFCFG="nsa_nr5g_band"` | radio.band | **READ** | Read NSA NR band preference list. | a field report _quectel_common2 |
| `AT+QNWPREFCFG="nsa_nr5g_band",<bands>` | radio.band | **MUTATING** | Set NSA NR bands. Live 77↔78 changes were field-tested on RM551EGL00AAR02A02M8G without reconnect. | a field report issue #24 |
| `AT+QNWPREFCFG="nr5g_band"` | radio.band | **READ** | Read SA NR band preference list. | a field report _quectel_common2 |
| `AT+QNWPREFCFG="nr5g_band",<bands>` | radio.band | **MUTATING** | Set SA NR bands; vendor-family manual says saved automatically, but Zoo retains firmware/model scope. | a field report issue #24 + Quectel family manual |
| `AT+QNWPREFCFG="mode_pref"` | radio.mode | **READ** | Read RAT preference. | a field report _quectel_common2 |
| `AT+QNWPREFCFG="mode_pref",<AUTO\|WCDMA\|LTE\|LTE:NR5G\|NR5G>` | radio.mode | **MUTATING** | Set RAT preference using the newer Quectel word values. | a field report _quectel_common2 |
| `AT+QNWLOCK="common/4g"` | cell.lock | **READ** | Read LTE cell lock where firmware returns the documented form. | a field report _quectel_common2 |
| `AT+QNWLOCK="common/4g",1,<EARFCN>,<PCI>` | cell.lock | **MUTATING** | Lock one LTE cell; EARFCN and PCI are a required pair in this recipe. | a field report _quectel_common2 |
| `AT+QNWLOCK="common/4g",0` | cell.lock | **MUTATING** | Clear LTE cell lock. | a field report _quectel_common2 |
| `AT+QNWLOCK="save_ctrl",1,1` | cell.lock.persistence | **HIGH** | Persist the lock across modem reboot. Persistence makes this an operator-level action. | a field report _quectel_common2 |
| `AT+CPMS?` | sms.storage | **READ** | Read SMS storage. Issue #28 captured CPMS? stuck in D state during one data freeze; the command is read-only but repeated concurrent AT access is part of the firmware-specific contention evidence. | a field report issue #28 |

### Transport / behavior notes

- QModem records RM551E-GL USB modes QMI/GobiNet/ECM/MBIM/RNDIS/NCM and PCIe modes QMI/GobiNet/MBIM. These are reference rows, not a claim about the live composition.
- The dedicated a field report USB 2c7c:0122 profile obtains the current VID:PID from the active sysfs modem path and uses profile helpers for protocol resolution; fixed tty numbering is not identity.
- Issue #24: RM551EGL00AAR02A02M8G applied LTE/NR QNWPREFCFG writes live; read-back was more reliable than trusting an empty successful sms_tool write response.
- Issue #28: on RM551EGL00AAR02A02M8G_01.001.01.001 over USB/qmiraw, periodic AT polling correlated with a much higher data-session freeze rate. ModemManager was inhibited and the application owned AT ports.
- Issue #28 also reports a similar failure previously seen on another host over PCIe/MHI, supporting a firmware-level hypothesis but not proving a universal RM551E defect.
- The shared profile intentionally does not implement common/5g cell lock without confirmed live application. Do not synthesize a 5G lock preset from command discovery alone.

## Extended vendor corpus

The tables above are curated family-specific knowledge. The much larger normalized QModem and other scripts corpora remain searchable in [AT-REFERENCE.md](AT-REFERENCE.md). Commands from that appendix are not automatically inherited by every model of the same vendor.
