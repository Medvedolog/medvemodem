/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <libubox/ulog.h>

#include "at.h"
#include "daemon.h"
#include "pdu.h"
#include "sms.h"
#include "sms_at.h"
#include "mmgr.h"

/* ---- SMS and USSD through ModemManager (spec 38) ----
 *
 * A modem ModemManager owns keeps its SMS in ModemManager: incoming messages
 * never reach the AT storages (S1), so they are read, sent and deleted through
 * its Messaging API, and USSD through its 3GPP USSD API — fixed mmcli calls in a
 * worker, the operator's number, text and code passed as arguments, never
 * through a shell (S7, U3). The SMS pass (UCI core.sms_poll, default 60 s, 0
 * off) lists the messages of every ModemManager-owned modem, fetches only those
 * not seen in a final state, journals new ones (`sms-received`, a count: no
 * number or text goes to the journal) and deletes a multipart stuck in
 * `receiving` 300 s after it was first seen (S3: it blocks all later reception;
 * the parts of one message can come minutes apart, so 120 s cut live ones).
 * Modems without ModemManager (AT path, S2/S15) are refused for now.
 */
#define MM_SMS_JOBS 4
#define MM_SMS_STUCK_MS 300000
#define MM_SMS_FRESH_MS 15000
#define MM_SMS_SEND_MS 30000
#define MM_USSD_MS 75000
enum sms_kind { SMS_LIST, SMS_SEND, SMS_DELETE, USSD_START, USSD_RESPOND, USSD_CANCEL, QM_LIST, QM_SEND, QM_DELETE };
static const char *const sms_kind_names[] = { "list", "send", "delete", "ussd-start", "ussd-respond", "ussd-cancel",
					      "qmodem-list", "qmodem-send", "qmodem-delete" };
static struct sms_job {
	bool active, deferred, killed;
	bool quiet;		/* a USSD cancel the engine sent after a failure: the state stays as it ended */
	enum sms_kind kind;
	char device_id[160], index[16];
	int target;		/* delete: the SMS index */
	char at_node[64];	/* the engine's AT port: the fallback when ModemManager fails (user, 2026-10-09) */
	char *number, *text, *value;	/* what to send again over AT */
	long long started;
	struct uloop_process proc;
	struct uloop_fd out;
	struct uloop_timeout kill;
	char *buf;
	size_t len, cap;
	struct ubus_context *ctx;
	struct ubus_request_data req;
} sms_jobs[MM_SMS_JOBS];
static json_object *sms_cache;		/* device_id -> {messages, at, read_ms, error} */
static json_object *sms_rx_seen;	/* "device|index" -> monotonic ms first seen in `receiving` */
static json_object *ussd_states;	/* device_id -> {state, code, reply, error, session, at} */
/*
 * Messages sent over the AT port: AT+CMGS keeps nothing in the modem, so the
 * conversation would lose the operator's side (field 2026-10-13). Kept in RAM
 * only (never on flash; the router's archive keeps received ones only), the last SMS_SENT_MAX per device,
 * served with the read ones under ids from SMS_SENT_ID up, "local": true.
 */
#define SMS_SENT_MAX 50
#define SMS_SENT_ID 100000
static json_object *sms_sent;		/* device_id -> [{id, type, number, text, state, epoch, local}] */
static int sms_sent_next = SMS_SENT_ID;

static void sms_sent_add(const char *id, const char *number, const char *text, bool ok)
{
	json_object *a, *m;

	if (!sms_sent)
		sms_sent = json_object_new_object();
	if (!(a = json_object_object_get(sms_sent, id))) {
		a = json_object_new_array();
		json_object_object_add(sms_sent, id, a);
	}
	m = json_object_new_object();
	json_object_object_add(m, "id", json_object_new_int(sms_sent_next++));
	json_object_object_add(m, "type", json_object_new_string("submit"));
	json_object_object_add(m, "number", json_object_new_string(number ? number : ""));
	json_object_object_add(m, "text", json_object_new_string(text ? text : ""));
	json_object_object_add(m, "state", json_object_new_string(ok ? "sent" : "failed"));
	json_object_object_add(m, "epoch", json_object_new_int64((long long)time(NULL)));
	json_object_object_add(m, "local", json_object_new_boolean(true));
	json_object_array_add(a, m);
	if (json_object_array_length(a) > SMS_SENT_MAX)
		json_object_array_del_idx(a, 0, json_object_array_length(a) - SMS_SENT_MAX);
}

/* the operator deletes a sent one from the list: true when it was one of these */
static bool sms_sent_del(const char *id, int want)
{
	json_object *a = sms_sent ? json_object_object_get(sms_sent, id) : NULL;
	size_t i;

	for (i = 0; a && i < json_object_array_length(a); i++)
		if (json_object_get_int(json_object_object_get(json_object_array_get_idx(a, i), "id")) == want) {
			json_object_array_del_idx(a, i, 1);
			return true;
		}
	return false;
}
static struct uloop_timeout sms_timer;

/* list, then each message not in a final state yet ($3: indices to skip, " 3 7 ") */
static const char sms_list_script[] =
	"m=$1; i=$2; skip=$3\n"
	"l=$(\"$m\" -J -m \"$i\" --messaging-list-sms 2>/dev/null) || exit 3\n"
	"printf '%s' \"$l\" | tr -d '\\n'; printf '\\n'\n"
	"for p in $(printf '%s' \"$l\" | tr ',' '\\n' | sed -n 's#.*\"\\(/org/freedesktop/ModemManager1/SMS/[0-9][0-9]*\\)\".*#\\1#p'); do\n"
	"  case \"$skip\" in *\" ${p##*/} \"*) continue;; esac\n"
	"  \"$m\" -J -s \"$p\" 2>/dev/null | tr -d '\\n'; printf '\\n'\n"
	"done\n"
	"exit 0\n";
/* create, then send; a draft that could not be sent is deleted again */
static const char sms_send_script[] =
	"m=$1; i=$2; a=$3\n"
	"o=$(\"$m\" -m \"$i\" --messaging-create-sms=\"$a\" 2>&1) || { printf '%s\\n' \"$o\"; exit 3; }\n"
	"p=$(printf '%s' \"$o\" | sed -n 's#.*\\(/org/freedesktop/ModemManager1/SMS/[0-9][0-9]*\\).*#\\1#p' | head -n 1)\n"
	"[ -n \"$p\" ] || { printf '%s\\n' \"$o\"; exit 3; }\n"
	"printf 'created %s\\n' \"${p##*/}\"\n"
	"if ! o=$(\"$m\" --timeout=25 -s \"$p\" --send 2>&1); then\n"
	"  printf '%s\\n' \"$o\"; \"$m\" -m \"$i\" --messaging-delete-sms=\"${p##*/}\" >/dev/null 2>&1; exit 4\n"
	"fi\n"
	"echo sent\n";
/* initiate or respond; then the session state, after a marker line */
static const char ussd_script[] =
	"m=$1; i=$2; verb=$3; v=$4\n"
	"if [ \"$verb\" = cancel ]; then \"$m\" -m \"$i\" --3gpp-ussd-cancel 2>&1; exit $?; fi\n"
	/* a session left open (an earlier request that timed out, a menu nobody answered) refuses a new one */
	"if [ \"$verb\" = initiate ] && \"$m\" -J -m \"$i\" --3gpp-ussd-status 2>/dev/null | tr -d ' \\n' |\n"
	"   grep -qE '\"status\":\"(active|user-response)\"'; then\n"
	"  \"$m\" -m \"$i\" --3gpp-ussd-cancel >/dev/null 2>&1; echo '-- stale session cancelled --'\n"
	"fi\n"
	"o=$(\"$m\" --timeout=60 -m \"$i\" --3gpp-ussd-$verb=\"$v\" 2>&1); rc=$?\n"
	"printf '%s\\n' \"$o\"\n"
	"echo '-- ussd status --'\n"
	"\"$m\" -J -m \"$i\" --3gpp-ussd-status 2>/dev/null\n"
	"exit $rc\n";

/* the ModemManager index of a modem ModemManager owns, or NULL with why */
static const char *sms_mm_index(const char *id, char *buf, size_t size, const char **why)
{
	json_object *mm = mm_devices ? json_object_object_get(mm_devices, id) : NULL;
	const char *obj = jstr(mm, "object"), *slash, *b = ledger_backend(id);

	if (frozen(id)) {
		*why = "the modem is frozen by the operator";
		return NULL;
	}
	if (!mm || !obj || (b && (!strcmp(b, "native") || !strcmp(b, "qmodem")))) {
		*why = qmodem_section_of(mm_registry_find(&registry, id)) || (b && !strcmp(b, "qmodem")) ?
		       "QModem owns this modem; its SMS are in QModem" :
		       "SMS and USSD go through ModemManager, which does not hold this modem; the AT path "
		       "for modems without ModemManager is not done yet (spec 38)";
		return NULL;
	}
	slash = strrchr(obj, '/');
	snprintf(buf, size, "%s", slash ? slash + 1 : obj);
	return buf;
}

static struct sms_job *sms_job_of(const char *id, enum sms_kind kind)
{
	int i;

	for (i = 0; i < MM_SMS_JOBS; i++)
		if (sms_jobs[i].active && !strcmp(sms_jobs[i].device_id, id) && sms_jobs[i].kind == kind)
			return &sms_jobs[i];
	return NULL;
}

static void sms_close_out(struct sms_job *j)
{
	if (j->out.fd >= 0) {
		if (j->out.registered)
			uloop_fd_delete(&j->out);
		close(j->out.fd);
		j->out.fd = -1;
	}
}

static void sms_drain(struct sms_job *j)
{
	char chunk[4096];
	ssize_t n;

	while (j->out.fd >= 0 && (n = read(j->out.fd, chunk, sizeof(chunk))) > 0) {
		size_t need = j->len + (size_t)n + 1;

		if (need > 1024 * 1024)
			continue;
		if (need > j->cap) {
			size_t cap = j->cap ? j->cap * 2 : 8192;
			char *nb;

			while (cap < need)
				cap *= 2;
			if (!(nb = realloc(j->buf, cap)))
				continue;
			j->buf = nb;
			j->cap = cap;
		}
		memcpy(j->buf + j->len, chunk, (size_t)n);
		j->len += (size_t)n;
		j->buf[j->len] = '\0';
	}
}

static void sms_readable(struct uloop_fd *u, unsigned int events)
{
	struct sms_job *j = container_of(u, struct sms_job, out);

	(void)events;
	sms_drain(j);
	if (u->eof || u->error)
		sms_close_out(j);
}

static void sms_kill_fired(struct uloop_timeout *t)
{
	struct sms_job *j = container_of(t, struct sms_job, kill);

	if (j->proc.pid > 0) {
		j->killed = true;
		kill(-j->proc.pid, SIGKILL);
	}
}

static void sms_exited(struct uloop_process *p, int ret);

static struct sms_job *sms_spawn(enum sms_kind kind, const char *id, const char *index, const char *a3,
				 const char *a4, int timeout_ms)
{
	struct sms_job *j = NULL;
	int i, fds[2], fd;
	pid_t pid;
	char target[64];

	for (i = 0; i < MM_SMS_JOBS && !j; i++)
		if (!sms_jobs[i].active)
			j = &sms_jobs[i];
	if (!j || pipe2(fds, O_CLOEXEC))
		return NULL;
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return NULL;
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
		switch (kind) {
		case SMS_LIST:
			execl("/bin/sh", "sh", "-c", sms_list_script, "mm-sms", mmcli_path, index, a3 ? a3 : " ", (char *)NULL);
			break;
		case SMS_SEND:
			execl("/bin/sh", "sh", "-c", sms_send_script, "mm-sms", mmcli_path, index, a3, (char *)NULL);
			break;
		case SMS_DELETE:
			snprintf(target, sizeof(target), "--messaging-delete-sms=%s", a3);
			execlp(mmcli_path, mmcli_path, "-m", index, target, (char *)NULL);
			break;
		case QM_LIST:
		case QM_SEND:
		case QM_DELETE: {
			/* r95: QModem's modem_ctrl.sh <method> <section> [argument]; index carries the section */
			char script[300];

			qmodem_ctrl_script(script, sizeof(script));
			execl(script, script, kind == QM_LIST ? "get_sms" : kind == QM_SEND ? "send_raw_pdu" : "delete_sms", index,
			      a3, (char *)NULL);
			break;
		}
		default:
			execl("/bin/sh", "sh", "-c", ussd_script, "mm-ussd", mmcli_path, index,
			      kind == USSD_START ? "initiate" : kind == USSD_RESPOND ? "respond" : "cancel",
			      a3 ? a3 : "", (char *)NULL);
		}
		_exit(127);
	}
	close(fds[1]);
	memset(j, 0, sizeof(*j));
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	j->active = true;
	j->kind = kind;
	snprintf(j->device_id, sizeof(j->device_id), "%s", id);
	snprintf(j->index, sizeof(j->index), "%s", index);
	j->target = (kind == SMS_DELETE || kind == QM_DELETE) && a3 ? atoi(a3) : -1;
	j->started = now_mono_ms();
	j->out.fd = fds[0];
	j->out.cb = sms_readable;
	uloop_fd_add(&j->out, ULOOP_READ);
	j->proc.pid = pid;
	j->proc.cb = sms_exited;
	uloop_process_add(&j->proc);
	j->kill.cb = sms_kill_fired;
	uloop_timeout_set(&j->kill, timeout_ms);
	(void)a4;
	return j;
}

/* indices of cached messages in a final state: not fetched again. mm: ModemManager's index of the modem now */
static void sms_skip_list(const char *id, const char *mm, char *out, size_t size)
{
	json_object *c = json_object_object_get(sms_cache, id), *msgs = json_object_object_get(c, "messages");
	size_t i, len = 0;

	out[0] = '\0';
	len += (size_t)snprintf(out, size, " ");
	if (jstr(c, "path") && strcmp(jstr(c, "path"), "modemmanager"))
		return;		/* the cache holds the AT storage's indices: nothing to skip */
	if (!jstr(c, "mm") || strcmp(jstr(c, "mm"), mm))
		return;		/* ModemManager restarted (another modem index): its SMS indices start over */
	for (i = 0; msgs && i < json_object_array_length(msgs) && len + 12 < size; i++) {
		json_object *m = json_object_array_get_idx(msgs, i);
		const char *st = jstr(m, "state");

		if (st && (!strcmp(st, "received") || !strcmp(st, "sent") || !strcmp(st, "stored")))
			len += (size_t)snprintf(out + len, size - len, "%d ", json_object_get_int(json_object_object_get(m, "id")));
	}
}

/* ---- the router's SMS archive (the owner, 2026-10-24: «избегать переполнения забирая смс в роутер») ----
 *
 * A SIM keeps 10 to 50 messages, and a full one makes the operator stop delivering (the FM350's storage is the SIM
 * only, 10 slots; ModemManager never deletes from it).
 * When the modem's storage is three quarters full, the oldest received messages move to the router: written to
 * flash first (fsync, then rename), deleted from the modem only after that, so a failed write loses nothing. Only a
 * whole received message the engine has known for SMS_ARCH_GRACE_MS (the forwarders and the bot read it at once) moves;
 * a part still waited for, a binary one and an undecoded one stay. The modem is emptied to half its storage; where the
 * storage size is not known (ModemManager) a SIM of 10 is assumed. Kept: the last SMS_ARCH_MAX per modem, in
 * state_dir/sms-archive.json (0600). The owner chose to keep them, so the texts are on flash here and nowhere else;
 * the journal gets counts only. UCI medvemodem.core.sms_archive '0' turns it off (default on).
 * Only the engine's own paths move messages (AT storage, ModemManager); a modem with a web page or QModem keeps its own.
 */
#define SMS_ARCH_ID 1000000000
#define SMS_ARCH_MAX 500
#define SMS_ARCH_GRACE_MS 600000
#define SMS_ARCH_SIM 10
static json_object *sms_arch;		/* device_id -> {"next": n, "messages": [...]} */
static json_object *sms_arch_known;	/* device_id -> {inbox key: monotonic ms first listed} */

static json_object *sms_arch_all(void)
{
	char path[PATH_MAX];

	if (sms_arch)
		return sms_arch;
	/* the RAM copy first: it holds the copies not on flash yet (a service restart without a reboot keeps them) */
	snprintf(path, sizeof(path), "%s/sms-archive.json", run_dir);
	if (access(path, R_OK))
		snprintf(path, sizeof(path), "%s/sms-archive.json", state_dir);
	sms_arch = json_object_from_file(path);
	if (!sms_arch || !json_object_is_type(sms_arch, json_type_object)) {
		json_object_put(sms_arch);
		sms_arch = json_object_new_object();
	}
	return sms_arch;
}

