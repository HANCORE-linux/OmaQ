#include "message_action.h"
#include "message.h"

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

int omaq_message_edit_wire_pack(char *out, size_t outn, const char *id, const char *text)
{
	int wr;

	if (!out || outn == 0 || !omaq_message_id_ok(id) || !text)
		return -1;
	wr = snprintf(out, outn, "OQE1|%s|%s", id, text);
	return wr < 0 || (size_t)wr >= outn ? -1 : 0;
}

int omaq_message_edit_wire_unpack(const char *wire, char *id, size_t idn,
				  char *text, size_t textn)
{
	const char *start, *sep;

	if (!wire || strncmp(wire, "OQE1|", 5) != 0)
		return -1;
	start = wire + 5;
	sep = strchr(start, '|');
	if (!sep || sep == start || copy_part(id, idn, start, (size_t)(sep - start)) != 0 ||
	    !omaq_message_id_ok(id) || copy_part(text, textn, sep + 1, strlen(sep + 1)) != 0)
		return -1;
	return 0;
}

int omaq_message_delete_wire_pack(char *out, size_t outn, const char *id)
{
	int wr;

	if (!out || outn == 0 || !omaq_message_id_ok(id))
		return -1;
	wr = snprintf(out, outn, "OQD1|%s", id);
	return wr < 0 || (size_t)wr >= outn ? -1 : 0;
}

int omaq_message_delete_wire_unpack(const char *wire, char *id, size_t idn)
{
	if (!wire || strncmp(wire, "OQD1|", 5) != 0 ||
	    copy_part(id, idn, wire + 5, strlen(wire + 5)) != 0 || !omaq_message_id_ok(id))
		return -1;
	return 0;
}
