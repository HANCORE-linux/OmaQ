#ifndef OMAQ_TOX_ADAPT_H
#define OMAQ_TOX_ADAPT_H

#ifdef HAVE_TOX

#include <stddef.h>
#include <stdint.h>

struct omaq_tox;

#define OMAQ_TOX_LOCKED 1

struct omaq_tox *omaq_tox_open(const char *home, const char *pass, int *err);
int omaq_tox_protect(struct omaq_tox *t, const char *pass);
int omaq_tox_unprotect(struct omaq_tox *t, const char *pass);
int omaq_tox_protected(const struct omaq_tox *t);
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

#define OMAQ_TOX_FILE_RESUME 0
#define OMAQ_TOX_FILE_PAUSE 1
#define OMAQ_TOX_FILE_CANCEL 2

int omaq_tox_file_send(struct omaq_tox *t, uint32_t friend, uint64_t size,
		       const char *name, uint32_t *fnum);
int omaq_tox_file_chunk(struct omaq_tox *t, uint32_t friend, uint32_t fnum,
			uint64_t pos, const uint8_t *data, size_t len);
int omaq_tox_file_control(struct omaq_tox *t, uint32_t friend, uint32_t fnum, int control);

typedef void (*omaq_on_file_recv)(void *ud, uint32_t friend, uint32_t fnum,
				  const char *name, uint64_t size);
typedef void (*omaq_on_file_chunk_req)(void *ud, uint32_t friend, uint32_t fnum,
				       uint64_t pos, size_t len);
typedef void (*omaq_on_file_chunk)(void *ud, uint32_t friend, uint32_t fnum,
				   uint64_t pos, const uint8_t *data, size_t len);
typedef void (*omaq_on_file_ctrl)(void *ud, uint32_t friend, uint32_t fnum, int control);
void omaq_tox_set_file_hooks(struct omaq_tox *t, omaq_on_file_recv recv,
			     omaq_on_file_chunk_req req, omaq_on_file_chunk chunk,
			     omaq_on_file_ctrl ctrl, void *ud);

int omaq_tox_av_call(struct omaq_tox *t, uint32_t friend);
int omaq_tox_av_answer(struct omaq_tox *t, uint32_t friend);
int omaq_tox_av_hangup(struct omaq_tox *t, uint32_t friend);

/* incoming: 1 = ringing, 0 = ended */
typedef void (*omaq_on_call)(void *ud, uint32_t friend, int incoming);
void omaq_tox_set_call_hook(struct omaq_tox *t, omaq_on_call cb, void *ud);

#endif /* HAVE_TOX */
#endif
