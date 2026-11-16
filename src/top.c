/*
 * medvemodem top: the terminal view of the engine (spec 25.8), read-only for now.
 *
 * A frontend like LuCI: everything comes from the engine over ubus (status, devices,
 * uplink, device, events); it never opens a modem port, resolves an owner, detects
 * modems or keeps a timer of its own (25.8.1). Plain ANSI escapes, no curses, so it runs
 * over SSH and a serial console on any router. `--once` prints one frame without
 * terminal control (scripts, a console without a terminal type, tests).
 */
#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "top.h"
#include "evtext.h"

#define TOP_REFRESH_MS 2000
#define TOP_MAX_LINES 256

static mm_top_call call;
/*
 * The language of the screen: English by default, Russian on request (--lang ru, MEDVEMODEM_LANG=ru,
 * or L on screen). Every word is written in both, side by side, so a string cannot lose its pair;
 * T() picks one at the moment of drawing, so L switches the next frame without a restart.
 */
static bool ru;
#define T(en, ru_) (ru ? (ru_) : (en))
#define LW(map, i) ((map)[i][ru ? 2 : 1])
/* the modem screen's tab, as LuCI's: 0 Обзор, 1 Радио, 2 Сервис; -1 all of them (--once) */
static int g_tab = -1;
static json_object *g_levels;	/* the recovery steps of the modem on screen, for the keys 1–8 */
#define SHOW(t) (g_tab < 0 || g_tab == (t))
static const char *const TAB_NAMES[][2] = { { "Overview", "Обзор" }, { "Radio", "Радио" }, { "Service", "Сервис" } };
/* the recovery ladder in the words of LuCI's «Сервис»: { key, English, Russian } as every word table here */
static const char *const STEP_W[][3] = {
	{ "observe", "Refresh modem info", "Обновить сведения о модеме" }, { "session", "Reconnect the internet", "Переподключить интернет" },
	{ "control", "Clear stuck ports", "Сбросить зависание портов" }, { "radio", "Restart network search", "Перезапустить поиск сети" },
	{ "soft-reset", "Reboot the modem", "Перезагрузить модем" }, { "rebind", "Restart the modem driver", "Перезапустить драйвер модема" },
	{ "usb-reset", "Reconnect the modem on USB", "Переподключить модем по USB" }, { "power", "Power-cycle USB", "Сбросить питание USB" },
	{ NULL, NULL, NULL } };
static bool color;
static struct termios saved_tty;
static bool raw_on;
static volatile sig_atomic_t stop, resized;
static bool ascii;
/* --once hides IMEI, ICCID, IMSI and the number unless --show-identifiers (audit 2026-10-15: snapshots end up in tickets) */
static bool redact;
static const char *ident(const char *v, char *buf, size_t len);
static int forced_width;

/* ---- the frame: lines of text, cut to the terminal width when written ---- */

static char *lines[TOP_MAX_LINES];
static int nlines;

static void frame_reset(void)
{
	for (int i = 0; i < nlines; i++)
		free(lines[i]);
	nlines = 0;
}

static void out(const char *fmt, ...)
{
	va_list ap;
	char *s = NULL;

	if (nlines >= TOP_MAX_LINES)
		return;
	va_start(ap, fmt);
	if (vasprintf(&s, fmt, ap) < 0)
		s = NULL;
	va_end(ap);
	if (s)
		lines[nlines++] = s;
}

/* colours only on a terminal that wants them: words and numbers carry the same */
static const char *c_(const char *code) { return color ? code : ""; }
#define C_OFF c_("\033[0m")
#define C_DIM c_("\033[2m")
#define C_BOLD c_("\033[1m")
#define C_INV c_("\033[7m")
static const char *zone_color(int z)
{
	static const char *const z_[] = { "\033[31m", "\033[33m", "\033[32m", "\033[32;1m" };
	return z < 0 || z > 3 ? "" : c_(z_[z]);
}

/* write one line cut to `width` display columns: UTF-8 lead bytes count, escapes do not */
static void put_line(FILE *f, const char *s, int width)
{
	int col = 0;

	while (*s) {
		if (*s == '\033') {
			/* only a colour (CSI ... m) passes, and only with colour on: the frame's own formatting.
			 * The data was scrubbed on arrival (scrub), so anything else here is dropped, never sent */
			const char *e = s + 1;
			bool sgr = false;

			if (*e == '[') {
				for (e++; *e && !(*e >= '@' && *e <= '~'); e++)
					;
				sgr = *e == 'm';
			}
			if (*e)
				e++;
			if (sgr && color)
				fwrite(s, 1, (size_t)(e - s), f);
			s = e;
			continue;
		}
		if ((unsigned char)*s < 0x20 || *s == 0x7f) {	/* no CR, BEL, BS or other control in a line */
			s++;
			continue;
		}
		if (((unsigned char)*s & 0xc0) != 0x80) {
			if (width > 0 && col >= width)
				break;
			col++;
		}
		fputc(*s++, f);
	}
	if (color)
		fputs("\033[0m", f);
}

/* `s` cut or padded to `w` display columns (UTF-8: a column per lead byte) */
static const char *pad(const char *s, int w, char *buf, size_t len)
{
	size_t o = 0;
	int col = 0;

	for (; s && *s && o + 4 < len; s++) {
		if (((unsigned char)*s & 0xc0) != 0x80) {
			if (col == w)
				break;
			col++;
		}
		buf[o++] = *s;
	}
	while (col < w && o + 1 < len) {
		buf[o++] = ' ';
		col++;
	}
	buf[o] = '\0';
	return buf;
}

/* ---- reading the engine's records ---- */

static json_object *get(json_object *o, const char *k)
{
	json_object *v = NULL;
	return o && json_object_is_type(o, json_type_object) && json_object_object_get_ex(o, k, &v) ? v : NULL;
}

static const char *str(json_object *o, const char *k)
{
	json_object *v = get(o, k);
	const char *s = v && json_object_is_type(v, json_type_string) ? json_object_get_string(v) : NULL;
	return s && *s && strcmp(s, "--") ? s : NULL;
}

static bool num(json_object *o, const char *k, double *d)
{
	json_object *v = get(o, k);
	if (!v || !(json_object_is_type(v, json_type_double) || json_object_is_type(v, json_type_int)))
		return false;
	*d = json_object_get_double(v);
	return true;
}

/* 14.2 -> "14.2" ("14,2" in Russian), -91.0 -> "-91" */
static const char *n1(double d, char *buf, size_t len)
{
	long t = (long)(d * 10 + (d < 0 ? -0.5 : 0.5));
	if (t % 10 == 0)
		snprintf(buf, len, "%ld", t / 10);
	else
		snprintf(buf, len, "%s%ld%s%ld", t < 0 ? "-" : "", labs(t) / 10, T(".", ","), labs(t) % 10);
	return buf;
}

static const char *word(const char *const map[][3], const char *key, const char *dflt)
{
	for (int i = 0; key && map[i][0]; i++)
		if (!strcmp(map[i][0], key))
			return LW(map, i);
	return key ? key : dflt;
}

static const char *const SESSION_W[][3] = {
	{ "connected", "connected", "подключён" }, { "connecting", "connecting", "подключается" },
	{ "disconnected", "not connected", "не подключён" }, { "failed", "connection failed", "ошибка подключения" }, { NULL, NULL, NULL } };
static const char *const MM_W[][3] = {
	{ "connected", "online", "в сети" }, { "registered", "online", "в сети" }, { "searching", "searching", "поиск сети" },
	{ "enabled", "enabled", "включён" }, { "enabling", "enabling", "включается" }, { "disabled", "disabled", "выключен" },
	{ "locked", "SIM locked", "SIM заблокирована" }, { "failed", "failed", "ошибка" }, { "initializing", "starting", "запуск" },
	{ "connecting", "connecting", "подключается" }, { "disconnecting", "disconnecting", "отключается" }, { NULL, NULL, NULL } };
static const char *const REG_W[][3] = {
	{ "home", "home network", "домашняя сеть" }, { "roaming", "roaming", "роуминг" }, { "searching", "searching", "поиск" },
	{ "denied", "registration denied", "отказ в регистрации" }, { "idle", "not registered", "не зарегистрирован" },
	{ "unknown", "unknown", "неизвестно" }, { NULL, NULL, NULL } };
static const char *const ROLE_W[][3] = {
	{ "carrying", "main", "основной" }, { "limited", "main, limited", "основной, ограничен" },
	{ "no_internet", "main, no internet", "основной, без интернета" }, { "backup_ready", "backup", "резерв" },
	{ "backup_limited", "backup, limited", "резерв, ограничен" }, { "backup_not_ready", "backup, offline", "резерв, без связи" },
	{ "checking", "checking", "проверяется" }, { "down", "not connected", "не подключён" }, { NULL, NULL, NULL } };
static const char *const NET_W[][3] = {
	{ "available", "yes", "есть" }, { "limited", "limited", "ограничен" }, { "unavailable", "no", "нет" },
	{ "checking", "checking", "проверяется" }, { NULL, NULL, NULL } };
static const char *const MMB_W[][3] = {
	{ "available", "running", "работает" }, { "unavailable", "not running", "не запущен" }, { "timeout", "not responding", "не отвечает" },
	{ "error", "error", "ошибка" }, { "disabled", "not used", "не используется" }, { "unknown", "unknown", "неизвестно" }, { NULL, NULL, NULL } };
static const char *const REC_W[][3] = {
	{ "idle", "idle", "не выполняется" }, { "running", "running", "выполняется" }, { "cooldown", "paused after a failure", "пауза после ошибки" },
	{ "manual_required", "needs a manual check", "нужна ручная проверка" }, { NULL, NULL, NULL } };
static const char *const OWNER_W[][3] = {
	{ "modemmanager", "ModemManager", "ModemManager" }, { "qmodem", "QModem", "QModem" }, { "native", "MedveModem", "MedveModem" },
	{ "none", "nobody", "никто" }, { "unresolved", "undetermined", "не определено" }, { NULL, NULL, NULL } };
static const char *const ROLEC_W[][3] = {
	{ "primary", "primary", "основная" }, { "secondary", "secondary", "доп." }, { "scg-primary", "5G NR", "5G NR" },
	{ "scg-secondary", "5G NR sec.", "5G NR доп." }, { "serving", "serving", "обслуж." }, { NULL, NULL, NULL } };

/* the RSRP scale of the page: <-110 weak, <-100 fair, <-90 good, else excellent */
static int rsrp_zone(double v) { return v < -110 ? 0 : v < -100 ? 1 : v < -90 ? 2 : 3; }

/* block glyphs; --ascii for a serial console without UTF-8 glyphs (5gtop's TOP_ASCII) */
static const char *bars(int z)
{
	static const char *const b[] = { "▮▯▯▯", "▮▮▯▯", "▮▮▮▯", "▮▮▮▮" }, *const a[] = { "#...", "##..", "###.", "####" };
	return z < 0 || z > 3 ? (ascii ? "...." : "····") : ascii ? a[z] : b[z];
}

static const char *ago(long long s, char *buf, size_t len)
{
	if (s < 60)
		snprintf(buf, len, T("%lld s", "%lld с"), s);
	else if (s < 3600)
		snprintf(buf, len, T("%lld min", "%lld мин"), s / 60);
	else if (s < 172800)
		snprintf(buf, len, T("%lld h %lld min", "%lld ч %lld мин"), s / 3600, s % 3600 / 60);
	else
		snprintf(buf, len, T("%lld d", "%lld дн"), s / 86400);
	return buf;
}

static const char *clock_of(long long t, char *buf, size_t len)
{
	time_t tt = (time_t)t;
	struct tm tm;
	if (!localtime_r(&tt, &tm) || !strftime(buf, len, "%H:%M:%S", &tm))
		snprintf(buf, len, "—");
	return buf;
}

