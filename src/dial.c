#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "at.h"
#include "dial.h"

static json_object *get(json_object *o, const char *k)
{
	json_object *v = NULL;

	return o && json_object_object_get_ex(o, k, &v) ? v : NULL;
}

const char *mm_dial_pdp(const char *iptype)
{
	if (!iptype || !*iptype || !strcmp(iptype, "auto") || !strcmp(iptype, "ipv4"))
		return "IP";
	if (!strcmp(iptype, "ipv6"))
		return "IPV6";
	if (!strcmp(iptype, "ipv4v6"))
		return "IPV4V6";
	return NULL;
}

static bool apn_ok(const char *apn)
{
	size_t n = 0;

	for (; apn[n]; n++)
		if (!isalnum((unsigned char)apn[n]) && apn[n] != '.' && apn[n] != '-' && apn[n] != '_')
			return false;
	return n <= 63;
}

/* one template with {cid}, {pdp}, {apn} filled; false on an unknown placeholder or overflow */
static bool fill(const char *tpl, int cid, const char *pdp, const char *apn, char *out, size_t size)
{
	size_t o = 0;

	while (*tpl) {
		char piece[80];
		const char *add = NULL;

		if (*tpl == '{') {
			if (!strncmp(tpl, "{cid}", 5)) {
				snprintf(piece, sizeof(piece), "%d", cid);
				add = piece;
				tpl += 5;
			} else if (!strncmp(tpl, "{pdp}", 5)) {
				add = pdp;
				tpl += 5;
			} else if (!strncmp(tpl, "{apn}", 5)) {
				add = apn;
				tpl += 5;
			} else {
				return false;
			}
			if (o + strlen(add) + 1 > size)
				return false;
			memcpy(out + o, add, strlen(add));
			o += strlen(add);
			continue;
		}
		if (*tpl == '\r' || *tpl == '\n' || o + 2 > size)
			return false;
		out[o++] = *tpl++;
	}
	out[o] = '\0';
	return true;
}

static bool add_filled(json_object *list, const char *tpl, int cid, const char *pdp, const char *apn)
{
	char line[256];

	if (!tpl || strncmp(tpl, "AT", 2) || !fill(tpl, cid, pdp, apn, line, sizeof(line)))
		return false;
	json_object_array_add(list, json_object_new_string(line));
	return true;
}

json_object *mm_dial_commands(json_object *recipe, const char *which, const char *apn, const char *pdp,
			      char *err, size_t err_size)
{
	json_object *list, *cid = get(recipe, "cid"), *steps;
	bool hangup = which && !strcmp(which, "hangup");
	size_t i;

	err[0] = '\0';
	if (!recipe || !json_object_is_type(cid, json_type_int) || !get(recipe, "transport") ||
	    strcmp(json_object_get_string(get(recipe, "transport")), "at")) {
		snprintf(err, err_size, "the profile has no usable AT dial recipe");
		return NULL;
	}
	if (!apn)
		apn = "";
	if (!apn_ok(apn)) {
		snprintf(err, err_size, "APN may only hold letters, digits, '.', '-' and '_' (at most 63)");
		return NULL;
	}
	if (!pdp || (strcmp(pdp, "IP") && strcmp(pdp, "IPV6") && strcmp(pdp, "IPV4V6"))) {
		snprintf(err, err_size, "PDP type must be IP, IPV6 or IPV4V6");
		return NULL;
	}
	list = json_object_new_array();
	steps = get(recipe, hangup ? "teardown" : "setup");
	for (i = 0; i < json_object_array_length(steps); i++)
		if (!add_filled(list, json_object_get_string(json_object_array_get_idx(steps, i)),
				json_object_get_int(cid), pdp, apn))
			goto bad;
	if (!hangup) {
		if (!add_filled(list, json_object_get_string(get(get(recipe, "address"), "command")), json_object_get_int(cid), pdp, apn))
			goto bad;
		if (get(recipe, "dns") &&
		    !add_filled(list, json_object_get_string(get(get(recipe, "dns"), "command")), json_object_get_int(cid), pdp, apn))
			goto bad;
	}
	if (!json_object_array_length(list))
		goto bad;
	return list;
bad:
	json_object_put(list);
	snprintf(err, err_size, "the dial recipe has a malformed command");
	return NULL;
}

