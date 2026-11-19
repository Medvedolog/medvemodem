# Foxconn T99W640 / Dell DW5934e (Qualcomm Snapdragon X72, SDX72)

> **Status:** `experimental` · profile `foxconn-t99w640-mbim` (USB only, ModemManager only) · no bench of ours.
> The owner, 2026-11-06: «внеси экспериментально USB композицию в поддержку МедвеМодем с учётом проблем и отвалов»;
> earlier the same day: «пока не делаем, он сырой, только PCI-e, отвалы».

## Identity

| Mode / endpoint | VID:PID | What it is |
|---|---|---|
| PCIe MHI, DW5934e with eSIM | `105b:e11d` | upstream `mhi-pci-generic` since Linux 6.11 (commit `bf30a75e6e00`) |
| PCIe MHI, DW5934e without eSIM | `105b:e11e` | |
| PCIe MHI, Foxconn T99W640 | `105b:e118` | upstream since Linux 6.11 |
| PCIe base / engineering | `17cb:0309` | Qualcomm SDX72 base ID |
| **USB, MBIM composition** | **`05c6:90d5`** | `cdc_mbim` on interface 02 (Windows: `USB\VID_05C6&PID_90D5&MI_02`, inbox MBIM) |
| Qualcomm EDL | `05c6:9008` | Sahara/Firehose; never touched by MedveModem |
| Dell composite EDL | `0489:e131` | not a 9008: stop |

`05c6:90d5` is also the T99W175 in MBIM and the Compal RXM-G1. The profile takes it only by ModemManager's model
(`T99W640`, `DW5934`, `DP25-42843`, `X72`); with another model the T99W175 or Compal profile applies.

The module is made for a PCIe M.2 slot; on a router it reaches USB only through an M.2-to-USB adapter board
(4PDA 1125602, the first post: «не работает через USB из коробки»). How a given unit is put into its USB
composition is not confirmed in our sources (HANDOFF 82 notes the `AT+` prefix of this family where the T99W175 uses
`AT^USBSWITCH`): MedveModem does not switch it.

## What the 4PDA topic found (1125602, a print copy of 05.11.26 from the owner)

- **USB works only after a PCIe start.** Kilm, 04.11 and 05.11.26: the module must power up with both buses connected
  (PCIe needed at least until UEFI finishes); once the USB composition appears the PCIe bus has to be cut *physically*
  (the PCIe adapter pulled out). Then it works on USB stably and keeps the network. With PCIe left connected, even with
  every driver removed, in Windows, Ubuntu 22 or OpenWrt 25, it falls to «no network» and stays there until a reboot;
  cutting PCIe in software did not help. He shelved it: in Russian networks worse than the T99W175 and T99W373, about the
  T77W968 in LTE.
- **The USB composition is 90D5: DIAG + ADB + MBIM** (kniga043, 20.10.26: the USB controller is alive, dwc3 initialised,
  `at_usb0`/`at_usb1` created inside the module). No one reported a tty on the host.
- **Identity over MBIM** (kniga043, 02.10.26, `qmicli --device-open-mbim`): manufacturer `DELL`, model `DP25-42843-47`,
  revision `FDE2.F0.0.0.1.2.GC.001`. ModemManager may name it by the firmware's MBIM identity instead («Generic Mobile
  Broadband Adapter», as the T99W175 in MBIM): then the T99W175's MBIM profile takes it (its recovery and binding are
  on the operator's request only). A tester's `mmcli -m any` settles which.
