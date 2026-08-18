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

#endif
