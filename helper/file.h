#ifndef OMAQ_FILE_H
#define OMAQ_FILE_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_FILE_MAX (8u * 1024u * 1024u)
#define OMAQ_FILE_NAME_MAX 128
#define OMAQ_FILE_ID_MAX 40

int omaq_file_path_ok(const char *path);
int omaq_file_basename(const char *path, char *out, size_t n);
int omaq_file_id_format(uint32_t friend, uint32_t fnum, char *out, size_t n);
int omaq_file_id_parse(const char *id, uint32_t *friend, uint32_t *fnum);

#ifdef HAVE_TOX
#include "tox_adapt.h"

int omaq_file_offer_store(uint32_t friend, uint32_t fnum, const char *name, uint64_t size);
int omaq_file_offer_lookup(uint32_t friend, uint32_t fnum, char *name, size_t n, uint64_t *size);
void omaq_file_offer_drop(uint32_t friend, uint32_t fnum);

int omaq_file_send_begin(struct omaq_tox *t, uint32_t friend, const char *path, uint32_t *fnum_out);
int omaq_file_send_avatar_begin(struct omaq_tox *t, uint32_t friend, const char *path,
				const uint8_t file_id[32], uint32_t *fnum_out);
int omaq_file_recv_begin(const char *home, const char *conv, uint32_t friend,
			 uint32_t fnum, const char *name, uint64_t size,
			 const char *dest_override, char *dest, size_t destn);
int omaq_file_chunk_out(struct omaq_tox *t, uint32_t friend, uint32_t fnum,
			uint64_t pos, size_t len);
int omaq_file_chunk_in(uint32_t friend, uint32_t fnum, uint64_t pos,
		       const uint8_t *data, size_t len, char *done_path, size_t n);
void omaq_file_cancel(struct omaq_tox *t, uint32_t friend, uint32_t fnum);
#endif

#endif
