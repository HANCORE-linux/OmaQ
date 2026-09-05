#ifndef OMAQ_TOX_ADAPT_H
#define OMAQ_TOX_ADAPT_H

#ifdef HAVE_TOX

#include <stddef.h>
#include <stdint.h>

struct omaq_tox;

#define OMAQ_TOX_ADD_REJECTED (-2)
#define OMAQ_TOX_ADD_STATE_FAILED (-3)

#define OMAQ_NICKNAME_MAX_CHARS 18

#define OMAQ_TOX_LOCKED 1

struct omaq_tox *omaq_tox_open(const char *home, const char *pass, int *err);
int omaq_tox_protect(struct omaq_tox *t, const char *pass);
int omaq_tox_unprotect(struct omaq_tox *t, const char *pass);
int omaq_tox_protected(const struct omaq_tox *t);
int omaq_tox_enable_recovery(struct omaq_tox *t, const char *state,
                             int preserve_primary_warning);
int omaq_tox_primary_acknowledged(struct omaq_tox *t);
int omaq_tox_recovery_degraded(const struct omaq_tox *t);
int omaq_tox_primary_uncertain(const struct omaq_tox *t);
#ifdef OMAQ_TOX_TEST
void omaq_tox_test_fail_primary_fsync(struct omaq_tox *t);
void omaq_tox_test_fail_before_primary(struct omaq_tox *t);
#endif
void omaq_tox_close(struct omaq_tox *t);
void omaq_tox_iterate(struct omaq_tox *t);
uint32_t omaq_tox_interval_ms(const struct omaq_tox *t);
int omaq_tox_self_addr_hex(struct omaq_tox *t, char *hex76);
int omaq_tox_self_pk_hex(struct omaq_tox *t, char *hex64);
int omaq_tox_self_name(struct omaq_tox *t, char *out, size_t n);
int omaq_tox_set_name(struct omaq_tox *t, const char *name);
int omaq_tox_friend_pk_hex(struct omaq_tox *t, uint32_t friend_number, char *hex64);
int omaq_tox_friend_add(struct omaq_tox *t, const char *addr_hex, const char *msg, uint32_t *fn_out);
void omaq_tox_discard(struct omaq_tox *t);
int omaq_tox_friend_accept(struct omaq_tox *t, const uint8_t *pk32);
int omaq_tox_friend_delete(struct omaq_tox *t, uint32_t friend_number);
uint32_t omaq_tox_friend_by_pk(struct omaq_tox *t, const uint8_t *pk32);
int omaq_tox_friend_count(struct omaq_tox *t, size_t *count);
int omaq_tox_friend_list(struct omaq_tox *t, uint32_t *out, size_t max);
int omaq_tox_friend_name(struct omaq_tox *t, uint32_t friend_number, char *out, size_t n);
int omaq_tox_friend_online(struct omaq_tox *t, uint32_t friend_number);
int omaq_tox_friend_status(struct omaq_tox *t, uint32_t friend_number);
int omaq_tox_nospam_rotate(struct omaq_tox *t);
int omaq_tox_send(struct omaq_tox *t, uint32_t friend_number, const char *text);
int omaq_tox_save(struct omaq_tox *t);
int omaq_tox_online(const struct omaq_tox *t);

typedef void (*omaq_on_request)(void *ud, const uint8_t *pk32, const char *msg);
typedef void (*omaq_on_presence)(void *ud, uint32_t friend, int online);
typedef void (*omaq_on_typing)(void *ud, uint32_t friend, int typing);
typedef void (*omaq_on_message)(void *ud, uint32_t friend, const char *text);
typedef void (*omaq_on_group_invite)(void *ud, uint32_t friend, const uint8_t *data, size_t len);
typedef void (*omaq_on_group_message)(void *ud, uint32_t gnum, uint32_t peer,
				      const uint8_t *message, size_t length);
typedef void (*omaq_on_group_peer)(void *ud, uint32_t gnum, uint32_t peer,
				   int joined, int removed);
typedef void (*omaq_on_group_packet)(void *ud, uint32_t gnum, uint32_t peer,
				     const uint8_t *data, size_t length,
				     int private_packet);
void omaq_tox_set_hooks(struct omaq_tox *t, omaq_on_request req, omaq_on_message msg, void *ud);
void omaq_tox_set_presence_hook(struct omaq_tox *t, omaq_on_presence cb, void *ud);
void omaq_tox_set_friend_status_hook(struct omaq_tox *t, omaq_on_presence cb,
				     void *ud);
void omaq_tox_set_typing_hook(struct omaq_tox *t, omaq_on_typing cb, void *ud);
int omaq_tox_set_typing(struct omaq_tox *t, uint32_t friend_number, int typing);
void omaq_tox_set_group_hooks(struct omaq_tox *t, omaq_on_group_invite inv,
			      omaq_on_group_message msg, omaq_on_group_peer peer, void *ud);
void omaq_tox_set_group_packet_hook(struct omaq_tox *t,
				    omaq_on_group_packet packet, void *ud);

