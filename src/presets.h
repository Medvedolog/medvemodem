#ifndef MEDVEMODEM_PRESETS_H
#define MEDVEMODEM_PRESETS_H

#include <json-c/json.h>

/*
 * Presets for the AT console and USSD (spec 11.5, 38): data files in
 * PRESET_DIR, matched to one modem here. Pure.
 */

/* The AT command sets of at-presets.json that fit this modem (a set without
 * 'match' fits all; a rule fits when every key it names matches: profile id
 * prefix, ModemManager manufacturer/model substring ignoring case, USB VID).
 * Each preset gains hazard (mm_at_hazard), allowed (the console sends it) and
 * why (when not): {groups, sets: [{id, title, source, presets: [...]}]}. */
json_object *mm_at_presets(json_object *catalog, const char *profile, const char *manufacturer, const char *model,
			   const char *vid);

/* The operator of ussd-codes.json whose PLMN (MCC+MNC) is this one:
 * {name, plmn, codes: [...]}, or NULL. */
json_object *mm_ussd_codes(json_object *catalog, const char *plmn);

#endif
