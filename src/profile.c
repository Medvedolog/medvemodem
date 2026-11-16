#define _GNU_SOURCE
#include "profile.h"

#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef CATALOG_FILE
#define CATALOG_FILE "/usr/share/medvemodem/profiles/catalog/hardware-inventory.json"
#endif

static json_object *load_json(const char *path)
{
	return json_object_from_file(path);
}

static bool json_file(const char *name)
{
	size_t n = strlen(name);
	return n > 5 && !strcmp(name + n - 5, ".json");
}

/*
 * The profile directory and the catalog, read once and kept (bench 2026-10-19, r56: the daemon at 57 % of a core
 * read and parsed every profile file many times a second, and every file again for each "extends"). Read again
 * when the directory changes (a file added, removed or replaced by a package) and, for a file edited in place, when
 * a file's time differs at a look at most once a minute. A caller gets a reference, never the cache to change.
 */
#define PROFILE_RESTAT_S 60
static struct {
	char dir[PATH_MAX];
	struct timespec mtime;
	time_t checked;
	uint64_t files_sig;		/* audit M05: unsigned, its overflow is defined (mod 2^64) */
	unsigned long generation;	/* audit M04: one more at every read, the daemon's memo follows it */
	json_object *raws;		/* [{"name", "json"}...] in alphasort order, the files as they are */
	json_object *list;		/* the inherited, valid profiles in the same order */
} pcache;

static uint64_t files_signature(const char *dir, json_object *raws)
{
	uint64_t sig = 0;
	size_t i;

	for (i = 0; raws && i < json_object_array_length(raws); i++) {
		char path[PATH_MAX];
		struct stat st;
		json_object *name = json_object_object_get(json_object_array_get_idx(raws, i), "name");

		snprintf(path, sizeof(path), "%s/%s", dir, json_object_get_string(name));
		if (!stat(path, &st))
			sig = sig * 31u + (uint64_t)st.st_mtim.tv_sec * 1000003u + (uint64_t)st.st_mtim.tv_nsec + (uint64_t)st.st_size;
	}
	return sig;
}

static json_object *inherit_from(json_object *raws, json_object *child, unsigned depth);

static bool pcache_fresh(const char *dir)
{
	struct stat st;
	time_t now = time(NULL);

	if (!pcache.list || strcmp(pcache.dir, dir) || stat(dir, &st) ||
	    st.st_mtim.tv_sec != pcache.mtime.tv_sec || st.st_mtim.tv_nsec != pcache.mtime.tv_nsec)
		return false;
	if (now - pcache.checked < PROFILE_RESTAT_S && now >= pcache.checked)
		return true;
	pcache.checked = now;
	return files_signature(dir, pcache.raws) == pcache.files_sig;
}

static bool valid_match(json_object *profile);

/* the inherited, valid profiles of dir (borrowed; valid until the next call), NULL when the directory cannot be read */
static json_object *profiles_of(const char *dir)
{
	struct dirent **entries = NULL;
	struct stat st;
	int count, i;

	if (pcache_fresh(dir))
		return pcache.list;
	if (stat(dir, &st) || (count = scandir(dir, &entries, NULL, alphasort)) < 0)
		return NULL;
	json_object_put(pcache.raws);
	json_object_put(pcache.list);
	pcache.raws = json_object_new_array();
	pcache.list = json_object_new_array();
	for (i = 0; i < count; i++) {
		char path[PATH_MAX];
		int n;

		if (json_file(entries[i]->d_name) &&
		    (n = snprintf(path, sizeof(path), "%s/%s", dir, entries[i]->d_name)) > 0 && (size_t)n < sizeof(path)) {
			json_object *raw = load_json(path), *e;

			if (raw) {
				e = json_object_new_object();
				json_object_object_add(e, "name", json_object_new_string(entries[i]->d_name));
				json_object_object_add(e, "json", raw);
				json_object_array_add(pcache.raws, e);
			}
		}
		free(entries[i]);
	}
	free(entries);
	for (i = 0; i < (int)json_object_array_length(pcache.raws); i++) {
		json_object *raw = json_object_object_get(json_object_array_get_idx(pcache.raws, i), "json");
		json_object *profile = inherit_from(pcache.raws, raw, 0);

		if (profile && valid_match(profile))
			json_object_array_add(pcache.list, profile);
		else
			json_object_put(profile);
	}
	snprintf(pcache.dir, sizeof(pcache.dir), "%s", dir);
	pcache.mtime = st.st_mtim;
	pcache.checked = time(NULL);
	pcache.files_sig = files_signature(dir, pcache.raws);
	pcache.generation++;
	return pcache.list;
}

unsigned long mm_profile_generation(const char *dir)
{
	profiles_of(dir);
	return pcache.generation;
}

