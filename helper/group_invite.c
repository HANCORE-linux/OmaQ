#include "group_invite.h"

#include <stdint.h>

int omaq_group_invite_match(uint32_t expected_friend, uint32_t friend_number,
                            int64_t expiry, int64_t now)
{
	if (expected_friend == UINT32_MAX || expected_friend != friend_number)
		return 0;
	return now < expiry;
}
