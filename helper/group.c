#include "group.h"

#include <stdio.h>
#include <string.h>

#define TABS 8

static struct {
	int used;
	uint32_t gnum;
	int dissolved;
	uint32_t peers[OMAQ_GROUP_PEERS];
	int npeers;
} tabs[TABS];

void omaq_group_reset(void)
{
	memset(tabs, 0, sizeof(tabs));
}

static int tab_i(uint32_t gnum, int create)
{
	int i;
	int free_i = -1;

	for (i = 0; i < TABS; i++) {
		if (!tabs[i].used) {
			if (free_i < 0)
				free_i = i;
			continue;
		}
		if (tabs[i].gnum == gnum)
			return i;
	}
	if (!create || free_i < 0)
		return -1;
	memset(&tabs[free_i], 0, sizeof(tabs[free_i]));
	tabs[free_i].used = 1;
	tabs[free_i].gnum = gnum;
	return free_i;
}

int omaq_group_id_format(uint32_t n, char *out, size_t nout)
{
	int wr;

	if (!out || nout < 3)
		return -1;
	wr = snprintf(out, nout, "g%u", n);
	if (wr < 0 || (size_t)wr >= nout)
		return -1;
	return 0;
}

int omaq_group_id_parse(const char *id, uint32_t *n)
{
	unsigned long v;
	char extra;

	if (!id || !n || id[0] != 'g')
		return -1;
	if (id[1] < '0' || id[1] > '9')
		return -1;
	if (sscanf(id + 1, "%lu%c", &v, &extra) != 1)
		return -1;
	if (v > 0xffffffffUL)
		return -1;
	*n = (uint32_t)v;
	return 0;
}

int omaq_group_dissolve_plan(omaq_role self, const omaq_role *roles, int n,
			     int *kick_idx, int *nkick)
{
	int i;

	if (!roles || !kick_idx || !nkick || n < 0)
		return -1;
	*nkick = 0;
	if (!omaq_role_may(self, ACT_DISSOLVE, ROLE_MEMBER))
		return -1;
	for (i = 0; i < n; i++) {
		if (omaq_role_may(self, ACT_KICK, roles[i]))
			kick_idx[(*nkick)++] = i;
	}
	return 0;
}

void omaq_group_note_peer(uint32_t gnum, uint32_t peer)
{
	int i = tab_i(gnum, 1);
	int k;

	if (i < 0)
		return;
	for (k = 0; k < tabs[i].npeers; k++) {
		if (tabs[i].peers[k] == peer)
			return;
	}
	if (tabs[i].npeers >= OMAQ_GROUP_PEERS)
		return;
	tabs[i].peers[tabs[i].npeers++] = peer;
}

void omaq_group_drop_peer(uint32_t gnum, uint32_t peer)
{
	int i = tab_i(gnum, 0);
	int k;

	if (i < 0)
		return;
	for (k = 0; k < tabs[i].npeers; k++) {
		if (tabs[i].peers[k] != peer)
			continue;
		tabs[i].peers[k] = tabs[i].peers[tabs[i].npeers - 1];
		tabs[i].npeers--;
		return;
	}
}

int omaq_group_peer_count(uint32_t gnum)
{
	int i = tab_i(gnum, 0);
	return i < 0 ? 0 : tabs[i].npeers;
}

uint32_t omaq_group_peer_at(uint32_t gnum, int i)
{
	int t = tab_i(gnum, 0);
	if (t < 0 || i < 0 || i >= tabs[t].npeers)
		return UINT32_MAX;
	return tabs[t].peers[i];
}

void omaq_group_mark_dissolved(uint32_t gnum)
{
	int i = tab_i(gnum, 1);
	if (i >= 0)
		tabs[i].dissolved = 1;
}

int omaq_group_is_dissolved(uint32_t gnum)
{
	int i = tab_i(gnum, 0);
	return i >= 0 && tabs[i].dissolved;
}

#ifdef HAVE_TOX

