// SPDX-License-Identifier: GPL-3.0-only
/*
 * A modem's session interface set up outside MedveModem (field 2026-10-27, a Nokia on an OpenWrt snapshot: the
 * owner's own `lte` over umbim on the T99's MBIM port, and the ModemManager installed with the engine told about the
 * same port three times). While such an interface is there the modem is its: the engine starts no connection of its
 * own, reports nothing to ModemManager and takes the ports back from it once. The operator hands the modem over
 * («Передать MedveModem», ubus handover): that interface goes down, its configuration untouched, ModemManager is
 * told the ports, and once it holds the modem the auto-connect rewrites the same section for it (netcfg's adopt:
 * the name and the other options stay, «Вернуть как было» restores the section). Nothing changes without that
 * request (the owner: «давай детектить такие конфликты и предлагать переделать под нас, но не молча все сносить»).
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <libubox/ulog.h>

#include "daemon.h"
#include "netcfg.h"

static json_object *found;	/* device_id -> {interface, proto, off?, generation} from the last network read */
static json_object *handed;	/* device_id -> {interface, generation}: a hand-over asked for this attachment */
static json_object *withdrawn;	/* "<id>|<generation>" -> true: the ports taken back from ModemManager once */

static long long generation_of(const char *id)
{
	json_object *rec = registry.devices ? json_object_object_get(registry.devices, id) : NULL;

	return rec ? json_object_get_int64(json_object_object_get(rec, "generation")) : -1;
}

void foreign_note(const char *id, json_object *rec, json_object *network, json_object *ledgers)
{
	json_object *f = mm_netcfg_foreign(rec, network, ledgers), *h;
	long long gen = json_object_get_int64(json_object_object_get(rec, "generation"));
	char key[200];

	if (!found)
		found = json_object_new_object();
	if (!f) {
		/* gone, or ours now (the hand-over's rewrite put it in the ledger) */
		if (json_object_object_get(found, id))
			ULOG_INFO("%s: no session interface outside MedveModem any more\n", id);
		json_object_object_del(found, id);
		if (handed)
			json_object_object_del(handed, id);
		return;
	}
	json_object_object_add(f, "generation", json_object_new_int64(gen));
	if (!json_object_object_get(found, id)) {
		ULOG_INFO("%s: interface %s (proto %s) set up outside MedveModem drives it: left to it\n", id,
			  jstr(f, "interface"), jstr(f, "proto"));
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "foreign-interface", jstr(f, "interface"));
		history_dirty = true;
	}
	json_object_object_add(found, id, f);
	h = handed ? json_object_object_get(handed, id) : NULL;
	if (h && (json_object_get_int64(json_object_object_get(h, "generation")) != gen ||
		  strcmp(jstr(h, "interface") ? jstr(h, "interface") : "", jstr(f, "interface"))))
		json_object_object_del(handed, id);	/* replugged or another interface: asked again */
	/* ModemManager came with the engine: it is not the second master of a modem the operator set up by hand */
	snprintf(key, sizeof(key), "%s|%lld", id, gen);
	if (!foreign_handed(id) && !(mm_devices && json_object_object_get(mm_devices, id)) &&
	    !(withdrawn && json_object_object_get(withdrawn, key))) {
		if (!withdrawn)
			withdrawn = json_object_new_object();
		json_object_object_add(withdrawn, key, json_object_new_boolean(1));
		mm_ports_withdraw(id);
	}
}

void foreign_retain(json_object *devices)
{
	json_object *maps[] = { found, handed };

	for (size_t i = 0; i < sizeof(maps) / sizeof(*maps); i++) {
		json_object *gone = json_object_new_array();

		if (maps[i]) {
			json_object_object_foreach(maps[i], id, v) {
				(void)v;
				if (!devices || !json_object_object_get(devices, id))
					json_object_array_add(gone, json_object_new_string(id));
			}
			for (size_t k = 0; k < json_object_array_length(gone); k++)
				json_object_object_del(maps[i], json_object_get_string(json_object_array_get_idx(gone, k)));
		}
		json_object_put(gone);
	}
}

json_object *foreign_of(const char *id)
{
	return found && id ? json_object_object_get(found, id) : NULL;
}

bool foreign_handed(const char *id)
{
	json_object *h = handed && id ? json_object_object_get(handed, id) : NULL;

	return h && json_object_get_int64(json_object_object_get(h, "generation")) == generation_of(id);
}

const char *foreign_adopt(const char *id)
{
	return foreign_handed(id) ? jstr(json_object_object_get(handed, id), "interface") : NULL;
}

bool foreign_holds(const char *id)
{
	return foreign_of(id) && !foreign_handed(id);
}

void foreign_stop(void)
{
	json_object_put(found);
	json_object_put(handed);
	json_object_put(withdrawn);
	found = handed = withdrawn = NULL;
}

enum { FH_ID, __FH_MAX };
const struct blobmsg_policy foreign_handover_policy[1] = {
	[FH_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
};

/* «Передать MedveModem»: the interface goes down, ModemManager is told the ports; the rewrite follows its hold */
int m_foreign_handover(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		       const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__FH_MAX];
	json_object *o = json_object_new_object(), *f, *h;
	const char *id, *iface;
	int rc;

	(void)obj;
	(void)method;
	blobmsg_parse(foreign_handover_policy, __FH_MAX, tb, blob_data(msg), blob_len(msg));
	id = tb[FH_ID] ? blobmsg_get_string(tb[FH_ID]) : NULL;
	f = foreign_of(id);
	if (!f || generation_of(id) < 0 || ledger_backend(id)) {
		/* ledger_backend: the rewrite is done (the next network read forgets the interface) */
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string("no interface set up outside MedveModem drives this modem"));
	} else if (foreign_handed(id)) {
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string("already handed over: waiting for ModemManager to take the modem"));
	} else if (frozen(id)) {
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string("the modem is frozen by the operator"));
	} else {
		iface = jstr(f, "interface");
		if (!handed)
			handed = json_object_new_object();
		h = json_object_new_object();
		json_object_object_add(h, "interface", json_object_new_string(iface));
		json_object_object_add(h, "generation", json_object_new_int64(generation_of(id)));
		json_object_object_add(handed, id, h);
		/* that interface only (CLAUDE.md, network safety): its section stays as it is until the rewrite */
		netifd_do(iface, "down");
		ULOG_INFO("%s: the operator hands it over: interface %s down, ModemManager told its ports\n", id, iface);
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "foreign-handover", iface);
		history_dirty = true;
		mm_ports_again(id);
		json_object_object_add(o, "result", json_object_new_string("started"));
		json_object_object_add(o, "interface", json_object_new_string(iface));
	}
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}
