#include "group.h"

#include <stdio.h>
#include <string.h>

#define TABS OMAQ_GROUPS_MAX

typedef struct {
	uint32_t peer;
	char key[OMAQ_GROUP_MEMBER_KEY_HEX + 1];
	char name[OMAQ_GROUP_MEMBER_NAME_MAX + 1];
	omaq_role role;
	int online;
	int self;
} omaq_group_member;

static struct {
	int used;
	uint32_t gnum;
	int dissolved;
	int limit;
	char id[OMAQ_GROUP_ID_MAX];
	char title[OMAQ_GROUP_TITLE_MAX + 1];
	omaq_group_member members[OMAQ_GROUP_PEERS];
	int npeers;
} tabs[TABS];
static int group_save_error;

void omaq_group_reset(void)
{
	memset(tabs, 0, sizeof(tabs));
	group_save_error = 0;
}

int omaq_group_take_save_error(void)
{
	int error = group_save_error;
	group_save_error = 0;
	return error;
}

static int utf8_text_ok(const char *value, size_t len, size_t max_len,
			int reject_controls)
{
	const unsigned char *text = (const unsigned char *)value;
	size_t i = 0;

	if (!value || len == 0 || len > max_len)
		return 0;
	while (i < len) {
		unsigned char c = text[i++];
		if (c == 0 || (reject_controls && (c < 0x20 || c == 0x7f)))
			return 0;
		if (c < 0x80)
			continue;
		if (c >= 0xc2 && c <= 0xdf) {
			if (i >= len || text[i] < 0x80 || text[i] > 0xbf ||
			    (reject_controls && c == 0xc2 && text[i] <= 0x9f))
				return 0;
			i++;
			continue;
		}
		if (c >= 0xe0 && c <= 0xef) {
			if (i + 1 >= len || text[i] < 0x80 || text[i] > 0xbf ||
			    text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
			    (c == 0xe0 && text[i] < 0xa0) ||
			    (c == 0xed && text[i] > 0x9f))
				return 0;
			i += 2;
			continue;
		}
		if (c >= 0xf0 && c <= 0xf4) {
			if (i + 2 >= len || text[i] < 0x80 || text[i] > 0xbf ||
			    text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
			    text[i + 2] < 0x80 || text[i + 2] > 0xbf ||
			    (c == 0xf0 && text[i] < 0x90) ||
			    (c == 0xf4 && text[i] > 0x8f))
				return 0;
			i += 3;
			continue;
		}
		return 0;
	}
	return 1;
}

int omaq_group_title_bytes_ok(const char *title, size_t len)
{
	return utf8_text_ok(title, len, OMAQ_GROUP_TITLE_MAX, 1);
}

int omaq_group_member_name_bytes_ok(const char *name, size_t len)
{
	return utf8_text_ok(name, len, OMAQ_GROUP_MEMBER_NAME_MAX, 1);
}

int omaq_group_message_bytes_ok(const uint8_t *message, size_t len)
{
	return utf8_text_ok((const char *)message, len, OMAQ_GROUP_MESSAGE_MAX, 0);
}

int omaq_group_title_ok(const char *title)
{
	return title ? omaq_group_title_bytes_ok(title, strlen(title)) : 0;
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
		if (tabs[i].gnum == gnum) {
			if (!tabs[i].dissolved || !create)
				return i;
			free_i = i;
			continue;
		}
		if (tabs[i].dissolved && free_i < 0)
			free_i = i;
	}
	if (!create || free_i < 0)
		return -1;
	memset(&tabs[free_i], 0, sizeof(tabs[free_i]));
	tabs[free_i].used = 1;
	tabs[free_i].gnum = gnum;
	return free_i;
}

int omaq_group_can_create(void)
{
	for (int i = 0; i < TABS; i++)
		if (!tabs[i].used || tabs[i].dissolved)
			return 1;
	return 0;
}

int omaq_group_id_format(uint32_t n, char *out, size_t nout)
{
	int i = tab_i(n, 0);

	if (i < 0 || !tabs[i].id[0] || !out ||
	    snprintf(out, nout, "%s", tabs[i].id) >= (int)nout)
		return -1;
	return 0;
}

int omaq_group_id_parse(const char *id, uint32_t *n)
{
	if (!id || !n || strlen(id) != OMAQ_GROUP_ID_MAX - 1 ||
	    id[0] != 'g' || id[1] != ':')
		return -1;
	for (int i = 0; i < TABS; i++) {
		if (tabs[i].used && !tabs[i].dissolved && strcmp(tabs[i].id, id) == 0) {
			*n = tabs[i].gnum;
			return 0;
		}
	}
	return -1;
}

