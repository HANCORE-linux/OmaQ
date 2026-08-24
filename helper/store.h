#ifndef OMAQ_STORE_H
#define OMAQ_STORE_H

#include <stddef.h>
#include "conversation.h"

/* Only this module opens history files. */

int omaq_store_append(const char *home, const char *conv_id, const char *line);
/* Removes current and rotated history for exactly one conversation. */
int omaq_store_clear(const char *home, const char *conv_id);
/* Updates an outgoing message in place; returns -2 when id is not owned/found. */
int omaq_store_update_message(const char *home, const char *conv_id, const char *id,
			       const char *text, int deleted, const char *expected_from);
/* Writes last `limit` lines into *out (malloc). Caller frees. */
int omaq_store_tail(const char *home, const char *conv_id, int limit, char **out, size_t *out_len);
/* Last matching lines (case-insensitive). Caller frees *out. */
int omaq_store_update_receipt(const char *home, const char *conv_id, const char *id,
			       const char *state);
int omaq_store_message_exists(const char *home, const char *conv_id, const char *id);
int omaq_store_update_reaction(const char *home, const char *conv_id, const char *id,
                                const char *emoji, const char *actor);
int omaq_store_search(const char *home, const char *conv_id, const char *needle,
		      int limit, char **out, size_t *out_len);
int omaq_store_unread_load(omaq_unread_state *state, const char *state_dir);
int omaq_store_unread_save(const omaq_unread_state *state, const char *state_dir);

#endif
