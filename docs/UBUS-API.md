# ubus API for other interfaces (read-only first)

Checked against the engine of 0.15.0-r197 (16.11.2026): every method it registers is named here
(`tests/test_docs_contract.py`).

The contract an interface other than luci-app-medvemodem (for example the
MedveWRT mobile UI) may rely on. Checked against `src/medvemodemd.c` and real
answers of engine 0.14.0-r17 (test harness with ModemManager stand-in). The
bench has not yet confirmed every field on every modem: a field may be absent
or `null` on real hardware, and the interface must show «нет данных», not fail.

## Rules

- Object `medvemodem` on the system ubus. Called from LuCI JS with
  `rpc.declare({ object: 'medvemodem', method: … })` through rpcd; the
  interface ships its own rpcd ACL (below).
- **Detection.** Call `status`. ubus `NOT_FOUND` (code 4) means the object is
  not registered: MedveModem not installed or not running («MedveModem не
  установлен или не запущен», with `/etc/init.d/medvemodemd start`).
  `PERMISSION_DENIED` (code 6) is the interface's own ACL missing a method:
  a different message, never «не установлен».
- **Compatibility.** `status.version` is the engine version; `device.schema`
  is 1. Fields are only added; an interface ignores fields it does not know
  and treats a missing field as unknown. A rename or removal bumps `schema`.
- **Identifiers.** The IMEI (`modemmanager.imei`), the module's USB serial
  (`serial`, part of `device_id`), the SIM's IMSI and ICCID
  (`modemmanager.sim_card.imsi`, `.iccid`) and its own number
  (`modemmanager.own_numbers`) are served and may be shown (decisions of
  2026-10-09, as colleagues do). The engine never logs them or keeps them on
  flash; diagnostic bundles (`medvemodem diag`) redact them, because they are
  meant to be shared. An interface should not log them either.
- **One owner.** The engine (or ModemManager under it) owns the modem's ports.
  Nothing else may open its AT or QMI ports beside it: an AT command next to
  ModemManager drops the data session on several modems (T77W968, T99W175).
  A legacy adapter (modeminfo, atinout, sms_tool) must stay off
  while `medvemodem` answers; it is not a fallback that runs in parallel.
- **Polling.** The read methods below answer from the engine's memory and
  cause no modem traffic. `device` every 2–5 s is fine. Cells and antennas
  are refreshed by the engine every 30 s at most (`modemmanager.radio_at`
  says when); polling faster shows the same values.

## Read methods (safe for a read-only interface)

