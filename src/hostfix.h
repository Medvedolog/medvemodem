#ifndef MEDVEMODEM_HOSTFIX_H
#define MEDVEMODEM_HOSTFIX_H

#include <stdbool.h>
#include <json-c/json.h>

/*
 * Host-side fixes (spec 15.2 stage 4, 14.1.6): interface-scoped driver binding
 * a profile declares, e.g. the serial interfaces of a modem the stock option
 * driver does not know. Pure: the daemon writes sysfs.
 *
 * bind (the profile's "bind" block): {"status", "serial": {"driver",
 * "usb_serial_driver", "interfaces": ["00", ...]}}. A dynamic new_id binds
 * every unbound interface of the VID:PID with the class, so the plan also
 * names the ones that must be given back afterwards ("collateral").
 */

/* {"action": "none" | "bind" | "blocked", "reason"?, "missing": [sysfs names],
 *  "collateral": [sysfs names], "class": "ff"}. New object; NULL without a serial block. */
json_object *mm_hostfix_plan(json_object *rec, json_object *bind);

/* Interfaces the driver holds that the profile did not ask for (after new_id). New array. */
json_object *mm_hostfix_extra(json_object *rec, json_object *bind);

/*
 * bind.net {"driver", "interfaces"}: the data driver's new_id for an ID no kernel table names, the interfaces it
 * must not keep given back, a data interface a serial driver took rescued. See hostfix.c. NULL without net.
 */
json_object *mm_hostfix_net_plan(json_object *rec, json_object *bind);
json_object *mm_hostfix_net_extra(json_object *rec, json_object *bind);

/* Whether automatic use is allowed: a bench-validated or field-reported binding (the owner, 2026-10-26). */
bool mm_hostfix_automatic(json_object *bind);

/*
 * Kernel drivers the router lacks for this modem: every interface no driver holds whose
 * class names one (MBIM, NCM, ECM, RNDIS, Huawei's NCM; any other vendor interface waits
 * for the serial driver) and that have(driver) says the kernel does not register.
 * [{"driver", "package", "interfaces": ["00", ...]}], in the order first met. New array.
 */
json_object *mm_hostfix_missing_drivers(json_object *rec, bool (*have)(const char *driver));

#endif