static json_object *sms_arch_of(const char *id)
{
	json_object *d = json_object_object_get(sms_arch_all(), id);

	return d ? json_object_object_get(d, "messages") : NULL;
}

/*
 * The copies of «Keep all SMS in the router» reach the flash in batches (the owner, 2026-11-08: «кумулятивный вариант
 * через /tmp, большие партии лучше для флеш»): each copy goes to run_dir (tmpfs) at once, the flash gets the whole file
 * SMS_ARCH_FLUSH_MS after the first copy not on it, and at the service's stop. Nothing leaves the modem for a copy, so
 * a power cut in between loses no message the modem still has; the next read copies it again. A move (the storage
 * filling) and a delete go to the flash at once, as before.
 */
#ifndef SMS_ARCH_FLUSH_MS
#define SMS_ARCH_FLUSH_MS (10 * 60 * 1000)
#endif
static struct uloop_timeout sms_arch_flush_timer;
static bool sms_arch_dirty;

static bool sms_arch_save(void);

static void sms_arch_ram(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	snprintf(path, sizeof(path), "%s/sms-archive.json", run_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(run_dir, 0755);
	if (json_object_to_file_ext(tmp, sms_arch_all(), JSON_C_TO_STRING_PLAIN) || chmod(tmp, 0600) || rename(tmp, path))
		unlink(tmp);
}

static void sms_arch_flush(struct uloop_timeout *t)
{
	(void)t;
	if (sms_arch_dirty && !sms_arch_save()) {
		ULOG_WARN("sms: the router's SMS archive could not reach the flash (%s); tried again in %d min\n", strerror(errno),
			  SMS_ARCH_FLUSH_MS / 60000);
		uloop_timeout_set(&sms_arch_flush_timer, budget_override_ms ? 2000 : SMS_ARCH_FLUSH_MS);
	}
}

/* on flash before anything leaves the modem: written, synced, renamed */
static bool sms_arch_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];
	const char *text = json_object_to_json_string_ext(sms_arch_all(), JSON_C_TO_STRING_PLAIN);
	size_t len = strlen(text), off = 0;
	int fd;

	snprintf(path, sizeof(path), "%s/sms-archive.json", state_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if ((fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600)) < 0)
		return false;
	while (off < len) {
		ssize_t n = write(fd, text + off, len - off);

		if (n <= 0 && errno != EINTR)
			break;
		off += n > 0 ? (size_t)n : 0;
	}
	if (off < len || fsync(fd)) {
		close(fd);
		unlink(tmp);
		return false;
	}
	close(fd);
	if (rename(tmp, path)) {
		unlink(tmp);
		return false;
	}
	/* the new name itself on flash before the modem's copy goes (audit r101 R09): a power cut right after the rename may
	 * otherwise keep the old directory entry; a directory that cannot be synced is no proof, the copy stays */
	if ((fd = open(state_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC)) < 0)
		return false;
	if (fsync(fd) && errno != EINVAL) {
		close(fd);
		return false;
	}
	close(fd);
	sms_arch_dirty = false;
	uloop_timeout_cancel(&sms_arch_flush_timer);
	sms_arch_ram();
	return true;
}

/* the modem's messages, then the router's: what the operator reads and what counts as seen */
static json_object *sms_with_archive(const char *id, json_object *msgs)
{
	json_object *all = json_object_new_array(), *a = sms_arch_of(id);
	size_t i;

	for (i = 0; msgs && i < json_object_array_length(msgs); i++)
		json_object_array_add(all, json_object_get(json_object_array_get_idx(msgs, i)));
	for (i = 0; a && i < json_object_array_length(a); i++) {
		json_object *o = json_object_array_get_idx(a, i);
		const char *src = jstr(o, "src"), *colon = src ? strchr(src, ':') : NULL;
		char k1[24], k2[24];
		size_t x;
		bool listed = false;

		/* moved, its delete not done yet: the modem's copy stands for it; a copy kept of every message («Keep all SMS
		 * in the router») stands hidden while the modem lists the same message, at whatever index a restart gave it */
		bool copy = json_object_get_boolean(json_object_object_get(o, "copy"));

		mm_sms_key(o, k1, sizeof(k1));
		for (x = 0; (colon || copy) && msgs && x < json_object_array_length(msgs) && !listed; x++) {
			json_object *m = json_object_array_get_idx(msgs, x);

			mm_sms_key(m, k2, sizeof(k2));
			listed = !strcmp(k1, k2) && (copy || json_object_get_int(json_object_object_get(m, "id")) == atoi(colon + 1));
		}
		if (!listed)
			json_object_array_add(all, json_object_get(o));
	}
	return all;
}

/* the operator deletes a message kept in the router */
static bool sms_arch_del(const char *id, int want)
{
	json_object *a = sms_arch_of(id);
	size_t i;

	for (i = 0; a && i < json_object_array_length(a); i++)
		if (json_object_get_int(json_object_object_get(json_object_array_get_idx(a, i), "id")) == want) {
			json_object_array_del_idx(a, i, 1);
			return sms_arch_save();
		}
	return false;
}

/* the operator deletes a message from the modem: its copy kept in the router goes too, or it would come back */
static void sms_arch_forget(const char *id, int want)
{
	json_object *c = sms_cache ? json_object_object_get(sms_cache, id) : NULL, *msgs = json_object_object_get(c, "messages");
	json_object *a = sms_arch_of(id), *m = NULL;
	char k1[24], k2[24];
	size_t i;
	bool gone = false;

	for (i = 0; msgs && i < json_object_array_length(msgs) && !m; i++)
		if (json_object_get_int(json_object_object_get(json_object_array_get_idx(msgs, i), "id")) == want)
			m = json_object_array_get_idx(msgs, i);
	if (!a || !m)
		return;
	mm_sms_key(m, k1, sizeof(k1));
	for (i = json_object_array_length(a); i-- > 0;) {
		json_object *o = json_object_array_get_idx(a, i);

		mm_sms_key(o, k2, sizeof(k2));
		if (json_object_get_boolean(json_object_object_get(o, "copy")) && !strcmp(k1, k2)) {
			json_object_array_del_idx(a, i, 1);
			gone = true;
		}
	}
	if (gone && !sms_arch_save())
		ULOG_WARN("sms %s: the router's SMS archive could not be written (%s)\n", id, strerror(errno));
}

/* a message kept in the router deleted: off the archive, nothing in the modem to delete (every path) */
static int sms_arch_delete_reply(struct ubus_context *ctx, struct ubus_request_data *req, const char *id, int want)
{
	json_object *o2 = json_object_new_object();
	int rc2;

	json_object_object_add(o2, "device_id", json_object_new_string(id));
	json_object_object_add(o2, "ok", json_object_new_boolean(sms_arch_del(id, want)));
	rc2 = reply_json(ctx, req, NULL, o2);
	json_object_put(o2);
	return rc2;
}

static long long sms_arch_when(json_object *m);

/*
 * «Хранить все SMS в роутере» (the owner, 2026-11-08: some modems lose their messages at a restart): every whole received
 * message is copied to the router's archive as soon as a read lists it, from any path (AT, ModemManager, a modem's web,
 * QModem); nothing is deleted from the modem. The copy stays hidden while the modem lists the same message and shows
 * once the modem has lost it; its key is the modem copy's, so it is no news and is not forwarded again. UCI
 * medvemodem.core.sms_keep_all '1' (default off): the texts are then on flash, the owner's choice (CLAUDE.md, the
 * router's SMS archive).
 */
static void sms_arch_copy(const char *id, json_object *msgs, const char *path)
{
	json_object *dev, *a, *have;
	int copied = 0;
	size_t i;

	if (!msgs || strcmp(core_opt("sms_keep_all", "0"), "1"))
		return;
	if (!(dev = json_object_object_get(sms_arch_all(), id))) {
		dev = json_object_new_object();
		json_object_object_add(dev, "next", json_object_new_int(SMS_ARCH_ID));
		json_object_object_add(dev, "messages", json_object_new_array());
		json_object_object_add(sms_arch_all(), id, dev);
	}
	a = json_object_object_get(dev, "messages");
	have = json_object_new_object();
	for (i = 0; i < json_object_array_length(a); i++) {
		char key[24];

		mm_sms_key(json_object_array_get_idx(a, i), key, sizeof(key));
		json_object_object_add(have, key, NULL);
	}
	for (i = 0; i < json_object_array_length(msgs); i++) {
		json_object *m = json_object_array_get_idx(msgs, i), *k;
		const char *st = jstr(m, "state"), *ty = jstr(m, "type");
		char key[24], src[48];

		if (!st || strcmp(st, "received") || (ty && strcmp(ty, "deliver")) || json_object_object_get(m, "have") ||
		    json_object_get_boolean(json_object_object_get(m, "binary")) || !jstr(m, "text") || jstr(m, "error"))
			continue;
		mm_sms_key(m, key, sizeof(key));
		if (json_object_object_get_ex(have, key, NULL))
			continue;
		json_object_object_add(have, key, NULL);
		snprintf(src, sizeof(src), "%s:%d", path && *path ? path : "mm", json_object_get_int(json_object_object_get(m, "id")));
		k = json_object_new_object();
		json_object_object_add(k, "id", json_object_new_int(json_object_get_int(json_object_object_get(dev, "next"))));
		json_object_object_add(dev, "next", json_object_new_int(json_object_get_int(json_object_object_get(dev, "next")) + 1));
		json_object_object_add(k, "type", json_object_new_string("deliver"));
		json_object_object_add(k, "state", json_object_new_string("received"));
		json_object_object_add(k, "number", json_object_new_string(jstr(m, "number") ? jstr(m, "number") : ""));
		json_object_object_add(k, "text", json_object_new_string(jstr(m, "text")));
		if (jstr(m, "timestamp"))
			json_object_object_add(k, "timestamp", json_object_new_string(jstr(m, "timestamp")));
		if (json_object_object_get(m, "epoch"))
			json_object_object_add(k, "epoch", json_object_new_int64(sms_arch_when(m)));
		json_object_object_add(k, "storage", json_object_new_string("router"));
		json_object_object_add(k, "src", json_object_new_string(src));
		json_object_object_add(k, "copy", json_object_new_boolean(1));
		json_object_object_add(k, "archived", json_object_new_int64((long long)time(NULL)));
		json_object_array_add(a, k);
		copied++;
	}
	json_object_put(have);
	if (!copied)
		return;
	if (json_object_array_length(a) > SMS_ARCH_MAX)
		json_object_array_del_idx(a, 0, json_object_array_length(a) - SMS_ARCH_MAX);
	/* in RAM now, on flash with the batch */
	sms_arch_ram();
	sms_arch_dirty = true;
	sms_arch_flush_timer.cb = sms_arch_flush;
	if (!sms_arch_flush_timer.pending)	/* the batch's clock starts with its first copy */
		uloop_timeout_set(&sms_arch_flush_timer, budget_override_ms ? 2000 : SMS_ARCH_FLUSH_MS);
	ULOG_INFO("sms %s: %d message%s copied to the router\n", id, copied, copied == 1 ? "" : "s");
}

static long long sms_arch_when(json_object *m)
{
	return json_object_object_get(m, "epoch") ? json_object_get_int64(json_object_object_get(m, "epoch")) : 0;
}

static int sms_arch_older(const void *a, const void *b)
{
	json_object *x = *(json_object *const *)a, *y = *(json_object *const *)b;
	long long tx = sms_arch_when(x), ty = sms_arch_when(y);

	if (tx != ty)
		return tx < ty ? -1 : 1;
	return json_object_get_int(json_object_object_get(x, "id")) - json_object_get_int(json_object_object_get(y, "id"));
}

static int sms_arch_slots(json_object *m)
{
	json_object *parts = json_object_object_get(m, "parts");

	return parts && json_object_array_length(parts) > 1 ? (int)json_object_array_length(parts) : 1;
}

/*
 * After a read of the engine's own path: move the oldest received messages to the router when the storage is three
 * quarters full. at_node: the AT port for an AT storage; mm: ModemManager's index of the modem for its storage.
 */
struct sms_reply_to;
static bool sms_at_start(const char *id, const char *node, json_object *job, int budget_ms, const struct sms_reply_to *to,
			 bool queue, const char **why);

static void sms_arch_step(const char *id, const char *at_node, const char *mm)
{
	json_object *c = sms_cache ? json_object_object_get(sms_cache, id) : NULL, *msgs = json_object_object_get(c, "messages");
	json_object *store = json_object_object_get(c, "store"), *known, *now_known, *pick, *dev, *a;
	long long now = now_mono_ms(), grace = budget_override_ms ? 1000 : SMS_ARCH_GRACE_MS;
	int used = 0, total = 0, freed = 0, moved = 0;
	bool draining;
	size_t i, n;
	json_object **cand;

	if (!c || jstr(c, "error") || !msgs || !strcmp(core_opt("sms_archive", "1"), "0"))
		return;
	if (!sms_arch_known)
		sms_arch_known = json_object_new_object();
	/* how long each received message has been known: only those still listed are remembered */
	known = json_object_object_get(sms_arch_known, id);
	now_known = json_object_new_object();
	for (i = 0; i < json_object_array_length(msgs); i++) {
		json_object *m = json_object_array_get_idx(msgs, i), *t;
		char key[24];

		mm_sms_key(m, key, sizeof(key));
		t = json_object_object_get(known, key);
		json_object_object_add(now_known, key, json_object_new_int64(t ? json_object_get_int64(t) : now));
		used += sms_arch_slots(m);
	}
	draining = json_object_get_boolean(json_object_object_get(known, "@drain"));
	json_object_object_add(sms_arch_known, id, now_known);
	if (store && json_object_object_get(store, "total") && json_object_get_int(json_object_object_get(store, "total")) > 0) {
		total = json_object_get_int(json_object_object_get(store, "total"));
		used = json_object_get_int(json_object_object_get(store, "used"));
	} else {
		total = SMS_ARCH_SIM;
	}
	/* from three quarters full down to half: the drain goes on over the reads its deletes bring */
	if (used * 4 < total * 3 && !(draining && used > total / 2))
		return;
	json_object_object_add(now_known, "@drain", json_object_new_boolean(true));
	if (at_node ? !at_node[0] : (!mm || !mm[0] || sms_job_of(id, SMS_DELETE)))
		return;
	n = json_object_array_length(msgs);
	if (!(cand = calloc(n ? n : 1, sizeof(*cand))))
		return;
	for (i = 0, n = 0; i < json_object_array_length(msgs); i++) {
		json_object *m = json_object_array_get_idx(msgs, i);
		const char *st = jstr(m, "state"), *ty = jstr(m, "type");
		char key[24];

		mm_sms_key(m, key, sizeof(key));
		if (!st || strcmp(st, "received") || (ty && strcmp(ty, "deliver")) || json_object_object_get(m, "have") ||
		    json_object_get_boolean(json_object_object_get(m, "binary")) || !jstr(m, "text") || jstr(m, "error") ||
		    now - json_object_get_int64(json_object_object_get(now_known, key)) < grace)
			continue;
		cand[n++] = m;
	}
	qsort(cand, n, sizeof(*cand), sms_arch_older);
	pick = json_object_new_array();
	/* ModemManager deletes one message per job: one now, the next after the read the delete brings */
	for (i = 0; i < n && used - freed > total / 2 && (at_node || !json_object_array_length(pick)); i++) {
		json_object_array_add(pick, json_object_get(cand[i]));
		freed += sms_arch_slots(cand[i]);
	}
	free(cand);
	if (!json_object_array_length(pick)) {
		json_object_put(pick);
		return;
	}
	if (!(dev = json_object_object_get(sms_arch_all(), id))) {
		dev = json_object_new_object();
		json_object_object_add(dev, "next", json_object_new_int(SMS_ARCH_ID));
		json_object_object_add(dev, "messages", json_object_new_array());
		json_object_object_add(sms_arch_all(), id, dev);
	}
	a = json_object_object_get(dev, "messages");
	for (i = 0; i < json_object_array_length(pick); i++) {
		json_object *m = json_object_array_get_idx(pick, i), *k;
		char src[48];
		size_t x;
		bool have = false;

		/* the same message again (its delete failed before): kept once */
		snprintf(src, sizeof(src), "%s:%d", at_node ? "at" : "mm", json_object_get_int(json_object_object_get(m, "id")));
		for (x = 0; x < json_object_array_length(a) && !have; x++) {
			json_object *o = json_object_array_get_idx(a, x);
			char k1[24], k2[24];

			mm_sms_key(o, k1, sizeof(k1));
			mm_sms_key(m, k2, sizeof(k2));
			have = !strcmp(k1, k2) && jstr(o, "src") && !strcmp(jstr(o, "src"), src);
		}
		if (have)
			continue;
		k = json_object_new_object();
		json_object_object_add(k, "id", json_object_new_int(json_object_get_int(json_object_object_get(dev, "next"))));
		json_object_object_add(dev, "next", json_object_new_int(json_object_get_int(json_object_object_get(dev, "next")) + 1));
		json_object_object_add(k, "type", json_object_new_string("deliver"));
		json_object_object_add(k, "state", json_object_new_string("received"));
		json_object_object_add(k, "number", json_object_new_string(jstr(m, "number") ? jstr(m, "number") : ""));
		json_object_object_add(k, "text", json_object_new_string(jstr(m, "text")));
		if (jstr(m, "timestamp"))
			json_object_object_add(k, "timestamp", json_object_new_string(jstr(m, "timestamp")));
		if (json_object_object_get(m, "epoch"))
			json_object_object_add(k, "epoch", json_object_new_int64(sms_arch_when(m)));
		json_object_object_add(k, "storage", json_object_new_string("router"));
		json_object_object_add(k, "src", json_object_new_string(src));
		json_object_object_add(k, "archived", json_object_new_int64((long long)time(NULL)));
		json_object_array_add(a, k);
		moved++;
	}
	if (json_object_array_length(a) > SMS_ARCH_MAX)
		json_object_array_del_idx(a, 0, json_object_array_length(a) - SMS_ARCH_MAX);
	if (moved && !sms_arch_save()) {
		/* nothing leaves the modem that is not on flash */
		ULOG_ERR("sms %s: the router's SMS archive could not be written (%s); the messages stay in the modem\n", id,
			 strerror(errno));
		json_object_put(sms_arch);
		sms_arch = NULL;
		json_object_put(pick);
		return;
	}
	{
		char detail[48];
		const char *why = NULL;

		snprintf(detail, sizeof(detail), "%d moved", (int)json_object_array_length(pick));
		if (at_node) {
			json_object *job = json_object_new_object(), *ind = json_object_new_array();

			for (i = 0; i < json_object_array_length(pick); i++) {
				json_object *m = json_object_array_get_idx(pick, i), *parts = json_object_object_get(m, "parts");
				size_t x;

				for (x = 0; parts && x < json_object_array_length(parts); x++)
					json_object_array_add(ind, json_object_get(json_object_array_get_idx(parts, x)));
				if (!parts || !json_object_array_length(parts))
					json_object_array_add(ind, json_object_get(json_object_object_get(m, "id")));
			}
			json_object_object_add(job, "op", json_object_new_string("delete"));
			json_object_object_add(job, "indices", ind);
			if (!sms_at_start(id, at_node, job, 15000 + 10000 * (int)json_object_array_length(ind), NULL, true, &why))
				ULOG_WARN("sms %s: archived messages not deleted from the modem now (%s); next read\n", id, why);
		} else {
			char idx[16];

			snprintf(idx, sizeof(idx), "%d", json_object_get_int(json_object_object_get(json_object_array_get_idx(pick, 0), "id")));
			if (!sms_spawn(SMS_DELETE, id, mm, idx, NULL, 15000))
				ULOG_WARN("sms %s: an archived message not deleted from the modem now; next read\n", id);
		}
		ULOG_INFO("sms %s: %d message%s moved to the router (storage %d of %d)\n", id, (int)json_object_array_length(pick),
			  json_object_array_length(pick) == 1 ? "" : "s", used, total);
		mm_history_add(&history, id, (long long)time(NULL), now, "sms-archived", detail);
		history_dirty = true;
	}
	json_object_put(pick);
}

