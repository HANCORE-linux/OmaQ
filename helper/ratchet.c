#include "ratchet.h"

#include <ctype.h>
#include <string.h>

int omaq_rk_ok(const char *hex64)
{
	size_t i;

	if (!hex64 || strlen(hex64) != OMAQ_RK_HEX)
		return 0;
	for (i = 0; i < OMAQ_RK_HEX; i++) {
		if (!isxdigit((unsigned char)hex64[i]))
			return 0;
	}
	return 1;
}

int omaq_ratchet_peer_ok(const char *peer)
{
	size_t i, length;

	if (!peer || !peer[0])
		return 0;
	length = strlen(peer);
	if (length != OMAQ_RATCHET_PEER_MAX - 1 || peer[0] != 'd' || peer[1] != ':')
		return 0;
	for (i = 2; i < length; i++)
		if (!((peer[i] >= '0' && peer[i] <= '9') ||
		      (peer[i] >= 'a' && peer[i] <= 'f')))
			return 0;
	return 1;
}
