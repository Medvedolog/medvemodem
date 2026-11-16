/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_TRAFHIST_H
#define MEDVEMODEM_TRAFHIST_H

#include <json-c/json.h>

/*
 * A modem's traffic and peak speed history (spec §42, the owner 2026-11-02).
 * Pure: the daemon hands in the 5 s rates it already computes from the network device's counters (bytes per second,
 * never a modem poll); this keeps them in RAM like the signal history: every sample of the last 5 minutes, one-minute
 * buckets of the last hour, five-minute buckets of the last day. A bucket keeps the average and the PEAK of each
 * direction: averaging alone hides the bursts the owner asks for.
 *
 * hist: the device's history object, kept by the caller (an empty object to start). rx, tx: bytes per second.
 * t: wall-clock seconds.
 */
void mm_trafhist_add(json_object *hist, long long rx, long long tx, long long t);

/*
 * The served history for a range "5m", "1h" or "24h" (anything else is "1h"), up to now: {"range", "step_s",
 * "points": [{"t", "rx_bps", "tx_bps", "rx_peak_bps", "tx_peak_bps", "rx_bytes", "tx_bytes"}] (bits per second; bytes estimated), "peak": {"rx_bps",
 * "tx_bps", "rx_at", "tx_at"} of the range, "bytes": {"rx", "tx"} of the range (estimated from the averages)}.
 * New object.
 */
json_object *mm_trafhist_view(json_object *hist, const char *range, long long now);

/* the highest 5 s rates so far into peak {"rx_bps", "tx_bps", "rx_at", "tx_at"} (bits per second): true when one grew */
int mm_trafpeak_add(json_object *peak, long long rx, long long tx, long long t);

#endif