/*
 * What may stop a modem receiving (the owner, 2026-10-16: «мониторинг заполненности и подвисших»):
 * an outgoing message left unsent in the modem's storage (bench: the T77's draft at index 0 made
 * ModemManager drop every new message stored at the same index, «A part with index 0 was already
 * taken»), a multipart still `receiving`, a storage full or nearly full (known over AT only).
 * NULL when nothing is wrong.
 */
static json_object *sms_health(json_object *c)
{
	json_object *msgs = json_object_object_get(c, "messages"), *store = json_object_object_get(c, "store");
	json_object *o = json_object_new_object(), *stuck = json_object_new_array();
	int receiving = 0, used = -1, total = -1;
	size_t i;

	for (i = 0; msgs && i < json_object_array_length(msgs); i++) {
		json_object *m = json_object_array_get_idx(msgs, i);
		const char *st = jstr(m, "state"), *ty = jstr(m, "type");

		if (st && ty && !strcmp(ty, "submit") && !strcmp(st, "stored"))
			json_object_array_add(stuck, json_object_get(json_object_object_get(m, "id")));
		if (st && !strcmp(st, "receiving"))
			receiving++;
	}
	if (store && json_object_object_get(store, "used") && json_object_object_get(store, "total")) {
		used = json_object_get_int(json_object_object_get(store, "used"));
		total = json_object_get_int(json_object_object_get(store, "total"));
	}
	if (json_object_array_length(stuck))
		json_object_object_add(o, "stuck", stuck);
	else
		json_object_put(stuck);
	if (receiving)
		json_object_object_add(o, "receiving", json_object_new_int(receiving));
	if (total > 0 && used >= total - 2)
		json_object_object_add(o, "full", json_object_new_boolean(used >= total));
	if (!json_object_object_length(o)) {
		json_object_put(o);
		return NULL;
	}
	return o;
}

/* the dashboard's envelope: {"unread", "inbox", "health"?} once the messages of the modem were read, else NULL */
json_object *sms_badge(const char *id)
{
	json_object *c = sms_cache ? json_object_object_get(sms_cache, id) : NULL, *b, *h, *all;

	if (!c || !json_object_object_get(c, "messages"))
		return NULL;
	all = sms_with_archive(id, json_object_object_get(c, "messages"));
	b = sms_seen_badge(id, all);
	json_object_put(all);
	if (!b)
		return NULL;
	if ((h = sms_health(c)))
		json_object_object_add(b, "health", h);
	return b;
}

static json_object *sms_view_mark(const char *id, bool mark)
{
	json_object *c = sms_cache ? json_object_object_get(sms_cache, id) : NULL, *o = json_object_new_object();

	json_object *mine = sms_with_archive(id, c ? json_object_object_get(c, "messages") : NULL);

	/* which of them the operator has not seen, said before this reply marks them (the page keeps them marked until
	 * their conversation is opened) */
	{
		json_object *un = sms_seen_unread_ids(id, mine);

		if (un)
			json_object_object_add(o, "unread_ids", un);
	}
	/* a reply to a client is the operator looking at the list; a peek (the bot) is not */
	if (mark && c && json_object_object_get(c, "messages"))
		sms_seen_mark(id, mine);

	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "path", json_object_new_string(c && jstr(c, "path") ? jstr(c, "path") : "modemmanager"));
	if (c && jstr(c, "fallback"))
		json_object_object_add(o, "fallback", json_object_new_string(jstr(c, "fallback")));
	if (c && json_object_object_get(c, "store"))
		json_object_object_add(o, "store", json_object_get(json_object_object_get(c, "store")));
	{
		json_object *all = json_object_get(mine), *sent = sms_sent ? json_object_object_get(sms_sent, id) : NULL;
		size_t i;

		for (i = 0; sent && i < json_object_array_length(sent); i++)
			json_object_array_add(all, json_object_get(json_object_array_get_idx(sent, i)));
		json_object_object_add(o, "messages", all);
	}
	if (c && json_object_object_get(c, "at"))
		json_object_object_add(o, "read_at", json_object_get(json_object_object_get(c, "at")));
	if (c && jstr(c, "error"))
		json_object_object_add(o, "error", json_object_new_string(jstr(c, "error")));
	if (c) {
		json_object *h = sms_health(c);

		if (h)
			json_object_object_add(o, "health", h);
	}
	json_object_put(mine);
	return o;
}

static json_object *sms_view(const char *id)
{
	return sms_view_mark(id, true);
}

/*
 * SMS forwarding (the owner, 2026-10-19): the new received messages of a read go, one JSON line
 * each, to sms-forward's stdin; it sends them to ntfy or a webhook when the operator turned it on, else exits. The
 * socket is not waited on: what does not fit it at once is left out, the engine never blocks on a slow server.
 */
static void sms_forward_exited(struct uloop_process *p, int ret)
{
	(void)ret;
	free(p);
}

static void sms_forward(const char *id, json_object *fwd)
{
	const char *helper = getenv("MEDVEMODEM_SMS_FORWARD");
	struct uloop_process *proc;
	int tfd, nul;
	size_t i;
	pid_t pid;

	json_object *sf = json_object_object_get(medvemodem_uci(), "sms_forward"), *mods = json_object_object_get(sf, "modem");
	bool picked = !jstr(sf, "modems_all") || strcmp(jstr(sf, "modems_all"), "0");

	helper = helper && *helper ? helper : "/usr/libexec/medvemodem/sms-forward";
	/* forwarding off: no helper started at all (audit 2026-10-20: it was forked for every batch, to exit at once) */
	if (!json_object_array_length(fwd) || strcmp(jstr(sf, "enabled") ? jstr(sf, "enabled") : "0", "1"))
		return;
	/* spec §43: all modems, or the ticked ones only */
	for (i = 0; !picked && mods && i < (json_object_is_type(mods, json_type_array) ? json_object_array_length(mods) : 1); i++)
		picked = !strcmp(json_object_get_string(json_object_is_type(mods, json_type_array) ? json_object_array_get_idx(mods, i) : mods), id);
	if (!picked)
		return;
	/* our own forwards are no news: never forwarded again (a loop between two modems otherwise) */
	for (i = json_object_array_length(fwd); i > 0; i--) {
		const char *t = jstr(json_object_array_get_idx(fwd, i - 1), "text");

		if (t && !strncmp(t, MM_FORWARD_MARK, strlen(MM_FORWARD_MARK)))
			json_object_array_del_idx(fwd, i - 1, 1);
	}
	/* spec §43: by SMS too, through another modem */
	if (jstr(sf, "sms") && !strcmp(jstr(sf, "sms"), "1"))
		for (i = 0; i < json_object_array_length(fwd); i++)
			alert_forward(id, jstr(json_object_array_get_idx(fwd, i), "number"), jstr(json_object_array_get_idx(fwd, i), "text"));
	if (!json_object_array_length(fwd) || (jstr(sf, "web") && !strcmp(jstr(sf, "web"), "0")))
		return;
	/* the process record first: a child uloop never learned of would stay a zombie */
	if (access(helper, X_OK) || !(proc = calloc(1, sizeof(*proc))))
		return;
	/* the messages through a file, not a socket: a non-blocking send into a helper that has not started reading yet
	 * lost the rest of a batch on EAGAIN, and a short write cut a line (audit STAB-02, 2026-11-19); the file is unlinked
	 * at once, so the text never stays in /tmp */
	{
		char tmpl[] = "/tmp/medvemodem-smsfwd.XXXXXX";
		FILE *f;

		if ((tfd = mkostemp(tmpl, O_CLOEXEC)) < 0) {
			free(proc);
			return;
		}
		unlink(tmpl);
		if (!(f = fdopen(dup(tfd), "w"))) {
			close(tfd);
			free(proc);
			return;
		}
		for (i = 0; i < json_object_array_length(fwd); i++) {
			json_object *m = json_object_array_get_idx(fwd, i), *o = json_object_new_object();
			char lb[64];
			const char *name = daemon_modem_label(id, lb, sizeof(lb));

			json_object_object_add(o, "modem", json_object_new_string(name ? name : id));
			json_object_object_add(o, "number", json_object_new_string(jstr(m, "number") ? jstr(m, "number") : ""));
			json_object_object_add(o, "text", json_object_new_string(jstr(m, "text") ? jstr(m, "text") : ""));
			json_object_object_add(o, "time", json_object_new_string(jstr(m, "timestamp") ? jstr(m, "timestamp") : ""));
			fprintf(f, "%s\n", json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN));
			json_object_put(o);
		}
		if (fclose(f) || lseek(tfd, 0, SEEK_SET) < 0) {
			close(tfd);
			free(proc);
			return;
		}
	}
	if ((pid = fork()) < 0) {
		close(tfd);
		free(proc);
		return;
	}
	if (pid == 0) {
		setpgid(0, 0);
		dup2(tfd, 0);
		if ((nul = open("/dev/null", O_RDWR)) >= 0) {
			dup2(nul, 1);
			dup2(nul, 2);
		}
		for (nul = 3; nul < 1024; nul++)
			close(nul);
		execl(helper, helper, (char *)NULL);
		_exit(127);
	}
	close(tfd);
	proc->pid = pid;
	proc->cb = sms_forward_exited;
	uloop_process_add(proc);
}

/* the messages of a read (owned), with what they replace: new received ones are journalled as a count */
static void sms_cache_put(const char *id, json_object *msgs, const char *path, const char *fallback, json_object *store)
{
	json_object *c, *old;
	long long now = now_mono_ms();
	int added = 0;
	size_t i, k;
	json_object *fwd = json_object_new_array();

	if (!sms_cache)
		sms_cache = json_object_new_object();
	if (!(c = json_object_object_get(sms_cache, id))) {
		c = json_object_new_object();
		json_object_object_add(sms_cache, id, c);
	}
	old = json_object_object_get(c, "messages");
	/* the first read of a modem is no news; a path change neither (other ids) */
	for (i = 0; json_object_object_get(c, "at") && jstr(c, "path") && !strcmp(jstr(c, "path"), path) &&
		    i < json_object_array_length(msgs); i++) {
		json_object *m = json_object_array_get_idx(msgs, i);
		int idx = json_object_get_int(json_object_object_get(m, "id"));
		bool known = false;

		for (k = 0; old && k < json_object_array_length(old) && !known; k++)
			known = json_object_get_int(json_object_object_get(json_object_array_get_idx(old, k), "id")) == idx &&
				jstr(json_object_array_get_idx(old, k), "state") &&
				!strcmp(jstr(json_object_array_get_idx(old, k), "state"), "received");
		/* a web modem's message days old found now (a box read for the first time: a HiLink's SIM inbox) is no news: listed, not
		 * forwarded again (the owner, 2026-11-06: «подтягивать снова, но без задваивания») */
		if (!strcmp(path, "web") && json_object_object_get(m, "epoch") &&
		    (long long)time(NULL) - json_object_get_int64(json_object_object_get(m, "epoch")) > 86400)
			continue;
		if (!known && jstr(m, "state") && !strcmp(jstr(m, "state"), "received") &&
		    jstr(m, "type") && !strcmp(jstr(m, "type"), "deliver")) {
			added++;
			json_object_array_add(fwd, json_object_get(m));
		}
	}
	json_object_object_add(c, "messages", msgs);
	json_object_object_add(c, "path", json_object_new_string(path));
	json_object_object_add(c, "fallback", fallback ? json_object_new_string(fallback) : NULL);
	json_object_object_add(c, "store", store ? json_object_get(store) : NULL);
	json_object_object_add(c, "at", json_object_new_int64((long long)time(NULL)));
	json_object_object_add(c, "read_ms", json_object_new_int64(now));
	json_object_object_del(c, "error");
	sms_arch_copy(id, msgs, path);
	/* the first read of a modem ever: what is in its storage is no news (the badge counts what comes after) */
	if (!sms_seen_known(id)) {
		json_object *all = sms_with_archive(id, msgs);

		sms_seen_mark(id, all);
		json_object_put(all);
	}
	if (added) {
		char detail[48];

		snprintf(detail, sizeof(detail), "%d new", added);
		mm_history_add(&history, id, (long long)time(NULL), now, "sms-received", detail);
		history_dirty = true;
		ULOG_INFO("sms %s: %d new message%s (%s)\n", id, added, added == 1 ? "" : "s", path);
		sms_forward(id, fwd);
		/* the Telegram bot forwards them when its owner allowed it (r85): the event names the modem and the new
		 * messages' storage indexes only, never a number or a text; the bot reads them itself, unmarked (peek) */
		{
			json_object *e = json_object_new_object(), *ids = json_object_new_array();

			for (i = 0; i < json_object_array_length(fwd); i++)
				json_object_array_add(ids, json_object_get(json_object_object_get(json_object_array_get_idx(fwd, i), "id")));
			json_object_object_add(e, "device_id", json_object_new_string(id));
			json_object_object_add(e, "ids", ids);
			broadcast("medvemodem.sms", e);
			json_object_put(e);
		}
	}
	json_object_put(fwd);
}

