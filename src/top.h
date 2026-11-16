#ifndef MM_TOP_H
#define MM_TOP_H

#include <json-c/json.h>

/* one call to the engine over ubus: 1 on success with *out set (the caller frees it) */
typedef int (*mm_top_call)(const char *method, json_object *args, json_object **out, int timeout_ms);

/* `medvemodem top [<device-id>] [--once] [--no-color] [--lang en|ru] ...` (spec 25.8): argv[1] is "top" */
int mm_top_main(int argc, char **argv, mm_top_call call);

#endif
