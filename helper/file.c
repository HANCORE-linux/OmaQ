#define _DEFAULT_SOURCE
#include "file.h"
#include "avatar.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_TOX
#define XFERS 4

static struct {
	int used;
	int sending;
	uint32_t friend;
	uint32_t fnum;
	FILE *fp;
	char path[512];
	uint64_t size;
	uint64_t got;
	int avatar;
} xf[XFERS];

static struct {
	int used;
	uint32_t friend;
	uint32_t fnum;
	uint64_t size;
	char name[OMAQ_FILE_NAME_MAX + 1];
} of[XFERS];

static int xf_find(uint32_t friend, uint32_t fnum, int create)
{
	int i, free_i = -1;

	for (i = 0; i < XFERS; i++) {
		if (!xf[i].used) {
			if (free_i < 0)
				free_i = i;
			continue;
		}
		if (xf[i].friend == friend && xf[i].fnum == fnum)
			return i;
	}
	if (!create || free_i < 0)
		return -1;
	memset(&xf[free_i], 0, sizeof(xf[free_i]));
	xf[free_i].used = 1;
	xf[free_i].friend = friend;
	xf[free_i].fnum = fnum;
	return free_i;
}

static void xf_drop(int i)
{
	if (i < 0 || !xf[i].used)
		return;
	if (xf[i].fp)
		fclose(xf[i].fp);
	memset(&xf[i], 0, sizeof(xf[i]));
}

static int of_find(uint32_t friend, uint32_t fnum, int create)
{
	int i, free_i = -1;

	for (i = 0; i < XFERS; i++) {
		if (!of[i].used) {
			if (free_i < 0)
				free_i = i;
			continue;
		}
		if (of[i].friend == friend && of[i].fnum == fnum)
			return i;
	}
	if (!create || free_i < 0)
		return -1;
	memset(&of[free_i], 0, sizeof(of[free_i]));
	of[free_i].used = 1;
	of[free_i].friend = friend;
	of[free_i].fnum = fnum;
	return free_i;
}
#endif

omaq_file_event omaq_file_event_for(int avatar, omaq_file_outcome outcome)
{
	if (avatar)
		return outcome == OMAQ_FILE_OUTCOME_DONE ? OMAQ_FILE_EVENT_AVATAR :
			OMAQ_FILE_EVENT_NONE;
	return outcome == OMAQ_FILE_OUTCOME_DONE ? OMAQ_FILE_EVENT_DONE :
		OMAQ_FILE_EVENT_FAILED;
}

int omaq_file_path_ok(const char *path)
{
	if (!path || path[0] != '/')
		return 0;
	if (strlen(path) >= 512 || strstr(path, "..") || strchr(path, '\n'))
		return 0;
	return 1;
}

int omaq_file_basename(const char *path, char *out, size_t n)
{
	const char *b;

	if (!path || !out || n < 2)
		return -1;
	b = strrchr(path, '/');
	b = b ? b + 1 : path;
	if (!b[0] || strchr(b, '/') || strstr(b, ".."))
		return -1;
	if (strlen(b) >= n || strlen(b) > OMAQ_FILE_NAME_MAX)
		return -1;
	memcpy(out, b, strlen(b) + 1);
	return 0;
}

int omaq_file_id_format(uint32_t friend, uint32_t fnum, char *out, size_t n)
{
	int wr;

	if (!out || n < 4)
		return -1;
	wr = snprintf(out, n, "%u:%u", friend, fnum);
	if (wr < 0 || (size_t)wr >= n)
		return -1;
	return 0;
}

int omaq_file_id_parse(const char *id, uint32_t *friend, uint32_t *fnum)
{
	unsigned long a, b;
	char *end;

	if (!id || !friend || !fnum || id[0] == 'g')
		return -1;
	a = strtoul(id, &end, 10);
	if (!end || *end != ':' || end == id)
		return -1;
	b = strtoul(end + 1, &end, 10);
	if (!end || *end != '\0')
		return -1;
	*friend = (uint32_t)a;
	*fnum = (uint32_t)b;
	return 0;
}

#ifdef HAVE_TOX

static int mkdir_p(const char *path)
{
	char tmp[512];
	char *p;

	if (!path || !path[0] || strlen(path) >= sizeof(tmp))
		return -1;
	strcpy(tmp, path);
	for (p = tmp + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
			return -1;
		*p = '/';
	}
	if (mkdir(tmp, 0700) != 0 && errno != EEXIST)
		return -1;
	return 0;
}

