#ifndef OMAQ_RATE_H
#define OMAQ_RATE_H

#include <stdint.h>

#define OMAQ_RATE_PER_MIN 5
#define OMAQ_RATE_PER_HOUR 20
#define OMAQ_RATE_KEY_SLOTS 32
#define OMAQ_RATE_KEY_LEN 65

typedef struct {
	char key[OMAQ_RATE_KEY_LEN];
	int64_t min[OMAQ_RATE_PER_MIN];
} omaq_rate_key;

typedef struct {
	omaq_rate_key keys[OMAQ_RATE_KEY_SLOTS];
	int64_t hour[OMAQ_RATE_PER_HOUR];
} omaq_rate;

void omaq_rate_init(omaq_rate *r);
/* 0 = allow and record, -1 = deny. key is an opaque id (e.g. peer pk hex). */
int omaq_rate_allow(omaq_rate *r, const char *key, int64_t now);
/* Per-key window only, for protocol-specific independent limiters. */
int omaq_rate_allow_key_only(omaq_rate *r, const char *key, int64_t now);

#endif