int omaq_tox_group_new(struct omaq_tox *t, const char *title, uint32_t *gnum);
/* Returns 0 on success, 1 for a transient send failure, and -1 permanently. */
int omaq_tox_group_invite_friend(struct omaq_tox *t, uint32_t gnum, uint32_t friend);
int omaq_tox_group_invite_accept(struct omaq_tox *t, uint32_t friend,
				 const uint8_t *data, size_t len, uint32_t *gnum);
int omaq_tox_group_set_role(struct omaq_tox *t, uint32_t gnum, uint32_t peer, int omaq_role);
int omaq_tox_group_kick(struct omaq_tox *t, uint32_t gnum, uint32_t peer);
int omaq_tox_group_leave(struct omaq_tox *t, uint32_t gnum);
int omaq_tox_group_send(struct omaq_tox *t, uint32_t gnum, const char *text);
int omaq_tox_group_custom_send(struct omaq_tox *t, uint32_t gnum,
			       const uint8_t *data, size_t length);
int omaq_tox_group_custom_private_send(struct omaq_tox *t, uint32_t gnum,
				       uint32_t peer, const uint8_t *data,
				       size_t length);
int omaq_tox_group_self_role(struct omaq_tox *t, uint32_t gnum, int *omaq_role);
int omaq_tox_group_peer_role(struct omaq_tox *t, uint32_t gnum, uint32_t peer, int *omaq_role);
int omaq_tox_group_self_peer(struct omaq_tox *t, uint32_t gnum, uint32_t *peer);
int omaq_tox_group_set_peer_limit(struct omaq_tox *t, uint32_t gnum, uint16_t limit);
int omaq_tox_group_peer_limit(struct omaq_tox *t, uint32_t gnum, uint16_t *limit);
int omaq_tox_group_count(struct omaq_tox *t, size_t *count);
int omaq_tox_group_numbers(struct omaq_tox *t, uint32_t *groups, size_t max,
			   size_t *count);
int omaq_tox_group_by_chat_id(struct omaq_tox *t, const char *chat_id,
			      uint32_t *gnum);
int omaq_tox_group_registry_proof(struct omaq_tox *t, const char *chat_id,
				  char *out, size_t n);
int omaq_tox_group_chat_id_hex(struct omaq_tox *t, uint32_t gnum, char *out,
				       size_t n);
int omaq_tox_group_name(struct omaq_tox *t, uint32_t gnum, char *out, size_t n,
			size_t *out_len);
int omaq_tox_group_peer_info(struct omaq_tox *t, uint32_t gnum, uint32_t peer,
			     char *key_hex, size_t key_n, char *name, size_t name_n,
			     size_t *name_len, int *role, int *online, int *self);

#define OMAQ_TOX_FILE_RESUME 0
#define OMAQ_TOX_FILE_PAUSE 1
#define OMAQ_TOX_FILE_CANCEL 2

int omaq_tox_file_send(struct omaq_tox *t, uint32_t friend, uint64_t size,
		       const char *name, uint32_t *fnum);
int omaq_tox_hash(const uint8_t *data, size_t n, uint8_t out32[32]);
int omaq_tox_file_send_avatar(struct omaq_tox *t, uint32_t friend, uint64_t size,
			      const uint8_t file_id[32], uint32_t *fnum);
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
typedef void (*omaq_on_avatar)(void *ud, uint32_t friend, uint32_t fnum, uint64_t size);
void omaq_tox_set_file_hooks(struct omaq_tox *t, omaq_on_file_recv recv,
			     omaq_on_file_chunk_req req, omaq_on_file_chunk chunk,
			     omaq_on_file_ctrl ctrl, void *ud);
void omaq_tox_set_avatar_hook(struct omaq_tox *t, omaq_on_avatar cb, void *ud);

int omaq_tox_av_call(struct omaq_tox *t, uint32_t friend);
int omaq_tox_av_answer(struct omaq_tox *t, uint32_t friend);
int omaq_tox_av_hangup(struct omaq_tox *t, uint32_t friend);
int omaq_tox_av_available(const struct omaq_tox *t);
/* Destroying the old session and creating its replacement are separate facts.
 * destroy() returning zero proves that no old ToxAV call context remains. */
int omaq_tox_av_destroy(struct omaq_tox *t);
int omaq_tox_av_create(struct omaq_tox *t);
int omaq_tox_av_reset(struct omaq_tox *t);
int omaq_tox_av_audio_send(struct omaq_tox *t, uint32_t friend,
			   const int16_t *pcm, size_t samples,
			   uint8_t channels, uint32_t rate);

#define OMAQ_TOX_CALL_ENDED 0
#define OMAQ_TOX_CALL_INCOMING 1
#define OMAQ_TOX_CALL_ACTIVE 2

typedef void (*omaq_on_call)(void *ud, uint32_t friend, int state);
typedef void (*omaq_on_audio)(void *ud, uint32_t friend, const int16_t *pcm,
			      size_t samples, uint8_t channels, uint32_t rate);
void omaq_tox_set_call_hook(struct omaq_tox *t, omaq_on_call cb, void *ud);
void omaq_tox_set_audio_hook(struct omaq_tox *t, omaq_on_audio cb, void *ud);

#endif /* HAVE_TOX */
#endif
