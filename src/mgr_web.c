/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "daemon.h"
#include "subnet.h"
#include "watch.h"

/* ---- the signal from a modem's own web (HANDOFF 164) ----
 *
 * A HiLink or ZTE modem (owner control "hilink": a cdc_ether/rndis network
 * device and no AT port) gives its signal only through its web page. The CLI's
 * `web-signal` reads it (src/websig.c) through the modem's own network device,
 * from the gateway netifd got there, or the vendor's usual address; one reading
 * per modem at a time, its answer kept like the AT reader's. The web password
 * is the device section's web_password, or the web's default when there is
 * none: it goes to the worker on its stdin, never in its arguments, the log or
 * a reply. The worker keeps the modem's session cookie and the time of its last
 * login try in a state file of the modem's own, and tries a login at most once
 * in five minutes. A reading the web refused (a password is wanted, the web is
 * locked) is kept a minute: the page asking every 30 s starts no new worker.
 */
#define MM_WEB_JOBS 4
#define MM_WEB_MS 25000
#define MM_WEB_REFUSED_KEEP_MS 60000
#define MM_WEB_OUT_MAX (512 * 1024)
static struct web_job {
	bool active, killed;
	char device_id[160];
	long long generation;
	struct uloop_process proc;
	struct uloop_fd out;
	struct uloop_timeout kill;
	char *buf;		/* what the worker printed (an SMS list can be long) */
	size_t len, cap;
	char op[12];		/* r94: "list", "send", "delete" for the SMS through the web; "" for the signal */
	bool act;		/* r117: op is "info", "ussd" or "reboot" of a HiLink web (web-act) */
	int index;
	bool deferred;
	struct ubus_context *ctx;
	struct ubus_request_data req;
} web_jobs[MM_WEB_JOBS];
static json_object *web_cache;	/* device_id -> {generation, at, reply} */
static json_object *web_auth_said;	/* device_id -> the login outcome last put in the history */
static json_object *web_models;	/* device_id -> the model its web named (HiLink DeviceName, ZTE model_name) */
/* device_id -> a hash of the password last handed over (RAM): a new one is tried without the five minutes' wait */
static json_object *web_pw_seen;

/* the device section's option, or NULL */
static const char *device_option(const char *id, const char *name)
{
	json_object *img = medvemodem_uci();

	if (!img)
		return NULL;
	json_object_object_foreach(img, sec, s) {
		(void)sec;
		if (jstr(s, ".type") && !strcmp(jstr(s, ".type"), "device") && jstr(s, "id") && !strcmp(jstr(s, "id"), id))
			return jstr(s, name) && *jstr(s, name) ? jstr(s, name) : NULL;
	}
	return NULL;
}

/* the web's model names live in RAM across an engine restart (HANDOFF 200 item 8: after the crash the E3372 lost
 * its name until the next web reading); run_dir, not flash: a reboot asks the web again anyway */
static bool web_models_loaded;

static void web_models_path(char *path, size_t size)
{
	snprintf(path, size, "%s/web-models.json", run_dir);
}

static void web_models_load(void)
{
	char path[PATH_MAX];
	json_object *o;

	if (web_models_loaded)
		return;
	web_models_loaded = true;
	web_models_path(path, sizeof(path));
	o = json_object_from_file(path);
	if (o && json_object_is_type(o, json_type_object) && !web_models)
		web_models = o;
	else
		json_object_put(o);
}

static void web_models_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	web_models_path(path, sizeof(path));
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(run_dir, 0755);
	if (!web_models || json_object_to_file_ext(tmp, web_models, JSON_C_TO_STRING_PLAIN) || rename(tmp, path))
		unlink(tmp);
}

/* the model the modem's web named, or NULL (borrowed) */
const char *web_model(const char *id)
{
	web_models_load();
	return web_models && id ? jstr(web_models, id) : NULL;
}

bool web_password_saved(const char *id)
{
	return device_option(id, "web_password") != NULL;
}

static void web_close_out(struct web_job *j)
{
	if (j->out.fd >= 0) {
		if (j->out.registered)
			uloop_fd_delete(&j->out);
		close(j->out.fd);
		j->out.fd = -1;
	}
}