/* "Стенд (T99W175)", else the model, else the USB product */
static void name_of(json_object *d, char *buf, size_t len)
{
	json_object *mm = get(d, "modemmanager");
	const char *model = str(mm, "model") ? str(mm, "model") : str(d, "product") ? str(d, "product") : str(d, "device_id");

	if (str(d, "name"))
		snprintf(buf, len, "%s (%s)", str(d, "name"), model ? model : "?");
	else
		snprintf(buf, len, "%s", model ? model : "?");
}

static json_object *signal_carrier(json_object *mm)
{
	json_object *sig = get(mm, "signal");
	return get(sig, "lte") ? get(sig, "lte") : get(sig, "5g");
}

static void state_of(json_object *d, char *buf, size_t len)
{
	json_object *mm = get(d, "modemmanager");
	const char *ss = str(get(d, "session"), "state");

	/* the IP session decides; a registration is not a connection (audit 2026-10-15: «в сети» with no session) */
	if (get(d, "present") && !json_object_get_boolean(get(d, "present")))
		snprintf(buf, len, "%s", T("unplugged from USB", "отключён от USB"));
	else if (get(d, "frozen"))
		snprintf(buf, len, "%s", T("frozen", "заморожен"));
	else if (ss)
		snprintf(buf, len, "%s", word(SESSION_W, ss, ""));
	else if (str(mm, "state") && (!strcmp(str(mm, "state"), "connected")))
		snprintf(buf, len, "%s", word(MM_W, "connected", ""));
	else if (str(mm, "state"))
		snprintf(buf, len, T("not connected (%s)", "не подключён (%s)"), word(MM_W, str(mm, "state"), ""));
	else
		snprintf(buf, len, "%s", T("no data", "нет данных"));
}

static void tech_of(json_object *mm, char *buf, size_t len)
{
	json_object *at = get(mm, "access_tech");
	bool lte = false, nr = false, g3 = false;

	for (size_t i = 0; at && json_object_is_type(at, json_type_array) && i < json_object_array_length(at); i++) {
		const char *t = json_object_get_string(json_object_array_get_idx(at, i));
		lte |= t && !strcmp(t, "lte");
		nr |= t && !strncmp(t, "5g", 2);
		g3 |= t && (!strcmp(t, "umts") || !strcmp(t, "hsdpa") || !strcmp(t, "hsupa") || !strcmp(t, "hspa"));
	}
	snprintf(buf, len, "%s", lte && nr ? "LTE/5G" : nr ? "5G" : lte ? "LTE" : g3 ? "3G" : "—");
}

/* ---- the modems' priority (LuCI's «Приоритет модемов»), and the order they are listed in ---- */

/* the order on screen while the operator moves modems with [ and ]; applied with P, dropped with Esc */
#define TOP_MAX_ORDER 32
static char g_order[TOP_MAX_ORDER][200];
static bool g_order_iface[TOP_MAX_ORDER];	/* only modems with an interface get a metric */
static int g_order_n;
static bool g_order_dirty;

/*
 * «Исключения автопереключения» (x), as the bot's menu: the channels failover never makes the main one, only the
 * operator by hand (core.failover_manual, by network device). A draft ticked on screen, applied with P against
 * the list it started from, so a change made in LuCI meanwhile is not overwritten.
 */
#define TOP_MAX_EX 16
static bool g_ex;			/* the screen is on */
static bool g_ex_loaded;		/* the draft has taken the saved list */
static char g_ex_base[TOP_MAX_EX][16];	/* the saved list the draft started from */
static int g_ex_base_n;
static char g_ex_pick[TOP_MAX_EX][16];	/* ticked */
static int g_ex_pick_n;
static char g_ex_dev[TOP_MAX_EX][16];	/* the channels on screen, in their order */
static int g_ex_n;

static bool has_iface(json_object *d) { return str(get(d, "connection"), "interface") != NULL; }
static bool is_present(json_object *d) { return !get(d, "present") || json_object_get_boolean(get(d, "present")); }
static bool is_connected(json_object *d) { const char *ss = str(get(d, "session"), "state"); return ss && !strcmp(ss, "connected"); }

static double metric_of(json_object *d)
{
	double m;
	return num(get(d, "connection"), "metric", &m) ? m : 1e9;
}

static int by_metric(const void *a, const void *b)
{
	json_object *x = *(json_object *const *)a, *y = *(json_object *const *)b;
	double mx = metric_of(x), my = metric_of(y);
	const char *ix = str(x, "device_id"), *iy = str(y, "device_id");

	return mx < my ? -1 : mx > my ? 1 : strcmp(ix ? ix : "", iy ? iy : "");
}

static int order_index(json_object *d)
{
	for (int i = 0; i < g_order_n; i++)
		if (str(d, "device_id") && !strcmp(g_order[i], str(d, "device_id")))
			return i;
	return TOP_MAX_ORDER;
}

static int by_order(const void *a, const void *b)
{
	int x = order_index(*(json_object *const *)a), y = order_index(*(json_object *const *)b);
	return x != y ? x - y : by_metric(a, b);
}

/* the list as the dashboard's: by the route metric (the priority), or by the operator's unapplied order */
static void order_devices(json_object *devs)
{
	json_object *list = get(devs, "devices"), *v[64], *sorted;
	size_t n = list && json_object_is_type(list, json_type_array) ? json_object_array_length(list) : 0;

	if (n > 64)
		n = 64;
	for (size_t i = 0; i < n; i++)
		v[i] = json_object_get(json_object_array_get_idx(list, i));
	qsort(v, n, sizeof(v[0]), g_order_dirty ? by_order : by_metric);
	sorted = json_object_new_array();
	for (size_t i = 0; i < n; i++)
		json_object_array_add(sorted, v[i]);
	json_object_object_add(devs, "devices", sorted);
	if (!g_order_dirty) {
		g_order_n = 0;
		for (size_t i = 0; i < n && g_order_n < TOP_MAX_ORDER; i++)
			if (str(v[i], "device_id"))
				snprintf(g_order[g_order_n++], sizeof(g_order[0]), "%s", str(v[i], "device_id"));
	}
	for (size_t i = 0; i < n; i++) {
		int k = order_index(v[i]);
		if (k < g_order_n)
			g_order_iface[k] = has_iface(v[i]);
	}
}

/* [ and ]: the selected modem one place up or down in the unapplied order */
static int order_move(int sel, int by)
{
	char t[200];

	if (sel < 0 || sel >= g_order_n || sel + by < 0 || sel + by >= g_order_n)
		return sel;
	memcpy(t, g_order[sel], sizeof(t));
	memcpy(g_order[sel], g_order[sel + by], sizeof(t));
	memcpy(g_order[sel + by], t, sizeof(t));
	{
		bool f = g_order_iface[sel];
		g_order_iface[sel] = g_order_iface[sel + by];
		g_order_iface[sel + by] = f;
	}
	g_order_dirty = true;
	return sel + by;
}

/* the engine's reasons (English as the engine says them) and in Russian (LuCI's PAUSE_WHY and the priority card's) */
static const char *const REASON_W[][3] = {
	{ "the modem has no network interface yet", "the modem has no network interface yet", "у модема ещё нет сетевого интерфейса" },
	{ "a recovery is running on this modem", "a recovery is running on this modem", "идёт восстановление" },
	{ "a bring-up is running", "a bring-up is running", "идёт подключение" },
	{ "a configuration change is being applied", "another change is being applied, try again in a few seconds",
	  "сейчас применяются другие настройки, повторите через несколько секунд" },
	{ "netifd did not take the request", "netifd did not take the request", "netifd не принял команду" },
	{ "an eSIM operation holds this modem", "an eSIM operation holds this modem", "идёт операция с eSIM" },
	{ "a bring-up is running on this modem", "a bring-up is running on this modem", "идёт подключение" },
	{ NULL, NULL, NULL } };

/* ---- screens ---- */

static int render_list(json_object *status, json_object *devs, json_object *up, int sel, int *count)
{
	json_object *list = get(devs, "devices"), *mmst = get(status, "modemmanager"), *carrier = get(up, "carrier");
	char t[16], b1[16], b2[16];
	size_t n = list && json_object_is_type(list, json_type_array) ? json_object_array_length(list) : 0;

	out(T("%sMedveModem %s%s · ModemManager: %s · Internet: %s%s%s%s   %s%s%s", "%sMedveModem %s%s · ModemManager: %s · Интернет: %s%s%s%s   %s%s%s"),
	    C_BOLD, str(status, "version") ? str(status, "version") : "?",
	    C_OFF, word(MMB_W, str(mmst, "backend"), T("unknown", "неизвестно")), word(NET_W, str(up, "internet"), T("unknown", "неизвестно")),
	    str(carrier, "name") ? T(", via ", ", через «") : "", str(carrier, "name") ? str(carrier, "name") : "", str(carrier, "name") ? T("", "»") : "",
	    C_DIM, clock_of((long long)time(NULL), t, sizeof(t)), C_OFF);
	out("");
	if (!n) {
		out("%s", T("No modems found.", "Модемы не найдены."));
		*count = 0;
		return 0;
	}
	{
		char p1[128], p2[96], p3[64], p4[32], p5[64];
		/* the headings go through pad() as the cells do, so either language keeps the columns */
		out("%s #  %s  %s %s %s %s %s %s%s", C_DIM, T("Pr", "Пр"), pad(T("Modem", "Модем"), 22, p1, sizeof(p1)),
		    pad(T("State", "Состояние"), 18, p2, sizeof(p2)), pad(T("Operator", "Оператор"), 14, p3, sizeof(p3)),
		    pad(T("Net", "Сеть"), 7, p4, sizeof(p4)), pad(T("Role", "Роль"), 17, p5, sizeof(p5)),
		    T("Signal (RSRP · SINR)", "Сигнал (RSRP · SINR)"), C_OFF);
	}
	for (size_t i = 0, rank = 0; i < n; i++) {
		json_object *d = json_object_array_get_idx(list, i), *mm = get(d, "modemmanager"), *car = signal_carrier(mm);
		char name[64], st[40], tech[16], sig[96] = "", role[40] = "", pr[8] = "—";
		double rsrp, snr;

		name_of(d, name, sizeof(name));
		state_of(d, st, sizeof(st));
		tech_of(mm, tech, sizeof(tech));
		if (num(car, "rsrp", &rsrp)) {
			int z = rsrp_zone(rsrp);
			snprintf(sig, sizeof(sig), "%s%s %s%s%s", zone_color(z), bars(z), n1(rsrp, b1, sizeof(b1)), C_OFF,
				 num(car, "snr", &snr) ? (snprintf(b2, sizeof(b2), " · %s", n1(snr, t, sizeof(t))), b2) : "");
		}
		snprintf(role, sizeof(role), "%s", str(get(d, "uplink"), "role") ? word(ROLE_W, str(get(d, "uplink"), "role"), "") : "");
		/* the priority: 1 the main channel, the rest the backups in this order (only modems with an interface have one) */
		if (has_iface(d))
			snprintf(pr, sizeof(pr), "%zu%s", ++rank, g_order_dirty ? "*" : "");
		{
			char p1[128], p2[96], p3[64], p4[32], p5[64], p6[16];
			out("%s%2zu  %s %s %s %s %s %s%s %s", (int)i == sel ? C_INV : "", i + 1, pad(pr, 3, p6, sizeof(p6)), pad(name, 22, p1, sizeof(p1)),
			    pad(st, 18, p2, sizeof(p2)), pad(str(mm, "operator") ? str(mm, "operator") : "—", 14, p3, sizeof(p3)), pad(tech, 7, p4, sizeof(p4)),
			    pad(*role ? role : "—", 17, p5, sizeof(p5)), (int)i == sel ? C_OFF : "", *sig ? sig : "—");
		}
	}
	if (g_order_dirty) {
		out("");
		out("%s%s%s", C_BOLD, T("Order changed (not applied): P apply, Esc cancel. The first is the main channel, the rest are backups.",
				       "Порядок изменён (не применён): P — применить, Esc — отменить. Первый — основной канал, остальные — резерв."), C_OFF);
	}
	*count = (int)n;
	return 0;
}

