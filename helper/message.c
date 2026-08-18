#include "message.h"
#include "store.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int omaq_message_append(const char *home, const char *conv_id, const char *from,
			const char *text, const char *dir)
{
	char line[1024];
	long long ts = (long long)time(NULL);
	if (!from || !text || !dir)
		return -1;
	if (strchr(text, '\n') || strchr(from, '"'))
		return -1;
	if (snprintf(line, sizeof(line),
		     "{\"ts\":%lld,\"from\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
		     ts, from, text, dir) >= (int)sizeof(line))
		return -1;
	return omaq_store_append(home, conv_id, line);
}

int omaq_message_history(const char *home, const char *conv_id, int limit,
			 char **out, size_t *out_len)
{
	if (limit <= 0)
		limit = 50;
	if (limit > 50)
		limit = 50;
	return omaq_store_tail(home, conv_id, limit, out, out_len);
}
