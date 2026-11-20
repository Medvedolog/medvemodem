#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>

#include "evtext.h"

/*
 * The journal as sentences (the owner, 2026-10-17: the console and the system log in English, plain words):
 * for `medvemodem events` on a terminal, the TUI and the system log. The modem page keeps its Russian.
 */

static const char *const titles[][2] = {
	{ "added", "Modem detected" }, { "reattached", "Modem back on USB" },
	{ "changed", "Modem ports changed" }, { "removed", "Modem gone from USB" },
	{ "usb-add", "Modem attached to USB" }, { "usb-remove", "Modem detached from USB" },
	{ "driver-bind", "Driver bound" }, { "driver-unbind", "Driver unbound" },
	{ "mm-add", "ModemManager claimed the modem" }, { "mm-remove", "ModemManager released the modem" },
	{ "mm-state", "Modem state changed" }, { "mm-kick", "Ports handed to ModemManager again" },
	{ "port-held", "A modem port is held by another program" }, { "port-released", "Another program released a modem port" },
	{ "mm-ports-withdrawn", "Serial ports withdrawn from ModemManager" },
	{ "mm-restart", "ModemManager restart" }, { "mm-restart-end", "ModemManager restart finished" },
	{ "sms-received", "SMS received" }, { "sms-sent", "SMS sent" },
	{ "service-restart", "A modem service restarted" }, { "service-stop", "A modem service stopped" },
	{ "web-login", "Logged in to modem web UI" },
	{ "subnet-clash", "Modem subnet conflict" },
	{ "subnet-moved", "Modem moved to a free subnet" },
	{ "esim-switch", "eSIM profile switched: the SIM is read again" },
	{ "subnet-move-failed", "Modem web UI rejected the new subnet" },
	{ "usb-rescue", "Manual USB controller / power reset" },
	{ "sms-cnmi-fixed", "Incoming SMS were not stored (CNMI 0,0): fixed" },
	{ "sms-stuck-dropped", "Stuck part of a long SMS deleted" },
	{ "sms-draft-dropped", "Unsent SMS draft deleted (it held a storage slot)" },
	{ "sms-storage", "SMS storage switched" }, { "sms-cleared", "SMS storage cleared" }, { "sms-center", "SMS center changed" },
	{ "session-up", "Connected" }, { "session-down", "Connection lost" },
	{ "session-failed", "Connection failed" },
	{ "recovery-start", "Recovery step started" }, { "recovery-end", "Recovery step finished" },
	{ "recovery-no-internet", "Session restored after recovery, no internet" },
	{ "hostfix-start", "Binding the service ports" }, { "hostfix-end", "Service ports bound" },
	{ "radio-change", "Network mode and bands changed" }, { "radio-kept", "Network mode change kept" },
	{ "radio-rollback", "Network mode and bands rolled back" }, { "radio-guard", "Cell lock guard" }, { "band-guard", "Band lock guard" },
	{ "reregister", "Registered in the network again" }, { "fplmn-clear", "SIM forbidden network list (FPLMN) cleared" }, { "band-change", "Band changed" }, { "cell-change", "Serving cell changed" }, { "ca-change", "Carrier aggregation changed" },
	{ "backend-return", "Modem returned to ModemManager" },
	{ "quota-warn", "SIM data cap nearly reached" }, { "quota-exceeded", "SIM data cap reached" },
	{ "ifup", "Interface brought up again" }, { "iface-repair", "Interface settings repaired" },
	{ "interface-retry", "Interface brought up again after a failed connection" },
	{ "registration-denied", "Network rejected registration" },
	{ "interface-down", "Interface taken down" }, { "interface-up", "Interface brought up" },
	{ "config", "Network settings changed" }, { "apn-auto", "APN chosen by the SIM" },
	{ "auto-connect", "New modem connected automatically" },
	{ "modeswitch-run", "Switching the modem out of storage mode" }, { "modeswitch-done", "Modem switched to modem mode" },
	{ "modeswitch-fix", "usb-modeswitch rule disabled" }, { "priority", "Modem priority changed" },
	{ "settings-follow", "Name and settings followed the modem" },
	{ "usb-config", "USB configuration changed manually" },
	{ "fcc-unlock-on", "FCC unlock enabled by user" },
	{ "frozen", "Management paused by user" }, { "unfrozen", "Management resumed by user" },
	{ "qmodem-release", "Reclaimed from QModem by user" },
	{ "fcc-unlock-off", "FCC unlock disabled by user" },
	{ "internet", "Internet through the modem" }, { "failover", "Traffic switching" },
};

static const char *const stages[][2] = {
	{ "observe", "Refresh modem info" }, { "session", "Reconnect" },
	{ "control", "Release stuck ports" }, { "radio", "Restart network search" },
	{ "soft-reset", "Reboot the modem" }, { "rebind", "Rebind the driver" },
	{ "usb-reset", "Reset the USB device" }, { "power", "Power-cycle the USB port" },
};

static const char *const words[][2] = {
	{ "succeeded", "done" }, { "failed", "failed" }, { "refused", "refused" },
	{ "busy", "busy" }, { "retry", "try later" }, { "started", "started" },
	{ "luci", "from LuCI" }, { "cli", "from the command line" }, { "automatic", "automatically" },
	{ "operator", "by the operator" }, { "user_requested", "manually" }, { "session_stuck", "session failed to connect" }, { "mm_bearer_stuck", "ModemManager holds a dead connection" },
	{ "registration_stuck", "the modem does not register" }, { "modem_firmware_hang", "modem not responding" },
	{ "no_internet", "connected but no internet" }, { "registration_denied", "registration denied" },
};

