#ifndef OMAQ_LINE_READER_H
#define OMAQ_LINE_READER_H

#include "json_io.h"

#include <stddef.h>

typedef struct {
	char buffer[OMAQ_JSON_LINE_MAX];
	size_t length;
	int discard;
} omaq_line_reader;

typedef int (*omaq_line_callback)(char *line, void *ctx);

void omaq_line_reader_init(omaq_line_reader *reader);
int omaq_line_reader_feed(omaq_line_reader *reader, const char *data, size_t len,
			  omaq_line_callback callback, void *ctx);

#endif
