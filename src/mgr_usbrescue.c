// SPDX-License-Identifier: GPL-3.0-only
/*
 * The last resort, by hand only (the owner, 2026-11-09: «давай сделаем как крайняя степень спасения, вручную с поиском
 * gpio»): a modem that answers no step of the ladder, not even its own USB re-authorisation, and no switchable hub port.
 * A router maker's support told a tester to unbind and bind the board's xHCI controller (xhci-mtk, 11200000.usb) or to
 * drop a GPIO named usb-enable.
 *
 *   usb_rescue {id}                          what the modem sits on: the controller, its driver, every other device on it,
 *                                            whether it may be touched, the GPIO lines that look like USB or modem power
 *   usb_rescue_run {id, what, gpio, confirm} controller: the driver's unbind, a pause, bind; gpio: off, a pause, on
 *
 * Never automatic. confirm is the fingerprint usb_rescue gave: a device plugged into the controller meanwhile (or a line
 * gone) refuses the run. Never the controller of the router's own storage (extroot, a root or overlay on a USB disk).
 * The writes run in a child with a time limit: a sysfs write that hangs never hangs the engine. Every modem on the
 * controller gets the event in its journal.
 */
#define _GNU_SOURCE	/* strcasestr: musl declares it only so */
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>
#include <libubox/uloop.h>

#include "daemon.h"
#include "mm_paths.h"
#include "sysfs.h"

#define UR_PAUSE_S 3		/* between unbind and bind, off and on */
#define UR_GPIO_PAUSE_S 5	/* a modem's capacitors drain slower than a controller resets */
#define UR_LIMIT_MS 30000

static struct {
	bool active;
	struct uloop_process proc;
	struct uloop_timeout kill;
	char what[16], target[NAME_MAX + 1], ids[1024];
} job;

static const char *base(const char *p)
{
	const char *s = strrchr(p, '/');

	return s ? s + 1 : p;
}

/* the platform or PCI device of the host controller a USB device hangs from: the parent of its root hub (usbN) */
static bool controller_of(const char *usbname, char *ctl, size_t size)
{
	char link[PATH_MAX], real[PATH_MAX], *s;

	snprintf(link, sizeof(link), "%s/%s", SYS_USB, usbname);
	if (!realpath(link, real))
		return false;
	while ((s = strrchr(real, '/')) && s != real) {
		if (!strncmp(s + 1, "usb", 3) && s[4] >= '0' && s[4] <= '9' && strspn(s + 4, "0123456789") == strlen(s + 4)) {
			*s = '\0';
			snprintf(ctl, size, "%s", real);
			return true;
		}
		*s = '\0';
	}
	return false;
}

static bool driver_of(const char *ctl, char *drv, size_t size)
{
	char link[PATH_MAX];

	snprintf(link, sizeof(link), "%s/driver", ctl);
	return realpath(link, drv) && size >= PATH_MAX;
}

/* the modem the registry knows on a USB device name, or NULL */
static const char *modem_on(const char *usbname, const char **name)
{
	char att[64];
	json_object *rec;

	snprintf(att, sizeof(att), "usb:%s", usbname);
	rec = mm_registry_by_attachment(&registry, att);
	if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present")))
		return NULL;
	*name = daemon_modem_name(jstr(rec, "device_id"));
	return jstr(rec, "device_id");
}

