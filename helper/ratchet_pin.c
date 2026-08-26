#define _DEFAULT_SOURCE
#include "ratchet_pin.h"

#include "ratchet.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int pin_path(const char *home, const char *conversation,
			char *path, size_t path_size)
{
	if (!home || !home[0] || !omaq_ratchet_peer_ok(conversation) || !path)
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
	int fd, parent_fd;

	if (!omaq_rk_ok(rk) || pin_path(home, conversation, path, sizeof(path)) != 0)
		return -1;
	if (ensure_dirs(home) != 0 || snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -1;
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		unlink(tmp);
		return -1;
	}
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
	if (snprintf(tmp, sizeof(tmp), "%s/ratchet/rk", home) >= (int)sizeof(tmp))
		return -1;
	parent_fd = open(tmp, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (parent_fd < 0)
		return -1;
	fd = fsync(parent_fd);
	close(parent_fd);
	return fd;
}

int omaq_ratchet_pin_get(const char *home, const char *conversation,
                         char *rk, size_t rk_size)
{
	char path[640], buf[OMAQ_RK_HEX + 2];
	FILE *f;
	struct stat st;
	size_t n;
	int fd;

	if (!rk || rk_size < OMAQ_RK_HEX + 1 ||
	    pin_path(home, conversation, path, sizeof(path)) != 0)
		return -1;
	rk[0] = '\0';
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || st.st_size <= 0 ||
	    st.st_size > OMAQ_RK_HEX + 1) {
		close(fd);
		return -1;
	}
	f = fdopen(fd, "r");
	if (!f) {
		close(fd);
		return -1;
	}
	n = fread(buf, 1, sizeof(buf) - 1, f);
	if (n != (size_t)st.st_size || ferror(f) || fclose(f) != 0)
		return -1;
	buf[n] = '\0';
	if (n && buf[n - 1] == '\n')
		buf[--n] = '\0';
	if (n != OMAQ_RK_HEX || !omaq_rk_ok(buf))
		return -1;
	memcpy(rk, buf, OMAQ_RK_HEX + 1);
	return 1;
}