static void web_drain(struct web_job *j)
{
	char chunk[2048];
	ssize_t n;

	while (j->out.fd >= 0 && (n = read(j->out.fd, chunk, sizeof(chunk))) > 0) {
		size_t take;

		if (j->len + (size_t)n + 1 > j->cap && j->cap < MM_WEB_OUT_MAX) {
			size_t cap = j->cap ? j->cap * 2 : 16384;
			char *b;

			while (cap < j->len + (size_t)n + 1 && cap < MM_WEB_OUT_MAX)
				cap *= 2;
			if (cap > MM_WEB_OUT_MAX)
				cap = MM_WEB_OUT_MAX;
			if ((b = realloc(j->buf, cap))) {
				j->buf = b;
				j->cap = cap;
			}
		}
		if (!j->buf)
			continue;
		take = j->cap - 1 - j->len < (size_t)n ? j->cap - 1 - j->len : (size_t)n;
		memcpy(j->buf + j->len, chunk, take);
		j->len += take;
		j->buf[j->len] = '\0';
	}
}

static void web_readable(struct uloop_fd *u, unsigned int events)
{
	struct web_job *j = container_of(u, struct web_job, out);

	(void)events;
	web_drain(j);
	if (u->eof || u->error)
		web_close_out(j);
}

static void web_kill_fired(struct uloop_timeout *t)
{
	struct web_job *j = container_of(t, struct web_job, kill);

	if (j->proc.pid > 0) {
		j->killed = true;
		kill(-j->proc.pid, SIGKILL);
	}
}

/* the reply the page reads: the AT reader's shape, source "web", and how the web answered */
static json_object *web_reply(const char *id, json_object *r, long long age)
{
	json_object *o = json_object_new_object(), *web = json_object_new_object();

	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "source", json_object_new_string("web"));
	json_object_object_add(o, "age_ms", json_object_new_int64(age));
	if (json_object_object_get(r, "metrics"))
		json_object_object_add(o, "metrics", json_object_get(metrics_clean(json_object_object_get(r, "metrics"))));
	else
		json_object_object_add(o, "refused", json_object_new_string(jstr(r, "error") ? jstr(r, "error") :
									   "the modem's web did not give the signal"));
	json_object_object_foreach(r, k, v)
		if (!strcmp(k, "kind") || !strcmp(k, "host") || !strcmp(k, "auth") || !strcmp(k, "wait_s") || !strcmp(k, "sim") || !strcmp(k, "network") ||
		    !strcmp(k, "data_link") || !strcmp(k, "sim_code"))
			json_object_object_add(web, k, json_object_get(v));
	json_object_object_add(web, "password_saved", json_object_new_boolean(web_password_saved(id)));
	json_object_object_add(o, "web", web);
	return o;
}

