#define _DEFAULT_SOURCE
#include "identity.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_TOX
struct omaq_tox *omaq_identity_load(const char *home)
{
	return omaq_tox_open(home);
}
#endif

static int path_ok(const char *p)
{
	if (!p || p[0] != '/')
		return 0;
	if (strlen(p) >= 512 || strstr(p, "..") || strchr(p, '\n'))
		return 0;
	return 1;
}

static int copy_file(const char *src, const char *dst)
{
	char tmp[580];
	FILE *in, *out;
	char buf[4096];
	size_t n;

	if (snprintf(tmp, sizeof(tmp), "%s.tmp", dst) >= (int)sizeof(tmp))
		return -1;
	in = fopen(src, "rb");
	if (!in)
		return -1;
	out = fopen(tmp, "wb");
	if (!out) {
		fclose(in);
		return -1;
	}
	if (fchmod(fileno(out), 0600) != 0) {
		fclose(in);
		fclose(out);
		unlink(tmp);
		return -1;
	}
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (fwrite(buf, 1, n, out) != n) {
			fclose(in);
			fclose(out);
			unlink(tmp);
			return -1;
		}
	}
	if (ferror(in) || fflush(out) != 0 || fsync(fileno(out)) != 0) {
		fclose(in);
		fclose(out);
		unlink(tmp);
		return -1;
	}
	fclose(in);
	fclose(out);
	if (rename(tmp, dst) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

int omaq_identity_export(const char *home, const char *path)
{
	char src[576];

	if (!home || !path_ok(path))
		return -1;
	if (snprintf(src, sizeof(src), "%s/tox.save", home) >= (int)sizeof(src))
		return -1;
	return copy_file(src, path);
}

int omaq_identity_import(const char *home, const char *path, int replace)
{
	char dst[576];
	struct stat st;

	if (!home || !path_ok(path))
		return -1;
	if (snprintf(dst, sizeof(dst), "%s/tox.save", home) >= (int)sizeof(dst))
		return -1;
	if (stat(dst, &st) == 0 && !replace)
		return 1; /* exists */
	return copy_file(path, dst);
}
