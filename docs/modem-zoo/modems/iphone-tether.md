# iPhone USB tethering

> **Status:** `external-reference-unvalidated` · Apple, USB hotspot.

Host-managed usbmuxd/trust/DHCP; never use AT modem control or recovery.

## Identity and owner

- Model aliases: `iphone usb tether`.
- USB/PCI IDs are **candidates**, not physical identity or proof of supported transport.
- Resolve with sysfs topology, USB interface descriptors, driver binding, identity from authorised control owner and firmware.
- Any composition, APDU, reset or bootloader operations are reference-only until validated on a matching physical device.

## Related reference

- [USB composition and boot mode recipes](../COMPOSITION-AND-BOOT-MODES.md)
- another modem manager source map

<!-- upstream-another modem manager-v3.5.2:start -->
## Field incidents — a field report another modem manager

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### iphone-usb-tether

**Observation:** iPhone USB tether mode uses usbmuxd, trust authorization and hotspot DHCP; not a cellular AT modem.

- **Scope:** models=iPhone USB tether; release=v3.5.2
- **Type / confidence:** `host-network` / `external-field-report`
- **Regression contracts:** no AT, SIM, CFUN or modem USB reset on iPhone tether; trust flow is a user action
- **Safety:** Do not classify missing modem tty as hardware error.
- **Original source:** issue / release / pinned source

<!-- upstream-another modem manager-v3.5.2:end -->


<!-- another modem manager-delta-v3.5.5:start -->
## another modem manager–v3.5.5 — release delta (2026-10-16)

**Reference only; not bench-validated.** Read from the diff v3.5.2..v3.5.5; no upstream code, no runtime preset. All records: another modem manager delta.

- **`…-v353-android-tether-cdc-ncm-ecm`** (`upstream-code-reference`, upstream-code-reference): Up to v3.5.2 the project recognised a tethering phone only by an RNDIS function with no modem control port. v3.5.3 also accepts phones that tether over CDC-NCM (02/0d) or CDC-ECM (02/06), which recent Android builds use. Because the same two classes are the data function of many cellular modems, the class alone is not enough: an NCM/ECM device counts as a phone only when it has no tty and no cdc-wdm port, its USB vendor ID is on a phone-vendor list, the vendor is not one that also ships WWAN modules, and a network device is actually bound. Source: a field report/another modem manager (another modem manager/root/usr/share/another modem manager/lib.sh, msw/hilink.sh, listmodems.sh).
- **`…-v353-known-modem-never-phone`** (`upstream-code-reference`, upstream-code-reference): v3.5.3 adds a 'known modem' test that runs before any phone classification: the device's VID:PID has a profile in the project's database, or its vendor ID is one of 23 modem vendors. A known modem is never treated as a phone, and when a modem is set up on a USB port that still carries a phone mark from an earlier device (or an earlier misclassification), the mark is removed from both the device and the network section and a syslog line says so. Before this, a stale per-port 'tether' mark outlived the device and a real modem on that port was shown and handled as a phone. Source: a field report/another modem manager (another modem manager/root/usr/share/another modem manager/lib.sh, msw/setup.sh, msw/hilink.sh, netpri.sh).

<!-- another modem manager-delta-v3.5.5:end -->


<!-- another modem manager-delta-v3.5.16:start -->
## another modem manager–v3.5.16 — release delta (2026-10-21)

**Reference only; not bench-validated.** Read from the diff v3.5.5..v3.5.16 at `a22590c108cc`; no upstream code, no runtime preset. Full review: another modem manager delta.

- **`…-v3511-usb-gadget-router`** — `upstream-code-reference`, not-bench-validated: v3.5.11 accepts another OpenWrt router attached over USB as an uplink. USB vendor IDs 0525 (NetChip, the usual Linux gadget ID) and 1d6b (Linux Foundation) mark such a gadget router, not a phone; its telemetry is fetched from http://&lt;gateway&gt;/cgi-bin/another modem manager-tele with a 10 s cache, 60 s after an empty answer. Source: a field report/another modem manager @ `a22590c` (root/usr/share/another modem manager/lib.sh:302-305, root/usr/share/another modem manager/lib.sh:858-886).
  - The gadget check runs before the phone classification of the v3.5.3 tethering rule.

<!-- another modem manager-delta-v3.5.16:end -->
