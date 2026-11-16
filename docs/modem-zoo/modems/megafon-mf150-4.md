# MegaFon MF150-4 (05c6:f00e, «Mobile.Router.L8D»): its web API

Source: the owner's HAR of the stick's web, 2026-10-30 (the stick in a Windows PC, firmware
`Mobile.Router.L8D.B06`, UI `Mobile.Router.L8D.W06`, hardware `Mobile.Router.L8D.D01`). The same USB IDs as the UFI
of `profiles/generic/ufi-05c6-f00e.json`: an RNDIS router inside the stick, no AT port. Field report, rank 1 in
the modem's observed behaviour. No identifier from the capture is kept here (IMSI, IMEI, numbers and SMS text left out).

## Address and name

- The stick's address is its setting `dev_ip` (factory 192.168.100.1, DHCP hands out .100 up). On the owner's router
  it had been set to 192.168.1.10 and handed out 192.168.1.100: inside the router's LAN 192.168.1.1/24. Nothing went
  to the internet through it, and its web answered under `status.megafon.ru` (the stick's own DNS name for itself,
  not a redirect to the operator). The engine now says such a clash (mgr_clash.c, r121).
- The subnet is changed by `POST /wxml/set_sys_dhcp.xml` (the page computes the DHCP range as .100 up from `dev_ip`).

## No login for reading

Every `mark_*.w.xml` read and every `/wxml/*.xml` POST in the capture went without a cookie. A login exists
(`POST /wxml/post_login.xml` `Name`, `password = md5(rand[0:3] + pwd + rand[3:8])`, `rand`; `login_check` 4 or 7),
but the reads do not ask for it. `mark_set_sys.w.xml` even returns the web login and password in clear text, and
`mark_about.w.xml` the IMSI and IMEI: never log or store those answers whole. The owner, 2026-10-30: «но мы будем это использовать» — the engine reads `login_username`/`login_pwd` there
when an action asks for a login and keeps them in memory only (never logged, never in a report or the run dir).

## Reads (GET, `?_=<ms>` against caching)

| URL | What | Fields |
|---|---|---|
| `mark_home.w.xml` | status page | `op_mode` «LTE,LTE»; `netstatus` (index into: static off, static on, dhcp off, dhcp on, pppoe off, pppoe on, init, dialing, **8 disconnected**, **9 connected**); `networkmode`; `rssi_info` «-99dBm,-10dB,6dB,-71dB» (the page shows only the first as «Сигнал»; by the values RSRP, RSRQ, SINR, RSSI); `cell_info` «PLMN,ECI(dec),PCI?,PLMN+TAC(hex)+ECI(hex)» (e.g. 25001,1703939,450,…: ECI 0x1A0003 = eNB 6656 cell 3); `umts_*`, `geran_cell_info` (-1 when not in use); `wan` (operator IP); `pri`, `sec` (DNS) |
| `mark_title.w.xml` | header, every 1–4 s | `cspn` (operator name), `netstatus`, `op_mode`, `roam`, `pin`, `rate` «down,up» (current rate), `systime`, an unread-SMS indication |
| `mark_conn.w.xml` | statistics | `tx`, `rx`, `all` («162.58 KBytes»), `active_time` «hh:mm:ss» for the session; `t_tx`, `t_rx`, `t_all`, `t_active_time` in total |
| `mark_set_net.w.xml` | APN | three `apn` profiles (`apn_name`, `user`, `pwd`, `apn_apn`, `authmode`), `apn_active`, `dial_mode`, `ip_mode`, DNS mode |
| `mark_set_pin.w.xml` | PIN | `state`, `pin` and `puk` tries left |
| `mark_set_pf.w.xml` | port forwarding and clients | `user_list` (name, ip, mac), `pf_enable`, `pf_list`, `wan_ip` |
| `mark_about.w.xml` | about | firmware versions, MAC (and IMSI, IMEI: do not keep) |
| `ussd_res.w.xml` | USSD answer, polled | `res` («-» until the answer) |

## Actions (POST, form-encoded)

- SMS list: `/wxml/sms_box.xml` `href=<box>&page=<n>` (box 0 inbox, 1 outbox, 2 drafts, 3 flash); 20 per page;
  `sms_count` «unread,inbox,outbox,drafts»; per message `sms_id`, `sms_src` «1,<sender>» (a leading 0 means none),
  `contact_name`, `sms_body` CDATA «1,<text>», `sms_time` «dd-mm-yyyy hh:mm:ss», `sms_status` (1 unread, 3 read in the
  capture). `sms_id` «-» ends the list.
- Read mark: `/wxml/sms_show.xml` `read_id=<id>` (marks it read: the bot must not call it).
- Send: `/wxml/sms_send.xml` `d_number`, `d_content`, `d_time=yymmddhhmmss` → `submit` (1 sent; -1 failed, the
  owner's test to a short number), `draft`.
- Delete: `/wxml/sms_del.xml` `href=<box>&no20=<id>`.
- USSD: `/wxml/ussd_send.xml` (or `ussd_answer.xml` inside a session) `send=<code>`, then poll `ussd_res.w.xml`;
  `/wxml/ussd_cancel.xml` `cancel=1`.
- Connect / disconnect: `/wxml/conn.xml` `conn=0` connects (when `netstatus` is 8), `conn=1` disconnects.
- Reboot: `/wxml/set_restore.xml` `reset=1` («Настройки устройства будут сброшены» on the page, a restart;
  `restore=1` is the factory reset: never). Also `/wxml/fota_op.xml` `reboot=1`.
- Statistics reset: `/wxml/clear_ts.xml` `clear_ts=1`. Network mode / operator: `set_net.xml`, `set_op.xml`, `op_list.xml`.

## For MedveModem

A third web dialect beside the HiLink's and ZTE's, kind "mark" in websig.c (2026-10-30): recognised by
`mark_title.w.xml` answering `<title>` with `<dsc>`; the signal (RSRP, RSRQ, SINR, RSSI, cell, eNB, PCI, TAC,
operator) from `mark_home.w.xml` and `mark_title.w.xml`; `web-act` info (WAN IP, traffic, versions), reboot
(`set_restore.xml reset=1`, so the ladder's «Перезагрузить модем» goes through it) and subnet. SMS and USSD not yet.

The LAN clash is fixed by itself (the owner: «для MF150-4 я бы автоматически устранял конфликт IP»): mgr_clash.c
picks 192.168.100.1 (else .101–.120, free of the router's lan and the other modems' sessions), web-act subnet posts
the system form back with the login the web gave (`set_sys_dhcp.xml`), then our own interface goes down and up.