int omaq_group_create(struct omaq_tox *t, const char *title, char *id_out, size_t n)
{
	uint32_t gnum;

	if (!title || !title[0])
		title = "group";
	if (omaq_tox_group_new(t, title, &gnum) != 0)
		return -1;
	{
		uint32_t selfp = UINT32_MAX;
		if (omaq_tox_group_self_peer(t, gnum, &selfp) == 0)
			omaq_group_note_peer(gnum, selfp);
	}
	return omaq_group_id_format(gnum, id_out, n);
}

int omaq_group_invite_friend(struct omaq_tox *t, const char *gid, uint32_t friend,
			     omaq_role self, omaq_role granted)
{
	uint32_t gnum;

	if (omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	if (omaq_group_is_dissolved(gnum))
		return -1;
	if (!omaq_role_may(self, ACT_INVITE, granted))
		return -1;
	return omaq_tox_group_invite_friend(t, gnum, friend);
}

int omaq_group_set_role(struct omaq_tox *t, const char *gid, uint32_t peer,
			omaq_role self, omaq_role next)
{
	uint32_t gnum;

	if (omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	if (!omaq_role_may(self, ACT_SET_ROLE, next))
		return -1;
	return omaq_tox_group_set_role(t, gnum, peer, next);
}

int omaq_group_kick(struct omaq_tox *t, const char *gid, uint32_t peer,
		    omaq_role self, omaq_role victim)
{
	uint32_t gnum;

	if (omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	if (!omaq_role_may(self, ACT_KICK, victim))
		return -1;
	if (omaq_tox_group_kick(t, gnum, peer) != 0)
		return -1;
	omaq_group_drop_peer(gnum, peer);
	return 0;
}

int omaq_group_leave(struct omaq_tox *t, const char *gid)
{
	uint32_t gnum;

	if (omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	if (omaq_tox_group_leave(t, gnum) != 0)
		return -1;
	omaq_group_mark_dissolved(gnum);
	return 0;
}

int omaq_group_dissolve(struct omaq_tox *t, const char *gid, omaq_role self)
{
	uint32_t gnum;
	omaq_role roles[OMAQ_GROUP_PEERS];
	int kick[OMAQ_GROUP_PEERS];
	int nkick = 0;
	int n;
	int i;
	uint32_t self_peer = UINT32_MAX;

	if (omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	if (!omaq_role_may(self, ACT_DISSOLVE, ROLE_MEMBER))
		return -1;
	n = omaq_group_peer_count(gnum);
	(void)omaq_tox_group_self_peer(t, gnum, &self_peer);
	for (i = 0; i < n; i++) {
		uint32_t p = omaq_group_peer_at(gnum, i);
		omaq_role r = ROLE_MEMBER;
		if (p == self_peer) {
			roles[i] = ROLE_OWNER;
			continue;
		}
		{
			int tr = 0;
			if (omaq_tox_group_peer_role(t, gnum, p, &tr) != 0)
				r = ROLE_MEMBER;
			else
				r = (omaq_role)tr;
		}
		roles[i] = r;
	}
	if (omaq_group_dissolve_plan(self, roles, n, kick, &nkick) != 0)
		return -1;
	for (i = 0; i < nkick; i++) {
		uint32_t p = omaq_group_peer_at(gnum, kick[i]);
		if (p != UINT32_MAX)
			(void)omaq_tox_group_kick(t, gnum, p);
	}
	if (omaq_tox_group_leave(t, gnum) != 0)
		return -1;
	omaq_group_mark_dissolved(gnum);
	return 0;
}

int omaq_group_send(struct omaq_tox *t, const char *gid, const char *text)
{
	uint32_t gnum;

	if (omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	if (omaq_group_is_dissolved(gnum))
		return -1;
	return omaq_tox_group_send(t, gnum, text);
}

int omaq_group_self_role(struct omaq_tox *t, const char *gid, omaq_role *out)
{
	uint32_t gnum;

	if (omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	{
		int tr = 0;
		if (omaq_tox_group_self_role(t, gnum, &tr) != 0)
			return -1;
		*out = (omaq_role)tr;
		return 0;
	}
}

#endif /* HAVE_TOX */
