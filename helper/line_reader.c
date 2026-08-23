#include "line_reader.h"

#include <errno.h>
#include <string.h>

void omaq_line_reader_init(omaq_line_reader *reader)
{
	if (reader)
		memset(reader, 0, sizeof(*reader));
}

int omaq_line_reader_feed(omaq_line_reader *reader, const char *data, size_t len,
			  omaq_line_callback callback, void *ctx)
{
	if (!reader || (!data && len) || !callback) {
		errno = EINVAL;
		return -1;
	}
	for (size_t i = 0; i < len; i++) {
		char c = data[i];

		if (reader->discard) {
			if (c == '\n') {
				reader->discard = 0;
				reader->length = 0;
			}
			continue;
		}
		if (c == '\0') {
			reader->length = 0;
			reader->discard = 1;
			continue;
		}
		if (c == '\n') {
			int rc;

			if (reader->length > 0 && reader->buffer[reader->length - 1] == '\r')
				reader->length--;
			reader->buffer[reader->length] = '\0';
			rc = callback(reader->buffer, ctx);
			reader->length = 0;
			if (rc != 0)
				return rc;
			continue;
		}
		if (reader->length + 1 >= sizeof(reader->buffer)) {
			reader->length = 0;
			reader->discard = 1;
			continue;
		}
		reader->buffer[reader->length++] = c;
	}
	return 0;
}
