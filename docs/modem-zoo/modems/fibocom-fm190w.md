# Fibocom FM190W-GL

**Status:** multi-source-reference — **не MedveModem verified support**

FM190W-GL — отдельное Snapdragon X75 / SDX75 поколение Fibocom. Его нельзя смешивать ни с FM150/FM160 только потому, что там тоже Qualcomm, ни тем более с MediaTek FM350.

## Generation boundary

Fibocom documents FM190W-GL as an SA/NSA 5G module based on Snapdragon X75 with Sub-6 + mmWave and physical USB/PCIe interfaces. This generation boundary is the most important fact for command hygiene.

## QModem transport evidence

| Model | Bus | Modes | Status |
|---|---|---|---|
| fm190w-gl | USB | QMI, GobiNet, ECM, MBIM, RNDIS, NCM | reference-unvalidated |
| fm190w-gl | PCIe | QMI | reference-unvalidated |

The PCIe QMI row is QModem evidence, not a MedveModem hardware test. A physical PCIe interface in vendor specifications also does not, by itself, define the runtime control owner.

## What Zoo deliberately does not inherit

- **FM350:** no automatic GTACT/GTUSBMODE inheritance; that page is MediaTek-specific.
- **FM150/FM160:** no automatic model-specific Qualcomm Fibocom recipes without FM190W evidence.
- **fixed tty roles:** none.
- **Telegram quirks:** none promoted for FM190W in the current 2026-10-01 curated corpus.

This is intentionally conservative: a thin correct page is better than a rich page built from the wrong Fibocom generation.

## Provenance

- Fibocom official FM190W-GL product documentation, reviewed 2026-10-01: Snapdragon X75/SDX75, SA/NSA, Sub-6/mmWave, USB/PCIe interfaces.
- QModem pin `c49654efc870f53712ee8e25bf181722eb1466d9`
  - `modem_support.usb.fm190w-gl`
  - `modem_support.pcie.fm190w-gl`
- normalized record: `reference/evidence/vendor-family-observations.json`
