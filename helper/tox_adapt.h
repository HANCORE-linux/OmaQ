#ifndef OMAQ_TOX_ADAPT_H
#define OMAQ_TOX_ADAPT_H

#ifdef HAVE_TOX

#include <stddef.h>
#include <stdint.h>

struct omaq_tox;

struct omaq_tox *omaq_tox_open(const char *home);
void omaq_tox_close(struct omaq_tox *t);
void omaq_tox_iterate(struct omaq_tox *t);
uint32_t omaq_tox_interval_ms(const struct omaq_tox *t);
int omaq_tox_self_addr_hex(struct omaq_tox *t, char *hex76);
int omaq_tox_friend_add(struct omaq_tox *t, const char *addr_hex, const char *msg);
int omaq_tox_friend_accept(struct omaq_tox *t, const uint8_t *pk32);
int omaq_tox_send(struct omaq_tox *t, uint32_t friend_number, const char *text);
void omaq_tox_save(struct omaq_tox *t);
int omaq_tox_online(const struct omaq_tox *t);

typedef void (*omaq_on_request)(void *ud, const uint8_t *pk32, const char *msg);
typedef void (*omaq_on_message)(void *ud, uint32_t friend, const char *text);
void omaq_tox_set_hooks(struct omaq_tox *t, omaq_on_request req, omaq_on_message msg, void *ud);

#endif /* HAVE_TOX */
#endif
