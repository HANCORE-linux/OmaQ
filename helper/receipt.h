#ifndef OMAQ_RECEIPT_H
#define OMAQ_RECEIPT_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_RECEIPT_OUTBOX_MAX 4096u

typedef struct {
	char conversation[80];
	char id[97];
	int64_t created;
	int64_t next_attempt;
	int acknowledged;
} omaq_receipt_outbox_entry;

typedef struct {
	omaq_receipt_outbox_entry *entries;
	size_t length;
	size_t capacity;
} omaq_receipt_outbox;

int omaq_message_wire_pack(char *out, size_t outn, const char *id,
			   const char *reply, const char *text);
int omaq_message_wire_unpack(const char *wire, char *id, size_t idn,
			     char *reply, size_t replyn, char *text, size_t textn);
int omaq_receipt_wire_pack(char *out, size_t outn, const char *id, const char *state);
int omaq_receipt_wire_unpack(const char *wire, char *id, size_t idn,
			     char *state, size_t staten);
int omaq_receipt_confirm_wire_pack(char *out, size_t outn, const char *id,
				   const char *state, const char *target);
int omaq_receipt_confirm_wire_unpack(const char *wire, char *id, size_t idn,
				     char *state, size_t staten,
				     char *target, size_t targetn);

void omaq_receipt_outbox_init(omaq_receipt_outbox *outbox);
void omaq_receipt_outbox_destroy(omaq_receipt_outbox *outbox);
int omaq_receipt_outbox_clone(omaq_receipt_outbox *destination,
			      const omaq_receipt_outbox *source);
int omaq_receipt_outbox_add(omaq_receipt_outbox *outbox,
			    const char *conversation, const char *id);
int omaq_receipt_outbox_remove(omaq_receipt_outbox *outbox,
			       const char *conversation, const char *id);
int omaq_receipt_outbox_load(omaq_receipt_outbox *outbox, const char *state_dir);
int omaq_receipt_outbox_save(const omaq_receipt_outbox *outbox, const char *state_dir);
int omaq_receipt_transaction_load(omaq_receipt_outbox *transaction,
				  const char *state_dir);
int omaq_receipt_transaction_save(const omaq_receipt_outbox *transaction,
				  const char *state_dir);
int omaq_receipt_transaction_mark_committed(const char *state_dir);
/* 1 = committed, 0 = prepared only, -1 = invalid/error. */
int omaq_receipt_transaction_committed(const char *state_dir);
int omaq_receipt_transaction_clear(const char *state_dir);

#endif
