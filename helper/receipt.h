#ifndef OMAQ_RECEIPT_H
#define OMAQ_RECEIPT_H

#include <stddef.h>

int omaq_message_wire_pack(char *out, size_t outn, const char *id,
			   const char *reply, const char *text);
int omaq_message_wire_unpack(const char *wire, char *id, size_t idn,
			     char *reply, size_t replyn, char *text, size_t textn);
int omaq_receipt_wire_pack(char *out, size_t outn, const char *id, const char *state);
int omaq_receipt_wire_unpack(const char *wire, char *id, size_t idn,
			     char *state, size_t staten);

#endif
