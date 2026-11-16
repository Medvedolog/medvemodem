/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef CELLEV_H
#define CELLEV_H

#include <json-c/json.h>

/*
 * Band, cell and carrier-aggregation changes of a modem, for its event history (the owner, 2026-11-06, from
 * network events). A digest of one reading: {"band": "B3" or "B3+n78", "cell": "1A2B3C" (the cell's id in
 * hex, else "PCI 101"), "ca": "B3+B7" (the carriers, the primary first; «-» when the reading
 * named one carrier; absent when it said nothing of carriers)}; any part may be
 * missing when the reading did not carry it.
 */
json_object *mm_cellev_digest_reading(json_object *m);	/* the engine's AT or web reading (metrics) */
json_object *mm_cellev_digest_mm(json_object *mm);	/* ModemManager's device: its serving cell, location, CA */

/*
 * One reading's digest against the device's state (kept by the caller, {} at first): a value is taken once two readings
 * in a row agree on it, and a change of a taken one is an event, at most one of each kind per hold_ms. Events go to
 * out as [kind, detail]: "band-change" «B3 → B7», "cell-change» «1A2B3C → 1A2B40», "ca-change" «B3+B7 → B3». The first
 * value of each part is no event. Returns the number added.
 */
int mm_cellev_observe(json_object *state, json_object *digest, long long now_ms, long long hold_ms, json_object *out);

#endif
