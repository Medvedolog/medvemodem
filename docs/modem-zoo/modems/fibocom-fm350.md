# Fibocom FM350 / RW350 family

> **Status:** `multi-source-field-rich` · MediaTek family, not interchangeable with Qualcomm FM160 or Intel-XMM L860.

## USB versus PCIe

QModem records:

- USB FM350/FM350R → **RNDIS**;
- PCIe FM350/FM350R → **MBIM**.

a field report's tested USB `0e8d:7126/7127` units report `GTUSBMODE` compositions 40/41, both RNDIS-oriented, and use a dedicated `fibocom` netifd protocol instead of pretending the USB path is QMI/MBIM. other scripts routes the same IDs through its hostless/“ECM” connection path; keep that as a source-specific stack label rather than rewriting the other evidence.

other scripts's historical table labels those IDs `xmm` and contains tty hints, but that label is historical stack terminology, not proof that FM350 is an Intel XMM modem. FM350 itself is MediaTek.

## GTACT is the center of radio control

`AT+GTACT=?` is unusually valuable because it describes the actual capability of the attached firmware. a field report uses it to distinguish 5G-capable FM350 from FM350-GL-16/RW350-GL-16 variants that can share IDs but have no NR bands.

Current documented RAT values include:

- 1 = UMTS;
- 2 = LTE;
- 4 = LTE/UMTS;
- 10 = Automatic;
- 14 = NR;
- 16 = NR/WCDMA;
- 17 = NR/LTE;
- 20 = NR/WCDMA/LTE.

Do not infer NR support from the model string alone.

## Important failure mode: IP up, traffic dead

A successful `GTACT` write does **not** mean a healthy session. Field tests show a band change can leave the interface registered and still holding its IP while actual traffic is dead.

The appropriate recovery is **interface reconnect / PDP re-dial**, not automatically rebooting the modem.

That observation belongs in Zoo because a health checker that only tests “interface has IP” will miss it.

## QModem over USB: the x.x.x.1 gateway, and upstream #198

**Our bench (2026-10-21), FM350 over USB/RNDIS under QModem:** QModem's `ip_change_fm350` (`modem_dial.sh`) sets the
interface static with the first host of the address's /24 as the gateway. The modem never answers that address; the
RNDIS queue hangs (`NETDEV WATCHDOG`), USB resets follow, the AT port goes and comes back, QModem dials again — the
«storm». The engine dials the FM350 with an on-link default route (profile `addressing.gateway`) and the same modem is
stable. From r82 our QModem build sets that line to `gateway="0.0.0.0"` (on-link), see
[the QModem upstream study](../../the working notes).

**Upstream [FUjr/QModem#198](https://github.com/FUjr/QModem/issues/198)** «FM350-GL频繁掉线» (2026-04-11, H29K board,
MediaTek, USB FM350-GL, `driver:rndis`): drops that clear by themselves, the SIM shown missing and the module
«disabled» during a drop, `at port response unexpected` every ~5 s; changing `pdp_index` did not help. The log quoted
there passes the same code path: `ip changed from ,10.111.39.94 to ,10.234.36.42`, `ip_change_fm350`,
`set interface 4_1 to 10.234.36.42`. A contributor put it down to the H29K's power; the maintainer closed it «not
planned» on 2026-07-27 (SIM and AT port lost together read as power or USB re-enumeration) and asked for dmesg/logread
around a drop, the USB topology and AT port tests. No kernel log was posted, so whether `NETDEV WATCHDOG` was there is
unknown. Our reading (an inference, not proven on their board): the SIM and AT port vanishing together is what a USB
reset does, and our storm has the same shape on stable power — the guessed gateway is a cause to rule out before power.
Upstream has no issue or fix for the gateway itself.

## FCC unlock

other scripts contributes a useful FM350-specific recipe absent from the first Zoo draft:

- `AT+GTFCCLOCKGEN` obtains a challenge;
- the stack combines it with its vendor hash and derives a response;
- `AT+GTFCCLOCKVER=<response>` submits the response;
- `+GTFCCLOCKVER: 1` is treated as successful unlock.

