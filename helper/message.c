#define _DEFAULT_SOURCE

#include "message.h"
#include "json_io.h"
#include "store.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

static unsigned message_seq;
static uint64_t message_nonce;
static int message_nonce_ready;

int omaq_message_id_ok(const char *id)
{
	size_t i, n;

	if (!id || !id[0])
		return 0;
	n = strlen(id);
	if (n > 96)
		return 0;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)id[i];
		if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
		    !(c >= '0' && c <= '9') && c != '-' && c != '_' && c != ':' && c != '.')
			return 0;
	}
	return 1;
}

int omaq_message_id_new(char *out, size_t outn)
{
	struct timespec now;

	if (!out || outn == 0)
		return -1;
	if (!message_nonce_ready) {
		if (getrandom(&message_nonce, sizeof(message_nonce), 0) != (ssize_t)sizeof(message_nonce)) {
			if (clock_gettime(CLOCK_REALTIME, &now) != 0)
				return -1;
			message_nonce = ((uint64_t)now.tv_sec << 32) ^ (uint64_t)now.tv_nsec ^ (uint64_t)getpid();
		}
		message_nonce_ready = 1;
	}
	message_seq++;
	if (snprintf(out, outn, "%llu-%u-%016llx",
			     (unsigned long long)time(NULL), message_seq,
			     (unsigned long long)message_nonce) >= (int)outn)
		return -1;
	return 0;
}

int omaq_message_append_id_reply(const char *home, const char *conv_id, const char *from,
				  const char *text, const char *dir, const char *message_id,
				  const char *reply_id)
{
	char esc_id[128], esc_from[256], esc_text[2800], esc_reply[128], line[3400];
	const char *reply = reply_id ? reply_id : "";
	int wr;

	if (!from || !text || !dir || !omaq_message_id_ok(message_id) ||
	    (reply[0] && !omaq_message_id_ok(reply)))
		return -1;
	if (strchr(from, '\n'))
		return -1;
	if (omaq_json_escape(message_id, esc_id, sizeof(esc_id)) != 0 ||
	    omaq_json_escape(from, esc_from, sizeof(esc_from)) != 0 ||
	    omaq_json_escape(text, esc_text, sizeof(esc_text)) != 0 ||
	    omaq_json_escape(reply, esc_reply, sizeof(esc_reply)) != 0)
		return -1;
	if (reply[0])
		wr = snprintf(line, sizeof(line),
			      "{\"id\":\"%s\",\"ts\":%lld,\"from\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\",\"reply\":\"%s\"}",
			      esc_id, (long long)time(NULL), esc_from, esc_text, dir, esc_reply);
	else
		wr = snprintf(line, sizeof(line),
			      "{\"id\":\"%s\",\"ts\":%lld,\"from\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			      esc_id, (long long)time(NULL), esc_from, esc_text, dir);
	if (wr < 0 || (size_t)wr >= sizeof(line))
		return -1;
	return omaq_store_append(home, conv_id, line);
}

int omaq_message_append_id(const char *home, const char *conv_id, const char *from,
			   const char *text, const char *dir, const char *message_id)
{
	return omaq_message_append_id_reply(home, conv_id, from, text, dir, message_id, "");
}

int omaq_message_append_with_id(const char *home, const char *conv_id, const char *from,
				const char *text, const char *dir, char *id_out, size_t id_outn)
{
	char id[64];

	if (omaq_message_id_new(id, sizeof(id)) != 0 ||
	    omaq_message_append_id(home, conv_id, from, text, dir, id) != 0)
		return -1;
	if (id_out && id_outn && snprintf(id_out, id_outn, "%s", id) >= (int)id_outn)
		return -1;
	return 0;
}

int omaq_message_edit(const char *home, const char *conv_id, const char *id, const char *text)
{
	if (!omaq_message_id_ok(id))
		return -1;
	return omaq_store_update_message(home, conv_id, id, text, 0, "me");
}

int omaq_message_delete(const char *home, const char *conv_id, const char *id)
{
	if (!omaq_message_id_ok(id))
		return -1;
	return omaq_store_update_message(home, conv_id, id, "", 1, "me");
}

int omaq_message_apply_edit_from(const char *home, const char *conv_id, const char *id,
				 const char *text, const char *from)
{
	if (!omaq_message_id_ok(id))
		return -1;
	return omaq_store_update_message(home, conv_id, id, text, 0, from);
}

int omaq_message_apply_delete_from(const char *home, const char *conv_id, const char *id,
				   const char *from)
{
	if (!omaq_message_id_ok(id))
		return -1;
	return omaq_store_update_message(home, conv_id, id, "", 1, from);
}

int omaq_message_apply_edit(const char *home, const char *conv_id, const char *id, const char *text)
{
	return omaq_message_apply_edit_from(home, conv_id, id, text, "peer");
}

int omaq_message_apply_delete(const char *home, const char *conv_id, const char *id)
{
	return omaq_message_apply_delete_from(home, conv_id, id, "peer");
}

int omaq_message_append(const char *home, const char *conv_id, const char *from,
			const char *text, const char *dir)
{
	return omaq_message_append_with_id(home, conv_id, from, text, dir, NULL, 0);
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

int omaq_message_search(const char *home, const char *conv_id, const char *needle,
			int limit, char **out, size_t *out_len)
{
	return omaq_store_search(home, conv_id, needle, limit, out, out_len);
}
