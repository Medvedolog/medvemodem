#define _GNU_SOURCE
#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "at.h"
#include "never_at.h"

/* Same classes and order as tools/evidence_hazards.py; POSIX ERE, matched on upper case. */
static const char *const rules[][2] = {
	{ "identity-write", "^AT\\+(EGMR(EXT)?|GTSN|LCTSN)=1,7([^0-9]|$)|^AT\\+S?P?IMEI=|^AT\\+SIMEI=|NVIMEI|"
			    "^AT\\^?[A-Z0-9_]*IMEI[A-Z0-9_]*=" },
	{ "persistent-write", "^AT\\+QNVF?W=|^AT\\^EFS=|^AT\\+QPRTPARA=3|NVENCRYPT|^AT@NVM:|^AT@STORE_NVM|"
			      "^AT[+^]CUSTOMER=|^AT\\^NV=|"
			      /* the SIM slot: an empty eSIM slot left the SIM lost across reboots (a reference project's field notes) */
			      "^AT[+^]SWITCH_SLOT=|^AT\\+CEISWITCHSIM=" },
	{ "privileged-unlock", "^AT!ENTERCND=" },
	{ "bootloader", "^AT\\+QFASTBOOT|^AT!BOOTHOLD|^AT\\+QDOWNLOAD|^AT\\+SYSCMD=.*EDL" },
	{ "power-off", "^AT\\+QPOWD|^AT\\+CPWROFF|^AT#SHDN|^AT\\+CPOF" },
	{ "composition", "^AT\\+QCFG=\"(USBNET|USBCFG|DATA_INTERFACE|PCIE/MODE)\",|^AT\\+GTUSBMODE=|^AT!USBCOMP=|"
			 "^AT\\+CUSBCFG=|^AT#USBCFG=|^AT\\+CPCIEMODE=|^AT\\+XDATACHANNEL=|^AT\\+QPCIE=|"
			 "^AT\\+MODESWITCH=|^AT\\+CUSBPIDSWITCH=|^AT[+^](USBSWITCH|SETCONFIG|PCIEMODE)=|"
			 /* 4PDA field reports 2026-10-15: GTDIPCMODE left an FM350 without its AT port; Huawei SETPORT/U2DIAG
			  * rewrite the composition in NV (the T77's ^SETMODE does too, but Huawei's is the network mode: not listed); the RM520's Ethernet AT and driver choice */
			 "^AT\\+GTDIPCMODE=|^AT\\^(SETPORT|U2DIAG)=|^AT\\+QETH=\"(ETH_AT|ETH_DRIVER)\",|^AT\\+QMAPWAC=" },
	{ "reset", "CFUN=1,1([^0-9]|$)|CFUN=1[56]([^0-9]|$)|^AT\\+RESET$|^AT\\^RESET|^AT!RESET|^AT\\+QRST|"
		   "^AT#REBOOT|^AT\\+CRESET" },
};
#define NRULES (sizeof(rules) / sizeof(rules[0]))

static bool matches(const char *pattern, const char *s)
{
	regex_t re;
	bool hit;

	if (regcomp(&re, pattern, REG_EXTENDED | REG_NOSUB))
		return false;
	hit = regexec(&re, s, 0, NULL, 0) == 0;
	regfree(&re);
	return hit;
}

/* "?" / "=?" everywhere; Quectel AT+Q...="name" without a value reads that setting */
static bool is_query(const char *sub)
{
	size_t n = strlen(sub);

	return (n && sub[n - 1] == '?') || matches("^AT\\+Q[A-Z0-9_]+=\"[^\"]*\"$", sub);
}

const char *mm_at_hazard(const char *cmd)
{
	size_t worst = NRULES, r;
	char *copy, *part, *save = NULL;

	if (!cmd)
		return "none";
	copy = strdup(cmd);
	if (!copy)
		return "none";
	for (part = strtok_r(copy, ";", &save); part; part = strtok_r(NULL, ";", &save)) {
		char sub[600], *p;

		while (isspace((unsigned char)*part))
			part++;
		for (p = part + strlen(part); p > part && isspace((unsigned char)p[-1]); )
			*--p = '\0';
		if (!*part)
			continue;
		snprintf(sub, sizeof(sub), "%s%s", strncasecmp(part, "AT", 2) ? "AT" : "", part);
		for (p = sub; *p; p++)
			*p = (char)toupper((unsigned char)*p);
		if (is_query(sub))
			continue;
		for (r = 0; r < worst; r++)
			if (matches(rules[r][1], sub)) {
				worst = r;
				break;
			}
	}
	free(copy);
	return worst < NRULES ? rules[worst][0] : "none";
}

