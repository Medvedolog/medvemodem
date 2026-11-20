/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_DAEMON_H
#define MEDVEMODEM_DAEMON_H

/*
 * Inside medvemodemd: what the engine core (medvemodemd.c) shares with its
 * state managers (mgr_*.c), and the managers' own interfaces. Not a library
 * interface: one daemon, one event loop, no locking.
 *
 * A manager owns one piece of state: it alone changes it, loads it at start,
 * saves it and frees it at stop. The core calls it at the moments named here
 * and asks it for views; nothing else touches the state.
 */

#include <stdbool.h>
#include <stddef.h>

#include <json-c/json.h>
#include <libubox/blobmsg_json.h>
#include <libubox/uloop.h>
#include <libubus.h>

#include "history.h"
#include "identity.h"
#include "lease.h"
#include "mm_paths.h"
#include "registry.h"

/* ---- the core ---- */

/* the refusal for a firmware that never answers USSD (the profile's hints.ussd) */
#define MM_USSD_UNSUPPORTED "this modem's firmware does not answer USSD (known for the FM350: the network never replies)"

extern struct mm_registry registry;
bool kernel_has_driver(const char *driver);	/* medvemodemd.c: the kernel registers this USB driver */
extern struct mm_history history;
extern bool history_dirty;		/* history_save() at the next chance */
extern const char *state_dir;		/* survives a reboot (flash) */
extern const char *run_dir;		/* /tmp/medvemodem (RAM) */
extern int uplink_override_ms;		/* -N, tests: every period of the uplink and the managers' timers */

long long now_mono_ms(void);
const char *jstr(json_object *o, const char *key);
/* a reading's keys without a value go (telemetry contract: no data, no key; a 0 stays); the object itself, for chaining */
json_object *metrics_clean(json_object *m);
void broadcast(const char *type, json_object *o);
int reply_json(struct ubus_context *ctx, struct ubus_request_data *req, const char *key, json_object *value);
json_object *served(json_object *rec);
const char *daemon_modem_name(const char *id);
/* the SIM read again soon: an eSIM profile switch changed it (the badge said the old operator for up to 30 min) */
void daemon_sim_reread(const char *id);
/* one background reading of the levels for the history of each modem QModem or the engine's AT reads */
void signal_history_read(void);
/* the name, else the model, else the USB product, else «USB 1.2»: what a short text calls a modem; buf for the last */
const char *daemon_modem_label(const char *id, char *buf, size_t size);
json_object *present_record(struct blob_attr *idattr);
json_object *medvemodem_uci(void);
json_object *core_uci(void);

extern const char *dev_dir;		/* /dev (tests: elsewhere) */
extern const char *conf_dir;
extern const char *mmcli_path;
extern int budget_override_ms;		/* -B, tests: every job's budget */
const char *core_opt(const char *name, const char *dflt);	/* UCI medvemodem.core */
void helper_path(const char *name, char *buf, size_t size);	/* <libexec>/medvemodem/<name>, beside route-metric */
const char *ledger_backend(const char *id);	/* who owns the modem by the ledger, or NULL */
bool rec_busy(const char *id);		/* a recovery attempt runs on the device */
bool frozen(const char *id);		/* «Заморозить»: no service touches the modem */
bool bringup_busy(const char *id);	/* a bring-up or a release of the device runs */
bool netifd_do(const char *iface, const char *method);	/* ubus network.interface.<iface> up|down; false: no answer */
json_object *netifd_status(const char *iface);		/* its netifd status, or NULL (a new reference) */
void schedule_pass(bool now);		/* a discovery pass */

/* tty leases (spec 11.6) */
extern struct mm_leases leases;
void lease_rearm(void);
void leases_revoked(json_object *list, const char *reason);
bool node_still_owned(const char *node, const char *attachment);

/* ModemManager reads */
extern const char *mm_backend;
extern json_object *mm_devices;
extern bool mm_running;
void mm_schedule(int delay_ms);
void mm_maybe_refresh(void);
/* a look-driven ModemManager read, held to the gentle poll period (-g) when set */
void mm_schedule_look(void);
/* a modem's state changed by the operator's hand (a SIM unlocked): read it soon, then at the follow-up times */
void mm_reread(void);
void mm_ports_withdraw(const char *id);	/* ModemManager is told the modem's ports went: it stops probing them */
void mm_ports_again(const char *id);	/* ... told them again, its reports counted afresh */