int omaq_group_set_chat_id(uint32_t gnum, const char *chat_id)
{
	int i;

	if (!chat_id || strlen(chat_id) != 64)
		return -1;
	for (int k = 0; k < 64; k++)
		if (!((chat_id[k] >= '0' && chat_id[k] <= '9') ||
		      (chat_id[k] >= 'a' && chat_id[k] <= 'f')))
			return -1;
	for (int k = 0; k < TABS; k++)
		if (tabs[k].used && !tabs[k].dissolved && tabs[k].gnum != gnum &&
		    tabs[k].id[0] && strcmp(tabs[k].id + 2, chat_id) == 0)
			return -1;
	i = tab_i(gnum, 1);
	if (i >= 0 && tabs[i].id[0] && strcmp(tabs[i].id + 2, chat_id) != 0) {
		memset(&tabs[i], 0, sizeof(tabs[i]));
		tabs[i].used = 1;
		tabs[i].gnum = gnum;
	}
	if (i < 0 || snprintf(tabs[i].id, sizeof(tabs[i].id), "g:%s", chat_id) >=
	    (int)sizeof(tabs[i].id))
		return -1;
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
	(void)omaq_group_note_member(gnum, peer, "", "", ROLE_MEMBER, 1, 0);
}

int omaq_group_note_member(uint32_t gnum, uint32_t peer, const char *key,
			   const char *name, omaq_role role, int online, int self)
{
	int i = tab_i(gnum, 1);
	int k;
	size_t key_len = key ? strlen(key) : 0;
	size_t name_len = name ? strlen(name) : 0;

	if (i < 0 || key_len > OMAQ_GROUP_MEMBER_KEY_HEX ||
	    name_len > OMAQ_GROUP_MEMBER_NAME_MAX ||
	    (name_len > 0 && !omaq_group_member_name_bytes_ok(name, name_len)))
		return -1;
	for (k = 0; k < tabs[i].npeers; k++) {
		if ((key_len > 0 && strcmp(tabs[i].members[k].key, key) == 0) ||
		    (key_len == 0 && tabs[i].members[k].peer == peer))
			break;
	}
	if (k == tabs[i].npeers) {
		if (tabs[i].npeers >= OMAQ_GROUP_PEERS)
			return -1;
		memset(&tabs[i].members[k], 0, sizeof(tabs[i].members[k]));
		tabs[i].npeers++;
	}
	tabs[i].members[k].peer = peer;
	if (key_len > 0)
		memcpy(tabs[i].members[k].key, key, key_len + 1);
	if (name_len > 0)
		memcpy(tabs[i].members[k].name, name, name_len + 1);
	tabs[i].members[k].role = role;
	tabs[i].members[k].online = online ? 1 : 0;
	tabs[i].members[k].self = self ? 1 : 0;
	return 0;
}

int omaq_group_set_title(uint32_t gnum, const char *title)
{
	int i = tab_i(gnum, 1);
	size_t len = title ? strlen(title) : 0;

	if (i < 0 || !omaq_group_title_ok(title))
		return -1;
	memcpy(tabs[i].title, title, len + 1);
	return 0;
}

void omaq_group_set_limit(uint32_t gnum, int limit)
{
	int i = tab_i(gnum, 1);

	if (i >= 0 && limit > 0 && limit <= OMAQ_GROUP_PEERS)
		tabs[i].limit = limit;
}

int omaq_group_limit(uint32_t gnum)
{
	int i = tab_i(gnum, 0);
	return i < 0 ? 0 : tabs[i].limit;
}

void omaq_group_drop_peer(uint32_t gnum, uint32_t peer)
{
	int i = tab_i(gnum, 0);
	int k;

	if (i < 0)
		return;
	for (k = 0; k < tabs[i].npeers; k++) {
		if (tabs[i].members[k].peer != peer)
			continue;
		tabs[i].members[k] = tabs[i].members[tabs[i].npeers - 1];
		memset(&tabs[i].members[tabs[i].npeers - 1], 0,
		       sizeof(tabs[i].members[tabs[i].npeers - 1]));
		tabs[i].npeers--;
		return;
	}
}