static void kv(const char *k, const char *fmt, ...)
{
	va_list ap;
	char *s = NULL;

	va_start(ap, fmt);
	if (vasprintf(&s, fmt, ap) < 0)
		s = NULL;
	va_end(ap);
	char p[64];
	out("%s%s%s %s", C_DIM, pad(k, 15, p, sizeof(p)), C_OFF, s ? s : "");
	free(s);
}

/* ---- the radio tab, as LuCI's (modem.js radio()) ---- */

static const char *const STRENGTH_W[][2] = { { "very weak", "очень слабый" }, { "weak", "слабый" }, { "good", "хороший" },
	{ "excellent", "отличный" } };
/* SINR zones, as the page's: < 0, < 10, < 20 dB */
static int sinr_zone(double v) { return v < 0 ? 0 : v < 10 ? 1 : v < 20 ? 2 : 3; }
static const char *const NOISE_W[][2] = { { "heavy interference", "сильные помехи" }, { "noticeable interference", "заметные помехи" },
	{ "slight interference", "небольшие помехи" }, { "clean", "чисто" } };

static bool is_type(json_object *c, const char *t) { return str(c, "type") && !strcmp(str(c, "type"), t); }

static const char *band_of(json_object *c, char *buf, size_t len)
{
	double band;

	if (num(c, "band", &band))
		snprintf(buf, len, "%s%.0f", is_type(c, "5gnr") ? "n" : is_type(c, "lte") ? "B" : "", band);
	else
		snprintf(buf, len, "%s", is_type(c, "5gnr") ? "5G" : is_type(c, "lte") ? "LTE" : str(c, "type") ? str(c, "type") : "?");
	return buf;
}

/* ModemManager's bandwidth is in kHz, QMI's and AT's in MHz */
static double mhz_of(json_object *c)
{
	double bw;

	if (num(c, "bandwidth_mhz", &bw) && bw > 0)
		return bw;
	if (num(c, "bandwidth", &bw) && bw > 0)
		return bw >= 1000 ? bw / 1000 : bw;
	return 0;
}

/* LTE and 5G lines from ModemManager's signal; false when it has neither */
static bool radio_tech_lines(json_object *mm, bool emit)
{
	json_object *sig = get(mm, "signal");
	bool any = false;

	for (int g = 0; g < 2; g++) {
		json_object *t = get(sig, g ? "5g" : "lte");
		double v;
		char line[256] = "", b[16];

		if (num(t, "rsrp", &v)) {
			int z = rsrp_zone(v);
			snprintf(line, sizeof(line), T("%s%s RSRP %s dBm (%s)%s", "%s%s RSRP %s дБм (%s)%s"), zone_color(z), bars(z), n1(v, b, sizeof(b)),
				 STRENGTH_W[z][ru], C_OFF);
		}
		if (num(t, "snr", &v))
			snprintf(line + strlen(line), sizeof(line) - strlen(line), T("%sSINR %s dB (%s)", "%sSINR %s дБ (%s)"), *line ? " · " : "",
				 n1(v, b, sizeof(b)), NOISE_W[sinr_zone(v)][ru]);
		if (num(t, "rsrq", &v))
			snprintf(line + strlen(line), sizeof(line) - strlen(line), T("%sRSRQ %s dB", "%sRSRQ %s дБ"), *line ? " · " : "", n1(v, b, sizeof(b)));
		if (num(t, "rssi", &v))
			snprintf(line + strlen(line), sizeof(line) - strlen(line), T("%sRSSI %s dBm", "%sRSSI %s дБм"), *line ? " · " : "", n1(v, b, sizeof(b)));
		if (!*line)
			continue;
		any = true;
		if (emit)
			kv(g ? "5G" : "LTE", "%s", line);
	}
	return any;
}

/* "eutran-3", "ngran-78", "utran-1" -> "LTE: B3, B7; 5G: n78", in band order */
static void bands_text(json_object *list, char *out_, size_t len)
{
	static const char *const PRE[][3] = { { "eutran-", "LTE", "B" }, { "ngran-", "5G", "n" }, { "utran-", "3G", "" } };
	size_t n = list && json_object_is_type(list, json_type_array) ? json_object_array_length(list) : 0;

	out_[0] = '\0';
	for (int g = 0; g < 3; g++) {
		int nums[128], k = 0;
		size_t pl = strlen(PRE[g][0]);

		for (size_t i = 0; i < n && k < 128; i++) {
			const char *b = json_object_get_string(json_object_array_get_idx(list, i));
			if (b && !strncmp(b, PRE[g][0], pl) && atoi(b + pl) > 0)
				nums[k++] = atoi(b + pl);
		}
		for (int i = 1; i < k; i++)
			for (int j = i; j > 0 && nums[j] < nums[j - 1]; j--) {
				int t = nums[j]; nums[j] = nums[j - 1]; nums[j - 1] = t;
			}
		for (int i = 0; i < k; i++)
			snprintf(out_ + strlen(out_), len - strlen(out_), "%s%s%s%d", i ? ", " : *out_ ? "; " : "",
				 i ? "" : PRE[g][1], i ? PRE[g][2] : (g == 2 ? ": " : g == 1 ? ": n" : ": B"), nums[i]);
	}
}

static const char *const MODE_W[][3] = { { "2g", "2G", "2G" }, { "3g", "3G", "3G" }, { "4g", "LTE", "LTE" }, { "5g", "5G", "5G" },
	{ "any", "any", "любые" }, { "none", "—", "—" }, { NULL, NULL, NULL } };

