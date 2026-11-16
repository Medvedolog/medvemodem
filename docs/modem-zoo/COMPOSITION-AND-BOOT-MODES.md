# Modem Zoo — USB compositions / boot modes

Source: a field report. **Knowledge base only; no runtime script or automatic recipe is installed by this page.**

## Mandatory rules

1. Resolve **physical USB topology, descriptor/driver, model, firmware, control owner, attachment generation** before proposing composition changes. VID:PID and serial alone are insufficient (Foxconn/Compal collisions).
2. A working cdc-wdm/wwan/RNDIS/ECM interface is a **veto** against blind usbmode config 0. USB configuration 0 means unconfigured, not network composition zero.
3. Persistent NVM writes, AT composition commands and loader exits require informed operator confirmation, a recovery route, exclusive owner lease, snapshot, single-attempt tracking and bounded verification.
4. Expected detach/re-enumeration is not a modem death: preserve physical port identity but invalidate all previous tty/cdc-wdm paths; rediscover on the next attachment generation.
5. **EDL `05c6:9008` is never fastboot.** `413c:81e4` and `413c:81e6` are working compositions; only an actual `ff/42/03` undriven single USB interface (or explicit, verified loader ID) can indicate fastboot. Active flashing sessions are not to be interrupted.
6. `fastboot reboot` exits loader mode without firmware write. Never run `flash`, `erase`, `boot`, `update`, generic `new_id` or EDL commands automatically. Repeated fastboot after reboot may be a hardware signal (Huasifei WH3000 Pro / M.2 pin 67).

## Field recipes and resulting compositions

### `xmm-l860-legacy-gtusbmode` — fibocom-l850-l860

- **Source state:** Fibocom Intel XMM MBIM/other
- **Expected:** XMM/NCM, often 8087:095a
- **Firmware scope:** L850 / firmware accepting GTUSBMODE
- **Risk:** `COMPOSITION_MUTATION` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `AT+GTUSBMODE=0`
  - `AT+CFUN=15`

- **Post-condition:** mode-setting response must NOT be ERROR; remove/add on same physical USB path; confirm cdc_ncm and ttyACM role, proto=xmm
- **Warnings:** ~40 seconds disappearance can be expected; no fixed ttyACM numbering.
- **Sources:** pinned source

### `xmm-l860-nvm-fallback` — fibocom-l850-l860

- **Source state:** L860 fw 18600/18601 GTUSBMODE returns ERROR
- **Expected:** XMM/NCM
- **Firmware scope:** 18600/18601
- **Risk:** `PERSISTENT_COMPOSITION_WRITE` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `at@nvm:cal_usbmode.num=0`
  - `at@store_nvm(cal_usbmode)`
  - `AT+CFUN=15`

- **Post-condition:** NVM read-back; USB remove/add after reset, expect 8087:095a; network protocol and data channel verified
- **Warnings:** The `at@store_nvm` step writes persistent parameters. No automatic action.; If no store, choice is lost on reboot.
- **Sources:** pinned source

### `foxconn-t99-qmi-mbim` — foxconn-t99w175

- **Source state:** T99W175 QMI 05c6:9025/1e2d:00b7 or MBIM 05c6:90d5/1e2d:00b3
- **Expected:** firmware/personality-dependent
- **Firmware scope:** variant-specific
- **Risk:** `PERSISTENT_COMPOSITION_WRITE` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `AT^USBSWITCH=<firmware-approved mode> (reference only)`
  - `modem re-enumeration may follow`

- **Post-condition:** CGMM/firmware/CUSTOMER identity before action; post-switch control owner exactly one and correct driver; recheck data channel on newly enumerated attachment
- **Warnings:** Do not infer a valid numeric command from the VID/PID alone.; Compal RXM-G1 shares 05c6 IDs.
- **Sources:** pinned source

### `quectel-usbnet` — quectel-rm520n

- **Source state:** Quectel USB composition driver-dependent
- **Expected:** QMI/MBIM/ECM/RNDIS variant
- **Firmware scope:** variant-specific
- **Risk:** `COMPOSITION_MUTATION` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `AT+QCFG="usbnet",<mode> (reference-only family command)`
  - `restart/re-enumeration per firmware manual`

- **Post-condition:** read back QCFG with permitted owner; confirm expected descriptors and data driver
- **Warnings:** Mode number mappings differ by Quectel model; no universal value table.; Do not combine with QPCIE/ADB changes in automated setup.
- **Sources:** pinned source

### `huawei-hilink-debug` — huawei-hilink

- **Source state:** E3372 HiLink DHCP/router and web API
- **Expected:** HiLink with temporary AT debug interfaces
- **Firmware scope:** variant-specific
- **Risk:** `COMPOSITION_MUTATION` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `Vendor HTTP debug endpoint (model/firmware-specific)`

- **Post-condition:** DHCP/web API still functional; debug AT tty recognized within same physical USB device
- **Warnings:** Debug disappears on reboot for observed Huawei E3372.; Do not use AT^SYSCFGEX for band setting on HiLink.
- **Sources:** pinned source

