#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cfgplan.h"
#include "cfgtxn.h"
#include "ucistore.h"

#define LOG_KEEP 16

static const char *const PACKAGES[] = { "network", "firewall", "medvemodem", "qmodem", NULL };

static json_object *get(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_is_type(o, json_type_object) && json_object_object_get_ex(o, key, &v) ? v : NULL;
}

static const char *str(json_object *o, const char *key)
{
	json_object *v = get(o, key);
	return v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
}

static json_object *copy(json_object *o)
{
	json_object *c = NULL;
	return o && !json_object_deep_copy(o, &c, NULL) ? c : NULL;
}

static void path_in(char *out, size_t size, const char *dir, const char *name)
{
	snprintf(out, size, "%s/%s", dir, name);
}

static void mkdirs(const char *dir)
{
	char buf[256], *p;

	snprintf(buf, sizeof(buf), "%s", dir);
	for (p = buf + 1; *p; p++)
		if (*p == '/') {
			*p = '\0';
			mkdir(buf, 0755);
			*p = '/';
		}
	mkdir(buf, 0755);
}

/* write-temp, fsync, rename: a reader sees the old or the new file */
static int write_atomic(const char *path, json_object *o)
{
	char tmp[300];
	const char *text = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_NOSLASHESCAPE);
	size_t len = strlen(text);
	int fd, ok;

	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
	if (fd < 0)
		return -1;
	ok = write(fd, text, len) == (ssize_t)len && write(fd, "\n", 1) == 1 && fsync(fd) == 0;
	close(fd);
	if (!ok || rename(tmp, path)) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

