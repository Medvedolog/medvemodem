# MedveModem 0.16.0 Alpha 1 r11 — Release Highlights

## 0.16.0-alpha1-r11

- **Component installs from LuCI finish again:** in r10 «Drivers and components» started the install of drivers,
  ModemManager, lpac or the bot and never finished it: the installer recorded `$` instead of its process number.
- **FM350 after a USB reset stays with MedveModem:** while the modem comes back through its MediaTek loader its profile is
  not known yet, and the engine handed it to ModemManager, which cannot dial it; its interface then stayed down until
  brought up by hand. A modem without a recognised profile or with an unstable USB link is no longer handed over, and an
  interface left waiting for ModemManager comes up again when the modem re-enumerates.
- **Russian translations** for «Drivers and components» and eight more strings that showed in English.
- **QModem handover recovery:** Auto releases a QModem section when the modem profile prefers ModemManager or native QMI/MBIM. Auto keeps QModem when the profile explicitly prefers QModem.
- **No false QModem ownership:** a saved `backend=qmodem` is no longer treated as proof that QModem owns the modem. Ownership is confirmed only after a real `modem-device` section appears.
- **Recover from failed adoption:** if QModem never creates its section, the administrator can switch to Auto, ModemManager or native control; MedveModem clears the stale backend selection and returns the modem to the selected path.
- **AT-port handover serialization:** wait for MedveModem's own AT operations to finish before QModem scans the modem, avoiding concurrent access to the same AT port.
- **LuCI diagnostics:** show an explicit failed/pending QModem handover state and do not display stale QModem readings as if they belonged to a live owner.
- **Regression coverage:** add source contracts for Auto/MM/native return paths and the missing-QModem-section case.

- **An interface broken in its configuration repairs itself:** after the r10 native choice a T99W175 kept only a metric in
  its `network.mmN` (no `proto`, no `device`) and the engine restarted the session and reset USB for hours. Such an
  interface is now written again (at most every 10 minutes) and recovery leaves it alone.
- **No «no device» while the modem is there:** netifd's `NO_DEVICE` reads as an interface not linked to the modem.
- **lpac and the bot install from «Drivers and components» again** (since r9 the installer read the release check
  shifted by one and fetched the wrong archive).
- **FM350 in RNDIS composition 40 (0e8d:7126) dials again:** AT is interface 4 there; the inherited interface 6 is a
  silent NPT port (a tester's report, firmware 29.24.02).
- **Security and stability audits:** the modem web client can no longer write past its request buffer and wipes the
  login on every exit; a batch of forwarded SMS is no longer lost when the helper starts late; no zombie processes when a
  signal interrupts a wait; LuCI background checks start with a clean environment; a proxy password with a newline is
  refused; the speed test is limited to 60 s per direction.
- **Full EN+RU proofreading** of LuCI, the Telegram bot and its help, the event log, SMS alerts, `medvemodem top` and the
  installer.
- **Profiles pass the schema again** (the EP06 and T99W640 field notes), and development moved back to the private
  repository: public releases are published from bench-tested builds only.

**Known limitation:** native QMI does not yet provide independent TX/RX antenna-chain readings while `cdc-wdm` is held by `uqmi`. For full T99W175 radio telemetry, use automatic mode with ModemManager.

**Validation:** frontend contracts, QModem handover contracts, Chromium smoke and six-target SDK compilation must pass. Hardware acceptance on T99W175 remains required before treating the release as field-approved.
