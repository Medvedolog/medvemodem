/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "daemon.h"
#include "mmgr.h"

/* ---- SIM unlock through ModemManager (the owner's request, 2026-10-17) ----
 *
 * A SIM that asks for its PIN (or, after three wrong ones, its PUK) is unlocked
 * from the page: `mmcli -i <sim> --pin=<PIN>` or `--puk=<PUK> --pin=<new PIN>`,
 * mmcli exec'd with its arguments (no shell), one attempt per modem at a time.
 * The codes are digits only, checked before anything runs; they are never
 * logged, journalled, kept or echoed: the journal says `sim-unlock` done or
 * failed, the reply carries ModemManager's error with every run of 4 or more
 * digits cut out. A PIN try that would be the last one is refused unless the
 * operator confirmed it: a wrong last PIN locks the SIM behind its PUK.
 * After the attempt the modem is read once more (`mmcli -J -m N`, no code in
 * it) so the reply carries the tries left, and ModemManager's view is read
 * again so the page sees the SIM unlocked.
 */
#define MM_SIM_JOBS 2
#define MM_SIM_UNLOCK_MS 20000
#define MM_SIM_READ_MS 10000
#define MM_SIM_SLOT_MS 35000
static struct sim_job {
	bool active, killed, reading, ok, puk, slot, rereg;
	char device_id[160], modem[16];
	char error[200];
	struct uloop_process proc;
	struct uloop_fd out;
	struct uloop_timeout kill;
	char buf[16384];
	size_t len;
	struct ubus_context *ctx;
	struct ubus_request_data req;
} sim_jobs[MM_SIM_JOBS];

