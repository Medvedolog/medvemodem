/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef MEDVEMODEM_USBTREE_H
#define MEDVEMODEM_USBTREE_H

#include <stdbool.h>
#include <json-c/json.h>

/*
 * «Порты и устройства» (docs/PORTS-AND-DEVICES.md): the USB tree as sysfs has it. Pure reading, never a write to
 * sysfs. A node per USB device (root hubs included), keyed by its sysfs name ("usb1", "1-1", "1-1.2"):
 * {path, parent, bus, devnum, speed_mbps, version, vidpid, manufacturer, product, has_serial, max_power_ma,
 *  config, runtime, kind ("controller"|"hub"|"device"), class_kind, controller {driver}, ports [{n, child,
 *  over_current, peer}], interfaces [{n, class, subclass, protocol, driver, nodes}]}.
 * An attribute the kernel does not give is left out («нет данных», never a zero). No serial number in it.
 */
json_object *mm_usbtree_scan(void);

/* The USB device a hub port names: "usb1-port2" -> "1-2", "1-1-port3" -> "1-1.3"; false when it is not one. */
bool mm_usbtree_port_child(const char *port, char *out, size_t size);

/*
 * One kernel log line (a /dev/kmsg record's text after ';', or a dmesg line's): what it says about a USB device.
 * Returns the kind ("reset", "err71", "err110", "err32", "enum", "overcurrent", "hc", "watchdog") and puts the
 * device's sysfs name (or, for "watchdog", the network interface; for "hc", the controller's name) in who;
 * NULL for any other line. The text itself is never kept (it can carry a serial).
 */
const char *mm_usbtree_kmsg(const char *line, char *who, size_t size);

/* The protocol a data driver speaks, for the card: "qmi_wwan" -> "QMI"; NULL when the driver says none. */
const char *mm_usbtree_protocol(const char *driver);

#endif