void omaq_group_mark_peer_offline(uint32_t gnum, uint32_t peer)
{
	int i = tab_i(gnum, 0);

	if (i < 0)
		return;
	for (int k = 0; k < tabs[i].npeers; k++) {
		if (tabs[i].members[k].peer == peer) {
			tabs[i].members[k].online = 0;
			return;
		}
	}
}

int omaq_group_count(void)
{
	int count = 0;

	for (int i = 0; i < TABS; i++)
		if (tabs[i].used && !tabs[i].dissolved)
			count++;
	return count;
}

uint32_t omaq_group_number_at(int index)
{
	for (int i = 0; i < TABS; i++) {
		if (!tabs[i].used || tabs[i].dissolved)
			continue;
		if (index-- == 0)
			return tabs[i].gnum;
	}
	return UINT32_MAX;
}

const char *omaq_group_title(uint32_t gnum)
{
	int i = tab_i(gnum, 0);
	return i < 0 ? "" : tabs[i].title;
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
	return tabs[t].members[i].peer;
}

const char *omaq_group_peer_key(uint32_t gnum, int i)
{
	int t = tab_i(gnum, 0);
	return t < 0 || i < 0 || i >= tabs[t].npeers ? "" : tabs[t].members[i].key;
}

const char *omaq_group_peer_name(uint32_t gnum, int i)
{
	int t = tab_i(gnum, 0);
	return t < 0 || i < 0 || i >= tabs[t].npeers ? "" : tabs[t].members[i].name;
}

int omaq_group_peer_for_key(uint32_t gnum, const char *member_key,
			    uint32_t *peer)
{
	int t = tab_i(gnum, 0);

	if (t < 0 || !member_key || strlen(member_key) != OMAQ_GROUP_MEMBER_KEY_HEX ||
	    !peer)
		return -1;
	for (int i = 0; i < tabs[t].npeers; i++) {
		if (strcmp(tabs[t].members[i].key, member_key) == 0) {
			*peer = tabs[t].members[i].peer;
			return 0;
		}
	}
	return -1;
}

omaq_role omaq_group_peer_cached_role(uint32_t gnum, int i)
{
	int t = tab_i(gnum, 0);
	return t < 0 || i < 0 || i >= tabs[t].npeers ? ROLE_MEMBER : tabs[t].members[i].role;
}

int omaq_group_peer_online(uint32_t gnum, int i)
{
	int t = tab_i(gnum, 0);
	return t >= 0 && i >= 0 && i < tabs[t].npeers && tabs[t].members[i].online;
}

