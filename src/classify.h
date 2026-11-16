#ifndef MEDVEMODEM_CLASSIFY_H
#define MEDVEMODEM_CLASSIFY_H

#include <stdbool.h>
#include <json-c/json.h>

/* Pure classification: facts in, decision out. No I/O here. */
enum mm_detect {
	MM_DETECT_DROP,
	MM_DETECT_CANDIDATE,
	MM_DETECT_MODESWITCH_PENDING,
	MM_DETECT_SERVICE_MODE,
};

struct mm_facts {
	const char *vid;
	const char *pid;
	json_object *interfaces;
	json_object *tty;
	json_object *wdm;
	json_object *net;
	bool catalog_known;
	const char *config_value;	/* bConfigurationValue; "" or "0" when unconfigured */
	json_object *modeswitch;	/* usb-mode.json devices[vid:pid], or NULL */
	bool mm_held;			/* ModemManager holds a modem on this device */
};

bool mm_array_nonempty(json_object *arr);
bool mm_interfaces_have_driver(json_object *interfaces, const char *driver);
/* qmi_wwan and the vendors' builds of it (qmi_wwan_q Quectel, qmi_wwan_f Fibocom, qmi_wwan_s SimCom: the kmods QModem's
 * feeds ship; they are QMI with raw-ip) */
bool mm_is_qmi_driver(const char *driver);
/* a usb-mode.json devices[vid:pid] entry with a variant that sets configuration 0 */
bool mm_modeswitch_unconfigures(json_object *entry);
/* vendor 12d1 with mass-storage interfaces only: a Huawei stick before its mode switch */
bool mm_huawei_storage(const char *vid, json_object *interfaces);
bool mm_likely_modem(json_object *interfaces, json_object *tty,
		     json_object *wdm, json_object *net, bool catalog_known);
/* Returns the decision; *configuration receives a new {state, evidence} object. */
enum mm_detect mm_classify(const struct mm_facts *f, json_object **configuration);
const char *mm_detect_status(enum mm_detect d);

#endif