/* Reject unsupported match keys rather than silently ignoring constraints. */
static bool valid_match(json_object *profile)
{
	json_object *match = NULL, *id = NULL, *policy = NULL, *priority = NULL;
	if (!json_object_is_type(profile, json_type_object) ||
	    !json_object_object_get_ex(profile, "id", &id) ||
	    !json_object_is_type(id, json_type_string) || !*json_object_get_string(id) || strlen(json_object_get_string(id)) >= 96 ||
	    !json_object_object_get_ex(profile, "policy", &policy) ||
	    !json_object_is_type(policy, json_type_object) ||
	    !json_object_object_get_ex(policy, "owner_priority", &priority) ||
	    !json_object_is_type(priority, json_type_array) || !json_object_array_length(priority) ||
	    !json_object_object_get_ex(profile, "match", &match) ||
	    !json_object_is_type(match, json_type_object) || !json_object_object_length(match))
		return false;
	json_object_object_foreach(match, key, value) {
		if (!strcmp(key, "drivers_any")) {
			if (!json_object_is_type(value, json_type_array) || !json_object_array_length(value))
				return false;
			for (size_t i = 0; i < json_object_array_length(value); i++) {
				json_object *v = json_object_array_get_idx(value, i);
				if (!json_object_is_type(v, json_type_string) || !*json_object_get_string(v))
					return false;
			}
		} else if (!strcmp(key, "mm_model_contains") && json_object_is_type(value, json_type_array)) {
			/* one module, several spellings of its model (Compal: SG500M2-X, RXM-G1, VOS 5G) */
			if (!json_object_array_length(value))
				return false;
			for (size_t i = 0; i < json_object_array_length(value); i++) {
				json_object *v = json_object_array_get_idx(value, i);
				if (!json_object_is_type(v, json_type_string) || !*json_object_get_string(v))
					return false;
			}
		} else if (!strcmp(key, "bus") || !strcmp(key, "vid") || !strcmp(key, "pid") ||
			   !strcmp(key, "manufacturer_contains") || !strcmp(key, "product_contains") ||
			   !strcmp(key, "mm_manufacturer_contains") || !strcmp(key, "mm_model_contains")) {
			if (!json_object_is_type(value, json_type_string) || !*json_object_get_string(value))
				return false;
		} else {
			return false;
		}
	}
	return true;
}

static json_object *find_parent(json_object *raws, const char *wanted)
{
	json_object *found = NULL;
	size_t i;

	for (i = 0; i < json_object_array_length(raws); i++) {
		json_object *p = json_object_object_get(json_object_array_get_idx(raws, i), "json"), *id = NULL;

		if (json_object_object_get_ex(p, "id", &id) && json_object_is_type(id, json_type_string) &&
		    !strcmp(json_object_get_string(id), wanted)) {
			if (found)	/* Duplicate IDs are ambiguous even before matching. */
				return NULL;
			found = p;
		}
	}
	return found ? json_object_get(found) : NULL;
}

/* Object sections overlay by key; arrays replace rather than concatenate. */
static json_object *inherit_from(json_object *raws, json_object *child, unsigned depth)
{
	json_object *extends = NULL;
	if (!json_object_is_type(child, json_type_object) || depth >= 16)
		return NULL;
	if (!json_object_object_get_ex(child, "extends", &extends))
		return json_object_get(child);
	if (!json_object_is_type(extends, json_type_string))
		return NULL;
	json_object *raw = find_parent(raws, json_object_get_string(extends));
	if (!raw)
		return NULL;
	json_object *parent = inherit_from(raws, raw, depth + 1);
	json_object_put(raw);
	if (!parent)
		return NULL;
	json_object *result = json_object_new_object();
	json_object_object_foreach(parent, pkey, pvalue) {
		/* a family base (no match) is not inherited as abstract, nor a parent's fit: a model below generic-qmi is exact */
		if (strcmp(pkey, "abstract") && strcmp(pkey, "fit"))
			json_object_object_add(result, pkey, json_object_get(pvalue));
	}
	json_object_object_foreach(child, key, value) {
		json_object *base = NULL;
		if ((!strcmp(key, "match") || !strcmp(key, "policy") || !strcmp(key, "hints")) &&
		    json_object_object_get_ex(parent, key, &base) &&
		    json_object_is_type(base, json_type_object) && json_object_is_type(value, json_type_object)) {
			json_object *section = json_object_new_object();
			json_object_object_foreach(base, bkey, bvalue) {
				/* a model named by its pid is that model with or without a driver bound: its family's drivers_any
				 * (which tells the family's compositions apart) is not inherited. A ZTE MF100 with no serial driver
				 * yet fell to the generic profile and was told to install the wrong drivers (a tester, 2026-11-12) */
				if (!strcmp(key, "match") && !strcmp(bkey, "drivers_any") && json_object_object_get(value, "pid"))
					continue;
				json_object_object_add(section, bkey, json_object_get(bvalue));
			}
			json_object_object_foreach(value, ckey, cvalue) {
				json_object_object_add(section, ckey, json_object_get(cvalue));
			}
			json_object_object_add(result, key, section);
		} else {
			json_object_object_add(result, key, json_object_get(value));
		}
	}
	json_object_put(parent);
	return result;
}