const char *mm_at_console_refusal(const char *cmd)
{
	const char *h, *p;
	char up[16];
	size_t i, n = cmd ? strlen(cmd) : 0;

	if (!n || n > 512)
		return "empty or longer than 512 characters";
	for (p = cmd; *p; p++)
		if (!isprint((unsigned char)*p))
			return "one printable line only";
	if (strncasecmp(cmd, "AT", 2))
		return "not an AT command";
	if (!strncmp(cmd, "A/", 2) || !strncmp(cmd, "a/", 2))
		return "dial, online and prompting commands are not supported on a shared AT port";
	/* every command of the line, not only its head (second audit 2026-10-21: «AT+CSQ;+CMGS="100"» passed and left the
	 * shared port waiting for an SMS text): each part after a «;» is read as AT<part> */
	for (p = cmd + 2; p; p = strchr(p, ';') ? strchr(p, ';') + 1 : NULL) {
		const char *q = p;

		while (*q == ' ')
			q++;
		if (!strncasecmp(q, "AT", 2))
			q += 2;
		strcpy(up, "AT");
		for (i = 2; i < sizeof(up) - 1 && q[i - 2] && q[i - 2] != ';'; i++)
			up[i] = (char)toupper((unsigned char)q[i - 2]);
		up[i] = '\0';
		if (!strncmp(up, "ATD", 3) || !strncmp(up, "ATO", 3) || !strcmp(up, "ATA") || !strncmp(up, "AT+CMGS", 7) ||
		    !strncmp(up, "AT+CMGW", 7) || !strncmp(up, "AT+CMGC", 7))
			return "dial, online and prompting commands are not supported on a shared AT port";
	}
	h = mm_at_hazard(cmd);
	if (strcmp(h, "none")) {
		if (!strcmp(h, "identity-write"))
			return "identity writes are never sent (hazard identity-write)";
		if (!strcmp(h, "reset"))
			return "modem restarts belong to the recovery ladder (hazard reset)";
		if (!strcmp(h, "composition"))
			return "composition changes run only as a validated recipe (hazard composition)";
		return "denied by the hazard list (spec 14.1.4); expert mode does not lift it";
	}
	return NULL;
}

/*
 * ^SETMODE= is the composition switch of the Foxconn T77 modules (HP lt4220, Dell, Telit, Foxconn IDs: a vendor
 * firmware writes at^setmode=1 to the HP's last tty), Huawei's network mode elsewhere: refused on those vendors only.
 */
const char *mm_at_device_refusal(const char *cmd, const char *vidpid)
{
	static const char *const t77_vids[] = { "03f0", "1bc7", "413c", "0489", NULL };
	const char *why = mm_at_console_refusal(cmd), *p;
	size_t i;

	if (why || !vidpid || !cmd)
		return why;
	for (i = 0; t77_vids[i]; i++) {
		if (strncasecmp(vidpid, t77_vids[i], 4))
			continue;
		for (p = cmd; (p = strcasestr(p, "SETMODE")); p += 7)
			if (p[7] == '=' && p[8] != '?')
				return "composition changes run only as a validated recipe (hazard composition)";
	}
	return NULL;
}

const char *const mm_at_metrics_commands[] = {
	"AT+CSQ", "AT+CESQ", "AT+COPS?", "AT+CREG?", "AT+CGREG?", "AT+CEREG?", "AT+C5GREG?",
	"AT+CPIN?", "AT+CFUN?", "AT+CGMI", "AT+CGMM", "AT+CGMR", NULL
};

bool mm_at_is_metrics_command(const char *cmd)
{
	int i;

	for (i = 0; cmd && mm_at_metrics_commands[i]; i++)
		if (!strcasecmp(cmd, mm_at_metrics_commands[i]))
			return true;
	return false;
}

bool mm_at_no_at_port(const char *vidpid, const char *ifnum)
{
	return mm_never_at(vidpid, ifnum);	/* one list, never_at.h: the roles use it too */
}

