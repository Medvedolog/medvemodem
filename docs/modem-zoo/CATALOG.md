# Modem Zoo — catalog

Сводный указатель machine-readable evidence. Наличие записи **не означает**, что модель уже hardware-verified в MedveModem.

## QModem model database

### fibocom

`fm150-ae`, `fm160-cn`, `fm160-eau`, `fm160-na`, `fm170-eau`, `fm190w-gl`, `fm350-gl`, `fm350-gl-00`, `fm350r-gl`, `fm650-cn`, `l610-eu`, `nl668`, `nl678`, `rw350r-gl`

### foxconn

`t77w968`, `t99w175`, `t99w368`, `t99w373`, `t99w640`

### gosuncn

`gm800`, `me3630-w`

### huawei

`mh5000-31`, `mh5000-82`, `mh5000-82m`, `mt5700m-cn`, `mt5710_cn`, `s10`

### meig

`srm815`, `srm821`, `srm825`, `srm825n`

### neoway

`n510m`

### openluat

`air724ug`

### quectel

`ag598eeu`, `ec200a`, `ec20f`, `ec21`, `ec25`, `eg25`, `em05`, `em05-cn`, `em12`, `em120k`, `em160r-gl`, `ep06`, `mh8001-eu`, `rg200u-cn`, `rg500q-ea`, `rg501q-eu`, `rg520f-eb`, `rg520f-eu`, `rg520f-jo`, `rg520n-eb`, `rg520n-eu`, `rg530f-na`, `rg650v-eu`, `rg801`, `rm500q-ae`, `rm500q-cn`, `rm500q-gl`, `rm500s-ce`, `rm500u-cn`, `rm500u-cnv`, `rm500u-ea`, `rm502q-ae`, `rm502q-gl`, `rm505q-ae`, `rm510q-gl`, `rm510q-glha`, `rm520f-bl`, `rm520f-gl`, `rm520n-cn`, `rm520n-eu`, `rm520n-gl`, `rm521f-gl`, `rm530n-gl`, `rm551e-gl`

### sierra

`em9190`, `em9291`, `mc7354`, `mc7355`

### simcom

`a7605c`, `a8200c-m2`, `simcom_a8200_serias`, `simcom_d8200g`, `simcom_sim8200ea-m2`, `simcom_sim8202g-m2`, `simcom_sim8380g-m2`

### telit

`fn990a40`

### thales

`mv32-w-a`, `mv32-w-b`

## Historical composition models — a field report/other scripts catalog

- **3372h** — `12d1:108a`, `12d1:14db`, `12d1:14dc`, `12d1:1506`, `12d1:155e`, `12d1:1566`
- **e392** — `12d1:1506`
- **em160** — `2c7c:0620`
- **ep06** — `2c7c:0306`
- **fm350** — `0e8d:7126`, `0e8d:7127`
- **fn990a28** — `1bc7:1070`
- **generic-hilink** — `05c6:90b4`, `05c6:f00e`, `0e8d:2004`, `1286:4e31`, `19d2:0557`, `19d2:0558`, `22d9:276a`, `2717:ff80`, `2d95:600a`, `2ecc:3012`
- **l830** — `2cb7:0210`
- **l850** — `2cb7:0007`, `2cb7:000b`, `8087:095a`
- **mf823** — `19d2:1244`, `19d2:1402`, `19d2:1403`, `19d2:1404`, `19d2:1405`
- **mf831** — `19d2:9994`
- **simcom** — `1e0e:9001`
- **t77w676** — `03f0:0857`, `03f0:0a57`, `1bc7:1900`, `1bc7:1901`
- **t77w968** — `1bc7:1910`, `1bc7:1911`, `413c:81d7`, `413c:81e0`
- **t99w175** — `05c6:9025`, `05c6:90d5`, `1e2d:00b3`, `1e2d:00b7`

## another modem manager — facts without a family page

Reference only, not bench-validated; full review: RELEASE-DELTA-2026-10-21.md.