This is a radio-enable capability, not a generic polling command, and should eventually belong to the resolved owner backend. A separate Telegram field recipe gives `AT+GTFCCLOCKMODE=0` as a persistent FCC-lock disable followed by reboot/replug; this is **high-risk persistent configuration**, not part of the normal unlock sequence.

## SIM / eSIM / SMS / USSD

- eUICC is field-confirmed on `0e8d:7127`;
- `GTDUALSIM` + `SIMTYPE?` are used for slot/type handling;
- SMS remains an AT-service concern behind the common broker;
- USSD on tested `0e8d:7126/7127` is explicitly recorded as **unsupported in practice**: the firmware advertises `+CUSD`, but extensive tests produced no usable response.
- **Our bench, 2026-10-26** (OPi Zero 3, 25.12, firmware 81600.0000.00.29.24.02, Beeline): `*110*10#` over the engine's AT port
  (ttyUSB7) — no answer from the network in 40 s. The profiles now carry `hints.ussd: unsupported`: the engine refuses at
  once and the modem page hides the USSD block.

### eSIM (eUICC) over AT

In the USB fibocom/RNDIS composition there is no `cdc-wdm` node: the eUICC is reachable only through AT logical channels (`+CCHO/+CGLA/+CCHC`). Field work (a field report esim.sh / esim-apdu-bridge.sh, v3.2.2, 2026-07..09) established:

- **one port of several.** Only one tty accepts `AT+CCHO` and its number changes on re-enumeration; the other ports answer `AT` but swallow CCHO without any reply. Find it with a short CCHO probe over this modem's own ports (never a neighbour's: a single-SIM SIM7600 once "got" the eSIM of an FM350 next to it), excluding a port used for dialling.
- **bare channel number.** CCHO answers `1`, not `+CCHO: 1`.
- **CGLA length in hex characters** (27.007); a byte count gets no answer.
- **leaked channels.** A killed operation leaves its channel open; then CCHO is unanswered on every port and the modem looks eSIM-less. `AT+CCHC=1..4` recovers it without a power cycle.
- **lpac's own AT driver stalls** on long CGLA chains (download stops at exactly 240 s). What works: lpac with `LPAC_APDU=stdio` and a bridge doing short exchanges. lpac **2.3.0's stdio driver is broken** (fixed upstream after that release).
- **port contention.** Metrics polling and the fibocom dialer use the same tty; with an empty eUICC the dialer retries every ~70 s and starves a download. Arbitrate per port (lease) and pause the dialer for the operation.
- **slow card.** A download may take ~10 minutes.
- **after enabling a profile** FM350 handles REFRESH itself (no `CFUN` reset, unlike SDX55); a SIM slot switch re-enumerates and resets the band mask.
- PCIe FM350 is recorded by QModem as MBIM; eUICC access over MBIM there is not field-verified. QModem has no eSIM support; its menu lists `AT+GTESIMCFG=0,0,0` ("Unlock ESIM") with undocumented semantics.

MedveModem implements this as the engine's AT bridge with a per-attachment eUICC port probe (docs/ESIM-INTEGRATION.md). Commands and risks: [AT-COMMANDS](../AT-COMMANDS.md), script card *esim.sh / esim-apdu-bridge.sh* in SCRIPTS.

## Known AT command corpus

