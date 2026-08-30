#ifndef OMAQ_TEXT_H
#define OMAQ_TEXT_H

#include <stddef.h>
#include <stdint.h>

/* Strict Unicode scalar UTF-8. NUL is always rejected inside the byte span. */
int omaq_utf8_bytes_ok(const uint8_t *value, size_t length, int reject_controls);

#endif