- **`…-v356-ufi003-qcnsp`** (`05c6:90b4`) — `upstream-code-reference`, not-bench-validated: On the UFI003 stick in USB modem mode the network mode is AT$QCNSP: read AT$QCNSP?, write AT$QCNSP=&lt;n&gt;,0,3 with 0 auto, 1 GSM, 2 WCDMA, 6 LTE; the allowed values are taken from AT$QCNSP=?. After the stick reboots its mode comes back as 10, so they restore the saved mode on the next poll and drop their band cache. Source: a field report/another modem manager @ `a22590c` (root/usr/share/another modem manager/modemband/05c690b4, root/usr/share/another modem manager/modem/usb/05c690b4:18-39).
- **`…-v3516-sms-storage-keep`** — `upstream-code-reference`, not-bench-validated: Their automatic SMS storage choice asks the capacity of ME and SM (up to two retries 3 s apart) and takes the larger. Before v3.5.16, when ME stayed silent and SM answered, the choice moved to SM and messages kept in ME vanished from the list. v3.5.16 keeps the current storage when the modem does not report its capacity and switches to the other one only when the modem rejects the current storage on AT+CPMS="X","X","X". Source: a field report/another modem manager @ `a22590c` (root/usr/share/another modem manager/lib.sh:2010-2075, root/usr/share/another modem manager/lib.sh:2036-2041, root/usr/share/another modem manager/lib.sh:2057-2065, root/usr/share/another modem manager/lib.sh:1963-1998).
- **`…-v357-soc-remoteproc`** — `upstream-code-reference`, not-bench-validated: A modem built into the SoC has no USB device: they detect it by a remoteproc child under /sys/bus/platform/devices, synthesise a VID:PID of 05c6:&lt;first four digits of /sys/devices/soc0/machine&gt;, take the model from /proc/device-tree/model and run it under ModemManager with device=qcom-soc. For these modems the initial EPS bearer setting is removed (v3.5.8), otherwise the modem hangs in init after a reboot; temperature is the maximum over all thermal zones. Source: a field report/another modem manager @ `a22590c` (root/usr/share/another modem manager/lib.sh:187-202, root/etc/uci-defaults/50-another modem manager-soc-mm-device.sh, root/etc/uci-defaults/45-another modem manager-mm-init-eps.sh).
- **`…-v357-rx-noant`** — `upstream-code-reference`, not-bench-validated: With receive diversity they count a chain as alive only when its RSRP is above -130 dBm; with 2 to 4 chains reported and only one alive the state is 'noant' - diversity is on but the second antenna is not connected - and MIMO is shown as RX 1. Source: a field report/another modem manager @ `a22590c` (root/usr/share/another modem manager/another modem manager.sh:2431-2436).

## Что дальше

Curated model-family pages постепенно будут покрывать самые интересные/сложные семейства; остальные записи уже доступны как machine evidence и попадут в страницы автоматически по мере расширения editorial mapping.

## Curated deep dives

- [Fibocom FM350 / RW350 — MediaTek, RNDIS/PCIe MBIM, GTACT, eSIM/USSD quirks](modems/fibocom-fm350.md)
- [Foxconn T99W175 / SDX55 — QMI, MBIM, QMI-over-MBIM, full AT corpus](modems/foxconn-t99w175.md)
- [Quectel RM520N / RM5xx — USB/PCIe transports, QNWPREFCFG, QNWLOCK, destructive composition hazards](modems/quectel-rm520n.md)
- [Foxconn T77W968 / Dell DW5821e — QMI/MBIM, FCC, SLMODE/SLBAND/ULCA](modems/foxconn-t77w968.md)
- [Quectel EC25 / EP06 / EM12 — QCFG/QENG LTE generations and model-specific differences](modems/quectel-lte-ec25-ep06-em12.md)
- [Fibocom L850 / L860 — Intel XMM, XACT, freq_lock, NVM composition](modems/fibocom-l850-l860.md)
- [Fibocom FM150 / FM160 — Qualcomm transport evidence, conservative command scope](modems/fibocom-fm150-fm160.md)
- [Quectel RM551E-GL — scoped QNWPREFCFG/QNWLOCK and AT-contention field evidence](modems/quectel-rm551e.md)
- [Quectel RM530N-GL — separate RM520-compatible mmWave family](modems/quectel-rm530n.md)
- [Fibocom FM190W-GL — SDX75 generation](modems/fibocom-fm190w.md)
- [Fibocom FM650-CN — UNISOC V510/V516 line](modems/fibocom-fm650.md)
- [Compal RXM-G1 / SG500M2-X / VOS5G — field incidents, compositions and safety](modems/compal-rxm-g1.md)
- [Sierra EM9190 / EM9191 / EM9291 — field incidents, compositions and safety](modems/sierra-em919x.md)
- [SIMCom SIM7100 / SIM7600 / SIM79xx — field incidents, compositions and safety](modems/simcom-sim7x.md)
- [ZTE MF79 / MF286 / Tenda MF6 / OLAX — field incidents, compositions and safety](modems/zte-mf-family.md)
- [Huawei E3372 / HiLink — field incidents, compositions and safety](modems/huawei-hilink.md)
- [MikroTik R11e-LTE — field incidents, compositions and safety](modems/mikrotik-r11e.md)
- [Telit LM960 / LN960 / FN990 — field incidents, compositions and safety](modems/telit-lm-fn.md)
- [iPhone USB tethering — field incidents, compositions and safety](modems/iphone-tether.md)
- [Global model-specific AT command corpus](AT-COMMANDS.md)
- [Extended QModem/other scripts raw AT reference](AT-REFERENCE.md)
- [Voice calls: what can call, how the neighbours do it](VOICE.md)
- [Telegram field notes](TELEGRAM-FIELD-NOTES.md)
- Telegram package artifacts
- [Composition/fastboot/EDL compatibility and safety](COMPOSITION-AND-BOOT-MODES.md)