| Command | Category | Risk | Purpose | Source |
|---|---|---|---|---|
| `AT+GTACT?` | radio.mode_band | **READ** | Read RAT/preference/band selection. | a field report/QModem |
| `AT+GTACT=?` | radio.capability | **READ** | Read accepted RAT and UMTS/LTE/NR band values; used to distinguish 5G-capable and GL-16/RW350 4G-only variants sharing IDs. | a field report FM350 |
| `AT+GTACT=<rat>[,<pref1>,<pref2>[,<bands>]]` | radio.mode_band | **MUTATING** | Change RAT and/or bands. Field tests show active PDP may become dead while IP remains assigned; reconnect is required. | a field report FM350 |
| `AT+GTCCINFO?` | cell.telemetry | **READ** | Serving and neighbour-cell/carry information for FM350 family. | a field report/QModem |
| `AT+GTCAINFO?` | radio.ca | **READ** | Carrier aggregation information where supported. | QModem Fibocom wrappers |
| `AT+ETHERMAL?` | hardware.temperature | **READ** | Temperature/thermal report used in current FM350 template. | a field report FM350 templates |
| `AT+GTUSBMODE?` | transport.composition | **READ** | Read USB composition. Tested FM350 USB reports modes 40/41. | a field report/QModem |
| `AT+GTUSBMODE=<mode>` | transport.composition | **HIGH** | Change Fibocom USB composition; model-generation-specific and can re-enumerate. | QModem Fibocom wrappers |
| `AT+GTDUALSIM?` | sim.switch | **READ** | Read active SIM slot on tested FM350. | a field report/QModem |
| `AT+GTDUALSIM=<slot>` | sim.switch | **MUTATING** | Switch SIM slot where supported. | QModem/a field report |
| `AT+SIMTYPE?` | sim.type | **READ** | Distinguish physical USIM/eSIM on tested FM350 slot implementation. | a field report quirks/simslot |
| `AT+ICCID` | sim.iccid | **READ** | Read ICCID. | a field report/QModem |
| `AT+GTPKGVER?` | identity.firmware | **READ** | Read Fibocom firmware package version. | a field report FM350 templates |
| `AT+CUSD=?` | ussd | **READ** | Firmware advertises CUSD capability on tested FM350, but that does not imply working supplementary services. | a field report field test |
| `AT+CUSD=1,<code>[,<dcs>]` | ussd | **UNSUPPORTED OBSERVED** | Exhaustively field-tested on 0e8d:7126/7127 without useful response; retained to document the failure mode. | a field report quirks |
| `AT+GTFCCLOCKGEN` | radio.fcc | **READ** | Request FCC unlock challenge. | other scripts create_hostless.sh |
| `AT+GTFCCLOCKVER=<response>` | radio.fcc | **MUTATING** | Submit derived FCC unlock response. | other scripts create_hostless.sh |


## Our bench, 2026-11-16 (FM350-GL over USB, RNDIS 0e8d:7127, idle LTE)

Answers on the AT port (interface 6, /dev/ttyUSB3, about 37 ms each). The cell id and TAC are written as `<ci>`/`<tac>`.

- `AT+GTPKGVER?` → `+GTPKGVER: "81600.0000.00.29.24.02_F27"` (ATI: `81600.0000.00.29.24.02`).
- `AT+GTACT=?` → RATs `(1,2,4,10,14,16,17,20)`; LTE `101…171`: B1, 2, 3, 4, 5, 7, 8, 12, 13, 14, 17, 18, 19, 20, 25, 26,
  28, 29, 30, 32, 34, 38, 39, 40, 41, 42, 43, 46, 48, 66, 71; NR `501…5079`: n1, 2, 3, 5, 7, 8, 20, 25, 28, 30, 38, 40,
  41, 48, 66, 71, 77, 78, 79 — the 5G FM350-GL, not the -16 variant without NR.
- `AT+GTCCINFO?`, idle (band and bandwidth empty: the modem gives them in connected mode only):
  `1,4,250,99,<tac>,<ci>,525,135,,,15,41,41,31` — serving LTE, EARFCN 525 (B1), PCI 135, SINR 15, RSRP idx 41 = −100 dBm,
  RSRQ idx 31 = −4.5 dB; neighbours `2,4,,,FFFF,00FFFFFFF,1301,389,,30,30,17` (B3, −111 dBm, −11.5 dB) and
  `…,1301,232,,24,24,52`. The engine's parser (src/radio.c, cells_gtcc) reads this layout as it is.