| Method | Arguments | Answer |
|---|---|---|
| `status` | — | `{version, uptime, devices, passes, last_pass, modemmanager: {backend, age_s, modems, unmapped, conflicts, restart}, …}`; `restart` is null or {state running/done/failed, at, finished_at, next_allowed_s}; `backend`: `available` / `unavailable` / `disabled` / `timeout` / `error` / `unknown`; `load`: {modemmanager: {runs, runs_1m, runs_10m, cells_runs, killed, last_ms, max_ms, avg_ms, running}, at: {jobs, killed}, usb: {adds, removes, resets, power_cycles}, recoveries} since the engine started (USB adds/removes are whole devices, not interfaces); `medvemodem diag profile` puts it in `engine.load` with `uptime_s`; `usb_class` (r179): `{suspects: [{usb, vidpid, manufacturer, product, why: ports/vendor}], modem: [vid:pid], not_modem: [vid:pid]}` — devices the last pass dropped that look like a modem (ports of their own, or a modem maker's ID), and the operator's lists |
| `devices` | `absent` (bool, optional: include unplugged modems) | `{devices: [device, …]}` — same records as `device` |
| `device` | `id` (device_id) | one device record (below); `NOT_FOUND` for an unknown id |
| `uplink` | — | `{internet: available / limited / unavailable / checking, dns_ok (only when false), carrier: {device, device_id, name} \| null, backup_ready, default_device, paths: [{device_id, name, device, role, reach, rx_rate, tx_rate}]}` |
| `events` | `id` (required: INVALID_ARGUMENT without it, NOT_FOUND for a device the registry does not know), `limit` (optional) | `{events: [{t, m, kind, detail}]}`, newest first, at most 64 per device (older ones drop out; kept in RAM across a daemon restart within one boot); `t` wall clock, `m` monotonic; `kind` e.g. `usb-add`, `mm-add`, `session-up`, `session-down`, `recovery-start`, `recovery-end`, `config` |
| `recovery` | `id` | the device's `recovery` block (below) |
| `lookup` | `node` (`/dev/cdc-wdm0`, `wwan0`, `ttyUSB2`) | which device owns the node |
| `leases`, `config_state` | — / `id` | port leases; configuration transaction state |
| `sms_list` | `id`, `refresh` (bool), `peek` (bool) | `{device_id, path: modemmanager, read_at, messages: [{id, number, text, state, type deliver/submit, storage, timestamp, epoch, class, delivery_state, reference, binary}], unread_ids?: [id received and not seen before this answer], error?}` or `{refused}`; from the engine's cache (read in the background every `core.sms_poll` s, and when older than 15 s); `refresh` waits for a fresh read; `peek` marks nothing seen (the bot, the dashboard's «Все SMS») |
| `presets` | `id` | `{at: {groups, sets: [{id, title, source, presets: [{group, title, command, hazard, allowed, why?}]}]}, at_owner, plmn, ussd: {name, plmn, codes: [{title, code, source, checked}]} \| null}` — AT console presets that fit the modem (data in `/usr/share/medvemodem/presets`), marked by the hazard list; the USSD codes of the SIM's operator |
| `ussd_status` | `id` | `{state: idle / running / done / failed / cancelled, request, code, reply, error, session (MM's: idle / active / user-response), started_at, finished_at}` |

Not for polling: `metrics` (`id`, `max_age_ms`) answers from ModemManager when
it owns the modem, but sends AT commands when the engine itself owns the AT
port; `esim_status` talks to the eUICC (seconds). Use them on demand only.
`metrics` with `kept: true` (r151) answers only from what the engine already read, whatever its age, and refuses
(«no reading of the modem kept yet») rather than ask a port: the modem report uses it.

## Device record: what the cards need

