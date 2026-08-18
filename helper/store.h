#ifndef OMAQ_STORE_H
#define OMAQ_STORE_H

#include <stddef.h>

/* Only this module opens history files. */

int omaq_store_append(const char *home, const char *conv_id, const char *line);
/* Writes last `limit` lines into *out (malloc). Caller frees. */
int omaq_store_tail(const char *home, const char *conv_id, int limit, char **out, size_t *out_len);
/* Last matching lines (case-insensitive). Caller frees *out. */
int omaq_store_search(const char *home, const char *conv_id, const char *needle,
		      int limit, char **out, size_t *out_len);

#endif