static void render_radio(json_object *d, json_object *mm)
{
	json_object *cells = get(mm, "cells"), *ant = get(mm, "antennas"), *loc = get(mm, "location"), *rs = get(d, "radio_settings");
	size_t ncells = cells && json_object_is_type(cells, json_type_array) ? json_object_array_length(cells) : 0;
	json_object *pcell = NULL;
	char line[512], b1[24], b2[24], b3[24];
	double v, w;

	radio_tech_lines(mm, true);
	if (num(mm, "signal_quality", &v))
		kv(T("Quality", "Качество"), "%.0f%%", v);

	/* the serving cell: the primary, else the first serving one */
	for (size_t i = 0; i < ncells; i++) {
		json_object *c = json_object_array_get_idx(cells, i);
		const char *role = str(c, "role");
		if (!get(c, "serving") || !json_object_get_boolean(get(c, "serving")))
			continue;
		if (!pcell || (role && (!strcmp(role, "primary") || !strcmp(role, "serving"))))
			pcell = pcell && str(pcell, "role") && !strcmp(str(pcell, "role"), "primary") ? pcell : c;
	}
	{
		/* where the modem is: hidden in a plain snapshot, as the identifiers (LuCI shows it on request) */
		const char *tac = str(loc, "tac") ? str(loc, "tac") : str(loc, "lac") ? str(loc, "lac") : str(pcell, "tac");
		const char *cid = str(loc, "cid") ? str(loc, "cid") : str(pcell, "ci");
		const char *ci = str(pcell, "ci") ? str(pcell, "ci") : cid;
		unsigned long n = ci && strspn(ci, "0123456789abcdefABCDEF") == strlen(ci) && strlen(ci) <= 8 ? strtoul(ci, NULL, 16) : 0;

		if (tac || cid)
			kv(T("Cell", "Сота"), "%s", redact ? T("TAC / CID hidden (--show-identifiers)", "TAC / CID скрыты (--show-identifiers)") :
			   (snprintf(line, sizeof(line), "TAC %s · CID %s", tac ? tac : "—", cid ? cid : "—"), line));
		if (pcell && is_type(pcell, "lte") && n > 0 && n <= 0xfffffff && !redact)
			kv(T("Base station", "Базовая"), T("eNB %lu, sector %lu%s%s", "eNB %lu, сектор %lu%s%s"), n / 256, n % 256, str(pcell, "pci") ? " · PCI " : "", str(pcell, "pci") ? str(pcell, "pci") : "");
		/* LTE's timing advance: one step is 78.12 m one way (36.213 4.2.3) */
		if (pcell && is_type(pcell, "lte") && num(pcell, "timing_advance", &v) && v >= 0 && v <= 1282) {
			double m = v * 78.12;
			if (m < 1000)
				kv(T("Distance", "Расстояние"), T("≈ %.0f m (TA %.0f, ±80 m)", "≈ %.0f м (TA %.0f, ±80 м)"), (double)((long)(m / 10 + 0.5) * 10), v);
			else
				kv(T("Distance", "Расстояние"), T("≈ %s km (TA %.0f, ±80 m)", "≈ %s км (TA %.0f, ±80 м)"), n1(m / 1000, b1, sizeof(b1)), v);
		}
	}

	{
		/* LTE carriers from QMI's carrier aggregation info when the engine has it (ModemManager lists the primary only) */
		json_object *ca = get(get(mm, "ca"), "cells");
		bool have_ca = ca && json_object_is_type(ca, json_type_array) && json_object_array_length(ca);
		size_t shown = 0;
		double total = 0;

		for (size_t i = 0; have_ca && i < json_object_array_length(ca); i++) {
			json_object *c = json_object_array_get_idx(ca, i);
			const char *stt = str(c, "state");
			bool idle = stt && (!strcmp(stt, "deactivated") || !strcmp(stt, "deconfigured"));
			double band, pci;
			char p1[32], p2[48];

			snprintf(b3, sizeof(b3), "B%.0f", num(c, "band", &band) ? band : 0);
			snprintf(line, sizeof(line), "%s%s %s PCI %-4.0f", idle ? C_DIM : "", pad(num(c, "band", &band) ? b3 : "?", 4, p1, sizeof(p1)),
				 pad(word(ROLEC_W, str(c, "role"), ""), 10, p2, sizeof(p2)), num(c, "pci", &pci) ? pci : 0);
			if (mhz_of(c) > 0)
				snprintf(line + strlen(line), sizeof(line) - strlen(line), T("  %s MHz", "  %s МГц"), n1(mhz_of(c), b1, sizeof(b1)));
			if (idle)
				snprintf(line + strlen(line), sizeof(line) - strlen(line), T("  configured, not in use%s", "  настроена, не используется%s"), C_OFF);
			else
				total += mhz_of(c);
			kv(shown++ ? "" : T("Carriers", "Несущие"), "%s", line);
		}
		for (size_t i = 0; i < ncells; i++) {
			json_object *c = json_object_array_get_idx(cells, i);
			char p1[32], p2[48];
			double ch;

			if (!get(c, "serving") || !json_object_get_boolean(get(c, "serving")))
				continue;
			if (have_ca && is_type(c, "lte"))
				continue;
			snprintf(line, sizeof(line), "%s %s PCI %-4s", pad(band_of(c, b3, sizeof(b3)), 4, p1, sizeof(p1)),
				 pad(word(ROLEC_W, str(c, "role"), ""), 10, p2, sizeof(p2)), str(c, "pci") ? str(c, "pci") : "—");
			if (num(c, "rsrp", &v))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "  %sRSRP %s%s", zone_color(rsrp_zone(v)), n1(v, b1, sizeof(b1)), C_OFF);
			if (num(c, "rsrq", &v))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "  RSRQ %s", n1(v, b1, sizeof(b1)));
			if (num(c, "sinr", &v))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "  SINR %s", n1(v, b2, sizeof(b2)));
			if (mhz_of(c) > 0)
				snprintf(line + strlen(line), sizeof(line) - strlen(line), T("  %s MHz", "  %s МГц"), n1(mhz_of(c), b1, sizeof(b1)));
			if (num(c, "earfcn", &ch) || num(c, "nrarfcn", &ch))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "  %s%s %.0f%s", C_DIM, get(c, "earfcn") ? "EARFCN" : "ARFCN", ch, C_OFF);
			total += mhz_of(c);
			kv(shown++ ? "" : T("Carriers", "Несущие"), "%s", line);
		}
		if (shown > 1 && total > 0)
			kv("", T("%stotal %s MHz%s", "%sвсего %s МГц%s"), C_DIM, n1(total, b1, sizeof(b1)), C_OFF);
	}

	{
		/* the neighbours, strongest first, 8 at most (LuCI's table holds 16) */
		json_object *nb[64];
		size_t n = 0;

		for (size_t i = 0; i < ncells && n < 64; i++) {
			json_object *c = json_object_array_get_idx(cells, i);
			if (!(get(c, "serving") && json_object_get_boolean(get(c, "serving"))))
				nb[n++] = c;
		}
		for (size_t i = 1; i < n; i++)
			for (size_t j = i; j > 0; j--) {
				double a = -999, bb = -999;
				num(nb[j - 1], "rsrp", &a);
				num(nb[j], "rsrp", &bb);
				if (bb <= a)
					break;
				json_object *t = nb[j]; nb[j] = nb[j - 1]; nb[j - 1] = t;
			}
		for (size_t i = 0; i < n && i < 8; i++) {
			json_object *c = nb[i];
			char p1[32];
			double ch;

			snprintf(line, sizeof(line), "%s PCI %-4s", pad(band_of(c, b3, sizeof(b3)), 4, p1, sizeof(p1)), str(c, "pci") ? str(c, "pci") : "—");
			if (num(c, "earfcn", &ch) || num(c, "nrarfcn", &ch))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), T("  channel %-6.0f", "  канал %-6.0f"), ch);
			if (num(c, "rsrp", &v))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "  %sRSRP %s%s", zone_color(rsrp_zone(v)), n1(v, b1, sizeof(b1)), C_OFF);
			if (num(c, "rsrq", &w))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "  RSRQ %s", n1(w, b2, sizeof(b2)));
			kv(i ? "" : T("Neighbours", "Соседние"), "%s", line);
		}
		if (n > 8)
			kv("", T("%sand %zu more%s", "%sи ещё %zu%s"), C_DIM, n - 8, C_OFF);
	}

	for (int g = 0, shown = 0; g < 2; g++) {
		json_object *grp = get(ant, g ? "nr" : "lte"), *ch = get(grp, "chains");
		double best = -1000;
		if (!ch || !json_object_is_type(ch, json_type_array) || !json_object_array_length(ch))
			continue;
		for (size_t i = 0; i < json_object_array_length(ch); i++)
			if (num(json_object_array_get_idx(ch, i), "rsrp", &v) && v > best)
				best = v;
		snprintf(line, sizeof(line), "%s", g ? "5G " : "LTE");
		for (size_t i = 0; i < json_object_array_length(ch); i++) {
			json_object *c = json_object_array_get_idx(ch, i);
			double cn = 0;
			num(c, "chain", &cn);
			if (num(c, "rsrp", &v) && get(c, "tuned") && json_object_get_boolean(get(c, "tuned")))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), "  RX%.0f %s%s%s", cn,
					 best - v >= 10 ? zone_color(0) : "", n1(v, b1, sizeof(b1)), best - v >= 10 ? C_OFF : "");
			else
				snprintf(line + strlen(line), sizeof(line) - strlen(line), T("  %sRX%.0f unused%s", "  %sRX%.0f не исп.%s"), C_DIM, cn, C_OFF);
		}
		kv(shown++ ? "" : T("Antennas", "Антенны"), "%s", line);
	}

	if (rs) {
		/* network mode and bands, whoever drives the modem (spec 25.9.12); read-only here, changed in LuCI */
		json_object *modes = get(rs, "modes"), *al = get(modes, "allowed");
		char bt[256];

		line[0] = '\0';
		for (size_t i = 0; al && json_object_is_type(al, json_type_array) && i < json_object_array_length(al); i++)
			snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s", i ? " + " : "",
				 word(MODE_W, json_object_get_string(json_object_array_get_idx(al, i)), json_object_get_string(json_object_array_get_idx(al, i))));
		if (*line)
			kv(T("Network mode", "Режим сети"), "%s%s%s", line,
			   str(modes, "preferred") && strcmp(str(modes, "preferred"), "none") ? T(" · preferred ", " · приоритет ") : "",
			   str(modes, "preferred") && strcmp(str(modes, "preferred"), "none") ? word(MODE_W, str(modes, "preferred"), str(modes, "preferred")) : "");
		bands_text(get(rs, "bands"), bt, sizeof(bt));
		if (get(rs, "bands_any") && json_object_get_boolean(get(rs, "bands_any")))
			kv(T("Bands", "Диапазоны"), "%s", T("all the modem supports", "все, что поддерживает модем"));
		else if (*bt)
			kv(T("Bands", "Диапазоны"), "%s", bt);
		if (!*line && !*bt && str(rs, "why")) {
			static const char *const WHY[][3] = {
				{ "not read yet", "not read yet: read while the Radio tab is open", "ещё не прочитаны: прочитаются при открытой вкладке «Радио»" },
				{ "QModem drives this modem", "QModem drives this modem; the settings are read through it",
				  "модемом управляет QModem, настройки читаются через него" },
				{ "reads it over AT", "the settings are read with AT commands", "настройки читаются AT-командами" },
				{ "no AT reader", "reading the settings over AT is not done for this model yet",
				  "для этой модели чтение настроек по AT ещё не сделано" },
				{ "modem's own router holds", "the modem's own router holds them: set them on its web page", "модем сам себе роутер: настройки — на его веб-странице" },
				{ "no owner reports", "nobody drives the modem now — radio settings unavailable",
				  "сейчас модемом никто не управляет — настройки радио недоступны" }, { NULL, NULL, NULL } };
			const char *say = NULL;
			for (int i = 0; WHY[i][0] && !say; i++)
				if (strstr(str(rs, "why"), WHY[i][0]))
					say = LW(WHY, i);
			kv(T("Network mode", "Режим сети"), "%s%s%s%s", C_DIM, say ? "" : T("unavailable: ", "недоступно: "), say ? say : str(rs, "why"), C_OFF);
		}
	}
	if (str(mm, "cells_source") && !strcmp(str(mm, "cells_source"), "at"))
		kv("", "%s%s%s", C_DIM, T("cells read over MedveModem's AT port (at most every 30 s, while this tab is open)",
					  "соты прочитаны через AT-порт MedveModem (не чаще раза в 30 с, пока открыта эта вкладка)"), C_OFF);
}

