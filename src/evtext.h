#ifndef MEDVEMODEM_EVTEXT_H
#define MEDVEMODEM_EVTEXT_H

#include <stdbool.h>
#include <stddef.h>

/* The English title of a history event kind, or NULL for a kind it does not know. */
const char *mm_event_title(const char *kind);

/* One history event as a sentence: Recovery step finished: "Power-cycle the USB port" failed: <why>.
 * An unknown kind is written as it is. Always NUL-terminated. */
void mm_event_words(const char *kind, const char *detail, char *out, size_t size);

#endif
