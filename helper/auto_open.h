#ifndef OMAQ_AUTO_OPEN_H
#define OMAQ_AUTO_OPEN_H

#include <stddef.h>

#define OMAQ_AUTO_OPEN_MAX 128
#define OMAQ_AUTO_OPEN_ID_MAX 67

typedef struct {
	char conversation[OMAQ_AUTO_OPEN_ID_MAX];
	int enabled;
} omaq_auto_open_entry;

typedef struct {
	omaq_auto_open_entry entries[OMAQ_AUTO_OPEN_MAX];
	size_t count;
	int direct_default;
} omaq_auto_open_state;

typedef enum {
	OMAQ_AUTO_OPEN_SOURCE_NONE = 0,
	OMAQ_AUTO_OPEN_SOURCE_CURRENT = 1,
	OMAQ_AUTO_OPEN_SOURCE_LEGACY_ACTIVE = 2,
	OMAQ_AUTO_OPEN_SOURCE_LEGACY_GLOBAL = 3
} omaq_auto_open_source;

void omaq_auto_open_init(omaq_auto_open_state *state);
int omaq_auto_open_load(const char *state_dir, const char *fingerprint,
			omaq_auto_open_state *state, omaq_auto_open_source *source);
int omaq_auto_open_save(const char *state_dir, const char *fingerprint,
			const omaq_auto_open_state *state);
int omaq_auto_open_set(omaq_auto_open_state *state, const char *conversation,
		       int enabled);
int omaq_auto_open_source_name(const char *fingerprint, omaq_auto_open_source source,
			       char *out, size_t out_size);
int omaq_auto_open_retire_global(const char *state_dir, const char *fingerprint);

#endif
