#ifndef MEDVEMODEM_REGISTRY_H
#define MEDVEMODEM_REGISTRY_H

#include <stdbool.h>
#include <json-c/json.h>

/*
 * The authoritative device registry (spec section 7).
 *
 * Pure: every function works on the registry and its arguments only. The
 * daemon feeds it discovery passes; tests feed it fixtures.
 */
struct mm_registry {
	json_object *devices;	/* device_id -> record */
	unsigned long passes;
};

void mm_registry_init(struct mm_registry *r);
void mm_registry_free(struct mm_registry *r);

/*
 * Merge one discovery pass taken at time `now`. Returns a new array of change
 * events: {"event": "added"|"reattached"|"changed"|"removed", "device_id",
 * "generation", "cause"?}. A change of attachment or port set starts a new
 * generation. "cause" names what differed: "returned" (was absent), or the
 * changed fields ("attachment_id,ports", "detection_status", ...).
 */
json_object *mm_registry_apply(struct mm_registry *r, json_object *scan, long long now);

/* New array of records; absent devices only when asked for. */
json_object *mm_registry_list(const struct mm_registry *r, bool include_absent);

/* Borrowed record by device_id or attachment_id, or NULL. */
json_object *mm_registry_find(const struct mm_registry *r, const char *id);

/*
 * Borrowed record holding this attachment ("usb:3-1.2"): the present one, else
 * the most recently changed absent one (a device that just left), or NULL.
 */
json_object *mm_registry_by_attachment(const struct mm_registry *r, const char *attachment_id);

/* Borrowed record of the present device owning a tty/WDM/netdev, or NULL. */
json_object *mm_registry_lookup_node(const struct mm_registry *r, const char *node);

#endif