static void sms_list_done(struct sms_job *j, int status)
{
	json_object *parsed = status == 0 && !j->killed ? mm_sms_parse(j->buf) : NULL;
	json_object *c, *old = NULL, *merged, *fresh;
	long long now = now_mono_ms();
	size_t i, k;

	if (!sms_cache)
		sms_cache = json_object_new_object();
	if (!sms_rx_seen)
		sms_rx_seen = json_object_new_object();
	c = json_object_object_get(sms_cache, j->device_id);
	if (!parsed || jstr(parsed, "error")) {
		if (!c) {
			c = json_object_new_object();
			json_object_object_add(sms_cache, j->device_id, c);
		}
		json_object_object_add(c, "error", json_object_new_string(j->killed ? "ModemManager did not answer in time" :
					parsed && jstr(parsed, "error") ? jstr(parsed, "error") : "ModemManager did not list the messages"));
		json_object_put(parsed);
		return;
	}
	/*
	 * ModemManager restarted: the modem has another index and the SMS indices
	 * start over, so a cached message would stand in for another one under its
	 * old number (HANDOFF 138). The cache of the old run is no base: nothing
	 * kept, nothing counted as news, no `receiving` timer carried over.
	 */
	if (c && jstr(c, "path") && !strcmp(jstr(c, "path"), "modemmanager") && jstr(c, "mm") && strcmp(jstr(c, "mm"), j->index)) {
		char prefix[176];
		size_t plen = (size_t)snprintf(prefix, sizeof(prefix), "%s|", j->device_id);
		struct json_object_iterator it, end;

		ULOG_INFO("sms %s: ModemManager index %s -> %s, cached messages dropped\n", j->device_id, jstr(c, "mm"), j->index);
		json_object_object_del(c, "messages");
		json_object_object_del(c, "at");
	again:
		it = json_object_iter_begin(sms_rx_seen);
		end = json_object_iter_end(sms_rx_seen);
		for (; !json_object_iter_equal(&it, &end); json_object_iter_next(&it))
			if (!strncmp(json_object_iter_peek_name(&it), prefix, plen)) {
				json_object_object_del(sms_rx_seen, json_object_iter_peek_name(&it));
				goto again;
			}
	}
	/* listed ones: fetched now, or kept from the cache (final state, not fetched again) */
	old = c ? json_object_object_get(c, "messages") : NULL;
	fresh = json_object_object_get(parsed, "messages");
	merged = json_object_new_array();
	{
		/* the listing's indices, in order: what the worker printed first */
		const char *p = j->buf;
		json_object *listing = NULL, *paths;

		while (p && *p && !listing) {
			size_t n = strcspn(p, "\n");
			char *line = strndup(p, n);
			json_object *d = line ? json_tokener_parse(line) : NULL;

			free(line);
			p += n + (p[n] == '\n');
			if (d && json_object_object_get(d, "modem.messaging.sms"))
				listing = d;
			else
				json_object_put(d);
		}
		paths = json_object_object_get(listing, "modem.messaging.sms");
		for (i = 0; paths && i < json_object_array_length(paths); i++) {
			const char *path = json_object_get_string(json_object_array_get_idx(paths, i)), *slash = strrchr(path, '/');
			int idx = slash ? atoi(slash + 1) : -1;
			json_object *m = NULL;

			for (k = 0; fresh && k < json_object_array_length(fresh) && !m; k++)
				if (json_object_get_int(json_object_object_get(json_object_array_get_idx(fresh, k), "id")) == idx)
					m = json_object_array_get_idx(fresh, k);
			if (!m && (!c || !jstr(c, "path") || !strcmp(jstr(c, "path"), "modemmanager")))
				for (k = 0; old && k < json_object_array_length(old) && !m; k++)
					if (json_object_get_int(json_object_object_get(json_object_array_get_idx(old, k), "id")) == idx)
						m = json_object_array_get_idx(old, k);
			if (m)
				json_object_array_add(merged, json_object_get(m));
		}
		json_object_put(listing);
	}
	sms_cache_put(j->device_id, merged, "modemmanager", NULL, NULL);
	json_object_object_add(json_object_object_get(sms_cache, j->device_id), "mm", json_object_new_string(j->index));
	/*
	 * An outgoing message left unsent in the modem's storage (state stored) takes a storage slot
	 * ModemManager then refuses to give a new incoming message (bench 2026-10-16, the T77: «A part
	 * with index 0 was already taken», the SMS lost). The owner: self-repair first. Deleted 10 min
	 * after it was first seen (a send in progress is never `stored`), journalled.
	 */
	for (i = 0; i < json_object_array_length(merged); i++) {
		json_object *m = json_object_array_get_idx(merged, i), *seen;
		char key[220], idx[16];

		snprintf(key, sizeof(key), "%s|%d|draft", j->device_id, json_object_get_int(json_object_object_get(m, "id")));
		if (!jstr(m, "state") || strcmp(jstr(m, "state"), "stored") || !jstr(m, "type") || strcmp(jstr(m, "type"), "submit")) {
			json_object_object_del(sms_rx_seen, key);
			continue;
		}
		if (!(seen = json_object_object_get(sms_rx_seen, key))) {
			json_object_object_add(sms_rx_seen, key, json_object_new_int64(now));
			continue;
		}
		if (now - json_object_get_int64(seen) < (budget_override_ms ? 1000 : 600000) || sms_job_of(j->device_id, SMS_DELETE))
			continue;
		snprintf(idx, sizeof(idx), "%d", json_object_get_int(json_object_object_get(m, "id")));
		if (sms_spawn(SMS_DELETE, j->device_id, j->index, idx, NULL, 15000)) {
			ULOG_WARN("sms %s: an unsent message %s stayed in the storage; deleted (it takes a slot new messages need)\n",
				  j->device_id, idx);
			mm_history_add(&history, j->device_id, (long long)time(NULL), now, "sms-draft-dropped", idx);
			history_dirty = true;
			json_object_object_del(sms_rx_seen, key);
		}
	}
	/* S3: a multipart stuck in `receiving` blocks reception; dropped 120 s after first seen */
	for (i = 0; i < json_object_array_length(merged); i++) {
		json_object *m = json_object_array_get_idx(merged, i), *seen;
		char key[200], idx[16];

		snprintf(key, sizeof(key), "%s|%d", j->device_id, json_object_get_int(json_object_object_get(m, "id")));
		if (!jstr(m, "state") || strcmp(jstr(m, "state"), "receiving")) {
			json_object_object_del(sms_rx_seen, key);
			continue;
		}
		if (!(seen = json_object_object_get(sms_rx_seen, key))) {
			json_object_object_add(sms_rx_seen, key, json_object_new_int64(now));
			continue;
		}
		if (now - json_object_get_int64(seen) < (budget_override_ms ? 1000 : MM_SMS_STUCK_MS) ||
		    sms_job_of(j->device_id, SMS_DELETE))
			continue;
		snprintf(idx, sizeof(idx), "%d", json_object_get_int(json_object_object_get(m, "id")));
		if (sms_spawn(SMS_DELETE, j->device_id, j->index, idx, NULL, 15000)) {
			ULOG_WARN("sms %s: message %s stuck in receiving for %lld s, deleted (it blocks reception)\n",
				  j->device_id, idx, (now - json_object_get_int64(seen)) / 1000);
			mm_history_add(&history, j->device_id, (long long)time(NULL), now, "sms-stuck-dropped", idx);
			history_dirty = true;
			json_object_object_del(sms_rx_seen, key);
		}
	}
	sms_arch_step(j->device_id, NULL, j->index);
	json_object_put(parsed);
}

static void sms_poll_schedule(int ms);
static json_object *sms_fallback_said;	/* device_id -> the ModemManager error last logged with the AT fallback */

/* ---- the AT path: a modem whose AT port is the engine's, and the fallback (user, 2026-10-09) ----
 *
 * «Я бы все же делал через АТ как фолбэк»: where ModemManager holds the modem
 * it stays the first path (S1: it receives), and the engine's own AT port (the
 * T99's ttys are the engine's since r22) takes over when ModemManager fails —
 * a list it cannot read, a send it refuses (S2), a USSD it cannot do. The
 * reply says which path answered and why. A modem without ModemManager uses
 * the AT path alone. Every exchange runs under the port's lease (S15).
 */
#define MM_SMS_AT_LIST_MS 35000
#define MM_SMS_AT_PART_MS 65000
#define MM_USSD_AT_WAIT_MS 40000
static int sms_ref;		/* the concatenation reference of the next long message */

/* the engine's AT node of a modem, or NULL */
static const char *sms_at_node(const char *id, char *buf, size_t size, const char **why)
{
	json_object *rec = mm_registry_find(&registry, id), *srv;
	const char *node;

	*why = "the modem is not attached";
	if (!rec)
		return NULL;
	srv = served(rec);
	node = engine_at_port(srv, why);
	if (node)
		snprintf(buf, size, "%s", node);
	json_object_put(srv);
	return node ? buf : NULL;
}

/* how SMS reach a modem: ModemManager's index and/or the engine's AT node; false with why when neither */
static bool sms_route(const char *id, char *idx, size_t isz, char *node, size_t nsz, const char **why)
{
	static char msg[320];
	const char *why_mm = NULL, *why_at = NULL;
	bool mm, at;

	idx[0] = node[0] = '\0';
	mm = sms_mm_index(id, idx, isz, &why_mm) != NULL;
	if (!mm)
		idx[0] = '\0';
	at = sms_at_node(id, node, nsz, &why_at) != NULL;
	if (mm || at)
		return true;
	if (why_mm && strstr(why_mm, "QModem")) {
		*why = why_mm;
		return false;
	}
	snprintf(msg, sizeof(msg), "SMS and USSD need ModemManager or an AT port of the engine; neither reaches this modem (%s)",
		 why_at ? why_at : "no AT port");
	*why = msg;
	return false;
}

/* SMS and USSD reach this modem now (ModemManager or the engine's AT port); else why not. srv: its
 * served record (called while it is built: no served() here) */
bool sms_capable(const char *id, json_object *srv, const char **why)
{
	char idx[16];
	const char *why_mm = NULL, *why_at = NULL;
	static char msg[320];

	*why = NULL;
	if (sms_mm_index(id, idx, sizeof(idx), &why_mm) || engine_at_port(srv, &why_at))
		return true;
	{
		/* r94: a HiLink's SMS through its own web */
		json_object *rec = mm_registry_find(&registry, id);

		if (rec && (web_sms_capable(rec, srv) || sms_qmodem_section(rec)))
			return true;
	}
	if (why_mm && strstr(why_mm, "QModem")) {
		*why = why_mm;
		return false;
	}
	snprintf(msg, sizeof(msg), "SMS and USSD need ModemManager or an AT port of the engine; neither reaches this modem (%s)",
		 why_at ? why_at : "no AT port");
	*why = msg;
	return false;
}

static struct at_job *sms_at_running(const char *id)
{
	int i;

	for (i = 0; i < MM_AT_JOBS; i++)
		if (at_jobs[i].active && at_jobs[i].kind == AT_SMS && !strcmp(at_jobs[i].device_id, id))
			return &at_jobs[i];
	return NULL;
}

/* who waits for the answer of an AT exchange: a ubus request, deferred here or already */
struct sms_reply_to {
	struct ubus_context *ctx;
	struct ubus_request_data *req;
	bool deferred;
};
/* exchanges waiting for the port of their modem: one at a time per port (S15), the operator's after the pass's */
#define MM_SMS_PENDING 6
static struct sms_pending {
	bool used, has_req;
	char id[160], node[64];
	json_object *job;
	int budget_ms;
	struct ubus_context *ctx;
	struct ubus_request_data req;
} sms_pending[MM_SMS_PENDING];

static void sms_defer_to(const struct sms_reply_to *to, struct ubus_context **ctx, struct ubus_request_data *req)
{
	*ctx = to->ctx;
	if (to->deferred)
		*req = *to->req;
	else
		ubus_defer_request(to->ctx, to->req, req);
}

/* one AT exchange (job owned); false with why. queue: wait behind a running exchange of the same modem */
static bool sms_at_start(const char *id, const char *node, json_object *job, int budget_ms, const struct sms_reply_to *to,
			 bool queue, const char **why)
{
	json_object *rec = mm_registry_find(&registry, id);
	static char msg[256];
	char detail[160] = "";
	struct at_job *j;
	bool same;
	int i;

	*why = NULL;
	if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present"))) {
		*why = "the modem is not attached";
		json_object_put(job);
		return false;
	}
	if (!(j = at_slot(id, AT_SMS, &same))) {
		for (i = 0; same && queue && i < MM_SMS_PENDING; i++) {
			struct sms_pending *q = &sms_pending[i];

			if (q->used)
				continue;
			memset(q, 0, sizeof(*q));
			q->used = true;
			snprintf(q->id, sizeof(q->id), "%s", id);
			snprintf(q->node, sizeof(q->node), "%s", node);
			q->job = job;
			q->budget_ms = budget_ms;
			if (to) {
				q->has_req = true;
				sms_defer_to(to, &q->ctx, &q->req);
			}
			return true;
		}
		*why = same ? "an SMS or USSD exchange is running on this modem's AT port" : "too many AT jobs are running";
		json_object_put(job);
		return false;
	}
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
	j->active = true;
	j->kind = AT_SMS;
	j->started = now_mono_ms();
	j->timeout_ms = budget_ms;
	j->cmds = json_object_new_array();
	json_object_array_add(j->cmds, json_object_new_string(jstr(job, "op") ? jstr(job, "op") : "?"));
	j->results = job;
	j->generation = json_object_get_int64(json_object_object_get(rec, "generation"));
	snprintf(j->device_id, sizeof(j->device_id), "%s", id);
	snprintf(j->node, sizeof(j->node), "%s", node);
	if ((*why = at_lease(j, detail, sizeof(detail))) || !at_spawn(j)) {
		snprintf(msg, sizeof(msg), "the AT port: %s%s%s", *why ? *why : "cannot start a worker", detail[0] ? ", " : "", detail);
		*why = msg;
		at_finish(j, json_object_new_object());
		return false;
	}
	if (to) {
		j->deferred = true;
		sms_defer_to(to, &j->ctx, &j->req);
	}
	return true;
}

/* the next exchange waiting for this modem's port, once the last one ended */
static void sms_at_next(const char *id)
{
	int i;

	for (i = 0; i < MM_SMS_PENDING; i++) {
		struct sms_pending *q = &sms_pending[i];
		struct sms_reply_to to = { q->ctx, &q->req, true };
		const char *why = NULL;

		if (!q->used || strcmp(q->id, id))
			continue;
		q->used = false;
		if (sms_at_start(q->id, q->node, q->job, q->budget_ms, q->has_req ? &to : NULL, false, &why))
			return;
		if (q->has_req) {
			json_object *o = json_object_new_object();

			json_object_object_add(o, "device_id", json_object_new_string(q->id));
			json_object_object_add(o, "refused", json_object_new_string(why ? why : "the AT port is busy"));
			reply_json(q->ctx, &q->req, NULL, o);
			ubus_complete_deferred_request(q->ctx, &q->req, UBUS_STATUS_OK);
			json_object_put(o);
		}
		/* that one could not start: the next queued for this modem is tried, none left waiting for a job that never
		 * comes (audit 2026-10-20) */
	}
}

/* the Snapdragon X20 family that drops CNMI to 0,0 (a field report), and the old
 * Huawei stick 12d1:1506 (the owner's E171, 2026-10-30: replies never showed; checked only, 2,1 written on 0,0) */
static bool sms_cnmi_model(const char *id)
{
	static const char *const cnmi_ids[] = { "413c:81d7", "413c:81e0", "413c:81e4", "413c:81e6", "413c:81d8", "0489:e0b5",
						"0489:e0b4", "12d1:1506", NULL };
	json_object *rec = mm_registry_find(&registry, id);
	const char *vp = rec ? jstr(rec, "vidpid") : NULL;
	int k;

	for (k = 0; vp && cnmi_ids[k]; k++)
		if (!strcmp(vp, cnmi_ids[k]))
			return true;
	return false;
}

static bool sms_at_list(const char *id, const char *node, const struct sms_reply_to *to, const char **why)
{
	json_object *job = json_object_new_object();

	json_object_object_add(job, "op", json_object_new_string("list"));
	if (sms_cnmi_model(id))
		json_object_object_add(job, "cnmi_fix", json_object_new_boolean(1));
	return sms_at_start(id, node, job, MM_SMS_AT_LIST_MS, to, to != NULL, why);
}

/*
 * The same CNMI check for such a modem ModemManager lists (the owner's T77W968 in MBIM, r122: after one sent SMS
 * no reply showed): after every send and with a ModemManager read at most once in 10 min, over the engine's AT port
 * when it has one. Queued behind a running AT job, never in its way.
 */
static json_object *sms_cnmi_at;	/* device_id -> monotonic ms of the last check */

static void sms_cnmi_check(const char *id, bool now)
{
	char node[64];
	const char *why = NULL;
	json_object *job, *t;
	long long ms = now_mono_ms();

	if (!sms_cnmi_model(id) || !sms_at_node(id, node, sizeof(node), &why))
		return;
	if (!sms_cnmi_at)
		sms_cnmi_at = json_object_new_object();
	t = json_object_object_get(sms_cnmi_at, id);
	if (!now && t && ms - json_object_get_int64(t) < 600000)
		return;
	json_object_object_add(sms_cnmi_at, id, json_object_new_int64(ms));
	job = json_object_new_object();
	json_object_object_add(job, "op", json_object_new_string("cnmi"));
	sms_at_start(id, node, job, 15000, NULL, true, &why);
}

