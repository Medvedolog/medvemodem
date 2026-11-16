# Quectel RM520N / RM5xx Qualcomm 5G family

> **Status:** `reference-rich-with-field-hazards` · external evidence is not runtime verification.

This page groups the popular Qualcomm-based RM500/RM502/RM520/RG520 generation only where the sources show shared command/transport behavior. Regional variants and firmware remain separate evidence.

## Transport model

QModem records the family with a very broad USB transport matrix:

- QMI / GobiNet;
- MBIM;
- ECM;
- RNDIS;
- NCM.

For PCIe variants it records **QMI / GobiNet / MBIM**.

a field report's RM520N-GLAP `17cb:0308` PCIe/MHI profile deliberately reuses the RM520N-GL AT radio profile: the control endpoint becomes `/dev/wwan<N>at<M>`, but the radio command language is the same.

This means Zoo must keep these dimensions separate:

```text
model + firmware
  -> bus (USB / PCIe-MHI)
  -> current composition
  -> data owner/protocol
  -> AT/QMI control endpoint
```

## Radio / cell telemetry

The strongest shared corpus is:

- `QENG="servingcell"` / `QENG="neighbourcell"`;
- `QCAINFO`;
- `QRSRP`;
- `QNWINFO`;
- `QTEMP`;
- `QNWPREFCFG` for 5G-era mode/band preference;
- `QNWLOCK` for cell locking.

QModem's RM520N-GL evidence marks cell lock as **non-persistent across reboot**.

## SMS / SIM / eSIM

QModem records for RM520N-GL firmware `RM520NGLAAR03A03M4G`:

- `AT+CNMI=2,1,0,0,0`;
- SMS URC prefix `+CMTI:`;
- setup must be reapplied after reboot.

It also records voice-related URCs `RING`, `+CLCC:`, and `NO CARRIER`.

a field report's capability map includes RM500Q/RM520N variants with eUICC support; slot control can use `QUIMSLOT` when the firmware exposes it. This must be capability-tested rather than assumed from the Quectel badge.

## Dangerous composition / recovery commands

This family is one of the strongest reasons for keeping destructive knowledge in Zoo.

a field report explicitly marks writes to `QPCIE`, `QCFG=usbcfg`, `QCFG=data_interface`, `QCFG=pcie/mode`, `QFASTBOOT` and `QPRTPARA=3` as dangerous.

Most severe observation: `AT+QPCIE="adb",1` on a PCIe RM520N was reported to disable the USB ports persistently; cited recovery required EDL/reflash.

## Known AT command corpus

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


## Source interpretation

- **a field report**: richest current RM520N parser, PCIe/USB hazard knowledge and field comments.
- **QModem**: broad regional model/transport database, SMS URC and vendor wrapper corpus.
- **other scripts**: long-lived Quectel field handling, protocol and SMS experience.
- **MedveModem**: keeps those observations separate from verified capabilities.

## MedveModem model

No write should be selected from VID:PID alone. A safe future semantic operation needs model + firmware + bus + current composition + owner + lease.

## Telegram + vendor field evidence

### GLAP is not GLAA

The Telegram export distinguishes `RM520NGLAP-M20-SGASA` from `RM520NGLAA-M20-SGASA`. Quectel support independently confirms the important part: GLAP/eFuse variants are PCIe-only and cannot simply be switched back to USB with an AT command. Exact ordering code must therefore be part of capability identity.

### Firmware can change regional operability

A chat warning about Russian registration restrictions is externally corroborated by a Quectel forum post reproducing the `RM520NGLAAR01A07M4G` release-note statement restricting normal registration in **RUS/IRN** and warning against downgrade. Zoo scopes this to affected firmware; it is not generalized to every RM520N branch.

### QNWLOCK is experimental

Quectel support repeatedly describes `QNWLOCK` as a testing command rather than a commercial control interface and states `common/5g` does not lock NSA cells. This makes post-action attach validation mandatory if a future MedveModem Expert action exposes it.

See [Telegram field notes](../TELEGRAM-FIELD-NOTES.md).

<!-- 4pda-field-2026-10-15:start -->
## 4PDA — дополнительные модельные полевые факты

**Reference only; not hardware-verified.** Данные из пользовательских HTML-архивов; связь `message-N` — только с локальным архивом. Нет новых runtime presets/AT operations. Все кейсы и SHA256: [4PDA field notes](../4PDA-FIELD-NOTES.md).

