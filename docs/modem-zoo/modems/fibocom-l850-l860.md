# Fibocom L850 / L860 Intel XMM family

> **Status:** `multi-source-field-rich`.

This is the Intel-XMM branch of the Fibocom zoo. Its `XACT/XLEC/XMCI` semantics and NVM composition handling are fundamentally different from MediaTek FM350 and Qualcomm FM160.

## Historical compositions

other scripts observations:

- `8087:095a` — XMM, AT `ttyACM0`, SMS `ttyACM2`;
- `2cb7:000b` — XMM, AT `ttyACM0`, SMS `ttyACM2`;
- `2cb7:0007` — another L850 observation with the same historical tty hints.

Again: tty numbers are evidence, not roles.

## XACT radio model

Current a field report L850/L860 logic treats:

- 1 = 3G;
- 2 = 4G;
- 4 = 3G+4G / Auto in UI semantics.

3G bands are plain numbers; LTE bands are encoded as `100 + band`.

When changing mode, the existing band tail must be preserved because shorter `XACT` writes can reset the band selection.

## Cell lock: write-only engineering interface

The family uses Intel engineering command:

`at@sic:freq_lock(...)`

The profile does not have a reliable corresponding read operation; it remembers what it wrote.

The field-proven sequence around lock changes matters:

1. `AT+CFUN=4`;
2. write/clear `freq_lock`;
3. `AT+CFUN=15`.

On an observed L860-GL-16 fw 18601, finishing with `CFUN=1` instead could leave the modem in endless network search. `CFUN=15` fixed it and may re-enumerate USB.

## USB composition / NVM

L860 firmware 18600/18601 can reject the older `AT+GTUSBMODE=0` path. a field report's working fallback is:

`at@nvm:cal_usbmode.num=0`

followed by:

`at@store_nvm(cal_usbmode)`

Without the store operation the selected composition is lost after reboot.

## Telemetry

Useful XMM-specific diagnostics include `XLEC`, `XMCI`, `RSRP`, `RSRQ`, `XCCINFO` and the engineering `pcell_scell_measurement_info()` view.

`XMCI=1` is deliberately not labelled a passive read: it can alter reporting state and therefore belongs behind ownership/lease arbitration.

## Known AT command corpus

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


## MedveModem lesson

For XMM, the “recipe” is often more important than the individual command: ordering, reset type and NVM persistence determine whether the modem returns usable.

## Telegram field evidence — re-enumeration is part of the lifecycle

Multiple attached logs for `8087:095a` show the same physical modem repeatedly losing and recreating `ttyACM0/1/2` and CDC-NCM functions during initialization. a router maker's firmware explicitly describes several reconnect cycles as normal for some Fibocom initialization paths.

A live diagnostic also shows three network functions (`wwan0/1/2`) while only `wwan0` is active and addressed. This reinforces two Zoo rules:

- one USB function is not one modem;
- one uevent is not one stable attachment.

For MedveModem, debounce/rescan plus attachment generations are not theoretical niceties — they correspond to real L850/L860 event storms.

A separate L860-GL-16 field recipe adds a useful cell-lock nuance. With the same B7/EARFCN, a literal PCI reportedly left only the primary carrier and no usable internet; using PCI `65535` kept B7 primary while secondary carriers returned after about ten seconds and remained stable during a three-day observation. The follow-up completes the operation with `AT+CFUN=15`. Zoo records `65535` only as **field-observed wildcard behavior**, not as a universal Intel contract; a future cell-lock job must verify registration, traffic and CA and retain an explicit rollback path.

See [Telegram field notes](../TELEGRAM-FIELD-NOTES.md).

<!-- 4pda-field-2026-10-15:start -->
## 4PDA — дополнительные модельные полевые факты

**Reference only; not hardware-verified.** Данные из пользовательских HTML-архивов; связь `message-N` — только с локальным архивом. Нет новых runtime presets/AT operations. Все кейсы и SHA256: [4PDA field notes](../4PDA-FIELD-NOTES.md).