static int download_dir(const char *home, char *out, size_t n)
{
	const char *base = getenv("OMAQ_DOWNLOAD_DIR");
	const char *user_home;
	struct passwd *pw;

	if (!base || base[0] != '/')
		base = getenv("XDG_DOWNLOAD_DIR");
	if (base && base[0] == '/')
		return snprintf(out, n, "%s/omaq", base) < (int)n ? 0 : -1;
	user_home = getenv("HOME");
	if (!user_home || user_home[0] != '/') {
		pw = getpwuid(getuid());
		user_home = pw ? pw->pw_dir : NULL;
	}
	if (!user_home || user_home[0] != '/')
		return -1;
	return snprintf(out, n, "%s/Downloads/omaq", user_home) < (int)n ? 0 : -1;
}

static FILE *open_download_file(const char *dir, const char *name,
				char *dest, size_t destn)
{
	unsigned int suffix;

	for (suffix = 0; suffix < 10000; suffix++) {
		int fd;
		int wr;

		wr = suffix == 0
			? snprintf(dest, destn, "%s/%s", dir, name)
			: snprintf(dest, destn, "%s/%s.%u", dir, name, suffix);
		if (wr < 0 || (size_t)wr >= destn)
			return NULL;
		fd = open(dest, O_WRONLY | O_CREAT | O_EXCL, 0600);
		if (fd >= 0) {
			FILE *fp = fdopen(fd, "wb");
			if (fp)
				return fp;
			close(fd);
			unlink(dest);
			return NULL;
		}
		if (errno != EEXIST)
			return NULL;
	}
	return NULL;
}

int omaq_file_offer_store(uint32_t friend, uint32_t fnum, const char *name, uint64_t size)
{
	char safe[OMAQ_FILE_NAME_MAX + 1];
	int i;

	if (!name || size > OMAQ_FILE_MAX || size == 0)
		return -1;
	if (omaq_file_basename(name, safe, sizeof(safe)) != 0)
		return -1;
	i = of_find(friend, fnum, 1);
	if (i < 0)
		return -1;
	of[i].size = size;
	snprintf(of[i].name, sizeof(of[i].name), "%s", safe);
	return 0;
}

int omaq_file_offer_lookup(uint32_t friend, uint32_t fnum, char *name, size_t n, uint64_t *size)
{
	int i = of_find(friend, fnum, 0);

	if (i < 0)
		return -1;
	if (name && n) {
		if (snprintf(name, n, "%s", of[i].name) >= (int)n)
			return -1;
	}
	if (size)
		*size = of[i].size;
	return 0;
}

void omaq_file_offer_drop(uint32_t friend, uint32_t fnum)
{
	int i = of_find(friend, fnum, 0);

	if (i >= 0)
		memset(&of[i], 0, sizeof(of[i]));
}