static const char *lookup(const char *const (*table)[2], size_t n, const char *key, const char *fallback)
{
	size_t i;

	for (i = 0; key && i < n; i++)
		if (!strcmp(table[i][0], key))
			return table[i][1];
	return fallback;
}

#define LOOKUP(t, k, f) lookup(t, sizeof(t) / sizeof(t[0]), k, f)

const char *mm_event_title(const char *kind)
{
	return LOOKUP(titles, kind, NULL);
}

void mm_event_words(const char *kind, const char *detail, char *out, size_t size)
{
	const char *title = mm_event_title(kind), *d = detail && *detail ? detail : NULL;
	char a[64] = "", b[64] = "", c[64] = "";
	int n;

	if (!title) {
		snprintf(out, size, "%s%s%s", kind ? kind : "?", d ? ": " : "", d ? d : "");
		return;
	}
	if (d && (!strcmp(kind, "recovery-start") || !strcmp(kind, "recovery-end")) &&
	    (n = sscanf(d, "%63s %63s %63s", a, b, c)) >= 2) {
		const char *stage = LOOKUP(stages, a, a), *reason = strchr(d, ':');
		char *colon = strchr(b, ':');

		if (colon)
			*colon = '\0';
		if (!strcmp(kind, "recovery-start"))
			snprintf(out, size, "%s: \"%s\", %s%s%s%s", title, stage, n >= 3 ? LOOKUP(words, c, "by hand") : "by hand",
				 strcmp(b, "user_requested") ? " (" : "", strcmp(b, "user_requested") ? LOOKUP(words, b, b) : "",
				 strcmp(b, "user_requested") ? ")" : "");
		else
			snprintf(out, size, "%s: \"%s\" %s%s%s", title, stage, LOOKUP(words, b, b), reason ? ":" : "", reason ? reason + 1 : "");
		return;
	}
	if (d && (!strcmp(kind, "driver-bind") || !strcmp(kind, "driver-unbind"))) {
		/* "255/2/97 option": the driver's name is what a person needs */
		const char *sp = strrchr(d, ' ');

		snprintf(out, size, "%s%s%s", title, sp ? ": " : "", sp ? sp + 1 : "");
		return;
	}
	if (d && !strcmp(kind, "mm-restart-end") && sscanf(d, "init script status %63s", a) == 1) {
		snprintf(out, size, "%s: the init script failed (status %s%s)", title, a, !strcmp(a, "127") ? ", ModemManager is not installed" : "");
		return;
	}
	if (!strcmp(kind, "sms-sent")) {
		bool at = d && strlen(d) > 3 && !strcmp(d + strlen(d) - 3, " at");

		snprintf(out, size, "%s%s", d && !strncmp(d, "failed", 6) ? "SMS not sent" : "SMS sent", at ? " over the modem's AT port" : "");
		return;
	}
	if (d && !strncmp(kind, "session-", 8)) {
		snprintf(out, size, "%s: interface %s", title, d);
		return;
	}
	/* why a modem was found or came back (the owner, 2026-10-28) */
	if (d && (!strcmp(kind, "added") || !strcmp(kind, "reattached"))) {
		snprintf(out, size, "%s: %s", title, !strcmp(d, "start") ? "on service start" : !strcmp(d, "uevent") ? "a USB event" :
			 !strcmp(d, "rescan request") ? "a rescan" : !strcmp(d, "returned") ? "reconnected" :
			 strstr(d, "attachment") ? "USB port changed" : strstr(d, "ports") ? "its ports changed" : d);
		return;
	}
	if (d && (!strcmp(kind, "radio-guard") || !strcmp(kind, "band-guard"))) {
		if (!strncmp(d, "no network for ", 15))
			snprintf(out, size, "%s: %s, the settings before the lock go back", title, d);
		else
			snprintf(out, size, "%s: %s", title, !strcmp(d, "done") ? "the settings before the lock are back" :
				 "the old settings could not be written, it gave up");
		return;
	}
	if (d && !strcmp(kind, "internet")) {
		snprintf(out, size, "%s: %s", title, !strcmp(d, "ok") ? "full internet access" :
			 !strcmp(d, "limited") ? "carrier whitelist only" : !strcmp(d, "down") ? "no answer" : d);
		return;
	}
	if (d && !strcmp(kind, "failover") && sscanf(d, "%63s %63s", a, b) == 2) {
		snprintf(out, size, "%s: %s %s", title, b, !strcmp(a, "down") ? "has no internet, the traffic moved to another link" :
			 !strcmp(a, "limited") ? "has the whitelist only, another link carries the traffic" :
			 !strcmp(a, "quota") ? "spent its quota, kept behind the others" : !strcmp(a, "manual") ? "is a manual link, kept behind" :
			 !strcmp(a, "demoted") ? "answers again, kept last" : !strcmp(a, "back") ? "answers again, its priority restored" :
			 "its priority restored");
		return;
	}
	/* the registry's and ModemManager's own words add nothing a person reads */
	if (!strcmp(kind, "added") || !strcmp(kind, "removed") || !strcmp(kind, "reattached") || !strcmp(kind, "usb-add") ||
	    !strcmp(kind, "usb-remove") || !strcmp(kind, "mm-add") || !strcmp(kind, "mm-remove") || !strncmp(kind, "hostfix-", 8))
		d = NULL;
	snprintf(out, size, "%s%s%s", title, d ? ": " : "", d ? d : "");
}