static bool sms_at_send(const char *id, const char *node, const char *number, const char *text, const char *mm_error,
			const struct sms_reply_to *to, const char **why)
{
	static char pdus[16][400];
	int lens[16], n, i;
	json_object *job, *arr;

	n = mm_pdu_submit(number, sms_center_of(id), text, (sms_ref++ & 0xff), pdus, lens, 16);
	if (n <= 0) {
		*why = "the text does not fit 16 parts";
		return false;
	}
	job = json_object_new_object();
	arr = json_object_new_array();
	json_object_object_add(job, "op", json_object_new_string("send"));
	for (i = 0; i < n; i++) {
		json_object *one = json_object_new_object();

		json_object_object_add(one, "pdu", json_object_new_string(pdus[i]));
		json_object_object_add(one, "len", json_object_new_int(lens[i]));
		json_object_array_add(arr, one);
	}
	json_object_object_add(job, "pdus", arr);
	json_object_object_add(job, "number", json_object_new_string(number));
	json_object_object_add(job, "text", json_object_new_string(text));
	if (mm_error)
		json_object_object_add(job, "mm_error", json_object_new_string(mm_error));
	return sms_at_start(id, node, job, n * MM_SMS_AT_PART_MS, to, true, why);
}

static bool sms_at_ussd_q(const char *id, const char *node, const char *verb, const char *value, bool quiet, const char **why)
{
	json_object *job = json_object_new_object();

	json_object_object_add(job, "op", json_object_new_string("ussd"));
	if (quiet)
		json_object_object_add(job, "quiet", json_object_new_boolean(true));
	json_object_object_add(job, "verb", json_object_new_string(verb));
	if (value)
		json_object_object_add(job, "value", json_object_new_string(value));
	json_object_object_add(job, "wait_ms", json_object_new_int(budget_override_ms ? 3000 : MM_USSD_AT_WAIT_MS));
	return sms_at_start(id, node, job, (budget_override_ms ? 3000 : MM_USSD_AT_WAIT_MS) + 15000, NULL, true, why);
}

static bool sms_at_ussd(const char *id, const char *node, const char *verb, const char *value, const char **why)
{
	return sms_at_ussd_q(id, node, verb, value, false, why);
}

/*
 * A menu the network opened (session user-response) and nobody answered is
 * closed after MM_USSD_MENU_MS: an open session refuses the next request and
 * keeps the modem's USSD busy (HANDOFF 138).
 */
#define MM_USSD_MENU_MS 60000
static struct uloop_timeout ussd_menu_timer;

static void ussd_menu_arm(json_object *st)
{
	int ms = budget_override_ms ? 1500 : MM_USSD_MENU_MS;

	if (jstr(st, "state") && !strcmp(jstr(st, "state"), "done") && jstr(st, "session") &&
	    !strcmp(jstr(st, "session"), "user-response")) {
		json_object_object_add(st, "menu_until", json_object_new_int64(now_mono_ms() + ms));
		uloop_timeout_set(&ussd_menu_timer, ms + 50);
	} else {
		json_object_object_del(st, "menu_until");
	}
}

/* a deferred ubus request of a ModemManager job, for the AT exchange that answers it instead */
static const struct sms_reply_to *sms_hand(struct sms_job *from, struct sms_reply_to *to)
{
	if (!from->deferred)
		return NULL;
	*to = (struct sms_reply_to){ from->ctx, &from->req, true };
	return to;
}

void sms_at_exited(struct at_job *j, json_object *r, const char *error)
{
	const char *op = jstr(j->results, "op") ? jstr(j->results, "op") : "";
	json_object *reply = NULL;
	bool then_cancel = false, quiet_cancel = false, listed;
	char id[160], node[64];

	at_release(j);
	if (!strcmp(op, "cnmi")) {
		if (json_object_get_boolean(json_object_object_get(r, "cnmi_fixed"))) {
			ULOG_WARN("sms %s: the modem had CNMI 0,0 (it reset it); set 2,1 so incoming SMS are stored\n", j->device_id);
			mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sms-cnmi-fixed", NULL);
			history_dirty = true;
			sms_poll_schedule(budget_override_ms ? 100 : 2000);
		}
	} else if (!strcmp(op, "list")) {
		json_object *parsed = !error && jstr(r, "cmgl") ? mm_sms_at_list(jstr(r, "cpms"), jstr(r, "cmgl")) : NULL;

		if (json_object_get_boolean(json_object_object_get(r, "cnmi_fixed")))
			ULOG_WARN("sms %s: the modem had CNMI 0,0 (it reset it); set 2,1 so incoming SMS are stored\n", j->device_id);
		const char *final = jstr(r, "error") ? NULL : jstr(r, "cmgl");

		if (parsed && final && !strstr(final, "ERROR")) {
			json_object *store = json_object_new_object();
			json_object *prev = json_object_object_get(sms_cache, j->device_id);

			json_object_object_add(store, "name", json_object_get(json_object_object_get(parsed, "storage")));
			json_object_object_add(store, "used", json_object_get(json_object_object_get(parsed, "used")));
			json_object_object_add(store, "total", json_object_get(json_object_object_get(parsed, "total")));
			{
				/* new messages go to mem3: where it is not the storage listed (mem1), they may never show here
				 * (a field report) */
				const char *h = jstr(r, "cpms") ? strstr(jstr(r, "cpms"), "+CPMS:") : NULL;
				char m1[8] = "", m3[8] = "";

				if (h && sscanf(h, "+CPMS: \"%7[A-Z]\",%*d,%*d,\"%*[A-Z]\",%*d,%*d,\"%7[A-Z]\"", m1, m3) == 2 && strcmp(m1, m3)) {
					for (char *c = m3; *c; c++)
						*c = (char)tolower((unsigned char)*c);
					json_object_object_add(store, "incoming", json_object_new_string(m3));
				}
			}
			sms_cache_put(j->device_id, json_object_get(json_object_object_get(parsed, "messages")), "at",
				      prev && jstr(prev, "fallback_pending") ? jstr(prev, "fallback_pending") : NULL, store);
			json_object_put(store);
		} else {
			json_object *c;

			if (!sms_cache)
				sms_cache = json_object_new_object();
			if (!(c = json_object_object_get(sms_cache, j->device_id))) {
				c = json_object_new_object();
				json_object_object_add(sms_cache, j->device_id, c);
			}
			json_object_object_add(c, "error", json_object_new_string(error ? error :
						jstr(r, "error") ? jstr(r, "error") : "the modem did not list its messages over AT"));
		}
		if (json_object_object_get(sms_cache, j->device_id))
			json_object_object_del(json_object_object_get(sms_cache, j->device_id), "fallback_pending");
		json_object_put(parsed);
		if (j->deferred)
			reply = sms_view(j->device_id);
	} else if (!strcmp(op, "send")) {
		int want = (int)json_object_array_length(json_object_object_get(j->results, "pdus"));
		int sent = (int)json_object_array_length(json_object_object_get(r, "refs"));
		bool ok = !error && want && sent == want;

		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(ok));
		json_object_object_add(reply, "via", json_object_new_string("at"));
		if (jstr(j->results, "mm_error"))
			json_object_object_add(reply, "mm_error", json_object_new_string(jstr(j->results, "mm_error")));
		if (want > 1)
			json_object_object_add(reply, "parts", json_object_new_int(want));
		if (!ok) {
			const char *ans = jstr(r, "answer"), *e = ans ? strstr(ans, "+CMS ERROR:") : NULL;
			char detail[200];

			json_object_object_add(reply, "error", json_object_new_string(sent ? "sent in part" : "not sent"));
			snprintf(detail, sizeof(detail), "%.*s", e ? (int)strcspn(e, "\r\n") : 150,
				 e ? e : error ? error : jstr(r, "error") ? jstr(r, "error") : ans ? ans : "");
			if (detail[0])
				json_object_object_add(reply, "detail", json_object_new_string(detail));
			if (sent)
				json_object_object_add(reply, "sent_parts", json_object_new_int(sent));
		}
		sms_sent_add(j->device_id, jstr(j->results, "number"), jstr(j->results, "text"), ok);
		ULOG_INFO("sms %s: send over AT %s%s\n", j->device_id, ok ? "ok" : "failed",
			  jstr(j->results, "mm_error") ? " (ModemManager had failed)" : "");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sms-sent", ok ? "ok at" : "failed at");
		history_dirty = true;
		sms_poll_schedule(budget_override_ms ? 100 : 2000);
		snprintf(id, sizeof(id), "%s", j->device_id);
		sms_cnmi_check(id, true);
	} else if (!strcmp(op, "delete")) {
		json_object *done = json_object_object_get(r, "deleted"), *msgs, *keep;
		size_t i, k;

		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(!error && !json_object_array_length(json_object_object_get(r, "failed")) &&
									    json_object_array_length(done)));
		json_object_object_add(reply, "via", json_object_new_string("at"));
		msgs = json_object_object_get(json_object_object_get(sms_cache, j->device_id), "messages");
		keep = json_object_new_array();
		for (i = 0; msgs && i < json_object_array_length(msgs); i++) {
			json_object *m = json_object_array_get_idx(msgs, i), *parts = json_object_object_get(m, "parts");
			bool gone = false;

			for (k = 0; done && k < json_object_array_length(done) && !gone; k++)
				gone = json_object_get_int(json_object_object_get(m, "id")) == json_object_get_int(json_object_array_get_idx(done, k));
			(void)parts;
			if (!gone)
				json_object_array_add(keep, json_object_get(m));
		}
		if (msgs)
			json_object_object_add(json_object_object_get(sms_cache, j->device_id), "messages", keep);
		else
			json_object_put(keep);
		sms_poll_schedule(budget_override_ms ? 100 : 2000);
	} else if (!strcmp(op, "clear")) {
		const char *del = jstr(r, "cmgd"), *cp = jstr(r, "cpms"), *h = cp ? strstr(cp, "+CPMS:") : NULL;
		const char *del3 = jstr(r, "cmgd3");
		bool ok = !error && del && strstr(del, "OK") && !strstr(del, "ERROR") &&
			  (!del3 || (strstr(del3, "OK") && !strstr(del3, "ERROR")));
		json_object *dev = json_object_object_get(sms_cache, j->device_id);

		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(ok));
		json_object_object_add(reply, "via", json_object_new_string("at"));
		if (h) {
			char line[96];

			snprintf(line, sizeof(line), "%.*s", (int)strcspn(h, "\r\n"), h);
			json_object_object_add(reply, "storage", json_object_new_string(line));
		}
		if (!ok)
			json_object_object_add(reply, "error", json_object_new_string(error ? error : "the modem did not clear the storage"));
		else if (dev)
			json_object_object_add(dev, "messages", json_object_new_array());
		ULOG_INFO("sms %s: storage cleared %s\n", j->device_id, ok ? "ok" : "failed");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sms-cleared", ok ? "done" : "failed");
		history_dirty = true;
		sms_poll_schedule(budget_override_ms ? 100 : 2000);
	} else if (!strcmp(op, "storage")) {
		const char *sm = jstr(r, "sm"), *me = jstr(r, "me"), *cp = jstr(r, "cpms"), *h = cp ? strstr(cp, "+CPMS:") : NULL;
		bool ok = !error && sm && strstr(sm, "OK") && !strstr(sm, "ERROR") && me && strstr(me, "OK") && !strstr(me, "ERROR");

		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(ok));
		json_object_object_add(reply, "via", json_object_new_string("at"));
		if (h) {
			char line[96];

			snprintf(line, sizeof(line), "%.*s", (int)strcspn(h, "\r\n"), h);
			json_object_object_add(reply, "storage", json_object_new_string(line));
		}
		if (!ok) {
			const char *e = sm && strstr(sm, "ERROR") ? sm : me && strstr(me, "ERROR") ? me : NULL;

			json_object_object_add(reply, "error", json_object_new_string(error ? error : "the modem did not take the storage"));
			if (e) {
				const char *x = strstr(e, "+CMS ERROR") ? strstr(e, "+CMS ERROR") : strstr(e, "+CME ERROR") ? strstr(e, "+CME ERROR") : "ERROR";
				char line[64];

				snprintf(line, sizeof(line), "%.*s", (int)strcspn(x, "\r\n"), x);
				json_object_object_add(reply, "detail", json_object_new_string(line));
			}
		}
		ULOG_INFO("sms %s: storage switched SIM → modem %s\n", j->device_id, ok ? "ok" : "failed");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sms-storage", ok ? "switched" : "failed");
		history_dirty = true;
		sms_poll_schedule(budget_override_ms ? 100 : 2000);
	} else if (!strcmp(op, "center")) {
		const char *csca = jstr(r, "csca"), *h = csca ? strstr(csca, "+CSCA:") : NULL, *set = jstr(r, "set");
		const char *value = jstr(j->results, "value");
		char got[128] = "", raw[128] = "";
		bool ok = !error && h;

		if (h && sscanf(h, "+CSCA: \"%127[^\"]\"", raw) == 1) {
			size_t n = strlen(raw), i, k = 0;
			bool hex = n >= 8 && n % 4 == 0 && strspn(raw, "0123456789ABCDEFabcdef") == n;

			/* UCS2 character set: "002B0037…" */
			for (i = 0; hex && i < n && k + 1 < sizeof(got); i += 4) {
				unsigned int u = 0;

				sscanf(raw + i, "%4x", &u);
				got[k++] = u < 128 ? (char)u : '?';
			}
			got[k] = '\0';
			if (!hex)
				snprintf(got, sizeof(got), "%s", raw);
		}
		if (value && (!set || !strstr(set, "OK") || strstr(set, "ERROR")))
			ok = false;
		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(ok));
		json_object_object_add(reply, "via", json_object_new_string("at"));
		if (got[0])
			json_object_object_add(reply, "center", json_object_new_string(got));
		if (value)
			json_object_object_add(reply, "written", json_object_new_boolean(ok));
		if (!ok) {
			const char *e = set && strstr(set, "ERROR") ? set : csca && strstr(csca, "ERROR") ? csca : NULL;

			json_object_object_add(reply, "error", json_object_new_string(error ? error : value ? "the modem did not take the SMS centre" :
										 "the modem did not give its SMS centre"));
			if (e)
				json_object_object_add(reply, "detail", json_object_new_string(strstr(e, "+CMS ERROR") ? strstr(e, "+CMS ERROR") :
											strstr(e, "+CME ERROR") ? strstr(e, "+CME ERROR") : "ERROR"));
		}
		if (value) {
			/* the SMS centre is the operator's public number, not the subscriber's */
			ULOG_INFO("sms %s: SMS centre %s on the SIM\n", j->device_id, ok ? "written" : "not written");
			mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sms-center", ok ? "written" : "failed");
			history_dirty = true;
		}
	} else if (!strcmp(op, "ussd")) {
		json_object *st = json_object_object_get(ussd_states, j->device_id), *u;
		const char *verb = jstr(j->results, "verb") ? jstr(j->results, "verb") : "";
		const char *cmd = jstr(r, "cusd");

		if (st && strcmp(verb, "cancel") && jstr(st, "state") && !strcmp(jstr(st, "state"), "cancelled")) {
			/* cancelled while it ran: end the network's session too, once this job is gone */
			then_cancel = true;
		} else if (st && !strcmp(verb, "cancel")) {
			if (!json_object_get_boolean(json_object_object_get(j->results, "quiet"))) {
				json_object_object_add(st, "state", json_object_new_string("cancelled"));
				json_object_object_del(st, "menu_until");
			}
			json_object_object_add(st, "session", json_object_new_string("idle"));
		} else if (st) {
			const char *err = NULL;
			char refused[160];

			u = jstr(r, "urc") ? mm_ussd_urc(jstr(r, "urc")) : cmd ? mm_ussd_urc(cmd) : NULL;
			if (!u) {
				if (cmd && strstr(cmd, "ERROR")) {
					const char *e = strstr(cmd, "ERROR");
					const char *s0 = strstr(cmd, "+CME ERROR") ? strstr(cmd, "+CME ERROR") : e;

					snprintf(refused, sizeof(refused), "the modem refused the request (%.*s)", (int)strcspn(s0, "\r\n"), s0);
					err = refused;
				} else {
					err = error ? error : jstr(r, "error") ? jstr(r, "error") : "no answer from the network in time";
				}
			} else {
				int m = json_object_get_int(json_object_object_get(u, "m"));

				if (m == 4)
					err = "USSD not supported by the network or the modem";
				else if (m == 5)
					err = "no answer from the network in time";
				else if (m == 2 && !jstr(u, "reply"))
					err = "the network ended the session";
				json_object_object_add(st, "session", json_object_new_string(m == 1 ? "user-response" : "idle"));
			}
			json_object_object_add(st, "state", json_object_new_string(err ? "failed" : "done"));
			json_object_object_add(st, "reply", !err && u ? json_object_new_string(jstr(u, "reply") ? jstr(u, "reply") : "") : NULL);
			json_object_object_add(st, "error", err ? json_object_new_string(err) : NULL);
			/* no answer: the network may still hold the session; end it so the next request starts clean */
			if (err && !u)
				quiet_cancel = true;
			json_object_put(u);
			ussd_menu_arm(st);
		}
		if (st) {
			json_object_object_add(st, "via", json_object_new_string("at"));
			json_object_object_add(st, "finished_at", json_object_new_int64((long long)time(NULL)));
			ULOG_INFO("ussd %s: %s over AT %s\n", j->device_id, verb, jstr(st, "state"));
		}
	}
	if (!reply)
		reply = json_object_new_object();
	snprintf(id, sizeof(id), "%s", j->device_id);
	snprintf(node, sizeof(node), "%s", j->node);
	listed = !strcmp(op, "list");
	at_finish(j, reply);
	if (listed)
		sms_arch_step(id, node, NULL);
	if (then_cancel || quiet_cancel) {
		const char *why = NULL;

		sms_at_ussd_q(id, node, "cancel", NULL, quiet_cancel && !then_cancel, &why);
	}
	sms_at_next(id);
}