/* uplinks: device_id or "dev:<netdev>" -> {dev, reach, traffic, ...} */
extern json_object *uplinks;

/* AT workers (spec 11.5, 11.6): one job per device and kind */
#define MM_AT_RADIO_MS 3000
#define MM_RADIO_MAX_AGE_MS 600000
enum at_kind { AT_CONSOLE, AT_METRICS, AT_PROBE, AT_DIAL, AT_HANGUP, AT_SMS, AT_RADIO, AT_CELLS, AT_SIM };
struct at_job {
	bool active, deferred, eof, exited, killed;
	enum at_kind kind;
	char device_id[160], node[64], lease_id[24], why[96];
	long long generation, started;
	int status, timeout_ms, settle_ms;
	struct uloop_process proc;
	struct uloop_fd out;
	struct uloop_timeout kill;
	char *buf;
	size_t len, cap;
	struct ubus_context *ctx;
	struct ubus_request_data req;
	json_object *cmds, *queue, *results;
};
#define MM_AT_JOBS 4
extern struct at_job at_jobs[MM_AT_JOBS];
struct at_job *at_slot(const char *device_id, enum at_kind kind, bool *same);
const char *at_lease(struct at_job *j, char *detail, size_t size);
bool at_spawn(struct at_job *j);
void at_finish(struct at_job *j, json_object *reply);
void at_release(struct at_job *j);
extern json_object *radio_cache;	/* device_id -> {generation, at, via, radio} (spec 25.9.12) */
bool at_running(const char *device_id, enum at_kind kind);
int at_refuse(struct ubus_context *ctx, struct ubus_request_data *req, const char *id, const char *why, const char *hazard);
const char *engine_at_port(json_object *srv, const char **why);
bool mm_lock_dialect(const char *dialect);
int at_begin(struct ubus_context *ctx, struct ubus_request_data *req, json_object *rec, enum at_kind kind,
	     const char *node, json_object *cmds, json_object *queue, int timeout_ms, json_object *results);
const char *qmodem_section_of(json_object *rec);
const char *qmodem_foreign_port(json_object *rec, const char *sec);	/* the section's AT port when it is not this modem's */
bool qmodem_installed(void);
const char *qmodem_ctrl_script(char *buf, size_t size);	/* <QModem>/modem_ctrl.sh */

/* ---- mgr_radio.c: changes of the radio settings (spec 25.9.12 stage 2) ----
 * Owns: the last change per device (want, the way back, state). Journalled
 * in <state>/radio-txn.json before the first write and at every step, so a
 * restart keeps the way back (audit F03). A device away keeps its change: it
 * is kept or rolled back once the device is back. */
void radio_mgr_start(void);		/* load the journal: a change caught writing is written back */
void radio_mgr_stop(void);
void radio_mgr_tick(void);		/* each uplink pass: keep a change whose connection came back, roll back after wait_s */
bool radio_txn_done(struct at_job *j, json_object *responses, const char *error, json_object *reply);
json_object *radio_change_view(const char *id);	/* the device record's view, or NULL */
/* The cell-lock guard: a cell lock MedveModem set and kept stays armed in
 * <state>/radio-guard.json; no registration for core.radio_lock_guard minutes (10 by default, 0 off) writes back
 * the settings read before the lock. */
json_object *metrics_cached(const char *id, long long max_age_ms);	/* medvemodemd.c: the AT reading, or NULL */
extern const struct blobmsg_policy radio_set_policy[2], radio_confirm_policy[2];
int m_radio_set(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		const char *method, struct blob_attr *msg);
int m_radio_confirm(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		    const char *method, struct blob_attr *msg);

/* ---- mgr_quota.c: monthly traffic quotas per SIM (spec 20.9) ----
 * Owns: the books per SIM (hash of the ICCID, <state>/sim-usage.json, saved
 * at most every 10 minutes and at stop) and the views of the links counted in
 * the latest uplink pass. A device not counted in a pass (away, disconnected,
 * another SIM, no counters) has no view after it (audit F02). */
void quota_mgr_start(void);
void quota_mgr_stop(void);		/* saves */
void quota_pass_begin(void);
void quota_count(const char *id, json_object *srv, const char *dev, long long bytes, const char *today);
void quota_pass_end(void);		/* drops the views not counted; saves when due */
json_object *quota_of(const char *id);	/* the served quota, or NULL */
json_object *quota_held_devs(void);	/* failover's opts.quota */