static void render_detail(json_object *d, json_object *events)
{
	json_object *mm = get(d, "modemmanager"), *ses = get(d, "session"), *nd = get(ses, "netifd"), *upl = get(d, "uplink");
	json_object *car = signal_carrier(mm), *card = get(mm, "sim_card"), *lab = get(d, "sim_label"), *rec = get(d, "recovery");
	json_object *cells = get(mm, "cells");
	char name[64], st[40], tech[16], b1[16], b2[16], b3[16], line[512];
	double v, w;

	name_of(d, name, sizeof(name));
	state_of(d, st, sizeof(st));
	tech_of(mm, tech, sizeof(tech));
	out("%s%s%s — %s %s · %s", C_BOLD, name, C_OFF, str(mm, "manufacturer") ? str(mm, "manufacturer") : "",
	    str(mm, "model") ? str(mm, "model") : str(d, "product") ? str(d, "product") : "", str(d, "device_id") ? str(d, "device_id") : "");
	if (g_tab >= 0)
		out(" %s %s %s  %s %s %s  %s %s %s", g_tab == 0 ? C_INV : "", TAB_NAMES[0][ru], g_tab == 0 ? C_OFF : "",
		    g_tab == 1 ? C_INV : "", TAB_NAMES[1][ru], g_tab == 1 ? C_OFF : "", g_tab == 2 ? C_INV : "", TAB_NAMES[2][ru], g_tab == 2 ? C_OFF : "");
	out("");
	if (SHOW(0)) {
	line[0] = '\0';
	if (str(ses, "interface"))
		snprintf(line + strlen(line), sizeof(line) - strlen(line), " · %s", str(ses, "interface"));
	if (str(nd, "ipv4"))
		snprintf(line + strlen(line), sizeof(line) - strlen(line), " · %s", str(nd, "ipv4"));
	if (num(nd, "uptime", &v) && v > 0)
		snprintf(line + strlen(line), sizeof(line) - strlen(line), " · %s", ago((long long)v, b1, sizeof(b1)));
	if (num(get(upl, "reach"), "latency_ms", &v))
		snprintf(line + strlen(line), sizeof(line) - strlen(line), T(" · latency %s ms", " · отклик %s мс"), n1(v, b2, sizeof(b2)));
	if (str(upl, "role"))
		snprintf(line + strlen(line), sizeof(line) - strlen(line), " · %s", word(ROLE_W, str(upl, "role"), ""));
	kv(T("Connection", "Соединение"), "%s%s", st, line);
	{
		/* the diagnosis by chain, as the page's (chain.c): ✓/✗/?/– per link, the reason of the broken one */
		static const char *const LINK_W[][3] = { { "device", "Device", "Устройство" }, { "network", "SIM & network", "SIM и сеть" },
			{ "session", "Session", "Сеанс" }, { "internet", "Internet", "Интернет" }, { "dns", "DNS", "DNS" }, { NULL, NULL, NULL } };
		static const char *const MARK[][3] = { { "ok", "✓", "✓" }, { "fail", "✗", "✗" }, { "unknown", "?", "?" }, { "skipped", "–", "–" },
			{ "warn", "!", "!" }, { NULL, NULL, NULL } };
		static const char *const MARK_A[][3] = { { "ok", "+", "+" }, { "fail", "X", "X" }, { "unknown", "?", "?" }, { "skipped", "-", "-" },
			{ "warn", "!", "!" }, { NULL, NULL, NULL } };
		static const char *const WHY_W[][3] = {
			{ "absent", "the modem is not seen on USB", "модем не виден на USB" },
			{ "composition", "the modem came up incomplete", "модем определился не полностью" },
			{ "service-mode", "service mode", "служебный режим" },
			{ "modeswitch-rule", "a usb-modeswitch rule resets the modem", "правило usb-modeswitch сбрасывает модем" },
			{ "sim-missing", "no SIM card", "нет SIM-карты" }, { "sim-locked", "the SIM waits for its PIN", "SIM ждёт PIN-код" },
			{ "registration-denied", "the operator denied registration", "оператор отказал в регистрации" },
			{ "searching", "searching for a network", "поиск сети" }, { "not-registered", "not registered on a network", "нет регистрации в сети" },
			{ "no-session", "no session", "сеанс не установлен" }, { "session-failed", "the session does not come up", "сеанс не устанавливается" },
			{ "whitelist", "operator's whitelist only", "только белый список оператора" },
			{ "restricted", "the operator restricted access", "оператор ограничил доступ" },
			{ "no-answer", "the check addresses do not answer", "проверочные адреса не отвечают" },
			{ "dns-failed", "the router's DNS does not answer", "DNS роутера не отвечает" },
			{ "dns-intercepted", "the operator answers for other DNS servers", "оператор отвечает за чужие DNS-серверы" },
			{ "dns-stub", "a stub instead of a blocked site's address", "заглушка вместо адреса заблокированного сайта" },
			{ "dns-rewritten", "the operator's DNS rewrites nonexistent names", "DNS оператора подменяет несуществующие имена" },
			{ NULL, NULL, NULL } };
		json_object *dg = get(d, "diagnosis"), *links = get(dg, "links");
		char ch[256] = "";

		for (size_t i = 0; links && json_object_is_type(links, json_type_array) && i < json_object_array_length(links); i++) {
			json_object *l = json_object_array_get_idx(links, i);
			snprintf(ch + strlen(ch), sizeof(ch) - strlen(ch), "%s%s %s", i ? (ascii ? " > " : " → ") : "",
				 word(ascii ? MARK_A : MARK, str(l, "state"), "?"), word(LINK_W, str(l, "link"), "?"));
		}
		if (*ch)
			kv(T("Chain", "Цепочка"), "%s", ch);
		if (str(dg, "link"))
			kv("", T("%sproblem: %s — %s%s%s%s%s", "%sпроблема: %s — %s%s%s%s%s"), C_DIM, word(LINK_W, str(dg, "link"), ""), word(WHY_W, str(dg, "reason"), ""),
			   str(dg, "detail") ? " (" : "", str(dg, "detail") ? str(dg, "detail") : "", str(dg, "detail") ? ")" : "", C_OFF);
		else if (get(dg, "warning"))
			kv("", T("%swarning: %s — %s%s%s%s%s", "%sвнимание: %s — %s%s%s%s%s"), C_DIM, word(LINK_W, str(get(dg, "warning"), "link"), ""),
			   word(WHY_W, str(get(dg, "warning"), "reason"), ""), str(get(dg, "warning"), "detail") ? " (" : "",
			   str(get(dg, "warning"), "detail") ? str(get(dg, "warning"), "detail") : "", str(get(dg, "warning"), "detail") ? ")" : "", C_OFF);
	}
	{
		/* addresses with prefix, both gateways and DNS, as the page shows them */
		json_object *a4 = get(nd, "ipv4_addresses"), *a6 = get(nd, "ipv6_addresses"), *dns = get(nd, "dns");
		char al[256] = "", dl[256] = "";
		for (int f = 0; f < 2; f++)
			for (size_t i = 0; (f ? a6 : a4) && json_object_is_type(f ? a6 : a4, json_type_array) && i < json_object_array_length(f ? a6 : a4); i++)
				snprintf(al + strlen(al), sizeof(al) - strlen(al), "%s%s", *al ? ", " : "", json_object_get_string(json_object_array_get_idx(f ? a6 : a4, i)));
		for (size_t i = 0; dns && json_object_is_type(dns, json_type_array) && i < json_object_array_length(dns); i++)
			snprintf(dl + strlen(dl), sizeof(dl) - strlen(dl), "%s%s", *dl ? ", " : "", json_object_get_string(json_object_array_get_idx(dns, i)));
		if (*al)
			kv(T("Addresses", "Адреса"), "%s", al);
		if (str(nd, "gateway4") || str(nd, "gateway6") || str(nd, "gateway"))
			kv(T("Gateway", "Шлюз"), "%s%s%s", str(nd, "gateway4") ? str(nd, "gateway4") : str(nd, "gateway") ? str(nd, "gateway") : "",
			   str(nd, "gateway6") && (str(nd, "gateway4") || str(nd, "gateway")) ? ", " : "", str(nd, "gateway6") ? str(nd, "gateway6") : "");
		if (*dl)
			kv("DNS", "%s", dl);
	}
	}

	line[0] = '\0';
	for (size_t i = 0; cells && json_object_is_type(cells, json_type_array) && i < json_object_array_length(cells); i++) {
		json_object *c = json_object_array_get_idx(cells, i);
		double band;
		if (!get(c, "serving") || !json_object_get_boolean(get(c, "serving")) || !num(c, "band", &band))
			continue;
		snprintf(line + strlen(line), sizeof(line) - strlen(line), "%s%s%.0f", *line ? "+" : "",
			 str(c, "type") && !strcmp(str(c, "type"), "5gnr") ? "n" : "B", band);
	}
	if (SHOW(0) || SHOW(1))
	kv(T("Network", "Сеть"), "%s%s%s%s · %s · %s%s%s", str(mm, "operator") ? str(mm, "operator") : "—",
	   str(mm, "operator_code") ? " (" : "", str(mm, "operator_code") ? str(mm, "operator_code") : "",
	   str(mm, "operator_code") ? ")" : "", word(REG_W, str(mm, "registration"), "—"), tech, *line ? " · " : "", line);

	/* the radio tab speaks per technology (LuCI's LTE and 5G cards); the overview keeps one line */
	if (SHOW(0) || (SHOW(1) && !radio_tech_lines(mm, false))) {
		if (num(car, "rsrp", &v)) {
			int z = rsrp_zone(v);
			snprintf(line, sizeof(line), T("%s%s RSRP %s dBm%s", "%s%s RSRP %s дБм%s"), zone_color(z), bars(z), n1(v, b1, sizeof(b1)), C_OFF);
			if (num(car, "rsrq", &w))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), " · RSRQ %s", n1(w, b2, sizeof(b2)));
			if (num(car, "snr", &w))
				snprintf(line + strlen(line), sizeof(line) - strlen(line), T(" · SINR %s dB", " · SINR %s дБ"), n1(w, b3, sizeof(b3)));
			kv(T("Signal", "Сигнал"), "%s", line);
		} else {
			kv(T("Signal", "Сигнал"), "%s", T("no data", "нет данных"));
		}
	}

	if (SHOW(1))
		render_radio(d, mm);

	if (SHOW(2)) {

	{
		const char *ur = str(mm, "unlock_required");
		const char *simst = ur && !strcmp(ur, "sim-pin") ? T("PIN required", "требуется PIN") :
				    ur && !strcmp(ur, "sim-puk") ? T("PUK required", "требуется PUK") :
				    ur && strcmp(ur, "none") && strcmp(ur, "sim-pin2") && strcmp(ur, "sim-puk2") ? T("locked", "заблокирована") :
				    /* a SIM object with nothing read: the firmware says READY with no SIM in it (a tester's MF192) */
				    str(mm, "sim") && !str(get(mm, "sim_card"), "imsi") && !str(get(mm, "sim_card"), "iccid") &&
				    !str(get(mm, "sim_card"), "operator_code") && !str(get(d, "sim_operator"), "code") &&
				    !(str(mm, "registration") && (!strcmp(str(mm, "registration"), "home") || !strcmp(str(mm, "registration"), "roaming"))) ?
				    T("not read (no IMSI)", "не читается (нет IMSI)") :
				    str(mm, "sim") ? T("inserted", "вставлена") : T("not detected", "не обнаружена");	/* PIN2/PUK2 do not lock the connection */
		json_object *own = get(mm, "own_numbers");
		const char *number = str(lab, "number") ? str(lab, "number") :
				     own && json_object_array_length(own) ? json_object_get_string(json_object_array_get_idx(own, 0)) : NULL;
		char nb[32], ib[32], sb[32];

		kv("SIM", "%s%s%s%s%s%s%s%s%s", simst,
		   str(card, "operator_name") ? " · " : "", str(card, "operator_name") ? str(card, "operator_name") : "",
		   number ? " · " : "", number ? ident(number, nb, sizeof(nb)) : "",
		   str(lab, "description") ? T(" · \"", " · «") : "", str(lab, "description") ? str(lab, "description") : "",
		   str(lab, "description") ? T("\"", "»") : "", "");
		if (str(card, "iccid") || str(card, "imsi"))
			kv("", "ICCID %s · IMSI %s", ident(str(card, "iccid"), ib, sizeof(ib)), ident(str(card, "imsi"), sb, sizeof(sb)));
	}

	{
		char eb[32];

		kv(T("Device", "Устройство"), T("firmware %s · IMEI %s · managed by %s", "прошивка %s · IMEI %s · управляет %s"), str(mm, "firmware") ? str(mm, "firmware") : "—",
		   ident(str(mm, "imei"), eb, sizeof(eb)), word(OWNER_W, str(get(d, "owner"), "control"), "—"));
	}

	{
		const char *rs = str(rec, "state");
		kv(T("Recovery", "Восстановление"), "%s%s%s", word(REC_W, rs, "—"), str(rec, "current_level") ? ": " : "",
		   str(rec, "current_level") ? str(rec, "current_level") : "");
	}
	{
		/* the steps the engine offers, numbered for the keys 1–8 */
		json_object *lv = get(rec, "levels");
		for (size_t i = 0; lv && json_object_is_type(lv, json_type_array) && i < json_object_array_length(lv) && i < 8; i++) {
			json_object *l = json_object_array_get_idx(lv, i);
			bool sup = get(l, "supported") && json_object_get_boolean(get(l, "supported"));
			bool aut = get(l, "automatic") && json_object_get_boolean(get(l, "automatic"));
			kv("", "%s%zu  %s%s%s", sup ? "" : C_DIM, i + 1, word(STEP_W, str(l, "name"), "?"),
			   sup ? (aut ? T("  · automatic on failure, and by hand", "  · автоматически при сбое и вручную") : T("  · by hand only", "  · только вручную")) :
			   T("  · unavailable", "  · недоступно"), sup ? "" : C_OFF);
		}
		kv(T("Emergency", "Аварийно"), "%s", T("M  restart ModemManager (every modem it holds, ~1 min)",
						      "M  перезапустить ModemManager (все модемы под ним, ~1 мин)"));
	}
	}

	if (SHOW(0)) {

	{
		json_object *ev = get(events, "events");
		size_t n = ev && json_object_is_type(ev, json_type_array) ? json_object_array_length(ev) : 0;
		for (size_t i = 0; i < n && i < 8; i++) {
			json_object *e = json_object_array_get_idx(ev, i);
			double t = 0;
			num(e, "t", &t);
			char said[320];

			mm_event_words(str(e, "kind"), str(e, "detail"), said, sizeof(said));
			kv(i ? "" : T("Events", "События"), "%s%s%s %s", C_DIM, clock_of((long long)t, b1, sizeof(b1)), C_OFF, said);
		}
		if (!n)
			kv(T("Events", "События"), "—");
	}
	}
}

/* ---- the engine ---- */

/*
 * Every string from the engine is data, never terminal control (audit 2026-10-15): a modem's
 * name, an operator's or an event's text could carry ESC [2J, an OSC 52 clipboard write, CR or
 * BEL. C0 controls, DEL and the C1 range (UTF-8 C2 80..9F) become a space, in place.
 */
static void scrub_string(json_object *o)
{
	const char *v = json_object_get_string(o);
	size_t n = v ? strlen(v) : 0, i, k = 0;
	char *buf;
	bool dirty = false;

	for (i = 0; i < n; i++)
		if ((unsigned char)v[i] < 0x20 || v[i] == 0x7f ||
		    ((unsigned char)v[i] == 0xc2 && (unsigned char)v[i + 1] >= 0x80 && (unsigned char)v[i + 1] <= 0x9f))
			dirty = true;
	if (!dirty || !(buf = malloc(n + 1)))
		return;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)v[i];

		if (c == 0xc2 && (unsigned char)v[i + 1] >= 0x80 && (unsigned char)v[i + 1] <= 0x9f) {
			buf[k++] = ' ';
			i++;
		} else {
			buf[k++] = c < 0x20 || c == 0x7f ? ' ' : (char)c;
		}
	}
	buf[k] = '\0';
	json_object_set_string(o, buf);
	free(buf);
}

static void scrub(json_object *o)
{
	if (json_object_is_type(o, json_type_string)) {
		scrub_string(o);
	} else if (json_object_is_type(o, json_type_array)) {
		for (size_t i = 0; i < json_object_array_length(o); i++)
			scrub(json_object_array_get_idx(o, i));
	} else if (json_object_is_type(o, json_type_object)) {
		json_object_object_foreach(o, k, v) {
			(void)k;
			scrub(v);
		}
	}
}

/* an identifier as the frame shows it: whole, or its last four digits under --once */
static const char *ident(const char *v, char *buf, size_t len)
{
	size_t n = v ? strlen(v) : 0;

	if (!v)
		return "—";
	if (!redact)
		return v;
	snprintf(buf, len, "%s%s", ascii ? "**** " : "•••• ", n > 4 ? v + n - 4 : "");
	return buf;
}

