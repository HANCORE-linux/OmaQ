#ifndef OMAQ_IDENTITY_H
#define OMAQ_IDENTITY_H

#define OMAQ_ID_LOCKED 1
#define OMAQ_IDENTITY_FILE_MAX (16u * 1024u * 1024u)

int omaq_identity_pass_ok(const char *pass);
int omaq_identity_new_pass_ok(const char *pass);
int omaq_identity_export(const char *home, const char *path);
int omaq_identity_export_exclusive(const char *home, const char *path);
int omaq_identity_import(const char *home, const char *path, int replace);
int omaq_identity_bundle_export(const char *home, const char *path);
int omaq_identity_bundle_snapshot(const char *source, const char *destination);
int omaq_identity_bundle_import(const char *home, const char *path, int replace);

#ifdef HAVE_TOX
#include "tox_adapt.h"
struct omaq_tox *omaq_identity_load(const char *home, const char *pass, int *err);
int omaq_identity_protect(struct omaq_tox *t, const char *pass);
int omaq_identity_unprotect(struct omaq_tox *t, const char *pass);
int omaq_identity_protected(const struct omaq_tox *t);
#endif

#endif
