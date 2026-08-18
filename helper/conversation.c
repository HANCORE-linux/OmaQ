#include "conversation.h"

#include <string.h>

void omaq_conv_init(omaq_conv *c, const char *id, omaq_conv_kind kind)
{
	memset(c, 0, sizeof(*c));
	if (id) {
		strncpy(c->id, id, sizeof(c->id) - 1);
	}
	c->kind = kind;
	c->unread = 0;
}

void omaq_conv_note(omaq_conv *c, const char *last, int incoming)
{
	if (!c)
		return;
	if (last) {
		strncpy(c->last, last, sizeof(c->last) - 1);
		c->last[sizeof(c->last) - 1] = '\0';
	}
	if (incoming)
		c->unread++;
}
