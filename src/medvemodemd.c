/*
 * medvemodemd: the MedveModem engine daemon (spec section 4.1).
 *
 * Discovery runs once at start, on request, and after kernel uevents settle:
 * a relevant uevent (re)arms a quiet-window timer of settle_ms, bounded so a
 * flapping device still gets a pass every MM_SETTLE_MAX_FACTOR windows. A lost
 * uevent (ENOBUFS) forces a pass. Idle means idle: no timer runs while nothing
 * changes and nobody asks.
 *
 * Every registry change and every kernel event that names a known USB device
 * goes into a bounded per-device history (spec 25.5); stability is computed
 * from it when asked, so it costs nothing while idle.
 *
 * ModemManager is read through `mmcli -J` in a supervised worker process with
 * a hard timeout (spec 4.1): after registry changes (with follow-ups while MM
 * probes a new modem), on rescan, and when a client asks with stale data.
 * Changes are broadcast as ubus events (medvemodem.device, medvemodem.stability).
 *
 * Configuration changes (spec 17) run as transactions on request only
 * (config_apply/release/rollback); after a commit a supervised reload hook
 * tells netifd and the firewall, and the request is answered when it ends.
 *
 * A bring-up job (spec 16, 19) configures the modem's interface, reloads,
 * asks netifd to bring it up and waits, within the protocol's budget, for an
 * address and a default route; on failure it rolls back what it committed.
 * Afterwards netifd's ifup/ifdown events keep the session state current.
 *
 * AT traffic (spec 11.5, 11.6) runs in worker processes, each under a tty
 * lease: the operator console (hazard policy of spec 14.1.4), read-only
 * metrics polls on request with a short cache, and bounded probing of unknown
 * ttys. A revoked lease kills its worker. ModemManager's ports are not used.
 *
 * Control backends are chosen per modem (MULTI-BACKEND-REFERENCE-MODEL): a
 * modem given to native netifd or to QModem is kept away from a running
 * ModemManager by a supervised `mmcli --inhibit-device`; QModem gets only the
 * modems it is handed (its auto-probe is switched off by the engine) and its
 * AT ports are QModem's, not ours.
 *
 * eSIM (docs/ESIM-INTEGRATION.md): the engine resolves the modem, its
 * generation and the APDU transport its owner leaves free, reserves the
 * operation and runs the managed lpac backend as a supervised child. This
 * stage is read-only (version, probe, chip, profiles).
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <time.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/klog.h>
#include <unistd.h>

#include <linux/netlink.h>

#include <json-c/json.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubox/ulog.h>
#include <libubus.h>

#include "at.h"
#include "dial.h"
#include "dial_at.h"
#include "atport.h"
#include "cfgplan.h"
#include "classify.h"
#include "cfgtxn.h"
#include "esim.h"
#include "discovery.h"
#include "history.h"
#include "identity.h"
#include "lease.h"
#include "simid.h"
#include "mm_paths.h"
#include "mmgr.h"
#include "netcfg.h"
#include "profile.h"
#include "recovery.h"
#include "hostfix.h"
#include "watch.h"
#include "uplink.h"
#include "apn.h"
#include "sysfs.h"
#include "session.h"
#include "subnet.h"
#include "sms.h"
#include "pdu.h"
#include "sms_at.h"
#include "presets.h"
#include "ucistore.h"
#include "failover.h"
#include "quota.h"
#include "sighist.h"
#include "chain.h"
#include "radio.h"
#include "registry.h"
#include "roles.h"
#include "uevent.h"
#include "daemon.h"
#include "version.h"

#define MM_SETTLE_DEFAULT_MS 2500
#define MM_SETTLE_MAX_FACTOR 4
#define MM_UEVENT_RCVBUF (512 * 1024)
#define MM_UEVENT_MSG 8192
#define MM_HISTORY_CAP 200	/* per modem, RAM (/tmp): a busy day of events (the owner, 2026-10-31: «ушедшие за сутки события») */
#define MM_HISTORY_PATH "/tmp/medvemodem/history.json"

static struct ubus_auto_conn conn;
static struct blob_buf b, eb;
static bool ubus_up;
struct mm_registry registry;
static time_t started;
static time_t last_pass;

static int settle_ms = MM_SETTLE_DEFAULT_MS;
static const char *uevent_test_socket;	/* tests feed uevents on a unix socket */
static struct uloop_fd uevent_fd;
static struct uloop_timeout settle_timer;
static struct timespec pending_since;
static bool pending;
static unsigned long uevents_seen, uevents_relevant, uevent_overflows;

struct mm_history history;
static const struct mm_stability_cfg stability_cfg = MM_STABILITY_DEFAULTS;
static const char *history_path = MM_HISTORY_PATH;
static char boot_id[64];
bool history_dirty;

/* ModemManager backend (read-only in M1) */
#define MM_WORKER_TIMEOUT_MS 15000
#define MM_WORKER_MAX_OUTPUT (256 * 1024)
#define MM_FRESH_MS 15000
const char *mmcli_path = "mmcli";
static const char *qmicli_path = "qmicli";
static const char *route_helper = "/usr/libexec/medvemodem/route-metric";	/* -T */

void helper_path(const char *name, char *buf, size_t size)
{
	const char *slash = strrchr(route_helper, '/');

	snprintf(buf, size, "%.*s%s", slash ? (int)(slash - route_helper + 1) : 0, route_helper, name);
}
static int mm_timeout_ms = MM_WORKER_TIMEOUT_MS;
static struct uloop_process mm_proc;
static struct uloop_fd mm_out;
static struct uloop_timeout mm_kill_timer, mm_sched_timer;
static long long mm_sched_at = -1;	/* monotonic deadline of mm_sched_timer */
static char *mm_buf;
static size_t mm_len, mm_cap;
bool mm_running;
static bool mm_rerun, mm_killed;
static int mm_followups;
static long long mm_last_done = -1;
/*
 * Load counters (status "load", the diagnostic report): how often the engine asked ModemManager and the
 * ports, how long that took and what was killed, and the USB churn. For the crash reports (bench 2026-10-15,
 * F18): what the router was doing in the minutes before a hang.
 */
#define LOAD_RING 128
static struct {
	unsigned long mm_runs, mm_cells_runs, mm_killed, at_jobs, at_killed;
	unsigned long usb_adds, usb_removes, usb_resets, power_cycles, recoveries;
	long long mm_last_ms, mm_max_ms, mm_total_ms;
	long long starts[LOAD_RING];	/* monotonic ms of the last mmcli worker starts */
} load;

/*
 * The service's own footprint for «Служба MedveModem» (the owner, 2026-10-19: PID, RSS and the CPU load at least
 * over a day). CPU is the daemon and the helpers it ran (mmcli, qmicli, the hooks): their forks are its load. Samples
 * are taken when the status is asked, at most hourly; the day's figure is from the oldest one inside 25 h.
 */
#define SELF_RING 26
static struct { long long at[SELF_RING], cpu[SELF_RING]; unsigned n; } self_cpu;

static long long cpu_used_ms(void)
{
	struct rusage me, kids;

	if (getrusage(RUSAGE_SELF, &me) || getrusage(RUSAGE_CHILDREN, &kids))
		return -1;
	return (me.ru_utime.tv_sec + me.ru_stime.tv_sec + kids.ru_utime.tv_sec + kids.ru_stime.tv_sec) * 1000LL +
	       (me.ru_utime.tv_usec + me.ru_stime.tv_usec + kids.ru_utime.tv_usec + kids.ru_stime.tv_usec) / 1000;
}

static json_object *self_status(void)
{
	json_object *o = json_object_new_object();
	long long now = now_mono_ms(), cpu = cpu_used_ms(), up = (long long)(time(NULL) - started) * 1000;
	unsigned last = (self_cpu.n + SELF_RING - 1) % SELF_RING, i, best = SELF_RING;
	char line[128];
	FILE *f;

	json_object_object_add(o, "pid", json_object_new_int((int)getpid()));
	if ((f = fopen("/proc/self/status", "r"))) {
		while (fgets(line, sizeof(line), f))
			if (!strncmp(line, "VmRSS:", 6)) {
				json_object_object_add(o, "rss_kb", json_object_new_int64(atoll(line + 6)));
				break;
			}
		fclose(f);
	}
	if (cpu < 0)
		return o;
	if (!self_cpu.n || now - self_cpu.at[last] >= 3600000) {
		self_cpu.at[self_cpu.n % SELF_RING] = now;
		self_cpu.cpu[self_cpu.n % SELF_RING] = cpu;
		self_cpu.n++;
	}
	for (i = 0; i < SELF_RING && i < self_cpu.n; i++)
		if (now - self_cpu.at[i] <= 25 * 3600000LL && now - self_cpu.at[i] >= 60000 &&
		    (best == SELF_RING || self_cpu.at[i] < self_cpu.at[best]))
			best = i;
	if (up > 0)
		json_object_object_add(o, "cpu_pct_since_start", json_object_new_double((double)((cpu * 1000 / up) / 10.0)));
	if (best < SELF_RING) {
		long long span = now - self_cpu.at[best];

		json_object_object_add(o, "cpu_pct", json_object_new_double((double)(((cpu - self_cpu.cpu[best]) * 1000 / span) / 10.0)));
		json_object_object_add(o, "cpu_window_s", json_object_new_int64(span / 1000));
	}
	json_object_object_add(o, "cpu_s", json_object_new_int64(cpu / 1000));
	return o;
}

static json_object *load_status(void)
{
	json_object *o = json_object_new_object(), *mm = json_object_new_object(), *at = json_object_new_object(),
		    *usb = json_object_new_object();
	long long now = now_mono_ms();
	unsigned long n = load.mm_runs < LOAD_RING ? load.mm_runs : LOAD_RING, i;
	int m1 = 0, m10 = 0;

	for (i = 0; i < n; i++) {
		long long age = now - load.starts[i];
		m1 += age < 60000;
		m10 += age < 600000;
	}
	json_object_object_add(mm, "runs", json_object_new_int64((long long)load.mm_runs));
	json_object_object_add(mm, "runs_1m", json_object_new_int(m1));
	json_object_object_add(mm, "runs_10m", json_object_new_int(m10));
	json_object_object_add(mm, "cells_runs", json_object_new_int64((long long)load.mm_cells_runs));
	json_object_object_add(mm, "killed", json_object_new_int64((long long)load.mm_killed));
	json_object_object_add(mm, "last_ms", json_object_new_int64(load.mm_last_ms));
	json_object_object_add(mm, "max_ms", json_object_new_int64(load.mm_max_ms));
	json_object_object_add(mm, "avg_ms", json_object_new_int64(load.mm_runs > load.mm_killed ?
		load.mm_total_ms / (long long)(load.mm_runs - load.mm_killed) : 0));
	json_object_object_add(mm, "running", json_object_new_boolean(mm_running));
	json_object_object_add(at, "jobs", json_object_new_int64((long long)load.at_jobs));
	json_object_object_add(at, "killed", json_object_new_int64((long long)load.at_killed));
	json_object_object_add(usb, "adds", json_object_new_int64((long long)load.usb_adds));
	json_object_object_add(usb, "removes", json_object_new_int64((long long)load.usb_removes));
	json_object_object_add(usb, "resets", json_object_new_int64((long long)load.usb_resets));
	json_object_object_add(usb, "power_cycles", json_object_new_int64((long long)load.power_cycles));
	json_object_object_add(o, "modemmanager", mm);
	json_object_object_add(o, "at", at);
	json_object_object_add(o, "usb", usb);
	json_object_object_add(o, "recoveries", json_object_new_int64((long long)load.recoveries));
	return o;
}
/*
 * -g <ms> (UCI core.mm_poll_s, 0 = off): the gentle poll for a router that a modem's
 * driver hangs (bench 2026-10-15, F18). A page looking, or the signal history's tick,
 * reads ModemManager at most once in this period instead of every MM_FRESH_MS; the
 * reads that follow an event (a modem added, a bring-up, a recovery) are not held.
 */
static int mm_gentle_ms;
static long long mm_last_start = -1;
/* clients waiting for a fresh ModemManager read (bounded by MM_WAIT_MS) */
#define MM_WAITERS 8
#define MM_WAIT_MS 2000
#define MM_WAIT_OLD_MS 120000	/* a view this young answers at once, read again behind the answer */
static struct mm_waiter {
	bool used, all, absent;
	char id[160];
	struct ubus_context *ctx;
	struct ubus_request_data req;
} mm_waiters[MM_WAITERS];
static struct uloop_timeout mm_wait_timer;
static void mm_waiters_answer(void);
/* modems ModemManager should hold and does not: device_id -> {generation, since, kicks, last, probing, probing_since} */
#define MM_KICK_MS 30000
#define MM_KICKS 3
/* ModemManager probing a device (a port of it open) holds the reports off for at most this long */
#define MM_PROBE_MAX_MS 300000
#define MM_PROBE_LOOK_MS 5000
static json_object *mm_absent;
/* device_id -> monotonic ms of the last report of its ports to ModemManager */
static json_object *mm_reported;
const char *proc_dir = "/proc", *mm_process = "ModemManager";
static void mm_kick_check(void);
static bool mm_probing(json_object *rec);
static bool mm_should_hold(const char *id, json_object *rec);
static bool mm_hold_due(const char *id, json_object *rec);
bool frozen(const char *id);
static json_object *frozen_map;	/* device id -> {who, at, was_down}: «Заморозить» below */
static void ifup_after_take(const char *id);
static json_object *mmr_status(void);
static bool uplinks_all_failing(void);
static void proxy_guard_turn(void);
static void tunnel_turn(const char *level);
static json_object *usb_power;		/* device_id -> {"events", "last"}: R11, from the kernel log */
static json_object *usb_txstall;	/* device_id -> {"events", "last"}: the driver's NETDEV WATCHDOG, from the kernel log */
static bool mm_wait(struct ubus_context *ctx, struct ubus_request_data *req, bool all, bool absent, const char *id);
const char *mm_backend = "unknown";
json_object *mm_devices;
static json_object *mm_unmapped, *mm_conflicts;
static int mm_modems;
static const int mm_followup_ms[] = { 45000, 15000 };	/* used from the end */
static json_object *stab_state;		/* device_id -> last broadcast stability state */
struct mm_leases leases;
static struct uloop_timeout lease_timer;
static void at_lease_ended(const char *lease_id, const char *reason);
static struct uloop_timeout stab_timer;

/* configuration transactions */
#define MM_RELOAD_TIMEOUT_MS 30000
const char *run_dir = MM_RUN_DIR;
const char *conf_dir = UCI_CONFDIR;
const char *state_dir = MM_STATE_DIR;
static const char *reload_hook = MM_RELOAD_HOOK;
static struct mm_cfgtxn cfg;
static struct uloop_process reload_proc;
static struct uloop_timeout reload_kill;
static bool reload_running, reload_killed, reload_deferred;
static struct ubus_context *reload_ctx;
static struct ubus_request_data reload_req;
static json_object *reload_reply;	/* the answer held until the reload ends */
static void job_after_reload(void);
static void job_prepare_backend(void);
static void job_adopted(int status);
static void job_aliased(int status);

/* data sessions */
#define MM_JOB_TICK_MS 2000
static const char *proto_dir = NETIFD_PROTO_DIR;
int budget_override_ms;		/* -B, for tests */
static json_object *sessions;		/* device_id -> session state */
static struct ubus_event_handler netifd_events;
static json_object *netifd_pending;	/* interfaces with an event to look at */
static struct uloop_timeout netifd_timer;
enum job_phase { JOB_RELOAD, JOB_RELEASE_MM, JOB_ADOPT, JOB_QSECTION, JOB_IFUP, JOB_WAIT, JOB_QRELEASE };
static struct {
	bool active, deferred, inhibit, qmodem, adopted, aliased;
	char qsection[64];
	char device_id[160], iface[32], proto[24], txn[24];
	long long generation, started, deadline;
	enum job_phase phase;
	struct ubus_context *ctx;
	struct ubus_request_data req;
	json_object *steps, *last;
} job;
static struct uloop_timeout job_timer;

/* AT workers */
#define MM_AT_CONSOLE_MS 5000
#define MM_AT_METRICS_MS 3000
#define MM_AT_PROBE_MS 1500
#define MM_METRICS_MAX_AGE_MS 30000
#define MM_QMODEM_CTRL_MS 15000
const char *dev_dir = DEV_DIR;
struct at_job at_jobs[MM_AT_JOBS];
static json_object *metrics_cache;	/* device_id -> {generation, at, port, metrics} */
json_object *radio_cache;	/* device_id -> {generation, at, via, radio} (spec 25.9.12) */
/* device_id -> {generation, at, cells}: the serving cells read over AT while ModemManager's cell info is off (r34) */
static json_object *cells_cache;
#define CELLS_EVERY_MS 30000		/* at most one AT^DEBUG? per modem in this period */
#define CELLS_SHOWN_MS 120000		/* older cells are not served */
json_object *uplinks;		/* device_id or "dev:<netdev>" -> {dev, since, traffic, reach, next} */
static json_object *probes;		/* device_id -> {generation, results: {node: verdict}} */
static json_object *cut_nodes;		/* node -> when a worker on it was killed mid-command */
static json_object *stuck_nodes;	/* node -> {pid, since}: a worker on it ignored SIGKILL (stuck in the kernel) */
static json_object *user_ifaces;	/* device_id -> the interface set up outside MedveModem for it (any match netcfg knows) */
static json_object *holders_known;	/* "ttyUSB7" -> {who, repeats}: the holders already told about */
static json_object *tty_holders;	/* "ttyUSB2" -> "smsd (pid 812)": other programs with a modem port open */
static bool tty_holder(const char *node, char *who, size_t size);

/* backends: QModem and ModemManager inhibitions */
#define MM_INHIBITS 8
#define MM_HOOK_TIMEOUT_MS 60000
#define MM_RELEASE_MM_MS 30000
static const char *qmodem_dir = QMODEM_DIR;
static json_object *qmodem_img;		/* the qmodem UCI package, re-read when its file changes */
static struct timespec qmodem_mtime;
static struct inhibit {
	bool used, wanted, forgotten;	/* forgotten: its ports were withdrawn from ModemManager once */
	bool parked;		/* refused after the withdrawal: no retry until ModemManager shows the modem */
	int failures;		/* quick exits in a row: ModemManager refused the inhibition */
	long long spawned;
	char device_id[160], path[256];
	struct uloop_process proc;
	struct uloop_timeout restart;
} inhibits[MM_INHIBITS];
static struct {
	bool running;
	struct uloop_process proc;
	struct uloop_timeout kill;
	void (*done)(int status);
} hook;
static struct {
	bool active;
	char device_id[160];
	struct ubus_context *ctx;
	struct ubus_request_data req;
} pending_release;

bool bringup_busy(const char *id)
{
	return (job.active && !strcmp(job.device_id, id)) || (pending_release.active && !strcmp(pending_release.device_id, id));
}
static void inhibits_sync(void);
static void frozen_list_sync(void);
static json_object *frozen_withdrawn;	/* device id -> the generation its ports were withdrawn from ModemManager in */
static bool mm_installed(void);

/* the kernel registers this USB driver (its module is loaded or built in) */
bool kernel_has_driver(const char *driver)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s", SYS_USB_DRIVERS, driver);
	return access(path, F_OK) == 0;
}
static bool mm_returning(const char *id);
static bool inhibited(const char *id);
static void release_after_qmodem(int status);
static void hostfix_sync(void);
static json_object *hostfixes;
static void watch_sync(void);
static void iface_sync(void);
static void apn_auto_sync(void);
static void auto_connect_sync(void);
static json_object *iface_checks;
/* netifd interface -> its "metric" option, from the last read of the network package (the modems' priority) */
/* native AT dials (HANDOFF 171), RAM: device_id -> the network of its last session; device_id -> true when a recovery
 * asked for a new session (the next dial does not reuse the one the modem holds) */
static json_object *dial_plmn, *dial_cold;
static json_object *dial_last;	/* device_id -> the last AT dial: {t, result, steps, pdp?, reused?, error?} (RAM) */
static json_object *iface_metrics;
/*
 * device_id -> {"model", "manufacturer"} ModemManager named last. Kept in the
 * state directory (identity.json): a model is no secret, and the device_id is
 * the modem's own serial. While ModemManager does not hold the modem (boot,
 * a reset, a failed probe) the profile is matched from it, so the host fix
 * and the profile's policy apply before ModemManager probes (bench
 * 2026-10-07: the T99 profile matched only after ModemManager, and the late
 * host fix made it probe everything again).
 */
static json_object *known_models;
static bool known_dirty;

static void known_load(void)
{
	char path[PATH_MAX];
	json_object *o;

	snprintf(path, sizeof(path), "%s/identity.json", state_dir);
	o = json_object_from_file(path);
	if (o && json_object_is_type(o, json_type_object)) {
		json_object_put(known_models);
		known_models = o;
	} else {
		json_object_put(o);
	}
}

static void known_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	if (!known_dirty || !known_models)
		return;
	snprintf(path, sizeof(path), "%s/identity.json", state_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if (!json_object_to_file_ext(tmp, known_models, JSON_C_TO_STRING_PLAIN) && !rename(tmp, path))
		known_dirty = false;
}
static json_object *sim_secret;		/* device_id -> {imsi, iccid}: only for the APN pick */

/*
 * The SIM's identity (r64) for a modem ModemManager does not hold: QModem's sim_info or the AT port (AT+CIMI for the
 * IMSI, AT+CRSM for the ICCID), once after each attach and again every SIM_EVERY_MS. QModem's answer without an ICCID
 * is followed by the AT port at once. RAM only: never logged, never written to flash (the traffic counters take a hash).
 */
#define SIM_EVERY_MS (30 * 60 * 1000LL)
#define SIM_TICK_MS 60000
#define SIM_CRSM "AT+CRSM=176,12258,0,0,10"
static json_object *sim_ident;		/* device_id -> {generation, at, source, imsi?, iccid?, direct?} */
static struct uloop_timeout sim_timer;

static json_object *sim_entry(const char *id)
{
	return id && sim_ident ? json_object_object_get(sim_ident, id) : NULL;
}

/* the ICCID of a device: ModemManager's card, else the engine's reading of this very attachment */
static const char *sim_iccid_of(const char *id, json_object *mm)
{
	json_object *card = mm ? json_object_object_get(mm, "sim_card") : NULL, *e = sim_entry(id), *rec;
	const char *c = jstr(card, "iccid");

	if (c && *c && strcmp(c, "--"))
		return c;
	rec = id ? mm_registry_find(&registry, id) : NULL;
	if (!e || !rec || json_object_get_int64(json_object_object_get(e, "generation")) !=
			   json_object_get_int64(json_object_object_get(rec, "generation")))
		return NULL;
	return jstr(e, "iccid");
}

static void caches_retain(void);
static void sim_store(const char *id, long long gen, const char *source, const char *imsi, const char *iccid, bool direct)
{
	json_object *e = json_object_new_object(), *sec;

	if (!sim_ident)
		sim_ident = json_object_new_object();
	json_object_object_add(e, "generation", json_object_new_int64(gen));
	json_object_object_add(e, "at", json_object_new_int64(now_mono_ms()));
	json_object_object_add(e, "source", json_object_new_string(source));
	if (imsi && *imsi)
		json_object_object_add(e, "imsi", json_object_new_string(imsi));
	if (iccid && *iccid)
		json_object_object_add(e, "iccid", json_object_new_string(iccid));
	if (direct)
		json_object_object_add(e, "direct", json_object_new_boolean(1));
	json_object_object_add(sim_ident, id, e);
	/* the APN pick's copy, as the modems ModemManager holds keep it (the identifiers are never logged) */
	if (!sim_secret)
		sim_secret = json_object_new_object();
	sec = json_object_new_object();
	if (imsi && *imsi)
		json_object_object_add(sec, "imsi", json_object_new_string(imsi));
	if (iccid && *iccid)
		json_object_object_add(sec, "iccid", json_object_new_string(iccid));
	json_object_object_add(sim_secret, id, sec);
}

/* an eSIM profile switch: the reading of the old profile goes, the next tick reads the new one (the owner, 2026-11-16:
 * YESIM enabled on a 9eSIM card, the card said «Билайн в сети T2» for half an hour) */
void daemon_sim_reread(const char *id)
{
	if (!id)
		return;
	if (sim_ident)
		json_object_object_del(sim_ident, id);
	if (sim_secret)
		json_object_object_del(sim_secret, id);
}

/* a read that gave nothing (killed, refused, no answer): tried again after 2, 4, 8 ... min, at most SIM_EVERY_MS; a
 * reading of this attachment is kept (audit 2026-10-20: a dead helper was asked every 18 s, a busy port every minute) */
static void sim_fail(const char *id, long long gen)
{
	json_object *e = sim_entry(id);

	if (!e || json_object_get_int64(json_object_object_get(e, "generation")) != gen) {
		sim_store(id, gen, "", NULL, NULL, false);
		e = sim_entry(id);
	}
	json_object_object_del(e, "direct");
	json_object_object_add(e, "fails", json_object_new_int(json_object_get_int(json_object_object_get(e, "fails")) + 1));
	json_object_object_add(e, "at", json_object_new_int64(now_mono_ms()));
}

/* how long a reading (or a failure) stands before the next try */
static long long sim_wait(json_object *e)
{
	int fails = json_object_get_int(json_object_object_get(e, "fails"));
	long long w = 60000LL << (fails < 5 ? fails : 5);

	return fails && w < SIM_EVERY_MS ? w : SIM_EVERY_MS;
}

/* the histories of the ids the registry no longer has go (audit M06) */
static void sim_retain(json_object *devices)
{
	json_object *gone = json_object_new_array();
	size_t i;

	if (sim_ident) {
		json_object_object_foreach(sim_ident, id, e) {
			(void)e;
			if (!json_object_object_get(devices, id))
				json_object_array_add(gone, json_object_new_string(id));
		}
	}
	for (i = 0; i < json_object_array_length(gone); i++) {
		const char *id = json_object_get_string(json_object_array_get_idx(gone, i));

		json_object_object_del(sim_ident, id);
		json_object_object_del(sim_secret, id);
	}
	json_object_put(gone);
}
static json_object *watches;
static json_object *watch_view_of(const char *id);
static json_object *uplink_view_of(const char *id, json_object *sess);
static json_object *connection_of(const char *id, json_object *srv);
static int reply_config(struct ubus_context *ctx, struct ubus_request_data *req, json_object *r);
static json_object *decision_of(const char *id);
static const char *modem_name(const char *id);
/* the operator's name of a modem, for the other managers (SMS forwarding) */
const char *daemon_modem_name(const char *id) { return modem_name(id); }
static const char *name_follows(const char *id, json_object *rec);
static json_object *sim_label(const char *iccid);
static json_object *recovery_view_of(const char *id, json_object *profile, const char *owner);
static const char *rec_owner(json_object *rec, json_object *srv);
#define MM_AT_SETTLE_MS 1000

static void note_stability(const char *id);

long long now_mono_ms(void)
{
	struct timespec t;

	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000LL + t.tv_nsec / 1000000;
}

static void read_boot_id(void)
{
	FILE *f = fopen("/proc/sys/kernel/random/boot_id", "r");

	if (f) {
		if (fgets(boot_id, sizeof(boot_id), f))
			boot_id[strcspn(boot_id, "\n")] = '\0';
		fclose(f);
	}
}

/* Atomic write into tmpfs: RAM only, so frequent snapshots cost no flash. */
static void history_save(void)
{
	json_object *snap;
	char dir[256], tmp[300];
	const char *slash = strrchr(history_path, '/');
	int fd;

	if (!history_dirty || !*boot_id)
		return;
	history_dirty = false;
	if (slash && (size_t)(slash - history_path) < sizeof(dir)) {
		memcpy(dir, history_path, (size_t)(slash - history_path));
		dir[slash - history_path] = '\0';
		mkdir(dir, 0755);
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", history_path);
	snap = mm_history_snapshot(&history, boot_id);
	fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd >= 0) {
		const char *text = json_object_to_json_string_ext(snap, JSON_C_TO_STRING_PLAIN);
		bool ok = write(fd, text, strlen(text)) == (ssize_t)strlen(text);

		close(fd);
		if (!ok || rename(tmp, history_path))
			unlink(tmp);
	}
	json_object_put(snap);
}

static bool history_load(void)
{
	json_object *snap = *boot_id ? json_object_from_file(history_path) : NULL;
	bool restored = snap && mm_history_restore(&history, snap, boot_id);

	if (restored)
		ULOG_INFO("event history restored from %s\n", history_path);
	json_object_put(snap);
	return restored;
}

/* How the previous engine ended, beside the history: "crash N" from the fatal signal handler, "stop N" from a
 * clean stop. Nothing there after a restored history means it was killed outright (SIGKILL, the OOM killer); the
 * owner's bench (2026-10-20) had an engine replaced during a USB swap and no word of why. */
static char last_exit_path[300];

static void last_exit_write(const char *what, int sig)
{
	char line[32];
	size_t n = strlen(what);
	int fd;

	if (!*last_exit_path)
		return;
	memcpy(line, what, n);
	line[n++] = ' ';
	if (sig >= 10)
		line[n++] = (char)('0' + sig / 10 % 10);
	line[n++] = (char)('0' + sig % 10);
	line[n++] = '\n';
	fd = open(last_exit_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
	if (fd >= 0) {
		if (write(fd, line, n) < 0) {}
		close(fd);
	}
}

static void fatal_signal(int sig)
{
	last_exit_write("crash", sig);	/* async-signal-safe: open, write, close only */
	signal(sig, SIG_DFL);
	raise(sig);
}

static void last_exit_report(bool restored)
{
	static const int fatal[] = { SIGSEGV, SIGBUS, SIGABRT, SIGFPE, SIGILL };
	const char *slash = strrchr(history_path, '/');
	char what[16] = "";
	int sig = 0;
	size_t i;
	FILE *f;

	if (!slash || (size_t)(slash - history_path) + sizeof("/last-exit") > sizeof(last_exit_path))
		return;
	snprintf(last_exit_path, sizeof(last_exit_path), "%.*s/last-exit", (int)(slash - history_path), history_path);
	f = fopen(last_exit_path, "r");
	if (f) {
		if (fscanf(f, "%15s %d", what, &sig) != 2)
			*what = '\0';
		fclose(f);
		unlink(last_exit_path);
	}
	if (!strcmp(what, "crash"))
		ULOG_ERR("the previous engine died by signal %d (%s); procd started this one\n", sig, strsignal(sig));
	else if (!strcmp(what, "stop"))
		ULOG_INFO("the previous engine was stopped (signal %d)\n", sig);
	else if (restored)
		ULOG_WARN("the previous engine ended without a word: killed outright (SIGKILL or the OOM killer)\n");
	for (i = 0; i < sizeof(fatal) / sizeof(fatal[0]); i++)
		signal(fatal[i], fatal_signal);
}

/*
 * ---- the operator's hooks (field request 2026-10-23; mwan3's /etc/mwan3.user, OpenWrt's hotplug.d) ----
 * Every script in /etc/hotplug.d/medvemodem/ runs (OpenWrt's hotplug-call sources each in a subshell) on a
 * modem's arrival and departure, a session up or down, a recovery step, a failover move and a switch of the
 * channel that carries the traffic, with the event in the environment: ACTION=device|session|recovery|
 * failover|switch and the event's fields in capitals (DEVICE_ID, STATE, INTERFACE, DEVICE, WHY...). One run at
 * a time in the events' order, at most 30 s each; 32 waiting at most, the newest dropped beyond.
 */
#ifndef MM_HOOK_DIR
#define MM_HOOK_DIR "/etc/hotplug.d/medvemodem"
#endif
#define HOOK_QUEUE 32
#define HOOK_TIMEOUT_MS 30000
static json_object *hook_queue;
static struct uloop_process hook_proc;
static struct uloop_timeout hook_kill;
static bool hook_running;
static void hook_next(void);

static const char *hook_dir(void)
{
	const char *d = getenv("MEDVEMODEM_HOOK_DIR");

	return d && *d ? d : MM_HOOK_DIR;
}

/* a script there: no fork for nobody */
static bool hooks_present(void)
{
	DIR *d = opendir(hook_dir());
	struct dirent *e;
	bool any = false;

	while (d && !any && (e = readdir(d)))
		any = e->d_name[0] != '.';
	if (d)
		closedir(d);
	return any;
}

static void ophook_exited(struct uloop_process *p, int ret)
{
	(void)p;
	hook_running = false;
	uloop_timeout_cancel(&hook_kill);
	if (ret)
		ULOG_WARN("hooks: the scripts of %s ended with status %d\n", hook_dir(), WIFEXITED(ret) ? WEXITSTATUS(ret) : 128 + WTERMSIG(ret));
	hook_next();
}

static void ophook_timeout(struct uloop_timeout *t)
{
	(void)t;
	if (hook_running && hook_proc.pid > 0) {
		ULOG_WARN("hooks: the scripts of %s ran over %d s, stopped\n", hook_dir(), HOOK_TIMEOUT_MS / 1000);
		kill(-hook_proc.pid, SIGKILL);
	}
}

static void hook_next(void)
{
	const char *hp = getenv("MEDVEMODEM_HOTPLUG");
	json_object *ev;
	pid_t pid;

	if (hook_running || !hook_queue || !json_object_array_length(hook_queue))
		return;
	ev = json_object_get(json_object_array_get_idx(hook_queue, 0));
	json_object_array_del_idx(hook_queue, 0, 1);
	pid = fork();
	if (pid == 0) {
		const char *argv[] = { hp && *hp ? hp : "/sbin/hotplug-call", "medvemodem", NULL };
		int fd = open("/dev/null", O_RDWR);

		setpgid(0, 0);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		/* the event and a path, nothing of the engine's own environment */
		clearenv();
		setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);
		json_object_object_foreach(ev, k, v)
			setenv(k, json_object_get_string(v), 1);
		execv(argv[0], (char *const *)argv);
		_exit(127);
	}
	json_object_put(ev);
	if (pid < 0) {
		ULOG_WARN("hooks: cannot start %s: %s\n", hp && *hp ? hp : "/sbin/hotplug-call", strerror(errno));
		return;
	}
	hook_running = true;
	hook_proc.pid = pid;
	hook_proc.cb = ophook_exited;
	uloop_process_add(&hook_proc);
	hook_kill.cb = ophook_timeout;
	uloop_timeout_set(&hook_kill, HOOK_TIMEOUT_MS);
}

/* an event for the hooks: its plain fields as NAME=value (dev is DEVICE), control characters as spaces */
static void hook_event(const char *action, json_object *o)
{
	json_object *ev;

	if (!hooks_present())
		return;
	if (!hook_queue)
		hook_queue = json_object_new_array();
	if (json_object_array_length(hook_queue) >= HOOK_QUEUE) {
		ULOG_WARN("hooks: %d events wait already, the %s event is dropped\n", HOOK_QUEUE, action);
		return;
	}
	ev = json_object_new_object();
	json_object_object_add(ev, "ACTION", json_object_new_string(action));
	if (o && json_object_is_type(o, json_type_object)) {
		json_object_object_foreach(o, k, v) {
			char name[40], val[256];
			size_t i;

			if (!v || json_object_is_type(v, json_type_object) || json_object_is_type(v, json_type_array) ||
			    !strcmp(k, "action") || strlen(k) >= sizeof(name))
				continue;
			snprintf(name, sizeof(name), "%s", !strcmp(k, "dev") ? "device" : k);
			for (i = 0; name[i]; i++)
				name[i] = isalnum((unsigned char)name[i]) ? (char)toupper((unsigned char)name[i]) : '_';
			snprintf(val, sizeof(val), "%s", json_object_is_type(v, json_type_boolean) ? (json_object_get_boolean(v) ? "1" : "0") :
							json_object_get_string(v));
			for (i = 0; val[i]; i++)
				if ((unsigned char)val[i] < 0x20 || val[i] == 0x7f)
					val[i] = ' ';
			json_object_object_add(ev, name, json_object_new_string(val));
		}
	}
	json_object_array_add(hook_queue, ev);
	hook_next();
}

/* ubus broadcast; nobody listening costs nothing. The modem's, session, recovery, failover and switch events
 * also reach the operator's hooks */
void broadcast(const char *type, json_object *o)
{
	static const char *const hooked[] = { "medvemodem.device", "medvemodem.session", "medvemodem.recovery",
					      "medvemodem.failover", "medvemodem.switch", "medvemodem.ready", NULL };

	for (int i = 0; hooked[i]; i++)
		if (!strcmp(type, hooked[i]))
			hook_event(type + strlen("medvemodem."), o);
	if (!ubus_up)
		return;
	blob_buf_init(&eb, 0);
	if (blobmsg_add_object(&eb, o))
		ubus_send_event(&conn.ctx, type, eb.head);
}

const char *mm_engine_savedir(void)
{
	return cfg.savedir;
}

static void discovery_pass(const char *why)
{
	json_object *scan;

	modem_class_sync();	/* the operator's «модем / не модем» (mgr_modemclass.c) */
	scan = mm_discover_all();
	json_object *events = mm_registry_apply(&registry, scan, (long long)time(NULL));
	long long mono = now_mono_ms();
	size_t i;

	last_pass = time(NULL);
	for (i = 0; i < json_object_array_length(events); i++) {
		json_object *e = json_object_array_get_idx(events, i), *v = NULL, *d = NULL, *g = NULL, *c = NULL;
		json_object_object_get_ex(e, "event", &v);
		json_object_object_get_ex(e, "device_id", &d);
		json_object_object_get_ex(e, "generation", &g);
		json_object_object_get_ex(e, "cause", &c);
		ULOG_INFO("%s %s generation %lld (%s)%s%s\n", json_object_get_string(v),
			  json_object_get_string(d), (long long)json_object_get_int64(g), why,
			  c ? " cause=" : "", c ? json_object_get_string(c) : "");
		/* why it was found, in the journal too (the owner, 2026-10-28: «писать причину пере-детекта модема»): the
		 * registry's cause, else what started the pass (the engine's start, a USB event, a rescan) */
		mm_history_add(&history, json_object_get_string(d), (long long)last_pass, mono,
			       json_object_get_string(v), c ? json_object_get_string(c) :
			       strcmp(json_object_get_string(v), "removed") ? why : NULL);
		history_dirty = true;
		broadcast("medvemodem.device", e);
		note_stability(json_object_get_string(d));
		/* spec 11.6: a new attachment generation ends every lease of the old one */
		if (!strcmp(json_object_get_string(v), "reattached") || !strcmp(json_object_get_string(v), "removed")) {
			json_object *gone = mm_lease_invalidate(&leases, json_object_get_string(d),
								!strcmp(json_object_get_string(v), "removed") ? -1 : json_object_get_int64(g));
			leases_revoked(gone, "generation");
			json_object_put(gone);
		}
	}
	if (json_object_array_length(events)) {
		/* ModemManager needs a while to probe a new modem: look again later */
		mm_followups = 2;
		mm_schedule(3000);
	}
	mm_history_retain(&history, registry.devices);
	signal_retain(registry.devices);	/* audit M06: the same policy for the signal and AT histories */
	downtime_retain(registry.devices);
	traffic_retain(registry.devices);
	mmlog_retain(registry.devices);
	web_retain(registry.devices);
	sim_retain(registry.devices);
	at_hist_retain(registry.devices);
	caches_retain();
	history_save();
	json_object_put(events);
	json_object_put(scan);
	inhibits_sync();	/* a modem handed to another backend came back */
	hostfix_sync();
	usbconf_sync();		/* the operator's USB configuration (a T77 in QMI) */
	usbtree_sync();		/* «Порты и устройства»: what came and went */
	watch_sync();
	iface_sync();
	auto_connect_sync();	/* a new modem a native backend drives */
}

/* the last modem whose USB device the kernel removed: a USB-level reset left the bus (R25) */
static char usb_left[160];
/* the last modem whose drivers the kernel unbound: authorized=0 detaches every interface without the device leaving the
 * bus (Mag's DW5821e, r141: the rebind cleared a NETDEV WATCHDOG hang and the reset was still called failed) */
static char usb_unbound[160];

/* A kernel event naming a USB device the registry knows goes into its history. */
static void history_from_uevent(const struct mm_uevent *ev)
{
	const char *kind = mm_uevent_kind(ev), *base, *what;
	char name[64], att[72], detail[128];
	json_object *rec, *idv = NULL;

	/* tty/wdm/netdev churn repeats what the registry records as "reattached
	 * ... ports"; it stays in the debug log and out of the bounded history */
	if (!kind || !strncmp(kind, "node-", 5) || !mm_uevent_usb_name(ev->devpath, name, sizeof(name)))
		return;
	snprintf(att, sizeof(att), "usb:%s", name);
	rec = mm_registry_by_attachment(&registry, att);
	if (!rec || !json_object_object_get_ex(rec, "device_id", &idv))
		return;
	base = strrchr(ev->devpath, '/');
	base = base ? base + 1 : ev->devpath;
	what = ev->interface ? ev->interface : base;
	if (ev->driver)
		snprintf(detail, sizeof(detail), "%s %s", what, ev->driver);
	else
		snprintf(detail, sizeof(detail), "%s", what);
	mm_history_add(&history, json_object_get_string(idv), (long long)time(NULL), now_mono_ms(), kind, detail);
	history_dirty = true;
	if (!strcmp(kind, "usb-remove"))
		snprintf(usb_left, sizeof(usb_left), "%s", json_object_get_string(idv));
	if (!strcmp(kind, "driver-unbind"))
		snprintf(usb_unbound, sizeof(usb_unbound), "%s", json_object_get_string(idv));
	note_stability(json_object_get_string(idv));
}

/* ---- stability transitions, broadcast when evidence changes them ---- */
static void note_stability(const char *id)
{
	json_object *st = mm_history_stability(&history, id, now_mono_ms(), &stability_cfg), *v = NULL, *old = NULL, *e;
	const char *now, *before;

	json_object_object_get_ex(st, "state", &v);
	now = json_object_get_string(v);
	before = json_object_object_get_ex(stab_state, id, &old) ? json_object_get_string(old) : "stable";
	if (strcmp(now, before)) {
		e = json_object_new_object();
		json_object_object_add(e, "device_id", json_object_new_string(id));
		json_object_object_add(e, "state", json_object_new_string(now));
		json_object_object_add(e, "previous", json_object_new_string(before));
		json_object_object_get_ex(st, "probable_cause", &v);
		json_object_object_add(e, "probable_cause", json_object_get(v));
		broadcast("medvemodem.stability", e);
		ULOG_INFO("%s stability %s -> %s\n", id, before, now);
		json_object_put(e);
		json_object_object_add(stab_state, id, json_object_new_string(now));
	}
	/* an unstable state decays without new events: look once when the window ends */
	if (strcmp(now, "stable"))
		uloop_timeout_set(&stab_timer, stability_cfg.window_s * 1000 + 1000);
	json_object_put(st);
}

static void stability_recheck(struct uloop_timeout *t)
{
	json_object *ids = json_object_new_array();
	size_t i;

	(void)t;
	{
		json_object_object_foreach(stab_state, id, state) {
			if (strcmp(json_object_get_string(state), "stable"))
				json_object_array_add(ids, json_object_new_string(id));
		}
	}
	for (i = 0; i < json_object_array_length(ids); i++)
		note_stability(json_object_get_string(json_object_array_get_idx(ids, i)));
	json_object_put(ids);
}

/* ---- ModemManager worker ---- */

/* sh only glues fixed mmcli calls; nothing from outside reaches the script */
static const char mm_script[] =
	"m=$1\n"
	"list=$(\"$m\" -J -L 2>/dev/null); rc=$?\n"
	"[ $rc -eq 127 ] && exit 127\n"
	"[ $rc -eq 0 ] || exit 3\n"
	"printf '%s\\n' \"$list\"\n"
	"for i in $(printf '%s' \"$list\" | tr ',[]{}' '\\n\\n\\n\\n\\n' | sed -n 's#.*/Modem/\\([0-9][0-9]*\\)\".*#\\1#p'); do\n"
	"  mj=$(\"$m\" -J -m \"$i\" 2>/dev/null) || continue\n"
	"  printf '%s\\n' \"$mj\"\n"
	/* the SIM: its home network, operator name and GID1 pick the APN suggestion (identifiers are served, never logged or kept on flash) */
	"  s=$(printf '%s' \"$mj\" | tr ',{}' '\\n\\n\\n' | sed -n 's#.*\"sim\" *: *\"/org/freedesktop/ModemManager1/SIM/\\([0-9][0-9]*\\)\".*#\\1#p' | head -n 1)\n"
	"  [ -n \"$s\" ] && \"$m\" -J -i \"$s\" 2>/dev/null && printf '\\n'\n"
	/* extended signal (RSRP, SINR) for the signal bars; MM polls it only once asked to, every 30 s */
	"  if sj=$(\"$m\" -J -m \"$i\" --signal-get 2>/dev/null); then\n"
	"    printf '%s\\n' \"$sj\"\n"
	"    case $(printf '%s' \"$sj\" | tr -d ' ') in *'\"rate\":\"0\"'*) \"$m\" -m \"$i\" --signal-setup=30 >/dev/null 2>&1;; esac\n"
	"  fi\n"
	/* radio details every 30 s at most ($2 = cells): the network part of the location (GPS is never read;
	 * 3GPP location is off by default and is switched on once it reads empty) and the serving and
	 * neighbour cells (ModemManager >= 1.20) */
	/* the background read for the carriers' history ($2 = cells-bg, every 5 min): the cell info alone, no location, no qmicli */
	"  [ \"$2\" = cells-bg ] && \"$m\" -J -m \"$i\" --get-cell-info 2>/dev/null && printf '\\n'\n"
	"  if [ \"$2\" = cells ]; then\n"
	"    lj=$(\"$m\" -J -m \"$i\" --location-get 2>/dev/null) && printf '%s\\n' \"$lj\"\n"
	"    case $(printf '%s' \"$lj\" | tr -d ' ') in *'\"cid\":\"--\"'*|*'\"cid\":\"\"'*|'') \"$m\" -m \"$i\" --location-enable-3gpp >/dev/null 2>&1;; esac\n"
	"    \"$m\" -J -m \"$i\" --get-cell-info 2>/dev/null && printf '\\n'\n"
	"  fi\n"
	/* $2 = ant: the antennas and the carriers alone (core.antennas, the owner 2026-10-16: with cell_info off they
	 * were gone too, and F18 hung «with qmicli off» — the cell info and location are the suspects, not these) */
	"  if [ \"$2\" = cells ] || [ \"$2\" = ant ]; then\n"
	/* antennas: QMI NAS tx-rx info through the proxy ModemManager itself uses (qmi-proxy, or mbim-proxy
	 * for an MBIM function), read-only, a few seconds at most; never AT beside ModemManager */
	"    w=$(printf '%s' \"$mj\" | tr ',[]' '\\n\\n\\n' | sed -nE 's#.*\"(cdc-wdm[0-9]+) \\((qmi|mbim)\\)\".*#\\1 \\2#p' | head -n 1)\n"
	"    q=$(command -v \"$3\" 2>/dev/null || PATH=/usr/sbin:/usr/bin:/sbin:/bin command -v \"$3\" 2>/dev/null); t=$(command -v timeout 2>/dev/null)\n"
	"    if [ -z \"$q\" ]; then printf '{\"txrx-why\":\"no-qmicli\"}\\n'\n"
	"    elif [ -z \"$w\" ]; then printf '{\"txrx-why\":\"no-control-port\"}\\n'\n"
	"    else\n"
	"      o=; [ \"${w#* }\" = mbim ] && o=--device-open-mbim; got=\n"
	"      for r in lte 5gnr; do\n"
	"        [ $r = 5gnr ] && case $(printf '%s' \"$mj\" | tr -d ' ') in *'5gnr'*) ;; *) continue;; esac\n"
	"        ${t:+$t 3} \"$q\" -p $o -d \"/dev/${w% *}\" --nas-get-tx-rx-info=$r 2>/dev/null | awk -F\"'\" -v ri=$r \"$4\" && got=1\n"
	"      done\n"
	"      [ -n \"$got\" ] || printf '{\"txrx-why\":\"failed\"}\\n'\n"
	/* LTE carrier aggregation: ModemManager's cell info carries the primary cell only (QMI) */
	"      case $(printf '%s' \"$mj\" | tr -d ' ') in *'\"lte\"'*) ${t:+$t 3} \"$q\" -p $o -d \"/dev/${w% *}\" --nas-get-lte-cphy-ca-info 2>/dev/null | awk -F\"'\" \"$5\";; esac\n"
	/* 5G NR levels from QMI NAS signal info, inside MBIM too: ModemManager's own read gave -32768 on the bench's
	 * MBIM modem (2026-10-26, «5gnr: RSRP: -32768»); the owner's go for QMI over MBIM, read only */
	"      case $(printf '%s' \"$mj\" | tr -d ' ') in *'5gnr'*) ${t:+$t 3} \"$q\" -p $o -d \"/dev/${w% *}\" --nas-get-signal-info 2>/dev/null | awk -F\"'\" \"$6\";; esac\n"
	"    fi\n"
	"  fi\n"
	"  for b in $(printf '%s' \"$mj\" | tr ',' '\\n' | sed -n 's#.*/Bearer/\\([0-9][0-9]*\\).*#\\1#p'); do\n"
	"    \"$m\" -J -b \"$b\" 2>/dev/null && printf '\\n'\n"
	"  done\n"
	"done\n"
	"exit 0\n";

/* `qmicli --nas-get-tx-rx-info` text ("RX Chain 0:", "Radio tuned: 'yes'", "Power: '-61.5 dBm'", ...,
 * "TX:", "In traffic: 'yes'") to one JSON line of fixed keys and numbers; nothing of the text is copied */
static const char txrx_awk[] =
	"function flush() { if (cur != \"\") { chains = chains (chains == \"\" ? \"\" : \",\") cur \"}\"; cur = \"\" } }\n"
	"function num(s) { return s ~ /^ *-?[0-9]+(\\.[0-9]+)? / || s ~ /^ *-?[0-9]+(\\.[0-9]+)?$/ }\n"
	"/RX Chain [0-9]+:/ { flush(); match($0, /RX Chain [0-9]+/); cur = \"{\\\"chain\\\":\" (substr($0, RSTART + 9, RLENGTH - 9) + 0); rx = 1; tx = 0; next }\n"
	"/(^|[] \\t])TX:/ { flush(); rx = 0; tx = 1; next }\n"
	"rx && /Radio tuned:/ { cur = cur \",\\\"tuned\\\":\" ($2 == \"yes\" ? \"true\" : \"false\") }\n"
	"rx && /Power:/ && num($2) { cur = cur \",\\\"power\\\":\" ($2 + 0) }\n"
	"rx && /RSRP:/ && num($2) { cur = cur \",\\\"rsrp\\\":\" ($2 + 0) }\n"
	"rx && /ECIO:/ && num($2) { cur = cur \",\\\"ecio\\\":\" ($2 + 0) }\n"
	"rx && /Phase:/ && num($2) { cur = cur \",\\\"phase\\\":\" ($2 + 0) }\n"
	"tx && /In traffic:/ { txt = \"\\\"traffic\\\":\" ($2 == \"yes\" ? \"true\" : \"false\") }\n"
	"tx && /Power:/ && num($2) && txt != \"\" { txt = txt \",\\\"power\\\":\" ($2 + 0) }\n"
	"END { flush(); if (chains == \"\") exit 1;\n"
	"  printf \"{\\\"txrx\\\":{\\\"ri\\\":\\\"%s\\\",\\\"chains\\\":[%s]%s}}\\n\", ri, chains, txt == \"\" ? \"\" : \",\\\"tx\\\":{\" txt \"}\" }\n";

/* `qmicli --nas-get-lte-cphy-ca-info` ("Primary Cell Info", "Secondary Cell 1 Info", "Physical Cell ID: '135'",
 * "RX Channel: '525'", "DL Bandwidth: '15'", "State: 'deactivated'", "Cell index: '1'") to one JSON line of
 * fixed keys and numbers; the band is not taken from it (the T77W968 reports it one too high) */
static const char ca_awk[] =
	"function flush() { if (cur != \"\") { cells = cells (cells == \"\" ? \"\" : \",\") cur \"}\"; cur = \"\" } }\n"
	"function num(s) { return s ~ /^ *[0-9]+(\\.[0-9]+)? *$/ }\n"
	"/^Primary Cell Info/ { flush(); cur = \"{\\\"role\\\":\\\"primary\\\"\"; next }\n"
	"/^Secondary Cell( [0-9]+)? Info/ { flush(); cur = \"{\\\"role\\\":\\\"secondary\\\"\"; next }\n"
	"/^[^ \\t]/ { flush(); next }\n"
	"cur != \"\" && /Physical Cell ID:/ && num($2) { cur = cur \",\\\"pci\\\":\" ($2 + 0) }\n"
	"cur != \"\" && /RX Channel:/ && num($2) { cur = cur \",\\\"earfcn\\\":\" ($2 + 0) }\n"
	"cur != \"\" && /DL Bandwidth:/ && num($2) { cur = cur \",\\\"bandwidth_mhz\\\":\" ($2 + 0) }\n"
	"cur != \"\" && /Cell index:/ && num($2) { cur = cur \",\\\"index\\\":\" ($2 + 0) }\n"
	"cur != \"\" && /State:/ { st = $2; gsub(/[^a-z-]/, \"\", st); if (st != \"\") cur = cur \",\\\"state\\\":\\\"\" st \"\\\"\" }\n"
	"END { flush(); if (cells == \"\") exit 1; printf \"{\\\"ca\\\":{\\\"cells\\\":[%s]}}\\n\", cells }\n";

/* `qmicli --nas-get-signal-info` ("5G:" then "RSRP: '-95 dBm'", "SNR: '6.0 dB'", "RSRQ: '-11 dB'") to one JSON line of
 * the NR levels in their physical ranges (a firmware's -32768 is no level), strings as mmcli prints them */
static const char nr_awk[] =
	"/^5G/ { in5 = 1; next }\n"
	"/^[^ \\t]/ { in5 = 0 }\n"
	"in5 && /RSRP:/ { v = $2 + 0; if (v > -160 && v < -30) o = o \",\\\"rsrp\\\":\\\"\" v \"\\\"\" }\n"
	"in5 && /RSRQ:/ { v = $2 + 0; if (v > -45 && v < 21) o = o \",\\\"rsrq\\\":\\\"\" v \"\\\"\" }\n"
	"in5 && /SNR:/ { v = $2 + 0; if (v > -30 && v < 61) o = o \",\\\"snr\\\":\\\"\" v \"\\\"\" }\n"
	"END { if (o == \"\") exit 1; printf \"{\\\"qsig\\\":{%s}}\\n\", substr(o, 2) }\n";

static void mm_drain(void)
{
	char chunk[4096];
	ssize_t n;

	while (mm_out.fd >= 0 && (n = read(mm_out.fd, chunk, sizeof(chunk))) > 0) {
		if (mm_len + (size_t)n > MM_WORKER_MAX_OUTPUT)
			continue;	/* keep reading so the worker never blocks; drop the excess */
		if (mm_len + (size_t)n > mm_cap) {
			size_t cap = mm_cap ? mm_cap * 2 : 16384;
			char *nb;

			while (cap < mm_len + (size_t)n)
				cap *= 2;
			nb = realloc(mm_buf, cap);
			if (!nb)
				continue;
			mm_buf = nb;
			mm_cap = cap;
		}
		memcpy(mm_buf + mm_len, chunk, (size_t)n);
		mm_len += (size_t)n;
	}
}

static void mm_close_out(void)
{
	if (mm_out.fd >= 0) {
		if (mm_out.registered)
			uloop_fd_delete(&mm_out);
		close(mm_out.fd);
		mm_out.fd = -1;
	}
}

static void mm_readable(struct uloop_fd *u, unsigned int events)
{
	(void)u; (void)events;
	mm_drain();
	if (mm_out.eof || mm_out.error)
		mm_close_out();
}

static void mm_withdraw_ttys(const char *id, json_object *mm);
static void identity_sync(void);

static void mm_apply(void)
{
	json_object *modems = mm_mmgr_parse(mm_buf ? mm_buf : "", mm_len);
	json_object *map = mm_mmgr_map(modems, &registry), *devs = NULL, *un = NULL, *cf = NULL, *changes;
	long long wall = (long long)time(NULL), mono = now_mono_ms();
	size_t i;

	json_object_object_get_ex(map, "devices", &devs);
	json_object_object_get_ex(map, "unmapped", &un);
	json_object_object_get_ex(map, "conflicts", &cf);
	changes = mm_mmgr_diff(mm_devices, devs);
	for (i = 0; i < json_object_array_length(changes); i++) {
		json_object *e = json_object_array_get_idx(changes, i), *id = NULL, *k = NULL, *d = NULL;

		json_object_object_get_ex(e, "device_id", &id);
		json_object_object_get_ex(e, "kind", &k);
		json_object_object_get_ex(e, "detail", &d);
		ULOG_INFO("%s %s %s\n", json_object_get_string(k), json_object_get_string(id), d ? json_object_get_string(d) : "");
		mm_history_add(&history, json_object_get_string(id), wall, mono, json_object_get_string(k),
			       d ? json_object_get_string(d) : NULL);
		history_dirty = true;
		json_object_object_add(e, "event", json_object_get(k));
		broadcast("medvemodem.device", e);
		note_stability(json_object_get_string(id));
		if (!strcmp(json_object_get_string(k), "mm-add"))
			ifup_after_take(json_object_get_string(id));
	}
	for (i = 0; cf && i < json_object_array_length(cf); i++) {
		json_object *c = json_object_array_get_idx(cf, i), *o = NULL, *r = NULL;
		json_object_object_get_ex(c, "object", &o);
		json_object_object_get_ex(c, "reason", &r);
		ULOG_WARN("ModemManager %s not mapped: %s\n", json_object_get_string(o), json_object_get_string(r));
	}
	/* the radio details are read every 30 s only: a read without them keeps the last ones of the same object */
	if (devs && mm_devices) {
		json_object_object_foreach(devs, rid, rm) {
			json_object *was = json_object_object_get(mm_devices, rid);
			static const char *const keep[] = { "cells", "location", "radio_at", "antennas", "ca" };
			size_t k;

			if (!was || !jstr(was, "object") || !jstr(rm, "object") || strcmp(jstr(was, "object"), jstr(rm, "object")))
				continue;
			for (k = 0; k < sizeof(keep) / sizeof(keep[0]); k++) {
				json_object *old = json_object_object_get(was, keep[k]);

				/* a reason there are no antennas is not kept past 2 min (bench 2026-10-20: «install qmi-utils» stayed
				 * for days with qmicli installed: no antenna read came to clear it while no page asked for one) */
				if (old && !strcmp(keep[k], "antennas") && jstr(old, "why") && !json_object_object_get(old, "lte") &&
				    !json_object_object_get(old, "nr") &&
				    (long long)wall - json_object_get_int64(json_object_object_get(old, "why_at")) > 120)
					continue;
				if (!json_object_object_get(rm, keep[k]) && old)
					json_object_object_add(rm, keep[k], json_object_get(old));
			}
		}
	}
	/* the owner's router (2026-10-21): «no-qmicli» while the same lookup by hand found /usr/bin/qmicli; said once, with
	 * what the engine ran, for the next look */
	if (devs) {
		static bool said;

		json_object_object_foreach(devs, nid, nm) {
			const char *why = jstr(json_object_object_get(nm, "antennas"), "why");

			(void)nid;
			if (!said && why && !strcmp(why, "no-qmicli")) {
				said = true;
				ULOG_WARN("antennas: the ModemManager worker found no %s (executable: %s, PATH %s)\n", qmicli_path,
					  access(qmicli_path, X_OK) ? strerror(errno) : "yes", getenv("PATH") ? getenv("PATH") : "unset");
			}
		}
	}
	if (devs) {
		json_object_object_foreach(devs, cid, cm) {
			if (json_object_object_get(cm, "cells") && !json_object_object_get(cm, "radio_at"))
				json_object_object_add(cm, "radio_at", json_object_new_int64(wall));
			signal_sample(cid, cm, wall);	/* the radio quality history (spec 25.9.13): every read is a sample */
		}
	}
	/* the SIM's identifiers are served to the operator (decision 2026-10-09) but never logged or kept on
	 * flash; diagnostic bundles redact them (diag.c) */
	if (devs) {
		json_object_object_foreach(devs, sid, sm) {
			json_object *card = json_object_object_get(sm, "sim_card"), *sec;

			mm_withdraw_ttys(sid, sm);

			/* the model stays known while the modem is away (a reset, USB re-enumeration) */
			if (jstr(sm, "model")) {
				json_object *was, *k;

				if (!known_models)
					known_models = json_object_new_object();
				was = json_object_object_get(known_models, sid);
				if (!was || !jstr(was, "model") || strcmp(jstr(was, "model"), jstr(sm, "model")) ||
				    (jstr(sm, "manufacturer") && (!jstr(was, "manufacturer") ||
								  strcmp(jstr(was, "manufacturer"), jstr(sm, "manufacturer"))))) {
					k = json_object_new_object();
					json_object_object_add(k, "model", json_object_new_string(jstr(sm, "model")));
					if (jstr(sm, "manufacturer"))
						json_object_object_add(k, "manufacturer", json_object_new_string(jstr(sm, "manufacturer")));
					json_object_object_add(known_models, sid, k);
					known_dirty = true;
					known_save();
				}
			}
			if (!card)
				continue;
			sec = json_object_new_object();
			if (!sim_secret)
				sim_secret = json_object_new_object();
			json_object_object_add(sec, "imsi", json_object_get(json_object_object_get(card, "imsi")));
			json_object_object_add(sec, "iccid", json_object_get(json_object_object_get(card, "iccid")));
			json_object_object_add(sim_secret, sid, sec);
		}
	}
	json_object_put(mm_devices);
	json_object_put(mm_unmapped);
	json_object_put(mm_conflicts);
	mm_devices = json_object_get(devs);
	mm_unmapped = json_object_get(un);
	mm_conflicts = json_object_get(cf);
	/* a modem ModemManager holds on a USB device the classifier dropped (a tester's MF192, 2026-11-09): its path makes
	 * the device a modem, and the scan runs again once when that set changes */
	{
		static json_object *held;
		json_object *now = json_object_new_object();
		size_t h;

		bool loose = false;

		/* every modem ModemManager holds, the mapped ones too: a device taken once stays taken */
		for (h = 0; un && h < json_object_array_length(un); h++) {
			const char *dev = jstr(json_object_array_get_idx(un, h), "usb_name");

			if (dev) {
				json_object_object_add(now, dev, json_object_new_boolean(1));
				loose = true;
			}
		}
		if (devs) {
			json_object_object_foreach(devs, hid, hm) {
				const char *dev = jstr(hm, "usb_name");

				(void)hid;
				if (dev)
					json_object_object_add(now, dev, json_object_new_boolean(1));
			}
		}
		if (strcmp(json_object_to_json_string(now), held ? json_object_to_json_string(held) : "{}")) {
			json_object_put(held);
			held = now;
			mm_discovery_held(held);
			if (loose)
				discovery_pass("modemmanager");
		} else
			json_object_put(now);
	}
	mm_modems = (int)json_object_array_length(modems);
	json_object_put(changes);
	json_object_put(map);
	json_object_put(modems);
	identity_sync();	/* before the settings are read: they may have to follow their modem first */
	history_save();
	inhibits_sync();	/* ModemManager may have just come up */
	mm_kick_check();
	hostfix_sync();
	watch_sync();		/* a profile from ModemManager's identity may ask for a binding */
	iface_sync();		/* ModemManager's path of the modem may differ from the configured one */
	apn_auto_sync();	/* a new SIM may want another APN */
	auto_connect_sync();	/* a new modem ModemManager now holds */
}

static void mm_exited(struct uloop_process *p, int ret)
{
	(void)p;
	uloop_timeout_cancel(&mm_kill_timer);
	mm_drain();
	mm_close_out();
	mm_running = false;
	mm_last_done = now_mono_ms();
	load.mm_last_ms = mm_last_done - mm_last_start;
	if (load.mm_last_ms > load.mm_max_ms)
		load.mm_max_ms = load.mm_last_ms;
	if (mm_killed) {
		load.mm_killed++;
		mm_backend = "timeout";
		ULOG_WARN("mmcli worker killed after %d ms\n", mm_timeout_ms);
	} else if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
		mm_backend = "available";
		mm_apply();
	} else if (WIFEXITED(ret) && (WEXITSTATUS(ret) == 127 || WEXITSTATUS(ret) == 3)) {
		/* not installed, or ModemManager not running: no owner objects */
		if (strcmp(mm_backend, "unavailable"))
			ULOG_INFO("ModemManager not available (%s)\n", WEXITSTATUS(ret) == 127 ? "no mmcli" : "not running");
		mm_backend = "unavailable";
		mm_len = 0;
		mm_apply();
	} else {
		mm_backend = "error";
	}
	if (!mm_killed)
		load.mm_total_ms += load.mm_last_ms;
	mm_len = 0;
	if (mm_rerun) {
		mm_rerun = false;
		mm_schedule(0);
	} else if (mm_followups > 0) {
		mm_schedule(mm_followup_ms[--mm_followups]);
	}
	mm_waiters_answer();
}

static void mm_kill(struct uloop_timeout *t)
{
	(void)t;
	if (mm_running && mm_proc.pid > 0) {
		mm_killed = true;
		kill(-mm_proc.pid, SIGKILL);	/* the worker's whole process group */
	}
}

#define MM_CELLS_MS 30000
#define MM_CELLS_BG_MS 300000		/* with nobody looking: the cells every 5 min, for the carriers' history */
static long long mm_cells_at = -1;
/*
 * The cells and the 3GPP location cost ModemManager a request to the modem:
 * read only while a client looks (a page, top, the CLI), within this of its
 * last request; the background reads for the signal history are plain
 * property reads (field 2026-10-12: «get location» and «retrieve cell info»
 * every 40 s with nobody looking).
 */
#define MM_CLIENT_MS 60000
static long long mm_client_at = -1;
/*
 * -c 0 (UCI core.cell_info 0, the default since r33): no `--location-get`, `--location-enable-3gpp` or
 * `--get-cell-info` at all. Bench 2026-10-15 (Orange Pi Zero 3, H616, T99W175 under ModemManager): the router
 * stalled a CPU and the watchdog reset it 30-90 s after these started with a page open, with qmicli off too,
 * and never in an hour without them. The signal (`--signal-get`) is still read.
 */
static int cell_info_on = 0;
/* -a 1 (UCI core.antennas, default on): with the cell info off, the antennas and the carriers are still read
 * over qmicli through ModemManager's proxy while a client looks, every 30 s (the owner, 2026-10-16) */
static int antennas_on = 1;
static long long mm_ant_at = -1;
int uplink_override_ms;		/* -N: every period, and the client window, shorter in tests */

static void mm_start(void)
{
	int fds[2], nul;
	pid_t pid;
	bool cells, cells_bg = false, ant = false;

	if (mm_running) {
		mm_rerun = true;
		return;
	}
	if (!strcmp(mmcli_path, "none")) {
		mm_backend = "disabled";
		return;
	}
	if (pipe2(fds, O_CLOEXEC))
		return;
	/* the radio details (cells, 3GPP location) cost ModemManager a request to the modem: every 30 s at most */
	/* the first read after the start brings them once, then only while a client looks */
	cells = cell_info_on && (mm_cells_at < 0 || (mm_client_at >= 0 && now_mono_ms() - mm_client_at < (uplink_override_ms ? 5 * uplink_override_ms : MM_CLIENT_MS) &&
				    now_mono_ms() - mm_cells_at >= MM_CELLS_MS));
	/* the carriers' history (spec 25.9.13) fills without a page too: the cell info alone (no location: field
	 * 2026-10-12), once in 5 min, while the history is on */
	if (cell_info_on && !cells && signal_history_on() && now_mono_ms() - mm_cells_at >= (uplink_override_ms ? 5 * uplink_override_ms : MM_CELLS_BG_MS))
		cells_bg = true;
	if (cells || cells_bg)
		mm_cells_at = now_mono_ms();
	if (!cell_info_on && antennas_on && mm_client_at >= 0 &&
	    now_mono_ms() - mm_client_at < (uplink_override_ms ? 5 * uplink_override_ms : MM_CLIENT_MS) &&
	    (mm_ant_at < 0 || now_mono_ms() - mm_ant_at >= MM_CELLS_MS)) {
		ant = true;
		mm_ant_at = now_mono_ms();
	}
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return;
	}
	if (pid == 0) {
		setpgid(0, 0);
		/* the worker and its mmcli/qmicli keep none of the daemon's descriptors (the ubus socket, an fd without CLOEXEC) */
		for (int fd = 3; fd < 1024; fd++)
			if (fd != fds[1])
				close(fd);
		nul = open("/dev/null", O_RDWR);
		dup2(nul, 0);
		dup2(fds[1], 1);
		dup2(nul, 2);
		execl("/bin/sh", "sh", "-c", mm_script, "mmcli-worker", mmcli_path, cells ? "cells" : cells_bg ? "cells-bg" : ant ? "ant" : "", qmicli_path, txrx_awk, ca_awk, nr_awk, (char *)NULL);
		_exit(126);
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	mm_len = 0;
	mm_killed = false;
	mm_out.fd = fds[0];
	mm_out.cb = mm_readable;
	uloop_fd_add(&mm_out, ULOOP_READ);
	mm_proc.pid = pid;
	mm_proc.cb = mm_exited;
	uloop_process_add(&mm_proc);
	mm_last_start = now_mono_ms();
	load.starts[load.mm_runs % LOAD_RING] = mm_last_start;
	load.mm_runs++;
	load.mm_cells_runs += cells || cells_bg;
	mm_kill_timer.cb = mm_kill;
	uloop_timeout_set(&mm_kill_timer, mm_timeout_ms + (cells || ant ? mm_timeout_ms / 2 : 0));	/* the radio pass reads more (qmicli 3 s each) */
	mm_running = true;
}

static void mm_sched_fired(struct uloop_timeout *t)
{
	(void)t;
	mm_sched_at = -1;
	mm_start();
}

/* Keep the earliest request; at most one worker runs at a time. */
void mm_schedule(int delay_ms)
{
	long long at = now_mono_ms() + delay_ms;

	if (mm_sched_at >= 0 && mm_sched_at <= at)
		return;
	mm_sched_at = at;
	uloop_timeout_set(&mm_sched_timer, delay_ms);
}

void mm_reread(void)
{
	mm_followups = 2;
	mm_schedule(budget_override_ms ? 200 : 2000);
}

/* Is the owner view too old for a client that is looking? (an MM that was down is retried sooner on request) */
static bool mm_stale(void)
{
	long long age = mm_last_done < 0 ? -1 : now_mono_ms() - mm_last_done;

	if (!strcmp(mm_backend, "disabled") || !strcmp(mmcli_path, "none"))
		return false;
	return age < 0 || age > (!strcmp(mm_backend, "available") ? (mm_gentle_ms > MM_FRESH_MS ? mm_gentle_ms : MM_FRESH_MS) : 10000);
}

/* a look-driven read: now, or when the gentle period since the last start is over */
void mm_schedule_look(void)
{
	long long wait = mm_gentle_ms && mm_last_start >= 0 ? mm_last_start + mm_gentle_ms - now_mono_ms() : 0;

	mm_schedule(wait > 0 ? (int)wait : 0);
}

/* A client is looking: refresh stale owner data. */
void mm_maybe_refresh(void)
{
	mm_client_at = now_mono_ms();
	if (!mm_running && mm_stale())
		mm_schedule_look();
}


static long long ms_since(const struct timespec *t)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (now.tv_sec - t->tv_sec) * 1000LL + (now.tv_nsec - t->tv_nsec) / 1000000;
}

static void settle_fired(struct uloop_timeout *t)
{
	(void)t;
	pending = false;
	discovery_pass("uevent");
}

/* Quiet window after the last hint, but never later than the bound. */
void schedule_pass(bool now)
{
	long long left;

	if (!pending) {
		pending = true;
		clock_gettime(CLOCK_MONOTONIC, &pending_since);
	}
	left = (long long)settle_ms * MM_SETTLE_MAX_FACTOR - ms_since(&pending_since);
	if (now || left <= 0)
		left = 0;
	else if (left > settle_ms)
		left = settle_ms;
	uloop_timeout_set(&settle_timer, (int)left);
}

static void uevent_readable(struct uloop_fd *u, unsigned int events)
{
	char buf[MM_UEVENT_MSG];
	struct mm_uevent ev;
	ssize_t len;

	(void)events;
	for (;;) {
		len = recv(u->fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
		if (len < 0) {
			if (errno == EINTR)
				continue;
			if (errno == ENOBUFS) {
				/* Hints were lost; sysfs still knows. */
				uevent_overflows++;
				ULOG_WARN("uevent buffer overflow, full rescan\n");
				schedule_pass(true);
				continue;
			}
			return;
		}
		buf[len] = '\0';
		uevents_seen++;
		if (!mm_uevent_parse(buf, (size_t)len + 1, &ev) || !mm_uevent_relevant(&ev))
			continue;
		uevents_relevant++;
		if (ev.action && ev.subsystem && !strcmp(ev.subsystem, "usb") && ev.devtype && !strcmp(ev.devtype, "usb_device")) {
			load.usb_adds += !strcmp(ev.action, "add");
			load.usb_removes += !strcmp(ev.action, "remove");
		}
		history_from_uevent(&ev);
		ulog(LOG_DEBUG, "uevent %s %s %s\n", ev.action, ev.subsystem, ev.devpath);
		schedule_pass(false);
	}
}

static int uevent_open(void)
{
	int fd, size = MM_UEVENT_RCVBUF;

	if (uevent_test_socket) {
		struct sockaddr_un sun = { .sun_family = AF_UNIX };

		fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
		if (fd < 0)
			return -1;
		strncpy(sun.sun_path, uevent_test_socket, sizeof(sun.sun_path) - 1);
		unlink(sun.sun_path);
		if (bind(fd, (struct sockaddr *)&sun, sizeof(sun))) {
			close(fd);
			return -1;
		}
	} else {
		struct sockaddr_nl snl = { .nl_family = AF_NETLINK, .nl_groups = 1 };

		fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK,
			    NETLINK_KOBJECT_UEVENT);
		if (fd < 0)
			return -1;
		/* FORCE needs CAP_NET_ADMIN; the plain size is the fallback. */
		if (setsockopt(fd, SOL_SOCKET, SO_RCVBUFFORCE, &size, sizeof(size)))
			setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
		if (bind(fd, (struct sockaddr *)&snl, sizeof(snl))) {
			close(fd);
			return -1;
		}
	}
	uevent_fd.fd = fd;
	uevent_fd.cb = uevent_readable;
	uloop_fd_add(&uevent_fd, ULOOP_READ);
	return 0;
}

int reply_json(struct ubus_context *ctx, struct ubus_request_data *req,
		      const char *key, json_object *value)
{
	blob_buf_init(&b, 0);
	if (key)
		blobmsg_add_json_element(&b, key, value);
	else
		blobmsg_add_object(&b, value);
	return ubus_send_reply(ctx, req, b.head);
}

static const char *objstr(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_string) ?
		json_object_get_string(v) : NULL;
}

static json_object *profile_from_mm_read(json_object *rec, json_object *mm);

static json_object *deep_copy_or_null(json_object *src)
{
	json_object *dst = NULL;

	return json_object_deep_copy(src, &dst, NULL) ? NULL : dst;
}

/*
 * Bench 2026-10-19 (r56, strace): medvemodemd at 57 % of a core, 19 000 openat in 10 s — every served() of a modem
 * ModemManager holds read and parsed all the profile files again, and served() runs many times a second (timers,
 * pages, checks). The decision depends only on the identity read here: kept per identity until the profile
 * cache reads the files again (a package upgrade, a file edited in place). NULL answers are kept too.
 */
#define PROFILE_MEMO_MAX 64
static json_object *profile_memo;		/* identity key -> {"d": decision or null} */
static unsigned long profile_memo_gen;		/* audit M04: the profile cache's generation the memo was made at */

static json_object *profile_from_mm(json_object *rec, json_object *mm)
{
	json_object *ifaces = json_object_object_get(rec, "interfaces"), *hit, *d;
	const char *parts[] = { objstr(rec, "vidpid"), objstr(rec, "manufacturer"), objstr(rec, "product"),
				objstr(mm, "manufacturer"), objstr(mm, "model"),
				ifaces ? json_object_to_json_string_ext(ifaces, JSON_C_TO_STRING_PLAIN) : NULL };
	char key[2048];
	size_t n = 0, i;
	unsigned long gen;

	for (i = 0; i < sizeof(parts) / sizeof(parts[0]) && n < sizeof(key); i++)
		n += snprintf(key + n, sizeof(key) - n, "%s\x1f", parts[i] ? parts[i] : "");
	if (n >= sizeof(key))
		return profile_from_mm_read(rec, mm);	/* an identity too long to key: read, as before */
	/* one generation with the profile cache (audit M04): a file edited in place leaves the directory's time as it
	 * was, the cache sees it at its once-a-minute look and so does the memo */
	if ((gen = mm_profile_generation(PROFILE_GENERIC_DIR)) != profile_memo_gen) {
		json_object_put(profile_memo);
		profile_memo = NULL;
		profile_memo_gen = gen;
	}
	if (!profile_memo || json_object_object_length(profile_memo) >= PROFILE_MEMO_MAX) {
		json_object_put(profile_memo);
		profile_memo = json_object_new_object();
	}
	if ((hit = json_object_object_get(profile_memo, key))) {
		d = json_object_object_get(hit, "d");
		return d ? deep_copy_or_null(d) : NULL;
	}
	d = profile_from_mm_read(rec, mm);
	hit = json_object_new_object();
	json_object_object_add(hit, "d", d ? deep_copy_or_null(d) : NULL);
	json_object_object_add(profile_memo, key, hit);
	return d;
}

static json_object *profile_from_mm_read(json_object *rec, json_object *mm)
{
	const char *vidpid = objstr(rec, "vidpid"), *usb_mfr = objstr(rec, "manufacturer");
	const char *usb_prod = objstr(rec, "product"), *mm_mfr = objstr(mm, "manufacturer");
	const char *mm_model = objstr(mm, "model");
	json_object *ifaces = json_object_object_get(rec, "interfaces");
	struct mm_profile_match match;
	char vid[5] = "", pid[5] = "";

	if (!vidpid || strlen(vidpid) != 9 || vidpid[4] != ':')
		return NULL;
	memcpy(vid, vidpid, 4);
	memcpy(pid, vidpid + 5, 4);
	if (mm_profile_resolve_usb_enriched(PROFILE_GENERIC_DIR, vid, pid, usb_mfr, usb_prod,
					   mm_mfr, mm_model, ifaces, &match) < 0)
		return NULL;
	{
		json_object *decision = json_object_new_object(), *dial = NULL;
		json_object_object_add(decision, "id", match.ambiguous ? NULL : json_object_new_string(match.id));
		json_object_object_add(decision, "status", json_object_new_string(match.ambiguous ? "ambiguous" : "matched"));
		json_object_object_add(decision, "candidates", json_object_get(match.candidates));
		json_object_object_add(decision, "score", json_object_new_int(match.score));
		json_object_object_add(decision, "fit", json_object_new_string(mm_profile_fit(&match)));
		json_object_object_add(decision, "reasons", match.ambiguous ? NULL : json_object_get(match.reasons));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "dial", &dial))
			json_object_object_add(decision, "dial", json_object_get(dial));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "recovery", &dial))
			json_object_object_add(decision, "recovery", json_object_get(dial));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "bind", &dial))
			json_object_object_add(decision, "bind", json_object_get(dial));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "policy", &dial) &&
		    jstr(dial, "mm_ports"))
			json_object_object_add(decision, "mm_ports", json_object_new_string(jstr(dial, "mm_ports")));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "radio", &dial))
			json_object_object_add(decision, "radio", json_object_get(dial));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "policy", &dial) && jstr(dial, "fcc_unlock"))
			json_object_object_add(decision, "fcc_unlock", json_object_new_string(jstr(dial, "fcc_unlock")));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "hints", &dial) &&
		    json_object_is_type(json_object_object_get(dial, "usb_configurations"), json_type_object))
			json_object_object_add(decision, "usb_configurations", json_object_get(json_object_object_get(dial, "usb_configurations")));
		/* a name for people when the USB strings say nothing, and a router mode that is the device's only one (UFI, 2026-10-19) */
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "hints", &dial)) {
			if (jstr(dial, "title"))
				json_object_object_add(decision, "title", json_object_new_string(jstr(dial, "title")));
			if (jstr(dial, "router_mode"))
				json_object_object_add(decision, "router_mode", json_object_new_string(jstr(dial, "router_mode")));
			/* a firmware that never answers USSD (FM350): refused at once, hidden on the page */
			if (jstr(dial, "ussd"))
				json_object_object_add(decision, "ussd", json_object_new_string(jstr(dial, "ussd")));
			/* the session protocols: «ppp» marks a serial-only modem (hostfix guesses no QMI interface on it) */
			if (json_object_is_type(json_object_object_get(dial, "protocols"), json_type_array))
				json_object_object_add(decision, "protocols", json_object_get(json_object_object_get(dial, "protocols")));
		}
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "hints", &dial) &&
		    json_object_is_type(json_object_object_get(dial, "at_interface_numbers"), json_type_array))
			json_object_object_add(decision, "at_interfaces", json_object_get(json_object_object_get(dial, "at_interface_numbers")));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "policy", &dial) &&
		    json_object_is_type(json_object_object_get(dial, "owner_priority"), json_type_array) &&
		    json_object_array_length(json_object_object_get(dial, "owner_priority")))
			json_object_object_add(decision, "prefer_owner", json_object_get(
				json_object_array_get_idx(json_object_object_get(dial, "owner_priority"), 0)));
		if (!match.ambiguous && json_object_object_get_ex(match.profile, "policy", &dial) &&
		    json_object_is_type(json_object_object_get(dial, "owner_priority"), json_type_array) &&
		    json_object_array_length(json_object_object_get(dial, "owner_priority")) > 1 &&
		    !strcmp(json_object_get_string(json_object_array_get_idx(json_object_object_get(dial, "owner_priority"), 1)),
			    "native"))
			json_object_object_add(decision, "fallback_owner", json_object_new_string("native"));
		json_object_object_add(decision, "identity_source", json_object_new_string("usb+modemmanager"));
		mm_profile_match_clear(&match);
		return decision;
	}
}

static json_object *session_from_mm(json_object *mm)
{
	const char *state = objstr(mm, "state"), *primary = objstr(mm, "primary_port");
	json_object *types = json_object_object_get(mm, "port_types"), *bearer = json_object_object_get(mm, "bearer");
	json_object *s, *net = NULL;
	const char *iface = bearer ? objstr(bearer, "interface") : NULL, *proto = NULL, *normalized;

	if (!state)
		return NULL;
	if (types && json_object_is_type(types, json_type_object)) {
		json_object_object_foreach(types, port, type) {
			const char *t = json_object_get_string(type);
			if (!iface && t && !strcmp(t, "net"))
				iface = port;
		}
		if (primary) {
			json_object *t = json_object_object_get(types, primary);
			const char *p = t ? json_object_get_string(t) : NULL;
			if (p && (!strcmp(p, "qmi") || !strcmp(p, "mbim")))
				proto = p;
		}
	}
	if (!strcmp(state, "connected"))
		normalized = "connected";
	else if (!strcmp(state, "connecting") || !strcmp(state, "registering") || !strcmp(state, "searching"))
		normalized = "connecting";
	else if (!strcmp(state, "failed"))
		normalized = "failed";
	else
		normalized = "idle";

	s = json_object_new_object();
	json_object_object_add(s, "state", json_object_new_string(normalized));
	if (iface)
		json_object_object_add(s, "interface", json_object_new_string(iface));
	if (proto)
		json_object_object_add(s, "proto", json_object_new_string(proto));
	json_object_object_add(s, "source", json_object_new_string("modemmanager"));
	if (bearer) {
		json_object *v4 = json_object_object_get(bearer, "ipv4");
		json_object *v6 = json_object_object_get(bearer, "ipv6");
		json_object *stats = json_object_object_get(bearer, "stats");
		json_object *dns = json_object_new_array();
		net = json_object_new_object();
		if (objstr(v4, "address"))
			json_object_object_add(net, "ipv4", json_object_new_string(objstr(v4, "address")));
		if (objstr(v6, "address"))
			json_object_object_add(net, "ipv6", json_object_new_string(objstr(v6, "address")));
		if (objstr(v4, "gateway"))
			json_object_object_add(net, "gateway4", json_object_new_string(objstr(v4, "gateway")));
		if (objstr(v6, "gateway"))
			json_object_object_add(net, "gateway6", json_object_new_string(objstr(v6, "gateway")));
		for (int family = 0; family < 2; family++) {
			json_object *ip = family ? v6 : v4, *servers = ip ? json_object_object_get(ip, "dns") : NULL;
			for (size_t i = 0; servers && json_object_is_type(servers, json_type_array) && i < json_object_array_length(servers); i++) {
				const char *server = json_object_get_string(json_object_array_get_idx(servers, i));
				bool seen = false;
				for (size_t j = 0; j < json_object_array_length(dns); j++)
					seen = seen || !strcmp(server, json_object_get_string(json_object_array_get_idx(dns, j)));
				if (server && *server && !seen)
					json_object_array_add(dns, json_object_new_string(server));
			}
		}
		json_object_object_add(net, "dns", dns);
		if (stats) {
			json_object *duration = json_object_object_get(stats, "duration");
			json_object *rx = json_object_object_get(stats, "rx_bytes");
			json_object *tx = json_object_object_get(stats, "tx_bytes");
			if (duration)
				json_object_object_add(net, "uptime", json_object_get(duration));
			if (rx)
				json_object_object_add(net, "rx_bytes", json_object_get(rx));
			if (tx)
				json_object_object_add(net, "tx_bytes", json_object_get(tx));
		}
		json_object_object_add(net, "bearer", json_object_get(json_object_object_get(bearer, "object")));
		json_object_object_add(s, "netifd", net);
	}
	return s;
}

/* A record as served: registry fields plus stability, as a new object. */
/*
 * The hub port a USB modem hangs on, as the kernel's port device
 * ("1-1.4" -> SYS_USB/1-1/1-1:1.0/1-1-port4, "1-1" -> SYS_USB/usb1/1-0:1.0/usb1-port1).
 * Its "disable" file switches the port off and on: on hubs with per-port
 * power switching that cuts VBUS (recovery study R9). False when there is none.
 */
static bool usb_port_dir(const char *usb, char *out, size_t size)
{
	const char *dot = strrchr(usb, '.'), *dash = strchr(usb, '-');
	char file[PATH_MAX + 16];

	if (dot)
		snprintf(out, size, "%s/%.*s/%.*s:1.0/%.*s-port%s", SYS_USB, (int)(dot - usb), usb, (int)(dot - usb), usb,
			 (int)(dot - usb), usb, dot + 1);
	else if (dash)
		snprintf(out, size, "%s/usb%.*s/%.*s-0:1.0/usb%.*s-port%s", SYS_USB, (int)(dash - usb), usb, (int)(dash - usb),
			 usb, (int)(dash - usb), usb, dash + 1);
	else
		return false;
	snprintf(file, sizeof(file), "%s/disable", out);
	return access(file, W_OK) == 0;
}

/*
 * The modem's radio settings, the same block whoever drives it (spec 25.9.12):
 * network mode (allowed technologies and the preferred one), bands now and
 * possible, and how they were read (via). ModemManager's own report for a
 * modem it holds; for one QModem or the engine drives, the last reading of
 * `ubus call medvemodem radio` (QModem's modem_ctrl.sh, or the profile's AT
 * dialect), with its age. writable: a change can be carried out (stage 2,
 * radio_set); change: the last one and where it stands.
 */
static json_object *radio_view(json_object *dev)
{
	json_object *o = json_object_new_object(), *mm = json_object_object_get(dev, "modemmanager");
	json_object *prof = json_object_object_get(json_object_object_get(dev, "profile"), "radio");
	json_object *cached = radio_cache ? json_object_object_get(radio_cache, jstr(dev, "device_id") ? jstr(dev, "device_id") : "") : NULL;
	const char *ctl = jstr(json_object_object_get(dev, "owner"), "control"), *dialect = prof ? jstr(prof, "dialect") : NULL;
	const char *via = "none", *why = NULL;
	bool qm = ctl && !strcmp(ctl, "qmodem");

	if (cached && json_object_get_int64(json_object_object_get(cached, "generation")) !=
		      json_object_get_int64(json_object_object_get(dev, "generation")))
		cached = NULL;
	if (mm && ctl && !strcmp(ctl, "modemmanager"))
		via = "modemmanager";
	else if (cached && !strcmp(jstr(cached, "via"), qm ? "qmodem" : "at"))
		via = jstr(cached, "via");
	else if (qm)
		why = "QModem drives this modem: not read yet, `ubus call medvemodem radio` reads it through QModem";
	else if (dialect && mm_radio_commands(dialect))
		why = "not read yet: `ubus call medvemodem radio` reads it over AT";
	else if (prof)
		why = "the engine has no AT reader for this modem's dialect yet";
	/* a HiLink or UFI is driven by its own router: «nobody drives it» read wrong (the owner, 2026-10-20) */
	else if (ctl && !strcmp(ctl, "hilink"))
		why = "the modem's own router holds the radio settings: set them on its web page";
	else
		why = "no owner reports the radio settings";
	json_object_object_add(o, "via", json_object_new_string(via));
	json_object_object_add(o, "dialect", dialect ? json_object_new_string(dialect) : NULL);
	if (!strcmp(via, "modemmanager")) {
		json_object_object_add(o, "modes", json_object_get(json_object_object_get(mm, "current_modes")));
		json_object *nw = mm_modes_narrowed(json_object_object_get(mm, "supported_modes"), json_object_object_get(mm, "supported_bands"),
						    json_object_object_get(mm, "port_types"));

		if (nw) {
			/* ModemManager narrowed to the one mode the modem stored: offer what its bands allow; a mode MM does not
			 * offer is written to the modem through QMI (mm_radio_plan) */
			json_object *pos = json_object_object_get(nw, "possible"), *combos = json_object_new_array(), *all, *no2g;
			size_t i;

			for (i = 0; i < json_object_array_length(pos); i++) {
				json_object *c = json_object_new_object(), *a = json_object_new_array();

				json_object_array_add(a, json_object_get(json_object_array_get_idx(pos, i)));
				json_object_object_add(c, "allowed", a);
				json_object_object_add(c, "preferred", NULL);
				json_object_array_add(combos, c);
			}
			all = json_object_new_object();
			json_object_object_add(all, "allowed", json_object_get(pos));
			json_object_object_add(all, "preferred", NULL);
			json_object_array_add(combos, all);
			if (json_object_array_length(pos) > 2 && !strcmp(json_object_get_string(json_object_array_get_idx(pos, 0)), "2g")) {
				json_object *a = json_object_new_array();

				no2g = json_object_new_object();
				for (i = 1; i < json_object_array_length(pos); i++)
					json_object_array_add(a, json_object_get(json_object_array_get_idx(pos, i)));
				json_object_object_add(no2g, "allowed", a);
				json_object_object_add(no2g, "preferred", NULL);
				json_object_array_add(combos, no2g);
			}
			json_object_object_add(o, "supported_modes", combos);
			json_object_object_add(o, "narrowed", nw);
		} else {
			json_object_object_add(o, "supported_modes", json_object_get(json_object_object_get(mm, "supported_modes")));
		}
		json_object_object_add(o, "bands", json_object_get(json_object_object_get(mm, "current_bands")));
		json_object_object_add(o, "supported_bands", json_object_get(json_object_object_get(mm, "supported_bands")));
		/* ModemManager has no cell lock: a T99's own (AT^LTE_LOCK?) comes from the engine's AT reading beside it */
		if (cached && !strcmp(jstr(cached, "via"), "at") &&
		    json_object_object_get(json_object_object_get(cached, "radio"), "cell_lock_rats")) {
			json_object *cr = json_object_object_get(cached, "radio");

			json_object_object_add(o, "cell_lock", json_object_get(json_object_object_get(cr, "cell_lock")));
			json_object_object_add(o, "cell_lock_rats", json_object_get(json_object_object_get(cr, "cell_lock_rats")));
		}
	} else if (strcmp(via, "none")) {
		json_object_object_foreach(json_object_object_get(cached, "radio"), k, v)
			json_object_object_add(o, k, json_object_get(v));
		json_object_object_add(o, "age_s", json_object_new_int64((now_mono_ms() -
			json_object_get_int64(json_object_object_get(cached, "at"))) / 1000));
	}
	/* stage 2: who can carry out a change (`ubus call medvemodem radio_set`) */
	{
		const char *nowrite = NULL;

		if (!strcmp(via, "none"))
			nowrite = "read the settings first";
		else if (!strcmp(via, "at") && !(dialect && (!strcmp(dialect, "quectel") || !strncmp(dialect, "foxconn-", 8) ||
							   !strcmp(dialect, "fibocom-mtk"))))
			nowrite = "writing this modem's AT dialect is not done yet";
		else if (!strcmp(via, "at") && !strcmp(dialect, "quectel") &&
			 !json_object_object_get(json_object_object_get(json_object_object_get(cached, "radio"), "raw"), "mode_pref"))
			nowrite = "this Quectel generation (QCFG) is read only for now";
		else if (!strcmp(via, "at") && !strcmp(dialect, "fibocom-mtk") &&
			 !json_object_object_get(json_object_object_get(json_object_object_get(cached, "radio"), "raw"), "gtact"))
			nowrite = "the modem did not answer AT+GTACT?: read only";
		else if (!strcmp(via, "at") && strncmp(dialect, "foxconn-", 8) == 0 &&
			 !json_object_object_get(json_object_object_get(json_object_object_get(cached, "radio"), "raw"), "slmode"))
			nowrite = "the modem did not answer AT^SLMODE?: read only";
		json_object_object_add(o, "writable", json_object_new_boolean(!nowrite));
		if (nowrite)
			json_object_object_add(o, "why_read_only", json_object_new_string(nowrite));
	}
	{
		json_object *change = radio_change_view(jstr(dev, "device_id") ? jstr(dev, "device_id") : "");

		if (change)
			json_object_object_add(o, "change", change);
	}
	if (why)
		json_object_object_add(o, "why", json_object_new_string(why));
	return o;
}

/*
 * What the engine repaired by itself lately (the owner, 2026-10-16: «можно на главной странице об этом
 * сообщать»): the newest self-repair of the last hour from the journal — {kind, at, detail} — for the card.
 */
static json_object *autofix_of(const char *id)
{
	static const char *const kinds[] = { "interface-retry", "sms-draft-dropped", "sms-stuck-dropped", "iface-repair", "ifup",
					     "mm-kick", "recovery-end", "recovery-no-internet", NULL };
	json_object *ev = mm_history_events(&history, id, 60), *out = NULL;
	long long now = (long long)time(NULL);
	size_t i;
	int k, count = 0;

	for (i = 0; ev && i < json_object_array_length(ev); i++) {
		json_object *e = json_object_array_get_idx(ev, i);
		const char *kind = jstr(e, "kind"), *detail = jstr(e, "detail");
		long long t = json_object_get_int64(json_object_object_get(e, "t"));

		if (!kind || now - t > 3600)
			continue;
		for (k = 0; kinds[k] && strcmp(kinds[k], kind); k++)
			;
		/* a recovery only when it ended well (the failed ones are the card's problem line) */
		if (!kinds[k] || (!strcmp(kind, "recovery-end") && (!detail || !strstr(detail, "succeeded"))))
			continue;
		count++;
		if (out)
			continue;
		out = json_object_new_object();
		json_object_object_add(out, "kind", json_object_new_string(kind));
		json_object_object_add(out, "at", json_object_new_int64(t));
		if (detail)
			json_object_object_add(out, "detail", json_object_new_string(detail));
	}
	json_object_put(ev);
	if (out)
		json_object_object_add(out, "count", json_object_new_int(count));	/* the base level shows «! N» */
	return out;
}

/*
 * ModemManager's FCC unlock hook for a USB ID (HANDOFF 176): <MM_FCC_UNLOCK_DIR>/<vid:pid>. Ours is a link to
 * MM_FCC_UNLOCK_HOOK the operator made with the button; anything else there (ModemManager's own hook, the user's) is
 * left alone. false for an ID that is not vvvv:pppp hex.
 */
static bool fcc_path(const char *vidpid, char key[16], char path[PATH_MAX])
{
	size_t i;

	if (!vidpid || strlen(vidpid) != 9 || vidpid[4] != ':')
		return false;
	for (i = 0; i < 9; i++) {
		char c = vidpid[i];

		if (i != 4 && !isxdigit((unsigned char)c))
			return false;
		key[i] = (char)tolower((unsigned char)c);
	}
	key[9] = '\0';
	snprintf(path, PATH_MAX, "%s/%s", MM_FCC_UNLOCK_DIR, key);
	return true;
}

/* 1 our link, 0 nothing there, -1 another hook */
static int fcc_hook_state(const char *path)
{
	char target[PATH_MAX];
	struct stat st;
	ssize_t n;

	if (lstat(path, &st))
		return 0;
	if (!S_ISLNK(st.st_mode) || (n = readlink(path, target, sizeof(target) - 1)) < 0)
		return -1;
	target[n] = '\0';
	return strcmp(target, MM_FCC_UNLOCK_HOOK) ? -1 : 1;
}

/* {"method", "hook", "enabled", "foreign", "installed"} for a modem whose profile names an FCC unlock */
static json_object *fcc_served(const char *method, const char *vidpid)
{
	json_object *o = json_object_new_object();
	char key[16], path[PATH_MAX];
	int st = fcc_path(vidpid, key, path) ? fcc_hook_state(path) : 0;

	json_object_object_add(o, "method", json_object_new_string(method));
	json_object_object_add(o, "enabled", json_object_new_boolean(st == 1));
	if (st < 0)
		json_object_object_add(o, "foreign", json_object_new_boolean(true));
	json_object_object_add(o, "installed", json_object_new_boolean(access(MM_FCC_UNLOCK_HOOK, X_OK) == 0));
	return o;
}

/*
 * What only the operator can fix on this modem now (HANDOFF 176): the dashboard's «Нужно ваше действие» and the modem
 * page's alert lead to the card for it. Kinds, in the order shown: drivers, modemmanager (missing packages),
 * modeswitch-rule, modeswitch-pending, sim-pin, sim-puk, fcc-unlock (a model with an FCC lock, the hook off, the
 * radio off by ModemManager's power state). Nothing here runs by itself. NULL when there is nothing.
 */
static json_object *actions_of(json_object *o)
{
	json_object *a = json_object_new_array(), *mm = json_object_object_get(o, "modemmanager");
	json_object *prof = json_object_object_get(o, "profile"), *fcc = json_object_object_get(o, "fcc_unlock");
	const char *cfg = jstr(json_object_object_get(o, "configuration"), "state"), *det = jstr(o, "detection_status");
	const char *unlock = jstr(mm, "unlock_required"), *power = jstr(mm, "power_state"), *prefer = jstr(prof, "prefer_owner");

#define ACT(k) json_object_array_add(a, json_object_new_string(k))
	if (!json_object_get_boolean(json_object_object_get(o, "present"))) {
		json_object_put(a);
		return NULL;
	}
	if (json_object_is_type(json_object_object_get(o, "drivers_missing"), json_type_array) &&
	    json_object_array_length(json_object_object_get(o, "drivers_missing")))
		ACT("drivers");
	if (!mm_installed() && strcmp(mm_backend, "disabled") && (!prefer || !strcmp(prefer, "modemmanager")))
		ACT("modemmanager");
	if (cfg && !strcmp(cfg, "modeswitch-rule"))
		ACT("modeswitch-rule");
	if (det && !strcmp(det, "modeswitch-pending"))
		ACT("modeswitch-pending");
	if (unlock && !strcmp(unlock, "sim-pin"))
		ACT("sim-pin");
	else if (unlock && !strcmp(unlock, "sim-puk"))
		ACT("sim-puk");
	if (foreign_holds(jstr(o, "device_id")))
		ACT("foreign-interface");
	if (fcc && !json_object_get_boolean(json_object_object_get(fcc, "enabled")) &&
	    !json_object_get_boolean(json_object_object_get(fcc, "foreign")) && power &&
	    (!strcmp(power, "low") || !strcmp(power, "off")))
		ACT("fcc-unlock");
#undef ACT
	if (!json_object_array_length(a)) {
		json_object_put(a);
		return NULL;
	}
	return a;
}

json_object *served(json_object *rec)
{
	json_object *o = json_object_new_object(), *idv = NULL;

	json_object_object_foreach(rec, key, val)
		json_object_object_add(o, key, json_object_get(val));
	if (json_object_object_get_ex(rec, "device_id", &idv)) {
		json_object *mm = NULL, *conf = NULL, *st = NULL, *finding;

		json_object_object_add(o, "stability", mm_history_stability(&history, json_object_get_string(idv),
									     now_mono_ms(), &stability_cfg));
		if (!(mm_devices && json_object_object_get_ex(mm_devices, json_object_get_string(idv), &mm)) && known_models &&
		    json_object_object_get(known_models, json_object_get_string(idv))) {
			/* ModemManager does not hold it now: the profile from the identity it named before */
			json_object *enriched = profile_from_mm(rec, json_object_object_get(known_models, json_object_get_string(idv)));

			if (enriched) {
				json_object_object_add(enriched, "identity_source", json_object_new_string("usb+remembered"));
				json_object_object_add(o, "profile", enriched);
			}
			mm = NULL;
		}
		if (mm_devices && json_object_object_get_ex(mm_devices, json_object_get_string(idv), &mm)) {
			json_object *enriched = profile_from_mm(rec, mm);
			json_object *cc = cells_cache && !cell_info_on ? json_object_object_get(cells_cache, json_object_get_string(idv)) : NULL;

			if (cc && json_object_get_int64(json_object_object_get(cc, "generation")) ==
				  json_object_get_int64(json_object_object_get(rec, "generation")) &&
			    now_mono_ms() - json_object_get_int64(json_object_object_get(cc, "at")) <= CELLS_SHOWN_MS) {
				/* the cells over AT in ModemManager's place (r34): the page shows them as it showed MM's */
				json_object *copy = NULL;

				json_object_deep_copy(mm, &copy, NULL);
				json_object_object_add(copy, "cells", json_object_get(json_object_object_get(cc, "cells")));
				json_object_object_add(copy, "cells_source", json_object_new_string("at"));
				json_object_object_add(o, "modemmanager", copy);
			} else {
				json_object_object_add(o, "modemmanager", json_object_get(mm));
			}
			if (enriched)
				json_object_object_add(o, "profile", enriched);
			/* a lock or radio-off finding replaces "unknown", never a structural one */
			finding = mm_mmgr_health(mm);
			if (finding && json_object_object_get_ex(rec, "configuration", &conf) &&
			    json_object_object_get_ex(conf, "state", &st) && !strcmp(json_object_get_string(st), "unknown"))
				json_object_object_add(o, "configuration", finding);
			else
				json_object_put(finding);
		}
		/* the FCC unlock the operator may turn on: whether ModemManager has our hook for this ID (HANDOFF 176) */
		if (jstr(json_object_object_get(o, "profile"), "fcc_unlock"))
			json_object_object_add(o, "fcc_unlock", fcc_served(jstr(json_object_object_get(o, "profile"), "fcc_unlock"),
									   jstr(rec, "vidpid")));
		{
			json_object *probed = probes ? json_object_object_get(probes, json_object_get_string(idv)) : NULL;
			json_object *input = rec, *roles, *held, *own = NULL;
			const char *op_at = jstr(decision_of(json_object_get_string(idv)), "at_interface");
			bool fresh = probed && json_object_get_int64(json_object_object_get(probed, "generation")) ==
						json_object_get_int64(json_object_object_get(rec, "generation"));

			/* probe verdicts of this attachment generation, and the operator's AT interface (UCI
			 * device option at_interface), are evidence for the roles; so is the profile ModemManager's
			 * identity picked (the T99's at_interfaces were lost when the roles read the bare record) */
			if (fresh || (op_at && *op_at) || json_object_object_get(o, "profile") != json_object_object_get(rec, "profile")) {
				input = json_object_new_object();
				json_object_object_foreach(rec, k2, v2)
					json_object_object_add(input, k2, json_object_get(v2));
				json_object_object_add(input, "profile", json_object_get(json_object_object_get(o, "profile")));
				if (fresh)
					json_object_object_add(input, "probes", json_object_get(json_object_object_get(probed, "results")));
				if (op_at && *op_at)
					json_object_object_add(input, "operator_at", json_object_new_string(op_at));
			}
			roles = mm_roles_resolve(input, mm, mm_backend);
			held = mm_lease_list(&leases, json_object_get_string(idv));
			if (input != rec)
				json_object_put(input);
			size_t i;

			for (i = 0; i < json_object_array_length(held); i++) {
				json_object *l = json_object_array_get_idx(held, i), *port = NULL, *c, *info;

				json_object_object_get_ex(l, "port", &port);
				c = mm_roles_candidate(roles, json_object_get_string(port));
				if (!c)
					continue;
				info = json_object_new_object();
				json_object_object_foreach(l, key, val)
					if (!strcmp(key, "lease_id") || !strcmp(key, "holder") || !strcmp(key, "purpose") || !strcmp(key, "mode"))
						json_object_object_add(info, key, json_object_get(val));
				json_object_object_add(c, "lease", info);
			}
			json_object_put(held);
			/* another program has the port open: shown, and the engine does not talk over it */
			{
				json_object *cands = json_object_object_get(roles, "candidates");

				for (i = 0; tty_holders && cands && i < json_object_array_length(cands); i++) {
					json_object *c = json_object_array_get_idx(cands, i);
					const char *node = jstr(c, "node"), *b = node ? strrchr(node, '/') : NULL;

					if (node && jstr(tty_holders, b ? b + 1 : node))
						json_object_object_add(c, "held_by", json_object_new_string(jstr(tty_holders, b ? b + 1 : node)));
				}
			}
			/* the owner below replaces discovery's "unresolved" placeholder */
			if (json_object_object_get_ex(roles, "owner", &own)) {
				json_object *led = mm_cfgtxn_ledger(&cfg, json_object_get_string(idv)), *ctl = NULL;
				const char *proto = mm_netcfg_ledger_proto(led), *qsec = qmodem_section_of(rec);
				const char *backend = ledger_backend(json_object_get_string(idv));
				bool native = proto && (!strcmp(proto, "qmi") || !strcmp(proto, "mbim") || !strcmp(proto, "medvemodem"));
				/* the engine dials it itself: a QModem section is what QModem's own scan made again after the
				 * hand-back, not QModem's modem (bench 2026-10-21: the FM350 on «MedveModem» had no SMS and AT tabs,
				 * every path refused as QModem's) */
				bool ours = native && backend && !strcmp(backend, "native");

				own = json_object_get(own);
				json_object_object_get_ex(own, "control", &ctl);
				if ((qsec && !ours) || (backend && !strcmp(backend, "qmodem"))) {
					/* QModem drives this modem: its ports and session are QModem's */
					bool mm_too = !strcmp(json_object_get_string(ctl), "modemmanager");
					json_object *q = json_object_new_object(), *qs = qsec ? json_object_object_get(qmodem_img, qsec) : NULL;

					json_object_put(own);
					own = json_object_new_object();
					json_object_object_add(own, "control", json_object_new_string("qmodem"));
					json_object_object_add(own, "session", json_object_new_string("qmodem"));
					json_object_object_add(own, "at", json_object_new_string("qmodem"));
					if (mm_too)
						json_object_object_add(own, "conflict", json_object_new_string(
							"ModemManager still holds a modem handed to QModem"));
					json_object_object_add(q, "section", qsec ? json_object_new_string(qsec) : NULL);
					json_object_object_add(q, "state", json_object_get(json_object_object_get(qs, "state")));
					json_object_object_add(q, "at_port", json_object_get(json_object_object_get(qs, "at_port")));
					json_object_object_add(q, "network", json_object_get(json_object_object_get(qs, "network")));
					json_object_object_add(q, "managed_by_engine", json_object_new_boolean(backend && !strcmp(backend, "qmodem")));
					if (qsec && qmodem_foreign_port(rec, qsec)) {
						json_object_object_add(q, "foreign_at_port", json_object_new_boolean(1));
						json_object_object_add(own, "conflict", json_object_new_string(
							"QModem's section names an AT port that is not this modem's (another modem's port)"));
					}
					json_object_object_add(o, "qmodem", q);
				} else if (native && !strcmp(json_object_get_string(ctl), "unresolved")) {
					/* spec 13: the native owner is the configuration we wrote */
					json_object_put(own);
					own = json_object_new_object();
					json_object_object_add(own, "control", json_object_new_string(!strcmp(proto, "qmi") ? "netifd-qmi" :
										       !strcmp(proto, "mbim") ? "netifd-mbim" : "medvemodem-at"));
					json_object_object_add(own, "session", json_object_new_string("netifd"));
					json_object_object_add(own, "at", json_object_get(json_object_object_get(json_object_object_get(roles, "owner"), "at")));
				} else if (native && !strcmp(json_object_get_string(ctl), "modemmanager")) {
					/* spec 12.2: ModemManager came up beside a native session */
					json_object_object_add(own, "conflict", json_object_new_string(
						"ModemManager took a modem configured for native netifd; two owners of one control port"));
				}
				if (ours && qsec) {
					json_object *qs = json_object_object_get(qmodem_img, qsec);
					const char *dial = jstr(qs, "enable_dial");
					char note[200];

					snprintf(note, sizeof(note), "QModem has a section for this modem again (%s)%s", qsec,
						 dial && !strcmp(dial, "0") ? "" : "; its dial may run beside the engine's");
					json_object_object_add(own, "conflict", json_object_new_string(note));
				}
				json_object_object_add(o, "owner", own);
				if (backend)
					json_object_object_add(o, "backend", json_object_new_string(backend));
				if (inhibited(json_object_get_string(idv)))
					json_object_object_add(o, "modemmanager_inhibited", json_object_new_boolean(true));
				json_object_put(led);
			}
			if (sessions && json_object_object_get(sessions, json_object_get_string(idv))) {
				json_object *sess = json_object_object_get(sessions, json_object_get_string(idv)), *copy;
				json_object *led = mm_cfgtxn_ledger(&cfg, json_object_get_string(idv));

				/* managed: the engine's configuration still owns the session's interface */
				copy = json_object_new_object();
				json_object_object_foreach(sess, sk, sv) {
					if (!strcmp(sk, "netifd") && json_object_is_type(sv, json_type_object) &&
					    json_object_object_get(sess, "up_at")) {
						/* the uptime now, from the moment the interface came up */
						json_object *n = json_object_new_object();
						long long up = (long long)time(NULL) - json_object_get_int64(json_object_object_get(sess, "up_at"));
						json_object_object_foreach(sv, nk, nv)
							json_object_object_add(n, nk, json_object_get(nv));
						json_object_object_add(n, "uptime", json_object_new_int64(up > 0 ? up : 0));
						json_object_object_add(copy, sk, n);
					} else if (strcmp(sk, "up_at")) {
						json_object_object_add(copy, sk, json_object_get(sv));
					}
				}
				json_object_object_add(copy, "managed", json_object_new_boolean(
					jstr(led, "interface") && jstr(sess, "interface") && !strcmp(jstr(led, "interface"), jstr(sess, "interface"))));
				{
					/* netifd's handler for the session's protocol (dhcp and static are built in) */
					const char *pr = jstr(sess, "proto");
					char path[300];

					if (pr && *pr && strcmp(pr, "dhcp") && strcmp(pr, "static") && !strchr(pr, '/')) {
						snprintf(path, sizeof(path), "%s/%s.sh", proto_dir, pr);
						if (access(path, F_OK))
							json_object_object_add(copy, "proto_missing", json_object_new_string(pr));
					}
				}
				json_object_put(led);
				json_object_object_add(o, "session", copy);
			}
			else if (mm) {
				json_object *mms = session_from_mm(mm);
				if (mms)
					json_object_object_add(o, "session", mms);
			}
			json_object_object_add(o, "port_roles", roles);
		}
		if (hostfixes && json_object_object_get(hostfixes, json_object_get_string(idv)))
			json_object_object_add(o, "host_fix", json_object_get(json_object_object_get(hostfixes, json_object_get_string(idv))));
		{
			/* interfaces waiting for a kernel driver the router lacks: the page names the package */
			json_object *miss = mm_hostfix_missing_drivers(rec, kernel_has_driver);

			if (json_object_array_length(miss))
				json_object_object_add(o, "drivers_missing", miss);
			else
				json_object_put(miss);
		}
		if (mm_hold_due(json_object_get_string(idv), rec))
			json_object_object_add(o, "mm_expected", json_object_new_boolean(1));
		if (frozen(json_object_get_string(idv)))
			json_object_object_add(o, "frozen", json_object_get(json_object_object_get(frozen_map, json_object_get_string(idv))));
		{
			json_object *e = mm_absent ? json_object_object_get(mm_absent, json_object_get_string(idv)) : NULL;

			if (e && json_object_get_boolean(json_object_object_get(e, "probing")))
				json_object_object_add(o, "mm_probing", json_object_new_boolean(1));
		}
		json_object_object_add(o, "health", watch_view_of(json_object_get_string(idv)));
		{
			const char *att = jstr(rec, "attachment_id");
			char port[PATH_MAX];
			json_object *prof = json_object_object_get(o, "profile"), *copy = NULL;

			/* a switchable hub port makes the power level possible by the button (R9) */
			if (att && !strncmp(att, "usb:", 4) && usb_port_dir(att + 4, port, sizeof(port))) {
				if (!prof)
					copy = json_object_new_object();
				else if (json_object_deep_copy(prof, &copy, NULL))
					copy = NULL;
				if (copy) {
					json_object_object_add(copy, "port_power", json_object_new_boolean(1));
					json_object_object_add(o, "profile", copy);
				}
			}
		}
		if (iface_checks && json_object_object_get(iface_checks, json_object_get_string(idv)))
			json_object_object_add(o, "netif", json_object_get(json_object_object_get(iface_checks, json_object_get_string(idv))));
		if (modem_name(json_object_get_string(idv)))
			json_object_object_add(o, "name", json_object_new_string(modem_name(json_object_get_string(idv))));
		json_object_object_add(o, "name_follows", json_object_new_string(name_follows(json_object_get_string(idv), rec)));
		{
			json_object *lab = sim_label(sim_iccid_of(json_object_get_string(idv), json_object_object_get(o, "modemmanager")));
			if (lab)
				json_object_object_add(o, "sim_label", lab);
			/* the SIM as the engine read it, for a modem ModemManager does not hold (LuCI: «SIM-карта») */
			{
				json_object *e = sim_entry(json_object_get_string(idv)), *s;

				if (e && json_object_get_int64(json_object_object_get(e, "generation")) == json_object_get_int64(json_object_object_get(rec, "generation")) &&
				    (jstr(e, "imsi") || jstr(e, "iccid"))) {
					s = json_object_new_object();
					json_object_object_add(s, "source", json_object_new_string(jstr(e, "source") ? jstr(e, "source") : ""));
					if (jstr(e, "imsi"))
						json_object_object_add(s, "imsi", json_object_new_string(jstr(e, "imsi")));
					if (jstr(e, "iccid"))
						json_object_object_add(s, "iccid", json_object_new_string(jstr(e, "iccid")));
					json_object_object_add(o, "sim", s);
				}
			}
		}
		/* the SIM's own operator (MCC+MNC), apart from the network it is on: Yota's SIM registers on MegaFon's network
		 * and the modem names the network (the owner, 2026-11-05). ModemManager's SIM card, else the IMSI the engine
		 * read (only its first digits leave), else a router-mode stick's own web */
		{
			json_object *mmo = json_object_object_get(o, "modemmanager"), *sec, *so;
			const char *code = jstr(json_object_object_get(mmo, "sim_card"), "operator_code"), *src = "sim";
			char buf[8];

			if (!code || strlen(code) < 5 || strlen(code) > 6 || strspn(code, "0123456789") != strlen(code)) {
				/* ModemManager's own copy, or the engine's reading of this very attachment (a SIM changes on a replug) */
				sec = mmo ? (sim_secret ? json_object_object_get(sim_secret, json_object_get_string(idv)) : NULL) :
				      sim_entry(json_object_get_string(idv));
				if (!mmo && sec && json_object_get_int64(json_object_object_get(sec, "generation")) !=
						   json_object_get_int64(json_object_object_get(rec, "generation")))
					sec = NULL;
				code = jstr(sec, "imsi") && simid_plmn_of_imsi(jstr(sec, "imsi"), buf, sizeof(buf)) ? buf : NULL;
				src = "imsi";
			}
			if (!code && (code = web_sim_plmn(rec)))
				src = "web";
			if (code && strlen(code) >= 5 && strlen(code) <= 6 && strspn(code, "0123456789") == strlen(code)) {
				so = json_object_new_object();
				json_object_object_add(so, "code", json_object_new_string(code));
				json_object_object_add(so, "source", json_object_new_string(src));
				json_object_object_add(o, "sim_operator", so);
			}
		}
		json_object_object_add(o, "uplink", uplink_view_of(json_object_get_string(idv), json_object_object_get(o, "session")));
		if (uplinks_all_failing())
			json_object_object_add(o, "uplinks_all_failing", json_object_new_boolean(1));
		if (usb_power && json_object_object_get(usb_power, json_object_get_string(idv)))
			json_object_object_add(o, "usb_power", json_object_get(json_object_object_get(usb_power, json_object_get_string(idv))));
		if (usb_txstall && json_object_object_get(usb_txstall, json_object_get_string(idv)))
			json_object_object_add(o, "tx_stall", json_object_get(json_object_object_get(usb_txstall, json_object_get_string(idv))));
		json_object_object_add(o, "connection", connection_of(json_object_get_string(idv), o));
		{
			json_object *act = actions_of(o);

			if (act)
				json_object_object_add(o, "action_needed", act);
		}
		/* the first broken link (alpha): read from the record built so far, the same for LuCI, TUI and CLI */
		json_object_object_add(o, "diagnosis", mm_chain(o));
		{
			json_object *q = quota_of(json_object_get_string(idv)), *sb = sms_badge(json_object_get_string(idv));
			if (q)
				json_object_object_add(o, "quota", q);
			if (sb)
				json_object_object_add(o, "sms", sb);
			if ((sb = downtime_of(json_object_get_string(idv))))
				json_object_object_add(o, "downtime", sb);
			if ((sb = mmlog_of(json_object_get_string(idv))))
				json_object_object_add(o, "mm_log", sb);
			if ((sb = neighbours_of(json_object_get_string(idv))))
				json_object_object_add(o, "port_holders", sb);
		}
		{
			json_object *af = autofix_of(json_object_get_string(idv));

			if (af)
				json_object_object_add(o, "autofix", af);
		}
		{
			/* the newest journal line, for the dashboard's «Событие» (the owner, 2026-10-17); the rest by `events` */
			json_object *ev = mm_history_events(&history, json_object_get_string(idv), 1);

			if (ev && json_object_array_length(ev))
				json_object_object_add(o, "last_event", json_object_get(json_object_array_get_idx(ev, 0)));
			json_object_put(ev);
		}
		if (known_models && jstr(json_object_object_get(known_models, json_object_get_string(idv)), "model"))
			json_object_object_add(o, "model", json_object_new_string(
				jstr(json_object_object_get(known_models, json_object_get_string(idv)), "model")));
		else if (web_model(json_object_get_string(idv)))
			json_object_object_add(o, "model", json_object_new_string(web_model(json_object_get_string(idv))));
		json_object_object_add(o, "recovery", recovery_view_of(json_object_get_string(idv),
			json_object_object_get(o, "profile"), rec_owner(mm_registry_find(&registry, json_object_get_string(idv)), o)));
		json_object_object_add(o, "radio_settings", radio_view(o));
		{
			/* what the operator can do with this modem now (field 2026-10-13): a page hides what is not there */
			json_object *cap = json_object_new_object(), *c;
			const char *why = NULL;

			c = json_object_new_object();
			if (frozen(json_object_get_string(idv)))
				why = "the modem is frozen by the operator";
			json_object_object_add(c, "ok", json_object_new_boolean(!why && sms_capable(json_object_get_string(idv), o, &why)));
			if (why)
				json_object_object_add(c, "why", json_object_new_string(why));
			{
				/* r94: a HiLink's SMS go through its web: no USSD, storage or SMS centre there */
				const char *w2 = NULL;
				json_object *r2 = mm_registry_find(&registry, json_object_get_string(idv));

				if (!why && r2 && !json_object_object_get(o, "modemmanager") && !engine_at_port(o, &w2) && web_sms_capable(r2, o)) {
					json_object_object_add(c, "via", json_object_new_string("web"));
					/* r117: a HiLink's web sends USSD and gives its own data too */
					if (web_huawei(r2, o) || web_mark(r2, o) || web_zte(r2, o))
						json_object_object_add(c, "ussd", json_object_new_string("web"));
				}
				else if (!why && r2 && !json_object_object_get(o, "modemmanager") && !engine_at_port(o, &w2) &&
					 sms_qmodem_section(r2))
					json_object_object_add(c, "via", json_object_new_string("qmodem"));	/* r95: no USSD here */
			}
			json_object_object_add(cap, "sms", c);
			why = NULL;
			c = json_object_new_object();
			json_object_object_add(c, "ok", json_object_new_boolean(engine_at_port(o, &why) != NULL));
			if (why)
				json_object_object_add(c, "why", json_object_new_string(why));
			json_object_object_add(cap, "at", c);
			if (jstr(json_object_object_get(o, "profile"), "ussd") && !strcmp(jstr(json_object_object_get(o, "profile"), "ussd"), "unsupported")) {
				c = json_object_new_object();
				json_object_object_add(c, "ok", json_object_new_boolean(0));
				json_object_object_add(c, "why", json_object_new_string(MM_USSD_UNSUPPORTED));
				json_object_object_add(cap, "ussd", c);
			}
			json_object_object_add(o, "capabilities", cap);
		}
	}
	return o;
}

/* ---- tty leases (spec 11.6) ---- */

/* The node still hangs below this USB device in sysfs: proves it is the same modem. */
bool node_still_owned(const char *node, const char *attachment)
{
	const char *bare = strncmp(node, "/dev/", 5) ? node : node + 5;
	char link[PATH_MAX], real[PATH_MAX], name[64];

	if (!attachment || strncmp(attachment, "usb:", 4))
		return false;
	snprintf(link, sizeof(link), "%s/%s/device", SYS_TTY, bare);
	return realpath(link, real) && mm_uevent_usb_name(real, name, sizeof(name)) && !strcmp(name, attachment + 4);
}

void lease_rearm(void)
{
	long long next = mm_lease_next_deadline(&leases), wait;

	if (next < 0) {
		uloop_timeout_cancel(&lease_timer);
		return;
	}
	wait = next - now_mono_ms();
	uloop_timeout_set(&lease_timer, wait > 0 ? (int)wait : 0);
}

void leases_revoked(json_object *list, const char *reason)
{
	size_t i;

	for (i = 0; list && i < json_object_array_length(list); i++) {
		json_object *l = json_object_array_get_idx(list, i), *e = json_object_new_object(), *id = NULL, *h = NULL;

		json_object_object_get_ex(l, "lease_id", &id);
		json_object_object_get_ex(l, "holder", &h);
		ULOG_INFO("lease %s of %s ended: %s\n", json_object_get_string(id), json_object_get_string(h), reason);
		at_lease_ended(json_object_get_string(id), reason);
		json_object_object_add(e, "event", json_object_new_string("ended"));
		json_object_object_add(e, "reason", json_object_new_string(reason));
		json_object_object_add(e, "lease", json_object_get(l));
		broadcast("medvemodem.lease", e);
		json_object_put(e);
	}
	lease_rearm();
}

static void lease_timeout(struct uloop_timeout *t)
{
	json_object *gone;

	(void)t;
	gone = mm_lease_expire(&leases, now_mono_ms());
	leases_revoked(gone, "expired");
	json_object_put(gone);
}

static int reply_record(struct ubus_context *ctx, struct ubus_request_data *req, json_object *rec)
{
	json_object *o = served(rec);
	int rc = reply_json(ctx, req, NULL, o);

	json_object_put(o);
	return rc;
}

/* ---- status ---- */
static int m_status(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	json_object *o = json_object_new_object();
	json_object *list = mm_registry_list(&registry, false);
	int rc;

	(void)obj; (void)method; (void)msg;
	json_object_object_add(o, "version", json_object_new_string(MEDVEMODEM_VERSION));
	json_object_object_add(o, "uptime", json_object_new_int64((long long)(time(NULL) - started)));
	json_object_object_add(o, "usb_class", modem_class_view());	/* mgr_modemclass.c: the suspects, the operator's word */
	json_object_object_add(o, "devices", json_object_new_int((int)json_object_array_length(list)));
	json_object_object_add(o, "passes", json_object_new_int64((long long)registry.passes));
	json_object_object_add(o, "last_pass", json_object_new_int64((long long)last_pass));
	json_object_object_add(o, "settle_ms", json_object_new_int(settle_ms));
	json_object_object_add(o, "pass_pending", json_object_new_boolean(pending));
	json_object_object_add(o, "uevents", json_object_new_int64((long long)uevents_seen));
	json_object_object_add(o, "uevents_relevant", json_object_new_int64((long long)uevents_relevant));
	json_object_object_add(o, "uevent_overflows", json_object_new_int64((long long)uevent_overflows));
	json_object_object_add(o, "history_devices", json_object_new_int(json_object_object_length(history.devices)));
	json_object_object_add(o, "leases", json_object_new_int((int)json_object_array_length(leases.list)));
	json_object_object_add(o, "load", load_status());
	json_object_object_add(o, "self", self_status());
	json_object_object_add(o, "services", procs_status());
	json_object_object_add(o, "kernel", kfault_status());
	{
		json_object *mm = json_object_new_object();

		json_object_object_add(mm, "backend", json_object_new_string(mm_backend));
		json_object_object_add(mm, "installed", json_object_new_boolean(mm_installed()));
		json_object_object_add(mm, "age_s", mm_last_done < 0 ? NULL : json_object_new_int64((now_mono_ms() - mm_last_done) / 1000));
		json_object_object_add(mm, "modems", json_object_new_int(mm_modems));
		json_object_object_add(mm, "unmapped", mm_unmapped ? json_object_get(mm_unmapped) : json_object_new_array());
		json_object_object_add(mm, "conflicts", mm_conflicts ? json_object_get(mm_conflicts) : json_object_new_array());
		json_object_object_add(mm, "restart", mmr_status());
		json_object_object_add(o, "modemmanager", mm);
	}
	mm_maybe_refresh();
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(list);
	json_object_put(o);
	return rc;
}

/* ---- devices ---- */
enum { DEVICES_ABSENT, __DEVICES_MAX };
static const struct blobmsg_policy devices_policy[] = {
	[DEVICES_ABSENT] = { .name = "absent", .type = BLOBMSG_TYPE_BOOL },
};

static int m_devices(struct ubus_context *ctx, struct ubus_object *obj,
		     struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__DEVICES_MAX];
	json_object *list, *out;
	size_t i;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(devices_policy, __DEVICES_MAX, tb, blob_data(msg), blob_len(msg));
	/* a stale ModemManager view would show a fallback profile: read it first (bounded) */
	if (mm_wait(ctx, req, true, tb[DEVICES_ABSENT] && blobmsg_get_bool(tb[DEVICES_ABSENT]), NULL))
		return UBUS_STATUS_OK;
	list = mm_registry_list(&registry, tb[DEVICES_ABSENT] && blobmsg_get_bool(tb[DEVICES_ABSENT]));
	out = json_object_new_array();
	for (i = 0; i < json_object_array_length(list); i++)
		json_object_array_add(out, served(json_object_array_get_idx(list, i)));
	rc = reply_json(ctx, req, "devices", out);
	json_object_put(out);
	json_object_put(list);
	return rc;
}

/* ---- device ---- */
enum { DEVICE_ID, __DEVICE_MAX };
static const struct blobmsg_policy device_policy[] = {
	[DEVICE_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
};

static int m_device(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__DEVICE_MAX];
	json_object *rec;

	(void)obj; (void)method;
	mm_maybe_refresh();
	blobmsg_parse(device_policy, __DEVICE_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[DEVICE_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_find(&registry, blobmsg_get_string(tb[DEVICE_ID]));
	if (!rec)
		return UBUS_STATUS_NOT_FOUND;
	if (mm_wait(ctx, req, false, false, jstr(rec, "device_id")))
		return UBUS_STATUS_OK;
	return reply_record(ctx, req, rec);
}

/* ---- lookup ---- */
enum { LOOKUP_NODE, __LOOKUP_MAX };
static const struct blobmsg_policy lookup_policy[] = {
	[LOOKUP_NODE] = { .name = "node", .type = BLOBMSG_TYPE_STRING },
};

static int m_lookup(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__LOOKUP_MAX];
	json_object *rec;

	(void)obj; (void)method;
	mm_maybe_refresh();
	blobmsg_parse(lookup_policy, __LOOKUP_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[LOOKUP_NODE])
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_lookup_node(&registry, blobmsg_get_string(tb[LOOKUP_NODE]));
	if (!rec)
		return UBUS_STATUS_NOT_FOUND;
	return reply_record(ctx, req, rec);
}

/* ---- events ---- */
enum { EVENTS_ID, EVENTS_LIMIT, __EVENTS_MAX };
static const struct blobmsg_policy events_policy[] = {
	[EVENTS_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[EVENTS_LIMIT] = { .name = "limit", .type = BLOBMSG_TYPE_INT32 },
};

static int m_events(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__EVENTS_MAX];
	json_object *rec, *idv = NULL, *list;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(events_policy, __EVENTS_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[EVENTS_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_find(&registry, blobmsg_get_string(tb[EVENTS_ID]));
	if (!rec || !json_object_object_get_ex(rec, "device_id", &idv))
		return UBUS_STATUS_NOT_FOUND;
	list = mm_history_events(&history, json_object_get_string(idv), tb[EVENTS_LIMIT] ? (int)blobmsg_get_u32(tb[EVENTS_LIMIT]) : 0);
	if (!list)
		list = json_object_new_array();
	rc = reply_json(ctx, req, "events", list);
	json_object_put(list);
	return rc;
}

/* ---- leases ---- */
enum { LA_ID, LA_PORT, LA_HOLDER, LA_PURPOSE, LA_MODE, LA_TTL, __LA_MAX };
static const struct blobmsg_policy lease_acquire_policy[] = {
	[LA_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[LA_PORT] = { .name = "port", .type = BLOBMSG_TYPE_STRING },
	[LA_HOLDER] = { .name = "holder", .type = BLOBMSG_TYPE_STRING },
	[LA_PURPOSE] = { .name = "purpose", .type = BLOBMSG_TYPE_STRING },
	[LA_MODE] = { .name = "mode", .type = BLOBMSG_TYPE_STRING },
	[LA_TTL] = { .name = "ttl_ms", .type = BLOBMSG_TYPE_INT32 },
};

static int refuse(struct ubus_context *ctx, struct ubus_request_data *req, const char *why, const char *detail)
{
	json_object *o = json_object_new_object();
	int rc;

	json_object_object_add(o, "granted", json_object_new_boolean(0));
	json_object_object_add(o, "reason", json_object_new_string(why));
	if (detail && *detail)
		json_object_object_add(o, "detail", json_object_new_string(detail));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

static int m_lease_acquire(struct ubus_context *ctx, struct ubus_object *obj,
			   struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__LA_MAX];
	json_object *rec, *idv = NULL, *att = NULL, *srv, *roles = NULL, *r, *lease, *revoked = NULL, *gone, *node = NULL, *o;
	const char *why, *port;
	char detail[160];
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(lease_acquire_policy, __LA_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[LA_ID] || !tb[LA_PORT] || !tb[LA_HOLDER] || !tb[LA_PURPOSE])
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_find(&registry, blobmsg_get_string(tb[LA_ID]));
	if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present")))
		return refuse(ctx, req, "device not present", NULL);
	json_object_object_get_ex(rec, "device_id", &idv);
	json_object_object_get_ex(rec, "attachment_id", &att);
	gone = mm_lease_expire(&leases, now_mono_ms());
	leases_revoked(gone, "expired");
	json_object_put(gone);

	if (frozen(json_object_get_string(idv)))
		return refuse(ctx, req, "the modem is frozen by the operator", "«Разморозить» on its card first");
	srv = served(rec);
	json_object_object_get_ex(srv, "port_roles", &roles);
	port = blobmsg_get_string(tb[LA_PORT]);
	if (!strcmp(port, "at"))
		json_object_object_get_ex(json_object_object_get(roles, "at"), "node", &node);
	/* revalidate against sysfs right now (spec 11.6), not against the last pass */
	if ((node || strcmp(port, "at")) && !node_still_owned(node ? json_object_get_string(node) : port, json_object_get_string(att))) {
		json_object_put(srv);
		schedule_pass(true);
		return refuse(ctx, req, "port is not on this device any more", "rescanning; retry after the pass");
	}
	r = json_object_new_object();
	json_object_object_add(r, "device_id", json_object_get(idv));
	json_object_object_add(r, "port", json_object_new_string(port));
	json_object_object_add(r, "holder", json_object_new_string(blobmsg_get_string(tb[LA_HOLDER])));
	json_object_object_add(r, "purpose", json_object_new_string(blobmsg_get_string(tb[LA_PURPOSE])));
	if (tb[LA_MODE])
		json_object_object_add(r, "mode", json_object_new_string(blobmsg_get_string(tb[LA_MODE])));
	if (tb[LA_TTL])
		json_object_object_add(r, "ttl_ms", json_object_new_int((int)blobmsg_get_u32(tb[LA_TTL])));
	lease = mm_lease_acquire(&leases, r, roles, now_mono_ms(), &why, detail, sizeof(detail), &revoked);
	json_object_put(r);
	json_object_put(srv);
	leases_revoked(revoked, "preempted");
	json_object_put(revoked);
	if (!lease)
		return refuse(ctx, req, why, detail);
	o = json_object_new_object();
	json_object_object_add(o, "granted", json_object_new_boolean(1));
	json_object_object_add(o, "lease", lease);
	lease_rearm();
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

enum { LR_LEASE, LR_TTL, __LR_MAX };
static const struct blobmsg_policy lease_policy[] = {
	[LR_LEASE] = { .name = "lease_id", .type = BLOBMSG_TYPE_STRING },
	[LR_TTL] = { .name = "ttl_ms", .type = BLOBMSG_TYPE_INT32 },
};

static int m_lease_release(struct ubus_context *ctx, struct ubus_object *obj,
			   struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__LR_MAX];
	json_object *o = json_object_new_object();
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(lease_policy, __LR_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[LR_LEASE]) {
		json_object_put(o);
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	json_object_object_add(o, "released", json_object_new_boolean(mm_lease_release(&leases, blobmsg_get_string(tb[LR_LEASE]))));
	lease_rearm();
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

static int m_lease_renew(struct ubus_context *ctx, struct ubus_object *obj,
			 struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__LR_MAX];
	json_object *list, *lease = NULL, *rec, *gen = NULL, *o;
	size_t i;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(lease_policy, __LR_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[LR_LEASE])
		return UBUS_STATUS_INVALID_ARGUMENT;
	list = mm_lease_list(&leases, NULL);
	for (i = 0; i < json_object_array_length(list); i++) {
		json_object *l = json_object_array_get_idx(list, i);
		if (!strcmp(json_object_get_string(json_object_object_get(l, "lease_id")), blobmsg_get_string(tb[LR_LEASE]))) {
			/* renew only within the device's current generation */
			rec = mm_registry_find(&registry, json_object_get_string(json_object_object_get(l, "device_id")));
			if (rec && json_object_object_get_ex(rec, "generation", &gen))
				lease = mm_lease_renew(&leases, blobmsg_get_string(tb[LR_LEASE]),
						       tb[LR_TTL] ? (int)blobmsg_get_u32(tb[LR_TTL]) : 0, now_mono_ms(),
						       json_object_get_int64(gen));
			break;
		}
	}
	json_object_put(list);
	o = json_object_new_object();
	json_object_object_add(o, "renewed", json_object_new_boolean(lease != NULL));
	if (lease)
		json_object_object_add(o, "lease", lease);
	lease_rearm();
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

static int m_leases(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	json_object *list;
	int rc;

	(void)obj; (void)method; (void)msg;
	list = mm_lease_list(&leases, NULL);
	rc = reply_json(ctx, req, "leases", list);
	json_object_put(list);
	return rc;
}

/* ---- rescan ---- */
static int m_rescan(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	json_object *o = json_object_new_object();
	unsigned long before = registry.passes;
	int rc;

	(void)obj; (void)method; (void)msg;
	if (pending) {
		/* This pass covers whatever the pending one would have seen. */
		uloop_timeout_cancel(&settle_timer);
		pending = false;
	}
	discovery_pass("rescan request");
	mm_schedule(0);
	json_object_object_add(o, "passes", json_object_new_int64((long long)(registry.passes - before)));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* ---- control backends: QModem and ModemManager inhibition ---- */

static const char *status_of(json_object *r);

/* QModem's UCI package, read again only when its file changed. Borrowed reference. */
/*
 * A replaced image is freed on the main loop's next turn, not at once: callers keep section names from it on their
 * stack across calls that may read the image again (served(), qmodem_foreign_port, another record's section), and
 * QModem's hotplug rewrites /etc/config/qmodem on USB add and remove (bench 2026-10-22, r82: SIGSEGV on a replug).
 */
static json_object *qmodem_img_old;
static struct uloop_timeout qmodem_img_reap;

static void qmodem_img_reaped(struct uloop_timeout *t)
{
	(void)t;
	json_object_put(qmodem_img_old);
	qmodem_img_old = NULL;
}

static void qmodem_img_retire(void)
{
	if (!qmodem_img)
		return;
	if (!qmodem_img_old)
		qmodem_img_old = json_object_new_array();
	json_object_array_add(qmodem_img_old, qmodem_img);	/* the array takes its reference */
	qmodem_img = NULL;
	qmodem_img_reap.cb = qmodem_img_reaped;
	uloop_timeout_set(&qmodem_img_reap, 0);
}

static json_object *qmodem_image(void)
{
	static const char *const pk[] = { "qmodem", NULL };
	char path[300], err[160];
	struct stat st;

	snprintf(path, sizeof(path), "%s/qmodem", conf_dir);
	if (stat(path, &st)) {
		qmodem_img_retire();
		memset(&qmodem_mtime, 0, sizeof(qmodem_mtime));
		return NULL;
	}
	if (!qmodem_img || st.st_mtim.tv_sec != qmodem_mtime.tv_sec || st.st_mtim.tv_nsec != qmodem_mtime.tv_nsec) {
		json_object *all = mm_uci_read(conf_dir, pk, err, sizeof(err));
		qmodem_img_retire();
		qmodem_img = all ? json_object_get(json_object_object_get(all, "qmodem")) : NULL;
		json_object_put(all);
		qmodem_mtime = st.st_mtim;
	}
	return qmodem_img;
}

bool qmodem_installed(void)
{
	char path[300];

	snprintf(path, sizeof(path), "%s/modem_scan.sh", qmodem_dir);
	return access(path, X_OK) == 0;
}

/* QModem's modem_ctrl.sh (r95: the SMS of a modem QModem holds go through it) */
const char *qmodem_ctrl_script(char *buf, size_t size)
{
	snprintf(buf, size, "%s/modem_ctrl.sh", qmodem_dir);
	return buf;
}

/* QModem's section for this record, or NULL (borrowed from the image) */
const char *qmodem_section_of(json_object *rec)
{
	json_object *wrap = json_object_new_object();
	const char *sec;

	json_object_object_add(wrap, "qmodem", json_object_get(qmodem_image()));
	sec = mm_netcfg_qmodem_section(wrap, rec);
	json_object_put(wrap);
	/* the name lives on in the cached image */
	if (sec && qmodem_img) {
		json_object_object_foreach(qmodem_img, k, v) {
			(void)v;
			if (!strcmp(k, sec))
				return k;
		}
	}
	return NULL;
}

/*
 * QModem's AT port for this modem is one of its own ports (bench 2026-10-19: the FM350's section 3_1_3, written when
 * it had ttyUSB ports, still named /dev/ttyUSB5 after it came back as ttyACM; that node was the T99's by then, and
 * every radio read through QModem's modem_ctrl.sh ran tom_modem on the T99). NULL when it is, else the node.
 */
const char *qmodem_foreign_port(json_object *rec, const char *sec)
{
	json_object *qs = sec && qmodem_img ? json_object_object_get(qmodem_img, sec) : NULL;
	json_object *tty = json_object_object_get(json_object_object_get(rec, "ports"), "tty");
	const char *at = jstr(qs, "at_port"), *b;
	size_t i;

	if (!at || !*at)
		return NULL;
	b = strrchr(at, '/');
	b = b ? b + 1 : at;
	for (i = 0; json_object_is_type(tty, json_type_array) && i < json_object_array_length(tty); i++) {
		const char *p = json_object_get_string(json_object_array_get_idx(tty, i)), *pb = p ? strrchr(p, '/') : NULL;

		if (p && !strcmp(pb ? pb + 1 : p, b))
			return NULL;
	}
	return at;
}

const char *ledger_backend(const char *id)
{
	json_object *led = json_object_object_get(json_object_object_get(cfg.ledger, "devices"), id), *b = NULL;

	return json_object_object_get_ex(led, "backend", &b) ? json_object_get_string(b) : NULL;
}

static void inhibit_spawn(struct inhibit *h);

static void inhibit_restart(struct uloop_timeout *t)
{
	struct inhibit *h = container_of(t, struct inhibit, restart);

	if (h->wanted && !h->proc.pid)
		inhibit_spawn(h);
}

/*
 * OpenWrt's ModemManager runs without udev and learns about ports only from
 * the hotplug scripts' kernel event reports, which come when the modem is
 * plugged. After an inhibition ends nothing reports them again, and the modem
 * stays nobody's (bench 2026-10-02): report its ports the way those scripts
 * do. Detached, so it also runs when the engine itself is stopping.
 */
/*
 * R3 at boot: the tty hotplug hook withdraws a control-only modem's serial ports, but when ModemManager is
 * still starting the withdrawal is refused ("device tty/ttyUSB8 not found") and the add it cached is replayed
 * once it runs, so it probes the AT port anyway (bench 2026-10-09: ttyUSB9 became the T99's primary port).
 * When a ModemManager read shows such a modem holding serial ports, they are withdrawn here — once per
 * ModemManager object, and never while a session is up or coming up: that would cost the connection.
 */
static json_object *mm_withdrawn;	/* device_id -> the ModemManager object already handled */

static void mm_withdraw_ttys(const char *id, json_object *mm)
{
	json_object *ports = json_object_object_get(mm, "ports"), *rec = mm_registry_find(&registry, id), *srv;
	const char *obj = jstr(mm, "object"), *state = jstr(mm, "state"), *mp;
	char names[8][48];
	int n = 0, fd;
	pid_t pid;

	if (!rec || !obj || !ports || !strcmp(mmcli_path, "none"))
		return;
	/* this ModemManager object was seen already: nothing to do (checked first, it runs on every ModemManager read);
	 * one left alone while connected is tried again once its session is down (bench 2026-10-26: the T77 kept its
	 * AT port with ModemManager all day, so the page had no AT tab) */
	if (mm_withdrawn && jstr(json_object_object_get(mm_withdrawn, id), "object") &&
	    !strcmp(jstr(json_object_object_get(mm_withdrawn, id), "object"), obj) &&
	    (!json_object_get_boolean(json_object_object_get(json_object_object_get(mm_withdrawn, id), "waiting")) ||
	     (state && (!strcmp(state, "connected") || !strcmp(state, "connecting") || !strcmp(state, "disconnecting")))))
		return;
	/* only the profile is wanted: its memoized decision, not a whole served() (audit 2026-10-20) */
	srv = profile_from_mm(rec, mm);
	mp = jstr(srv, "mm_ports");
	if (!mp || strcmp(mp, "control")) {
		json_object_put(srv);
		return;
	}
	json_object_put(srv);
	for (size_t i = 0; i < json_object_array_length(ports) && n < 8; i++) {
		const char *p = json_object_get_string(json_object_array_get_idx(ports, i));
		if (p && (!strncmp(p, "ttyUSB", 6) || !strncmp(p, "ttyACM", 6)) && !strpbrk(p, ",=/ "))
			snprintf(names[n++], sizeof(names[0]), "%s", p);
	}
	if (!n)
		return;
	if (!mm_withdrawn)
		mm_withdrawn = json_object_new_object();
	{
		bool up = state && (!strcmp(state, "connected") || !strcmp(state, "connecting") || !strcmp(state, "disconnecting"));
		json_object *note = json_object_new_object();

		json_object_object_add(note, "object", json_object_new_string(obj));
		if (up)
			json_object_object_add(note, "waiting", json_object_new_boolean(1));
		json_object_object_add(mm_withdrawn, id, note);
		if (up) {
			ULOG_WARN("ModemManager holds %d serial port%s of %s (its event cache replayed them at start); left as is while "
				  "the session is up, withdrawn when it is down\n", n, n == 1 ? "" : "s", id);
			return;
		}
	}
	ULOG_WARN("ModemManager took %d serial port%s of %s (its event cache replayed them at start); withdrawing them\n",
		  n, n == 1 ? "" : "s", id);
	mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "mm-ports-withdrawn", names[0]);
	history_dirty = true;
	pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		if (fork() != 0)
			_exit(0);	/* the grandchild works; the engine reaps only the child */
		setsid();
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		for (int i = 0; i < n; i++) {
			char arg[160];
			pid_t c = fork();

			if (c == 0) {
				snprintf(arg, sizeof(arg), "--report-kernel-event=action=remove,name=%s,subsystem=tty", names[i]);
				execlp(mmcli_path, mmcli_path, arg, (char *)NULL);
				_exit(127);
			}
			if (c > 0)
				waitpid(c, NULL, 0);
		}
		_exit(0);
	}
	waitpid(pid, NULL, 0);
}

static void mm_report_ports_ex(const char *id, bool forget);

static void mm_report_ports(const char *id)
{
	mm_report_ports_ex(id, false);
}

/* mgr_foreign.c: a modem left to an interface set up outside MedveModem: ModemManager stops probing it */
void mm_ports_withdraw(const char *id)
{
	mm_report_ports_ex(id, true);
}

/* ... and handed over by the operator: told again, the count of the reports started afresh */
void mm_ports_again(const char *id)
{
	if (mm_absent)
		json_object_object_del(mm_absent, id);
	mm_report_ports(id);
}

/* forget: only the removes, every kind of port: ModemManager stops probing a modem it may not have */
static void mm_report_ports_ex(const char *id, bool forget)
{
	static const char *const kinds[][2] = { { "tty", "tty" }, { "control", "usbmisc" }, { "net", "net" } };
	json_object *rec = id ? mm_registry_find(&registry, id) : NULL, *ports = json_object_object_get(rec, "ports");
	char names[24][48], subs[24][16], paths[24][PATH_MAX];
	bool common = access(MM_COMMON_SH, R_OK) == 0, control_only;
	int n = 0, k, i, fd;
	pid_t pid;

	if (!ports || !strcmp(mmcli_path, "none"))
		return;
	{
		json_object *srv = served(rec);
		const char *mp = jstr(json_object_object_get(srv, "profile"), "mm_ports");

		control_only = mp && !strcmp(mp, "control");
		json_object_put(srv);
	}
	for (k = control_only && !forget ? 1 : 0; k < 3; k++) {	/* R3: a control-only modem's serial ports stay ours */
		json_object *list = json_object_object_get(ports, kinds[k][0]);
		size_t j;

		for (j = 0; list && j < json_object_array_length(list) && n < 24; j++) {
			const char *node = json_object_get_string(json_object_array_get_idx(list, j)), *name;

			if (!node)
				continue;
			name = strncmp(node, "/dev/", 5) ? node : node + 5;
			if (!*name || strpbrk(name, ",=/ "))
				continue;
			snprintf(names[n], sizeof(names[0]), "%s", name);
			snprintf(subs[n], sizeof(subs[0]), "%s", kinds[k][1]);
			{
				char link[PATH_MAX];

				snprintf(link, sizeof(link), "/sys/class/%s/%s", kinds[k][1], name);
				if (!realpath(link, paths[n]))
					snprintf(paths[n], sizeof(paths[0]), "%s", link);
			}
			n++;
		}
	}
	if (!n)
		return;
	if (forget) {
		ULOG_INFO("withdrawing %d port%s of %s from ModemManager\n", n, n == 1 ? "" : "s", id);
	} else {
		ULOG_INFO("reporting %d port%s of %s to ModemManager\n", n, n == 1 ? "" : "s", id);
		if (!mm_reported)
			mm_reported = json_object_new_object();
		json_object_object_add(mm_reported, id, json_object_new_int64(now_mono_ms()));
	}
	pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		if (fork() != 0)
			_exit(0);	/* the grandchild works; the engine reaps only the child */
		setsid();
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		sleep(1);	/* let the inhibition's teardown finish first */
		/* a remove first: an add for a port whose context ModemManager is still probing is dropped
		 * ("port context already being processed", bench 2026-10-07); a remove ends that context */
		for (int a = 0; a < (forget ? 1 : 2); a++)
			for (i = 0; i < n; i++) {
				const char *action = a ? "add" : "remove";
				char arg[160];
				pid_t c = fork();

				if (c == 0) {
					/* MM's own hotplug helper also keeps its event cache, which a restarted MM replays */
					if (common)
						execl("/bin/sh", "sh", "-c", ". \"$0\" && mm_report_event \"$4\" \"$1\" \"$2\" \"$3\"",
						      MM_COMMON_SH, names[i], subs[i], paths[i], action, (char *)NULL);
					snprintf(arg, sizeof(arg), "--report-kernel-event=action=%s,name=%s,subsystem=%s",
						 action, names[i], subs[i]);
					execlp(mmcli_path, mmcli_path, arg, (char *)NULL);
					_exit(127);
				}
				if (c > 0)
					waitpid(c, NULL, 0);
			}
		_exit(0);
	}
	waitpid(pid, NULL, 0);
}

static void inhibit_exited(struct uloop_process *p, int ret)
{
	struct inhibit *h = container_of(p, struct inhibit, proc);
	int delay = 5000;

	h->proc.pid = 0;
	if (h->wanted) {
		/* a quick exit is a refusal (ModemManager does not hold the modem): back off */
		h->failures = now_mono_ms() - h->spawned < 3000 ? h->failures + 1 : 0;
		for (int i = 1; i < h->failures && delay < 300000; i++)
			delay *= 2;
		if (delay > 300000)
			delay = 300000;
		/*
		 * bench 2026-10-19 (FM350 handed to QModem): "couldn't inhibit device: Modem not exported in the bus"
		 * every few seconds while ModemManager kept probing the modem and building objects for it. ModemManager
		 * refuses to inhibit a modem it has not exported, and probes it meanwhile: the second refusal withdraws
		 * its ports from ModemManager (once), and the retries say so once, then only at the longest wait.
		 */
		if (h->failures >= 2 && !h->forgotten && !(mm_devices && json_object_object_get(mm_devices, h->device_id))) {
			h->forgotten = true;
			mm_report_ports_ex(h->device_id, true);
		}
		/*
		 * bench 2026-10-20 (FM350 under QModem, r68): with its ports withdrawn ModemManager answers NotFound, and
		 * the retries went on forever at the longest wait. A modem ModemManager does not see needs no inhibition:
		 * the next ModemManager read showing it (inhibit_want) inhibits it again.
		 */
		if (h->failures >= 3 && h->forgotten && !(mm_devices && json_object_object_get(mm_devices, h->device_id))) {
			if (!h->parked)
				ULOG_INFO("ModemManager does not see %s (its ports are withdrawn); no inhibition is needed until it does\n",
					  h->device_id);
			h->parked = true;
			return;
		}
		if (h->failures <= 1)
			ULOG_WARN("ModemManager inhibition of %s ended (status %d); retrying in %d s\n", h->device_id, ret,
				  delay / 1000);
		else if (h->failures == 2 || delay >= 300000)
			ULOG_INFO("ModemManager refuses to inhibit %s (it has not exported the modem); retrying in %d s%s\n",
				  h->device_id, delay / 1000, h->failures == 2 ? ", said once" : "");
		h->restart.cb = inhibit_restart;
		uloop_timeout_set(&h->restart, delay);
	} else {
		h->used = false;
		mm_report_ports(h->device_id);	/* hand the modem back */
		/* ModemManager needs some seconds to probe it again: look then, and later */
		mm_followups = 2;
		mm_schedule(5000);
	}
}

/* `mmcli --inhibit-device=UID` holds ModemManager off the device while it runs */
static void inhibit_spawn(struct inhibit *h)
{
	char arg[300];
	pid_t pid;
	int fd;

	snprintf(arg, sizeof(arg), "--inhibit-device=%s", h->path);
	pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		prctl(PR_SET_PDEATHSIG, SIGTERM);	/* never outlive the engine */
		setpgid(0, 0);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execlp(mmcli_path, mmcli_path, arg, (char *)NULL);
		_exit(127);
	}
	h->proc.pid = pid;
	h->spawned = now_mono_ms();
	h->proc.cb = inhibit_exited;
	uloop_process_add(&h->proc);
	ULOG_INFO("ModemManager inhibited for %s (%s)\n", h->device_id, h->path);
}

/* the device path ModemManager knows the modem by */
static bool mm_uid_of(json_object *rec, const char *id, char *out, size_t size)
{
	json_object *mm = mm_devices ? json_object_object_get(mm_devices, id) : NULL;
	const char *att = jstr(rec, "attachment_id");
	char link[300], real[PATH_MAX];

	if (jstr(mm, "device")) {
		snprintf(out, size, "%s", jstr(mm, "device"));
		return true;
	}
	if (!att || strncmp(att, "usb:", 4))
		return false;
	snprintf(link, sizeof(link), "%s/%s", SYS_USB, att + 4);
	if (!realpath(link, real) || strlen(real) >= size)
		return false;	/* a cut path would inhibit some other device */
	memcpy(out, real, strlen(real) + 1);
	return true;
}

/*
 * Every present modem whose backend is native or QModem is inhibited while
 * ModemManager runs; everything else is not.
 */
/* one modem ModemManager is kept off */
static void inhibit_want(const char *id)
{
	json_object *rec = mm_registry_find(&registry, id);
	struct inhibit *h = NULL;
	char path[256];
	int i;

	if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present")))
		return;
	/* an inhibition keeps its device path: once ModemManager let go it no longer
	 * reports one; only ModemManager showing the modem at another path replaces it */
	for (i = 0; i < MM_INHIBITS && !h; i++)
		if (inhibits[i].used && !strcmp(inhibits[i].device_id, id))
			h = &inhibits[i];
	if (h && mm_devices && json_object_object_get(mm_devices, id) && mm_uid_of(rec, id, path, sizeof(path)) &&
	    strcmp(path, h->path)) {
		ULOG_INFO("%s is at %s now; inhibiting there\n", id, path);
		if (h->proc.pid > 0)
			kill(h->proc.pid, SIGTERM);
		h->wanted = false;
		h = NULL;
	}
	if (!h && !mm_uid_of(rec, id, path, sizeof(path)))
		return;
	for (i = 0; i < MM_INHIBITS && !h; i++)
		if (!inhibits[i].used) {
			h = &inhibits[i];
			memset(h, 0, sizeof(*h));
			h->used = true;
			snprintf(h->device_id, sizeof(h->device_id), "%s", id);
			snprintf(h->path, sizeof(h->path), "%s", path);
		}
	if (!h)
		return;
	h->wanted = true;
	if (h->parked) {
		if (!(mm_devices && json_object_object_get(mm_devices, id)))
			return;
		ULOG_INFO("ModemManager shows %s again; inhibiting it\n", id);
		h->parked = false;
		h->failures = 0;
	}
	/* a refusal's backoff (inhibit_exited) is waited out: every ModemManager read and discovery pass comes here, and
	 * spawning at once made the 5..300 s wait void (audit 2026-10-20: mmcli every few seconds) */
	if (!h->proc.pid && !h->restart.pending)
		inhibit_spawn(h);
}

static void inhibits_sync(void)
{
	json_object *devs = json_object_object_get(cfg.ledger, "devices");
	int i;

	for (i = 0; i < MM_INHIBITS; i++)
		inhibits[i].wanted = false;
	if (!strcmp(mm_backend, "available") && strcmp(mmcli_path, "none")) {
		if (devs) {
			json_object_object_foreach(devs, id, led) {
				const char *b = jstr(led, "backend");

				if (id[0] == '@' || !b || (strcmp(b, "qmodem") && strcmp(b, "native")) || mm_returning(id) || frozen(id))
					continue;
				inhibit_want(id);
			}
		}
		/* a frozen modem whatever its backend; its ports out of ModemManager's event cache once per attachment of
		 * this engine's run (after a reboot the boot's hotplug put them back), so a restarted ModemManager does not
		 * take it again (the owner, 2026-10-21) */
		if (frozen_map) {
			json_object_object_foreach(frozen_map, fid, fv) {
				json_object *rec = fid[0] != '@' ? mm_registry_find(&registry, fid) : NULL;
				long long gen;

				(void)fv;
				if (!rec)
					continue;
				inhibit_want(fid);
				gen = json_object_get_int64(json_object_object_get(rec, "generation"));
				if (json_object_get_boolean(json_object_object_get(rec, "present")) &&
				    json_object_get_int64(json_object_object_get(frozen_withdrawn, fid)) != gen) {
					if (!frozen_withdrawn)
						frozen_withdrawn = json_object_new_object();
					json_object_object_add(frozen_withdrawn, fid, json_object_new_int64(gen));
					mm_report_ports_ex(fid, true);
				}
			}
		}
	}
	for (i = 0; i < MM_INHIBITS; i++) {
		struct inhibit *h = &inhibits[i];
		if (!h->used || h->wanted)
			continue;
		uloop_timeout_cancel(&h->restart);
		if (h->proc.pid > 0) {
			ULOG_INFO("ModemManager inhibition of %s released\n", h->device_id);
			kill(h->proc.pid, SIGTERM);	/* inhibit_exited frees the slot */
		} else {
			h->used = false;
		}
	}
}

static bool inhibited(const char *id)
{
	int i;

	for (i = 0; i < MM_INHIBITS; i++)
		if (inhibits[i].used && inhibits[i].wanted && !strcmp(inhibits[i].device_id, id) && inhibits[i].proc.pid > 0)
			return true;
	return false;
}

static void hook_exited(struct uloop_process *p, int ret)
{
	void (*done)(int) = hook.done;

	(void)p;
	uloop_timeout_cancel(&hook.kill);
	hook.running = false;
	hook.done = NULL;
	if (done)
		done(ret);
}

static void hook_timeout(struct uloop_timeout *t)
{
	(void)t;
	if (hook.running && hook.proc.pid > 0)
		kill(-hook.proc.pid, SIGKILL);
}

/* run one helper (QModem's modem_scan.sh) with a hard limit; false when busy or failed */
static bool hook_run(const char *const argv[], void (*done)(int))
{
	pid_t pid;
	int fd;

	if (hook.running)
		return false;
	if (argv[1] && !strncmp(argv[1], "qmodem", 6))
		procs_expect("qmodem");	/* its service may be reloaded: not a crash (mgr_procs.c) */
	pid = fork();
	if (pid < 0)
		return false;
	if (pid == 0) {
		setpgid(0, 0);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execv(argv[0], (char *const *)argv);
		_exit(127);
	}
	hook.running = true;
	hook.done = done;
	hook.proc.pid = pid;
	hook.proc.cb = hook_exited;
	uloop_process_add(&hook.proc);
	hook.kill.cb = hook_timeout;
	uloop_timeout_set(&hook.kill, MM_HOOK_TIMEOUT_MS);
	return true;
}

static json_object *qmodem_global(void *arg, json_object *current, json_object *ledgers)
{
	json_object *g = json_object_new_object(), *d = json_object_new_object(), *opts = json_object_new_array(), *o;

	(void)arg; (void)current; (void)ledgers;
	o = json_object_new_object();
	json_object_object_add(o, "package", json_object_new_string("qmodem"));
	json_object_object_add(o, "section", json_object_new_string("main"));
	json_object_object_add(o, "option", json_object_new_string("block_auto_probe"));
	json_object_object_add(o, "value", json_object_new_string("1"));
	json_object_array_add(opts, o);
	json_object_object_add(d, "options", opts);
	json_object_object_add(d, "sections", json_object_new_object());
	json_object_object_add(d, "list_items", json_object_new_array());
	json_object_object_add(d, "backend", json_object_new_string("qmodem-global"));
	json_object_object_add(g, "action", json_object_new_string("configure"));
	json_object_object_add(g, "reason", json_object_new_string("QModem gets only the modems it is handed"));
	json_object_object_add(g, "desired", d);
	return g;
}

/* QModem's own auto-probe stays off while any modem is handed to it */
static void qmodem_global_sync(void)
{
	json_object *devs = json_object_object_get(cfg.ledger, "devices"), *r;
	bool any = false, have = devs && json_object_object_get(devs, "@qmodem");

	if (devs) {
		json_object_object_foreach(devs, id, led)
			if (id[0] != '@' && jstr(led, "backend") && !strcmp(jstr(led, "backend"), "qmodem"))
				any = true;
	}
	if (any == have)
		return;
	r = mm_cfgtxn_run(&cfg, "@qmodem", any ? qmodem_global : NULL, NULL, false, now_mono_ms());
	ULOG_INFO("QModem auto-probe %s: %s\n", any ? "switched off" : "restored", status_of(r));
	json_object_put(r);
}

/*
 * QModem's sections of modems it was not handed (field 2026-10-13). Our
 * block_auto_probe also switches off what QModem 3.3.0 does by itself at boot
 * and on a USB remove: disable the section of a modem no longer at its path.
 * A section left enabled is dialled at boot through its old AT node, which may
 * be another modem's DIAG port by then (an Orange Pi Zero 3 numbers its USB
 * buses anew at every boot; a FM350 section's ttyUSB1 was a T99's, its dialer
 * hung in the kernel there). So while the engine hands modems to QModem, every
 * enabled modem-device section that is not one of them is disabled through the
 * reload hook ("qmodem-stale <section>...": state disabled, qmodem_network
 * reloaded, which stops its dialer). QModem enables a section again when it is
 * handed the modem.
 */
#define QSTALE_RETRY_MS 60000
static struct uloop_process qstale_proc;
static bool qstale_running;
static struct uloop_timeout qstale_kill;	/* a hook that hangs is stopped: qstale_running must not stay true for good */

static void qstale_kill_fired(struct uloop_timeout *t)
{
	(void)t;
	if (qstale_proc.pid > 0)
		kill(-qstale_proc.pid, SIGKILL);
}
static long long qstale_at = -QSTALE_RETRY_MS;

static void qstale_exited(struct uloop_process *p, int ret)
{
	(void)p;
	qstale_running = false;
	qstale_proc.pid = 0;
	uloop_timeout_cancel(&qstale_kill);
	if (!WIFEXITED(ret) || WEXITSTATUS(ret))
		ULOG_WARN("disabling stale QModem sections failed (status %d)\n", ret);
}

/*
 * QModem's section of a modem handed to it names an AT port that is not this modem's (bench 2026-10-20: the FM350's
 * 2_1_3 kept a ttyUSB number another modem had taken after the sticks were moved; the engine rightly kept QModem off
 * that port, so QModem's dial had no instance, «qmodem_network» looked stopped, and «Исправить» changed nothing).
 * QModem's own boot scan would have rewritten it, but block_auto_probe keeps that off: the engine writes the modem's
 * own AT port into the section and QModem's dial starts again. Once per section and port; a look a minute.
 */
static bool uci_plain_name(const char *s);
static long long qport_at;
static json_object *qport_done;	/* "<section>|<node>" -> true */

static void qport_exited(int status)
{
	if (!WIFEXITED(status) || WEXITSTATUS(status))
		ULOG_WARN("QModem's AT port could not be written (status %d)\n", status);
}

static void qmodem_port_pass(void);

/*
 * The section names a pass reads (qmodem_section_of, qmodem_foreign_port) point into the QModem image, and served()
 * inside the pass re-reads /etc/config/qmodem when its mtime changed — QModem's hotplug rewrites it on USB add and
 * remove — and frees the old image: the names were read freed (bench 2026-10-22, r82: SIGSEGV on a replug). The pass
 * holds the image it started with.
 */
static void qmodem_port_sync(void)
{
	json_object *hold = json_object_get(qmodem_image());

	qmodem_port_pass();
	json_object_put(hold);
}

static void qmodem_port_pass(void)
{
	if (!registry.devices || !strcmp(reload_hook, "none") || !qmodem_installed() || hook.running ||
	    now_mono_ms() - qport_at < (uplink_override_ms ? 5 * uplink_override_ms : 60000))
		return;
	if (!qport_done)
		qport_done = json_object_new_object();
	json_object_object_foreach(registry.devices, id, rec) {
		const char *choice = jstr(decision_of(id), "backend"), *sec, *foreign, *node;
		json_object *srv;
		char key[200], detail[240];

		/* QModem drives it: the engine handed it over, the operator chose QModem, or (choice automatic) QModem's own
		 * scan has a section for it (field 2026-10-20: the FM350 left on «auto» was not repaired, picked by hand it
		 * was); a modem the operator gives to another backend is not QModem's to fix */
		if (choice && *choice && strcmp(choice, "auto") && strcmp(choice, "qmodem"))
			continue;
		if (!json_object_get_boolean(json_object_object_get(rec, "present")) || frozen(id) ||
		    !(sec = qmodem_section_of(rec)) || !uci_plain_name(sec) || !(foreign = qmodem_foreign_port(rec, sec)))
			continue;
		srv = served(rec);
		node = jstr(json_object_object_get(json_object_object_get(srv, "port_roles"), "at"), "node");
		if (!node || strncmp(node, "/dev/tty", 8) || node[strspn(node, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/_.-")]) {
			json_object_put(srv);
			continue;	/* its own AT port is not known (yet): the conflict stays said, nothing written */
		}
		snprintf(key, sizeof(key), "%s|%s", sec, node);
		if (json_object_object_get(qport_done, key)) {
			json_object_put(srv);
			continue;
		}
		qport_at = now_mono_ms();	/* a minute between attempts, not between looks */
		{
			const char *argv[] = { reload_hook, "qmodem-port", sec, node, NULL };

			if (!hook_run(argv, qport_exited)) {
				json_object_put(srv);
				return;
			}
		}
		json_object_object_add(qport_done, key, json_object_new_boolean(1));
		snprintf(detail, sizeof(detail), "QModem's section %s named %s, another modem's port: now %s, QModem's dial started again",
			 sec, foreign, node);
		ULOG_WARN("%s: %s\n", id, detail);
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "qmodem-port", detail);
		history_dirty = true;
		json_object_put(srv);
		return;	/* one hook at a time */
	}
}

/* a handed modem gone from USB this long is QModem's no more: until then its section and interfaces stay (the owner's
 * FM350, 2026-10-21: the engine's own USB reset sent it through its bootloader for a minute, and the stale pass in that
 * minute switched its section off and dropped its interfaces, so nobody dialled when it came back) */
#define QMODEM_AWAY_S 600

static bool qmodem_handed_section(const char *sec)
{
	if (!registry.devices)
		return false;
	json_object_object_foreach(registry.devices, id, rec) {
		const char *b = ledger_backend(id), *q;

		if (!b || strcmp(b, "qmodem"))
			continue;
		if (!json_object_get_boolean(json_object_object_get(rec, "present")) &&
		    (long long)time(NULL) - json_object_get_int64(json_object_object_get(rec, "last_change")) >= QMODEM_AWAY_S)
			continue;
		if ((q = qmodem_section_of(rec)) && !strcmp(q, sec))
			return true;
	}
	return job.active && job.qmodem && job.qsection[0] && !strcmp(job.qsection, sec);
}

/* the modem a QModem section names (option path ".../3-1.3/") is on USB now */
static bool qmodem_slot_present(json_object *qs)
{
	const char *path = jstr(qs, "path");
	char slot[64], att[72];
	size_t n;

	if (!path || !registry.devices)
		return false;
	snprintf(slot, sizeof(slot), "%s", path);
	while ((n = strlen(slot)) && slot[n - 1] == '/')
		slot[n - 1] = 0;
	snprintf(att, sizeof(att), "usb:%s", strrchr(slot, '/') ? strrchr(slot, '/') + 1 : slot);
	json_object_object_foreach(registry.devices, id, rec) {
		(void)id;
		if (json_object_get_boolean(json_object_object_get(rec, "present")) && jstr(rec, "attachment_id") &&
		    !strcmp(jstr(rec, "attachment_id"), att))
			return true;
	}
	return false;
}

static bool uci_plain_name(const char *s)
{
	const char *p;

	for (p = s; *p && (isalnum((unsigned char)*p) || *p == '_'); p++)
		;
	return !*p && *s;
}

/*
 * QModem's sections and interfaces left behind (field 2026-10-13, 2026-10-15). With block_auto_probe
 * (ours) QModem's own clean-ups do not run: its USB-remove only disables the section, and its boot check
 * that deletes the interfaces of a gone path is skipped. So: sections no modem handed to QModem uses are
 * switched off, and QModem's network interfaces (option modem_config) go when their section is gone or
 * off, their USB device is gone, or they are an old name of a modem now handed under ours (3_1_3 beside
 * mm1). A modem QModem runs by itself, present and on, keeps its interfaces as they are.
 */
static void qmodem_stale_sync(void)
{
	static const char *const npk[] = { "network", NULL };
	json_object *devs = json_object_object_get(cfg.ledger, "devices"), *img, *all;
	const char *argv[48];
	static char on_again[8][80];	/* «+<section>»: argv points here */
	int n_on = 0;
	char err[160];
	int n = 0, k, first_if;
	pid_t pid;

	static struct timespec seen;	/* QModem's file as the last pass saw it */

	if (qstale_running || !strcmp(reload_hook, "none") || !devs || !json_object_object_get(devs, "@qmodem") ||
	    !qmodem_installed() || !(img = qmodem_image()))
		return;
	/* once a minute, and at once when QModem's file changed: its USB-remove switching a handed modem's section off is
	 * undone in seconds, not after the dial was down a minute (the owner's FM350, 2026-10-21) */
	if (now_mono_ms() - qstale_at < (uplink_override_ms ? 5 * uplink_override_ms : QSTALE_RETRY_MS) &&
	    seen.tv_sec == qmodem_mtime.tv_sec && seen.tv_nsec == qmodem_mtime.tv_nsec)
		return;
	seen = qmodem_mtime;
	qstale_at = now_mono_ms();
	/* held for the pass: qmodem_handed_section() reads the image again when QModem rewrote its file meanwhile,
	 * which freed the one walked here, its section names in argv with it (valgrind, 2026-10-17) */
	json_object_get(img);
	argv[n++] = reload_hook;
	argv[n++] = "qmodem-stale";
	json_object_object_foreach(img, sec, val) {
		/* a modem handed to QModem, on USB, its section off: QModem's USB-remove switched it off when the stick came
		 * back after a reset, and its add is off under block_auto_probe (ours) — on again, or nobody dials it */
		if (n < 18 && n_on < 8 && !strcmp(jstr(val, ".type") ? jstr(val, ".type") : "", "modem-device") &&
		    jstr(val, "state") && !strcmp(jstr(val, "state"), "disabled") && qmodem_handed_section(sec) &&
		    qmodem_slot_present(val) && uci_plain_name(sec) && strlen(sec) < sizeof(on_again[0]) - 1) {
			snprintf(on_again[n_on], sizeof(on_again[0]), "+%s", sec);
			argv[n++] = on_again[n_on++];
			continue;
		}
		/* a section whose modem is on USB now is QModem's own (or ours): only one at a path gone is switched off */
		if (n >= 18 || strcmp(jstr(val, ".type") ? jstr(val, ".type") : "", "modem-device") ||
		    (jstr(val, "state") && !strcmp(jstr(val, "state"), "disabled")) || qmodem_handed_section(sec) ||
		    qmodem_slot_present(val))
			continue;
		if (!uci_plain_name(sec))
			continue;	/* a name uci would not take as it is */
		argv[n++] = sec;
	}
	argv[n++] = "--";
	first_if = n;
	if ((all = mm_uci_read(conf_dir, npk, err, sizeof(err)))) {
		json_object_object_foreach(json_object_object_get(all, "network"), iface, v) {
			const char *mc = jstr(v, "modem_config");
			json_object *qs = mc ? json_object_object_get(img, mc) : NULL;
			bool drop = false;

			if (!mc || strcmp(jstr(v, ".type") ? jstr(v, ".type") : "", "interface") || n >= 46 || !uci_plain_name(iface))
				continue;
			if (!qs) {
				drop = true;					/* its section is gone */
			} else if (qmodem_handed_section(mc)) {
				/* ours: only what is not its present name (alias, or the section's own) */
				const char *alias = jstr(qs, "alias"), *name = alias && *alias && strcmp(alias, "-") ? alias : mc;
				size_t l = strlen(name);

				drop = strcmp(iface, name) && !(!strncmp(iface, name, l) && !strcmp(iface + l, "v6"));
			} else {
				drop = (jstr(qs, "state") && !strcmp(jstr(qs, "state"), "disabled")) || !qmodem_slot_present(qs);
				for (k = 2; k < first_if - 1 && !drop; k++)
					drop = !strcmp(argv[k], mc);		/* switched off in this pass */
			}
			if (drop)
				argv[n++] = iface;
		}
	}
	if (n == 3) {
		json_object_put(all);
		json_object_put(img);
		return;
	}
	argv[n] = NULL;
	for (k = 2; k < first_if - 1; k++)
		if (argv[k][0] == '+')
			ULOG_WARN("QModem section %s of a modem handed to QModem was off (its USB came back): on again\n", argv[k] + 1);
		else
			ULOG_WARN("QModem section %s is no modem handed to QModem: disabled\n", argv[k]);
	for (k = first_if; k < n; k++)
		ULOG_WARN("QModem's interface %s is left behind (its modem gone, off or renamed): removed\n", argv[k]);
	pid = fork();
	if (pid < 0) {
		json_object_put(all);
		json_object_put(img);
		return;
	}
	if (pid == 0) {
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			if (fd > 2)
				close(fd);
		}
		/* the engine's descriptors (its ubus socket among them) are not the hook's to keep (audit 2026-10-20) */
		setpgid(0, 0);
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execv(reload_hook, (char *const *)argv);
		_exit(127);
	}
	json_object_put(all);		/* the child has its own copy of argv's strings */
	json_object_put(img);
	qstale_running = true;
	qstale_proc.pid = pid;
	qstale_proc.cb = qstale_exited;
	uloop_process_add(&qstale_proc);
	qstale_kill.cb = qstale_kill_fired;
	uloop_timeout_set(&qstale_kill, MM_RELOAD_TIMEOUT_MS);
}

/* ---- configuration transactions (spec 17) ---- */

static void reload_done(const char *result)
{
	json_object *e;

	reload_running = false;
	uloop_timeout_cancel(&reload_kill);
	ULOG_INFO("configuration reload: %s\n", result);
	if (job.active && job.phase == JOB_RELOAD)
		job_after_reload();
	if (!reload_reply)
		return;
	json_object_object_add(reload_reply, "reload", json_object_new_string(result));
	e = json_object_new_object();
	json_object_object_foreach(reload_reply, k, v)
		if (!strcmp(k, "device_id") || !strcmp(k, "txn") || !strcmp(k, "status") || !strcmp(k, "packages") ||
		    !strcmp(k, "reload"))
			json_object_object_add(e, k, json_object_get(v));
	broadcast("medvemodem.config", e);
	json_object_put(e);
	if (reload_deferred) {
		reply_config(reload_ctx, &reload_req, reload_reply);
		ubus_complete_deferred_request(reload_ctx, &reload_req, UBUS_STATUS_OK);
		reload_deferred = false;
	}
	json_object_put(reload_reply);
	reload_reply = NULL;
}

static void reload_exited(struct uloop_process *p, int ret)
{
	char why[64];

	(void)p;
	if (reload_killed)
		snprintf(why, sizeof(why), "timeout");
	else if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0)
		snprintf(why, sizeof(why), "ok");
	else if (WIFEXITED(ret))
		snprintf(why, sizeof(why), "failed (exit %d)", WEXITSTATUS(ret));
	else
		snprintf(why, sizeof(why), "failed (signal %d)", WTERMSIG(ret));
	reload_done(why);
}

static void reload_timeout(struct uloop_timeout *t)
{
	(void)t;
	if (reload_running && reload_proc.pid > 0) {
		reload_killed = true;
		kill(-reload_proc.pid, SIGKILL);
	}
}

/* Run the reload hook for the committed packages; false when there is none to run. */
static bool reload_start(json_object *packages)
{
	const char *argv[8];
	size_t i, n = 0;
	pid_t pid;

	if (!strcmp(reload_hook, "none") || !packages || !json_object_array_length(packages))
		return false;
	argv[n++] = reload_hook;
	for (i = 0; i < json_object_array_length(packages) && n < 7; i++)
		argv[n++] = json_object_get_string(json_object_array_get_idx(packages, i));
	argv[n] = NULL;
	pid = fork();
	if (pid < 0) {
		ULOG_ERR("cannot start the reload hook: %s\n", strerror(errno));
		return false;
	}
	if (pid == 0) {
		int fd = open("/dev/null", O_RDWR);
		setpgid(0, 0);
		if (fd >= 0) {
			dup2(fd, 0);
			if (fd > 2)
				close(fd);
		}
		/* not the engine's ubus socket or other descriptors into `network reload` (audit 2026-10-20) */
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execv(reload_hook, (char *const *)argv);
		_exit(127);
	}
	reload_proc.pid = pid;
	reload_proc.cb = reload_exited;
	uloop_process_add(&reload_proc);
	reload_running = true;
	reload_killed = false;
	reload_kill.cb = reload_timeout;
	uloop_timeout_set(&reload_kill, MM_RELOAD_TIMEOUT_MS);
	return true;
}

static json_object *desire(void *arg, json_object *current, json_object *ledgers)
{
	return mm_netcfg_desired(json_object_object_get(arg, "rec"), current, ledgers, json_object_object_get(arg, "env"));
}

/* what the generator needs to know about this router */
static bool auto_connecting;
static json_object *wired_now(json_object *net);
static json_object *netifd_call(const char *iface, const char *method);

/* a wired uplink is up now (netifd): a modem's new interface goes behind it (the owner's R3S, 2026-10-25) */
static bool wired_up_now(void)
{
	json_object *w = wired_now(NULL);
	bool up = false;

	for (size_t i = 0; !up && i < json_object_array_length(w); i++) {
		json_object *st = netifd_call(jstr(json_object_array_get_idx(w, i), "interface"), "status");

		up = json_object_get_boolean(json_object_object_get(st, "up"));
		json_object_put(st);
	}
	json_object_put(w);
	return up;
}	/* the bring-up in hand is auto_connect_sync's (a backup metric) */

static json_object *desire_arg(json_object *rec)
{
	static const char *const protos[] = { "modemmanager", "qmi", "mbim", "ncm", "dhcp", "medvemodem", NULL };
	json_object *a = json_object_new_object(), *env = json_object_new_object(), *list = json_object_new_array();
	char path[300];
	int i;

	for (i = 0; protos[i]; i++) {
		snprintf(path, sizeof(path), "%s/%s.sh", proto_dir, protos[i]);
		/* dhcp is built into netifd */
		if (!strcmp(protos[i], "dhcp") || access(path, F_OK) == 0)
			json_object_array_add(list, json_object_new_string(protos[i]));
	}
	json_object_object_add(env, "modemmanager", json_object_new_string(mm_backend));
	json_object_object_add(env, "mm_installed", json_object_new_boolean(mm_installed()));
	json_object_object_add(env, "protos", list);
	json_object_object_add(env, "qmodem", json_object_new_boolean(qmodem_installed()));
	if (auto_connecting)
		json_object_object_add(env, "auto_connect", json_object_new_boolean(1));
	if (wired_up_now())
		json_object_object_add(env, "wired_up", json_object_new_boolean(1));
	/* the highest metric of the wired uplinks: a modem's backup metric goes behind it (the owner's R3S, 2026-10-30:
	 * a WAN at metric 100 and a new E171 at the fixed backup 100 — the modem took the default route) */
	{
		json_object *w = wired_now(NULL);
		int top = -1;

		for (size_t i = 0; i < json_object_array_length(w); i++) {
			json_object *m = json_object_object_get(json_object_array_get_idx(w, i), "metric");

			if (m && json_object_get_int(m) > top)
				top = json_object_get_int(m);
		}
		json_object_put(w);
		if (top >= 0)
			json_object_object_add(env, "wired_metric", json_object_new_int(top));
	}
	/* an interface set up by hand the operator handed over just now (mgr_foreign.c): rewritten as with option adopt */
	if (foreign_adopt(jstr(rec, "device_id")))
		json_object_object_add(env, "adopt", json_object_new_string(foreign_adopt(jstr(rec, "device_id"))));
	json_object_object_add(a, "rec", served(rec));
	json_object_object_add(a, "env", env);
	return a;
}

static const char *status_of(json_object *r)
{
	json_object *v = NULL;
	return json_object_object_get_ex(r, "status", &v) ? json_object_get_string(v) : "";
}

/* Answer now, or after the reload of what was committed. */
/* a configuration change in the device's history: the page's journal shows what the engine wrote */
static void config_note(json_object *r, const char *what)
{
	const char *st = status_of(r), *id = jstr(r, "device_id");
	char detail[128];

	if (!id || (strcmp(st, "committed") && strcmp(st, "rolled-back")))
		return;
	snprintf(detail, sizeof(detail), "%s %s %s", what, st,
		 jstr(json_object_object_get(r, "generator"), "interface") ? jstr(json_object_object_get(r, "generator"), "interface") : "");
	mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "config", detail);
	history_dirty = true;
}

/* a configuration answer as served: a copy without the SIM PIN the interface carries (option pincode) */
static json_object *config_served(json_object *r)
{
	json_object *c = NULL;

	if (!r || json_object_deep_copy(r, &c, NULL)) {
		json_object_put(c);
		c = json_object_new_object();
		json_object_object_add(c, "status", json_object_new_string("failed"));
		json_object_object_add(c, "error", json_object_new_string("out of memory"));
		return c;
	}
	mm_netcfg_hide_pin(c);
	return c;
}

static int reply_config(struct ubus_context *ctx, struct ubus_request_data *req, json_object *r)
{
	json_object *c = config_served(r);
	int rc = reply_json(ctx, req, NULL, c);

	json_object_put(c);
	return rc;
}

static int finish(struct ubus_context *ctx, struct ubus_request_data *req, json_object *r)
{
	json_object *ops = NULL, *packages = NULL;
	int rc;

	json_object_object_get_ex(r, "ops", &ops);
	json_object_object_get_ex(r, "packages", &packages);
	if (strcmp(status_of(r), "planned")) {
		qmodem_global_sync();
		inhibits_sync();
	}
	if ((!strcmp(status_of(r), "committed") || !strcmp(status_of(r), "rolled-back")) &&
	    ops && json_object_array_length(ops) && reload_start(packages)) {
		reload_reply = r;
		reload_ctx = ctx;
		ubus_defer_request(ctx, req, &reload_req);
		reload_deferred = true;
		return UBUS_STATUS_OK;
	}
	if (!strcmp(status_of(r), "committed") || !strcmp(status_of(r), "rolled-back"))
		json_object_object_add(r, "reload", json_object_new_string(strcmp(reload_hook, "none") ? "not needed" : "disabled"));
	rc = reply_config(ctx, req, r);
	json_object_put(r);
	return rc;
}

enum { CFG_ID, CFG_RELEASE, __CFG_MAX };
static const struct blobmsg_policy config_policy[] = {
	[CFG_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[CFG_RELEASE] = { .name = "release", .type = BLOBMSG_TYPE_BOOL },
};

/* the device_id behind an id or attachment; a device only the ledger knows keeps its id */
static const char *config_device(struct blob_attr **tb, json_object **rec)
{
	json_object *idv = NULL;
	const char *id = blobmsg_get_string(tb[CFG_ID]);

	*rec = mm_registry_find(&registry, id);
	if (*rec && json_object_object_get_ex(*rec, "device_id", &idv))
		return json_object_get_string(idv);
	return id;
}

static int config_call(struct ubus_context *ctx, struct ubus_request_data *req, struct blob_attr *msg,
		       const char *what)
{
	struct blob_attr *tb[__CFG_MAX];
	json_object *rec, *r, *led;
	const char *id;
	bool release;

	blobmsg_parse(config_policy, __CFG_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[CFG_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	id = config_device(tb, &rec);
	led = mm_cfgtxn_ledger(&cfg, id);
	json_object_put(led);
	if (!rec && !led)
		return UBUS_STATUS_NOT_FOUND;
	release = !strcmp(what, "release") || (tb[CFG_RELEASE] && blobmsg_get_bool(tb[CFG_RELEASE]));
	if (strcmp(what, "plan") && (reload_running || job.active || pending_release.active)) {
		r = json_object_new_object();
		json_object_object_add(r, "device_id", json_object_new_string(id));
		json_object_object_add(r, "status", json_object_new_string("busy"));
		json_object_object_add(r, "reason", json_object_new_string("another configuration change is being applied"));
		return finish(ctx, req, r);
	}
	if (release && strcmp(what, "plan") && ledger_backend(id) && !strcmp(ledger_backend(id), "qmodem") &&
	    rec && qmodem_section_of(rec)) {
		/* QModem lets go first (its section, interface and dialer), then our ledger */
		char script[300], sec[64];
		const char *argv[] = { script, "remove", sec, NULL };

		snprintf(script, sizeof(script), "%s/modem_scan.sh", qmodem_dir);
		snprintf(sec, sizeof(sec), "%s", qmodem_section_of(rec));
		if (hook_run(argv, release_after_qmodem)) {
			pending_release.active = true;
			snprintf(pending_release.device_id, sizeof(pending_release.device_id), "%s", id);
			pending_release.ctx = ctx;
			ubus_defer_request(ctx, req, &pending_release.req);
			return UBUS_STATUS_OK;
		}
	}
	if (!strcmp(what, "rollback")) {
		r = mm_cfgtxn_rollback(&cfg, id, now_mono_ms());
	} else if (release) {
		r = mm_cfgtxn_run(&cfg, id, NULL, NULL, !strcmp(what, "plan"), now_mono_ms());
	} else if (!rec) {
		r = json_object_new_object();
		json_object_object_add(r, "device_id", json_object_new_string(id));
		json_object_object_add(r, "status", json_object_new_string("refused"));
		json_object_object_add(r, "reason", json_object_new_string("the modem is not known to the registry"));
	} else {
		json_object *arg = desire_arg(rec);
		r = mm_cfgtxn_run(&cfg, id, desire, arg, !strcmp(what, "plan"), now_mono_ms());
		json_object_put(arg);
	}
	if (strcmp(what, "plan")) {
		ULOG_WARN("config %s %s: %s\n", what, id, status_of(r));
		config_note(r, what);
	}
	return finish(ctx, req, r);
}

static void release_after_qmodem(int status)
{
	json_object *r = mm_cfgtxn_run(&cfg, pending_release.device_id, NULL, NULL, false, now_mono_ms());
	char d[32];

	snprintf(d, sizeof(d), "exit %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	json_object_object_add(r, "qmodem_remove", json_object_new_string(d));
	ULOG_INFO("config release %s from QModem: %s\n", pending_release.device_id, status_of(r));
	qmodem_global_sync();
	inhibits_sync();
	reply_config(pending_release.ctx, &pending_release.req, r);
	ubus_complete_deferred_request(pending_release.ctx, &pending_release.req, UBUS_STATUS_OK);
	json_object_put(r);
	memset(&pending_release, 0, sizeof(pending_release));
}

static int m_config_plan(struct ubus_context *ctx, struct ubus_object *obj,
			 struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	(void)obj; (void)method;
	return config_call(ctx, req, msg, "plan");
}

static int m_config_apply(struct ubus_context *ctx, struct ubus_object *obj,
			  struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	(void)obj; (void)method;
	return config_call(ctx, req, msg, "apply");
}

static int m_config_release(struct ubus_context *ctx, struct ubus_object *obj,
			    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	(void)obj; (void)method;
	return config_call(ctx, req, msg, "release");
}

static int m_config_rollback(struct ubus_context *ctx, struct ubus_object *obj,
			     struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	(void)obj; (void)method;
	return config_call(ctx, req, msg, "rollback");
}

static int m_config_state(struct ubus_context *ctx, struct ubus_object *obj,
			  struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__CFG_MAX];
	json_object *o = json_object_new_object(), *rec;
	const char *id = NULL;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(config_policy, __CFG_MAX, tb, blob_data(msg), blob_len(msg));
	if (tb[CFG_ID]) {
		id = config_device(tb, &rec);
		json_object_object_add(o, "device_id", json_object_new_string(id));
		json_object_object_add(o, "ledger", mm_cfgtxn_ledger(&cfg, id));
	} else {
		json_object_object_add(o, "devices", json_object_get(json_object_object_get(cfg.ledger, "devices")));
	}
	json_object_object_add(o, "transactions", mm_cfgtxn_log(&cfg, id));
	json_object_object_add(o, "recovered", json_object_get(cfg.recovered));
	json_object_object_add(o, "reload_running", json_object_new_boolean(reload_running));
	rc = reply_config(ctx, req, o);
	json_object_put(o);
	return rc;
}

/* ---- data sessions (spec 16, 19) ---- */

static void ubus_json_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	json_object **out = req->priv;
	char *text;

	(void)type;
	if (!msg || *out)
		return;
	text = blobmsg_format_json(msg, true);
	if (text) {
		*out = json_tokener_parse(text);
		free(text);
	}
}

/* call a netifd interface object; NULL when netifd has no such interface */
/*
 * Audit M07: these calls are synchronous on the engine's own ubus context (2 s each). A netifd that stops answering
 * would stack them, interface after interface, and hold the event loop (timers, pages). After a timeout the engine
 * stops asking for NETIFD_SLOW_MS: a status is answered from the last one netifd gave for that interface, an
 * action (up, down) fails at once and is tried again later by its own timer.
 */
#define NETIFD_SLOW_MS 30000
static long long netifd_slow_until = -1;
static json_object *netifd_last;		/* interface -> its last status */

static json_object *netifd_call(const char *iface, const char *method)
{
	struct ubus_context *ctx = &conn.ctx;
	struct blob_buf args = { 0 };
	json_object *out = NULL;
	bool status = !strcmp(method, "status");
	long long now = now_mono_ms();
	char path[64];
	uint32_t id;
	int rc;

	if (!ubus_up)
		return NULL;
	if (netifd_slow_until >= 0 && now < netifd_slow_until) {
		json_object *last = status && netifd_last ? json_object_object_get(netifd_last, iface) : NULL;

		return last ? deep_copy_or_null(last) : NULL;
	}
	snprintf(path, sizeof(path), "network.interface.%s", iface);
	if (ubus_lookup_id(ctx, path, &id))
		return NULL;
	blob_buf_init(&args, 0);
	rc = ubus_invoke(ctx, id, method, args.head, ubus_json_cb, &out, 2000);
	blob_buf_free(&args);
	if (rc == UBUS_STATUS_TIMEOUT) {
		if (netifd_slow_until < 0 || now >= netifd_slow_until)
			ULOG_WARN("netifd did not answer %s %s in 2 s: not asked for %d s\n", iface, method, NETIFD_SLOW_MS / 1000);
		netifd_slow_until = now_mono_ms() + NETIFD_SLOW_MS;
		json_object_put(out);
		return NULL;
	}
	if (rc && !out)
		return NULL;
	if (status && out) {
		if (!netifd_last)
			netifd_last = json_object_new_object();
		/* a bounded book: the interfaces this engine looks at, cleared when it grows past them */
		if (json_object_object_length(netifd_last) >= 64 && !json_object_object_get(netifd_last, iface)) {
			json_object_put(netifd_last);
			netifd_last = json_object_new_object();
		}
		json_object_object_add(netifd_last, iface, deep_copy_or_null(out));
	}
	return out ? out : json_object_new_object();
}

static json_object *assess(const char *iface)
{
	json_object *main = netifd_call(iface, "status"), *children = json_object_new_object(), *a;
	static const char *const suffix[] = { "_4", "_6", "v6" };	/* v6: QModem's IPv6 twin */
	char child[48];
	size_t i;

	for (i = 0; i < 3; i++) {
		json_object *st;
		snprintf(child, sizeof(child), "%s%s", iface, suffix[i]);
		if ((st = netifd_call(child, "status")))
			json_object_object_add(children, child, st);
	}
	a = mm_session_assess(main, children);
	json_object_put(main);
	json_object_put(children);
	return a;
}

const char *jstr(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_string) ?
		json_object_get_string(v) : NULL;
}

/* record and broadcast a session state change of a device */
static void session_set(const char *id, const char *state, const char *iface, const char *proto,
			const char *reason, json_object *assessment)
{
	json_object *old = json_object_object_get(sessions, id), *s = json_object_new_object();
	const char *prev = jstr(old, "state");
	bool changed = !prev || strcmp(prev, state);

	json_object_object_add(s, "state", json_object_new_string(state));
	if (iface)
		json_object_object_add(s, "interface", json_object_new_string(iface));
	if (proto)
		json_object_object_add(s, "proto", json_object_new_string(proto));
	json_object_object_add(s, "since", changed || !json_object_object_get(old, "since") ?
			       json_object_new_int64((long long)time(NULL)) : json_object_get(json_object_object_get(old, "since")));
	if (reason)
		json_object_object_add(s, "reason", json_object_new_string(reason));
	if (assessment)
		json_object_object_add(s, "netifd", json_object_get(assessment));
	/* netifd's uptime is read only when something changes: keep the moment the interface came up and
	 * serve the uptime from it (it read 0 forever: the reading taken at the connect) */
	if (assessment && json_object_object_get(assessment, "uptime"))
		json_object_object_add(s, "up_at", json_object_new_int64((long long)time(NULL) -
				       json_object_get_int64(json_object_object_get(assessment, "uptime"))));
	else if (!changed && json_object_object_get(old, "up_at"))
		json_object_object_add(s, "up_at", json_object_get(json_object_object_get(old, "up_at")));
	/* its network inside the LAN: looked at when it comes up (mgr_clash.c) */
	if (!strcmp(state, "connected")) {
		json_object *c = changed && assessment ? clash_check(id, assessment) :
				 json_object_get(json_object_object_get(old, "clash"));

		if (c)
			json_object_object_add(s, "clash", c);
	}
	json_object_object_add(sessions, id, s);
	if (!changed)
		return;
	ULOG_INFO("session %s: %s%s%s\n", id, state, reason ? ": " : "", reason ? reason : "");
	if (!strcmp(state, "connected") || !strcmp(state, "disconnected") || !strcmp(state, "failed"))
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(),
			       !strcmp(state, "connected") ? "session-up" : !strcmp(state, "failed") ? "session-failed" : "session-down",
			       iface);
	history_dirty = true;
	{
		json_object *e = json_object_new_object();
		json_object_object_add(e, "device_id", json_object_new_string(id));
		json_object_object_add(e, "state", json_object_new_string(state));
		if (iface)
			json_object_object_add(e, "interface", json_object_new_string(iface));
		if (reason)
			json_object_object_add(e, "reason", json_object_new_string(reason));
		broadcast("medvemodem.session", e);
		json_object_put(e);
	}
	watch_sync();	/* a session going down may be a finding */
}

static void job_log(const char *step, const char *detail)
{
	json_object *e = json_object_new_object();

	json_object_object_add(e, "ms", json_object_new_int64(now_mono_ms() - job.started));
	json_object_object_add(e, "step", json_object_new_string(step));
	if (detail)
		json_object_object_add(e, "detail", json_object_new_string(detail));
	json_object_array_add(job.steps, e);
}

static void job_finish(const char *result, const char *reason)
{
	json_object *o = json_object_new_object();
	const char *id = job.device_id;

	uloop_timeout_cancel(&job_timer);
	job_log(result, reason);
	/* a failed bring-up takes back what it handed to QModem */
	if (!strcmp(result, "failed") && job.qmodem && job.adopted && job.qsection[0]) {
		char script[300];
		const char *argv[] = { script, "remove", job.qsection, NULL };

		snprintf(script, sizeof(script), "%s/modem_scan.sh", qmodem_dir);
		job_log(hook_run(argv, NULL) ? "qmodem-remove" : "qmodem-remove-skipped", job.qsection);
	}
	/* a failed bring-up keeps the configuration it wrote and takes its interface down through
	 * netifd's ubus object: a rollback (and the next try's commit) each cost a `network reload`,
	 * which also reloads wireless (hostapd, wpa_supplicant) on every attempt (field 2026-10-12).
	 * The operator's release takes the configuration back. */
	if (!strcmp(result, "failed") && job.iface[0] && !job.qmodem) {
		json_object *r = netifd_call(job.iface, "down");

		job_log("ifdown", r ? (job.txn[0] ? "configuration kept" : NULL) : "netifd did not answer");
		json_object_put(r);
	}
	session_set(id, !strcmp(result, "connected") ? "connected" : !strcmp(result, "failed") ? "failed" : "idle",
		    job.iface[0] ? job.iface : NULL, job.proto[0] ? job.proto : NULL, reason, job.last);
	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "result", json_object_new_string(result));
	if (reason)
		json_object_object_add(o, "reason", json_object_new_string(reason));
	if (job.iface[0])
		json_object_object_add(o, "interface", json_object_new_string(job.iface));
	if (job.proto[0])
		json_object_object_add(o, "proto", json_object_new_string(job.proto));
	if (job.txn[0])
		json_object_object_add(o, "txn", json_object_new_string(job.txn));
	json_object_object_add(o, "netifd", json_object_get(job.last));
	json_object_object_add(o, "steps", json_object_get(job.steps));
	json_object_object_add(o, "elapsed_ms", json_object_new_int64(now_mono_ms() - job.started));
	if (job.deferred) {
		reply_json(job.ctx, &job.req, NULL, o);
		ubus_complete_deferred_request(job.ctx, &job.req, UBUS_STATUS_OK);
	}
	broadcast("medvemodem.bringup", o);
	json_object_put(o);
	json_object_put(job.steps);
	json_object_put(job.last);
	memset(&job, 0, sizeof(job));
}

/* every step re-checks the attachment generation (spec 4.1) */
static bool job_device_ok(void)
{
	json_object *rec = mm_registry_find(&registry, job.device_id), *g = NULL;

	return rec && json_object_get_boolean(json_object_object_get(rec, "present")) &&
	       json_object_object_get_ex(rec, "generation", &g) && json_object_get_int64(g) == job.generation;
}

static void job_step(void)
{
	long long now = now_mono_ms();
	const char *state;

	if (!job.active)
		return;
	if (!job_device_ok()) {
		job_finish("aborted", "the modem was removed or re-attached during bring-up");
		return;
	}
	if (job.phase == JOB_RELOAD || job.phase == JOB_QRELEASE)
		return;	/* job_after_reload, bringup_after_qmodem continue (the helper has its own limit) */
	if (job.phase == JOB_RELEASE_MM) {
		if (!mm_devices || !json_object_object_get(mm_devices, job.device_id)) {
			job_log("modemmanager-released", NULL);
			job_prepare_backend();
		} else if (now >= job.deadline) {
			job_finish("failed", "ModemManager did not let go of the modem (inhibition did not take effect)");
		} else {
			/* a full ModemManager read every 3 s, not back to back every second (audit 2026-10-20) */
			mm_schedule(0);
			uloop_timeout_set(&job_timer, 3000);
		}
		return;
	}
	if (job.phase == JOB_ADOPT) {
		json_object *rec = mm_registry_find(&registry, job.device_id);
		const char *qsec = qmodem_section_of(rec), *att = jstr(rec, "attachment_id");

		if (qsec) {
			job.phase = JOB_QSECTION;
			uloop_timeout_set(&job_timer, 0);
		} else {
			const char *argv[] = { NULL, "add", att ? att + 4 : "", "usb", "0", NULL };
			char script[300];

			snprintf(script, sizeof(script), "%s/modem_scan.sh", qmodem_dir);
			argv[0] = script;
			if (hook_run(argv, job_adopted)) {
				job_log("qmodem-add", att ? att + 4 : NULL);
			} else if (now >= job.deadline) {
				/* bounded (audit 2026-10-20): a helper that never frees up ends the bring-up */
				job_finish("failed", "QModem's helper never became free");
				return;
			} else {
				uloop_timeout_set(&job_timer, 1000);	/* the helper is busy: try again */
			}
		}
		return;
	}
	if (job.phase == JOB_QSECTION) {
		json_object *rec = mm_registry_find(&registry, job.device_id);
		const char *qsec = qmodem_section_of(rec);

		if (qsec) {
			json_object *qs = json_object_object_get(qmodem_img, qsec);
			const char *alias = jstr(qs, "alias");

			snprintf(job.qsection, sizeof(job.qsection), "%s", qsec);
			if (!job.aliased && job.iface[0] && strcmp(job.iface, qsec) && (!alias || strcmp(alias, job.iface))) {
				/* QModem's interface under our name (mm<N>, the owner 2026-10-16): its alias, and its dial
				 * started again under it; the interfaces left under the old name go with the stale sync */
				const char *argv[] = { reload_hook, "qmodem-alias", qsec, job.iface, NULL };

				if (!strcmp(reload_hook, "none") || hook_run(argv, job_aliased)) {
					job_log("qmodem-alias", job.iface);
					if (!strcmp(reload_hook, "none"))
						job_aliased(0);
				} else if (now >= job.deadline) {
					/* bounded (audit 2026-10-20): a helper that never frees up ends the bring-up */
					job_finish("failed", "QModem's helper never became free");
					return;
				} else {
					uloop_timeout_set(&job_timer, 1000);	/* the helper is busy: try again */
				}
				return;
			}
			if (!job.iface[0])
				snprintf(job.iface, sizeof(job.iface), "%s", alias && *alias && strcmp(alias, "-") ? alias : qsec);
			job_log("qmodem-section", qsec);
			/* QModem dials and creates its interface itself: only watch */
			job.phase = JOB_WAIT;
			job.deadline = now + (budget_override_ms > 0 ? budget_override_ms : mm_session_budget_ms("qmodem"));
			session_set(job.device_id, "connecting", job.iface, job.proto, NULL, NULL);
			uloop_timeout_set(&job_timer, 0);
		} else if (now >= job.deadline) {
			job_finish("failed", "QModem did not take the modem (no modem-device section appeared)");
		} else {
			uloop_timeout_set(&job_timer, 1000);
		}
		return;
	}
	json_object_put(job.last);
	job.last = assess(job.iface);
	state = jstr(job.last, "state");
	if (!strcmp(state, "connected")) {
		job_finish("connected", NULL);
		return;
	}
	if (job.phase == JOB_IFUP) {
		json_object *r;
		if (!strcmp(state, "missing")) {
			if (now >= job.deadline)
				job_finish("failed", "netifd did not create the interface");
			else
				uloop_timeout_set(&job_timer, 500);
			return;
		}
		r = netifd_call(job.iface, "up");
		job_log("ifup", r ? NULL : "netifd did not answer");
		json_object_put(r);
		job.phase = JOB_WAIT;
		session_set(job.device_id, "connecting", job.iface, job.proto, NULL, job.last);
	} else if (!job.qmodem && !strcmp(state, "down") && json_object_array_length(json_object_object_get(job.last, "errors")) &&
		   json_object_object_get(job.last, "autostart") &&
		   !json_object_get_boolean(json_object_object_get(job.last, "autostart"))) {
		/* the protocol handler gave up (proto_block_restart): netifd will not try again by itself,
		 * so the rest of the budget is only waiting (field 2026-10-12: MM_CONNECT_FAILED, then 53 s) */
		char why[160];

		snprintf(why, sizeof(why), "the protocol handler gave up; netifd: %s",
			 json_object_get_string(json_object_array_get_idx(json_object_object_get(job.last, "errors"), 0)));
		job_finish("failed", why);
		return;
	} else if (now >= job.deadline) {
		char why[160];
		json_object *errs = json_object_object_get(job.last, "errors");
		snprintf(why, sizeof(why), "no address and default route within %d s%s%s",
			 ((budget_override_ms > 0 ? budget_override_ms : mm_session_budget_ms(job.proto)) + 999) / 1000,
			 errs && json_object_array_length(errs) ? "; netifd: " : "",
			 errs && json_object_array_length(errs) ? json_object_get_string(json_object_array_get_idx(errs, 0)) : "");
		job_finish("failed", why);
		return;
	}
	uloop_timeout_set(&job_timer, MM_JOB_TICK_MS);
}

static void job_fired(struct uloop_timeout *t)
{
	(void)t;
	job_step();
}

/* after ModemManager let go (or was never involved): QModem adopts, or netifd */
static void job_prepare_backend(void)
{
	if (job.qmodem) {
		job.phase = JOB_ADOPT;
		job.deadline = now_mono_ms() + 90000;	/* QModem retries a slow modem up to 5 x 8 s */
	} else {
		job.phase = JOB_IFUP;
		job.deadline = now_mono_ms() + (budget_override_ms > 0 ? budget_override_ms : mm_session_budget_ms(job.proto));
	}
	uloop_timeout_set(&job_timer, 0);
}

static void job_aliased(int status)
{
	char d[32];

	snprintf(d, sizeof(d), "exit %d", status);
	job_log("qmodem-alias-done", d);
	job.aliased = true;	/* the alias is set: the next look at the section only watches */
	uloop_timeout_set(&job_timer, 0);
}

static void job_adopted(int status)
{
	char d[32];

	if (!job.active)
		return;
	snprintf(d, sizeof(d), "exit %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	job_log("qmodem-add-done", d);
	job.adopted = true;
	job.phase = JOB_QSECTION;
	uloop_timeout_set(&job_timer, 0);
}

static void job_after_reload(void)
{
	job_log("reloaded", NULL);
	inhibits_sync();
	if (job.inhibit && mm_devices && json_object_object_get(mm_devices, job.device_id)) {
		/* ModemManager has the modem: wait until the inhibition makes it let go */
		job.phase = JOB_RELEASE_MM;
		job.deadline = now_mono_ms() + MM_RELEASE_MM_MS;
		job_log("inhibit-modemmanager", NULL);
		mm_schedule(0);
		uloop_timeout_set(&job_timer, 1000);
		return;
	}
	job_prepare_backend();
}

/* netifd events: wake the job, or refresh the state of a managed interface */
static void netifd_event(struct ubus_context *ctx, struct ubus_event_handler *ev, const char *type, struct blob_attr *msg)
{
	char *text = blobmsg_format_json(msg, true);
	json_object *o = text ? json_tokener_parse(text) : NULL;
	const char *iface = jstr(o, "interface");

	(void)ctx; (void)ev; (void)type;
	free(text);
	if (iface && json_object_array_length(netifd_pending) < 32) {
		char base[32];
		size_t n = strlen(iface);
		snprintf(base, sizeof(base), "%s", iface);
		if (n > 2 && n < sizeof(base) && ((base[n - 2] == '_' && (base[n - 1] == '4' || base[n - 1] == '6')) ||
						  (base[n - 2] == 'v' && base[n - 1] == '6')))
			base[n - 2] = '\0';
		json_object_array_add(netifd_pending, json_object_new_string(base));
		/* out of the event callback before calling netifd back */
		uloop_timeout_set(&netifd_timer, 0);
	}
	json_object_put(o);
}

/* device whose managed interface this is, or NULL */
static const char *device_of_interface(const char *iface)
{
	json_object *devs = json_object_object_get(cfg.ledger, "devices");

	if (devs) {
		json_object_object_foreach(devs, id, led)
			if (jstr(led, "interface") && !strcmp(jstr(led, "interface"), iface))
				return id;
	}
	return NULL;
}

static void refresh_interface(const char *iface)
{
	const char *id = device_of_interface(iface), *state, *prev;
	json_object *a, *led;

	if (!id)
		return;
	led = mm_cfgtxn_ledger(&cfg, id);
	a = assess(iface);
	state = jstr(a, "state");
	prev = jstr(json_object_object_get(sessions, id), "state");
	if (!strcmp(state, "connected"))
		session_set(id, "connected", iface, mm_netcfg_ledger_proto(led), NULL, a);
	else if (prev && !strcmp(prev, "connected"))
		session_set(id, "disconnected", iface, mm_netcfg_ledger_proto(led), "netifd reports the interface down", a);
	else if (!prev)
		session_set(id, !strcmp(state, "connecting") ? "connecting" : "idle", iface, mm_netcfg_ledger_proto(led), NULL, a);
	json_object_put(a);
	json_object_put(led);
}

static void netifd_fired(struct uloop_timeout *t)
{
	json_object *list = netifd_pending;
	size_t i;

	(void)t;
	netifd_pending = json_object_new_array();
	for (i = 0; i < json_object_array_length(list); i++) {
		const char *iface = json_object_get_string(json_object_array_get_idx(list, i));
		size_t j;
		bool dup = false;

		for (j = 0; j < i; j++)
			dup = dup || !strcmp(iface, json_object_get_string(json_object_array_get_idx(list, j)));
		if (dup)
			continue;
		if (job.active && !strcmp(iface, job.iface)) {
			if (job.phase != JOB_RELOAD)
				job_step();
		} else {
			refresh_interface(iface);
		}
	}
	json_object_put(list);
}

/* look at every managed interface once, when ubus is up */
static void sessions_initial(void)
{
	json_object *devs = json_object_object_get(cfg.ledger, "devices");

	if (!devs)
		return;
	json_object_object_foreach(devs, id, led) {
		(void)id;
		if (jstr(led, "interface"))
			json_object_array_add(netifd_pending, json_object_new_string(jstr(led, "interface")));
	}
	uloop_timeout_set(&netifd_timer, 0);
}

enum { BU_ID, BU_WAIT, __BU_MAX };
static const struct blobmsg_policy bringup_policy[] = {
	[BU_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[BU_WAIT] = { .name = "wait", .type = BLOBMSG_TYPE_BOOL },
};

/* a bring-up's first steps (configuration, reload); false with result and reason in o when it cannot start */
/*
 * The operator chose another way to drive a modem the engine had handed to QModem (bench 2026-10-19: the
 * FM350 set to «MedveModem напрямую» stayed QModem's, every page said QModem and ModemManager, the planner
 * refused "release it from QModem first" and nothing told the operator). QModem lets go first: its section,
 * its interface and its dialer, through its own script; a section QModem made by itself stays the operator's
 * to remove in QModem.
 */
static bool qmodem_leaving(json_object *rec, char *sec, size_t size)
{
	const char *id = jstr(rec, "device_id"), *b = ledger_backend(id), *choice = jstr(decision_of(id), "backend");
	const char *q = qmodem_section_of(rec);

	if (!q || !b || strcmp(b, "qmodem") || !choice || (strcmp(choice, "native") && strcmp(choice, "modemmanager")))
		return false;
	snprintf(sec, size, "%s", q);
	return true;
}

static bool bringup_txn(json_object *rec, json_object *o);
static void mm_return_wait(const char *id, long long generation);

static void bringup_after_qmodem(int status)
{
	json_object *rec, *o, *r;
	const char *choice;
	char d[32];

	if (!job.active || job.phase != JOB_QRELEASE)
		return;
	snprintf(d, sizeof(d), "exit %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	job_log("qmodem-remove-done", d);
	qmodem_global_sync();
	rec = mm_registry_find(&registry, job.device_id);
	if (!rec || !job_device_ok()) {
		job_finish("aborted", "the modem was removed or re-attached during bring-up");
		return;
	}
	if (qmodem_section_of(rec)) {
		job_finish("failed", "QModem kept its section for this modem");
		return;
	}
	mm_history_add(&history, job.device_id, (long long)time(NULL), now_mono_ms(), "qmodem-release", job.qsection);
	history_dirty = true;
	choice = jstr(decision_of(job.device_id), "backend");
	if (choice && !strcmp(choice, "modemmanager")) {
		/* our QModem ledger goes, the inhibition with it; the bring-up follows once ModemManager has the modem */
		r = mm_cfgtxn_run(&cfg, job.device_id, NULL, NULL, false, now_mono_ms());
		job_log("released", status_of(r));
		json_object_put(r);
		mm_return_wait(job.device_id, job.generation);
		inhibits_sync();
		mm_schedule(0);
		job_finish("released", "QModem let go of the modem; the bring-up follows once ModemManager has it");
		return;
	}
	o = json_object_new_object();
	if (!bringup_txn(rec, o))
		job_finish("failed", jstr(o, "reason") ? jstr(o, "reason") : "the configuration was refused");
	json_object_put(o);
}

static bool bringup_go(json_object *rec, json_object *o)
{
	char sec[64];

	if (frozen(jstr(rec, "device_id"))) {
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string("the modem is frozen by the operator"));
		return false;
	}
	if (qmodem_leaving(rec, sec, sizeof(sec))) {
		char script[300];
		const char *argv[] = { script, "remove", sec, NULL };
		json_object *g = NULL;

		snprintf(script, sizeof(script), "%s/modem_scan.sh", qmodem_dir);
		if (!hook_run(argv, bringup_after_qmodem)) {
			json_object_object_add(o, "result", json_object_new_string("busy"));
			json_object_object_add(o, "reason", json_object_new_string("QModem's helper is busy; try again"));
			return false;
		}
		memset(&job, 0, sizeof(job));
		job.active = true;
		job.started = now_mono_ms();
		job.steps = json_object_new_array();
		job.phase = JOB_QRELEASE;
		snprintf(job.device_id, sizeof(job.device_id), "%s", jstr(rec, "device_id"));
		snprintf(job.qsection, sizeof(job.qsection), "%s", sec);
		json_object_object_get_ex(rec, "generation", &g);
		job.generation = json_object_get_int64(g);
		job_log("qmodem-remove", sec);
		ULOG_INFO("%s: the operator chose %s; QModem lets go of it first (section %s)\n", job.device_id,
			  jstr(decision_of(job.device_id), "backend"), sec);
		session_set(job.device_id, "configuring", NULL, NULL, NULL, NULL);
		return true;
	}
	return bringup_txn(rec, o);
}

/* the transaction and the job after it; a job already running (QModem let go first) goes on */
static bool bringup_txn(json_object *rec, json_object *o)
{
	json_object *r, *arg, *gen = NULL, *g = NULL, *pk = NULL;
	const char *st;

	arg = desire_arg(rec);
	r = mm_cfgtxn_run(&cfg, jstr(rec, "device_id"), desire, arg, false, now_mono_ms());
	json_object_put(arg);
	config_note(r, "bringup");
	st = status_of(r);
	if (strcmp(st, "committed") && strcmp(st, "unchanged")) {
		json_object_object_add(o, "result", json_object_new_string(!strcmp(st, "refused") ? "refused" : "failed"));
		json_object_object_add(o, "reason", json_object_get(json_object_object_get(r, json_object_object_get(r, "reason") ? "reason" : "error")));
		json_object_object_add(o, "config", config_served(r));
		json_object_put(r);
		return false;
	}
	if (!job.active) {
		memset(&job, 0, sizeof(job));
		job.active = true;
		job.started = now_mono_ms();
		job.steps = json_object_new_array();
	}
	job.qsection[0] = '\0';
	snprintf(job.device_id, sizeof(job.device_id), "%s", jstr(rec, "device_id"));
	json_object_object_get_ex(rec, "generation", &g);
	job.generation = json_object_get_int64(g);
	json_object_object_get_ex(r, "generator", &gen);
	snprintf(job.iface, sizeof(job.iface), "%s", jstr(gen, "interface") ? jstr(gen, "interface") : "");
	snprintf(job.proto, sizeof(job.proto), "%s", jstr(gen, "proto") ? jstr(gen, "proto") : "");
	job.inhibit = json_object_get_boolean(json_object_object_get(gen, "inhibit_mm"));
	job.qmodem = jstr(gen, "backend") && !strcmp(jstr(gen, "backend"), "qmodem");
	if (job.qmodem)
		qmodem_global_sync();
	job_log("configured", st);
	json_object_object_get_ex(r, "packages", &pk);
	if (!strcmp(st, "committed")) {
		snprintf(job.txn, sizeof(job.txn), "%s", jstr(r, "txn") ? jstr(r, "txn") : "");
		if (reload_start(pk))
			job.phase = JOB_RELOAD;	/* reload_done continues, bounded by the reload timeout */
		else {
			job_after_reload();
		}
	} else {
		job_after_reload();
	}
	json_object_put(r);
	session_set(job.device_id, "configuring", job.iface, job.proto, NULL, NULL);
	return true;
}

static int m_bringup(struct ubus_context *ctx, struct ubus_object *obj,
		     struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__BU_MAX];
	json_object *rec, *o, *idv = NULL;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(bringup_policy, __BU_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[BU_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_find(&registry, blobmsg_get_string(tb[BU_ID]));
	if (!rec)
		return UBUS_STATUS_NOT_FOUND;
	json_object_object_get_ex(rec, "device_id", &idv);
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_get(idv));
	if (esim_reserved(json_object_get_string(idv))) {
		json_object_object_add(o, "result", json_object_new_string("busy"));
		json_object_object_add(o, "reason", json_object_new_string("an eSIM operation holds this modem"));
		goto reply;
	}
	if (rec_busy(json_object_get_string(idv))) {
		json_object_object_add(o, "result", json_object_new_string("busy"));
		json_object_object_add(o, "reason", json_object_new_string("a recovery is running on this modem"));
		goto reply;
	}
	if (job.active || reload_running || pending_release.active) {
		json_object_object_add(o, "result", json_object_new_string("busy"));
		json_object_object_add(o, "reason", json_object_new_string(job.active ? "another bring-up is running"
										   : "a configuration change is being applied"));
		goto reply;
	}
	if (!json_object_get_boolean(json_object_object_get(rec, "present"))) {
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string("the modem is not attached"));
		goto reply;
	}
	if (!bringup_go(rec, o))
		goto reply;
	if (tb[BU_WAIT] && blobmsg_get_bool(tb[BU_WAIT])) {
		job.ctx = ctx;
		ubus_defer_request(ctx, req, &job.req);
		job.deferred = true;
		json_object_put(o);
		return UBUS_STATUS_OK;
	}
	json_object_object_add(o, "result", json_object_new_string("started"));
	json_object_object_add(o, "interface", json_object_new_string(job.iface));
	json_object_object_add(o, "proto", json_object_new_string(job.proto));
reply:
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/*
 * The operator's «Отключить / Подключить» (audit 2026-10-13: LuCI called network.interface
 * up/down itself, so the engine's journal never said who took the link down). netifd keeps
 * autostart off after a down, and the engine leaves such an interface alone (watch.c).
 */
bool netifd_do(const char *iface, const char *method);
static json_object *deactivated;	/* device id -> {by, who, at}: taken down by the operator */
static json_object *iface_retries;	/* device id -> {step, next}: netifd gave up, the engine tries again */
/* device id -> {since, at, mono}: the network refused the modem's registration, and no registration came since */
static json_object *reg_denied;
#define MM_DENIED_RETRY_MS 1800000LL

/* the operator's downs outlive an engine restart (bench 2026-10-16: an automatic down read as
 * «Деактивирован пользователем» because the engine only guessed after a restart) */
static void deactivated_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	snprintf(path, sizeof(path), "%s/deactivated.json", state_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if (!json_object_to_file_ext(tmp, deactivated, JSON_C_TO_STRING_PLAIN))
		rename(tmp, path);
}

static void deactivated_load(void)
{
	char path[PATH_MAX];
	json_object *o;

	snprintf(path, sizeof(path), "%s/deactivated.json", state_dir);
	o = json_object_from_file(path);
	if (o && json_object_is_type(o, json_type_object)) {
		json_object_put(deactivated);
		deactivated = o;
	} else {
		json_object_put(o);
	}
}
/*
 * «Заморозить» (the owner, 2026-10-19): no service touches the modem on USB until the operator lets it go.
 * The engine starts nothing on it (bring-up, auto-connect, recovery, AT, driver binding, ModemManager's
 * port reports), its interface goes down, and a running ModemManager is inhibited for it. QModem's own
 * polling is QModem's: a modem QModem has is reported as such, never edited here. Kept across restarts.
 */

/* the last AT/QModem reading of a modem, if it is fresh and of its current attachment (borrowed; the cell-lock guard) */
json_object *metrics_cached(const char *id, long long max_age_ms)
{
	json_object *c = metrics_cache ? json_object_object_get(metrics_cache, id) : NULL, *rec = mm_registry_find(&registry, id);

	if (!c || !rec || json_object_get_int64(json_object_object_get(c, "generation")) !=
			  json_object_get_int64(json_object_object_get(rec, "generation")) ||
	    now_mono_ms() - json_object_get_int64(json_object_object_get(c, "at")) > max_age_ms)
		return NULL;
	return json_object_object_get(c, "metrics");
}

bool frozen(const char *id)
{
	return frozen_map && id && json_object_object_get(frozen_map, id);
}

static void frozen_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	snprintf(path, sizeof(path), "%s/frozen.json", state_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(state_dir, 0755);
	if (!json_object_to_file_ext(tmp, frozen_map, JSON_C_TO_STRING_PLAIN))
		rename(tmp, path);
}

static void frozen_load(void)
{
	char path[PATH_MAX];
	json_object *o;

	snprintf(path, sizeof(path), "%s/frozen.json", state_dir);
	o = json_object_from_file(path);
	if (o && json_object_is_type(o, json_type_object)) {
		json_object_put(frozen_map);
		frozen_map = o;
	} else {
		json_object_put(o);
	}
}

enum { CN_ID, CN_UP, CN_WHO, __CN_MAX };
static const struct blobmsg_policy connect_policy[] = {
	[CN_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[CN_UP] = { .name = "up", .type = BLOBMSG_TYPE_BOOL },
	[CN_WHO] = { .name = "who", .type = BLOBMSG_TYPE_STRING },
};

static int m_connect(struct ubus_context *ctx, struct ubus_object *obj,
		     struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__CN_MAX];
	json_object *rec, *o, *conn = NULL;
	const char *id, *iface, *who;
	char detail[96];
	bool up;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(connect_policy, __CN_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[CN_ID] || !tb[CN_UP])
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_find(&registry, blobmsg_get_string(tb[CN_ID]));
	if (!rec)
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	up = blobmsg_get_bool(tb[CN_UP]);
	who = tb[CN_WHO] ? blobmsg_get_string(tb[CN_WHO]) : "ubus";
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	if (esim_reserved(id) || rec_busy(id) || job.active || reload_running || pending_release.active) {
		json_object_object_add(o, "result", json_object_new_string("busy"));
		json_object_object_add(o, "reason", json_object_new_string(esim_reserved(id) ? "an eSIM operation holds this modem" :
						   rec_busy(id) ? "a recovery is running on this modem" :
						   job.active ? "a bring-up is running" : "a configuration change is being applied"));
		goto reply;
	}
	if (up && frozen(id)) {
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string("the modem is frozen by the operator"));
		goto reply;
	}
	conn = connection_of(id, rec);
	iface = jstr(conn, "interface");
	if (!iface) {
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string("the modem has no network interface yet"));
		goto reply;
	}
	json_object_object_add(o, "interface", json_object_new_string(iface));
	if (!netifd_do(iface, up ? "up" : "down")) {
		json_object_object_add(o, "result", json_object_new_string("failed"));
		json_object_object_add(o, "reason", json_object_new_string("netifd did not take the request"));
		goto reply;
	}
	/* «Деактивирован пользователем» on the card until «Активировать» (the owner, 2026-10-16) */
	if (!deactivated)
		deactivated = json_object_new_object();
	if (up) {
		json_object_object_del(deactivated, id);
	} else {
		json_object *d = json_object_new_object();
		json_object_object_add(d, "by", json_object_new_string("operator"));
		json_object_object_add(d, "who", json_object_new_string(who));
		json_object_object_add(d, "at", json_object_new_int64((long long)time(NULL)));
		/* the model it was said of: another one in the same port is not deactivated */
		if (jstr(mm_registry_find(&registry, id), "vidpid"))
			json_object_object_add(d, "vidpid", json_object_new_string(jstr(mm_registry_find(&registry, id), "vidpid")));
		json_object_object_add(deactivated, id, d);
	}
	deactivated_save();
	snprintf(detail, sizeof(detail), "%s by the operator (%.40s)", iface, who);
	mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), up ? "interface-up" : "interface-down", detail);
	history_dirty = true;
	ULOG_INFO("interface %s %s by the operator (%s) for %s\n", iface, up ? "up" : "down", who, id);
	json_object_object_add(o, "result", json_object_new_string("done"));
reply:
	json_object_put(conn);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* open on asking only, for a while (audit 2026-10-21: always open, every LAN client reached every modem's page):
 * LuCI's link asks web_open, the forward lives core.web_forward_minutes (15; '0' keeps it always open) after */
static json_object *webfwd_open;	/* device_id -> monotonic ms it closes at */
static bool webfwd_enabled(void);
static int webfwd_port(const char *id);
static void webfwd_turn(void);
static long long webfwd_ttl_ms(void);

enum { WO_ID, __WO_MAX };
static const struct blobmsg_policy web_open_policy[] = {
	[WO_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
};

/* LuCI's «Открыть веб-интерфейс»: the modem's forward opens (again) for core.web_forward_minutes; the reply has the port */
static int m_web_open(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__WO_MAX];
	json_object *rec, *o;
	const char *id;
	long long ttl = webfwd_ttl_ms();
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(web_open_policy, __WO_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[WO_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_find(&registry, blobmsg_get_string(tb[WO_ID]));
	if (!rec)
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	if (!webfwd_enabled() || webfwd_port(id) <= 0)
		return UBUS_STATUS_NOT_SUPPORTED;
	if (!webfwd_open)
		webfwd_open = json_object_new_object();
	if (ttl)
		json_object_object_add(webfwd_open, id, json_object_new_int64(now_mono_ms() + ttl));
	webfwd_turn();
	o = json_object_new_object();
	json_object_object_add(o, "port", json_object_new_int(webfwd_port(id)));
	json_object_object_add(o, "open_s", json_object_new_int64(ttl ? ttl / 1000 : -1));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

enum { FZ_ID, FZ_ON, FZ_WHO, __FZ_MAX };
static const struct blobmsg_policy freeze_policy[] = {
	[FZ_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[FZ_ON] = { .name = "on", .type = BLOBMSG_TYPE_BOOL },
	[FZ_WHO] = { .name = "who", .type = BLOBMSG_TYPE_STRING },
};

/* «Заморозить» / «Разморозить»: see frozen_map */
static int m_freeze(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__FZ_MAX];
	json_object *rec, *o, *conn = NULL, *d;
	const char *id, *iface, *who, *qsec;
	char detail[96];
	bool on, was_down;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(freeze_policy, __FZ_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[FZ_ID] || !tb[FZ_ON])
		return UBUS_STATUS_INVALID_ARGUMENT;
	rec = mm_registry_find(&registry, blobmsg_get_string(tb[FZ_ID]));
	if (!rec)
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	on = blobmsg_get_bool(tb[FZ_ON]);
	who = tb[FZ_WHO] ? blobmsg_get_string(tb[FZ_WHO]) : "ubus";
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	if (on == frozen(id)) {
		json_object_object_add(o, "result", json_object_new_string("unchanged"));
		json_object_object_add(o, "frozen", json_object_new_boolean(on));
		goto reply;
	}
	if (esim_reserved(id) || rec_busy(id) || (job.active && !strcmp(job.device_id, id))) {
		json_object_object_add(o, "result", json_object_new_string("busy"));
		json_object_object_add(o, "reason", json_object_new_string(esim_reserved(id) ? "an eSIM operation holds this modem" :
						   rec_busy(id) ? "a recovery is running on this modem" : "a bring-up is running on this modem"));
		goto reply;
	}
	conn = connection_of(id, rec);
	iface = jstr(conn, "interface");
	if (!frozen_map)
		frozen_map = json_object_new_object();
	if (on) {
		was_down = deactivated && json_object_object_get(deactivated, id);
		if (iface && !was_down && !netifd_do(iface, "down"))
			json_object_object_add(o, "interface_warning", json_object_new_string("netifd did not take the interface down"));
		d = json_object_new_object();
		json_object_object_add(d, "who", json_object_new_string(who));
		json_object_object_add(d, "at", json_object_new_int64((long long)time(NULL)));
		json_object_object_add(d, "was_down", json_object_new_boolean(was_down));
		/* QModem's dial of it off too, remembered to be put back (the owner, 2026-10-19) */
		if ((qsec = qmodem_section_of(rec)) && strcmp(reload_hook, "none")) {
			json_object *qs = json_object_object_get(qmodem_img, qsec);
			const char *argv[] = { reload_hook, "qmodem-dial", qsec, "0", NULL };

			json_object_object_add(d, "qmodem_section", json_object_new_string(qsec));
			json_object_object_add(d, "qmodem_dial", json_object_new_string(jstr(qs, "enable_dial") ? jstr(qs, "enable_dial") : "1"));
			if (!hook_run(argv, NULL))
				json_object_object_add(o, "qmodem_warning", json_object_new_string("QModem's helper is busy; its dial was not stopped"));
		}
		json_object_object_add(frozen_map, id, d);
	} else {
		json_object *was = json_object_object_get(frozen_map, id);
		const char *qd = jstr(was, "qmodem_dial"), *qsw = jstr(was, "qmodem_section");

		was_down = json_object_get_boolean(json_object_object_get(was, "was_down"));
		if (qsw && qd && !strcmp(qd, "1") && strcmp(reload_hook, "none")) {
			char sec[64];
			const char *argv[] = { reload_hook, "qmodem-dial", sec, "1", NULL };

			snprintf(sec, sizeof(sec), "%s", qsw);
			if (!hook_run(argv, NULL))
				json_object_object_add(o, "qmodem_warning", json_object_new_string("QModem's helper is busy; its dial was not started again"));
		}
		json_object_object_del(frozen_map, id);
		/* the interface comes back as it was before the freeze; a deactivated one stays down */
		if (iface && !was_down && !(deactivated && json_object_object_get(deactivated, id)) && !netifd_do(iface, "up"))
			json_object_object_add(o, "interface_warning", json_object_new_string("netifd did not bring the interface up"));
	}
	frozen_save();
	{
		/* thawed: an inhibition still running gives the ports back when it ends (inhibit_exited); a parked one, or
		 * none, does not — they are reported here for a modem ModemManager is to have */
		bool held = inhibited(id);
		const char *b = ledger_backend(id);

		if (!on && frozen_withdrawn)
			json_object_object_del(frozen_withdrawn, id);
		/* frozen: ModemManager let go of it, its ports out of its event cache (inhibits_sync) */
		inhibits_sync();
		if (!on && !held && !(b && (!strcmp(b, "native") || !strcmp(b, "qmodem"))))
			mm_report_ports(id);
	}
	frozen_list_sync();
	if (iface)
		json_object_object_add(o, "interface", json_object_new_string(iface));
	/* QModem's own services are not ours to stop: the page says so */
	if ((qsec = qmodem_section_of(rec)))
		json_object_object_add(o, "qmodem_section", json_object_new_string(qsec));
	snprintf(detail, sizeof(detail), "by the operator (%.40s)", who);
	mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), on ? "frozen" : "unfrozen", detail);
	history_dirty = true;
	ULOG_WARN("%s %s by the operator (%s)\n", id, on ? "frozen" : "unfrozen", who);
	json_object_object_add(o, "result", json_object_new_string("done"));
	json_object_object_add(o, "frozen", json_object_new_boolean(on));
reply:
	json_object_put(conn);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/*
 * «Приоритет модемов» (owner's request, bench 2026-10-15): the order the operator gives, first = main.
 * Each modem's interface gets the route metric 100, 110, 120... in that order and netifd reloads; the
 * lowest metric carries the traffic and failover (spec 20.8) moves from there. A modem without an
 * interface yet keeps its place in the numbering and gets its metric with its interface later.
 */
static void metrics_note(json_object *net);
static int priority_live(json_object *moved);
static const char *session_dev(json_object *sess);
static char *slurp(const char *path);
static void carrier_switched(const char *from, const char *to, const char *why);
enum { PR_ORDER, PR_SET, __PR_MAX };
static const struct blobmsg_policy priority_policy[] = {
	[PR_ORDER] = { .name = "order", .type = BLOBMSG_TYPE_ARRAY },
	/* {"<device_id>" or "iface:<name>": metric}: the metric typed in the channels table (the owner, 2026-10-30:
	 * «разреши метрику прямо в нашей дашборд таблице править у всех») instead of the order's place */
	[PR_SET] = { .name = "set", .type = BLOBMSG_TYPE_TABLE },
};

static json_object *wired_now(json_object *net);

/* the metric of the order's n-th place (1-based): 100, 110, 120... A tunnel's own default route (WireGuard, AmneziaWG
 * over a modem, metric 1-99) stays above the uplinks (the base is 100, not 10, for that;
 * the owner 2026-10-23: a VPN over the modem under the operator's whitelists) */
#define PRIORITY_METRIC(n) (90 + 10 * (n))

/* a wired uplink already holds this metric */
static bool wired_metric(json_object *wired, int metric)
{
	for (size_t i = 0; i < json_object_array_length(wired); i++)
		if (json_object_get_int(json_object_object_get(json_object_array_get_idx(wired, i), "metric")) == metric)
			return true;
	return false;
}

/* network.<iface>.metric to the value, against the one there now; false when it is so already */
static bool metric_op(json_object *img, const char *iface, int metric, json_object *ops)
{
	json_object *sec = json_object_object_get(json_object_object_get(img, "network"), iface), *op;
	char m[16];

	snprintf(m, sizeof(m), "%d", metric);
	if (!sec || (jstr(sec, "metric") && !strcmp(jstr(sec, "metric"), m)))
		return false;	/* already so: nothing to write */
	op = json_object_new_object();
	json_object_object_add(op, "op", json_object_new_string("set"));
	json_object_object_add(op, "package", json_object_new_string("network"));
	json_object_object_add(op, "section", json_object_new_string(iface));
	json_object_object_add(op, "option", json_object_new_string("metric"));
	json_object_object_add(op, "value", json_object_new_string(m));
	/* the value there now: an op without one expects no metric, so every order after the first was
	 * skipped as changed-since (bench r33, the owner 2026-10-16: moving a modem down did nothing) */
	json_object_object_add(op, "expect", json_object_get(json_object_object_get(sec, "metric")));
	json_object_array_add(ops, op);
	return true;
}

static int m_priority(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	static const char *const pkgs[] = { "network", NULL };
	struct blob_attr *tb[__PR_MAX], *cur;
	json_object *o, *img = NULL, *ops, *items, *checked = NULL, *apply, *wired = NULL;
	char err[160];
	size_t rem;
	int n = 0, slot = 0, rc;
	bool with_wired = false;

	(void)obj; (void)method;
	blobmsg_parse(priority_policy, __PR_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[PR_ORDER])
		return UBUS_STATUS_INVALID_ARGUMENT;
	o = json_object_new_object();
	ops = json_object_new_array();
	items = json_object_new_array();
	if (job.active || reload_running || pending_release.active) {
		json_object_object_add(o, "result", json_object_new_string("busy"));
		json_object_object_add(o, "reason", json_object_new_string("a configuration change is being applied"));
		goto reply;
	}
	if (!(img = mm_uci_read(conf_dir, pkgs, err, sizeof(err)))) {
		json_object_object_add(o, "result", json_object_new_string("failed"));
		json_object_object_add(o, "reason", json_object_new_string(err));
		goto reply;
	}
	/* the router's own uplinks (a wired WAN) take a place in the order as "iface:<name>" (field request
	 * 2026-10-23: the wire first, the modems its backup); an order of modems alone leaves each of them where its
	 * metric puts it: the modems' numbers step over it (a WAN put second stays second after «Сделать основным») */
	wired = wired_now(json_object_object_get(img, "network"));
	blobmsg_for_each_attr(cur, tb[PR_ORDER], rem)
		if (blobmsg_type(cur) == BLOBMSG_TYPE_STRING && !strncmp(blobmsg_get_string(cur), "iface:", 6))
			with_wired = true;
	blobmsg_for_each_attr(cur, tb[PR_ORDER], rem) {
		json_object *rec = NULL, *conn = NULL, *it, *w = NULL;
		const char *iface, *name = blobmsg_type(cur) == BLOBMSG_TYPE_STRING ? blobmsg_get_string(cur) : "";
		int typed = 0, metric;
		struct blob_attr *sv;
		size_t srem;

		if (tb[PR_SET])
			blobmsg_for_each_attr(sv, tb[PR_SET], srem)
				if (blobmsg_type(sv) == BLOBMSG_TYPE_INT32 && !strcmp(blobmsg_name(sv), name) &&
				    (int)blobmsg_get_u32(sv) >= 0 && (int)blobmsg_get_u32(sv) <= 9999)
					typed = (int)blobmsg_get_u32(sv) + 1;	/* +1: a typed 0 is a value too */

		if (blobmsg_type(cur) != BLOBMSG_TYPE_STRING || n >= 32)
			continue;
		n++;
		slot++;
		if (!strncmp(name, "iface:", 6)) {
			for (size_t i = 0; i < json_object_array_length(wired); i++)
				if (!strcmp(jstr(json_object_array_get_idx(wired, i), "interface"), name + 6))
					w = json_object_array_get_idx(wired, i);
			if (!w)
				continue;
			iface = name + 6;
		} else {
			if (!(rec = mm_registry_find(&registry, name)))
				continue;
			conn = connection_of(jstr(rec, "device_id"), rec);
			iface = jstr(conn, "interface");
		}
		while (!with_wired && !typed && wired_metric(wired, PRIORITY_METRIC(slot)))
			slot++;
		it = json_object_new_object();
		if (w)
			json_object_object_add(it, "interface_id", json_object_new_string(name));
		else
			json_object_object_add(it, "device_id", json_object_new_string(jstr(rec, "device_id")));
		json_object_object_add(it, "priority", json_object_new_int(n));
		metric = typed ? typed - 1 : PRIORITY_METRIC(slot);
		if (iface && json_object_object_get(json_object_object_get(img, "network"), iface)) {
			json_object_object_add(it, "interface", json_object_new_string(iface));
			json_object_object_add(it, "metric", json_object_new_int(metric));
			if (metric_op(img, iface, metric, ops))
				json_object_object_add(it, "changed", json_object_new_boolean(1));
			/* its IPv6 twin (wan6) in the same place */
			for (size_t i = 0; w && i < json_object_array_length(json_object_object_get(w, "twins")); i++)
				metric_op(img, json_object_get_string(json_object_array_get_idx(json_object_object_get(w, "twins"), i)), metric, ops);
		}
		json_object_array_add(items, it);
		json_object_put(conn);
	}
	checked = mm_cfg_check(img, ops);
	apply = json_object_object_get(checked, "apply");
	if (apply && json_object_array_length(apply)) {
		size_t i;

		if (mm_uci_apply(conf_dir, cfg.savedir, apply, err, sizeof(err))) {
			json_object_object_add(o, "result", json_object_new_string("failed"));
			json_object_object_add(o, "reason", json_object_new_string(err));
			goto reply;
		}
		for (i = 0; i < json_object_array_length(apply); i++)
			mm_cfg_simulate(img, json_object_array_get_idx(apply, i));
	}
	/* no network reload: netifd restarts an interface whose metric changed, and every connected modem hung up and
	 * dialled again on each «Сделать основным» (bench 2026-10-20, 15:03 and 15:08). The configuration keeps the
	 * order for the next ifup; the live routes move now — every one against the kernel's metric, not only where
	 * the configuration changed (bench 2026-10-22: a press after a failed move found UCI right and moved nothing) */
	{
		json_object *pk = json_object_new_array();
		int live;

		for (size_t i = 0; i < json_object_array_length(items); i++) {
			json_object *it = json_object_array_get_idx(items, i), *st = NULL;
			const char *dev = jstr(it, "device_id") ?
					  session_dev(sessions ? json_object_object_get(sessions, jstr(it, "device_id")) : NULL) : NULL;

			/* a wired uplink's device is netifd's word */
			if (!jstr(it, "device_id") && jstr(it, "interface") && (st = netifd_call(jstr(it, "interface"), "status")))
				dev = jstr(st, "l3_device");
			if (json_object_object_get(it, "metric") && dev) {
				json_object *m = json_object_new_object();

				json_object_object_add(m, "dev", json_object_new_string(dev));
				json_object_object_add(m, "metric", json_object_get(json_object_object_get(it, "metric")));
				json_object_array_add(pk, m);
			}
			json_object_put(st);
		}
		char top0[IFNAMSIZ + 1] = "", *r4 = slurp(PROC_NET_ROUTE);
		json_object *routes = mm_failover_routes(r4 ? r4 : "");

		free(r4);
		if (mm_failover_top(routes, NULL))
			snprintf(top0, sizeof(top0), "%s", mm_failover_top(routes, NULL));
		json_object_put(routes);
		live = priority_live(pk);
		json_object_object_add(o, "routes", json_object_new_string(live > 0 ? "moved" : live < 0 ? "failed" : "unchanged"));
		json_object_put(pk);
		if (live > 0 && *top0) {
			/* the order put another channel first: its connections start anew there */
			r4 = slurp(PROC_NET_ROUTE);
			routes = mm_failover_routes(r4 ? r4 : "");
			free(r4);
			if (mm_failover_top(routes, NULL) && strcmp(mm_failover_top(routes, NULL), top0))
				carrier_switched(top0, mm_failover_top(routes, NULL), "priority");
			json_object_put(routes);
		}
	}
	metrics_note(json_object_object_get(img, "network"));
	for (size_t i = 0; i < json_object_array_length(items); i++) {
		json_object *it = json_object_array_get_idx(items, i);
		char detail[96];

		if (!jstr(it, "device_id"))
			continue;	/* a wired uplink: no modem's history */
		snprintf(detail, sizeof(detail), "priority %d%s%.20s", json_object_get_int(json_object_object_get(it, "priority")),
			 jstr(it, "interface") ? ", metric of " : "", jstr(it, "interface") ? jstr(it, "interface") : "");
		mm_history_add(&history, jstr(it, "device_id"), (long long)time(NULL), now_mono_ms(), "priority", detail);
	}
	history_dirty = true;
	ULOG_INFO("modem priority set by the operator: %d uplink(s), %d metric(s) changed\n", n,
		  apply ? (int)json_object_array_length(apply) : 0);
	json_object_object_add(o, "result", json_object_new_string("done"));
	json_object_object_add(o, "changed", json_object_new_int(apply ? (int)json_object_array_length(apply) : 0));
reply:
	json_object_object_add(o, "items", items);
	json_object_put(checked);
	json_object_put(img);
	json_object_put(ops);
	json_object_put(wired);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* ---- AT workers (spec 11.5, 11.6) ---- */

static const char *purpose_of(enum at_kind k)
{
	return k == AT_CONSOLE ? "operator" : k == AT_METRICS || k == AT_CELLS ? "metrics" : k == AT_DIAL || k == AT_HANGUP ? "session" :
	       k == AT_SMS ? "sms" : "diagnostics";
}

static void at_close_out(struct at_job *j)
{
	if (j->out.fd >= 0) {
		if (j->out.registered)
			uloop_fd_delete(&j->out);
		close(j->out.fd);
		j->out.fd = -1;
	}
}

static void at_drain(struct at_job *j)
{
	char chunk[4096];
	ssize_t n;

	while (j->out.fd >= 0 && (n = read(j->out.fd, chunk, sizeof(chunk))) > 0) {
		if (j->len + (size_t)n > 512 * 1024)
			continue;
		if (j->len + (size_t)n + 1 > j->cap) {
			size_t cap = j->cap ? j->cap * 2 : 16384;
			char *nb;
			while (cap < j->len + (size_t)n + 1)
				cap *= 2;
			nb = realloc(j->buf, cap);
			if (!nb)
				continue;
			j->buf = nb;
			j->cap = cap;
		}
		memcpy(j->buf + j->len, chunk, (size_t)n);
		j->len += (size_t)n;
		j->buf[j->len] = '\0';
	}
}

static void at_readable(struct uloop_fd *u, unsigned int events)
{
	struct at_job *j = container_of(u, struct at_job, out);

	(void)events;
	at_drain(j);
	if (u->eof || u->error)
		at_close_out(j);
}

static void at_exited(struct uloop_process *p, int ret);
static void at_stuck_fix(const char *id, const char *node, long long generation);

/*
 * A worker that ignores SIGKILL sleeps in the kernel (state D) on the port: an FM350's ttyUSB3 hung so for hours
 * (a field log: sms_tool in D on the AT port, the data link still up). Waiting for it held the
 * job's slot for ever, and «an AT job is running on this modem» then refused every AT job and every recovery of the
 * modem. 5 s after the kill the job is let go: answered, its slot free, the port marked stuck (no new worker hangs on
 * it too) until the stuck worker is gone or the modem re-attaches.
 */
#define AT_STUCK_GRACE_MS 5000
/* tests: with the hang hook on, a signal the worker survives, as a worker in D survives SIGKILL */
static int at_kill_signal(void)
{
#ifdef MM_TEST_HOOKS
	if (getenv("MEDVEMODEM_TEST_AT_HANG_S"))
		return SIGWINCH;
#endif
	return SIGKILL;
}
static void at_kill_fired(struct uloop_timeout *t)
{
	struct at_job *j = container_of(t, struct at_job, kill);
	json_object *e;

	if (j->proc.pid <= 0)
		return;
	if (!j->killed) {
		j->killed = true;
		snprintf(j->why, sizeof(j->why), "the port did not answer in time");
		kill(-j->proc.pid, at_kill_signal());
		uloop_timeout_set(&j->kill, AT_STUCK_GRACE_MS);
		return;
	}
	ULOG_ERR("%s: the AT worker (pid %d) on %s ignores SIGKILL: the port is stuck in the kernel; new AT jobs on it wait "
		 "until it is gone or the modem re-attaches\n", j->device_id, (int)j->proc.pid, j->node);
	if (j->node[0]) {
		e = json_object_new_object();
		json_object_object_add(e, "pid", json_object_new_int(j->proc.pid));
		json_object_object_add(e, "since", json_object_new_int64(now_mono_ms()));
		json_object_object_add(e, "device_id", json_object_new_string(j->device_id));
		json_object_object_add(e, "generation", json_object_new_int64(j->generation));
		json_object_object_add(stuck_nodes, j->node, e);
	}
	mm_history_add(&history, j->device_id, (long long)time(NULL), now_mono_ms(), "at-port-stuck", j->node);
	history_dirty = true;
	snprintf(j->why, sizeof(j->why), "the port is stuck in the kernel (its worker ignores SIGKILL)");
	{
		char id[sizeof(j->device_id)], node[sizeof(j->node)];
		long long gen = j->generation;

		snprintf(id, sizeof(id), "%s", j->device_id);
		snprintf(node, sizeof(node), "%s", j->node);
		uloop_process_delete(&j->proc);
		at_exited(&j->proc, -1);
		if (node[0])
			at_stuck_fix(id, node, gen);
	}
}

/* why a port takes no new worker now, or NULL: its last worker is stuck in the kernel and the modem was not re-attached */
static const char *at_stuck(const char *node, json_object *rec)
{
	json_object *e = node && stuck_nodes ? json_object_object_get(stuck_nodes, node) : NULL;
	char proc[64];

	if (!e)
		return NULL;
	snprintf(proc, sizeof(proc), "%s/%d", proc_dir, json_object_get_int(json_object_object_get(e, "pid")));
	if (access(proc, F_OK) ||
	    (rec && json_object_get_int64(json_object_object_get(rec, "generation")) != json_object_get_int64(json_object_object_get(e, "generation")))) {
		ULOG_INFO("%s: the stuck AT worker is gone or the modem re-attached; the port takes jobs again\n", node);
		json_object_object_del(stuck_nodes, node);
		return NULL;
	}
	return "the AT port is stuck in the kernel (a worker on it ignores SIGKILL); MedveModem binds its interface again, then resets the modem";
}
static json_object *at_reply(struct at_job *j, const char *error);

/*
 * In the worker: QModem's `modem_ctrl.sh <method> <section>` for each method,
 * {"responses": {method: its JSON}} (spec 25.9.12). QModem owns the modem's
 * AT ports, so the engine asks QModem rather than the port.
 */
static json_object *qmodem_ctrl_run(const char *section, const char *const *methods, int n, char *err, size_t size)
{
	json_object *r = json_object_new_object(), *resp = json_object_new_object();
	char script[300];
	int i, complete = 0;

	snprintf(script, sizeof(script), "%s/modem_ctrl.sh", qmodem_dir);
	json_object_object_add(r, "responses", resp);
	for (i = 0; i < n; i++) {
		/* a write is "method\x1fargument": modem_ctrl.sh <method> <section> <argument> */
		char method[64], key[80];
		const char *sep = strchr(methods[i], '\x1f');
		const char *argv[] = { script, method, section, sep ? sep + 1 : NULL, NULL };
		char buf[16384];
		size_t len = 0;
		ssize_t got;
		int fds[2], fd;
		pid_t pid;
		json_object *parsed;
		char *brace;

		snprintf(method, sizeof(method), "%.*s", sep ? (int)(sep - methods[i]) : (int)strlen(methods[i]), methods[i]);
		if (sep)
			snprintf(key, sizeof(key), "step%d", i);
		else
			snprintf(key, sizeof(key), "%s", method);
		if (pipe(fds))
			break;
		pid = fork();
		if (pid < 0) {
			close(fds[0]);
			close(fds[1]);
			break;
		}
		if (pid == 0) {
			fd = open("/dev/null", O_RDWR);
			dup2(fds[1], 1);
			if (fd >= 0) {
				dup2(fd, 0);
				dup2(fd, 2);
			}
			for (fd = 3; fd < 1024; fd++)
				close(fd);
			execv(argv[0], (char *const *)argv);
			_exit(127);
		}
		close(fds[1]);
		while (len + 1 < sizeof(buf) && (got = read(fds[0], buf + len, sizeof(buf) - 1 - len)) > 0)
			len += (size_t)got;
		buf[len] = '\0';
		close(fds[0]);
		waitpid(pid, NULL, 0);
		/* its shell libraries may say something before the JSON */
		brace = strchr(buf, '{');
		parsed = brace ? json_tokener_parse(brace) : NULL;
		if (parsed && json_object_is_type(parsed, json_type_object)) {
			json_object_object_add(resp, key, parsed);
			complete++;
		} else {
			json_object_put(parsed);
			if (!err[0])
				snprintf(err, size, "QModem's modem_ctrl.sh %s gave no JSON", method);
		}
	}
	/* one answer is worth reading: the other may be a method this modem lacks */
	json_object_object_add(r, "complete", json_object_new_int(complete));
	return r;
}

/*
 * In the worker: `mmcli -m <index> <args>` per step (args split on \x1f),
 * {"responses": {"stepN": "ok" | "exit N"}, "complete": steps that succeeded}
 * (radio stage 2: ModemManager carries out the changes for a modem it holds).
 */
/*
 * A «qmi» step (the modem's stored mode preference, a ModemManager narrowed to one mode: mm_modes_narrowed): qmicli
 * through the proxy ModemManager itself uses (-p; --device-open-mbim on an MBIM function), then read back. The arguments
 * are the engine's own: a cdc-wdm name checked by mm_modes_narrowed and a preference built from a fixed list.
 */
static bool qmi_pref_run(const char *wdm, bool mbim, const char *arg, char *err, size_t size)
{
	char dev[48], cmd[512], line[256], want[96];
	const char *eq = strchr(arg, '=');
	bool seen = false, ok = true;
	FILE *f;
	int status;

	if (!wdm || strncmp(wdm, "cdc-wdm", 7) || strspn(wdm + 7, "0123456789") != strlen(wdm + 7) || !eq ||
	    strncmp(arg, "--nas-set-system-selection-preference=", 38) || strspn(eq + 1, "gsmutela5nr|") != strlen(eq + 1)) {
		snprintf(err, size, "qmi: no control port to write the mode preference through");
		return false;
	}
	snprintf(dev, sizeof(dev), "/dev/%s", wdm);
	snprintf(cmd, sizeof(cmd), "'%s' -p %s -d '%s' '%s' >/dev/null 2>&1", qmicli_path, mbim ? "--device-open-mbim" : "", dev, arg);
	status = system(cmd);
	if (status != 0) {
		snprintf(err, size, "qmicli %s: exit %d", arg, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
		return false;
	}
	/* read back: «Mode preference: 'umts, lte'» has every technology written */
	snprintf(cmd, sizeof(cmd), "'%s' -p %s -d '%s' --nas-get-system-selection-preference 2>/dev/null", qmicli_path,
		 mbim ? "--device-open-mbim" : "", dev);
	if (!(f = popen(cmd, "r"))) {
		snprintf(err, size, "qmicli: the read back did not start");
		return false;
	}
	snprintf(want, sizeof(want), "%s", eq + 1);
	while (fgets(line, sizeof(line), f))
		if (strstr(line, "Mode preference:")) {
			seen = true;
			ok = mm_qmi_pref_matches(line, want);	/* exactly the set written (audit of r135) */
		}
	/* a read back that failed is no confirmation, whatever it printed */
	status = pclose(f);
	if (status != 0)
		ok = false;
	if (!seen || !ok) {
		snprintf(err, size, "qmicli: the modem did not keep the mode preference (%s)", seen ? "another one read back" : "nothing read back");
		return false;
	}
	return true;
}

static json_object *mm_steps_run(const char *index, const char *wdm, bool mbim, const char *const *steps, int n, char *err, size_t size)
{
	json_object *r = json_object_new_object(), *resp = json_object_new_object();
	int i, complete = 0;

	json_object_object_add(r, "responses", resp);
	for (i = 0; i < n; i++) {
		const char *argv[12];
		char copy[1024], *save = NULL, *w, key[16];
		int k = 0, status = -1, fd;
		pid_t pid;

		argv[k++] = mmcli_path;
		argv[k++] = "-m";
		argv[k++] = index;
		snprintf(copy, sizeof(copy), "%s", steps[i]);
		for (w = strtok_r(copy, "\x1f", &save); w && k < 11; w = strtok_r(NULL, "\x1f", &save))
			argv[k++] = w;
		argv[k] = NULL;
		snprintf(key, sizeof(key), "step%d", i);
		if (!strncmp(steps[i], "qmi\x1f", 4)) {
			if (qmi_pref_run(wdm, mbim, steps[i] + 4, err, size)) {
				json_object_object_add(resp, key, json_object_new_string("ok"));
				complete++;
				continue;
			}
			json_object_object_add(resp, key, json_object_new_string("exit 1"));
			break;
		}
		pid = fork();
		if (pid == 0) {
			fd = open("/dev/null", O_RDWR);
			if (fd >= 0) {
				dup2(fd, 0);
				dup2(fd, 1);
				dup2(fd, 2);
			}
			execvp(argv[0], (char *const *)argv);
			_exit(127);
		}
		if (pid > 0)
			waitpid(pid, &status, 0);
		if (pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
			json_object_object_add(resp, key, json_object_new_string("ok"));
			complete++;
		} else {
			char text[32];

			snprintf(text, sizeof(text), "exit %d", pid > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1);
			json_object_object_add(resp, key, json_object_new_string(text));
			if (!err[0])
				snprintf(err, size, "mmcli %s: %s", steps[i], text);
			break;	/* the next step may depend on this one */
		}
	}
	json_object_object_add(r, "complete", json_object_new_int(complete));
	return r;
}

/* Run the exchange in a child; the child keeps only its pipe. */
bool at_spawn(struct at_job *j)
{
	const char *list[16], *base = strrchr(j->node, '/');
	char path[160];
	int fds[2], n = 0, fd;
	pid_t pid;

	for (n = 0; n < 16 && n < (int)json_object_array_length(j->cmds); n++)
		list[n] = json_object_get_string(json_object_array_get_idx(j->cmds, n));
	snprintf(path, sizeof(path), "%s/%s", dev_dir, base ? base + 1 : j->node);
	{
		/* the last worker on this port was cut off: its late answer may still come */
		json_object *cut = json_object_object_get(cut_nodes, j->node);
		j->settle_ms = cut && now_mono_ms() - json_object_get_int64(cut) < 60000 ? MM_AT_SETTLE_MS : 0;
		json_object_object_del(cut_nodes, j->node);
	}
	if (pipe2(fds, O_CLOEXEC))
		return false;
	pid = fork();
	if (pid < 0) {
		close(fds[0]);
		close(fds[1]);
		return false;
	}
	if (pid == 0) {
		char err[256] = "";
		json_object *r;
		const char *text;
		size_t len, off = 0;

		setpgid(0, 0);
#ifdef MM_TEST_HOOKS
		/* tests: a worker that hangs past its kill, as one stuck in the kernel does */
		if (getenv("MEDVEMODEM_TEST_AT_HANG_S"))
			sleep((unsigned)atoi(getenv("MEDVEMODEM_TEST_AT_HANG_S")));
#endif
		/* a child stuck on a dead tty must not keep the ubus socket open */
		for (fd = 3; fd < 1024; fd++)
			if (fd != fds[1])
				close(fd);
		r = j->kind == AT_SMS ? mm_sms_at_run(path, j->results, j->settle_ms, err, sizeof(err)) :
		    j->kind == AT_DIAL && jstr(j->results, "flow") ? mm_dial_at_run(path, j->results, j->settle_ms, err, sizeof(err)) :
		    !j->node[0] && jstr(j->results, "mm_index") ? mm_steps_run(jstr(j->results, "mm_index"), jstr(j->results, "qmi_wdm"),
								   json_object_get_boolean(json_object_object_get(j->results, "qmi_mbim")), list, n, err, sizeof(err)) :
		    !j->node[0] ? qmodem_ctrl_run(jstr(j->results, "qmodem_section"), list, n, err, sizeof(err)) :
		    mm_at_exchange(path, list, n, j->timeout_ms, j->settle_ms, err, sizeof(err));
		if (!r)
			r = json_object_new_object();
		if (err[0])
			json_object_object_add(r, "error", json_object_new_string(err));
		text = json_object_to_json_string_ext(r, JSON_C_TO_STRING_PLAIN);
		len = strlen(text);
		while (off < len) {
			ssize_t w = write(fds[1], text + off, len - off);
			if (w < 0 && errno == EINTR)
				continue;	/* a signal is no reason to cut the answer (review 2026-11-15) */
			if (w <= 0)
				break;
			off += (size_t)w;
		}
		_exit(0);
	}
	close(fds[1]);
	fcntl(fds[0], F_SETFL, fcntl(fds[0], F_GETFL) | O_NONBLOCK);
	j->len = 0;
	j->killed = false;
	j->out.fd = fds[0];
	j->out.cb = at_readable;
	uloop_fd_add(&j->out, ULOOP_READ);
	j->proc.pid = pid;
	j->proc.cb = at_exited;
	load.at_jobs++;
	uloop_process_add(&j->proc);
	j->kill.cb = at_kill_fired;
	uloop_timeout_set(&j->kill, n * j->timeout_ms + j->settle_ms + 2000);
	return true;
}

/* Lease the job's node; NULL when granted, else why (detail filled). */
const char *at_lease(struct at_job *j, char *detail, size_t size)
{
	json_object *rec = mm_registry_find(&registry, j->device_id), *srv, *roles = NULL, *req, *lease, *revoked = NULL;
	const char *why = NULL;

	detail[0] = '\0';
	if (!rec)
		return "device not present";
	if (frozen(j->device_id))
		return "the modem is frozen by the operator";
	if ((why = at_stuck(j->node, rec)))
		return why;
	if (!node_still_owned(j->node, json_object_get_string(json_object_object_get(rec, "attachment_id")))) {
		schedule_pass(true);
		return "port is not on this device any more";
	}
	{
		const char *b = strrchr(j->node, '/');
		char who[96];

		/* two readers on one tty get fragments of each other's answers (S15): never talk over another program */
		if (tty_holder(b ? b + 1 : j->node, who, sizeof(who))) {
			snprintf(detail, size, "%s", who);
			return "the port is held by another program";
		}
	}
	srv = served(rec);
	json_object_object_get_ex(srv, "port_roles", &roles);
	req = json_object_new_object();
	json_object_object_add(req, "device_id", json_object_new_string(j->device_id));
	json_object_object_add(req, "port", json_object_new_string(j->node));
	json_object_object_add(req, "holder", json_object_new_string(j->kind == AT_CONSOLE ? "medvemodemd-console" :
								     j->kind == AT_METRICS ? "medvemodemd-metrics" :
								     j->kind == AT_PROBE ? "medvemodemd-probe" :
								     j->kind == AT_SMS ? "medvemodemd-sms" :
								     j->kind == AT_RADIO ? "medvemodemd-radio" :
								     j->kind == AT_CELLS ? "medvemodemd-cells" :
								     j->kind == AT_SIM ? "medvemodemd-sim" : "medvemodemd-dial"));
	json_object_object_add(req, "purpose", json_object_new_string(j->kind == AT_SMS && jstr(j->results, "op") &&
								      !strcmp(jstr(j->results, "op"), "ussd") ? "ussd" : purpose_of(j->kind)));
	json_object_object_add(req, "mode", json_object_new_string("exclusive"));
	json_object_object_add(req, "ttl_ms", json_object_new_int((int)json_object_array_length(j->cmds) * j->timeout_ms + 5000));
	lease = mm_lease_acquire(&leases, req, roles, now_mono_ms(), &why, detail, size, &revoked);
	json_object_put(req);
	json_object_put(srv);
	leases_revoked(revoked, "preempted");
	json_object_put(revoked);
	if (!lease)
		return why;
	snprintf(j->lease_id, sizeof(j->lease_id), "%s", json_object_get_string(json_object_object_get(lease, "lease_id")));
	json_object_put(lease);
	lease_rearm();
	return NULL;
}

void at_release(struct at_job *j)
{
	if (j->lease_id[0]) {
		mm_lease_release(&leases, j->lease_id);
		j->lease_id[0] = '\0';
		lease_rearm();
	}
}

void at_finish(struct at_job *j, json_object *reply)
{
	at_release(j);
	json_object_object_add(reply, "elapsed_ms", json_object_new_int64(now_mono_ms() - j->started));
	if (j->deferred) {
		reply_json(j->ctx, &j->req, NULL, reply);
		ubus_complete_deferred_request(j->ctx, &j->req, UBUS_STATUS_OK);
	}
	json_object_put(reply);
	free(j->buf);
	json_object_put(j->cmds);
	json_object_put(j->queue);
	json_object_put(j->results);
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
}

static json_object *at_reply(struct at_job *j, const char *error)
{
	json_object *o = json_object_new_object();

	json_object_object_add(o, "device_id", json_object_new_string(j->device_id));
	if (j->node[0])
		json_object_object_add(o, "port", json_object_new_string(j->node));
	if (error)
		json_object_object_add(o, "error", json_object_new_string(error));
	return o;
}

static bool at_probe_next(struct at_job *j);

/* the FM350's carrier command and whether a modem is that kind (its radio dialect, profiles/generic/fibocom-fm350.json) */
#define FIBOCOM_CA "AT+GTCAINFO?"
#define FIBOCOM_CC "AT+GTCCINFO?"	/* the serving cells with their SINR: 3GPP's own commands have none */
static bool fibocom_mtk(json_object *srv)
{
	const char *d = jstr(json_object_object_get(srv, "radio_settings"), "dialect");

	return d && !strcmp(d, "fibocom-mtk");
}

/* the answer text of a QModem send_at step (its at_cfg.res), NULL when QModem's AT failed */
static const char *sim_send_at_res(json_object *responses, const char *key)
{
	json_object *cfg = json_object_object_get(json_object_object_get(responses, key), "at_cfg");

	return jstr(cfg, "status") && !strcmp(jstr(cfg, "status"), "1") ? jstr(cfg, "res") : NULL;
}

/*
 * An FM350 after AT+GTACT (or its flight mode for a cell lock) keeps its address while the data path is dead:
 * a band change drops the PDP context (a live FM350: registered, the same
 * address, 0 of 3 pings; the interface's reconnect brings it back). Our own session is dialled again (L1:
 * down/up of our interface only); a session the engine does not run is left to whoever runs it.
 */
enum rec_outcome { REC_STARTED, REC_DONE, REC_NOT };
static enum rec_outcome rec_start(json_object *rec, int level, const char *reason, bool automatic,
				  const char **result, char *buf, size_t size);
static char rec_source[16];

/*
 * A stuck AT port is mended, not left hung (the owner, 2026-11-08: «зависшее хуже чем сброшенное»): first the driver of
 * that one serial interface is let go and bound again (a field report: an FM350's interface 06 rebound by hand
 * answered again, the data link untouched), in a child of its own, since a write to unbind on a hung tty may itself
 * hang. A port stuck again on the same attachment after two rebinds gets the modem's USB reset, through the recovery
 * with its caps (5 min, 1 h). The rebinds: at most 2 per attachment, 5 min apart.
 */
#define AT_STUCK_REBINDS 2
#ifndef AT_STUCK_REBIND_GAP_MS
#define AT_STUCK_REBIND_GAP_MS (5 * 60 * 1000)
#endif
static json_object *stuck_fixes;	/* device_id -> {generation, rebinds, last} */
static struct uloop_process stuck_proc;
static char stuck_proc_node[64];

static void at_stuck_rebound(struct uloop_process *p, int ret)
{
	(void)p;
	if (WIFEXITED(ret) && WEXITSTATUS(ret) == 0) {
		ULOG_INFO("%s: the AT port's interface was bound again; the port takes jobs again\n", stuck_proc_node);
		json_object_object_del(stuck_nodes, stuck_proc_node);
	} else {
		ULOG_WARN("%s: binding the AT port's interface again failed (status %d)\n", stuck_proc_node, ret);
	}
	stuck_proc.pid = 0;
}

static void at_stuck_fix(const char *id, const char *node, long long generation)
{
	json_object *rec = mm_registry_find(&registry, id), *st;
	char tty[PATH_MAX], dev[PATH_MAX], drvl[PATH_MAX + 16], drv[PATH_MAX], why[200];
	const char *base = strrchr(node, '/'), *result = NULL;
	char *slash;
	long long now = now_mono_ms();
	int rebinds;
	pid_t pid;

	if (!rec || frozen(id) || stuck_proc.pid > 0)
		return;
	if (!stuck_fixes)
		stuck_fixes = json_object_new_object();
	st = json_object_object_get(stuck_fixes, id);
	if (!st || json_object_get_int64(json_object_object_get(st, "generation")) != generation) {
		st = json_object_new_object();
		json_object_object_add(st, "generation", json_object_new_int64(generation));
		json_object_object_add(stuck_fixes, id, st);
	}
	rebinds = json_object_get_int(json_object_object_get(st, "rebinds"));
	if (rebinds >= AT_STUCK_REBINDS) {
		/* rebinding did not hold: the modem's USB reset (the recovery's caps apply) */
		if (rec_start(rec, MM_REC_USB_RESET, "control_port_unresponsive", false, &result, why, sizeof(why)) == REC_NOT)
			ULOG_WARN("%s: the AT port is stuck again and the USB reset did not start: %s\n", id, why);
		else
			ULOG_WARN("%s: the AT port is stuck again after %d rebinds: the modem's USB reset\n", id, rebinds);
		return;
	}
	if (json_object_object_get(st, "last") && now - json_object_get_int64(json_object_object_get(st, "last")) < AT_STUCK_REBIND_GAP_MS)
		return;
	/* the tty's USB interface: ttyUSBn -> its usb-serial port -> the interface; ttyACMn -> the interface */
	snprintf(tty, sizeof(tty), "%s/%s/device", SYS_TTY, base ? base + 1 : node);
	if (!realpath(tty, dev))
		return;
	if (!strncmp(strrchr(dev, '/') + 1, "tty", 3) && (slash = strrchr(dev, '/')))
		*slash = '\0';
	snprintf(drvl, sizeof(drvl), "%s/driver", dev);
	if (!strchr(strrchr(dev, '/') + 1, ':') || !realpath(drvl, drv))
		return;
	json_object_object_add(st, "rebinds", json_object_new_int(rebinds + 1));
	json_object_object_add(st, "last", json_object_new_int64(now));
	mm_history_add(&history, id, (long long)time(NULL), now, "at-port-rebind", strrchr(dev, '/') + 1);
	history_dirty = true;
	ULOG_WARN("%s: the AT port %s is stuck: its interface %s is unbound from %s and bound again (%d of %d)\n", id, node,
		  strrchr(dev, '/') + 1, strrchr(drv, '/') + 1, rebinds + 1, AT_STUCK_REBINDS);
	snprintf(stuck_proc_node, sizeof(stuck_proc_node), "%s", node);
	pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		char path[PATH_MAX + 16];
		const char *iface = strrchr(dev, '/') + 1;
		int fd, ok = 0;

		/* a child stuck in the kernel on unbind must not hold the engine's ubus socket and locks (review 2026-11-15) */
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		setsid();
		alarm(60);
		snprintf(path, sizeof(path), "%s/unbind", drv);
		if ((fd = open(path, O_WRONLY | O_CLOEXEC)) >= 0) {
			ok = write(fd, iface, strlen(iface)) == (ssize_t)strlen(iface);
			close(fd);
		}
		sleep(1);
		snprintf(path, sizeof(path), "%s/bind", drv);
		if ((fd = open(path, O_WRONLY | O_CLOEXEC)) >= 0) {
			ok = (write(fd, iface, strlen(iface)) == (ssize_t)strlen(iface)) && ok;
			close(fd);
		}
		_exit(ok ? 0 : 1);
	}
	stuck_proc.pid = pid;
	stuck_proc.cb = at_stuck_rebound;
	uloop_process_add(&stuck_proc);
}

static json_object *redial_due;		/* device ids: after the radio job has let go of the modem */

static void radio_redial_fired(struct uloop_timeout *t)
{
	json_object *due = redial_due;

	(void)t;
	redial_due = NULL;
	json_object_object_foreach(due, id, v) {
		json_object *rec = mm_registry_find(&registry, id);
		const char *result = NULL;
		char why[200];

		(void)v;
		if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present")))
			continue;
		snprintf(rec_source, sizeof(rec_source), "radio");
		if (rec_start(rec, MM_REC_SESSION, "session_stuck", false, &result, why, sizeof(why)) == REC_NOT)
			ULOG_INFO("radio %s: no redial after the change (%s: %s)\n", id, result ? result : "", why);
		rec_source[0] = '\0';
	}
	json_object_put(due);
}

static struct uloop_timeout redial_timer = { .cb = radio_redial_fired };

static void radio_redial(json_object *rec)
{
	if (!redial_due)
		redial_due = json_object_new_object();
	json_object_object_add(redial_due, jstr(rec, "device_id"), json_object_new_boolean(1));
	uloop_timeout_set(&redial_timer, 2000);
}

static void at_exited(struct uloop_process *p, int ret)
{
	struct at_job *j = container_of(p, struct at_job, proc);
	json_object *r, *responses = NULL, *rec, *o;
	const char *error = NULL;

	(void)ret;
	uloop_timeout_cancel(&j->kill);
	load.at_killed += j->killed;
	at_drain(j);
	at_close_out(j);
	r = j->buf ? json_tokener_parse(j->buf) : NULL;
	json_object_object_get_ex(r, "responses", &responses);
	/* a port that hung or failed settles a moment; a QModem job has no port of ours (no "" entry) */
	if (j->node[0] && (j->killed || jstr(r, "error")))
		json_object_object_add(cut_nodes, j->node, json_object_new_int64(now_mono_ms()));
	rec = mm_registry_find(&registry, j->device_id);
	if (j->killed)
		error = j->why;
	else if (!rec || json_object_get_int64(json_object_object_get(rec, "generation")) != j->generation)
		error = "the modem was re-attached; the answer is discarded";
	else if (!r)
		error = "the worker returned nothing";
	else if (jstr(r, "error") && !json_object_get_int(json_object_object_get(r, "complete")) && j->kind != AT_SMS)
		error = jstr(r, "error");

	if (j->kind == AT_SMS) {
		sms_at_exited(j, r, error);
		json_object_put(r);
		return;
	}

	if (j->kind == AT_PROBE) {
		/* binary bytes are "not-at", nothing at all "silent": neither is an AT port */
		const char *verdict = j->killed && !strncmp(j->why, "lease", 5) ? "interrupted" :
			mm_at_probe_verdict(jstr(responses, "AT") ? jstr(responses, "AT") : "",
					    jstr(responses, "AT+CGMI") ? jstr(responses, "AT+CGMI") : "");
		json_object_object_add(j->results, j->node, json_object_new_string(verdict));
		at_release(j);
		json_object_put(r);
		if (at_probe_next(j))
			return;
		o = at_reply(j, NULL);
		json_object_object_del(o, "port");
		if (rec && json_object_get_int64(json_object_object_get(rec, "generation")) == j->generation) {
			json_object *entry = json_object_new_object();
			json_object_object_add(entry, "generation", json_object_new_int64(j->generation));
			json_object_object_add(entry, "results", json_object_get(j->results));
			json_object_object_add(probes, j->device_id, entry);
			ULOG_INFO("probe %s: %s\n", j->device_id, json_object_to_json_string(j->results));
		}
		json_object_object_add(o, "results", json_object_get(j->results));
		at_finish(j, o);
		return;
	}
	o = at_reply(j, error);
	if (j->kind == AT_RADIO) {
		bool qm = !j->node[0] && !jstr(j->results, "mm_index");
		json_object *b = NULL, *srv;

		/* stage 2: a change or its rollback; the reads after it refresh the reading */
		if (jstr(j->results, "op") && radio_txn_done(j, responses, error, o) && rec &&
		    jstr(j->results, "dialect") && !strcmp(jstr(j->results, "dialect"), "fibocom-mtk"))
			radio_redial(rec);
		if (!j->node[0] && jstr(j->results, "mm_index")) {
			mm_schedule(0);
		} else if (!error && qm)
			b = mm_radio_from_qmodem(json_object_object_get(responses, "get_network_prefer"),
						 json_object_object_get(responses, "get_lockband"));
		else if (!error)
			b = mm_radio_from_at(jstr(j->results, "dialect"), responses);
		if (b) {
			json_object *entry = json_object_new_object();

			json_object_object_add(entry, "generation", json_object_new_int64(j->generation));
			json_object_object_add(entry, "at", json_object_new_int64(now_mono_ms()));
			json_object_object_add(entry, "via", json_object_new_string(qm ? "qmodem" : "at"));
			json_object_object_add(entry, "radio", b);
			json_object_object_add(radio_cache, j->device_id, entry);
		} else if (!error && !jstr(j->results, "op")) {
			json_object_object_add(o, "error", json_object_new_string(qm ? "QModem's answers said nothing about the radio settings"
										      : "the modem's answers were not understood"));
		}
		if (rec) {
			srv = served(rec);
			json_object_object_add(o, "radio_settings", json_object_get(json_object_object_get(srv, "radio_settings")));
			json_object_put(srv);
		}
		ULOG_INFO("radio %s via %s: %s\n", j->device_id, jstr(j->results, "mm_index") ? "modemmanager" : qm ? "qmodem" : "at",
			  jstr(o, "error") ? jstr(o, "error") : jstr(j->results, "op") ? jstr(j->results, "op") : "read");
		json_object_put(r);
		at_finish(j, o);
		return;
	}
	if (j->kind == AT_SIM) {
		/* the identifiers stay here: never in the reply, the log or the flash (the operator reads them on the page) */
		char imsi[24] = "", iccid[24] = "";
		bool qmodem = !j->node[0];
		bool direct = false;
		long long gen = rec ? json_object_get_int64(json_object_object_get(rec, "generation")) : -1;

		if (!error && qmodem && !jstr(j->results, "via")) {
			simid_from_qmodem(responses, imsi, sizeof(imsi), iccid, sizeof(iccid));
		} else if (!error && qmodem) {
			/* QModem's own send_at on its AT port: the same two commands as on the port */
			simid_imsi_from_cimi(sim_send_at_res(responses, "step0"), imsi, sizeof(imsi));
			simid_iccid_from_crsm(sim_send_at_res(responses, "step1"), iccid, sizeof(iccid));
		} else if (!error) {
			simid_imsi_from_cimi(jstr(responses, "AT+CIMI"), imsi, sizeof(imsi));
			simid_iccid_from_crsm(jstr(responses, SIM_CRSM), iccid, sizeof(iccid));
		}
		/* QModem answered its sim_info without an ICCID: its send_at on the port is asked at once, by the timer. A
		 * killed or failed read, and a fallback that gave nothing, wait (sim_fail): never a loop on QModem */
		direct = !error && !j->killed && qmodem && !jstr(j->results, "via") && !iccid[0];
		if (rec && gen == j->generation) {
			if (!error && !j->killed && (imsi[0] || iccid[0] || direct))
				sim_store(j->device_id, j->generation, qmodem && !jstr(j->results, "via") ? "qmodem" : "at", imsi, iccid, direct);
			else
				sim_fail(j->device_id, j->generation);
		}
		/* two lines for one QModem read were taken for a doubled read (bench 2026-10-20): each names its step */
		ULOG_INFO("sim %s: identity %s%s\n", j->device_id,
			  !qmodem ? "read on the AT port" : jstr(j->results, "via") ? "read through QModem's send_at" :
			  "read through QModem's sim_info",
			  error || j->killed ? " (no answer)" : direct ? " (no ICCID in it; asking its send_at)" :
			  !imsi[0] && !iccid[0] ? " (nothing in the answer)" : "");
		json_object_put(r);
		at_finish(j, o);	/* puts the reply made above */
		if (direct)
			uloop_timeout_set(&sim_timer, 0);
		return;
	}
	if (j->kind == AT_CELLS) {
		const char *cmd = mm_cells_command(jstr(j->results, "dialect"));
		/* through QModem's send_at (r96): its at_cfg.res holds the answer */
		const char *raw = !j->node[0] ? sim_send_at_res(responses, "step0") : jstr(responses, cmd);
		/* spec 26.1 step 2: a T77's ^CA_INFO? (its own command: a firmware without it answers ERROR to that alone) */
		const char *ca = j->node[0] ? jstr(responses, MM_T77_CA_COMMAND) : NULL;
		char *both = NULL;
		json_object *c, *heat = j->node[0] && !error ? mm_heat_from_at(jstr(responses, MM_SDX_TEMP_COMMAND),
									      jstr(responses, MM_SDX_THERM_COMMAND)) : NULL;

		if (raw && ca && asprintf(&both, "%s\n%s", raw, ca) < 0)
			both = NULL;
		c = !error && cmd && raw ? mm_cells_from_at(jstr(j->results, "dialect"), both ? both : raw) : NULL;
		free(both);
		/* a cell answer that says the modem's heat itself (Sierra !GSTATUS?) */
		if (!heat && json_object_object_get(c, "heat"))
			heat = json_object_get(json_object_object_get(c, "heat"));

		/* the answer itself is never kept or logged: SDX/X20 print the TMSI and the IP address in it */
		if (c) {
			json_object *entry = json_object_new_object();

			json_object_object_add(entry, "generation", json_object_new_int64(j->generation));
			json_object_object_add(entry, "at", json_object_new_int64(now_mono_ms()));
			json_object_object_add(entry, "cells", json_object_get(json_object_object_get(c, "cells")));
			if (heat)
				json_object_object_add(entry, "heat", json_object_get(heat));
			json_object_object_add(cells_cache, j->device_id, entry);
			json_object_object_add(o, "cells", json_object_get(json_object_object_get(c, "cells")));
			json_object_put(c);
		} else if (!error) {
			json_object_object_add(o, "cells", json_object_new_array());	/* not on a cell now */
			json_object_object_del(cells_cache, j->device_id);
		}
		if (heat)
			json_object_object_add(o, "heat", heat);
		json_object_object_add(o, "source", json_object_new_string("at"));
		json_object_put(r);
		at_finish(j, o);
		return;
	}
	if (j->kind == AT_DIAL || j->kind == AT_HANGUP) {
		json_object *x = j->results;
		char err[256] = "";

		json_object_object_add(o, "action", json_object_new_string(j->kind == AT_DIAL ? "dial" : "hangup"));
		if (j->kind == AT_DIAL && jstr(x, "flow")) {
			json_object *res = error ? NULL : mm_dial_flow_result(json_object_object_get(x, "recipe"), r, err, sizeof(err));

			if (json_object_object_get(r, "steps"))
				json_object_object_add(o, "steps", json_object_get(json_object_object_get(r, "steps")));
			if (res) {
				json_object_object_foreach(res, k, v)
					json_object_object_add(o, k, json_object_get(v));
				if (jstr(res, "plmn")) {
					if (!dial_plmn)
						dial_plmn = json_object_new_object();
					json_object_object_add(dial_plmn, j->device_id, json_object_new_string(jstr(res, "plmn")));
				}
				json_object_put(res);
			} else if (!error) {
				json_object_object_add(o, "error", json_object_new_string(err));
			}
		} else if (!error && j->kind == AT_DIAL) {
			json_object *res = mm_dial_result(json_object_object_get(x, "recipe"), responses, jstr(x, "apn"), jstr(x, "pdp"),
							  err, sizeof(err));
			if (res) {
				json_object_object_foreach(res, k, v)
					json_object_object_add(o, k, json_object_get(v));
				json_object_put(res);
			} else {
				json_object_object_add(o, "error", json_object_new_string(err));
			}
		} else if (!error) {
			/* teardown: every command answered OK */
			json_object_object_foreach(responses, cmd, raw) {
				json_object *p = mm_at_parse(cmd, json_object_get_string(raw));
				if (!json_object_get_boolean(json_object_object_get(p, "ok")) && !err[0])
					snprintf(err, sizeof(err), "%s: %s", cmd, jstr(p, "final") ? jstr(p, "final") : "no final result");
				json_object_put(p);
			}
			if (err[0])
				json_object_object_add(o, "error", json_object_new_string(err));
		}
		json_object_object_add(o, "result", json_object_new_string(json_object_object_get(o, "error") ? "failed" :
									   j->kind == AT_DIAL ? "connected" : "released"));
		json_object_object_add(o, "recipe_status", json_object_get(json_object_object_get(json_object_object_get(x, "recipe"), "status")));
		if (j->kind == AT_DIAL) {
			/* the page shows how the last dial went (the owner, 2026-10-17): the steps and the outcome, no address */
			static const char *const keep[] = { "result", "steps", "pdp", "reused", "error", NULL };
			json_object *last = json_object_new_object();
			int i;

			json_object_object_add(last, "t", json_object_new_int64((long long)time(NULL)));
			for (i = 0; keep[i]; i++)
				if (json_object_object_get(o, keep[i]))
					json_object_object_add(last, keep[i], json_object_get(json_object_object_get(o, keep[i])));
			if (!dial_last)
				dial_last = json_object_new_object();
			json_object_object_add(dial_last, j->device_id, last);
		}
		ULOG_INFO("%s %s: %s%s%s\n", j->kind == AT_DIAL ? "dial" : "hangup", j->device_id, jstr(o, "result"),
			  jstr(o, "error") ? ": " : "", jstr(o, "error") ? jstr(o, "error") : "");
		broadcast(j->kind == AT_DIAL ? "medvemodem.dial" : "medvemodem.hangup", o);
		json_object_put(r);
		at_finish(j, o);
		return;
	}
	if (!error && j->kind == AT_CONSOLE) {
		const char *cmd = json_object_get_string(json_object_array_get_idx(j->cmds, 0));
		json_object *parsed = mm_at_parse(cmd, jstr(responses, cmd) ? jstr(responses, cmd) : "");

		json_object_object_add(o, "command", json_object_new_string(cmd));
		json_object_object_foreach(parsed, k, v)
			json_object_object_add(o, k, json_object_get(v));
		json_object_put(parsed);
		at_hist_add(j->device_id, cmd, o);
	} else if (j->kind == AT_CONSOLE && rec) {
		/* a late answer of a modem gone from the registry feeds no history (HANDOFF 200 item 17) */
		at_hist_add(j->device_id, json_object_get_string(json_object_array_get_idx(j->cmds, 0)), o);
	} else if (!error) {
		bool via_q = jstr(j->results, "qmodem_section") != NULL;
		json_object *m = metrics_clean(via_q ? mm_qmodem_metrics(responses) : mm_at_metrics(responses)), *entry = json_object_new_object();
		int complete = json_object_get_int(json_object_object_get(r, "complete"));
		const char *ca_raw = via_q ? sim_send_at_res(responses, "step1") : jstr(responses, FIBOCOM_CA);
		json_object *ca = ca_raw ? mm_fibocom_ca(ca_raw) : NULL;

		if (ca)	/* the shape of ModemManager's QMI carriers: the pages read both alike */
			json_object_object_add(m, "ca", ca);
		/* the FM350 on the engine's AT: the SINR from its serving cells (the owner, 2026-11-06: «sinr FM350 не умеет
		 * отдавать?» — CESQ has none); NR's levels too where the 3GPP commands gave none */
		if (!via_q && jstr(responses, FIBOCOM_CC)) {
			json_object *cc = mm_cells_from_at("fibocom-mtk", jstr(responses, FIBOCOM_CC)), *cl = json_object_object_get(cc, "cells");

			for (size_t k = 0; cl && k < json_object_array_length(cl); k++) {
				json_object *c = json_object_array_get_idx(cl, k), *v;
				bool nr = jstr(c, "type") && !strcmp(jstr(c, "type"), "5gnr");
				json_object *to = m;

				if (!json_object_get_boolean(json_object_object_get(c, "serving")))
					continue;
				if (nr) {
					if (!(to = json_object_object_get(m, "nr"))) {
						to = json_object_new_object();
						json_object_object_add(m, "nr", to);
					}
					if ((v = json_object_object_get(c, "rsrp")) && !json_object_object_get(to, "rsrp_dbm"))
						json_object_object_add(to, "rsrp_dbm", json_object_new_int((int)json_object_get_double(v)));
					if ((v = json_object_object_get(c, "rsrq")) && !json_object_object_get(to, "rsrq_db"))
						json_object_object_add(to, "rsrq_db", json_object_new_double(json_object_get_double(v)));
				}
				if ((v = json_object_object_get(c, "sinr")) && !json_object_object_get(to, "sinr_db"))
					json_object_object_add(to, "sinr_db", json_object_new_double(json_object_get_double(v)));
			}
			json_object_put(cc);
		}

		json_object_object_add(o, "source", json_object_new_string(via_q ? "qmodem" : "at"));
		json_object_object_add(o, "age_ms", json_object_new_int(0));
		if (complete < (int)json_object_array_length(j->cmds))
			json_object_object_add(o, "partial", json_object_new_boolean(true));
		json_object_object_add(o, "metrics", json_object_get(m));
		json_object_object_add(entry, "generation", json_object_new_int64(j->generation));
		json_object_object_add(entry, "at", json_object_new_int64(now_mono_ms()));
		json_object_object_add(entry, "port", json_object_new_string(via_q ? "qmodem" : j->node));
		json_object_object_add(entry, "metrics", m);
		json_object_object_add(metrics_cache, j->device_id, entry);
		signal_sample_reading(j->device_id, m, (long long)time(NULL));	/* the history of a modem not ModemManager's */
	}
	json_object_put(r);
	at_finish(j, o);
}

static bool at_probe_next(struct at_job *j)
{
	while (json_object_array_length(j->queue)) {
		char detail[160];
		const char *why;

		snprintf(j->node, sizeof(j->node), "%s", json_object_get_string(json_object_array_get_idx(j->queue, 0)));
		json_object_array_del_idx(j->queue, 0, 1);
		why = at_lease(j, detail, sizeof(detail));
		if (why) {
			char msg[256];
			snprintf(msg, sizeof(msg), "skipped: %s%s%s", why, detail[0] ? ", " : "", detail);
			json_object_object_add(j->results, j->node, json_object_new_string(msg));
			continue;
		}
		if (at_spawn(j))
			return true;
		at_release(j);
		json_object_object_add(j->results, j->node, json_object_new_string("skipped: cannot start a worker"));
	}
	return false;
}

static void at_lease_ended(const char *lease_id, const char *reason)
{
	int i;

	for (i = 0; lease_id && i < MM_AT_JOBS; i++) {
		struct at_job *j = &at_jobs[i];
		if (j->active && j->lease_id[0] && !strcmp(j->lease_id, lease_id)) {
			j->lease_id[0] = '\0';	/* already gone from the lease list */
			if (j->proc.pid > 0) {
				j->killed = true;
				snprintf(j->why, sizeof(j->why), "lease ended: %s", reason);
				kill(-j->proc.pid, at_kill_signal());
				/* a worker the kill does not end is stuck in the kernel: let go after the grace (at_kill_fired) */
				uloop_timeout_set(&j->kill, AT_STUCK_GRACE_MS);
				/* the job that took the lease spawns before this one is reaped: it settles now, not after (audit
				 * 2026-10-21: the late answer of the cut command came into the next job's first read) */
				if (j->node[0])
					json_object_object_add(cut_nodes, j->node, json_object_new_int64(now_mono_ms()));
			}
		}
	}
}

struct at_job *at_slot(const char *device_id, enum at_kind kind, bool *same)
{
	struct at_job *free_slot = NULL;
	int i;

	*same = false;
	for (i = 0; i < MM_AT_JOBS; i++) {
		if (at_jobs[i].active && !strcmp(at_jobs[i].device_id, device_id) && at_jobs[i].kind == kind)
			*same = true;
		if (!at_jobs[i].active && !free_slot)
			free_slot = &at_jobs[i];
	}
	return *same ? NULL : free_slot;
}

bool at_running(const char *device_id, enum at_kind kind)
{
	int i;

	for (i = 0; i < MM_AT_JOBS; i++)
		if (at_jobs[i].active && !strcmp(at_jobs[i].device_id, device_id) && at_jobs[i].kind == kind)
			return true;
	return false;
}

int at_refuse(struct ubus_context *ctx, struct ubus_request_data *req, const char *id, const char *why,
		     const char *hazard)
{
	json_object *o = json_object_new_object();
	int rc;

	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "refused", json_object_new_string(why));
	if (hazard)
		json_object_object_add(o, "hazard", json_object_new_string(hazard));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* The device's AT node usable by the engine, or NULL with why. */
const char *engine_at_port(json_object *srv, const char **why)
{
	json_object *roles = json_object_object_get(srv, "port_roles"), *at = json_object_object_get(roles, "at");
	const char *owner = jstr(json_object_object_get(srv, "owner"), "at");

	*why = NULL;
	if (frozen(jstr(srv, "device_id"))) {
		*why = "the modem is frozen by the operator";
		return NULL;
	}
	if (owner && !strcmp(owner, "qmodem")) {
		*why = "QModem owns this modem's AT ports; use `ubus call qmodem send_at`";
		return NULL;
	}
	if (owner && !strcmp(owner, "modemmanager")) {
		*why = "the AT port belongs to ModemManager (spec 11.6); use mmcli for this modem";
		return NULL;
	}
	if (!jstr(at, "node")) {
		*why = "no AT port is known for this modem; `medvemodem probe <id>` looks for one";
		return NULL;
	}
	return jstr(at, "node");
}

int at_begin(struct ubus_context *ctx, struct ubus_request_data *req, json_object *rec, enum at_kind kind,
		    const char *node, json_object *cmds, json_object *queue, int timeout_ms, json_object *extra)
{
	const char *id = jstr(rec, "device_id"), *why = NULL;
	json_object *o = NULL;
	char detail[160] = "";
	bool same, started;
	struct at_job *j = at_slot(id, kind, &same);
	int rc;

	if (rec_busy(id)) {
		json_object_put(cmds);
		json_object_put(queue);
		json_object_put(extra);
		return ctx ? at_refuse(ctx, req, id, "a recovery is running on this modem", NULL) : UBUS_STATUS_UNKNOWN_ERROR;
	}
	if ((kind == AT_METRICS || kind == AT_CELLS || kind == AT_SIM) && ussd_running(id)) {
		/* bench 2026-10-16 (T99): readings beside a USSD request; they wait until it ends */
		json_object_put(cmds);
		json_object_put(queue);
		json_object_put(extra);
		return ctx ? at_refuse(ctx, req, id, "a USSD request is running on this modem; the readings wait for it", NULL) :
			     UBUS_STATUS_UNKNOWN_ERROR;
	}
	if (!j) {
		json_object_put(cmds);
		json_object_put(queue);
		json_object_put(extra);
		return ctx ? at_refuse(ctx, req, id, same ? "the same kind of AT job is already running for this modem"
							  : "too many AT jobs are running", NULL) : UBUS_STATUS_UNKNOWN_ERROR;
	}
	memset(j, 0, sizeof(*j));
	j->out.fd = -1;
	j->active = true;
	j->kind = kind;
	j->started = now_mono_ms();
	j->timeout_ms = timeout_ms;
	j->cmds = cmds;
	j->queue = queue;
	j->results = extra ? extra : json_object_new_object();
	j->generation = json_object_get_int64(json_object_object_get(rec, "generation"));
	snprintf(j->device_id, sizeof(j->device_id), "%s", id);
	if (kind == AT_PROBE) {
		started = at_probe_next(j);
		if (!started) {
			/* nothing left to probe: answer with what was skipped, if anything */
			o = at_reply(j, NULL);
			json_object_object_add(o, "results", json_object_get(j->results));
		}
	} else {
		/* a QModem job (no node) asks QModem, not a port: nothing to lease */
		snprintf(j->node, sizeof(j->node), "%s", node ? node : "");
		why = node ? at_lease(j, detail, sizeof(detail)) : NULL;
		started = !why && at_spawn(j);
		if (!started) {
			char msg[256];
			snprintf(msg, sizeof(msg), "%s%s%s", why ? why : "cannot start a worker", detail[0] ? ": " : "", detail);
			o = at_reply(j, NULL);
			json_object_object_add(o, "refused", json_object_new_string(msg));
		}
	}
	if (!started) {
		/* a job the engine started itself (no caller) says so in its result */
		rc = ctx ? reply_json(ctx, req, NULL, o) : UBUS_STATUS_UNKNOWN_ERROR;
		at_finish(j, o);
		return rc;
	}
	if (!ctx)
		return UBUS_STATUS_OK;
	j->ctx = ctx;
	ubus_defer_request(ctx, req, &j->req);
	j->deferred = true;
	return UBUS_STATUS_OK;
}

enum { AT_ID, AT_COMMAND, AT_TIMEOUT, __AT_MAX };
static const struct blobmsg_policy at_policy[] = {
	[AT_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[AT_COMMAND] = { .name = "command", .type = BLOBMSG_TYPE_STRING },
	[AT_TIMEOUT] = { .name = "timeout_ms", .type = BLOBMSG_TYPE_INT32 },
};

json_object *present_record(struct blob_attr *idattr)
{
	json_object *rec = idattr ? mm_registry_find(&registry, blobmsg_get_string(idattr)) : NULL;

	return rec && json_object_get_boolean(json_object_object_get(rec, "present")) ? rec : NULL;
}

static int m_at_command(struct ubus_context *ctx, struct ubus_object *obj,
			struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__AT_MAX];
	json_object *rec, *srv, *cmds;
	const char *cmd, *refusal, *node, *why;
	int timeout = MM_AT_CONSOLE_MS, rc;

	(void)obj; (void)method;
	blobmsg_parse(at_policy, __AT_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[AT_ID] || !tb[AT_COMMAND])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[AT_ID])))
		return mm_registry_find(&registry, blobmsg_get_string(tb[AT_ID])) ?
			at_refuse(ctx, req, blobmsg_get_string(tb[AT_ID]), "device not present", NULL) : UBUS_STATUS_NOT_FOUND;
	cmd = blobmsg_get_string(tb[AT_COMMAND]);
	if ((refusal = mm_at_device_refusal(cmd, jstr(rec, "vidpid")))) {
		ULOG_WARN("AT console refused for %s: %s (%s)\n", jstr(rec, "device_id"), refusal, mm_at_hazard(cmd));
		{
			json_object *h = json_object_new_object();

			json_object_object_add(h, "refused", json_object_new_string(refusal));
			at_hist_add(jstr(rec, "device_id"), cmd, h);
			json_object_put(h);
		}
		return at_refuse(ctx, req, jstr(rec, "device_id"), refusal, mm_at_hazard(cmd));
	}
	if (tb[AT_TIMEOUT])
		timeout = (int)blobmsg_get_u32(tb[AT_TIMEOUT]);
	if (timeout < 200)
		timeout = 200;
	if (timeout > 30000)
		timeout = 30000;
	srv = served(rec);
	node = engine_at_port(srv, &why);
	if (!node) {
		rc = at_refuse(ctx, req, jstr(rec, "device_id"), why, NULL);
		json_object_put(srv);
		return rc;
	}
	cmds = json_object_new_array();
	json_object_array_add(cmds, json_object_new_string(cmd));
	rc = at_begin(ctx, req, rec, AT_CONSOLE, node, cmds, NULL, timeout, NULL);
	json_object_put(srv);
	return rc;
}

/* ---- native AT dial (spec 19.4): asked by netifd proto medvemodem ---- */

enum { DL_ID, DL_APN, DL_PDPTYPE, DL_IFACE, __DL_MAX };
static const struct blobmsg_policy dial_policy[] = {
	[DL_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[DL_APN] = { .name = "apn", .type = BLOBMSG_TYPE_STRING },
	[DL_PDPTYPE] = { .name = "pdptype", .type = BLOBMSG_TYPE_STRING },
	[DL_IFACE] = { .name = "interface", .type = BLOBMSG_TYPE_STRING },
};

/*
 * The APN login of the dial: options auth, username and password of the netifd interface that asked (proto
 * medvemodem, this modem's device_id), read here from /etc/config/network, so the password never travels on a command
 * line (ubus call arguments show in ps). false with err when the login is unusable.
 */
static bool dial_login(json_object *job, const char *iface, const char *id, char *err, size_t err_size)
{
	static const char *const npk[] = { "network", NULL };
	json_object *all, *sec;
	bool ok = true;

	err[0] = '\0';
	if (!iface || !uci_plain_name(iface) || !(all = mm_uci_read(conf_dir, npk, err, err_size))) {
		err[0] = '\0';
		return true;
	}
	sec = json_object_object_get(json_object_object_get(all, "network"), iface);
	if (sec && jstr(sec, "proto") && !strcmp(jstr(sec, "proto"), "medvemodem") &&
	    jstr(sec, "device_id") && !strcmp(jstr(sec, "device_id"), id))
		ok = mm_dial_job_auth(job, jstr(sec, "auth"), jstr(sec, "username"), jstr(sec, "password"), err, err_size);
	json_object_put(all);
	return ok;
}

/* the tty of one USB interface of the modem (a dial recipe's data port), or NULL */
static const char *tty_of_interface(json_object *srv, int number)
{
	json_object *cands = json_object_object_get(json_object_object_get(srv, "port_roles"), "candidates");
	size_t i;

	for (i = 0; cands && i < json_object_array_length(cands); i++) {
		json_object *c = json_object_array_get_idx(cands, i);

		if (jstr(c, "kind") && !strcmp(jstr(c, "kind"), "tty") && jstr(c, "number") &&
		    strtol(jstr(c, "number"), NULL, 16) == number)	/* bInterfaceNumber is hex in sysfs */
			return jstr(c, "node");
	}
	return NULL;
}

static int m_dial(struct ubus_context *ctx, struct ubus_object *obj,
		  struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__DL_MAX];
	json_object *rec, *srv, *recipe, *cmds, *x, *t;
	const char *node, *why, *apn, *pdp, *id;
	bool hangup = !strcmp(method, "hangup");
	char err[160];
	int rc, timeout = 30000;

	(void)obj;
	blobmsg_parse(dial_policy, __DL_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[DL_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[DL_ID])))
		return mm_registry_find(&registry, blobmsg_get_string(tb[DL_ID])) ?
			at_refuse(ctx, req, blobmsg_get_string(tb[DL_ID]), "device not present", NULL) : UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	srv = served(rec);
	recipe = json_object_object_get(json_object_object_get(srv, "profile"), "dial");
	if (!recipe) {
		json_object_put(srv);
		return at_refuse(ctx, req, id, "this modem's profile has no AT dial recipe", NULL);
	}
	node = engine_at_port(srv, &why);
	if (!node) {
		rc = at_refuse(ctx, req, id, why, NULL);
		json_object_put(srv);
		return rc;
	}
	apn = tb[DL_APN] ? blobmsg_get_string(tb[DL_APN]) : "";
	pdp = mm_dial_pdp(tb[DL_PDPTYPE] ? blobmsg_get_string(tb[DL_PDPTYPE]) : NULL);
	if (!pdp) {
		json_object_put(srv);
		return at_refuse(ctx, req, id, "pdptype must be ipv4, ipv6 or ipv4v6", NULL);
	}
	if (json_object_object_get_ex(recipe, "timeout_ms", &t))
		timeout = json_object_get_int(t);
	if (!hangup && jstr(recipe, "flow") && !strcmp(jstr(recipe, "flow"), "3gpp")) {
		/* the 3GPP dialogue (HANDOFF 171): one worker step; a recovery's request for a new session is used once */
		bool cold = dial_cold && json_object_object_get(dial_cold, id);

		x = mm_dial_job(recipe, apn, pdp, cold, dial_plmn ? jstr(dial_plmn, id) : NULL, timeout - 3000, err, sizeof(err));
		if (x && !dial_login(x, tb[DL_IFACE] ? blobmsg_get_string(tb[DL_IFACE]) : NULL, id, err, sizeof(err))) {
			json_object_put(x);
			x = NULL;
		}
		if (!x) {
			json_object_put(srv);
			return at_refuse(ctx, req, id, err, NULL);
		}
		if (json_object_object_get(recipe, "data_port")) {
			/* XMM over NCM: the session's data is bound to a port of its own, never the AT port */
			int n = json_object_get_int(json_object_object_get(json_object_object_get(recipe, "data_port"), "interface"));
			const char *data = tty_of_interface(srv, n);

			if (!data || !strcmp(data, node)) {
				snprintf(err, sizeof(err), "the data port of this modem (USB interface %d) %s", n,
					 data ? "is its AT port" : "is not there");
				json_object_put(x);
				json_object_put(srv);
				return at_refuse(ctx, req, id, err, NULL);
			}
			{
				/* the worker opens it as it opens the AT port: in the device directory */
				const char *base = strrchr(data, '/');
				char path[160];

				snprintf(path, sizeof(path), "%s/%s", dev_dir, base ? base + 1 : data);
				json_object_object_add(x, "data_node", json_object_new_string(path));
			}
		}
		if (cold)
			json_object_object_del(dial_cold, id);
		json_object_object_add(x, "recipe", json_object_get(recipe));
		cmds = json_object_new_array();
		json_object_array_add(cmds, json_object_new_string("3gpp dialogue"));
		rc = at_begin(ctx, req, rec, AT_DIAL, node, cmds, NULL, timeout, x);
		json_object_put(srv);
		return rc;
	}
	cmds = mm_dial_commands(recipe, hangup ? "hangup" : "dial", apn, pdp, err, sizeof(err));
	if (!cmds) {
		json_object_put(srv);
		return at_refuse(ctx, req, id, err, NULL);
	}
	x = json_object_new_object();
	json_object_object_add(x, "recipe", json_object_get(recipe));
	json_object_object_add(x, "apn", json_object_new_string(apn));
	json_object_object_add(x, "pdp", json_object_new_string(pdp));
	rc = at_begin(ctx, req, rec, hangup ? AT_HANGUP : AT_DIAL, node, cmds, NULL, timeout, x);
	json_object_put(srv);
	return rc;
}

enum { MT_ID, MT_AGE, MT_KEPT, __MT_MAX };
static const struct blobmsg_policy metrics_policy[] = {
	[MT_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[MT_AGE] = { .name = "max_age_ms", .type = BLOBMSG_TYPE_INT32 },
	/* kept: the last reading only, whatever its age; nothing is asked of the modem's ports (the report without --at,
	 * the owner 2026-10-21 and 2026-11-04) */
	[MT_KEPT] = { .name = "kept", .type = BLOBMSG_TYPE_BOOL },
};

/* metrics ModemManager already has: no AT traffic beside it */
json_object *metrics_clean(json_object *m)
{
	json_object_iter it;
	char *drop[64];
	int n = 0, i;

	if (!json_object_is_type(m, json_type_object))
		return m;
	json_object_object_foreachC(m, it) {
		if (!it.val) {
			if (n < 64)
				drop[n++] = it.key;
		} else if (json_object_is_type(it.val, json_type_object)) {
			metrics_clean(it.val);
		}
	}
	for (i = 0; i < n; i++)
		json_object_object_del(m, drop[i]);
	return m;
}

static json_object *mm_metrics(json_object *mm)
{
	json_object *m = json_object_new_object();
	static const char *const keys[][2] = {
		{ "signal_quality", "signal_quality_percent" }, { "access_tech", "access_technology" },
		{ "operator", "operator" }, { "registration", "registration" }, { "state", "state" },
		{ "manufacturer", "manufacturer" }, { "model", "model" }, { "revision", "firmware" },
	};
	json_object *sig = json_object_object_get(mm, "signal");
	json_object *carrier = json_object_object_get(sig, "lte") ? json_object_object_get(sig, "lte") : json_object_object_get(sig, "5g");
	size_t i;

	for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		json_object *v = json_object_object_get(mm, keys[i][0]);
		if (v)
			json_object_object_add(m, keys[i][1], json_object_get(v));
	}
	/* the primary carrier (LTE, the anchor of NSA; else NR) in the AT metrics' names */
	if (json_object_object_get(carrier, "rsrp"))
		json_object_object_add(m, "rsrp_dbm", json_object_get(json_object_object_get(carrier, "rsrp")));
	if (json_object_object_get(carrier, "rsrq"))
		json_object_object_add(m, "rsrq_db", json_object_get(json_object_object_get(carrier, "rsrq")));
	if (json_object_object_get(carrier, "snr"))
		json_object_object_add(m, "sinr_db", json_object_get(json_object_object_get(carrier, "snr")));
	if (sig)
		json_object_object_add(m, "signal", json_object_get(sig));
	return m;
}

/*
 * A reading of the levels over QModem's tool or the engine's own AT port: for the page (ctx) or, with no caller, for
 * the history (signal_history_read). srv stays the caller's.
 */
static int metrics_refuse(struct ubus_context *ctx, struct ubus_request_data *req, const char *id, const char *why)
{
	return ctx ? at_refuse(ctx, req, id, why, NULL) : UBUS_STATUS_NOT_SUPPORTED;
}

static int metrics_launch(struct ubus_context *ctx, struct ubus_request_data *req, json_object *rec, json_object *srv)
{
	const char *id = jstr(rec, "device_id"), *node, *why;
	json_object *cmds;
	int i, rc;

	/* QModem holds the modem's AT ports: its own cell_info answers (the owner, 2026-10-19: the FM350 under QModem) */
	if (!strcmp(jstr(json_object_object_get(srv, "owner"), "control") ? jstr(json_object_object_get(srv, "owner"), "control") : "", "qmodem")) {
		const char *sec = qmodem_section_of(rec);
		json_object *x;

		if (!sec || !qmodem_installed())
			rc = metrics_refuse(ctx, req, id, "QModem drives this modem but has no section for it");
		else if (qmodem_foreign_port(rec, sec))
			rc = metrics_refuse(ctx, req, id, "QModem's section names an AT port that is not this modem's; nothing is sent");
		else {
			const char *port = qmodem_img ? jstr(json_object_object_get(qmodem_img, sec), "at_port") : NULL;
			char ca[320];

			cmds = json_object_new_array();
			json_object_array_add(cmds, json_object_new_string("cell_info"));
			/* the FM350's carriers: QModem's cell_info names the primary band only (its LTE path reads no SCC), so its
			 * own send_at asks AT+GTCAINFO? on the port its section names (the owner, 2026-10-23: «CA для FM350 под QModem») */
			if (port && fibocom_mtk(srv) &&
			    snprintf(ca, sizeof(ca), "send_at\x1f{\"at\":\"%s\",\"port\":\"%s\"}", FIBOCOM_CA, port) < (int)sizeof(ca))
				json_object_array_add(cmds, json_object_new_string(ca));
			x = json_object_new_object();
			json_object_object_add(x, "qmodem_section", json_object_new_string(sec));
			rc = at_begin(ctx, req, rec, AT_METRICS, NULL, cmds, NULL, MM_QMODEM_CTRL_MS, x);
		}
		return rc;
	}
	node = engine_at_port(srv, &why);
	if (!node)
		return metrics_refuse(ctx, req, id, why);
	cmds = json_object_new_array();
	for (i = 0; mm_at_metrics_commands[i]; i++)
		json_object_array_add(cmds, json_object_new_string(mm_at_metrics_commands[i]));
	if (fibocom_mtk(srv)) {	/* the FM350's carriers and cells, last: an older firmware that refuses one costs no other */
		json_object_array_add(cmds, json_object_new_string(FIBOCOM_CA));
		json_object_array_add(cmds, json_object_new_string(FIBOCOM_CC));
	}
	return at_begin(ctx, req, rec, AT_METRICS, node, cmds, NULL, MM_AT_METRICS_MS, NULL);
}

/*
 * The history of a modem neither ModemManager nor its own web reads (the FM350 under QModem or the engine's AT): one
 * reading a minute with no page open (the owner, 2026-11-06: «график приема почему-то с прерываниями» — it filled only
 * while a page asked). Not a frozen modem, not beside a recovery or a USSD (at_begin refuses those), not when a reading
 * younger than 45 s is kept.
 */
void signal_history_read(void)
{
	if (!registry.devices)
		return;
	json_object_object_foreach(registry.devices, id, rec) {
		json_object *srv, *cached = json_object_object_get(metrics_cache, id);
		const char *ctl, *atown;

		if (!json_object_get_boolean(json_object_object_get(rec, "present")) || frozen(id))
			continue;
		if (cached && json_object_get_int64(json_object_object_get(cached, "generation")) == json_object_get_int64(json_object_object_get(rec, "generation")) &&
		    now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at")) < 45000)
			continue;
		srv = served(rec);
		ctl = jstr(json_object_object_get(srv, "owner"), "control");
		atown = jstr(json_object_object_get(srv, "owner"), "at");
		/* QModem's tool, or an AT port that is the engine's own; ModemManager's and a web modem's have their timers */
		if (ctl && strcmp(ctl, "modemmanager") && strcmp(ctl, "hilink") &&
		    (!strcmp(ctl, "qmodem") || (atown && !strcmp(atown, "medvemodem"))))
			metrics_launch(NULL, NULL, rec, srv);
		json_object_put(srv);
	}
}

static int m_metrics(struct ubus_context *ctx, struct ubus_object *obj,
		     struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__MT_MAX];
	json_object *rec, *srv, *mm, *cached, *o;
	const char *id;
	long long max_age = MM_METRICS_MAX_AGE_MS;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(metrics_policy, __MT_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[MT_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[MT_ID])))
		return mm_registry_find(&registry, blobmsg_get_string(tb[MT_ID])) ?
			at_refuse(ctx, req, blobmsg_get_string(tb[MT_ID]), "device not present", NULL) : UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	/* a frozen modem is read by no one: not its AT port, not QModem's tool, not its own web page */
	if (frozen(id))
		return at_refuse(ctx, req, id, "the modem is frozen by the operator", NULL);
	if (tb[MT_AGE])
		max_age = (int)blobmsg_get_u32(tb[MT_AGE]);
	mm_maybe_refresh();
	srv = served(rec);
	mm = json_object_object_get(srv, "modemmanager");
	if (mm && !strcmp(jstr(json_object_object_get(srv, "owner"), "control") ? jstr(json_object_object_get(srv, "owner"), "control") : "", "modemmanager")) {
		o = json_object_new_object();
		json_object_object_add(o, "device_id", json_object_new_string(id));
		json_object_object_add(o, "source", json_object_new_string("modemmanager"));
		json_object_object_add(o, "age_ms", mm_last_done < 0 ? NULL : json_object_new_int64(now_mono_ms() - mm_last_done));
		json_object_object_add(o, "metrics", metrics_clean(mm_metrics(mm)));
		rc = reply_json(ctx, req, NULL, o);
		json_object_put(o);
		json_object_put(srv);
		return rc;
	}
	/* a HiLink or ZTE web modem: its own web page gives the signal (HANDOFF 164) */
	if (jstr(json_object_object_get(srv, "owner"), "control") && !strcmp(jstr(json_object_object_get(srv, "owner"), "control"), "hilink")) {
		rc = web_metrics(ctx, req, rec, srv, max_age);
		json_object_put(srv);
		return rc;
	}
	cached = json_object_object_get(metrics_cache, id);
	if (tb[MT_KEPT] && blobmsg_get_bool(tb[MT_KEPT]))
		max_age = 1LL << 60;
	if (cached && json_object_get_int64(json_object_object_get(cached, "generation")) == json_object_get_int64(json_object_object_get(rec, "generation")) &&
	    now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at")) <= max_age) {
		o = json_object_new_object();
		json_object_object_add(o, "device_id", json_object_new_string(id));
		json_object_object_add(o, "port", json_object_get(json_object_object_get(cached, "port")));
		json_object_object_add(o, "source", json_object_new_string(jstr(cached, "port") && !strcmp(jstr(cached, "port"), "qmodem") ? "qmodem" : "at"));
		json_object_object_add(o, "age_ms", json_object_new_int64(now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at"))));
		json_object_object_add(o, "metrics", json_object_get(json_object_object_get(cached, "metrics")));
		rc = reply_json(ctx, req, NULL, o);
		json_object_put(o);
		json_object_put(srv);
		return rc;
	}
	if (tb[MT_KEPT] && blobmsg_get_bool(tb[MT_KEPT])) {
		json_object_put(srv);
		return at_refuse(ctx, req, id, "no reading of the modem kept yet", NULL);
	}
	rc = metrics_launch(ctx, req, rec, srv);
	json_object_put(srv);
	return rc;
}

/*
 * The serving cells over AT (r34, bench 2026-10-15 F18): with ModemManager's cell info off
 * the page asks this while «Радио» is open; one dialect command (Foxconn AT^DEBUG?) at most
 * once in CELLS_EVERY_MS per modem, through the engine's AT port. The cells then stand in the
 * device record's modemmanager.cells for CELLS_SHOWN_MS.
 */
enum { CL_ID, CL_LEVELS, __CL_MAX };
static const struct blobmsg_policy cells_policy[] = {
	[CL_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	/* spec 26.1 step 3: the page has the carriers (QMI's CA info) without the secondaries' levels; an SDX55's AT^DEBUG?
	 * gives them, so it is asked even where ModemManager's cell info is on */
	[CL_LEVELS] = { .name = "levels", .type = BLOBMSG_TYPE_BOOL },
};

static int m_cells(struct ubus_context *ctx, struct ubus_object *obj,
		   struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__CL_MAX];
	json_object *rec, *srv, *o, *cached, *cmds, *extra;
	const char *id, *dialect, *cmd, *node, *why = NULL;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(cells_policy, __CL_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[CL_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[CL_ID])))
		return mm_registry_find(&registry, blobmsg_get_string(tb[CL_ID])) ?
			at_refuse(ctx, req, blobmsg_get_string(tb[CL_ID]), "device not present", NULL) : UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	srv = served(rec);
	dialect = jstr(json_object_object_get(srv, "radio_settings"), "dialect");
	cmd = mm_cells_command(dialect);
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	/* Compal RXM-G1 (SG500M2-X) shares 05c6:9025 with the T99W175 and answers AT^DEBUG? with nothing (a reference project's field notes) */
	{
		json_object *mmv = json_object_object_get(srv, "modemmanager");
		const char *model = jstr(mmv, "model"), *maker = jstr(mmv, "manufacturer");

		if (cmd && ((model && (strcasestr(model, "SG500") || strcasestr(model, "RXM"))) || (maker && strcasestr(maker, "compal"))))
			cmd = NULL;
	}
	/* ModemManager's cell info stands in only for a modem it holds (an FM350 on the engine's AT is not one) */
	if ((cell_info_on && json_object_object_get(srv, "modemmanager") &&
	     !(tb[CL_LEVELS] && blobmsg_get_bool(tb[CL_LEVELS]) && dialect && (!strcmp(dialect, "foxconn-sdx") || !strcmp(dialect, "quectel")))) || !cmd) {
		bool mm_cells = cell_info_on && json_object_object_get(srv, "modemmanager");

		json_object_object_add(o, "source", json_object_new_string(mm_cells ? "modemmanager" : "none"));
		if (!mm_cells)
			json_object_object_add(o, "why", json_object_new_string("no cell report over AT for this modem yet"));
		goto reply;
	}
	cached = json_object_object_get(cells_cache, id);
	if (cached && json_object_get_int64(json_object_object_get(cached, "generation")) == json_object_get_int64(json_object_object_get(rec, "generation")) &&
	    now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at")) < CELLS_EVERY_MS) {
		json_object_object_add(o, "source", json_object_new_string("at"));
		json_object_object_add(o, "age_ms", json_object_new_int64(now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at"))));
		json_object_object_add(o, "cells", json_object_get(json_object_object_get(cached, "cells")));
		if (json_object_object_get(cached, "heat"))
			json_object_object_add(o, "heat", json_object_get(json_object_object_get(cached, "heat")));
		goto reply;
	}
	/* r96: QModem holds the FM350's AT ports: its own send_at asks the cell report on the port its section names, as
	 * for the carriers (the owner, 2026-10-24: «смс/соседи максимально у всех моделей») */
	if (!strcmp(jstr(json_object_object_get(srv, "owner"), "control") ? jstr(json_object_object_get(srv, "owner"), "control") : "", "qmodem")) {
		const char *sec = qmodem_section_of(rec), *port = sec && qmodem_img ? jstr(json_object_object_get(qmodem_img, sec), "at_port") : NULL;
		char line[320];

		json_object_put(o);
		/* spec 26.1 step 5: a Quectel's AT+QCAINFO goes on the engine's own port only, never through QModem's */
		if (!strcmp(dialect, "quectel") || !sec || !qmodem_installed() || !port || qmodem_foreign_port(rec, sec) ||
		    snprintf(line, sizeof(line), "send_at\x1f{\"at\":\"%s\",\"port\":\"%s\"}", cmd, port) >= (int)sizeof(line)) {
			rc = at_refuse(ctx, req, id, !strcmp(dialect, "quectel") ? "QModem drives this modem; its carriers are read on the engine's own AT port only" :
					     "QModem drives this modem and names no AT port of it", NULL);
			json_object_put(srv);
			return rc;
		}
		cmds = json_object_new_array();
		json_object_array_add(cmds, json_object_new_string(line));
		extra = json_object_new_object();
		json_object_object_add(extra, "dialect", json_object_new_string(dialect));
		json_object_object_add(extra, "qmodem_section", json_object_new_string(sec));
		rc = at_begin(ctx, req, rec, AT_CELLS, NULL, cmds, NULL, MM_QMODEM_CTRL_MS, extra);
		json_object_put(srv);
		return rc;
	}
	if (!(node = engine_at_port(srv, &why))) {
		json_object_put(o);
		rc = at_refuse(ctx, req, id, why, NULL);
		json_object_put(srv);
		return rc;
	}
	json_object_put(o);
	cmds = json_object_new_array();
	json_object_array_add(cmds, json_object_new_string(cmd));
	if (!strcmp(dialect, "foxconn-t77"))
		json_object_array_add(cmds, json_object_new_string(MM_T77_CA_COMMAND));
	/* the modem's heat beside the cells (2026-11-02): degrees on a T99, a throttling level where there are none */
	if (!strcmp(dialect, "foxconn-sdx")) {
		json_object_array_add(cmds, json_object_new_string(MM_SDX_TEMP_COMMAND));
		json_object_array_add(cmds, json_object_new_string(MM_SDX_THERM_COMMAND));
	}
	extra = json_object_new_object();
	json_object_object_add(extra, "dialect", json_object_new_string(dialect));
	rc = at_begin(ctx, req, rec, AT_CELLS, node, cmds, NULL, !strcmp(dialect, "foxconn-sdx") ? 5000 : 3000, extra);
	json_object_put(srv);
	return rc;
reply:
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	json_object_put(srv);
	return rc;
}

/*
 * The SIM's identity of the modems ModemManager does not hold, read by the engine itself (r64): a job without a caller
 * (QModem's sim_info first, the AT port for an ICCID QModem did not give), once per attachment and every SIM_EVERY_MS.
 */
static void sim_tick(struct uloop_timeout *t)
{
	json_object *list = mm_registry_list(&registry, false);
	long long now = now_mono_ms();
	size_t i;

	(void)t;
	for (i = 0; list && i < json_object_array_length(list); i++) {
		json_object *rec = json_object_array_get_idx(list, i), *srv, *mm, *e, *x, *cmds;
		const char *id = jstr(rec, "device_id"), *why = NULL, *node, *section;
		long long gen = json_object_get_int64(json_object_object_get(rec, "generation"));
		bool direct, fresh;

		/* a frozen modem is touched by no one, QModem's methods included */
		if (!id || !json_object_get_boolean(json_object_object_get(rec, "present")) || frozen(id))
			continue;
		e = sim_entry(id);
		direct = e && json_object_get_boolean(json_object_object_get(e, "direct"));
		fresh = e && json_object_get_int64(json_object_object_get(e, "generation")) == gen && !direct &&
			now - json_object_get_int64(json_object_object_get(e, "at")) < sim_wait(e);
		if (fresh)
			continue;
		srv = served(rec);
		mm = json_object_object_get(srv, "modemmanager");
		/* ModemManager holds the modem and its SIM: its own card is the reading */
		if (jstr(json_object_object_get(mm, "sim_card"), "iccid")) {
			json_object_put(srv);
			continue;
		}
		section = jstr(json_object_object_get(srv, "owner"), "control") && !strcmp(jstr(json_object_object_get(srv, "owner"), "control"), "qmodem") ?
			  qmodem_section_of(rec) : NULL;
		if (section && qmodem_installed() && !qmodem_foreign_port(rec, section)) {
			/* QModem holds the AT ports: its sim_info first, then its own send_at on the port its section names (the
			 * engine keeps off a port QModem owns; qmodem_foreign_port above made sure it is this modem's) */
			const char *port = qmodem_img ? jstr(json_object_object_get(qmodem_img, section), "at_port") : NULL;
			char a[320], b[320];

			cmds = json_object_new_array();
			x = json_object_new_object();
			json_object_object_add(x, "qmodem_section", json_object_new_string(section));
			if (!direct) {
				json_object_array_add(cmds, json_object_new_string("sim_info"));
			} else if (port && snprintf(a, sizeof(a), "send_at\x1f{\"at\":\"AT+CIMI\",\"port\":\"%s\"}", port) < (int)sizeof(a) &&
				   snprintf(b, sizeof(b), "send_at\x1f{\"at\":\"%s\",\"port\":\"%s\"}", SIM_CRSM, port) < (int)sizeof(b)) {
				json_object_array_add(cmds, json_object_new_string(a));
				json_object_array_add(cmds, json_object_new_string(b));
				json_object_object_add(x, "via", json_object_new_string("send_at"));
			} else {
				json_object_put(cmds);
				json_object_put(x);
				json_object_put(srv);
				continue;
			}
			/* refused (a recovery, USSD, a busy port): waits like a failed read */
			if (at_begin(NULL, NULL, rec, AT_SIM, NULL, cmds, NULL, MM_QMODEM_CTRL_MS, x) != UBUS_STATUS_OK)
				sim_fail(id, gen);
		} else if (!section && (node = engine_at_port(srv, &why))) {
			cmds = json_object_new_array();
			json_object_array_add(cmds, json_object_new_string("AT+CIMI"));
			json_object_array_add(cmds, json_object_new_string(SIM_CRSM));
			if (at_begin(NULL, NULL, rec, AT_SIM, node, cmds, NULL, 3000, NULL) != UBUS_STATUS_OK)
				sim_fail(id, gen);
		}
		json_object_put(srv);
	}
	json_object_put(list);
	uloop_timeout_set(&sim_timer, SIM_TICK_MS);
}

void sim_mgr_start(void)
{
	/* half a minute after the start: the bring-ups and the first readings go first (MEDVEMODEM_SIM_FIRST_MS: tests) */
	const char *first = getenv("MEDVEMODEM_SIM_FIRST_MS");

	sim_timer.cb = sim_tick;
	uloop_timeout_set(&sim_timer, first && *first ? atoi(first) : 30000);
}

/* ---- radio settings (spec 25.9.12): read through QModem or the AT dialect ---- */

/* a modem ModemManager holds whose cell lock the engine reads over its own AT port (T99W175: AT^LTE_LOCK?) */
bool mm_lock_dialect(const char *dialect)
{
	return dialect && !strcmp(dialect, "foxconn-sdx");
}

static bool mm_lock_beside(json_object *srv, json_object *cached, bool refresh)
{
	const char *why = NULL;

	if (!mm_lock_dialect(jstr(json_object_object_get(srv, "radio_settings"), "dialect")) || !engine_at_port(srv, &why))
		return false;
	return refresh || !cached || strcmp(jstr(cached, "via") ? jstr(cached, "via") : "", "at") ||
	       json_object_get_int64(json_object_object_get(cached, "generation")) != json_object_get_int64(json_object_object_get(srv, "generation")) ||
	       now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at")) > MM_RADIO_MAX_AGE_MS;
}

enum { RS_ID, RS_REFRESH, __RS_MAX };
static const struct blobmsg_policy radio_policy[] = {
	[RS_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[RS_REFRESH] = { .name = "refresh", .type = BLOBMSG_TYPE_BOOL },
};

static int m_radio(struct ubus_context *ctx, struct ubus_object *obj,
		   struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__RS_MAX];
	json_object *rec, *srv, *rs, *cmds, *cached, *o;
	const char *id, *via, *dialect, *node = NULL, *why = NULL, *const *list;
	bool refresh;
	int rc, i;

	(void)obj; (void)method;
	blobmsg_parse(radio_policy, __RS_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[RS_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[RS_ID])))
		return mm_registry_find(&registry, blobmsg_get_string(tb[RS_ID])) ?
			at_refuse(ctx, req, blobmsg_get_string(tb[RS_ID]), "device not present", NULL) : UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	refresh = tb[RS_REFRESH] && blobmsg_get_bool(tb[RS_REFRESH]);
	mm_maybe_refresh();
	srv = served(rec);
	rs = json_object_object_get(srv, "radio_settings");
	via = jstr(rs, "via");
	dialect = jstr(rs, "dialect");
	cached = json_object_object_get(radio_cache, id);
	/* ModemManager's report is always current; a recent reading stands unless asked again. A T99 under
	 * ModemManager is still read over the engine's AT port for its cell lock, which ModemManager has not */
	if ((!strcmp(via, "modemmanager") && !mm_lock_beside(srv, cached, refresh)) ||
	    (strcmp(via, "none") && !refresh && cached &&
	     now_mono_ms() - json_object_get_int64(json_object_object_get(cached, "at")) <= MM_RADIO_MAX_AGE_MS)) {
		o = json_object_new_object();
		json_object_object_add(o, "device_id", json_object_new_string(id));
		json_object_object_add(o, "radio_settings", json_object_get(rs));
		rc = reply_json(ctx, req, NULL, o);
		json_object_put(o);
		json_object_put(srv);
		return rc;
	}
	if (!strcmp(jstr(json_object_object_get(srv, "owner"), "control") ? jstr(json_object_object_get(srv, "owner"), "control") : "", "qmodem")) {
		const char *sec = qmodem_section_of(rec);
		json_object *x;

		if (!sec || !qmodem_installed()) {
			json_object_put(srv);
			return at_refuse(ctx, req, id, "QModem drives this modem but has no section for it", NULL);
		}
		if (qmodem_foreign_port(rec, sec)) {
			json_object_put(srv);
			return at_refuse(ctx, req, id, "QModem's section names an AT port that is not this modem's; nothing is sent", NULL);
		}
		cmds = json_object_new_array();
		json_object_array_add(cmds, json_object_new_string("get_network_prefer"));
		json_object_array_add(cmds, json_object_new_string("get_lockband"));
		x = json_object_new_object();
		json_object_object_add(x, "qmodem_section", json_object_new_string(sec));
		rc = at_begin(ctx, req, rec, AT_RADIO, NULL, cmds, NULL, MM_QMODEM_CTRL_MS, x);
		json_object_put(srv);
		return rc;
	}
	if (!dialect || !(list = mm_radio_commands(dialect))) {
		rc = at_refuse(ctx, req, id, jstr(rs, "why") ? jstr(rs, "why") : "no reader for this modem", NULL);
		json_object_put(srv);
		return rc;
	}
	node = engine_at_port(srv, &why);
	if (!node) {
		rc = at_refuse(ctx, req, id, why, NULL);
		json_object_put(srv);
		return rc;
	}
	cmds = json_object_new_array();
	for (i = 0; list[i]; i++)
		json_object_array_add(cmds, json_object_new_string(list[i]));
	o = json_object_new_object();
	json_object_object_add(o, "dialect", json_object_new_string(dialect));
	rc = at_begin(ctx, req, rec, AT_RADIO, node, cmds, NULL, MM_AT_RADIO_MS, o);
	json_object_put(srv);
	return rc;
}


static int m_probe(struct ubus_context *ctx, struct ubus_object *obj,
		   struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__DEVICE_MAX];
	json_object *rec, *srv, *cands, *queue, *cmds;
	const char *id, *control;
	bool ignored;
	size_t i;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(device_policy, __DEVICE_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[DEVICE_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[DEVICE_ID])))
		return mm_registry_find(&registry, blobmsg_get_string(tb[DEVICE_ID])) ?
			at_refuse(ctx, req, blobmsg_get_string(tb[DEVICE_ID]), "device not present", NULL) : UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	srv = served(rec);
	control = jstr(json_object_object_get(srv, "owner"), "control");
	if (control && !strcmp(control, "qmodem")) {
		json_object_put(srv);
		return at_refuse(ctx, req, id, "QModem owns this modem and has resolved its ports", NULL);
	}
	/* a control-only modem (profile mm_ports "control"): ModemManager ignores its ttys by the engine's own
	 * rule (F12), so they are probed beside it without a race; before, the T99 had no way to an AT port */
	ignored = jstr(json_object_object_get(srv, "profile"), "mm_ports") &&
		  !strcmp(jstr(json_object_object_get(srv, "profile"), "mm_ports"), "control");
	if (control && !strcmp(control, "modemmanager") && !ignored) {
		json_object_put(srv);
		return at_refuse(ctx, req, id, "ModemManager owns this modem and has resolved its ports", NULL);
	}
	/* an inhibited modem is out of ModemManager's reach: nothing probes beside us */
	if (!inhibited(id) && !ignored &&
	    (!strcmp(mm_backend, "available") || !strcmp(mm_backend, "unknown") || !strcmp(mm_backend, "timeout"))) {
		json_object_put(srv);
		return at_refuse(ctx, req, id, "ModemManager may still be probing this modem (spec 11.7); "
					       "probing beside it would race for the ports", NULL);
	}
	cands = json_object_object_get(json_object_object_get(srv, "port_roles"), "candidates");
	queue = json_object_new_array();
	for (i = 0; cands && i < json_object_array_length(cands); i++) {
		json_object *c = json_object_array_get_idx(cands, i);
		if (strcmp(jstr(c, "kind"), "tty") || strcmp(jstr(c, "role"), "unknown") || strcmp(jstr(c, "owner"), "none"))
			continue;
		if (jstr(c, "source") && !strcmp(jstr(c, "source"), "probe"))
			continue;	/* already probed in this generation */
		if (mm_at_no_at_port(jstr(rec, "vidpid"), jstr(c, "number")))
			continue;	/* H12: never sent AT */
		json_object_array_add(queue, json_object_new_string(jstr(c, "node")));
	}
	cmds = json_object_new_array();
	json_object_array_add(cmds, json_object_new_string("AT"));
	json_object_array_add(cmds, json_object_new_string("AT+CGMI"));
	rc = at_begin(ctx, req, rec, AT_PROBE, NULL, cmds, queue, MM_AT_PROBE_MS, NULL);
	json_object_put(srv);
	return rc;
}

/* ---- recovery controller (spec 22.1) ---- */

/*
 * One recovery at a time on the router: a ladder step on one modem never runs
 * beside another's (22.1.9), and never beside a bring-up, a configuration
 * change, an eSIM operation or an AT job of the same modem (22.1.7). The
 * policy (what may run, loop protection) is recovery.c; this executes a step
 * through the modem's owner or sysfs and then verifies that the modem came
 * back usable (22.1.11), not merely that it re-enumerated.
 */
#define MM_REC_TICK_MS 1000
#define MM_REC_DEADLINE_MS 120000
#define MM_REC_SETTLE_MS 5000
#define MM_REC_CMD_MS 30000
static const struct mm_recovery_cfg rec_cfg = MM_RECOVERY_DEFAULTS;
static json_object *rec_states;		/* device_id -> recovery state */
enum rec_phase { REC_CMD, REC_SECOND, REC_VERIFY };
static struct {
	bool active, deferred, proc_running, mm_owned, automatic, web;
	enum rec_phase phase;
	int level, cmd, ncmds, ports0;
	long long gen0;
	char device_id[160], reason[40], usb[64], mm_index[16], iface[32], port[PATH_MAX];
	const char *cmds[2];
	long long started, deadline, verify_from, last_nudge;
	unsigned long passes0;
	struct ubus_context *ctx;
	struct ubus_request_data req;
	json_object *steps, *unbound;
	char waiting[96];	/* what verification still waits for, for the page */
	struct uloop_process proc;
	struct uloop_timeout timer, kill;
} rj;

bool rec_busy(const char *id)
{
	return rj.active && id && !strcmp(rj.device_id, id);
}

static json_object *rec_state(const char *id)
{
	json_object *s = NULL;

	if (!rec_states)
		rec_states = json_object_new_object();
	if (!json_object_object_get_ex(rec_states, id, &s)) {
		s = json_object_new_object();
		json_object_object_add(rec_states, id, s);
	}
	return s;
}

/* who carries out the ladder's steps: the control owner, or "web" for a HiLink no ModemManager holds (its reboot
 * goes through its own web, r117) */
static const char *rec_owner(json_object *rec, json_object *srv)
{
	const char *owner = jstr(json_object_object_get(srv, "owner"), "control");

	if ((!owner || (strcmp(owner, "modemmanager") && strcmp(owner, "qmodem"))) && rec && (web_huawei(rec, srv) || web_zte(rec, srv)))
		return "web";	/* a ZTE goform web reconnects and reboots too (2026-11-03) */
	if ((!owner || (strcmp(owner, "modemmanager") && strcmp(owner, "qmodem"))) && rec && (web_mark(rec, srv) || web_uz(rec, srv)))
		return "web-reboot";	/* an MF150-4's or a UZ801's web reboots it, neither has a reconnect (2026-10-30) */
	return owner;
}

static json_object *recovery_view_of(const char *id, json_object *profile, const char *owner)
{
	json_object *s = rec_states ? json_object_object_get(rec_states, id) : NULL, *v, *levels;
	size_t i;

	v = mm_recovery_view(s, profile, owner, rec_busy(id) ? rj.level : -1, rec_busy(id) ? rj.reason : NULL,
			     now_mono_ms(), (long long)time(NULL), &rec_cfg);
	/* every level with why not, and whether the watch may start it by itself (the page's ladder) */
	levels = mm_recovery_levels(profile, owner, false);
	for (i = 0; i < json_object_array_length(levels); i++)
		json_object_object_add(json_object_array_get_idx(levels, i), "automatic",
				       json_object_new_boolean(!mm_recovery_support(profile, owner, (int)i, true)));
	json_object_object_add(v, "levels", levels);
	/* the page follows a running step: what was done, for how long, what it still waits for */
	if (rec_busy(id)) {
		json_object *p = json_object_new_object();
		json_object_object_add(p, "steps", json_object_get(rj.steps));
		json_object_object_add(p, "elapsed_s", json_object_new_int64((now_mono_ms() - rj.started) / 1000));
		json_object_object_add(p, "limit_s", json_object_new_int64((rj.deadline - rj.started) / 1000));
		if (*rj.waiting)
			json_object_object_add(p, "waiting", json_object_new_string(rj.waiting));
		json_object_object_add(v, "progress", p);
	}
	if (s && json_object_object_get(s, "last_steps"))
		json_object_object_add(v, "last_steps", json_object_get(json_object_object_get(s, "last_steps")));
	return v;
}

static void rec_log(const char *step, const char *detail)
{
	json_object *s = json_object_new_object();

	json_object_object_add(s, "step", json_object_new_string(step));
	if (detail && *detail)
		json_object_object_add(s, "detail", json_object_new_string(detail));
	json_object_object_add(s, "ms", json_object_new_int64(now_mono_ms() - rj.started));
	json_object_array_add(rj.steps, s);
}

static void rec_event(const char *event, const char *detail)
{
	json_object *e = json_object_new_object();

	json_object_object_add(e, "event", json_object_new_string(event));
	json_object_object_add(e, "device_id", json_object_new_string(rj.device_id));
	json_object_object_add(e, "level", json_object_new_string(mm_recovery_level_name(rj.level)));
	json_object_object_add(e, "reason", json_object_new_string(rj.reason));
	if (detail)
		json_object_object_add(e, "detail", json_object_new_string(detail));
	broadcast("medvemodem.recovery", e);
	json_object_put(e);
}

static int ports_of(json_object *rec)
{
	json_object *p = json_object_object_get(rec, "ports");
	int n = 0;

	json_object_object_foreach(p, k, v) {
		(void)k;
		if (json_object_is_type(v, json_type_array))
			n += (int)json_object_array_length(v);
	}
	return n;
}

/* «disabled» is a modem ModemManager holds and nobody has asked to connect yet: after a power cycle it stays so
 * until a bring-up enables it (field 2026-10-17, an E171 without an interface: the step read «not usable») */
static bool mm_state_usable(const char *s)
{
	static const char *const ok[] = { "disabled", "enabled", "searching", "registered", "connecting", "connected",
					  "disconnecting", NULL };
	int i;

	for (i = 0; s && ok[i]; i++)
		if (!strcmp(s, ok[i]))
			return true;
	return false;
}

static json_object *uplink_state(const char *key);

static void rec_finish(const char *result, const char *detail)
{
	json_object *o = json_object_new_object(), *state = rec_state(rj.device_id);
	char line[96];

	uloop_timeout_cancel(&rj.timer);
	uloop_timeout_cancel(&rj.kill);
	if (rj.proc_running && rj.proc.pid > 0) {
		kill(-rj.proc.pid, SIGKILL);
		uloop_process_delete(&rj.proc);
	}
	rec_log(result, detail);
	mm_recovery_record(state, rj.level, rj.reason, result, detail, now_mono_ms(), (long long)time(NULL), &rec_cfg);
	snprintf(line, sizeof(line), "%s %s", mm_recovery_level_name(rj.level), result);
	mm_history_add(&history, rj.device_id, (long long)time(NULL), now_mono_ms(), "recovery-end", line);
	history_dirty = true;
	ULOG_INFO("recovery %s of %s (%s): %s%s%s\n", mm_recovery_level_name(rj.level), rj.device_id, rj.reason, result,
		  detail ? ": " : "", detail ? detail : "");
	json_object_object_add(o, "device_id", json_object_new_string(rj.device_id));
	json_object_object_add(o, "level", json_object_new_string(mm_recovery_level_name(rj.level)));
	json_object_object_add(o, "reason", json_object_new_string(rj.reason));
	json_object_object_add(o, "result", json_object_new_string(result));
	if (detail)
		json_object_object_add(o, "detail", json_object_new_string(detail));
	json_object_object_add(o, "steps", json_object_get(rj.steps));
	json_object_object_add(o, "elapsed_ms", json_object_new_int64(now_mono_ms() - rj.started));
	/* how the last one went, for the page after the fact */
	{
		json_object *ls = json_object_new_object();
		json_object_object_add(ls, "level", json_object_new_string(mm_recovery_level_name(rj.level)));
		json_object_object_add(ls, "result", json_object_new_string(result));
		if (detail)
			json_object_object_add(ls, "detail", json_object_new_string(detail));
		json_object_object_add(ls, "at", json_object_new_int64((long long)time(NULL)));
		json_object_object_add(ls, "elapsed_s", json_object_new_int64((now_mono_ms() - rj.started) / 1000));
		json_object_object_add(ls, "steps", json_object_get(rj.steps));
		json_object_object_add(state, "last_steps", ls);
	}
	if (rj.deferred) {
		reply_json(rj.ctx, &rj.req, NULL, o);
		ubus_complete_deferred_request(rj.ctx, &rj.req, UBUS_STATUS_OK);
	}
	rec_event(!strcmp(result, "succeeded") ? "recovery_succeeded" : "recovery_failed", detail);
	/* a step «succeeded» when the session is back, not the internet (HANDOFF 200 item 10): the first reachability
	 * answer after it says which (reach_done) */
	if (!strcmp(result, "succeeded"))
		json_object_object_add(uplink_state(rj.device_id), "after_recovery", json_object_new_int64(now_mono_ms()));
	json_object_put(o);
	json_object_put(rj.steps);
	json_object_put(rj.unbound);
	if (rj.automatic && watches && json_object_object_get(watches, rj.device_id))
		mm_watch_finished(json_object_object_get(watches, rj.device_id), now_mono_ms());
	memset(&rj, 0, sizeof(rj));
	watch_sync();
}

/* one sysfs attribute write; false with the reason in err */
static bool sysfs_put(const char *path, const char *value, char *err, size_t size)
{
	int fd = open(path, O_WRONLY | O_CLOEXEC), e = errno;
	ssize_t n;

	if (fd < 0) {
		snprintf(err, size, "%s: %s", path, strerror(e));
		errno = e;
		return false;
	}
	n = write(fd, value, strlen(value));
	e = errno;
	if (n < 0)
		snprintf(err, size, "%s: %s", path, strerror(e));
	close(fd);
	errno = e;	/* callers tell EEXIST from a real failure */
	return n >= 0;
}

/* L5 first half: unbind every bound interface of the modem, remembering where it was */
static bool rec_bind(char *err, size_t size);

static bool rec_unbind(json_object *rec, char *err, size_t size)
{
	json_object *ifs = json_object_object_get(rec, "interfaces");
	size_t i;

	rj.unbound = json_object_new_array();
	for (i = 0; ifs && i < json_object_array_length(ifs); i++) {
		const char *sys = jstr(json_object_array_get_idx(ifs, i), "sysfs");
		char link[PATH_MAX], drv[PATH_MAX], path[PATH_MAX + 16];
		json_object *u;

		if (!sys || strchr(sys, '/'))
			continue;
		snprintf(link, sizeof(link), "%s/%s/driver", SYS_USB, sys);
		if (!realpath(link, drv))
			continue;	/* not bound: nothing to give back */
		snprintf(path, sizeof(path), "%s/unbind", drv);
		if (!sysfs_put(path, sys, err, size)) {
			/* never a half-taken composition: the interfaces let go so far go back to their drivers (audit of r135) */
			char back[160] = "";

			if (json_object_array_length(rj.unbound) && !rec_bind(back, sizeof(back)))
				snprintf(err + strlen(err), size - strlen(err), "; and the interfaces let go before did not all come back (%s)", back);
			return false;
		}
		u = json_object_new_object();
		json_object_object_add(u, "interface", json_object_new_string(sys));
		json_object_object_add(u, "driver", json_object_new_string(drv));
		json_object_array_add(rj.unbound, u);
	}
	if (!json_object_array_length(rj.unbound)) {
		snprintf(err, size, "no interface of this modem has a driver bound");
		return false;
	}
	return true;
}

static bool rec_bind(char *err, size_t size)
{
	size_t i;
	bool ok = true;

	for (i = 0; rj.unbound && i < json_object_array_length(rj.unbound); i++) {
		json_object *u = json_object_array_get_idx(rj.unbound, i);
		char path[PATH_MAX + 16];

		snprintf(path, sizeof(path), "%s/bind", jstr(u, "driver"));
		ok = sysfs_put(path, jstr(u, "interface"), err, size) && ok;	/* bind them all, report the first failure */
	}
	return ok;
}

/* the hub port off ("1") or on ("0"); its USB 3 companion (peer) with it */
static bool port_put(const char *port, const char *value, char *err, size_t size)
{
	char path[2 * PATH_MAX + 16], peer[PATH_MAX];
	ssize_t n;

	snprintf(path, sizeof(path), "%s/peer", port);
	n = readlink(path, peer, sizeof(peer) - 1);
	if (n > 0 && *value == '1') {
		peer[n] = 0;
		snprintf(path, sizeof(path), "%s/%s/disable", port, peer);
		sysfs_put(path, value, NULL, 0);
	}
	snprintf(path, sizeof(path), "%s/disable", port);
	if (!sysfs_put(path, value, err, size))
		return false;
	if (n > 0 && *value == '0') {
		peer[n] = 0;
		snprintf(path, sizeof(path), "%s/%s/disable", port, peer);
		sysfs_put(path, value, NULL, 0);
	}
	return true;
}

/* run_dir/port-off names the port a power step switched off until it is on again: an engine that dies in between
 * (field 2026-10-30, a segfault 6 s into a HiLink's power cycle) left the modem dark until someone replugged it;
 * the next engine switches it back on at start */
static void port_off_path(char *out, size_t size)
{
	snprintf(out, size, "%s/port-off", run_dir);
}

static bool rec_port(const char *value, char *err, size_t size)
{
	char mark[PATH_MAX];
	FILE *f;

	port_off_path(mark, sizeof(mark));
	if (*value == '1') {
		mkdir(run_dir, 0755);
		if ((f = fopen(mark, "w"))) {
			fprintf(f, "%s\n", rj.port);
			fclose(f);
		}
		if (!port_put(rj.port, value, err, size)) {
			unlink(mark);
			return false;
		}
		return true;
	}
	if (!port_put(rj.port, value, err, size))
		return false;
	unlink(mark);
	return true;
}

static void port_off_restore(void)
{
	char mark[PATH_MAX], port[PATH_MAX], err[128] = "";
	FILE *f;

	port_off_path(mark, sizeof(mark));
	if (!(f = fopen(mark, "r")))
		return;
	if (!fgets(port, sizeof(port), f))
		*port = '\0';
	fclose(f);
	port[strcspn(port, "\n")] = '\0';
	/* only a port under the USB tree, as rec_start found it */
	if (strncmp(port, SYS_USB "/", strlen(SYS_USB "/")) || strstr(port, "..")) {
		unlink(mark);
		return;
	}
	if (port_put(port, "0", err, sizeof(err)))
		ULOG_WARN("USB port %s was left switched off by the previous engine; switched on again\n", port);
	else
		ULOG_ERR("USB port %s was left switched off by the previous engine and cannot be switched on: %s\n", port, err);
	unlink(mark);
}

static bool rec_authorize(const char *value, char *err, size_t size)
{
	char path[PATH_MAX];

	snprintf(path, sizeof(path), "%s/%s/authorized", SYS_USB, rj.usb);
	return sysfs_put(path, value, err, size);
}

json_object *netifd_status(const char *iface)
{
	return netifd_call(iface, "status");
}

/*
 * Mark a proto modemmanager interface available again, as OpenWrt's ModemManager-monitor does
 * (proto_set_available: network.interface notify_proto, action 11). The proto marks it unavailable when a setup finds
 * no modem (ModemManager not running yet at boot: «Device not managed by ModemManager»), and only the monitor marks it
 * available, by the modem's physdev. A modem ModemManager 1.24 gave no physdev (a ZTE MF100 on a Nokia, 2026-11-05:
 * «No 'sysfspath' for object …») stays unavailable for good, and netifd ignores every up: «available»: false, NO_DEVICE.
 */
static bool netifd_set_available(const char *iface)
{
	struct ubus_context *ctx = &conn.ctx;
	struct blob_buf args = { 0 };
	uint32_t id;
	int rc;

	if (!ubus_up || ubus_lookup_id(ctx, "network.interface", &id))
		return false;
	blob_buf_init(&args, 0);
	blobmsg_add_string(&args, "interface", iface);
	blobmsg_add_u32(&args, "action", 11);
	blobmsg_add_u8(&args, "available", 1);
	rc = ubus_invoke(ctx, id, "notify_proto", args.head, NULL, NULL, 2000);
	blob_buf_free(&args);
	return rc == 0;
}

/* a netifd interface method whose answer does not matter, only that netifd took it */
bool netifd_do(const char *iface, const char *method)
{
	json_object *r = netifd_call(iface, method);

	json_object_put(r);
	return r != NULL;
}

static void rec_verify_start(void)
{
	int settle = budget_override_ms ? 300 : MM_REC_SETTLE_MS;

	rj.phase = REC_VERIFY;
	rj.verify_from = now_mono_ms() + settle;
	rec_log("verifying", NULL);
	schedule_pass(false);
	if (strcmp(mm_backend, "disabled"))
		mm_schedule(settle);
	uloop_timeout_set(&rj.timer, settle);
}

static void rec_proc_spawn(void);

static void rec_proc_exited(struct uloop_process *p, int ret)
{
	char detail[96];

	(void)p;
	uloop_timeout_cancel(&rj.kill);
	rj.proc_running = false;
	snprintf(detail, sizeof(detail), "mmcli -m %s %s: exit %d", rj.mm_index, rj.cmds[rj.cmd],
		 WIFEXITED(ret) ? WEXITSTATUS(ret) : -1);
	rec_log("owner-command", detail);
	if (!WIFEXITED(ret) || WEXITSTATUS(ret)) {
		rec_finish("failed", detail);
		return;
	}
	if (++rj.cmd < rj.ncmds)
		rec_proc_spawn();
	else
		rec_verify_start();
}

static void rec_proc_timeout(struct uloop_timeout *t)
{
	(void)t;
	if (rj.proc_running && rj.proc.pid > 0)
		kill(-rj.proc.pid, SIGKILL);
}

/* ModemManager carries out L3 and L4 for the modem it owns: `mmcli -m N <verb>` */
static void rec_proc_spawn(void)
{
	const char *argv[] = { mmcli_path, "-m", rj.mm_index, rj.cmds[rj.cmd], NULL };
	pid_t pid = fork();
	int fd;

	if (pid < 0) {
		rec_finish("failed", "cannot start mmcli");
		return;
	}
	if (pid == 0) {
		setpgid(0, 0);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	rj.proc_running = true;
	rj.proc.pid = pid;
	rj.proc.cb = rec_proc_exited;
	uloop_process_add(&rj.proc);
	rj.kill.cb = rec_proc_timeout;
	uloop_timeout_set(&rj.kill, MM_REC_CMD_MS);
}

/* did the modem come back usable? NULL when yes, else what is still missing */
static const char *rec_check(void)
{
	json_object *rec = mm_registry_find(&registry, rj.device_id), *mm = NULL, *a;
	const char *st;

	if (rj.level == MM_REC_SESSION) {
		a = assess(rj.iface);
		st = jstr(a, "state");
		st = st && !strcmp(st, "connected") ? NULL : "the session is not connected again";
		json_object_put(a);
		return st;
	}
	if (registry.passes == rj.passes0)
		return "no discovery pass since the action";
	if (!rec || !json_object_get_boolean(json_object_object_get(rec, "present")))
		return "the modem is not attached";
	if (rj.level == MM_REC_OBSERVE || rj.level == MM_REC_CONTROL) {
		if (strcmp(mm_backend, "available"))
			return NULL;
		return mm_last_done > rj.started ? NULL : "ModemManager has not been read again";
	}
	/* R25: back means it left the bus and returned, not merely still there; a usb-reset (deauthorize, authorize)
	 * may keep the device on the bus and only drop its drivers, which then counts */
	if ((rj.level == MM_REC_USB_RESET || rj.level == MM_REC_POWER) && strcmp(usb_left, rj.device_id) &&
	    (rj.level != MM_REC_USB_RESET || strcmp(usb_unbound, rj.device_id)) &&
	    json_object_get_int64(json_object_object_get(rec, "generation")) <= rj.gen0)
		return "the modem has not left the USB bus and come back";
	if (ports_of(rec) < rj.ports0)
		return "not all ports of the modem are back";
	if (!rj.mm_owned)
		return NULL;
	if (mm_last_done <= rj.verify_from)
		return "ModemManager has not been read since the modem settled";
	if (!mm_devices || !json_object_object_get_ex(mm_devices, rj.device_id, &mm))
		return "ModemManager has not taken the modem back";
	if (!mm_state_usable(jstr(mm, "state")))
		return "ModemManager has the modem but it is not usable yet";
	return NULL;
}

static void rec_tick(struct uloop_timeout *t)
{
	char err[200] = "";
	const char *missing;
	long long now = now_mono_ms();

	(void)t;
	if (!rj.active)
		return;
	if (rj.phase == REC_SECOND) {
		/* a session's interface up only once ModemManager has finished taking the old one down: up while it still
		 * disconnects, the proto's register answers «WrongState: Operation not allowed while modem is connected», the
		 * connect fails and ModemManager disables the modem (a tester's ZTE MF100 on OpenWrt 24.10, 2026-11-14: up 5 s
		 * after down, the modem left disabled, the interface down for good). 30 s at most, then up anyway */
		if (rj.level == MM_REC_SESSION && rj.mm_owned && now - rj.started < 30000) {
			json_object *mmd = mm_devices ? json_object_object_get(mm_devices, rj.device_id) : NULL;
			const char *ms = jstr(mmd, "state");

			/* still «connected»: ModemManager starts the disconnect a few seconds after netifd's down (6 s on the
			 * MF100), or never when its bearer is what is stuck (mm_bearer_stuck): 10 s for that, then up */
			if (ms && (!strcmp(ms, "disconnecting") || !strcmp(ms, "connecting") ||
				   (!strcmp(ms, "connected") && now - rj.started < (budget_override_ms ? 0 : 10000)))) {
				uloop_timeout_set(&rj.timer, 1000);
				return;
			}
		}
		/* the second half of a two-step action, a moment after the first */
		bool ok = rj.level == MM_REC_REBIND ? rec_bind(err, sizeof(err)) :
			  rj.level == MM_REC_USB_RESET ? rec_authorize("1", err, sizeof(err)) :
			  rj.level == MM_REC_POWER ? rec_port("0", err, sizeof(err)) :
			  netifd_do(rj.iface, "up");
		rec_log(rj.level == MM_REC_REBIND ? "bound" : rj.level == MM_REC_USB_RESET ? "authorized" :
			rj.level == MM_REC_POWER ? "port-enabled" : "interface-up", ok ? NULL : err);
		if (!ok) {
			rec_finish("failed", err[0] ? err : "netifd did not take the interface up");
			return;
		}
		rec_verify_start();
		return;
	}
	if (rj.phase != REC_VERIFY)
		return;
	missing = rec_check();
	snprintf(rj.waiting, sizeof(rj.waiting), "%s", missing ? missing : "");
	if (!missing) {
		rec_finish("succeeded", NULL);
		return;
	}
	if (now >= rj.deadline) {
		rec_finish("failed", missing);
		return;
	}
	/* uevents normally drive the passes; look anyway in case they were lost */
	if (now - rj.last_nudge >= 3000) {
		rj.last_nudge = now;
		if (rj.level != MM_REC_SESSION)
			schedule_pass(true);
		if (rj.mm_owned || rj.level == MM_REC_OBSERVE || rj.level == MM_REC_CONTROL)
			mm_schedule(0);
	}
	uloop_timeout_set(&rj.timer, MM_REC_TICK_MS);
}

/* 22.1.6: what the modem looked like before a disruptive step */
static json_object *rec_snapshot(json_object *srv)
{
	static const char *const keep[] = { "device_id", "attachment_id", "generation", "vidpid", "profile", "owner",
					    "ports", "modemmanager", "session", "stability", NULL };
	json_object *o = json_object_new_object();
	int i;

	for (i = 0; keep[i]; i++)
		json_object_object_add(o, keep[i], json_object_get(json_object_object_get(srv, keep[i])));
	json_object_object_add(o, "events", mm_history_events(&history, jstr(srv, "device_id"), 20));
	json_object_object_add(o, "t", json_object_new_int64((long long)time(NULL)));
	return o;
}

enum { RC_ID, RC_LEVEL, RC_REASON, RC_WAIT, RC_SOURCE, __RC_MAX };
/* rec_source: who asked for the attempt being started: "luci", "cli", "radio" (logged; declared above) */
static const struct blobmsg_policy recover_policy[] = {
	[RC_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[RC_LEVEL] = { .name = "level", .type = BLOBMSG_TYPE_STRING },
	[RC_REASON] = { .name = "reason", .type = BLOBMSG_TYPE_STRING },
	[RC_WAIT] = { .name = "wait", .type = BLOBMSG_TYPE_BOOL },
	[RC_SOURCE] = { .name = "source", .type = BLOBMSG_TYPE_STRING },
};

static int rec_refuse(struct ubus_context *ctx, struct ubus_request_data *req, const char *id, const char *level,
		      const char *result, const char *why, json_object *srv)
{
	json_object *o = json_object_new_object();
	int rc;

	json_object_object_add(o, "device_id", json_object_new_string(id));
	if (level)
		json_object_object_add(o, "level", json_object_new_string(level));
	json_object_object_add(o, "result", json_object_new_string(result));
	json_object_object_add(o, "reason", json_object_new_string(why));
	if (srv)
		json_object_object_add(o, "levels", mm_recovery_levels(json_object_object_get(srv, "profile"),
			rec_owner(mm_registry_find(&registry, id), srv), false));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/*
 * Start a recovery of a present modem (operator or watch). REC_STARTED: it
 * runs; REC_DONE: it finished at once (failed, recorded); REC_NOT: it did not
 * start, *result is busy, retry or refused and buf says why.
 */
static enum rec_outcome rec_start(json_object *rec, int level, const char *reason, bool automatic,
				  const char **result, char *buf, size_t size)
{
	json_object *srv, *mm, *sess, *st;
	const char *id = jstr(rec, "device_id"), *owner, *why, *web_why = NULL;
	char line[96];
	int i;

	/* 22.1.7: nothing else may be working on this modem */
	why = rj.active ? "another recovery is running" :
	      (job.active || reload_running || pending_release.active) ? "a bring-up or configuration change is running" :
	      esim_reserved(id) ? "an eSIM operation holds this modem" : frozen(id) ? "the modem is frozen by the operator" : NULL;
	for (i = 0; !why && i < MM_AT_JOBS; i++)
		if (at_jobs[i].active && !strcmp(at_jobs[i].device_id, id))
			why = "an AT job is running on this modem";
	if (why) {
		*result = "busy";
		snprintf(buf, size, "%s", why);
		return REC_NOT;
	}

	/* never decide on a stale ModemManager view (bench 2026-10-02): read it, then ask again */
	if (!strcmp(mm_backend, "available") && (mm_last_done < 0 || now_mono_ms() - mm_last_done > MM_FRESH_MS)) {
		mm_schedule(0);
		*result = "retry";
		snprintf(buf, size, "reading ModemManager again; ask once more in a few seconds");
		return REC_NOT;
	}
	srv = served(rec);
	owner = jstr(json_object_object_get(srv, "owner"), "control");
	{
		json_object *profile = json_object_object_get(srv, "profile"), *own = NULL;
		const char *gate_owner = rec_owner(rec, srv), *refused;

		/* the kernel says the USB network driver's transmit queue hangs: re-authorizing the USB device is the
		 * driver's cure whatever owns the modem and whatever its profile declares (bench 2026-10-20: an FM350 of
		 * QModem's, NETDEV WATCHDOG every 2-3 minutes; QModem finds the modem again after it re-enumerates).
		 * The loop protection (caps, cooldown, a flapping USB link) still applies. */
		if (level == MM_REC_USB_RESET && !strcmp(reason, "no_internet") && json_object_object_get(srv, "tx_stall")) {
			json_object *r = json_object_new_object();

			own = json_object_new_object();
			json_object_object_add(own, "id", json_object_new_string("usb-network-driver-hang"));
			json_object_object_add(own, "status", json_object_new_string("matched"));
			json_object_object_add(r, "usb_reauthorize", json_object_new_boolean(1));
			json_object_object_add(r, "status", json_object_new_string("bench-validated"));
			json_object_object_add(own, "recovery", r);
			profile = own;
			gate_owner = NULL;
		}
		refused = mm_recovery_gate(rec_state(id), profile, gate_owner, json_object_object_get(srv, "stability"), level,
					   automatic, now_mono_ms(), &rec_cfg, buf, size);
		json_object_put(own);
		if (refused) {
			*result = "refused";
			json_object_put(srv);
			return REC_NOT;
		}
	}

	memset(&rj, 0, sizeof(rj));
	snprintf(rj.device_id, sizeof(rj.device_id), "%s", id);
	snprintf(rj.reason, sizeof(rj.reason), "%s", reason);
	rj.level = level;
	load.recoveries++;
	mm = json_object_object_get(srv, "modemmanager");
	rj.mm_owned = owner && !strcmp(owner, "modemmanager") && mm;
	/* a modem's own web: its reboot (L4), a HiLink's reconnect of the mobile session (L3; an MF150-4's web has none) */
	rj.web = (level == MM_REC_SOFT_RESET || (level == MM_REC_RADIO && (web_huawei(rec, srv) || web_zte(rec, srv)))) && !rj.mm_owned &&
		 !strncmp(rec_owner(rec, srv) ? rec_owner(rec, srv) : "", "web", 3);
	if (mm)
		snprintf(rj.mm_index, sizeof(rj.mm_index), "%lld", (long long)json_object_get_int64(json_object_object_get(mm, "index")));
	if (jstr(rec, "attachment_id") && !strncmp(jstr(rec, "attachment_id"), "usb:", 4))
		snprintf(rj.usb, sizeof(rj.usb), "%s", jstr(rec, "attachment_id") + 4);
	sess = json_object_object_get(srv, "session");

	/* per-level preconditions the policy cannot see */
	why = NULL;
	if (level == MM_REC_SESSION) {
		json_object *mine = sessions ? json_object_object_get(sessions, id) : NULL, *probe;

		if (!mine || !jstr(mine, "interface"))
			why = jstr(sess, "source") && !strcmp(jstr(sess, "source"), "modemmanager")
				? "the session was not set up by MedveModem; restart it where it was configured"
				: "MedveModem manages no session for this modem";
		else if (!(probe = netifd_call(jstr(mine, "interface"), "status")))
			why = "netifd does not know the session's interface";
		else {
			json_object_put(probe);
			snprintf(rj.iface, sizeof(rj.iface), "%s", jstr(mine, "interface"));
		}
	} else if ((level == MM_REC_RADIO || level == MM_REC_SOFT_RESET) && !rj.mm_owned && !rj.web) {
		why = "ModemManager does not own this modem";
	} else if ((level == MM_REC_REBIND || level == MM_REC_USB_RESET || level == MM_REC_POWER) && !rj.usb[0]) {
		why = "not a USB modem";
	} else if (level == MM_REC_POWER && !usb_port_dir(rj.usb, rj.port, sizeof(rj.port))) {
		why = "the USB port of this modem cannot be switched off";
	} else if (level == MM_REC_USB_RESET || level == MM_REC_POWER) {
		/* 22.1.9: a device with other modems below it is a hub, not this modem */
		json_object *devs = registry.devices;
		char prefix[80];

		snprintf(prefix, sizeof(prefix), "usb:%s.", rj.usb);
		json_object_object_foreach(devs, oid, other) {
			if (strcmp(oid, id) && jstr(other, "attachment_id") && !strncmp(jstr(other, "attachment_id"), prefix, strlen(prefix)) &&
			    json_object_get_boolean(json_object_object_get(other, "present")))
				why = "another modem hangs below this USB device; resetting it would take that one down too";
		}
	}
	if (why) {
		*result = "refused";
		snprintf(buf, size, "%s", why);
		json_object_put(srv);
		memset(&rj, 0, sizeof(rj));
		return REC_NOT;
	}

	rj.active = true;
	rj.started = now_mono_ms();
	/* a reset modem boots, re-enumerates and is probed again: give those levels longer */
	rj.deadline = rj.started + (budget_override_ms ? budget_override_ms :
				    level >= MM_REC_SOFT_RESET ? MM_REC_DEADLINE_MS * 3 / 2 : MM_REC_DEADLINE_MS);
	rj.last_nudge = rj.started;
	rj.passes0 = registry.passes;
	rj.ports0 = ports_of(rec);
	rj.gen0 = json_object_get_int64(json_object_object_get(rec, "generation"));
	usb_left[0] = 0;
	usb_unbound[0] = 0;
	rj.steps = json_object_new_array();
	rj.timer.cb = rec_tick;
	/* who asked, in the log and the history: the web interface, the CLI, or the watch by itself */
	snprintf(line, sizeof(line), "%s %s %s", mm_recovery_level_name(level), reason,
		 automatic ? "automatic" : *rec_source ? rec_source : "operator");
	mm_history_add(&history, id, (long long)time(NULL), rj.started, "recovery-start", line);
	history_dirty = true;
	rec_log("started", line);
	rec_event("recovery_started", NULL);
	ULOG_WARN("recovery %s of %s requested (%s, %s)\n", mm_recovery_level_name(level), id, reason,
		    automatic ? "automatic" : *rec_source ? rec_source : "operator");

	if (level >= MM_REC_DISRUPTIVE) {
		json_object *gone;

		st = rec_state(id);
		json_object_object_add(st, "last_snapshot", rec_snapshot(srv));
		rec_log("snapshot", NULL);
		/* 22.1.7: leases of the old mapping end before the modem goes away */
		gone = mm_lease_invalidate(&leases, id, -1);
		leases_revoked(gone, "recovery");
		json_object_put(gone);
		if (metrics_cache)
			json_object_object_del(metrics_cache, id);
		if (radio_cache)
			json_object_object_del(radio_cache, id);
		rec_log("owner-quiesced", NULL);
	}
	/* a HiLink: its web is asked to reboot, then the modem is waited for as after any reset */
	if (rj.web && !web_act(rec, srv, level == MM_REC_RADIO ? "reconnect" : "reboot", NULL, NULL, NULL, &web_why))
		rj.web = false;
	json_object_put(srv);

	switch (level) {
	case MM_REC_OBSERVE:
		schedule_pass(true);
		rec_verify_start();
		break;
	case MM_REC_SESSION:
		/* a native dial keeps the session across down/up (a metric change): this one must be new */
		if (!dial_cold)
			dial_cold = json_object_new_object();
		json_object_object_add(dial_cold, id, json_object_new_boolean(1));
		if (!netifd_do(rj.iface, "down")) {
			rec_finish("failed", "netifd did not take the interface down");
			break;
		}
		rec_log("interface-down", rj.iface);
		rj.phase = REC_SECOND;
		uloop_timeout_set(&rj.timer, 1000);
		break;
	case MM_REC_CONTROL: {
		json_object *gone = mm_lease_invalidate(&leases, id, -1);

		leases_revoked(gone, "recovery");
		rec_log("leases-released", NULL);
		json_object_put(gone);
		if (probes)
			json_object_object_del(probes, id);
		if (metrics_cache)
			json_object_object_del(metrics_cache, id);
		if (radio_cache)
			json_object_object_del(radio_cache, id);
		inhibits_sync();
		rec_log("control-remapped", NULL);
		/* QModem drives it (bench 2026-10-20, FM350): its section names another modem's port — written now, not
		 * in a minute — or QModem's dial is started again (qmodem_network had no instance and nobody dialled) */
		{
			/* a copy: qmodem_port_sync() below may re-read the QModem image this name points into */
			char sec_buf[64];
			const char *sec = qmodem_section_of(rec);

			if (sec) {
				snprintf(sec_buf, sizeof(sec_buf), "%s", sec);
				sec = sec_buf;
			}
			if (sec && uci_plain_name(sec) && qmodem_installed() && strcmp(reload_hook, "none")) {
				if (qmodem_foreign_port(rec, sec)) {
					qport_at = 0;
					json_object_put(qport_done);	/* a write that did not stick is tried again */
					qport_done = NULL;
					qmodem_port_sync();
					rec_log("qmodem-port", sec);
				} else {
					const char *argv[] = { reload_hook, "qmodem-dial", sec, NULL };

					rec_log("qmodem-dial", hook_run(argv, qport_exited) ? sec : "the reload hook is busy");
				}
			}
		}
		if (mm_should_hold(id, rec)) {
			/* MM gave up on the modem: tell it about the ports again (the way the hotplug scripts do) */
			mm_report_ports(id);
			rec_log("ports-reported", NULL);
			mm_followups = 2;
		}
		rec_verify_start();
		break;
	}
	case MM_REC_RADIO:
	case MM_REC_SOFT_RESET:
		if (!rj.mm_owned) {
			if (!rj.web) {
				rec_finish("failed", web_why ? web_why : level == MM_REC_RADIO ? "the modem's web did not take the reconnect" :
										 "the modem's web did not take the reboot");
				break;
			}
			rec_log(level == MM_REC_RADIO ? "web-reconnect-asked" : "web-reboot-asked", NULL);
			rec_verify_start();
			break;
		}
		rj.ncmds = level == MM_REC_RADIO ? 2 : 1;
		rj.cmds[0] = level == MM_REC_RADIO ? "--disable" : "--reset";
		rj.cmds[1] = "--enable";
		rj.phase = REC_CMD;
		rec_proc_spawn();
		break;
	case MM_REC_REBIND:
		/* never while ModemManager is still probing it: a rebind then can end in -71 and a device left unconfigured
		 * until a power cycle (field notes, 2026-11-02) */
		/* the holders are looked at now, not only the kick's last look: after its five minutes that look stops
		 * marking a probe, and a holder may have come since (audit of r135, 2026-11-02) */
		if ((mm_absent && json_object_get_boolean(json_object_object_get(json_object_object_get(mm_absent, rj.device_id), "probing"))) ||
		    mm_probing(rec)) {
			rec_finish("failed", "ModemManager is still probing the modem: no USB rebind during its probe");
			break;
		}
		if (!rec_unbind(rec, buf, size)) {
			rec_finish("failed", buf);
			break;
		}
		rec_log("unbound", NULL);
		rj.phase = REC_SECOND;
		uloop_timeout_set(&rj.timer, 1000);
		break;
	case MM_REC_POWER:
		if (!rec_port("1", buf, size)) {
			rec_finish("failed", buf);
			break;
		}
		rec_log("port-disabled", rj.usb);
		load.power_cycles++;
		rj.phase = REC_SECOND;
		uloop_timeout_set(&rj.timer, budget_override_ms ? 300 : 6000);
		break;
	case MM_REC_USB_RESET:
		if (!rec_authorize("0", buf, size)) {
			rec_finish("failed", buf);
			break;
		}
		rec_log("deauthorized", rj.usb);
		load.usb_resets++;
		rj.phase = REC_SECOND;
		uloop_timeout_set(&rj.timer, 2000);
		break;
	default:
		rec_finish("failed", "no executor for this level");
		break;
	}
	rj.automatic = automatic;
	return rj.active ? REC_STARTED : REC_DONE;
}

static int m_recover(struct ubus_context *ctx, struct ubus_object *obj,
		     struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__RC_MAX];
	json_object *rec, *srv;
	const char *id, *reason, *result = NULL;
	char buf[200];
	enum rec_outcome out;
	int level, rc;

	(void)obj; (void)method;
	blobmsg_parse(recover_policy, __RC_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[RC_ID] || !tb[RC_LEVEL])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = present_record(tb[RC_ID])))
		return mm_registry_find(&registry, blobmsg_get_string(tb[RC_ID])) ?
			rec_refuse(ctx, req, blobmsg_get_string(tb[RC_ID]), NULL, "refused", "the modem is not attached", NULL)
			: UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	level = mm_recovery_level_parse(blobmsg_get_string(tb[RC_LEVEL]));
	if (level < 0)
		return rec_refuse(ctx, req, id, NULL, "refused", "unknown recovery level", NULL);
	reason = tb[RC_REASON] ? blobmsg_get_string(tb[RC_REASON]) : "user_requested";
	if (!mm_recovery_reason_valid(reason))
		return rec_refuse(ctx, req, id, mm_recovery_level_name(level), "refused", "unknown recovery reason", NULL);
	snprintf(rec_source, sizeof(rec_source), "%s", tb[RC_SOURCE] && strspn(blobmsg_get_string(tb[RC_SOURCE]),
		 "abcdefghijklmnopqrstuvwxyz") == strlen(blobmsg_get_string(tb[RC_SOURCE])) ? blobmsg_get_string(tb[RC_SOURCE]) : "");
	out = rec_start(rec, level, reason, false, &result, buf, sizeof(buf));
	rec_source[0] = '\0';
	if (out == REC_NOT) {
		srv = !strcmp(result, "refused") ? served(rec) : NULL;
		rc = rec_refuse(ctx, req, id, mm_recovery_level_name(level), result, buf, srv);
		json_object_put(srv);
		return rc;
	}
	if (out == REC_DONE)
		return rec_refuse(ctx, req, id, mm_recovery_level_name(level), "failed",
				  json_object_get_string(json_object_object_get(
					  json_object_object_get(rec_state(id), "last_failure"), "detail")), NULL);
	if (tb[RC_WAIT] && !blobmsg_get_bool(tb[RC_WAIT]))
		return rec_refuse(ctx, req, id, mm_recovery_level_name(level), "started", "running", NULL);
	rj.ctx = ctx;
	ubus_defer_request(ctx, req, &rj.req);
	rj.deferred = true;
	return UBUS_STATUS_OK;
}

/* ---- host fixes (spec 15.2 stage 4): interface-scoped driver binding ---- */

/*
 * A profile's "bind" block names the interfaces the stock serial driver does
 * not take by itself. Like the reference stacks' init scripts this writes a
 * dynamic new_id, but scoped: interfaces the new_id takes that the profile
 * did not ask for (an ADB port) are given back on every pass, a listed
 * interface another driver holds blocks the fix, and at most two attempts run
 * per attachment generation. The dynamic ID stays, so a re-enumeration binds
 * the same interfaces again. Only bench-validated bindings run; -F 0 (UCI
 * core.host_fixes 0) turns all of it off.
 */
#define MM_HOSTFIX_TRIES 2
#define MM_HOSTFIX_SETTLE_MS 3000
static int host_fixes = 1;
/* hostfixes: device_id -> {generation, tries, result, ...}, declared above */
static struct uloop_timeout hostfix_timer;
static void hostfix_run(struct uloop_timeout *t);

static const char *driver_of(const char *iface, char *buf, size_t size)
{
	char link[PATH_MAX], real[PATH_MAX];
	const char *base;

	snprintf(link, sizeof(link), "%s/%s/driver", SYS_USB, iface);
	if (!realpath(link, real))
		return NULL;
	base = strrchr(real, '/');
	snprintf(buf, size, "%.*s", (int)(size ? size - 1 : 0), base ? base + 1 : real);
	return buf;
}

static void hostfix_note(const char *id, json_object *st, const char *result, const char *detail)
{
	json_object_object_add(st, "result", json_object_new_string(result));
	if (detail)
		json_object_object_add(st, "detail", json_object_new_string(detail));
	else
		json_object_object_del(st, "detail");
	json_object_object_add(st, "t", json_object_new_int64((long long)time(NULL)));
	ULOG_INFO("host fix %s: %s%s%s\n", id, result, detail ? ": " : "", detail ? detail : "");
}

/*
 * bind.net first (the owner, 2026-10-26: «максимально универсально»): the data driver's
 * new_id for an ID no kernel table names, what it took beyond the profile given back at once (the reference stacks'
 * qmi_wwan new_id then unbind of 1bc7:1910's 2-4), a data interface a serial driver holds rescued to it
 * (a field report). True while it acted or waits for the kernel: the serial step waits a pass.
 */
static bool hostfix_net(const char *id, json_object *rec, json_object *bind, json_object *st, const char *vidpid)
{
	json_object *net = json_object_object_get(bind, "net"), *plan, *extra, *list;
	const char *drv = jstr(net, "driver"), *act;
	char err[256], path[PATH_MAX], idstr[24], now_drv[64];
	bool acted = false;
	size_t i;

	if (!net || !drv || !*drv || strchr(drv, '/'))
		return false;
	snprintf(idstr, sizeof(idstr), "%.4s %.4s ff", vidpid, vidpid + 5);
	extra = mm_hostfix_net_extra(rec, bind);
	for (i = 0; i < json_object_array_length(extra); i++) {
		const char *sys = json_object_get_string(json_object_array_get_idx(extra, i));

		snprintf(path, sizeof(path), "%s/%s/unbind", SYS_USB_DRIVERS, drv);
		if (sysfs_put(path, sys, err, sizeof(err)))
			ULOG_INFO("host fix %s: gave %s back from %s (not a data interface of the profile)\n", id, sys, drv);
		acted = true;
	}
	json_object_put(extra);
	plan = mm_hostfix_net_plan(rec, bind);
	act = jstr(plan, "action");
	if (act && strcmp(act, "none")) {
		if (json_object_object_get(st, "net_pass") &&
		    (json_object_get_int64(json_object_object_get(st, "net_pass")) >= (long long)registry.passes ||
		     now_mono_ms() < json_object_get_int64(json_object_object_get(st, "net_settle")))) {
			hostfix_timer.cb = hostfix_run;
			uloop_timeout_set(&hostfix_timer, MM_HOSTFIX_SETTLE_MS);
			json_object_put(plan);
			return true;
		}
		if (json_object_get_int(json_object_object_get(st, "net_tries")) >= MM_HOSTFIX_TRIES) {
			if (strcmp(jstr(st, "result") ? jstr(st, "result") : "", "failed"))
				hostfix_note(id, st, "failed", "the data interface is still not on its driver after the new_id");
			json_object_put(plan);
			return false;
		}
		json_object_object_add(st, "net_tries", json_object_new_int(json_object_get_int(json_object_object_get(st, "net_tries")) + 1));
		json_object_object_add(st, "net_pass", json_object_new_int64((long long)registry.passes));
		json_object_object_add(st, "net_settle", json_object_new_int64(now_mono_ms() + MM_HOSTFIX_SETTLE_MS));
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "hostfix-start", idstr);
		/* the dynamic ID first: a rescue's bind needs the driver to match the device (EEXIST: there already) */
		snprintf(path, sizeof(path), "%s/%s/new_id", SYS_USB_DRIVERS, drv);
		if (!sysfs_put(path, idstr, err, sizeof(err)) && errno != EEXIST) {
			hostfix_note(id, st, "failed", err);
		} else {
			list = json_object_object_get(plan, "rescue");
			for (i = 0; i < json_object_array_length(list); i++) {
				json_object *r = json_object_array_get_idx(list, i);
				const char *sys = jstr(r, "sysfs"), *from = jstr(r, "driver");

				if (!sys || !from || strchr(from, '/'))
					continue;
				snprintf(path, sizeof(path), "%s/%s/unbind", SYS_USB_DRIVERS, from);
				sysfs_put(path, sys, err, sizeof(err));
				snprintf(path, sizeof(path), "%s/%s/bind", SYS_USB_DRIVERS, drv);
				if (sysfs_put(path, sys, err, sizeof(err)))
					ULOG_INFO("host fix %s: rescued data interface %s from %s to %s\n", id, sys, from, drv);
			}
			/* the kernel binds while the write runs: what it took beyond the data interfaces goes back */
			list = json_object_object_get(plan, "collateral");
			for (i = 0; i < json_object_array_length(list); i++) {
				const char *sys = json_object_get_string(json_object_array_get_idx(list, i)), *d = driver_of(sys, now_drv, sizeof(now_drv));

				snprintf(path, sizeof(path), "%s/%s/unbind", SYS_USB_DRIVERS, drv);
				if (d && !strcmp(d, drv))
					sysfs_put(path, sys, err, sizeof(err));
			}
			hostfix_note(id, st, "applied", idstr);
		}
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "hostfix-end", jstr(st, "result"));
		history_dirty = true;
		schedule_pass(false);
		acted = true;
	}
	json_object_put(plan);
	return acted;
}

static void hostfix_device(const char *id, json_object *rec)
{
	json_object *srv = served(rec), *bind = json_object_object_get(json_object_object_get(srv, "profile"), "bind");
	json_object *serial = json_object_object_get(bind, "serial"), *plan, *extra, *st;
	const char *driver = jstr(serial, "driver"), *usd = jstr(serial, "usb_serial_driver"), *vidpid = jstr(rec, "vidpid");
	long long gen = json_object_get_int64(json_object_object_get(rec, "generation"));
	char err[256] = "", path[PATH_MAX], idstr[24], now_drv[64];
	const char *mp = jstr(json_object_object_get(srv, "profile"), "mm_ports");
	/* ModemManager ignores every serial port of such a modem (its own rule, r22): what the new_id took beyond
	 * the profile (ADB) stays bound. Unbinding a port usb-serial had created a few ms before, while
	 * ModemManager probed the next ones, is the suspect of the bench's kernel crashes (2026-10-09, F13) */
	bool keep = mp && !strcmp(mp, "control");
	size_t i;

	if (!bind || !driver || !usd || !vidpid || strlen(vidpid) != 9 || !mm_hostfix_automatic(bind)) {
		json_object_put(srv);
		return;
	}
	if (!hostfixes)
		hostfixes = json_object_new_object();
	if (!(st = json_object_object_get(hostfixes, id)) ||
	    json_object_get_int64(json_object_object_get(st, "generation")) != gen) {
		st = json_object_new_object();
		json_object_object_add(st, "generation", json_object_new_int64(gen));
		json_object_object_add(st, "tries", json_object_new_int(0));
		json_object_object_add(hostfixes, id, st);
	}

	if (hostfix_net(id, rec, bind, st, vidpid)) {
		json_object_put(srv);
		return;
	}
	/* what a new_id took beyond the profile goes back, on every pass (not for a control-only modem) */
	extra = mm_hostfix_extra(rec, bind);
	if (keep && json_object_array_length(extra) && !json_object_object_get(st, "kept")) {
		json_object_object_add(st, "kept", json_object_get(extra));
		ULOG_INFO("host fix %s: %zu interface(s) beyond the profile stay on %s (ModemManager ignores its serial ports)\n",
			  id, json_object_array_length(extra), driver);
	}
	for (i = 0; !keep && i < json_object_array_length(extra); i++) {
		const char *sys = json_object_get_string(json_object_array_get_idx(extra, i));
		json_object *done = json_object_object_get(st, "released");
		bool again = false;

		for (size_t k = 0; done && k < json_object_array_length(done); k++)
			again = again || !strcmp(json_object_get_string(json_object_array_get_idx(done, k)), sys);
		if (again)
			continue;	/* once per generation: the uevent of the unbind brings the next pass */
		if (!done) {
			done = json_object_new_array();
			json_object_object_add(st, "released", done);
		}
		json_object_array_add(done, json_object_new_string(sys));
		snprintf(path, sizeof(path), "%s/%s/unbind", SYS_USB_DRIVERS, driver);
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "hostfix-start", sys);
		if (!sysfs_put(path, sys, err, sizeof(err)))
			ULOG_WARN("host fix %s: could not give %s back: %s\n", id, sys, err);
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "hostfix-end", "released");
		history_dirty = true;
		ULOG_INFO("host fix %s: gave %s back from %s\n", id, sys, driver);
	}
	json_object_put(extra);

	plan = mm_hostfix_plan(rec, bind);
	if (!strcmp(jstr(plan, "action"), "none")) {
		const char *now_result = json_object_get_int(json_object_object_get(st, "tries")) ? "bound" : "not-needed";

		/* said once per change: every pass repeated "not-needed" in the log (bench 2026-10-07) */
		if (!jstr(st, "result") || strcmp(jstr(st, "result"), now_result))
			hostfix_note(id, st, now_result, NULL);
	} else if (!strcmp(jstr(plan, "action"), "blocked")) {
		if (!jstr(st, "result") || strcmp(jstr(st, "result"), "blocked"))
			hostfix_note(id, st, "blocked", jstr(plan, "reason"));
	} else if (json_object_object_get(st, "pass") &&
		   (json_object_get_int64(json_object_object_get(st, "pass")) >= (long long)registry.passes ||
		    now_mono_ms() < json_object_get_int64(json_object_object_get(st, "settle_until")))) {
		/* applied, and the kernel's binds (uevents ~0.1 s after the new_id) not yet seen by a discovery pass:
		 * the record still shows the interfaces unbound. A second new_id here took the ADB interface again
		 * (ttyUSB8 came and went twice, bench 2026-10-09). Look again once the binds have settled. */
		hostfix_timer.cb = hostfix_run;
		uloop_timeout_set(&hostfix_timer, MM_HOSTFIX_SETTLE_MS);
	} else if (json_object_get_int(json_object_object_get(st, "tries")) >= MM_HOSTFIX_TRIES) {
		if (strcmp(jstr(st, "result") ? jstr(st, "result") : "", "failed"))
			hostfix_note(id, st, "failed", "the interfaces are still unbound after the new_id");
	} else {
		/* "vid pid class": only vendor-class interfaces, never the storage or a CDC one */
		snprintf(idstr, sizeof(idstr), "%.4s %.4s %s", vidpid, vidpid + 5, jstr(plan, "class"));
		json_object_object_add(st, "tries", json_object_new_int(json_object_get_int(json_object_object_get(st, "tries")) + 1));
		json_object_object_add(st, "pass", json_object_new_int64((long long)registry.passes));
		json_object_object_add(st, "settle_until", json_object_new_int64(now_mono_ms() + MM_HOSTFIX_SETTLE_MS));
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "hostfix-start", idstr);
		snprintf(path, sizeof(path), "%s/%s/new_id", SYS_USBSERIAL_DRIVERS, usd);
		/* EEXIST: the dynamic ID is there already (an earlier run of the engine) */
		if (!sysfs_put(path, idstr, err, sizeof(err)) && errno != EEXIST) {
			char err2[256];

			snprintf(path, sizeof(path), "%s/%s/new_id", SYS_USB_DRIVERS, driver);
			if (!sysfs_put(path, idstr, err2, sizeof(err2)) && errno != EEXIST) {
				hostfix_note(id, st, "failed", err);
				goto out;
			}
		}
		/* the kernel binds while the write runs: give back what it took beyond the plan */
		json_object *coll = json_object_object_get(plan, "collateral");
		for (i = 0; !keep && i < json_object_array_length(coll); i++) {
			const char *sys = json_object_get_string(json_object_array_get_idx(coll, i)), *d = driver_of(sys, now_drv, sizeof(now_drv));

			snprintf(path, sizeof(path), "%s/%s/unbind", SYS_USB_DRIVERS, driver);
			if (d && !strcmp(d, driver))
				sysfs_put(path, sys, err, sizeof(err));
		}
		json_object_object_add(st, "bound", json_object_get(json_object_object_get(plan, "missing")));
		hostfix_note(id, st, "applied", idstr);
out:
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "hostfix-end", jstr(st, "result"));
		history_dirty = true;
		schedule_pass(false);
	}
	json_object_put(plan);
	json_object_put(srv);
}

static void hostfix_run(struct uloop_timeout *t)
{
	(void)t;
	if (!host_fixes || !registry.devices)
		return;
	json_object_object_foreach(registry.devices, id, rec) {
		/* a reset or rebind waiting for the modem to come back: bind its ports now, before ModemManager
		 * probes the ADB port as a modem port (bench 2026-10-07) */
		bool verifying = rec_busy(id) && rj.phase == REC_VERIFY &&
				 (rj.level == MM_REC_SOFT_RESET || rj.level == MM_REC_REBIND || rj.level == MM_REC_USB_RESET ||
				  rj.level == MM_REC_POWER);

		if (!json_object_get_boolean(json_object_object_get(rec, "present")) || (rec_busy(id) && !verifying) ||
		    esim_reserved(id) || frozen(id) || (job.active && !strcmp(job.device_id, id)))
			continue;
		hostfix_device(id, rec);
	}
}

/*
 * A usb-mode.json rule that sets configuration 0 for a working modem (Dell
 * DW5821e/DW5829e, HP lt4120; H7): usbmode unconfigures the device on every
 * hotplug while ModemManager holds its ports (field 2026-10-12: a dozen runs
 * on 413c:81e0, a kernel Oops 100 s later). Both reference stacks take the rule
 * out by themselves; here the operator asks for it (LuCI, ubus). The rule is
 * renamed, not deleted, so usbmode no longer matches it and it can be put
 * back; the file before the first change stays beside it as a backup.
 */
#define MM_MODESWITCH_DISABLED "_medvemodem_disabled"

static bool copy_file(const char *from, const char *to, char *err, size_t size)
{
	char buf[4096], tmp[PATH_MAX];
	FILE *in = fopen(from, "r"), *out;
	size_t n;
	bool ok = true;

	if (!in) {
		snprintf(err, size, "%s: %s", from, strerror(errno));
		return false;
	}
	snprintf(tmp, sizeof(tmp), "%s.tmp", to);
	if (!(out = fopen(tmp, "w"))) {
		snprintf(err, size, "%s: %s", tmp, strerror(errno));
		fclose(in);
		return false;
	}
	while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0)
		ok = fwrite(buf, 1, n, out) == n;
	ok = ok && !ferror(in) && !fflush(out) && !fsync(fileno(out));
	fclose(in);
	if (fclose(out) || !ok || rename(tmp, to)) {
		snprintf(err, size, "%s: %s", to, strerror(errno));
		unlink(tmp);
		return false;
	}
	return true;
}

/*
 * The FCC unlock of a modem whose profile names one (T77W968 family, observation H6): the operator's button links
 * medvemodem's hook as ModemManager's fcc-unlock.d/<vid:pid> (enable) or removes that link (disable). Never at install,
 * never by itself, never over a hook it did not make. ModemManager reads the directory at every attempt to turn the
 * radio on: no restart; a modem already given up on is retried after a replug. HANDOFF 176.
 */
enum { FCC_ID, FCC_ENABLE, __FCC_MAX };
static const struct blobmsg_policy fcc_policy[] = {
	[FCC_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
	[FCC_ENABLE] = { .name = "enable", .type = BLOBMSG_TYPE_BOOL },
};

static int m_fcc_unlock(struct ubus_context *ctx, struct ubus_object *obj,
			struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__FCC_MAX];
	json_object *rec, *srv, *o;
	const char *id, *how, *result = NULL, *why = NULL;
	char key[16], path[PATH_MAX], err[PATH_MAX + 64] = "";
	bool enable;
	int st, rc;

	(void)obj; (void)method;
	blobmsg_parse(fcc_policy, __FCC_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[FCC_ID] || !tb[FCC_ENABLE])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[FCC_ID]))))
		return UBUS_STATUS_NOT_FOUND;
	enable = blobmsg_get_bool(tb[FCC_ENABLE]);
	id = jstr(rec, "device_id");
	srv = served(rec);
	how = jstr(json_object_object_get(srv, "profile"), "fcc_unlock");
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	if (!how) {
		result = "refused";
		why = "this modem's profile names no FCC unlock";
	} else if (!fcc_path(jstr(rec, "vidpid"), key, path)) {
		result = "refused";
		why = "not a USB device";
	} else if ((st = fcc_hook_state(path)) < 0) {
		result = "refused";
		why = "ModemManager already has another FCC unlock hook for this USB ID; it is left as it is";
	} else if (enable && st == 1) {
		result = "enabled";
	} else if (!enable && st == 0) {
		result = "disabled";
	} else if (enable && access(MM_FCC_UNLOCK_HOOK, X_OK)) {
		result = "failed";
		why = "medvemodem's FCC unlock hook is not installed";
	} else if (enable) {
		/* OpenWrt's ModemManager package ships no /etc/ModemManager (Mag's DW5821e, 2026-11-03: «cannot link
		 * …/fcc-unlock.d/413c:81d7: No such file or directory»): the parent first */
		char parent[PATH_MAX], *slash;

		snprintf(parent, sizeof(parent), "%s", MM_FCC_UNLOCK_DIR);
		if ((slash = strrchr(parent, '/')) && slash != parent) {
			*slash = 0;
			if (mkdir(parent, 0755) && errno != EEXIST)
				ULOG_WARN("cannot create %s: %s\n", parent, strerror(errno));
		}
		if ((mkdir(MM_FCC_UNLOCK_DIR, 0755) && errno != EEXIST) || symlink(MM_FCC_UNLOCK_HOOK, path)) {
			snprintf(err, sizeof(err), "cannot link %s: %s", path, strerror(errno));
			result = "failed";
			why = err;
		} else {
			result = "enabled";
			ULOG_WARN("FCC unlock hook for %s (%s) turned on by the operator\n", key, id);
			mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "fcc-unlock-on", key);
			history_dirty = true;
		}
	} else if (unlink(path)) {
		snprintf(err, sizeof(err), "cannot remove %s: %s", path, strerror(errno));
		result = "failed";
		why = err;
	} else {
		result = "disabled";
		ULOG_WARN("FCC unlock hook for %s (%s) turned off by the operator\n", key, id);
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "fcc-unlock-off", key);
		history_dirty = true;
	}
	json_object_object_add(o, "result", json_object_new_string(result));
	if (why)
		json_object_object_add(o, "reason", json_object_new_string(why));
	if (how)
		json_object_object_add(o, "fcc_unlock", fcc_served(how, jstr(rec, "vidpid")));
	json_object_put(srv);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

static const struct blobmsg_policy msw_policy[] = {
	{ .name = "id", .type = BLOBMSG_TYPE_STRING },
};

static int m_modeswitch_fix(struct ubus_context *ctx, struct ubus_object *obj,
			    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[1];
	json_object *rec, *o = json_object_new_object(), *file = NULL, *devices = NULL, *entry = NULL;
	const char *vidpid, *id;
	char key[16], off[64], backup[PATH_MAX], tmp[PATH_MAX], err[PATH_MAX + 64] = "";
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(msw_policy, 1, tb, blob_data(msg), blob_len(msg));
	if (!tb[0]) {
		json_object_put(o);
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[0])))) {
		json_object_put(o);
		return UBUS_STATUS_NOT_FOUND;
	}
	id = jstr(rec, "device_id");
	vidpid = jstr(rec, "vidpid");
	json_object_object_add(o, "device_id", json_object_new_string(id));
	if (!vidpid || strlen(vidpid) != 9) {
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string("not a USB device"));
		goto reply;
	}
	snprintf(key, sizeof(key), "%s", vidpid);
	for (char *c = key; *c; c++)
		if (*c >= 'A' && *c <= 'F')
			*c = (char)(*c - 'A' + 'a');
	json_object_object_add(o, "rule", json_object_new_string(key));
	file = json_object_from_file(USB_MODE_JSON);
	if (file && json_object_object_get_ex(file, "devices", &devices))
		json_object_object_get_ex(devices, key, &entry);
	if (!mm_modeswitch_unconfigures(entry)) {
		json_object_object_add(o, "result", json_object_new_string("not-needed"));
		json_object_object_add(o, "reason", json_object_new_string(file ? "usb-mode.json sets no configuration 0 for this device"
										   : "no readable usb-mode.json"));
		goto reply;
	}
	snprintf(backup, sizeof(backup), "%s.medvemodem-backup", USB_MODE_JSON);
	if (access(backup, F_OK) && !copy_file(USB_MODE_JSON, backup, err, sizeof(err))) {
		json_object_object_add(o, "result", json_object_new_string("failed"));
		json_object_object_add(o, "reason", json_object_new_string(err));
		goto reply;
	}
	snprintf(off, sizeof(off), "%s%s", key, MM_MODESWITCH_DISABLED);
	json_object_object_add(devices, off, json_object_get(entry));
	json_object_object_del(devices, key);
	snprintf(tmp, sizeof(tmp), "%s.medvemodem-tmp", USB_MODE_JSON);
	if (json_object_to_file_ext(tmp, file, JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_PRETTY_TAB) || rename(tmp, USB_MODE_JSON)) {
		snprintf(err, sizeof(err), "cannot write %s: %s", USB_MODE_JSON, strerror(errno));
		unlink(tmp);
		json_object_object_add(o, "result", json_object_new_string("failed"));
		json_object_object_add(o, "reason", json_object_new_string(err));
		goto reply;
	}
	ULOG_WARN("usb-mode.json: the configuration 0 rule for %s renamed to %s by the operator (backup %s)\n", key, off, backup);
	mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "modeswitch-fix", key);
	history_dirty = true;
	json_object_object_add(o, "result", json_object_new_string("fixed"));
	json_object_object_add(o, "disabled_as", json_object_new_string(off));
	json_object_object_add(o, "backup", json_object_new_string(backup));
	/* a device the rule already unconfigured stays so until it enumerates again */
	{
		const char *att = jstr(rec, "attachment_id");
		char cv[16] = "";

		if (att && !strncmp(att, "usb:", 4)) {
			FILE *f;

			snprintf(tmp, sizeof(tmp), "%s/%s/bConfigurationValue", SYS_USB, att + 4);
			if ((f = fopen(tmp, "r"))) {
				if (!fgets(cv, sizeof(cv), f))
					cv[0] = '\0';
				fclose(f);
			}
		}
		json_object_object_add(o, "replug", json_object_new_boolean(!cv[0] || cv[0] == '\n' || cv[0] == '0'));
	}
	schedule_pass(true);
reply:
	json_object_put(file);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/*
 * The operator's «Переключить в режим модема» (F19, field 2026-10-15: an E171 stayed a CD-ROM).
 * One run on the button, never on its own: the switch is one USB message to the stick, it
 * writes nothing to its NV (AT^U2DIAG / AT^SETPORT would, and stay out of this). With the
 * package: usbmode switches what its rules know; a Huawei ID its rules lack gets a one-entry
 * config of ours with the HuaweiNew message. The classic usb_modeswitch: -J (HuaweiNewMode),
 * Huawei only. The stick re-enumerates as the modem; the record follows by itself.
 */
#define MSW_RUN_TIMEOUT_MS 20000
#define MSW_RUN_GAP_MS 30000
static struct {
	bool running;
	struct uloop_process proc;
	struct uloop_timeout kill;
	long long started;
	char id[200];
} mswr = { .started = -1 };

static void mswr_kill(struct uloop_timeout *t)
{
	(void)t;
	if (mswr.running && mswr.proc.pid > 0)
		kill(-mswr.proc.pid, SIGKILL);
}

static void mswr_exited(struct uloop_process *p, int ret)
{
	char detail[48];

	(void)p;
	uloop_timeout_cancel(&mswr.kill);
	mswr.running = false;
	snprintf(detail, sizeof(detail), WIFEXITED(ret) ? "exit %d" : "killed", WIFEXITED(ret) ? WEXITSTATUS(ret) : 0);
	mm_history_add(&history, mswr.id, (long long)time(NULL), now_mono_ms(), "modeswitch-done", detail);
	history_dirty = true;
	ULOG_INFO("mode switch for %s: %s\n", mswr.id, detail);
	schedule_pass(true);
}

static int m_modeswitch_run(struct ubus_context *ctx, struct ubus_object *obj,
			    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[1];
	json_object *rec, *o = json_object_new_object(), *msw;
	const char *id, *vidpid, *tool, *why = NULL;
	char vid[8] = "", pid[8] = "", conf[PATH_MAX] = "", key[16];
	bool rule, huawei;
	long long now = now_mono_ms();
	pid_t child;
	int rc, fd;

	(void)obj; (void)method;
	blobmsg_parse(msw_policy, 1, tb, blob_data(msg), blob_len(msg));
	if (!tb[0]) {
		json_object_put(o);
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[0])))) {
		json_object_put(o);
		return UBUS_STATUS_NOT_FOUND;
	}
	id = jstr(rec, "device_id");
	vidpid = jstr(rec, "vidpid");
	msw = json_object_object_get(rec, "modeswitch");
	tool = jstr(msw, "tool");
	rule = json_object_get_boolean(json_object_object_get(msw, "rule"));
	json_object_object_add(o, "device_id", json_object_new_string(id));
	if (vidpid && strlen(vidpid) == 9) {
		snprintf(vid, sizeof(vid), "%.4s", vidpid);
		snprintf(pid, sizeof(pid), "%.4s", vidpid + 5);
	}
	huawei = !strcasecmp(vid, "12d1");
	if (!json_object_get_boolean(json_object_object_get(rec, "present")) || !jstr(rec, "detection_status") ||
	    strcmp(jstr(rec, "detection_status"), "modeswitch-pending") || !*pid)
		why = "the device is not waiting for a mode switch";
	else if (!tool)
		why = "usb-modeswitch is not installed";
	else if (!strcmp(tool, "usb_modeswitch") && !huawei)
		why = "the classic usb_modeswitch is run for Huawei sticks only";
	else if (!strcmp(tool, "usbmode") && !rule && !huawei)
		why = "usb-mode.json has no rule for this device";
	else if (mswr.running)
		why = "a mode switch is running";
	else if (mswr.started >= 0 && now - mswr.started < MSW_RUN_GAP_MS)
		why = "a mode switch ran less than 30 s ago";
	if (why) {
		json_object_object_add(o, "result", json_object_new_string("refused"));
		json_object_object_add(o, "reason", json_object_new_string(why));
		goto reply;
	}
	if (!strcmp(tool, "usbmode") && !rule) {
		/* the rules lack this Huawei ID: one entry of ours, the HuaweiNew message usbmode builds itself */
		FILE *f;

		snprintf(key, sizeof(key), "%s:%s", vid, pid);
		for (char *c = key; *c; c++)
			*c = (char)tolower((unsigned char)*c);
		snprintf(conf, sizeof(conf), "%s/usbmode-huawei.json", run_dir);
		mkdir(run_dir, 0755);
		if (!(f = fopen(conf, "w")) || fprintf(f, "{ \"messages\": [], \"devices\": { \"%s\": { \"*\": { \"mode\": \"HuaweiNew\" } } } }\n", key) < 0 ||
		    fclose(f)) {
			json_object_object_add(o, "result", json_object_new_string("failed"));
			json_object_object_add(o, "reason", json_object_new_string("cannot write the one-entry usbmode config"));
			goto reply;
		}
	}
	child = fork();
	if (child < 0) {
		json_object_object_add(o, "result", json_object_new_string("failed"));
		json_object_object_add(o, "reason", json_object_new_string("cannot start the tool"));
		goto reply;
	}
	if (child == 0) {
		setpgid(0, 0);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		if (!strcmp(tool, "usbmode") && *conf)
			execl(USBMODE_BIN, "usbmode", "-c", conf, "-s", (char *)NULL);
		else if (!strcmp(tool, "usbmode"))
			execl(USBMODE_BIN, "usbmode", "-s", (char *)NULL);
		else
			execl(USB_MODESWITCH_BIN, "usb_modeswitch", "-v", vid, "-p", pid, "-J", (char *)NULL);
		_exit(127);
	}
	mswr.running = true;
	mswr.started = now;
	snprintf(mswr.id, sizeof(mswr.id), "%s", id);
	mswr.proc.pid = child;
	mswr.proc.cb = mswr_exited;
	uloop_process_add(&mswr.proc);
	mswr.kill.cb = mswr_kill;
	uloop_timeout_set(&mswr.kill, MSW_RUN_TIMEOUT_MS);
	{
		char detail[80];
		snprintf(detail, sizeof(detail), "%s %s%s", tool, vidpid, *conf ? " (HuaweiNew, no rule)" : "");
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "modeswitch-run", detail);
		history_dirty = true;
		ULOG_WARN("mode switch by the operator: %s for %s\n", detail, id);
	}
	json_object_object_add(o, "result", json_object_new_string("started"));
	json_object_object_add(o, "tool", json_object_new_string(tool));
reply:
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* after a discovery pass or a ModemManager read: profiles may have changed */
/*
 * R3: the USB paths whose serial ports ModemManager must not probe (profile
 * policy mm_ports "control"), one per line in the run directory. The tty
 * hotplug script that runs after ModemManager's own (26-medvemodem-mm-ports)
 * withdraws such a port from ModemManager at once, so it probes only the
 * QMI/MBIM control port: no AT probe of a booting modem's diagnostic ports.
 *
 * That withdrawal races ModemManager's start: its wrapper replays the event
 * cache and MM took ttyUSB9 of the T99 anyway (bench 2026-10-09, HANDOFF 79,
 * 83). So the same modems also get a rule ModemManager reads itself: without
 * udev (OpenWrt) it loads /lib/udev/rules.d once, before its first device, and
 * honours ENV{ID_MM_PORT_IGNORE} (its own Cinterion and Dell rules do). One
 * rule per modem, keyed by its USB serial (ATTRS{serial} is looked up from the
 * tty up to the USB device), so other modems with the same VID:PID are not
 * touched. The file is on flash: it is there before ModemManager at the next
 * boot. Modems not present now keep their rule; a present modem whose profile
 * no longer says "control" loses it. Takes effect when ModemManager starts.
 */
static char withheld[1024];
static const char *mm_rules_path = "/lib/udev/rules.d/78-mm-medvemodem.rules";
static char *mm_rules_text;

static bool rule_safe(const char *s)
{
	if (!s || !*s || strlen(s) > 64)
		return false;
	for (; *s; s++)
		if (!isalnum((unsigned char)*s) && *s != '.' && *s != '_' && *s != '-')
			return false;
	return true;
}

static void mm_rules_sync(void)
{
	static const char head[] = "# Written by medvemodemd: serial ports ModemManager must not take (profile mm_ports \"control\").\n"
				   "# One rule per modem, keyed by its USB serial; read by ModemManager when it starts.\n";
	char *old = NULL, *out, line[512], dir[PATH_MAX], tmp[PATH_MAX + 8], *slash;
	size_t cap = 4096, len = 0;
	FILE *f;

	snprintf(dir, sizeof(dir), "%s", mm_rules_path);
	slash = strrchr(dir, '/');
	if (!slash)
		return;
	*slash = 0;
	if (access(dir, W_OK))
		return;		/* no ModemManager rules directory: nothing reads it */
	if (!(out = malloc(cap)))
		return;
	len = (size_t)snprintf(out, cap, "%s", head);
	/* keep the rules of modems that are not here now */
	if ((f = fopen(mm_rules_path, "r"))) {
		char id[200] = "";

		while (fgets(line, sizeof(line), f)) {
			if (!strncmp(line, "# device ", 9)) {
				snprintf(id, sizeof(id), "%.*s", (int)strcspn(line + 9, "\n"), line + 9);
				continue;
			}
			if (line[0] == '#' || !*id)
				continue;
			{
				json_object *rec = mm_registry_find(&registry, id);

				if (!(rec && json_object_get_boolean(json_object_object_get(rec, "present"))) &&
				    len + strlen(id) + strlen(line) + 16 < cap)
					len += (size_t)snprintf(out + len, cap - len, "# device %s\n%s", id, line);
			}
			id[0] = 0;
		}
		fclose(f);
	}
	if (registry.devices) {
		json_object_object_foreach(registry.devices, id, rec) {
			const char *serial = jstr(rec, "serial"), *vp = jstr(rec, "vidpid"), *mp;
			json_object *srv;
			char vid[8] = "", pid[8] = "";

			if (!json_object_get_boolean(json_object_object_get(rec, "present")) || (serial && *serial && !rule_safe(serial)) ||
			    !vp || sscanf(vp, "%4[0-9a-f]:%4[0-9a-f]", vid, pid) != 2)
				continue;
			srv = served(rec);
			mp = jstr(json_object_object_get(srv, "profile"), "mm_ports");
			/* a modem with no USB serial (the bench's DW5821e) by its VID:PID alone: the same profile, so the same
			 * policy, holds for every modem of that ID (bench 2026-10-26: no rule, ModemManager took its AT port) */
			if (mp && !strcmp(mp, "control") && len + 400 < cap)
				len += (size_t)snprintf(out + len, cap - len,
					"# device %s\nSUBSYSTEM==\"tty\", ATTRS{idVendor}==\"%s\", ATTRS{idProduct}==\"%s\", "
					"%s%s%sENV{ID_MM_PORT_IGNORE}=\"1\"\n", id, vid, pid, serial && *serial ? "ATTRS{serial}==\"" : "",
					serial && *serial ? serial : "", serial && *serial ? "\", " : "");
			json_object_put(srv);
		}
	}
	if (len == strlen(head))
		out[0] = 0, len = 0;
	if (mm_rules_text && !strcmp(mm_rules_text, out)) {
		free(out);
		return;
	}
	/* what is on flash may already say this (a restart of the engine) */
	if ((f = fopen(mm_rules_path, "r"))) {
		old = calloc(1, cap + 1);
		if (old)
			if (fread(old, 1, cap, f)) {}
		fclose(f);
	}
	if ((old && !strcmp(old, out)) || (!old && !len)) {
		free(old);
		free(mm_rules_text);
		mm_rules_text = out;
		return;
	}
	free(old);
	snprintf(tmp, sizeof(tmp), "%s.tmp", mm_rules_path);
	if (!len) {
		unlink(mm_rules_path);
	} else {
		bool ok = (f = fopen(tmp, "w")) != NULL;

		ok = ok && fputs(out, f) >= 0;
		if (f && fclose(f))
			ok = false;
		if (!ok || rename(tmp, mm_rules_path)) {
			/* said once for this text: not rewritten and warned about on every sync (a full overlay) */
			ULOG_WARN("cannot write %s: %s\n", mm_rules_path, strerror(errno));
			unlink(tmp);
			free(mm_rules_text);
			mm_rules_text = out;
			return;
		}
	}
	ULOG_INFO("ModemManager port rules updated (%s); they apply from its next start\n", mm_rules_path);
	free(mm_rules_text);
	mm_rules_text = out;
}

/*
 * The frozen modems' USB paths (run_dir/mm-frozen) for the hotplug hook 26-medvemodem-mm-frozen: a port of one that
 * appears again (a USB reset, a replug) is withdrawn from ModemManager at once, before ModemManager probes it with AT
 * (the owner, 2026-10-21: «ModemManager продолжает трогать» a frozen modem).
 */
static char frozen_listed[1024];
static bool frozen_listed_once;

static void frozen_list_sync(void)
{
	char text[1024] = "", path[PATH_MAX], tmp[PATH_MAX + 8];
	size_t len = 0;
	FILE *f;

	if (frozen_map) {
		json_object_object_foreach(frozen_map, id, v) {
			json_object *rec = mm_registry_find(&registry, id);
			const char *att = jstr(rec, "attachment_id");

			(void)v;
			if (att && !strncmp(att, "usb:", 4) && len + strlen(att + 4) + 2 < sizeof(text))
				len += (size_t)snprintf(text + len, sizeof(text) - len, "%s\n", att + 4);
		}
	}
	if (frozen_listed_once && !strcmp(text, frozen_listed))
		return;
	snprintf(path, sizeof(path), "%s/mm-frozen", run_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(run_dir, 0755);
	if (!(f = fopen(tmp, "w")))
		return;
	fputs(text, f);
	if (fclose(f) || rename(tmp, path))
		return;
	snprintf(frozen_listed, sizeof(frozen_listed), "%s", text);
	frozen_listed_once = true;
}

static void withhold_sync(void)
{
	char text[1024] = "", path[PATH_MAX], tmp[PATH_MAX + 8];
	size_t len = 0;
	FILE *f;

	if (registry.devices) {
		json_object_object_foreach(registry.devices, id, rec) {
			const char *att = jstr(rec, "attachment_id"), *mp;
			json_object *srv;

			(void)id;
			if (!att || strncmp(att, "usb:", 4))
				continue;
			srv = served(rec);
			mp = jstr(json_object_object_get(srv, "profile"), "mm_ports");
			if (mp && !strcmp(mp, "control") && len + strlen(att + 4) + 2 < sizeof(text))
				len += (size_t)snprintf(text + len, sizeof(text) - len, "%s\n", att + 4);
			json_object_put(srv);
		}
	}
	mm_rules_sync();
	frozen_list_sync();
	if (!strcmp(text, withheld))
		return;
	snprintf(path, sizeof(path), "%s/mm-withhold", run_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(run_dir, 0755);
	if (!(f = fopen(tmp, "w")))
		return;
	fputs(text, f);
	if (fclose(f) || rename(tmp, path))
		return;
	snprintf(withheld, sizeof(withheld), "%s", text);
	if (*text)
		ULOG_INFO("serial ports kept from ModemManager on USB %.*s\n", (int)strcspn(text, "\n"), text);
}

static void hostfix_sync(void)
{
	withhold_sync();
	hostfix_timer.cb = hostfix_run;
	uloop_timeout_set(&hostfix_timer, 0);
}

/* ---- fresh owner view for clients, and a modem ModemManager did not take ---- */

static void answer_devices(struct ubus_context *ctx, struct ubus_request_data *req, bool absent)
{
	json_object *list = mm_registry_list(&registry, absent), *out = json_object_new_array();
	size_t i;

	for (i = 0; i < json_object_array_length(list); i++)
		json_object_array_add(out, served(json_object_array_get_idx(list, i)));
	reply_json(ctx, req, "devices", out);
	json_object_put(out);
	json_object_put(list);
}

/* answer everyone who waited for the ModemManager read: it finished, or MM_WAIT_MS passed */
static void mm_waiters_answer(void)
{
	int i;

	uloop_timeout_cancel(&mm_wait_timer);
	for (i = 0; i < MM_WAITERS; i++) {
		struct mm_waiter *w = &mm_waiters[i];
		json_object *rec;

		if (!w->used)
			continue;
		w->used = false;
		if (w->all) {
			answer_devices(w->ctx, &w->req, w->absent);
			ubus_complete_deferred_request(w->ctx, &w->req, UBUS_STATUS_OK);
		} else if ((rec = mm_registry_find(&registry, w->id))) {
			reply_record(w->ctx, &w->req, rec);
			ubus_complete_deferred_request(w->ctx, &w->req, UBUS_STATUS_OK);
		} else {
			ubus_complete_deferred_request(w->ctx, &w->req, UBUS_STATUS_NOT_FOUND);
		}
	}
}

static void mm_wait_expired(struct uloop_timeout *t)
{
	(void)t;
	mm_waiters_answer();
}

/* hold a device/devices answer until ModemManager was read again; false when it is fresh or no slot is free */
static bool mm_wait(struct ubus_context *ctx, struct ubus_request_data *req, bool all, bool absent, const char *id)
{
	int i;

	if (!mm_stale())
		return false;
	/*
	 * A page opened after a quiet minute waited up to MM_WAIT_MS for every mmcli read (the owner, 2026-10-19: «главная
	 * MedveModem, страница модем явно имеют задержки»). A view read within MM_WAIT_OLD_MS answers now and is read
	 * again behind it (the page's next poll shows it); only an older one, or none yet, holds the answer.
	 */
	if (mm_last_done >= 0 && !strcmp(mm_backend, "available") && now_mono_ms() - mm_last_done < MM_WAIT_OLD_MS) {
		mm_maybe_refresh();
		return false;
	}
	for (i = 0; i < MM_WAITERS && mm_waiters[i].used; i++)
		;
	if (i == MM_WAITERS)
		return false;
	mm_maybe_refresh();
	mm_waiters[i].used = true;
	mm_waiters[i].all = all;
	mm_waiters[i].absent = absent;
	snprintf(mm_waiters[i].id, sizeof(mm_waiters[i].id), "%s", id ? id : "");
	mm_waiters[i].ctx = ctx;
	ubus_defer_request(ctx, req, &mm_waiters[i].req);
	if (!mm_wait_timer.pending) {
		mm_wait_timer.cb = mm_wait_expired;
		uloop_timeout_set(&mm_wait_timer, MM_WAIT_MS);
	}
	return true;
}

/*
 * A present modem ModemManager should hold but has no object for: after a
 * reset MM may have probed it while it was still booting, given up ("Failed to
 * find primary AT port") and, without udev on OpenWrt, never looks again
 * (bench 2026-10-02). After MM_KICK_MS report its ports; up to MM_KICKS times
 * per generation, each wait twice the one before: a report that arrives while
 * MM is still probing the booting modem is dropped ("port context already
 * being processed", bench 2026-10-07), the next one after MM gave up works.
 *
 * A report starts with a remove, which ends what MM was doing with the device.
 * While MM probes or initialises it (a port of it is open in ModemManager or
 * its QMI/MBIM proxy) nothing is reported and the wait starts again; the FM350
 * takes about a minute (bench 2026-10-09: reports after 31 s and the
 * mm_not_holding control step destroyed two modem objects MM had just built).
 * Waits count from the last report, whoever made it (H19).
 */
/* ports of the device by their node name (ttyUSB2, cdc-wdm0) */
static bool mm_port_of(json_object *rec, const char *node)
{
	static const char *const kinds[] = { "tty", "control", NULL };
	json_object *ports = json_object_object_get(rec, "ports");
	int k;

	for (k = 0; ports && kinds[k]; k++) {
		json_object *list = json_object_object_get(ports, kinds[k]);
		size_t j;

		for (j = 0; list && j < json_object_array_length(list); j++) {
			const char *p = json_object_get_string(json_object_array_get_idx(list, j)), *b;

			if (!p)
				continue;
			b = strrchr(p, '/');
			if (!strcmp(b ? b + 1 : p, node))
				return true;
		}
	}
	return false;
}

/*
 * A pppd netifd runs for an interface the engine manages: OpenWrt's proto modemmanager dials PPP itself for a modem
 * whose bearer asks for it («IPv4 connection setup required in interface mm0: ppp», pppd with «ifname ppp-mm0» and no
 * ModemManager plugin). A ZTE MF100 under r163 still read «ttyUSB2 is held by another program, pppd» (a tester's Nokia,
 * 2026-11-06). A pppd of an interface the engine did not make (a 3g/ppp one by hand) stays another program.
 */
static bool ppp_of_our_interface(const char *cmd)
{
	/* «ifname ppp-mm0» (netifd's ppp.sh, ModemManager 1.24 on a snapshot) or «ipparam mm0» alone: OpenWrt 24.10's proto
	 * modemmanager (ModemManager 1.22) starts pppd with no ifname, its link is ppp0 (a tester's MF100 under r186,
	 * 2026-11-12, still read «another program holds a modem port: pppd») */
	const char *p = strstr(cmd, "ifname ppp-"), *key = "ifname ppp-";
	char name[32];
	size_t n;
	json_object *devs = json_object_object_get(cfg.ledger, "devices");

	if (!p) {
		p = strstr(cmd, " ipparam ");
		key = " ipparam ";
	}
	if (!p || !devs)
		return false;
	p += strlen(key);
	n = strcspn(p, " ");
	if (!n || n >= sizeof(name))
		return false;
	memcpy(name, p, n);
	name[n] = 0;
	json_object_object_foreach(devs, id, led) {
		(void)id;
		if (jstr(led, "interface") && !strcmp(jstr(led, "interface"), name))
			return true;
	}
	return false;
}

/* Processes other than the engine (its workers keep its name), ModemManager and its proxies that have a
 * tty or cdc-wdm open: {node: "comm (pid N)"}. */
static json_object *scan_tty_holders(void)
{
	json_object *out = json_object_new_object();
	DIR *d = opendir(proc_dir), *f;
	struct dirent *de, *fe;
	char self[16];

	snprintf(self, sizeof(self), "%d", (int)getpid());
	while (d && (de = readdir(d))) {
		char path[PATH_MAX], comm[64] = "", link[PATH_MAX];
		FILE *fp;
		ssize_t n;

		if (!isdigit((unsigned char)de->d_name[0]) || !strcmp(de->d_name, self))
			continue;
		snprintf(path, sizeof(path), "%s/%s/comm", proc_dir, de->d_name);
		if (!(fp = fopen(path, "r")))
			continue;
		if (!fgets(comm, sizeof(comm), fp))
			comm[0] = 0;
		fclose(fp);
		comm[strcspn(comm, "\n")] = 0;
		if (!strcmp(comm, "medvemodemd") || !strncmp(comm, mm_process, 15) || !strcmp(comm, "qmi-proxy") ||
		    !strcmp(comm, "mbim-proxy"))
			continue;
		/* ModemManager's own PPP: the pppd it starts carries its plugin (mm-pppd-plugin.so). A ZTE MF100 dialling
		 * under ModemManager read «Порт модема занят другой программой: ttyUSB2: pppd» (2026-11-04); a pppd of
		 * netifd's 3g/ppp protocol on a ModemManager modem is still another program */
		if (!strcmp(comm, "pppd")) {
			char cmd[1024] = "";
			size_t got;

			snprintf(path, sizeof(path), "%s/%s/cmdline", proc_dir, de->d_name);
			if ((fp = fopen(path, "r"))) {
				got = fread(cmd, 1, sizeof(cmd) - 1, fp);
				fclose(fp);
				for (size_t i = 0; i < got; i++)
					if (!cmd[i])
						cmd[i] = ' ';
				cmd[got] = 0;
				if (strstr(cmd, "mm-pppd-plugin") || ppp_of_our_interface(cmd))
					continue;
			}
		}
		snprintf(path, sizeof(path), "%s/%s/fd", proc_dir, de->d_name);
		if (!(f = opendir(path)))
			continue;
		while ((fe = readdir(f))) {
			char fd[PATH_MAX + 300], who[96], *del;
			const char *b;
			bool gone;

			if (fe->d_name[0] == '.')
				continue;
			snprintf(fd, sizeof(fd), "%s/%s", path, fe->d_name);
			n = readlink(fd, link, sizeof(link) - 1);
			if (n <= 0)
				continue;
			link[n] = 0;
			/* a port whose device went away reads "/dev/ttyUSB4 (deleted)": still held, its number not reusable */
			gone = (del = strstr(link, " (deleted)")) != NULL;
			if (gone)
				*del = 0;
			if (strncmp(link, "/dev/", 5))
				continue;
			b = strrchr(link, '/') + 1;
			if (strncmp(b, "ttyUSB", 6) && strncmp(b, "ttyACM", 6) && strncmp(b, "cdc-wdm", 7) && strncmp(b, "wwan", 4))
				continue;
			snprintf(who, sizeof(who), "%.40s (pid %.12s)%s", comm, de->d_name, gone ? ", the port is gone" : "");
			json_object_object_add(out, b, json_object_new_string(who));
		}
		closedir(f);
	}
	if (d)
		closedir(d);
	return out;
}

static void holders_note(json_object *before, json_object *after);

static bool tty_holder(const char *node, char *who, size_t size)
{
	json_object *all = scan_tty_holders();
	const char *w = jstr(all, node);

	if (w)
		snprintf(who, size, "%s", w);
	holders_note(tty_holders, all);	/* the fresh look serves the pages and the journal too */
	json_object_put(tty_holders);
	tty_holders = all;
	return w != NULL;
}

static struct uloop_timeout holders_timer;

/* the device a node ("ttyUSB2") belongs to */
static const char *device_of_node(const char *node)
{
	json_object_object_foreach(registry.devices, id, rec) {
		static const char *const kinds[] = { "tty", "control" };
		size_t k, i;

		for (k = 0; k < 2; k++) {
			json_object *list = json_object_object_get(json_object_object_get(rec, "ports"), kinds[k]);

			for (i = 0; list && i < json_object_array_length(list); i++) {
				const char *p = json_object_get_string(json_object_array_get_idx(list, i)), *b = strrchr(p, '/');

				if (!strcmp(b ? b + 1 : p, node))
					return id;
			}
		}
	}
	return NULL;
}

/* "tom_modem (pid 812)" and "tom_modem (pid 940)": one program */
static bool holder_program_is(const char *a, const char *b)
{
	size_t n;

	if (!a || !b)
		return false;
	n = strcspn(a, "(");
	return n == strcspn(b, "(") && !strncmp(a, b, n);
}

/* a modem port another program takes or lets go: one journal line each way (the operator's question
 * «why does the console say busy» is answered in the modem's history) */
static void holders_note(json_object *before, json_object *after)
{
	const char *id;
	char detail[160];

	if (!after)
		return;
	if (!before) {		/* the first look: what is held already is news too */
		json_object *none = json_object_new_object();

		holders_note(none, after);
		json_object_put(none);
		return;
	}
	if (!holders_known)
		holders_known = json_object_new_object();
	json_object_object_foreach(after, node, who) {
		const char *w = json_object_get_string(who);
		json_object *k = json_object_object_get(holders_known, node);
		int repeats;

		if (jstr(before, node) && !strcmp(jstr(before, node), w))
			continue;
		if (!registry.devices || !(id = device_of_node(node)))
			continue;
		/*
		 * bench 2026-10-20 (FM350 handed to QModem): "ttyUSB7 is held by another program, tom_modem (pid N)" every
		 * 15..45 s, a new pid each time: QModem runs its AT tool anew for every look. The same program taking the
		 * same port again is no news: the journal and the log say so once.
		 */
		if (k && holder_program_is(jstr(k, "who"), w)) {
			repeats = json_object_get_int(json_object_object_get(k, "repeats")) + 1;
			json_object_object_add(k, "repeats", json_object_new_int(repeats));
			if (repeats == 1)
				ULOG_INFO("%s: %s is taken by the same program again and again; said once\n", id, node);
			continue;
		}
		k = json_object_new_object();
		json_object_object_add(k, "who", json_object_new_string(w));
		json_object_object_add(holders_known, node, k);
		snprintf(detail, sizeof(detail), "%s: %s", node, w);
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "port-held", detail);
		history_dirty = true;
		ULOG_WARN("%s: %s is held by another program, %s; the engine leaves it alone\n", id, node, w);
	}
	json_object_object_foreach(before, node2, who2) {
		(void)who2;
		if (jstr(after, node2) || !registry.devices || !(id = device_of_node(node2)))
			continue;
		/* a repeating holder's comings and goings: its first release only */
		if (json_object_get_int(json_object_object_get(json_object_object_get(holders_known, node2), "repeats")))
			continue;
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "port-released", node2);
		history_dirty = true;
	}
}

/* a present modem with a serial port: only then is there a port anyone could hold */
static bool any_tty_present(void)
{
	if (registry.devices) {
		json_object_object_foreach(registry.devices, id, rec) {
			json_object *tty = json_object_object_get(json_object_object_get(rec, "ports"), "tty");

			(void)id;
			if (json_object_get_boolean(json_object_object_get(rec, "present")) &&
			    json_object_is_type(tty, json_type_array) && json_object_array_length(tty))
				return true;
		}
	}
	return false;
}

static void holders_scan(struct uloop_timeout *t)
{
	json_object *fresh;

	/* the whole of /proc, every fd of every process: not without a serial port to look for (audit 2026-10-20) */
	if (!any_tty_present()) {
		json_object_put(tty_holders);
		tty_holders = NULL;
		uloop_timeout_set(t, budget_override_ms ? 500 : 15000);
		return;
	}
	fresh = scan_tty_holders();

	holders_note(tty_holders, fresh);
	json_object_put(tty_holders);
	tty_holders = fresh;
	uloop_timeout_set(t, budget_override_ms ? 500 : 15000);
}

/* ModemManager (or the QMI/MBIM proxy it talks through) has a port of the device open: it is probing it */
static bool mm_probing(json_object *rec)
{
	DIR *d = opendir(proc_dir), *f;
	struct dirent *de, *fe;
	bool busy = false;

	if (!d)
		return false;
	while (!busy && (de = readdir(d))) {
		char path[PATH_MAX], comm[64] = "", link[PATH_MAX];
		FILE *fp;
		ssize_t n;

		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		snprintf(path, sizeof(path), "%s/%s/comm", proc_dir, de->d_name);
		if (!(fp = fopen(path, "r")))
			continue;
		if (!fgets(comm, sizeof(comm), fp))
			comm[0] = 0;
		fclose(fp);
		comm[strcspn(comm, "\n")] = 0;
		/* comm is cut at 15 characters */
		if (strncmp(comm, mm_process, 15) && strcmp(comm, "qmi-proxy") && strcmp(comm, "mbim-proxy"))
			continue;
		snprintf(path, sizeof(path), "%s/%s/fd", proc_dir, de->d_name);
		if (!(f = opendir(path)))
			continue;
		while (!busy && (fe = readdir(f))) {
			char fd[PATH_MAX + 300];
			const char *b;

			if (fe->d_name[0] == '.')
				continue;
			snprintf(fd, sizeof(fd), "%s/%s", path, fe->d_name);
			n = readlink(fd, link, sizeof(link) - 1);
			if (n <= 0)
				continue;
			link[n] = 0;
			if (strncmp(link, "/dev/", 5))
				continue;
			b = strrchr(link, '/') + 1;
			busy = mm_port_of(rec, b);
		}
		closedir(f);
	}
	closedir(d);
	return busy;
}

static long long mm_kick_wait(void)
{
	return budget_override_ms ? 1000 : MM_KICK_MS;
}

/* the last report of the device's ports to ModemManager, -1 without one */
static long long mm_reported_at(const char *id)
{
	json_object *t = mm_reported ? json_object_object_get(mm_reported, id) : NULL;

	return t ? json_object_get_int64(t) : -1;
}

/* a port ModemManager could hold: a serial or a QMI/MBIM one (a router-mode modem or a phone's tether has neither) */
static bool mm_ports_present(json_object *rec)
{
	json_object *ports = json_object_object_get(rec, "ports"), *tty = json_object_object_get(ports, "tty");
	json_object *ctl = json_object_object_get(ports, "control");

	/* a removed modem keeps an empty "ports" object: no list to count */
	return (json_object_is_type(tty, json_type_array) && json_object_array_length(tty) > 0) ||
	       (json_object_is_type(ctl, json_type_array) && json_object_array_length(ctl) > 0);
}

/* ModemManager runs and this modem is its to hold: not inhibited, not native, not QModem's, not a router */
static bool mm_should_hold(const char *id, json_object *rec)
{
	const char *b = ledger_backend(id), *prefer = jstr(json_object_object_get(rec, "profile"), "prefer_owner");

	/* bench 2026-10-19: an Android RNDIS tether (generic HiLink) got the mm_not_holding control step every
	 * 2-3 minutes; ModemManager has nothing to take on a router-mode device */
	if (!mm_ports_present(rec) || (prefer && !strcmp(prefer, "hilink")) || (b && !strcmp(b, "device")) || frozen(id) ||
	    foreign_holds(id))
		return false;
	return !strcmp(mm_backend, "available") && json_object_get_boolean(json_object_object_get(rec, "present")) &&
	       !(mm_devices && json_object_object_get(mm_devices, id)) && !inhibited(id) && !qmodem_section_of(rec) &&
	       !(b && (!strcmp(b, "native") || !strcmp(b, "qmodem")));
}

/* ... and is late: not probing it, and no report of its ports is still being worked through */
static bool mm_hold_due(const char *id, json_object *rec)
{
	json_object *e = mm_absent ? json_object_object_get(mm_absent, id) : NULL;
	long long r = mm_reported_at(id);

	if (!mm_should_hold(id, rec))
		return false;
	if (e && json_object_get_boolean(json_object_object_get(e, "probing")))
		return false;
	/* the reports this attachment gets are spent: ModemManager will not take it by itself, the recovery's
	 * control step repeating every few minutes would only stir it (bench 2026-10-19, the T99) */
	if (e && json_object_get_int(json_object_object_get(e, "kicks")) >= MM_KICKS)
		return false;
	return r < 0 || now_mono_ms() - r >= mm_kick_wait();
}

static void mm_kick_check(void)
{
	long long now = now_mono_ms(), next_kick = -1;

	if (!mm_absent)
		mm_absent = json_object_new_object();
	if (!registry.devices || strcmp(mm_backend, "available")) {
		return;
	}
	json_object_object_foreach(registry.devices, id, rec) {
		json_object *e = json_object_object_get(mm_absent, id);
		long long gen = json_object_get_int64(json_object_object_get(rec, "generation"));
		const char *b = ledger_backend(id);

		const char *prefer = jstr(json_object_object_get(rec, "profile"), "prefer_owner");

		/* a router-mode modem or tether has nothing ModemManager takes (bench 2026-10-19); a frozen one is let be */
		if (!json_object_get_boolean(json_object_object_get(rec, "present")) ||
		    (mm_devices && json_object_object_get(mm_devices, id)) || inhibited(id) || qmodem_section_of(rec) ||
		    (b && (!strcmp(b, "native") || !strcmp(b, "qmodem") || !strcmp(b, "device"))) || !mm_ports_present(rec) ||
		    (prefer && !strcmp(prefer, "hilink")) || frozen(id) || foreign_holds(id)) {
			if (e)
				json_object_object_del(mm_absent, id);
			continue;
		}
		if (!e || json_object_get_int64(json_object_object_get(e, "generation")) != gen) {
			e = json_object_new_object();
			json_object_object_add(e, "generation", json_object_new_int64(gen));
			json_object_object_add(e, "since", json_object_new_int64(now));
			json_object_object_add(mm_absent, id, e);
		}
		if (rec_busy(id))
			continue;	/* a recovery step works on it; its end reads ModemManager again */
		{
			int kicks = json_object_get_int(json_object_object_get(e, "kicks"));
			long long wait = mm_kick_wait() << kicks;	/* 30, 60, 120 s */
			long long from = kicks ? json_object_get_int64(json_object_object_get(e, "last")) :
					 json_object_get_int64(json_object_object_get(e, "since"));
			json_object *ps = json_object_object_get(e, "probing_since");
			bool probing;
			char detail[64];

			if (kicks >= MM_KICKS)
				continue;
			probing = mm_probing(rec);
			if (probing && (!ps || now - json_object_get_int64(ps) < MM_PROBE_MAX_MS)) {
				if (!ps) {
					ULOG_INFO("ModemManager is probing %s; its ports are not reported meanwhile\n", id);
					json_object_object_add(e, "probing_since", json_object_new_int64(now));
				}
				json_object_object_add(e, "probing", json_object_new_boolean(1));
				/* the wait starts again once MM lets go of the ports */
				json_object_object_add(e, kicks ? "last" : "since", json_object_new_int64(now));
				if (next_kick < 0 || now + (budget_override_ms ? 200 : MM_PROBE_LOOK_MS) < next_kick)
					next_kick = now + (budget_override_ms ? 200 : MM_PROBE_LOOK_MS);
				continue;
			}
			json_object_object_del(e, "probing");
			if (!probing)
				json_object_object_del(e, "probing_since");
			if (mm_reported_at(id) > from)
				from = mm_reported_at(id);
			if (now - from < wait) {
				/* no event may come to look again: MM reads on its own when the next report is due */
				if (next_kick < 0 || from + wait < next_kick)
					next_kick = from + wait;
				continue;
			}
			ULOG_WARN("ModemManager has not taken %s for %lld s; reporting its ports again (%d of %d)\n", id,
				  (now - json_object_get_int64(json_object_object_get(e, "since"))) / 1000, kicks + 1, MM_KICKS);
			json_object_object_add(e, "kicks", json_object_new_int(kicks + 1));
			json_object_object_add(e, "last", json_object_new_int64(now));
			snprintf(detail, sizeof(detail), "ports reported again (%d of %d)", kicks + 1, MM_KICKS);
			mm_history_add(&history, id, (long long)time(NULL), now, "mm-kick", detail);
			history_dirty = true;
			mm_report_ports(id);
			mm_followups = 2;
			mm_schedule(5000);
			if (kicks + 1 < MM_KICKS && (next_kick < 0 || now + (wait << 1) < next_kick))
				next_kick = now + (wait << 1);
		}
	}
	if (next_kick >= 0)
		mm_schedule((int)(next_kick - now + (budget_override_ms ? 100 : 500)));	/* an earlier read stays earlier */
}

/*
 * H18: netifd's proto modemmanager gives up when
 * ModemManager is late or loses the modem, and the interface stays down until
 * someone runs ifup. When MM takes a modem (again), interfaces with proto
 * modemmanager for that modem that netifd should run (autostart) but has down
 * are brought up. Interfaces the engine manages are left to the engine.
 */
/*
 * netifd gives up on an interface whose connect failed (proto modemmanager:
 * MM_CONNECT_FAILED, then proto_block_restart): autostart off, down until
 * someone runs ifup (bench 2026-10-16: the T99 lost its cell during a USSD and
 * stayed down, a backup channel gone for good). An interface the engine
 * manages, not taken down by the operator, is brought up again once the modem
 * is registered: after 30 s, then 1, 2, 5 and every 10 min; journalled.
 */
static struct uloop_timeout iface_retry_timer;

static void iface_retry_fired(struct uloop_timeout *t)
{
	static const int wait_s[] = { 30, 60, 120, 300, 600 };
	json_object *devs = json_object_object_get(cfg.ledger, "devices");
	long long now = now_mono_ms();
	int tick = budget_override_ms ? 300 : 15000;

	if (!iface_retries)
		iface_retries = json_object_new_object();
	if (!reg_denied)
		reg_denied = json_object_new_object();
	json_object_object_foreach(devs, id, led) {
		const char *iface = jstr(led, "interface");
		json_object *st, *rs = json_object_object_get(iface_retries, id), *mm, *dn;
		const char *reg;
		int step;
		bool blocked, unavailable;

		/* the operator's «Деактивировать» is over once the connection is up again (raised another way), and it was
		 * another modem's when another model sits in the port now: the record of a stick deactivated on 03.11 named a
		 * FM350 put in its port on 16.11 «отключён пользователем в LuCI» and kept the engine from raising it (the owner,
		 * 2026-11-16: «почему модем выключился в luci якобы пользователем хотя я этого не делал») */
		{
			json_object *dd = deactivated ? json_object_object_get(deactivated, id) : NULL, *dr;
			json_object *ss = sessions ? json_object_object_get(sessions, id) : NULL;
			const char *sst = jstr(ss, "state");
			/* connected after the deactivation, not the session the press is taking down this very moment */
			bool upnow = sst && !strcmp(sst, "connected") &&
				     json_object_get_int64(json_object_object_get(ss, "since")) > json_object_get_int64(json_object_object_get(dd, "at")) + 5;

			if (dd && (upnow ||
				   (jstr(dd, "vidpid") && (dr = mm_registry_find(&registry, id)) && jstr(dr, "vidpid") &&
				    strcmp(jstr(dd, "vidpid"), jstr(dr, "vidpid"))))) {
				ULOG_INFO("%s: the operator's deactivation is over (%s)\n", id,
					  upnow ? "connected again" : "another modem in the port");
				json_object_object_del(deactivated, id);
				deactivated_save();
			}
		}
		/* the network's «no» outlives the modem's disabling: after a failed connect ModemManager disables the modem,
		 * a disabled modem reports no registration, and the retry below enabled it again every ten minutes, for
		 * good (a tester's ZTE MF192, 2026-11-11: registration denied, «не поднимает», no status on the card) */
		mm = mm_devices ? json_object_object_get(mm_devices, id) : NULL;
		reg = jstr(mm, "registration");
		dn = json_object_object_get(reg_denied, id);
		if (!mm || (reg && (!strcmp(reg, "home") || !strcmp(reg, "roaming")))) {
			if (dn)
				ULOG_INFO("%s: %s, the network's refusal is over\n", id, mm ? "registered" : "gone");
			json_object_object_del(reg_denied, id);
			dn = NULL;
		} else if (reg && !strcmp(reg, "denied") && !(jstr(mm, "power_state") && !strcmp(jstr(mm, "power_state"), "low"))) {
			if (!dn) {
				dn = json_object_new_object();
				json_object_object_add(dn, "since", json_object_new_int64((long long)time(NULL)));
				json_object_object_add(reg_denied, id, dn);
				ULOG_INFO("%s: the network denied registration; connects wait %lld min between tries\n", id,
					  MM_DENIED_RETRY_MS / 60000);
				mm_history_add(&history, id, (long long)time(NULL), now, "registration-denied", NULL);
				history_dirty = true;
			}
			json_object_object_add(dn, "at", json_object_new_int64((long long)time(NULL)));
			json_object_object_add(dn, "mono", json_object_new_int64(now));
		}
		if (!iface || (deactivated && json_object_object_get(deactivated, id)) || rec_busy(id) || job.active ||
		    reload_running || !(st = netifd_call(iface, "status"))) {
			if (iface && deactivated && json_object_object_get(deactivated, id))
				json_object_object_del(iface_retries, id);
			continue;
		}
		/* down and not trying: netifd turned autostart off after a failed connect; or kept it on but gave up while
		 * ModemManager was not running yet, and ModemManager, holding the modem now, keeps it disabled until a
		 * connect (2026-11-04, a ZTE MF100 on a Nokia: «Подключение…» for good, autostart still on) */
		blocked = !json_object_get_boolean(json_object_object_get(st, "up")) &&
			  !json_object_get_boolean(json_object_object_get(st, "pending")) &&
			  ((json_object_object_get(st, "autostart") && !json_object_get_boolean(json_object_object_get(st, "autostart"))) ||
			   (jstr(mm, "state") && !strcmp(jstr(mm, "state"), "disabled")));
		/* unavailable to netifd, and the monitor that would make it available again cannot find the modem */
		unavailable = json_object_object_get(st, "available") &&
			      !json_object_get_boolean(json_object_object_get(st, "available")) &&
			      jstr(mm, "lookup") && !strcmp(jstr(st, "proto") ? jstr(st, "proto") : "", "modemmanager");
		json_object_put(st);
		if (!blocked) {
			json_object_object_del(iface_retries, id);
			continue;
		}
		step = rs ? json_object_get_int(json_object_object_get(rs, "step")) : 0;
		if (!rs) {
			rs = json_object_new_object();
			json_object_object_add(rs, "step", json_object_new_int(0));
			json_object_object_add(rs, "next", json_object_new_int64(now + (budget_override_ms ? 500 : wait_s[0] * 1000)));
			json_object_object_add(iface_retries, id, rs);
			continue;
		}
		/* registered, or held by ModemManager and disabled: it enables a modem only on a connect, so a registration
		 * would never come (2026-11-04, a ZTE MF100 on a Nokia: netifd started mm0 before ModemManager ran and gave
		 * up; ModemManager took the modem two minutes later and kept it disabled: «Подключение…» for good) */
		if (now < json_object_get_int64(json_object_object_get(rs, "next")) ||
		    ((!reg || (strcmp(reg, "home") && strcmp(reg, "roaming"))) &&
		     !(jstr(mm, "state") && !strcmp(jstr(mm, "state"), "disabled"))))
			continue;
		/* refused by the network: a try every half an hour (a SIM unblocked, roaming allowed), not every ten minutes */
		if (dn && now - json_object_get_int64(json_object_object_get(dn, "mono")) <
			  (budget_override_ms ? 3000 : MM_DENIED_RETRY_MS))
			continue;
		{
			char detail[64];

			snprintf(detail, sizeof(detail), "%s, try %d", iface, step + 1);
			ULOG_INFO("interface %s of %s: netifd gave up after a failed connect; up again (try %d)\n", iface, id, step + 1);
			mm_history_add(&history, id, (long long)time(NULL), now, "interface-retry", detail);
			history_dirty = true;
			if (unavailable && netifd_set_available(iface))
				ULOG_INFO("interface %s of %s: marked available for netifd (ModemManager gave the modem no physdev)\n",
					  iface, id);
			netifd_do(iface, "up");
		}
		step++;
		json_object_object_add(rs, "step", json_object_new_int(step));
		json_object_object_add(rs, "next", json_object_new_int64(now + (budget_override_ms ? 1000 :
					wait_s[step < 5 ? step : 4] * 1000LL)));
	}
	uloop_timeout_set(t, tick);
}

static json_object *ifup_pending;	/* device_id -> true */
static struct uloop_timeout ifup_timer;

static void ifup_run(struct uloop_timeout *t)
{
	static const char *const pkgs[] = { "network", NULL };
	json_object *img, *net, *ledgers = json_object_object_get(cfg.ledger, "devices");
	char err[160];

	(void)t;
	if (!ifup_pending)
		return;
	img = mm_uci_read(conf_dir, pkgs, err, sizeof(err));
	net = json_object_object_get(img, "network");
	json_object_object_foreach(ifup_pending, id, v) {
		json_object *mm = mm_devices ? json_object_object_get(mm_devices, id) : NULL;
		const char *dev = jstr(mm, "device");
		(void)v;

		if (!dev || !net)
			continue;
		json_object_object_foreach(net, name, sec) {
			char key[128];
			json_object *st, *up, *autostart;
			bool engine = false;

			if (!jstr(sec, ".type") || strcmp(jstr(sec, ".type"), "interface") || !jstr(sec, "proto") ||
			    strcmp(jstr(sec, "proto"), "modemmanager") || !jstr(sec, "device") ||
			    (strcmp(jstr(sec, "device"), dev) && (!jstr(mm, "lookup") || strcmp(jstr(sec, "device"), jstr(mm, "lookup")))))
				continue;
			snprintf(key, sizeof(key), "network.%s", name);
			json_object_object_foreach(ledgers, lid, led) {
				(void)lid;
				engine = engine || json_object_object_get(json_object_object_get(led, "sections"), key);
			}
			if (engine || !(st = netifd_call(name, "status")))
				continue;
			up = json_object_object_get(st, "up");
			autostart = json_object_object_get(st, "autostart");
			if (!json_object_get_boolean(up) && autostart && json_object_get_boolean(autostart) &&
			    !json_object_get_boolean(json_object_object_get(st, "pending"))) {
				ULOG_INFO("ModemManager holds %s again; netifd had given up on %s, bringing it up\n", id, name);
				mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "ifup", name);
				history_dirty = true;
				netifd_do(name, "up");
			}
			json_object_put(st);
		}
	}
	json_object_put(img);
	json_object_put(ifup_pending);
	ifup_pending = NULL;
}

static void ifup_after_take(const char *id)
{
	if (!ifup_pending)
		ifup_pending = json_object_new_object();
	json_object_object_add(ifup_pending, id, json_object_new_boolean(1));
	ifup_timer.cb = ifup_run;
	uloop_timeout_set(&ifup_timer, budget_override_ms ? 500 : 3000);	/* let netifd see MM first */
}

/* ---- the modem's network interface: checked and repaired (spec 17, F9, F10) ----
 *
 * Every proto modemmanager interface that serves a present modem is checked
 * after discovery passes and ModemManager reads: its device must be the
 * modem's current sysfs path (H618 numbers its USB buses in boot order, so a
 * path written on one boot names nothing on the next: F9), and it must be in
 * a firewall zone (an interface outside every zone gets no NAT: the router
 * reaches the internet, its clients do not: F10). A user's interface (the
 * image's "modem", another package's, one made by hand) is repaired with single
 * UCI ops that apply only while the old value is still there; it joins "wan"
 * only when it is in no zone (never moved out of another one). The engine's
 * own interface is repaired by its transaction (ledger kept). Then the
 * changed packages are reloaded and the interface brought up. Findings are
 * served per device as "netif"; -L 0 (UCI core.repair_interfaces 0)
 * keeps them reported only.
 */
static int repair_ifaces = 1;
/* iface_checks: device_id -> [{name, engine, zone, findings, repaired}], declared above */
static json_object *iface_tried;	/* "<id>|<generation>|<iface>" -> true: one repair try each */
static struct uloop_timeout iface_timer;

static bool path_exists(const char *path)
{
	struct stat st;

	return !stat(path, &st);
}

/* the modem's sysfs device path now: ModemManager's, else the registry's USB device */
static const char *current_path(const char *id, json_object *rec, char *buf, size_t size)
{
	json_object *mm = mm_devices ? json_object_object_get(mm_devices, id) : NULL;
	const char *att = jstr(rec, "attachment_id");
	char link[PATH_MAX];

	if (jstr(mm, "lookup"))		/* no physdev from ModemManager: netifd finds it by its object (mmgr.c) */
		return jstr(mm, "lookup");
	if (jstr(mm, "device"))
		return jstr(mm, "device");
	if (!att || strncmp(att, "usb:", 4))
		return NULL;
	snprintf(link, sizeof(link), "%s/%s", SYS_USB, att + 4);
	if (size < PATH_MAX || !realpath(link, buf))
		return NULL;
	/* the form netifd and ModemManager use (a no-op on the router; tests have their sysfs elsewhere) */
	if (realpath(SYS_DEVICES, link) && strcmp(link, "/sys/devices") && !strncmp(buf, link, strlen(link)) &&
	    buf[strlen(link)] == '/') {
		char rest[PATH_MAX];

		snprintf(rest, sizeof(rest), "%s", buf + strlen(link));
		snprintf(buf, size, "/sys/devices%s", rest);
	}
	return buf;
}

static void metrics_note(json_object *net)
{
	json_object *m = json_object_new_object();

	json_object_object_foreach(net, sec, val) {
		const char *t = jstr(val, ".type"), *v = jstr(val, "metric");

		/* no metric is netifd's 0: such an interface beats every 10, 20, 30 the priority gives (bench 2026-10-20: a
		 * cdc_ether dongle on its own interface carried the traffic, and «Сделать основным» never showed elsewhere) */
		if (t && !strcmp(t, "interface"))
			json_object_object_add(m, sec, json_object_new_int(v && *v ? atoi(v) : 0));
	}
	json_object_put(iface_metrics);
	iface_metrics = m;
}

/* the router's own uplinks (a wired WAN) of the network config, the modems' interfaces left out */
static json_object *wired_now(json_object *net)
{
	json_object *skip = json_object_new_array(), *w, *leds = json_object_object_get(cfg.ledger, "devices");

	if (leds) {
		json_object_object_foreach(leds, id, led) {
			(void)id;
			if (jstr(led, "interface"))
				json_object_array_add(skip, json_object_new_string(jstr(led, "interface")));
		}
	}
	if (iface_checks) {
		json_object_object_foreach(iface_checks, id2, list) {
			(void)id2;
			for (size_t i = 0; json_object_is_type(list, json_type_array) && i < json_object_array_length(list); i++)
				if (jstr(json_object_array_get_idx(list, i), "name"))
					json_object_array_add(skip, json_object_new_string(jstr(json_object_array_get_idx(list, i), "name")));
		}
	}
	if (!net) {
		/* read now: the operator's own edit of the network (a wan6 added) shows at once */
		static const char *const pkgs[] = { "network", NULL };
		char err[160];
		json_object *img = mm_uci_read(conf_dir, pkgs, err, sizeof(err));

		w = mm_netcfg_wired(json_object_object_get(img, "network"), skip);
		json_object_put(img);
	} else {
		w = mm_netcfg_wired(net, skip);
	}
	json_object_put(skip);
	return w;
}

static void add_package(json_object *pk, const char *name)
{
	size_t i;

	for (i = 0; i < json_object_array_length(pk); i++)
		if (!strcmp(json_object_get_string(json_object_array_get_idx(pk, i)), name))
			return;
	json_object_array_add(pk, json_object_new_string(name));
}

/*
 * A router-mode modem's DHCP interface names the kernel's network device (usb0, usb1), and the kernel names them in the
 * order the sticks come up (bench 2026-10-20: a UFI moved to another port came up after the E3372; the two swapped
 * usb0 and usb1, each modem's interface carried the other one's traffic, the E3372's recovery restarted the UFI's
 * session, and a leftover interface of the UFI's old port held the E3372's device). The interface follows its
 * modem's device now: an absent modem's interface naming this one's device is released first, then this modem's own
 * is rewritten by its transaction. Once per modem and generation and device, as the other repairs.
 */
#define MM_SQUAT_GRACE_S 180
static long long squat_wait_ms;	/* a release put off by the grace: iface_run looks again then */

/* a hand-set device that is provably wrong: a netdev that does not exist or is another present modem's, any /dev node
 * (it is not this modem's: the planner names another). A bridge or a VLAN the operator put there stays theirs */
static bool device_provably_wrong(const char *id, const char *was)
{
	char path[PATH_MAX];

	if (!was || !*was)
		return false;
	if (!strncmp(was, "/dev/", 5))
		return true;
	snprintf(path, sizeof(path), "%s/%s", SYS_NET, was);
	if (access(path, F_OK))
		return true;
	json_object_object_foreach(registry.devices, oid, orec) {
		json_object *nets = json_object_object_get(json_object_object_get(orec, "ports"), "net");
		size_t n;

		if (!strcmp(oid, id) || !json_object_get_boolean(json_object_object_get(orec, "present")))
			continue;
		for (n = 0; nets && n < json_object_array_length(nets); n++)
			if (!strcmp(json_object_get_string(json_object_array_get_idx(nets, n)), was))
				return true;
	}
	return false;
}

/*
 * A repair is tried once per modem, attachment and device (HANDOFF 200 item 11: a refused one ran again on every pass),
 * but a transaction that failed for a passing reason (the configuration lock taken, a write that did not go) is tried
 * again a minute later, at most MM_REPAIR_TRIES times (an audit of r181, D-03: it waited for the next replug). A refusal
 * of the planner (nothing to write, the operator's field) stays final.
 */
#define MM_REPAIR_TRIES 5
#define MM_REPAIR_BACKOFF_MS 60000LL

static bool repair_due(const char *key)
{
	json_object *v = json_object_object_get(iface_tried, key);

	if (!v)
		return true;
	if (json_object_is_type(v, json_type_boolean))
		return false;
	return now_mono_ms() >= json_object_get_int64(json_object_object_get(v, "at"));
}

static void repair_done(const char *key, json_object *r)
{
	json_object *v = json_object_object_get(iface_tried, key), *n;
	int tries = v && json_object_is_type(v, json_type_object) ? json_object_get_int(json_object_object_get(v, "tries")) : 0;

	if (strcmp(status_of(r), "failed") || tries + 1 >= MM_REPAIR_TRIES) {
		json_object_object_add(iface_tried, key, json_object_new_boolean(1));
		return;
	}
	{
		long long backoff = getenv("MEDVEMODEM_REPAIR_BACKOFF_MS") ? atoll(getenv("MEDVEMODEM_REPAIR_BACKOFF_MS")) : MM_REPAIR_BACKOFF_MS;

		n = json_object_new_object();
		json_object_object_add(n, "tries", json_object_new_int(tries + 1));
		json_object_object_add(n, "at", json_object_new_int64(now_mono_ms() + backoff));
		json_object_object_add(iface_tried, key, n);
		if (!squat_wait_ms || backoff + 1000 < squat_wait_ms)
			squat_wait_ms = backoff + 1000;	/* iface_run looks again then */
	}
}

static void netdev_repair(json_object *img, const char *id, json_object *rec, json_object *pk, json_object *ups)
{
	json_object *nets = json_object_object_get(json_object_object_get(rec, "ports"), "net"), *chk, *sq;
	json_object *ledgers = json_object_object_get(cfg.ledger, "devices");
	const char *net = nets && json_object_array_length(nets) ? json_object_get_string(json_object_array_get_idx(nets, 0)) : NULL;
	char key[240];
	size_t i, k;

	if (!net || !repair_ifaces)
		return;
	{
		/* the device the planner would write now (the netdev, the AT dial's netdev, a control node, a tty): whatever
		 * the kernel numbered anew is compared with it (the owner, 2026-11-09: «не только на fm350 случай») */
		json_object *arg = desire_arg(rec), *plan = desire(arg, img, ledgers), *secs, *f = NULL;
		const char *mine = ledgers ? jstr(json_object_object_get(ledgers, id), "interface") : NULL;

		if (mine && (secs = json_object_object_get(json_object_object_get(plan, "desired"), "sections"))) {
			char key[64];

			snprintf(key, sizeof(key), "network.%s", mine);
			f = json_object_object_get(json_object_object_get(secs, key), "fields");
		}
		chk = mm_netcfg_netdev_check(img, ledgers, id, net, jstr(f, "device"));
		json_object_put(plan);
		json_object_put(arg);
	}
	sq = json_object_object_get(chk, "squatters");
	for (i = 0; i < json_object_array_length(sq); i++) {
		const char *other = json_object_get_string(json_object_array_get_idx(sq, i));
		json_object *orec = json_object_object_get(registry.devices, other), *r, *rp;
		char detail[200];

		/* a present modem naming this device is stale itself and gets its own device on its own turn */
		if (orec && json_object_get_boolean(json_object_object_get(orec, "present")))
			continue;
		/* not in the first minutes after a start, while the sticks are still coming up: a later pass releases it (a
		 * tester's OPiZ3, 2026-11-09, r175: the M150-4 came up seconds after the FM350, its mm5 named the FM350's new
		 * eth3 and was released while the stick was on its way; mm5 kept only its hand-set fields) */
		{
			/* by the monotonic clock (a router without an RTC jumps years at its first NTP answer), and a look is
			 * asked for when the wait is over: nothing else may bring one (regression review 2026-11-11). Each
			 * collision of an absent modem and a device has its own wait from when it was first seen, at a start or
			 * hours later at a replug (an audit of r181, D-02: one wait for the engine's whole life) */
			static json_object *seen;
			char sk[300];
			long long grace = (getenv("MEDVEMODEM_SQUAT_GRACE_S") ? atoll(getenv("MEDVEMODEM_SQUAT_GRACE_S")) : MM_SQUAT_GRACE_S) * 1000LL;
			long long first, now = now_mono_ms();

			if (!seen)
				seen = json_object_new_object();
			snprintf(sk, sizeof(sk), "%s|%s", other, net);
			if (!json_object_object_get(seen, sk)) {
				/* a day's old ones go: the map stays small */
				json_object *drop = json_object_new_array();
				size_t q;

				json_object_object_foreach(seen, ok, ov)
					if (now - json_object_get_int64(ov) > 86400000LL)
						json_object_array_add(drop, json_object_new_string(ok));
				for (q = 0; q < json_object_array_length(drop); q++)
					json_object_object_del(seen, json_object_get_string(json_object_array_get_idx(drop, q)));
				json_object_put(drop);
				json_object_object_add(seen, sk, json_object_new_int64(now));
			}
			first = json_object_get_int64(json_object_object_get(seen, sk));
			if (now - first < grace) {
				long long left = first + grace - now + 1000;

				if (!squat_wait_ms || left < squat_wait_ms)
					squat_wait_ms = left;
				continue;
			}
		}
		/* "<id>|<generation>|…": map_retain keeps a key while its modem's attachment lasts (HANDOFF 200 item 11:
		 * "squat|…" read as a device id was wiped each pass, so a refused repair ran again on every one) */
		snprintf(key, sizeof(key), "%s|%lld|squat|%s|%s", id, (long long)json_object_get_int64(json_object_object_get(rec, "generation")),
			 other, net);
		if (!repair_due(key))
			continue;
		r = mm_cfgtxn_run(&cfg, other, NULL, NULL, false, now_mono_ms());
		repair_done(key, r);
		snprintf(detail, sizeof(detail), "the interface of an absent modem named %s, now this modem's: released (%s)", net, status_of(r));
		ULOG_WARN("%s: %s\n", other, detail);
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "iface-repair", detail);
		history_dirty = true;
		config_note(r, "release");
		if (!strcmp(status_of(r), "committed"))
			for (k = 0; (rp = json_object_object_get(r, "packages")) && k < json_object_array_length(rp); k++)
				add_package(pk, json_object_get_string(json_object_array_get_idx(rp, k)));
		json_object_put(r);
	}
	if (jstr(chk, "stale")) {
		snprintf(key, sizeof(key), "%s|%lld|netdev|%s", id, (long long)json_object_get_int64(json_object_object_get(rec, "generation")), net);
		if (repair_due(key)) {
			json_object *arg = desire_arg(rec), *r, *rp;
			char detail[200];

			/* the stale value is taken back whoever wrote it (a tester's OPiZ3, 2026-11-09: mm3's device set to eth2 by
			 * hand, the next boot named the FM350 eth3, and the repair said «unchanged»: the field was the user's) */
			{
				const char *was = jstr(json_object_object_get(json_object_object_get(img, "network"), jstr(chk, "stale")), "device");
				json_object *env = json_object_object_get(arg, "env");

				if (!env) {
					env = json_object_new_object();
					json_object_object_add(arg, "env", env);
				}
				if (device_provably_wrong(id, was))
					json_object_object_add(env, "stale_device", json_object_new_string(was));
			}
			r = mm_cfgtxn_run(&cfg, id, desire, arg, false, now_mono_ms());
			repair_done(key, r);
			snprintf(detail, sizeof(detail), "%s: network device %s (the kernel names the sticks in the order they come up): %s",
				 jstr(chk, "stale"), net, status_of(r));
			ULOG_WARN("interface of %s repaired: %s\n", id, detail);
			mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "iface-repair", detail);
			history_dirty = true;
			config_note(r, "repair");
			if (!strcmp(status_of(r), "committed")) {
				for (k = 0; (rp = json_object_object_get(r, "packages")) && k < json_object_array_length(rp); k++)
					add_package(pk, json_object_get_string(json_object_array_get_idx(rp, k)));
				json_object_array_add(ups, json_object_new_string(id));
			}
			json_object_put(arg);
			json_object_put(r);
		}
	}
	json_object_put(chk);
}

static void iface_run(struct uloop_timeout *t)
{
	static const char *const pkgs[] = { "network", "firewall", NULL };
	json_object *img, *pk, *ups, *paths;
	char err[160];

	(void)t;
	if (!registry.devices)
		return;
	if (reload_running || job.active || pending_release.active || rj.active) {
		uloop_timeout_set(&iface_timer, 2000);	/* someone else is changing things: look after */
		return;
	}
	if (!(img = mm_uci_read(conf_dir, pkgs, err, sizeof(err))))
		return;
	metrics_note(json_object_object_get(img, "network"));
	pk = json_object_new_array();
	ups = json_object_new_array();
	if (!iface_checks)
		iface_checks = json_object_new_object();
	if (!iface_tried)
		iface_tried = json_object_new_object();
	foreign_retain(registry.devices);
	/* the present modems' paths: an interface left on a hub is moved only to the one modem behind it */
	paths = json_object_new_array();
	json_object_object_foreach(registry.devices, pid, prec) {
		char pbuf[PATH_MAX];
		const char *pp;

		if (json_object_get_boolean(json_object_object_get(prec, "present")) && (pp = current_path(pid, prec, pbuf, sizeof(pbuf))))
			json_object_array_add(paths, json_object_new_string(pp));
	}
	json_object_object_foreach(registry.devices, id, rec) {
		json_object *res, *ledgers = json_object_object_get(cfg.ledger, "devices");
		char buf[PATH_MAX];
		const char *path;
		size_t i, k;

		if (!json_object_get_boolean(json_object_object_get(rec, "present")) ||
		    !(path = current_path(id, rec, buf, sizeof(buf)))) {
			json_object_object_del(iface_checks, id);
			continue;
		}
		foreign_note(id, rec, json_object_object_get(img, "network"), ledgers);
		/* the user's own interface by any match config_plan uses (its netdev too: a router-mode stick's `device usb0`,
		 * a tester's MF833V 2026-11-08, read «no interface» on its card while config_plan named mm1) */
		{
			json_object *sv = served(rec);
			const char *ui = mm_netcfg_user_interface(sv, json_object_object_get(img, "network"), ledgers);

			if (!user_ifaces)
				user_ifaces = json_object_new_object();
			if (ui)
				json_object_object_add(user_ifaces, id, json_object_new_string(ui));
			else
				json_object_object_del(user_ifaces, id);
			json_object_put(sv);
		}
		{
			json_object *mmr = mm_devices ? json_object_object_get(mm_devices, id) : NULL;

			res = mm_netcfg_repair(img, ledgers, path, path_exists, paths, jstr(mmr, "lookup") ? jstr(mmr, "device") : NULL);
		}
		for (i = 0; i < json_object_array_length(res); i++) {
			json_object *it = json_object_array_get_idx(res, i), *ops = json_object_object_get(it, "ops");
			json_object *repaired = json_object_new_array(), *prev;
			const char *name = jstr(it, "name");
			char key[200], *prev_why;

			snprintf(key, sizeof(key), "%s|%lld|%s", id,
				 (long long)json_object_get_int64(json_object_object_get(rec, "generation")), name);
			/* true: tried; a string: refused for that reason, tried again after the next read */
			prev = json_object_object_get(iface_tried, key);
			if (!repair_ifaces || !json_object_array_length(json_object_object_get(it, "findings")) ||
			    json_object_is_type(prev, json_type_boolean)) {
				json_object_object_add(it, "repaired", repaired);
				json_object_object_del(it, "ops");
				continue;
			}
			prev_why = prev ? strdup(json_object_get_string(prev)) : NULL;	/* prev goes with the next line */
			json_object_object_add(iface_tried, key, json_object_new_boolean(1));
			if (!json_object_get_boolean(json_object_object_get(it, "engine"))) {
				free(prev_why);
				prev_why = NULL;
			}
			if (json_object_get_boolean(json_object_object_get(it, "engine"))) {
				/* our own interface: the transaction rewrites it and keeps the ledger; a stale device is taken back
				 * even when someone else wrote it (env stale_device) */
				json_object *arg = desire_arg(rec), *r;

				if (jstr(it, "stale_device"))
					json_object_object_add(json_object_object_get(arg, "env"), "stale_device",
							       json_object_new_string(jstr(it, "stale_device")));
				r = mm_cfgtxn_run(&cfg, id, desire, arg, false, now_mono_ms());
				json_object *rp = json_object_object_get(r, "packages");

				if (!strcmp(status_of(r), "refused")) {
					/* not yet (bench 2026-10-09: the bus number changed and ModemManager had not
					 * built the modem at the first passes): tried again after the next read */
					const char *why = jstr(r, "reason") ? jstr(r, "reason") : "not yet";

					if (!prev_why || strcmp(prev_why, why))
						ULOG_INFO("interface %s of %s: repair waits: %s\n", name, id, why);
					json_object_object_add(iface_tried, key, json_object_new_string(why));
					free(prev_why);
					json_object_put(arg);
					json_object_put(r);
					json_object_object_add(it, "repaired", repaired);
					json_object_object_del(it, "ops");
					continue;
				}
				free(prev_why);
				/* what was wrong, said with it: each repair costs a network reload, Wi-Fi included (the owner's boot,
				 * 2026-10-21: mm1 and mm0 repaired at every start, the log did not say why) */
				ULOG_WARN("interface %s of %s: repaired by a transaction (%s): %s\n", name, id,
					  json_object_to_json_string_ext(json_object_object_get(it, "findings"), JSON_C_TO_STRING_PLAIN),
					  status_of(r));
				config_note(r, "repair");
				if (!strcmp(status_of(r), "committed")) {
					for (k = 0; rp && k < json_object_array_length(rp); k++)
						add_package(pk, json_object_get_string(json_object_array_get_idx(rp, k)));
					json_object_array_add(repaired, json_object_new_string("transaction"));
					json_object_array_add(ups, json_object_new_string(id));
				}
				json_object_put(arg);
				json_object_put(r);
			} else if (ops && json_object_array_length(ops)) {
				json_object *checked = mm_cfg_check(img, ops), *apply = json_object_object_get(checked, "apply");

				if (apply && json_object_array_length(apply) &&
				    !mm_uci_apply(conf_dir, cfg.savedir, apply, err, sizeof(err))) {
					for (k = 0; k < json_object_array_length(apply); k++) {
						json_object *op = json_object_array_get_idx(apply, k);
						const char *what = !strcmp(jstr(op, "package"), "network") ? "device" : "wan-zone";
						char detail[160];

						mm_cfg_simulate(img, op);
						add_package(pk, jstr(op, "package"));
						json_object_array_add(repaired, json_object_new_string(what));
						snprintf(detail, sizeof(detail), "%s: %s", name, !strcmp(what, "device") ?
							 "device path of this boot" : "joined the wan zone");
						ULOG_WARN("interface %s of %s repaired: %s\n", name, id, detail);
						mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "iface-repair", detail);
						history_dirty = true;
					}
					json_object_array_add(ups, json_object_new_string(id));
				} else if (apply && json_object_array_length(apply)) {
					ULOG_ERR("interface %s of %s: repair failed: %s\n", name, id, err);
				}
				json_object_put(checked);
			}
			json_object_object_add(it, "repaired", repaired);
			json_object_object_del(it, "ops");
		}
		netdev_repair(img, id, rec, pk, ups);
		json_object_object_add(iface_checks, id, res);
	}
	if (json_object_array_length(pk)) {
		size_t i;

		reload_start(pk);
		for (i = 0; i < json_object_array_length(ups); i++)
			ifup_after_take(json_object_get_string(json_object_array_get_idx(ups, i)));
	}
	json_object_put(pk);
	json_object_put(ups);
	json_object_put(paths);
	json_object_put(img);
	if (squat_wait_ms > 0) {	/* a release the start's grace put off (netdev_repair) */
		uloop_timeout_set(&iface_timer, (int)squat_wait_ms);
		squat_wait_ms = 0;
	}
	auto_connect_sync();	/* it waits for this read: an operator's own interface is known now */
}

static void iface_sync(void)
{
	/* a pending look stays: reads in a row (the signal history's, every minute) must not push it away for good */
	if (iface_timer.pending)
		return;
	iface_timer.cb = iface_run;
	uloop_timeout_set(&iface_timer, budget_override_ms ? 200 : 2000);	/* MM's read settles first */
}

/* ---- emergency ModemManager restart: by the operator only (H16 revised 2026-10-09) ----
 *
 * The engine never restarts ModemManager on its own: it disturbs every modem on the
 * router. The operator may, when ModemManager hangs. `restart` of the init script starts
 * the new daemon while the old one still holds its D-Bus name, and ModemManager then
 * stays away for about 95 s (H16); so: stop, wait until the old
 * process is gone, start. Once in 5 min at most; automatic recovery waits 3 min after it.
 */
#define MM_RESTART_MIN_GAP_MS (5 * 60000)
#define MM_RESTART_QUIET_MS 180000
#define MM_RESTART_TIMEOUT_MS 90000
static const char *mm_init_path = "/etc/init.d/modemmanager";
static struct {
	bool running;
	struct uloop_process proc;
	struct uloop_timeout kill;
	long long started, finished;	/* monotonic ms, -1: never */
	long long at, done_at;		/* wall clock */
	int status;			/* exit status of the init script's start, -1: killed */
} mmr = { .started = -1, .finished = -1 };

static const char mmr_script[] =
	"i=$1; p=$2\n"
	"\"$i\" stop >/dev/null 2>&1\n"
	"n=0; while pidof \"$p\" >/dev/null 2>&1 && [ $n -lt 30 ]; do sleep 1; n=$((n+1)); done\n"
	"if pidof \"$p\" >/dev/null 2>&1; then kill -9 $(pidof \"$p\") 2>/dev/null; sleep 1; fi\n"
	"\"$i\" start >/dev/null 2>&1\n";

static json_object *mmr_status(void)
{
	json_object *o;

	if (mmr.started < 0)
		return NULL;
	o = json_object_new_object();
	json_object_object_add(o, "state", json_object_new_string(mmr.running ? "running" : mmr.status == 0 ? "done" : "failed"));
	json_object_object_add(o, "at", json_object_new_int64(mmr.at));
	json_object_object_add(o, "finished_at", mmr.running ? NULL : json_object_new_int64(mmr.done_at));
	json_object_object_add(o, "next_allowed_s", json_object_new_int64(
		now_mono_ms() - mmr.started >= MM_RESTART_MIN_GAP_MS ? 0 : (MM_RESTART_MIN_GAP_MS - (now_mono_ms() - mmr.started)) / 1000));
	return o;
}

/* automatic recovery holds off while ModemManager restarts and settles */
static bool mmr_quiet(long long now)
{
	return mmr.running || (mmr.finished >= 0 && now - mmr.finished < MM_RESTART_QUIET_MS);
}

static void mmr_journal(const char *kind, const char *detail)
{
	if (!registry.devices)
		return;
	json_object_object_foreach(registry.devices, id, rec) {
		if (json_object_get_boolean(json_object_object_get(rec, "present")))
			mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), kind, detail);
	}
	history_dirty = true;
}

static void mmr_kill(struct uloop_timeout *t)
{
	(void)t;
	if (mmr.running && mmr.proc.pid > 0)
		kill(-mmr.proc.pid, SIGKILL);
}

static void mmr_exited(struct uloop_process *p, int ret)
{
	char detail[64];
	int i;

	(void)p;
	uloop_timeout_cancel(&mmr.kill);
	mmr.running = false;
	mmr.finished = now_mono_ms();
	mmr.done_at = (long long)time(NULL);
	mmr.status = WIFEXITED(ret) ? WEXITSTATUS(ret) : -1;
	snprintf(detail, sizeof(detail), mmr.status == 0 ? "started again" : "init script status %d", mmr.status);
	ULOG_WARN("ModemManager restart by the operator: %s\n", detail);
	mmr_journal("mm-restart-end", detail);
	/* inhibitions ended with the old daemon: take them up again at once, without the back-off */
	for (i = 0; i < MM_INHIBITS; i++)
		if (inhibits[i].used && inhibits[i].wanted && !inhibits[i].proc.pid) {
			inhibits[i].failures = 0;
			inhibits[i].restart.cb = inhibit_restart;
			uloop_timeout_set(&inhibits[i].restart, 2000);
		}
	/* the modems come back through mm-add, which also brings their interfaces up (H18) */
	mm_followups = 2;
	mm_schedule(5000);
}

static const struct blobmsg_policy mmr_policy[] = {
	{ .name = "confirm", .type = BLOBMSG_TYPE_BOOL },
	{ .name = "reason", .type = BLOBMSG_TYPE_STRING },
};

static int mmr_reply(struct ubus_context *ctx, struct ubus_request_data *req, const char *result, const char *why, long long retry_s)
{
	json_object *o = json_object_new_object();
	int rc;

	json_object_object_add(o, "result", json_object_new_string(result));
	if (why)
		json_object_object_add(o, "why", json_object_new_string(why));
	if (retry_s > 0)
		json_object_object_add(o, "retry_s", json_object_new_int64(retry_s));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

static int m_mm_restart(struct ubus_context *ctx, struct ubus_object *obj,
			struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[2];
	char reason[80] = "", detail[120];
	long long now = now_mono_ms();
	pid_t pid;
	int fd;

	(void)obj; (void)method;
	blobmsg_parse(mmr_policy, 2, tb, blob_data(msg), blob_len(msg));
	if (!tb[0] || !blobmsg_get_bool(tb[0]))
		return mmr_reply(ctx, req, "refused", "confirm is required: every modem on the router loses ModemManager for a while", 0);
	if (tb[1]) {
		size_t k, j = 0;
		const char *r = blobmsg_get_string(tb[1]);
		for (k = 0; r[k] && j < sizeof(reason) - 1; k++)
			if (isprint((unsigned char)r[k]))
				reason[j++] = r[k];
		reason[j] = '\0';
	}
	if (!strcmp(mm_backend, "disabled") || !strcmp(mmcli_path, "none"))
		return mmr_reply(ctx, req, "refused", "ModemManager is not used on this router", 0);
	if (!mm_installed())
		return mmr_reply(ctx, req, "refused", "ModemManager is not installed", 0);
	if (mmr.running)
		return mmr_reply(ctx, req, "running", NULL, 0);
	if (mmr.started >= 0 && now - mmr.started < MM_RESTART_MIN_GAP_MS)
		return mmr_reply(ctx, req, "refused", "restarted less than 5 min ago",
				 (MM_RESTART_MIN_GAP_MS - (now - mmr.started)) / 1000 + 1);
	if (rj.active)
		return mmr_reply(ctx, req, "refused", "a recovery step is running", 0);
	pid = fork();
	if (pid < 0)
		return mmr_reply(ctx, req, "failed", "cannot start the init script", 0);
	if (pid == 0) {
		setpgid(0, 0);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			dup2(fd, 2);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execl("/bin/sh", "sh", "-c", mmr_script, "mm-restart", mm_init_path, mm_process, (char *)NULL);
		_exit(127);
	}
	procs_expect("modemmanager");
	mmr.running = true;
	mmr.started = now;
	mmr.at = (long long)time(NULL);
	mmr.proc.pid = pid;
	mmr.proc.cb = mmr_exited;
	uloop_process_add(&mmr.proc);
	mmr.kill.cb = mmr_kill;
	uloop_timeout_set(&mmr.kill, MM_RESTART_TIMEOUT_MS);
	snprintf(detail, sizeof(detail), "operator%s%s", *reason ? ": " : "", reason);
	ULOG_WARN("ModemManager restart requested by the %s\n", detail);
	mmr_journal("mm-restart", detail);
	return mmr_reply(ctx, req, "started", NULL, 0);
}

/* ---- automatic recovery (spec 22.1.4, 22.1.5, 23) ---- */

/*
 * After discovery passes, ModemManager reads, session changes and finished
 * recoveries the watch looks at every present modem (watch.c) and, when a
 * finding has held long enough, starts the lowest ladder level the gate
 * allows automatically: L0-L2 always; radio, modem reset and USB reset from a field-reported or bench-validated
 * profile, a driver rebind and the port's power only from a bench-validated one,
 * within the same loop protection as an operator request. One recovery at a
 * time on the router. A timer runs only while a finding waits for its
 * threshold. -A 0 (UCI core.auto_recovery 0) turns starting off; the health
 * view stays.
 */
static int auto_recovery = 1;
static int watch_override_ms;		/* -w: every threshold and grace, for tests */
/* watches: device_id -> watch state, declared above */
static struct uloop_timeout watch_timer;

static struct mm_watch_cfg watch_cfg(void)
{
	struct mm_watch_cfg c = MM_WATCH_DEFAULTS;

	if (watch_override_ms)
		c.session_ms = c.registration_ms = c.failed_ms = c.conflict_ms = c.grace_ms = c.internet_ms =
			c.search_ms = watch_override_ms;
	return c;
}

static json_object *watch_state(const char *id)
{
	json_object *st;

	if (!watches)
		watches = json_object_new_object();
	if (!(st = json_object_object_get(watches, id))) {
		st = json_object_new_object();
		json_object_object_add(watches, id, st);
	}
	return st;
}

static void watch_run(struct uloop_timeout *t)
{
	struct mm_watch_cfg cfg = watch_cfg();
	long long now = now_mono_ms(), next = -1;

	(void)t;
	if (!registry.devices)
		return;
	json_object_object_foreach(registry.devices, id, rec) {
		json_object *st, *srv, *step, *cand, *f;
		const char *reason;
		size_t i;

		if (!json_object_get_boolean(json_object_object_get(rec, "present"))) {
			if (watches)
				json_object_object_del(watches, id);
			continue;
		}
		if (frozen(id))
			continue;	/* nothing is repaired on a frozen modem */
		st = watch_state(id);
		srv = served(rec);
		step = mm_watch_step(srv, st, now, rec_busy(id), &cfg);
		json_object_put(srv);
		f = json_object_object_get(step, "finding");
		reason = jstr(f, "reason");
		cand = json_object_object_get(step, "candidates");
		size_t skipped = 0;

		if (auto_recovery && cand && json_object_array_length(cand) && mmr_quiet(now)) {
			/* ModemManager restarted by the operator: its modems are expected to be away a while */
			long long when = mmr.running ? now + 10000 : mmr.finished + MM_RESTART_QUIET_MS;
			if (next < 0 || when < next)
				next = when;
			json_object_put(step);
			continue;
		}
		for (i = 0; auto_recovery && !rj.active && cand && i < json_object_array_length(cand); i++) {
			int level = json_object_get_int(json_object_array_get_idx(cand, i));
			const char *result = NULL;
			char why[200];
			enum rec_outcome out = rec_start(rec, level, reason, true, &result, why, sizeof(why));

			if (out == REC_STARTED || out == REC_DONE) {
				ULOG_INFO("automatic recovery of %s: %s for %s (%s)\n", id, mm_recovery_level_name(level), reason,
					  jstr(f, "evidence") ? jstr(f, "evidence") : "");
				mm_watch_tried(st, level, now);
				if (out == REC_DONE)
					mm_watch_finished(st, now);
				break;
			}
			if (!strcmp(result, "refused")) {
				mm_watch_skip(st, level, why);	/* not allowed automatically: the next level may be */
				skipped++;
				continue;
			}
			/* busy or a stale MM view: look again shortly */
			if (next < 0 || now + 5000 < next)
				next = now + 5000;
			break;
		}
		if (skipped && skipped == json_object_array_length(cand)) {
			/* nothing on the ladder may run automatically: say so now (stalled) */
			json_object *again;

			srv = served(rec);
			again = mm_watch_step(srv, st, now, rec_busy(id), &cfg);
			json_object_put(again);
			json_object_put(srv);
		}
		if (json_object_get_int64(json_object_object_get(step, "next_ms")) > 0 &&
		    (next < 0 || json_object_get_int64(json_object_object_get(step, "next_ms")) < next))
			next = json_object_get_int64(json_object_object_get(step, "next_ms"));
		json_object_put(step);
	}
	if (next >= 0) {
		watch_timer.cb = watch_run;
		uloop_timeout_set(&watch_timer, next > now ? (int)(next - now) : 0);
	}
}

/* something changed: look once the current event is handled (several in one loop pass coalesce) */
static json_object *watch_view_of(const char *id)
{
	json_object *st = watches ? json_object_object_get(watches, id) : NULL;

	return mm_watch_view(st, now_mono_ms());
}

static void watch_sync(void)
{
	watch_timer.cb = watch_run;
	uloop_timeout_set(&watch_timer, 0);	/* right after this event; a pending threshold is re-derived */
}

/* ---- uplinks for the dashboard (spec 25.9.10) ----
 *
 * Every sample period the engine reads the counters of each connected modem's
 * network device (traffic, rates, day and month totals; never a modem poll)
 * and the main table's default route. Each path is checked through its own
 * device: a TCP connect bound to it (SO_BINDTODEVICE) to the reach targets
 * (UCI core.reach_target, default 1.1.1.1:443 then 8.8.8.8:443); a connection
 * proves the path, a refusal does not: under an operator's whitelist the DPI
 * refuses for every server outside it within milliseconds (bench 2026-10-27, a
 * Beeline T99: Telegram, Google, 1.1.1.1 all «connection refused» in 30 ms,
 * Yandex and VK open), and the path looked fine. When none answers, the restricted
 * targets are tried (UCI core.reach_restricted, default Russian sites that
 * stay reachable under an operator's whitelist; "none" turns them off): an
 * answer there makes the path "limited", which is no fault of the modem. A
 * verdict changes only after repeated results (mm_reach_judge); an unconfirmed
 * change is checked again after 10 s. The default path is checked every
 * minute, backups every five; a non-modem default (WAN) is checked too, so
 * the summary can say whether the router is online. -N <ms> sets all periods
 * (tests).
 */
#define UPLINK_SAMPLE_MS 5000
#define UPLINK_PRIMARY_MS 60000
#define UPLINK_BACKUP_MS 300000
#define UPLINK_CONNECT_MS 3000
/* a serial PPP link (ppp0, ppp-mm0: a 2G/3G stick dialled by pppd) wakes its radio first: on EDGE the first SYN takes
 * one to three seconds, and the 3 s budget read a working link as «no answer» (a tester's ZTE MF100 under r188, 2026-11-14:
 * 1110 ms on the card, «no answer through ppp0» in the log, the session restarted until it stayed down) */
/* the same for any modem ModemManager reports on 2G or 3G only, a modern one fallen back from LTE too (the owner,
 * 2026-11-14: «полезно и для других модемов»); LTE and 5G keep 3 s, a dead link is told as fast as before */
#define UPLINK_CONNECT_PPP_MS 10000
#define UPLINK_RECHECK_MS 10000
#define UPLINK_JOBS 8
#define UPLINK_SAVE_MS 60000
#define FAILOVER_PROBE_MS 30000
static bool failover_active(void);

static char default_dev[IFNAMSIZ + 1];
static struct uloop_timeout uplink_timer;
static json_object *mmuci_img;		/* the medvemodem package, re-read when the file changes */
static struct timespec mmuci_mtime;
static off_t mmuci_size;	/* two writes inside one tick of the file system's clock keep the mtime: the size tells */
static long long uplink_saved_ms;

struct reach_job {
	bool active;
	char key[128], dev[IFNAMSIZ + 1], target[80], world_target[80], world_error[64];
	int index, phase;	/* phase 1: the restricted targets */
	bool near;		/* a target of this check was answered by the modem itself */
	bool slow;		/* 2G/3G or a serial PPP link: the longer connect budget */
	long long start;
	struct uloop_fd fd;
	struct uloop_timeout timer;
};
static struct reach_job reach_jobs[UPLINK_JOBS];
static json_object *defaults, *restricted_defaults, *restricted_none;	/* reach targets without UCI */

json_object *medvemodem_uci(void)
{
	static const char *const pk[] = { "medvemodem", NULL };
	char path[300], err[160];
	struct stat st;

	snprintf(path, sizeof(path), "%s/medvemodem", conf_dir);
	if (stat(path, &st)) {
		json_object_put(mmuci_img);
		mmuci_img = NULL;
		return NULL;
	}
	if (!mmuci_img || st.st_mtim.tv_sec != mmuci_mtime.tv_sec || st.st_mtim.tv_nsec != mmuci_mtime.tv_nsec || st.st_size != mmuci_size) {
		json_object *all = mm_uci_read(conf_dir, pk, err, sizeof(err));
		json_object_put(mmuci_img);
		mmuci_img = all ? json_object_get(json_object_object_get(all, "medvemodem")) : NULL;
		json_object_put(all);
		mmuci_mtime = st.st_mtim;
		mmuci_size = st.st_size;
	}
	return mmuci_img;
}

/* the operator's name of a modem: config modem / option device_id / option name */
static const char *modem_name(const char *id)
{
	json_object *img = medvemodem_uci();

	if (!img || !id)
		return NULL;
	json_object_object_foreach(img, sec, s) {
		(void)sec;
		if (!strcmp(jstr(s, ".type") ? jstr(s, ".type") : "", "modem") && jstr(s, "device_id") &&
		    !strcmp(jstr(s, "device_id"), id) && jstr(s, "name") && *jstr(s, "name"))
			return jstr(s, "name");
	}
	return NULL;
}

/*
 * What a short text names a modem by (an SMS forwarded by SMS, an alert): the operator's name, else the model
 * ModemManager or the stick's web gave, else the USB product, else «USB 1.2» for a port id. Field 2026-11-06: an
 * unnamed HiLink forwarded «↪ usb-port:platform/fcc00000.usb/xhci-hcd.auto@1.2 · T-Mob: Чтобы поль…», the raw
 * id ate the 70 characters of the SMS.
 */
const char *daemon_modem_label(const char *id, char *buf, size_t size)
{
	const char *n = modem_name(id), *at;
	json_object *rec;

	if (!id)
		return NULL;
	if (n && *n)
		return n;
	if (known_models && jstr(json_object_object_get(known_models, id), "model") && *jstr(json_object_object_get(known_models, id), "model"))
		return jstr(json_object_object_get(known_models, id), "model");
	if ((n = web_model(id)) && *n)
		return n;
	if ((rec = mm_registry_find(&registry, id)) && (n = jstr(rec, "product")) && *n)
		return n;
	if (!strncmp(id, "usb-port:", 9) && (at = strrchr(id, '@')) && at[1]) {
		snprintf(buf, size, "USB %s", at + 1);
		return buf;
	}
	return id;
}

/*
 * The IMEI key (HANDOFF 167): a modem's name and settings (config modem, device, esim) are found by its
 * device_id, which is the USB port for a modem with no serial of its own or with a serial its clone shares.
 * After each read of ModemManager they follow the modem by a salted hash of its IMEI: into the port it
 * moved to, or parked while another modem sits where it was. The IMEI is never written down or logged.
 */
static char id_salt[80];
static int id_salt_state;		/* 0 not read yet, 1 read, -1 no randomness */
static json_object *web_id_keys;	/* device id -> {generation, key}: a router-mode stick's salted IMEI key from its web */
static json_object *id_shared;		/* IMEI keys seen on two modems at once (<state>/imei-shared.json): no follow by them */

static json_object *id_shared_keys(void)
{
	char path[PATH_MAX];

	if (!id_shared) {
		snprintf(path, sizeof(path), "%s/imei-shared.json", state_dir);
		id_shared = json_object_from_file(path);
		if (!json_object_is_type(id_shared, json_type_object)) {
			json_object_put(id_shared);
			id_shared = json_object_new_object();
		}
	}
	return id_shared;
}

static void id_shared_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];

	snprintf(path, sizeof(path), "%s/imei-shared.json", state_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	if (json_object_to_file_ext(tmp, id_shared, JSON_C_TO_STRING_PLAIN) || rename(tmp, path))
		ULOG_WARN("the IMEI keys shared by two modems could not be saved\n");
}

static bool id_salt_ready(void)
{
	char path[PATH_MAX];

	if (!id_salt_state) {
		bool kept = false;

		snprintf(path, sizeof(path), "%s/id-salt", state_dir);
		/* a salt the next start would not have makes keys that point nowhere then: none is better (review 2026-11-15) */
		id_salt_state = mm_salt_load(path, id_salt, sizeof(id_salt), &kept) && kept ? 1 : -1;
		if (id_salt_state < 0)
			ULOG_WARN("no IMEI key salt (no randomness, or it could not be saved): names follow the USB port only\n");
	}
	return id_salt_state > 0;
}

/* the IMEI a HiLink's web gave (the owner, 2026-11-01: «привязка HiLink по IMEI как доп — да»): only its salted key
 * stays, for this attachment (generation) of the stick */
void identity_web_imei(const char *id, long long generation, const char *imei)
{
	char key[MM_IMEI_KEY_SIZE];
	json_object *e;

	if (!id || !mm_valid_imei(imei) || !id_salt_ready())
		return;
	mm_imei_key(id_salt, imei, key);
	if (!web_id_keys)
		web_id_keys = json_object_new_object();
	e = json_object_new_object();
	json_object_object_add(e, "generation", json_object_new_int64(generation));
	json_object_object_add(e, "key", json_object_new_string(key));
	json_object_object_add(web_id_keys, id, e);
	explicit_bzero(key, sizeof(key));
}

/* the web's key of a present record of this generation, or NULL */
static const char *web_id_key(const char *id, json_object *rec)
{
	json_object *e = web_id_keys ? json_object_object_get(web_id_keys, id) : NULL;

	return e && json_object_get_int64(json_object_object_get(e, "generation")) ==
		    json_object_get_int64(json_object_object_get(rec, "generation")) ? jstr(e, "key") : NULL;
}
static char id_follow_err[160];		/* said once until it changes: the next read tries again */

/*
 * A freeze follows its modem like the names (HANDOFF 200 item 9: the UFI, port-identified, moved ports on a replug
 * and came back unfrozen under another id). The freeze keeps the modem's identity key (salted, never the IMEI) once
 * it is seen; the modem in another port takes the freeze along, and another modem in the frozen port is not frozen
 * by it (the entry is parked under its key until the modem comes back).
 */
static void frozen_follow(json_object *present)
{
	json_object *moves;
	bool changed = false;
	size_t i;

	if (!frozen_map)
		return;
	moves = json_object_new_array();
	json_object_object_foreach(frozen_map, fid, fv) {
		const char *key = jstr(fv, "key"), *here = jstr(present, fid), *holder = NULL;
		bool parked = !strncmp(fid, "parked:", 7);
		json_object *mv;
		char to[80];
		int n = 0;

		if (!key) {
			if (here && !parked) {
				json_object_object_add(fv, "key", json_object_new_string(here));
				changed = true;
			}
			continue;
		}
		json_object_object_foreach(present, pid, pk) {
			if (strcmp(json_object_get_string(pk), key))
				continue;
			holder = pid;
			n++;
		}
		if (n == 1 && strcmp(holder, fid))
			snprintf(to, sizeof(to), "%s", holder);
		else if (!n && here && !parked)
			snprintf(to, sizeof(to), "parked:%s", key);
		else
			continue;
		mv = json_object_new_array();
		json_object_array_add(mv, json_object_new_string(fid));
		json_object_array_add(mv, json_object_new_string(to));
		json_object_array_add(moves, mv);
	}
	for (i = 0; i < json_object_array_length(moves); i++) {
		json_object *mv = json_object_array_get_idx(moves, i);
		const char *from = json_object_get_string(json_object_array_get_idx(mv, 0));
		const char *to = json_object_get_string(json_object_array_get_idx(mv, 1));
		json_object *entry = json_object_get(json_object_object_get(frozen_map, from));
		bool park = !strncmp(to, "parked:", 7);

		json_object_object_del(frozen_map, from);
		if (frozen_withdrawn)
			json_object_object_del(frozen_withdrawn, from);
		if (!frozen(to))
			json_object_object_add(frozen_map, to, entry);
		else
			json_object_put(entry);
		changed = true;
		if (park) {
			ULOG_INFO("the freeze of %s parked: another modem is in its port\n", from);
			mm_history_add(&history, from, (long long)time(NULL), now_mono_ms(), "settings-follow", "freeze: parked, another modem is in this port");
		} else {
			char detail[200];

			snprintf(detail, sizeof(detail), "freeze: from %.160s", strncmp(from, "parked:", 7) ? from : "parked");
			ULOG_INFO("the freeze followed its modem to %s (from %s)\n", to, strncmp(from, "parked:", 7) ? from : "parked");
			mm_history_add(&history, to, (long long)time(NULL), now_mono_ms(), "settings-follow", detail);
		}
		history_dirty = true;
	}
	json_object_put(moves);
	if (!changed)
		return;
	frozen_save();
	inhibits_sync();
	frozen_list_sync();
}

/*
 * A modem that only its port named (no USB serial) gets a new id in another port. When its IMEI (or a router-mode
 * stick's USB identity) proves it is the one that left `from`, its name and settings move (mm_identity_follow), and
 * the old record goes too: the dashboard showed it twice, the absent one asking for a fix (the owner, 2026-10-31:
 * «переткнул ep-06 из хаба … напрямую в порт r3s — он размножился»).
 */
static void forget_moved(const char *from, const char *to)
{
	json_object *rec;

	if (!from || !to || !strncmp(from, "parked:", 7) || !strcmp(from, to) || !registry.devices ||
	    !(rec = json_object_object_get(registry.devices, from)) || json_object_get_boolean(json_object_object_get(rec, "present")))
		return;
	ULOG_INFO("%s moved to %s: its old record is dropped\n", from, to);
	json_object_object_del(registry.devices, from);
}

/*
 * The SIM keys of the modems present ({device_id: key}) and every present id: the modem's IMEI, else its USB identity,
 * with its SIM's IMSI, salted (the owner, 2026-11-05: «пусть будет соль»). The IMSI is ModemManager's card's, or the
 * engine's reading of this very attachment; neither is written down, only the key.
 */
static json_object *sim_keys_now(json_object **ids)
{
	json_object *sims = json_object_new_object();

	*ids = json_object_new_object();
	if (!registry.devices)
		return sims;
	json_object_object_foreach(registry.devices, rid, rec) {
		json_object *mm = mm_devices ? json_object_object_get(mm_devices, rid) : NULL, *e;
		const char *imsi = NULL;
		char who[260], key[MM_IMEI_KEY_SIZE];

		if (!json_object_get_boolean(json_object_object_get(rec, "present")))
			continue;
		json_object_object_add(*ids, rid, json_object_new_boolean(1));
		if (mm)
			imsi = jstr(sim_secret ? json_object_object_get(sim_secret, rid) : NULL, "imsi");
		else if ((e = sim_entry(rid)) && json_object_get_int64(json_object_object_get(e, "generation")) ==
						    json_object_get_int64(json_object_object_get(rec, "generation")))
			imsi = jstr(e, "imsi");
		if (!imsi || strlen(imsi) < 14 || strspn(imsi, "0123456789") != strlen(imsi))
			continue;
		if (mm && mm_valid_imei(jstr(mm, "imei")))
			snprintf(who, sizeof(who), "sim|imei:%s|%s", jstr(mm, "imei"), imsi);
		else if (jstr(rec, "vidpid"))
			snprintf(who, sizeof(who), "sim|usb:%s:%.60s:%.60s|%s", jstr(rec, "vidpid"),
				 jstr(rec, "manufacturer") ? jstr(rec, "manufacturer") : "", jstr(rec, "product") ? jstr(rec, "product") : "", imsi);
		else
			continue;
		mm_imei_key(id_salt, who, key);
		explicit_bzero(who, sizeof(who));
		json_object_object_add(sims, rid, json_object_new_string(key));
	}
	return sims;
}

static void identity_sync(void)
{
	json_object *present, *ops, *cur, *checked, *apply, *img = medvemodem_uci(), *rekey;
	char key[MM_IMEI_KEY_SIZE], err[160];
	size_t i;

	if (!img || !id_salt_ready())
		return;
	present = json_object_new_object();
	rekey = json_object_new_array();
	if (mm_devices) {
		json_object_object_foreach(mm_devices, id, m) {
			if (!mm_valid_imei(jstr(m, "imei")))
				continue;
			mm_imei_key(id_salt, jstr(m, "imei"), key);
			json_object_object_add(present, id, json_object_new_string(key));
		}
	}
	/*
	 * A modem in router mode (HiLink, UFI) is never ModemManager's: no IMEI to follow, and with no serial of its own
	 * its name stayed in the old port (the owner, 2026-10-20: a UFI moved to another port lost «Chinese LTE dongle»).
	 * Its USB identity (VID:PID, manufacturer, product) keys it instead while no other present device shares it;
	 * a second identical one makes it no key at all, so a name never jumps to the wrong stick.
	 */
	if (registry.devices) {
		json_object_object_foreach(registry.devices, rid, rec) {
			const char *prefer = jstr(json_object_object_get(rec, "profile"), "prefer_owner"), *strength = jstr(rec, "identity_strength");
			char tuple[200];
			int same = 0;

			const char *wk = web_id_key(rid, rec);

			if (!json_object_get_boolean(json_object_object_get(rec, "present")) || !prefer || strcmp(prefer, "hilink") ||
			    (strength && !strcmp(strength, "serial")) || !jstr(rec, "vidpid") || json_object_object_get(present, rid))
				continue;
			snprintf(tuple, sizeof(tuple), "usb:%s:%s:%s", jstr(rec, "vidpid"), jstr(rec, "manufacturer") ? jstr(rec, "manufacturer") : "",
				 jstr(rec, "product") ? jstr(rec, "product") : "");
			/* its web gave its IMEI: that keys it, two identical sticks too. A name first keyed by the USB identity in
			 * this port is keyed again by the IMEI (and a freeze with it), so nothing is parked on the way */
			if (wk) {
				json_object *ro, *fz = frozen_map ? json_object_object_get(frozen_map, rid) : NULL;
				size_t r;

				mm_imei_key(id_salt, tuple, key);
				ro = mm_identity_rekey(img, rid, key, wk);
				for (r = 0; r < json_object_array_length(ro); r++)
					json_object_array_add(rekey, json_object_get(json_object_array_get_idx(ro, r)));
				json_object_put(ro);
				if (fz && jstr(fz, "key") && !strcmp(jstr(fz, "key"), key)) {
					json_object_object_add(fz, "key", json_object_new_string(wk));
					frozen_save();
				}
				json_object_object_add(present, rid, json_object_new_string(wk));
				continue;
			}
			json_object_object_foreach(registry.devices, oid, other) {
				(void)oid;
				if (json_object_get_boolean(json_object_object_get(other, "present")) && jstr(other, "vidpid") &&
				    !strcmp(jstr(other, "vidpid"), jstr(rec, "vidpid")) &&
				    !strcmp(jstr(other, "manufacturer") ? jstr(other, "manufacturer") : "", jstr(rec, "manufacturer") ? jstr(rec, "manufacturer") : "") &&
				    !strcmp(jstr(other, "product") ? jstr(other, "product") : "", jstr(rec, "product") ? jstr(rec, "product") : ""))
					same++;
			}
			if (same != 1)
				continue;
			mm_imei_key(id_salt, tuple, key);
			/* keyed by its IMEI already: the USB identity says nothing until its web is read (a name parked on every
			 * start in its own port otherwise) */
			if (mm_identity_keyed_other(img, rid, key))
				continue;
			json_object_object_add(present, rid, json_object_new_string(key));
		}
	}
	/* one IMEI on two modems (written into both by their owner): neither follows by it, now or later */
	if (mm_identity_shared(present, id_shared_keys())) {
		ULOG_WARN("two modems report the same IMEI: their names and settings stay with their USB ports\n");
		id_shared_save();
	}
	/* the keys change first; the follow runs on the next pass, on the image they made */
	if (json_object_array_length(rekey)) {
		json_object_put(present);
		cur = json_object_new_object();
		json_object_object_add(cur, "medvemodem", json_object_get(img));
		checked = mm_cfg_check(cur, rekey);
		apply = json_object_object_get(checked, "apply");
		if (apply && json_object_array_length(apply) && !mm_uci_apply(conf_dir, cfg.savedir, apply, err, sizeof(err)))
			ULOG_INFO("names and settings keyed by the modem's IMEI from its web now (%zu)\n", json_object_array_length(apply));
		json_object_put(checked);
		json_object_put(cur);
		json_object_put(rekey);
		return;
	}
	json_object_put(rekey);
	frozen_follow(present);
	ops = mm_identity_follow(img, present);
	json_object_put(present);
	/* nothing to move by the IMEI: the SIM key (its IMEI or USB identity with its SIM) moves what is left */
	if (!json_object_array_length(ops)) {
		json_object *ids, *sims = sim_keys_now(&ids);

		json_object_put(ops);
		ops = mm_identity_follow_sim(img, sims, ids);
		json_object_put(sims);
		json_object_put(ids);
	}
	if (!json_object_array_length(ops)) {
		json_object_put(ops);
		return;
	}
	cur = json_object_new_object();
	json_object_object_add(cur, "medvemodem", json_object_get(img));
	checked = mm_cfg_check(cur, ops);
	apply = json_object_object_get(checked, "apply");
	if (apply && json_object_array_length(apply) && mm_uci_apply(conf_dir, cfg.savedir, apply, err, sizeof(err))) {
		if (strcmp(err, id_follow_err))
			ULOG_ERR("names and settings could not follow their modem: %s\n", err);
		snprintf(id_follow_err, sizeof(id_follow_err), "%s", err);
	} else {
		id_follow_err[0] = '\0';
		for (i = 0; apply && i < json_object_array_length(apply); i++) {
			json_object *op = json_object_array_get_idx(apply, i), *s;
			const char *to = jstr(op, "value"), *from = jstr(op, "expect"), *type;
			char detail[200];

			if (!strcmp(jstr(op, "option"), "imei_key") || !strcmp(jstr(op, "option"), "sim_key"))
				continue;	/* a key added: nothing moved */
			s = json_object_object_get(img, jstr(op, "section"));
			type = jstr(s, ".type") ? jstr(s, ".type") : "?";
			if (!strncmp(to, "parked:", 7)) {
				snprintf(detail, sizeof(detail), "%s: parked, another modem is in this port", type);
				ULOG_INFO("%s settings of %s parked: another modem is in its port\n", type, from);
				mm_history_add(&history, from, (long long)time(NULL), now_mono_ms(), "settings-follow", detail);
			} else {
				snprintf(detail, sizeof(detail), "%s: from %.160s", type, strncmp(from, "parked:", 7) ? from : "parked");
				ULOG_INFO("%s settings followed their modem to %s (from %s)\n", type, to,
					  strncmp(from, "parked:", 7) ? from : "parked");
				mm_history_add(&history, to, (long long)time(NULL), now_mono_ms(), "settings-follow", detail);
				forget_moved(from, to);
			}
			history_dirty = true;
		}
	}
	json_object_put(checked);
	json_object_put(cur);
	json_object_put(ops);
}

/* what keeps a modem's name and settings with it: its own USB serial, its IMEI key, or only the port */
static const char *name_follows(const char *id, json_object *rec)
{
	json_object *mm = mm_devices && id ? json_object_object_get(mm_devices, id) : NULL;

	if (jstr(rec, "identity_strength") && !strcmp(jstr(rec, "identity_strength"), "serial"))
		return "serial";
	if (id_salt_state > 0 && mm_valid_imei(jstr(mm, "imei"))) {
		char key[MM_IMEI_KEY_SIZE];

		mm_imei_key(id_salt, jstr(mm, "imei"), key);
		return json_object_object_get(id_shared_keys(), key) ? "port" : "imei";
	}
	return "port";
}

/* the operator's label of a SIM: config sim / option iccid / option number, option description */
static json_object *sim_label(const char *iccid)
{
	json_object *img = medvemodem_uci(), *o;

	if (!img || !iccid || !*iccid)
		return NULL;
	json_object_object_foreach(img, sec, s) {
		(void)sec;
		if (strcmp(jstr(s, ".type") ? jstr(s, ".type") : "", "sim") || !jstr(s, "iccid") || strcmp(jstr(s, "iccid"), iccid))
			continue;
		if (!(jstr(s, "number") && *jstr(s, "number")) && !(jstr(s, "description") && *jstr(s, "description")) &&
		    !(jstr(s, "sms_center") && *jstr(s, "sms_center")))
			return NULL;
		o = json_object_new_object();
		if (jstr(s, "number") && *jstr(s, "number"))
			json_object_object_add(o, "number", json_object_new_string(jstr(s, "number")));
		if (jstr(s, "description") && *jstr(s, "description"))
			json_object_object_add(o, "description", json_object_new_string(jstr(s, "description")));
		if (jstr(s, "sms_center") && *jstr(s, "sms_center"))
			json_object_object_add(o, "sms_center", json_object_new_string(jstr(s, "sms_center")));
		return o;
	}
	return NULL;
}

/*
 * The SMS centre the operator gave a SIM (config sim / option sms_center, by
 * its ICCID): every message sent from it carries that centre — in the PDU over
 * AT, `smsc=` to ModemManager. NULL: the one written on the SIM.
 */
const char *sms_center_of(const char *id)
{
	static char buf[24];
	json_object *mm = mm_devices && id ? json_object_object_get(mm_devices, id) : NULL;
	json_object *lab = sim_label(sim_iccid_of(id, mm));
	const char *v = jstr(lab, "sms_center");

	buf[0] = '\0';
	if (v && mm_sms_number_ok(v))
		snprintf(buf, sizeof(buf), "%s", v);
	json_object_put(lab);
	return buf[0] ? buf : NULL;
}

static json_object *reach_targets(void)
{
	json_object *core = json_object_object_get(medvemodem_uci(), "core");
	json_object *t = core ? json_object_object_get(core, "reach_target") : NULL;

	if (t && json_object_is_type(t, json_type_array) && json_object_array_length(t))
		return t;
	if (!defaults) {
		defaults = json_object_new_array();
		json_object_array_add(defaults, json_object_new_string("1.1.1.1:443"));
		json_object_array_add(defaults, json_object_new_string("8.8.8.8:443"));
	}
	return defaults;
}

static json_object *reach_restricted(void)
{
	json_object *core = json_object_object_get(medvemodem_uci(), "core");
	json_object *t = core ? json_object_object_get(core, "reach_restricted") : NULL;
	size_t i;

	if (!restricted_none)
		restricted_none = json_object_new_array();
	if (t && json_object_is_type(t, json_type_array) && json_object_array_length(t)) {
		for (i = 0; i < json_object_array_length(t); i++)
			if (!strcmp(json_object_get_string(json_object_array_get_idx(t, i)), "none"))
				return restricted_none;
		return t;
	}
	if (!restricted_defaults) {
		restricted_defaults = json_object_new_array();
		json_object_array_add(restricted_defaults, json_object_new_string("77.88.55.242:443"));	/* ya.ru */
		json_object_array_add(restricted_defaults, json_object_new_string("87.240.132.72:443"));	/* vk.com */
	}
	return restricted_defaults;
}

static json_object *reach_list(struct reach_job *j)
{
	return j->phase ? reach_restricted() : reach_targets();
}

static int uplink_period(int normal)
{
	return uplink_override_ms ? uplink_override_ms : normal;
}

static json_object *uplink_state(const char *key)
{
	json_object *st;

	if (!uplinks)
		uplinks = json_object_new_object();
	if (!(st = json_object_object_get(uplinks, key))) {
		st = json_object_new_object();
		json_object_object_add(uplinks, key, st);
	}
	return st;
}

/* "1.1.1.1:443" or "[2606:4700::1111]:443" */
static bool parse_target(const char *t, struct sockaddr_storage *sa, socklen_t *len)
{
	char host[64];
	const char *colon;
	long port;
	size_t n;

	memset(sa, 0, sizeof(*sa));
	if (*t == '[') {
		const char *end = strchr(t, ']');
		if (!end || end[1] != ':' || (n = (size_t)(end - t - 1)) >= sizeof(host))
			return false;
		memcpy(host, t + 1, n);
		host[n] = 0;
		colon = end + 1;
	} else {
		if (!(colon = strrchr(t, ':')) || (n = (size_t)(colon - t)) >= sizeof(host))
			return false;
		memcpy(host, t, n);
		host[n] = 0;
	}
	port = strtol(colon + 1, NULL, 10);
	if (port <= 0 || port > 65535)
		return false;
	if (inet_pton(AF_INET, host, &((struct sockaddr_in *)sa)->sin_addr) == 1) {
		((struct sockaddr_in *)sa)->sin_family = AF_INET;
		((struct sockaddr_in *)sa)->sin_port = htons((uint16_t)port);
		*len = sizeof(struct sockaddr_in);
		return true;
	}
	if (inet_pton(AF_INET6, host, &((struct sockaddr_in6 *)sa)->sin6_addr) == 1) {
		((struct sockaddr_in6 *)sa)->sin6_family = AF_INET6;
		((struct sockaddr_in6 *)sa)->sin6_port = htons((uint16_t)port);
		*len = sizeof(struct sockaddr_in6);
		return true;
	}
	return false;
}

static void reach_close(struct reach_job *j)
{
	if (j->fd.registered)
		uloop_fd_delete(&j->fd);
	if (j->fd.fd >= 0)
		close(j->fd.fd);
	j->fd.fd = -1;
	uloop_timeout_cancel(&j->timer);
}

/*
 * R24: a modem path that answers and whose interface is in a firewall zone
 * gets one look at fw4's compiled rules per device (the reload hook's
 * fw-check verb); a miss reloads the firewall, written to the history.
 */
/*
 * R34: the carrying path that answers by IP also gets the router's resolver
 * asked (UCI core.dns_server, default 127.0.0.1:53; "none" turns it off) for
 * core.dns_name (default ya.ru, reachable under whitelists). Two failures in a
 * row make st.dns.ok false: sites will not open although the link works.
 */
#define DNS_TIMEOUT_MS 3000
struct dns_job {
	bool active;
	char key[128];
	unsigned int id;
	struct uloop_fd fd;
	struct uloop_timeout timer;
};
static struct dns_job dns_job;

const char *core_opt(const char *name, const char *dflt)
{
	json_object *core = json_object_object_get(medvemodem_uci(), "core");
	const char *v = jstr(core, name);

	return v && *v ? v : dflt;
}

static void dns_finish(bool ok, const char *error)
{
	struct dns_job *j = &dns_job;
	json_object *st = uplinks ? json_object_object_get(uplinks, j->key) : NULL, *d, *was;
	int fails;

	if (j->fd.registered)
		uloop_fd_delete(&j->fd);
	if (j->fd.fd >= 0)
		close(j->fd.fd);
	j->fd.fd = -1;
	uloop_timeout_cancel(&j->timer);
	j->active = false;
	if (!st)
		return;
	was = json_object_object_get(st, "dns");
	fails = ok ? 0 : (int)json_object_get_int(json_object_object_get(was, "fails")) + 1;
	d = json_object_new_object();
	/* one lost datagram is no verdict: false only after two failures in a row */
	json_object_object_add(d, "ok", json_object_new_boolean(ok || (fails < 2 && (!was ||
		json_object_get_boolean(json_object_object_get(was, "ok"))))));
	json_object_object_add(d, "name", json_object_new_string(core_opt("dns_name", "ya.ru")));
	if (!ok)
		json_object_object_add(d, "error", json_object_new_string(error));
	json_object_object_add(d, "fails", json_object_new_int(fails));
	json_object_object_add(d, "at", json_object_new_int64((long long)time(NULL)));
	if ((!was || json_object_get_boolean(json_object_object_get(was, "ok"))) &&
	    !json_object_get_boolean(json_object_object_get(d, "ok")))
		ULOG_WARN("the router's resolver does not answer for %s: %s\n", core_opt("dns_name", "ya.ru"), error);
	json_object_object_add(st, "dns", d);
}

static void dns_readable(struct uloop_fd *fd, unsigned int events)
{
	unsigned char buf[512];
	ssize_t n;
	int rcode = 0, r;
	char why[48];

	(void)events;
	n = recv(fd->fd, buf, sizeof(buf), 0);
	if (n < 0) {
		dns_finish(false, strerror(errno));
		return;
	}
	r = mm_dns_answer(buf, (size_t)n, dns_job.id, &rcode);
	if (r < 0)
		return;		/* not ours: keep waiting */
	snprintf(why, sizeof(why), rcode == 3 ? "no such name" : rcode == 5 ? "refused" : rcode == 2 ? "server failure" :
		 rcode ? "error code %d" : "no address in the answer", rcode);
	dns_finish(r == 1, why);
}

static void dns_timeout(struct uloop_timeout *t)
{
	(void)t;
	dns_finish(false, "no answer");
}

static void dns_start(const char *key)
{
	const char *server = core_opt("dns_server", "127.0.0.1:53");
	struct sockaddr_storage sa;
	socklen_t len;
	unsigned char q[300];
	int n, fd;

	if (dns_job.active || !strcmp(server, "none") || !parse_target(server, &sa, &len))
		return;
	memset(&dns_job, 0, sizeof(dns_job));
	snprintf(dns_job.key, sizeof(dns_job.key), "%s", key);
	dns_job.id = (unsigned int)(now_mono_ms() & 0xffff);
	n = mm_dns_query(q, sizeof(q), core_opt("dns_name", "ya.ru"), dns_job.id);
	fd = n ? socket(sa.ss_family, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0) : -1;
	if (fd < 0)
		return;
	dns_job.active = true;
	dns_job.fd.fd = fd;
	dns_job.timer.cb = dns_timeout;
	if (sendto(fd, q, (size_t)n, 0, (struct sockaddr *)&sa, len) < 0) {
		dns_finish(false, strerror(errno));
		return;
	}
	dns_job.fd.cb = dns_readable;
	uloop_fd_add(&dns_job.fd, ULOOP_READ);
	uloop_timeout_set(&dns_job.timer, DNS_TIMEOUT_MS);
}

struct fw_job {
	bool active;
	char id[128], dev[IFNAMSIZ + 1];
	struct uloop_process proc;
	struct uloop_timeout kill;	/* a hook that never ends must not hold every later check (review 2026-11-15) */
};
static struct fw_job fw_job;
#define CHILD_KILL_MS (300 * 1000)

static void fw_kill_fired(struct uloop_timeout *t)
{
	struct fw_job *j = container_of(t, struct fw_job, kill);

	if (j->active && j->proc.pid > 0) {
		ULOG_WARN("the firewall check of %s did not end in %d s: stopped\n", j->dev, CHILD_KILL_MS / 1000);
		kill(j->proc.pid, SIGKILL);
	}
}

static void fw_exited(struct uloop_process *p, int ret)
{
	struct fw_job *j = container_of(p, struct fw_job, proc);

	j->active = false;
	uloop_timeout_cancel(&j->kill);
	if (WIFEXITED(ret) && WEXITSTATUS(ret) == 3) {
		char detail[96];

		ULOG_WARN("firewall rules did not name %s of %s: firewall reloaded\n", j->dev, j->id);
		snprintf(detail, sizeof(detail), "firewall reloaded: its rules missed %s", j->dev);
		mm_history_add(&history, j->id, (long long)time(NULL), now_mono_ms(), "config", detail);
		history_dirty = true;
	}
}

static void fw_check(const char *id, json_object *st, const char *dev)
{
	json_object *checks = iface_checks ? json_object_object_get(iface_checks, id) : NULL;
	bool zoned = false;
	size_t i;
	pid_t pid;

	if (fw_job.active || !strcmp(reload_hook, "none") ||
	    (jstr(st, "fw_dev") && !strcmp(jstr(st, "fw_dev"), dev)))
		return;
	for (i = 0; checks && json_object_is_type(checks, json_type_array) && i < json_object_array_length(checks); i++)
		zoned = zoned || jstr(json_object_array_get_idx(checks, i), "zone");
	if (!zoned)
		return;
	json_object_object_add(st, "fw_dev", json_object_new_string(dev));
	pid = fork();
	if (pid < 0)
		return;
	if (pid == 0) {
		int fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execl(reload_hook, reload_hook, "fw-check", dev, (char *)NULL);
		_exit(127);
	}
	snprintf(fw_job.id, sizeof(fw_job.id), "%s", id);
	snprintf(fw_job.dev, sizeof(fw_job.dev), "%s", dev);
	fw_job.active = true;
	fw_job.proc.pid = pid;
	fw_job.proc.cb = fw_exited;
	uloop_process_add(&fw_job.proc);
	fw_job.kill.cb = fw_kill_fired;
	uloop_timeout_set(&fw_job.kill, CHILD_KILL_MS);
}

/* the first verdict after a recovery that «succeeded»: a session without the internet is said, not called restored */
static void reach_after_recovery(const char *id, json_object *st, const char *level, long long now)
{
	long long at = json_object_get_int64(json_object_object_get(st, "after_recovery"));
	json_object *last = json_object_object_get(rec_state(id), "last_steps");

	if (!strcmp(level, "down")) {
		ULOG_WARN("recovery of %s brought the session up, but the internet is still down\n", id);
		mm_history_add(&history, id, (long long)time(NULL), now, "recovery-no-internet", NULL);
		history_dirty = true;
		if (last)
			json_object_object_add(last, "internet", json_object_new_boolean(false));
	} else if (strcmp(level, "ok") && strcmp(level, "limited") && now - at < 10 * 60 * 1000LL) {
		return;		/* not judged yet */
	} else if (last && strcmp(level, "ok") == 0) {
		json_object_object_add(last, "internet", json_object_new_boolean(true));
	}
	json_object_object_del(st, "after_recovery");
}

void reach_redirect_found(const char *key, const char *host)
{
	json_object *st = uplinks ? json_object_object_get(uplinks, key) : NULL, *reach = json_object_object_get(st, "reach");

	if (reach && !json_object_get_boolean(json_object_object_get(reach, "ok"))) {
		json_object_object_add(reach, "redirect", json_object_new_string(host));
		uplink_saved_ms = 0;
	}
}

static void reach_done(struct reach_job *j, bool ok, const char *error)
{
	json_object *st = uplink_state(j->key), *r = json_object_new_object();
	long long now = now_mono_ms();
	bool primary = !strcmp(jstr(st, "dev") ? jstr(st, "dev") : "", default_dev);
	bool limited = ok && j->phase, recheck = false;

	reach_close(j);
	json_object_object_add(r, "ok", json_object_new_boolean(ok && !limited));
	if (limited)
		json_object_object_add(r, "limited", json_object_new_boolean(true));
	json_object_object_add(r, "device", json_object_new_string(j->dev));
	/* a failure names the world target that failed last, not the restricted ones */
	if (!ok && j->phase && *j->world_target)
		json_object_object_add(r, "target", json_object_new_string(j->world_target));
	else if (*j->target)
		json_object_object_add(r, "target", json_object_new_string(j->target));
	if (ok)
		json_object_object_add(r, "latency_ms", json_object_new_int64(now - j->start));
	if (!ok || limited)
		json_object_object_add(r, "error", json_object_new_string(j->phase && *j->world_error ? j->world_error :
									 error ? error : "no answer"));
	json_object_object_add(r, "at", json_object_new_int64((long long)time(NULL)));
	json_object_object_add(r, "mono", json_object_new_int64(now));
	/* a result for the device the path has now (a session may have moved meanwhile) */
	if (jstr(st, "dev") && !strcmp(jstr(st, "dev"), j->dev)) {
		json_object *prev = json_object_object_get(st, "reach"), *v = mm_reach_judge(prev, r, &recheck);
		const char *was = prev ? jstr(prev, "level") : NULL, *level = jstr(v, "level");

		if (prev ? strcmp(was ? was : "", level) : strcmp(level, "ok"))
			ULOG_INFO("reachability through %s: %s%s\n", j->dev, !strcmp(level, "ok") ? "ok via " :
				  !strcmp(level, "limited") ? "limited (White Lists), only " : "failed: ",
				  strcmp(level, "down") ? j->target : error ? error : "no answer");
		/* the internet through the modem as it changes (the owner, 2026-10-28: when the whitelists came, when the
		 * internet went), in its journal; the first verdict only when it is not the whole internet */
		if (strncmp(j->key, "dev:", 4) && (!strcmp(level, "ok") || !strcmp(level, "limited") || !strcmp(level, "down")) &&
		    (was ? strcmp(was, level) : strcmp(level, "ok"))) {
			mm_history_add(&history, j->key, (long long)time(NULL), now, "internet", level);
			history_dirty = true;
		}
		/* an operator's page (r156, a tester's Tele2 SIM with no money): the TLS hello cut or answered with HTTP
		 * asks one plain HTTP request on a change of the verdict; its host stays with the check until it is ok */
		if (strncmp(j->key, "dev:", 4) && strcmp(level, "ok")) {
			const char *e = jstr(r, "error");

			/* once a bad episode: «asked» rides with the check until it is ok again (a verdict taken over from the
			 * device's own check before ModemManager named the modem counts as no change) */
			if (prev && jstr(prev, "redirect"))
				json_object_object_add(v, "redirect", json_object_new_string(jstr(prev, "redirect")));
			if (prev && json_object_get_boolean(json_object_object_get(prev, "redirect_asked")))
				json_object_object_add(v, "redirect_asked", json_object_new_boolean(1));
			if (e && (strstr(e, "cut right after it opened") || strstr(e, "an answer that is not TLS")) &&
			    !json_object_get_boolean(json_object_object_get(v, "redirect_asked"))) {
				json_object_object_add(v, "redirect_asked", json_object_new_boolean(1));
				redirect_probe(j->key, j->dev, jstr(r, "target"));
			}
		}
		json_object_object_add(st, "reach", v);
		if (strncmp(j->key, "dev:", 4) && json_object_object_get(st, "after_recovery"))
			reach_after_recovery(j->key, st, level, now);
		if (strncmp(j->key, "dev:", 4) && strcmp(level, "down"))
			fw_check(j->key, st, j->dev);
		if (primary && strcmp(level, "down"))
			dns_start(j->key);
		else if (!primary)
			json_object_object_del(st, "dns");
		json_object_object_add(st, "next", json_object_new_int64(now + (recheck ?
			(uplink_override_ms ? uplink_override_ms : UPLINK_RECHECK_MS) :
			uplink_period(failover_active() ? FAILOVER_PROBE_MS : primary ? UPLINK_PRIMARY_MS : UPLINK_BACKUP_MS))));
	}
	json_object_put(r);
	j->active = false;
}

static void reach_try(struct reach_job *j);

/*
 * A world target «answering» a modem's path in under 4 ms is the modem itself: the owner's E3372 without its SIM
 * (r123) took every TCP connect for its own «no service» page, 1.1.1.1 in 2 ms, and the card said «Интернет
 * работает». No radio path is that near; a wired WAN (dev:) and a local target are left alone.
 */
#define REACH_NEAR "answered in under 4 ms: the modem itself, not the internet"

static bool reach_too_near(struct reach_job *j)
{
	if (!strncmp(j->key, "dev:", 4) || !strncmp(j->target, "127.", 4) || !strncmp(j->target, "[::1]", 5))
		return false;
	/* once the modem answered for itself, every later answer of the same check is taken as its own: the owner's
	 * E3372-325 without its SIM (r131) answered 1.1.1.1 in under 4 ms and vk.com a little slower, and the card said
	 * «only the second group answers: whitelists» */
	if (now_mono_ms() - j->start < 4)
		j->near = true;
	return j->near;
}

static void reach_next(struct reach_job *j, const char *error)
{
	reach_close(j);
	j->index++;
	if (j->index < (int)json_object_array_length(reach_list(j))) {
		reach_try(j);
	} else if (!j->phase && j->near) {
		/* the modem itself answered: its «restricted» answers would be its own too (no whitelists without a SIM) */
		reach_done(j, false, REACH_NEAR);
	} else if (!j->phase && json_object_array_length(reach_restricted())) {
		snprintf(j->world_target, sizeof(j->world_target), "%s", j->target);
		snprintf(j->world_error, sizeof(j->world_error), "%s", error ? error : "no answer");
		j->phase = 1;
		j->index = 0;
		reach_try(j);
	} else {
		reach_done(j, false, error);
	}
}


/*
 * A connection to :443 proves no internet by itself: with no money on the SIM an operator accepts every TCP connection
 * to show its «top up» page (a tester's T99, MTS? 2026-11-03: 1.1.1.1:443 «connected», curl https 000, generate_204 302;
 * the path was called ok and failover flapped down/back every 45 s). A TLS server answers a ClientHello with a TLS
 * record (a handshake or an alert); an operator's page answers HTTP or cuts the connection. One small hello, no
 * certificate needed. core.reach_tls '1' asks it of every target, '0' of none; by default of :443 ones.
 */
#define REACH_NOT_TLS "an answer that is not TLS: the operator's own page (no money or a block)"
#define REACH_CUT "the connection was cut right after it opened (the operator's page or a block)"
static const unsigned char reach_hello[60] = {
	0x16, 0x03, 0x01, 0x00, 0x37,			/* a handshake record of 55 bytes */
	0x01, 0x00, 0x00, 0x33, 0x03, 0x03,		/* ClientHello of 51, TLS 1.2 */
	0x6d, 0x65, 0x64, 0x76, 0x65, 0x6d, 0x6f, 0x64, 0x65, 0x6d, 0x2d, 0x72, 0x65, 0x61, 0x63, 0x68,
	0x2d, 0x63, 0x68, 0x65, 0x63, 0x6b, 0x2d, 0x6e, 0x6f, 0x6e, 0x63, 0x65, 0x2d, 0x30, 0x30, 0x31,	/* random */
	0x00,						/* no session id */
	0x00, 0x0c, 0x13, 0x01, 0x13, 0x02, 0xc0, 0x2b, 0xc0, 0x2f, 0x00, 0x9c, 0x00, 0x2f,	/* six suites */
	0x01, 0x00					/* no compression */
};

static bool reach_wants_tls(struct reach_job *j)
{
	json_object *c = core_uci();
	const char *v = c ? jstr(c, "reach_tls") : NULL;
	size_t n = strlen(j->target);

	if (v && !strcmp(v, "1"))
		return true;
	if (v && !strcmp(v, "0"))
		return false;
	return n > 4 && !strcmp(j->target + n - 4, ":443");
}

static void reach_readable(struct uloop_fd *fd, unsigned int events)
{
	struct reach_job *j = container_of(fd, struct reach_job, fd);
	unsigned char b[5];
	ssize_t n = recv(fd->fd, b, sizeof(b), 0);

	(void)events;
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return;
	if (n >= 1 && (b[0] == 0x16 || b[0] == 0x15))
		reach_done(j, true, NULL);
	else
		reach_next(j, n > 0 ? REACH_NOT_TLS : REACH_CUT);
}

static void reach_tls_timeout(struct uloop_timeout *t)
{
	reach_next(container_of(t, struct reach_job, timer), REACH_CUT);
}

/* connected: done, or the hello first */
static void reach_connected(struct reach_job *j)
{
	if (!reach_wants_tls(j)) {
		reach_done(j, true, NULL);
		return;
	}
	if (send(j->fd.fd, reach_hello, sizeof(reach_hello), MSG_NOSIGNAL) != (ssize_t)sizeof(reach_hello)) {
		reach_next(j, REACH_CUT);
		return;
	}
	if (j->fd.registered)
		uloop_fd_delete(&j->fd);
	j->fd.cb = reach_readable;
	uloop_fd_add(&j->fd, ULOOP_READ);
	j->timer.cb = reach_tls_timeout;
	uloop_timeout_set(&j->timer, j->slow ? UPLINK_CONNECT_PPP_MS : UPLINK_CONNECT_MS);
}

static void reach_writable(struct uloop_fd *fd, unsigned int events)
{
	struct reach_job *j = container_of(fd, struct reach_job, fd);
	int err = 0;
	socklen_t len = sizeof(err);

	(void)events;
	if (getsockopt(fd->fd, SOL_SOCKET, SO_ERROR, &err, &len))
		err = errno;
	if (!err && reach_too_near(j))
		reach_next(j, REACH_NEAR);
	else if (!err)
		reach_connected(j);
	else
		reach_next(j, strerror(err));
}

static void reach_timeout(struct uloop_timeout *t)
{
	reach_next(container_of(t, struct reach_job, timer), "no answer");
}

static void reach_try(struct reach_job *j)
{
	struct sockaddr_storage sa;
	socklen_t len;
	const char *t = json_object_get_string(json_object_array_get_idx(reach_list(j), (size_t)j->index));
	int fd;

	snprintf(j->target, sizeof(j->target), "%s", t ? t : "");
	if (!t || !parse_target(t, &sa, &len)) {
		reach_next(j, "bad target");
		return;
	}
	fd = socket(sa.ss_family, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		reach_next(j, strerror(errno));
		return;
	}
	j->fd.fd = fd;
	if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, j->dev, (socklen_t)strlen(j->dev) + 1)) {
		reach_next(j, strerror(errno));
		return;
	}
	j->start = now_mono_ms();
	if (!connect(fd, (struct sockaddr *)&sa, len)) {
		if (reach_too_near(j))
			reach_next(j, REACH_NEAR);
		else
			reach_connected(j);
		return;
	}
	if (errno != EINPROGRESS) {
		reach_next(j, strerror(errno));
		return;
	}
	j->fd.cb = reach_writable;
	uloop_fd_add(&j->fd, ULOOP_WRITE);
	j->timer.cb = reach_timeout;
	uloop_timeout_set(&j->timer, j->slow ? UPLINK_CONNECT_PPP_MS : UPLINK_CONNECT_MS);
}

static bool reach_running(const char *key)
{
	int k;

	for (k = 0; k < UPLINK_JOBS; k++)
		if (reach_jobs[k].active && !strcmp(reach_jobs[k].key, key))
			return true;
	return false;
}

static void reach_start(const char *key, const char *dev)
{
	int k;

	for (k = 0; k < UPLINK_JOBS && reach_jobs[k].active; k++)
		;
	if (k == UPLINK_JOBS)
		return;	/* all busy: the next tick tries again */
	memset(&reach_jobs[k], 0, sizeof(reach_jobs[k]));
	reach_jobs[k].active = true;
	reach_jobs[k].fd.fd = -1;
	snprintf(reach_jobs[k].key, sizeof(reach_jobs[k].key), "%s", key);
	snprintf(reach_jobs[k].dev, sizeof(reach_jobs[k].dev), "%s", dev);
	reach_jobs[k].slow = !strncmp(dev, "ppp", 3);
	if (!reach_jobs[k].slow && strncmp(key, "dev:", 4) && mm_devices) {
		json_object *at = json_object_object_get(json_object_object_get(mm_devices, key), "access_tech");
		size_t i, n = json_object_is_type(at, json_type_array) ? json_object_array_length(at) : 0;

		reach_jobs[k].slow = n > 0;
		for (i = 0; i < n; i++) {
			const char *v = json_object_get_string(json_object_array_get_idx(at, i));

			if (!v || !strncmp(v, "lte", 3) || !strcmp(v, "5gnr"))
				reach_jobs[k].slow = false;
		}
	}
	/* a router-mode stick whose own web says no SIM or no network: no probe, it would answer for itself */
	if (strncmp(key, "dev:", 4)) {
		const char *off = web_offline(mm_registry_find(&registry, key));

		if (off) {
			reach_done(&reach_jobs[k], false, off);
			return;
		}
	}
	reach_try(&reach_jobs[k]);
}

static bool read_ll(const char *dev, const char *file, long long *v)
{
	char path[PATH_MAX], line[32];

	snprintf(path, sizeof(path), "%s/%s/%s", SYS_NET, dev, file);
	if (!mm_read_line(path, line, sizeof(line)))
		return false;
	*v = strtoll(line, NULL, 10);
	return true;
}

/* the day's bucket says from when it counts: midnight when it rolled over here, else the first sample (the dashboard's
 * «Итого … с 14:05», the owner 2026-10-26) */
static void traffic_mark_from(json_object *traffic)
{
	json_object *d = json_object_object_get(traffic, "day");

	if (d && !json_object_object_get(d, "from"))
		json_object_object_add(d, "from", json_object_new_int64((long long)time(NULL)));
}

/* a wired WAN, a tunnel or PPP the failover watches or the default route goes through: its bytes as a modem's */
static void dev_traffic(json_object *st, const char *dev, long long now, const char *day, const char *month)
{
	json_object *traffic = json_object_object_get(st, "traffic");
	long long rx, tx;

	if (!read_ll(dev, "statistics/rx_bytes", &rx) || !read_ll(dev, "statistics/tx_bytes", &tx))
		return;
	if (!traffic) {
		traffic = json_object_new_object();
		json_object_object_add(st, "traffic", traffic);
	}
	mm_traffic_sample(traffic, rx, tx, now, false, day, month);
	traffic_mark_from(traffic);
}

/* a channel's row: today's bytes and from when they count */
static void path_traffic(json_object *p, json_object *traffic)
{
	json_object *d = json_object_object_get(traffic, "day");

	if (!d)
		return;
	json_object_object_add(p, "today_bytes", json_object_new_int64(json_object_get_int64(json_object_object_get(d, "rx_bytes")) +
								       json_object_get_int64(json_object_object_get(d, "tx_bytes"))));
	if (json_object_object_get(d, "from"))
		json_object_object_add(p, "today_from", json_object_get(json_object_object_get(d, "from")));
}

static char *slurp(const char *path)
{
	FILE *f = fopen(path, "r");
	char *buf;
	size_t n = 0, cap = 4096;

	if (!f)
		return NULL;
	buf = malloc(cap);
	while (buf) {
		size_t got = fread(buf + n, 1, cap - n - 1, f);
		char *more;

		n += got;
		if (n < cap - 1)
			break;
		/* a failed realloc keeps the old block: freed, not lost */
		if (!(more = realloc(buf, cap *= 2)))
			free(buf);
		buf = more;
	}
	if (buf)
		buf[n] = 0;
	fclose(f);
	return buf;
}

static void read_default_dev(void)
{
	char *r4 = slurp(PROC_NET_ROUTE), *r6 = slurp(PROC_NET_ROUTE6);

	if (!mm_uplink_default_dev(r4, r6, default_dev, sizeof(default_dev)))
		default_dev[0] = 0;
	free(r4);
	free(r6);
}

/* the network device of a served session, when it is connected */
static const char *session_dev(json_object *sess)
{
	json_object *n = json_object_object_get(sess, "netifd");

	if (jstr(n, "l3_device"))
		return jstr(n, "l3_device");
	if (jstr(sess, "source") && !strcmp(jstr(sess, "source"), "modemmanager"))
		return jstr(sess, "interface");
	return NULL;
}

static void uplink_save(void)
{
	char path[PATH_MAX], tmp[PATH_MAX + 8];
	json_object *o;

	if (!uplinks)
		return;
	o = json_object_new_object();
	json_object_object_foreach(uplinks, key, st) {
		json_object *t = json_object_object_get(st, "traffic"), *keep = json_object_new_object();
		if (json_object_object_get(t, "day"))
			json_object_object_add(keep, "day", json_object_get(json_object_object_get(t, "day")));
		if (json_object_object_get(t, "month"))
			json_object_object_add(keep, "month", json_object_get(json_object_object_get(t, "month")));
		if (json_object_object_length(keep))
			json_object_object_add(o, key, keep);
		else
			json_object_put(keep);
	}
	snprintf(path, sizeof(path), "%s/traffic.json", run_dir);
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	mkdir(run_dir, 0755);
	if (!json_object_to_file_ext(tmp, o, JSON_C_TO_STRING_PLAIN))
		rename(tmp, path);
	json_object_put(o);
}

/* day and month totals of an earlier engine run (they live in the run directory: a reboot clears them) */
static void uplink_load(void)
{
	char path[PATH_MAX];
	json_object *o;

	snprintf(path, sizeof(path), "%s/traffic.json", run_dir);
	if (!(o = json_object_from_file(path)))
		return;
	if (json_object_is_type(o, json_type_object)) {
		json_object_object_foreach(o, key, keep) {
			json_object *t, *day = json_object_object_get(keep, "day"), *month = json_object_object_get(keep, "month");

			/* an earlier version saved null for a modem it had not counted yet */
			if (!json_object_is_type(day, json_type_object) && !json_object_is_type(month, json_type_object))
				continue;
			t = json_object_new_object();
			if (json_object_is_type(day, json_type_object))
				json_object_object_add(t, "day", json_object_get(day));
			if (json_object_is_type(month, json_type_object))
				json_object_object_add(t, "month", json_object_get(month));
			json_object_object_add(uplink_state(key), "traffic", t);
		}
	}
	json_object_put(o);
}

/*
 * R11: once a minute the kernel log (MEDVEMODEM_KMSG_FILE and
 * MEDVEMODEM_KMSG_NOW in tests) is searched for USB power trouble of each
 * modem's USB path during the last 10 minutes; absent modems included (an
 * under-powered modem may never finish enumerating).
 */
#define USB_POWER_EVERY_MS 60000
#define USB_POWER_WINDOW_S 600
static long long usb_power_ms;

/*
 * USB autosuspend off for a modem: a suspended usbnet modem's transmit queue hangs («NETDEV WATCHDOG») and the
 * kernel's endpoint reset that follows has taken a whole USB controller down (a field report: an
 * FM350 over RNDIS on a Banana Pi R4). «auto» becomes «on» once per modem; core.usb_autosuspend '1' leaves it be.
 */
static void usb_keep_awake(const char *id, const char *usb_path)
{
	json_object *c = core_uci();
	char path[300], now[16] = "";
	FILE *f;

	if (c && jstr(c, "usb_autosuspend") && !strcmp(jstr(c, "usb_autosuspend"), "1"))
		return;
	if (strchr(usb_path, '/') || strstr(usb_path, ".."))
		return;
	snprintf(path, sizeof(path), "%s/%s/power/control", SYS_USB, usb_path);
	if (!(f = fopen(path, "r")))
		return;
	if (!fgets(now, sizeof(now), f))
		*now = 0;
	fclose(f);
	if (strncmp(now, "auto", 4))
		return;
	if ((f = fopen(path, "w"))) {
		bool ok = fputs("on\n", f) >= 0;

		ok = !fclose(f) && ok;
		if (ok)
			ULOG_INFO("%s: USB autosuspend off for %s (a suspended modem's data path hangs)\n", id, usb_path);
	}
}

static void usb_power_scan(void)
{
	const char *file = getenv("MEDVEMODEM_KMSG_FILE"), *nowenv = getenv("MEDVEMODEM_KMSG_NOW");
	char *log = NULL;
	struct timespec ts;
	double now;

	if (file) {
		log = slurp(file);
	} else {
		int size = klogctl(10, NULL, 0), n;

		if (size > 0 && (log = calloc(1, (size_t)size + 1))) {
			n = klogctl(3, log, size);
			if (n < 0) {
				free(log);
				log = NULL;
			} else {
				log[n] = 0;
			}
		}
	}
	clock_gettime(CLOCK_BOOTTIME, &ts);
	now = nowenv ? strtod(nowenv, NULL) : ts.tv_sec + ts.tv_nsec / 1e9;
	json_object_put(usb_power);
	usb_power = NULL;
	json_object_put(usb_txstall);
	usb_txstall = NULL;
	if (log && registry.devices) {
		json_object_object_foreach(registry.devices, id, rec) {
			const char *att = jstr(rec, "attachment_id");
			char last[200];
			int n;

			if (!att || strncmp(att, "usb:", 4))
				continue;
			if (json_object_get_boolean(json_object_object_get(rec, "present")))
				usb_keep_awake(id, att + 4);
			n = mm_usb_tx_stalls(log, att + 4, now, USB_POWER_WINDOW_S, last, sizeof(last));
			if (n) {
				json_object *o = json_object_new_object();

				if (!usb_txstall)
					usb_txstall = json_object_new_object();
				json_object_object_add(o, "events", json_object_new_int(n));
				json_object_object_add(o, "last", json_object_new_string(last));
				json_object_object_add(usb_txstall, id, o);
			}
			n = mm_usb_power_events(log, att + 4, now, USB_POWER_WINDOW_S, last, sizeof(last));
			if (!n)
				continue;
			if (!usb_power)
				usb_power = json_object_new_object();
			{
				json_object *o = json_object_new_object();
				json_object_object_add(o, "events", json_object_new_int(n));
				json_object_object_add(o, "last", json_object_new_string(last));
				json_object_object_add(usb_power, id, o);
			}
		}
	}
	free(log);
}

/* ---- uplink failover by route metrics (spec 20.8): off by default ---- */

#define FAILOVER_PENALTY 10000
#define FAILOVER_EVENTS 20
#define FAILOVER_MOVES 8	/* per family and turn: more routes wait for the next turn */
static json_object *fo_state;		/* dev -> {base, set?, down?, demoted?}, IPv4 */
static json_object *fo_state6;		/* the same for IPv6 */
static json_object *fo_events;		/* the last moves, newest last */
static json_object *fo_routes;		/* the IPv4 default routes of the last turn */
static struct uloop_process fo_proc;
static bool fo_running;
static long long fo_retry_ms;		/* the helper failed: no new turn before this */
#define FAILOVER_RETRY_MS 60000

json_object *core_uci(void)
{
	return json_object_object_get(medvemodem_uci(), "core");
}

/* the operator's switch (UCI medvemodem.core.failover, off unless '1') */
static bool failover_enabled(void)
{
	json_object *c = core_uci();

	return c && jstr(c, "failover") && !strcmp(jstr(c, "failover"), "1");
}

/* why the switch is on but failover does not act; NULL when it acts */
static const char *failover_blocked(void)
{
	char path[300];

	/* one owner of routing policy: mwan3 decides when it is there (spec 20.7) */
	snprintf(path, sizeof(path), "%s/mwan3", conf_dir);
	return access(path, F_OK) == 0 ? "mwan3 is installed: it owns the routing policy" : NULL;
}

static bool failover_active(void)
{
	return failover_enabled() && !failover_blocked();
}

/* the plan's options: active, penalty, manual links (core.failover_manual), failback (core.failback) */
static json_object *failover_opts(bool active)
{
	json_object *c = core_uci(), *o = json_object_new_object(), *m = c ? json_object_object_get(c, "failover_manual") : NULL;
	long p = c && jstr(c, "failover_penalty") ? atol(jstr(c, "failover_penalty")) : 0;

	json_object_object_add(o, "active", json_object_new_boolean(active));
	/* above every metric a channel may have (the channel table takes 0..9999): a lower one reads as a penalty
	 * already given (failover.c), review 2026-11-15 */
	json_object_object_add(o, "penalty", json_object_new_int64(p >= 10000 && p <= 1000000 ? p : FAILOVER_PENALTY));
	if (m && json_object_is_type(m, json_type_string)) {
		json_object *one = json_object_new_array();

		json_object_array_add(one, json_object_get(m));
		json_object_object_add(o, "manual", one);
	} else {
		json_object_object_add(o, "manual", m && json_object_is_type(m, json_type_array) ? json_object_get(m) : json_object_new_array());
	}
	json_object_object_add(o, "demote", json_object_new_boolean(c && jstr(c, "failback") && !strcmp(jstr(c, "failback"), "demote")));
	json_object_object_add(o, "quota", quota_held_devs());
	return o;
}

/* the IPv6 default routes of the main table (the proc file mixes every table) */
static json_object *failover_routes6(void)
{
#ifdef MM_ROUTE6_MAIN
	char *r6 = slurp(MM_ROUTE6_MAIN);	/* tests: the table as a file */
#else
	char *r6 = mm_failover_main6();
#endif
	json_object *r = mm_failover_routes6(r6);

	free(r6);
	return r;
}

static char fo_switch_from[IFNAMSIZ + 1], fo_switch_to[IFNAMSIZ + 1];	/* the carrier the running moves change */
static void carrier_switched(const char *from, const char *to, const char *why);

static void failover_exited(struct uloop_process *p, int ret)
{
	(void)p;
	fo_running = false;
	if (!ret && *fo_switch_from && *fo_switch_to)
		carrier_switched(fo_switch_from, fo_switch_to, "failover");
	fo_switch_from[0] = fo_switch_to[0] = 0;
	if (ret) {
		/* a route the helper cannot move would be planned again every turn */
		fo_retry_ms = now_mono_ms() + FAILOVER_RETRY_MS;
		ULOG_WARN("failover: the route helper ended with status %d, next turn in %d s\n", ret, FAILOVER_RETRY_MS / 1000);
	}
}

/* a helper in its own process group, stdin and stdout on /dev/null (stderr reaches the log) */
static pid_t failover_exec(const char *const *argv)
{
	pid_t pid = fork();

	if (pid == 0) {
		int fd = open("/dev/null", O_RDWR);

		setpgid(0, 0);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 1);
			if (fd > 2)
				close(fd);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execv(argv[0], (char *const *)argv);
		_exit(127);
	}
	return pid;
}

/* argv: helper, then dev from to per move, the IPv6 ones after -6 */
/* waitpid that a signal does not cut short: a SIGCHLD of another child or a second SIGTERM returned EINTR and the
 * engine went on (stopped) while the route helper still ran (the failover tests' flake under load, r84) */
static pid_t wait_child(pid_t pid, int *status)
{
	pid_t r;

	while ((r = waitpid(pid, status, 0)) < 0 && errno == EINTR)
		;
	return r;
}

static pid_t failover_spawn(json_object *moves, json_object *moves6)
{
	const char *argv[2 * 3 * FAILOVER_MOVES + 4];
	char nums[2 * FAILOVER_MOVES][2][24];
	json_object *fam[2] = { moves, moves6 };
	int n = 0, k = 0, f;
	size_t i;

	argv[n++] = route_helper;
	for (f = 0; f < 2; f++) {
		if (!fam[f] || !json_object_array_length(fam[f]))
			continue;
		if (f)
			argv[n++] = "-6";
		for (i = 0; i < json_object_array_length(fam[f]) && i < FAILOVER_MOVES; i++, k++) {
			json_object *m = json_object_array_get_idx(fam[f], i);

			snprintf(nums[k][0], sizeof(nums[k][0]), "%lld", (long long)json_object_get_int64(json_object_object_get(m, "from")));
			snprintf(nums[k][1], sizeof(nums[k][1]), "%lld", (long long)json_object_get_int64(json_object_object_get(m, "to")));
			argv[n++] = jstr(m, "dev");
			argv[n++] = nums[k][0];
			argv[n++] = nums[k][1];
		}
	}
	argv[n] = NULL;
	return failover_exec(argv);
}

/* ---- the carrier switched: the old channel's tracked connections go, the hooks hear it (spec 20.8) ---- */
static struct uloop_process flush_proc;
static bool flush_running;

/* core.failover_flush: 'link' (unset) the old channel's connections, 'all' the whole table, '0' none */
static const char *flush_mode(void)
{
	json_object *c = core_uci();
	const char *v = c ? jstr(c, "failover_flush") : NULL;

	return v && !strcmp(v, "0") ? NULL : v && !strcmp(v, "all") ? "all" : "link";
}

static void flush_exited(struct uloop_process *p, int ret)
{
	(void)p;
	flush_running = false;
	if (ret)
		ULOG_WARN("failover: the connection flush ended with status %d\n", WIFEXITED(ret) ? WEXITSTATUS(ret) : 128 + WTERMSIG(ret));
}

/* the default route moved from one device to another (failover's move or the operator's order) */
static void carrier_switched(const char *from, const char *to, const char *why)
{
	const char *mode = flush_mode(), *slash = strrchr(route_helper, '/');
	char path[PATH_MAX];
	json_object *e = json_object_new_object();
	pid_t pid;

	ULOG_NOTE("the traffic moved from %s to %s (%s)\n", from, to, why);
	if (mode && !flush_running) {
		const char *argv[] = { path, mode, from, NULL };

		snprintf(path, sizeof(path), "%.*sconntrack-flush", slash ? (int)(slash - route_helper + 1) : 0, route_helper);
		if ((pid = failover_exec(argv)) > 0) {
			flush_running = true;
			flush_proc.pid = pid;
			flush_proc.cb = flush_exited;
			uloop_process_add(&flush_proc);
		}
	}
	json_object_object_add(e, "dev", json_object_new_string(to));
	json_object_object_add(e, "previous", json_object_new_string(from));
	json_object_object_add(e, "why", json_object_new_string(why));
	json_object_object_add(e, "at", json_object_new_int64((long long)time(NULL)));
	broadcast("medvemodem.switch", e);
	json_object_put(e);
}

static const char *failover_why_text(const char *why)
{
	return !strcmp(why, "down") ? "has no internet, traffic steered away" :
	       !strcmp(why, "manual") ? "is a manual link, kept behind the others" :
	       !strcmp(why, "demoted") ? "answers again, kept last until the operator takes it back" :
	       !strcmp(why, "quota") ? "its SIM spent the monthly quota, kept behind the others" :
	       !strcmp(why, "limited") ? "limited (White Lists), another link carries the traffic" :
	       !strcmp(why, "back") ? "answers again, its priority restored" : "restored (failover off or one uplink left)";
}

static void failover_record(json_object *moves, bool v6)
{
	size_t i;

	for (i = 0; i < json_object_array_length(moves) && i < FAILOVER_MOVES; i++) {
		json_object *m = json_object_array_get_idx(moves, i), *e = json_object_get(m);

		json_object_object_add(e, "at", json_object_new_int64((long long)time(NULL)));
		if (v6)
			json_object_object_add(e, "ipv6", json_object_new_boolean(1));
		ULOG_WARN("failover: %s%s %s (metric %lld -> %lld)\n", jstr(m, "dev"), v6 ? " (IPv6)" : "", failover_why_text(jstr(m, "why")),
			  (long long)json_object_get_int64(json_object_object_get(m, "from")),
			  (long long)json_object_get_int64(json_object_object_get(m, "to")));
		broadcast("medvemodem.failover", e);
		/* in the journal of the modem whose route it is: «why dev» */
		if (!v6 && uplinks) {
			json_object_object_foreach(uplinks, id, st) {
				char d[96];

				if (!strncmp(id, "dev:", 4) || !jstr(st, "dev") || strcmp(jstr(st, "dev"), jstr(m, "dev")))
					continue;
				snprintf(d, sizeof(d), "%s %s", jstr(m, "why") ? jstr(m, "why") : "?", jstr(m, "dev"));
				mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "failover", d);
				history_dirty = true;
			}
		}
		json_object_array_add(fo_events, e);
		while (json_object_array_length(fo_events) > FAILOVER_EVENTS)
			json_object_array_del_idx(fo_events, 0, 1);
	}
}

/* ---- DNS follows the traffic (R35): the dead links' servers leave dnsmasq's list ---- */

#ifndef MM_RESOLV_AUTO
#define MM_RESOLV_AUTO "/tmp/resolv.conf.d/resolv.conf.auto"
#endif
static char fo_dns_applied[256];	/* the devices the helper last ran with, " a b" */
static long long fo_dns_mtime;		/* netifd's file as the helper left it */
static struct uloop_process fo_dns_proc;
static bool fo_dns_running;

/* on with failover unless core.failover_dns '0' */
static bool failover_dns_enabled(void)
{
	json_object *c = core_uci();

	return !(c && jstr(c, "failover_dns") && !strcmp(jstr(c, "failover_dns"), "0"));
}

static long long resolv_mtime(void)
{
	struct stat sb;

	return stat(MM_RESOLV_AUTO, &sb) ? 0 : (long long)sb.st_mtim.tv_sec * 1000000000LL + sb.st_mtim.tv_nsec;
}

static void failover_dns_exited(struct uloop_process *p, int ret)
{
	(void)p;
	fo_dns_running = false;
	fo_dns_mtime = resolv_mtime();
	if (ret)
		ULOG_WARN("failover: the DNS helper ended with status %d\n", ret);
}

/* dns-follow next to the route helper, with the given devices */
static pid_t failover_dns_spawn(const char *devs)
{
	const char *argv[2 + 16];
	char path[PATH_MAX], list[sizeof(fo_dns_applied)], *save = NULL, *w;
	const char *slash = strrchr(route_helper, '/');
	int n = 0;

	snprintf(path, sizeof(path), "%.*sdns-follow", slash ? (int)(slash - route_helper + 1) : 0, route_helper);
	snprintf(list, sizeof(list), "%s", devs);
	argv[n++] = path;
	for (w = strtok_r(list, " ", &save); w && n < 17; w = strtok_r(NULL, " ", &save))
		argv[n++] = w;
	argv[n] = NULL;
	return failover_exec(argv);
}

/* after a turn: the links steered away as dead, and netifd's file rewritten since the last run */
static void failover_dns(bool active)
{
	char dead[sizeof(fo_dns_applied)] = "";
	size_t used = 0;
	pid_t pid;

	if (fo_dns_running)
		return;
	if (active && failover_dns_enabled() && fo_state) {
		json_object_object_foreach(fo_state, dev, st) {
			if (json_object_object_get(st, "down") && json_object_object_get(st, "set") && strlen(dev) + 2 < sizeof(dead) - used)
				used += (size_t)snprintf(dead + used, sizeof(dead) - used, " %s", dev);
		}
	}
	if (!strcmp(dead, fo_dns_applied) && (!*dead || resolv_mtime() == fo_dns_mtime))
		return;
	if ((pid = failover_dns_spawn(dead)) > 0) {
		fo_dns_running = true;
		fo_dns_proc.pid = pid;
		fo_dns_proc.cb = failover_dns_exited;
		uloop_process_add(&fo_dns_proc);
		snprintf(fo_dns_applied, sizeof(fo_dns_applied), "%s", dead);
	}
}

/* ---- does the operator touch DNS (spec 25.9.20): the CLI's dns-check through each modem, one at a time ---- */

#define DNSCHK_EVERY_MS (6LL * 3600 * 1000)
static struct {
	bool active;
	char id[128], since[64], out[PATH_MAX];
	struct uloop_process proc;
	struct uloop_timeout kill;	/* bounded by its own timeouts; this is for a check that hangs anyway */
} dnschk;

static void dnschk_kill_fired(struct uloop_timeout *t)
{
	(void)t;
	if (dnschk.active && dnschk.proc.pid > 0) {
		ULOG_WARN("the DNS check of %s did not end in %d s: stopped\n", dnschk.id, CHILD_KILL_MS / 1000);
		kill(dnschk.proc.pid, SIGKILL);
	}
}
static long long dnschk_next_ms;	/* a failed start waits a minute */
/* modem id -> the monotonic ms of its last answered check: the 6 h count does not follow the wall clock
 * (a router without an RTC jumps at its NTP sync) nor the CLI's "at" */
static json_object *dnschk_mono;

/* off unless core.dns_check '1' (the owner, 2026-11-03: under the TSPU nearly every operator intercepts DNS, and the
 * warning on every modem only frightened people); the check by hand (the CLI's dns-check, LuCI's site check) stays */
static bool dnschk_enabled(void)
{
	json_object *c = core_uci();

	return c && jstr(c, "dns_check") && !strcmp(jstr(c, "dns_check"), "1");
}

static void dnschk_exited(struct uloop_process *p, int ret)
{
	json_object *r = json_object_from_file(dnschk.out), *st;

	(void)p; (void)ret;
	dnschk.active = false;
	uloop_timeout_cancel(&dnschk.kill);
	unlink(dnschk.out);
	st = uplinks ? json_object_object_get(uplinks, dnschk.id) : NULL;
	if (!r || !json_object_object_get(r, "intercept") || !st) {
		/* no answer (the CLI missing or failing): the next try in a minute */
		if (!r || !json_object_object_get(r, "intercept"))
			dnschk_next_ms = now_mono_ms() + 60000;
		json_object_put(r);
		return;
	}
	json_object_object_add(r, "since", json_object_new_string(dnschk.since));
	if (!dnschk_mono)
		dnschk_mono = json_object_new_object();
	json_object_object_add(dnschk_mono, dnschk.id, json_object_new_int64(now_mono_ms()));
	if (!strcmp(jstr(json_object_object_get(r, "intercept"), "state") ? jstr(json_object_object_get(r, "intercept"), "state") : "", "intercepted") ||
	    !strcmp(jstr(json_object_object_get(r, "nxdomain"), "state") ? jstr(json_object_object_get(r, "nxdomain"), "state") : "", "rewritten") ||
	    !strcmp(jstr(json_object_object_get(r, "blocked"), "state") ? jstr(json_object_object_get(r, "blocked"), "state") : "", "stub"))
		ULOG_NOTE("dns-check: %s: the operator answers DNS for others, gives stubs or rewrites NXDOMAIN\n", dnschk.id);
	json_object_object_add(st, "dns_check", r);
}

/* a modem whose session is up: checked once per session, again after 6 h */
static void dnschk_turn(const char *id, json_object *st, json_object *sess, const char *dev)
{
	json_object *was = json_object_object_get(st, "dns_check"), *dns = json_object_object_get(json_object_object_get(sess, "netifd"), "dns");
	const char *since = jstr(st, "since") ? jstr(st, "since") : "", *cli = getenv("MEDVEMODEM_CLI");
	const char *server = dns && json_object_is_type(dns, json_type_array) && json_object_array_length(dns) ?
			     json_object_get_string(json_object_array_get_idx(dns, 0)) : NULL;
	const char *argv[6];
	long long now = now_mono_ms();
	int fd, n = 0;
	pid_t pid;

	if (dnschk.active || now < dnschk_next_ms || !dnschk_enabled())
		return;
	/* once per session and device (ModemManager's port may name it until netifd's arrives), again after 6 h */
	if (was && jstr(was, "since") && !strcmp(jstr(was, "since"), since) && jstr(was, "device") && !strcmp(jstr(was, "device"), dev) &&
	    json_object_object_get(dnschk_mono, id) && now - json_object_get_int64(json_object_object_get(dnschk_mono, id)) < DNSCHK_EVERY_MS)
		return;
	/* a check that answered does not hold the next one: the session's device may change at once
	 * (ModemManager's port until netifd's arrives) and is checked again then (CI 2026-10-16) */
	snprintf(dnschk.out, sizeof(dnschk.out), "%s/dns-check.json", run_dir);
	mkdir(run_dir, 0755);
	argv[n++] = cli && *cli ? cli : "/usr/sbin/medvemodem";
	argv[n++] = "dns-check";
	argv[n++] = dev;
	if (server && *server)
		argv[n++] = server;
	/*
	 * The path's own round trip (the reach check through this device): an answer far faster came from the router.
	 * An argument, not the environment: the installed CLI drops its caller's environment (review 2026-11-15: on a
	 * router the time never arrived, so a local resolver was never told from the operator's).
	 */
	json_object *reach = json_object_object_get(st, "reach");
	int path_ms = reach && json_object_get_boolean(json_object_object_get(reach, "ok")) &&
		      json_object_object_get(reach, "latency_ms") ? json_object_get_int(json_object_object_get(reach, "latency_ms")) : -1;
	char path_arg[32];

	if (path_ms >= 0) {
		snprintf(path_arg, sizeof(path_arg), "--path-ms=%d", path_ms);
		argv[n++] = path_arg;
	}
	argv[n] = NULL;
	pid = fork();
	if (pid == 0) {
		fd = open(dnschk.out, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (fd < 0)
			_exit(127);
		dup2(fd, 1);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 2);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execv(argv[0], (char *const *)argv);
		_exit(127);
	}
	if (pid < 0) {
		dnschk_next_ms = now + 60000;
		return;
	}
	dnschk.active = true;
	snprintf(dnschk.id, sizeof(dnschk.id), "%s", id);
	snprintf(dnschk.since, sizeof(dnschk.since), "%s", since);
	dnschk.proc.pid = pid;
	dnschk.proc.cb = dnschk_exited;
	uloop_process_add(&dnschk.proc);
	dnschk.kill.cb = dnschk_kill_fired;
	uloop_timeout_set(&dnschk.kill, CHILD_KILL_MS);
}

/* ---- the fixed TTL of a modem's outgoing packets (a tariff for phones; field request 2026-10-13) ---- */

#define TTL_REFRESH_MS (10 * 60 * 1000)
static char ttl_applied[512] = "?";	/* " dev ttl since" per modem, as the helper last ran; "?" clears leftovers once */
static long long ttl_applied_ms;
static struct uloop_process ttl_proc;
static bool ttl_running;
static int ttl_status = -1;		/* the helper's last exit: 0 done, 3 no egress hook, else refused */
static json_object *decision_of(const char *id);

/* config device / option ttl: 1..255, 0 when not set */
static int device_ttl(const char *id)
{
	const char *v = jstr(decision_of(id), "ttl");
	char *end = NULL;
	long n = v && *v ? strtol(v, &end, 10) : 0;

	return end && !*end && n >= 1 && n <= 255 ? (int)n : 0;
}

static void ttl_exited(struct uloop_process *p, int ret)
{
	int code = WIFEXITED(ret) ? WEXITSTATUS(ret) : 1;

	(void)p;
	ttl_running = false;
	if (code != ttl_status && code && *ttl_applied)
		ULOG_WARN("ttl: %s\n", code == 3 ? "no egress hook on a modem's device: offloaded connections keep their TTL" :
						   "nft refused the TTL rules");
	ttl_status = code;
}

/* the sessions up with a TTL asked for, applied again on a change, a new session or every 10 min */
static void ttl_turn(void)
{
	char want[sizeof(ttl_applied)] = "", path[PATH_MAX], list[sizeof(ttl_applied)], *save = NULL, *w;
	const char *argv[2 + 2 * 16], *slash = strrchr(route_helper, '/');
	size_t used = 0;
	int n = 0, k = 0;
	pid_t pid;

	if (ttl_running)
		return;
	if (uplinks) {
		json_object_object_foreach(uplinks, id, st) {
			int ttl = strncmp(id, "dev:", 4) && jstr(st, "dev") ? device_ttl(id) : 0;

			if (ttl && used + strlen(jstr(st, "dev")) + 64 < sizeof(want))
				used += (size_t)snprintf(want + used, sizeof(want) - used, " %s %d %s", jstr(st, "dev"), ttl,
							 jstr(st, "since") && *jstr(st, "since") ? jstr(st, "since") : "-");
		}
	}
	if (!strcmp(want, ttl_applied) && (!*want || now_mono_ms() - ttl_applied_ms < TTL_REFRESH_MS))
		return;
	snprintf(path, sizeof(path), "%.*sttl-fix", slash ? (int)(slash - route_helper + 1) : 0, route_helper);
	snprintf(list, sizeof(list), "%s", want);
	argv[n++] = path;
	/* dev ttl since triples: the helper takes the pairs */
	for (w = strtok_r(list, " ", &save); w && n < 1 + 2 * 16; w = strtok_r(NULL, " ", &save), k++)
		if (k % 3 != 2)
			argv[n++] = w;
	argv[n] = NULL;
	if ((pid = failover_exec(argv)) > 0) {
		ttl_running = true;
		ttl_proc.pid = pid;
		ttl_proc.cb = ttl_exited;
		uloop_process_add(&ttl_proc);
		snprintf(ttl_applied, sizeof(ttl_applied), "%s", want);
		ttl_applied_ms = now_mono_ms();
	}
}

/* hardware flow offloading (fw4 defaults flow_offloading_hw '1', MediaTek's PPE): offloaded flows pass the kernel by,
 * no hook sees them and they keep their TTL (a WH3000 Pro owner's report, 2026-11-03: Beeline saw the tethering) */
static bool fw_hw_offload(void)
{
	static const char *const pk[] = { "firewall", NULL };
	char err[160];
	json_object *all = mm_uci_read(conf_dir, pk, err, sizeof(err)), *fw = all ? json_object_object_get(all, "firewall") : NULL;
	bool on = false;

	if (fw && json_object_is_type(fw, json_type_object)) {
		json_object_object_foreach(fw, k, sec) {
			(void)k;
			if (jstr(sec, ".type") && !strcmp(jstr(sec, ".type"), "defaults") && jstr(sec, "flow_offloading_hw") &&
			    !strcmp(jstr(sec, "flow_offloading_hw"), "1") && (!jstr(sec, "flow_offloading") || !strcmp(jstr(sec, "flow_offloading"), "1")))
				on = true;
		}
	}
	json_object_put(all);
	return on;
}

/* what the modem page says: asked for, and whether the rules stand on this session's device */
static json_object *ttl_view(const char *id)
{
	json_object *st = uplinks ? json_object_object_get(uplinks, id) : NULL, *o;
	int ttl = device_ttl(id);
	char mine[128];

	if (!ttl)
		return NULL;
	o = json_object_new_object();
	json_object_object_add(o, "value", json_object_new_int(ttl));
	snprintf(mine, sizeof(mine), " %s %d ", jstr(st, "dev") ? jstr(st, "dev") : "", ttl);
	json_object_object_add(o, "state", json_object_new_string(
		!jstr(st, "dev") ? "waiting" : !strstr(ttl_applied, mine) || ttl_running || ttl_status < 0 ? "applying" :
		ttl_status == 0 ? "on" : ttl_status == 3 ? "no-egress" : "failed"));
	if (jstr(st, "dev"))
		json_object_object_add(o, "device", json_object_new_string(jstr(st, "dev")));
	if (fw_hw_offload())
		json_object_object_add(o, "hw_offload", json_object_new_boolean(true));
	return o;
}

/*
 * ---- the router's internet works again: the services the operator chose start anew (field request 2026-10-23) ----
 * A tunnel or a bypass (podkop, zapret, passwall, a clash) starts at boot before the modem has dialled, finds no
 * internet (its lists not fetched, its tunnel to nowhere) and notices only at its own check, minutes later
 * (a FM350 owner's report: up to 10 min with ZeroBlock). Under an
 * operator's whitelist the modem's channel is «limited», yet the VLESS server on a whitelisted address answers: a
 * limited carrier counts as working. Restarted when the channel that carries the traffic answers: the first answer
 * after a boot (the engine started within 10 min of it), and again after it was without one for a minute or more.
 * Not when a backup comes up while the carrier works (a restart then only breaks the tunnel). At most once in 5 min.
 * core list restart_on_uplink names the services (/etc/init.d/<name>); the hooks hear ACTION=ready either way.
 */
#ifndef MM_PROC_UPTIME
#define MM_PROC_UPTIME "/proc/uptime"
#endif
#ifndef READY_LOST_MS
#define READY_LOST_MS 60000
#endif
#ifndef READY_EVERY_MS
#define READY_EVERY_MS (5 * 60 * 1000)
#endif
#define READY_BOOT_S 600
static int ready_state = -1;		/* -1 not known yet, 0 no working carrier, 1 one works */
static long long ready_lost_ms, ready_fired_ms = -1;
static double ready_start_uptime = -1;	/* the system's uptime when the engine started */
static struct uloop_process ready_proc;
static bool ready_running;

static const char *initd_dir(void)
{
	const char *d = getenv("MEDVEMODEM_INITD");

	return d && *d ? d : "/etc/init.d";
}

static bool service_name_ok(const char *n)
{
	return n && *n && strlen(n) < 64 && strspn(n, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.-") == strlen(n) &&
	       n[0] != '.';
}

/* the services the operator chose: core list restart_on_uplink (one value or a list); «-» alone is «none». Never set:
 * ZeroBlock when it is installed (the owner, 2026-11-08, from a whitelist guide: it waits for its own check up to
 * 10 min after a boot, HANDOFF 211) */
static json_object *ready_services(void)
{
	json_object *c = core_uci(), *v = c ? json_object_object_get(c, "restart_on_uplink") : NULL, *out = json_object_new_array();
	size_t i, n = v && json_object_is_type(v, json_type_array) ? json_object_array_length(v) : v ? 1 : 0;

	if (!v) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "%s/zeroblock", initd_dir());
		if (access(path, X_OK) == 0)
			json_object_array_add(out, json_object_new_string("zeroblock"));
		return out;
	}
	for (i = 0; i < n; i++) {
		const char *name = json_object_get_string(json_object_is_type(v, json_type_array) ? json_object_array_get_idx(v, i) : v);

		if (service_name_ok(name) && strcmp(name, "-"))
			json_object_array_add(out, json_object_new_string(name));
	}
	return out;
}

static void ready_exited(struct uloop_process *p, int ret)
{
	(void)p;
	ready_running = false;
	if (ret)
		ULOG_WARN("ready: restarting the services ended with status %d\n", WIFEXITED(ret) ? WEXITSTATUS(ret) : 128 + WTERMSIG(ret));
}

static void ready_fire(const char *dev, const char *level, const char *why)
{
	json_object *svcs = ready_services(), *e = json_object_new_object();
	const char *slash = strrchr(route_helper, '/');
	long long now = now_mono_ms();

	if (json_object_array_length(svcs) && !ready_running && (ready_fired_ms < 0 || now - ready_fired_ms >= READY_EVERY_MS)) {
		const char *argv[2 + 16];
		char path[PATH_MAX];
		int n = 0;
		pid_t pid;

		snprintf(path, sizeof(path), "%.*ssvc-restart", slash ? (int)(slash - route_helper + 1) : 0, route_helper);
		argv[n++] = path;
		for (size_t i = 0; i < json_object_array_length(svcs) && n < 17; i++)
			argv[n++] = json_object_get_string(json_object_array_get_idx(svcs, i));
		argv[n] = NULL;
		if ((pid = failover_exec(argv)) > 0) {
			ready_running = true;
			ready_proc.pid = pid;
			ready_proc.cb = ready_exited;
			uloop_process_add(&ready_proc);
			ready_fired_ms = now;
			ULOG_NOTE("ready: the internet works through %s (%s): restarting %s\n", dev, why,
				  json_object_to_json_string_ext(svcs, JSON_C_TO_STRING_PLAIN));
		}
	}
	json_object_object_add(e, "dev", json_object_new_string(dev));
	json_object_object_add(e, "level", json_object_new_string(level));
	json_object_object_add(e, "why", json_object_new_string(why));
	json_object_object_add(e, "at", json_object_new_int64((long long)time(NULL)));
	broadcast("medvemodem.ready", e);
	json_object_put(e);
	json_object_put(svcs);
}

/* the reach verdict of the carrier (the default route's device); NULL before its first */
static const char *carrier_level(void)
{
	const char *level = NULL;

	if (*default_dev && uplinks) {
		json_object_object_foreach(uplinks, key, st) {
			json_object *r = json_object_object_get(st, "reach");

			(void)key;
			if (jstr(st, "dev") && !strcmp(jstr(st, "dev"), default_dev) && r && jstr(r, "level"))
				level = jstr(r, "level");
		}
	}
	return level;
}

/* after the reach checks of a pass: does the carrier (the default route's device) answer? */
static void ready_turn(void)
{
	const char *level = carrier_level();
	bool alive;
	long long now = now_mono_ms();

	if (ready_start_uptime < 0) {
		FILE *f = fopen(MM_PROC_UPTIME, "r");

		if (!f || fscanf(f, "%lf", &ready_start_uptime) != 1)
			ready_start_uptime = 1e9;
		if (f)
			fclose(f);
	}
	tunnel_turn(level);
	if (*default_dev && !level)
		return;		/* the carrier's first verdict is not in yet */
	alive = level && (!strcmp(level, "ok") || !strcmp(level, "limited"));
	if (ready_state < 0) {
		ready_state = alive;
		if (alive && ready_start_uptime < READY_BOOT_S)
			ready_fire(default_dev, level, "boot");
		else if (!alive)
			ready_lost_ms = now;
		return;
	}
	if (!alive && ready_state == 1) {
		ready_state = 0;
		ready_lost_ms = now;
	} else if (alive && ready_state == 0) {
		ready_state = 1;
		if (now - ready_lost_ms >= READY_LOST_MS)
			ready_fire(default_dev, level, "back");
	}
}

/* one turn after the reach checks: decide, then move the routes in a worker */
static void failover_turn(void)
{
	json_object *levels, *moves, *moves6, *routes6, *opts;
	char *r4;
	bool active = failover_active();

	if (fo_running || now_mono_ms() < fo_retry_ms ||
	    (!active && (!fo_state || !json_object_object_length(fo_state)) && (!fo_state6 || !json_object_object_length(fo_state6)) &&
	     !*fo_dns_applied))
		return;
	if (!fo_state)
		fo_state = json_object_new_object();
	if (!fo_state6)
		fo_state6 = json_object_new_object();
	if (!fo_events)
		fo_events = json_object_new_array();
	r4 = slurp(PROC_NET_ROUTE);
	json_object_put(fo_routes);
	fo_routes = mm_failover_routes(r4);
	free(r4);
	routes6 = failover_routes6();
	/* IPv6 follows the device's verdict: the checks run over IPv4 */
	levels = json_object_new_object();
	if (uplinks) {
		json_object_object_foreach(uplinks, key, st) {
			json_object *r = json_object_object_get(st, "reach");

			(void)key;
			if (jstr(st, "dev") && r && jstr(r, "level"))
				json_object_object_add(levels, jstr(st, "dev"), json_object_new_string(jstr(r, "level")));
		}
	}
	opts = failover_opts(active);
	moves = mm_failover_plan(fo_routes, levels, fo_state, opts);
	moves6 = mm_failover_plan(routes6, levels, fo_state6, opts);
	json_object_put(opts);
	json_object_put(levels);
	json_object_put(routes6);
	if (json_object_array_length(moves) || json_object_array_length(moves6)) {
		/* the IPv4 carrier these moves change, told once they stand */
		const char *top0 = mm_failover_top(fo_routes, NULL), *top1 = mm_failover_top(fo_routes, moves);
		pid_t pid;

		fo_switch_from[0] = fo_switch_to[0] = 0;
		if (top0 && top1 && strcmp(top0, top1) && strlen(top0) <= IFNAMSIZ && strlen(top1) <= IFNAMSIZ) {
			snprintf(fo_switch_from, sizeof(fo_switch_from), "%s", top0);
			snprintf(fo_switch_to, sizeof(fo_switch_to), "%s", top1);
		}
		pid = failover_spawn(moves, moves6);
		if (pid > 0) {
			fo_running = true;
			fo_proc.pid = pid;
			fo_proc.cb = failover_exited;
			uloop_process_add(&fo_proc);
		}
		failover_record(moves, false);
		failover_record(moves6, true);
	}
	json_object_put(moves);
	json_object_put(moves6);
	failover_dns(active);
}

/*
 * A connected modem whose interface netifd holds up with an IPv4 default route, while the kernel has none through its
 * device (the owner's T99W175 on r153, 2026-11-04: «резерв готов · нет маршрута по умолчанию», «100 / —»; the traffic
 * never came back to it, not by failover, not by the order). netifd adds a route with NLM_F_REPLACE: an interface
 * brought up again (the HiLink's recovery at 13:48) with the metric netifd still keeps — «Сделать основным» moves the
 * live routes only, without a reload — replaced the T99's default route of that metric. The route goes back as netifd
 * has it, at the configured metric or the first free one above it; failover takes it as the base. Seen on two passes
 * in a row, so an ifup on its way is not raced.
 */
static json_object *route_lost;	/* device_id -> passes seen without the route */
static json_object *uplink_srv(json_object *rec, const char *id);

static void route_repair(void)
{
	json_object *routes;
	char *r4;

	if (!registry.devices || fo_running)
		return;
	if (!route_lost)
		route_lost = json_object_new_object();
	r4 = slurp(PROC_NET_ROUTE);
	routes = mm_failover_routes(r4 ? r4 : "");
	free(r4);
	json_object_object_foreach(registry.devices, id, rec) {
		json_object *srv, *sess, *conn, *st = NULL, *rl;
		const char *dev, *iface, *gw = NULL;
		long long metric = 0;
		bool has = false, wants = false;
		size_t i;
		int seen;

		if (!json_object_get_boolean(json_object_object_get(rec, "present")) || frozen(id)) {
			json_object_object_del(route_lost, id);
			continue;
		}
		srv = uplink_srv(rec, id);
		sess = json_object_object_get(srv, "session");
		dev = session_dev(sess);
		for (i = 0; dev && i < json_object_array_length(routes); i++)
			has = has || !strcmp(jstr(json_object_array_get_idx(routes, i), "dev"), dev);
		if (!dev || has || !jstr(sess, "state") || strcmp(jstr(sess, "state"), "connected")) {
			json_object_object_del(route_lost, id);
			json_object_put(srv);
			continue;
		}
		conn = connection_of(id, srv);
		iface = jstr(conn, "interface");
		if (iface && (st = netifd_call(iface, "status")) && json_object_get_boolean(json_object_object_get(st, "up")) &&
		    jstr(st, "l3_device") && !strcmp(jstr(st, "l3_device"), dev)) {
			rl = json_object_object_get(st, "route");
			for (i = 0; rl && i < json_object_array_length(rl); i++) {
				json_object *r = json_object_array_get_idx(rl, i);

				if (jstr(r, "target") && !strcmp(jstr(r, "target"), "0.0.0.0") &&
				    !json_object_get_int(json_object_object_get(r, "mask"))) {
					wants = true;
					gw = jstr(r, "nexthop");
				}
			}
			metric = json_object_object_get(conn, "metric") ? json_object_get_int64(json_object_object_get(conn, "metric")) :
				 json_object_object_get(st, "metric") ? json_object_get_int64(json_object_object_get(st, "metric")) : 0;
		}
		seen = wants ? json_object_get_int(json_object_object_get(route_lost, id)) + 1 : 0;
		if (!wants) {
			json_object_object_del(route_lost, id);
		} else if (seen < 2) {
			json_object_object_add(route_lost, id, json_object_new_int(seen));
		} else {
			char m[24], d[IFNAMSIZ + 1], g[48];
			const char *argv[6];
			int status = -1;
			pid_t pid;

			/* the kernel keeps one IPv4 default route per metric: the first free one from the configured up */
			for (;;) {
				bool held = false;

				for (i = 0; i < json_object_array_length(routes); i++)
					held = held || json_object_get_int64(json_object_object_get(json_object_array_get_idx(routes, i), "metric")) == metric;
				if (!held)
					break;
				metric++;
			}
			snprintf(m, sizeof(m), "%lld", metric);
			snprintf(d, sizeof(d), "%s", dev);
			snprintf(g, sizeof(g), "%s", gw && strspn(gw, "0123456789.") == strlen(gw) ? gw : "");
			argv[0] = route_helper;
			argv[1] = "--add";
			argv[2] = d;
			argv[3] = m;
			argv[4] = *g ? g : NULL;
			argv[5] = NULL;
			pid = failover_exec(argv);
			if (pid > 0)
				wait_child(pid, &status);
			json_object_object_del(route_lost, id);
			if (pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
				ULOG_WARN("route %s: %s (%s) had no default route while netifd holds it up; added back at metric %s\n",
					  id, d, iface, m);
				mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "route", "default route restored");
				history_dirty = true;
			} else {
				ULOG_WARN("route %s: %s (%s) has no default route and the helper could not add it\n", id, d, iface);
			}
		}
		json_object_put(st);
		json_object_put(conn);
		json_object_put(srv);
	}
	json_object_put(routes);
}

/* the engine stops: every route it moved goes back (synchronously, the helper is quick) */
static void failover_restore_all(void)
{
	json_object *moves, *moves6, *routes6, *opts;
	pid_t pid;
	char *r4;

	/* the servers of the dead links come back first */
	if (fo_dns_running && fo_dns_proc.pid > 0)
		wait_child(fo_dns_proc.pid, NULL);
	if (*fo_dns_applied && (pid = failover_dns_spawn("")) > 0) {
		wait_child(pid, NULL);
		fo_dns_applied[0] = 0;
	}
	if ((!fo_state || !json_object_object_length(fo_state)) && (!fo_state6 || !json_object_object_length(fo_state6)))
		return;
	if (!fo_state)
		fo_state = json_object_new_object();
	if (!fo_state6)
		fo_state6 = json_object_new_object();
	if (fo_running && fo_proc.pid > 0)
		wait_child(fo_proc.pid, NULL);
	/* the routes as they are now: the last move may have landed after the last read */
	r4 = slurp(PROC_NET_ROUTE);
	json_object_put(fo_routes);
	fo_routes = mm_failover_routes(r4);
	free(r4);
	routes6 = failover_routes6();
	opts = failover_opts(false);
	moves = mm_failover_plan(fo_routes, NULL, fo_state, opts);
	moves6 = mm_failover_plan(routes6, NULL, fo_state6, opts);
	if ((json_object_array_length(moves) || json_object_array_length(moves6)) && (pid = failover_spawn(moves, moves6)) > 0)
		wait_child(pid, NULL);
	json_object_put(opts);
	json_object_put(routes6);
	json_object_put(moves);
	json_object_put(moves6);
}

/* «Сделать основным» on the live routes: each moved device's default routes go to the new metric, one put
 * behind by failover stays the same distance behind it, and failover's memory takes the new base */
static int priority_live(json_object *moved)
{
	json_object *fam[2], *state[2], *moves[2], *pending = json_object_new_array();
	char *r4;
	pid_t pid;
	size_t i, j;
	int f, rc = 0, status;

	if (!moved || !json_object_array_length(moved)) {
		json_object_put(pending);
		return 0;
	}
	if (fo_running && fo_proc.pid > 0) {
		wait_child(fo_proc.pid, NULL);	/* the routes as failover's last move left them */
		/* reaped here: uloop must not keep it (its pid may be reused) nor failover wait for it for ever (review
		 * 2026-10-22: fo_running stayed true and failover never moved a route again) */
		uloop_process_delete(&fo_proc);
		fo_running = false;
	}
	r4 = slurp(PROC_NET_ROUTE);
	fam[0] = mm_failover_routes(r4);
	free(r4);
	fam[1] = failover_routes6();
	state[0] = fo_state;
	state[1] = fo_state6;
	for (f = 0; f < 2; f++) {
		moves[f] = json_object_new_array();
		for (i = 0; i < json_object_array_length(moved); i++) {
			json_object *mv = json_object_array_get_idx(moved, i);
			long long want = json_object_get_int64(json_object_object_get(mv, "metric"));

			for (j = 0; j < json_object_array_length(fam[f]); j++) {
				json_object *r = json_object_array_get_idx(fam[f], j), *st, *m;
				long long now = json_object_get_int64(json_object_object_get(r, "metric")), to = want;

				if (strcmp(jstr(r, "dev"), jstr(mv, "dev")))
					continue;
				st = state[f] ? json_object_object_get(state[f], jstr(r, "dev")) : NULL;
				if (st) {
					long long base = json_object_get_int64(json_object_object_get(st, "base"));
					json_object *p;

					if (now > base)
						to = want + (now - base);
					/* failover learns the new base only once the routes stand so (review 2026-10-22: set before
					 * the helper ran, a failed move made failover adopt the old metric as the base) */
					p = json_object_new_object();
					json_object_object_add(p, "st", json_object_get(st));
					json_object_object_add(p, "base", json_object_new_int64(want));
					json_object_object_add(p, "set", json_object_new_int64(to));
					json_object_array_add(pending, p);
				}
				if (to == now)
					break;
				m = json_object_new_object();
				json_object_object_add(m, "dev", json_object_new_string(jstr(r, "dev")));
				json_object_object_add(m, "from", json_object_new_int64(now));
				json_object_object_add(m, "to", json_object_new_int64(to));
				json_object_array_add(moves[f], m);
				break;
			}
		}
	}
	if (json_object_array_length(moves[0]) || json_object_array_length(moves[1])) {
		if ((pid = failover_spawn(moves[0], moves[1])) > 0 && wait_child(pid, &status) == pid &&
		    WIFEXITED(status) && WEXITSTATUS(status) == 0)
			rc = 1;
		else
			rc = -1;
	}
	if (rc >= 0) {
		for (i = 0; i < json_object_array_length(pending); i++) {
			json_object *p = json_object_array_get_idx(pending, i), *st = json_object_object_get(p, "st");

			json_object_object_add(st, "base", json_object_get(json_object_object_get(p, "base")));
			if (json_object_object_get(st, "set"))
				json_object_object_add(st, "set", json_object_get(json_object_object_get(p, "set")));
		}
	} else {
		ULOG_WARN("priority: the route helper could not move the routes; failover keeps its order\n");
	}
	for (f = 0; f < 2; f++) {
		json_object_put(fam[f]);
		json_object_put(moves[f]);
	}
	json_object_put(pending);
	return rc;
}

static void failover_view_routes(json_object *out, json_object *now, json_object *state, json_object *manual, json_object *quota, bool v6)
{
	size_t i, j;

	for (i = 0; i < json_object_array_length(now); i++) {
		json_object *r = json_object_array_get_idx(now, i), *v = json_object_new_object();
		json_object *st = state ? json_object_object_get(state, jstr(r, "dev")) : NULL;
		bool is_manual = false;

		for (j = 0; manual && j < json_object_array_length(manual); j++)
			is_manual = is_manual || !strcmp(json_object_get_string(json_object_array_get_idx(manual, j)), jstr(r, "dev"));
		json_object_object_add(v, "device", json_object_new_string(jstr(r, "dev")));
		if (v6)
			json_object_object_add(v, "ipv6", json_object_new_boolean(1));
		json_object_object_add(v, "metric", json_object_get(json_object_object_get(r, "metric")));
		if (st)
			json_object_object_add(v, "base", json_object_get(json_object_object_get(st, "base")));
		/* what the kernel has, not what the last turn decided: the move may still be running */
		json_object_object_add(v, "steered_away", json_object_new_boolean(st &&
			json_object_get_int64(json_object_object_get(r, "metric")) > json_object_get_int64(json_object_object_get(st, "base"))));
		if (is_manual)
			json_object_object_add(v, "manual", json_object_new_boolean(1));
		for (j = 0; quota && j < json_object_array_length(quota); j++)
			if (!strcmp(json_object_get_string(json_object_array_get_idx(quota, j)), jstr(r, "dev")))
				json_object_object_add(v, "quota", json_object_new_boolean(1));
		if (st && json_object_object_get(st, "demoted"))
			json_object_object_add(v, "demoted", json_object_new_boolean(1));
		json_object_array_add(out, v);
	}
}

/* the uplink reply's failover block */
static json_object *failover_view(void)
{
	json_object *o = json_object_new_object(), *routes = json_object_new_array(), *now, *now6, *opts = failover_opts(false);
	const char *blocked = failover_blocked();
	char *r4 = slurp(PROC_NET_ROUTE);

	/* the table as it is now, not as the last turn read it */
	now = mm_failover_routes(r4);
	free(r4);
	now6 = failover_routes6();

	json_object_object_add(o, "enabled", json_object_new_boolean(failover_enabled()));
	json_object_object_add(o, "active", json_object_new_boolean(failover_enabled() && !blocked));
	if (failover_enabled() && blocked)
		json_object_object_add(o, "why", json_object_new_string(blocked));
	json_object_object_add(o, "failback", json_object_new_string(
		json_object_get_boolean(json_object_object_get(opts, "demote")) ? "demote" : "restore"));
	json_object_object_add(o, "manual", json_object_get(json_object_object_get(opts, "manual")));
	failover_view_routes(routes, now, fo_state, json_object_object_get(opts, "manual"), json_object_object_get(opts, "quota"), false);
	failover_view_routes(routes, now6, fo_state6, json_object_object_get(opts, "manual"), json_object_object_get(opts, "quota"), true);
	json_object_put(now);
	json_object_put(now6);
	json_object_put(opts);
	json_object_object_add(o, "routes", routes);
	json_object_object_add(o, "events", fo_events ? json_object_get(fo_events) : json_object_new_array());
	return o;
}

/* is dev one of the default routes failover watches */
static bool failover_watches(const char *dev)
{
	size_t i;

	for (i = 0; fo_routes && i < json_object_array_length(fo_routes); i++)
		if (!strcmp(jstr(json_object_array_get_idx(fo_routes, i), "dev"), dev))
			return true;
	return false;
}

/*
 * A native interface the engine chose by itself ("auto") while an installed
 * ModemManager did not answer (boot, a restart) stays native: the ledger keeps
 * it, ModemManager is inhibited for it, so ModemManager never takes the modem
 * again, and at every attach both reach for the control port before the
 * inhibition holds (field 2026-10-13, T99: uqmi and ModemManager on cdc-wdm0,
 * the session up and down). When such a modem has no connection and
 * ModemManager answers, the engine takes its native interface down and lifts
 * the inhibition; once ModemManager has taken the modem, a bring-up moves the
 * interface to proto modemmanager. One try per attachment; a connected native
 * session is never broken for it. An operator's backend 'native' is left alone.
 */
#define MM_RETURN_WAIT_MS 300000
static json_object *mm_return;		/* device_id -> {generation, since, iface} */
static json_object *mm_return_tried;	/* "<id>|<generation>" -> true */

static bool mm_installed(void)
{
	return access(mm_init_path, X_OK) == 0;
}

static bool mm_returning(const char *id)
{
	return mm_return && json_object_object_get(mm_return, id);
}

/* ModemManager may take the modem now: lift the inhibition, bring it up once it has it */
static void mm_return_wait(const char *id, long long generation)
{
	json_object *ret = json_object_new_object();

	if (!mm_return)
		mm_return = json_object_new_object();
	json_object_object_add(ret, "generation", json_object_new_int64(generation));
	json_object_object_add(ret, "since", json_object_new_int64(now_mono_ms()));
	json_object_object_add(mm_return, id, ret);
}

static void mm_return_sync(void)
{
	long long now = now_mono_ms();
	json_object *gone = json_object_new_array();
	size_t i;

	if (!registry.devices || !strcmp(mm_backend, "disabled")) {
		json_object_put(gone);
		return;
	}
	if (!mm_return)
		mm_return = json_object_new_object();
	if (!mm_return_tried)
		mm_return_tried = json_object_new_object();
	json_object_object_foreach(registry.devices, id, rec) {
		/* borrowed: mm_cfgtxn_ledger() hands a reference out, and this loop never gave it back, so every ledger a
		 * bring-up or a setting replaced stayed allocated (valgrind, 2026-10-17); the walk changes no ledger before
		 * its last use of this one */
		json_object *led = json_object_object_get(json_object_object_get(cfg.ledger, "devices"), id), *dec = decision_of(id);
		json_object *ret = json_object_object_get(mm_return, id), *srv;
		const char *b = jstr(led, "backend"), *choice = jstr(dec, "backend"), *iface = jstr(led, "interface"), *state, *control;
		long long gen = json_object_get_int64(json_object_object_get(rec, "generation"));
		char key[200];

		/* one try per attachment and choice: the operator's new choice tries again */
		snprintf(key, sizeof(key), "%s|%lld|%s", id, gen, choice ? choice : "auto");
		if (ret) {
			/* waiting for ModemManager: a bring-up once it has the modem, the native interface back if it never comes */
			srv = served(rec);
			control = jstr(json_object_object_get(srv, "owner"), "control");
			if (!json_object_get_boolean(json_object_object_get(rec, "present")) ||
			    json_object_get_int64(json_object_object_get(ret, "generation")) != gen) {
				json_object_array_add(gone, json_object_new_string(id));
			} else if (control && !strcmp(control, "modemmanager") && !job.active && !reload_running && !pending_release.active) {
				json_object *o = json_object_new_object();
				bool ok = bringup_go(rec, o);

				ULOG_INFO("%s: ModemManager has the modem again: bring-up %s\n", id, ok ? "started" : jstr(o, "result"));
				mm_history_add(&history, id, (long long)time(NULL), now, "backend-return", ok ? "ModemManager took the modem" : "bring-up refused");
				history_dirty = true;
				json_object_put(o);
				json_object_array_add(gone, json_object_new_string(id));
			} else if (now - json_object_get_int64(json_object_object_get(ret, "since")) > MM_RETURN_WAIT_MS) {
				ULOG_WARN("%s: ModemManager did not take the modem; the native interface again\n", id);
				mm_history_add(&history, id, (long long)time(NULL), now, "backend-return", "ModemManager did not take the modem");
				history_dirty = true;
				if (jstr(ret, "iface"))
					json_object_put(netifd_call(jstr(ret, "iface"), "up"));
				json_object_array_add(gone, json_object_new_string(id));
			}
			json_object_put(srv);
			continue;
		}
		/* auto, or the operator's choice of ModemManager (field 2026-10-13: «ModemManager» chosen while the ledger
		 * said native kept uqmi, the inhibition kept ModemManager away, the choice could never take) */
		bool want_mm = choice && !strcmp(choice, "modemmanager");

		if (!b || strcmp(b, "native") || (choice && *choice && strcmp(choice, "auto") && !want_mm) || !iface || frozen(id) ||
		    !json_object_get_boolean(json_object_object_get(rec, "present")) || !mm_installed() ||
		    strcmp(mm_backend, "available") || json_object_object_get(mm_return_tried, key))
			continue;
		/* a model ModemManager cannot dial (its profile's next owner is the engine: FM350) is native under auto on
		 * purpose, not for want of ModemManager: handing it back would only bring it back here */
		if (!want_mm && jstr(json_object_object_get(rec, "profile"), "fallback_owner") &&
		    !strcmp(jstr(json_object_object_get(rec, "profile"), "fallback_owner"), "native"))
			continue;
		/* nor one whose profile puts the engine first (FM350: owner_priority native, qmodem, modemmanager): handed back,
		 * ModemManager found «not supported by any plugin» and the engine took it again, every recovery a tug of war
		 * (the owner, 2026-11-16: «что за драка») */
		if (!want_mm && jstr(json_object_object_get(rec, "profile"), "prefer_owner") &&
		    !strcmp(jstr(json_object_object_get(rec, "profile"), "prefer_owner"), "native"))
			continue;
		state = jstr(sessions ? json_object_object_get(sessions, id) : NULL, "state");
		/* a working auto session is not broken for it; the operator's choice is */
		if ((!want_mm && state && !strcmp(state, "connected")) || (job.active && !strcmp(job.device_id, id)) || rec_busy(id) ||
		    esim_reserved(id))
			continue;
		json_object_object_add(mm_return_tried, key, json_object_new_boolean(1));
		ret = json_object_new_object();
		json_object_object_add(ret, "generation", json_object_new_int64(gen));
		json_object_object_add(ret, "since", json_object_new_int64(now));
		json_object_object_add(ret, "iface", json_object_new_string(iface));
		json_object_object_add(mm_return, id, ret);
		ULOG_WARN("%s: native interface %s chosen while ModemManager did not answer; handing the modem back to ModemManager\n", id, iface);
		mm_history_add(&history, id, (long long)time(NULL), now, "backend-return", "native interface down, ModemManager may take the modem");
		history_dirty = true;
		json_object_put(netifd_call(iface, "down"));
	}
	for (i = 0; i < json_object_array_length(gone); i++)
		json_object_object_del(mm_return, json_object_get_string(json_object_array_get_idx(gone, i)));
	if (json_object_array_length(gone) || json_object_object_length(mm_return))
		inhibits_sync();
	json_object_put(gone);
}

/*
 * What the uplink tick needs of a device: its session (as served() picks it: the engine's, else ModemManager's),
 * ModemManager's view for the SIM card and the engine's own SIM reading for the quota. Not a whole served() for
 * every modem every 5 s, with its file checks and views (audit 2026-10-20).
 */
/*
 * The web pages of modems in router mode through the router's own address (the owner, 2026-10-20: a PC with a
 * proxy client in TUN mode sent 192.168.8.1 into its tunnel; the router's LAN address it reaches). Each such modem
 * with a session gets a port of its own, the same across restarts (from its id), and web-forward builds one nftables
 * table of DNATs from the LAN. core.web_forward '0' turns it off.
 */
#define WEBFWD_BASE 8081
#define WEBFWD_SPAN 19
static char webfwd_applied[1024] = "?";	/* "id port host dev\n" per modem, as the helper last ran; "?" clears leftovers */
static char webfwd_planned[1024];	/* the same for every modem that may have one: its port whether open or not */
static struct uloop_process webfwd_proc;
static bool webfwd_running;

static bool webfwd_enabled(void)
{
	json_object *c = core_uci();

	return !(c && jstr(c, "web_forward") && !strcmp(jstr(c, "web_forward"), "0"));
}

/* a modem that is itself a router with a web page: HiLink, UFI; not a phone (it has no page) */
static void webfwd_note(json_object *rec, json_object *st, json_object *sess)
{
	json_object *prof = json_object_object_get(rec, "profile");
	const char *prefer = jstr(prof, "prefer_owner"), *pid = jstr(prof, "id") ? jstr(prof, "id") : "";
	const char *gw = jstr(json_object_object_get(sess, "netifd"), "gateway4");

	char first[24];
	json_object *a4 = json_object_object_get(json_object_object_get(sess, "netifd"), "ipv4_addresses");

	/* a UFI whose SIM has no data gives a lease without a gateway: its web is the first address of the subnet
	 * (the owner's UZ801, 2026-10-30: «и веб ярлычок тоже») */
	if ((!gw || !*gw) && json_object_is_type(a4, json_type_array) && json_object_array_length(a4))
		gw = mm_subnet_first_host(json_object_get_string(json_object_array_get_idx(a4, 0)), first, sizeof(first));
	if (prefer && !strcmp(prefer, "hilink") && strncmp(pid, "phone-", 6) && gw && *gw && strspn(gw, "0123456789.") == strlen(gw))
		json_object_object_add(st, "web_host", json_object_new_string(gw));
	else
		json_object_object_del(st, "web_host");
}

static long long webfwd_ttl_ms(void)
{
	json_object *c = core_uci();
	const char *m = jstr(c, "web_forward_minutes");
	long long v = m && *m && strspn(m, "0123456789") == strlen(m) ? atoll(m) : 15;

	return v > 1440 ? 1440 * 60000LL : v * 60000LL;
}

/* how long this modem's forward stays open, in ms; -1 always, 0 closed */
static long long webfwd_left(const char *id)
{
	long long ttl = webfwd_ttl_ms(), until;

	if (!ttl)
		return -1;
	until = json_object_get_int64(json_object_object_get(webfwd_open, id));
	return until > now_mono_ms() ? until - now_mono_ms() : 0;
}

static int webfwd_cmp(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static char webfwd_pending[1024];	/* what the running helper applies: «applied» only once it says so */
static long long webfwd_retry_at;	/* a failed apply is tried again from here (30 s) */

static void webfwd_exited(struct uloop_process *p, int ret)
{
	(void)p;
	webfwd_running = false;
	/* applied when nft took it (second audit 2026-10-21: a failure was taken for done and never tried again) */
	if (!ret) {
		snprintf(webfwd_applied, sizeof(webfwd_applied), "%s", webfwd_pending);
		return;
	}
	ULOG_WARN("web-forward: the modem web pages could not be forwarded (status %d); tried again in 30 s\n", ret);
	webfwd_retry_at = now_mono_ms() + (uplink_override_ms ? 5 * uplink_override_ms : 30000);
}

/* the port a modem's page has through the router (open or not), 0 for none */
static int webfwd_port(const char *id)
{
	char key[200];
	const char *at;

	snprintf(key, sizeof(key), "%s ", id);
	for (at = webfwd_planned; (at = strstr(at, key)); at++)
		if (at == webfwd_planned || at[-1] == '\n')
			return atoi(at + strlen(key));
	return 0;
}

/* another modem's page planned at the same address (two HiLinks both at 192.168.8.1): the router reaches only one of
 * them there, whichever its route to that address goes through, so both ports open the same page (second audit
 * 2026-10-21, R03) */
static bool webfwd_shared(const char *id)
{
	char host[64] = "", other[64];
	const char *line;

	for (line = webfwd_planned; *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : line + strlen(line)) {
		size_t n = strlen(id);

		if (!strncmp(line, id, n) && line[n] == ' ' && sscanf(line + n, " %*d %63s", host) == 1)
			break;
	}
	if (!*host)
		return false;
	for (line = webfwd_planned; *line; line = strchr(line, '\n') ? strchr(line, '\n') + 1 : line + strlen(line)) {
		size_t n = strlen(id);

		if (!strncmp(line, id, n) && line[n] == ' ')
			continue;
		if (sscanf(line, "%*s %*d %63s", other) == 1 && !strcmp(other, host))
			return true;
	}
	return false;
}

static void webfwd_turn(void)
{
	const char *ids[16], *slash = strrchr(route_helper, '/'), *argv[4];
	char want[sizeof(webfwd_applied)] = "", plan[sizeof(webfwd_planned)] = "", path[PATH_MAX], spec[PATH_MAX];
	bool taken[WEBFWD_SPAN] = { false };
	size_t used = 0, planned = 0;
	int n = 0, i;
	FILE *f;
	pid_t pid;

	if (webfwd_running)
		return;
	if (uplinks && webfwd_enabled()) {
		json_object_object_foreach(uplinks, id, st) {
			if (strncmp(id, "dev:", 4) && jstr(st, "web_host") && jstr(st, "dev") && n < 16)
				ids[n++] = id;
		}
	}
	/* the ports from the ids, sorted, so the same modems get the same ports whatever the order they came in */
	qsort(ids, (size_t)n, sizeof(ids[0]), webfwd_cmp);
	for (i = 0; i < n; i++) {
		json_object *st = json_object_object_get(uplinks, ids[i]);
		unsigned int h = 2166136261u;
		const char *c;
		int k;

		for (c = ids[i]; *c; c++)
			h = (h ^ (unsigned char)*c) * 16777619u;
		for (k = 0; k < WEBFWD_SPAN && taken[(h + (unsigned)k) % WEBFWD_SPAN]; k++)
			;
		if (k == WEBFWD_SPAN)
			break;
		taken[(h + (unsigned)k) % WEBFWD_SPAN] = true;
		if (planned + strlen(ids[i]) + 64 < sizeof(plan))
			planned += (size_t)snprintf(plan + planned, sizeof(plan) - planned, "%s %d %s %s\n", ids[i],
						    WEBFWD_BASE + (int)((h + (unsigned)k) % WEBFWD_SPAN), jstr(st, "web_host"), jstr(st, "dev"));
		if (!webfwd_left(ids[i]))
			continue;	/* not asked for, or its time is over */
		if (used + strlen(ids[i]) + 64 < sizeof(want))
			used += (size_t)snprintf(want + used, sizeof(want) - used, "%s %d %s %s\n", ids[i],
						 WEBFWD_BASE + (int)((h + (unsigned)k) % WEBFWD_SPAN), jstr(st, "web_host"), jstr(st, "dev"));
	}
	snprintf(webfwd_planned, sizeof(webfwd_planned), "%s", plan);
	if (!strcmp(want, webfwd_applied) || now_mono_ms() < webfwd_retry_at)
		return;
	snprintf(spec, sizeof(spec), "%s/web-forward.spec", run_dir);
	mkdir(run_dir, 0755);
	if (!(f = fopen(spec, "w")))
		return;
	/* the helper takes "port host dev": the id stays here */
	for (const char *line = want; *line; ) {
		const char *end = strchr(line, '\n'), *sp = strchr(line, ' ');

		if (!end || !sp || sp > end)
			break;
		fprintf(f, "%.*s\n", (int)(end - sp - 1), sp + 1);
		line = end + 1;
	}
	fclose(f);
	snprintf(path, sizeof(path), "%.*sweb-forward", slash ? (int)(slash - route_helper + 1) : 0, route_helper);
	argv[0] = path;
	argv[1] = "apply";
	argv[2] = spec;
	argv[3] = NULL;
	if ((pid = failover_exec(argv)) > 0) {
		webfwd_running = true;
		webfwd_proc.pid = pid;
		webfwd_proc.cb = webfwd_exited;
		uloop_process_add(&webfwd_proc);
		snprintf(webfwd_pending, sizeof(webfwd_pending), "%s", want);
	}
}

static json_object *uplink_srv(json_object *rec, const char *id)
{
	json_object *o = json_object_new_object(), *mm = mm_devices ? json_object_object_get(mm_devices, id) : NULL, *e;

	if (mm)
		json_object_object_add(o, "modemmanager", json_object_get(mm));
	if (sessions && json_object_object_get(sessions, id)) {
		json_object_object_add(o, "session", json_object_get(json_object_object_get(sessions, id)));
	} else if (mm) {
		json_object *mms = session_from_mm(mm);

		if (mms)
			json_object_object_add(o, "session", mms);
	}
	e = sim_entry(id);
	if (e && jstr(e, "iccid") && json_object_get_int64(json_object_object_get(e, "generation")) ==
				     json_object_get_int64(json_object_object_get(rec, "generation"))) {
		json_object *s = json_object_new_object();

		json_object_object_add(s, "iccid", json_object_new_string(jstr(e, "iccid")));
		json_object_object_add(o, "sim", s);
	}
	return o;
}

/* a modem's link this pass: the day's downtime and the SMS alerts */
static void link_note(const char *id, const char *state, const char *day)
{
	downtime_note(id, state, day);
	alert_note(id, state);
}

static void uplink_tick(struct uloop_timeout *t)
{
	long long now = now_mono_ms();
	char day[16], month[8];
	time_t wall = time(NULL);
	struct tm tm;
	bool modem_default = false;

	(void)t;
	localtime_r(&wall, &tm);
	strftime(day, sizeof(day), "%Y-%m-%d", &tm);
	strftime(month, sizeof(month), "%Y-%m", &tm);
	read_default_dev();
	/* a router-mode modem moved to another port: its name follows without ModemManager's reads too (at most every 30 s) */
	{
		static long long id_sync_ms;

		if (!id_sync_ms || now - id_sync_ms >= (uplink_override_ms ? 5 * uplink_override_ms : 30000)) {
			id_sync_ms = now;
			identity_sync();
		}
	}
	quota_pass_begin();
	signal_mgr_tick();
	qmodem_stale_sync();
	qmodem_port_sync();
	mm_return_sync();
	if (!usb_power_ms || now - usb_power_ms >= (uplink_override_ms ? uplink_override_ms : USB_POWER_EVERY_MS)) {
		usb_power_scan();
		usb_power_ms = now;
	}
	if (registry.devices) {
		json_object_object_foreach(registry.devices, id, rec) {
			json_object *srv, *sess, *st, *traffic;
			const char *dev, *since;
			long long rx, tx;
			bool restart;

			if (!json_object_get_boolean(json_object_object_get(rec, "present"))) {
				if (uplinks && (st = json_object_object_get(uplinks, id))) {
					json_object_object_del(st, "dev");
					json_object_object_del(st, "reach");
				}
				link_note(id, NULL, day);
				continue;
			}
			srv = uplink_srv(rec, id);
			sess = json_object_object_get(srv, "session");
			dev = session_dev(sess);
			st = uplink_state(id);
			if (!dev || !jstr(sess, "state") || strcmp(jstr(sess, "state"), "connected")) {
				json_object_object_del(st, "dev");
				json_object_object_del(st, "reach");
				/* no session: an outage only for a modem with an interface that should have one (not a frozen one) */
				link_note(id, ledger_backend(id) && !frozen(id) ? "off" : NULL, day);
				json_object_put(srv);
				continue;
			}
			since = json_object_get_string(json_object_object_get(sess, "since"));
			restart = !jstr(st, "dev") || strcmp(jstr(st, "dev"), dev) ||
				  strcmp(since ? since : "", jstr(st, "since") ? jstr(st, "since") : "");
			if (restart) {
				json_object_object_add(st, "dev", json_object_new_string(dev));
				json_object_object_add(st, "since", json_object_new_string(since ? since : ""));
				json_object_object_del(st, "reach");
				json_object_object_add(st, "next", json_object_new_int64(now));
				{
					/* checked as a plain device before ModemManager named it: keep that answer */
					char key[IFNAMSIZ + 8];
					json_object *was;

					snprintf(key, sizeof(key), "dev:%s", dev);
					was = uplinks ? json_object_object_get(uplinks, key) : NULL;
					if (was && json_object_object_get(was, "reach")) {
						json_object_object_add(st, "reach", json_object_get(json_object_object_get(was, "reach")));
						json_object_object_add(st, "next", json_object_get(json_object_object_get(was, "next")));
					}
				}
			}
			if (!(traffic = json_object_object_get(st, "traffic"))) {
				traffic = json_object_new_object();
				json_object_object_add(st, "traffic", traffic);
			}
			if (read_ll(dev, "statistics/rx_bytes", &rx) && read_ll(dev, "statistics/tx_bytes", &tx)) {
				/* the month bucket's growth is this sample's bytes (a new month starts from zero) */
				json_object *mb = json_object_object_get(traffic, "month");
				long long before = mb && jstr(mb, "key") && !strcmp(jstr(mb, "key"), month) ?
						   json_object_get_int64(json_object_object_get(mb, "rx_bytes")) +
						   json_object_get_int64(json_object_object_get(mb, "tx_bytes")) : 0;

				mm_traffic_sample(traffic, rx, tx, now, restart, day, month);
				traffic_mark_from(traffic);
				if (!restart)
					traffic_sample(id, traffic, (long long)time(NULL));	/* spec §42 */
				mb = json_object_object_get(traffic, "month");
				quota_count(id, srv, dev, json_object_get_int64(json_object_object_get(mb, "rx_bytes")) +
						      json_object_get_int64(json_object_object_get(mb, "tx_bytes")) - before, day);
			}
			if (!strcmp(dev, default_dev))
				modem_default = true;
			if (now >= json_object_get_int64(json_object_object_get(st, "next")) && !reach_running(id))
				reach_start(id, dev);
			{
				const char *lv = jstr(json_object_object_get(st, "reach"), "level");

				link_note(id, !lv ? NULL : !strcmp(lv, "ok") ? "ok" : !strcmp(lv, "limited") ? "limited" : "none", day);
			}
			dnschk_turn(id, st, sess, dev);
			webfwd_note(rec, st, sess);
			json_object_put(srv);
		}
	}
	quota_pass_end();
	/* the default route goes through something else (WAN, Wi-Fi): is the router online? */
	if (uplinks) {
		json_object_object_foreach(uplinks, key, st) {
			if (!strncmp(key, "dev:", 4) && (modem_default || strcmp(key + 4, default_dev)) &&
			    !(failover_active() && failover_watches(key + 4)))
				json_object_object_del(uplinks, key);
			(void)st;
		}
	}
	if (*default_dev && !modem_default) {
		char key[IFNAMSIZ + 8];
		json_object *st;

		snprintf(key, sizeof(key), "dev:%s", default_dev);
		st = uplink_state(key);
		if (!jstr(st, "dev")) {
			json_object_object_add(st, "dev", json_object_new_string(default_dev));
			json_object_object_add(st, "next", json_object_new_int64(now));
		}
		dev_traffic(st, default_dev, now, day, month);
		if (now >= json_object_get_int64(json_object_object_get(st, "next")) && !reach_running(key))
			reach_start(key, default_dev);
	}
	/* failover checks every default route, a modem's or not (spec 20.8) */
	if (failover_active() && fo_routes) {
		size_t i;

		for (i = 0; i < json_object_array_length(fo_routes); i++) {
			const char *dev = jstr(json_object_array_get_idx(fo_routes, i), "dev");
			char key[IFNAMSIZ + 8];
			bool modem = false;
			json_object *st;

			if (uplinks) {
				json_object_object_foreach(uplinks, k2, s2) {
					modem = modem || (strncmp(k2, "dev:", 4) && jstr(s2, "dev") && !strcmp(jstr(s2, "dev"), dev));
				}
			}
			if (modem || strlen(dev) > IFNAMSIZ)
				continue;
			snprintf(key, sizeof(key), "dev:%s", dev);
			st = uplink_state(key);
			if (!jstr(st, "dev")) {
				json_object_object_add(st, "dev", json_object_new_string(dev));
				json_object_object_add(st, "next", json_object_new_int64(now));
			}
			/* the default device was sampled above already */
			if (strcmp(dev, default_dev) || modem_default)
				dev_traffic(st, dev, now, day, month);
			if (now >= json_object_get_int64(json_object_object_get(st, "next")) && !reach_running(key))
				reach_start(key, dev);
		}
	}
	ttl_turn();
	webfwd_turn();
	ready_turn();
	route_repair();
	failover_turn();
	alert_tick(default_dev);
	wlmode_tick(carrier_level());
	proxy_guard_turn();
	radio_mgr_tick();
	usbconf_sync();		/* a choice saved, or a recovery or bring-up over: no uevent says so */
	if (now - uplink_saved_ms >= UPLINK_SAVE_MS) {
		uplink_save();
		uplink_saved_ms = now;
	}
	uplink_timer.cb = uplink_tick;
	uloop_timeout_set(&uplink_timer, uplink_period(UPLINK_SAMPLE_MS));
}

/* the checked uplinks, each device once, and how many of them fail */
static void uplinks_count(int *checked, int *failing)
{
	const char *seen[UPLINK_JOBS * 2];
	int n = 0, down = 0, i;
	bool dup;

	json_object_object_foreach(uplinks, key, st) {
		json_object *r = json_object_object_get(st, "reach");

		(void)key;
		if (!r || !jstr(st, "dev") || n >= (int)(sizeof(seen) / sizeof(seen[0])))
			continue;
		for (i = 0, dup = false; i < n; i++)
			dup = dup || !strcmp(seen[i], jstr(st, "dev"));
		if (dup)	/* one device seen as a modem's and as a plain default (boot) */
			continue;
		seen[n++] = jstr(st, "dev");
		down += jstr(r, "level") && !strcmp(jstr(r, "level"), "down");
	}
	*checked = n;
	*failing = down;
}

/* ---- the proxy guard (spec 20.8): direct checks blocked, the internet there through the local proxy ---- */

#define PROXY_GUARD_MS 300000	/* one probe per five minutes at most, its answer good for twice that */
static struct uloop_process pg_proc;
static bool pg_running, pg_ok;
static long long pg_at = -PROXY_GUARD_MS;

/* on unless core.proxy_guard '0'; core.proxy_guard_proxy host:port, else a local clash/mihomo's port */
static bool proxy_guard_enabled(void)
{
	json_object *c = core_uci();

	return !(c && jstr(c, "proxy_guard") && !strcmp(jstr(c, "proxy_guard"), "0"));
}

static void proxy_guard_exited(struct uloop_process *p, int ret)
{
	bool was = pg_ok;

	(void)p;
	pg_running = false;
	pg_at = now_mono_ms();
	pg_ok = ret == 0;
	if (pg_ok && !was)
		ULOG_WARN("every direct check fails, but the internet answers through the local proxy: "
			  "the checks look firewall-blocked, no modem is recovered for it\n");
}

/* every checked uplink fails: ask through the local proxy, now and then */
static void proxy_guard_turn(void)
{
	const char *argv[3 + 8], *slash = strrchr(route_helper, '/');
	char path[PATH_MAX];
	json_object *c = core_uci(), *t = reach_targets();
	int n, down, k = 0;
	size_t i;
	pid_t pid;

	if (!uplinks || pg_running || !proxy_guard_enabled())
		return;
	uplinks_count(&n, &down);
	if (n < 1 || down < n) {
		pg_ok = false;
		return;
	}
	if (now_mono_ms() - pg_at < PROXY_GUARD_MS)
		return;
	snprintf(path, sizeof(path), "%.*sproxy-probe", slash ? (int)(slash - route_helper + 1) : 0, route_helper);
	argv[k++] = path;
	argv[k++] = c && jstr(c, "proxy_guard_proxy") ? jstr(c, "proxy_guard_proxy") : "auto";
	for (i = 0; i < json_object_array_length(t) && k < 10; i++)
		argv[k++] = json_object_get_string(json_object_array_get_idx(t, i));
	argv[k] = NULL;
	if ((pid = failover_exec(argv)) > 0) {
		pg_running = true;
		pg_proc.pid = pid;
		pg_proc.cb = proxy_guard_exited;
		uloop_process_add(&pg_proc);
	}
}

/*
 * ---- the internet through the tunnel (the owner 2026-10-23): under an operator's whitelist the carrier is «limited»,
 * yet podkop's VLESS (or another tunnel) on an allowed address carries everything else. tunnel-probe asks the tunnel's
 * own controller once a minute while the carrier is limited; the uplink reply says «works through the tunnel» ----
 */
#ifndef TUNNEL_EVERY_MS
#define TUNNEL_EVERY_MS 60000
#endif
static struct {
	bool running;
	long long at;		/* the last answer, monotonic ms; -1 none */
	char out[PATH_MAX];
	struct uloop_process proc;
} tun = { .at = -1 };
static json_object *tun_result;	/* {ok, via, outbound?, delay_ms?} of the last probe */
static long long tun_every = TUNNEL_EVERY_MS;	/* five times that when the probe runs sing-box's own fetch */

static void tunnel_exited(struct uloop_process *p, int ret)
{
	json_object *r = json_object_from_file(tun.out);
	bool was = tun_result && json_object_get_boolean(json_object_object_get(tun_result, "ok"));

	(void)p;
	tun.running = false;
	unlink(tun.out);
	json_object_put(tun_result);
	tun_result = NULL;
	if (WIFEXITED(ret) && WEXITSTATUS(ret) <= 1 && r && json_object_is_type(r, json_type_object)) {
		tun_result = r;
		r = NULL;
		if (json_object_get_boolean(json_object_object_get(tun_result, "ok")) && !was)
			ULOG_INFO("tunnel: the internet answers through %s (%s)\n", jstr(tun_result, "via") ? jstr(tun_result, "via") : "?",
				  jstr(tun_result, "outbound") ? jstr(tun_result, "outbound") : "?");
	}
	json_object_put(r);
	tun.at = now_mono_ms();
	tun_every = tun_result && jstr(tun_result, "via") && !strcmp(jstr(tun_result, "via"), "sing-box-fetch") ?
		    5 * TUNNEL_EVERY_MS : TUNNEL_EVERY_MS;
}

static void tunnel_turn(const char *level)
{
	const char *slash = strrchr(route_helper, '/');
	char path[PATH_MAX];
	long long now = now_mono_ms();
	pid_t pid;

	if (!level || strcmp(level, "limited")) {
		/* a carrier with the whole internet, or none: nothing to say about a tunnel */
		json_object_put(tun_result);
		tun_result = NULL;
		tun.at = -1;
		return;
	}
	if (tun.running || (tun.at >= 0 && now - tun.at < tun_every))
		return;
	snprintf(path, sizeof(path), "%.*stunnel-probe", slash ? (int)(slash - route_helper + 1) : 0, route_helper);
	snprintf(tun.out, sizeof(tun.out), "%s/tunnel.json", run_dir);
	mkdir(run_dir, 0755);
	pid = fork();
	if (pid == 0) {
		int fd = open(tun.out, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);

		setpgid(0, 0);
		if (fd < 0)
			_exit(127);
		dup2(fd, 1);
		fd = open("/dev/null", O_RDWR);
		if (fd >= 0) {
			dup2(fd, 0);
			dup2(fd, 2);
		}
		for (fd = 3; fd < 1024; fd++)
			close(fd);
		execl(path, path, (char *)NULL);
		_exit(127);
	}
	if (pid < 0) {
		tun.at = now;
		return;
	}
	tun.running = true;
	tun.proc.pid = pid;
	tun.proc.cb = tunnel_exited;
	uloop_process_add(&tun.proc);
}

/* the uplink reply's tunnel block: a fresh answer only (three probes' time) */
static json_object *tunnel_view(void)
{
	json_object *o;

	if (!tun_result || tun.at < 0 || now_mono_ms() - tun.at > 3 * tun_every)
		return NULL;
	o = json_object_get(tun_result);
	return o;
}

/*
 * None of the checked uplinks answers: two or more (R5), or one while the
 * internet answers through the local proxy (the proxy guard): the targets,
 * the upstream or a firewall, not a modem.
 */
static bool uplinks_all_failing(void)
{
	int n, down;

	if (!uplinks)
		return false;
	uplinks_count(&n, &down);
	return n >= 1 && down == n && (n >= 2 || (pg_ok && now_mono_ms() - pg_at < 2 * PROXY_GUARD_MS));
}

static json_object *reach_view(json_object *reach)
{
	json_object *r;

	if (!reach)
		return NULL;
	r = json_object_new_object();
	json_object_object_foreach(reach, k, v)
		if (strcmp(k, "mono") && strcmp(k, "redirect_asked"))
			json_object_object_add(r, k, json_object_get(v));
	json_object_object_add(r, "age_s", json_object_new_int64(
		(now_mono_ms() - json_object_get_int64(json_object_object_get(reach, "mono"))) / 1000));
	return r;
}

/* the uplink part of a served device: device, role, check, traffic, network */
static json_object *uplink_view_of(const char *id, json_object *sess)
{
	json_object *st = uplinks ? json_object_object_get(uplinks, id) : NULL, *o = json_object_new_object(), *net;
	json_object *n = json_object_object_get(sess, "netifd");
	const char *dev = jstr(st, "dev") ? jstr(st, "dev") : session_dev(sess);
	const char *role = mm_uplink_role(jstr(sess, "state"), dev, default_dev, json_object_object_get(st, "reach"));
	long long mtu;

	json_object_object_add(o, "device", dev ? json_object_new_string(dev) : NULL);
	json_object_object_add(o, "role", json_object_new_string(role));
	/* a check of a session that is gone says nothing (the next sample drops it) */
	json_object_object_add(o, "reach", strcmp(role, "down") ? reach_view(json_object_object_get(st, "reach")) : NULL);
	if (strcmp(role, "down") && json_object_object_get(st, "dns"))
		json_object_object_add(o, "dns", json_object_get(json_object_object_get(st, "dns")));
	if (strcmp(role, "down") && json_object_object_get(st, "dns_check")) {
		json_object *c = json_object_object_get(st, "dns_check"), *v = json_object_new_object();

		json_object_object_foreach(c, k, val)
			if (strcmp(k, "since"))
				json_object_object_add(v, k, json_object_get(val));
		json_object_object_add(o, "dns_check", v);
	}
	if (json_object_object_get(st, "traffic"))
		json_object_object_add(o, "traffic", mm_traffic_view(json_object_object_get(st, "traffic")));
	/* the carrier under a whitelist with a tunnel over it that answers: said on the modem too, not «limited» alone (the
	 * owner, 2026-10-28: «limited access … но инет работает через подкоп, и этого не увидел в интерфейсе») */
	if (!strcmp(role, "limited")) {
		json_object *t = tunnel_view();

		if (t && json_object_get_boolean(json_object_object_get(t, "ok"))) {
			json_object *v = json_object_new_object();

			json_object_object_add(v, "ok", json_object_new_boolean(1));
			if (jstr(t, "via"))
				json_object_object_add(v, "via", json_object_new_string(jstr(t, "via")));
			if (jstr(t, "outbound"))
				json_object_object_add(v, "outbound", json_object_new_string(jstr(t, "outbound")));
			json_object_object_add(o, "tunnel", v);
		}
		json_object_put(t);
	}
	net = json_object_new_object();
	json_object_object_add(net, "default_route", json_object_new_boolean(dev && !strcmp(dev, default_dev)));
	if (dev && read_ll(dev, "mtu", &mtu))
		json_object_object_add(net, "mtu", json_object_new_int64(mtu));
	if (jstr(n, "gateway") || jstr(n, "gateway4") || jstr(n, "gateway6"))
		json_object_object_add(net, "gateway", json_object_new_string(
			jstr(n, "gateway") ? jstr(n, "gateway") : jstr(n, "gateway4") ? jstr(n, "gateway4") : jstr(n, "gateway6")));
	if (json_object_object_get(n, "dns"))
		json_object_object_add(net, "dns", json_object_get(json_object_object_get(n, "dns")));
	if (json_object_object_get(n, "metric"))
		json_object_object_add(net, "metric", json_object_get(json_object_object_get(n, "metric")));
	json_object_object_add(o, "network", net);
	return o;
}

/*
 * The modem's connection settings for the page (spec 25.9.10): which
 * interface carries it, whether the engine manages it or it is the user's
 * (the image's "modem", one made by hand: it can be handed over, option
 * adopt), the APN and IP type set, and an APN suggestion for the SIM's
 * network from profiles/apn.json.
 */
static struct { char *db, *overrides; struct timespec db_mtime; char db_path[PATH_MAX]; } apn_data;
static json_object *apn_cache;		/* device_id -> {"key", "pick"} */

static char *slurp(const char *path);

/* the APN data: a database fetched by apn-update (state dir) before the packaged one; re-read when it changes */
static void apn_load(void)
{
	char etc[PATH_MAX], pkg[PATH_MAX];
	const char *path;
	struct stat st;

	snprintf(etc, sizeof(etc), "%s/providers.tsv", state_dir);
	snprintf(pkg, sizeof(pkg), "%s/providers.tsv", APN_DIR);
	path = !stat(etc, &st) && st.st_size > 0 ? etc : pkg;
	if (stat(path, &st)) {
		memset(&st, 0, sizeof(st));
	}
	if (!apn_data.db || strcmp(path, apn_data.db_path) || st.st_mtim.tv_sec != apn_data.db_mtime.tv_sec ||
	    st.st_mtim.tv_nsec != apn_data.db_mtime.tv_nsec) {
		free(apn_data.db);
		apn_data.db = slurp(path);
		apn_data.db_mtime = st.st_mtim;
		snprintf(apn_data.db_path, sizeof(apn_data.db_path), "%s", path);
		json_object_put(apn_cache);
		apn_cache = NULL;
	}
	if (!apn_data.overrides) {
		snprintf(pkg, sizeof(pkg), "%s/overrides.list", APN_DIR);
		apn_data.overrides = slurp(pkg);
	}
}

static json_object *apn_suggest(const char *id, json_object *mm)
{
	json_object *card = json_object_object_get(mm, "sim_card"), *sec = sim_secret ? json_object_object_get(sim_secret, id) : NULL;
	struct mm_apn_sim sim = { jstr(card, "operator_code"), jstr(sec, "imsi"), jstr(card, "operator_name"), jstr(card, "gid1"),
				  jstr(sec, "iccid"), jstr(mm, "operator_code") };
	json_object *c, *pick;
	char key[512];

	apn_load();
	/* a new database clears the cache (apn_load) */
	snprintf(key, sizeof(key), "%.32s|%.20s|%.64s|%.40s|%.24s|%.16s", sim.home ? sim.home : "", sim.imsi ? sim.imsi : "",
		 sim.spn ? sim.spn : "", sim.gid1 ? sim.gid1 : "", sim.iccid ? sim.iccid : "", sim.network ? sim.network : "");
	if (!apn_cache)
		apn_cache = json_object_new_object();
	c = json_object_object_get(apn_cache, id);
	if (!c || !jstr(c, "key") || strcmp(jstr(c, "key"), key)) {
		c = json_object_new_object();
		json_object_object_add(c, "key", json_object_new_string(key));
		json_object_object_add(c, "pick", mm_apn_pick(apn_data.db, apn_data.overrides, &sim));
		json_object_object_add(apn_cache, id, c);
	}
	pick = json_object_object_get(c, "pick");
	return pick ? json_object_get(pick) : NULL;
}

static json_object *decision_of(const char *id)
{
	json_object *img = medvemodem_uci();

	if (!img || !id)
		return NULL;
	json_object_object_foreach(img, sec, s) {
		(void)sec;
		if (jstr(s, ".type") && !strcmp(jstr(s, ".type"), "device") && jstr(s, "id") && !strcmp(jstr(s, "id"), id))
			return s;
	}
	return NULL;
}

/* dnsmasq asks only its own servers, not the uplinks' (option noresolv: podkop points it at its sing-box, 127.0.0.42):
 * the DNS of a modem's session, the operator's or typed, does not reach the router's names. Read at most every 30 s */
static bool dnsmasq_noresolv(void)
{
	static long long at = -1;
	static bool on;
	long long now = now_mono_ms();

	if (at < 0 || now - at >= 30000) {
		static const char *const pkgs[] = { "dhcp", NULL };
		char err[160];
		json_object *img = mm_uci_read(conf_dir, pkgs, err, sizeof(err)), *dhcp = json_object_object_get(img, "dhcp");

		on = false;
		if (dhcp) {
			json_object_object_foreach(dhcp, sec, val) {
				(void)sec;
				if (jstr(val, ".type") && !strcmp(jstr(val, ".type"), "dnsmasq") && jstr(val, "noresolv") &&
				    !strcmp(jstr(val, "noresolv"), "1"))
					on = true;
			}
		}
		json_object_put(img);
		at = now;
	}
	return on;
}

static json_object *connection_of(const char *id, json_object *srv)
{
	json_object *c = json_object_new_object(), *led = mm_cfgtxn_ledger(&cfg, id), *dec = decision_of(id);
	json_object *netif = iface_checks ? json_object_object_get(iface_checks, id) : NULL, *first = NULL, *op;
	const char *iface;
	bool managed = jstr(led, "interface") != NULL;
	size_t i;

	for (i = 0; netif && i < json_object_array_length(netif); i++) {
		json_object *n = json_object_array_get_idx(netif, i);

		/* another modem's interface the engine runs, left on this modem's port, is not this modem's (2026-11-10: the
		 * M150-4 read the moved MF192's mm8 as its own: no «Подключить», no web chip) */
		if (json_object_get_boolean(json_object_object_get(n, "engine")) &&
		    !(managed && jstr(n, "name") && !strcmp(jstr(n, "name"), jstr(led, "interface"))))
			continue;
		if (!first || (managed && jstr(n, "name") && !strcmp(jstr(n, "name"), jstr(led, "interface"))))
			first = n;
	}
	iface = managed ? jstr(led, "interface") : jstr(first, "name");
	json_object_object_add(c, "interface", iface ? json_object_new_string(iface) : NULL);
	json_object_object_add(c, "managed", json_object_new_boolean(managed));
	/* the modem's priority is its interface's route metric: the lowest carries the traffic (spec 20.8) */
	if (iface && iface_metrics && json_object_object_get(iface_metrics, iface))
		json_object_object_add(c, "metric", json_object_get(json_object_object_get(iface_metrics, iface)));
	if (managed && iface) {
		char key[128];

		snprintf(key, sizeof(key), "network.%s", iface);
		json_object_object_add(c, "adopted", json_object_new_boolean(json_object_get_boolean(json_object_object_get(
			json_object_object_get(json_object_object_get(led, "sections"), key), "adopted"))));
	}
	if (!managed && jstr(first, "name"))
		json_object_object_add(c, "user_interface", json_object_new_string(jstr(first, "name")));
	else if (!managed && user_ifaces && jstr(user_ifaces, id))
		json_object_object_add(c, "user_interface", json_object_new_string(jstr(user_ifaces, id)));
	/* a session interface set up outside MedveModem (mgr_foreign.c): the modem is its until the operator hands it over */
	if (!managed && foreign_of(id)) {
		json_object *f = foreign_of(id), *v = json_object_new_object();

		json_object_object_add(v, "interface", json_object_new_string(jstr(f, "interface")));
		json_object_object_add(v, "proto", json_object_new_string(jstr(f, "proto")));
		if (json_object_get_boolean(json_object_object_get(f, "off")))
			json_object_object_add(v, "off", json_object_new_boolean(1));
		if (foreign_handed(id))
			json_object_object_add(v, "handed", json_object_new_boolean(1));
		json_object_object_add(c, "foreign", v);
		if (!jstr(first, "name"))
			json_object_object_add(c, "user_interface", json_object_new_string(jstr(f, "interface")));
	}
	/* unset is on only before the modem has an interface (the owner, 2026-10-17: a new modem takes its SIM's
	 * APN); a working setup, ours or the operator's, keeps what it has (review 2026-10-17) */
	json_object_object_add(c, "auto_connect", json_object_new_boolean(!jstr(dec, "auto_connect") || strcmp(jstr(dec, "auto_connect"), "0")));
	if (jstr(dec, "apn_auto") ? strcmp(jstr(dec, "apn_auto"), "0") != 0 : !iface) {
		/* the field is the administrator's: what they typed, never the APN taken from the SIM (which the
		 * interface carries), or a save would freeze it */
		json_object_object_add(c, "apn_auto", json_object_new_boolean(1));
		if (jstr(dec, "apn") && *jstr(dec, "apn"))
			json_object_object_add(c, "apn", json_object_new_string(jstr(dec, "apn")));
		if (jstr(first, "apn") && *jstr(first, "apn"))
			json_object_object_add(c, "apn_in_use", json_object_new_string(jstr(first, "apn")));
	} else if (jstr(dec, "apn") || jstr(first, "apn")) {
		json_object_object_add(c, "apn", json_object_new_string(jstr(dec, "apn") ? jstr(dec, "apn") : jstr(first, "apn")));
	}
	if (jstr(dec, "iptype") || jstr(first, "iptype"))
		json_object_object_add(c, "iptype", json_object_new_string(jstr(dec, "iptype") ? jstr(dec, "iptype") : jstr(first, "iptype")));
	{
		/* the DNS servers typed for this modem (option dns), used instead of the operator's */
		json_object *dns = mm_netcfg_manual_dns(dec);

		if (dns)
			json_object_object_add(c, "dns", dns);
		json_object_object_add(c, "dns_mode", json_object_new_string(jstr(dec, "dns_mode") && !strcmp(jstr(dec, "dns_mode"), "replace") ?
									      "replace" : "add"));
		if (dnsmasq_noresolv())
			json_object_object_add(c, "dns_bypassed", json_object_new_boolean(1));
	}
	/* the operator's choices the modem page shows: who drives the modem, the AT interface */
	json_object_object_add(c, "backend", json_object_new_string(jstr(dec, "backend") && *jstr(dec, "backend") ? jstr(dec, "backend") : "auto"));
	if (jstr(dec, "at_interface") && *jstr(dec, "at_interface"))
		json_object_object_add(c, "at_interface", json_object_new_string(jstr(dec, "at_interface")));
	{
		json_object *uc = usbconf_view(id, srv);

		if (uc)
			json_object_object_add(c, "usb_config", uc);
	}
	/* the SIM PIN kept for netifd (option pin -> pincode): only whether there is one, never the digits */
	json_object_object_add(c, "pin_saved", json_object_new_boolean(jstr(dec, "pin") && *jstr(dec, "pin")));
	/* the modem web's password (HiLink, ZTE): only whether there is one, for a web modem */
	if (jstr(json_object_object_get(srv, "owner"), "control") && !strcmp(jstr(json_object_object_get(srv, "owner"), "control"), "hilink"))
		json_object_object_add(c, "web_password_saved", json_object_new_boolean(jstr(dec, "web_password") && *jstr(dec, "web_password")));
	/* its web page through the router: the port on the router's own address (web-forward) */
	if (webfwd_port(id) > 0) {
		long long left = webfwd_left(id);

		json_object_object_add(c, "web_port", json_object_new_int(webfwd_port(id)));
		/* -1: always open; else the seconds it stays open, 0 for closed (LuCI's link opens it) */
		json_object_object_add(c, "web_open_s", json_object_new_int64(left < 0 ? -1 : left / 1000));
		if (webfwd_shared(id))
			json_object_object_add(c, "web_shared_host", json_object_new_boolean(true));
	}
	if (json_object_object_get(srv, "modemmanager") && (op = apn_suggest(id, json_object_object_get(srv, "modemmanager")))) {
		json_object *card = json_object_object_get(json_object_object_get(srv, "modemmanager"), "sim_card");
		const char *name = jstr(card, "operator_name") ? jstr(card, "operator_name") :
				   jstr(json_object_object_get(srv, "modemmanager"), "operator");

		if (name)
			json_object_object_add(op, "operator", json_object_new_string(name));
		json_object_object_add(c, "apn_suggestion", op);
	}
	if ((op = ttl_view(id)))
		json_object_object_add(c, "ttl", op);
	if (dial_last && json_object_object_get(dial_last, id))
		json_object_object_add(c, "dial", json_object_get(json_object_object_get(dial_last, id)));
	{
		/* down and kept down: by the operator (connect, kept on flash), else netifd gave up (autostart off
		 * after an error) and the engine brings it up again (iface_retry) */
		json_object *nd = json_object_object_get(json_object_object_get(srv, "session"), "netifd"), *as, *errs;
		json_object *known = deactivated ? json_object_object_get(deactivated, id) : NULL;

		as = json_object_object_get(nd, "autostart");
		errs = json_object_object_get(nd, "errors");
		if (iface && nd && !json_object_get_boolean(json_object_object_get(nd, "up")) && as && !json_object_get_boolean(as)) {
			json_object *d = known ? json_object_get(known) : json_object_new_object();

			if (!known) {
				json_object *rs = iface_retries ? json_object_object_get(iface_retries, id) : NULL;

				json_object_object_add(d, "by", json_object_new_string("netifd"));
				if (errs && json_object_is_type(errs, json_type_array) && json_object_array_length(errs))
					json_object_object_add(d, "why", json_object_get(json_object_array_get_idx(errs, 0)));
				if (rs) {
					long long left = (json_object_get_int64(json_object_object_get(rs, "next")) - now_mono_ms()) / 1000;

					json_object_object_add(d, "retry_in_s", json_object_new_int64(left > 0 ? left : 0));
					json_object_object_add(d, "retries", json_object_get(json_object_object_get(rs, "step")));
				}
			}
			json_object_object_add(c, "deactivated", d);
		}
	}
	{
		/* the network refused the registration: since when, and when the next connect is tried (LuCI's card) */
		json_object *dn = reg_denied ? json_object_object_get(reg_denied, id) : NULL;

		if (dn) {
			json_object *o = json_object_new_object();
			long long left = (json_object_get_int64(json_object_object_get(dn, "mono")) +
					  (budget_override_ms ? 3000 : MM_DENIED_RETRY_MS) - now_mono_ms()) / 1000;

			json_object_object_add(o, "since", json_object_get(json_object_object_get(dn, "since")));
			json_object_object_add(o, "at", json_object_get(json_object_object_get(dn, "at")));
			json_object_object_add(o, "retry_in_s", json_object_new_int64(left > 0 ? left : 0));
			json_object_object_add(c, "registration_denied", o);
		}
	}
	json_object_put(led);
	return c;
}

/*
 * option apn_auto '1' (the owner's request, test report 2026-10-15): the APN follows the SIM. A
 * modem whose interface MedveModem manages and whose session is not up gets its configuration
 * again when the APN it should have (the one the operator typed, else the SIM's suggestion)
 * differs from the one written (a SIM swapped, a database
 * update); a working connection is left alone until it drops. One APN tried once per modem, so a
 * wrong suggestion does not loop; one modem at a time, never while a change or a recovery runs.
 */
static json_object *apn_auto_tried;	/* device id -> the APN last applied automatically */

static void apn_auto_sync(void)
{
	if (job.active || reload_running || pending_release.active || !registry.devices)
		return;
	json_object_object_foreach(registry.devices, id, rec) {
		json_object *dec = decision_of(id), *srv, *led, *o;
		const char *sug, *want, *iface, *applied, *ss, *tried;
		char key[128], detail[160];

		/* following a swapped SIM stays opt-in ('1'): a modem left with the default takes its SIM's APN when its
		 * interface is made, and is not reconfigured under a running setup afterwards */
		if (!dec || !jstr(dec, "apn_auto") || strcmp(jstr(dec, "apn_auto"), "1") ||
		    !json_object_get_boolean(json_object_object_get(rec, "present")) || esim_reserved(id) || rec_busy(id) || frozen(id))
			continue;
		led = mm_cfgtxn_ledger(&cfg, id);
		iface = jstr(led, "interface");
		if (!iface) {		/* not brought up through MedveModem yet: the operator's first «Подключить» does it */
			json_object_put(led);
			continue;
		}
		snprintf(key, sizeof(key), "network.%s", iface);
		applied = jstr(json_object_object_get(json_object_object_get(json_object_object_get(led, "sections"), key), "fields"), "apn");
		srv = served(rec);
		sug = jstr(json_object_object_get(json_object_object_get(srv, "connection"), "apn_suggestion"), "apn");
		want = jstr(dec, "apn") && *jstr(dec, "apn") ? jstr(dec, "apn") : sug && *sug ? sug : NULL;
		ss = jstr(json_object_object_get(srv, "session"), "state");
		tried = apn_auto_tried ? jstr(apn_auto_tried, id) : NULL;
		if (!want || (applied && !strcmp(applied, want)) || (ss && (!strcmp(ss, "connected") || !strcmp(ss, "connecting"))) ||
		    (tried && !strcmp(tried, want))) {
			json_object_put(srv);
			json_object_put(led);
			continue;
		}
		if (!apn_auto_tried)
			apn_auto_tried = json_object_new_object();
		json_object_object_add(apn_auto_tried, id, json_object_new_string(want));
		snprintf(detail, sizeof(detail), "%s: %s -> %s%s", iface, applied ? applied : "(none)", want, jstr(dec, "apn") && *jstr(dec, "apn") ? " (typed)" : "");
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "apn-auto", detail);
		history_dirty = true;
		ULOG_INFO("apn_auto: %s for %s\n", detail, id);
		o = json_object_new_object();
		bringup_go(rec, o);
		json_object_put(o);
		json_object_put(srv);
		json_object_put(led);
		return;		/* one modem at a time: the next pass takes the next */
	}
}

/*
 * -b 1 (UCI core.auto_connect, default on in the init script; the owner, 2026-10-17: «авто подключение с выбором
 * APN авто»): a modem nothing connects, one with no interface of MedveModem's and none of the operator's, is
 * brought up by itself with its SIM's APN (apn_auto is on unless set '0'), once per attachment. Not a modem
 * QModem drives, nor one whose owner is not settled, waiting for a driver the router lacks, held by an eSIM
 * operation or a recovery, or with `option auto_connect '0'` in its device section. ModemManager's modem waits
 * until ModemManager holds it in a usable state (disabled counts: the bring-up enables it).
 */
static int auto_connect_on;
static json_object *auto_connect_tried;	/* "<id>|<generation>" -> true */

static void auto_connect_sync(void)
{
	if (!auto_connect_on || job.active || reload_running || pending_release.active || !registry.devices)
		return;
	json_object_object_foreach(registry.devices, id, rec) {
		json_object *dec, *srv, *led, *conn, *miss, *mm = NULL, *o;
		const char *control, *off;
		char key[192];
		bool go;

		if (!json_object_get_boolean(json_object_object_get(rec, "present")) || esim_reserved(id) || rec_busy(id) ||
		    frozen(id))
			continue;
		/* not before the network config was read for it: an operator's own interface must be seen first
		 * (review 2026-10-17: at boot it ran before and journalled a refusal) */
		if (!iface_checks || !json_object_object_get(iface_checks, id))
			continue;
		snprintf(key, sizeof(key), "%s|%lld", id, (long long)json_object_get_int64(json_object_object_get(rec, "generation")));
		if (auto_connect_tried && json_object_object_get(auto_connect_tried, key))
			continue;
		dec = decision_of(id);
		off = jstr(dec, "auto_connect");
		if (off && !strcmp(off, "0"))
			continue;
		led = mm_cfgtxn_ledger(&cfg, id);
		if (jstr(led, "interface")) {
			json_object_put(led);
			continue;
		}
		json_object_put(led);
		srv = served(rec);
		control = jstr(json_object_object_get(srv, "owner"), "control");
		conn = json_object_object_get(srv, "connection");
		miss = json_object_object_get(srv, "drivers_missing");
		/* a native modem has no owner before its first interface: one with a QMI/MBIM control port where
		 * ModemManager does not run is taken as native (review 2026-10-17: it never connected) */
		go = control && (!strcmp(control, "modemmanager") || !strncmp(control, "netifd-", 7) ||
				 (!strcmp(control, "unresolved") && strcmp(mm_backend, "available") &&
				  json_object_object_get(json_object_object_get(srv, "port_roles"), "control"))) &&
		     !jstr(conn, "interface") && (!jstr(conn, "user_interface") || foreign_adopt(id)) &&
		     !(miss && json_object_array_length(miss));
		if (go && !strcmp(control, "modemmanager"))
			go = mm_devices && json_object_object_get_ex(mm_devices, id, &mm) && mm_state_usable(jstr(mm, "state"));
		json_object_put(srv);
		if (!go)
			continue;
		if (!auto_connect_tried)
			auto_connect_tried = json_object_new_object();
		json_object_object_add(auto_connect_tried, key, json_object_new_boolean(1));
		o = json_object_new_object();
		auto_connecting = true;
		go = bringup_go(rec, o);
		auto_connecting = false;
		ULOG_INFO("auto-connect: %s: a new modem with no interface, bring-up %s\n", id, go ? "started" : jstr(o, "result"));
		mm_history_add(&history, id, (long long)time(NULL), now_mono_ms(), "auto-connect", go ? "started" : jstr(o, "result"));
		history_dirty = true;
		json_object_put(o);
		return;		/* one modem at a time: the next pass takes the next */
	}
}

/* the router's uplink summary: which path carries the traffic, whether it answers, whether a backup is ready */
enum { FF_DEVICE, __FF_MAX };
static const struct blobmsg_policy forgive_policy[] = {
	[FF_DEVICE] = { .name = "device", .type = BLOBMSG_TYPE_STRING },
};

/* the operator takes a demoted link back (failback "demote"): one device, or every one */
static int m_failover_forgive(struct ubus_context *ctx, struct ubus_object *obj,
			      struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__FF_MAX];
	const char *dev;
	json_object *o;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(forgive_policy, __FF_MAX, tb, blob_data(msg), blob_len(msg));
	dev = tb[FF_DEVICE] ? blobmsg_get_string(tb[FF_DEVICE]) : NULL;
	if (fo_state)
		mm_failover_forgive(fo_state, dev);
	if (fo_state6)
		mm_failover_forgive(fo_state6, dev);
	failover_turn();
	o = failover_view();
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/*
 * The links failover never makes main by itself (core.failover_manual), set whole: the Telegram bot's exclusion
 * menu edits a draft and applies it at once, with the list it started from in «expect» - when LuCI changed the list
 * in between, nothing is written and the reply carries the list as it is now (result «changed»).
 */
enum { FM_DEVICES, FM_EXPECT, __FM_MAX };
static const struct blobmsg_policy manual_policy[] = {
	[FM_DEVICES] = { .name = "devices", .type = BLOBMSG_TYPE_ARRAY },
	[FM_EXPECT] = { .name = "expect", .type = BLOBMSG_TYPE_ARRAY },
};

static json_object *manual_now(void)
{
	json_object *c = core_uci(), *m = c ? json_object_object_get(c, "failover_manual") : NULL, *out = json_object_new_array();

	if (m && json_object_is_type(m, json_type_string))
		json_object_array_add(out, json_object_get(m));
	for (size_t i = 0; m && json_object_is_type(m, json_type_array) && i < json_object_array_length(m); i++)
		json_object_array_add(out, json_object_get(json_object_array_get_idx(m, i)));
	return out;
}

static bool same_set(json_object *a, struct blob_attr *b)
{
	struct blob_attr *cur;
	size_t rem, n = 0;

	blobmsg_for_each_attr(cur, b, rem) {
		bool found = false;

		if (blobmsg_type(cur) != BLOBMSG_TYPE_STRING)
			return false;
		for (size_t i = 0; i < json_object_array_length(a); i++)
			found = found || !strcmp(json_object_get_string(json_object_array_get_idx(a, i)), blobmsg_get_string(cur));
		if (!found)
			return false;
		n++;
	}
	return n == json_object_array_length(a);
}

static int m_failover_manual(struct ubus_context *ctx, struct ubus_object *obj,
			     struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__FM_MAX], *cur;
	json_object *o = json_object_new_object(), *now = manual_now(), *list = json_object_new_array(), *ops, *op;
	char err[160];
	size_t rem;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(manual_policy, __FM_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[FM_DEVICES]) {
		json_object_put(o); json_object_put(now); json_object_put(list);
		return UBUS_STATUS_INVALID_ARGUMENT;
	}
	blobmsg_for_each_attr(cur, tb[FM_DEVICES], rem) {
		const char *d = blobmsg_type(cur) == BLOBMSG_TYPE_STRING ? blobmsg_get_string(cur) : "";
		size_t k, n = strlen(d);
		bool dup = false;

		for (k = 0; k < n && (isalnum((unsigned char)d[k]) || strchr("._@-", d[k])); k++)
			;
		if (!n || n > 15 || k != n || json_object_array_length(list) >= 16) {
			json_object_put(o); json_object_put(now); json_object_put(list);
			return UBUS_STATUS_INVALID_ARGUMENT;
		}
		for (k = 0; k < json_object_array_length(list); k++)
			dup = dup || !strcmp(json_object_get_string(json_object_array_get_idx(list, k)), d);
		if (!dup)
			json_object_array_add(list, json_object_new_string(d));
	}
	if (tb[FM_EXPECT] && !same_set(now, tb[FM_EXPECT])) {
		json_object_object_add(o, "result", json_object_new_string("changed"));
		json_object_object_add(o, "manual", now);
		json_object_put(list);
		goto reply;
	}
	ops = json_object_new_array();
	op = json_object_new_object();
	json_object_object_add(op, "op", json_object_new_string(json_object_array_length(list) ? "set" : "delete"));
	json_object_object_add(op, "package", json_object_new_string("medvemodem"));
	json_object_object_add(op, "section", json_object_new_string("core"));
	json_object_object_add(op, "option", json_object_new_string("failover_manual"));
	if (json_object_array_length(list))
		json_object_object_add(op, "value", json_object_get(list));
	json_object_array_add(ops, op);
	rc = mm_uci_apply(conf_dir, cfg.savedir, ops, err, sizeof(err));
	json_object_put(ops);
	json_object_put(now);
	if (rc) {
		json_object_object_add(o, "result", json_object_new_string("failed"));
		json_object_object_add(o, "reason", json_object_new_string(err));
		json_object_put(list);
		goto reply;
	}
	ULOG_INFO("failover: the links kept manual set by the operator: %zu\n", json_object_array_length(list));
	failover_turn();
	json_object_object_add(o, "result", json_object_new_string("done"));
	json_object_object_add(o, "manual", list);
reply:
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

static int m_uplink(struct ubus_context *ctx, struct ubus_object *obj,
		    struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	json_object *paths = json_object_new_array(), *o, *kroutes, *wired;
	char *r4 = slurp(PROC_NET_ROUTE);
	int rc;

	(void)obj; (void)method; (void)msg;
	/* the kernel's metric beside the configured one: the Expert table of the channels (HANDOFF 200 item 16) */
	kroutes = mm_failover_routes(r4 ? r4 : "");
	free(r4);
	if (registry.devices) {
		json_object_object_foreach(registry.devices, id, rec) {
			json_object *srv, *u, *p, *conn;
			size_t k;

			if (!json_object_get_boolean(json_object_object_get(rec, "present")))
				continue;
			srv = served(rec);
			u = json_object_object_get(srv, "uplink");
			p = json_object_new_object();
			json_object_object_add(p, "device_id", json_object_new_string(id));
			if (jstr(srv, "name"))
				json_object_object_add(p, "name", json_object_new_string(jstr(srv, "name")));
			json_object_object_add(p, "device", json_object_get(json_object_object_get(u, "device")));
			json_object_object_add(p, "role", json_object_get(json_object_object_get(u, "role")));
			json_object_object_add(p, "reach", json_object_get(json_object_object_get(u, "reach")));
			if (json_object_object_get(u, "dns"))
				json_object_object_add(p, "dns", json_object_get(json_object_object_get(u, "dns")));
			json_object_object_add(p, "rx_rate", json_object_get(json_object_object_get(json_object_object_get(u, "traffic"), "rx_rate")));
			json_object_object_add(p, "tx_rate", json_object_get(json_object_object_get(json_object_object_get(u, "traffic"), "tx_rate")));
			path_traffic(p, json_object_object_get(u, "traffic"));
			if (traffic_peak_of(id))	/* spec §42: the highest 5 s rates since the router booted */
				json_object_object_add(p, "peak", json_object_get(traffic_peak_of(id)));
			conn = json_object_object_get(srv, "connection");
			if (json_object_object_get(conn, "metric"))
				json_object_object_add(p, "metric", json_object_get(json_object_object_get(conn, "metric")));
			for (k = 0; jstr(u, "device") && k < json_object_array_length(kroutes); k++) {
				json_object *r = json_object_array_get_idx(kroutes, k);

				if (jstr(r, "dev") && !strcmp(jstr(r, "dev"), jstr(u, "device")))
					json_object_object_add(p, "kernel_metric", json_object_get(json_object_object_get(r, "metric")));
			}
			json_object_array_add(paths, p);
			json_object_put(srv);
		}
	}
	if (uplinks) {
		json_object_object_foreach(uplinks, key, st) {
			json_object *p;

			bool modem = false;
			size_t i;

			if (strncmp(key, "dev:", 4))
				continue;
			/* the engine learned this device is a modem's: the modem's path says it */
			for (i = 0; i < json_object_array_length(paths); i++)
				modem = modem || !strcmp(jstr(json_object_array_get_idx(paths, i), "device") ?
							 jstr(json_object_array_get_idx(paths, i), "device") : "", key + 4);
			if (modem)
				continue;
			p = json_object_new_object();
			json_object_object_add(p, "device", json_object_new_string(key + 4));
			json_object_object_add(p, "role", json_object_new_string(
				mm_uplink_role("connected", key + 4, default_dev, json_object_object_get(st, "reach"))));
			json_object_object_add(p, "reach", reach_view(json_object_object_get(st, "reach")));
			path_traffic(p, json_object_object_get(st, "traffic"));
			for (size_t k = 0; k < json_object_array_length(kroutes); k++)
				if (jstr(json_object_array_get_idx(kroutes, k), "dev") && !strcmp(jstr(json_object_array_get_idx(kroutes, k), "dev"), key + 4))
					json_object_object_add(p, "kernel_metric", json_object_get(json_object_object_get(json_object_array_get_idx(kroutes, k), "metric")));
			json_object_array_add(paths, p);
		}
	}
	json_object_put(kroutes);
	/* the router's own uplinks (a wired WAN) with netifd's device: the order of «Приоритет» and the channels' names */
	wired = wired_now(NULL);
	for (size_t i = 0; i < json_object_array_length(wired); i++) {
		json_object *w = json_object_array_get_idx(wired, i), *st = netifd_call(jstr(w, "interface"), "status");

		json_object_object_add(w, "up", json_object_new_boolean(json_object_get_boolean(json_object_object_get(st, "up"))));
		if (jstr(st, "l3_device")) {
			json_object_object_add(w, "device", json_object_new_string(jstr(st, "l3_device")));
			for (size_t k = 0; k < json_object_array_length(paths); k++) {
				json_object *p = json_object_array_get_idx(paths, k);

				if (!jstr(p, "device_id") && jstr(p, "device") && !strcmp(jstr(p, "device"), jstr(st, "l3_device"))) {
					json_object_object_add(p, "interface", json_object_new_string(jstr(w, "interface")));
					/* the page names the channel by it: wired WAN, PPPoE, L2TP (the owner, 2026-10-25) */
					json_object_object_add(p, "proto", json_object_get(json_object_object_get(w, "proto")));
					json_object_object_add(p, "metric", json_object_get(json_object_object_get(w, "metric")));
				}
			}
		}
		json_object_put(st);
	}
	o = mm_uplink_summary(paths, *default_dev ? default_dev : NULL);
	{
		json_object *wl = wlmode_view();

		if (wl)
			json_object_object_add(o, "wlmode", wl);
	}
	json_object_object_add(o, "default_device", *default_dev ? json_object_new_string(default_dev) : NULL);
	json_object_object_add(o, "paths", paths);
	json_object_object_add(o, "wired", wired);
	{
		/* the tunnels and bypasses on this router that may start anew when the internet works (ready_turn), the
		 * operator's choice first */
		static const char *const known[] = { "podkop", "zapret", "zapret2", "youtubeUnblock", "byedpi", "passwall", "passwall2",
						     "ssclash", "clash", "mihomo", "nikki", "openclash", "sing-box", "xray", "v2ray",
						     "zeroblock", "tor", "https-dns-proxy", "dnscrypt-proxy", "stubby", "adguardhome",
						     "AdGuardHome", NULL };
		json_object *chosen = ready_services(), *svcs = json_object_new_array();
		char path[PATH_MAX];

		for (size_t i = 0; i < json_object_array_length(chosen); i++) {
			json_object *one = json_object_new_object();
			const char *n = json_object_get_string(json_object_array_get_idx(chosen, i));

			snprintf(path, sizeof(path), "%s/%s", initd_dir(), n);
			json_object_object_add(one, "name", json_object_new_string(n));
			json_object_object_add(one, "restart", json_object_new_boolean(1));
			json_object_object_add(one, "installed", json_object_new_boolean(access(path, X_OK) == 0));
			json_object_array_add(svcs, one);
		}
		for (int i = 0; known[i]; i++) {
			bool seen = false;

			for (size_t k = 0; k < json_object_array_length(chosen); k++)
				seen = seen || !strcmp(json_object_get_string(json_object_array_get_idx(chosen, k)), known[i]);
			snprintf(path, sizeof(path), "%s/%s", initd_dir(), known[i]);
			if (!seen && access(path, X_OK) == 0) {
				json_object *one = json_object_new_object();

				json_object_object_add(one, "name", json_object_new_string(known[i]));
				json_object_object_add(one, "restart", json_object_new_boolean(0));
				json_object_object_add(one, "installed", json_object_new_boolean(1));
				json_object_array_add(svcs, one);
			}
		}
		json_object_object_add(o, "services", svcs);
		json_object_put(chosen);
	}
	json_object_object_add(o, "failover", failover_view());
	{
		json_object *t = tunnel_view();

		if (t) {
			json_object *v = json_object_new_object();

			json_object_object_foreach(t, k, val)
				json_object_object_add(v, k, json_object_get(val));
			json_object_object_add(v, "age_s", json_object_new_int64((now_mono_ms() - tun.at) / 1000));
			json_object_object_add(o, "tunnel", v);
			json_object_put(t);
		}
	}
	{
		json_object *g = json_object_new_object();

		json_object_object_add(g, "enabled", json_object_new_boolean(proxy_guard_enabled()));
		/* the checks fail, the internet answers through the local proxy: no recovery for it */
		json_object_object_add(g, "blocked_checks", json_object_new_boolean(pg_ok && now_mono_ms() - pg_at < 2 * PROXY_GUARD_MS));
		if (pg_at >= 0)
			json_object_object_add(g, "age_s", json_object_new_int64((now_mono_ms() - pg_at) / 1000));
		json_object_object_add(o, "proxy_guard", g);
	}
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* the recovery history and the last pre-reset snapshot (22.1.6), for diagnostic bundles */
/* «Проверить связь»: the reach and DNS check of a connected modem now, not at its next turn */
static int m_check(struct ubus_context *ctx, struct ubus_object *obj,
		   struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__DEVICE_MAX];
	json_object *rec, *st, *o, *srv, *sess;
	const char *id, *result;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(device_policy, __DEVICE_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[DEVICE_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[DEVICE_ID]))))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	srv = served(rec);
	sess = json_object_object_get(srv, "session");
	if (!json_object_get_boolean(json_object_object_get(rec, "present")) || !session_dev(sess) ||
	    !jstr(sess, "state") || strcmp(jstr(sess, "state"), "connected"))
		result = "not-connected";
	else if (reach_running(id))
		result = "running";
	else {
		/* a session the sampler has not seen yet is checked on this pass anyway; a check asked by hand asks the
		 * operator's page again too (r163 §10: `check` sent no plain HTTP once the episode had asked) */
		if (uplinks && (st = json_object_object_get(uplinks, id))) {
			json_object_object_add(st, "next", json_object_new_int64(0));
			json_object *rch = json_object_object_get(st, "reach");

			if (rch)	/* json-c's object_del on NULL crashes: no check yet, nothing asked */
				json_object_object_del(rch, "redirect_asked");
		}
		uloop_timeout_set(&uplink_timer, 0);
		result = "started";
	}
	json_object_put(srv);
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "result", json_object_new_string(result));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

static int m_recovery(struct ubus_context *ctx, struct ubus_object *obj,
		      struct ubus_request_data *req, const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__DEVICE_MAX];
	json_object *rec, *st, *o;
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(device_policy, __DEVICE_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[DEVICE_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[DEVICE_ID]))))
		return UBUS_STATUS_NOT_FOUND;
	st = rec_states ? json_object_object_get(rec_states, jstr(rec, "device_id")) : NULL;
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(jstr(rec, "device_id")));
	json_object_object_add(o, "attempts", json_object_get(json_object_object_get(st, "attempts")));
	json_object_object_add(o, "last_snapshot", json_object_get(json_object_object_get(st, "last_snapshot")));
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	return rc;
}

/* ---- presets for the AT console and USSD (spec 11.5, 38) ---- */

enum { PRE_ID, __PRE_MAX };
static const struct blobmsg_policy presets_policy[] = {
	[PRE_ID] = { .name = "id", .type = BLOBMSG_TYPE_STRING },
};

static json_object *preset_file(const char *name)
{
	char path[512], *text;
	json_object *o;

	snprintf(path, sizeof(path), "%s/%s", PRESET_DIR, name);
	if (!(text = slurp(path)))
		return NULL;
	o = json_tokener_parse(text);
	free(text);
	return o;
}

/* the AT presets that fit the modem, marked by hazard, and the USSD codes of its SIM's operator */
static int m_presets(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		     const char *method, struct blob_attr *msg)
{
	struct blob_attr *tb[__PRE_MAX];
	json_object *rec, *srv, *mm, *card, *sec, *at, *ussd, *o, *codes;
	const char *id, *vidpid, *plmn;
	char vid[8] = "", imsi_plmn[8] = "";
	int rc;

	(void)obj; (void)method;
	blobmsg_parse(presets_policy, __PRE_MAX, tb, blob_data(msg), blob_len(msg));
	if (!tb[PRE_ID])
		return UBUS_STATUS_INVALID_ARGUMENT;
	if (!(rec = mm_registry_find(&registry, blobmsg_get_string(tb[PRE_ID]))))
		return UBUS_STATUS_NOT_FOUND;
	id = jstr(rec, "device_id");
	srv = served(rec);
	mm = json_object_object_get(srv, "modemmanager");
	card = json_object_object_get(mm, "sim_card");
	sec = sim_secret ? json_object_object_get(sim_secret, id) : NULL;
	vidpid = jstr(rec, "vidpid");
	if (vidpid)
		snprintf(vid, sizeof(vid), "%.4s", vidpid);
	/* the SIM's own operator; its IMSI (Russia: a two-digit MNC); the network it is on */
	if (jstr(sec, "imsi") && strlen(jstr(sec, "imsi")) >= 5)
		snprintf(imsi_plmn, sizeof(imsi_plmn), "%.5s", jstr(sec, "imsi"));
	plmn = jstr(card, "operator_code") ? jstr(card, "operator_code") : imsi_plmn[0] ? imsi_plmn : jstr(mm, "operator_code");
	at = preset_file("at-presets.json");
	ussd = preset_file("ussd-codes.json");
	o = json_object_new_object();
	json_object_object_add(o, "device_id", json_object_new_string(id));
	json_object_object_add(o, "at", mm_at_presets(at, jstr(json_object_object_get(srv, "profile"), "id"),
						      jstr(mm, "manufacturer") ? jstr(mm, "manufacturer") : jstr(rec, "manufacturer"),
						      jstr(mm, "model") ? jstr(mm, "model") : jstr(rec, "product"), vid));
	json_object_object_add(o, "at_owner", json_object_get(json_object_object_get(json_object_object_get(srv, "owner"), "at")));
	if (plmn)
		json_object_object_add(o, "plmn", json_object_new_string(plmn));
	codes = mm_ussd_codes(ussd, plmn);
	json_object_object_add(o, "ussd", codes);
	rc = reply_json(ctx, req, NULL, o);
	json_object_put(o);
	json_object_put(at);
	json_object_put(ussd);
	json_object_put(srv);
	return rc;
}

static const struct ubus_method methods[] = {
	UBUS_METHOD("presets", m_presets, presets_policy),
	UBUS_METHOD_NOARG("status", m_status),
	UBUS_METHOD("devices", m_devices, devices_policy),
	UBUS_METHOD("device", m_device, device_policy),
	UBUS_METHOD("lookup", m_lookup, lookup_policy),
	UBUS_METHOD("events", m_events, events_policy),
	UBUS_METHOD("lease_acquire", m_lease_acquire, lease_acquire_policy),
	UBUS_METHOD("lease_release", m_lease_release, lease_policy),
	UBUS_METHOD("lease_renew", m_lease_renew, lease_policy),
	UBUS_METHOD_NOARG("leases", m_leases),
	UBUS_METHOD_NOARG("rescan", m_rescan),
	UBUS_METHOD("config_plan", m_config_plan, config_policy),
	UBUS_METHOD("config_apply", m_config_apply, config_policy),
	UBUS_METHOD("config_release", m_config_release, config_policy),
	UBUS_METHOD("config_rollback", m_config_rollback, config_policy),
	UBUS_METHOD("config_state", m_config_state, config_policy),
	UBUS_METHOD("bringup", m_bringup, bringup_policy),
	UBUS_METHOD("connect", m_connect, connect_policy),
	UBUS_METHOD("freeze", m_freeze, freeze_policy),
	UBUS_METHOD("web_open", m_web_open, web_open_policy),
	UBUS_METHOD("web_info", m_web_info, web_info_policy),
	UBUS_METHOD("web_reboot", m_web_info, web_info_policy),
	UBUS_METHOD("web_do", m_web_do, web_do_policy),
	UBUS_METHOD("priority", m_priority, priority_policy),
	UBUS_METHOD("at_command", m_at_command, at_policy),
	UBUS_METHOD("metrics", m_metrics, metrics_policy),
	UBUS_METHOD("probe", m_probe, device_policy),
	UBUS_METHOD("radio", m_radio, radio_policy),
	UBUS_METHOD("cells", m_cells, cells_policy),
	UBUS_METHOD("signal_history", m_signal_history, sighist_policy),
	UBUS_METHOD("traffic_history", m_traffic_history, trafhist_policy),
	UBUS_METHOD("at_history", m_at_history, at_history_policy),
	UBUS_METHOD("radio_set", m_radio_set, radio_set_policy),
	UBUS_METHOD("radio_confirm", m_radio_confirm, radio_confirm_policy),
	UBUS_METHOD("dial", m_dial, dial_policy),
	UBUS_METHOD("hangup", m_dial, dial_policy),
	UBUS_METHOD("esim_status", m_esim_status, esim_policy),
	UBUS_METHOD("esim_action", m_esim_action, esim_policy),
	UBUS_METHOD("recover", m_recover, recover_policy),
	UBUS_METHOD("recovery", m_recovery, device_policy),
	UBUS_METHOD("check", m_check, device_policy),
	UBUS_METHOD("mm_restart", m_mm_restart, mmr_policy),
	UBUS_METHOD_NOARG("uplink", m_uplink),
	UBUS_METHOD("failover_forgive", m_failover_forgive, forgive_policy),
	UBUS_METHOD("modeswitch_fix", m_modeswitch_fix, msw_policy),
	UBUS_METHOD("fcc_unlock", m_fcc_unlock, fcc_policy),
	UBUS_METHOD("modeswitch_run", m_modeswitch_run, msw_policy),
	UBUS_METHOD("failover_manual", m_failover_manual, manual_policy),
	UBUS_METHOD("sms_list", m_sms, sms_policy),
	UBUS_METHOD("sms_send", m_sms, sms_policy),
	UBUS_METHOD("sms_delete", m_sms, sms_policy),
	UBUS_METHOD("sms_center", m_sms, sms_policy),
	UBUS_METHOD("sms_storage", m_sms, sms_policy),
	UBUS_METHOD("sms_clear", m_sms, sms_policy),
	UBUS_METHOD("ussd_start", m_ussd, ussd_policy),
	UBUS_METHOD("ussd_respond", m_ussd, ussd_policy),
	UBUS_METHOD("ussd_cancel", m_ussd, ussd_policy),
	UBUS_METHOD("ussd_status", m_ussd, ussd_policy),
	UBUS_METHOD("sim_unlock", m_sim_unlock, sim_unlock_policy),
	UBUS_METHOD("sim_slot", m_sim_slot, sim_slot_policy),
	UBUS_METHOD("reregister", m_reregister, reregister_policy),
	UBUS_METHOD("fplmn", m_fplmn, fplmn_policy),
	UBUS_METHOD("handover", m_foreign_handover, foreign_handover_policy),
	UBUS_METHOD("neighbours", m_neighbours, neighbours_policy),
	UBUS_METHOD("alerts", m_alerts, alerts_policy),
	UBUS_METHOD("wlmode", m_wlmode, wlmode_policy),
	UBUS_METHOD_NOARG("usbtree", m_usbtree),
	UBUS_METHOD("usbtree_detail", m_usbtree_detail, usbtree_detail_policy),
	UBUS_METHOD("usb_class", m_usb_class, usb_class_policy),
	UBUS_METHOD("usb_rescue", m_usb_rescue, usb_rescue_policy),
	UBUS_METHOD("usb_rescue_run", m_usb_rescue_run, usb_rescue_run_policy),
};

static struct ubus_object_type object_type = UBUS_OBJECT_TYPE("medvemodem", methods);

static struct ubus_object object = {
	.name = "medvemodem",
	.type = &object_type,
	.methods = methods,
	.n_methods = ARRAY_SIZE(methods),
};

static void on_connected(struct ubus_context *ctx)
{
	int rc = ubus_add_object(ctx, &object);

	if (rc)
		ULOG_ERR("cannot publish ubus object: %s\n", ubus_strerror(rc));
	else
		ULOG_INFO("ubus object 'medvemodem' published\n");
	ubus_up = !rc;
	netifd_events.cb = netifd_event;
	if (ubus_register_event_handler(ctx, &netifd_events, "network.interface"))
		ULOG_WARN("cannot listen to netifd events; session state follows requests only\n");
	sessions_initial();
	iface_retry_timer.cb = iface_retry_fired;
	uloop_timeout_set(&iface_retry_timer, budget_override_ms ? 300 : 15000);
}

static void usage(const char *prog)
{
	fprintf(stderr, "Usage: %s [-s ubus-socket] [-S (log to stderr)] [-d (debug)] [-t settle-ms]\n"
			"       [-U uevent-test-socket] [-H history-file]\n"
			"       [-M mmcli-path|none] [-K mmcli-timeout-ms] [-X qmicli-path] [-T route-helper] [-I modemmanager-init] [-J modemmanager-process]\n"
			"       [-O proc-dir] [-V modemmanager-rules-file]\n"
			"       [-C uci-confdir] [-E state-dir] [-W run-dir] [-R reload-hook|none]\n"
			"       [-P netifd-proto-dir] [-B bring-up-budget-ms] [-D dev-dir] [-Q qmodem-dir]\n"
			"       [-Y esim-backend] [-G esim-timeout-ms] [-c 0|1 (cell info and 3GPP location from ModemManager)] [-a 0|1 (antennas over qmicli)]\n"
			"       [-b 0|1 (connect a new modem nothing connects, its APN by the SIM)]\n"
			"       [-g gentle-poll-ms (a look reads ModemManager at most once in it)]\n", prog);
}

/* the ids of a map the registry no longer has, dropped (keys "<id>" or "<id>|<generation>|...": those of an earlier
 * attachment go too); kept_gen: a "<id>|<gen>" key of the current generation stays */
static void map_retain(json_object *map, bool by_generation)
{
	json_object *gone;
	size_t i;

	if (!map || !registry.devices)
		return;
	gone = json_object_new_array();
	{
		json_object_object_foreach(map, key, v) {
			char id[256];
			const char *bar = strchr(key, '|');
			json_object *rec;

			(void)v;
			snprintf(id, sizeof(id), "%.*s", bar ? (int)(bar - key) : (int)strlen(key), key);
			rec = json_object_object_get(registry.devices, id);
			if (!rec || (by_generation && bar && atoll(bar + 1) != json_object_get_int64(json_object_object_get(rec, "generation"))))
				json_object_array_add(gone, json_object_new_string(key));
		}
	}
	for (i = 0; i < json_object_array_length(gone); i++)
		json_object_object_del(map, json_object_get_string(json_object_array_get_idx(gone, i)));
	json_object_put(gone);
}

/*
 * Audit 2026-10-20: the engine's per-device books go with the device (the registry keeps 32 absent ones), and the
 * one-try books of an earlier attachment go when the device re-attaches. The operator's own choices (deactivated,
 * on flash) and the books the managers prune themselves are not touched here.
 */
static void caches_retain(void)
{
	json_object *maps[] = { metrics_cache, cells_cache, radio_cache, probes, dial_plmn, dial_cold, dial_last, stab_state,
				mm_withdrawn, mm_reported, sessions, hostfixes, rec_states };
	size_t i;

	for (i = 0; i < sizeof(maps) / sizeof(maps[0]); i++)
		map_retain(maps[i], false);
	map_retain(iface_tried, true);
	/* HANDOFF 200 item 17: these two lived until the engine stopped */
	map_retain(frozen_withdrawn, false);
	map_retain(webfwd_open, false);
	map_retain(mm_return_tried, true);
	map_retain(auto_connect_tried, true);
}

int main(int argc, char **argv)
{
	int ch, log_channels = ULOG_SYSLOG, log_level = LOG_INFO;

	while ((ch = getopt(argc, argv, "a:b:g:c:s:Sdt:U:H:M:K:C:E:W:R:P:B:D:Q:Y:G:F:A:w:N:L:X:I:J:O:V:T:h")) != -1) {
		switch (ch) {
		case 's': conn.path = optarg; break;
		case 'S': log_channels = ULOG_STDIO; break;
		case 'd': log_level = LOG_DEBUG; break;
		case 't':
			settle_ms = atoi(optarg);
			if (settle_ms < 50 || settle_ms > 60000) {
				fprintf(stderr, "settle-ms must be 50..60000\n");
				return 2;
			}
			break;
		case 'U': uevent_test_socket = optarg; break;
		case 'H': history_path = optarg; break;
		case 'M': mmcli_path = optarg; break;
		case 'X': qmicli_path = optarg; break;
		case 'T': route_helper = optarg; break;
		case 'I': mm_init_path = optarg; break;
		case 'J': mm_process = optarg; break;
		case 'O': proc_dir = optarg; break;
		case 'V': mm_rules_path = optarg; break;
		case 'K': mm_timeout_ms = atoi(optarg) > 0 ? atoi(optarg) : MM_WORKER_TIMEOUT_MS; break;
		case 'C': conf_dir = optarg; break;
		case 'E': state_dir = optarg; break;
		case 'W': run_dir = optarg; break;
		case 'R': reload_hook = optarg; break;
		case 'P': proto_dir = optarg; break;
		case 'B': budget_override_ms = atoi(optarg); break;
		case 'D': dev_dir = optarg; break;
		case 'Q': qmodem_dir = optarg; break;
		case 'Y': esim_set_backend(optarg); break;
		case 'F': host_fixes = atoi(optarg); break;
		case 'A': auto_recovery = atoi(optarg); break;
		case 'w': watch_override_ms = atoi(optarg); break;
		case 'N': uplink_override_ms = atoi(optarg); break;
		case 'L': repair_ifaces = atoi(optarg); break;
		case 'c': cell_info_on = atoi(optarg); break;
		case 'a': antennas_on = atoi(optarg); break;
		case 'b': auto_connect_on = atoi(optarg); break;
		case 'g': mm_gentle_ms = atoi(optarg) > 0 ? atoi(optarg) : 0; break;
		case 'G': esim_set_timeout(atoi(optarg)); break;
		default: usage(argv[0]); return ch == 'h' ? 0 : 2;
		}
	}
	ulog_open(log_channels, LOG_DAEMON, "medvemodemd");
	ulog_threshold(log_level);
	/* qmicli by its full path: the worker shell's lookup found none where /usr/bin/qmicli ran by hand (the owner's
	 * T99 and DW5821e, 2026-10-21: «установите qmi-utils» with qmi-utils installed) */
	if (!strchr(qmicli_path, '/')) {
		static const char *const dirs[] = { "/usr/bin", "/usr/sbin", "/bin", "/sbin" };
		static char found[64];
		size_t d;

		for (d = 0; d < sizeof(dirs) / sizeof(dirs[0]); d++) {
			snprintf(found, sizeof(found), "%s/%s", dirs[d], qmicli_path);
			if (!access(found, X_OK)) {
				qmicli_path = found;
				break;
			}
		}
	}

	started = time(NULL);
	mm_registry_init(&registry);
	mm_history_init(&history, MM_HISTORY_CAP);
	stab_state = json_object_new_object();
	sessions = json_object_new_object();
	metrics_cache = json_object_new_object();
	radio_cache = json_object_new_object();
	cells_cache = json_object_new_object();
	cut_nodes = json_object_new_object();
	stuck_nodes = json_object_new_object();
	probes = json_object_new_object();
	{
		int k;
		for (k = 0; k < MM_AT_JOBS; k++)
			at_jobs[k].out.fd = -1;
	}
	esim_mgr_start();
	netifd_pending = json_object_new_array();
	netifd_timer.cb = netifd_fired;
	job_timer.cb = job_fired;
	mm_leases_init(&leases);
	lease_timer.cb = lease_timeout;
	mm_out.fd = -1;
	read_boot_id();
	last_exit_report(history_load());
	uloop_init();
	/* an interrupted transaction is rolled back before anything else runs */
	mm_cfgtxn_init(&cfg, conf_dir, state_dir, run_dir);
	known_load();
	deactivated_load();
	frozen_load();
	port_off_restore();
	if (cfg.recovered) {
		json_object *pk = NULL;
		ULOG_WARN("configuration transaction %s was interrupted: %s\n",
			  json_object_get_string(json_object_object_get(cfg.recovered, "txn")), status_of(cfg.recovered));
		json_object_object_get_ex(cfg.recovered, "packages", &pk);
		reload_start(pk);
	}
	settle_timer.cb = settle_fired;
	stab_timer.cb = stability_recheck;
	mm_sched_timer.cb = mm_sched_fired;
	/* Listen before the first scan so nothing between them is missed. */
	if (uevent_open())
		ULOG_ERR("cannot listen to uevents: %s; rescan on request only\n", strerror(errno));
	{
		char dups[PATH_MAX];

		snprintf(dups, sizeof(dups), "%s/serial-dups.json", state_dir);
		mm_discovery_serial_dups_file(dups);	/* serials once seen twice stay no identity (audit of r181, D-01) */
	}
	discovery_pass("start");
	/* ModemManager may still be starting at boot: first look now, then later */
	mm_followups = 2;
	mm_schedule(0);
	uplink_load();
	quota_mgr_start();
	downtime_mgr_start();
	traffic_mgr_start();
	mmlog_start();
	neighbours_start();
	procs_start();
	kfault_start();
	sms_seen_start();
	radio_mgr_start();
	signal_mgr_start();
	usbtree_start();
	sim_mgr_start();
	uplink_timer.cb = uplink_tick;
	uloop_timeout_set(&uplink_timer, 0);
	sms_mgr_start();
	holders_timer.cb = holders_scan;
	uloop_timeout_set(&holders_timer, 0);

	conn.cb = on_connected;
	ubus_auto_connect(&conn);
	last_exit_write("stop", uloop_run());

	ubus_auto_shutdown(&conn);
	if (uevent_fd.registered) {
		uloop_fd_delete(&uevent_fd);
		close(uevent_fd.fd);
	}
	if (mm_running && mm_proc.pid > 0)
		kill(-mm_proc.pid, SIGKILL);
	if (reload_running && reload_proc.pid > 0)
		kill(-reload_proc.pid, SIGKILL);
	/* the engine stops: the modems go back to ModemManager, while the state they are judged by is still there */
	{
		int k;
		for (k = 0; k < MM_INHIBITS; k++)
			if (inhibits[k].used && inhibits[k].proc.pid > 0) {
				kill(inhibits[k].proc.pid, SIGTERM);
				mm_report_ports(inhibits[k].device_id);
			}
	}
	json_object_put(reload_reply);
	mm_cfgtxn_free(&cfg);
	json_object_put(job.steps);
	json_object_put(job.last);
	json_object_put(sessions);
	json_object_put(netifd_pending);
	{
		int k;
		for (k = 0; k < MM_AT_JOBS; k++)
			if (at_jobs[k].active) {
				if (at_jobs[k].proc.pid > 0)
					kill(-at_jobs[k].proc.pid, SIGKILL);
				at_close_out(&at_jobs[k]);
				free(at_jobs[k].buf);
				json_object_put(at_jobs[k].cmds);
				json_object_put(at_jobs[k].queue);
				json_object_put(at_jobs[k].results);
			}
	}
	json_object_put(metrics_cache);
	json_object_put(radio_cache);
	json_object_put(cells_cache);
	json_object_put(dnschk_mono);
	json_object_put(apn_auto_tried);
	json_object_put(auto_connect_tried);
	json_object_put(deactivated);
	json_object_put(iface_retries);
	json_object_put(reg_denied);
	uloop_timeout_cancel(&iface_retry_timer);
	radio_mgr_stop();
	json_object_put(mm_return);
	json_object_put(mm_return_tried);
	json_object_put(cut_nodes);
	json_object_put(stuck_nodes);
	json_object_put(user_ifaces);
	json_object_put(webfwd_open);
	if (hook.running && hook.proc.pid > 0)
		kill(-hook.proc.pid, SIGKILL);
	esim_mgr_stop();
	sim_mgr_stop();
	web_mgr_stop();
	redirect_mgr_stop();
	usbconf_stop();
	usbtree_stop();
	json_object_put(qmodem_img);
	json_object_put(qmodem_img_old);
	json_object_put(probes);
	{
		int k;
		for (k = 0; k < UPLINK_JOBS; k++)
			if (reach_jobs[k].active)
				reach_close(&reach_jobs[k]);
		if (dns_job.active) {
			if (dns_job.fd.registered)
				uloop_fd_delete(&dns_job.fd);
			close(dns_job.fd.fd);
			uloop_timeout_cancel(&dns_job.timer);
		}
	}
	uplink_save();
	quota_mgr_stop();
	downtime_mgr_stop();
	traffic_mgr_stop();
	mmlog_stop();
	neighbours_stop();
	alert_mgr_stop();
	wlmode_mgr_stop();
	clash_mgr_stop();
	procs_stop();
	kfault_stop();
	sms_seen_stop();
	signal_mgr_stop();
	failover_restore_all();
	json_object_put(fo_state);
	json_object_put(fo_events);
	json_object_put(fo_routes);
	json_object_put(uplinks);
	json_object_put(usb_power);
	json_object_put(usb_txstall);
	json_object_put(iface_checks);
	json_object_put(apn_cache);
	json_object_put(sim_secret);
	known_save();
	json_object_put(known_models);
	free(apn_data.db);
	free(apn_data.overrides);
	json_object_put(iface_tried);
	json_object_put(mmuci_img);
	json_object_put(defaults);
	json_object_put(restricted_defaults);
	json_object_put(restricted_none);
	mm_close_out();
	uloop_done();
	history_save();
	blob_buf_free(&b);
	blob_buf_free(&eb);
	free(mm_buf);
	json_object_put(mm_devices);
	json_object_put(mm_unmapped);
	json_object_put(mm_conflicts);
	json_object_put(mm_absent);
	json_object_put(mm_reported);
	sms_mgr_stop();
	at_hist_stop();
	foreign_stop();
	json_object_put(tty_holders);
	json_object_put(holders_known);
	/* the forwards to the modems' pages leave with the engine: a stopped service forwards nothing */
	if ((*webfwd_applied && strcmp(webfwd_applied, "?")) || *webfwd_pending) {
		const char *slash = strrchr(route_helper, '/');
		char path[PATH_MAX];
		const char *argv[] = { path, "clear", NULL };
		pid_t pid;

		snprintf(path, sizeof(path), "%.*sweb-forward", slash ? (int)(slash - route_helper + 1) : 0, route_helper);
		if ((pid = failover_exec(argv)) > 0)
			waitpid(pid, NULL, 0);
	}
	free(mm_rules_text);
	json_object_put(stab_state);
	mm_leases_free(&leases);
	mm_history_free(&history);
	mm_registry_free(&registry);
	ulog_close();
	return 0;
}