enum { SIM_ID, SIM_PIN, SIM_PUK, SIM_CONFIRM, __SIM_MAX };
const struct blobmsg_policy sim_unlock_policy[__SIM_MAX] = {
	[SIM_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[SIM_PIN] = { .name = "pin", .type = BLOBMSG_TYPE_STRING },
	[SIM_PUK] = { .name = "puk", .type = BLOBMSG_TYPE_STRING },
	[SIM_CONFIRM] = { .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
};

/* digits only, min..max of them: nothing a shell or mmcli's parser could read as anything else */
static bool digits(const char *s, size_t min, size_t max)
{
	size_t n = s ? strlen(s) : 0, i;

	if (n < min || n > max)
		return false;
	for (i = 0; i < n; i++)
		if (s[i] < '0' || s[i] > '9')
			return false;
	return true;
}

/* the number after the last '/' of a D-Bus path, or false */
static bool path_index(const char *path, char *out, size_t size)
{
	const char *slash = path ? strrchr(path, '/') : NULL;

	if (!slash || !digits(slash + 1, 1, 6) || strlen(slash + 1) >= size)
		return false;
	snprintf(out, size, "%s", slash + 1);
	return true;
}

/*
 * mmcli's own words after "error:", one line, printable, every run of 4 or
 * more digits dropped: mmcli does not echo the code, but nothing that could
 * be one leaves the engine.
 */
static void sim_error_text(const char *buf, char *out, size_t size)
{
	const char *s = buf ? strstr(buf, "error:") : NULL;
	size_t o = 0;

	out[0] = '\0';
	if (!s)
		return;
	s += 6;
	while (*s == ' ')
		s++;
	while (*s && *s != '\n' && o + 1 < size) {
		if (isdigit((unsigned char)*s)) {
			size_t run = strspn(s, "0123456789");

			if (run < 4) {
				if (o + run + 1 >= size)
					break;
				memcpy(out + o, s, run);
				o += run;
			}
			s += run;
			continue;
		}
		if ((unsigned char)*s >= 0x20 && *s != 0x7f)
			out[o++] = *s;
		s++;
	}
	while (o && out[o - 1] == ' ')
		o--;
	out[o] = '\0';
}

static void sim_close_out(struct sim_job *j)
{
	if (j->out.fd >= 0) {
		if (j->out.registered)
			uloop_fd_delete(&j->out);
		close(j->out.fd);
		j->out.fd = -1;
	}
}

static void sim_drain(struct sim_job *j)
{
	char chunk[2048];
	ssize_t n;

	while (j->out.fd >= 0 && (n = read(j->out.fd, chunk, sizeof(chunk))) > 0) {
		size_t room = sizeof(j->buf) - 1 - j->len, take = (size_t)n < room ? (size_t)n : room;

		memcpy(j->buf + j->len, chunk, take);
		j->len += take;
		j->buf[j->len] = '\0';
	}
}

static void sim_readable(struct uloop_fd *u, unsigned int events)
{
	struct sim_job *j = container_of(u, struct sim_job, out);

	(void)events;
	sim_drain(j);
	if (u->eof || u->error)
		sim_close_out(j);
}

static void sim_kill_fired(struct uloop_timeout *t)
{
	struct sim_job *j = container_of(t, struct sim_job, kill);

	if (j->proc.pid > 0) {
		j->killed = true;
		kill(-j->proc.pid, SIGKILL);
	}
}

static void sim_exited(struct uloop_process *p, int ret);

/* run mmcli with argv (NULL-terminated, argv[0] is mmcli itself); false when it cannot start */
static bool sim_run(struct sim_job *j, char *const argv[], int timeout_ms)
{
	int fds[2], fd;
	pid_t pid;

	if (pipe2(fds, O_CLOEXEC))
		return false;
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return false;
	}
	if (pid == 0) {
		setpgid(0, 0);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0)
			dup2(fd, 0);
		dup2(fds[1], 1);
		dup2(fds[1], 2);
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	j->len = 0;
	j->buf[0] = '\0';
	j->killed = false;
	j->out.fd = fds[0];
	j->out.cb = sim_readable;
	uloop_fd_add(&j->out, ULOOP_READ);
	memset(&j->proc, 0, sizeof(j->proc));
	j->proc.pid = pid;
	j->proc.cb = sim_exited;
	uloop_process_add(&j->proc);
	j->kill.cb = sim_kill_fired;
	uloop_timeout_set(&j->kill, budget_override_ms ? budget_override_ms : timeout_ms);
	return true;
}

static void sim_reply(struct sim_job *j, json_object *mm)
{
	json_object *o = json_object_new_object(), *r = json_object_object_get(mm, "unlock_retries");

	json_object_object_add(o, "device_id", json_object_new_string(j->device_id));
	json_object_object_add(o, "ok", json_object_new_boolean(j->ok));
	if (!j->ok)
		json_object_object_add(o, "error", json_object_new_string(j->error));
	if (r && json_object_object_length(r))
		json_object_object_add(o, "retries", json_object_get(r));
	if (jstr(mm, "unlock_required"))
		json_object_object_add(o, "unlock_required", json_object_new_string(jstr(mm, "unlock_required")));
	reply_json(j->ctx, &j->req, NULL, o);
	ubus_complete_deferred_request(j->ctx, &j->req, UBUS_STATUS_OK);
	json_object_put(o);
}

static void sim_exited(struct uloop_process *p, int ret)
{
	struct sim_job *j = container_of(p, struct sim_job, proc);
	int status = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;

	uloop_timeout_cancel(&j->kill);
	sim_drain(j);
	sim_close_out(j);
	if (j->rereg) {
		/* a new registration asked of ModemManager: the next reads show where it registered */
		j->ok = status == 0 && !j->killed;
		if (!j->ok) {
			sim_error_text(j->buf, j->error, sizeof(j->error));
			if (!j->error[0])
				snprintf(j->error, sizeof(j->error), j->killed ? "no answer from ModemManager in time" :
					 "ModemManager did not register the modem (mmcli exit %d)", status);
		}
		ULOG_INFO("network %s: re-register %s\n", j->device_id, j->ok ? "done" : "failed");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "reregister", j->ok ? "done" : "failed");
		history_dirty = true;
		mm_reread();
		sim_reply(j, NULL);
		memset(j, 0, sizeof(*j));
		j->out.fd = -1;
		return;
	}
	if (j->slot) {
		/* a slot switch: ModemManager probes the modem again; the next read shows the SIM of the new slot */
		j->ok = status == 0 && !j->killed;
		if (!j->ok) {
			sim_error_text(j->buf, j->error, sizeof(j->error));
			if (!j->error[0])
				snprintf(j->error, sizeof(j->error), j->killed ? "no answer from ModemManager in time" :
					 "ModemManager did not switch the slot (mmcli exit %d)", status);
		}
		ULOG_INFO("sim %s: slot switch %s\n", j->device_id, j->ok ? "done" : "failed");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sim-slot", j->ok ? "done" : "failed");
		history_dirty = true;
		mm_reread();
		sim_reply(j, NULL);
		memset(j, 0, sizeof(*j));
		j->out.fd = -1;
		return;
	}
	if (!j->reading) {
		char *argv[] = { (char *)mmcli_path, "-J", "-m", j->modem, NULL };

		j->ok = status == 0 && !j->killed;
		if (!j->ok) {
			sim_error_text(j->buf, j->error, sizeof(j->error));
			if (j->killed)
				snprintf(j->error, sizeof(j->error), "no answer from ModemManager in time");
			else if (!j->error[0])
				snprintf(j->error, sizeof(j->error), "ModemManager did not accept the code (mmcli exit %d)", status);
		}
		/* the result only: neither code goes to the log or the journal */
		ULOG_INFO("sim %s: %s unlock %s\n", j->device_id, j->puk ? "PUK" : "PIN", j->ok ? "done" : "failed");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sim-unlock", j->ok ? "done" : "failed");
		history_dirty = true;
		mm_reread();
		memset(j->buf, 0, sizeof(j->buf));
		j->reading = true;
		if (sim_run(j, argv, MM_SIM_READ_MS))
			return;
		sim_reply(j, NULL);
	} else {
		json_object *modems = status == 0 && !j->killed ? mm_mmgr_parse(j->buf, j->len) : NULL;

		sim_reply(j, modems && json_object_array_length(modems) ? json_object_array_get_idx(modems, 0) : NULL);
		json_object_put(modems);
	}
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
}

