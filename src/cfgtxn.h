#ifndef MEDVEMODEM_CFGTXN_H
#define MEDVEMODEM_CFGTXN_H

#include <stdbool.h>
#include <json-c/json.h>

/*
 * Configuration transactions (spec 17): one writer at a time (process lock
 * plus a lock file), a plan from the committed state, a journal on flash
 * before the first commit, the ledger of managed fields written atomically
 * after the last one. A journal found at start is rolled back.
 *
 * Files in statedir: managed.json (ledger), journal.json (only during a
 * transaction). UCI staging in a private directory under rundir.
 */
struct mm_cfgtxn {
	char confdir[256], statedir[256], savedir[256], lockfile[256];
	json_object *ledger;	/* {"schema": 1, "devices": {id: ledger}} */
	json_object *log;	/* recent transactions, newest last */
	json_object *recovered;	/* report of a journal rolled back at start, or NULL */
	unsigned long next;
};

/* Generator: {"action": "configure"|"none", "desired"?, ...} for this device. */
typedef json_object *(*mm_cfg_desire_fn)(void *arg, json_object *current, json_object *ledgers);

int mm_cfgtxn_init(struct mm_cfgtxn *t, const char *confdir, const char *statedir, const char *rundir);
void mm_cfgtxn_free(struct mm_cfgtxn *t);

/*
 * Configure (fn) or release (fn NULL) one device. dry_run plans only.
 * {"txn"?, "device_id", "status": planned | unchanged | committed | refused | failed,
 *  "reason"?, "ops", "conflicts", "skipped", "packages", "generator"?, "error"?}
 */
json_object *mm_cfgtxn_run(struct mm_cfgtxn *t, const char *device_id, mm_cfg_desire_fn fn, void *arg,
			   bool dry_run, long long now);

/* Undo the device's last committed transaction where nobody changed it since. */
json_object *mm_cfgtxn_rollback(struct mm_cfgtxn *t, const char *device_id, long long now);

/* Ledger of one device (new reference) or NULL; and recent transactions. */
json_object *mm_cfgtxn_ledger(struct mm_cfgtxn *t, const char *device_id);
json_object *mm_cfgtxn_log(struct mm_cfgtxn *t, const char *device_id);

#endif
