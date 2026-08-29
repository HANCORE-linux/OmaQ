#ifndef OMAQ_SURFACE_H
#define OMAQ_SURFACE_H

#include <stddef.h>

#define OMAQ_SURFACE_MAX 32

typedef struct {
	char conversation[80];
	char monitor[64];
	int x, y;
	int width, height;
	int pinned;
} omaq_surface;

/* Persist only canonical d:/g: ids. Numeric direct ids are legacy input only. */
int omaq_surface_legacy_direct_present(const char *state);
int omaq_surface_discard_legacy_direct(const char *state);
int omaq_surface_set(const char *state, const omaq_surface *s);
int omaq_surface_get(const char *state, const char *conv, omaq_surface *s);
int omaq_surface_list(const char *state, omaq_surface *out, int cap);

#endif
