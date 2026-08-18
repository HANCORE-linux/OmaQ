#include "safety.h"

#include <stdio.h>
#include <string.h>

static int lower_hex64(const char *s, char *out)
{
	int i;

	if (!s || strlen(s) != 64)
		return -1;
	for (i = 0; i < 64; i++) {
		char c = s[i];
		if (c >= 'A' && c <= 'F')
			c = (char)(c - 'A' + 'a');
		if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
			return -1;
		out[i] = c;
	}
	out[64] = '\0';
	return 0;
}

static void group16(const char *hex, char *out)
{
	int i;
	char *p = out;

	for (i = 0; i < 16; i++) {
		if (i)
			*p++ = ' ';
		memcpy(p, hex + (size_t)i * 4, 4);
		p += 4;
	}
	*p = '\0';
}

int omaq_safety_code(const char *pk_a, const char *pk_b, char *out, size_t n)
{
	char a[65], b[65];
	char first[80], second[80];
	int wr;

	if (!out || n < 8)
		return -1;
	if (lower_hex64(pk_a, a) != 0 || lower_hex64(pk_b, b) != 0)
		return -1;
	if (strcmp(a, b) <= 0) {
		group16(a, first);
		group16(b, second);
	} else {
		group16(b, first);
		group16(a, second);
	}
	wr = snprintf(out, n, "%s / %s", first, second);
	if (wr < 0 || (size_t)wr >= n)
		return -1;
	return 0;
}
