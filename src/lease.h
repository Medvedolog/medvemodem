#ifndef MEDVEMODEM_LEASE_H
#define MEDVEMODEM_LEASE_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/*
 * Central tty lease arbitration (spec 11.6). Pure: the caller passes the
 * current role resolution of the device and the monotonic clock; the daemon
 * revalidates the node against sysfs before calling.
 *
 * One exclusive holder per port, or any number of shared-safe holders.
 * A request outranking every conflicting holder displaces them (they are
 * returned as revoked); otherwise it is refused as busy. Every lease has a
 * deadline and belongs to one attachment generation.
 */
#define MM_LEASE_TTL_DEFAULT_MS 30000
#define MM_LEASE_TTL_MAX_MS 120000

struct mm_leases {
	json_object *list;
	unsigned long next;
};

void mm_leases_init(struct mm_leases *l);
void mm_leases_free(struct mm_leases *l);

/* recovery 5, session 4, operator/esim 3, sms/ussd 2, diagnostics 1, metrics 0; -1 unknown */
int mm_lease_priority(const char *purpose);

/*
 * req: {"device_id", "port" (node, or "at" for the resolved AT port), "holder",
 *       "purpose", "mode" (exclusive | shared-safe), "ttl_ms"}.
 * Returns a new lease, or NULL with *why (static text) and detail filled.
 * *revoked becomes a new array of displaced leases (possibly empty).
 */
json_object *mm_lease_acquire(struct mm_leases *l, json_object *req, json_object *roles, long long now,
			      const char **why, char *detail, size_t detail_size, json_object **revoked);

bool mm_lease_release(struct mm_leases *l, const char *lease_id);

/* Extend a live lease of the given generation; new lease copy, or NULL. */
json_object *mm_lease_renew(struct mm_leases *l, const char *lease_id, int ttl_ms, long long now,
			    long long generation);

/* Remove expired leases; new array of them. */
json_object *mm_lease_expire(struct mm_leases *l, long long now);

/* Remove leases of a device not in this generation (-1: the device is gone). */
json_object *mm_lease_invalidate(struct mm_leases *l, const char *device_id, long long generation);

/* Earliest deadline, or -1 when no lease is held. */
long long mm_lease_next_deadline(const struct mm_leases *l);

/* New array of leases, all or one device's. */
json_object *mm_lease_list(const struct mm_leases *l, const char *device_id);

#endif