int omaq_file_send_begin(struct omaq_tox *t, uint32_t friend, const char *path, uint32_t *fnum_out)
{
	struct stat st;
	char name[OMAQ_FILE_NAME_MAX + 1];
	uint32_t fnum;
	int i;
	FILE *fp;

	if (!omaq_file_path_ok(path) || omaq_file_basename(path, name, sizeof(name)) != 0)
		return -1;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
	    (uint64_t)st.st_size > OMAQ_FILE_MAX)
		return -1;
	fp = fopen(path, "rb");
	if (!fp)
		return -1;
	if (omaq_tox_file_send(t, friend, (uint64_t)st.st_size, name, &fnum) != 0) {
		fclose(fp);
		return -1;
	}
	i = xf_find(friend, fnum, 1);
	if (i < 0) {
		(void)omaq_tox_file_control(t, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		fclose(fp);
		return -1;
	}
	xf[i].sending = 1;
	xf[i].avatar = 0;
	xf[i].fp = fp;
	xf[i].size = (uint64_t)st.st_size;
	if (snprintf(xf[i].path, sizeof(xf[i].path), "%s", path) >= (int)sizeof(xf[i].path)) {
		(void)omaq_tox_file_control(t, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		xf_drop(i);
		return -1;
	}
	if (fnum_out)
		*fnum_out = fnum;
	return 0;
}

int omaq_file_send_avatar_begin(struct omaq_tox *t, uint32_t friend, const char *path,
				const uint8_t file_id[32], uint32_t *fnum_out)
{
	struct stat st;
	uint32_t fnum;
	int i;
	FILE *fp;

	if (!omaq_file_path_ok(path) || !file_id)
		return -1;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
	    (uint64_t)st.st_size > OMAQ_AVATAR_MAX)
		return -1;
	fp = fopen(path, "rb");
	if (!fp)
		return -1;
	if (omaq_tox_file_send_avatar(t, friend, (uint64_t)st.st_size, file_id, &fnum) != 0) {
		fclose(fp);
		return -1;
	}
	i = xf_find(friend, fnum, 1);
	if (i < 0) {
		(void)omaq_tox_file_control(t, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		fclose(fp);
		return -1;
	}
	xf[i].sending = 1;
	xf[i].avatar = 1;
	xf[i].fp = fp;
	xf[i].size = (uint64_t)st.st_size;
	if (snprintf(xf[i].path, sizeof(xf[i].path), "%s", path) >= (int)sizeof(xf[i].path)) {
		(void)omaq_tox_file_control(t, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		xf_drop(i);
		return -1;
	}
	if (fnum_out)
		*fnum_out = fnum;
	return 0;
}

int omaq_file_recv_begin(const char *home, const char *conv, uint32_t friend,
			 uint32_t fnum, const char *name, uint64_t size,
			 const char *dest_override, char *dest, size_t destn,
			 int avatar)
{
	char dir[512], safe[OMAQ_FILE_NAME_MAX + 1];
	int i;
	FILE *fp = NULL;

	if (!home || !conv || !name || size == 0 || size > OMAQ_FILE_MAX)
		return -1;
	if (omaq_file_basename(name, safe, sizeof(safe)) != 0)
		return -1;
	if (strchr(conv, '/') || strstr(conv, ".."))
		return -1;
	if (dest_override && dest_override[0]) {
		if (!omaq_file_path_ok(dest_override))
			return -1;
		if (snprintf(dest, destn, "%s", dest_override) >= (int)destn)
			return -1;
	} else {
		if (download_dir(home, dir, sizeof(dir)) != 0 || mkdir_p(dir) != 0)
			return -1;
		fp = open_download_file(dir, safe, dest, destn);
		if (!fp)
			return -1;
	}
	if (dest_override && dest_override[0]) {
		fp = fopen(dest, "wb");
		if (!fp)
			return -1;
	}
	if (fchmod(fileno(fp), 0600) != 0) {
		fclose(fp);
		unlink(dest);
		return -1;
	}
	i = xf_find(friend, fnum, 1);
	if (i < 0) {
		fclose(fp);
		unlink(dest);
		return -1;
	}
	xf[i].sending = 0;
	xf[i].avatar = avatar != 0;
	xf[i].fp = fp;
	xf[i].size = size;
	if (snprintf(xf[i].path, sizeof(xf[i].path), "%s", dest) >= (int)sizeof(xf[i].path)) {
		xf_drop(i);
		unlink(dest);
		return -1;
	}
	omaq_file_offer_drop(friend, fnum);
	return 0;
}

int omaq_file_is_avatar(uint32_t friend, uint32_t fnum)
{
	int i = xf_find(friend, fnum, 0);

	return i >= 0 && xf[i].avatar;
}

int omaq_file_chunk_out(struct omaq_tox *t, uint32_t friend, uint32_t fnum,
			uint64_t pos, size_t len)
{
	int i = xf_find(friend, fnum, 0);
	uint8_t *buf;
	size_t got;

	if (i < 0 || !xf[i].sending || !xf[i].fp)
		return -1;
	if (len == 0) {
		xf_drop(i);
		return 0;
	}
	if (len > 65536)
		return -1;
	if (fseeko(xf[i].fp, (off_t)pos, SEEK_SET) != 0)
		return -1;
	buf = malloc(len);
	if (!buf)
		return -1;
	got = fread(buf, 1, len, xf[i].fp);
	if (got != len) {
		free(buf);
		return -1;
	}
	if (omaq_tox_file_chunk(t, friend, fnum, pos, buf, got) != 0) {
		free(buf);
		return -1;
	}
	free(buf);
	return 0;
}

int omaq_file_chunk_in(uint32_t friend, uint32_t fnum, uint64_t pos,
		       const uint8_t *data, size_t len, char *done_path, size_t n)
{
	int i = xf_find(friend, fnum, 0);

	if (i < 0 || xf[i].sending || !xf[i].fp)
		return -1;
	if (len == 0) {
		if (xf[i].got != xf[i].size)
			return -1;
		if (done_path && n) {
			if (snprintf(done_path, n, "%s", xf[i].path) >= (int)n)
				return -1;
		}
		xf_drop(i);
		return 1;
	}
	if (!data)
		return -1;
	if (pos + len > xf[i].size)
		return -1;
	if (fseeko(xf[i].fp, (off_t)pos, SEEK_SET) != 0)
		return -1;
	if (fwrite(data, 1, len, xf[i].fp) != len)
		return -1;
	if (pos + len > xf[i].got)
		xf[i].got = pos + len;
	return 0;
}

void omaq_file_cancel(struct omaq_tox *t, uint32_t friend, uint32_t fnum)
{
	int i = xf_find(friend, fnum, 0);

	(void)omaq_tox_file_control(t, friend, fnum, OMAQ_TOX_FILE_CANCEL);
	if (i >= 0) {
		if (!xf[i].sending && xf[i].path[0])
			unlink(xf[i].path);
		xf_drop(i);
	}
	omaq_file_offer_drop(friend, fnum);
}

#endif /* HAVE_TOX */
