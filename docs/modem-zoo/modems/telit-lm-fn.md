# Telit LM960 / LN960 / FN990

> **Status:** `external-reference-unvalidated` · Telit, Qualcomm 4G/5G.

CA/status via Telit AT dialect; USSD may require disruptive RAT switch.

## Identity and owner

- Model aliases: `lm960`, `ln960`, `fn990`.
- USB/PCI IDs are **candidates**, not physical identity or proof of supported transport.
- Resolve with sysfs topology, USB interface descriptors, driver binding, identity from authorised control owner and firmware.
- Any composition, APDU, reset or bootloader operations are reference-only until validated on a matching physical device.

## Related reference

- [USB composition and boot mode recipes](../COMPOSITION-AND-BOOT-MODES.md)
- another modem manager source map

<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### telit-lm960-metrics

**Observation:** For LM960 QMI channel owned by dialer, signal/CA may come from AT (#CAINFOEXT/#BND); USSD may require switching to 3G, disrupting service.

- **Scope:** models=LM960, FN990, LN960
- **Type / confidence:** `at-owner` / `external-field-report`
- **Regression contracts:** only use AT telemetry through the control owner; USSD must not cause implicit RAT switch
- **Safety:** Telit LN960 personality does not prove Foxconn T77 vendor dialect.
- **Original source:** issue / release / pinned source

<!-- upstream-another modem manager-v3.5.2:end -->


<!-- another modem manager-delta-v3.5.16:start -->
## another modem manager–v3.5.16 — release delta (2026-10-21)

**Reference only; not bench-validated.** Read from the diff v3.5.5..v3.5.16 at `a22590c108cc`; no upstream code, no runtime preset. Full review: another modem manager delta.

- **`…-v3514-shared-sms-port`** — `upstream-code-reference`, not-bench-validated: Their SMS list 'sometimes loads, sometimes not' because the metrics poll and the SMS tool open the same tty independently; measured on a Telit LM960A18: 2 of 5 back-to-back reads empty, 8 of 8 with pauses. Their answer is a separate SMS port: the first answering port other than the metrics port and other than the dial port (on L860-GL under their xmm proto SMS/USSD/AT on the dial tty broke the session); under ModemManager no separate port is picked. v3.5.14 adds sms.shared_at_port=1 to keep SMS on the metrics port for modems whose second port answers but misbehaves. Source: a field report/another modem manager @ `a22590c` (root/usr/share/another modem manager/ensureports.sh:25-73, root/usr/share/another modem manager/ensureports.sh:105-107).
  - The Telit LM960 has three AT ports in their notes (ttyUSB9/10/11).

<!-- another modem manager-delta-v3.5.16:end -->
