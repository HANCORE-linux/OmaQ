#ifndef OMAQ_AVATAR_H
#define OMAQ_AVATAR_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_AVATAR_MAX (512u * 1024u)
#define OMAQ_INLINE_IMAGE_SOURCE_MAX (8u * 1024u * 1024u)
#define OMAQ_AVATAR_ID_MAX 66

int omaq_avatar_id_ok(const char *id);
int omaq_avatar_src_ok(const char *path);
int omaq_avatar_dest(const char *home, const char *id, char *out, size_t n);
int omaq_avatar_is_dest(const char *home, const char *path);
int omaq_avatar_validate_file(const char *path);
int omaq_inline_image_validate_file(const char *path);
int omaq_inline_image_import_file(const char *source, const char *destination);
int omaq_inline_image_canonicalize_file(const char *path);
int omaq_avatar_reconcile(const char *home, const char *id);
int omaq_avatar_cleanup_temps(const char *home);
int omaq_avatar_commit_received(const char *home, const char *id,
				const char *staging, char *dest, size_t destn);
int omaq_avatar_install(const char *home, const char *id, const char *src,
			char *dest, size_t destn);

#endif
