#ifndef MEDVEMODEM_SYSFS_H
#define MEDVEMODEM_SYSFS_H

#include <stdbool.h>
#include <stddef.h>
#include <json-c/json.h>

bool mm_read_line(const char *path, char *buf, size_t len);
bool mm_is_usb_device_dir(const char *name);
const char *mm_base_name(const char *path);
/* Stable port key: host controller path + port chain, without the bus number. */
bool mm_usb_port_key(const char *real, const char *name,
		     char *controller, size_t clen, char *chain, size_t chlen);
bool mm_owned_by_usb_device(const char *link_path, const char *device_real);
/* Nodes of class_root owned by the USB device; iface_map (optional) gets
 * node -> USB interface ("1-1:1.2") for each node found. */
void mm_append_owned_nodes(json_object *arr, const char *class_root,
			   const char *device_real, const char *prefix,
			   json_object *iface_map);
json_object *mm_scan_interfaces(const char *usb_name);

#endif