/* every USB device under the controller (root hubs and interfaces left out), the modems named */
static json_object *devices_on(const char *ctl)
{
	json_object *out = json_object_new_array();
	DIR *d = opendir(SYS_USB);
	struct dirent *de;
	size_t n = strlen(ctl);

	while (d && (de = readdir(d))) {
		char link[PATH_MAX], real[PATH_MAX], v[256];
		const char *id, *name = NULL;
		json_object *o;

		if (de->d_name[0] == '.' || strchr(de->d_name, ':') || !strncmp(de->d_name, "usb", 3))
			continue;
		snprintf(link, sizeof(link), "%s/%s", SYS_USB, de->d_name);
		if (!realpath(link, real) || strncmp(real, ctl, n) || real[n] != '/')
			continue;
		o = json_object_new_object();
		json_object_object_add(o, "usb", json_object_new_string(de->d_name));
		{
			char vid[16] = "", pid[16] = "", path[PATH_MAX + 32];

			snprintf(path, sizeof(path), "%s/idVendor", real);
			mm_read_line(path, vid, sizeof(vid));
			snprintf(path, sizeof(path), "%s/idProduct", real);
			mm_read_line(path, pid, sizeof(pid));
			if (*vid && *pid) {
				snprintf(v, sizeof(v), "%.4s:%.4s", vid, pid);
				json_object_object_add(o, "vidpid", json_object_new_string(v));
			}
			snprintf(path, sizeof(path), "%s/product", real);
			if (mm_read_line(path, v, sizeof(v)) && *v)
				json_object_object_add(o, "product", json_object_new_string(v));
		}
		if ((id = modem_on(de->d_name, &name))) {
			json_object_object_add(o, "device_id", json_object_new_string(id));
			if (name && *name)
				json_object_object_add(o, "name", json_object_new_string(name));
		}
		json_object_array_add(out, o);
	}
	if (d)
		closedir(d);
	return out;
}

/* a mounted disk on this controller (ctl NULL: on any USB controller, for a power line that may feed any port): the
 * router's own storage (/, /overlay, /rom: extroot) or the operator's data; pulled from under its filesystem it is lost
 * work or a dead system (regression review 2026-11-11: any mount, not the root's only) */
static const char *storage_guard(const char *ctl)
{
	static char why[320];
	FILE *f = fopen(PROC_MOUNTS, "r");
	char line[512];
	size_t n = ctl ? strlen(ctl) : 0;

	why[0] = '\0';
	while (f && fgets(line, sizeof(line), f)) {
		char dev[128], mnt[128], link[PATH_MAX], real[PATH_MAX], disk[64];
		bool own;
		size_t k;

		if (sscanf(line, "%127s %127s", dev, mnt) != 2 || strncmp(dev, "/dev/sd", 7))
			continue;
		own = !strcmp(mnt, "/") || !strcmp(mnt, "/overlay") || !strcmp(mnt, "/rom");
		snprintf(disk, sizeof(disk), "%s", dev + 5);
		for (k = strlen(disk); k > 2 && disk[k - 1] >= '0' && disk[k - 1] <= '9'; k--)
			disk[k - 1] = '\0';	/* sda1 -> sda */
		snprintf(link, sizeof(link), "%s/%s", SYS_BLOCK, disk);
		if (!realpath(link, real) || !strstr(real, "/usb"))
			continue;	/* not a USB disk (SATA, a card reader on the SoC) */
		if (!ctl || (!strncmp(real, ctl, n) && real[n] == '/')) {
			snprintf(why, sizeof(why), own ? "the router's own storage (%s on %s) is on this controller" :
				 "a mounted disk (%s on %s) is on this controller: unmount it first", dev, mnt);
			if (own)
				break;
		}
	}
	if (f)
		fclose(f);
	return why[0] ? why : NULL;
}