static int lock_open(struct mm_cfgtxn *t)
{
	int fd = open(t->lockfile, O_RDWR | O_CREAT | O_CLOEXEC, 0600);

	if (fd >= 0 && flock(fd, LOCK_EX) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static void lock_close(int fd)
{
	if (fd >= 0) {
		flock(fd, LOCK_UN);
		close(fd);
	}
}

static json_object *devices(struct mm_cfgtxn *t)
{
	return get(t->ledger, "devices");
}

static int save_ledger(struct mm_cfgtxn *t)
{
	char path[300];

	path_in(path, sizeof(path), t->statedir, "managed.json");
	return write_atomic(path, t->ledger);
}

static void set_ledger(struct mm_cfgtxn *t, const char *id, json_object *led)
{
	if (led)
		json_object_object_add(devices(t), id, led);
	else
		json_object_object_del(devices(t), id);
}

static json_object *report(const char *id, const char *status)
{
	json_object *r = json_object_new_object();

	json_object_object_add(r, "device_id", json_object_new_string(id));
	json_object_object_add(r, "status", json_object_new_string(status));
	return r;
}

static void fail(json_object *r, const char *status, const char *error)
{
	json_object_object_add(r, "status", json_object_new_string(status));
	json_object_object_add(r, "error", json_object_new_string(error));
}

/* apply guarded undo ops on the committed state; the skipped ones are returned in r */
static int undo_apply(struct mm_cfgtxn *t, json_object *undo, json_object *r, char *err, size_t err_size)
{
	json_object *current = mm_uci_read(t->confdir, PACKAGES, err, err_size), *checked;
	int rc;

	if (!current)
		return -1;
	checked = mm_cfg_check(current, undo);
	rc = mm_uci_apply(t->confdir, t->savedir, get(checked, "apply"), err, err_size);
	json_object_object_add(r, "ops", json_object_get(get(checked, "apply")));
	json_object_object_add(r, "skipped", json_object_get(get(checked, "skipped")));
	json_object_put(checked);
	json_object_put(current);
	return rc;
}

static void recover(struct mm_cfgtxn *t)
{
	char path[300], err[256];
	json_object *j, *r;
	const char *id;

	path_in(path, sizeof(path), t->statedir, "journal.json");
	j = json_object_from_file(path);
	if (!j)
		return;
	id = str(j, "device_id");
	r = report(id ? id : "?", "rolled-back");
	json_object_object_add(r, "txn", json_object_get(get(j, "txn")));
	json_object_object_add(r, "packages", json_object_get(get(j, "packages")));
	if (!id || undo_apply(t, get(j, "undo"), r, err, sizeof(err))) {
		fail(r, "failed", id ? err : "malformed journal");
	} else {
		set_ledger(t, id, copy(get(j, "ledger_before")));
		if (save_ledger(t))
			fail(r, "failed", "cannot write the ledger");
		else
			unlink(path);
	}
	t->recovered = r;
	json_object_put(j);
}

int mm_cfgtxn_init(struct mm_cfgtxn *t, const char *confdir, const char *statedir, const char *rundir)
{
	char path[300];
	int fd;

	memset(t, 0, sizeof(*t));
	snprintf(t->confdir, sizeof(t->confdir), "%s", confdir);
	snprintf(t->statedir, sizeof(t->statedir), "%s", statedir);
	snprintf(t->savedir, sizeof(t->savedir), "%s/uci", rundir);
	snprintf(t->lockfile, sizeof(t->lockfile), "%s/config.lock", rundir);
	mkdirs(rundir);
	mkdirs(statedir);
	t->log = json_object_new_array();
	t->next = 1;
	path_in(path, sizeof(path), statedir, "managed.json");
	t->ledger = json_object_from_file(path);
	if (!t->ledger || !json_object_is_type(get(t->ledger, "devices"), json_type_object)) {
		json_object_put(t->ledger);
		t->ledger = json_object_new_object();
		json_object_object_add(t->ledger, "schema", json_object_new_int(1));
		json_object_object_add(t->ledger, "devices", json_object_new_object());
	}
	fd = lock_open(t);
	recover(t);
	lock_close(fd);
	return 0;
}

void mm_cfgtxn_free(struct mm_cfgtxn *t)
{
	json_object_put(t->ledger);
	json_object_put(t->log);
	json_object_put(t->recovered);
	t->ledger = t->log = t->recovered = NULL;
}

static void remember(struct mm_cfgtxn *t, json_object *entry)
{
	json_object_array_add(t->log, entry);
	if (json_object_array_length(t->log) > LOG_KEEP)
		json_object_array_del_idx(t->log, 0, 1);
}

/*
 * An interface the operator hands over (generator "adopt": ["network.modem"]):
 * the fields the engine sets that the section already has count as the
 * engine's, so the plan rewrites them; the others stay the user's. Marked
 * "adopted": released, the section is kept. New ledger object.
 */
static json_object *adopt_seed(json_object *before, json_object *current, json_object *desired, json_object *keys)
{
	json_object *led = before ? copy(before) : json_object_new_object(), *secs = get(led, "sections");
	size_t i;

	if (!secs) {
		secs = json_object_new_object();
		json_object_object_add(led, "sections", secs);
	}
	for (i = 0; i < json_object_array_length(keys); i++) {
		const char *key = json_object_get_string(json_object_array_get_idx(keys, i)), *dot = strchr(key, '.');
		json_object *cur, *df = get(get(get(desired, "sections"), key), "fields"), *s, *f;
		char pkg[64];

		if (!dot || get(secs, key) || (size_t)(dot - key) >= sizeof(pkg))
			continue;
		snprintf(pkg, sizeof(pkg), "%.*s", (int)(dot - key), key);
		if (!(cur = get(get(current, pkg), dot + 1)))
			continue;
		s = json_object_new_object();
		f = json_object_new_object();
		json_object_object_add(s, "type", json_object_get(get(cur, ".type")));
		if (df) {
			json_object_object_foreach(df, k, v) {
				(void)v;
				if (get(cur, k))
					json_object_object_add(f, k, copy(get(cur, k)));
			}
		}
		json_object_object_add(s, "fields", f);
		json_object_object_add(s, "adopted", json_object_new_boolean(1));
		json_object_object_add(secs, key, s);
	}
	return led;
}

json_object *mm_cfgtxn_run(struct mm_cfgtxn *t, const char *device_id, mm_cfg_desire_fn fn, void *arg,
			   bool dry_run, long long now)
{
	json_object *r = report(device_id, "planned"), *current, *gen = NULL, *desired = NULL, *plan, *checked;
	json_object *before, *orig, *seeded = NULL, *journal, *entry;
	char err[256], txn[24], path[300];
	int fd = lock_open(t);

	if (fd < 0) {
		fail(r, "failed", "cannot take the configuration lock");
		return r;
	}
	current = mm_uci_read(t->confdir, PACKAGES, err, sizeof(err));
	if (!current) {
		fail(r, "failed", err);
		lock_close(fd);
		return r;
	}
	if (fn) {
		gen = fn(arg, current, devices(t));
		json_object_object_add(r, "generator", gen);
		if (strcmp(str(gen, "action") ? str(gen, "action") : "", "configure")) {
			json_object_object_add(r, "status", json_object_new_string("refused"));
			json_object_object_add(r, "reason", json_object_get(get(gen, "reason")));
			json_object_put(current);
			lock_close(fd);
			return r;
		}
		desired = get(gen, "desired");
	}
	before = get(devices(t), device_id);
	orig = before;
	if (get(gen, "adopt"))
		before = seeded = adopt_seed(before, current, desired, get(gen, "adopt"));
	plan = mm_cfg_plan(current, before, desired);
	checked = mm_cfg_check(current, get(plan, "ops"));
	json_object_object_add(r, "ops", json_object_get(get(checked, "apply")));
	json_object_object_add(r, "conflicts", json_object_get(get(plan, "conflicts")));
	json_object_object_add(r, "skipped", json_object_get(get(checked, "skipped")));
	json_object_object_add(r, "packages", json_object_get(get(plan, "packages")));

	if (dry_run)
		goto out;
	if (!json_object_array_length(get(checked, "apply"))) {
		json_object_object_add(r, "status", json_object_new_string("unchanged"));
		/* the ledger still learns what became the user's; flash is written only on a change */
		if (!json_object_equal(before, get(plan, "ledger"))) {
			set_ledger(t, device_id, copy(get(plan, "ledger")));
			save_ledger(t);
		}
		goto out;
	}

	snprintf(txn, sizeof(txn), "C%lu", t->next++);
	json_object_object_add(r, "txn", json_object_new_string(txn));
	journal = json_object_new_object();
	json_object_object_add(journal, "txn", json_object_new_string(txn));
	json_object_object_add(journal, "device_id", json_object_new_string(device_id));
	json_object_object_add(journal, "undo", json_object_get(get(plan, "undo")));
	json_object_object_add(journal, "ledger_before", copy(orig));
	json_object_object_add(journal, "packages", json_object_get(get(plan, "packages")));
	path_in(path, sizeof(path), t->statedir, "journal.json");
	if (write_atomic(path, journal)) {
		json_object_put(journal);
		fail(r, "failed", "cannot write the transaction journal");
		goto out;
	}
	json_object_put(journal);

	if (mm_uci_apply(t->confdir, t->savedir, get(checked, "apply"), err, sizeof(err))) {
		/* some packages may be committed: put back what we changed */
		json_object *undo = report(device_id, "rolled-back");
		char err2[256];

		fail(r, "failed", err);
		if (undo_apply(t, get(plan, "undo"), undo, err2, sizeof(err2)) == 0)
			unlink(path);
		json_object_object_add(r, "rollback", undo);
		goto out;
	}
	entry = json_object_new_object();
	json_object_object_add(entry, "txn", json_object_new_string(txn));
	json_object_object_add(entry, "device_id", json_object_new_string(device_id));
	json_object_object_add(entry, "time", json_object_new_int64(now));
	json_object_object_add(entry, "kind", json_object_new_string(fn ? "configure" : "release"));
	json_object_object_add(entry, "ops", json_object_get(get(checked, "apply")));
	json_object_object_add(entry, "undo", json_object_get(get(plan, "undo")));
	json_object_object_add(entry, "packages", json_object_get(get(plan, "packages")));
	/* the ledger before this transaction, as the journal keeps it: the adopted seed is the plan's, not the past */
	json_object_object_add(entry, "ledger_before", copy(orig));
	json_object_object_add(entry, "status", json_object_new_string("committed"));

	set_ledger(t, device_id, copy(get(plan, "ledger")));
	if (save_ledger(t)) {
		/* the journal stays: the next start rolls this transaction back */
		fail(r, "failed", "configuration committed but the ledger could not be written");
		json_object_put(entry);
		goto out;
	}
	unlink(path);
	remember(t, entry);
	json_object_object_add(r, "status", json_object_new_string("committed"));
out:
	json_object_put(checked);
	json_object_put(plan);
	json_object_put(current);
	json_object_put(seeded);
	lock_close(fd);
	return r;
}

json_object *mm_cfgtxn_rollback(struct mm_cfgtxn *t, const char *device_id, long long now)
{
	json_object *r = report(device_id, "rolled-back"), *entry = NULL;
	char err[256];
	size_t i;
	int fd;

	for (i = json_object_array_length(t->log); i-- > 0; ) {
		json_object *e = json_object_array_get_idx(t->log, i);
		if (!strcmp(str(e, "device_id"), device_id) && !strcmp(str(e, "status"), "committed")) {
			entry = e;
			break;
		}
	}
	if (!entry) {
		fail(r, "refused", "no transaction of this device to roll back since the engine started");
		return r;
	}
	json_object_object_add(r, "txn", json_object_get(get(entry, "txn")));
	fd = lock_open(t);
	if (fd < 0) {
		fail(r, "failed", "cannot take the configuration lock");
		return r;
	}
	if (undo_apply(t, get(entry, "undo"), r, err, sizeof(err))) {
		fail(r, "failed", err);
	} else {
		json_object_object_add(entry, "status", json_object_new_string("rolled-back"));
		json_object_object_add(entry, "rolled_back", json_object_new_int64(now));
		set_ledger(t, device_id, copy(get(entry, "ledger_before")));
		if (save_ledger(t))
			fail(r, "failed", "rolled back but the ledger could not be written");
		json_object_object_add(r, "packages", json_object_get(get(entry, "packages")));
	}
	lock_close(fd);
	return r;
}

json_object *mm_cfgtxn_ledger(struct mm_cfgtxn *t, const char *device_id)
{
	return json_object_get(get(devices(t), device_id));
}

json_object *mm_cfgtxn_log(struct mm_cfgtxn *t, const char *device_id)
{
	json_object *out = json_object_new_array();
	size_t i;

	for (i = 0; i < json_object_array_length(t->log); i++) {
		json_object *e = json_object_array_get_idx(t->log, i), *row;
		if (device_id && strcmp(str(e, "device_id"), device_id))
			continue;
		row = json_object_new_object();
		json_object_object_foreach(e, k, v)
			if (strcmp(k, "undo") && strcmp(k, "ledger_before"))
				json_object_object_add(row, k, json_object_get(v));
		json_object_array_add(out, row);
	}
	return out;
}