static int ex_find(char list[][16], int n, const char *dev)
{
	for (int i = 0; i < n; i++)
		if (!strcmp(list[i], dev))
			return i;
	return -1;
}

static void ex_put(char list[][16], int *n, const char *dev)
{
	if (dev && *dev && strlen(dev) < 16 && *n < TOP_MAX_EX && ex_find(list, *n, dev) < 0)
		snprintf(list[(*n)++], 16, "%s", dev);
}

static void ex_toggle(const char *dev)
{
	int i = ex_find(g_ex_pick, g_ex_pick_n, dev);

	if (i < 0) {
		ex_put(g_ex_pick, &g_ex_pick_n, dev);
		return;
	}
	memmove(g_ex_pick[i], g_ex_pick[i + 1], (size_t)(g_ex_pick_n - i - 1) * sizeof(g_ex_pick[0]));
	g_ex_pick_n--;
}

static bool ex_changed(void)
{
	if (g_ex_pick_n != g_ex_base_n)
		return true;
	for (int i = 0; i < g_ex_pick_n; i++)
		if (ex_find(g_ex_base, g_ex_base_n, g_ex_pick[i]) < 0)
			return true;
	return false;
}

static void render_excl(json_object *up, int sel)
{
	json_object *fo = get(up, "failover"), *man = get(fo, "manual"), *paths = get(up, "paths");
	size_t np = paths && json_object_is_type(paths, json_type_array) ? json_object_array_length(paths) : 0;

	if (!g_ex_loaded && up) {
		/* the engine gives one device as a string, more as an array */
		g_ex_base_n = 0;
		if (man && json_object_is_type(man, json_type_string))
			ex_put(g_ex_base, &g_ex_base_n, json_object_get_string(man));
		for (size_t i = 0; man && json_object_is_type(man, json_type_array) && i < json_object_array_length(man); i++)
			ex_put(g_ex_base, &g_ex_base_n, json_object_get_string(json_object_array_get_idx(man, i)));
		memcpy(g_ex_pick, g_ex_base, sizeof(g_ex_pick));
		g_ex_pick_n = g_ex_base_n;
		g_ex_loaded = true;
	}
	g_ex_n = 0;
	for (size_t i = 0; i < np; i++)
		ex_put(g_ex_dev, &g_ex_n, str(json_object_array_get_idx(paths, i), "device"));
	for (int i = 0; i < g_ex_base_n; i++)	/* a saved channel that is not up now stays untickable */
		ex_put(g_ex_dev, &g_ex_n, g_ex_base[i]);
	out("%s%s%s", C_BOLD, T("Failover exclusions", "Исключения автопереключения"), C_OFF);
	out("%s", T("Failover never makes a ticked channel the main one: only you, by hand.",
		    "Отмеченные каналы автопереключение не делает основными: только вы, вручную."));
	out(T("Saved: %d · ticked: %d%s", "Сохранено: %d · выбрано: %d%s"), g_ex_base_n, g_ex_pick_n,
	    ex_changed() ? T(" · not applied", " · не применено") : "");
	if (fo && get(fo, "enabled") && !json_object_get_boolean(get(fo, "enabled")))
		out("%s%s%s", C_DIM, T("Failover is off in the settings: the list takes effect once it is on.",
				       "Автопереключение выключено в настройках: список подействует, когда его включат."), C_OFF);
	out("");
	if (!up) {
		out("%s", T("The MedveModem service did not answer.", "Служба MedveModem не ответила."));
		return;
	}
	if (!g_ex_n) {
		out("%s", T("No channels yet.", "Каналов пока нет."));
		return;
	}
	for (int i = 0; i < g_ex_n; i++) {
		const char *name = NULL, *role = NULL;
		char p1[128];

		for (size_t k = 0; k < np; k++) {
			json_object *pa = json_object_array_get_idx(paths, k);
			if (str(pa, "device") && !strcmp(str(pa, "device"), g_ex_dev[i])) {
				name = str(pa, "name");
				role = str(pa, "role");
				break;
			}
		}
		{
			char label[96];
			snprintf(label, sizeof(label), name ? "%s (%s)" : "%s%s", name ? name : g_ex_dev[i], name ? g_ex_dev[i] : "");
			out("%s%2d  %s %s %s%s", i == sel ? C_INV : "", i + 1, ex_find(g_ex_pick, g_ex_pick_n, g_ex_dev[i]) >= 0 ? "[x]" : "[ ]",
			    pad(label, 36, p1, sizeof(p1)), role ? word(ROLE_W, role, role) : T("not up", "не поднят"), i == sel ? C_OFF : "");
		}
	}
}

static json_object *ask(const char *method, const char *id, int limit)
{
	json_object *args = json_object_new_object(), *res = NULL;

	if (id)
		json_object_object_add(args, "id", json_object_new_string(id));
	if (limit > 0)
		json_object_object_add(args, "limit", json_object_new_int(limit));
	if (!strcmp(method, "devices"))
		json_object_object_add(args, "absent", json_object_new_boolean(1));
	call(method, args, &res, 5000);
	json_object_put(args);
	if (res)
		scrub(res);
	return res;
}

/*
 * r34 (F18): with ModemManager's cell info off, the Foxconn modems' cells come over AT^DEBUG? through
 * the engine, as LuCI asks for them: only while «Радио» is on screen. The engine sends AT^DEBUG? at
 * most every 30 s and answers from its reading inside the period. A plain snapshot never asks.
 */
static void ask_cells(const char *id, json_object *d)
{
	json_object *mm = get(d, "modemmanager"), *cells = get(mm, "cells"), *r;
	const char *dialect = str(get(d, "radio_settings"), "dialect");

	if (g_tab != 1 || !(get(d, "present") && json_object_get_boolean(get(d, "present"))) ||
	    (cells && json_object_is_type(cells, json_type_array) && json_object_array_length(cells)) ||
	    !dialect || (strcmp(dialect, "foxconn-sdx") && strcmp(dialect, "foxconn-t77")))
		return;
	r = ask("cells", id, 0);
	if (r && json_object_is_type(get(r, "cells"), json_type_array)) {
		if (!mm)	/* the engine drives the modem alone: no ModemManager object to hang them on */
			json_object_object_add(d, "modemmanager", (mm = json_object_new_object()));
		json_object_object_add(mm, "cells", json_object_get(get(r, "cells")));
		json_object_object_add(mm, "cells_source", json_object_new_string("at"));
	}
	json_object_put(r);
}

/* the network mode and bands not read yet (an AT or QModem reader): read once per modem while «Радио» is open, as LuCI */
static void ask_radio(const char *id, json_object *d)
{
	static char asked_id[200];
	const char *why = str(get(d, "radio_settings"), "why");
	json_object *r;

	if (g_tab != 1 || !why || !strstr(why, "not read yet") || !strcmp(asked_id, id) ||
	    !(get(d, "present") && json_object_get_boolean(get(d, "present"))))
		return;
	snprintf(asked_id, sizeof(asked_id), "%s", id);
	r = ask("radio", id, 0);
	if (r && get(r, "radio_settings"))
		json_object_object_add(d, "radio_settings", json_object_get(get(r, "radio_settings")));
	json_object_put(r);
}

/* one frame of the list or of one modem; false when the engine does not answer */
static bool build(const char *id, int sel, int *count, char *sel_id, size_t sel_len)
{
	frame_reset();
	if (id) {
		json_object *d = ask("device", id, 0), *ev;
		if (!d) {
			out(T("Modem %s not found, or the MedveModem service is not responding.", "Модем %s не найден, или служба MedveModem не отвечает."), id);
			return false;
		}
		ask_cells(id, d);
		ask_radio(id, d);
		ev = ask("events", id, 10);
		json_object_put(g_levels);
		g_levels = json_object_get(get(get(d, "recovery"), "levels"));
		render_detail(d, ev);
		json_object_put(d);
		json_object_put(ev);
		return true;
	}
	if (g_ex) {
		json_object *up = ask("uplink", NULL, 0);

		render_excl(up, sel);
		*count = g_ex_n;
		json_object_put(up);
		return up != NULL;
	}
	{
		json_object *st = ask("status", NULL, 0), *devs, *up;
		if (!st) {
			out("%s", T("The MedveModem service is not responding (ubus: medvemodem). Start it: /etc/init.d/medvemodemd start",
				    "Служба MedveModem не отвечает (ubus: medvemodem). Запуск: /etc/init.d/medvemodemd start"));
			*count = 0;
			return false;
		}
		devs = ask("devices", NULL, 0);
		if (devs)
			order_devices(devs);
		up = ask("uplink", NULL, 0);
		render_list(st, devs, up, sel, count);
		if (sel_id && sel >= 0 && sel < *count) {
			const char *s = str(json_object_array_get_idx(get(devs, "devices"), (size_t)sel), "device_id");
			snprintf(sel_id, sel_len, "%s", s ? s : "");
		}
		json_object_put(st);
		json_object_put(devs);
		json_object_put(up);
		return true;
	}
}

/* ---- actions: the same engine calls, confirmations and limits as LuCI ---- */

static void act(const char *method, json_object *args, const char *what, char *msg, size_t len)
{
	json_object *r = NULL;
	const char *res, *why;
	char b[48];

	if (!call(method, args, &r, 10000) || !r) {
		snprintf(msg, len, T("%s: the MedveModem service did not answer.", "%s: служба MedveModem не ответила."), what);
		json_object_put(args);
		return;
	}
	res = str(r, "result");
	why = str(r, "reason") ? str(r, "reason") : str(r, "why");
	if (why)
		why = word(REASON_W, why, why);
	if (!strcmp(method, "connect") || !strcmp(method, "priority"))
		snprintf(msg, len, "%s: %s%s%s", what, res && !strcmp(res, "done") ? T("done", "выполнено") : T("failed", "не выполнено"),
			 why && strcmp(res ? res : "", "done") ? " — " : "", why && strcmp(res ? res : "", "done") ? why : "");
	else if (!strcmp(method, "check"))
		snprintf(msg, len, "%s", res && !strcmp(res, "started") ? T("Connection check started; the result in a few seconds.",
									   "Проверка связи запущена; результат через несколько секунд.") :
			 res && !strcmp(res, "running") ? T("A connection check is already running.", "Проверка связи уже выполняется.") :
			 res && !strcmp(res, "not-connected") ? T("Not connected: nothing to check.", "Соединение не установлено: проверка невозможна.") :
			 T("Connection check: no answer.", "Проверка связи: нет ответа."));
	else if (!strcmp(method, "failover_manual") && res && !strcmp(res, "done"))
		snprintf(msg, len, T("Exclusions applied: %zu.", "Исключения применены: %zu."), json_object_array_length(get(args, "devices")));
	else if (!strcmp(method, "failover_manual"))
		snprintf(msg, len, "%s%s%s", res && !strcmp(res, "changed") ?
			 T("The list was changed elsewhere (LuCI, the bot); the draft starts again from it.",
			   "Список изменили в другом месте (LuCI, бот); черновик начат заново с него.") :
			 T("Exclusions not applied", "Исключения не применены"), why ? " — " : "", why ? why : "");
	else if (!strcmp(method, "mm_restart"))
		snprintf(msg, len, "%s%s%s%s", res && !strcmp(res, "started") ? T("ModemManager is restarting; the modems will be back in about a minute.",
									      "ModemManager перезапускается; модемы вернутся примерно через минуту.") :
			 res && !strcmp(res, "running") ? T("ModemManager is already restarting.", "ModemManager уже перезапускается.") :
			 T("ModemManager restart failed", "Перезапуск ModemManager не выполнен"),
			 res && strcmp(res, "started") && strcmp(res, "running") && why ? ": " : "",
			 res && strcmp(res, "started") && strcmp(res, "running") && why ? why : "",
			 get(r, "retry_s") ? (snprintf(b, sizeof(b), T(" (retry in %lld s)", " (через %lld с)"), (long long)json_object_get_int64(get(r, "retry_s"))), b) : "");
	else
		snprintf(msg, len, "%s: %s%s%s", what, res && !strcmp(res, "started") ? T("started", "запущено") :
			 res && !strcmp(res, "done") ? T("done", "выполнено") : res && !strcmp(res, "refused") ? T("refused", "отклонено") :
			 res && !strcmp(res, "busy") ? T("busy", "занято") : res && !strcmp(res, "unchanged") ? T("already so", "уже так") :
			 res ? res : T("no answer", "нет ответа"), why ? " — " : "", why ? why : "");
	json_object_put(r);
	json_object_put(args);
}