static bool array_has_driver(json_object *interfaces, const char *wanted)
{
	size_t i;

	if (!interfaces || !wanted)
		return false;

	for (i = 0; i < json_object_array_length(interfaces); i++) {
		json_object *iface = json_object_array_get_idx(interfaces, i);
		json_object *o = NULL;
		const char *s;

		if (!json_object_object_get_ex(iface, "driver", &o))
			continue;
		s = json_object_get_string(o);
		if (s && !strcmp(s, wanted))
			return true;
	}
	return false;
}

static void add_reason(json_object *arr, const char *reason)
{
	json_object_array_add(arr, json_object_new_string(reason));
}

static int score_match(json_object *profile, const char *vid, const char *pid,
		       const char *manufacturer, const char *product,
		       const char *mm_manufacturer, const char *mm_model,
		       json_object *interfaces, json_object *reasons)
{
	json_object *match = NULL, *o = NULL;
	int score = 0;

	if (!json_object_object_get_ex(profile, "match", &match))
		return -1;

	if (json_object_object_get_ex(match, "bus", &o)) {
		const char *bus = json_object_get_string(o);
		if (!strcmp(bus, "usb")) {
			score += 20;
			add_reason(reasons, "bus=usb");
		} else if (!strcmp(bus, "any")) {
			score += 1;
			add_reason(reasons, "bus=any");
		} else {
			return -1;
		}
	}

	if (json_object_object_get_ex(match, "vid", &o)) {
		const char *s = json_object_get_string(o);
		if (!s || strcasecmp(s, vid))
			return -1;
		score += 80;
		add_reason(reasons, "vid");
	}
	if (json_object_object_get_ex(match, "pid", &o)) {
		const char *s = json_object_get_string(o);
		if (!s || strcasecmp(s, pid))
			return -1;
		score += 100;
		add_reason(reasons, "pid");
	}
	if (json_object_object_get_ex(match, "manufacturer_contains", &o)) {
		const char *s = json_object_get_string(o);
		if (!manufacturer || !s || !strcasestr(manufacturer, s))
			return -1;
		score += 30;
		add_reason(reasons, "manufacturer");
	}
	if (json_object_object_get_ex(match, "product_contains", &o)) {
		const char *s = json_object_get_string(o);
		if (!product || !s || !strcasestr(product, s))
			return -1;
		score += 30;
		add_reason(reasons, "product");
	}
	if (json_object_object_get_ex(match, "mm_manufacturer_contains", &o)) {
		const char *s = json_object_get_string(o);
		if (!mm_manufacturer || !s || !strcasestr(mm_manufacturer, s))
			return -1;
		score += 60;
		add_reason(reasons, "mm-manufacturer");
	}
	if (json_object_object_get_ex(match, "mm_model_contains", &o)) {
		bool found = false;
		if (json_object_is_type(o, json_type_array)) {
			for (size_t i = 0; mm_model && !found && i < json_object_array_length(o); i++)
				found = strcasestr(mm_model, json_object_get_string(json_object_array_get_idx(o, i))) != NULL;
		} else {
			const char *s = json_object_get_string(o);
			found = mm_model && s && strcasestr(mm_model, s);
		}
		if (!found)
			return -1;
		score += 80;
		add_reason(reasons, "mm-model");
	}
	if (json_object_object_get_ex(match, "drivers_any", &o)) {
		size_t i;
		bool found = false;
		for (i = 0; i < json_object_array_length(o); i++) {
			const char *d = json_object_get_string(json_object_array_get_idx(o, i));
			if (d && array_has_driver(interfaces, d)) {
				found = true;
				break;
			}
		}
		if (!found)
			return -1;
		score += 50;
		add_reason(reasons, "driver");
	}

	return score;
}

int mm_profile_resolve_usb_enriched(const char *dir, const char *vid, const char *pid,
				    const char *manufacturer, const char *product,
				    const char *mm_manufacturer, const char *mm_model,
				    json_object *interfaces,
				    struct mm_profile_match *out)
{
	json_object *list;
	int best = -1;

	memset(out, 0, sizeof(*out));
	out->score = -1;
	out->candidates = json_object_new_array();
	if (!(list = profiles_of(dir))) {
		mm_profile_match_clear(out);
		return -1;
	}

