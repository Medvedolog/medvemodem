# Fibocom FM650-CN

**Status:** multi-source-reference — **не MedveModem verified support**

FM650-CN — UNISOC-линия Fibocom. Это не родственник FM150/FM160/FM190W по Qualcomm-командному стеку и не FM350 по MediaTek-командному стеку.

## Revision boundary matters

Fibocom's own material documents:

- an FM650-CN generation on **UNISOC V510**;
- a later **V516** version described as pin-compatible with the V510 version.

Therefore even the same commercial model string is insufficient to infer identical firmware/AT behavior. Zoo keeps the platform revision explicit until control identity/firmware proves it.

## QModem transport evidence

QModem has one `fm650-cn` USB record:

- platform: `unisoc`
- modes: **ECM / MBIM / RNDIS / NCM**
- manufacturer ID: `2cb7`

There is no matching QModem PCIe model row at this pin. Zoo therefore does not manufacture a PCIe control-mode claim.

## Command policy

No reviewed evidence justifies inheriting:

- FM350 GTACT/GTUSBMODE;
- FM150/FM160 Qualcomm recipes;
- FM190W SDX75 behavior;
- any fixed tty mapping.

No promoted Telegram observation currently targets FM650-CN.

## Provenance

- Fibocom FM650/FG650 V510 mass-production material, reviewed 2026-10-01.
- Fibocom V516 announcement explicitly describing V510/V516 pin compatibility, reviewed 2026-10-01.
- QModem pin `c49654efc870f53712ee8e25bf181722eb1466d9`: `modem_support.usb.fm650-cn`.
- normalized record: `reference/evidence/vendor-family-observations.json`

<!-- external-another package-modem5g-2026-10-16:start -->
## another package / another package — external field facts (2026-10-16)

**Reference only; not bench-validated.** Checked against the pinned upstream files and the issue/PR pages; no upstream code, no runtime preset. All cases: another package / another package field notes.

- **`another package-gtusbmode-unisoc-read-map`** (`upstream-code-reference`, upstream-code-reference): For UNISOC-based Fibocom modules another package reads GTUSBMODE 34/35 as ECM, 36/37 as NCM, 38/39 as RNDIS and 40/41 as MBIM. The same 40/41 means RNDIS on the MediaTek FM350, so a vendor-wide GTUSBMODE table is wrong by construction. Source: another package/5G-Modem-Support luci-app-modem/root/usr/share/modem/fibocom.sh.

<!-- external-another package-modem5g-2026-10-16:end -->
