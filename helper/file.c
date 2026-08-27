#define _DEFAULT_SOURCE
#include "file.h"
#include "avatar.h"

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
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
int omaq_file_busy(void)
{
	int i;

	for (i = 0; i < XFERS; i++) {
		if (xf[i].used || of[i].used)
			return 1;
	}
	return 0;
}

int omaq_file_friend_active(uint32_t friend)
{
	for (int i = 0; i < XFERS; i++)
		if ((xf[i].used && xf[i].friend == friend) ||
		    (of[i].used && of[i].friend == friend))
			return 1;
	return 0;
}

void omaq_file_reset(void)
{
	int i;

	for (i = 0; i < XFERS; i++) {
		if (xf[i].used && !xf[i].sending && xf[i].path[0])
			unlink(xf[i].path);
		xf_drop(i);
	}
	memset(of, 0, sizeof(of));
}
#endif

omaq_file_event omaq_file_event_for(int avatar, omaq_file_outcome outcome)
{
	if (avatar)
		return outcome == OMAQ_FILE_OUTCOME_DONE ? OMAQ_FILE_EVENT_AVATAR :
			OMAQ_FILE_EVENT_NONE;
	if (outcome == OMAQ_FILE_OUTCOME_DONE)
		return OMAQ_FILE_EVENT_DONE;
	if (outcome == OMAQ_FILE_OUTCOME_CANCEL)
		return OMAQ_FILE_EVENT_CANCELED;
	return OMAQ_FILE_EVENT_FAILED;
}

int omaq_file_path_ok(const char *path)
{
	if (!path || path[0] != '/')
		return 0;
	if (strlen(path) >= 512 || strstr(path, "..") || strchr(path, '\n'))
		return 0;
	return 1;
}

static int unicode_filename_format_control(uint32_t codepoint)
{
	return codepoint == 0x00ad ||
		(codepoint >= 0x0600 && codepoint <= 0x0605) ||
		codepoint == 0x061c || codepoint == 0x06dd || codepoint == 0x070f ||
		(codepoint >= 0x0890 && codepoint <= 0x0891) || codepoint == 0x08e2 ||
		(codepoint >= 0x17b4 && codepoint <= 0x17b5) || codepoint == 0x180e ||
		(codepoint >= 0x200b && codepoint <= 0x200f) ||
		(codepoint >= 0x202a && codepoint <= 0x202e) ||
		(codepoint >= 0x2060 && codepoint <= 0x2064) ||
		(codepoint >= 0x2066 && codepoint <= 0x206f) || codepoint == 0xfeff ||
		(codepoint >= 0xfff9 && codepoint <= 0xfffb) || codepoint == 0x110bd ||
		codepoint == 0x110cd || (codepoint >= 0x13430 && codepoint <= 0x1343f) ||
		(codepoint >= 0x1bca0 && codepoint <= 0x1bca3) ||
		(codepoint >= 0x1d173 && codepoint <= 0x1d17a) || codepoint == 0xe0001 ||
		(codepoint >= 0xe0020 && codepoint <= 0xe007f);
}

int omaq_file_name_bytes_ok(const uint8_t *name, size_t length)
{
	size_t i = 0;

	if (!name || length == 0 || length > OMAQ_FILE_NAME_MAX)
		return 0;
	while (i < length) {
		uint8_t lead = name[i++];
		uint32_t codepoint;
		size_t continuation, j;
		if (lead < 0x80) {
			codepoint = lead;
			continuation = 0;
		} else if (lead >= 0xc2 && lead <= 0xdf) {
			codepoint = lead & 0x1fu;
			continuation = 1;
		} else if (lead >= 0xe0 && lead <= 0xef) {
			codepoint = lead & 0x0fu;
			continuation = 2;
		} else if (lead >= 0xf0 && lead <= 0xf4) {
			codepoint = lead & 0x07u;
			continuation = 3;
		} else {
			return 0;
		}
		if (continuation > length - i)
			return 0;
		for (j = 0; j < continuation; j++) {
			uint8_t next = name[i++];
			if ((next & 0xc0) != 0x80)
				return 0;
			codepoint = (codepoint << 6) | (uint32_t)(next & 0x3f);
		}
		if ((continuation == 2 && codepoint < 0x800) ||
		    (continuation == 3 && codepoint < 0x10000) ||
		    codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
		    codepoint < 0x20 || (codepoint >= 0x7f && codepoint <= 0x9f) ||
		    codepoint == '/' || codepoint == 0x2028 || codepoint == 0x2029 ||
		    unicode_filename_format_control(codepoint))
			return 0;
	}
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
	if (strlen(b) >= n ||
	    !omaq_file_name_bytes_ok((const uint8_t *)b, strlen(b)))
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
	const char *p;
	uint64_t a = 0, b = 0;

	if (!id || !friend || !fnum || id[0] < '0' || id[0] > '9')
		return -1;
	p = id;
	if (p[0] == '0' && p[1] != ':')
		return -1;
	while (*p >= '0' && *p <= '9') {
		a = a * 10u + (uint64_t)(*p - '0');
		if (a > UINT32_MAX)
			return -1;
		p++;
	}
	if (*p != ':' || p[1] < '0' || p[1] > '9')
		return -1;
	p++;
	if (p[0] == '0' && p[1] != '\0')
		return -1;
	while (*p >= '0' && *p <= '9') {
		b = b * 10u + (uint64_t)(*p - '0');
		if (b > UINT32_MAX)
			return -1;
		p++;
	}
	if (*p != '\0')
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
	const char *extension = strrchr(name, '.');
	unsigned int suffix;

	if (extension == name)
		extension = NULL;
	for (suffix = 0; suffix < 10000; suffix++) {
		int fd;
		int wr;

		if (suffix == 0) {
			wr = snprintf(dest, destn, "%s/%s", dir, name);
		} else if (extension) {
			wr = snprintf(dest, destn, "%s/%.*s.%u%s", dir,
				      (int)(extension - name), name, suffix, extension);
		} else {
			wr = snprintf(dest, destn, "%s/%s.%u", dir, name, suffix);
		}
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

static FILE *open_receive_exclusive(const char *path)
{
	FILE *file;
	int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);

	if (fd < 0)
		return NULL;
	file = fdopen(fd, "wb");
	if (!file) {
		close(fd);
		unlink(path);
	}
	return file;
}

static FILE *open_receive_override(const char *path)
{
	struct stat st;
	FILE *file;
	int fd;

	fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || fchmod(fd, 0600) != 0 || ftruncate(fd, 0) != 0) {
		close(fd);
		return NULL;
	}
	file = fdopen(fd, "wb");
	if (!file)
		close(fd);
	return file;
}