| Card | Fields (`device` …) |
|---|---|
| List and switch | `device_id`, `present`, `product`, `manufacturer`, `modemmanager.model`; the user's name `name` (UCI `medvemodem.@modem[].name` matched by `device_id`); `name_follows` serial / imei / port: what keeps the name and the modem's settings with it in another USB port (HANDOFF 167); `connection.usb_config` {count, current, wanted?, state kept/set/waiting/refused/failed, why} for a module with more than one USB configuration (UCI `config device` / `option usb_config`, HANDOFF 168) |
| Registration and operator | `modemmanager.registration` (`home`, `roaming`, `searching`, `denied`, `idle`), `operator`, `operator_code` (PLMN), `access_tech` ([`lte`, `5gnr`, …]), `location` {mcc, mnc, tac, cid} |
| Registration refused (r183) | `connection.registration_denied` {since, at, retry_in_s}: the network denied the registration and none came since; kept while ModemManager holds the modem disabled after the failed connect, the engine's retry waits 30 min between tries |
| SIM | `modemmanager.unlock_required` (`sim-pin`, `sim-puk`, null), `sim` (object path or null = no SIM), `sim_card` {operator_code, operator_name, gid1, imsi, iccid}, `own_numbers` [..]; the operator's label `sim_label` {number, description} (top level of the device record, from UCI `config sim` / `option iccid`) |
| Signal | `modemmanager.signal_quality` (%), `signal.lte` {rsrp, rsrq, rssi, snr}, `signal.5g` {rsrp, rsrq, snr} (also `umts` {rscp, ecio}, `gsm` {rssi}) — from `--signal-get`, refreshed every 30 s |
| Radio settings | `radio_settings` {via `modemmanager`/`qmodem`/`at`/`none`, dialect, modes {allowed [2g,3g,4g,5g], preferred}, supported_modes, bands, supported_bands [`eutran-3`, `ngran-78`], bands_any (no restriction), age_s (a QModem / AT reading), writable false, why} — the same block whoever drives the modem (spec 25.9.12); read only for now. QModem's and the AT readers answer on request: `radio` below |
| Radio | `modemmanager.cells` [{type lte/5gnr, serving, role primary/secondary/scg-primary/scg-secondary/neighbour, earfcn \| nrarfcn, band, pci, rsrp, rsrq, sinr, bandwidth (kHz from QMI), tac, ci, plmn}], `current_bands` ([`eutran-3`, `ngran-78`]: check a computed band against them), `antennas` {lte \| nr: {chains [{chain, tuned, rsrp, power, ecio, phase}], tx {traffic, power}}, why}, `radio_at` |
| Device | `modemmanager.manufacturer`, `model`, `firmware`, `imei`; `vidpid`, `profile.id` (expert) |
| Backend and owner | `owner` {control, session, at}: `modemmanager` / `qmodem` / `native` / `none` / `unresolved` (profile owners such as `vendor` may appear: show the word as is); `profile.prefer_owner`; `status.modemmanager.backend` |
| APN by the SIM | `connection.apn_auto` true when `config device` / `option apn_auto '1'`; then `connection.apn` is only the typed APN and `apn_in_use` the interface's. A typed APN wins; an empty one makes the engine write `connection.apn_suggestion` (APN, login, auth `pap-or-chap` → `both`), and again after a SIM swap while the session is down (journal `apn-auto`) |
| Last AT dial | `connection.dial` {t, result connected/failed, steps [...], pdp?, reused?, error?} — the engine's last AT dial of this modem (FM350 without QModem, `dial_at.c`); no address; RAM only |
| Fixed TTL | `connection.ttl` {value, state on/applying/waiting/no-egress/failed, device} — only when `config device` / `option ttl` is set (spec 25.9.17) |
| Connection | `session` {state: connected/connecting/disconnected/failed, interface, proto, netifd {ipv4, uptime}}, `uplink` {device, role, reach {ok, target, latency_ms, age_s}, traffic {rx_bytes, tx_bytes}} |
| Diagnosis | `diagnosis` {ok, link device/network/session/internet/dns \| null, reason \| null, detail \| null, standby, links [{link, state ok/fail/unknown/skipped/warn}], warning? {link dns, reason dns-intercepted/dns-stub/dns-rewritten, detail}} — the first broken link of the chain (spec 25.9.16, 25.9.20) |
| DNS check | `uplink.dns_check` {device, at, resolvers [{kind, address, identity, udp_ms, tcp_ms, verdict ok/intercepted/unclear}], intercept {state ok/intercepted/skipped, detail}, nxdomain {state ok/rewritten/no-answer/error/skipped, server, ms, detail}, names [{name, doh, plain {server: answer}, verdict ok/stub/skipped}], blocked {state ok/stub/skipped, detail}} (spec 25.9.20) |
| Health | `stability` {state stable/flapping/…, physical_disconnects, usb_reenumerations, driver_rebinds, hourly[24]}, `health.state`, `recovery` {state idle / running / cooldown / manual_required, attempts_1h, last_success, last_failure, levels [{name, supported, automatic, why}], recent} |
| Last event | `last_event` {t (unix time), m (monotonic ms), kind, detail?, requested?} — the newest line of the device's journal (`events` gives the rest, newest first); absent when the journal is empty |

## Write methods (later stages, after the bench confirms them)

