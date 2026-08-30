#ifndef OMAQ_ROLES_H
#define OMAQ_ROLES_H

#include <stdbool.h>

typedef enum {
	ROLE_MEMBER = 0,
	ROLE_ADMIN = 1,
	ROLE_OWNER = 2
} omaq_role;

typedef enum {
	ACT_READ = 0,
	ACT_WRITE,
	ACT_LEAVE,
	ACT_INVITE,   /* target = role being granted */
	ACT_SET_ROLE, /* target = new role */
	ACT_KICK,     /* target = current role of victim */
	ACT_DISSOLVE
} omaq_action;

bool omaq_role_valid(omaq_role role);
bool omaq_role_may(omaq_role actor, omaq_action what, omaq_role target);
int omaq_role_parse(const char *s, omaq_role *out);
int omaq_action_parse(const char *s, omaq_action *out);
const char *omaq_role_name(omaq_role r);

#endif