- **The minute carousel** (starimage, 08.10.26, from the toolkit's report): registration completes, 17–20 s later the
  modem deregisters itself, a cycle of about 45 s, independent of antennas, OS, operator and APN. Kilm saw the same on
  PCIe; on USB after the PCIe cut he did not. kniga043 (09.10.26): editing the module's EFS
  `/nv/item_files/therm_monitor/config.ini` (not signed) to drop the action on the phantom temperature; later (18.10)
  he called the thermal conclusions partly wrong. MedveModem never writes the module's EFS.
- **FCC.** Unlocked by kniga043's own key generator (not published); Kilm's unit started in CFUN 0 without a lock and
  took CFUN 1 by AT. After the unlock: broken power saving and the reject deadlock (below).
- **Customer IDs** (Kilm, 15.09.26): 0 Generic/Foxconn, 32 Foxconn ODM, 33 Dell, 34 Thales, 35 HP, 36 HP alt; changing
  it did not help.
- **Flashing** goes through EDL by pin 3 at 3.3 V (edl-sdx7x); a wrong 512 MB image bricked one unit. Not ours to touch.

## Known faults (what the router cannot fix)

1. **False PA temperature → the modem's own thermal protection (F32).** At about 34 °C ambient MPSS reports
   `sdr0_pa` 131–137 °C (sdr0 and mmw_ific0 33–34 °C). CFCM acts on `PA_FR1_SDR0`, the modem deregisters, its mode
   preference is cleared and it goes to power save: the host sees it powered and enumerated, registration idle, no
   signal, no data session. It survives cold boots and every Dell firmware (A05, A09, A10 checked); the cause is three
   wrong thermistor associations in the signed RF profile `1183_0_0.mbn` (dw5934e-5g-toolkit, research report
   2026-08-22, an OEM fix request). No reset from the router cures it.
2. **Deadlock after a foreign base station's reject (F33).** The developer of an experimental FCC unlock describes the
   firmware locking up after a reject; a ping every 4-5 s after it registers holds it off, which he calls
   unsatisfactory (4PDA 1125602, kniga043 02.10.26). MedveModem's reach checks send traffic through each modem, but
   not that often: no model-specific keepalive is added.
3. **FCC lock** (Dell builds). The toolkit persists the unlock inside the module; MedveModem offers no FCC hook for
   `05c6:90d5` (one hook would cover the T99W175 and Compal sharing the ID).
4. **A runtime driver switch** (`mhi_q` → `mhi-pci-generic`) gave «Device died» in the toolkit's notes: the profile
   allows no driver rebind and no USB re-authorize.
5. **eSIM slot first.** The eSIM build may come up on its eSIM slot; the toolkit's AutoNet switches to the physical
   slot at most once per boot when the primary is empty and exactly one other slot holds a SIM. MedveModem leaves the
   slot to the operator (the modem page's SIM slot preset).
6. **Power.** An X72 with an adapter draws more than a USB 2 port gives; drops on a weak port are expected. Use a USB 3
   port or a powered adapter.

## New: SDX72 firmware AT / factory diagnostics research (18 Nov 2026)

The owner supplied a **35-page T99W640/DW5934e AT/DIAG technical study v2.4**. [The detailed firmware notes](foxconn-t99w640-at-diag.md) preserve the AP/CP split, a curated AT read-only inventory, the LTE and NR lock formats, all **34 factory ftm subcommands**, the QCDM packet inventory, selected NV/EFS identifiers and risks.

**Provenance boundary:** read-only commands were tested on ENG firmware; Retail was cross-checked with static binary analysis. Destructive or state-changing forms were documented from reverse engineering **without physical write tests**. MedveModem does **not** interpret this as permission to access a live managed device or implement an automatic FCC, DIAG, FTM, NV/EFS or Cell Lock write. No new driver binding or session-control action is implied.

Operationally important discoveries:

- **Vendor interface:** the PCIe host MBIM node (typically `/dev/wwan0mbim0`) can carry proprietary AT_OVER_MBIM via QMI; plain PCIe DUN `/dev/wwan0at0` may be unusable on Retail. USB may expose ordinary AT in an appropriate composition, but the current `05c6:90d5` profile cannot assume a host tty is present.
- **LTE locking:** exact on-chip syntax is `AT+LTE_LOCK=<PCI>,<EARFCN>`, **PCI first**, up to eight pairs. Only `AT+LTE_LOCK?` is a read; bare `AT+LTE_LOCK` clears the lock. NR locking has separate `<band>,<scs>,<nr_arfcn>,<count>,<pci...>` fields. Neither write path is enabled.
- **CA and modes:** `AT+CA_INFO?` reports PCC/SCC; `AT+CA_ENABLE=0` **enables** CA, nonzero values disable. SDX72 `SLMODE` accepts a persist/temporary flag plus enumerated mode 0..7, **not SDX55's RAT-bitmask form**.
- **Sensors and antenna levels:** `AT+TEMP?` reads TSENS/PA/Skin/Ambient, `AT$QCTEMP` reads CP temperature, `AT$QCRSRP` / `AT$QCRSRQ` give per-Rx signal levels, and `AT+RSSI=LTE,ALL` is the parameterized antenna read. These are reference-only under MM ownership.
- **Forbidden even for reading:** on ENG `AT+VZWRSRP?` and `AT+VZWRSRQ?` caused an immediate **MHI SSR crash**. The Retail handler looked similar statically; never auto-query them. Bare `AT+ODIS` resets ODIS settings and bare `AT+ULCA` alters EFS; never infer read safety from a command name.
- **FCC:** `AT+FCC_LOCK=0` / `ftm unlock_fcc` do **not** replace authenticated host QMI FCC unlock. The factory `ftm` dispatcher is not `AT+FTM` and is not implemented here.
- **DIAG:** QCDM is a separate HDLC-framed port; PCIe transport may need AP-side activation. DIAG reads and all NV/EFS, GPIO, USB PID, EDL and factory operations are **not** exposed by the profile.

**Status remains experimental, USB MBIM and ModemManager-managed.** This research improves support knowledge, not the actual AT-over-MBIM driver or physical compatibility; field-reported thermal cycling and the PCIe/USB coexistence issue remain unresolved.

## MedveModem's behaviour (profile `foxconn-t99w640-mbim`)

- ModemManager owns it (`mm_ports: control`), the session through netifd's ModemManager proto, MBIM.
- No direct AT console while MM owns it, no active radio dialect or vendor AT-over-MBIM bridge, no `new_id`, no rebind. Firmware command metadata is documented only.
- Recovery on the operator's request only (`field-reported`): reconnect, radio off/on and a reset through
  ModemManager; no USB re-authorize, no port power cycle; 90 s to settle (the X72 boots slowly).
- What to send for a bench profile: `lsusb -t`, `cat /sys/kernel/debug/usb/devices` for the modem, `mmcli -m any`,
  `ls /dev/cdc-wdm* /dev/wwan*`, the modem's report from its page.

## Sources

- github.com/Angrtoy/dw5934e-5g-toolkit (studied 2026-11-06, behaviour only): README identity matrix, Windows
  installer (`05c6:90d5` MI_02), OpenWrt deployment notes, research report on the false PA temperature, coldboot notes.
- 4PDA topic 1125602: the print copy the owner sent on 2026-11-06 (posts to 05.11.26); the live site was not
  reachable from the session.
