/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <string.h>

#include "trafhist.h"

/* Traffic and peak speed history (spec §42): see trafhist.h. */

#define RAW_S 300		/* every sample of the last 5 minutes */
#define MIN_S 3600		/* one-minute buckets of the last hour */
#define DAY_S 86400		/* five-minute buckets of the last day */

static long long num(json_object *o, const char *k)
{
	return json_object_get_int64(json_object_object_get(o, k));
}

static json_object *arr(json_object *hist, const char *k)
{
	json_object *a = json_object_object_get(hist, k);

	if (!json_object_is_type(a, json_type_array)) {
		a = json_object_new_array();
		json_object_object_add(hist, k, a);
	}
	return a;
}

/* entries older than the window go from the front */
static void trim(json_object *a, long long from)
{
	size_t n = 0;

	while (n < json_object_array_length(a) && num(json_object_array_get_idx(a, n), "t") < from)
		n++;
	if (n)
		json_object_array_del_idx(a, 0, n);
}

/* a bucket of step s: sums for the average and the peaks */
static void bucket_add(json_object *a, long long step, long long rx, long long tx, long long t)
{
	long long start = t - t % step;
	size_t n = json_object_array_length(a);
	json_object *b = n ? json_object_array_get_idx(a, n - 1) : NULL;

	if (!b || num(b, "t") != start) {
		b = json_object_new_object();
		json_object_object_add(b, "t", json_object_new_int64(start));
		json_object_array_add(a, b);
	}
	json_object_object_add(b, "n", json_object_new_int64(num(b, "n") + 1));
	json_object_object_add(b, "rx", json_object_new_int64(num(b, "rx") + rx));
	json_object_object_add(b, "tx", json_object_new_int64(num(b, "tx") + tx));
	if (rx > num(b, "rxp"))
		json_object_object_add(b, "rxp", json_object_new_int64(rx));
	if (tx > num(b, "txp"))
		json_object_object_add(b, "txp", json_object_new_int64(tx));
}

void mm_trafhist_add(json_object *hist, long long rx, long long tx, long long t)
{
	json_object *raw, *s;
	size_t n;

	if (!hist || rx < 0 || tx < 0 || t <= 0)
		return;
	raw = arr(hist, "raw");
	n = json_object_array_length(raw);
	/* a read in the same second replaces the one before (as the signal history) */
	if (n && num(json_object_array_get_idx(raw, n - 1), "t") == t)
		json_object_array_del_idx(raw, n - 1, 1);
	s = json_object_new_object();
	json_object_object_add(s, "t", json_object_new_int64(t));
	json_object_object_add(s, "rx", json_object_new_int64(rx));
	json_object_object_add(s, "tx", json_object_new_int64(tx));
	json_object_array_add(raw, s);
	bucket_add(arr(hist, "min"), 60, rx, tx, t);
	bucket_add(arr(hist, "five"), 300, rx, tx, t);
	trim(raw, t - RAW_S);
	trim(arr(hist, "min"), t - MIN_S);
	trim(arr(hist, "five"), t - DAY_S);
}

int mm_trafpeak_add(json_object *peak, long long rx, long long tx, long long t)
{
	int grew = 0;

	if (!peak || rx < 0 || tx < 0)
		return 0;
	if (rx * 8 > num(peak, "rx_bps")) {
		json_object_object_add(peak, "rx_bps", json_object_new_int64(rx * 8));
		json_object_object_add(peak, "rx_at", json_object_new_int64(t));
		grew = 1;
	}
	if (tx * 8 > num(peak, "tx_bps")) {
		json_object_object_add(peak, "tx_bps", json_object_new_int64(tx * 8));
		json_object_object_add(peak, "tx_at", json_object_new_int64(t));
		grew = 1;
	}
	return grew;
}

json_object *mm_trafhist_view(json_object *hist, const char *range, long long now)
{
	json_object *o = json_object_new_object(), *pts = json_object_new_array(), *src, *peak = json_object_new_object();
	const char *r = range && (!strcmp(range, "5m") || !strcmp(range, "24h")) ? range : "1h";
	long long span = !strcmp(r, "5m") ? RAW_S : !strcmp(r, "24h") ? DAY_S : MIN_S, step = !strcmp(r, "5m") ? 5 : !strcmp(r, "24h") ? 300 : 60;
	long long brx = 0, btx = 0, prx = -1, ptx = -1, prx_at = 0, ptx_at = 0;
	bool raw = !strcmp(r, "5m");
	size_t i;

	src = hist ? json_object_object_get(hist, raw ? "raw" : !strcmp(r, "24h") ? "five" : "min") : NULL;
	for (i = 0; json_object_is_type(src, json_type_array) && i < json_object_array_length(src); i++) {
		json_object *e = json_object_array_get_idx(src, i), *p;
		long long t = num(e, "t"), n = raw ? 1 : num(e, "n"), rx, tx, rxp, txp;

		if (t < now - span || n <= 0)
			continue;
		rx = raw ? num(e, "rx") : num(e, "rx") / n;
		tx = raw ? num(e, "tx") : num(e, "tx") / n;
		rxp = raw ? rx : num(e, "rxp");
		txp = raw ? tx : num(e, "txp");
		p = json_object_new_object();
		json_object_object_add(p, "t", json_object_new_int64(t));
		json_object_object_add(p, "rx_bps", json_object_new_int64(rx * 8));
		json_object_object_add(p, "tx_bps", json_object_new_int64(tx * 8));
		json_object_object_add(p, "rx_peak_bps", json_object_new_int64(rxp * 8));
		json_object_object_add(p, "tx_peak_bps", json_object_new_int64(txp * 8));
		json_object_object_add(p, "rx_bytes", json_object_new_int64(raw ? rx * 5 : num(e, "rx") * 5));
		json_object_object_add(p, "tx_bytes", json_object_new_int64(raw ? tx * 5 : num(e, "tx") * 5));
		json_object_array_add(pts, p);
		/* bytes of the range: each sample stands for 5 s (raw), a bucket for its samples' 5 s each */
		brx += raw ? rx * 5 : num(e, "rx") * 5;
		btx += raw ? tx * 5 : num(e, "tx") * 5;
		if (rxp > prx) { prx = rxp; prx_at = t; }
		if (txp > ptx) { ptx = txp; ptx_at = t; }
	}
	json_object_object_add(o, "range", json_object_new_string(r));
	json_object_object_add(o, "step_s", json_object_new_int64(step));
	json_object_object_add(o, "points", pts);
	if (prx >= 0) {
		json_object_object_add(peak, "rx_bps", json_object_new_int64(prx * 8));
		json_object_object_add(peak, "tx_bps", json_object_new_int64(ptx * 8));
		json_object_object_add(peak, "rx_at", json_object_new_int64(prx_at));
		json_object_object_add(peak, "tx_at", json_object_new_int64(ptx_at));
	}
	json_object_object_add(o, "peak", peak);
	{
		json_object *b = json_object_new_object();

		json_object_object_add(b, "rx", json_object_new_int64(brx));
		json_object_object_add(b, "tx", json_object_new_int64(btx));
		json_object_object_add(o, "bytes", b);
	}
	return o;
}
