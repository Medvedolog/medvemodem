/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_RADIO_H
#define MEDVEMODEM_RADIO_H

#include <json-c/json.h>
#include <stdbool.h>

/*
 * Radio settings (spec 25.9.12) read from an owner other than ModemManager,
 * normalised to the block ModemManager's report gives:
 *   {modes: {allowed: [2g, 3g, 4g, 5g] | [any], preferred: x | null} | null,
 *    bands: ["eutran-3", "ngran-78", "utran-1"], bands_any: bool,
 *    supported_bands: [...] (when the owner says)}
 * Read only: the commands are queries.
 */

/* the AT queries of a dialect (NULL-terminated), or NULL when the engine cannot read it yet */
const char *const *mm_radio_commands(const char *dialect);

/* the answers of mm_radio_commands (command -> raw text) to the block; NULL when nothing was understood */
json_object *mm_radio_from_at(const char *dialect, json_object *responses);

/* QModem's modem_ctrl.sh get_network_prefer and get_lockband objects to the block; NULL when neither says anything */
json_object *mm_radio_from_qmodem(json_object *prefer, json_object *lockband);

/*
 * Stage 2: the steps that set want on a modem, whoever drives it.
 * current: the block as read now (ModemManager's view or the last reading:
 * supported_modes, supported_bands and raw are used when there). want:
 * {modes?: {allowed: [...] | ["any"], preferred?: x | null}, bands?: [...] |
 * "any"} (what is left out stays), or a block read earlier with its raw values
 * (a rollback writes those back as they were). Returns
 *   {"steps": [[arg, ...], ...]} -- modemmanager: mmcli arguments per call;
 *   qmodem: [method, argument] per modem_ctrl.sh call; at: one command each,
 * or NULL with why filled: an invalid request, or one this owner cannot carry
 * out (never half of it).
 */
json_object *mm_radio_plan(const char *via, const char *dialect, json_object *current, json_object *want, char *why, size_t size);
/*
 * ModemManager narrowed to one mode by the mode the modem stored (a field report): a
 * multimode QMI/MBIM modem with «LTE only» stored makes MM offer `allowed: 4g` alone, and its --set-allowed-modes cannot
 * widen it; MM reads the stored preference only when it starts. Given MM's supported_modes and supported_bands and its
 * port_types, {offered: [the one mode], possible: ["3g", "4g", ...] by the bands, wdm: "cdc-wdmN", mbim: bool} or NULL.
 * A plan for a mode MM does not offer then writes the modem's own preference through qmicli (the owner, 2026-11-02:
 * «иначе пользователю придётся перетыкать модем в ПК»).
 */
json_object *mm_modes_narrowed(json_object *supported_modes, json_object *supported_bands, json_object *port_types);
/* qmicli's «Mode preference: 'umts, lte'» read back holds exactly the technologies written ("lte|umts"), in any order:
 * one more or one fewer is not the preference written (audit of r135: «lte» passed against 'umts, lte, 5gnr') */
bool mm_qmi_pref_matches(const char *line, const char *want);

/*
 * The serving cells over AT where ModemManager's cell info is off (r34, bench 2026-10-15
 * F18): the dialect's cell report command (NULL when there is none), and its answer
 * normalised to ModemManager's cell shape: {cells: [{type lte|5gnr, serving true, role
 * primary|secondary|scg-primary, band, earfcn|nrarfcn, pci (string), bandwidth (MHz),
 * rsrp, rsrq, sinr, rssi, tac, ci (hex strings), plmn}]} or NULL when the modem is not
 * on a cell. Foxconn SDX55 (T99W175) and X20 (T77W968, DW5821e) `AT^DEBUG?`.
 */
const char *mm_cells_command(const char *dialect);
/* the T77's carriers by number (band, width), sent after AT^DEBUG? on the engine's own port; its answer joins ^DEBUG?'s */
#define MM_T77_CA_COMMAND "AT^CA_INFO?"
json_object *mm_cells_from_at(const char *dialect, const char *raw);
/*
 * The modem's heat, read beside the cells on an SDX55 (a field report): `AT^TEMP?` gives
 * degrees on a T99W175 ("TSENS: 45C", "PA: 40C"); a firmware with no degrees (Compal RXM-G1) gives `AT+CEITHERM?`
 * throttling levels 0-3 per sensor ("pa level: 0", ...), of which the highest counts. {modem_c?, pa_c?, throttle?} or
 * NULL when neither said anything usable. Never a level shown as degrees.
 */
#define MM_SDX_TEMP_COMMAND "AT^TEMP?"
#define MM_SDX_THERM_COMMAND "AT+CEITHERM?"
json_object *mm_heat_from_at(const char *temp, const char *therm);

/* EP06-E: optional read-only sensor query on the engine-owned AT port. */
#define MM_EP06_TEMP_COMMAND "AT+QTEMP"
#define MM_EP06_SERVING_COMMAND "AT+QENG=\"servingcell\""
#define MM_EP06_NEIGHBOUR_COMMAND "AT+QENG=\"neighbourcell\""
json_object *mm_quectel_temp_from_at(const char *raw);

#endif