static bool final_line(const char *l)
{
	static const char *const exact[] = { "OK", "ERROR", "NO CARRIER", "BUSY", "NO ANSWER", "NO DIALTONE", NULL };
	int i;

	for (i = 0; exact[i]; i++)
		if (!strcmp(l, exact[i]))
			return true;
	/* CONNECT [<rate>]: a data call taken (V.250; AT+CGDATA on Intel XMM), the port carries data after it */
	return !strncmp(l, "+CME ERROR", 10) || !strncmp(l, "+CMS ERROR", 10) || !strcmp(l, "CONNECT") ||
	       !strncmp(l, "CONNECT ", 8);
}

/* calls fn for every non-empty line (trimmed), stops when it returns true */
static void each_line(const char *raw, bool (*fn)(const char *line, void *arg), void *arg)
{
	const char *p = raw;

	while (p && *p) {
		size_t n = strcspn(p, "\r\n");
		char line[512];
		size_t a = 0, b = n;

		while (a < b && isspace((unsigned char)p[a]))
			a++;
		while (b > a && isspace((unsigned char)p[b - 1]))
			b--;
		if (b > a) {
			snprintf(line, sizeof(line), "%.*s", (int)(b - a), p + a);
			if (fn(line, arg))
				return;
		}
		p += n;
		p += strspn(p, "\r\n");
	}
}

static bool find_final(const char *line, void *arg)
{
	if (final_line(line)) {
		*(bool *)arg = true;
		return true;
	}
	return false;
}

bool mm_at_complete(const char *raw)
{
	bool done = false;

	each_line(raw, find_final, &done);
	return done;
}

size_t mm_at_stray_prefix(const char *cmd, const char *raw)
{
	const char *p = raw, *end;
	bool foreign = false;

	/* first non-empty line: an echo of another command? */
	p += strspn(p, "\r\n ");
	end = p + strcspn(p, "\r\n");
	if (end > p && !strncasecmp(p, "AT", 2) && !((size_t)(end - p) == strlen(cmd) && !strncasecmp(p, cmd, strlen(cmd))))
		foreign = true;
	if (!foreign)
		return 0;
	/* skip to the end of the first final result line after it */
	for (p = end; *p; ) {
		const char *l, *e;
		char line[128];

		p += strspn(p, "\r\n");
		l = p;
		e = l + strcspn(l, "\r\n");
		/* trimmed both ways, as each_line() trims what the parser reads (review 2026-11-15: " OK") */
		while (l < e && isspace((unsigned char)*l))
			l++;
		snprintf(line, sizeof(line), "%.*s", (int)(e - l), l);
		while (*line && isspace((unsigned char)line[strlen(line) - 1]))
			line[strlen(line) - 1] = '\0';
		p = e;
		if (*e && final_line(line))
			return (size_t)(e - raw) + strspn(e, "\r\n");
	}
	return 0;	/* its final has not arrived yet: wait */
}

struct parse {
	const char *cmd;
	json_object *lines, *final;
	bool first;
};

static bool collect(const char *line, void *arg)
{
	struct parse *ps = arg;

	if (ps->final)
		return true;
	if (ps->first && ps->cmd && !strcasecmp(line, ps->cmd)) {
		ps->first = false;	/* echo */
		return false;
	}
	ps->first = false;
	if (final_line(line))
		ps->final = json_object_new_string(line);
	else
		json_object_array_add(ps->lines, json_object_new_string(line));
	return false;
}

json_object *mm_at_parse(const char *cmd, const char *raw)
{
	struct parse ps = { cmd, json_object_new_array(), NULL, true };
	json_object *out = json_object_new_object();

	each_line(raw, collect, &ps);
	json_object_object_add(out, "ok", json_object_new_boolean(ps.final && !strcmp(json_object_get_string(ps.final), "OK")));
	json_object_object_add(out, "final", ps.final);
	json_object_object_add(out, "lines", ps.lines);
	return out;
}

/* ---- metrics ---- */

/* the first line of cmd's response starting with prefix, past the prefix and spaces */
static const char *answer(json_object *raw, const char *cmd, const char *prefix, char *buf, size_t size)
{
	json_object *r = NULL, *p, *lines, *ok;
	size_t i, n = prefix ? strlen(prefix) : 0;
	const char *found = NULL;

	if (!json_object_object_get_ex(raw, cmd, &r))
		return NULL;
	p = mm_at_parse(cmd, json_object_get_string(r));
	lines = json_object_object_get(p, "lines");
	ok = json_object_object_get(p, "ok");
	for (i = 0; json_object_get_boolean(ok) && i < json_object_array_length(lines); i++) {
		const char *l = json_object_get_string(json_object_array_get_idx(lines, i));
		if (!prefix || !strncasecmp(l, prefix, n)) {
			l += n;
			while (*l == ' ')
				l++;
			snprintf(buf, size, "%s", l);
			found = buf;
			break;
		}
	}
	json_object_put(p);
	return found;
}

