# Modem Zoo — Telegram field notes

> Field evidence from two user-provided public Telegram exports dated 2026-10-01.
> No raw chat bodies are mirrored here. A field report is not MedveModem hardware verification.

The exports contain 34,739 messages. Automated indexing selected 1,090 technically relevant
messages for search/provenance; only the observations below were promoted into the curated Zoo layer.

## Confidence model

- live-diagnostic / live-kernel-log — captured device or kernel state;
- developer-field-report — explicit maintainer/developer observation on real hardware;
- multi-report / multi-device — repeated observations, still scoped by firmware/hardware;
- community-recipe — useful field method requiring independent validation;
- contradictory — evidence disagrees; Zoo keeps both sides;
- externally-corroborated — chat observation independently matched to upstream/vendor material.

## Privacy / provenance

- ordinary participants are not named in curated evidence;
- public project handles may be retained when technically relevant;
- message id/date/attachment are kept as provenance pointers;
- opaque firmware/calibration binaries from chats are not copied into the repository.

## Cross-family architecture lessons

### owner-conflict-qmi-proto-vs-mm

**Confidence:** `developer-interpretation-with-counterexample` · **Risk:** `operational`

One other scripts attributes a broken image to concurrent qmi-proto and ModemManager ownership, while a later report notes a router maker's firmware can use qmicli, MM and COM ports together when roles are coordinated.

**Details:**
- Best interpretation: duplicate network/control ownership is the problem, not the mere presence of qmicli and MM.

**MedveModem implications:**
- Arbitration must be per resource/operation, not per executable name.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 940556, 2025-12-05, other scripts
- tg-lte-modemy-2026-10-01 msg 1420878, 2026-06-12, other scripts

### mmcli-ussd-qmi-field-test

**Confidence:** `community-multi-model-test` · **Risk:** `reference`

A community script reports working USSD through ModemManager mmcli 3GPP USSD methods on EP06, T77W968 and T99W175 in QMI setups.

**Details:**
- The literal script uses mmcli -m any, unsuitable for MedveModem multi-modem semantics; the useful evidence is the MM-owned USSD path.

**MedveModem implications:**
- Prefer owner API when MM owns the modem rather than opening raw AT concurrently.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 1340412, 2026-05-06, ITDog Chat

### other scripts

**Confidence:** `attached-package-artifact` · **Risk:** `operational`

Attached other scripts packages show an explicit evolution from board-gated reauthorization of every root USB bus in 1.0-r3/r4 to VID:PID-scoped reauthorization of recognized modem devices in 1.2-r1/r2.

**Details:**
- 1.0-r3/r4 generated fix-usb-reinit on selected router boards and toggled authorized on every /sys/bus/usb/devices/usb* root bus.
- 1.2-r1/r2 added modem.list and fix-modem-reinit, iterating physical USB devices and reauthorizing only matching modem VID:PID entries.
- The 1.2-r1 announcement explicitly says reinit should become router-model independent and target the modem itself.
- The hotplug package still starts/stops smstools3 globally around supported modem add/remove events, exposing the single-global-SMS-owner heritage.

**MedveModem implications:**
- Recovery should be device-scoped by resolved attachment identity; root-hub-wide reset is an escalation hazard.
- External SMS/process ownership must be per modem/resource rather than global when true multi-modem support is required.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 530883, 2025-06-26, attachment other scripts.0-r3_all.ipk
- tg-lte-modemy-2026-10-01 msg 537970, 2025-06-29, attachment other scripts.0-r4_all.ipk, other scripts
- tg-lte-modemy-2026-10-01 msg 559916, 2025-07-08, attachment other scripts.2-r1_all.ipk, other scripts
- tg-lte-modemy-2026-10-01 msg 671461, 2025-08-25, attachment other scripts.2-r2_all.ipk, other scripts

### Archived 1.0.2 modem package architecture

**Confidence:** `attached-package-artifact` · **Risk:** `reference`

