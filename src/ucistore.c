#define _GNU_SOURCE
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uci.h>

#include "ucistore.h"

#define MAX_DELTA_PATHS 8

struct store {
	struct uci_context *ctx;
	struct uci_list *hidden[MAX_DELTA_PATHS];
	int nhidden;
};

static const char *str(json_object *o, const char *key)
{
	json_object *v = NULL;
	return o && json_object_object_get_ex(o, key, &v) && json_object_is_type(v, json_type_string) ?
		json_object_get_string(v) : NULL;
}

static void uci_error(struct uci_context *ctx, const char *what, char *err, size_t err_size)
{
	char *msg = NULL;

	uci_get_errorstr(ctx, &msg, what);
	snprintf(err, err_size, "%s", msg ? msg : what);
	free(msg);
}

/*
 * A context on confdir that sees only our private staging directory: every
 * other delta path (the shared /tmp/.uci) is unlinked from the search list,
 * and linked back before the context is freed so libuci releases it.
 */
static int store_open(struct store *st, const char *confdir, const char *savedir, char *err, size_t err_size)
{
	struct uci_list *l, *next;

	memset(st, 0, sizeof(*st));
	st->ctx = uci_alloc_context();
	if (!st->ctx) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	if (uci_set_confdir(st->ctx, confdir) || uci_set_savedir(st->ctx, savedir)) {
		uci_error(st->ctx, "uci", err, err_size);
		uci_free_context(st->ctx);
		st->ctx = NULL;
		return -1;
	}
	for (l = st->ctx->delta_path.next; l != &st->ctx->delta_path; l = next) {
		struct uci_element *e = (struct uci_element *)l;

		next = l->next;
		if (!strcmp(e->name, savedir) || st->nhidden >= MAX_DELTA_PATHS)
			continue;
		l->prev->next = l->next;
		l->next->prev = l->prev;
		st->hidden[st->nhidden++] = l;
	}
	return 0;
}

static void store_close(struct store *st)
{
	int i;

	if (!st->ctx)
		return;
	for (i = 0; i < st->nhidden; i++) {
		struct uci_list *l = st->hidden[i], *head = &st->ctx->delta_path;
		l->next = head->next;
		l->prev = head;
		head->next->prev = l;
		head->next = l;
	}
	uci_free_context(st->ctx);
	st->ctx = NULL;
}

static json_object *package_json(struct uci_package *p)
{
	json_object *out = json_object_new_object();
	struct uci_element *se, *oe, *le;

	uci_foreach_element(&p->sections, se) {
		struct uci_section *s = uci_to_section(se);
		json_object *sec = json_object_new_object();

		json_object_object_add(sec, ".type", json_object_new_string(s->type));
		if (s->anonymous)
			json_object_object_add(sec, ".anonymous", json_object_new_boolean(true));
		uci_foreach_element(&s->options, oe) {
			struct uci_option *o = uci_to_option(oe);

			if (o->type == UCI_TYPE_STRING) {
				json_object_object_add(sec, oe->name, json_object_new_string(o->v.string));
			} else {
				json_object *list = json_object_new_array();
				uci_foreach_element(&o->v.list, le)
					json_object_array_add(list, json_object_new_string(le->name));
				json_object_object_add(sec, oe->name, list);
			}
		}
		json_object_object_add(out, se->name, sec);
	}
	return out;
}

static bool exists(const char *confdir, const char *package)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/%s", confdir, package);
	return access(path, F_OK) == 0;
}

json_object *mm_uci_read(const char *confdir, const char *const *packages, char *err, size_t err_size)
{
	struct store st;
	json_object *out;
	size_t i;

	/* reading needs no staging; a directory that never holds deltas */
	if (store_open(&st, confdir, "/nonexistent/medvemodem-uci", err, err_size))
		return NULL;
	out = json_object_new_object();
	for (i = 0; packages[i]; i++) {
		struct uci_package *p = NULL;

		if (!exists(confdir, packages[i])) {
			json_object_object_add(out, packages[i], json_object_new_object());
			continue;
		}
		if (uci_load(st.ctx, packages[i], &p) || !p) {
			uci_error(st.ctx, packages[i], err, err_size);
			json_object_put(out);
			store_close(&st);
			return NULL;
		}
		json_object_object_add(out, packages[i], package_json(p));
		uci_unload(st.ctx, p);
	}
	store_close(&st);
	return out;
}

/* words of a string option, for turning "wan wan6" into a list */
static int split_words(const char *s, char words[][64], int max)
{
	int n = 0;

	while (s && *s && n < max) {
		size_t w;
		s += strspn(s, " \t");
		w = strcspn(s, " \t");
		if (w && w < 64) {
			memcpy(words[n], s, w);
			words[n++][w] = '\0';
		}
		s += w;
	}
	return n;
}