- `AT+ECELL` (MediaTek's): `+ECELL: 2,7,"<ci>","<tac>",250,99,135,41,24,-400,-32,,,,1,525,7,"00FFFFFFF","FFFF",,,389,30,17,-444,-46,,,,,1301`.
  The first field is the number of cells; each cell then has 15 fields: RAT (7 LTE), cell id, TAC, MCC, MNC, PCI, RSRP
  index, RSRQ index, **RSRP in ¼ dBm** (−400 = −100 dBm), **RSRQ in ¼ dB** (−32 = −8 dB), **TA**, three more fields,
  EARFCN. TA is empty in idle: it exists only with an RRC connection, so an antenna page has to hold the data link busy to see it.
  The serving cell's 4th extra field was `1`, a neighbour's empty. The ¼-dB RSRP is finer than GTCCINFO's 1 dB:
  useful for an antenna page where single decibels matter.

## Historical port observations

- `0e8d:7126`: historical AT/SMS hint `ttyUSB1`;
- `0e8d:7127`: historical AT/SMS hint `ttyUSB3`.

They are not stable port roles.

## Family boundary

Do not inherit FM350 `GTACT`/RNDIS behavior to FM160 merely because both say Fibocom. FM160 is a Qualcomm generation with a different transport model.

## Telegram field evidence — 2024–2026 exports

- a router maker's firmware test hardware on firmware `8C4A103C HP 29.24.02 5052` was observed with `GTUSBMODE=40/41` switching between `0e8d:7127/7126`. In each observed composition control/AT/SMS shared one ACM endpoint (historical hints ttyACM3/ttyACM1), which is direct evidence for central port arbitration rather than independent pollers.
- Repeated a router maker's firmware reports say USSD could not be made to work on tested FM350. This remains firmware/test-scope evidence, not a vendor-wide specification.
- A community `fm350_antenna_scan.sh` PoC uses `AT+GTRXPATHEN=<rat>,1/2/4/8` to isolate RX paths, reads `CESQ` / `GTCCINFO`, and restores mask `15` via an exit trap. Because it actually disables receive paths, Zoo classifies it as an Expert session-affecting diagnostic.
- Community HSPA parser work, later edited by a router maker's firmware, uses `CESQ` with RSCP `raw-120` and Ec/No `raw/2-24`; it was still explicitly awaiting wider field verification.
- The Telegram FCC challenge/response recipe matches other scripts's independent `GTFCCLOCKGEN` / `GTFCCLOCKVER` implementation.

See [Telegram field notes](../TELEGRAM-FIELD-NOTES.md) for message IDs and confidence labels.

<!-- 4pda-field-2026-10-15:start -->
## 4PDA — дополнительные модельные полевые факты

**Reference only; not hardware-verified.** Данные из пользовательских HTML-архивов; связь `message-N` — только с локальным архивом. Нет новых runtime presets/AT operations. Все кейсы и SHA256: [4PDA field notes](../4PDA-FIELD-NOTES.md).

- **`4PDA-1057776-9505`** (danger): После GTUSBMODE=40 пользователь получил только META+DEBUG без доступного AT-порта. **MedveModem:** Сценарий потери AT-port; запрет автоматического перехода в mode40. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776).
- **`4PDA-1057776-9507`** (danger): Тот же автор подтвердил, что при META+DEBUG и отсутствии AT-порта помогла перепрошивка. **MedveModem:** Recovery относится к сервисным действиям с согласованием FW и бэкапом; никакого циклического AT-switchover. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776).
- **`4PDA-1057776-4745`** (read): OpenWrt 23.05.5 на Beeline Turbo+: после reboot при том же SuperSpeed RNDIS устройство видно, но AT/интернет не работают; после физического переподключения работают. В проблемном состоянии интерфейс USB If#4 без driver, после переподключения занят option. Отдельно отмечены нагрев до 75°C и исчезновение USB-модемов до снятия питания роутера. **MedveModem:** Различать cold/warm boot, отслеживать driver bind каждого интерфейса, сохранять lsusb -t и события netifd. Нет доказательства, что причиной был только нагрев. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776).
- **`4PDA-1057776-7043`** (read): Huasifei WS1208v2/OpenWrt: после GTDIPCMODE=3,1,1,1,3,15, GTUSBMODE=41 наблюдал 0e8d:7127, семь ttyUSB и AT на ttyUSB3; сеть показывала IP и метрики, но трафик не шёл ни на other scripts, ни на OpenWrt 24.10.0 с двумя вариантами управляющих пакетов. **MedveModem:** Состояния connected/IP и Internet reachable не эквивалентны; проверять маршрут, DNS, ICMP по IP, PDP data-path; это НЕ подтверждённый фикс. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776).
- **`4PDA-1057776-1734`** (write): После обновления до 81600.0000.00.29.23.16 a router maker's firmware не обнаруживал модем; после AT+GTUSBMODE=41 определение и связь восстановились. **MedveModem:** В профиле хранить FW, до/после PID и транспорт. Не отправлять команду автоматически. [4PDA topic](https://4pda.to/forum/index.php?showtopic=1057776).

