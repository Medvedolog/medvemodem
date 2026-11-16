/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_SIGHIST_H
#define MEDVEMODEM_SIGHIST_H

#include <json-c/json.h>

/*
 * Radio quality history of a modem (spec 25.9.13). Pure: the daemon hands in
 * what ModemManager reported; this keeps it in RAM, coarser the older it is:
 * every sample of the last 5 minutes, one-minute averages of the last hour,
 * five-minute averages of the last day. A reboot clears it.
 *
 * hist: the device's history object, kept by the caller (an empty object to
 * start). mm: the device's ModemManager view ({"signal": {"lte", "5g", ...},
 * "access_tech", "signal_quality", "cells"?}). t: wall-clock seconds. A sample
 * with no number at all is not kept. A change of the access technology or of
 * the serving cell (when the cells were read) becomes an event.
 */
void mm_sighist_add(json_object *hist, json_object *mm, long long t);

/*
 * The served history for a range "5m", "1h" or "24h" (anything else is "1h"),
 * up to now: {"range", "step_s", "points": [{"t", "lte_rsrp"?, "lte_rsrq"?,
 * "lte_sinr"?, "nr_rsrp"?, "nr_rsrq"?, "nr_sinr"?, "quality"?, "tech"?, "carriers"?: {"lte-3": rsrp,
 * "5gnr-78": rsrp, ...}}] (carriers: the serving cells of the cell reads in that step, by technology and
 * band; "lte-3#2" for a second carrier in one band),
 * "events": [{"t", "kind": "tech" | "cell", "detail"}]}. Averages are rounded
 * to one decimal. New object.
 */
json_object *mm_sighist_view(json_object *hist, const char *range, long long now);

#endif
