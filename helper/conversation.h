#ifndef OMAQ_CONVERSATION_H
#define OMAQ_CONVERSATION_H

#include <stddef.h>

#define OMAQ_CONV_ID_MAX 80
#define OMAQ_LAST_MAX 160

typedef enum { CONV_DIRECT = 0, CONV_GROUP = 1 } omaq_conv_kind;

typedef struct {
	char id[OMAQ_CONV_ID_MAX];
	omaq_conv_kind kind;
	int unread;
	char last[OMAQ_LAST_MAX];
} omaq_conv;

void omaq_conv_init(omaq_conv *c, const char *id, omaq_conv_kind kind);
void omaq_conv_note(omaq_conv *c, const char *last, int incoming);

typedef struct {
	char id[OMAQ_CONV_ID_MAX];
	unsigned count;
} omaq_unread_entry;

typedef struct {
	omaq_unread_entry *entries;
	size_t length;
	size_t capacity;
} omaq_unread_state;

void omaq_unread_init(omaq_unread_state *state);
void omaq_unread_destroy(omaq_unread_state *state);
int omaq_unread_clone(omaq_unread_state *destination, const omaq_unread_state *source);
int omaq_unread_set(omaq_unread_state *state, const char *conversation, unsigned count);
int omaq_unread_increment(omaq_unread_state *state, const char *conversation);
int omaq_unread_clear(omaq_unread_state *state, const char *conversation);
unsigned omaq_unread_count(const omaq_unread_state *state, const char *conversation);
unsigned omaq_unread_total(const omaq_unread_state *state);

#endif
