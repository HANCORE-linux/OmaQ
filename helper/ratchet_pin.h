#ifndef OMAQ_RATCHET_PIN_H
#define OMAQ_RATCHET_PIN_H

#include <stddef.h>

/* Expected direct-chat Ratchet identity pins, persisted per conversation. */
int omaq_ratchet_pin_set(const char *home, const char *conversation, const char *rk);
/* Returns 1 when found, 0 when absent, -1 on invalid/corrupt state. */
int omaq_ratchet_pin_get(const char *home, const char *conversation,
                         char *rk, size_t rk_size);

#endif