static int fields(const char *s, char out[][64], int max)
{
	int n = 0;
	bool quoted = false;
	size_t k = 0;

	if (!s)
		return 0;
	for (; n < max; s++) {
		if (*s == '"') {
			quoted = !quoted;
			continue;
		}
		if (*s == ' ' && !quoted && !k)
			continue;	/* "+CEREG: 2, 1, ..." on some firmwares */
		if ((*s == ',' && !quoted) || !*s) {
			out[n][k < 64 ? k : 63] = '\0';
			n++;
			k = 0;
			if (!*s)
				break;
			continue;
		}
		if (k < 63)
			out[n][k++] = *s;
	}
	return n;
}

static const char *reg_state(int stat)
{
	static const char *const names[] = { "not-registered", "home", "searching", "denied", "unknown", "roaming" };

	return stat >= 0 && stat <= 5 ? names[stat] : stat == 6 || stat == 7 ? "sms-only" : stat == 10 ? "roaming" : "unknown";
}

static const char *act_name(int act)
{
	switch (act) {
	case 0: case 1: case 3: return "gsm";
	case 2: case 4: case 5: case 6: return "umts";
	case 7: case 9: return "lte";
	case 10: return "lte-5gc";
	case 11: case 12: return "nr";
	case 13: return "nr-nsa";
	default: return "unknown";
	}
}

/*
 * QModem rpcd cell_info ({ "modem_info": [{"key","value"}...] }): serving LTE/NR signal,
 * RAT, MCC/MNC and cell identity. This is a structured cached QModem response;
 * no direct tty access and no SIM identifiers are requested here.
 */
static bool qmodem_metric_integer(const char *value, long long *out)
{
	char *end;
	int base = strpbrk(value, "abcdefABCDEF") || !strncmp(value, "0x", 2) ? 16 : 10;
	long long n = strtoll(value, &end, base);

	if (end == value || *end)
		return false;
	*out = n;
	return true;
}

static int qmodem_split_values(const char *src, char out[][48], int max)
{
	char buf[256], *save = NULL, *p;
	int n = 0;

	if (!src || !*src)
		return 0;
	snprintf(buf, sizeof(buf), "%s", src);
	for (p = strtok_r(buf, "/", &save); p && n < max; p = strtok_r(NULL, "/", &save)) {
		char *end;
		while (*p == ' ' || *p == '\t')
			p++;
		end = p + strlen(p);
		while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
			*--end = '\0';
		if (*p)
			snprintf(out[n++], sizeof(out[0]), "%s", p);
	}
	return n;
}

static bool qmodem_band_number(const char *value, long long *out)
{
	const char *p = value;
	while (*p && !isdigit((unsigned char)*p))
		p++;
	return *p && qmodem_metric_integer(p, out);
}

