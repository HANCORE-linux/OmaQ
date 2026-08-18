#ifndef OMAQ_SURFACE_H
#define OMAQ_SURFACE_H

#include <stddef.h>

#define OMAQ_SURFACE_MAX 32

typedef struct {
	char conversation[80];
	char monitor[64];
	int x, y;
	int pinned;
} omaq_surface;

/* Read/write $OMAQ_STATE/surfaces.jsonl. Only this module opens that file. */
int omaq_surface_set(const char *state, const omaq_surface *s);
int omaq_surface_get(const char *state, const char *conv, omaq_surface *s);

#endif
