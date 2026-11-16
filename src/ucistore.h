#ifndef MEDVEMODEM_UCISTORE_H
#define MEDVEMODEM_UCISTORE_H

#include <stddef.h>
#include <json-c/json.h>

/*
 * libuci adapter for the configuration engine (spec 17). Reads and writes
 * committed configuration only: the shared staging directory (/tmp/.uci) is
 * never read or flushed, so changes staged by LuCI or another tool are
 * neither committed by us nor lost.
 */

/* {"<package>": image} (cfgplan.h); a missing package reads as {}. New object, or NULL with err. */
json_object *mm_uci_read(const char *confdir, const char *const *packages, char *err, size_t err_size);

/*
 * Apply checked ops (mm_cfg_check "apply") and commit each touched package.
 * savedir is a private staging directory. 0 on success, -1 with err.
 */
int mm_uci_apply(const char *confdir, const char *savedir, json_object *ops, char *err, size_t err_size);

#endif
