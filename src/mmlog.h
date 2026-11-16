/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_MMLOG_H
#define MEDVEMODEM_MMLOG_H

#include <stddef.h>

/*
 * ModemManager's log lines that say a port of the modem is not its own to drive (the owner, 2026-10-27, after
 * a field study of recovery triggers): a hint for «Диагностика» and the report, never a recovery trigger. Pure.
 *
 * mm_mmlog_kind: "port-lost" («no longer controllable»), "port-busy" («could not / couldn't grab port»),
 * "mbim-not-opened" (MBIM «NotOpened»), "reprobe" («connection broken, reprobing»), "no-at" («Failed to find
 * primary AT port»), "unhandled-port" («unhandled port type»), "sim-changed" («SIM identifier has changed»), or NULL.
 * mm_mmlog_port: the first port name in the line (cdc-wdmN, ttyUSBN, ttyACMN, wwanN, wwanNmbimN, wwanNqmiN) into
 * buf; false when none. mm_mmlog_modem: N of «[modemN]», or -1. Nothing else of the line is kept (it may hold
 * identifiers).
 */
const char *mm_mmlog_kind(const char *line);
int mm_mmlog_port(const char *line, char *buf, size_t size);
int mm_mmlog_modem(const char *line);

#endif