static void web_exited(struct uloop_process *p, int ret)
{
	struct web_job *j = container_of(p, struct web_job, proc);
	json_object *r, *o, *entry;

	(void)ret;
	uloop_timeout_cancel(&j->kill);
	web_drain(j);
	web_close_out(j);
	r = j->killed || !j->buf ? NULL : json_tokener_parse(j->buf);
	if (j->buf) {
		explicit_bzero(j->buf, j->len);	/* SMS texts: nowhere but in the answer */
		free(j->buf);
		j->buf = NULL;
	}
	if (!r || !json_object_is_type(r, json_type_object)) {
		json_object_put(r);
		r = json_object_new_object();
		json_object_object_add(r, "error", json_object_new_string(j->killed ? "the modem's web did not answer in time" :
										    "the web reader failed"));
	}
	if (j->act) {
		/* r117: the USSD answer goes to the USSD state the page asks; info and reboot answer their caller */
		json_object *o2 = json_object_new_object();

		if (!strcmp(j->op, "ussd"))
			ussd_web_done(j->device_id, r);
		else if (!strcmp(j->op, "subnet"))
			clash_moved(j->device_id, r);
		json_object_object_add(o2, "device_id", json_object_new_string(j->device_id));
		json_object_object_foreach(r, k, v)
			if (strcmp(k, "answer"))	/* the USSD answer only through ussd_status */
				json_object_object_add(o2, k, json_object_get(v));
		if (!strcmp(j->op, "reboot"))
			ULOG_WARN("web %s: reboot asked through the web: %s\n", j->device_id,
				  json_object_get_boolean(json_object_object_get(r, "ok")) ? "accepted" : "refused");
		if (j->deferred) {
			reply_json(j->ctx, &j->req, NULL, o2);
			ubus_complete_deferred_request(j->ctx, &j->req, UBUS_STATUS_OK);
		}
		json_object_put(o2);
		json_object_put(r);
		memset(j, 0, sizeof(*j));
		j->out.fd = -1;
		return;
	}
	if (j->op[0]) {
		/* r94: the SMS through the web go to the SMS manager, its cache and its answers */
		sms_web_done(j->device_id, j->op, j->index, r, j->deferred ? j->ctx : NULL, &j->req);
		json_object_put(r);
		memset(j, 0, sizeof(*j));
		j->out.fd = -1;
		return;
	}
	/* the stick's IMEI (websig, HiLink): its salted key for the identity, then gone from the answer */
	{
		json_object *imei = json_object_object_get(r, "id_imei");

		if (imei && json_object_is_type(imei, json_type_string))
			identity_web_imei(j->device_id, j->generation, json_object_get_string(imei));
		if (imei) {
			explicit_bzero((char *)json_object_get_string(imei), (size_t)json_object_get_string_len(imei));
			json_object_object_del(r, "id_imei");
		}
	}
	o = web_reply(j->device_id, r, 0);
	/* a late answer of a modem gone or come back since (another generation) neither names it nor feeds its history
	 * (second audit 2026-10-21): the caller still gets it */
	{
		json_object *rec = mm_registry_find(&registry, j->device_id);

		if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present")) ||
		    json_object_get_int64(json_object_object_get(rec, "generation")) != j->generation)
			goto answer;
	}
	entry = json_object_new_object();
	json_object_object_add(entry, "generation", json_object_new_int64(j->generation));
	json_object_object_add(entry, "at", json_object_new_int64(now_mono_ms()));
	json_object_object_add(entry, "reply", r);
	json_object_object_add(web_cache, j->device_id, entry);
	/* a HiLink stick has one USB ID for several models (E3372h, E8372h: 12d1:14db/14dc) and no ModemManager to
	 * name it: its own web does, and the name stays when a later reading is refused */
	if (jstr(json_object_object_get(r, "metrics"), "model")) {
		const char *model = jstr(json_object_object_get(r, "metrics"), "model");

		web_models_load();
		if (!web_models)
			web_models = json_object_new_object();
		if (!jstr(web_models, j->device_id) || strcmp(jstr(web_models, j->device_id), model)) {
			json_object_object_add(web_models, j->device_id, json_object_new_string(model));
			web_models_save();
		}
	}
	/* the signal history from the web too (the owner, 2026-10-20: a HiLink's «История сигнала» stayed empty) */
	signal_sample_reading(j->device_id, json_object_object_get(r, "metrics"), (long long)time(NULL));
	if (jstr(r, "auth") && strcmp(jstr(r, "auth"), "none") && strcmp(jstr(r, "auth"), "waiting")) {
		ULOG_INFO("web %s: %s login: %s\n", j->device_id, jstr(r, "kind") ? jstr(r, "kind") : "?", jstr(r, "auth"));
		/* the event history says each new outcome of the login once (the owner, r123: «в истории событий не
		 * видно этого всего») */
		if (!web_auth_said)
			web_auth_said = json_object_new_object();
		if (!jstr(web_auth_said, j->device_id) || strcmp(jstr(web_auth_said, j->device_id), jstr(r, "auth"))) {
			json_object_object_add(web_auth_said, j->device_id, json_object_new_string(jstr(r, "auth")));
			mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "web-login", jstr(r, "auth"));
			history_dirty = true;
		}
	}
	r = NULL;	/* the cache holds it */
answer:
	json_object_put(r);
	if (j->deferred) {
		reply_json(j->ctx, &j->req, NULL, o);
		ubus_complete_deferred_request(j->ctx, &j->req, UBUS_STATUS_OK);
	}
	json_object_put(o);
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
}

/* the modem's web address: the gateway netifd has on its device, else the vendor's usual one */
static const char *web_host(json_object *rec, json_object *srv, const char **kind)
{
	const char *gw = jstr(json_object_object_get(json_object_object_get(srv, "session"), "netifd"), "gateway4");
	const char *vidpid = jstr(rec, "vidpid") ? jstr(rec, "vidpid") : "";

	/* Brovi's E3372-325 (VID 3566, sold as Huawei; bench 2026-10-20: the E3372 came back as 3566:2001 after a replug)
	 * is a HiLink web as well: asked as one, not guessed as ZTE when its session call answers otherwise */
	*kind = !strncmp(vidpid, "12d1:", 5) || !strncmp(vidpid, "3566:", 5) ? "huawei" :
		/* Yota's sticks (Gemtek, GCT LU150): a plain /status (2026-11-08); the ZTE MF922 with Yota's firmware
		 * (19d2:0565) may answer either: told by the reading (/status first, ZTE's goform after) */
		!strncmp(vidpid, "15a9:", 5) || !strcmp(vidpid, "1076:8002") ? "yota" :
		!strcmp(vidpid, "19d2:0565") ? "auto" : !strncmp(vidpid, "19d2:", 5) ? "zte" : "auto";
	/* an unknown vendor's web as the last reading found it (a «Mobile.Router» MF150-4 is "mark", 2026-10-30) */
	if (!strcmp(*kind, "auto") && web_cache) {
		json_object *c = json_object_object_get(web_cache, jstr(rec, "device_id") ? jstr(rec, "device_id") : "");
		const char *k = jstr(json_object_object_get(c, "reply"), "kind");

		if (k && json_object_get_int64(json_object_object_get(c, "generation")) ==
			 json_object_get_int64(json_object_object_get(rec, "generation")) &&
		    (!strcmp(k, "mark") || !strcmp(k, "uz") || !strcmp(k, "zte") || !strcmp(k, "huawei") || !strcmp(k, "yota")))
			*kind = k;
	}
	if ((!gw || !*gw)) {
		/* no gateway in the lease (a UFI whose SIM has no data): the first address of its subnet */
		static char first[24];
		json_object *a4 = json_object_object_get(json_object_object_get(json_object_object_get(srv, "session"), "netifd"),
							 "ipv4_addresses");

		if (json_object_is_type(a4, json_type_array) && json_object_array_length(a4))
			gw = mm_subnet_first_host(json_object_get_string(json_object_array_get_idx(a4, 0)), first, sizeof(first));
	}
	if (gw && *gw && strspn(gw, "0123456789.") == strlen(gw))
		return gw;
	return !strcmp(*kind, "huawei") ? "192.168.8.1" : !strcmp(*kind, "zte") ? "192.168.0.1" : NULL;
}

