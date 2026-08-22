#ifndef OMAQ_MESSAGE_ACTION_H
#define OMAQ_MESSAGE_ACTION_H

#include <stddef.h>

int omaq_message_edit_wire_pack(char *out, size_t outn, const char *id, const char *text);
int omaq_message_edit_wire_unpack(const char *wire, char *id, size_t idn,
				  char *text, size_t textn);
int omaq_message_delete_wire_pack(char *out, size_t outn, const char *id);
int omaq_message_delete_wire_unpack(const char *wire, char *id, size_t idn);

#endif