- **`4PDA-1106442-2`** (read): Автор сообщает AA поддерживает USB и PCIe; AP — только PCIe. **MedveModem:** Separate `usb`/`pcie_mhi` transport profile, no phantom USB probing on AP. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442).
- **`4PDA-1106442-93`** (danger): После переходов RJ45↔QMI связь не поднималась, хотя a router maker's firmware распознавал модем. **MedveModem:** Capture QMAP/MPDN route state; no blind usbnet toggling in failover. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442).
- **`4PDA-1106442-1938`** (danger): После прошивки AP инженерного образца на 001 аппарат был недоступен. Автор восстановил EDL/QFIL с erase-all и затем собственным backup; аналогичный опыт для HP. **MedveModem:** EDL/erase-all explicitly forbidden in unattended recovery. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442).
- **`4PDA-1106442-2375`** (danger): AP после AT USB mode потерял AT доступ. Другой AA временно оживал после прошивки на ~2 дня, позже снова умер. **MedveModem:** AT missing != no modem: differentiate PCIe/QDL/USB, no repeated flash looping. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442).
- **`4PDA-1106442-2378`** (read): В AT+QCAINFO автор получил PCC LTE B3 и SCC B1, B3, B7, B7 — одновременно пять LTE компонент. **MedveModem:** Parse variable number of SCC and band duplicates, snapshots not fixed supported CA. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1106442).

<!-- 4pda-field-2026-10-15:end -->


<!-- upstream-field-notes-v3.5.2:start -->
## Field incidents — archived field notes

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### Composition and boot-mode recipes (reference only)

Exact operation stages, exit checks and prerequisites: [composition matrix](../COMPOSITION-AND-BOOT-MODES.md).

- **`quectel-usbnet`** — `COMPOSITION_MUTATION`; Quectel USB composition driver-dependent → QMI/MBIM/ECM/RNDIS variant; explicit approval required; Mode number mappings differ by Quectel model; no universal value table.

<!-- upstream-field-notes-v3.5.2:end -->


<!-- external-another package-modem5g-2026-10-16:start -->
## another package / another package — external field facts (2026-10-16)

**Reference only; not bench-validated.** Checked against the pinned upstream files and the issue/PR pages; no upstream code, no runtime preset. All cases: another package / another package field notes.

- **`rm520n-gl-aa-qmi-single-dialer-modem5g`** (`field-reported`, external-field-report): Working QMI setup: USB interface 4 (x-1:1.4) is the QMI WWAN endpoint, with /dev/cdc-wdm0, wwan0 and AT on ttyUSB2. Starting two connection-manager instances (one IPv4, one IPv6) on the same cdc-wdm0 made both fail with QMI client-ID timeouts; one instance with IPv4 and IPv6 enabled worked. The kernel's initial binding already created wwan0, so the unbind/rebind the scripts did was unnecessary. Source: [a10463981/another package TROUBLESHOOTING.md](https://github.com/a10463981/another package/blob/43cd09745ffc2fb276196e215ba64e112837e8c5/TROUBLESHOOTING.md).
- **`rm500q-gl-qmi-no-cdc-wdm-issue44`** (`field-reported`, external-field-report): In QMI mode no /dev/cdc-wdm0 appeared, while every other USB mode worked. The maintainer judged it most likely a driver problem and suggested building only Quectel's QMI driver instead of the generic one; the reporter never confirmed the fix. Source: another package/5G-Modem-Support issue #44 (closed 2025-12-23 without confirmation).
- **`rg500q-ea-controller-crash-wan-flap-modem5g-issue4`** (`field-reported`, external-field-report): The management daemon died about every 6-7 minutes and procd respawned it; some 20 s later the connection manager (a child of init, not of the daemon) also restarted, wwan0 went down for about 8 s and DHCP handed out a new IPv4 address. The reporter ruled out OOM (no kill in dmesg, ~16 MB RSS). No maintainer reply; the cause is unknown. Source: [a10463981/another package issue #4 (open, opened 2026-09-22)](https://github.com/a10463981/another package/issues/4).
- **`quectel-pcie-mhi-linux-6-12-build-break-issue48`** (`field-reported`, external-field-report): The vendor MHI driver fails to compile on Linux 6.12: no_llseek no longer exists, and the bus match callback now takes a const struct device_driver pointer. A commenter proposed noop_llseek and the const signature; nobody reported that the rebuilt driver then worked at runtime. Source: another package/5G-Modem-Support issue #48 (open).

<!-- external-another package-modem5g-2026-10-16:end -->