static bool web_spawn(struct web_job *j, const char *host, const char *dev, const char *kind, const char *state, const char *password,
		      bool fresh, const char *payload)
{
	const char *cli = getenv("MEDVEMODEM_CLI");
	char index[16];
	const char *argv[10] = { cli && *cli ? cli : "/usr/sbin/medvemodem", j->act ? "web-act" : j->op[0] ? "web-sms" : "web-signal",
				 host, dev, kind, state };
	int out[2], in[2], fd, a = 6;

	if (j->op[0]) {
		argv[a++] = j->op;
		if (j->index >= 0 && !strcmp(j->op, "delete")) {
			snprintf(index, sizeof(index), "%d", j->index);
			argv[a++] = index;
		}
	}
	if (fresh)
		argv[a++] = "fresh";
	argv[a] = NULL;
	pid_t pid;

	if (pipe2(out, O_CLOEXEC))
		return false;
	/* a socket, not a pipe, for the password: a worker that died first must not SIGPIPE the engine */
	if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, in)) {
		close(out[0]);
		close(out[1]);
		return false;
	}
	pid = fork();
	if (pid < 0) {
		close(out[0]); close(out[1]); close(in[0]); close(in[1]);
		return false;
	}
	if (pid == 0) {
		setpgid(0, 0);
		dup2(in[0], 0);
		dup2(out[1], 1);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0)
			dup2(fd, 2);
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execv(argv[0], (char *const *)argv);
		_exit(127);
	}
	close(out[1]);
	close(in[0]);
	/* the password: one line on the worker's stdin (the socket buffer holds it whole) */
	{
		size_t n = password ? strlen(password) : 0;

		if ((n && send(in[1], password, n, MSG_NOSIGNAL) != (ssize_t)n) || send(in[1], "\n", 1, MSG_NOSIGNAL) != 1)
			ULOG_WARN("web %s: the password did not reach the reader\n", j->device_id);
		/* the message to send: a line after the password (never in the arguments, which ps shows) */
		if (payload && *payload && (send(in[1], payload, strlen(payload), MSG_NOSIGNAL) != (ssize_t)strlen(payload) ||
					    send(in[1], "\n", 1, MSG_NOSIGNAL) != 1))
			ULOG_WARN("web %s: the message did not reach the reader\n", j->device_id);
	}
	close(in[1]);
	fcntl(out[0], F_SETFL, fcntl(out[0], F_GETFL) | O_NONBLOCK);
	j->len = j->cap = 0;
	j->buf = NULL;
	j->out.fd = out[0];
	j->out.cb = web_readable;
	uloop_fd_add(&j->out, ULOOP_READ);
	memset(&j->proc, 0, sizeof(j->proc));
	j->proc.pid = pid;
	j->proc.cb = web_exited;
	uloop_process_add(&j->proc);
	j->kill.cb = web_kill_fired;
	uloop_timeout_set(&j->kill, budget_override_ms ? budget_override_ms : MM_WEB_MS);
	return true;
}

static int web_refuse(struct ubus_context *ctx, struct ubus_request_data *req, const char *id, const char *why)
{
	json_object *o = json_object_new_object();
	int rc;

	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "source", json_object_new_string("web"));
	json_object_object_add(o, "refused", json_object_new_string(why));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* one state file per modem: its cookie and its last login try; the folder made on the way */
