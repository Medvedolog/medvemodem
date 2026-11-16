#ifndef MEDVEMODEM_CFGPLAN_H
#define MEDVEMODEM_CFGPLAN_H

#include <json-c/json.h>

/*
 * Configuration change planning (spec 17). Pure: works on JSON images of UCI
 * packages and never touches files.
 *
 * Package image: {"<section>": {".type": "...", "<option>": "value" | ["a", ...]}}
 * keyed by section name in file order; current: {"<package>": image}.
 *
 * Desired state and ledger (what MedveModem wrote last) share one shape:
 *   {"interface": "mm0",
 *    "sections": {"network.mm0": {"type": "interface", "fields": {"proto": "..."}}},
 *    "list_items": [{"package": "firewall", "type": "zone", "name": "wan",
 *                    "option": "network", "value": "mm0"}],
 *    "options": [{"package": "qmodem", "section": "main", "option": "block_auto_probe",
 *                 "value": "1"}]}
 *
 * "options" are single fields in a section someone else owns; the ledger keeps
 * the value found before ("previous", absent = null) so a release puts it back.
 *
 * Ownership is three-way: a field is ours only while its current value is the
 * one we wrote. A field we never wrote, or one the user changed or removed, is
 * the user's and is never touched; the plan reports it as a conflict.
 *
 * Ops: {"op": "add-section" | "set" | "delete" | "delete-section" | "add-list"
 *       | "del-list", "package", "section", "type"?, "option"?, "value"?,
 *       "match"? (list ops: {"type", "name"}), "expect"?}
 * "expect" is the state the op assumes (old value, null for absent, the whole
 * section for delete-section); an op whose expectation fails is skipped.
 */

/* {"ops", "undo", "conflicts", "ledger", "packages"}. desired NULL releases. New object. */
json_object *mm_cfg_plan(json_object *current, json_object *ledger, json_object *desired);

/*
 * Check ops in order against current, simulating each applied one.
 * {"apply": [ops], "skipped": [{"op", "reason"}]}; list ops get their
 * section resolved from "match". New object; current is not modified.
 */
json_object *mm_cfg_check(json_object *current, json_object *ops);

/* Apply one checked op to a package image set (test and simulation helper). */
void mm_cfg_simulate(json_object *current, json_object *op);

/* Section of package with this type whose option "name" equals name, or NULL. */
const char *mm_cfg_find_named(json_object *current, const char *package, const char *type, const char *name);

#endif