An archived modem-management IPK (version 1.0.2) supplied with a router firmware provides direct package-level evidence of the historical stack: USB hotplug dispatch, fixed model port tables, one global network.modem interface, mmcli -m any discovery, companion modeminfo/mmconfig/atinout/smstools3 configuration and board-specific broad USB reinit.

**Details:**
- Package control metadata identifies the historical 1.0.2 integration stack; private source-tree paths are omitted from this public overview.
- Its hotplug guard avoids launching a second instance of the modem-management process while one is running.
- The package removes ModemManager's tty hotplug hook, configures one global network.modem, and carries per-model historical tty port tables.
- Its board-specific fix-usb-reinit toggles authorization on all root USB buses on a small router whitelist.

**MedveModem implications:**
- This is direct provenance for other scripts/a router maker's firmware orchestration rather than evidence that modeminfo/mmconfig/atinout/smstools3 themselves were authored by other scripts.
- MedveModem should keep event coalescing/rescan, explicit device_id and attachment-scoped recovery instead of importing the single-global-modem assumptions.

**Evidence:**
- tg-modemy-2026-10-01 msg 195423, 2025-06-23, archived 1.0.2 IPK attachment, a router maker's firmware

## Foxconn T99W175 / SDX55

### t99-9025-live-qmi-mm

**Confidence:** `live-diagnostic` · **Risk:** `read`

A live T99W175 snapshot at 05c6:9025 shows cdc-wdm0, ttyUSB0..3, qmi_wwan, ModemManager connected state and DELL/Snapdragon X55 identity on F0.6.0.0.6.GC.004.

**Details:**
- Concrete evidence that QMI data/control and MM device management can coexist in a coordinated stack.

**MedveModem implications:**
- Resolve owner/protocol from bound driver + MM object + physical topology.

**Evidence:**
- tg-modemy-2026-10-01 msg 891880, 2026-09-08

### t99-00b7-sms-multi-device

**Confidence:** `multi-device-developer-field-report` · **Risk:** `reference`

other scripts roughly ten T99W175 units operated in 00B7 with SMS working on all of them.

**Details:**
- Stronger than a single anecdote but not every firmware/customer combination is identified.

**MedveModem implications:**
- Treat 00B7 as a strong SMS-capable candidate, not an unconditional capability.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 1379725, 2026-05-22, other scripts

### t99-composition-port-variance

**Confidence:** `contradictory-field-reports` · **Risk:** `high-mutating-reenumeration`

T99 composition labels do not guarantee the same serial behavior across firmware: one report had 00B3 with usable ttyUSB and 90D5 without serial ports; another F0.6.0.0.6 unit gained 00B7 functions but AT ports remained unresponsive.

**Details:**
- The second case used imported composition artifacts, so mismatch is plausible.

**MedveModem implications:**
- After composition change verify endpoints and a proven AT response instead of trusting the label.

**Evidence:**
- tg-modemy-2026-10-01 msg 45948, 2024-12-05
- tg-modemy-2026-10-01 msg 512881, 2026-02-26

### t99-single-at-port-contention

**Confidence:** `developer-diagnosis-with-live-repro` · **Risk:** `operational`

A live 05c6:9025 session shows interleaved/corrupted AT replies when atinout shares the T99W175 command port with other consumers; a router maker's firmware attributes this to the family exposing one COM/AT path and concurrent readers.

**Details:**
- Repeated AT^CA_INFO? requests returned echoes/fragments of other commands, intermittent ERROR, and eventually correct CA output.
- a router maker's firmware recommends using the stack's serialized sms_tool path instead of raw concurrent atinout access.

**MedveModem implications:**
- T99 status, SMS, diagnostics and Expert AT must share one broker/lease rather than open the tty independently.
- A syntactically corrupted reply is evidence of ownership/contention, not necessarily modem firmware failure.

**Evidence:**
- tg-modemy-2026-10-01 msg 879080, 2026-09-01
- tg-modemy-2026-10-01 msg 879486, 2026-09-01, a router maker's firmware

