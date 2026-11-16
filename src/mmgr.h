#ifndef MEDVEMODEM_MMGR_H
#define MEDVEMODEM_MMGR_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

#include "registry.h"

/*
 * ModemManager as a read-only owner backend (spec 4.1, M1 item 3).
 *
 * Pure: the daemon runs `mmcli -J -L` and `mmcli -J -m N` in a supervised
 * worker and hands the concatenated output here. Nothing in this module talks
 * to ModemManager or assumes polling; M2 replaces the worker with D-Bus
 * signals and keeps these functions.
 */

/*
 * Normalized modems from concatenated mmcli JSON output, a new array of
 * {"object", "index", "device", "usb_name", "ports", "port_types",
 *  "primary_port", "drivers", "plugin", "state", "failed_reason",
 *  "power_state", "unlock_required", "unlock_retries" ({"sim-pin": 3, ...}),
 *  "manufacturer", "model", "revision",
 *  "access_tech", "signal_quality", "operator", "registration", "sim"}.
 * mmcli's "--" (no value) becomes null. Unparsable chunks are skipped.
 */
json_object *mm_mmgr_parse(const char *buf, size_t len);

/*
 * Map modems onto present registry devices. Returns a new object:
 *   "devices":   device_id -> modem + "mapping" (exact | path-only | ports-only)
 *   "unmapped":  modems no present device matches (e.g. PCIe for now)
 *   "conflicts": modems whose ports belong to another device, or two modems
 *                claiming one device; such modems are mapped to nobody.
 * The physical device path decides; ports confirm. A port that belongs to a
 * different device is never resolved by guessing.
 */
json_object *mm_mmgr_map(json_object *modems, const struct mm_registry *reg);

/*
 * Changes between two "devices" maps as history events, a new array of
 * {"device_id", "kind": mm-add | mm-remove | mm-state, "detail"}.
 */
json_object *mm_mmgr_diff(json_object *old_devices, json_object *new_devices);

/* Configuration finding from ModemManager (spec 14.1.1 "locked"), or NULL. */
json_object *mm_mmgr_health(json_object *modem);

/* band numbers from channel numbers (0 when unknown) */
int mm_mmgr_lte_band(long earfcn);
int mm_mmgr_nr_band(long arfcn);

#endif