static int sim_refuse(struct ubus_context *ctx, struct ubus_request_data *req, const char *id, const char *why,
		      json_object *retries)
{
	json_object *o = json_object_new_object();
	int rc;

	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "ok", json_object_new_boolean(false));
	json_object_object_add(o, "refused", json_object_new_string(why));
	if (retries && json_object_object_length(retries))
		json_object_object_add(o, "retries", json_object_get(retries));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

int m_sim_unlock(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__SIM_MAX];
	json_object *rec, *srv, *mm = NULL, *retries;
	const char *id, *pin, *puk, *control, *lock;
	char sim[16], modem[16], pin_arg[24], puk_arg[24];
	struct sim_job *j = NULL;
	bool confirm, held;
	int i, left;

	(void)obj; (void)method;
	blobmsg_parse(sim_unlock_policy, __SIM_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[SIM_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[SIM_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	pin = tb[SIM_PIN] ? blobmsg_get_string(tb[SIM_PIN]) : NULL;
	puk = tb[SIM_PUK] ? blobmsg_get_string(tb[SIM_PUK]) : NULL;
	confirm = tb[SIM_CONFIRM] && blobmsg_get_bool(tb[SIM_CONFIRM]);
	if (puk && !*puk)
		puk = NULL;
	if (!digits(pin, 4, 8))
		return sim_refuse(ctx, req, id, puk ? "the new PIN: 4 to 8 digits" : "the PIN: 4 to 8 digits", NULL);
	if (puk && !digits(puk, 8, 8))
		return sim_refuse(ctx, req, id, "the PUK: exactly 8 digits", NULL);

	srv = served(rec);
	control = jstr(json_object_object_get(srv, "owner"), "control");
	held = control && !strcmp(control, "modemmanager") && strcmp(mmcli_path, "none") &&
	       mm_devices && json_object_object_get_ex(mm_devices, id, &mm);
	json_object_put(srv);
	if (!held || !path_index(jstr(mm, "sim"), sim, sizeof(sim)) || !path_index(jstr(mm, "object"), modem, sizeof(modem)))
		return sim_refuse(ctx, req, id, held ? "ModemManager reports no SIM in this modem" :
				  "the SIM is unlocked through ModemManager, which does not hold this modem", NULL);
	lock = jstr(mm, "unlock_required");
	retries = json_object_object_get(mm, "unlock_retries");
	if (!lock || (strcmp(lock, "sim-pin") && strcmp(lock, "sim-puk")))
		return sim_refuse(ctx, req, id, "the SIM asks for neither its PIN nor its PUK", retries);
	if (!strcmp(lock, "sim-puk") && !puk)
		return sim_refuse(ctx, req, id, "the SIM is locked by its PUK: the PUK and a new PIN are needed", retries);
	if (!strcmp(lock, "sim-pin") && puk)
		return sim_refuse(ctx, req, id, "the SIM asks for its PIN, not its PUK", retries);
	/* the last try decides: a wrong PIN then locks the SIM behind its PUK, a wrong PUK for good */
	left = json_object_object_get(retries, puk ? "sim-puk" : "sim-pin") ?
	       json_object_get_int(json_object_object_get(retries, puk ? "sim-puk" : "sim-pin")) : -1;
	if (left >= 0 && left <= 1 && !confirm)
		return sim_refuse(ctx, req, id, left == 0 ? "no tries left" :
				  puk ? "one PUK try left: a wrong PUK blocks the SIM for good; confirm to go on" :
				  "one PIN try left: a wrong PIN locks the SIM behind its PUK; confirm to go on", retries);
	if (rec_busy(id))
		return sim_refuse(ctx, req, id, "a recovery is running on this modem", NULL);
	for (i = 0; i < MM_SIM_JOBS; i++)
		if (sim_jobs[i].active && !strcmp(sim_jobs[i].device_id, id))
			return sim_refuse(ctx, req, id, "an unlock of this SIM is running", NULL);
	for (i = 0; i < MM_SIM_JOBS && !j; i++)
		if (!sim_jobs[i].active)
			j = &sim_jobs[i];
	if (!j)
		return sim_refuse(ctx, req, id, "too many unlocks are running", NULL);

	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
	snprintf(j->device_id, sizeof(j->device_id), "%s", id);
	snprintf(j->modem, sizeof(j->modem), "%s", modem);
	j->puk = puk != NULL;
	snprintf(pin_arg, sizeof(pin_arg), "--pin=%s", pin);
	{
		char *with_puk[] = { (char *)mmcli_path, "--timeout=15", "-i", sim, puk_arg, pin_arg, NULL };
		char *with_pin[] = { (char *)mmcli_path, "--timeout=15", "-i", sim, pin_arg, NULL };
		bool started;

		if (puk)
			snprintf(puk_arg, sizeof(puk_arg), "--puk=%s", puk);
		started = sim_run(j, puk ? with_puk : with_pin, MM_SIM_UNLOCK_MS);
		/* the child has its copy: none stays in the engine's memory */
		explicit_bzero(pin_arg, sizeof(pin_arg));
		explicit_bzero(puk_arg, sizeof(puk_arg));
		if (!started) {
			memset(j, 0, sizeof(*j));
			j->out.fd = -1;
			return sim_refuse(ctx, req, id, "cannot start mmcli", NULL);
		}
	}
	j->active = true;
	j->ctx = ctx;
	ubus_defer_request(ctx, req, &j->req);
	return UBUS_STATUS_OK;
}

enum { SLOT_ID, SLOT_SLOT, __SLOT_MAX };
const struct blobmsg_policy sim_slot_policy[__SLOT_MAX] = {
	[SLOT_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[SLOT_SLOT] = { .name = "slot", .type = BLOBMSG_TYPE_INT32 },
};

/*
 * The SIM slot ModemManager uses (the owner, 2026-10-27): `mmcli -m N
 * --set-primary-sim-slot=S`, the way ModemManager itself offers it, so no second client opens the port. The modem
 * is probed again: its connection goes down for that time and comes back with the other SIM.
 */
int m_sim_slot(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	       const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__SLOT_MAX];
	json_object *rec, *srv, *mm = NULL, *slots;
	const char *id, *control;
	char modem[16], arg[40];
	struct sim_job *j = NULL;
	int slot, i;
	bool held;

	(void)obj; (void)method;
	blobmsg_parse(sim_slot_policy, __SLOT_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[SLOT_ID] || !tb[SLOT_SLOT])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[SLOT_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	slot = (int)blobmsg_get_u32(tb[SLOT_SLOT]);
	srv = served(rec);
	control = jstr(json_object_object_get(srv, "owner"), "control");
	held = control && !strcmp(control, "modemmanager") && strcmp(mmcli_path, "none") &&
	       mm_devices && json_object_object_get_ex(mm_devices, id, &mm);
	json_object_put(srv);
	if (!held || !path_index(jstr(mm, "object"), modem, sizeof(modem)))
		return sim_refuse(ctx, req, id, "the slot is switched through ModemManager, which does not hold this modem", NULL);
	slots = json_object_object_get(mm, "sim_slots");
	if (!json_object_is_type(slots, json_type_array) || slot < 1 || (size_t)slot > json_object_array_length(slots))
		return sim_refuse(ctx, req, id, "this modem has no such SIM slot", NULL);
	if (json_object_get_int(json_object_object_get(mm, "primary_sim_slot")) == slot)
		return sim_refuse(ctx, req, id, "this slot is in use already", NULL);
	if (frozen(id))
		return sim_refuse(ctx, req, id, "the modem is frozen by the operator", NULL);
	if (rec_busy(id) || bringup_busy(id))
		return sim_refuse(ctx, req, id, "a recovery or a connection of this modem is running", NULL);
	for (i = 0; i < MM_SIM_JOBS; i++)
		if (sim_jobs[i].active && !strcmp(sim_jobs[i].device_id, id))
			return sim_refuse(ctx, req, id, "a SIM operation of this modem is running", NULL);
	for (i = 0; i < MM_SIM_JOBS && !j; i++)
		if (!sim_jobs[i].active)
			j = &sim_jobs[i];
	if (!j)
		return sim_refuse(ctx, req, id, "too many SIM operations are running", NULL);
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
	j->slot = true;
	snprintf(j->device_id, sizeof(j->device_id), "%s", id);
	snprintf(j->modem, sizeof(j->modem), "%s", modem);
	snprintf(arg, sizeof(arg), "--set-primary-sim-slot=%d", slot);
	{
		char *argv[] = { (char *)mmcli_path, "--timeout=30", "-m", modem, arg, NULL };

		if (!sim_run(j, argv, MM_SIM_SLOT_MS)) {
			memset(j, 0, sizeof(*j));
			j->out.fd = -1;
			return sim_refuse(ctx, req, id, "cannot start mmcli", NULL);
		}
	}
	ULOG_INFO("sim %s: switching to slot %d\n", id, slot);
	j->active = true;
	j->ctx = ctx;
	ubus_defer_request(ctx, req, &j->req);
	return UBUS_STATUS_OK;
}

/*
 * «Перерегистрация в сети» (the owner, 2026-11-06): the gentlest recovery step, on the
 * operator's button. A modem ModemManager holds registers again through it (--3gpp-register-home, the automatic
 * network selection); one with the engine's AT port detaches and attaches (AT+COPS=2, AT+COPS=0). Not automatic
 * until a bench shows it helps where a reconnect does not.
 */
enum { RR_ID, __RR_MAX };
const struct blobmsg_policy reregister_policy[__RR_MAX] = {
	[RR_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
};

int m_reregister(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__RR_MAX];
	json_object *rec, *srv, *mm = NULL;
	const char *id, *control, *node, *why = NULL;
	char modem[16];
	struct sim_job *j = NULL;
	int i;
	bool held;

	(void)obj; (void)method;
	blobmsg_parse(reregister_policy, __RR_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[RR_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[RR_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	if (frozen(id))
		return sim_refuse(ctx, req, id, "the modem is frozen by the operator", NULL);
	if (rec_busy(id) || bringup_busy(id))
		return sim_refuse(ctx, req, id, "a recovery or a connection of this modem is running", NULL);
	srv = served(rec);
	control = jstr(json_object_object_get(srv, "owner"), "control");
	held = control && !strcmp(control, "modemmanager") && strcmp(mmcli_path, "none") &&
	       mm_devices && json_object_object_get_ex(mm_devices, id, &mm) && path_index(jstr(mm, "object"), modem, sizeof(modem));
	if (!held) {
		/* the engine's own AT port: detach, then the automatic selection; the console's answer is the reply */
		json_object *cmds;
		int rc;

		if (!(node = engine_at_port(srv, &why))) {
			json_object_put(srv);
			return sim_refuse(ctx, req, id, why ? why : "neither ModemManager nor the engine's AT port can reach this modem", NULL);
		}
		cmds = json_object_new_array();
		json_object_array_add(cmds, json_object_new_string("AT+COPS=2"));
		json_object_array_add(cmds, json_object_new_string("AT+COPS=0"));
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "reregister", "at");
		history_dirty = true;
		ULOG_INFO("network %s: re-register over AT (COPS=2, COPS=0)\n", id);
		rc = at_begin(ctx, req, rec, AT_CONSOLE, node, cmds, NULL, 30000, NULL);
		json_object_put(srv);
		return rc;
	}
	json_object_put(srv);
	for (i = 0; i < MM_SIM_JOBS; i++)
		if (sim_jobs[i].active && !strcmp(sim_jobs[i].device_id, id))
			return sim_refuse(ctx, req, id, "a SIM or network operation of this modem is running", NULL);
	for (i = 0; i < MM_SIM_JOBS && !j; i++)
		if (!sim_jobs[i].active)
			j = &sim_jobs[i];
	if (!j)
		return sim_refuse(ctx, req, id, "too many SIM operations are running", NULL);
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
	j->rereg = true;
	snprintf(j->device_id, sizeof(j->device_id), "%s", id);
	snprintf(j->modem, sizeof(j->modem), "%s", modem);
	{
		char *argv[] = { (char *)mmcli_path, "--timeout=60", "-m", modem, "--3gpp-register-home", NULL };

		if (!sim_run(j, argv, 65000)) {
			memset(j, 0, sizeof(*j));
			j->out.fd = -1;
			return sim_refuse(ctx, req, id, "cannot start mmcli", NULL);
		}
	}
	ULOG_INFO("network %s: re-register through ModemManager\n", id);
	j->active = true;
	j->ctx = ctx;
	ubus_defer_request(ctx, req, &j->req);
	return UBUS_STATUS_OK;
}

/*
 * The SIM's forbidden networks (EF_FPLMN, 6F7B; the owner, 2026-11-06): read and, on the
 * operator's confirmed button, cleared. A network that refused the SIM once (roaming, a failed registration) is kept
 * there and never tried again by the automatic selection. Over the engine's AT port (AT+CRSM READ/UPDATE BINARY, the
 * 3GPP minimum of 4 networks, 12 bytes); the answer is the console's, the page decodes the networks. A frozen modem or
 * one in a recovery is refused.
 */
enum { FP_ID, FP_CLEAR, __FP_MAX };
const struct blobmsg_policy fplmn_policy[__FP_MAX] = {
	[FP_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[FP_CLEAR] = { .name = "clear", .type = BLOBMSG_TYPE_BOOL },
};

int m_fplmn(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	    const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__FP_MAX];
	json_object *rec, *srv, *cmds;
	const char *id, *node, *why = NULL;
	bool clear;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(fplmn_policy, __FP_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[FP_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[FP_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	clear = tb[FP_CLEAR] && blobmsg_get_bool(tb[FP_CLEAR]);
	if (frozen(id))
		return sim_refuse(ctx, req, id, "the modem is frozen by the operator", NULL);
	if (clear && (rec_busy(id) || bringup_busy(id)))
		return sim_refuse(ctx, req, id, "a recovery or a connection of this modem is running", NULL);
	srv = served(rec);
	node = engine_at_port(srv, &why);
	json_object_put(srv);
	if (!node)
		return sim_refuse(ctx, req, id, why ? why : "the SIM's files are read over the engine's AT port, which this modem has not", NULL);
	cmds = json_object_new_array();
	json_object_array_add(cmds, json_object_new_string(clear ? "AT+CRSM=214,28539,0,0,12,\"FFFFFFFFFFFFFFFFFFFFFFFF\"" :
								  "AT+CRSM=176,28539,0,0,12"));
	if (clear) {
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "fplmn-clear", "at");
		history_dirty = true;
		ULOG_INFO("sim %s: the forbidden networks cleared by the operator\n", id);
	}
	rc = at_begin(ctx, req, rec, AT_CONSOLE, node, cmds, NULL, 5000, NULL);
	return rc;
}

void sim_mgr_stop(void)
{
	int i;

	for (i = 0; i < MM_SIM_JOBS; i++)
		if (sim_jobs[i].active) {
			uloop_timeout_cancel(&sim_jobs[i].kill);
			if (sim_jobs[i].proc.pid > 0) {
				uloop_process_delete(&sim_jobs[i].proc);
				kill(-sim_jobs[i].proc.pid, SIGKILL);
			}
			sim_close_out(&sim_jobs[i]);
			memset(&sim_jobs[i], 0, sizeof(sim_jobs[i]));
			sim_jobs[i].out.fd = -1;
		}
}
