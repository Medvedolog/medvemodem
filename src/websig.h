/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_WEBSIG_H
#define MEDVEMODEM_WEBSIG_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/*
 * The signal from a modem's own web page (HANDOFF 162, 164): Huawei HiLink (/api/...) and ZTE (/goform/...)
 * modems with no AT port at all. Read in a vendor firmware and in field reports.
 *
 * The signal, the network, the model and the firmware. Never the IMEI, IMSI, ICCID or phone number. SMS only
 * through mm_web_sms (never logged, never stored). A login is made only when the page refuses to answer without
 * one, with the operator's password or the web's own default ("admin"), and at most once in MM_WEB_LOGIN_EVERY_S
 * per modem: a wrong password must not lock the modem's web. A web that says it is locked, or that someone else is logged in,
 * is not tried. The session cookie and the time of the last login try live in a state file of the modem's own
 * (0600), so two modems never share one.
 *
 * mm_web_signal answers
 *   {"kind": "huawei"|"zte", "host", "auth": "none"|"ok"|"needed"|"wrong"|"locked"|"busy"|"waiting",
 *    "wait_s", "metrics": {...}, "error"}
 * auth: none — no login was needed; ok — logged in; needed — the default password was refused (the operator
 * is asked for the modem's one); wrong — the operator's password was refused; locked — the web locked logins
 * (wait_s); busy — someone else is logged in; waiting — the last try was less than MM_WEB_LOGIN_EVERY_S ago.
 * metrics: the AT reader's names (rssi_dbm, rsrp_dbm, rsrq_db, sinr_db, operator, operator_code,
 * access_technology, model, firmware) and band, earfcn, pci, cell_id, enb, tac, bandwidth_mhz, rscp_dbm,
 * ecio_db; a 5G cell in "nr" {rsrp_dbm, rsrq_db, sinr_db, pci, arfcn, band}: an NSA modem keeps both.
 */

#define MM_WEB_LOGIN_EVERY_S 300

struct mm_web_args {
	const char *host;	/* an IPv4 address, ":port" for tests */
	const char *dev;	/* the modem's network device (SO_BINDTODEVICE), or NULL */
	const char *kind;	/* "huawei", "zte" or "auto" */
	const char *password;	/* the operator's, or NULL/"" for the web's default */
	const char *state;	/* the modem's state file */
	long long now;		/* wall clock seconds */
	bool fresh;		/* the operator gave a new password: tried without waiting (a locked web still waits) */
};

json_object *mm_web_signal(const struct mm_web_args *a);

/*
 * r94: SMS of a HiLink modem through its web: op "list" -> "messages" [{id, number, text, state received|sent,
 * type deliver|submit, storage "web", unread?, timestamp, epoch}], "send" (number, text) -> "ok", "delete"
 * (index) -> "ok"; "kind", "auth", "wait_s" and "error" as mm_web_signal. HiLink /api/sms and, from r95, ZTE
 * goform (UCS-2 hex texts; state "unsent" for a message the web could not send).
 */
json_object *mm_web_sms(const struct mm_web_args *a, const char *op, const char *number, const char *text, int index);

/*
 * r117: more of a HiLink web: op "info" -> wan_ip, wan_ipv6, traffic {session, month, total: {rx, tx, s}, month.cleared},
 * device {name, firmware, hardware, webui}; "ussd" (code) -> answer; "reboot" -> ok. "kind", "auth", "wait_s" and
 * "error" as mm_web_signal. Never the IMEI, IMSI, serial or phone number; the USSD answer is never logged.
 */
json_object *mm_web_act(const struct mm_web_args *a, const char *op, const char *code);
/* kind "mark" (a «Mobile.Router» web: MegaFon MF150-4, 05c6:f00e; the owner's HAR 2026-10-30): no login; op "info"
 * (wan_ip, traffic {session, total}, device), "reboot" (its restart, never the factory reset), "subnet" (code: the
 * stick's new a.b.c.1; its login read from its own settings page and posted back, never kept) -> ok, address */

/* for tests */
json_object *mm_web_hilink_metrics(const char *signal, const char *info, const char *plmn);
json_object *mm_web_zte_metrics(json_object *z);
json_object *mm_web_mark_metrics(const char *home, const char *title);
json_object *mm_web_uz_metrics(json_object *status, json_object *info);
/* a Yota stick's /status («Key=Value» lines): levels, the cell, the operator; the IMSI only as the SIM's operator code */
json_object *mm_web_yota_metrics(const char *body);
void mm_md5_hex(const char *s, size_t n, char out[33]);	/* for tests */
int mm_web_xml_tag(const char *xml, const char *tag, char *out, size_t size);

#endif
