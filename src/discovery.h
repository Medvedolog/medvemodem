#ifndef MEDVEMODEM_DISCOVERY_H
#define MEDVEMODEM_DISCOVERY_H

#include <json-c/json.h>

/* One read-only pass over sysfs: every modem-like USB device as a JSON array. */
json_object *mm_discover_all(void);
/* the USB devices ModemManager holds a modem on ({"6-1": true}): such a device is a modem whatever its drivers say */
void mm_discovery_held(json_object *paths);
/* the operator's word by VID:PID ({"19d2:1218": true}): a modem whatever the rules say, or never one */
void mm_discovery_overrides(json_object *modem, json_object *not_modem);
/* the devices the last pass dropped that look like a modem all the same: [{usb, vidpid, manufacturer, product, why}] */
json_object *mm_discovery_suspects(void);
/* where the USB serials once seen on two devices at once are kept (the daemon's state dir; NULL: memory only) */
void mm_discovery_serial_dups_file(const char *path);

#endif
