#define _DEFAULT_SOURCE
#include "avatar.h"
#include "file.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

static int mkdir_p(const char *path)
{
	struct stat st;

	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode) ? 0 : -1;
	if (mkdir(path, 0700) != 0)
		return -1;
	return 0;
}

int omaq_avatar_id_ok(const char *id)
{
	size_t i, n;

	if (!id || !id[0])
		return 0;
	if (strcmp(id, "self") == 0)
		return 1;
	n = strlen(id);
	if (n > OMAQ_AVATAR_ID_MAX)
		return 0;
	for (i = 0; i < n; i++) {
		if (id[i] < '0' || id[i] > '9')
			return 0;
	}
	return 1;
}

int omaq_avatar_src_ok(const char *path)
{
	const char *dot;

	if (!omaq_file_path_ok(path))
		return 0;
	dot = strrchr(path, '.');
	if (!dot)
		return 0;
	if (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 ||
	    strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".webp") == 0)
		return 1;
	return 0;
}

int omaq_avatar_dest(const char *home, const char *id, char *out, size_t n)
{
	int wr;

	if (!home || !home[0] || !omaq_avatar_id_ok(id) || !out || n < 8)
		return -1;
	wr = snprintf(out, n, "%s/avatars/%s.png", home, id);
	if (wr < 0 || (size_t)wr >= n)
		return -1;
	return 0;
}

int omaq_avatar_is_dest(const char *home, const char *path)
{
	char prefix[512], idbuf[OMAQ_AVATAR_ID_MAX + 8];
	const char *id;
	size_t n;
	int wr;

	if (!path || !home)
		return 0;
	wr = snprintf(prefix, sizeof(prefix), "%s/avatars/", home);
	if (wr < 0 || (size_t)wr >= sizeof(prefix))
		return 0;
	if (strncmp(path, prefix, (size_t)wr) != 0)
		return 0;
	id = path + wr;
	n = strlen(id);
	if (n < 5 || strcmp(id + n - 4, ".png") != 0)
		return 0;
	n -= 4;
	if (n >= sizeof(idbuf))
		return 0;
	memcpy(idbuf, id, n);
	idbuf[n] = '\0';
	return omaq_avatar_id_ok(idbuf);
}

int omaq_avatar_install(const char *home, const char *id, const char *src,
			char *dest, size_t destn)
{
	char dir[512], out[512];
	unsigned char buf[OMAQ_AVATAR_MAX];
	struct stat st;
	FILE *in, *fp;
	size_t got;
	int png, jpg, webp;

	if (!omaq_avatar_src_ok(src) || omaq_avatar_dest(home, id, out, sizeof(out)) != 0)
		return -1;
	if (stat(src, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
	    (uint64_t)st.st_size > OMAQ_AVATAR_MAX)
		return -1;
	if (snprintf(dir, sizeof(dir), "%s/avatars", home) >= (int)sizeof(dir))
		return -1;
	if (mkdir_p(dir) != 0)
		return -1;
	in = fopen(src, "rb");
	if (!in)
		return -1;
	got = fread(buf, 1, sizeof(buf), in);
	fclose(in);
	if (got == 0 || got != (size_t)st.st_size)
		return -1;
	png = got >= 8 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G';
	jpg = got >= 3 && buf[0] == 0xff && buf[1] == 0xd8 && buf[2] == 0xff;
	webp = got >= 12 && buf[0] == 'R' && buf[8] == 'W' && buf[9] == 'E' &&
	       buf[10] == 'B' && buf[11] == 'P';
	if (!png && !jpg && !webp)
		return -1;
	fp = fopen(out, "wb");
	if (!fp)
		return -1;
	if (fchmod(fileno(fp), 0600) != 0 || fwrite(buf, 1, got, fp) != got) {
		fclose(fp);
		unlink(out);
		return -1;
	}
	fclose(fp);
	if (dest && destn) {
		if (snprintf(dest, destn, "%s", out) >= (int)destn)
			return -1;
	}
	return 0;
}
