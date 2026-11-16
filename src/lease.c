#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "lease.h"
#include "roles.h"

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

static const char *str(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static long long num(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v ? json_object_get_int64(v) : 0;
}

void mm_leases_init(struct mm_leases *l)
{
	l->list = json_object_new_array();
	l->next = 1;
}

void mm_leases_free(struct mm_leases *l)
{
	json_object_put(l->list);
	l->list = NULL;
}

int mm_lease_priority(const char *purpose)
{
	/* USSD above SMS and the operator's console (user, 2026-10-10): a USSD session waits on the
	 * network for tens of seconds and a menu dies when the port is taken from it */
	static const char *const order[] = { "metrics", "diagnostics", "sms", "operator", "ussd", "session", "recovery" };
	int i;

	if (purpose && !strcmp(purpose, "esim"))
		return 3;	/* an operator's eSIM request ranks with operator commands */
	for (i = 0; purpose && i < (int)(sizeof(order) / sizeof(order[0])); i++)
		if (!strcmp(purpose, order[i]))
			return i;
	return -1;
}

/* remove the leases at the given indexes (ascending), returning them */
static json_object *take(struct mm_leases *l, bool (*match)(json_object *, const void *), const void *arg)
{
	json_object *out = json_object_new_array();
	size_t i = json_object_array_length(l->list);

	while (i-- > 0) {
		json_object *x = json_object_array_get_idx(l->list, i);
		if (match(x, arg)) {
			json_object_array_add(out, json_object_get(x));
			json_object_array_del_idx(l->list, i, 1);
		}
	}
	return out;
}

struct port_key {
	const char *device_id, *port;
};

static bool same_port(json_object *x, const void *arg)
{
	const struct port_key *k = arg;
	return !strcmp(str(x, "device_id"), k->device_id) && !strcmp(str(x, "port"), k->port);
}

json_object *mm_lease_acquire(struct mm_leases *l, json_object *req, json_object *roles, long long now,
			      const char **why, char *detail, size_t detail_size, json_object **revoked)
{
	const char *dev = str(req, "device_id"), *port = str(req, "port"), *holder = str(req, "holder");
	const char *purpose = str(req, "purpose"), *mode = str(req, "mode") ? str(req, "mode") : "exclusive";
	int prio = mm_lease_priority(purpose), top = -1;
	long long ttl = num(req, "ttl_ms");
	bool exclusive = !strcmp(mode, "exclusive"), conflict = false;
	json_object *cand, *lease, *blocker = NULL;
	const char *node, *role;
	struct port_key key;
	size_t i;

	*revoked = json_object_new_array();
	detail[0] = '\0';
	*why = NULL;
	if (!dev || !port || !holder || !*holder) {
		*why = "device_id, port and holder are required";
		return NULL;
	}
	if (prio < 0) {
		*why = "unknown purpose";
		return NULL;
	}
	if (!exclusive && strcmp(mode, "shared-safe")) {
		*why = "mode must be exclusive or shared-safe";
		return NULL;
	}
	if (!strcmp(port, "at")) {
		node = str(get(roles, "at"), "node");
		if (!node) {
			*why = "this device has no resolved AT port";
			return NULL;
		}
	} else {
		node = port;
	}
	cand = mm_roles_candidate(roles, node);
	if (!cand) {
		*why = "port does not belong to this device";
		return NULL;
	}
	node = str(cand, "node");
	role = str(cand, "role");
	if (strcmp(str(cand, "kind"), "tty")) {
		*why = "only serial ports are leased; control and network nodes belong to the owner";
		return NULL;
	}
	if (!strcmp(str(cand, "owner"), "modemmanager")) {
		/* spec 11.6: not without explicit policy and MM coordination (not in M1) */
		*why = "port is owned by ModemManager";
		return NULL;
	}
	if (!strcmp(role, "data") || !strcmp(role, "reserved")) {
		snprintf(detail, detail_size, "role %s", role);
		*why = "port role is not leasable";
		return NULL;
	}
	if ((!strcmp(role, "diag") || !strcmp(role, "gnss")) && strcmp(purpose, "diagnostics")) {
		snprintf(detail, detail_size, "role %s", role);
		*why = "port role is leased for diagnostics only";
		return NULL;
	}

	for (i = 0; i < json_object_array_length(l->list); i++) {
		json_object *x = json_object_array_get_idx(l->list, i);

		if (strcmp(str(x, "device_id"), dev) || strcmp(str(x, "port"), node))
			continue;
		if (!exclusive && !strcmp(str(x, "mode"), "shared-safe"))
			continue;	/* shared with shared */
		conflict = true;
		if ((int)num(x, "priority") >= top) {
			top = (int)num(x, "priority");
			blocker = x;
		}
	}
	if (conflict && prio <= top) {
		snprintf(detail, detail_size, "held by %s (%s), %lld ms left", str(blocker, "holder"),
			 str(blocker, "purpose"), num(blocker, "deadline") - now);
		*why = "busy";
		return NULL;
	}
	if (conflict) {
		/* outranks every holder: displace them (shared ones only by an exclusive request) */
		json_object_put(*revoked);
		key.device_id = dev;
		key.port = node;
		*revoked = take(l, same_port, &key);
		if (!exclusive) {
			/* a shared request only displaced exclusives; put shared holders back */
			size_t j = json_object_array_length(*revoked);
			while (j-- > 0) {
				json_object *x = json_object_array_get_idx(*revoked, j);
				if (!strcmp(str(x, "mode"), "shared-safe")) {
					json_object_array_add(l->list, json_object_get(x));
					json_object_array_del_idx(*revoked, j, 1);
				}
			}
		}
	}
	if (ttl <= 0)
		ttl = MM_LEASE_TTL_DEFAULT_MS;
	if (ttl > MM_LEASE_TTL_MAX_MS)
		ttl = MM_LEASE_TTL_MAX_MS;

	lease = json_object_new_object();
	snprintf(detail, detail_size, "L%lu", l->next++);
	json_object_object_add(lease, "lease_id", json_object_new_string(detail));
	detail[0] = '\0';
	json_object_object_add(lease, "device_id", json_object_new_string(dev));
	json_object_object_add(lease, "port", json_object_new_string(node));
	json_object_object_add(lease, "role", json_object_new_string(role));
	json_object_object_add(lease, "holder", json_object_new_string(holder));
	json_object_object_add(lease, "purpose", json_object_new_string(purpose));
	json_object_object_add(lease, "priority", json_object_new_int(prio));
	json_object_object_add(lease, "mode", json_object_new_string(mode));
	json_object_object_add(lease, "generation", json_object_new_int64(num(roles, "generation")));
	json_object_object_add(lease, "acquired", json_object_new_int64(now));
	json_object_object_add(lease, "deadline", json_object_new_int64(now + ttl));
	json_object_array_add(l->list, json_object_get(lease));
	return lease;
}

bool mm_lease_release(struct mm_leases *l, const char *lease_id)
{
	size_t i;

	for (i = 0; lease_id && i < json_object_array_length(l->list); i++)
		if (!strcmp(str(json_object_array_get_idx(l->list, i), "lease_id"), lease_id)) {
			json_object_array_del_idx(l->list, i, 1);
			return true;
		}
	return false;
}

json_object *mm_lease_renew(struct mm_leases *l, const char *lease_id, int ttl_ms, long long now,
			    long long generation)
{
	size_t i;

	if (ttl_ms <= 0)
		ttl_ms = MM_LEASE_TTL_DEFAULT_MS;
	if (ttl_ms > MM_LEASE_TTL_MAX_MS)
		ttl_ms = MM_LEASE_TTL_MAX_MS;
	for (i = 0; lease_id && i < json_object_array_length(l->list); i++) {
		json_object *x = json_object_array_get_idx(l->list, i);

		if (strcmp(str(x, "lease_id"), lease_id))
			continue;
		if (num(x, "deadline") <= now || num(x, "generation") != generation)
			return NULL;
		json_object_object_add(x, "deadline", json_object_new_int64(now + ttl_ms));
		return json_object_get(x);
	}
	return NULL;
}

static bool expired(json_object *x, const void *arg)
{
	return num(x, "deadline") <= *(const long long *)arg;
}

json_object *mm_lease_expire(struct mm_leases *l, long long now)
{
	return take(l, expired, &now);
}

struct gen_key {
	const char *device_id;
	long long generation;
};

static bool stale(json_object *x, const void *arg)
{
	const struct gen_key *k = arg;
	return !strcmp(str(x, "device_id"), k->device_id) && (k->generation < 0 || num(x, "generation") != k->generation);
}

json_object *mm_lease_invalidate(struct mm_leases *l, const char *device_id, long long generation)
{
	struct gen_key k = { device_id, generation };
	return take(l, stale, &k);
}

long long mm_lease_next_deadline(const struct mm_leases *l)
{
	long long best = -1;
	size_t i;

	for (i = 0; i < json_object_array_length(l->list); i++) {
		long long d = num(json_object_array_get_idx(l->list, i), "deadline");
		if (best < 0 || d < best)
			best = d;
	}
	return best;
}

json_object *mm_lease_list(const struct mm_leases *l, const char *device_id)
{
	json_object *out = json_object_new_array();
	size_t i;

	for (i = 0; i < json_object_array_length(l->list); i++) {
		json_object *x = json_object_array_get_idx(l->list, i);
		if (!device_id || !strcmp(str(x, "device_id"), device_id))
			json_object_array_add(out, json_object_get(x));
	}
	return out;
}
