#ifndef OMAQ_SAFETY_H
#define OMAQ_SAFETY_H

#include <stddef.h>

#define OMAQ_SAFETY_MAX 168

/* Canonical display of two 64-hex public keys. Same pair ⇒ same string
 * regardless of argument order. 0 = ok, -1 = invalid. */
int omaq_safety_code(const char *pk_a, const char *pk_b, char *out, size_t n);

#endif
