#ifndef OMAQ_SOUND_H
#define OMAQ_SOUND_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_SOUND_MAX 16
#define OMAQ_SOUND_ID_HEX 32
#define OMAQ_SOUND_LABEL_MAX 96
#define OMAQ_SOUND_PATH_MAX 768
#define OMAQ_SOUND_FILE_MAX (8u * 1024u * 1024u)

typedef struct {
	char id[OMAQ_SOUND_ID_HEX + 1];
	char label[OMAQ_SOUND_LABEL_MAX + 1];
	char path[OMAQ_SOUND_PATH_MAX];
	uint64_t size;
} omaq_sound;

/* Custom sounds are private, bounded copies below OMAQ_HOME/custom-sounds. */
int omaq_sound_list(const char *home, omaq_sound *out, int capacity);
int omaq_sound_import(const char *home, const char *source, omaq_sound *out);
int omaq_sound_remove(const char *home, const char *id);

#endif