/* ---- mgr_sms_seen.c: which received SMS the operator has seen (spec 38.2.2) ----
 * Owns: device_id -> inbox keys seen (hashes, never the text;
 * <state>/sms-seen.json, written when the set changes). A shared mark for
 * every client of sms_list (audit 2026-10-13 section 5.1). */
void sms_seen_start(void);
void sms_seen_stop(void);
bool sms_seen_known(const char *id);	/* the device's inbox was seen once */
void sms_seen_mark(const char *id, json_object *msgs);
json_object *sms_seen_badge(const char *id, json_object *msgs);	/* {"unread", "inbox"} */
json_object *sms_seen_unread_ids(const char *id, json_object *msgs);	/* [ids] of received ones not seen yet, or NULL */

/* ---- mgr_signal.c: the signal history (spec 25.9.13) ----
 * Owns: device_id -> history (RAM only, a reboot clears it) and the minute
 * timer of background reads; the timer is stopped while UCI
 * core.signal_history is '0' and started again by the uplink pass. */
void signal_mgr_start(void);
void sim_mgr_start(void);
void signal_mgr_stop(void);
void signal_retain(json_object *devices);	/* audit M06: drop the ids the registry no longer has */
void signal_mgr_tick(void);		/* each uplink pass */
void signal_sample(const char *id, json_object *mm, long long wall);	/* every ModemManager read */
void signal_sample_reading(const char *id, json_object *m, long long wall);	/* the engine's own readings */
bool signal_history_on(void);		/* core.signal_history not '0' */
extern const struct blobmsg_policy sighist_policy[2];
int m_signal_history(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		     const char *method, struct blob_attr *msg);

/* ---- mgr_sms.c: SMS and USSD (spec 38) ----
 * Owns: the SMS/USSD jobs (mmcli workers, the AT path through at_jobs), the
 * messages read per device (RAM), the USSD sessions and the background SMS
 * pass (UCI core.sms_poll). Nothing on flash but what mgr_sms_seen.c keeps. */
void sms_mgr_start(void);		/* the first pass after ModemManager's first reads */
void sms_mgr_stop(void);
json_object *sms_badge(const char *id);	/* the dashboard's envelope, or NULL */
bool sms_capable(const char *id, json_object *srv, const char **why);	/* SMS/USSD reach the modem now; else why */
const char *sms_center_of(const char *id);
bool ussd_running(const char *id);	/* a USSD request of the modem is on its way: background reads wait */	/* the SMS centre of the SIM's label (config sim, by ICCID), or NULL */
void sms_at_exited(struct at_job *j, json_object *r, const char *error);	/* an AT_SMS job ended */
extern const struct blobmsg_policy sms_policy[6], ussd_policy[3];
int m_sms(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	  const char *method, struct blob_attr *msg);
int m_ussd(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	   const char *method, struct blob_attr *msg);

/* ---- mgr_esim.c: eSIM (docs/ESIM-INTEGRATION.md) ----
 * Owns: the lpac jobs, the eUICC port per device and attachment generation
 * (RAM), the leases they hold. A device with an eSIM job is reserved: the
 * core's fixes, bring-up and recovery leave it alone. */
void esim_set_backend(const char *path);	/* -Y */
void esim_set_timeout(int ms);			/* -G */
void esim_mgr_start(void);
void esim_mgr_stop(void);			/* kills the running jobs */
bool esim_reserved(const char *id);
extern const struct blobmsg_policy esim_policy[3];
int m_esim_status(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		  const char *method, struct blob_attr *msg);
int m_esim_action(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		  const char *method, struct blob_attr *msg);

/* ---- mgr_at_history.c: the AT console's history (field 2026-10-13) ----
 * Owns: per device the commands of the last day (100 at most) with the answer,
 * RAM only (answers may hold identifiers). Fed by the console and its refusals. */
void at_hist_add(const char *id, const char *cmd, json_object *reply);
void at_hist_retain(json_object *devices);
void at_hist_stop(void);
extern const struct blobmsg_policy at_history_policy[2];
int m_at_history(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg);

/* ---- mgr_sim.c: SIM unlock through ModemManager (PIN, PUK) ----
 * Owns: the unlock jobs (mmcli workers, RAM). The codes live only in the
 * request and the worker's arguments: never logged, journalled or replied. */
