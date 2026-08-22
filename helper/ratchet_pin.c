#define _DEFAULT_SOURCE
#include "ratchet_pin.h"

#include "ratchet.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int conversation_ok(const char *conversation)
{
	size_t i;

	if (!conversation || !conversation[0] || strlen(conversation) >= 32)
		return 0;
	for (i = 0; conversation[i]; i++) {
		if (!isdigit((unsigned char)conversation[i]))
			return 0;
	}
	return 1;
}

static int pin_path(const char *home, const char *conversation,
			char *path, size_t path_size)
{
	if (!home || !home[0] || !conversation_ok(conversation) || !path)
		return -1;
	if (snprintf(path, path_size, "%s/ratchet/rk/%s", home, conversation) >=
	    (int)path_size)
		return -1;
	return 0;
}

static int ensure_dirs(const char *home)
{
	char dir[576];

	if (!home || !home[0])
		return -1;
	if (snprintf(dir, sizeof(dir), "%s/ratchet", home) >= (int)sizeof(dir) ||
	    (mkdir(dir, 0700) != 0 && errno != EEXIST))
		return -1;
	if (snprintf(dir, sizeof(dir), "%s/ratchet/rk", home) >= (int)sizeof(dir) ||
	    (mkdir(dir, 0700) != 0 && errno != EEXIST))
		return -1;
	return 0;
}

int omaq_ratchet_pin_set(const char *home, const char *conversation, const char *rk)
{
	char path[640], tmp[648];
	FILE *f;

	if (!omaq_rk_ok(rk) || pin_path(home, conversation, path, sizeof(path)) != 0)
		return -1;
	if (ensure_dirs(home) != 0 || snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -1;
	f = fopen(tmp, "w");
	if (!f)
		return -1;
	if (fchmod(fileno(f), 0600) != 0 ||
	    fprintf(f, "%s\n", rk) < 0 || fflush(f) != 0 || fsync(fileno(f)) != 0) {
		fclose(f);
		unlink(tmp);
		return -1;
	}
	if (fclose(f) != 0 || rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

int omaq_ratchet_pin_get(const char *home, const char *conversation,
                         char *rk, size_t rk_size)
{
	char path[640], buf[OMAQ_RK_HEX + 2];
	FILE *f;
	size_t n;

	if (!rk || rk_size < OMAQ_RK_HEX + 1 ||
	    pin_path(home, conversation, path, sizeof(path)) != 0)
		return -1;
	rk[0] = '\0';
	f = fopen(path, "r");
	if (!f) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	n = fread(buf, 1, sizeof(buf) - 1, f);
	if (ferror(f) || fclose(f) != 0)
		return -1;
	buf[n] = '\0';
	if (n && buf[n - 1] == '\n')
		buf[--n] = '\0';
	if (n != OMAQ_RK_HEX || !omaq_rk_ok(buf))
		return -1;
	memcpy(rk, buf, OMAQ_RK_HEX + 1);
	return 1;
}