static int apply_op(struct uci_context *ctx, json_object *op)
{
	const char *kind = str(op, "op");
	struct uci_ptr ptr = { 0 };
	json_object *value = NULL;

	ptr.package = str(op, "package");
	ptr.section = str(op, "section");
	ptr.option = str(op, "option");
	json_object_object_get_ex(op, "value", &value);
	if (!kind || !ptr.package || !ptr.section)
		return UCI_ERR_INVAL;
	if (!strcmp(kind, "add-section")) {
		ptr.option = NULL;
		ptr.value = str(op, "type");
		return uci_lookup_ptr(ctx, &ptr, NULL, false) ?: uci_set(ctx, &ptr);
	}
	if (!strcmp(kind, "delete-section")) {
		ptr.option = NULL;
		return uci_lookup_ptr(ctx, &ptr, NULL, false) ?: uci_delete(ctx, &ptr);
	}
	if (!ptr.option)
		return UCI_ERR_INVAL;
	if (uci_lookup_ptr(ctx, &ptr, NULL, false))
		return ctx->err;
	if (!strcmp(kind, "delete"))
		return ptr.o ? uci_delete(ctx, &ptr) : 0;
	if (!strcmp(kind, "set")) {
		size_t i;
		int rc;

		if (value && json_object_is_type(value, json_type_string)) {
			ptr.value = json_object_get_string(value);
			return uci_set(ctx, &ptr);
		}
		if (ptr.o && (rc = uci_delete(ctx, &ptr)))
			return rc;
		for (i = 0; value && i < json_object_array_length(value); i++) {
			struct uci_ptr item = ptr;
			item.o = NULL;
			item.value = json_object_get_string(json_object_array_get_idx(value, i));
			if ((rc = uci_add_list(ctx, &item)))
				return rc;
		}
		return 0;
	}
	ptr.value = str(op, "value");
	if (!ptr.value)
		return UCI_ERR_INVAL;
	if (!strcmp(kind, "add-list") || !strcmp(kind, "del-list")) {
		bool add = !strcmp(kind, "add-list");
		int rc;

		if (ptr.o && ptr.o->type == UCI_TYPE_STRING && strpbrk(ptr.o->v.string, " \t")) {
			/* an option holding several words becomes a list of them first */
			char words[32][64];
			int i, n = split_words(ptr.o->v.string, words, 32);
			struct uci_ptr item = ptr;

			item.value = NULL;
			if ((rc = uci_delete(ctx, &item)))
				return rc;
			for (i = 0; i < n; i++) {
				if (!add && !strcmp(words[i], ptr.value))
					continue;
				item = ptr;
				item.o = NULL;
				item.value = words[i];
				if ((rc = uci_add_list(ctx, &item)))
					return rc;
			}
			if (!add)
				return 0;
			ptr.o = NULL;
			return uci_add_list(ctx, &ptr);
		}
		if (add)
			return uci_add_list(ctx, &ptr);
		if (ptr.o && ptr.o->type == UCI_TYPE_STRING) {
			ptr.value = NULL;
			return !strcmp(ptr.o->v.string, str(op, "value")) ? uci_delete(ctx, &ptr) : 0;
		}
		return uci_del_list(ctx, &ptr);
	}
	return UCI_ERR_INVAL;
}

static void clear_staging(const char *savedir, json_object *ops)
{
	char path[512];
	size_t i;

	mkdir(savedir, 0700);
	for (i = 0; i < json_object_array_length(ops); i++) {
		snprintf(path, sizeof(path), "%s/%s", savedir, str(json_object_array_get_idx(ops, i), "package"));
		unlink(path);
	}
}

int mm_uci_apply(const char *confdir, const char *savedir, json_object *ops, char *err, size_t err_size)
{
	struct store st;
	const char *done[16];
	int ndone = 0, rc = 0;
	size_t i;

	if (!ops || !json_object_array_length(ops))
		return 0;
	clear_staging(savedir, ops);	/* leftovers of an interrupted run are not ours to commit */
	if (store_open(&st, confdir, savedir, err, err_size))
		return -1;
	for (i = 0; i < json_object_array_length(ops); i++) {
		json_object *op = json_object_array_get_idx(ops, i);

		if (apply_op(st.ctx, op)) {
			char what[160];
			snprintf(what, sizeof(what), "%s %s.%s%s%s", str(op, "op"), str(op, "package"), str(op, "section"),
				 str(op, "option") ? "." : "", str(op, "option") ? str(op, "option") : "");
			uci_error(st.ctx, what, err, err_size);
			rc = -1;
			goto out;
		}
	}
	for (i = 0; i < json_object_array_length(ops); i++) {
		const char *pkg = str(json_object_array_get_idx(ops, i), "package");
		struct uci_ptr ptr = { 0 };
		int j;
		bool seen = false;

		for (j = 0; j < ndone; j++)
			seen = seen || !strcmp(done[j], pkg);
		if (seen || ndone >= 16)
			continue;
		done[ndone++] = pkg;
		ptr.package = pkg;
		if (uci_lookup_ptr(st.ctx, &ptr, NULL, false) || !ptr.p || uci_save(st.ctx, ptr.p) ||
		    uci_commit(st.ctx, &ptr.p, false)) {
			uci_error(st.ctx, pkg, err, err_size);
			rc = -1;
			goto out;
		}
	}
out:
	store_close(&st);
	clear_staging(savedir, ops);
	return rc;
}