/* the board's GPIO lines that look like USB or modem power: their names only (gpio-export in the board's device tree) */
static json_object *power_gpios(void)
{
	static const char *const words[] = { "usb", "vbus", "modem", "lte", "4g", "5g", "wwan", "pcie", "sim", NULL };
	json_object *out = json_object_new_array();
	DIR *d = opendir(SYS_GPIO);
	struct dirent *de;

	while (d && (de = readdir(d))) {
		char path[PATH_MAX], v[16];
		bool hit = false;
		int i;

		if (de->d_name[0] == '.' || !strncmp(de->d_name, "gpiochip", 8) || !strcmp(de->d_name, "export") ||
		    !strcmp(de->d_name, "unexport"))
			continue;
		for (i = 0; words[i] && !hit; i++)
			hit = strcasestr(de->d_name, words[i]) != NULL;
		/* a SIM detect or select line is no power: never offered */
		if (!hit || (strcasestr(de->d_name, "sim") && !strcasestr(de->d_name, "power")))
			continue;
		snprintf(path, sizeof(path), "%s/%s/direction", SYS_GPIO, de->d_name);
		if (mm_read_line(path, v, sizeof(v)) && strcmp(v, "out"))
			continue;
		snprintf(path, sizeof(path), "%s/%s/value", SYS_GPIO, de->d_name);
		if (!mm_read_line(path, v, sizeof(v)) || (strcmp(v, "0") && strcmp(v, "1")))
			continue;
		{
			json_object *o = json_object_new_object();

			json_object_object_add(o, "name", json_object_new_string(de->d_name));
			json_object_object_add(o, "value", json_object_new_int(atoi(v)));
			json_object_array_add(out, o);
		}
	}
	if (d)
		closedir(d);
	return out;
}

static void fingerprint(json_object *o, char *out, size_t size)
{
	const char *s = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
	unsigned long h = 5381;

	for (; *s; s++)
		h = h * 33 + (unsigned char)*s;
	snprintf(out, size, "%08lx", h & 0xffffffffUL);
}

/* the view of usb_rescue; NULL with *err when the modem has no USB device */
static json_object *view(const char *id, const char **err)
{
	json_object *rec = mm_registry_find(&registry, id), *o, *fp;
	const char *att = jstr(rec, "attachment_id"), *guard;
	char ctl[PATH_MAX], drv[PATH_MAX], fps[16];

	if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present"))) {
		*err = "the modem is not connected";
		return NULL;
	}
	if (!att || strncmp(att, "usb:", 4) || !controller_of(att + 4, ctl, sizeof(ctl))) {
		*err = "the modem is not on a USB controller";
		return NULL;
	}
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	{
		json_object *c = json_object_new_object();

		json_object_object_add(c, "name", json_object_new_string(base(ctl)));
		if (driver_of(ctl, drv, sizeof(drv)))
			json_object_object_add(c, "driver", json_object_new_string(base(drv)));
		json_object_object_add(o, "controller", c);
	}
	json_object_object_add(o, "devices", devices_on(ctl));
	if ((guard = storage_guard(ctl)))
		json_object_object_add(o, "blocked", json_object_new_string(guard));
	else if (!json_object_object_get(json_object_object_get(o, "controller"), "driver"))
		json_object_object_add(o, "blocked", json_object_new_string("the controller has no driver to unbind"));
	json_object_object_add(o, "gpios", power_gpios());
	if (storage_guard(NULL))
		json_object_object_add(o, "gpio_blocked", json_object_new_string(storage_guard(NULL)));
	fp = json_object_new_array();
	json_object_array_add(fp, json_object_new_string(ctl));
	json_object_array_add(fp, json_object_get(json_object_object_get(o, "devices")));
	json_object_array_add(fp, json_object_get(json_object_object_get(o, "gpios")));
	{
		/* the values of the lines change by themselves: their names only */
		json_object *g = json_object_array_get_idx(fp, 2), *names = json_object_new_array();
		size_t i;

		for (i = 0; i < json_object_array_length(g); i++)
			json_object_array_add(names, json_object_get(json_object_object_get(json_object_array_get_idx(g, i), "name")));
		json_object_array_put_idx(fp, 2, names);
	}
	fingerprint(fp, fps, sizeof(fps));
	json_object_put(fp);
	json_object_object_add(o, "confirm", json_object_new_string(fps));
	json_object_object_add(o, "running", json_object_new_boolean(job.active));
	return o;
}

