#ifndef OMAQ_MESSAGE_H
#define OMAQ_MESSAGE_H

#include <stddef.h>

int omaq_message_append(const char *home, const char *conv_id, const char *from,
			const char *text, const char *dir);
int omaq_message_history(const char *home, const char *conv_id, int limit,
			 char **out, size_t *out_len);
int omaq_message_search(const char *home, const char *conv_id, const char *needle,
			int limit, char **out, size_t *out_len);

#endif