### t99-cpms-post-flash-sms-kick

**Confidence:** `repeated-developer-field-report` · **Risk:** `mutating-session`

a router maker's firmware documents a firmware-scoped T99W175 SMS quirk: from at least F0.6.0.0.6, briefly selecting SIM storage with CPMS=SM and returning to ME makes smstools3 SMS reception work persistently until the next firmware update.

**Details:**
- smstools3 continuously selects ME storage, so the field recipe only needs a one-shot AT+CPMS="SM","SM","SM"; the daemon returns storage to ME.
- The report says the effect survives reboot, power loss, composition changes and customer changes, but should remain scoped to tested firmware.

**MedveModem implications:**
- Represent this as a one-shot, firmware-scoped SMS backend initialization with verification, not a generic periodic AT preset.
- CPMS changes must use the same AT lease as the SMS daemon.

**Evidence:**
- tg-modemy-2026-10-01 msg 256020, 2025-09-01, a router maker's firmware
- tg-modemy-2026-10-01 msg 256216, 2025-09-02, a router maker's firmware

## Foxconn T77W968 / DW5821e

### t77-f1052-gc-rfnv-recipe

**Confidence:** `developer-community-recipe` · **Risk:** `destructive`

A a router maker's firmware recipe scoped to T77W968 F1.0.0.5.2 documents GC profile selection, SBFS policy handling and replacing active RFNV 00028874 with stock 2-antenna/4-antenna variants.

**Details:**
- MCFG? reads current profile and MCFG=? lists profile IDs; the GC numeric selector must be discovered on-device.
- fx.zip contains 00028874_2ant and 00028874_4ant reference blobs; Zoo records existence but does not mirror calibration binaries.

**MedveModem implications:**
- RFNV manipulation is recovery/expert knowledge, never automatic init.

**Evidence:**
- tg-modemy-2026-10-01 msg 176593, 2025-06-01, attachment fx.zip, a router maker's firmware
- tg-lte-modemy-2026-10-01 msg 563905, 2025-07-09, other scripts
- tg-lte-modemy-2026-10-01 msg 565030, 2025-07-09, other scripts

### t77-hotplug-resource-ownership

**Confidence:** `developer-field-description` · **Risk:** `operational`

other scripts hotplug logic that reauthorizes USB, binds serial functions, stops/restarts smstools3 around replug, and changes T77 interface binding by customer mode.

**Details:**
- smstools3 restart is explicitly used to avoid an unreleased COM port.
- Customer 2 may unbind extra cdc-wdm and bind it as serial; customer 1 may be shifted to QMI for ModemManager.

**MedveModem implications:**
- Serial consumers need central lease/release around re-enumeration.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 620280, 2025-08-02, other scripts

### t77-live-mbim-to-qmi-transition

**Confidence:** `live-kernel-log` · **Risk:** `operational`

A captured 413c:81d7 boot trace shows the same T77W968 first bound as cdc_mbim and then rebound as qmi_wwan while ttyUSB0..3 were recreated.

**Details:**
- cdc_mbim first exposes cdc-wdm0/wwan0; a few seconds later it unregisters, qmi_wwan binds the same control/data role, and serial endpoints return.
- historical integration stack recognizes t77w968 after the transition and ModemManager receives add/remove/add events.

**MedveModem implications:**
- Live driver/control plane outranks PID/customer lore.
- Transport switch must invalidate stale endpoints and advance attachment generation.

**Evidence:**
- tg-modemy-2026-10-01 msg 276503, 2025-09-22, attachment syslog.log.txt
- tg-modemy-2026-10-01 msg 169516, 2025-05-21, a router maker's firmware

### t77-customer2-setmode2-serial

**Confidence:** `multi-report` · **Risk:** `high-mutating-reenumeration`

Multiple reports describe T77 customer 2 with setmode 0 hiding serial ports while setmode 2 exposes serial functions on some USB setups.