enum { UR_ID, __UR_MAX };
const struct blobmsg_policy usb_rescue_policy[1] = {
	[UR_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
};

int m_usb_rescue(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
		 struct blob_attr *msg)
{
	struct blob_attr *tb[__UR_MAX];
	json_object *o;
	const char *err = NULL;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(usb_rescue_policy, __UR_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[UR_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	o = view(blobmsg_get_string(tb[UR_ID]), &err);
	if (!o) {
		o = json_object_new_object();
		json_object_object_add(o, "device_id", json_object_new_string(blobmsg_get_string(tb[UR_ID])));
		json_object_object_add(o, "refused", json_object_new_string(err));
	}
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

static void journal(const char *kind, const char *detail)
{
	char ids[sizeof(job.ids)], *save = NULL, *id;

	snprintf(ids, sizeof(ids), "%s", job.ids);
	for (id = strtok_r(ids, " ", &save); id; id = strtok_r(NULL, " ", &save))
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), kind, detail);
	history_dirty = true;
}

static void job_done(struct uloop_process *p, int ret)
{
	char detail[320];
	bool ok = WIFEXITED(ret) && WEXITSTATUS(ret) == 0;

	(void)p;
	uloop_timeout_cancel(&job.kill);
	job.active = false;
	snprintf(detail, sizeof(detail), "%s %s: %s", job.what, job.target, ok ? "done" : WIFSIGNALED(ret) ? "stopped: it hung" : "failed");
	ULOG_WARN("usb rescue by the operator, %s\n", detail);
	journal("usb-rescue", detail);
	schedule_pass(true);
}

static void job_kill(struct uloop_timeout *t)
{
	(void)t;
	if (job.active && job.proc.pid > 0)
		kill(job.proc.pid, SIGKILL);
}

static int write_file(const char *path, const char *v)
{
	FILE *f = fopen(path, "w");
	int rc;

	if (!f)
		return -1;
	rc = fputs(v, f) < 0 ? -1 : 0;
	if (fclose(f))
		rc = -1;
	return rc;
}

enum { URR_ID, URR_WHAT, URR_GPIO, URR_CONFIRM, __URR_MAX };
const struct blobmsg_policy usb_rescue_run_policy[4] = {
	[URR_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[URR_WHAT] = { .name = "what", .type = BLOBMSG_TYPE_STRING },
	[URR_GPIO] = { .name = "gpio", .type = BLOBMSG_TYPE_STRING },
	[URR_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_STRING },
};

int m_usb_rescue_run(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
		     struct blob_attr *msg)
{
	struct blob_attr *tb[__URR_MAX];
	json_object *v, *o = json_object_new_object(), *devs;
	const char *id, *what, *gpio = NULL, *err = NULL, *refused = NULL;
	char ctl[PATH_MAX], drv[PATH_MAX], path[PATH_MAX + 64], value[8] = "";
	size_t i;
	pid_t pid;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(usb_rescue_run_policy, __URR_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[URR_ID] || !tb[URR_WHAT] || !tb[URR_CONFIRM]) {
		json_object_put(o);
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	id = blobmsg_get_string(tb[URR_ID]);
	what = blobmsg_get_string(tb[URR_WHAT]);
	if (strcmp(what, "controller") && strcmp(what, "gpio")) {
		json_object_put(o);
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	json_object_object_add(o, "device_id", json_object_new_string(id));
	v = view(id, &err);
	if (!v)
		refused = err;
	else if (job.active)
		refused = "a rescue is running";
	else if (strcmp(jstr(v, "confirm"), blobmsg_get_string(tb[URR_CONFIRM])))
		refused = "the devices on the controller changed: look again before you confirm";
	else if (!strcmp(what, "controller") && jstr(v, "blocked"))
		refused = jstr(v, "blocked");
	else if (!strcmp(what, "gpio") && storage_guard(NULL))
		refused = storage_guard(NULL);	/* the line may feed any port: no mounted USB disk anywhere */
	else if (!strcmp(what, "gpio")) {
		json_object *g = json_object_object_get(v, "gpios");

		gpio = tb[URR_GPIO] ? blobmsg_get_string(tb[URR_GPIO]) : NULL;
		for (i = 0; gpio && i < json_object_array_length(g); i++)
			if (!strcmp(jstr(json_object_array_get_idx(g, i), "name"), gpio))
				break;
		if (!gpio || i == json_object_array_length(g))
			refused = "no such power line";
		else
			snprintf(value, sizeof(value), "%d", json_object_get_int(json_object_object_get(json_object_array_get_idx(g, i), "value")));
	}
	if (refused) {
		json_object_object_add(o, "refused", json_object_new_string(refused));
		json_object_put(v);
		rc = reply_json(ctx, req, NULL, o);
		json_object_put(o);
		return rc;
	}
	/* who goes down with it: every modem on the controller gets the event (a GPIO line is the whole board's) */
	job.ids[0] = '\0';
	devs = json_object_object_get(v, "devices");
	for (i = 0; i < json_object_array_length(devs); i++) {
		const char *did = jstr(json_object_array_get_idx(devs, i), "device_id");

		if (did && strlen(job.ids) + strlen(did) + 2 < sizeof(job.ids))
			snprintf(job.ids + strlen(job.ids), sizeof(job.ids) - strlen(job.ids), "%s%s", job.ids[0] ? " " : "", did);
	}
	if (!job.ids[0])
		snprintf(job.ids, sizeof(job.ids), "%s", id);
	controller_of(jstr(mm_registry_find(&registry, id), "attachment_id") + 4, ctl, sizeof(ctl));
	snprintf(job.what, sizeof(job.what), "%s", what);
	snprintf(job.target, sizeof(job.target), "%s", !strcmp(what, "gpio") ? gpio : base(ctl));
	if (!strcmp(what, "controller") && !driver_of(ctl, drv, sizeof(drv))) {
		json_object_object_add(o, "refused", json_object_new_string("the controller has no driver to unbind"));
		goto reply;
	}
	journal("usb-rescue", !strcmp(what, "gpio") ? "the power line is switched off and on by the operator" :
		"the USB controller is restarted by the operator");
	pid = fork();
	if (pid < 0) {
		json_object_object_add(o, "refused", json_object_new_string("could not start"));
		goto reply;
	}
	if (pid == 0) {
		int bad = 0;

		/* a controller's unbind can stay in the kernel for good: not with the engine's ubus socket (review 2026-11-15) */
		for (int fd = 3; fd < 1024; fd++)
			close(fd);
		if (!strcmp(what, "controller")) {
			snprintf(path, sizeof(path), "%s/unbind", drv);
			bad |= write_file(path, base(ctl));
			sleep(UR_PAUSE_S);
			snprintf(path, sizeof(path), "%s/bind", drv);
			bad |= write_file(path, base(ctl));
		} else {
			snprintf(path, sizeof(path), "%s/%s/value", SYS_GPIO, gpio);
			bad |= write_file(path, strcmp(value, "1") ? "1" : "0");
			sleep(UR_GPIO_PAUSE_S);
			bad |= write_file(path, value);
		}
		_exit(bad ? 1 : 0);
	}
	job.active = true;
	job.proc.pid = pid;
	job.proc.cb = job_done;
	uloop_process_add(&job.proc);
	job.kill.cb = job_kill;
	uloop_timeout_set(&job.kill, UR_LIMIT_MS);
	ULOG_WARN("usb rescue by the operator: %s %s (%s)\n", what, job.target, job.ids);
	json_object_object_add(o, "result", json_object_new_string("started"));
	json_object_object_add(o, "target", json_object_new_string(job.target));
reply:
	json_object_put(v);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* the service stops: a rescue under way is stopped with it, not left to run beside the next start (audit STAB-05) */
void usbrescue_stop(void)
{
	if (!job.active)
		return;
	uloop_timeout_cancel(&job.kill);
	if (job.proc.pid > 0) {
		kill(-job.proc.pid, SIGKILL);
		kill(job.proc.pid, SIGKILL);
	}
	uloop_process_delete(&job.proc);
	job.active = false;
}
