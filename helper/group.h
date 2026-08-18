#ifndef OMAQ_GROUP_H
#define OMAQ_GROUP_H

#include "roles.h"

#include <stddef.h>
#include <stdint.h>

#define OMAQ_GROUP_ID_MAX 16
#define OMAQ_GROUP_PEERS 32

int omaq_group_id_format(uint32_t n, char *out, size_t nout);
int omaq_group_id_parse(const char *id, uint32_t *n);

/* Which peer indices the actor may kick during dissolve. 0 or -1. */
int omaq_group_dissolve_plan(omaq_role self, const omaq_role *roles, int n,
			     int *kick_idx, int *nkick);

void omaq_group_note_peer(uint32_t gnum, uint32_t peer);
void omaq_group_drop_peer(uint32_t gnum, uint32_t peer);
int omaq_group_peer_count(uint32_t gnum);
uint32_t omaq_group_peer_at(uint32_t gnum, int i);
void omaq_group_mark_dissolved(uint32_t gnum);
int omaq_group_is_dissolved(uint32_t gnum);

#ifdef HAVE_TOX
#include "tox_adapt.h"

int omaq_group_create(struct omaq_tox *t, const char *title, char *id_out, size_t n);
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
#endif

#endif
