#ifndef OMAQ_GROUP_INVITE_H
#define OMAQ_GROUP_INVITE_H

#include <stdint.h>

/* A raw NGC invite is admissible only for a redeemed, unexpired token. */
int omaq_group_invite_match(uint32_t expected_friend, uint32_t friend_number,
                            int64_t expiry, int64_t now);

#endif
