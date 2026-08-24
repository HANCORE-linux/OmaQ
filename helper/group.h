#ifndef OMAQ_GROUP_H
#define OMAQ_GROUP_H

#include "roles.h"

#include <stddef.h>
#include <stdint.h>

#define OMAQ_GROUP_ID_MAX 67
#define OMAQ_GROUPS_MAX 8
#define OMAQ_GROUP_PEERS 10
#define OMAQ_GROUP_TITLE_MAX 48
#define OMAQ_GROUP_MEMBER_NAME_MAX 128
#define OMAQ_GROUP_MEMBER_KEY_HEX 64
#define OMAQ_GROUP_MESSAGE_MAX 1399

int omaq_group_title_bytes_ok(const char *title, size_t len);
int omaq_group_member_name_bytes_ok(const char *name, size_t len);
int omaq_group_message_bytes_ok(const uint8_t *message, size_t len);
int omaq_group_title_ok(const char *title);
int omaq_group_id_format(uint32_t n, char *out, size_t nout);
int omaq_group_id_parse(const char *id, uint32_t *n);
int omaq_group_set_chat_id(uint32_t gnum, const char *chat_id);

/* Which peer indices the actor may kick during dissolve. 0 or -1. */
int omaq_group_dissolve_plan(omaq_role self, const omaq_role *roles, int n,
			     int *kick_idx, int *nkick);

void omaq_group_reset(void);
void omaq_group_note_peer(uint32_t gnum, uint32_t peer);
int omaq_group_note_member(uint32_t gnum, uint32_t peer, const char *key,
			   const char *name, omaq_role role, int online, int self);
int omaq_group_can_create(void);
int omaq_group_set_title(uint32_t gnum, const char *title);
void omaq_group_set_limit(uint32_t gnum, int limit);
int omaq_group_limit(uint32_t gnum);
void omaq_group_drop_peer(uint32_t gnum, uint32_t peer);
void omaq_group_mark_peer_offline(uint32_t gnum, uint32_t peer);
int omaq_group_count(void);
uint32_t omaq_group_number_at(int i);
const char *omaq_group_title(uint32_t gnum);
int omaq_group_peer_count(uint32_t gnum);
uint32_t omaq_group_peer_at(uint32_t gnum, int i);
const char *omaq_group_peer_key(uint32_t gnum, int i);
const char *omaq_group_peer_name(uint32_t gnum, int i);
int omaq_group_peer_for_key(uint32_t gnum, const char *member_key,
			    uint32_t *peer);
omaq_role omaq_group_peer_cached_role(uint32_t gnum, int i);
int omaq_group_peer_online(uint32_t gnum, int i);
int omaq_group_peer_self(uint32_t gnum, int i);
void omaq_group_mark_dissolved(uint32_t gnum);
int omaq_group_is_dissolved(uint32_t gnum);
int omaq_group_take_save_error(void);

#ifdef HAVE_TOX
#include "tox_adapt.h"

int omaq_group_create(struct omaq_tox *t, const char *title, char *id_out, size_t n,
		      uint32_t *group_out);
int omaq_group_refresh_id(struct omaq_tox *t, uint32_t gnum, char *out, size_t n);
int omaq_group_refresh_member(struct omaq_tox *t, uint32_t gnum, uint32_t peer);
int omaq_group_refresh_title(struct omaq_tox *t, uint32_t gnum);
int omaq_group_validate_limit(struct omaq_tox *t, uint32_t gnum);
int omaq_group_invite_friend(struct omaq_tox *t, const char *gid, uint32_t friend,
			     omaq_role self, omaq_role granted);
int omaq_group_set_role(struct omaq_tox *t, const char *gid, uint32_t peer,
			omaq_role self, omaq_role next);
int omaq_group_kick(struct omaq_tox *t, const char *gid, uint32_t peer,
		    omaq_role self, omaq_role victim);
int omaq_group_leave(struct omaq_tox *t, const char *gid);
int omaq_group_dissolve(struct omaq_tox *t, const char *gid, omaq_role self);
int omaq_group_send(struct omaq_tox *t, const char *gid, const char *text);
int omaq_group_self_role(struct omaq_tox *t, const char *gid, omaq_role *out);
int omaq_group_resolve_member(struct omaq_tox *t, const char *gid,
			      const char *member_key, uint32_t *peer,
			      omaq_role *role);
#endif

#endif
