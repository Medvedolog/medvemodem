#ifndef MEDVEMODEM_PROFILE_H
#define MEDVEMODEM_PROFILE_H

#include <stdbool.h>
#include <json-c/json.h>

struct mm_profile_match {
	char id[96];
	int score;
	bool operational;
	bool ambiguous;
	json_object *candidates;
	json_object *reasons;
	json_object *profile;
};

bool mm_catalog_known_usb(const char *vid, const char *pid);
int mm_profile_resolve_usb(const char *dir, const char *vid, const char *pid,
			   const char *manufacturer, const char *product,
			   json_object *interfaces,
			   struct mm_profile_match *out);
int mm_profile_resolve_usb_enriched(const char *dir, const char *vid, const char *pid,
				    const char *manufacturer, const char *product,
				    const char *mm_manufacturer, const char *mm_model,
				    json_object *interfaces,
				    struct mm_profile_match *out);
void mm_profile_match_clear(struct mm_profile_match *m);
/* how well the match knows the model: "exact" (its own profile), "family" (a vendor's), "generic" (by driver only),
 * "ambiguous" (two profiles tie); the profile's own "fit", never inherited */
const char *mm_profile_fit(const struct mm_profile_match *m);

/* the profile cache's generation for dir: it changes whenever the files are read again (a file added, removed,
 * replaced or edited in place); a cache of decisions made from them keeps it and drops what it has when it moves */
unsigned long mm_profile_generation(const char *dir);

#endif

