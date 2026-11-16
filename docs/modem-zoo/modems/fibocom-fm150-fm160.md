# Fibocom FM150 / FM160 Qualcomm 5G family

> **Status:** `reference-only` · deliberately conservative.

FM150/FM160 is separated from FM350 and L850/L860 because it is a **Qualcomm** Fibocom generation.

## Transport evidence

QModem currently records FM160 variants as:

- PCIe: **QMI + MBIM**;
- USB: **QMI + GobiNet + ECM + MBIM + RNDIS + NCM**.

a field report's eSIM capability map associates `2cb7:0104/0105` with FM150/FM160.

## What Zoo does not claim yet

Current source evidence is much thinner for model-specific FM160 AT behavior than for FM350 or L860. Therefore this page intentionally does **not** copy:

- FM350 `GTACT` semantics;
- FM350 USB `GTUSBMODE 40/41` behavior;
- L850/L860 `XACT`;
- L860 `cal_usbmode` NVM recipes.

QModem's generic Fibocom wrapper corpus is still available in [Extended AT reference](../AT-REFERENCE.md), but those commands remain **candidates** until tied to an FM150/FM160 firmware or a verified field report.

## Known useful facts

- vendor: Fibocom;
- platform: Qualcomm;
- eUICC-capable IDs are known from a field report's capability map;
- transport support is rich in QModem;
- model-specific command promotion needs more evidence.

This is preferable to manufacturing a “complete” page by mixing three unrelated Fibocom generations.

<!-- external-another package-modem5g-2026-10-16:start -->
## another package / another package — external field facts (2026-10-16)

**Reference only; not bench-validated.** Checked against the pinned upstream files and the issue/PR pages; no upstream code, no runtime preset. All cases: another package / another package field notes.

- **`another package-gtusbmode-qualcomm-read-map`** (`upstream-code-reference`, upstream-code-reference): For Qualcomm-based Fibocom modules another package reads GTUSBMODE 17/31/32/34 as QMI, 18/23/33/35 as ECM, 29/30 as MBIM and 24 as RNDIS; its setter writes 32 for QMI, 30 for MBIM, 24 for RNDIS and 18 for both ECM and NCM. The table is internally inconsistent: 18 is listed for NCM too, but the ECM branch matches first, so NCM can never be read back. Source: another package/5G-Modem-Support luci-app-modem/root/usr/share/modem/fibocom.sh.
- **`fibocom-qualcomm-qmi-wwan-f-scope-issue50`** (`field-reported`, external-maintainer-statement): Per the another package maintainer, the Fibocom-specific kmod-qmi_wwan_f driver is needed only by Fibocom modules built on Qualcomm chips (the FM150/FM160 generation), not by the MediaTek FM350. Source: another package/5G-Modem-Support issue #50, maintainer reply 2026-06-13.
- **`fm150-ae-pcie-quectel-cm-138-regression-issue49`** (`field-reported`, external-field-report): Over PCIe, quectel-cm V1.3.8 could not bring up a QMI data call on an FM150-AE-00; rolling back to the version the reporter calls '1.36' dialed normally. One report, no replies, no OS or firmware given. Source: another package/5G-Modem-Support issue #49 (open).

<!-- external-another package-modem5g-2026-10-16:end -->
