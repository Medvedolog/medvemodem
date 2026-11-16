# Foxconn T99W373 / Thales MV32-W (Qualcomm Snapdragon X62, SDX62)

> **Status:** `experimental` · profiles `foxconn-t99w373-0489-e0f0`, `thales-mv32w-1e2d-00f1` … `00f4` · no bench of ours.
> The owner, 2026-11-06: «добавь экспериментальный, пусть хотя бы инет поднимается».

## Identity

| VID:PID | What it is | Source |
|---|---|---|
| `0489:e0f0` | Foxconn T99W373, USB | archived modem utility field notes, our hardware inventory (imported) |
| `1e2d:00f1` | Thales MV32-W-A, MBIM | QModem modem_support.json |
| `1e2d:00f2` | Thales MV32-W-B, MBIM or RMNET | QModem modem_support.json |
| `1e2d:00f3`, `1e2d:00f4` | MV32-W, composition not documented to us | archived modem utility field notes |
| PCIe `105b:e118`/`e11d`/`e11e` | PCIe variants in archived modem utility 3.2.x (shared with the T99W640's IDs) | not supported |

archived modem utility's notes also put T99W373 under `05c6:90d5` with ModemManager (a label only): there the T99W175's
MBIM profile takes it.

## What MedveModem does

- The internet through ModemManager over MBIM: `cdc_mbim` binds an MBIM function by its USB class, so no ID table is
  needed for the data path. An RMNET composition needs `qmi_wwan` to know the ID; the kernel's table may not, and the
  profile does not bind it (a generic profile by driver applies if it binds anyway).
- No AT console and no radio dialect: the SDX62's AT set (`AT^SWITCH_SLOT` for the SIM slot among others) is not
  confirmed as the T99W175's `AT^SLMODE`/`AT^BAND_PREF`.
- `0489:e0f0`: the Foxconn FCC unlock hook (`foxconn-qmi`) is offered for the operator to turn on when ModemManager
  keeps the radio off; never by itself.
- Recovery on the operator's request only: reconnect, radio off/on, a reset through ModemManager; 60 s to settle.

## Field notes

- Kilm (4PDA 1125602, 05.11.26): in Russian networks better than the T99W640 and the T99W175.

## For a bench profile

`lsusb -t`, `cat /sys/kernel/debug/usb/devices` (the modem's block), `mmcli -m any`, the modem's report from its page.