json_object *mm_qmodem_metrics(json_object *responses)
{
	json_object *m = json_object_new_object(), *nr = json_object_new_object(), *info = NULL, *cur = m;
	char mcc[8] = "", mnc[8] = "", ca_type[16] = "";
	char ca_pci[256] = "", ca_earfcn[256] = "", ca_band[256] = "", ca_ul[256] = "", ca_dl[256] = "";
	size_t i;

	json_object_object_get_ex(json_object_object_get(responses, "cell_info"), "modem_info", &info);
	for (i = 0; info && json_object_is_type(info, json_type_array) && i < json_object_array_length(info); i++) {
		json_object *e = json_object_array_get_idx(info, i), *kv = NULL, *vv = NULL;
		const char *k, *v, *extra = NULL;
		char *end;
		double d;

		{
			json_object *x = NULL;
			if (json_object_object_get_ex(e, "extra_info", &x) && json_object_is_type(x, json_type_string))
				extra = json_object_get_string(x);
		}

		if (!json_object_object_get_ex(e, "key", &kv) || !json_object_object_get_ex(e, "value", &vv))
			continue;
		k = json_object_get_string(kv);
		v = json_object_get_string(vv);
		if (!k || !v)
			continue;
		if (extra && (!strcmp(extra, "CA-LTE") || !strcmp(extra, "CA-NR"))) {
			snprintf(ca_type, sizeof(ca_type), "%s", extra);
			if (!strcmp(k, "Physical Cell ID (CA)"))
				snprintf(ca_pci, sizeof(ca_pci), "%s", v);
			else if (!strcmp(k, "EARFCN (CA)") || !strcmp(k, "ARFCN (CA)"))
				snprintf(ca_earfcn, sizeof(ca_earfcn), "%s", v);
			else if (!strcmp(k, "Band (CA)"))
				snprintf(ca_band, sizeof(ca_band), "%s", v);
			else if (!strcmp(k, "UL Bandwidth (CA)"))
				snprintf(ca_ul, sizeof(ca_ul), "%s", v);
			else if (!strcmp(k, "DL Bandwidth (CA)"))
				snprintf(ca_dl, sizeof(ca_dl), "%s", v);
			continue;
		}
		if (!strcmp(k, "network_mode")) {
			json_object_object_add(m, "access_technology", json_object_new_string(
				strstr(v, "SA") && !strstr(v, "NSA") ? "nr" : strstr(v, "NSA") || strstr(v, "EN-DC") ? "nr-nsa" :
				strstr(v, "LTE") ? "lte" : strstr(v, "WCDMA") ? "umts" : "unknown"));
			if (strstr(v, "SA") && !strstr(v, "NSA"))
				cur = nr;
			continue;
		}
		if (!strcmp(k, "NR5G-NSA")) {
			cur = nr;
			continue;
		}
		if (!strcmp(k, "LTE")) {
			cur = m;
			continue;
		}
		if (!strcmp(k, "MCC") && !mcc[0] && strlen(v) == 3 && strspn(v, "0123456789") == 3)
			snprintf(mcc, sizeof(mcc), "%s", v);
		if (!strcmp(k, "MNC") && !mnc[0] && strlen(v) >= 2 && strlen(v) <= 3 && strspn(v, "0123456789") == strlen(v))
			snprintf(mnc, sizeof(mnc), "%s", v);
		if (!strcmp(k, "Band")) {
			if (!json_object_object_get(cur, "band"))
				json_object_object_add(cur, "band", json_object_new_string(v));
			continue;
		}
		if (!strcmp(k, "Cell ID") || !strcmp(k, "TAC") || !strcmp(k, "Physical Cell ID") ||
		    !strcmp(k, "EARFCN") || !strcmp(k, "ARFCN")) {
			long long value;
			const char *field = !strcmp(k, "Cell ID") ? "cell_id" : !strcmp(k, "TAC") ? "tac" :
				!strcmp(k, "Physical Cell ID") ? "pci" : "earfcn";
			if (!json_object_object_get(cur, field) && qmodem_metric_integer(v, &value))
				json_object_object_add(cur, field, json_object_new_int64(value));
			continue;
		}
		d = strtod(v, &end);
		if (end == v)
			continue;
		if (!strcmp(k, "RSRP") && !json_object_object_get(cur, "rsrp_dbm"))
			json_object_object_add(cur, "rsrp_dbm", json_object_new_int((int)(d < 0 ? d - 0.5 : d + 0.5)));
		else if (!strcmp(k, "RSRQ") && !json_object_object_get(cur, "rsrq_db"))
			json_object_object_add(cur, "rsrq_db", json_object_new_double(d));
		else if ((!strcmp(k, "SINR") || !strcmp(k, "RSSNR")) && !json_object_object_get(cur, "sinr_db"))
			json_object_object_add(cur, "sinr_db", json_object_new_double(d));
		else if (!strcmp(k, "RSSI") && cur == m && !json_object_object_get(m, "rssi_dbm"))
			json_object_object_add(m, "rssi_dbm", json_object_new_int((int)d));
		else if (!strcmp(k, "DL Bandwidth") && !json_object_object_get(cur, "bandwidth_mhz"))
			json_object_object_add(cur, "bandwidth_mhz", json_object_new_double(d));
	}
	if (mcc[0] && mnc[0]) {
		char code[16];

		snprintf(code, sizeof(code), "%.3s%.3s", mcc, mnc);
		json_object_object_add(m, "operator", json_object_new_string(code));
	}
	/* QModem's structured CA summary replaces the redundant second GTCAINFO query. */
	if (ca_type[0]) {
		char pci[8][48] = {{0}}, earfcn[8][48] = {{0}}, band[8][48] = {{0}}, ul[8][48] = {{0}}, dl[8][48] = {{0}};
		int np = qmodem_split_values(ca_pci, pci, 8), ne = qmodem_split_values(ca_earfcn, earfcn, 8);
		int nb = qmodem_split_values(ca_band, band, 8), nu = qmodem_split_values(ca_ul, ul, 8);
		int nd = qmodem_split_values(ca_dl, dl, 8), count = np;
		json_object *cells = json_object_new_array(), *ca = json_object_new_object();
		int j;

		if (ne > count) count = ne;
		if (nb > count) count = nb;
		if (nu > count) count = nu;
		if (nd > count) count = nd;
		for (j = 0; j < count; j++) {
			json_object *c = json_object_new_object();
			long long value;
			char *end;
			json_object_object_add(c, "role", json_object_new_string("secondary"));
			json_object_object_add(c, "type", json_object_new_string(!strcmp(ca_type, "CA-NR") ? "5gnr" : "lte"));
			if (j < nb && qmodem_band_number(band[j], &value))
				json_object_object_add(c, "band", json_object_new_int64(value));
			if (j < np && qmodem_metric_integer(pci[j], &value))
				json_object_object_add(c, "pci", json_object_new_int64(value));
			if (j < ne && qmodem_metric_integer(earfcn[j], &value))
				json_object_object_add(c, "earfcn", json_object_new_int64(value));
			if (j < nd && strtod(dl[j], &end) >= 0 && end != dl[j])
				json_object_object_add(c, "bandwidth_mhz", json_object_new_double(strtod(dl[j], NULL)));
			if (j < nu && strtod(ul[j], &end) >= 0 && end != ul[j])
				json_object_object_add(c, "ul_bandwidth_mhz", json_object_new_double(strtod(ul[j], NULL)));
			json_object_array_add(cells, c);
		}
		if (json_object_array_length(cells)) {
			json_object_object_add(ca, "cells", cells);
			json_object_object_add(m, "ca", ca);
		} else {
			json_object_put(cells);
			json_object_put(ca);
		}
	}
	/* SA: the only cell is 5G, the page reads it as such */
	if (json_object_object_length(nr))
		json_object_object_add(m, "nr", nr);
	else
		json_object_put(nr);
	return m;
}

