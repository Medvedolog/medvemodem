# UZ801 4G UFI dongle (05c6:f00e, «Chinese dongle 4G UFI»): its web API

Source: the owner's HAR of the stick's web, 2026-10-30 (firmware UZ801-V2.3.12, Qualcomm MSM8916-class, web at
192.168.100.1, DHCP .100–.200) and the router's diagnostic report of it (rndis_host on usb 1.2, same USB IDs as
`profiles/generic/ufi-05c6-f00e.json` and the MF150-4). Field report from a real device. No identifier from
the capture is kept here.

## API

Every call is `POST /ajax` with a JSON body (sent as form-urlencoded by the page's jQuery) and answers
`{"results":[{…}],"error_info":"none","flag":"1"}`. No cookie: the login holds for the client's address; logged
out, any call answers an HTML page that sends the browser to `login.htm`.

| funcNo | What |
|---|---|
| 1000 | login `{username, password}` (admin / admin) → net_mode, fwversion, conn_mode (and the IMEI: never kept) |
| 1001 | status: `oper`, `netstatus` «Connected»/«Disconnected», `netmode` «LTE»/«UNKNOWN», `rssi` 0–5 bars |
| 1002 | the stick's LAN: IP, mask, DNS, Wi-Fi SSID and password |
| 1003 | traffic `up_bytes`, `down_bytes` in KB, Wi-Fi clients |
| 1004 | `conn_mode` 0 auto / 1 manual |
| 1005 | `net_mode` 0 auto, 1 3G only, 2 4G only |
| 1006–1010 | Wi-Fi settings |
| 1011 | DHCP: range, clients |
| 1013 | restart |
| 1014 | factory reset (never sent) |
| 1015 | SIM: status (and ICCID, IMSI: never kept) |
| 1016–1018 | APN profiles: read, write, connect |
| 1020 | web password |
| 1029 | `dbm` « -71 dBm», manufacturer, fwversion (and the IMEI) |
| 2002 | IMEI write (never) |
| 3002 | `device_name` «UZ801», `sw_version` |

No SMS or USSD in this firmware's web.

## For MedveModem (2026-10-30)

Kind "uz" in websig.c: recognised by a JSON answer (or the login page) of `/ajax` {funcNo 1001}; the signal (RSSI,
operator, technology, firmware) without the IMEI; login 1000 with the web's default or the operator's password, at
most once in 5 minutes; `web-act` info (traffic, name, firmware) and reboot (1013), so the ladder's «Перезагрузить
модем» goes through it. Its web link (the 🌐 chip) works without a gateway in the lease too: a stick whose SIM has
no data gives none, and its web is then the first address of the subnet.
