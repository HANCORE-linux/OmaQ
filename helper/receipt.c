#include "receipt.h"
#include "message.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int copy_part(char *out, size_t outn, const char *start, size_t len)
{
	if (!out || outn == 0 || !start || len + 1 > outn)
		return -1;
	memcpy(out, start, len);
	out[len] = '\0';
	return 0;
}

static int looks_like_message_id(const char *start, size_t len)
{
	char id[97];

	if (copy_part(id, sizeof(id), start, len) != 0)
		return 0;
	return omaq_message_id_ok(id);
}

int omaq_message_wire_pack(char *out, size_t outn, const char *id,
			   const char *reply, const char *text)
{
	int wr;
	const char *reply_id = reply && reply[0] ? reply : "-";

	if (!out || outn == 0 || !omaq_message_id_ok(id) || !text || strchr(reply_id, '|'))
		return -1;
	wr = snprintf(out, outn, "OQM1|%s|%s|%s", id, reply_id, text);
	return wr < 0 || (size_t)wr >= outn ? -1 : 0;
}

int omaq_message_wire_unpack(const char *wire, char *id, size_t idn,
			     char *reply, size_t replyn, char *text, size_t textn)
{
	const char *start, *sep, *reply_sep;

	if (!wire || strncmp(wire, "OQM1|", 5) != 0)
		return -1;
	start = wire + 5;
	sep = strchr(start, '|');
	if (!sep || sep == start || copy_part(id, idn, start, (size_t)(sep - start)) != 0 ||
	    !omaq_message_id_ok(id))
		return -1;
	reply_sep = strchr(sep + 1, '|');
	if (reply_sep && looks_like_message_id(sep + 1, (size_t)(reply_sep - (sep + 1)))) {
		if (copy_part(reply, replyn, sep + 1, (size_t)(reply_sep - (sep + 1))) != 0)
			return -1;
		if (strcmp(reply, "-") == 0)
			reply[0] = '\0';
		return copy_part(text, textn, reply_sep + 1, strlen(reply_sep + 1));
	}
	if (reply && replyn)
		reply[0] = '\0';
	return copy_part(text, textn, sep + 1, strlen(sep + 1));
}

int omaq_receipt_wire_pack(char *out, size_t outn, const char *id, const char *state)
{
	int wr;

	if (!out || outn == 0 || !omaq_message_id_ok(id) || !state || strchr(state, '|'))
		return -1;
	if (strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0)
		return -1;
	wr = snprintf(out, outn, "OQA1|%s|%s", state, id);
	return wr < 0 || (size_t)wr >= outn ? -1 : 0;
}

int omaq_receipt_wire_unpack(const char *wire, char *id, size_t idn,
			     char *state, size_t staten)
{
	const char *start, *sep;

	if (!wire || strncmp(wire, "OQA1|", 5) != 0)
		return -1;
	start = wire + 5;
	sep = strchr(start, '|');
	if (!sep || sep == start || copy_part(state, staten, start, (size_t)(sep - start)) != 0)
		return -1;
	if ((strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0) ||
	    copy_part(id, idn, sep + 1, strlen(sep + 1)) != 0 || !omaq_message_id_ok(id))
		return -1;
	return 0;
}