/*
 * AT+GTCAINFO? of the FM350 (MediaTek T700; its AT manual 11.1.16, read against live answers):
 * one "PCC:<band>,<pci>,<earfcn>,<dl_bw>,..." line for the primary carrier and
 * "SCC<n>:<state>,<ul_cfg>,<band>,<pci>,<earfcn>,<dl_bw>,..." for each secondary one, in use when its state is 2.
 * A band is coded: LTE 100 + band (103 = B3), NR 5000 + band or 500 + band (5078, 578 = n78). A width is in 200 kHz
 * steps (50 = 10 MHz, 100 = 20 MHz; 6 = 1.4 MHz). An idle modem (no RRC) answers no PCC: no carriers then.
 */
static json_object *fibocom_cell(const char *role, long band, long pci, long earfcn, long bw)
{
	json_object *c = json_object_new_object();
	bool nr = band >= 500;

	json_object_object_add(c, "role", json_object_new_string(role));
	json_object_object_add(c, "type", json_object_new_string(nr ? "5gnr" : "lte"));
	json_object_object_add(c, "band", json_object_new_int((int)(band >= 5000 ? band - 5000 : nr ? band - 500 : band - 100)));
	if (pci >= 0 && pci <= 1007)
		json_object_object_add(c, "pci", json_object_new_int((int)pci));
	if (earfcn > 0)
		json_object_object_add(c, "earfcn", json_object_new_int64(earfcn));
	if (bw == 6)
		json_object_object_add(c, "bandwidth_mhz", json_object_new_double(1.4));
	else if (bw > 0 && bw <= 2000)
		json_object_object_add(c, "bandwidth_mhz", json_object_new_double(bw / 5.0));
	return c;
}

/*
 * Spec 26.1 step 4: the DL layers and modulation of each carrier, the SCC's RSRP. As the FM350 answers
 * them (4pda 1057776 #1361, #1454 and a field report):
 *   PCC  band,pci,earfcn,dl_bw,ul_bw,dl_mimo,ul_mimo,dl_mod,ul_mod,rsrp   (live firmwares: an ul_bw the manual lacks;
 *        without it, 9 fields and dl_mimo fifth: told by the count, or by a fifth field above 4, a width)
 *   SCCn state,ul_cfg,band,pci,earfcn,dl_bw,ul_bw,dl_mimo,ul_mimo,dl_mod,ul_mod,rsrp
 * Layers 1..4 only (0 or 255: nothing sent on it now, the modulation then a placeholder); modulation 0 BPSK, 1 QPSK,
 * 2 16QAM, 3 64QAM, 4 256QAM, 5 1024QAM. A firmware copies the PCC's RSRP into every SCC: when every SCC
 * says the PCC's value and one of them is another cell, the copies of other cells are dropped.
 */