`check` (run the reach and DNS check now), `bringup`, `dial` / `hangup`
(`id`, `apn`, `pdptype`; a dial answers {result connected / failed, ipv4, prefix_len, gateway (absent: the
default route on the link), dns, error}; with a recipe of `flow` 3gpp also {pdp (the type that got the
address), reused (the session the modem held), plmn, steps}, HANDOFF 171), `recover` (`id`, `level`, `reason`, `wait`),
`at_command` (`id`, `command`, `timeout_ms`; the engine refuses dangerous
commands and AT beside ModemManager), `esim_action` (`id`, `action`: version / probe / chip / profiles, or enable with
`iccid`: a switch to a profile already on the eUICC, answered {ok, restart_advised} — the ICCID is in no answer and no
log; download and delete wait for Alpha 2: until then, provision new profiles on a built-in eUICC or removable UICC/9eSIM using a compatible phone/app such as EasyEUICC or OpenEUICC on Android. LuCI offers Diagnostics → Check for an eSIM, then the eSIM tab to read and enable an installed profile), `config_plan` /
`config_apply` / `config_rollback` / `config_release`, `lease_*`, `rescan`,
`probe`, and `mm_restart` (`confirm` true, `reason`): the operator's emergency
ModemManager restart, every modem on the router is affected, once in 5 min,
answers started / running / refused {why, retry_s}. Each other method names
its `device_id`.

`connect` (`id`, `up` bool, `who` optional, r26): the operator's «Отключить /
Подключить» of the modem's netifd interface through the engine, so the journal
says who did it (events `interface-down` / `interface-up`, detail «<iface> by
the operator (<who>)»). Answers {device_id, result done|busy|refused|failed,
interface, reason}; busy while an eSIM operation, a recovery, a bring-up or a
configuration change holds the modem; refused without an interface. netifd
keeps autostart off after a down, and the engine leaves such an interface alone.
LuCI no longer holds `network.interface` up/down in its ACL.

`priority` (`order` array of device ids, r33): the operator's modem order, first =
main. Each listed modem's interface gets `network.<iface>.metric` 10, 20, 30… by its
place (a modem without an interface keeps its place in the numbering), netifd reloads
once when anything changed, and each modem's journal gets `priority` «priority N,
metric of <iface>». Answers {result done|busy|failed, changed (metrics written),
items [{device_id, priority, interface, metric}], reason}. The device record's
`connection.metric` is the interface's metric as last read.

Cell info (`-c`, UCI `core.cell_info`, r33): off by default — the engine sends
ModemManager no `--location-get`, `--location-enable-3gpp` or `--get-cell-info`
(bench 2026-10-15: an Orange Pi Zero 3 with a T99W175 hung within a minute of them);
`modemmanager.cells`, the location and the carriers' history are then absent.

Gentle poll (`-g <ms>`, UCI `core.mm_poll_s` seconds, r34, default off): a page looking
or the signal history's tick reads ModemManager at most once in that period (otherwise a
look refreshes a view older than 15 s). Reads after an event — a modem added, a
bring-up, a recovery — are not held. For a router whose modem driver hangs under the
page's polling (bench 2026-10-15, F18): `uci set medvemodem.core.mm_poll_s=60`.

`cells` (`id`, read, r34): the serving cells over AT while ModemManager's cell info is
off — Foxconn SDX55 (T99W175, dialect foxconn-sdx) and X20 (T77W968/DW5821e,
foxconn-t77) `AT^DEBUG?` through the engine's AT port, at most once in 30 s per modem.
Answers {device_id, source at|modemmanager|none, cells [{type lte|5gnr, serving, role
primary|secondary|scg-primary, band, earfcn|nrarfcn, pci, bandwidth, rsrp, rsrq, sinr,
rssi, tac, ci (hex), plmn}], age_ms when the reading is reused, why}. The reading stands
in the device record's `modemmanager.cells` (with `cells_source: at`) for 2 min. The
answer itself is never kept or logged (the modem prints its TMSI and IP address in it).
No neighbour cells over AT yet.

