#ifndef MEDVEMODEM_ROLES_H
#define MEDVEMODEM_ROLES_H

#include <json-c/json.h>

/*
 * Port roles and ownership (spec 11.4, 11.10, 13). Pure.
 *
 * Evidence, strongest first: ModemManager port types, kernel driver and
 * interface class (a cdc-wdm of qmi_wwan is control, a CDC ACM interface with
 * protocol 01 speaks AT), then nothing: an unknown tty stays unknown until a
 * validated profile or a bounded probe (M2) says more. A tty number is never
 * evidence.
 *
 * mm:       the device's mapped ModemManager modem, or NULL;
 * mm_state: the ModemManager backend state ("available", "unavailable", ...),
 *           so "MM has not mapped it yet" is not mistaken for "MM does not own it".
 *
 * Returns {"generation", "owner": {control, session, at}, "at", "control",
 * "candidates": [{node, kind, interface, number, class, driver, role,
 * confidence, source, owner}]}.
 */
json_object *mm_roles_resolve(json_object *rec, json_object *mm, const char *mm_state);

/* The candidate for a node ("ttyUSB2" or "/dev/ttyUSB2") in a resolution, or NULL. */
json_object *mm_roles_candidate(json_object *roles, const char *node);

#endif
