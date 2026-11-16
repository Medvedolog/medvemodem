# Compal RXM-G1 / SG500M2-X / VOS5G

> **Status:** `external-reference-unvalidated` · Compal, Qualcomm 5G.

Shared Qualcomm USB IDs with Foxconn, QMI CID exhaustion, USB power requirements; no default Foxconn AT recipes.

## Identity and owner

- Model aliases: `rxm-g1`, `sg500m2-x`, `vos5g`.
- USB/PCI IDs are **candidates**, not physical identity or proof of supported transport.
- Resolve with sysfs topology, USB interface descriptors, driver binding, identity from authorised control owner and firmware.
- Any composition, APDU, reset or bootloader operations are reference-only until validated on a matching physical device.

## Related reference

- [USB composition and boot mode recipes](../COMPOSITION-AND-BOOT-MODES.md)
- another modem manager source map

<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### compal-serial-collision

**Observation:** Two Compal physical devices observed with identical serial 2e6172c9; ID-only and serial-only matching can merge modems.

- **Scope:** models=RXM-G1, SG500M2-X, VOS 5G; serial=2e6172c9; usb_ids=05c6:90d5, 05c6:9025, 05c6:90d6, 05c6:9091
- **Type / confidence:** `device-identity` / `external-field-report`
- **Regression contracts:** duplicate serial requires distinct port/attachment IDs; vendor AT dialect must not derive from shared PID
- **Safety:** A second modem must never inherit the first modem control interface.
- **Original source:** issue / release / pinned source

### compal-qmi-clients-and-power

**Observation:** QMI NAS CID exhaustion observed around six client IDs; separate incident where underpowered hub gave tty but no MBIM data function.

- **Scope:** models=RXM-G1, VOS 5G; backend=QMI/MM
- **Type / confidence:** `control-backend` / `external-field-report`
- **Regression contracts:** CID exhaustion is not automatically physical USB failure; lack of data driver with -71/over-current should stop reset ladder
- **Safety:** qmi-proxy and owner arbitration before any reset; examine hub power budget.
- **Original source:** issue / release / pinned source

<!-- upstream-another modem manager-v3.5.2:end -->
