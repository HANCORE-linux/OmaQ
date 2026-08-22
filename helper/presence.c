#include "presence.h"

#include <stdio.h>
#include <string.h>

static int conversation_ok(const char *conversation)
{
	size_t i;

	if (!conversation || !conversation[0])
		return 0;
	for (i = 0; conversation[i]; i++) {
		if (conversation[i] < '0' || conversation[i] > '9')
			return 0;
	}
	return i < 16;
}

int omaq_presence_typing_event(char *out, size_t outn, const char *conversation, int typing)
{
	int wr;

	if (!out || outn == 0 || !conversation_ok(conversation))
		return -1;
	wr = snprintf(out, outn,
			"{\"event\":\"typing\",\"conversation\":\"%s\",\"typing\":%s}",
			conversation, typing ? "true" : "false");
	return wr < 0 || (size_t)wr >= outn ? -1 : 0;
}
