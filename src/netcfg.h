#ifndef MEDVEMODEM_NETCFG_H
#define MEDVEMODEM_NETCFG_H

#include <stddef.h>
#include <stdbool.h>
#include <json-c/json.h>

/*
 * Desired network configuration of one modem (spec 17, 19). Pure.
 *
 * rec:     the served registry record (owner, ports, modemmanager view);
 * current: {"network", "firewall", "medvemodem"} package images;
 * ledgers: {"<device_id>": ledger} of every managed device.
 *
 * Operator decisions come from /etc/config/medvemodem:
 *   config device
 *       option id 'usb-serial:...'   (device_id)
 *       option managed '0'           (never configure this modem)
 *       option interface 'lte'       (logical interface name, default mm<N>)
 *       option zone 'wan'            (firewall zone, '' for none)
 *       option apn / iptype / auth / username / password / metric
 *       option pin '1234'            (the SIM PIN kept on the router: netifd's
 *                                     option pincode of proto modemmanager/qmi/mbim)
 *       option backend 'auto'        (auto | modemmanager | native | qmodem)
 *
 * env:     {"modemmanager": backend state, "protos": [netifd protocol handlers present],
 *           "qmodem": QModem installed}.
 *
 * Owner policy (spec 12): ModemManager when it owns the modem; native netifd
 * QMI/MBIM only when ModemManager is not running at all (disabled or
 * unavailable), never beside it; DHCP on the netdev of a router-mode modem.
 *
 * Backends are chosen per modem (MULTI-BACKEND-REFERENCE-MODEL): an explicit
 * `native` or `qmodem` keeps ModemManager off that modem ("inhibit_mm"), so
 * one router can run a modem under MM and another under QModem. In `auto`, a
 * modem QModem already manages stays QModem's.
 *
 * Returns {"action": "configure" | "none", "reason", "interface"?, "proto"?,
 *          "owner"?, "backend"?, "inhibit_mm"?, "user_interface"?,
 *          "qmodem_section"?, "desired"?}. New object.
 */
json_object *mm_netcfg_desired(json_object *rec, json_object *current, json_object *ledgers, json_object *env);

/* Hide the SIM PIN (option pincode) in a served copy of a plan, a ledger or a log, in place. */
void mm_netcfg_hide_pin(json_object *o);

/* proto of the device's managed interface in its ledger, or NULL */
const char *mm_netcfg_ledger_proto(json_object *ledger);

/* QModem's section for this modem's USB slot ("1-1.2" -> "1_1_2"), name into out. */
void mm_netcfg_qmodem_name(json_object *rec, char *out, size_t size);

/* The existing QModem modem-device section for this modem, or NULL. */
const char *mm_netcfg_qmodem_section(json_object *current, json_object *rec);

/* The operator's device section for this device_id, or NULL. */
json_object *mm_netcfg_decision(json_object *current, const char *device_id);

/* the DNS servers the operator typed for a modem (option dns: a list, or addresses split by spaces or commas), only
 * valid IPv4/IPv6 addresses, at most MM_NETCFG_DNS_MAX; NULL when none (new array) */
#define MM_NETCFG_DNS_MAX 4
json_object *mm_netcfg_manual_dns(json_object *decision);

/* a session interface not ours on this modem's control or tty port (proto mbim, qmi, ncm, 3g, wwan, directip):
 * {"interface", "proto", "off": its auto is 0 or it is disabled}; NULL when none (new object) */
json_object *mm_netcfg_foreign(json_object *rec, json_object *network, json_object *ledgers);
/* the interface the user already set up for this modem (its control port, tty, netdev or ModemManager device), or NULL */
const char *mm_netcfg_user_interface(json_object *rec, json_object *network, json_object *ledgers);

/* the router's own uplink interfaces in the network config (a wired WAN, PPPoE): [{interface, proto, metric,
 * twins: [the dhcpv6 interfaces on it]}], without the names in skip (the modems'); new array */
json_object *mm_netcfg_wired(json_object *network, json_object *skip);

/*
 * Whether two sysfs paths of USB devices name the same physical port on the
 * same controller, whatever bus numbers the kernel gave the root hubs this
 * boot: ".../5200000.usb/usb2/2-1/2-1.4" and ".../5200000.usb/usb1/1-1/1-1.4"
 * are the same port (H618 numbers its EHCI/OHCI buses in boot order, F9).
 * False for equal paths, so a match always means a stale path.
 */
bool mm_netcfg_same_usb_port(const char *a, const char *b);

/*
 * Check the proto modemmanager interfaces that serve this modem (spec 17, F9,
 * F10): its current sysfs path, and the interfaces whose device is that path,
 * or a path of the same port that no longer exists (exists() says). Per
 * interface: {"name", "engine": its ledger's, "zone": name | null,
 * "findings": ["stale-device", "no-zone", "zone-not-wan:<zone>"],
 * "ops": [cfgplan ops repairing a user's interface]}. The engine's own
 * interfaces get findings only (a transaction repairs them). A stale device
 * becomes the current path (expect: the old one); an interface in no
 * firewall zone joins "wan" when that zone exists; one in another zone is
 * reported, never moved. New array.
 */
json_object *mm_netcfg_repair(json_object *current, json_object *ledgers, const char *path,
			      bool (*exists)(const char *path), json_object *paths, const char *alias);
/* the sysfs path a names a USB hub (or port) the device b sits behind, whatever the bus numbers (a field report of
 * modem-telemetry, 2026-11-01: an interface left on «…/usb1/1-1», its T99 behind that hub at «…/usb5/5-1/5-1.3»).
 * With paths (the present modems' paths), the repair moves such an interface to b when b is the only modem behind
 * that hub: «hub-device». */
bool mm_netcfg_usb_ancestor(const char *a, const char *b);

/*
 * A router-mode modem's DHCP interface names its network device by the kernel's name (usb0, eth1), which
 * follows the order the sticks came up in, not the stick (bench 2026-10-20: a UFI moved to another port came up
 * after the E3372, the two swapped usb0/usb1, and each modem's interface carried the other one's traffic). For the
 * modem device_id whose network device is now net: {"stale": the name of its engine interface when that names
 * another device, else null; "squatters": [device ids of the other ledgers whose DHCP interface names net]}.
 * New object.
 */
/* {stale: our interface whose device is not `want` (the planner's device now; else `net`), squatters: [absent modems'
 * interfaces naming it]}; sysfs paths are left to mm_netcfg_repair */
json_object *mm_netcfg_netdev_check(json_object *current, json_object *ledgers, const char *device_id, const char *net,
				    const char *want);
/* the network device the engine's own AT dial uses for this modem (its profile's net_interface, else its first) */
const char *mm_netcfg_dial_netdev(json_object *rec);

#endif
