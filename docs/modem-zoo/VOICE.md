# Voice calls: what can call, how the neighbours do it (2026-11-10)

The owner asked which bench modem can make a voice call and how the neighbours do it. Nothing here is bench-validated:
it is the vendors' documentation, the neighbours' code (behaviour only) and the field notes. To be checked on the stand.

## What a call needs

1. Voice allowed by the firmware: data-card firmwares often strip it (CS voice off, no IMS profile).
2. A network for it: 2G/3G circuit-switched, or VoLTE (the operator's IMS profile in the modem).
3. A sound path, for talking only: USB audio (UAC) or PCM (Quectel), audio over a serial port (SIMCom `AT+CPCMREG`, old
   Huawei sticks with voice unlocked). Without it a call still connects and rings the other phone, in silence.

`ATD<number>;` (the semicolon makes it a voice call) or ModemManager's `--voice-create-call` places it; `ATH` or
`mmcli -c N --hangup` ends it. `mmcli -m N` lists a Voice interface when ModemManager can call through the modem.

## The zoo

| Model | Calls | Sound | Note |
|---|---|---|---|
| Quectel EC25 / EG25 / EC21, EP06, EM12, RM500Q / RM520N | yes (`ATD…;`, VoLTE with IMS) | UAC or PCM | the most voice-capable in the zoo |
| SIMCom SIM7600 | yes | over a serial port (`AT+CPCMREG`) | |
| Huawei E171 / E173 / E1550 / E1752 (3G) | only with a voice-unlocked firmware | a serial port | the `chan_dongle` path |
| ZTE MF192, MF100 (3G) | maybe over 3G, MTS firmware may block | unlikely | to try: `mmcli -m N`, `ATD…;` |
| Foxconn T99W175, Dell DW5821e, Fibocom FM350 | unlikely (laptop WWAN firmware) | no | |
| HiLink sticks, UFI / M150-4, MTS 8810FT | no (a router inside) | no | |

## The neighbours

- another modem manager 3.9.34: no calls; shows a VoLTE on/off flag for one MeiG (2dee:4d57).
- QModem: stable none; main has experimental VoIP/SIP for AW1000, RM520N, RG500Q (the modem's voice to SIP), not taken.
- luci-app-sms-tool-js: logs incoming calls (RING, the caller's number); its service holds an AT port.
- Asterisk `chan_dongle`: old Huawei 3G sticks with voice unlocked, sound over a serial port.

## For MedveModem

Not telephony. A possible later feature: an alert by a missed call (the router calls the owner, hangs up after a few
rings): no sound needed, works on any modem that takes `ATD…;`. Waits for a bench modem that calls.
