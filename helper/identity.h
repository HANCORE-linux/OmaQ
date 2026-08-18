#ifndef OMAQ_IDENTITY_H
#define OMAQ_IDENTITY_H

#ifdef HAVE_TOX

#include "tox_adapt.h"

struct omaq_tox *omaq_identity_load(const char *home);

#endif
#endif
