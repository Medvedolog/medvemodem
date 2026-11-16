// SPDX-License-Identifier: GPL-3.0-only
/*
 * «Это модем / не модем» by hand (the owner, 2026-11-09: «может спрашивать в карточке при подозрении на модем и
 * выпадающие или галочки там делать в экспертном режиме?»). The classifier drops a USB device without a modem driver
 * (an Arduino, a USB-UART adapter), and a modem it does not know went with it (a tester's ZTE MF192: all its ports
 * CDC ACM). The operator's word, by VID:PID, in medvemodem.core: list usb_modem (always a modem) and list
 * usb_not_modem (never one, never asked about). A module outside the core: it hands the lists to discovery before each
 * pass and answers the method; the core only calls it.
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <libubox/ulog.h>

#include "daemon.h"
#include "discovery.h"
#include "ucistore.h"

/* a core option as a set: a list, or one value, or several words (an old hand-written option) */
static json_object *core_set(const char *opt)
{
	json_object *v = json_object_object_get(json_object_object_get(medvemodem_uci(), "core"), opt), *o = json_object_new_object();
	size_t i;

	if (v && json_object_is_type(v, json_type_array)) {
		for (i = 0; i < json_object_array_length(v); i++)
			json_object_object_add(o, json_object_get_string(json_object_array_get_idx(v, i)), json_object_new_boolean(1));
	} else if (v && json_object_is_type(v, json_type_string)) {
		char buf[512], *save = NULL, *w;

		snprintf(buf, sizeof(buf), "%s", json_object_get_string(v));
		for (w = strtok_r(buf, " \t", &save); w; w = strtok_r(NULL, " \t", &save))
			json_object_object_add(o, w, json_object_new_boolean(1));
	}
	return o;
}

void modem_class_sync(void)
{
	json_object *yes = core_set("usb_modem"), *no = core_set("usb_not_modem");

	mm_discovery_overrides(yes, no);
	json_object_put(yes);
	json_object_put(no);
}

/* {suspects: [...], modem: [vid:pid...], not_modem: [...]} for the status */
json_object *modem_class_view(void)
{
	json_object *o = json_object_new_object(), *s = mm_discovery_suspects(), *yes = core_set("usb_modem"),
		    *no = core_set("usb_not_modem"), *a;

	json_object_object_add(o, "suspects", s ? json_object_get(s) : json_object_new_array());
	a = json_object_new_array();
	json_object_object_foreach(yes, k, v1) {
		(void)v1;
		json_object_array_add(a, json_object_new_string(k));
	}
	json_object_object_add(o, "modem", a);
	a = json_object_new_array();
	json_object_object_foreach(no, k2, v2) {
		(void)v2;
		json_object_array_add(a, json_object_new_string(k2));
	}
	json_object_object_add(o, "not_modem", a);
	json_object_put(yes);
	json_object_put(no);
	return o;
}

static bool vidpid_ok(const char *s)
{
	int i;

	if (!s || strlen(s) != 9 || s[4] != ':')
		return false;
	for (i = 0; i < 9; i++)
		if (i != 4 && !isxdigit((unsigned char)s[i]))
			return false;
	return true;
}

static json_object *list_without(json_object *set, const char *drop, const char *add)
{
	json_object *a = json_object_new_array();

	json_object_object_foreach(set, k, v) {
		(void)v;
		if (strcmp(k, drop))
			json_object_array_add(a, json_object_new_string(k));
	}
	if (add)
		json_object_array_add(a, json_object_new_string(add));
	return a;
}

static json_object *set_op(const char *opt, json_object *value)
{
	json_object *op = json_object_new_object();

	json_object_object_add(op, "op", json_object_new_string("set"));
	json_object_object_add(op, "package", json_object_new_string("medvemodem"));
	json_object_object_add(op, "section", json_object_new_string("core"));
	json_object_object_add(op, "option", json_object_new_string(opt));
	json_object_object_add(op, "value", value);
	return op;
}

enum { UC_USB, UC_CLASS, __UC_MAX };
const struct blobmsg_policy usb_class_policy[2] = {
	[UC_USB] = { .name = "vidpid", .type = BLOBMSG_TYPE_STRING },
	[UC_CLASS] = { .name = "class", .type = BLOBMSG_TYPE_STRING },
};

/* usb_class {vidpid, class: modem | not_modem | auto} */
int m_usb_class(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
		struct blob_attr *msg)
{
	struct blob_attr *tb[__UC_MAX];
	json_object *o, *ops, *yes, *no;
	const char *cls;
	char vp[16], err[160];
	int i, rc;

	(void)obj; (void)method;
	blobmsg_parse(usb_class_policy, __UC_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[UC_USB] || !tb[UC_CLASS])
		return UBUS_STATUS_INVALID_ARGUMENT;
	snprintf(vp, sizeof(vp), "%s", blobmsg_get_string(tb[UC_USB]));
	for (i = 0; vp[i]; i++)
		vp[i] = (char)tolower((unsigned char)vp[i]);
	cls = blobmsg_get_string(tb[UC_CLASS]);
	if (!vidpid_ok(vp) || (strcmp(cls, "modem") && strcmp(cls, "not_modem") && strcmp(cls, "auto")))
		return UBUS_STATUS_INVALID_ARGUMENT;
	yes = core_set("usb_modem");
	no = core_set("usb_not_modem");
	ops = json_object_new_array();
	json_object_array_add(ops, set_op("usb_modem", list_without(yes, vp, !strcmp(cls, "modem") ? vp : NULL)));
	json_object_array_add(ops, set_op("usb_not_modem", list_without(no, vp, !strcmp(cls, "not_modem") ? vp : NULL)));
	json_object_put(yes);
	json_object_put(no);
	o = json_object_new_object();
	json_object_object_add(o, "vidpid", json_object_new_string(vp));
	json_object_object_add(o, "class", json_object_new_string(cls));
	if (mm_uci_apply(conf_dir, mm_engine_savedir(), ops, err, sizeof(err))) {
		json_object_object_add(o, "error", json_object_new_string(err));
	} else {
		ULOG_INFO("USB %s set by the operator: %s\n", vp, cls);
		modem_class_sync();
		schedule_pass(true);
		json_object_object_add(o, "ok", json_object_new_boolean(true));
	}
	json_object_put(ops);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}