**Details:**
- External USB customer2/setmode2 worked with other scripts as 1bc7:1910 in one report, while an internal M.2 path appeared as 1bc7:9905 and still lacked normal MM visibility.

**MedveModem implications:**
- Customer/setmode behavior is board/bus/firmware-specific, not a universal composition table.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 615630, 2025-07-31
- tg-lte-modemy-2026-10-01 msg 615728, 2025-07-31
- tg-lte-modemy-2026-10-01 msg 944457, 2025-12-06
- tg-lte-modemy-2026-10-01 msg 876356, 2025-11-14, other scripts

### t77-f2-two-antenna-uncertain

**Confidence:** `anecdotal-experimental` · **Risk:** `high`

A two-antenna T77W968 with F2.0xx firmware was reported with identical active, 2-ant and 4-ant 00028874 blobs and only 2CC observed; downgrade and RFNV replacement effectiveness were explicitly uncertain.

**Details:**
- The source itself marks the downgrade failure and possible replacement path as uncertain.

**MedveModem implications:**
- Investigation lead only, not a supported recipe.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 1433681, 2026-06-18, other scripts

### t77-reset-network-interface-loss

**Confidence:** `single-field-repro` · **Risk:** `operational`

A T77W968 field report reproduced loss of the network interface after AT^RESET while modem telemetry still showed a registered cell.

**Details:**
- The report began with a working interface that later disappeared; manually issuing AT^RESET reproduced modem reboot followed by the missing network interface.
- This is one device/report and does not establish a family-wide firmware defect.

**MedveModem implications:**
- Reset success is not equivalent to session recovery: after re-enumeration re-resolve WDM/netdev/owner and require post-reset network success criteria.
- UI radio telemetry must not be used as proof that the data plane is restored.

**Evidence:**
- tg-modemy-2026-10-01 msg 813600, 2026-07-20

## Fibocom FM350 / RW350

### fm350-gtusbmode-dual-composition

**Confidence:** `developer-field-report` · **Risk:** `operational`

a router maker's firmware test hardware on firmware 8C4A103C HP 29.24.02 5052 was observed in GTUSBMODE 40/41 with VID:PID 0e8d:7127/7126 and one shared ACM port serving control/AT/SMS in each composition.

**Details:**
- Historical hints: 7126 -> ttyACM1; 7127 -> ttyACM3.
- The same physical command port serving telemetry and SMS creates a real contention case; tty numbers remain attachment-specific.

**MedveModem implications:**
- Use a central port lease/broker.
- Composition change must invalidate role mappings.

**Evidence:**
- tg-modemy-2026-10-01 msg 161431, 2025-05-13, a router maker's firmware

### fm350-ussd-negative-field

**Confidence:** `repeated-developer-field-report` · **Risk:** `reference`

a router maker's firmware repeatedly reported that USSD could not be made to work on tested FM350 units, independently of the a router maker's firmware UI stack.

**Details:**
- Repeated six days apart; still firmware/test-scope evidence, not a vendor-wide specification.

**MedveModem implications:**
- Do not infer working USSD from AT+CUSD test capability alone.

**Evidence:**
- tg-modemy-2026-10-01 msg 170192, 2025-05-22, a router maker's firmware
- tg-modemy-2026-10-01 msg 173638, 2025-05-28, a router maker's firmware

### fm350-fcc-challenge-response

**Confidence:** `multi-source-reference` · **Risk:** `high-mutating-persistent`

The chats contain the same FM350 FCC challenge/response flow independently implemented by other scripts: GTFCCLOCKGEN challenge, derived response, then GTFCCLOCKVER.

**Details:**
- Telegram gives vendor hash 3df8c719 and SHA-256 based derivation.
- A follow-up field message gives AT+GTFCCLOCKMODE=0 as a persistent FCC-lock disable operation followed by modem reboot/replug; Zoo classifies this separately from the ordinary challenge/response unlock.