- **`4PDA-1066668-207`** (danger): После попытки MBIM и обновления FW появился циклический reboot; AT+GTUSBMODE в последующих прошивках не помогал. **MedveModem:** Вводить manual recovery; FW downgrade не выполнять автоматически. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668).
- **`4PDA-1066668-8873`** (danger): После неудачной перепрошивки L860 постоянно перезапускался; cal_usbmode=0 и 10 со store_nvm и CFUN=15 возвращали OK. **MedveModem:** OK от AT-setter ≠ успешная смена композиции / recovery. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668).
- **`4PDA-1066668-10532`** (read): OpenWrt 23.05.3, два L860, 8087:095a + 2cb7:000b, USB2 hub; dmesg `Not enough host controller resources for new device state` и `can't set config #1, error -12`. **MedveModem:** Classify USB resource exhaustion; не лечить сменой IMEI/VID/PID и бесконечными resets. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668).
- **`4PDA-1066668-16498`** (write): Debian, FW 18601.5001.00.01.16.11, ttyACM0..2 и 3 NCM. mmcli выбрал PPP; dhclient не дал адрес; повторный CGACT=1,1 на активном context автор связал с крашем. **MedveModem:** Перед CGACT проверять CGACT?; не применять DHCP к NCM по умолчанию; DTR lifetime; no competing MM/NM. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668).
- **`4PDA-1066668-570`** (read): В логе сначала 8087:07f9, затем USB disconnect и 8087:095a, `MODEM + 2 CDC-ACM + 3 CDC-NCM + SS`, ttyACM0..2, три CDC-NCM. **MedveModem:** Тест временной шкалы и USB ID перехода, hotplug wait/rebind вместо immediate brick. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668).
- **`4PDA-1066668-17319`** (danger): Автор переключил L850 GTUSBMODE=7, в OpenWrt выбрал протокол ModemManager и APN, повторные reboot и hotplug работали. **MedveModem:** Фикс ограничен конкретным стендом; устройство не конвертировать автоматически в MBIM. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1066668).

<!-- 4pda-field-2026-10-15:end -->


<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### xmm-firmware-18601-mode

**Observation:** L860 fw 18600/18601 rejects GTUSBMODE=0; NVM cal_usbmode.num=0 + store_nvm required for persistent NCM/XMM, then CFUN=15.

- **Scope:** models=L850-GL, L860-GL; firmware=18600, 18601; post_usb_ids=8087:095a
- **Type / confidence:** `composition-switch` / `external-field-report`
- **Regression contracts:** GTUSBMODE ERROR must not be interpreted as completed switch; NVM write requires explicit owner authorization and recovery path; re-enumeration expected ~40 s
- **Safety:** Persistent flash/NVM write; not automatic. CFUN=15 may also be required for L860 cell lock changes.
- **Original source:** issue / release / pinned source

### Composition and boot-mode recipes (reference only)

Exact operation stages, exit checks and prerequisites: [composition matrix](../COMPOSITION-AND-BOOT-MODES.md).

- **`xmm-l860-legacy-gtusbmode`** — `COMPOSITION_MUTATION`; Fibocom Intel XMM MBIM/other → XMM/NCM, often 8087:095a; explicit approval required; ~40 seconds disappearance can be expected; no fixed ttyACM numbering.
- **`xmm-l860-nvm-fallback`** — `PERSISTENT_COMPOSITION_WRITE`; L860 fw 18600/18601 GTUSBMODE returns ERROR → XMM/NCM; explicit approval required; The `at@store_nvm` step writes persistent parameters. No automatic action.

<!-- upstream-another modem manager-v3.5.2:end -->


<!-- another modem manager-delta-v3.5.16:start -->
## another modem manager–v3.5.16 — release delta (2026-10-21)

**Reference only; not bench-validated.** Read from the diff v3.5.5..v3.5.16 at `a22590c108cc`; no upstream code, no runtime preset. Full review: another modem manager delta.

- **`…-v3512-xmm-xgauth`** (`2cb7:000b`) — `upstream-code-reference`, not-bench-validated: For XMM modules PAP/CHAP credentials are set with AT+XGAUTH=1,&lt;0 none|1 PAP|2 CHAP&gt;,"user","pass" (no login: AT+XGAUTH=1,0,"",""), falling back to AT+CGAUTH when XGAUTH returns ERROR. Their packaged xmm proto loses the login, so XMM interfaces with credentials are migrated to their fibocom proto. Source: a field report/another modem manager @ `a22590c` (root/lib/netifd/proto/fibocom.sh:121-145, root/etc/uci-defaults/52-another modem manager-xmm-auth-to-fibocom.sh).
  - The migration is a one-time uci-defaults script; interfaces without credentials stay on the xmm proto.

<!-- another modem manager-delta-v3.5.16:end -->