<!-- 4pda-field-2026-10-15:end -->


<!-- upstream-field-notes-v3.5.2:start -->
## Field incidents — archived field notes

**External field reports / source comments, not MedveModem bench verification.** Pin `c2bd09cbef546c12b4fdd80d105993eaa3fb569b` (2026-10-15). No recipe below is enabled in runtime profiles.

### fm350-radio-dead-after-cfun

**Observation:** FM350 retained AT replies but CSQ=99,99 and radio stayed dead after CFUN=1,1; only physical USB power-cycle revived it.

- **Scope:** models=FM350-GL; usb_ids=0e8d:7126, 0e8d:7127
- **Type / confidence:** `recovery-hazard` / `external-field-report`
- **Regression contracts:** live AT does not imply healthy radio; no blind repeated CFUN for CSQ=99,99
- **Safety:** USB hard power only after port capability proven, external power and operator approval.
- **Original source:** issue / release / pinned source

### Composition and boot-mode recipes (reference only)

Exact operation stages, exit checks and prerequisites: [composition matrix](../COMPOSITION-AND-BOOT-MODES.md).

- **`fm350-usb-gtusbmode-40-41`** — `COMPOSITION_MUTATION`; USB FM350/RW350, MediaTek control, observed 0e8d:7126/7127 → USB RNDIS (not PCIe MBIM); explicit approval required; Upstream 7127 profile states +GTUSBMODE: 41; not enough evidence for a universal numeric mode switch table.

<!-- upstream-field-notes-v3.5.2:end -->


<!-- external-another package-modem5g-2026-10-16:start -->
## another package / another package — external field facts (2026-10-16)

**Reference only; not bench-validated.** Checked against the pinned upstream files and the issue/PR pages; no upstream code, no runtime preset. All cases: another package / another package field notes.

- **`another package-gtusbmode-mediatek-read-map`** (`upstream-code-reference`, upstream-code-reference): another package's Fibocom handler interprets the AT+GTUSBMODE? answer per chipset platform. For MediaTek it reads 29 as MBIM and 40/41 as RNDIS; its setter sends 41 for every requested mode on MediaTek. This agrees with the Zoo's existing 40/41 = RNDIS reading for USB FM350 and adds a 29 = MBIM label that no other source in the Zoo has confirmed for USB. Source: another package/5G-Modem-Support luci-app-modem/root/usr/share/modem/fibocom.sh.
- **`fm350-pve-usb-passthrough-drops-issue45`** (`field-reported`, external-field-report): Frequent disconnects of an FM350-GL passed through to an OpenWrt VM. The reporter later traced the drops to the VM's USB passthrough repeatedly losing the device, not to the modem driver; on bare Windows the link was steadier but still dropped occasionally. The reporter gave up on the modem. Source: another package/5G-Modem-Support issue #45 (closed 2025-12-23).
- **`fm350-no-vendor-qmi-driver-issue50`** (`field-reported`, external-maintainer-statement): The another package maintainer states that the FM350-GL needs neither the Quectel connection manager nor the Fibocom kmod-qmi_wwan_f driver: quectel-CM is for Quectel/Qualcomm modules, and qmi_wwan_f is needed only by Fibocom modules with Qualcomm chips. The reporter's FM350 worked without them. Source: another package/5G-Modem-Support issue #50 (closed 2026-06-13).
- **`fm350-usb-at-dial-manual-address-issue37`** (`field-reported`, external-field-report): On OpenWrt 24.10.1 a user brought an FM350-GL up over USB by dialing with AT commands, reading the address the modem reported and typing it into the interface by hand each time; there was no automation for that last step. Source: another package/5G-Modem-Support issue #37 (closed 2025-05-08).