static void fibocom_link(json_object *c, long mimo, long mod)
{
	static const char *const mods[] = { "BPSK", "QPSK", "16QAM", "64QAM", "256QAM", "1024QAM" };

	if (mimo < 1 || mimo > 4)
		return;
	json_object_object_add(c, "mimo_layers", json_object_new_int((int)mimo));
	if (mod >= 0 && mod <= 5)
		json_object_object_add(c, "modulation", json_object_new_string(mods[mod]));
}

json_object *mm_fibocom_ca(const char *raw)
{
	json_object *cells = json_object_new_array(), *o;
	const char *p = raw;
	long pcc_rsrp = 0, pcc_pci = -1;
	int scc_rsrp = 0, scc_same = 0;
	bool scc_other = false;

	while (p && *p) {
		const char *end = strpbrk(p, "\r\n"), *colon;
		char line[256];
		long f[12];
		int n = 0;
		char *q, *e;

		snprintf(line, sizeof(line), "%.*s", end ? (int)(end - p) : (int)strlen(p), p);
		p = end ? end + strspn(end, "\r\n") : NULL;
		colon = strchr(line, ':');
		if (!colon || (strncmp(line, "PCC", 3) && strncmp(line, "SCC", 3)))
			continue;
		for (q = (char *)colon + 1; n < 12; q = e + 1) {
			f[n] = strtol(q, &e, 10);
			if (e == q)
				break;
			n++;
			while (*e == ' ')
				e++;
			if (*e != ',')
				break;
		}
		/* a band out of the coding is not a carrier (a firmware with another layout) */
		if (!strncmp(line, "PCC", 3) && n >= 4 && ((f[0] > 100 && f[0] < 200) || f[0] > 500)) {
			json_object *c = fibocom_cell("primary", f[0], f[1], f[2], f[3]);
			bool ul_bw = n >= 10 || (n == 9 && f[4] > 4);

			if (n >= 9)
				fibocom_link(c, ul_bw ? f[5] : f[4], ul_bw ? f[7] : f[6]);
			/* an NSA modem prints the NR leg's PCC first: the LTE one's RSRP is what the copies repeat */
			if (n >= 9 && f[0] < 200) {
				pcc_rsrp = f[n - 1];
				pcc_pci = f[1];
			}
			json_object_array_add(cells, c);
		} else if (!strncmp(line, "SCC", 3) && n >= 6 && f[0] == 2 && ((f[2] > 100 && f[2] < 200) || f[2] > 500)) {
			json_object *c = fibocom_cell("secondary", f[2], f[3], f[4], f[5]);

			if (n >= 11)
				fibocom_link(c, f[7], f[9]);
			if (n >= 12 && f[11] >= -157 && f[11] <= -40) {
				json_object_object_add(c, "rsrp", json_object_new_double((double)f[11]));
				scc_rsrp++;
				scc_same += f[11] == pcc_rsrp;
				scc_other = scc_other || f[3] != pcc_pci;
			}
			json_object_array_add(cells, c);
		}
	}
	if (scc_rsrp >= 2 && scc_same == scc_rsrp && scc_other) {
		size_t i;

		for (i = 0; i < json_object_array_length(cells); i++) {
			json_object *c = json_object_array_get_idx(cells, i);

			if (json_object_get_int(json_object_object_get(c, "pci")) != pcc_pci)
				json_object_object_del(c, "rsrp");
		}
	}
	/* the primary first, as the page draws them; none without a primary */
	if (!json_object_array_length(cells) || strcmp(json_object_get_string(json_object_object_get(json_object_array_get_idx(cells, 0), "role")), "primary")) {
		json_object_put(cells);
		return NULL;
	}
	o = json_object_new_object();
	json_object_object_add(o, "cells", cells);
	return o;
}

