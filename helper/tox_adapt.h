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
int omaq_tox_self_pk_hex(struct omaq_tox *t, char *hex64);
int omaq_tox_friend_pk_hex(struct omaq_tox *t, uint32_t friend_number, char *hex64);
int omaq_tox_friend_add(struct omaq_tox *t, const char *addr_hex, const char *msg);
int omaq_tox_friend_accept(struct omaq_tox *t, const uint8_t *pk32);
int omaq_tox_friend_delete(struct omaq_tox *t, uint32_t friend_number);
uint32_t omaq_tox_friend_by_pk(struct omaq_tox *t, const uint8_t *pk32);
int omaq_tox_nospam_rotate(struct omaq_tox *t);
int omaq_tox_send(struct omaq_tox *t, uint32_t friend_number, const char *text);
void omaq_tox_save(struct omaq_tox *t);
int omaq_tox_online(const struct omaq_tox *t);

typedef void (*omaq_on_request)(void *ud, const uint8_t *pk32, const char *msg);
typedef void (*omaq_on_message)(void *ud, uint32_t friend, const char *text);
typedef void (*omaq_on_group_invite)(void *ud, uint32_t friend, const uint8_t *data, size_t len);
typedef void (*omaq_on_group_message)(void *ud, uint32_t gnum, uint32_t peer, const char *text);
typedef void (*omaq_on_group_peer)(void *ud, uint32_t gnum, uint32_t peer, int joined);
void omaq_tox_set_hooks(struct omaq_tox *t, omaq_on_request req, omaq_on_message msg, void *ud);
void omaq_tox_set_group_hooks(struct omaq_tox *t, omaq_on_group_invite inv,
			      omaq_on_group_message msg, omaq_on_group_peer peer, void *ud);

int omaq_tox_group_new(struct omaq_tox *t, const char *title, uint32_t *gnum);
int omaq_tox_group_invite_friend(struct omaq_tox *t, uint32_t gnum, uint32_t friend);
int omaq_tox_group_invite_accept(struct omaq_tox *t, uint32_t friend,
				 const uint8_t *data, size_t len, uint32_t *gnum);
int omaq_tox_group_set_role(struct omaq_tox *t, uint32_t gnum, uint32_t peer, int omaq_role);
int omaq_tox_group_kick(struct omaq_tox *t, uint32_t gnum, uint32_t peer);
int omaq_tox_group_leave(struct omaq_tox *t, uint32_t gnum);
int omaq_tox_group_send(struct omaq_tox *t, uint32_t gnum, const char *text);
int omaq_tox_group_self_role(struct omaq_tox *t, uint32_t gnum, int *omaq_role);
int omaq_tox_group_peer_role(struct omaq_tox *t, uint32_t gnum, uint32_t peer, int *omaq_role);
int omaq_tox_group_self_peer(struct omaq_tox *t, uint32_t gnum, uint32_t *peer);

#endif /* HAVE_TOX */
#endif