<!-- external-another package-modem5g-2026-10-16:end -->


<!-- field-delta-v3.5.16:start -->
## Дополнительные полевые наблюдения (2026-10-21)

**Reference only; not bench-validated.** Read from the diff v3.5.5..v3.5.16 at `a22590c108cc`; no upstream code, no runtime preset. Details: archived field observations.

- **`…-v3515-fm350-sms-send-iface6`** (`0e8d:7127`) — `upstream-code-reference`, not-bench-validated: On their FM350 unit AT answers on USB interfaces 03 and 06 (their comment calls ttyUSB1 and ttyUSB3 the real modem ports, from a live bench on 2026-08-03); interface 02 answers a bare AT but is not a modem port. Metrics sit on the first port that returns the model to AT+CGMM (03 for them), but SMS sending works only on interface 06. v3.5.15 pins the SMS send port by interface number read from sysfs, not by ttyUSB index, and the send page re-reads the config before every send so a tty renumbered after a reconnect is not used. Source: архивные полевые сведения @ `a22590c` (root/usr/share/archived modem utility/quirks.sh:337-358, root/usr/share/archived modem utility/ensureports.sh:113-117, htdocs/luci-static/resources/view/modem5g/sendsms.js:248-254, root/usr/share/archived modem utility/mkiface.sh:586-591, root/usr/share/archived modem utility/detect.sh:209-215).
  - The send-port lookup knows exactly one pair, 0e8d:7127 -&gt; bInterfaceNumber 06; it walks /sys/class/tty/ttyUSB*, resolves each tty's device link to '&lt;usb path&gt; &lt;interface number&gt;' and returns the node only if it is a character device.
  - Port detection is two-pass: first a port that answers AT+CGMM with a model, then any port that answers AT; picking the first port that merely answered AT (interface 02) made their AT-dial proto fail with 'AT port not answer!' in a loop.
  - Only the SMS send key is written; the read, USSD and AT-console ports are left as they were.
  - MedveModem's own bench (2026-10-19, firmware 81600.0000.00.29.24.02) saw AT only on interface 06, not on 03; the difference is unexplained (firmware is the likely cause), so interface 03 is not added without our own measurement.
- **`…-v3516-fm350-ussd-silent-cpms-short-form`** (`0e8d:7127`, `0e8d:7126`) — `upstream-code-reference`, not-bench-validated: Two FM350 quirks recorded in their code: USSD never answers - AT+CUSD=? lists (0-2), but AT+CUSD=1,... returns neither OK, ERROR nor a +CUSD URC on either AT port, so they report USSD as unsupported for 0e8d:7127/7126; and the short AT+CPMS form (one storage) resets mem2/mem3 to SM, so after asking storage capacity they always apply the full three-storage form. Source: архивные полевые сведения @ `a22590c` (root/usr/share/archived modem utility/quirks.sh:38-43, root/usr/share/archived modem utility/quirks.sh:98-107, root/usr/share/archived modem utility/lib.sh:2020-2025).
  - Their USSD matrix: DCS 15, 0 and none; plain text, packed GSM7 and UCS2 hex; TE character sets IRA/GSM/UCS2; LTE and 3G with live CS; a listener on all seven ports - always OK-or-silence, while the same SIMs read USSD on a Compal module.
  - Their reading: the module is data-only and its firmware has no supplementary services; it offers only two USB compositions, both RNDIS (AT+GTUSBMODE=? -&gt; (40,41)), so there is no MBIM path under ModemManager either.
  - Periodic polling must never use the short CPMS form on FM350; a capacity query with it has to be followed at once by the full AT+CPMS="X","X","X".

<!-- field-delta-v3.5.16:end -->