**MedveModem implications:**
- Model FCC unlock as an owner-aware semantic capability, never background polling.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 1070102, 2026-01-29
- tg-lte-modemy-2026-10-01 msg 1070113, 2026-01-29
- other scripts: create_hostless.sh / GTFCCLOCKGEN + GTFCCLOCKVER

### fm350-rxpath-antenna-scan

**Confidence:** `experimental-community-recipe` · **Risk:** `session-affecting`

A shared proof-of-concept measures FM350 receive paths separately by changing GTRXPATHEN masks and reading CESQ/GTCCINFO.

**Details:**
- Masks 1/2/4/8 select individual RX paths; 15 restores all paths.
- The script uses an EXIT/INT/TERM trap to restore 15.
- This actively disables other RX paths and is not passive telemetry.

**MedveModem implications:**
- Expert-only diagnostic behind an exclusive lease with guaranteed restore/finally.

**Evidence:**
- tg-modemy-2026-10-01 msg 810694, 2026-07-18, attachment fm350_antenna_scan.sh

### fm350-hspa-cesq-parser

**Confidence:** `community-patch-reviewed-by-maintainer` · **Risk:** `read`

A community modeminfo patch adds FM350 HSPA parsing from CESQ; a router maker's firmware later published a modified version and requested field verification.

**Details:**
- Reviewed formulas: RSCP = raw - 120; Ec/No = raw/2 - 24.
- The parser explicitly prefers CESQ over CSQ for this path.

**MedveModem implications:**
- Keep signal formulas RAT/firmware scoped and fixture-test with captured responses.

**Evidence:**
- tg-modemy-2026-10-01 msg 819585, 2026-07-24, attachment FM350.txt.txt
- tg-modemy-2026-10-01 msg 819626, 2026-07-24, attachment FM350_v2.txt
- tg-modemy-2026-10-01 msg 820158, 2026-07-25, attachment INTEL_FM350, a router maker's firmware

## Fibocom L850 / L860 (Intel XMM)

### xmm-reenumeration-retry-is-normal

**Confidence:** `developer-report-plus-live-logs` · **Risk:** `operational`

Fibocom XMM initialization may cause several disconnect/reconnect cycles; historical integration stack treats each appearance as another setup opportunity rather than a new physical modem.

**Details:**
- Captured L850 logs show repeated setup, cdc_ncm unregister/register, ttyACM recreation and netifd retries before connectivity.

**MedveModem implications:**
- Uevents are hints; topology/sysfs identity plus attachment generation define truth.
- Debounce/rescan must tolerate event storms and transient endpoint loss.

**Evidence:**
- tg-modemy-2026-10-01 msg 213195, 2025-07-18, a router maker's firmware
- tg-modemy-2026-10-01 msg 212984, 2025-07-17, attachment laatlog.txt
- tg-modemy-2026-10-01 msg 451738, 2026-01-23, attachment log.txt
- tg-modemy-2026-10-01 msg 771607, 2026-06-22, attachment syslog (1).log (2).txt
- tg-modemy-2026-10-01 msg 876075, 2026-08-30, attachment syslog.log (1).txt

### xmm-8087-095a-live-topology

**Confidence:** `live-diagnostic` · **Risk:** `read`

A live 8087:095a diagnostic shows ttyACM0/1/2 and three CDC-NCM network functions, with wwan0 active and wwan1/wwan2 down.

**Details:**
- Only wwan0 had link/IP in the capture.

**MedveModem implications:**
- Do not treat every network function as an independent usable session.

**Evidence:**
- tg-modemy-2026-10-01 msg 732799, 2026-06-04, attachment modem-diag-20260604-020034.txt

### xmm-primary-lock-pci-wildcard

**Confidence:** `community-multi-day-field-recipe` · **Risk:** `high-mutating`

An L860-GL-16 report found that locking B7/EARFCN with a literal PCI left only the primary carrier and no usable internet, while PCI 65535 kept B7 primary and allowed secondary carriers to return; the state was observed stable for three days.

