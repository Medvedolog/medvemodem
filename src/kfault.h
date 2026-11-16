/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_KFAULT_H
#define MEDVEMODEM_KFAULT_H

#include <stdbool.h>
#include <stddef.h>

/*
 * The kernel's own words for a router in trouble (the owner, 2026-10-31, after the R3S hung: «HC died и failed to set
 * voltage — да обязательно и не только для r3s»). Pure: a line in, a kind out; nothing of the line is kept beyond a
 * short name of who said it (a controller, a regulator, a process name).
 *
 * Kinds, the first nine critical (the router is not well until it is restarted or its power is seen to):
 *   "hc-died"      a USB host controller died («HC died», «not responding, assume dead»): every modem on it is gone
 *   "voltage"      a regulator could not set a voltage («failed to set voltage»): the PMIC or its i2c, usually power
 *   "undervoltage" the board says its supply sags («Under-voltage detected», «Undervoltage detected»)
 *   "stall"        an RCU stall («self-detected stall», «detected stalls»): a CPU stuck
 *   "lockup"       a soft or hard lockup
 *   "oops"         a kernel oops («Internal error: Oops», «Unable to handle kernel», «BUG: unable to handle»)
 *   "panic"        «Kernel panic»
 *   "overheat"     «critical temperature»
 *   "hung-task"    a task blocked for more than N seconds
 *   then warnings: "oom" (the OOM killer), "fs-error" (EXT4/SQUASHFS/UBIFS errors, block I/O errors).
 * mm_kfault_split: the text of one kernel log line after its prefix («<3>[  950.000000] », «3,101,950000000,-;» of
 * /dev/kmsg, or none) and its time since boot in seconds, or -1 when the line has none.
 */
const char *mm_kfault_kind(const char *text, char *who, size_t size);
double mm_kfault_split(const char *line, const char **text);
bool mm_kfault_critical(const char *kind);

#endif
