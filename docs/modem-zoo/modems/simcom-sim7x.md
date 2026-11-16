# SIMCom SIM7100 / SIM7600 / SIM79xx

> **Status:** `external-reference-unvalidated` · SIMCom, LTE/5G variants.

CPSI parser RAT-specific; USB IDs overlap SIMCom families, AT+CGMM required.

## Identity and owner

- Model aliases: `sim7100`, `sim7600`, `sim7906`, `sim7912`.
- USB/PCI IDs are **candidates**, not physical identity or proof of supported transport.
- Resolve with sysfs topology, USB interface descriptors, driver binding, identity from authorised control owner and firmware.
- Any composition, APDU, reset or bootloader operations are reference-only until validated on a matching physical device.

## Related reference

- [USB composition and boot mode recipes](../COMPOSITION-AND-BOOT-MODES.md)
- archive source map

<!-- upstream-field-notes-v3.5.2:start -->
## Field incidents — archived field notes

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### simcom-cpsi-mixed-format

**Observation:** SIM7100 GSM CPSI layout differs from LTE; RSSI -86 must not be displayed as LTE band B-86.

- **Scope:** models=SIM7100, SIM7600, SIM7906, SIM7912; usb_ids=1e0e:9001, 1e0e:9002, 1e0e:9004
- **Type / confidence:** `telemetry-parser` / `external-field-report`
- **Regression contracts:** parse RAT before choosing CPSI field offsets; never parse a negative RSSI value as a band
- **Safety:** Similar PID does not prove same SIMCom model; AT+CGMM after safe lease.
- **Original source:** issue / release / pinned source

<!-- upstream-field-notes-v3.5.2:end -->