/* IPv4 addresses on the response line that starts with prefix, in order, 0.0.0.0 left out */
static json_object *ipv4_on_line(const char *raw, const char *prefix)
{
	json_object *out = json_object_new_array();
	const char *line = raw ? strstr(raw, prefix) : NULL;
	const char *end, *p;

	if (!line)
		return out;
	end = strpbrk(line, "\r\n");
	if (!end)
		end = line + strlen(line);
	for (p = line + strlen(prefix); p < end; p++) {
		char cand[16];
		size_t n = 0;
		struct in_addr a;

		if (!isdigit((unsigned char)*p))
			continue;
		/* the whole run of digits and dots: "1.2.3.4" is an address, a cid or a
		   dotted IPv6 / address-and-mask run like "10.1.2.3.255.255.255.0" is not */
		while (p + n < end && (isdigit((unsigned char)p[n]) || p[n] == '.'))
			n++;
		if (n < sizeof(cand)) {
			memcpy(cand, p, n);
			cand[n] = '\0';
			if (inet_pton(AF_INET, cand, &a) == 1 && a.s_addr != 0)
				json_object_array_add(out, json_object_new_string(cand));
		}
		p += n;
	}
	return out;
}

json_object *mm_dial_result(json_object *recipe, json_object *responses, const char *apn, const char *pdp,
			    char *err, size_t err_size)
{
	json_object *cmds = mm_dial_commands(recipe, "dial", apn, pdp, err, err_size), *steps, *addr, *out, *dns;
	json_object *addressing = get(recipe, "addressing");
	size_t i, nsetup;
	int plen;
	char gws[16];

	if (!cmds)
		return NULL;
	steps = get(recipe, "setup");
	nsetup = json_object_array_length(steps);
	for (i = 0; i < nsetup; i++) {
		const char *cmd = json_object_get_string(json_object_array_get_idx(cmds, i));
		json_object *raw = get(responses, cmd), *p;

		if (!raw) {
			snprintf(err, err_size, "%s: no answer", cmd);
			json_object_put(cmds);
			return NULL;
		}
		p = mm_at_parse(cmd, json_object_get_string(raw));
		if (!json_object_get_boolean(get(p, "ok"))) {
			snprintf(err, err_size, "%s: %s", cmd, get(p, "final") ? json_object_get_string(get(p, "final")) : "no final result");
			json_object_put(p);
			json_object_put(cmds);
			return NULL;
		}
		json_object_put(p);
	}
	addr = ipv4_on_line(json_object_get_string(get(responses, json_object_get_string(json_object_array_get_idx(cmds, nsetup)))),
			    json_object_get_string(get(get(recipe, "address"), "prefix")));
	if (!json_object_array_length(addr)) {
		snprintf(err, err_size, "%s: no IPv4 address", json_object_get_string(json_object_array_get_idx(cmds, nsetup)));
		json_object_put(addr);
		json_object_put(cmds);
		return NULL;
	}
	plen = json_object_get_int(get(addressing, "prefix_len"));
	if (plen < 8 || plen > 30)
		plen = 24;
	mm_dial_first_host(json_object_get_string(json_object_array_get_idx(addr, 0)), plen, gws);

	out = json_object_new_object();
	json_object_object_add(out, "ipv4", json_object_get(json_object_array_get_idx(addr, 0)));
	json_object_object_add(out, "prefix_len", json_object_new_int(plen));
	json_object_object_add(out, "gateway", json_object_new_string(gws));
	dns = get(recipe, "dns") ? ipv4_on_line(json_object_get_string(get(responses, json_object_get_string(
						json_object_array_get_idx(cmds, nsetup + 1)))),
					  json_object_get_string(get(get(recipe, "dns"), "prefix")))
				 : json_object_new_array();
	json_object_object_add(out, "dns", dns);
	json_object_put(addr);
	json_object_put(cmds);
	return out;
}

json_object *mm_dial_ipv4s(const char *raw, const char *prefix)
{
	return ipv4_on_line(raw, prefix);
}

void mm_dial_first_host(const char *ipv4, int plen, char out[16])
{
	struct in_addr a, net, gw;
	uint32_t mask, host;

	out[0] = '\0';
	if (!ipv4 || inet_pton(AF_INET, ipv4, &a) != 1 || plen < 1 || plen > 30)
		return;
	mask = htonl(~0u << (32 - plen));
	net.s_addr = a.s_addr & mask;
	host = ntohl(net.s_addr) + 1;
	if (htonl(host) == a.s_addr)
		host++;
	gw.s_addr = htonl(host);
	inet_ntop(AF_INET, &gw, out, 16);
}

