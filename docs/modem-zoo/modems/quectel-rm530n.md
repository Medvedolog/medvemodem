# Quectel RM530N-GL

**Status:** multi-source-reference — **не MedveModem verified support**

RM530N-GL нельзя считать просто «RM520N с другим названием». Quectel позиционирует его как Release-16 M.2 модуль с Sub-6 + mmWave, сохраняя совместимость форм-фактора с RM520N. Для Zoo это отдельная product/firmware family.

## Bus / composition / control

| Layer | Evidence |
|---|---|
| Physical host interfaces | Quectel: USB and PCIe driver support |
| USB modes | QModem: QMI / GobiNet / ECM / MBIM / RNDIS / NCM |
| PCIe modes | QModem: QMI / GobiNet / MBIM |
| Radio | Quectel: NSA + SA, Sub-6 + mmWave |
| Data owner | unresolved until runtime |
| Control transport | derive from bound driver/endpoints and owner |

Official USB/PCIe support is a hardware/software capability statement. It does not prove that every listed QModem mode exists on every firmware/composition.

## Why it is separate from RM520N

- RM530N adds mmWave to the Sub-6 capability shared with the RM520N design family.
- The QModem band record includes mmWave NR bands 257/258/260/261.
- Shared RM5x0N AT documentation is useful **command-family evidence**, not permission to copy every RM520N firmware quirk.
- Current a field report `collect.sh` recognizes `RM530N*` full-build strings in its Quectel firmware diagnostic, but Zoo does not promote a cross-model firmware heuristic into verified RM530N behavior without a matching RM530N field report.

## Port policy

No fixed tty is recorded as truth. For USB, bind/protocol evidence must come from the modem's own sysfs attachment and current drivers. For PCIe/MHI, the control/data endpoints belong to that bus generation and must not be projected from USB names.

## Current evidence gap

At the reviewed pins there is no dedicated RM530N a field report profile and no promoted Telegram observation. Therefore:

- the page preserves QModem transport candidates and official vendor capabilities;
- model-specific destructive/persistent AT commands are not invented;
- ownership and exact AT endpoint semantics remain to be validated on real hardware/firmware.

## Provenance

- Quectel RM530N-GL official product page, reviewed 2026-10-01.
- QModem pin `c49654efc870f53712ee8e25bf181722eb1466d9`
  - `modem_support.usb.rm530n-gl`
  - `modem_support.pcie.rm530n-gl`
- a field report : `collect.sh` full-build diagnostic mentions `RM530N*`; this is implementation evidence only.
- normalized record: `reference/evidence/vendor-family-observations.json`