void sim_mgr_stop(void);			/* kills the running jobs */
extern const struct blobmsg_policy sim_unlock_policy[4];
int m_sim_unlock(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg);
extern const struct blobmsg_policy sim_slot_policy[2];
int m_sim_slot(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	       const char *method, struct blob_attr *msg);	/* the SIM slot ModemManager uses */
extern const struct blobmsg_policy reregister_policy[1];
int m_reregister(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg);	/* register in the network again (operator's step) */
extern const struct blobmsg_policy fplmn_policy[2];
int m_fplmn(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	    const char *method, struct blob_attr *msg);	/* the SIM's forbidden networks: read, or clear */

/* ---- mgr_web.c: the signal from a HiLink or ZTE modem's own web (HANDOFF 164) ----
 * Owns: the web readers (CLI workers, RAM) and their last answers. The web
 * password (device section web_password) goes to the worker's stdin only. */
int web_metrics(struct ubus_context *ctx, struct ubus_request_data *req, json_object *rec, json_object *srv, long long max_age);
void web_background(json_object *rec, json_object *srv);	/* a router-mode modem's web, every 30 s with no page */
bool web_password_saved(const char *id);
/* the modem's own web says it has no SIM or no network (a fresh reading); NULL otherwise. The reach check takes it as
 * «down» without probing, and recovery does not reset for it (watch.c: the prefix) */
const char *web_offline(json_object *rec);
const char *web_sim_plmn(json_object *rec);
/* ---- mgr_redirect.c: who cut the connection (r156): one plain HTTP request through the modem ----
 * mm_redirect_foreign: from an HTTP response head, the host a 3xx sends to when it is not the asked one (pure). */
bool mm_redirect_foreign(const char *resp, const char *asked, char *out, size_t size);
void redirect_probe(const char *key, const char *dev, const char *target);	/* target: «a.b.c.d[:port]» */
void redirect_mgr_stop(void);
/* medvemodemd.c: the probe's answer into the path's check (an operator's page host) */
void reach_redirect_found(const char *key, const char *host);
/* r94: a HiLink's SMS through its web (mgr_web.c), answered by the SMS manager (mgr_sms.c) */
bool web_sms_capable(json_object *rec, json_object *srv);
bool web_sms(json_object *rec, json_object *srv, const char *op, const char *number, const char *text, int index,
	     struct ubus_context *ctx, struct ubus_request_data *req, const char **why);
const char *sms_qmodem_section(json_object *rec);	/* r95: QModem's section when the SMS go through QModem */
void sms_web_done(const char *id, const char *op, int index, json_object *r, struct ubus_context *ctx, struct ubus_request_data *req);
/* r117: more of a HiLink's web (mgr_web.c): info and reboot by the page's buttons, USSD answered to mgr_sms.c */
bool web_huawei(json_object *rec, json_object *srv);
bool web_mark(json_object *rec, json_object *srv);
bool web_zte(json_object *rec, json_object *srv);	/* a ZTE goform web (2026-11-03): info, USSD, reconnect, mode, reboot */
bool web_uz(json_object *rec, json_object *srv);	/* a UZ801 UFI's /ajax web the last reading recognised */	/* a «Mobile.Router» web (MF150-4) the last reading recognised */
bool web_act(json_object *rec, json_object *srv, const char *op, const char *code, struct ubus_context *ctx,
	     struct ubus_request_data *req, const char **why);
void ussd_web_done(const char *id, json_object *r);
extern const struct blobmsg_policy web_info_policy[1];
int m_web_info(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
	       struct blob_attr *msg);	/* web_info (read), web_reboot (write) */
extern const struct blobmsg_policy web_do_policy[4];
int m_web_do(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req, const char *method,
	     struct blob_attr *msg);	/* web_do (write): a HiLink's reconnect, counters cleared, mode and bands */
const char *web_model(const char *id);
void web_mgr_stop(void);
void web_retain(json_object *devices);	/* the ids the registry no longer has go (second audit M01) */

/* ---- mgr_modemclass.c: «Это модем / не модем» by hand, by VID:PID (medvemodem.core usb_modem / usb_not_modem) ---- */
void modem_class_sync(void);		/* before each discovery pass: the lists to discovery */
json_object *modem_class_view(void);	/* {suspects, modem, not_modem} for the status */
extern const struct blobmsg_policy usb_class_policy[2];
int m_usb_class(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		const char *method, struct blob_attr *msg);
const char *mm_engine_savedir(void);		/* the engine's UCI staging dir, for a module's own writes */