/* ---- the terminal ---- */

static void restore(void)
{
	if (raw_on) {
		fputs("\033[?25h\033[?1049l", stdout);
		fflush(stdout);
		tcsetattr(STDIN_FILENO, TCSANOW, &saved_tty);
		raw_on = false;
	}
}

static void on_signal(int s)
{
	if (s == SIGWINCH)
		resized = 1;
	else
		stop = 1;
}

static void term_size(int *w, int *h)
{
	struct winsize ws;

	*w = 80;
	*h = 24;
	if (!ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) && ws.ws_col > 0 && ws.ws_row > 0) {
		*w = ws.ws_col;
		*h = ws.ws_row;
	}
	/* a width set by hand wins: not every terminal reports its size (5gtop's lesson) */
	if (forced_width > 0)
		*w = forced_width;
}

enum key { K_NONE, K_UP, K_DOWN, K_OPEN, K_BACK, K_QUIT, K_REFRESH, K_DIGIT, K_TAB, K_CHAR };

/* bytes read but not yet taken: two keys can come in one read (fast typing, a paste, SSH) */
static char kbuf[32];
static size_t klen;

static enum key read_key(int timeout_ms, int *digit)
{
	struct pollfd p = { .fd = STDIN_FILENO, .events = POLLIN };
	char b[8];
	ssize_t n;
	size_t used = 1;

	if (!klen) {
		ssize_t r;
		if (poll(&p, 1, timeout_ms) <= 0)
			return K_NONE;	/* a timeout, or a signal (SIGWINCH): redraw */
		r = read(STDIN_FILENO, kbuf, sizeof(kbuf));
		if (r <= 0)
			return K_QUIT;
		klen = (size_t)r;
	}
	n = (ssize_t)(klen < sizeof(b) - 1 ? klen : sizeof(b) - 1);
	memcpy(b, kbuf, (size_t)n);
	if (b[0] == '\033') {
		/* over SSH or a serial line ESC, [ and A may come in separate reads: wait a moment for the rest
		 * (audit 2026-10-15); a lone ESC after that is Back */
		while (n < 3 && (n < 2 || b[1] == '[' || b[1] == 'O')) {
			struct pollfd q = { .fd = STDIN_FILENO, .events = POLLIN };
			ssize_t m;

			if (poll(&q, 1, 60) <= 0 || (m = read(STDIN_FILENO, b + n, sizeof(b) - 1 - (size_t)n)) <= 0)
				break;
			memcpy(kbuf + klen, b + n, (size_t)m);	/* kbuf held fewer than 3 bytes here */
			klen += (size_t)m;
			n += m;
		}
		if (n >= 3 && (b[1] == '[' || b[1] == 'O')) {
			/* «ESC [ 3 ~», «ESC [ 15 ~»: parameters up to the final byte go too, or «~» reads as a key (review 2026-11-15) */
			size_t end = 2;

			if (b[1] == '[')
				while (end < (size_t)n && (unsigned char)b[end] >= 0x20 && (unsigned char)b[end] <= 0x3f)
					end++;
			if (end >= (size_t)n)
				end = (size_t)n - 1;
			memmove(kbuf, kbuf + end + 1, klen - end - 1);
			klen -= end + 1;
			if (end > 2)
				return K_NONE;	/* Delete, PageUp, a function key: none of ours */
			*digit = -1;	/* Shift-Tab: the tab before */
			return b[2] == 'A' ? K_UP : b[2] == 'B' ? K_DOWN : b[2] == 'C' ? K_OPEN : b[2] == 'D' ? K_BACK : b[2] == 'Z' ? K_TAB : K_NONE;
		}
		used = n >= 2 && (b[1] == '[' || b[1] == 'O') ? (size_t)n : 1;	/* a lone ESC is Back; a key after it stays */
		memmove(kbuf, kbuf + used, klen - used);
		klen -= used;
		return K_BACK;
	}
	memmove(kbuf, kbuf + 1, klen - 1);
	klen -= 1;
	switch (b[0]) {
	case 'q': case 'Q': case 3: return K_QUIT;
	case 'k': return K_UP;
	case 'j': return K_DOWN;
	case '\r': case '\n': case 'l': return K_OPEN;
	case 'h': case 'b': case 127: return K_BACK;
	case 'r': case 'R': return K_REFRESH;
	case '\t': *digit = 1; return K_TAB;
	case 'c': case 'C': case 'M': case 'y': case 'Y':
		*digit = b[0];
		return K_CHAR;
	default:
		if (b[0] >= '1' && b[0] <= '9') {
			*digit = b[0] - '1';
			return K_DIGIT;
		}
	}
	/* any other key: nothing by itself, but it cancels a confirmation */
	*digit = (unsigned char)b[0];
	return K_CHAR;
}

static void draw(const char *hint, int scroll)
{
	int w, h;

	term_size(&w, &h);
	fputs("\033[H", stdout);
	for (int i = scroll, row = 0; i < nlines && row < h - 1; i++, row++) {
		put_line(stdout, lines[i], w);
		fputs("\033[K\r\n", stdout);
	}
	fputs("\033[J", stdout);
	fprintf(stdout, "\033[%d;1H%s", h, C_DIM);
	put_line(stdout, hint, w);
	fputs("\033[K", stdout);
	fflush(stdout);
}

