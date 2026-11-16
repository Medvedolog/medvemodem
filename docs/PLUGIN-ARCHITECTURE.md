# Plugin and device-profile architecture

## Core principle

MedveModem must keep the core small.

The core owns only generic orchestration:

- hotplug event intake
- device identity and lifecycle
- profile selection
- port/protocol resolution
- transactional UCI/network/firewall changes
- state publication
- recovery orchestration
- plugin loading

Vendor/model-specific behavior must live outside the core.

## Device profiles

Profiles should be declarative whenever possible.

Suggested layout:

```
profiles/
  usb/
    2c7c_0800.json
    2c7c_0125.json
    1bc7_1910.json
  pci/
    105b_e0b0.json
  families/
    quectel.json
    fibocom.json
    foxconn.json
    xmm.json
    hilink.json
```

A device profile may describe:

- USB VID/PID and optional revision/product matching
- PCI IDs
- vendor/family/model
- expected interfaces
- AT/data/SMS/GNSS ports
- protocol priority
- QMI/MBIM/NCM/ECM/RNDIS/HiLink/XMM hints
- required kernel module/new_id actions
- reset/rebind policy
- ModemManager policy
- AT command preset
- metric parser plugin
- band-control plugin
- cell-lock plugin
- health/recovery policy
- quirks and capability flags

Profiles should support inheritance/overlay:

```
generic
  -> vendor
     -> family
        -> exact device
```

This avoids copying hundreds of common fields.

## Plugins

Use plugins only for behavior that cannot be expressed safely as data.

Suggested interfaces:

```
plugins/
  detect/
  protocol/
  ports/
  metrics/
  radio/
  recovery/
  hilink/
  vendor/
```

Examples:

- `detect/fibocom`: special model identification
- `ports/xmm`: unusual AT/data port mapping
- `metrics/quectel`: parse QENG/QCAINFO
- `radio/fibocom`: band and cell lock
- `hilink/huawei`: HTTP API backend
- `recovery/usb-rebind`: authorized/bind/unbind logic
- `protocol/qmi-raw`: nonstandard data-session setup

Plugins should expose narrow operations rather than a global shell environment.

## Stable plugin ABI

Initial logical API:

```
probe(device) -> score/capabilities
resolve_ports(device) -> ports
resolve_protocol(device) -> protocol candidate
collect_metrics(ctx) -> normalized metrics
set_radio_mode(ctx, request)
set_bands(ctx, request)
lock_cell(ctx, request)
unlock_cell(ctx)
recover(ctx, reason)
shutdown(ctx)
```

Not every plugin implements every method.

The core selects plugins through profile metadata.

## Normalized state

All backends should map into one internal state schema, for example:

```
device:
  id
  bus
  vendor
  model
  firmware
  imei
  usb_vid
  usb_pid

sim:
  state
  iccid
  imsi
  operator_mccmnc

radio:
  rat
  mode
  rssi
  rsrp
  rsrq
  sinr
  band
  nr_band
  cell_id
  pci
  earfcn
  nrarfcn
  temperature

carriers:
  - rat
    band
    pci
    channel
    rsrp

ports:
  at
  sms
  data
  control

network:
  proto
  interface
  device
  ipv4
  ipv6
```

LuCI, CLI and telemetry consume this schema instead of querying modems independently.

## Detection scoring

Avoid first-match-wins where possible.

A profile/plugin returns a confidence score based on evidence:

- exact VID/PID
- product/manufacturer strings
- USB interface classes
- AT identity
- driver binding
- known firmware/model strings

The highest compatible profile wins.

This is important for devices sharing Qualcomm/Fibocom/Foxconn IDs or changing USB composition after reset.

## Event model

Core should be event-driven:

- USB add/remove
- tty add/remove
- usbmisc add/remove
- netdev add/remove
- ModemManager modem add/remove
- interface up/down
- recovery timeout

Do not perform all discovery synchronously in a single hotplug script.

Hotplug should enqueue a device event; the manager resolves the stable device state after interfaces settle.

## Modem ownership and control-plane priority

The primary job is not to expose every possible modem feature. It is to make the modem come up correctly, stay up, and have exactly one owner for each control path.

### Priority order

Default ownership policy:

1. **ModemManager first** when the modem is supported and stable through MM.
2. Native netifd QMI/MBIM/NCM only when MM is unsuitable, unavailable, or explicitly disabled by profile.
3. Vendor-specific/direct AT/QMI/MBIM control only for narrow operations that do not conflict with the active owner.
4. HiLink/ECM/RNDIS devices use their network/API model and should not be forced into MM.

A profile may override the default only with a documented reason.

### No double ownership

The core must prevent two stacks from trying to configure the same modem at the same time.

Examples of forbidden states:

- ModemManager owns `cdc-wdm0` while `uqmi` independently opens the same control device for session management.
- MM owns an AT port while another long-running service polls or reconfigures that same port.
- netifd QMI/MBIM and ModemManager both try to create the data session.
- recovery code resets or rebinds a device without first coordinating with its active owner.

Read-only or short vendor-specific AT access may be allowed only through an arbitration layer and only when the profile marks it safe.

### Driver/module responsibility

The manager should determine the required kernel/user-space stack from the resolved profile and observed USB interfaces.

Typical responsibilities include:

- ensure the required kernel modules are available/loaded
- add dynamic VID/PID IDs only when necessary
- avoid binding serial/network drivers to interfaces that belong to another function
- wait for the complete stable USB composition before declaring detection complete
- detect missing/bad bindings and repair them using profile-declared actions
- persist required dynamic binding policy across reboot when the kernel does not

Module loading and rebinding are part of device bring-up, not UI behavior.

### ModemManager integration

When MM is the selected owner:

- wait for the MM modem object instead of racing it from hotplug
- use MM state as the authoritative lifecycle/session state
- allow MM to choose/manage QMI/MBIM control ports
- avoid deleting or replacing MM hotplug rules globally
- do not hide MM LuCI integration as a side effect
- use MM inhibit/reserve mechanisms when temporary exclusive access is genuinely required
- release inhibition deterministically
- never leave the modem permanently inhibited after a crash/restart

The goal is coexistence with MM, not working around it.

### Owner state

The normalized device state should explicitly record:

```
owner:
  control: modemmanager | netifd-qmi | netifd-mbim | ncm | xmm | hilink | vendor
  session: modemmanager | netifd | device
  at: modemmanager | shared-safe | medvemodem | unavailable
```

Recovery and plugins must consult this ownership state before touching the modem.

### Stability over feature count

For the first release, success means:

- modem identified reliably
- correct modules bound
- correct owner selected
- one stable data session
- reconnect after carrier loss
- reconnect after modem reset
- reconnect after USB re-enumeration
- cold-boot detection
- no ModemManager races
- no duplicate network interfaces/sessions
- no permanent port grabs

Advanced metrics, band locking and vendor controls are secondary to those invariants.

## Recovery

Recovery must also be profile-driven.

Use the canonical L0–L7 ladder in TECHNICAL-SPEC.md section 22.1.
Profiles declare validated capabilities; the core coordinates owners, leases,
cooldowns, execution and post-action validation.

## Data sources

Hardware knowledge comes from the modem zoo ([modem-zoo](modem-zoo/README.md)), field reports from real routers and
the vendors' AT manuals, in that order (SOURCE-HIERARCHY.md): expanded hardware profiles, port
detection, recovery techniques, metrics parsing, radio controls, HiLink support and multi-modem behaviour. Other
projects are studied for behaviour only; no code is taken (CATALOG-PROVENANCE.md).

Do not import eSIM/lpac code from other projects; eSIM is a core capability
adapter (ESIM-INTEGRATION.md), not a plugin with its own
discovery, locks or resets.

## Anti-combiner rules

The following should be rejected during review:

- adding model-specific branches directly to the core when a profile/plugin can represent them
- copying a full vendor script into the manager core
- querying the modem independently from LuCI and CLI
- hardcoding `/dev/ttyUSB0`, `cdc-wdm0` or `wwan0`
- assuming one modem
- assuming one USB composition per modem
- destructive recovery without a profile declaring it safe
- mixing UI logic with modem transport logic

## Implementation order

Follow the canonical milestones in TECHNICAL-SPEC.md section 33.

Recovery levels follow section 22.1 of that specification; plugin operations use
semantic action names and never independently choose escalation levels.
