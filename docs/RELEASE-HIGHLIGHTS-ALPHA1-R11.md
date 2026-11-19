# MedveModem 0.16.0 Alpha 1 r11 — Release Highlights

## 0.16.0-alpha1-r11

- **QModem handover recovery:** Auto releases a QModem section when the modem profile prefers ModemManager or native QMI/MBIM. Auto keeps QModem when the profile explicitly prefers QModem.
- **No false QModem ownership:** a saved `backend=qmodem` is no longer treated as proof that QModem owns the modem. Ownership is confirmed only after a real `modem-device` section appears.
- **Recover from failed adoption:** if QModem never creates its section, the administrator can switch to Auto, ModemManager or native control; MedveModem clears the stale backend selection and returns the modem to the selected path.
- **AT-port handover serialization:** wait for MedveModem's own AT operations to finish before QModem scans the modem, avoiding concurrent access to the same AT port.
- **LuCI diagnostics:** show an explicit failed/pending QModem handover state and do not display stale QModem readings as if they belonged to a live owner.
- **Regression coverage:** add source contracts for Auto/MM/native return paths and the missing-QModem-section case.

**Known limitation:** native QMI does not yet provide independent TX/RX antenna-chain readings while `cdc-wdm` is held by `uqmi`. For full T99W175 radio telemetry, use automatic mode with ModemManager.

**Validation:** frontend contracts, QModem handover contracts, Chromium smoke and six-target SDK compilation must pass. Hardware acceptance on T99W175 remains required before treating the release as field-approved.