**Details:**
- Reported working form: at@sic:freq_lock(0,3,107,1,3048,65535).
- The same EARFCN with PCI 361 kept the primary but did not restore the expected secondary aggregation in that setup.
- The follow-up says to complete the lock with AT+CFUN=15 and describes enable=0 as the unlock form.

**MedveModem implications:**
- Treat PCI 65535 as field-observed wildcard behavior, not a universal Intel specification.
- Cell-lock jobs need post-lock attach/traffic/CA validation and explicit rollback; command success alone is insufficient.

**Evidence:**
- tg-modemy-2026-10-01 msg 85237, 2025-02-04
- tg-modemy-2026-10-01 msg 85340, 2025-02-04

## Quectel RM520N / RM5xx

### rm520-glap-glaa-bus-split

**Confidence:** `externally-corroborated-field-report` · **Risk:** `reference`

Telegram distinguishes RM520NGLAP-M20-SGASA as PCIe-only and RM520NGLAA-M20-SGASA as USB/PCIe-capable; Quectel forum support independently confirms GLAP PCIe-only/eFuse variants cannot be converted to USB by AT command.

**Details:**
- Do not infer USB capability from the RM520N-GL marketing name alone.

**MedveModem implications:**
- Exact ordering code belongs in identity/capability evidence.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 857941, 2025-11-07
- quectel-forum-rm520: Modify RM520NGLAP to have USB; RM520N-GL with PCIe interface only

### rm520-rus-irn-firmware-restriction

**Confidence:** `externally-corroborated` · **Risk:** `high`

Telegram warns about regional registration restrictions on newer RM520N-GL firmware; a Quectel forum post reproduces R01A07M4G release-note language restricting normal registration in RUS and IRN and warning against downgrade.

**Details:**
- Scope this to affected firmware such as RM520NGLAAR01A07M4G, not the whole family.
- Zoo does not promote community bypass recipes.

**MedveModem implications:**
- Firmware version is part of operational identity and regional compatibility.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 857941, 2025-11-07
- tg-lte-modemy-2026-10-01 msg 857973, 2025-11-07
- quectel-forum-rm520: RM520N-GL sim-card can't register / R01A07M4G release-note excerpt

### rm520-qnwlock-test-only

**Confidence:** `vendor-forum-reference` · **Risk:** `experimental-mutating`

Quectel support states QNWLOCK common/5g does not support NSA cell locking and repeatedly describes QNWLOCK as a test command not recommended for commercial use.

**Details:**
- This reinforces model/firmware-scoped, experimental classification.

**MedveModem implications:**
- A semantic cell-lock feature needs limitation disclosure and post-lock attach validation.

**Evidence:**
- quectel-forum-rm520: RM520N - Cell Tower Lock; RM520N-GL NSA Cell Locking

## Quectel EC25 / EP06 / EM12

### ep06-ussd-path-contradiction

**Confidence:** `contradictory` · **Risk:** `reference`

The Telegram claim that USSD works through ModemManager on an unspecified EP06 conflicts with existing EP06-E/EG06-E raw-AT field evidence where CUSD yields no usable network response.

**Details:**
- Telegram does not specify EP06 regional variant or firmware.
- Difference may be variant, firmware, RAT/network or control path; current evidence cannot decide.

**MedveModem implications:**
- Represent USSD as variant/control-path evidence, not one family-wide boolean.

**Evidence:**
- tg-lte-modemy-2026-10-01 msg 1340412, 2026-05-06, ITDog Chat
- a field report-historical integration stack: quirks.sh EP06-E/EG06-E negative USSD evidence

## What this changes architecturally

The strongest repeated lesson is not a new AT command. It is that modem state is eventually consistent:
USB functions re-enumerate, protocol bindings can change, command ports are shared, and external owners
such as ModemManager/netifd/SMS daemons can hold resources at the same time. The Zoo therefore keeps
field observations as evidence for identity generations, owner arbitration, leases, worker timeouts and
post-action verification instead of converting chat recipes directly into automatic init logic.