static void sms_exited(struct uloop_process *p, int ret)
{
	struct sms_job *j = container_of(p, struct sms_job, proc);
	int status = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
	json_object *reply = NULL;

	uloop_timeout_cancel(&j->kill);
	sms_drain(j);
	sms_close_out(j);
	switch (j->kind) {
	case SMS_LIST: {
		json_object *c;

		sms_list_done(j, status);
		sms_cnmi_check(j->device_id, false);
		c = json_object_object_get(sms_cache, j->device_id);
		if (j->at_node[0] && c && jstr(c, "error")) {
			/* ModemManager could not list: the AT storage is read instead, and the page says so */
			const char *why = NULL;
			struct sms_reply_to to;

			json_object_object_add(c, "fallback_pending", json_object_new_string(jstr(c, "error")));
			if (sms_at_list(j->device_id, j->at_node, sms_hand(j, &to), &why)) {
				/* said once per modem and reason, not at every pass */
				json_object *said = sms_fallback_said ? json_object_object_get(sms_fallback_said, j->device_id) : NULL;

				if (!said || strcmp(json_object_get_string(said), jstr(c, "error"))) {
					ULOG_WARN("sms %s: ModemManager did not list (%s); reading over AT\n", j->device_id, jstr(c, "error"));
					if (!sms_fallback_said)
						sms_fallback_said = json_object_new_object();
					json_object_object_add(sms_fallback_said, j->device_id, json_object_new_string(jstr(c, "error")));
				}
				j->deferred = false;
			}
		} else if (c && !jstr(c, "error") && sms_fallback_said) {
			json_object_object_del(sms_fallback_said, j->device_id);	/* ModemManager lists again: a new failure is said */
		}
		if (j->deferred)
			reply = sms_view(j->device_id);
		break;
	}
	case SMS_SEND:
		if ((status != 0 && !j->killed) && j->at_node[0] && j->number && j->text) {
			/* S2 with the user's fallback: what ModemManager could not send goes over AT, said openly */
			const char *e = j->buf ? strstr(j->buf, "error: ") : NULL, *why = NULL;
			char mm_error[240];
			struct sms_reply_to to;

			snprintf(mm_error, sizeof(mm_error), "%s: %.*s", status == 4 ? "created but not sent" : "not created",
				 (int)strcspn(e ? e + 7 : (j->buf ? j->buf : ""), "\n"), e ? e + 7 : (j->buf ? j->buf : ""));
			if (sms_at_send(j->device_id, j->at_node, j->number, j->text, mm_error, sms_hand(j, &to), &why)) {
				ULOG_WARN("sms %s: ModemManager did not send; sending over AT\n", j->device_id);
				mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sms-sent", "failed");
				history_dirty = true;
				j->deferred = false;
				break;
			}
		}
		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(status == 0 && !j->killed));
		if (status != 0 || j->killed) {
			/* ModemManager's own words (S2: a firmware may refuse what AT would send) */
			const char *e = j->buf ? strstr(j->buf, "error: ") : NULL;
			char msg[240];

			snprintf(msg, sizeof(msg), "%.*s", (int)strcspn(e ? e + 7 : (j->buf ? j->buf : ""), "\n"),
				 e ? e + 7 : (j->buf ? j->buf : ""));
			json_object_object_add(reply, "error", json_object_new_string(j->killed ? "not sent in time" :
						status == 4 ? "created but not sent" : "not created"));
			if (msg[0])
				json_object_object_add(reply, "detail", json_object_new_string(msg));
		}
		ULOG_INFO("sms %s: send %s\n", j->device_id, status == 0 && !j->killed ? "ok" : "failed");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sms-sent",
			       status == 0 && !j->killed ? "ok" : "failed");
		history_dirty = true;
		sms_cnmi_check(j->device_id, true);
		break;
	case SMS_DELETE:
		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(status == 0 && !j->killed));
		if (status == 0 && !j->killed && sms_cache) {
			json_object *msgs = json_object_object_get(json_object_object_get(sms_cache, j->device_id), "messages");
			json_object *keep = json_object_new_array();
			size_t i;

			for (i = 0; msgs && i < json_object_array_length(msgs); i++)
				if (json_object_get_int(json_object_object_get(json_object_array_get_idx(msgs, i), "id")) != j->target)
					json_object_array_add(keep, json_object_get(json_object_array_get_idx(msgs, i)));
			if (msgs)
				json_object_object_add(json_object_object_get(sms_cache, j->device_id), "messages", keep);
			else
				json_object_put(keep);
		}
		break;
	case QM_LIST: {
		/* r95: QModem's get_sms (its tom_modem reads the AT storage; QModem caches it 20 s) */
		json_object *msgs = status == 0 && !j->killed ? mm_sms_qmodem_list(j->buf) : NULL;

		if (msgs) {
			sms_cache_put(j->device_id, msgs, "qmodem", NULL, NULL);
		} else {
			json_object *c;

			if (!sms_cache)
				sms_cache = json_object_new_object();
			if (!(c = json_object_object_get(sms_cache, j->device_id))) {
				c = json_object_new_object();
				json_object_object_add(c, "path", json_object_new_string("qmodem"));
				json_object_object_add(sms_cache, j->device_id, c);
			}
			json_object_object_add(c, "error", json_object_new_string(j->killed ? "QModem did not list the messages in time" :
										    "QModem did not list the messages"));
		}
		break;
	}
	case QM_SEND: {
		/* one PDU per run; the parts left wait in value (newline-separated) */
		bool ok = status == 0 && !j->killed && j->buf && strstr(j->buf, "\"status\":\"1\"") && !strstr(j->buf, "ERROR");
		char *rest = j->value;

		if (ok && rest && *rest) {
			char *nl = strchr(rest, '\n'), *next = nl ? nl + 1 : NULL;
			struct sms_job *k;

			if (nl)
				*nl = '\0';
			if ((k = sms_spawn(QM_SEND, j->device_id, j->index, rest, NULL, MM_SMS_SEND_MS))) {
				k->value = next && *next ? strdup(next) : NULL;
				if (j->deferred) {
					k->deferred = true;
					k->ctx = j->ctx;
					k->req = j->req;
					j->deferred = false;
				}
				break;
			}
			ok = false;
		}
		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(ok));
		if (!ok)
			json_object_object_add(reply, "error", json_object_new_string(j->killed ? "not sent in time" : "QModem did not send it"));
		ULOG_INFO("sms %s: send through QModem %s\n", j->device_id, ok ? "ok" : "failed");
		mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "sms-sent", ok ? "ok" : "failed");
		history_dirty = true;
		break;
	}
	case QM_DELETE: {
		bool ok = status == 0 && !j->killed;

		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(reply, "ok", json_object_new_boolean(ok));
		if (ok && sms_cache) {
			json_object *msgs = json_object_object_get(json_object_object_get(sms_cache, j->device_id), "messages");
			json_object *keep = json_object_new_array();
			size_t i;

			for (i = 0; msgs && i < json_object_array_length(msgs); i++)
				if (json_object_get_int(json_object_object_get(json_object_array_get_idx(msgs, i), "id")) != j->target)
					json_object_array_add(keep, json_object_get(json_object_array_get_idx(msgs, i)));
			if (msgs)
				json_object_object_add(json_object_object_get(sms_cache, j->device_id), "messages", keep);
			else
				json_object_put(keep);
		}
		break;
	}
	default: {
		json_object *st = json_object_object_get(ussd_states, j->device_id), *u;
		char *mark = j->buf ? strstr(j->buf, "-- ussd status --") : NULL;
		json_object *sj = mark ? json_tokener_parse(mark + strlen("-- ussd status --\n")) : NULL;
		const char *session;

		if (!st)
			break;
		if (mark)
			*mark = '\0';
		u = json_object_object_get(json_object_object_get(json_object_object_get(sj, "modem"), "3gpp"), "ussd");
		session = jstr(u, "status");
		json_object_object_add(st, "session", session && strcmp(session, "--") ? json_object_new_string(session) : NULL);
		if (j->kind == USSD_CANCEL && j->quiet) {
			;	/* the engine's own cleanup after a failure: the failure stays what the operator sees */
		} else if (j->kind == USSD_CANCEL) {
			json_object_object_add(st, "state", json_object_new_string("cancelled"));
			json_object_object_del(st, "menu_until");
		} else if (jstr(st, "state") && !strcmp(jstr(st, "state"), "cancelled")) {
			;	/* the operator cancelled it: the request's own end does not undo that */
		} else {
			json_object *r = mm_ussd_parse(j->killed ? "" : j->buf, j->killed ? 1 : status);
			const char *e = jstr(r, "error");
			/* HANDOFF 138: no answer in time is the network's or the modem's, not ModemManager's; the
			 * session may still be open there, so the AT port would start a second one beside it */
			bool late = j->killed || (e && (strcasestr(e, "timeout") || strcasestr(e, "timed out")));
			bool open = session && (!strcmp(session, "active") || !strcmp(session, "user-response"));

			json_object_object_add(st, "state", json_object_new_string(jstr(r, "reply") ? "done" : "failed"));
			json_object_object_add(st, "reply", jstr(r, "reply") ? json_object_new_string(jstr(r, "reply")) : NULL);
			json_object_object_add(st, "error", e ? json_object_new_string(late ? "no answer from the network in time" : e) : NULL);
			json_object_object_add(st, "via", json_object_new_string("modemmanager"));
			if (!jstr(r, "reply") && (late || open)) {
				/* end whatever is left of it, so the next request starts clean (U: a stale session refuses it) */
				struct sms_job *c = sms_spawn(USSD_CANCEL, j->device_id, j->index, NULL, NULL, 15000);

				if (c)
					c->quiet = true;
				json_object_object_add(st, "session", json_object_new_string("idle"));
			} else if (!jstr(r, "reply") && j->at_node[0] && j->value) {
				/* ModemManager could not: the same request over the engine's AT port */
				const char *why = NULL;

				if (sms_at_ussd(j->device_id, j->at_node, j->kind == USSD_START ? "start" : "respond", j->value, &why)) {
					json_object_object_add(st, "mm_error", json_object_new_string(jstr(r, "error") ? jstr(r, "error") : "failed"));
					json_object_object_add(st, "state", json_object_new_string("running"));
					json_object_object_add(st, "via", json_object_new_string("at"));
					json_object_object_del(st, "error");
				}
			}
			json_object_put(r);
		}
		json_object_object_add(st, "finished_at", json_object_new_int64((long long)time(NULL)));
		if (j->kind != USSD_CANCEL)
			ussd_menu_arm(st);
		/* the code is the operator's (a balance query), the reply may be personal: neither is logged */
		ULOG_INFO("ussd %s: %s %s\n", j->device_id, sms_kind_names[j->kind], jstr(st, "state"));
		json_object_put(sj);
	}
	}
	if (j->deferred) {
		if (!reply)
			reply = sms_view(j->device_id);
		reply_json(j->ctx, &j->req, NULL, reply);
		ubus_complete_deferred_request(j->ctx, &j->req, UBUS_STATUS_OK);
	}
	json_object_put(reply);
	if (j->kind == SMS_SEND || j->kind == SMS_DELETE || j->kind == QM_SEND || j->kind == QM_DELETE)
		sms_poll_schedule(budget_override_ms ? 100 : 2000);	/* the list shows it soon */
	free(j->buf);
	free(j->number);
	free(j->text);
	free(j->value);
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
}

static int sms_poll_ms(void)
{
	int s = atoi(core_opt("sms_poll", "60"));

	if (s <= 0)
		return 0;
	return budget_override_ms ? 2000 : (s < 15 ? 15 : s) * 1000;
}

static void sms_poll(struct uloop_timeout *t)
{
	int every = sms_poll_ms();

	(void)t;
	if (!every && !sms_cache)
		return;
	if (mm_devices && !strcmp(mm_backend, "available")) {
		json_object_object_foreach(mm_devices, id, mm) {
			char idx[16], skip[512];
			const char *why;
			json_object *rec = mm_registry_find(&registry, id);

			char node[64];
			struct sms_job *j;

			/* a modem ModemManager gave up on (state failed: no SIM, an SIM error) has no messages to list; read every
			 * minute it only wrote «did not list … reading over AT» (an LN960 without its SIM, r135) */
			if (jstr(mm, "state") && !strcmp(jstr(mm, "state"), "failed"))
				continue;
			if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present")) ||
			    !sms_mm_index(id, idx, sizeof(idx), &why) || sms_job_of(id, SMS_LIST) || rec_busy(id) || ussd_running(id))
				continue;
			sms_skip_list(id, idx, skip, sizeof(skip));
			if ((j = sms_spawn(SMS_LIST, id, idx, skip, NULL, 20000)) && sms_at_node(id, node, sizeof(node), &why))
				snprintf(j->at_node, sizeof(j->at_node), "%s", node);
		}
	}
	/* modems without ModemManager: their AT port is read once SMS were asked for since the start
	 * (the port is not touched for a modem nobody reads SMS on); a HiLink's web likewise (r94) */
	if (sms_cache) {
		json_object_object_foreach(sms_cache, id, c) {
			json_object *rec = mm_registry_find(&registry, id);
			char idx[16], node[64];
			const char *why;

			if (rec && json_object_get_boolean(json_object_object_get(rec, "present")) && jstr(c, "path") &&
			    !strcmp(jstr(c, "path"), "qmodem")) {
				const char *sec = sms_qmodem_section(rec);

				if (sec && !sms_job_of(id, QM_LIST) && !rec_busy(id))
					sms_spawn(QM_LIST, id, sec, NULL, NULL, 30000);
				continue;
			}
			if (rec && json_object_get_boolean(json_object_object_get(rec, "present")) && jstr(c, "path") &&
			    !strcmp(jstr(c, "path"), "web")) {
				json_object *srv = served(rec);

				if (web_sms_capable(rec, srv))
					web_sms(rec, srv, "list", NULL, NULL, -1, NULL, NULL, &why);
				json_object_put(srv);
				continue;
			}
			if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present")) || rec_busy(id) ||
			    sms_mm_index(id, idx, sizeof(idx), &why) || !sms_at_node(id, node, sizeof(node), &why) || sms_at_running(id) ||
			    ussd_running(id))
				continue;
			sms_at_list(id, node, NULL, &why);
		}
	}
	if (every)
		uloop_timeout_set(&sms_timer, every);
}

static void sms_poll_schedule(int ms)
{
	sms_timer.cb = sms_poll;
	uloop_timeout_set(&sms_timer, ms);
}

