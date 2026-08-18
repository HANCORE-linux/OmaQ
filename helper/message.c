#include "message.h"
#include "json_io.h"
#include "store.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

int omaq_message_append(const char *home, const char *conv_id, const char *from,
			const char *text, const char *dir)
{
	char esc_from[256], esc_text[2800], line[3200];
	static unsigned seq;
	long long ts = (long long)time(NULL);

	if (!from || !text || !dir)
		return -1;
	if (strchr(from, '\n') || strchr(text, '\n'))
		return -1;
	if (omaq_json_escape(from, esc_from, sizeof(esc_from)) != 0)
		return -1;
	if (omaq_json_escape(text, esc_text, sizeof(esc_text)) != 0)
		return -1;
	seq++;
	if (snprintf(line, sizeof(line),
		     "{\"id\":\"%lld-%u\",\"ts\":%lld,\"from\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
		     ts, seq, ts, esc_from, esc_text, dir) >= (int)sizeof(line))
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