/* ---- mgr_usbrescue.c: the last resort by hand — the USB controller's unbind/bind, a board's power GPIO ---- */
extern const struct blobmsg_policy usb_rescue_policy[1], usb_rescue_run_policy[4];
int m_usb_rescue(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg);
int m_usb_rescue_run(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		     const char *method, struct blob_attr *msg);

/* ---- mgr_usbtree.c: «Порты и устройства», the USB tree, its history and bounce (docs/PORTS-AND-DEVICES.md) ----
 * Reads sysfs and the kernel log, writes nothing, acts on nothing. */
void usbtree_start(void);		/* the first look now, then once a minute; the kernel log from its end */
void usbtree_sync(void);		/* after each discovery pass */
void usbtree_stop(void);
extern const struct blobmsg_policy usbtree_detail_policy[1];
int m_usbtree(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	      const char *method, struct blob_attr *msg);
int m_usbtree_detail(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		     const char *method, struct blob_attr *msg);

/* ---- mgr_usbconf.c: the operator's USB configuration of a modem (ALPHA1 3, H8) ----
 * Owns: the writes of bConfigurationValue per device (RAM), at most 2 in 10 minutes. */
void usbconf_sync(void);				/* after each discovery pass and each uplink tick */
json_object *usbconf_view(const char *id, json_object *rec);	/* connection.usb_config, or NULL */
void usbconf_stop(void);

/* ---- mgr_foreign.c: a session interface set up outside MedveModem (field 2026-10-27) ----
 * Owns: per device the interface of the last network read (RAM), the hand-over the operator asked for this
 * attachment, and the one withdrawal of the ports from ModemManager per attachment. While the interface is there
 * and not handed over, the core starts no connection and reports nothing to ModemManager for the modem. */
void foreign_note(const char *id, json_object *rec, json_object *network, json_object *ledgers);	/* each interface pass */
void foreign_retain(json_object *devices);
json_object *foreign_of(const char *id);	/* {interface, proto, off?, generation} or NULL (borrowed) */
bool foreign_holds(const char *id);		/* there, and not handed over */
bool foreign_handed(const char *id);		/* handed over this attachment, not rewritten yet */
const char *foreign_adopt(const char *id);	/* the interface the bring-up rewrites for the engine, or NULL */
void foreign_stop(void);
extern const struct blobmsg_policy foreign_handover_policy[1];
int m_foreign_handover(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		       const char *method, struct blob_attr *msg);

/* ---- mgr_downtime.c: the time each modem was without the internet today (2026-10-27) ----
 * Owns: per device the day's book of downtime.c (RAM, mirrored in <run>/downtime.json). Fed by each uplink pass. */
void downtime_mgr_start(void);
void downtime_mgr_stop(void);
void downtime_note(const char *id, const char *state, const char *day);	/* "ok", "limited", "none", "off" or NULL */
void downtime_retain(json_object *devices);
json_object *downtime_of(const char *id);	/* the served view, or NULL (new object) */

/* ---- mgr_traffic.c: traffic and peak speed per modem (spec §42, 2026-11-02) ----
 * Owns: device_id -> trafhist.c's history (RAM, off with core.signal_history '0') and the peaks since the router's
 * boot (mirrored in <run>/traffic-peaks.json). Fed by the uplink pass's 5 s rates, never a modem poll. */
void traffic_mgr_start(void);
void traffic_mgr_stop(void);
void traffic_retain(json_object *devices);
void traffic_sample(const char *id, json_object *traffic, long long wall);	/* after mm_traffic_sample() */
json_object *traffic_peak_of(const char *id);	/* borrowed {rx_bps, tx_bps, rx_at, tx_at}, or NULL */
extern const struct blobmsg_policy trafhist_policy[2];
int m_traffic_history(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		      const char *method, struct blob_attr *msg);

/* ---- mgr_mmlog.c: ModemManager's own words about a port it cannot drive (2026-10-27) ----
 * Owns: the `logread -f` follower and per device the hints (kind, port, time, count; RAM, never the line). */