static void web_state_path(const char *id, char *state, size_t size)
{
	size_t n;

	snprintf(state, size, "%s/web", run_dir);
	mkdir(run_dir, 0755);
	mkdir(state, 0700);
	n = strlen(state);
	snprintf(state + n, size - n, "/");
	for (const char *p = id; *p && strlen(state) + 2 < size; p++) {
		size_t l = strlen(state);

		state[l] = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') || *p == '-' || *p == '.' ? *p : '_';
		state[l + 1] = '\0';
	}
}

/* a reader for the modem's web, without a caller (the background read) or for one (web_metrics); NULL with why */
static struct web_job *web_start_op(json_object *rec, json_object *srv, const char *op, int index, const char *payload, const char **why,
				     bool act)
{
	const char *id = jstr(rec, "device_id"), *kind, *host = web_host(rec, srv, &kind), *dev;
	json_object *net = json_object_object_get(json_object_object_get(rec, "ports"), "net");
	struct web_job *j = NULL;
	char state[512];
	bool fresh;
	int i;

	for (i = 0; i < MM_WEB_JOBS; i++) {
		/* one signal reading and one SMS request per modem at a time */
		if (web_jobs[i].active && !strcmp(web_jobs[i].device_id, id) && !web_jobs[i].op[0] == !op) {
			*why = op ? "an SMS request to the modem's web is running" : "the modem's web is being read";
			return NULL;
		}
		if (!web_jobs[i].active && !j)
			j = &web_jobs[i];
	}
	/* a phone in tethering has no web of its own: its gateway is the phone (the phone kind, 2026-10-19) */
	if (!strncmp(jstr(json_object_object_get(srv, "profile"), "id") ? jstr(json_object_object_get(srv, "profile"), "id") : "", "phone-", 6)) {
		*why = "a phone has no web of its own";
		return NULL;
	}
	if (!host) {
		*why = "the modem's web address is not known yet";
		return NULL;
	}
	if (!j) {
		*why = "busy";
		return NULL;
	}
	dev = net && json_object_array_length(net) ? json_object_get_string(json_object_array_get_idx(net, 0)) : "-";
	web_state_path(id, state, sizeof(state));
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
	snprintf(j->device_id, sizeof(j->device_id), "%s", id);
	snprintf(j->op, sizeof(j->op), "%s", op ? op : "");
	j->act = act;
	j->index = index;
	j->generation = json_object_get_int64(json_object_object_get(rec, "generation"));
	/* the password changed since the last reading: the operator's new one goes at once */
	{
		const char *pw = device_option(id, "web_password");
		unsigned long long h = 1469598103934665603ULL;
		char hs[24];

		for (const char *p = pw ? pw : ""; *p; p++)
			h = (h ^ (unsigned char)*p) * 1099511628211ULL;
		snprintf(hs, sizeof(hs), "%llx", h);
		if (!web_pw_seen)
			web_pw_seen = json_object_new_object();
		fresh = jstr(web_pw_seen, id) && strcmp(jstr(web_pw_seen, id), hs);
		json_object_object_add(web_pw_seen, id, json_object_new_string(hs));
	}
	if (!web_spawn(j, host, dev && *dev ? dev : "-", kind, state, device_option(id, "web_password"), fresh, payload)) {
		memset(j, 0, sizeof(*j));
		j->out.fd = -1;
		*why = "cannot start the web reader";
		return NULL;
	}
	j->active = true;
	return j;
}

static struct web_job *web_start(json_object *rec, json_object *srv, const char **why)
{
	return web_start_op(rec, srv, NULL, -1, NULL, why, false);
}

/*
 * r94: a HiLink modem's SMS through its web; a ZTE web's from r95. ctx/req: the caller that
 * waits for the answer (deferred here), or NULL for the background list. false with why.
 */
bool web_sms_capable(json_object *rec, json_object *srv)
{
	const char *kind = NULL, *ctl = jstr(json_object_object_get(srv, "owner"), "control");

	/* a HiLink or a ZTE web (r95), a «Mobile.Router» one (MF150-4, 2026-10-30); an unknown web is not tried */
	return ctl && !strcmp(ctl, "hilink") && web_host(rec, srv, &kind) && kind &&
	       (!strcmp(kind, "huawei") || !strcmp(kind, "zte") || !strcmp(kind, "mark"));
}

bool web_sms(json_object *rec, json_object *srv, const char *op, const char *number, const char *text, int index,
	     struct ubus_context *ctx, struct ubus_request_data *req, const char **why)
{
	struct web_job *j;
	char *payload = NULL;

