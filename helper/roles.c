#include "roles.h"

#include <string.h>

const char *omaq_role_name(omaq_role r)
{
	switch (r) {
	case ROLE_MEMBER:
		return "member";
	case ROLE_ADMIN:
		return "admin";
	case ROLE_OWNER:
		return "owner";
	}
	return "";
}

int omaq_role_parse(const char *s, omaq_role *out)
{
	if (!s || !out)
		return -1;
	if (strcmp(s, "member") == 0) {
		*out = ROLE_MEMBER;
		return 0;
	}
	if (strcmp(s, "admin") == 0) {
		*out = ROLE_ADMIN;
		return 0;
	}
	if (strcmp(s, "owner") == 0) {
		*out = ROLE_OWNER;
		return 0;
	}
	return -1;
}

int omaq_action_parse(const char *s, omaq_action *out)
{
	if (!s || !out)
		return -1;
	if (strcmp(s, "read") == 0) {
		*out = ACT_READ;
		return 0;
	}
	if (strcmp(s, "write") == 0) {
		*out = ACT_WRITE;
		return 0;
	}
	if (strcmp(s, "leave") == 0) {
		*out = ACT_LEAVE;
		return 0;
	}
	if (strcmp(s, "invite") == 0) {
		*out = ACT_INVITE;
		return 0;
	}
	if (strcmp(s, "setRole") == 0) {
		*out = ACT_SET_ROLE;
		return 0;
	}
	if (strcmp(s, "kick") == 0) {
		*out = ACT_KICK;
		return 0;
	}
	if (strcmp(s, "dissolve") == 0) {
		*out = ACT_DISSOLVE;
		return 0;
	}
	return -1;
}

bool omaq_role_valid(omaq_role role)
{
	return role == ROLE_MEMBER || role == ROLE_ADMIN || role == ROLE_OWNER;
}

bool omaq_role_may(omaq_role actor, omaq_action what, omaq_role target)
{
	if (!omaq_role_valid(actor) || !omaq_role_valid(target) ||
	    what < ACT_READ || what > ACT_DISSOLVE)
		return false;
	switch (what) {
	case ACT_READ:
	case ACT_WRITE:
	case ACT_LEAVE:
		return true;
	case ACT_INVITE:
		if (actor == ROLE_MEMBER)
			return false;
		return target == ROLE_MEMBER || target == ROLE_ADMIN;
	case ACT_SET_ROLE:
		if (target == ROLE_OWNER)
			return false;
		if (actor == ROLE_OWNER)
			return target == ROLE_MEMBER || target == ROLE_ADMIN;
		if (actor == ROLE_ADMIN)
			return target == ROLE_ADMIN;
		return false;
	case ACT_KICK:
		if (actor == ROLE_OWNER)
			return target != ROLE_OWNER;
		if (actor == ROLE_ADMIN)
			return target == ROLE_MEMBER;
		return false;
	case ACT_DISSOLVE:
		return actor == ROLE_OWNER;
	}
	return false;
}