int omaq_file_download_create(const char *name, const char *dest_override,
			      char *dest, size_t destn)
{
	char dir[512], safe[OMAQ_FILE_NAME_MAX + 1];
	struct stat status;
	const char *extension;
	unsigned int suffix;
	int fd = -1;

	if (!name || !dest || destn == 0 ||
	    omaq_file_basename(name, safe, sizeof(safe)) != 0)
		return -1;
	if (dest_override && dest_override[0]) {
		if (!omaq_file_path_ok(dest_override) ||
		    snprintf(dest, destn, "%s", dest_override) >= (int)destn)
			return -1;
		fd = open(dest, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (fd < 0 || fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
		    status.st_uid != geteuid() || status.st_nlink != 1 ||
		    fchmod(fd, 0600) != 0 || ftruncate(fd, 0) != 0) {
			if (fd >= 0)
				close(fd);
			return -1;
		}
		return fd;
	}
	if (download_dir(NULL, dir, sizeof(dir)) != 0 || mkdir_p(dir) != 0)
		return -1;
	extension = strrchr(safe, '.');
	if (extension == safe)
		extension = NULL;
	for (suffix = 0; suffix < 10000; suffix++) {
		int written;
		if (suffix == 0)
			written = snprintf(dest, destn, "%s/%s", dir, safe);
		else if (extension)
			written = snprintf(dest, destn, "%s/%.*s.%u%s", dir,
					   (int)(extension - safe), safe, suffix, extension);
		else
			written = snprintf(dest, destn, "%s/%s.%u", dir, safe, suffix);
		if (written < 0 || (size_t)written >= destn)
			return -1;
		fd = open(dest, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
			  0600);
		if (fd >= 0)
			return fd;
		if (errno != EEXIST)
			return -1;
	}
	return -1;
}

int omaq_file_recv_begin(const char *home, const char *conv, uint32_t friend,
			 uint32_t fnum, const char *name, uint64_t size,
			 const char *dest_override, char *dest, size_t destn,
			 int avatar)
{
	char dir[512], safe[OMAQ_FILE_NAME_MAX + 1], target[512];
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
		if (avatar) {
			uint32_t nonce;
			if (getrandom(&nonce, sizeof(nonce), 0) != (ssize_t)sizeof(nonce) ||
			    snprintf(target, sizeof(target), "%s.incoming.%u.%u.%08x", dest,
				     friend, fnum, nonce) >= (int)sizeof(target) ||
			    snprintf(dest, destn, "%s", target) >= (int)destn)
				return -1;
			fp = open_receive_exclusive(target);
		} else {
			fp = open_receive_override(dest);
		}
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

int omaq_file_can_cancel(uint32_t friend, uint32_t fnum)
{
	return xf_find(friend, fnum, 0) >= 0 || of_find(friend, fnum, 0) >= 0;
}

int omaq_file_is_sending(uint32_t friend, uint32_t fnum)
{
	int i = xf_find(friend, fnum, 0);

	return i >= 0 && xf[i].sending;
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
	if (pos > xf[i].size || len > xf[i].size - pos)
		return -1;
	if (fseeko(xf[i].fp, (off_t)pos, SEEK_SET) != 0)
		return -1;
	if (fwrite(data, 1, len, xf[i].fp) != len)
		return -1;
	if ((uint64_t)len > UINT64_MAX - pos)
		return -1;
	if (pos + (uint64_t)len > xf[i].got)
		xf[i].got = pos + (uint64_t)len;
	return 0;
}

void omaq_file_drop(uint32_t friend, uint32_t fnum)
{
	int i = xf_find(friend, fnum, 0);

	if (i >= 0) {
		if (!xf[i].sending && xf[i].path[0])
			unlink(xf[i].path);
		xf_drop(i);
	}
	omaq_file_offer_drop(friend, fnum);
}

int omaq_file_cancel(struct omaq_tox *t, uint32_t friend, uint32_t fnum)
{
	if (omaq_tox_file_control(t, friend, fnum, OMAQ_TOX_FILE_CANCEL) != 0)
		return -1;
	omaq_file_drop(friend, fnum);
	return 0;
}

#endif /* HAVE_TOX */