void mmlog_start(void);
void mmlog_stop(void);
void mmlog_retain(json_object *devices);
json_object *mmlog_of(const char *id);	/* [{kind, port?, at, count}] or NULL (a new reference) */

/* ---- mgr_neighbours.c: other modem helpers on this router (2026-10-28) ----
 * Owns: the last answer of the `neighbours` helper (known packages, programs on modem ports, the operator's scripts),
 * run every 10 min and on request. Read only. */
void neighbours_start(void);
void neighbours_stop(void);
void neighbours_refresh(void);
json_object *neighbours_of(const char *id);	/* [{pid, comm, node, device_id}] on its ports, or NULL (a new reference) */
extern const struct blobmsg_policy neighbours_policy[1];
int m_neighbours(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
		 const char *method, struct blob_attr *msg);

/* ---- mgr_alert.c: SMS alerts to the address book (spec 38.6; the owner, 2026-10-29) ----
 * Owns: alert.c's episodes, the queue of SMS to send and the last sends (RAM). Fed by each uplink pass. */
void alert_note(const char *id, const char *level);	/* a modem's verdict this pass, as downtime_note's */
void alert_tick(const char *default_dev);		/* the end of the pass: the episodes, the SMS */
void alert_mgr_stop(void);
/* spec §43: one received SMS of modem `from` forwarded by SMS to sms_forward's recipients through another modem */
void alert_forward(const char *from, const char *sender, const char *text);
/* spec §43: a forwarded SMS starts with this mark; one received with it is not forwarded again */
#define MM_FORWARD_MARK "\xe2\x86\xaa "	/* "↪ " */
extern const struct blobmsg_policy alerts_policy[2];
int m_alerts(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	     const char *method, struct blob_attr *msg);

/* ---- mgr_wlmode.c: the whitelist mode (spec §44; the owner, 2026-11-03) ----
 * Owns: whether the mode is on, the admin's word, the limited/whole times, the helper's last answer (RAM); the
 * helper's ledger on flash. Fed by each uplink pass with the carrier's reach level. */
void wlmode_tick(const char *level);
json_object *wlmode_view(void);			/* NULL when not set up and off */
void wlmode_mgr_stop(void);
extern const struct blobmsg_policy wlmode_policy[1];
int m_wlmode(struct ubus_context *ctx, struct ubus_object *obj, struct ubus_request_data *req,
	     const char *method, struct blob_attr *msg);

/* ---- mgr_clash.c: a modem's network inside the router's LAN (field 2026-10-30) ----
 * Owns: per device the clash last said in the log (RAM). Asked when a session comes up. */
json_object *clash_check(const char *id, json_object *assessment);	/* subnet.c's clash, or NULL (a new reference) */
void clash_moved(const char *id, json_object *reply);	/* the web's answer to "subnet": the interface brought up again */
void clash_mgr_stop(void);

/* ---- mgr_procs.c: ModemManager and QModem beside the engine (the owner, 2026-10-30) ----
 * Owns: per service the main pid, memory, time up and the last 24 h of restarts and stops (RAM). Looked at every 30 s;
 * a restart or a stop is broadcast as medvemodem.service. */
extern const char *proc_dir, *mm_process;
void procs_start(void);
void procs_stop(void);
void procs_expect(const char *name);	/* "modemmanager" or "qmodem": the engine itself restarts it now */
json_object *procs_status(void);	/* {modemmanager?: {...}, qmodem?: {...}} (a new object) */

/* medvemodemd.c: a router-mode stick's IMEI as its web gave it: kept only as the salted key of the identity */
void identity_web_imei(const char *id, long long generation, const char *imei);

/* ---- mgr_kfault.c: the router's health in the kernel's words (the owner, 2026-10-31) ----
 * Owns: this boot's kinds of kernel trouble (dead USB controller, voltage, stalls, oops ...) and the last troubled
 * boot's, read from the ring every 30 s; says each kind once a boot (medvemodem.kernel). Acts on nothing. */
void kfault_start(void);
void kfault_stop(void);
json_object *kfault_status(void);	/* {kinds, previous_boot?} (a new object) */
/* the salted label key of a SIM by its ICCID (config sim / option key): false with no salt or no ICCID (r12) */
bool sim_label_key_of(const char *iccid, char out[MM_IMEI_KEY_SIZE]);

/* the USB rescue job stopped with the service (mgr_usbrescue.c) */
void usbrescue_stop(void);

#endif