/* ---- the 3GPP dialogue ---- */

/* the fields of the first line starting with prefix (quotes removed), NULL when no such line; new array */
static json_object *fields_of(const char *raw, const char *prefix, const char **next)
{
	const char *line = raw, *end, *p;
	json_object *out;
	char f[128];
	size_t n = 0;
	bool quoted = false;

	while (line && *line) {
		while (*line == '\r' || *line == '\n' || *line == ' ')
			line++;
		if (!strncmp(line, prefix, strlen(prefix)))
			break;
		line = strpbrk(line, "\r\n");
	}
	if (!line || !*line)
		return NULL;
	end = strpbrk(line, "\r\n");
	if (!end)
		end = line + strlen(line);
	if (next)
		*next = end;
	out = json_object_new_array();
	for (p = line + strlen(prefix); p <= end; p++) {
		if (p == end || (*p == ',' && !quoted)) {
			while (n && f[n - 1] == ' ')
				n--;
			f[n] = '\0';
			json_object_array_add(out, json_object_new_string(f));
			n = 0;
			continue;
		}
		if (*p == '"') {
			quoted = !quoted;
			continue;
		}
		if ((*p == ' ' && !n) || n + 1 >= sizeof(f))
			continue;
		f[n++] = *p;
	}
	return out;
}

static const char *field(json_object *f, size_t i)
{
	return f && i < json_object_array_length(f) ? json_object_get_string(json_object_array_get_idx(f, i)) : NULL;
}

static bool is_num(const char *s)
{
	if (!s || !*s)
		return false;
	for (; *s; s++)
		if (!isdigit((unsigned char)*s))
			return false;
	return true;
}

static bool ipv4_ok(const char *s)
{
	struct in_addr a;

	return s && strlen(s) < 16 && inet_pton(AF_INET, s, &a) == 1 && a.s_addr != 0;
}

int mm_dial_cops(const char *raw, char plmn[8])
{
	json_object *f = fields_of(raw, "+COPS:", NULL);
	int mode = -1;

	plmn[0] = '\0';
	if (!f)
		return -1;
	if (is_num(field(f, 0)))
		mode = atoi(field(f, 0));
	if (is_num(field(f, 2)) && (strlen(field(f, 2)) == 5 || strlen(field(f, 2)) == 6))
		snprintf(plmn, 8, "%s", field(f, 2));
	json_object_put(f);
	return mode;
}

int mm_dial_reg(const char *raw)
{
	static const char *const prefixes[] = { "+CEREG:", "+C5GREG:", "+CREG:", "+CGREG:" };
	size_t i;

	for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
		json_object *f = fields_of(raw, prefixes[i], NULL);
		const char *st;
		int r = -1;

		if (!f)
			continue;
		/* the query answer is <n>,<stat>[,...]; a bare <stat> is the unsolicited form */
		st = json_object_array_length(f) >= 2 ? field(f, 1) : field(f, 0);
		if (is_num(st))
			r = atoi(st);
		json_object_put(f);
		return r;
	}
	return -1;
}

bool mm_dial_cgact(const char *raw, int cid)
{
	const char *p = raw;
	json_object *f;

	while (p && (f = fields_of(p, "+CGACT:", &p))) {
		bool hit = is_num(field(f, 0)) && atoi(field(f, 0)) == cid && is_num(field(f, 1)) && atoi(field(f, 1)) == 1;

		json_object_put(f);
		if (hit)
			return true;
	}
	return false;
}

bool mm_dial_cgdcont(const char *raw, int cid, char *pdp, size_t pdp_size, char *apn, size_t apn_size)
{
	const char *p = raw;
	json_object *f;

	while (p && (f = fields_of(p, "+CGDCONT:", &p))) {
		bool hit = is_num(field(f, 0)) && atoi(field(f, 0)) == cid;

		if (hit) {
			snprintf(pdp, pdp_size, "%s", field(f, 1) ? field(f, 1) : "");
			snprintf(apn, apn_size, "%s", field(f, 2) ? field(f, 2) : "");
		}
		json_object_put(f);
		if (hit)
			return true;
	}
	return false;
}

bool mm_dial_cgpaddr(const char *raw, char ipv4[16])
{
	json_object *a = ipv4_on_line(raw, "+CGPADDR:");
	bool ok = json_object_array_length(a) > 0;

	snprintf(ipv4, 16, "%s", ok ? json_object_get_string(json_object_array_get_idx(a, 0)) : "");
	json_object_put(a);
	return ok;
}