int mm_top_main(int argc, char **argv, mm_top_call fn)
{
	const char *id = NULL;
	bool once = false, show_ids = false, no_color = getenv("NO_COLOR") && *getenv("NO_COLOR");
	const char *lang = getenv("MEDVEMODEM_LANG");
	const char *term = getenv("TERM");
	char open_id[200] = "", sel_id[200] = "", msg[300] = "", hint[400], pend_step[32] = "";
	int sel = 0, count = 0, scroll = 0, pend = 0;	/* pend: 1 a recovery step, 2 the ModemManager restart, 3 a deactivation, 4 a freeze, awaiting y */
	int ex_sel = 0;	/* the line chosen on the exclusions screen */
	char pend_id[200] = "", pend_name[80] = "";
	struct termios t;

	call = fn;
	for (int i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "--once"))
			once = true;
		else if (!strcmp(argv[i], "--show-identifiers"))
			show_ids = true;
		else if (!strcmp(argv[i], "--no-color"))
			no_color = true;
		else if (!strcmp(argv[i], "--ascii"))
			ascii = true;
		else if (!strcmp(argv[i], "--width") && i + 1 < argc && atoi(argv[i + 1]) >= 40)
			forced_width = atoi(argv[++i]);
		else if (!strcmp(argv[i], "--lang") && i + 1 < argc && (!strcmp(argv[i + 1], "en") || !strcmp(argv[i + 1], "ru")))
			lang = argv[++i];
		else if (argv[i][0] != '-' && !id)
			id = argv[i];
		else {
			fprintf(stderr, "Usage: medvemodem top [<device-id>] [--once] [--show-identifiers] [--no-color] [--ascii] [--width N] [--lang en|ru]\n");
			return 2;
		}
	}
	/* English unless Russian is asked for; the flag wins over MEDVEMODEM_LANG ("ru", "ru_RU.UTF-8" and the like) */
	ru = lang && !strncmp(lang, "ru", 2);
	/* a plain snapshot: asked for, not a terminal, a dumb one, or one whose mode cannot be read */
	if (once || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO) || (term && !strcmp(term, "dumb")) ||
	    tcgetattr(STDIN_FILENO, &saved_tty)) {
		bool ok;
		color = !no_color && !once && isatty(STDOUT_FILENO) && !(term && !strcmp(term, "dumb"));
		redact = once && !show_ids;
		ok = build(id, -1, &count, NULL, 0);
		for (int i = 0; i < nlines; i++) {
			put_line(stdout, lines[i], 0);
			fputc('\n', stdout);
		}
		frame_reset();
		return ok ? 0 : 3;
	}

	color = !no_color;
	if (id) {
		snprintf(open_id, sizeof(open_id), "%s", id);
		g_tab = 0;
	}
	/* the interactive screen is the operator's own: identifiers shown (redact stays false) */
	t = saved_tty;
	t.c_lflag &= ~(tcflag_t)(ICANON | ECHO);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &t)) {
		fprintf(stderr, "medvemodem top: cannot set the terminal mode; try --once\n");
		return 2;
	}
	raw_on = true;
	atexit(restore);
	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	signal(SIGHUP, on_signal);
	{
		/* no SA_RESTART: a resize interrupts the wait and the frame is redrawn at once */
		struct sigaction sa = { .sa_handler = on_signal };
		sigemptyset(&sa.sa_mask);
		sigaction(SIGWINCH, &sa, NULL);
	}
	fputs("\033[?1049h\033[?25l\033[2J", stdout);

	while (!stop) {
		enum key k;
		int digit = 0;

		if (resized) {
			/* a narrower window keeps the old frame's tails: clear once */
			resized = 0;
			fputs("\033[2J", stdout);
		}
		build(*open_id ? open_id : NULL, g_ex ? ex_sel : sel, &count, sel_id, sizeof(sel_id));
		if (g_ex && count && ex_sel >= count)
			ex_sel = count - 1;
		if (!g_ex && !*open_id && count && sel >= count)
			sel = count - 1;
		if (scroll > nlines - 1)
			scroll = nlines > 0 ? nlines - 1 : 0;
		if (pend)
			/* the keys first: on an 80-column console the rest may be cut */
			snprintf(hint, sizeof(hint), T("%sy — confirm, any other key — cancel: \"%s%s\", %s", "%sy — выполнить, другая клавиша — отмена: «%s%s», %s"),
				 C_OFF, pend == 4 ? T("Freeze ", "Заморозить ") : pend == 3 ? T("Deactivate ", "Деактивировать ") :
				 pend == 2 ? T("Restart ModemManager", "Перезапустить ModemManager") :
				 word(STEP_W, pend_step, pend_step), pend >= 3 ? pend_name : "",
				 pend == 4 ? T("nobody touches it and its link is down until Unfreeze (z)",
					       "его никто не трогает, связи через него нет до разморозки (z)") :
				 pend == 3 ? T("the link through it stays down until Activate or a router reboot",
					       "связь через него пропадёт до «Активировать» или перезагрузки роутера") :
				 pend == 2 ? T("every modem it holds offline ~1 min", "все модемы под ним без связи ~1 мин") :
				 T("the link through this modem may drop", "связь через этот модем может прерваться"));
		else if (*msg)
			snprintf(hint, sizeof(hint), "%s%s", C_OFF, msg);
		else
			/* L last: on a narrow console it is the one cut, not q; the English lines fit 80 columns */
			snprintf(hint, sizeof(hint), "%s", g_ex ?
				T("↑↓ select · Space tick · P apply · Esc back · q quit · L русский",
				  "↑↓ выбор · Пробел отметить · P применить · Esc назад · q выход · L English") :
				*open_id ? (g_tab == 2 ?
				T("Tab tab · 1–8 step · M restart MM · c check · Esc back · q quit · L русский",
				  "Tab вкладка · 1–8 шаг восстановления · M перезапуск ModemManager · c проверить связь · Esc назад · q выход · L English") :
				T("Tab tab · c check · d on/off · ↑↓ scroll · Esc back · z freeze · q quit · L русский",
				  "Tab вкладка · c проверить связь · d (де)активировать · ↑↓ прокрутка · Esc назад · z заморозка · q выход · L English")) :
				g_order_dirty ? T("[ ] move · P apply order · Esc cancel · ↑↓ select · q quit · L русский",
						  "[ ] переставить · P применить порядок · Esc отменить · ↑↓ выбор · q выход · L English") :
				T("↑↓ Enter · d on/off · [ ] order · x exclusions · z freeze · q quit · L русский",
				  "↑↓ Enter открыть · d (де)активировать · [ ] приоритет · x исключения · z заморозка · q выход · L English"));
		draw(hint, *open_id ? scroll : 0);
		/* wait for a key or the next refresh; a key redraws at once */
		k = read_key(TOP_REFRESH_MS, &digit);
		if (k != K_NONE)
			msg[0] = '\0';
		if (pend && k != K_NONE) {
			/* only y confirms; any other key cancels */
			if (k == K_CHAR && (digit == 'y' || digit == 'Y')) {
				json_object *a = json_object_new_object();
				if (pend == 4) {
					char what[160];
					json_object_object_add(a, "id", json_object_new_string(pend_id));
					json_object_object_add(a, "on", json_object_new_boolean(1));
					json_object_object_add(a, "who", json_object_new_string("TUI"));
					snprintf(what, sizeof(what), T("Freeze \"%s\"", "Заморозка «%s»"), pend_name);
					act("freeze", a, what, msg, sizeof(msg));
				} else if (pend == 3) {
					char what[160];
					json_object_object_add(a, "id", json_object_new_string(pend_id));
					json_object_object_add(a, "up", json_object_new_boolean(0));
					json_object_object_add(a, "who", json_object_new_string("TUI"));
					snprintf(what, sizeof(what), T("Deactivation \"%s\"", "Деактивация «%s»"), pend_name);
					act("connect", a, what, msg, sizeof(msg));
				} else if (pend == 2) {
					json_object_object_add(a, "confirm", json_object_new_boolean(1));
					json_object_object_add(a, "reason", json_object_new_string("TUI"));
					act("mm_restart", a, T("ModemManager restart", "Перезапуск ModemManager"), msg, sizeof(msg));
				} else {
					json_object_object_add(a, "id", json_object_new_string(open_id));
					json_object_object_add(a, "level", json_object_new_string(pend_step));
					json_object_object_add(a, "wait", json_object_new_boolean(0));
					json_object_object_add(a, "source", json_object_new_string("tui"));
					act("recover", a, word(STEP_W, pend_step, pend_step), msg, sizeof(msg));
				}
			} else {
				snprintf(msg, sizeof(msg), "%s", T("Cancelled.", "Отменено."));
			}
			pend = 0;
			continue;
		}
		if (g_ex) {
			if (k == K_QUIT) {
				stop = 1;
			} else if (k == K_UP && ex_sel > 0) {
				ex_sel--;
			} else if (k == K_DOWN && ex_sel + 1 < g_ex_n) {
				ex_sel++;
			} else if ((k == K_OPEN || (k == K_CHAR && digit == ' ')) && ex_sel < g_ex_n) {
				ex_toggle(g_ex_dev[ex_sel]);
			} else if (k == K_DIGIT && digit < g_ex_n) {
				ex_sel = digit;
				ex_toggle(g_ex_dev[ex_sel]);
			} else if (k == K_CHAR && digit == 'L') {
				ru = !ru;
			} else if (k == K_CHAR && digit == 'P') {
				json_object *a = json_object_new_object(), *devs = json_object_new_array(), *base = json_object_new_array();
				for (int i = 0; i < g_ex_pick_n; i++)
					json_object_array_add(devs, json_object_new_string(g_ex_pick[i]));
				for (int i = 0; i < g_ex_base_n; i++)
					json_object_array_add(base, json_object_new_string(g_ex_base[i]));
				json_object_object_add(a, "devices", devs);
				json_object_object_add(a, "expect", base);
				if (!ex_changed()) {
					json_object_put(a);
					snprintf(msg, sizeof(msg), "%s", T("Nothing changed.", "Изменений нет."));
				} else {
					act("failover_manual", a, T("Exclusions", "Исключения"), msg, sizeof(msg));
					g_ex_loaded = false;	/* the saved list read again: applied, or changed elsewhere */
				}
			} else if (k == K_BACK) {
				if (ex_changed())
					snprintf(msg, sizeof(msg), "%s", T("Exclusions unchanged.", "Исключения не изменены."));
				g_ex = false;
			}
			continue;
		}
		switch (k) {
		case K_TAB:
			if (*open_id) {
				g_tab = (g_tab + (digit < 0 ? 2 : 1)) % 3;
				scroll = 0;
			}
			break;
		case K_CHAR:
			if (digit == 'L') {
				/* the language, on any screen: the next frame (drawn at once) speaks the other one */
				ru = !ru;
			} else if (*open_id && (digit == 'c' || digit == 'C')) {
				json_object *a = json_object_new_object();
				json_object_object_add(a, "id", json_object_new_string(open_id));
				act("check", a, T("Connection check", "Проверка связи"), msg, sizeof(msg));
			} else if (*open_id && digit == 'M') {
				pend = 2;
			} else if (!*open_id && digit == 'x' && !g_order_dirty) {
				g_ex = true;
				g_ex_loaded = false;
				ex_sel = 0;
			} else if (digit == 'z' && (*open_id || (count && *sel_id))) {
				/* «Заморозить / Разморозить», as the card's menu and the bot: freezing asks first, thawing does not */
				const char *target = *open_id ? open_id : sel_id;
				json_object *d = ask("device", target, 0);
				char name[64];

				name_of(d, name, sizeof(name));
				if (!d) {
					snprintf(msg, sizeof(msg), "%s", T("The MedveModem service did not answer.", "Служба MedveModem не ответила."));
				} else if (get(d, "frozen")) {
					json_object *a = json_object_new_object();
					char what[160];
					json_object_object_add(a, "id", json_object_new_string(target));
					json_object_object_add(a, "on", json_object_new_boolean(0));
					json_object_object_add(a, "who", json_object_new_string("TUI"));
					snprintf(what, sizeof(what), T("Unfreeze \"%s\"", "Разморозка «%s»"), name);
					act("freeze", a, what, msg, sizeof(msg));
				} else {
					snprintf(pend_id, sizeof(pend_id), "%s", target);
					snprintf(pend_name, sizeof(pend_name), "%s", name);
					pend = 4;
				}
				json_object_put(d);
			} else if (digit == 'd' && (*open_id || (count && *sel_id))) {
				/* «Деактивировать / Активировать», as the dashboard's card: down asks first, up does not */
				const char *target = *open_id ? open_id : sel_id;
				json_object *d = ask("device", target, 0);
				char name[64];

				name_of(d, name, sizeof(name));
				if (!d)
					snprintf(msg, sizeof(msg), "%s", T("The MedveModem service did not answer.", "Служба MedveModem не ответила."));
				else if (!is_present(d))
					snprintf(msg, sizeof(msg), T("\"%s\" is unplugged from USB.", "«%s» отключён от USB."), name);
				else if (!has_iface(d))
					snprintf(msg, sizeof(msg), T("\"%s\": the modem has no network interface yet.", "«%s»: у модема ещё нет сетевого интерфейса."), name);
				else if (is_connected(d)) {
					snprintf(pend_id, sizeof(pend_id), "%s", target);
					snprintf(pend_name, sizeof(pend_name), "%s", name);
					pend = 3;
				} else {
					json_object *a = json_object_new_object();
					char what[160];
					json_object_object_add(a, "id", json_object_new_string(target));
					json_object_object_add(a, "up", json_object_new_boolean(1));
					json_object_object_add(a, "who", json_object_new_string("TUI"));
					snprintf(what, sizeof(what), T("Activation \"%s\"", "Активация «%s»"), name);
					act("connect", a, what, msg, sizeof(msg));
				}
				json_object_put(d);
			} else if (!*open_id && (digit == '[' || digit == ']')) {
				sel = order_move(sel, digit == '[' ? -1 : 1);
			} else if (!*open_id && digit == 'P' && g_order_dirty) {
				json_object *a = json_object_new_object(), *order = json_object_new_array();
				for (int i = 0; i < g_order_n; i++)
					if (g_order_iface[i])
						json_object_array_add(order, json_object_new_string(g_order[i]));
				json_object_object_add(a, "order", order);
				act("priority", a, T("Modem order", "Порядок модемов"), msg, sizeof(msg));
				if (!strstr(msg, T(": failed", "не выполнено")))
					g_order_dirty = false;
				else
					snprintf(msg + strlen(msg), sizeof(msg) - strlen(msg), "%s", T(" (Esc to cancel)", " (Esc — отменить)"));
			}
			break;
		case K_QUIT:
			stop = 1;
			break;
		case K_UP:
			if (*open_id) {
				if (scroll > 0)
					scroll--;
			} else if (sel > 0) {
				sel--;
			}
			break;
		case K_DOWN:
			if (*open_id)
				scroll++;
			else if (sel + 1 < count)
				sel++;
			break;
		case K_OPEN:
			if (!*open_id && count && *sel_id) {
				snprintf(open_id, sizeof(open_id), "%s", sel_id);
				scroll = 0;
				g_tab = 0;
			}
			break;
		case K_DIGIT:
			if (*open_id && g_tab == 2) {
				/* a step of the ladder: confirmed first, refused by the engine when not allowed */
				json_object *l = g_levels && json_object_is_type(g_levels, json_type_array) && digit < (int)json_object_array_length(g_levels) ?
						 json_object_array_get_idx(g_levels, (size_t)digit) : NULL;
				if (!l || !str(l, "name")) {
					snprintf(msg, sizeof(msg), T("No step number %d.", "Нет шага с номером %d."), digit + 1);
				} else if (!(get(l, "supported") && json_object_get_boolean(get(l, "supported")))) {
					snprintf(msg, sizeof(msg), T("\"%s\" is not available for this modem.", "«%s» недоступен для этого модема."), word(STEP_W, str(l, "name"), ""));
				} else if (!strcmp(str(l, "name"), "observe")) {
					json_object *a = json_object_new_object();	/* no interruption: no confirmation, as in LuCI */
					json_object_object_add(a, "id", json_object_new_string(open_id));
					json_object_object_add(a, "level", json_object_new_string("observe"));
					json_object_object_add(a, "wait", json_object_new_boolean(0));
					json_object_object_add(a, "source", json_object_new_string("tui"));
					act("recover", a, word(STEP_W, "observe", ""), msg, sizeof(msg));
				} else {
					snprintf(pend_step, sizeof(pend_step), "%s", str(l, "name"));
					pend = 1;
				}
			} else if (!*open_id && digit < count) {
				sel = digit;
				build(NULL, sel, &count, sel_id, sizeof(sel_id));
				if (*sel_id) {
					snprintf(open_id, sizeof(open_id), "%s", sel_id);
					scroll = 0;
					g_tab = 0;
				}
			}
			break;
		case K_BACK:
			if (!*open_id && g_order_dirty) {
				g_order_dirty = false;
				snprintf(msg, sizeof(msg), "%s", T("Order unchanged.", "Порядок не изменён."));
			} else if (*open_id && !id) {
				open_id[0] = '\0';
				scroll = 0;
			} else if (*open_id && id) {
				stop = 1;
			}
			break;
		default:
			break;
		}
	}
	frame_reset();
	json_object_put(g_levels);
	g_levels = NULL;
	restore();
	return 0;
}
