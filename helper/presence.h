#ifndef OMAQ_PRESENCE_H
#define OMAQ_PRESENCE_H

#include <stddef.h>

/* Build the helper event for a direct-chat typing state. */
int omaq_presence_typing_event(char *out, size_t outn, const char *conversation, int typing);

#endif
