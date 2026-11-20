# MikroTik R11e-LTE

> **Status:** `external-reference-unvalidated` · MikroTik, LTE.

AT on ttyACM0 can reboot device; never probe without verified profile.

## Identity and owner

- Model aliases: `r11e-lte`.
- USB/PCI IDs are **candidates**, not physical identity or proof of supported transport.
- Resolve with sysfs topology, USB interface descriptors, driver binding, identity from authorised control owner and firmware.
- Any composition, APDU, reset or bootloader operations are reference-only until validated on a matching physical device.

## Related reference

- [USB composition and boot mode recipes](../COMPOSITION-AND-BOOT-MODES.md)
- another modem manager source map

<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### mikrotik-r11e-at-reset

**Observation:** MikroTik R11e-LTE may reset on ANY AT to ttyACM0; a field report unbinds this port via noatports.sh.

- **Scope:** model=R11e-LTE; usb_ids=2cd2:0001
- **Type / confidence:** `never-at` / `external-field-report`
- **Regression contracts:** zero AT probes on ttyACM0, including ATI; keep diagnostics independent of forbidden tty
- **Safety:** Never use read-only AT as a discovery test on this interface.
- **Original source:** issue / release / pinned source

<!-- upstream-another modem manager-v3.5.2:end -->