	for (size_t index = 0; index < json_object_array_length(list); index++) {
		/* borrowed from the cache; the one kept is a copy: a caller may change it, never the cache */
		json_object *profile = json_object_array_get_idx(list, index), *id = NULL, *reasons, *copy = NULL;
		const char *idstr;
		int score;

		if (!json_object_object_get_ex(profile, "id", &id))
			continue;
		idstr = json_object_get_string(id);
		reasons = json_object_new_array();
		score = score_match(profile, vid, pid, manufacturer, product,
				    mm_manufacturer, mm_model, interfaces, reasons);
		if (score >= 0 && score >= best) {
			if (score > best) {
				json_object_put(out->candidates);
				out->candidates = json_object_new_array();
			}
			json_object *candidate = json_object_new_object();
			json_object_object_add(candidate, "id", json_object_new_string(idstr));
			json_object_object_add(candidate, "score", json_object_new_int(score));
			json_object_object_add(candidate, "reasons", json_object_get(reasons));
			json_object_array_add(out->candidates, candidate);
		}
		if (score > best && !json_object_deep_copy(profile, &copy, NULL) && copy) {
			if (out->profile)
				json_object_put(out->profile);
			if (out->reasons)
				json_object_put(out->reasons);
			snprintf(out->id, sizeof(out->id), "%s", idstr ? idstr : "unknown");
			out->score = score;
			out->operational = score >= 0;
			out->profile = copy;
			out->reasons = reasons;
			best = score;
		} else {
			json_object_put(reasons);
		}
	}
	out->ambiguous = json_object_array_length(out->candidates) > 1;
	out->operational = best >= 0 && !out->ambiguous;
	if (best < 0)
		mm_profile_match_clear(out);
	return best;
}

int mm_profile_resolve_usb(const char *dir, const char *vid, const char *pid,
			   const char *manufacturer, const char *product,
			   json_object *interfaces,
			   struct mm_profile_match *out)
{
	return mm_profile_resolve_usb_enriched(dir, vid, pid, manufacturer, product,
					       NULL, NULL, interfaces, out);
}

/* the catalog, kept while its file is the same (it was read for every modem at every discovery pass) */
static json_object *catalog(void)
{
	static json_object *cat;
	static struct timespec mtime;
	static off_t size;
	struct stat st;

	if (stat(CATALOG_FILE, &st)) {
		json_object_put(cat);
		cat = NULL;
		return NULL;
	}
	if (!cat || st.st_mtim.tv_sec != mtime.tv_sec || st.st_mtim.tv_nsec != mtime.tv_nsec || st.st_size != size) {
		json_object_put(cat);
		cat = load_json(CATALOG_FILE);
		mtime = st.st_mtim;
		size = st.st_size;
	}
	return cat ? json_object_get(cat) : NULL;
}

bool mm_catalog_known_usb(const char *vid, const char *pid)
{
	json_object *root, *records = NULL;
	size_t i;
	bool found = false;

	root = catalog();
	if (!root)
		return false;
	if (!json_object_object_get_ex(root, "records", &records))
		goto out;

	for (i = 0; i < json_object_array_length(records); i++) {
		json_object *rec = json_object_array_get_idx(records, i);
		json_object *bus = NULL, *match = NULL, *v = NULL, *p = NULL;
		const char *bs, *vs, *ps;

		if (!json_object_object_get_ex(rec, "bus", &bus) ||
		    !json_object_object_get_ex(rec, "match", &match))
			continue;
		bs = json_object_get_string(bus);
		if (!bs || strcmp(bs, "usb"))
			continue;
		if (!json_object_object_get_ex(match, "vid", &v) ||
		    !json_object_object_get_ex(match, "pid", &p))
			continue;
		vs = json_object_get_string(v);
		ps = json_object_get_string(p);
		if (vs && ps && !strcasecmp(vs, vid) && !strcasecmp(ps, pid)) {
			found = true;
			break;
		}
	}
out:
	json_object_put(root);
	return found;
}

void mm_profile_match_clear(struct mm_profile_match *m)
{
	if (!m)
		return;
	if (m->candidates)
		json_object_put(m->candidates);
	if (m->profile)
		json_object_put(m->profile);
	if (m->reasons)
		json_object_put(m->reasons);
	memset(m, 0, sizeof(*m));
}


const char *mm_profile_fit(const struct mm_profile_match *m)
{
	json_object *fit = NULL;

	if (m->ambiguous)
		return "ambiguous";
	if (m->profile && json_object_object_get_ex(m->profile, "fit", &fit) && json_object_is_type(fit, json_type_string) &&
	    (!strcmp(json_object_get_string(fit), "family") || !strcmp(json_object_get_string(fit), "generic")))
		return json_object_get_string(fit);
	return "exact";
}