SMS and USSD (spec 38, r23–r24): through ModemManager for a modem it holds,
over the engine's AT port when ModemManager fails or holds no modem (answers
carry `path`/`via` `modemmanager`|`at`, and `fallback`/`mm_error` with
ModemManager's words; an AT listing adds `store` {name, used, total} and per
message `parts`, `have`/`total` for an incomplete long one); `{refused}` for
QModem's modems and modems with neither:
`sms_send` (`id`, `number` `+?[0-9]{3,20}`, `text` UTF-8 ≤ 1600 bytes) →
`{ok, via?, parts?, error?: not created / created but not sent / not sent in
time / not sent / sent in part, detail?: ModemManager's or the modem's words}`,
a ModemManager draft that was not sent is deleted again;
`sms_delete` (`id`, `sms`: the message id) → `{ok}`; `sms_center` (`id`,
`number?`) reads the SMS centre written on the SIM, or writes `number` there
first (`AT+CSCA`, over the engine's AT port only) → `{ok, center?, written?,
via: at, error?, detail?}`, journalled as `sms-center` (written/failed);
`sms_storage` (`id`) switches the SMS storage to the SIM and back to the modem's
(`AT+CPMS="SM","SM","SM"`, then `"ME","ME","ME"`, over the engine's AT port; the T99's
field recipe after a flash) → `{ok, storage?: the +CPMS line, error?, detail?}`,
journalled as `sms-storage`;
`ussd_start` (`id`,
`code` `[0-9*#+]{2,32}`), `ussd_respond` (`id`, `reply`), `ussd_cancel` (`id`)
→ `{result: started}` at once, the outcome through `ussd_status` (`closed`: a
menu nobody answered in 60 s was cancelled by the engine). The engine
journals `sms-received` (a count), `sms-sent` (ok/failed) and
`sms-stuck-dropped` (a multipart stuck in `receiving` for 300 s is deleted: it
blocks reception); numbers, texts, USSD codes and replies are never logged.
`sim_unlock` (`id`, `pin` 4–8 digits, `puk?` exactly 8 digits, `confirm?`;
write ACL, the owner's request 2026-10-17): a SIM that asks for its PIN (or
PUK) is unlocked through ModemManager for a modem it holds (`mmcli -i <sim>
--pin=` or `--puk= --pin=<new PIN>`, exec'd without a shell, one at a time per
modem, 20 s) → `{ok, error?: mmcli's words without any run of 4+ digits,
retries?: {"sim-pin": N, "sim-puk": N} read after the try, unlock_required?}`
or `{ok: false, refused, retries?}` (bad input, not ModemManager's modem, the
SIM not locked that way, or one PIN/PUK try left without `confirm: true`).
Journalled as `sim-unlock` done/failed; the codes are never logged, journalled
or replied. `modemmanager.unlock_retries` carries ModemManager's tries left.
A PIN the operator keeps on the router is the device section's `option pin`:
the engine writes it as netifd's `option pincode` of its proto
modemmanager/qmi/mbim interface, serves only `connection.pin_saved`, and shows
`(hidden)` in place of the digits in configuration plans, ledgers and logs.
`uplink` carries `failover` {enabled, active, why?, failback: restore|demote,
manual [device], routes [{device, ipv6?, metric, base, steered_away, manual?,
demoted?}], events [{dev, from, to, why: down|manual|demoted|back|off, ipv6?,
at}]} (spec 20.8; on with UCI `medvemodem.core.failover '1'`); each move is
also the ubus event `medvemodem.failover`. It carries `proxy_guard` {enabled,
blocked_checks, age_s?} too: every check fails while the internet answers
through the local proxy, so no modem is recovered for it.
`failover_forgive` (`device`, optional; write ACL) → the `failover` block: a
link kept last after its death (`failback 'demote'`) takes its priority back,
one device or every one.
`signal_history` (`id`, `range` 5m | 1h | 24h; read ACL, spec 25.9.13) →
`{device_id, range, step_s, points: [{t, lte_rsrp?, lte_rsrq?, lte_sinr?,
nr_rsrp?, nr_rsrq?, nr_sinr?, quality?, tech?, carriers?: {"lte-3": rsrp,
"5gnr-78": rsrp, ...}}], events: [{t, kind: tech | cell, detail}]}`; averages for
1h (per minute) and 24h (per 5 minutes); carriers from the cell reads (r27).
`at_history` (`id`, `clear` bool; read ACL of the engineer's group, 0.15.0-r24)
→ `{device_id, commands: [{t, cmd, ok?, final?, lines?, error?, refused?, port?,
elapsed_ms?}]}`: the AT console's commands of the last day (100 at most, newest
last), refused ones included; RAM only; `clear` empties it first.
`metrics` is in the read ACL from 0.15.0-r24: the modem page reads network and
signal over the engine's AT port for a modem ModemManager does not own.
From 0.15.0-r40 a HiLink or ZTE web modem (owner control `hilink`) is read
through its own web page: `source: "web"` and `web` {kind huawei/zte, host,
auth none/ok/needed/wrong/locked/busy/waiting, wait_s, password_saved}; a
refusal carries `refused` and `web`. The web password is the device section's
`web_password` (never served: `connection.web_password_saved`); `max_age_ms` 0
reads again at once.
The device record carries `sms` {unread, inbox} (spec 38.2.2) once the modem's
messages were read; an `sms_list` answer marks them seen.
The device record carries `quota` (spec 20.9) while its SIM is known:
{iccid, used_bytes, limit_bytes?, percent?, warn_percent, state: none | ok |
warn | exceeded, period_start, resets_on, action: warn | backup}; failover's
routes carry `quota: true` for a link held behind for it.
`probe` (`id`; write ACL since 0.15.0-r13, LuCI «Найти AT-порт») →
`{results: {node: at | not-at | silent}}` or `{refused}`; allowed beside
ModemManager for a control-only modem (profile `mm_ports "control"`). The
device record's `connection` carries the operator's `backend` (auto when
unset) and `at_interface`; `port_roles.at.source` is `operator` for the latter.
`modeswitch_fix` (`id`; write ACL) → `{device_id, rule, result: fixed |
not-needed | refused | failed, disabled_as?, backup?, replug?, reason?}`: a
`/etc/usb-mode.json` rule that sets configuration 0 for this modem (finding
`modeswitch-rule`, H7) is renamed to `<vid:pid>_medvemodem_disabled`, so
usbmode no longer matches it; the file before the first change stays as
`/etc/usb-mode.json.medvemodem-backup`. `replug` true: the device is still
unconfigured and needs to enumerate again. Journal `modeswitch-fix`.

`modeswitch_run` (`id`; write ACL, the operator's group) → `{device_id, result: started |
refused | failed, tool?, reason?}`: a stick waiting in its storage mode
(`detection_status` `modeswitch-pending`; the record's `modeswitch` says `{rule, mode?,
target?, tool: usbmode | usb_modeswitch | null}`) is switched once: `usbmode -s` with the
rules, `usbmode -c <run>/usbmode-huawei.json -s` with a one-entry HuaweiNew config when
the rules lack a Huawei ID, or `usb_modeswitch -v <vid> -p <pid> -J` (Huawei only). One
USB message, nothing written to the stick; once in 30 s, killed after 20 s. Refused:
`usb-modeswitch is not installed`, `the device is not waiting for a mode switch`,
`usb-mode.json has no rule for this device` (not Huawei), `a mode switch is running`,
`a mode switch ran less than 30 s ago`. Journal `modeswitch-run`, `modeswitch-done`.
`web_info` (`id`; read ACL) and `web_reboot` (`id`; write ACL), r117: a HiLink modem's own web (Huawei API) →
`{device_id, kind, auth, wan_ip?, wan_ipv6?, traffic {session, month, total: {rx, tx, s}, month.cleared}, device {name,
firmware, hardware, webui}}` / `{device_id, ok}` or `{refused}`; never the IMEI, IMSI, serial or number. `ussd_start`
for such a modem goes through its web (`ussd_status` `via: "web"`), `capabilities.sms.ussd` is `"web"`.

`radio_set` (`id`, `modes` {allowed: [2g|3g|4g|5g] or [any], preferred?},
`bands` [eutran-N, ngran-N, utran-N] or "any", `wait_s` 30..600; write ACL)
→ `{device_id, change, radio_settings}` or `{refused}`: the change through the
modem's owner (spec 25.9.12 stage 2), then a wait for the connection and an
automatic rollback when it does not come back. `radio_confirm` (`id`, `action`
keep | rollback; write ACL) → `{device_id, change}`. The record's
`radio_settings` carries `writable`, `why_read_only?` and `change`; each state
change is also the ubus event `medvemodem.radio`. A `cell_lock` kept (or applied
with no connection to watch) arms the cell-lock guard: without a registration for
UCI `core.radio_lock_guard` minutes (10 when unset, `0` off) the settings read
before the lock go back; the change then reads `rolled_back`, why «the cell lock
guard took it back: no network», and the journal has `radio-guard` entries.
`radio` (`id`, `refresh` bool; read ACL, queries only) → `{device_id,
radio_settings, error?}` or `{refused}`: ModemManager's report as is; for a
QModem modem `modem_ctrl.sh get_network_prefer` and `get_lockband` of its
section; for an engine modem the profile dialect's queries (quectel
QNWPREFCFG/QCFG, fibocom-mtk `AT+GTACT?`, xmm `AT+XACT?`, huawei
`AT^SYSCFGEX?`) under a diagnostics lease (holder `medvemodemd-radio`). The
reading is kept for 10 min (the device record shows it with `age_s`);
`refresh` reads again.
A band restriction kept arms the band guard the same way (r160): without a registration the bands before any
restriction go back, with the network mode as the latest change left it (r163); the journal says `band-guard`.

## Other methods (r163)

Each takes the device `id` where it names one; write methods need the write ACL (`luci-app-medvemodem`'s). Answers
carry `{refused}` with the reason when the engine will not do it.

| Method | Parameters | What it does |
|---|---|---|
| `reregister` | `id` | the gentlest recovery step (r160): ModemManager's `--3gpp-register-home`, else `AT+COPS=2` and `AT+COPS=0` on the engine's AT port; by the button only |
| `fplmn` | `id`, `clear` bool | the SIM's forbidden networks (EF_FPLMN) read with `AT+CRSM=176,28539`; `clear` writes the file full of `FF` (`AT+CRSM=214`): it changes the SIM card, so the page asks first |
| `wlmode` | `action`: `status` (or none), `on`, `off`, `check` | the whitelist mode (spec 44): `{wlmode: {enabled, on, manual, server, label, delay_ms, lan, router, busy?, error?}}`; `on`/`off` answer `{result: started\|busy\|refused}` and run the `wlmode` helper, whose ledger (`/etc/medvemodem/wlmode.json`) gives back exactly what it changed |
| `freeze` | `id`, `on` bool, `who` | «Приостановка управления»: no one touches the modem (the engine, ModemManager, QModem) |
| `handover` | `id` | «Передать MedveModem»: the interface goes down, ModemManager is told the ports |
| `fcc_unlock` | `id`, `enable` bool | links (or unlinks) the engine's FCC-unlock hook in ModemManager's `fcc-unlock.d` for a profile that names one |
| `sim_slot` | `id`, `slot` | ModemManager's `--set-primary-sim-slot`: the modem is probed again with the other SIM |
| `sms_clear` | `id` | the modem's SMS storage emptied over the engine's AT port (not where ModemManager holds it); the page asks first |
| `alerts` | `test` bool, `forward` | SMS alerts and forwarding by SMS: `{enabled, recipients, senders, queued, recent}`; `test` sends a test SMS now |
| `failover_manual` | `devices` [ids], `expect` | the order of the channels set by hand (the priority card), checked against the expected current order |
| `traffic_history` | `id`, `range` | a device's traffic over 5 min, an hour or a day (RAM) |
| `neighbours` | `refresh` bool | the neighbouring programs that may hold a modem (packages, port holders, scripts); `refresh` runs the helper |
| `usbtree` / `usbtree_detail` | — / `path` | the USB tree with the modems on it; one node's details |
| `usb_rescue` | `id` | the last resort (r180), what it would touch: `{device_id, controller: {name, driver}, devices: [{usb, vidpid, product, device_id?, name?}], blocked?, gpios: [{name, value}], gpio_blocked?, confirm, running}`; `blocked` when a mounted USB disk (the router's own storage or any other) is on the controller, `gpio_blocked` when one is mounted on any controller (r181); `gpios` are the board's output lines named like USB or modem power; `{refused}` for a modem off USB |
| `usb_rescue_run` | `id`, `what` (`controller` / `gpio`), `gpio`, `confirm` | by hand only: the controller's driver unbind, 3 s, bind; or the line off (its other value), 5 s, back; in a child with a 30 s limit; `confirm` must be the fingerprint `usb_rescue` gave (the devices and lines unchanged); every modem on the controller gets `usb-rescue` in its journal; `{result: started, target}` or `{refused}` |
| `usb_class` | `vidpid` (`19d2:1218`), `class` (`modem` / `not_modem` / `auto`) | the operator's word on a USB device by VID:PID (r179): `modem` takes it whatever the rules say, `not_modem` drops it and never asks again, `auto` gives it back to the rules; kept in `medvemodem.core` lists `usb_modem` / `usb_not_modem`, a discovery pass follows; `{vidpid, class, ok}` or `{…, error}` |
| `web_open` | `id` | opens the forward to a router-mode modem's own web page for `core.web_forward_minutes`; the reply has the port |
| `web_do` | `id`, `op` (`reconnect`, `clear-traffic`, `netmode`), `mode`, `lte_band` | a HiLink web's own actions |
| `lease_acquire` / `lease_renew` / `lease_release` | `id`, `port`, `holder`, `purpose`, `mode`, `ttl_ms` / `lease_id`, `ttl_ms` | a lease of a modem's AT port for another program, so it never talks over the engine |

## rpcd ACL for a read-only interface

```json
{
  "medvewrt-medvemodem-read": {
    "description": "Read MedveModem state",
    "read": {
      "ubus": { "medvemodem": ["status", "devices", "device", "uplink", "events", "recovery", "lookup"] },
      "uci": ["medvemodem"]
    }
  }
}
```

## Clients outside LuCI (an Android app)

The same contract over rpcd's JSON-RPC on the router's web server
(`uhttpd-mod-ubus`, already present with LuCI): `POST http(s)://<router>/ubus`.

1. Log in once:
   `{"jsonrpc":"2.0","id":1,"method":"call","params":["00000000000000000000000000000000","session","login",{"username":"medvewrt","password":"…"}]}`
   → `result[1].ubus_rpc_session` (a token; it expires after `timeout`
   seconds without use, default 300: log in again on ubus code 6).
2. Call: `{"jsonrpc":"2.0","id":2,"method":"call","params":["<token>","medvemodem","device",{"id":"usb-serial:…"}]}`
   → `result[0]` is the ubus status (0 = ok, 4 = not found, 6 = denied),
   `result[1]` the answer.

Rules for the app:

- **The router's own login, as LuCI uses it.** OpenWrt runs everything as root and MedveModem splits no access
  rights (the owner, 2026-10-22); no package of ours makes a system user (2026-10-31: one made every install signal
  ubusd). An app that only reads may still ask for the read ACL above in its own rpcd login, made by the admin.
- **Credentials and token** in the Android Keystore (EncryptedSharedPreferences),
  never in logs. Prefer HTTPS (`luci-ssl`) with the router's self-signed
  certificate pinned on first use; over plain HTTP say that the password
  travels in the clear on the LAN.
- **LAN only.** Never open `/ubus` to the WAN; remote access goes through a
  VPN to the router.
- **Polling.** In the foreground `device` every 2–5 s; in the background
  nothing faster than WorkManager's 15 min. Alerts while the app is closed
  come from the Telegram channel (spec 38.6.1), not from polling.
- **Antenna aiming (spec 25.2.2)** fits the app better than a browser: a
  native wake lock, sound and vibration. It needs the engine's `aim` session
  (not implemented yet); until then the app shows the 30 s values.
