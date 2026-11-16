# Quectel RM551E-GL

**Status:** multi-source-field-rich-reference — **не MedveModem verified support**

RM551E-GL заслуживает отдельной клетки Zoo. Синтаксис части команд пересекается с RM5x0N, но полевое поведение по AT contention и live band apply уже достаточно специфично, чтобы не прятать модель внутри общей страницы RM520N.

## Identity / bus / transport

| Layer | Evidence |
|---|---|
| Model | RM551E-GL |
| USB | QModem: QMI / GobiNet / ECM / MBIM / RNDIS / NCM |
| PCIe | QModem: QMI / GobiNet / MBIM |
| Dedicated OpenWrt field profile | a field report `2c7c:0122` |
| Data owner | runtime fact; issue #28 tested application-owned AT with ModemManager inhibited |
| Control transport | resolve from current topology/driver/owner; never from a fixed tty |

QModem rows are reference-unvalidated. a field report's dedicated USB profile is stronger model-scoped evidence, but it still does not turn every listed QModem composition into verified MedveModem support.

## Dedicated a field report profile

`modem/usb/2c7c0122` is explicitly RM551E-GL. It collects `QNWINFO`, `QCAINFO`, `QENG="servingcell"`, `QSPN`, standard identity/SIM fields and uses the active modem's sysfs identity for protocol resolution.

That last point matters: a copied fixed port/VID:PID context had already caused wrong protocol detection in this code lineage. Zoo therefore keeps **physical attachment → current functions/drivers → owner → control endpoint** as the order of truth.

## Firmware-scoped field observation: live band apply

a field report issue #24 tested RM551E-GL firmware `RM551EGL00AAR02A02M8G` over USB/qmiraw.

- `QNWPREFCFG` NSA 78→77, 77→78 and 78→77 changes were applied without a reconnect.
- The NR leg disappeared/reappeared within seconds while the data session stayed up.
- A successful write could produce no useful `sms_tool` output, so the reliable pattern was **read → compare → write → read-back**.
- The reporter explicitly tested RM551E, not older RM520N hardware. Zoo does not generalize this field result across generations.

## Firmware-scoped field observation: background AT contention

Issue #28 is more important architecturally than another command list. On `RM551EGL00AAR02A02M8G_01.001.01.001`, USB/qmiraw, with ModemManager inhibited and the application owning AT ports:

| AT pressure | Observed freezes |
|---|---:|
| telemetry + 60 s SMS polling | 3 / 7 h |
| no AT at all | 0 / 24.2 h |
| SMS polling only | 3 / 14 h |

One freeze caught `AT+CPMS?` stuck in D state. The report also says a similar failure had occurred on another host over PCIe/MHI, which supports a firmware-level hypothesis but does **not** prove a universal RM551E defect.

**Zoo conclusion:** owner and AT scheduling are part of modem compatibility. A read-only command can still be operationally hazardous when a firmware/data path reacts badly to parallel or periodic control traffic.

## Scoped AT corpus

See [AT-COMMANDS.md](../AT-COMMANDS.md) → **Quectel RM551E-GL scoped command corpus**.

Important entries include:

- `AT+QNWPREFCFG="lte_band"`
- `AT+QNWPREFCFG="nsa_nr5g_band"`
- `AT+QNWPREFCFG="nr5g_band"`
- `AT+QNWPREFCFG="mode_pref"`
- `AT+QNWLOCK="common/4g",1,<EARFCN>,<PCI>`
- `AT+QNWLOCK="save_ctrl",1,1`

`save_ctrl` is persistent. The shared field profile deliberately does not synthesize a 5G lock preset without confirmed live use.

## What is not claimed

- no fixed tty number is authoritative;
- USB/PCIe presence does not select a data owner;
- QModem's mode list is not proof of the currently loaded composition;
- issue #24/#28 are external field reports, not our hardware verification;
- RM551E observations are not silently inherited by RM520N.

## Provenance

- a field report 
  - `…/root/usr/share/…/modem/usb/2c7c0122`, blob `b20753300e5e1313302fbbf282b452c9c211f1b2`
  - `…/root/usr/share/…/modemband/_quectel_common2`, blob `cf993831daa71f6683287177db08c19fa194c670`
  - GitHub issue #24, created 2026-09-15
  - GitHub issue #28, created 2026-09-18
- QModem pin `c49654efc870f53712ee8e25bf181722eb1466d9`
  - `modem_support.usb.rm551e-gl`
  - `modem_support.pcie.rm551e-gl`
- normalized record: `reference/evidence/vendor-family-observations.json`

<!-- upstream-field-notes-v3.5.2:start -->
## Field incidents — archived field notes

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### rm551-rawqmi-at-poll-freeze

**Observation:** On RM551E qmiraw, AT polling induced ~5x more data-plane freezes; no_at with tele.refresh and sms.mirror_interval disabled improved stability.

- **Scope:** model=RM551E-GL; usb_ids=2c7c:0122; firmware=RM551EGL00AAR02A02M8G_01.001.01.001
- **Type / confidence:** `control-backend` / `external-field-report`
- **Regression contracts:** zero unleased background AT reads when no_at is set; data-plane freeze != modem disappearance
- **Safety:** Disable ALL hidden AT consumers, including eSIM/SMS/UI, under no_at.
- **Original source:** issue / release / pinned source

### rm551-qmicli-unsupported-t

**Observation:** libqmi 1.36 qmicli -t option incompatibility yielded command failure falsely blamed on modem; reset not warranted.

- **Scope:** model=RM551E-GL; libqmi=1.36; backend=qmiraw
- **Type / confidence:** `false-diagnosis` / `external-field-report`
- **Regression contracts:** treat cli-argument/usage failure as tooling failure, not modem hang; block recovery escalation on unsupported argument
- **Safety:** Probe supported CLI flags by installed tool version.
- **Original source:** issue / release / pinned source

### rm551-virtual-hub-usbpower

**Observation:** USB port disable in VM / ASM3242 root hub produced -110 and missing modem for ~55 minutes until USB controller reset.

- **Scope:** model=RM551E-GL; platform=VM passthrough ASM3242 xHCI
- **Type / confidence:** `recovery-hazard` / `external-field-report`
- **Regression contracts:** root hub/virtual controller must never be considered validated switchable modem port; absence after USB reset requires bounded stop, not reset loop
- **Safety:** No unattended root-hub disable or unbind.
- **Original source:** issue / release / pinned source

### rm551-live-band-double-cfun

**Observation:** On specific RM551E firmware QNWPREFCFG band mask applied live; overlapping CFUN resets broke network session unnecessarily.

- **Scope:** model=RM551E-GL; firmware=RM551EGL00AAR02A02M8G; band=QNWPREFCFG
- **Type / confidence:** `radio-mutation` / `external-field-report`
- **Regression contracts:** read back band before deciding reset; deduplicate restart across competing managers
- **Safety:** Do not generalize live apply to all Quectel firmwares.
- **Original source:** issue / release / pinned source

<!-- upstream-field-notes-v3.5.2:end -->