int omaq_group_peer_self(uint32_t gnum, int i)
{
	int t = tab_i(gnum, 0);
	return t >= 0 && i >= 0 && i < tabs[t].npeers && tabs[t].members[i].self;
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

int omaq_group_refresh_id(struct omaq_tox *t, uint32_t gnum, char *out, size_t n)
{
	char chat_id[65];

	if (omaq_tox_group_chat_id_hex(t, gnum, chat_id, sizeof(chat_id)) != 0 ||
	    omaq_group_set_chat_id(gnum, chat_id) != 0)
		return -1;
	return omaq_group_id_format(gnum, out, n);
}

int omaq_group_refresh_member(struct omaq_tox *t, uint32_t gnum, uint32_t peer)
{
	char key[OMAQ_GROUP_MEMBER_KEY_HEX + 1];
	char name[OMAQ_GROUP_MEMBER_NAME_MAX + 1];
	size_t name_len = 0;
	int role, online, self;

	if (omaq_tox_group_peer_info(t, gnum, peer, key, sizeof(key),
				     name, sizeof(name), &name_len, &role, &online,
				     &self) != 0)
		return -1;
	if (!omaq_group_member_name_bytes_ok(name, name_len))
		memcpy(name, "Member", 7);
	return omaq_group_note_member(gnum, peer, key, name, (omaq_role)role,
				      online, self);
}

int omaq_group_refresh_title(struct omaq_tox *t, uint32_t gnum)
{
	char title[OMAQ_GROUP_TITLE_MAX + 1];
	size_t len = 0;

	if (omaq_tox_group_name(t, gnum, title, sizeof(title), &len) != 0 ||
	    !omaq_group_title_bytes_ok(title, len))
		return -1;
	return omaq_group_set_title(gnum, title);
}

int omaq_group_validate_limit(struct omaq_tox *t, uint32_t gnum)
{
	uint16_t limit;

	if (omaq_tox_group_peer_limit(t, gnum, &limit) != 0)
		return -1;
	if (limit > OMAQ_GROUP_PEERS) {
		int role;
		if (omaq_tox_group_self_role(t, gnum, &role) != 0 ||
		    (omaq_role)role != ROLE_OWNER ||
		    omaq_tox_group_set_peer_limit(t, gnum, OMAQ_GROUP_PEERS) != 0 ||
		    omaq_tox_group_peer_limit(t, gnum, &limit) != 0 ||
		    limit > OMAQ_GROUP_PEERS)
			return -1;
	}
	omaq_group_set_limit(gnum, (int)limit);
	return 0;
}

static int cleanup_new_group(struct omaq_tox *t, uint32_t group)
{
	int rc = omaq_tox_group_leave(t, group);

	if (rc < 0)
		return -1;
	if (rc > 0)
		group_save_error = 1;
	omaq_group_mark_dissolved(group);
	return 0;
}

int omaq_group_create(struct omaq_tox *t, const char *title, char *id_out, size_t n,
		      uint32_t *group_out)
{
	uint32_t gnum;

	if (id_out && n)
		id_out[0] = '\0';
	if (group_out)
		*group_out = UINT32_MAX;
	if (!title || !title[0])
		title = "group";
	if (!omaq_group_title_ok(title) || !omaq_group_can_create())
		return -1;
	if (omaq_tox_group_new(t, title, &gnum) != 0)
		return -1;
	if (group_out)
		*group_out = gnum;
	if (omaq_group_refresh_id(t, gnum, id_out, n) != 0)
		return cleanup_new_group(t, gnum) == 0 ? -1 : -3;
	if (omaq_group_set_title(gnum, title) != 0)
		return cleanup_new_group(t, gnum) == 0 ? -1 : -2;
	if (omaq_tox_group_set_peer_limit(t, gnum, OMAQ_GROUP_PEERS) != 0)
		return cleanup_new_group(t, gnum) == 0 ? -1 : -2;
	omaq_group_set_limit(gnum, OMAQ_GROUP_PEERS);
	{
		uint32_t selfp = UINT32_MAX;
		if (omaq_tox_group_self_peer(t, gnum, &selfp) == 0)
			(void)omaq_group_refresh_member(t, gnum, selfp);
	}
	return 0;
}

int omaq_group_invite_friend(struct omaq_tox *t, const char *gid, uint32_t friend,
			     omaq_role self, omaq_role granted)
{
	uint32_t gnum;

	if (omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	if (omaq_group_is_dissolved(gnum) || omaq_group_limit(gnum) <= 0 ||
	    omaq_group_peer_count(gnum) >= omaq_group_limit(gnum))
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
	{
		int rc = omaq_tox_group_leave(t, gnum);
		if (rc < 0)
			return -1;
		if (rc > 0)
			group_save_error = 1;
	}
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
	{
		int rc = omaq_tox_group_leave(t, gnum);
		if (rc < 0)
			return -1;
		if (rc > 0)
			group_save_error = 1;
	}
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

int omaq_group_resolve_member(struct omaq_tox *t, const char *gid,
			      const char *member_key, uint32_t *peer,
			      omaq_role *role)
{
	uint32_t gnum;

	if (!member_key || strlen(member_key) != OMAQ_GROUP_MEMBER_KEY_HEX ||
	    !peer || !role || omaq_group_id_parse(gid, &gnum) != 0)
		return -1;
	for (size_t i = 0; i < OMAQ_GROUP_MEMBER_KEY_HEX; i++)
		if (!((member_key[i] >= '0' && member_key[i] <= '9') ||
		      (member_key[i] >= 'a' && member_key[i] <= 'f')))
			return -1;
	{
		char key[OMAQ_GROUP_MEMBER_KEY_HEX + 1];
		char name[OMAQ_GROUP_MEMBER_NAME_MAX + 1];
		uint32_t candidate;
		size_t name_len = 0;
		int current_role, online, self;

		if (omaq_group_peer_for_key(gnum, member_key, &candidate) != 0 ||
		    omaq_tox_group_peer_info(t, gnum, candidate, key, sizeof(key),
					 name, sizeof(name), &name_len, &current_role,
					 &online, &self) != 0 || self ||
		    strcmp(key, member_key) != 0)
			return -1;
		if (!omaq_group_member_name_bytes_ok(name, name_len))
			memcpy(name, "Member", 7);
		if (omaq_group_note_member(gnum, candidate, key, name,
					   (omaq_role)current_role, online, self) != 0)
			return -1;
		*peer = candidate;
		*role = (omaq_role)current_role;
		return 0;
	}
}

#endif /* HAVE_TOX */