### `zte-mf6-web-debug` — zte-mf-family

- **Source state:** Tenda MF6 19d2:1557 web/API only
- **Expected:** web-managed router with AT debug enabled
- **Firmware scope:** variant-specific
- **Risk:** `COMPOSITION_MUTATION` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `Authorized vendor web API enable debug (device-specific)`

- **Post-condition:** model identification and web auth; debug mode persistence across reboot on reported MF6
- **Warnings:** Unlike Huawei E3372, MF6 debug persists.
- **Sources:** pinned source

### `zte-cdrom-start-eject` — zte-mf-family

- **Source state:** 19d2:0542 / 0548 / 0564 USB mass storage
- **Expected:** working modem composition (possibly 19d2:0565)
- **Firmware scope:** variant-specific
- **Risk:** `COMPOSITION_MUTATION` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `usbmode -s -c usb-mode-extra.json (StandardEject)`

- **Post-condition:** mass-storage class confirmed pre-eject; verify USB ID/driver after detach/re-enumeration
- **Warnings:** Never eject a modem already in active data composition.
- **Sources:** pinned source

### `dw5821e-usb-config-restoration` — foxconn-t77w968

- **Source state:** 413c:81d7/81e0 config 0 with no cdc-wdm/wwan
- **Expected:** last known good USB bConfigurationValue
- **Firmware scope:** variant-specific
- **Risk:** `DRIVER_COMPOSITION_REPAIR` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `remove verified harmful config:0 rule, backup first`
  - `only after confirmed data-channel loss: restore last-known-good bConfigurationValue`

- **Post-condition:** never mutate healthy modem; new cdc-wdm/wwan/driver and fresh attachment generation
- **Warnings:** Never treat config=0 as a selectable working mode.; Do not guess bNumConfigurations last as universally correct.
- **Sources:** pinned source, pinned source

### `foxconn-fastboot-exit` — foxconn-t77w968

- **Source state:** DW5821e true fastboot ff/42/03 or 413c:81d6 / 81e1
- **Expected:** normal USB modem, after one reboot
- **Firmware scope:** variant-specific
- **Risk:** `BOOTLOADER_EXIT_ONLY` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `fastboot -s <unique serial> reboot`
  - `fallback upstream: usb-serial generic new_id on ff interface and send raw ASCII reboot (reference-only)`

- **Post-condition:** not EDL 05c6:9008; no active flash/edl tool; one matching device and new non-bootloader USB enumeration; 60 s between attempts, max 3 per physical slot per boot
- **Warnings:** 413c:81e4 and 413c:81e6 are WORKING DW5829e compositions, not fastboot.; Never flash/erase/boot/update or issue EDL commands.; Pin 67 short to ground requires hardware fix; reboot alone may loop.
- **Sources:** pinned source

### `fm350-usb-gtusbmode-40-41` — fibocom-fm350

- **Source state:** USB FM350/RW350, MediaTek control, observed 0e8d:7126/7127
- **Expected:** USB RNDIS (not PCIe MBIM)
- **Firmware scope:** 0e8d:7126/7127, model/firmware-specific GTUSBMODE 40/41
- **Risk:** `COMPOSITION_MUTATION` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `AT+GTUSBMODE? (query current USB composition)`
  - `AT+GTUSBMODE=<40-or-41 after verifying actual firmware map> (DO NOT AUTO-EXECUTE)`

- **Post-condition:** new USB descriptor/bound driver after any re-enumeration; RNDIS data plane and AT control-port ownership restored
- **Warnings:** Upstream 7127 profile states +GTUSBMODE: 41; not enough evidence for a universal numeric mode switch table.; Do not recommend USB MBIM on this family solely because a PCIe FM350 uses MBIM.
- **Sources:** pinned source

### `huawei-at-composition-hazards` — huawei-hilink

- **Source state:** E3372 web-debug or modem-mode with tty interfaces
- **Expected:** no automatic composition change
- **Firmware scope:** Huawei E3372 classical and HiLink branches differ
- **Risk:** `PERSISTENT_COMPOSITION_WRITE` · `manual-confirmation` · `upstream-recipe-external-unvalidated`
- **Observed upstream sequence (not executable instructions):**

  - `AT^SETPORT and AT^U2DIAG are composition writes: expert hazard ONLY`
  - `AT^SYSCFGEX on HiLink is not a safe band-configuration shortcut`

- **Post-condition:** any necessary switching recipe must be specific to firmware; control and data USB interfaces must remain recoverable
- **Warnings:** No universal SETPORT/U2DIAG map across Huawei submodels; changing it can lose data/AT ports.
- **Sources:** pinned source

## Test boundaries

- the field regression tests pins external incidents/recipes and forbids automatic promotion.
- `tests/test_discovery.py`, `tests/test_at.py`, `tests/test_recovery.py` exercise the actual C discovery / AT hazard / recovery gates, not physical composition switches.
- Hardware-only experiments, modem firmware flashing and real fastboot exits are **not simulated**. Separate bench validation is required before enabling any operation.