enum { SMS_ID, SMS_NUMBER, SMS_TEXT, SMS_INDEX, SMS_REFRESH, SMS_PEEK, __SMS_MAX };
const struct blobmsg_policy sms_policy[__SMS_MAX] = {
	[SMS_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[SMS_NUMBER] = { .name = "number", .type = BLOBMSG_TYPE_STRING },
	[SMS_TEXT] = { .name = "text", .type = BLOBMSG_TYPE_STRING },
	[SMS_INDEX] = { .name = "sms", .type = BLOBMSG_TYPE_INT32 },
	[SMS_REFRESH] = { .name = "refresh", .type = BLOBMSG_TYPE_BOOL },
	/* sms_list for the Telegram bot: what is known, nothing marked seen (the owner reads on the router) */
	[SMS_PEEK] = { .name = "peek", .type = BLOBMSG_TYPE_BOOL },
};

static int sms_refuse(struct ubus_context *ctx, struct ubus_request_data *req, const char *id, const char *why)
{
	json_object *o = json_object_new_object();
	int rc;

	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "refused", json_object_new_string(why));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* r95: QModem holds this modem: its section, when QModem is there and the section names this modem's port */
const char *sms_qmodem_section(json_object *rec)
{
	const char *sec = rec ? qmodem_section_of(rec) : NULL;

	if (!sec || !qmodem_installed() || qmodem_foreign_port(rec, sec) || frozen(jstr(rec, "device_id")))
		return NULL;
	return sec;
}

/*
 * r95: a modem QModem holds (an FM350 under QModem): its SMS through QModem's own modem_ctrl.sh — get_sms,
 * send_raw_pdu (our PDUs, one call per part: its send_sms hands the text to a shell unquoted), delete_sms. USSD,
 * the storage and the SMS centre stay QModem's.
 */
static int sms_qm_method(struct ubus_context *ctx, struct ubus_request_data *req, const char *method, json_object *rec,
			 const char *sec, struct blob_attr **tb)
{
	const char *id = jstr(rec, "device_id");
	json_object *c = sms_cache ? json_object_object_get(sms_cache, id) : NULL;
	struct sms_job *j = NULL;

	if (!strcmp(method, "sms_list")) {
		bool peek = tb[SMS_PEEK] && blobmsg_get_bool(tb[SMS_PEEK]);
		bool running = sms_job_of(id, QM_LIST) != NULL;

		if (c && json_object_object_get(c, "read_ms") && !(tb[SMS_REFRESH] && blobmsg_get_bool(tb[SMS_REFRESH]))) {
			json_object *o;
			int rc;

			if (now_mono_ms() - json_object_get_int64(json_object_object_get(c, "read_ms")) > MM_SMS_FRESH_MS && !running)
				sms_spawn(QM_LIST, id, sec, NULL, NULL, 30000);
			o = sms_view_mark(id, !peek);
			rc = reply_json(ctx, req, NULL, o);
			json_object_put(o);
			return rc;
		}
		if (running || peek) {
			if (!running)
				sms_spawn(QM_LIST, id, sec, NULL, NULL, 30000);
			return sms_refuse(ctx, req, id, "the messages are being read; ask again in a moment");
		}
		j = sms_spawn(QM_LIST, id, sec, NULL, NULL, 30000);
	} else if (!strcmp(method, "sms_send")) {
		const char *number = tb[SMS_NUMBER] ? blobmsg_get_string(tb[SMS_NUMBER]) : NULL;
		const char *text = tb[SMS_TEXT] ? blobmsg_get_string(tb[SMS_TEXT]) : NULL;
		char pdus[16][400], rest[16 * 401] = "";
		int lens[16], n, i;
		size_t used = 0;

		if (!mm_sms_number_ok(number))
			return sms_refuse(ctx, req, id, "the number: digits only, an optional + in front, 3 to 20 digits");
		if (!mm_sms_text_ok(text))
			return sms_refuse(ctx, req, id, "the text: 1 to 1600 bytes of UTF-8, no control characters");
		if (sms_job_of(id, QM_SEND))
			return sms_refuse(ctx, req, id, "a message is being sent from this modem");
		n = mm_pdu_submit(number, sms_center_of(id), text, (sms_ref++ & 0xff), pdus, lens, 16);
		if (n <= 0)
			return sms_refuse(ctx, req, id, "the message does not fit");
		for (i = 1; i < n; i++)
			used += (size_t)snprintf(rest + used, sizeof(rest) - used, "%s%s", i > 1 ? "\n" : "", pdus[i]);
		j = sms_spawn(QM_SEND, id, sec, pdus[0], NULL, MM_SMS_SEND_MS);
		if (j && used)
			j->value = strdup(rest);
		explicit_bzero(pdus, sizeof(pdus));
		explicit_bzero(rest, sizeof(rest));
	} else if (!strcmp(method, "sms_delete")) {
		json_object *msgs = json_object_object_get(c, "messages"), *m = NULL, *parts;
		char list[256] = "";
		size_t i, used = 0;
		int want;

		if (!tb[SMS_INDEX] || (int)blobmsg_get_u32(tb[SMS_INDEX]) < 0)
			return UBUS_STATUS_INVALID_ARGUMENT;
		want = (int)blobmsg_get_u32(tb[SMS_INDEX]);
		if (want >= SMS_ARCH_ID)
			return sms_arch_delete_reply(ctx, req, id, want);
		sms_arch_forget(id, want);
		for (i = 0; msgs && i < json_object_array_length(msgs) && !m; i++)
			if (json_object_get_int(json_object_object_get(json_object_array_get_idx(msgs, i), "id")) == want)
				m = json_object_array_get_idx(msgs, i);
		parts = json_object_object_get(m, "parts");
		for (i = 0; parts && i < json_object_array_length(parts) && used + 12 < sizeof(list); i++)
			used += (size_t)snprintf(list + used, sizeof(list) - used, "%s%d", i ? " " : "",
						 json_object_get_int(json_object_array_get_idx(parts, i)));
		if (!used)
			snprintf(list, sizeof(list), "%d", want);
		j = sms_spawn(QM_DELETE, id, sec, list, NULL, 30000);
		if (j)
			j->target = want;
	} else {
		return sms_refuse(ctx, req, id, "QModem holds this modem: its USSD, SMS storage and SMS centre are set in QModem");
	}
	if (!j)
		return sms_refuse(ctx, req, id, "busy: too many SMS requests at once; ask again in a moment");
	j->ctx = ctx;
	j->deferred = true;
	ubus_defer_request(ctx, req, &j->req);
	return UBUS_STATUS_OK;
}

/*
 * r94: a HiLink modem (E3372-325 and kin) keeps its SMS behind its own web: list, send and delete through it
 * (mgr_web.c starts the worker, websig.c speaks the API), the answers into the same cache, journal, forwarding
 * and bot event as every other path. Its storage and SMS centre are the web's own business: not offered.
 */
static int sms_web_method(struct ubus_context *ctx, struct ubus_request_data *req, const char *method, json_object *rec,
			  json_object *srv, struct blob_attr **tb)
{
	const char *id = jstr(rec, "device_id"), *why = NULL;
	json_object *c = sms_cache ? json_object_object_get(sms_cache, id) : NULL;

	if (!strcmp(method, "sms_list")) {
		bool peek = tb[SMS_PEEK] && blobmsg_get_bool(tb[SMS_PEEK]);
		bool fresh = c && json_object_object_get(c, "read_ms") &&
			     now_mono_ms() - json_object_get_int64(json_object_object_get(c, "read_ms")) <= MM_SMS_FRESH_MS;

		if (c && json_object_object_get(c, "read_ms") && !(tb[SMS_REFRESH] && blobmsg_get_bool(tb[SMS_REFRESH]))) {
			json_object *o;
			int rc;

			if (!fresh)
				web_sms(rec, srv, "list", NULL, NULL, -1, NULL, NULL, &why);	/* busy: the next pass */
			o = sms_view_mark(id, !peek);
			rc = reply_json(ctx, req, NULL, o);
			json_object_put(o);
			return rc;
		}
		if (peek) {
			web_sms(rec, srv, "list", NULL, NULL, -1, NULL, NULL, &why);
			return sms_refuse(ctx, req, id, "the messages are being read; ask again in a moment");
		}
		return web_sms(rec, srv, "list", NULL, NULL, -1, ctx, req, &why) ? UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
	}
	if (!strcmp(method, "sms_send")) {
		const char *number = tb[SMS_NUMBER] ? blobmsg_get_string(tb[SMS_NUMBER]) : NULL;
		const char *text = tb[SMS_TEXT] ? blobmsg_get_string(tb[SMS_TEXT]) : NULL;

		if (!mm_sms_number_ok(number))
			return sms_refuse(ctx, req, id, "the number: digits only, an optional + in front, 3 to 20 digits");
		if (!mm_sms_text_ok(text))
			return sms_refuse(ctx, req, id, "the text: 1 to 1600 bytes of UTF-8, no control characters");
		return web_sms(rec, srv, "send", number, text, -1, ctx, req, &why) ? UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
	}
	if (!strcmp(method, "sms_delete")) {
		if (!tb[SMS_INDEX] || (int)blobmsg_get_u32(tb[SMS_INDEX]) < 0)
			return UBUS_STATUS_INVALID_ARGUMENT;
		if ((int)blobmsg_get_u32(tb[SMS_INDEX]) >= SMS_ARCH_ID)
			return sms_arch_delete_reply(ctx, req, id, (int)blobmsg_get_u32(tb[SMS_INDEX]));
		sms_arch_forget(id, (int)blobmsg_get_u32(tb[SMS_INDEX]));
		return web_sms(rec, srv, "delete", NULL, NULL, (int)blobmsg_get_u32(tb[SMS_INDEX]), ctx, req, &why) ?
		       UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
	}
	return sms_refuse(ctx, req, id, "this modem keeps its SMS behind its own web: its storage and SMS centre are set there");
}

/* what the web said: the list into the cache (news journalled and forwarded as on the other paths), or the outcome */
void sms_web_done(const char *id, const char *op, int index, json_object *r, struct ubus_context *ctx, struct ubus_request_data *req)
{
	json_object *reply = NULL, *msgs = json_object_object_get(r, "messages");
	const char *auth = jstr(r, "auth"), *err = jstr(r, "error");
	char why[200] = "";

	if (auth && (!strcmp(auth, "needed") || !strcmp(auth, "wrong")))
		snprintf(why, sizeof(why), "the modem's web wants its password: set it on the modem's page, «Settings»");
	else if (auth && !strcmp(auth, "locked"))
		snprintf(why, sizeof(why), "the modem's web locked its logins for a while");
	else if (auth && !strcmp(auth, "waiting"))
		snprintf(why, sizeof(why), "the last login to the modem's web failed less than 5 minutes ago");
	else if (err)
		snprintf(why, sizeof(why), "%s", err);
	if (!strcmp(op, "list")) {
		if (json_object_is_type(msgs, json_type_array)) {
			sms_cache_put(id, json_object_get(msgs), "web", NULL, NULL);
		} else {
			json_object *c;

			if (!sms_cache)
				sms_cache = json_object_new_object();
			if (!(c = json_object_object_get(sms_cache, id))) {
				c = json_object_new_object();
				json_object_object_add(c, "path", json_object_new_string("web"));
				json_object_object_add(sms_cache, id, c);
			}
			json_object_object_add(c, "error", json_object_new_string(why[0] ? why : "the modem's web did not list the messages"));
		}
		if (ctx)
			reply = sms_view(id);
	} else {
		bool ok = json_object_get_boolean(json_object_object_get(r, "ok"));

		reply = json_object_new_object();
		json_object_object_add(reply, "device_id", json_object_new_string(id));
		json_object_object_add(reply, "ok", json_object_new_boolean(ok));
		if (!ok)
			json_object_object_add(reply, "error", json_object_new_string(why[0] ? why : "the modem's web refused it"));
		if (!strcmp(op, "send")) {
			ULOG_INFO("sms %s: send through the web %s\n", id, ok ? "ok" : "failed");
			mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "sms-sent", ok ? "ok" : "failed");
			history_dirty = true;
		} else if (ok && sms_cache) {
			json_object *list = json_object_object_get(json_object_object_get(sms_cache, id), "messages"), *keep;
			size_t i;

			if (list) {
				keep = json_object_new_array();
				for (i = 0; i < json_object_array_length(list); i++)
					if (json_object_get_int(json_object_object_get(json_object_array_get_idx(list, i), "id")) != index)
						json_object_array_add(keep, json_object_get(json_object_array_get_idx(list, i)));
				json_object_object_add(json_object_object_get(sms_cache, id), "messages", keep);
			}
		}
		sms_poll_schedule(budget_override_ms ? 100 : 2000);	/* the list shows it soon */
	}
	if (ctx) {
		reply_json(ctx, req, NULL, reply);
		ubus_complete_deferred_request(ctx, req, UBUS_STATUS_OK);
	}
	json_object_put(reply);
}

