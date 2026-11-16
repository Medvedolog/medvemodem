# Sierra EM9190 / EM9191 / EM9291

> **Status:** `external-reference-unvalidated` · Sierra Wireless, Qualcomm 5G.

QMI/MBIM, AT!GSTATUS truncation and eSIM CCHO risk; restrict engineering commands by model.

## Identity and owner

- Model aliases: `em9190`, `em9191`, `em9291`.
- USB/PCI IDs are **candidates**, not physical identity or proof of supported transport.
- Resolve with sysfs topology, USB interface descriptors, driver binding, identity from authorised control owner and firmware.
- Any composition, APDU, reset or bootloader operations are reference-only until validated on a matching physical device.

## Related reference

- [USB composition and boot mode recipes](../COMPOSITION-AND-BOOT-MODES.md)
- archive source map

<!-- upstream-field-notes-v3.5.2:start -->
## Field incidents — archived field notes

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### sierra-ccho-sim-hang

**Observation:** A CCHO probe on EM9190 tty hung SIM until card reinsertion. Port enumeration is not a safe APDU transport probe.

- **Scope:** models=EM9190; usb_ids=1199:90d3
- **Type / confidence:** `esim-hazard` / `external-field-report`
- **Regression contracts:** no blind CCHO sweep across Sierra tty candidates; model allowlist and owner lease required for APDU
- **Safety:** No automatic eSIM-port probing on Sierra.
- **Original source:** issue / release / pinned source

<!-- upstream-field-notes-v3.5.2:end -->