int mm_dial_cgatt(const char *raw)
{
	json_object *f = fields_of(raw, "+CGATT:", NULL);
	int r = f && is_num(field(f, 0)) ? atoi(field(f, 0)) : -1;

	json_object_put(f);
	return r;
}

bool mm_dial_cgcontrdp(const char *raw, char gw[16], json_object *dns)
{
	const char *p = raw;
	json_object *f;

	gw[0] = '\0';
	/* <cid>,<bearer>,<apn>,<local address and mask>,<gateway>,<DNS>,<DNS>,...: one line per address family */
	while (p && (f = fields_of(p, "+CGCONTRDP:", &p))) {
		bool any = false;

		if (ipv4_ok(field(f, 4))) {
			snprintf(gw, 16, "%s", field(f, 4));
			any = true;
		}
		if (ipv4_ok(field(f, 5))) {
			json_object_array_add(dns, json_object_new_string(field(f, 5)));
			any = true;
		}
		if (ipv4_ok(field(f, 6))) {
			json_object_array_add(dns, json_object_new_string(field(f, 6)));
			any = true;
		}
		json_object_put(f);
		if (any)
			return true;
	}
	return false;
}

json_object *mm_dial_job(json_object *recipe, const char *apn, const char *pdp, bool cold, const char *plmn_was,
			 int budget_ms, char *err, size_t err_size)
{
	json_object *job, *cid = get(recipe, "cid"), *dns = get(recipe, "dns");
	char line[256];

	err[0] = '\0';
	if (!recipe || !json_object_is_type(cid, json_type_int) || !get(recipe, "transport") ||
	    strcmp(json_object_get_string(get(recipe, "transport")), "at")) {
		snprintf(err, err_size, "the profile has no usable AT dial recipe");
		return NULL;
	}
	if (!apn)
		apn = "";
	if (!apn_ok(apn)) {
		snprintf(err, err_size, "APN may only hold letters, digits, '.', '-' and '_' (at most 63)");
		return NULL;
	}
	if (!pdp || (strcmp(pdp, "IP") && strcmp(pdp, "IPV6") && strcmp(pdp, "IPV4V6"))) {
		snprintf(err, err_size, "PDP type must be IP, IPV6 or IPV4V6");
		return NULL;
	}
	job = json_object_new_object();
	json_object_object_add(job, "op", json_object_new_string("dial"));
	json_object_object_add(job, "flow", json_object_new_string("3gpp"));
	json_object_object_add(job, "cid", json_object_new_int(json_object_get_int(cid)));
	json_object_object_add(job, "apn", json_object_new_string(apn));
	json_object_object_add(job, "pdp", json_object_new_string(pdp));
	/* IPv6 alone has nothing to fall back to: its address is not the one this dial looks for */
	if (strcmp(pdp, "IPV6"))
		json_object_object_add(job, "alt", json_object_new_string(strcmp(pdp, "IP") ? "IP" : "IPV4V6"));
	json_object_object_add(job, "cold", json_object_new_boolean(cold));
	if (plmn_was && *plmn_was)
		json_object_object_add(job, "plmn_was", json_object_new_string(plmn_was));
	json_object_object_add(job, "budget_ms", json_object_new_int(budget_ms));
	if (dns && fill(json_object_get_string(get(dns, "command")), json_object_get_int(cid), pdp, apn, line, sizeof(line)) &&
	    !strncmp(line, "AT", 2) && get(dns, "prefix")) {
		json_object_object_add(job, "dns_cmd", json_object_new_string(line));
		json_object_object_add(job, "dns_prefix", json_object_get(get(dns, "prefix")));
	}
	/*
	 * A family's own steps (Intel XMM over NCM): its login
	 * command, commands before the activation (AT+XDNS, or +XDNS? stays empty), and commands for the data port once
	 * an address is there (AT+XDATACHANNEL, AT+CGDATA="M-RAW_IP": without them the address carries nothing).
	 */
	if (get(recipe, "login") && !strcmp(json_object_get_string(get(recipe, "login")), "xgauth"))
		json_object_object_add(job, "login", json_object_new_string("xgauth"));
	{
		static const char *const lists[][2] = { { "before_activate", "pre" }, { "data_port", "post" } };
		size_t l, i;

		for (l = 0; l < 2; l++) {
			json_object *src = get(recipe, lists[l][0]), *out;

			if (l == 1)
				src = get(src, "commands");
			if (!src)
				continue;
			out = json_object_new_array();
			for (i = 0; i < json_object_array_length(src); i++)
				if (!add_filled(out, json_object_get_string(json_object_array_get_idx(src, i)),
						json_object_get_int(cid), pdp, apn)) {
					json_object_put(out);
					json_object_put(job);
					snprintf(err, err_size, "the profile's dial recipe has a bad command in %s", lists[l][0]);
					return NULL;
				}
			json_object_object_add(job, lists[l][1], out);
		}
	}
	return job;
}