int m_sms(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__SMS_MAX];
	json_object *rec, *c;
	const char *id, *why = NULL;
	char idx[16], node[64], arg[2048], target[16];
	struct sms_job *j = NULL;
	struct sms_reply_to to = { ctx, req, false };
	int rc;

	(void)obj;
	blobmsg_parse(sms_policy, __SMS_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[SMS_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[SMS_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	if (!sms_route(id, idx, sizeof(idx), node, sizeof(node), &why)) {
		json_object *srv = served(rec);

		const char *sec = sms_qmodem_section(rec);

		rc = web_sms_capable(rec, srv) ? sms_web_method(ctx, req, method, rec, srv, tb) :
		     sec ? sms_qm_method(ctx, req, method, rec, sec, tb) : sms_refuse(ctx, req, id, why);
		json_object_put(srv);
		return rc;
	}
	if (rec_busy(id))
		return sms_refuse(ctx, req, id, "a recovery is running on this modem");
	c = sms_cache ? json_object_object_get(sms_cache, id) : NULL;
	if (!strcmp(method, "sms_list")) {
		bool running = sms_job_of(id, SMS_LIST) || (!idx[0] && sms_at_running(id));
		bool peek = tb[SMS_PEEK] && blobmsg_get_bool(tb[SMS_PEEK]);

		/* a peek answers from what is known only, and never waits: not read yet, the read starts and it says so */
		if (peek && !(c && json_object_object_get(c, "read_ms"))) {
			if (!running) {
				char skip[512];

				if (idx[0]) {
					sms_skip_list(id, idx, skip, sizeof(skip));
					if ((j = sms_spawn(SMS_LIST, id, idx, skip, NULL, 20000)))
						snprintf(j->at_node, sizeof(j->at_node), "%s", node);
				} else {
					sms_at_list(id, node, NULL, &why);
				}
			}
			return sms_refuse(ctx, req, id, "the messages are being read; ask again in a moment");
		}

		if (c && json_object_object_get(c, "read_ms") && !(tb[SMS_REFRESH] && blobmsg_get_bool(tb[SMS_REFRESH]))) {
			json_object *o;

			/* stale: read again in the background, answer with what is known */
			if (now_mono_ms() - json_object_get_int64(json_object_object_get(c, "read_ms")) > MM_SMS_FRESH_MS && !running) {
				char skip[512];

				if (idx[0]) {
					sms_skip_list(id, idx, skip, sizeof(skip));
					if ((j = sms_spawn(SMS_LIST, id, idx, skip, NULL, 20000)))
						snprintf(j->at_node, sizeof(j->at_node), "%s", node);
				} else {
					sms_at_list(id, node, NULL, &why);
				}
			}
			o = sms_view_mark(id, !peek);
			rc = reply_json(ctx, req, NULL, o);
			json_object_put(o);
			return rc;
		}
		if (idx[0] && sms_job_of(id, SMS_LIST))
			return sms_refuse(ctx, req, id, "the messages are being read; ask again in a moment");
		if (idx[0]) {
			char skip[512];

			sms_skip_list(id, idx, skip, sizeof(skip));
			j = sms_spawn(SMS_LIST, id, idx, skip, NULL, 20000);
		} else {
			return sms_at_list(id, node, &to, &why) ? UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
		}
	} else if (!strcmp(method, "sms_send")) {
		const char *number = tb[SMS_NUMBER] ? blobmsg_get_string(tb[SMS_NUMBER]) : NULL;
		const char *text = tb[SMS_TEXT] ? blobmsg_get_string(tb[SMS_TEXT]) : NULL;

		if (!mm_sms_number_ok(number))
			return sms_refuse(ctx, req, id, "the number: digits only, an optional + in front, 3 to 20 digits");
		if (!mm_sms_text_ok(text))
			return sms_refuse(ctx, req, id, "the text: 1 to 1600 bytes of UTF-8, no control characters");
		if (sms_job_of(id, SMS_SEND))
			return sms_refuse(ctx, req, id, "a message is being sent from this modem");
		if (idx[0]) {
			if (strchr(text, '\'') && strchr(text, '"') && node[0]) {
				/* both quotes: ModemManager's parser cannot carry them as they are; the AT path can */
				return sms_at_send(id, node, number, text, "the text holds both quote characters", &to, &why) ?
				       UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
			} else if (!mm_sms_create_arg(number, text, sms_center_of(id), arg, sizeof(arg))) {
				return sms_refuse(ctx, req, id, "the message does not fit ModemManager's request");
			} else {
				j = sms_spawn(SMS_SEND, id, idx, arg, NULL, MM_SMS_SEND_MS);
				if (j) {
					j->number = strdup(number);
					j->text = strdup(text);
				}
			}
		} else {
			return sms_at_send(id, node, number, text, NULL, &to, &why) ? UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
		}
	} else if (!strcmp(method, "sms_clear")) {
		json_object *job;

		if (!node[0])
			return sms_refuse(ctx, req, id, "the SMS storage is cleared over the engine's AT port, and this modem's AT port "
					  "is ModemManager's");
		job = json_object_new_object();
		json_object_object_add(job, "op", json_object_new_string("clear"));
		return sms_at_start(id, node, job, 60000, &to, true, &why) ? UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
	} else if (!strcmp(method, "sms_storage")) {
		json_object *job;

		if (!node[0])
			return sms_refuse(ctx, req, id, "the SMS storage is switched over the engine's AT port, and this modem's AT port "
					  "is ModemManager's");
		job = json_object_new_object();
		json_object_object_add(job, "op", json_object_new_string("storage"));
		return sms_at_start(id, node, job, 40000, &to, true, &why) ? UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
	} else if (!strcmp(method, "sms_center")) {
		/* the SMS centre written on the SIM: read, or written then read back, over the engine's AT port */
		const char *number = tb[SMS_NUMBER] ? blobmsg_get_string(tb[SMS_NUMBER]) : NULL;
		json_object *job;

		if (number && *number && !mm_sms_number_ok(number))
			return sms_refuse(ctx, req, id, "the number: digits only, an optional + in front, 3 to 20 digits");
		if (!node[0])
			return sms_refuse(ctx, req, id, "the SIM's SMS centre is read and written over the engine's AT port, and this "
					  "modem's AT port is ModemManager's; the centre of the SIM's label goes with every message instead");
		job = json_object_new_object();
		json_object_object_add(job, "op", json_object_new_string("center"));
		if (number && *number)
			json_object_object_add(job, "value", json_object_new_string(number));
		return sms_at_start(id, node, job, 30000, &to, true, &why) ? UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
	} else {
		json_object *msgs = json_object_object_get(c, "messages"), *m = NULL;
		int want;
		size_t i;

		if (!tb[SMS_INDEX] || (int)blobmsg_get_u32(tb[SMS_INDEX]) < 0)
			return UBUS_STATUS_INVALID_ARGUMENT;
		want = (int)blobmsg_get_u32(tb[SMS_INDEX]);
		if (want >= SMS_ARCH_ID)
			return sms_arch_delete_reply(ctx, req, id, want);
		/* deleted from the modem by the operator: its copy in the router goes with it */
		sms_arch_forget(id, want);
		if (want >= SMS_SENT_ID) {
			/* a sent one kept in RAM: off the list, nothing in the modem to delete */
			json_object *o2 = json_object_new_object();
			int rc2;

			json_object_object_add(o2, "device_id", json_object_new_string(id));
			json_object_object_add(o2, "ok", json_object_new_boolean(sms_sent_del(id, want)));
			rc2 = reply_json(ctx, req, NULL, o2);
			json_object_put(o2);
			return rc2;
		}
		for (i = 0; msgs && i < json_object_array_length(msgs) && !m; i++)
			if (json_object_get_int(json_object_object_get(json_object_array_get_idx(msgs, i), "id")) == want)
				m = json_object_array_get_idx(msgs, i);
		if ((m && jstr(m, "path") && !strcmp(jstr(m, "path"), "at")) || !idx[0]) {
			/* a message of the AT storage: all its parts */
			json_object *job = json_object_new_object(), *ind = json_object_new_array(), *parts = json_object_object_get(m, "parts");

			if (!node[0]) {
				json_object_put(job);
				json_object_put(ind);
				return sms_refuse(ctx, req, id, "the message is in the modem's storage and the AT port is not the engine's now");
			}
			for (i = 0; parts && i < json_object_array_length(parts); i++)
				json_object_array_add(ind, json_object_get(json_object_array_get_idx(parts, i)));
			if (!json_object_array_length(ind))
				json_object_array_add(ind, json_object_new_int(want));
			json_object_object_add(job, "op", json_object_new_string("delete"));
			json_object_object_add(job, "indices", ind);
			return sms_at_start(id, node, job, 15000 + 10000 * (int)json_object_array_length(ind), &to, true, &why) ?
			       UBUS_STATUS_OK : sms_refuse(ctx, req, id, why);
		} else {
			snprintf(target, sizeof(target), "%d", want);
			if (sms_job_of(id, SMS_DELETE))
				return sms_refuse(ctx, req, id, "a message is being deleted on this modem");
			j = sms_spawn(SMS_DELETE, id, idx, target, NULL, 15000);
		}
	}
	if (!j)
		return sms_refuse(ctx, req, id, "too many SMS jobs are running");
	snprintf(j->at_node, sizeof(j->at_node), "%s", node);
	j->deferred = true;
	j->ctx = ctx;
	ubus_defer_request(ctx, req, &j->req);
	return UBUS_STATUS_OK;
}

enum { USSD_ID, USSD_CODE, USSD_REPLY, __USSD_MAX };
const struct blobmsg_policy ussd_policy[__USSD_MAX] = {
	[USSD_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[USSD_CODE] = { .name = "code", .type = BLOBMSG_TYPE_STRING },
	[USSD_REPLY] = { .name = "reply", .type = BLOBMSG_TYPE_STRING },
};

/* USSD is asynchronous (U2): start answers at once, the page asks ussd_status */
int m_ussd(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		  const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__USSD_MAX];
	json_object *rec, *st, *o;
	const char *id, *why = NULL, *value = NULL;
	char idx[16], node[64];
	enum sms_kind kind;
	bool at;
	int rc;

	(void)obj;
	blobmsg_parse(ussd_policy, __USSD_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[USSD_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[USSD_ID])))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	if (!ussd_states)
		ussd_states = json_object_new_object();
	st = json_object_object_get(ussd_states, id);
	if (!strcmp(method, "ussd_status")) {
		o = st ? json_object_get(st) : json_object_new_object();
		if (!st)
			json_object_object_add(o, "state", json_object_new_string("idle"));
		json_object_object_add(o, "device_id", json_object_new_string(id));
		rc = reply_json(ctx, req, NULL, o);
		json_object_put(o);
		return rc;
	}
	if (!sms_route(id, idx, sizeof(idx), node, sizeof(node), &why)) {
		/* r117: a HiLink has neither: its own web sends the request (the owner, 2026-10-28) */
		json_object *srv = served(rec);
		bool web = web_huawei(rec, srv) || web_mark(rec, srv) || web_zte(rec, srv);	/* an MF150-4's (2026-10-30), a ZTE's (11-03) */
		const char *wwhy = NULL;

		if (!web) {
			json_object_put(srv);
			return sms_refuse(ctx, req, id, why);
		}
		if (!strcmp(method, "ussd_cancel")) {
			json_object_put(srv);
			if (st)
				json_object_object_add(st, "state", json_object_new_string("cancelled"));
			goto started;
		}
		value = tb[USSD_CODE] ? blobmsg_get_string(tb[USSD_CODE]) : tb[USSD_REPLY] ? blobmsg_get_string(tb[USSD_REPLY]) : NULL;
		if (!mm_ussd_code_ok(value) && !(!strcmp(method, "ussd_respond") && value && strspn(value, "0123456789*#+") == strlen(value) &&
						  *value && strlen(value) <= 32)) {
			json_object_put(srv);
			return sms_refuse(ctx, req, id, "the code: digits, *, # and + only, 2 to 32 characters");
		}
		if (st && jstr(st, "state") && !strcmp(jstr(st, "state"), "running")) {
			json_object_put(srv);
			return sms_refuse(ctx, req, id, "a USSD request is running on this modem");
		}
		if (!web_act(rec, srv, "ussd", value, NULL, NULL, &wwhy)) {
			json_object_put(srv);
			return sms_refuse(ctx, req, id, wwhy);
		}
		json_object_put(srv);
		st = json_object_new_object();
		json_object_object_add(st, "state", json_object_new_string("running"));
		json_object_object_add(st, "request", json_object_new_string(!strcmp(method, "ussd_start") ? "start" : "respond"));
		json_object_object_add(st, "via", json_object_new_string("web"));
		if (!strcmp(method, "ussd_start"))
			json_object_object_add(st, "code", json_object_new_string(value));
		json_object_object_add(st, "started_at", json_object_new_int64((long long)time(NULL)));
		json_object_object_add(ussd_states, id, st);
		goto started;
	}
	if (!strcmp(method, "ussd_start")) {
		const char *u = jstr(json_object_object_get(rec, "profile"), "ussd");

		if (u && !strcmp(u, "unsupported"))
			return sms_refuse(ctx, req, id, MM_USSD_UNSUPPORTED);
		value = tb[USSD_CODE] ? blobmsg_get_string(tb[USSD_CODE]) : NULL;
		if (!mm_ussd_code_ok(value))
			return sms_refuse(ctx, req, id, "the code: digits, *, # and + only, 2 to 32 characters");
		kind = USSD_START;
	} else if (!strcmp(method, "ussd_respond")) {
		value = tb[USSD_REPLY] ? blobmsg_get_string(tb[USSD_REPLY]) : NULL;
		if (!mm_ussd_reply_ok(value))
			return sms_refuse(ctx, req, id, "the reply: 1 to 64 printable characters, no quotes");
		kind = USSD_RESPOND;
	} else {
		kind = USSD_CANCEL;
	}
	/* a session the AT port carries stays there; a new one goes to ModemManager first */
	at = !idx[0] || (kind != USSD_START && st && jstr(st, "via") && !strcmp(jstr(st, "via"), "at") && node[0]);
	/* a model whose network refuses ModemManager's USSD on LTE but answers it on the AT port: straight there (the
	 * owner's T77 on MegaFon, 2026-11-02: ModemManager gave up after 41 s each time, the AT port answered) */
	if (!at && kind == USSD_START && node[0]) {
		/* the served record: the registry's own has neither ModemManager's state nor the profile's radio block */
		json_object *srv = served(rec);
		json_object *radio = json_object_object_get(json_object_object_get(srv, "profile"), "radio");
		json_object *tech = json_object_object_get(json_object_object_get(srv, "modemmanager"), "access_tech");
		size_t i, n = json_object_is_type(tech, json_type_array) ? json_object_array_length(tech) : 0;

		if (!radio)
			radio = json_object_object_get(json_object_object_get(rec, "profile"), "radio");
		for (i = 0; json_object_object_get(radio, "ussd_at_on_lte") && i < n && !at; i++) {
			const char *t = json_object_get_string(json_object_array_get_idx(tech, i));

			at = t && (!strcmp(t, "lte") || !strcmp(t, "5gnr"));
		}
		json_object_put(srv);
	}
	if (kind == USSD_CANCEL && at && sms_at_running(id) && st && jstr(st, "state") && !strcmp(jstr(st, "state"), "running")) {
		/* the request still waits on the port: stop it; its end sends AT+CUSD=2 */
		struct at_job *r = sms_at_running(id);

		json_object_object_add(st, "state", json_object_new_string("cancelled"));
		r->killed = true;
		snprintf(r->why, sizeof(r->why), "cancelled");
		if (r->proc.pid > 0)
			kill(-r->proc.pid, SIGKILL);
		goto started;
	}
	if (kind != USSD_CANCEL && (sms_job_of(id, USSD_START) || sms_job_of(id, USSD_RESPOND) ||
				    (st && jstr(st, "state") && !strcmp(jstr(st, "state"), "running"))))
		return sms_refuse(ctx, req, id, "a USSD request is running on this modem");
	if (rec_busy(id))
		return sms_refuse(ctx, req, id, "a recovery is running on this modem");
	if (at) {
		if (!sms_at_ussd(id, node, kind == USSD_START ? "start" : kind == USSD_RESPOND ? "respond" : "cancel", value, &why))
			return sms_refuse(ctx, req, id, why);
	} else {
		struct sms_job *j = sms_spawn(kind, id, idx, value, NULL, kind == USSD_CANCEL ? 15000 : MM_USSD_MS);

		if (!j)
			return sms_refuse(ctx, req, id, "too many SMS/USSD jobs are running");
		snprintf(j->at_node, sizeof(j->at_node), "%s", node);
		j->value = value ? strdup(value) : NULL;
	}
	if (kind != USSD_CANCEL) {
		st = json_object_new_object();
		json_object_object_add(st, "state", json_object_new_string("running"));
		json_object_object_add(st, "request", json_object_new_string(kind == USSD_START ? "start" : "respond"));
		json_object_object_add(st, "via", json_object_new_string(at ? "at" : "modemmanager"));
		if (kind == USSD_START)
			json_object_object_add(st, "code", json_object_new_string(value));
		json_object_object_add(st, "started_at", json_object_new_int64((long long)time(NULL)));
		json_object_object_add(ussd_states, id, st);
	}
started:
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "result", json_object_new_string("started"));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* r117: a HiLink's USSD answer from its web (mgr_web.c); the answer is never logged */
void ussd_web_done(const char *id, json_object *r)
{
	json_object *st = ussd_states ? json_object_object_get(ussd_states, id) : NULL;
	const char *answer = jstr(r, "answer");

	if (!st || !jstr(st, "state") || strcmp(jstr(st, "state"), "running"))
		return;	/* cancelled meanwhile */
	json_object_object_add(st, "state", json_object_new_string(answer ? "done" : "failed"));
	json_object_object_add(st, "reply", answer ? json_object_new_string(answer) : NULL);
	json_object_object_add(st, "error", answer ? NULL : json_object_new_string(jstr(r, "error") ? jstr(r, "error") :
							jstr(r, "auth") && strcmp(jstr(r, "auth"), "none") && strcmp(jstr(r, "auth"), "ok") ?
							"the modem's web asks for a password" : "the modem's web gave no answer"));
	json_object_object_add(st, "finished_at", json_object_new_int64((long long)time(NULL)));
	ULOG_INFO("ussd %s: through the web %s\n", id, jstr(st, "state"));
}

/* menus nobody answered in time: closed, and the page says why */
static void ussd_menu_fired(struct uloop_timeout *t)
{
	long long now = now_mono_ms(), next = 0;

	(void)t;
	if (!ussd_states)
		return;
	json_object_object_foreach(ussd_states, id, st) {
		long long until = json_object_get_int64(json_object_object_get(st, "menu_until"));
		char idx[16], node[64];
		const char *why = NULL;
		bool at;

		if (!until)
			continue;
		if (until > now) {
			next = next && next < until ? next : until;
			continue;
		}
		json_object_object_del(st, "menu_until");
		if (!sms_route(id, idx, sizeof(idx), node, sizeof(node), &why))
			continue;
		at = !idx[0] || (jstr(st, "via") && !strcmp(jstr(st, "via"), "at") && node[0]);
		if (at ? !sms_at_ussd(id, node, "cancel", NULL, &why) : !sms_spawn(USSD_CANCEL, id, idx, NULL, NULL, 15000))
			continue;
		json_object_object_add(st, "closed", json_object_new_string("no answer to the menu in time"));
		ULOG_INFO("ussd %s: menu closed, no answer in time\n", id);
	}
	if (next)
		uloop_timeout_set(&ussd_menu_timer, (int)(next - now) + 50);
}

/* a USSD request of this modem is on its way: the background reads wait (HANDOFF 138) */
bool ussd_running(const char *id)
{
	json_object *st = ussd_states && id ? json_object_object_get(ussd_states, id) : NULL;

	return sms_job_of(id, USSD_START) || sms_job_of(id, USSD_RESPOND) ||
	       (st && jstr(st, "state") && !strcmp(jstr(st, "state"), "running"));
}

void sms_mgr_start(void)
{
	ussd_menu_timer.cb = ussd_menu_fired;
	for (int k = 0; k < MM_SMS_JOBS; k++)
		sms_jobs[k].out.fd = -1;
	sms_poll_schedule(budget_override_ms ? 1000 : 20000);	/* ModemManager reads first */
}

void sms_mgr_stop(void)
{
	uloop_timeout_cancel(&sms_timer);
	uloop_timeout_cancel(&ussd_menu_timer);
	/* the copies still in RAM reach the flash at the stop */
	uloop_timeout_cancel(&sms_arch_flush_timer);
	if (sms_arch_dirty && !sms_arch_save())
		ULOG_WARN("sms: the router's SMS archive could not reach the flash at the stop (%s)\n", strerror(errno));
	json_object_put(sms_cache);
	json_object_put(sms_rx_seen);
	json_object_put(ussd_states);
	json_object_put(sms_sent);
	json_object_put(sms_cnmi_at);
	sms_cnmi_at = NULL;
	json_object_put(sms_fallback_said);
	sms_fallback_said = NULL;
	json_object_put(sms_arch);
	json_object_put(sms_arch_known);
	sms_cache = sms_rx_seen = ussd_states = sms_sent = sms_arch = sms_arch_known = NULL;
}