json_object *mm_at_metrics(json_object *raw)
{
	json_object *m = json_object_new_object(), *reg = json_object_new_object();
	char buf[256], f[8][64];
	const char *a;
	int n;

	if ((a = answer(raw, "AT+CSQ", "+CSQ:", buf, sizeof(buf))) && fields(a, f, 2) == 2) {
		int rssi = atoi(f[0]);
		if (rssi >= 0 && rssi <= 31)
			json_object_object_add(m, "rssi_dbm", json_object_new_int(-113 + 2 * rssi));
	}
	if ((a = answer(raw, "AT+CESQ", "+CESQ:", buf, sizeof(buf))) && fields(a, f, 6) == 6) {
		int rsrq = atoi(f[4]), rsrp = atoi(f[5]);
		if (rsrq >= 0 && rsrq <= 34)
			json_object_object_add(m, "rsrq_db", json_object_new_double(-20.0 + rsrq / 2.0));
		if (rsrp >= 0 && rsrp <= 97)
			json_object_object_add(m, "rsrp_dbm", json_object_new_int(-141 + rsrp));
	}
	if ((a = answer(raw, "AT+COPS?", "+COPS:", buf, sizeof(buf)))) {
		n = fields(a, f, 4);
		if (n >= 3 && f[2][0])
			json_object_object_add(m, "operator", json_object_new_string(f[2]));
		if (n >= 4)
			json_object_object_add(m, "access_technology", json_object_new_string(act_name(atoi(f[3]))));
	}
	{
		static const char *const regs[][3] = {
			{ "AT+CREG?", "+CREG:", "cs" }, { "AT+CGREG?", "+CGREG:", "ps" },
			{ "AT+CEREG?", "+CEREG:", "eps" }, { "AT+C5GREG?", "+C5GREG:", "nr" },
		};
		size_t i;
		for (i = 0; i < sizeof(regs) / sizeof(regs[0]); i++)
			if ((a = answer(raw, regs[i][0], regs[i][1], buf, sizeof(buf))) && fields(a, f, 2) == 2)
				json_object_object_add(reg, regs[i][2], json_object_new_string(reg_state(atoi(f[1]))));
	}
	json_object_object_add(m, "registration", reg);
	if ((a = answer(raw, "AT+CPIN?", "+CPIN:", buf, sizeof(buf))))
		json_object_object_add(m, "sim", json_object_new_string(a));
	else if (json_object_object_get(raw, "AT+CPIN?")) {
		json_object *p = mm_at_parse("AT+CPIN?", json_object_get_string(json_object_object_get(raw, "AT+CPIN?")));
		const char *fin = json_object_get_string(json_object_object_get(p, "final"));
		if (fin && strstr(fin, "ERROR: 10"))
			json_object_object_add(m, "sim", json_object_new_string("NOT INSERTED"));
		json_object_put(p);
	}
	if ((a = answer(raw, "AT+CFUN?", "+CFUN:", buf, sizeof(buf)))) {
		int fun = atoi(a);
		json_object_object_add(m, "radio", json_object_new_string(fun == 1 ? "on" : fun == 4 ? "rf-off" : fun == 0 ? "minimum" : "other"));
	}
	if ((a = answer(raw, "AT+CGMI", NULL, buf, sizeof(buf))))
		json_object_object_add(m, "manufacturer", json_object_new_string(a));
	if ((a = answer(raw, "AT+CGMM", NULL, buf, sizeof(buf))))
		json_object_object_add(m, "model", json_object_new_string(a));
	if ((a = answer(raw, "AT+CGMR", NULL, buf, sizeof(buf))))
		json_object_object_add(m, "firmware", json_object_new_string(a));
	return m;
}

const char *mm_at_probe_verdict(const char *at_raw, const char *cgmi_raw)
{
	if (!at_raw || !*at_raw)
		return "silent";
	{
	json_object *a = mm_at_parse("AT", at_raw), *b = mm_at_parse("AT+CGMI", cgmi_raw);
	bool ok = json_object_get_boolean(json_object_object_get(a, "ok")) &&
		  json_object_get_boolean(json_object_object_get(b, "ok")) &&
		  json_object_array_length(json_object_object_get(b, "lines")) > 0;
	const char *p;

	/* binary traffic (DIAG, GNSS NMEA bursts) is never an AT port */
	for (p = at_raw; ok && p && *p; p++)
		if (!isprint((unsigned char)*p) && !isspace((unsigned char)*p))
			ok = false;
	json_object_put(a);
	json_object_put(b);
	return ok ? "at" : "not-at";
	}
}
