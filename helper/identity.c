#ifdef HAVE_TOX

#include "identity.h"

struct omaq_tox *omaq_identity_load(const char *home)
{
	return omaq_tox_open(home);
}

#endif
