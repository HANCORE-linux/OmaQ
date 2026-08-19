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