	if (number && text) {
		json_object *p = json_object_new_object();

		json_object_object_add(p, "number", json_object_new_string(number));
		json_object_object_add(p, "text", json_object_new_string(text));
		payload = strdup(json_object_to_json_string_ext(p, JSON_C_TO_STRING_PLAIN));
		json_object_put(p);
	}
	j = web_start_op(rec, srv, op, index, payload, why, false);
	if (payload) {
		explicit_bzero(payload, strlen(payload));
		free(payload);
	}
	if (!j)
		return false;
	if (ctx) {
		j->ctx = ctx;
		j->deferred = true;
		ubus_defer_request(ctx, req, &j->req);
	}
	return true;
}

/* r117: a HiLink web only (its API answers so); a ZTE or an unknown web is not asked */
bool web_huawei(json_object *rec, json_object *srv)
{
	const char *kind = NULL, *ctl = jstr(json_object_object_get(srv, "owner"), "control");

	return ctl && !strcmp(ctl, "hilink") && web_host(rec, srv, &kind) && kind && !strcmp(kind, "huawei");
}

/* 2026-11-03: a ZTE goform web (MF79/MF823/MF833-class sticks): the actions a HiLink's web has, but clearing the
 * traffic counters */
bool web_zte(json_object *rec, json_object *srv)
{
	const char *kind = NULL, *ctl = jstr(json_object_object_get(srv, "owner"), "control");

	return ctl && !strcmp(ctl, "hilink") && web_host(rec, srv, &kind) && kind && !strcmp(kind, "zte");
}

/* 2026-10-30: a «Mobile.Router» web (MF150-4) the last reading recognised */
bool web_mark(json_object *rec, json_object *srv)
{
	const char *kind = NULL, *ctl = jstr(json_object_object_get(srv, "owner"), "control");

	return ctl && !strcmp(ctl, "hilink") && web_host(rec, srv, &kind) && kind && !strcmp(kind, "mark");
}

/* 2026-10-30: a UZ801 UFI's /ajax web the last reading recognised (info and reboot) */
bool web_uz(json_object *rec, json_object *srv)
{
	const char *kind = NULL, *ctl = jstr(json_object_object_get(srv, "owner"), "control");

	return ctl && !strcmp(ctl, "hilink") && web_host(rec, srv, &kind) && kind && !strcmp(kind, "uz");
}

/* r117: info, ussd (code) or reboot through a HiLink web; info, reboot or subnet (code) through a «Mobile.Router» one
 * (2026-10-30); ctx: the caller waits for the answer. false with why */
bool web_act(json_object *rec, json_object *srv, const char *op, const char *code, struct ubus_context *ctx,
	     struct ubus_request_data *req, const char **why)
{
	struct web_job *j;
	char payload[96] = "";

	if (!web_huawei(rec, srv) && !web_mark(rec, srv) && !(web_uz(rec, srv) && (!strcmp(op, "info") || !strcmp(op, "reboot"))) &&
	    !(web_zte(rec, srv) && strcmp(op, "clear-traffic") && strcmp(op, "subnet"))) {
		*why = "only a HiLink, ZTE or Mobile.Router modem's web is asked so";
		return false;
	}
	if (code)
		snprintf(payload, sizeof(payload), "{\"code\":\"%s\"}", code);	/* checked: digits, *, # and + */
	if (!(j = web_start_op(rec, srv, op, -1, *payload ? payload : NULL, why, true)))
		return false;
	if (ctx) {
		j->ctx = ctx;
		j->deferred = true;
		ubus_defer_request(ctx, req, &j->req);
	}
	return true;
}

