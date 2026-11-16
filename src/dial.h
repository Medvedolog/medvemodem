#ifndef MEDVEMODEM_DIAL_H
#define MEDVEMODEM_DIAL_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

/*
 * Data session dialled over AT (native backend, spec 19.4). Pure.
 *
 * The recipe is the profile's "dial" block (profiles/schema.json): setup
 * commands, an address query, an optional DNS query, the addressing rule and
 * teardown commands, with the placeholders {cid}, {pdp} and {apn}.
 */

/*
 * The PDP type for the operator's iptype: "ipv4" (or empty/"auto") -> "IP",
 * "ipv6" -> "IPV6", "ipv4v6" -> "IPV4V6"; NULL for anything else.
 */
const char *mm_dial_pdp(const char *iptype);

/*
 * Commands of one run: "dial" (setup, then the address and DNS queries) or
 * "hangup" (teardown), placeholders filled. The APN is checked here: at most 63
 * characters of letters, digits, '.', '-' and '_' (empty is allowed: the network
 * picks one); nothing from it can end the quoted AT string or the line.
 * NULL with err for a bad recipe, APN or PDP type.
 */
json_object *mm_dial_commands(json_object *recipe, const char *which, const char *apn, const char *pdp,
			      char *err, size_t err_size);

/*
 * The outcome of a dial run from its responses ({"<command>": raw}):
 * {"ipv4": "a.b.c.d", "prefix_len": n, "gateway": "a.b.c.1", "dns": [..]}.
 * NULL with err when a setup command did not answer OK (the error names it) or
 * the address query gave no usable IPv4 address (0.0.0.0 counts as none).
 * Gateway "first-host": the first host of the address's prefix, or the second
 * one when the address is that host itself.
 */
json_object *mm_dial_result(json_object *recipe, json_object *responses, const char *apn, const char *pdp,
			    char *err, size_t err_size);

/*
 * The 3GPP dialogue ("flow": "3gpp", field knowledge of other projects' FM350/L860 handlers, HANDOFF 171): the
 * registration is checked (a deregistered modem, +COPS: 2, is told AT+COPS=0), an active context of the same APN
 * and PDP type on the same network is reused (netifd tears an interface down and up for a metric change: the
 * session stays), otherwise the context is defined, released (AT+CGACT=0: a bare re-activation hangs on a context a
 * band change left half-active) and activated only while AT+CGACT? does not list it (activating an active one
 * crashed an FM350, +CME ERROR: 500); no address with IP retries once with IPV4V6 and back (a network that
 * registers yet never activates IPv4 alone). Gateway and DNS from AT+CGCONTRDP, the recipe's DNS query besides.
 *
 * mm_dial_job: the worker's job for one dial, NULL with err as mm_dial_commands. cold: the session must not be
 * reused (a recovery asked for it); plmn_was: the network of the last session of this modem, or NULL.
 */
json_object *mm_dial_job(json_object *recipe, const char *apn, const char *pdp, bool cold, const char *plmn_was,
			 int budget_ms, char *err, size_t err_size);

/*
 * The APN login (option auth, username, password of the interface) added to a job: AT+CGAUTH=<cid>,<prot>,"user","pass"
 * after the context is defined, before it is activated; a held session is reused only when AT+CGAUTH? shows the same
 * protocol and user (a modem reboot forgets the login and the network may still give an address that carries nothing).
 * auth "pap" 1, "chap" 2, "both" 1 (PAP, as other projects send for "both"), "none" or empty: nothing is added.
 * false with err for an unknown auth, or a user or password holding a quote, a backslash, a control character or more
 * than 64 bytes (nothing from them can end the quoted AT string or the line). The error never quotes them.
 */
bool mm_dial_job_auth(json_object *job, const char *auth, const char *user, const char *pass, char *err, size_t err_size);

/* the reply of a 3GPP dialogue run ({"ipv4", "gw"?, "dns", "plmn", "pdp", "reused", "steps"} or "error") */
json_object *mm_dial_flow_result(json_object *recipe, json_object *run, char *err, size_t err_size);

/* answers of one command, parsed (raw as the modem sent it, echo and final code included) */
int mm_dial_cops(const char *raw, char plmn[8]);		/* +COPS mode (0 auto, 1 manual, 2 deregistered), -1 */
int mm_dial_reg(const char *raw);				/* <stat> of +CEREG/+C5GREG/+CREG, -1 when none */
bool mm_dial_cgact(const char *raw, int cid);			/* +CGACT lists cid active */
bool mm_dial_cgdcont(const char *raw, int cid, char *pdp, size_t pdp_size, char *apn, size_t apn_size);
bool mm_dial_cgpaddr(const char *raw, char ipv4[16]);		/* the first IPv4 of +CGPADDR, 0.0.0.0 is none */
int mm_dial_cgatt(const char *raw);				/* +CGATT: 1 / 0, -1 */
/* +CGAUTH: <cid>,<prot>[,"user"]: the protocol of cid (user filled, "" when none), -1 when cid is not listed */
int mm_dial_cgauth(const char *raw, int cid, char *user, size_t user_size);
/* +CGCONTRDP: the first line with IPv4 values: gateway (field 5) and DNS (6, 7); false when none */
bool mm_dial_cgcontrdp(const char *raw, char gw[16], json_object *dns);
/* the IPv4 addresses on the line starting with prefix, in order, 0.0.0.0 and dotted IPv6 left out; new array */
json_object *mm_dial_ipv4s(const char *raw, const char *prefix);
/* the first host of ipv4/plen, or the second when the address is that host */
void mm_dial_first_host(const char *ipv4, int plen, char out[16]);

#endif
