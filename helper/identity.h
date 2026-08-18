#ifndef OMAQ_IDENTITY_H
#define OMAQ_IDENTITY_H

#ifdef HAVE_TOX
#include "tox_adapt.h"
struct omaq_tox *omaq_identity_load(const char *home);
#endif

int omaq_identity_export(const char *home, const char *path);
int omaq_identity_import(const char *home, const char *path, int replace);

#endif