enum { WEBI_ID, __WEBI_MAX };
const struct blobmsg_policy web_info_policy[__WEBI_MAX] = {
	[WEBI_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
};

/* web_info {id} (read) and web_reboot {id} (write): a HiLink's own web, asked when the page's button is pressed */
int m_web_info(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
	       struct blob_attr *msg)
{
	struct blob_attr *tb[__WEBI_MAX];
	json_object *rec, *srv;
	const char *why = NULL, *id;
	bool ok;

	(void)obj;
	blobmsg_parse(web_info_policy, __WEBI_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[WEBI_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[WEBI_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	if (frozen(id))
		return web_refuse(ctx, req, id, "the modem is frozen by the operator");
	srv = served(rec);
	ok = web_act(rec, srv, !strcmp(method, "web_reboot") ? "reboot" : "info", NULL, ctx, req, &why);
	json_object_put(srv);
	return ok ? UBUS_STATUS_OK : web_refuse(ctx, req, id, why);
}

enum { WEBD_ID, WEBD_OP, WEBD_MODE, WEBD_LTE, __WEBD_MAX };
const struct blobmsg_policy web_do_policy[__WEBD_MAX] = {
	[WEBD_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[WEBD_OP] = { .name = "op", .type = BLOBMSG_TYPE_STRING },
	[WEBD_MODE] = { .name = "mode", .type = BLOBMSG_TYPE_STRING },
	[WEBD_LTE] = { .name = "lte_band", .type = BLOBMSG_TYPE_STRING },
};

/* web_do {id, op: reconnect|clear-traffic|netmode, mode?, lte_band?} (write): a HiLink web's actions of the E3372-325
 * HAR (2026-10-30); netmode: mode 00 auto, 01 2G, 02 3G, 03 4G, lte_band a hex mask (empty: kept) */
int m_web_do(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
	     struct blob_attr *msg)
{
	struct blob_attr *tb[__WEBD_MAX];
	json_object *rec, *srv;
	const char *why = NULL, *id, *op, *mode, *lte;
	char code[48] = "";
	bool ok;

	(void)obj;
	(void)method;
	blobmsg_parse(web_do_policy, __WEBD_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[WEBD_ID] || !tb[WEBD_OP])
		return UBUS_STATUS_INVALID_ARGUMENT;
	op = blobmsg_get_string(tb[WEBD_OP]);
	if (strcmp(op, "reconnect") && strcmp(op, "clear-traffic") && strcmp(op, "netmode"))
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!strcmp(op, "netmode")) {
		mode = tb[WEBD_MODE] ? blobmsg_get_string(tb[WEBD_MODE]) : "";
		lte = tb[WEBD_LTE] ? blobmsg_get_string(tb[WEBD_LTE]) : "";
		if (strlen(mode) != 2 || mode[0] != '0' || !strchr("0123", mode[1]) || strlen(lte) > 16 ||
		    strspn(lte, "0123456789abcdefABCDEF") != strlen(lte))
			return UBUS_STATUS_INVALID_ARGUMENT;
		snprintf(code, sizeof(code), "%s,%s", mode, lte);
	}
	if (!(rec = present_record(tb[WEBD_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	if (frozen(id))
		return web_refuse(ctx, req, id, "the modem is frozen by the operator");
	srv = served(rec);
	if (!web_huawei(rec, srv) && !(web_zte(rec, srv) && strcmp(op, "clear-traffic"))) {
		json_object_put(srv);
		return web_refuse(ctx, req, id, web_zte(rec, srv) ? "a ZTE web keeps its traffic counters" :
								     "only a HiLink or ZTE modem's web is asked so");
	}
	ok = web_act(rec, srv, op, *code ? code : NULL, ctx, req, &why);
	json_object_put(srv);
	if (ok)
		ULOG_INFO("web %s: %s asked through the web by the operator\n", id, op);
	return ok ? UBUS_STATUS_OK : web_refuse(ctx, req, id, why);
}

/* the last reading of this attachment, or NULL; its age in ms */
static json_object *web_cached(json_object *rec, long long *age)
{
	json_object *cached = web_cache ? json_object_object_get(web_cache, jstr(rec, "device_id")) : NULL;

	if (!cached || json_object_get_int64(json_object_object_get(cached, "generation")) !=
		       json_object_get_int64(json_object_object_get(rec, "generation")))
		return NULL;
	*age = now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at"));
	return json_object_object_get(cached, "reply");
}

/*
 * Why a router-mode modem has no mobile data, as its own web said it in a reading under MM_WEB_OFFLINE_MS; NULL when
 * it said nothing of the kind (or is not read). The owner's E3372-325 and MegaFon M150-4 without a SIM (2026-11-04):
 * the stick answers 8.8.8.8 itself in 2-5 ms, the 4 ms rule missed the 5 ms one, the HiLink stayed «main» with the
 * traffic going nowhere. SimStatus 0/255 and ServiceStatus other than 2 are the field-read fields (websig.c).
 */
#define MM_WEB_OFFLINE_MS 180000
const char *web_offline(json_object *rec)
{
	long long age = 0;
	json_object *r = rec && web_cache ? web_cached(rec, &age) : NULL;
	const char *s;

	if (!r || age > MM_WEB_OFFLINE_MS)
		return NULL;
	if ((s = jstr(r, "sim")))
		return !strcmp(s, "absent") ? MM_WEB_OFFLINE_PREFIX "no SIM card" : MM_WEB_OFFLINE_PREFIX "the SIM card is not usable";
	if ((s = jstr(r, "network")) && !strcmp(s, "none"))
		return MM_WEB_OFFLINE_PREFIX "no mobile network";
	return NULL;
}

/* the SIM's home operator code the modem's own web gave (a HiLink's IMSI digits, websig.c), or NULL */
const char *web_sim_plmn(json_object *rec)
{
	long long age = 0;
	json_object *r = rec && web_cache ? web_cached(rec, &age) : NULL;

	return jstr(json_object_object_get(r, "metrics"), "sim_operator_code");
}

int web_metrics(struct ubus_context *ctx, struct ubus_request_data *req, json_object *rec, json_object *srv, long long max_age)
{
	const char *id = jstr(rec, "device_id"), *why = NULL;
	struct web_job *j;
	long long age = 0;
	json_object *r;

	if (!web_cache)
		web_cache = json_object_new_object();
	if ((r = web_cached(rec, &age))) {
		bool refused = !json_object_object_get(r, "metrics");

		/* max_age 0 reads afresh (a password just saved) */
		if (age <= (refused && max_age > 0 && max_age < MM_WEB_REFUSED_KEEP_MS ? MM_WEB_REFUSED_KEEP_MS : max_age)) {
			json_object *o = web_reply(id, r, age);
			int rc = reply_json(ctx, req, NULL, o);

			json_object_put(o);
			return rc;
		}
	}
	if (!(j = web_start(rec, srv, &why))) {
		/* the reader busy with this modem's SMS or an action (the owner's E3372 page, 2026-10-30: «Сеть и сигнал» all
		 * dashes, «Читаем данные модема…» for ever, while its card had the signal): the last reading, however old */
		if (r) {
			json_object *o = web_reply(id, r, age);
			int rc = reply_json(ctx, req, NULL, o);

			json_object_put(o);
			return rc;
		}
		return web_refuse(ctx, req, id, why);
	}
	j->ctx = ctx;
	j->deferred = true;
	ubus_defer_request(ctx, req, &j->req);
	return UBUS_STATUS_OK;
}

/*
 * The background read (the owner, 2026-10-21: «фоновое чтение веба HiLink раз в 5 минут»; 2026-10-22: «сделай чтобы
 * не было пробелов … можно и раз в 30 сек»): a router-mode modem's web read with no page open, so its signal history
 * has no gaps where nobody looked (the page's chart breaks a line only after 90 s without a sample). Once in
 * MM_WEB_BG_MS at most, a reading any page took meanwhile counts; a refused one (a password wanted, the web locked)
 * is not asked again sooner either, and the worker's own login rule (one try in 5 min) still holds.
 */
#define MM_WEB_BG_MS 30000
void web_background(json_object *rec, json_object *srv)
{
	const char *why = NULL;
	long long age = 0;

	if (!web_cache)
		web_cache = json_object_new_object();
	if (web_cached(rec, &age) && age < (uplink_override_ms ? 5 * uplink_override_ms : MM_WEB_BG_MS))
		return;
	web_start(rec, srv, &why);	/* busy or no address yet: the next tick asks again */
}

/* the ids the registry no longer has lose their reading, model, password mark and state file (second audit 2026-10-21:
 * these lived until the engine stopped, one more set per modem identity ever seen) */
void web_retain(json_object *devices)
{
	json_object *maps[3], *gone = json_object_new_array();
	size_t i, k;

	web_models_load();
	maps[0] = web_cache;
	maps[1] = web_models;
	maps[2] = web_pw_seen;

	for (k = 0; k < sizeof(maps) / sizeof(maps[0]); k++) {
		if (!maps[k])
			continue;
		json_object_object_foreach(maps[k], id, v) {
			(void)v;
			if (!json_object_object_get(devices, id))
				json_object_array_add(gone, json_object_new_string(id));
		}
	}
	for (i = 0; i < json_object_array_length(gone); i++) {
		const char *id = json_object_get_string(json_object_array_get_idx(gone, i));
		char state[512];

		for (k = 0; k < sizeof(maps) / sizeof(maps[0]); k++)
			if (maps[k])
				json_object_object_del(maps[k], id);
		web_state_path(id, state, sizeof(state));
		unlink(state);
	}
	if (json_object_array_length(gone) && web_models)
		web_models_save();
	json_object_put(gone);
}

void web_mgr_stop(void)
{
	int i;

	json_object_put(web_auth_said);
	web_auth_said = NULL;
	for (i = 0; i < MM_WEB_JOBS; i++) {
		if (web_jobs[i].active && web_jobs[i].proc.pid > 0)
			kill(-web_jobs[i].proc.pid, SIGKILL);
		free(web_jobs[i].buf);
		web_jobs[i].buf = NULL;
	}
	json_object_put(web_cache);
	web_cache = NULL;
	json_object_put(web_models);
	web_models = NULL;
	web_models_loaded = false;
	json_object_put(web_pw_seen);
	web_pw_seen = NULL;
}