static bool cred_ok(const char *s)
{
	size_t n = 0;

	for (; s && *s; s++, n++)
		if ((unsigned char)*s < 0x20 || *s == 0x7f || *s == '"' || *s == '\\')
			return false;
	return n <= 64;
}

bool mm_dial_job_auth(json_object *job, const char *auth, const char *user, const char *pass, char *err, size_t err_size)
{
	int prot;

	err[0] = '\0';
	if (!auth || !*auth || !strcmp(auth, "none"))
		return true;
	prot = !strcmp(auth, "pap") || !strcmp(auth, "both") ? 1 : !strcmp(auth, "chap") ? 2 : 0;
	if (!prot) {
		snprintf(err, err_size, "option auth must be pap, chap, both or none");
		return false;
	}
	if (!cred_ok(user) || !cred_ok(pass)) {
		snprintf(err, err_size, "the APN user name and password may not hold quotes, backslashes or control characters "
			 "and are at most 64 characters");
		return false;
	}
	json_object_object_add(job, "auth", json_object_new_int(prot));
	json_object_object_add(job, "username", json_object_new_string(user ? user : ""));
	json_object_object_add(job, "password", json_object_new_string(pass ? pass : ""));
	return true;
}

int mm_dial_cgauth(const char *raw, int cid, char *user, size_t user_size)
{
	const char *p = raw;
	json_object *f;

	user[0] = '\0';
	while (p && (f = fields_of(p, "+CGAUTH:", &p))) {
		int prot = is_num(field(f, 0)) && atoi(field(f, 0)) == cid && is_num(field(f, 1)) ? atoi(field(f, 1)) : -1;

		if (prot >= 0)
			snprintf(user, user_size, "%s", field(f, 2) ? field(f, 2) : "");
		json_object_put(f);
		if (prot >= 0)
			return prot;
	}
	return -1;
}

json_object *mm_dial_flow_result(json_object *recipe, json_object *run, char *err, size_t err_size)
{
	json_object *out, *addressing = get(recipe, "addressing"), *v;
	const char *ip = get(run, "ipv4") ? json_object_get_string(get(run, "ipv4")) : NULL;
	const char *gw = get(run, "gw") ? json_object_get_string(get(run, "gw")) : NULL;
	int plen = json_object_get_int(get(addressing, "prefix_len"));
	char first[16];

	err[0] = '\0';
	if (!ipv4_ok(ip)) {
		snprintf(err, err_size, "no IPv4 address");
		return NULL;
	}
	if (plen < 8 || plen > 30)
		plen = 24;
	out = json_object_new_object();
	json_object_object_add(out, "ipv4", json_object_new_string(ip));
	json_object_object_add(out, "prefix_len", json_object_new_int(plen));
	/* the network's gateway, else the recipe's rule: on-link (none: the default route on the link) or the first host */
	if (ipv4_ok(gw)) {
		json_object_object_add(out, "gateway", json_object_new_string(gw));
	} else if (get(addressing, "gateway") && !strcmp(json_object_get_string(get(addressing, "gateway")), "first-host")) {
		mm_dial_first_host(ip, plen, first);
		json_object_object_add(out, "gateway", json_object_new_string(first));
	}
	json_object_object_add(out, "dns", (v = get(run, "dns")) ? json_object_get(v) : json_object_new_array());
	/* a link that answers no ARP (XMM NCM): the handler turns it off, or the address carries nothing */
	if ((v = get(addressing, "arp")) && json_object_is_type(v, json_type_boolean) && !json_object_get_boolean(v))
		json_object_object_add(out, "arp", json_object_new_boolean(false));
	json_object_object_add(out, "pdp", json_object_get(get(run, "pdp")));
	json_object_object_add(out, "reused", json_object_new_boolean(json_object_get_boolean(get(run, "reused"))));
	if (get(run, "plmn"))
		json_object_object_add(out, "plmn", json_object_get(get(run, "plmn")));
	return out;
}
