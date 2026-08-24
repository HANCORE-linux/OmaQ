#define _DEFAULT_SOURCE
#include "identity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int omaq_identity_pass_ok(const char *pass)
{
	size_t n;

	if (!pass)
		return 0;
	n = strlen(pass);
	if (n == 0 || n > 128)
		return 0;
	if (strchr(pass, '\n') || strchr(pass, '\r'))
		return 0;
	return 1;
}

#ifdef HAVE_TOX
struct omaq_tox *omaq_identity_load(const char *home, const char *pass, int *err)
{
	if (pass && pass[0] && !omaq_identity_pass_ok(pass)) {
		if (err)
			*err = -1;
		return NULL;
	}
	return omaq_tox_open(home, pass, err);
}

int omaq_identity_protect(struct omaq_tox *t, const char *pass)
{
	if (!omaq_identity_pass_ok(pass))
		return -1;
	return omaq_tox_protect(t, pass);
}

int omaq_identity_unprotect(struct omaq_tox *t, const char *pass)
{
	if (!omaq_identity_pass_ok(pass))
		return -1;
	return omaq_tox_unprotect(t, pass);
}

int omaq_identity_protected(const struct omaq_tox *t)
{
	return omaq_tox_protected(t);
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
	char tmp[640], buf[4096];
	FILE *in = NULL, *out = NULL;
	struct stat st;
	size_t n, total = 0;
	int in_fd = -1, out_fd = -1, tmp_created = 0, rc = -1;

	if (snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", dst, (long)getpid()) >= (int)sizeof(tmp))
		return -1;
	if (lstat(dst, &st) == 0) {
		if (!S_ISREG(st.st_mode))
			return -1;
	} else if (errno != ENOENT) {
		return -1;
	}
	in_fd = open(src, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
	if (in_fd < 0 || fstat(in_fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size <= 0 || (uint64_t)st.st_size > OMAQ_IDENTITY_FILE_MAX)
		goto done;
	in = fdopen(in_fd, "rb");
	if (!in)
		goto done;
	in_fd = -1;
	out_fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK |
		      O_NOFOLLOW, 0600);
	if (out_fd < 0)
		goto done;
	tmp_created = 1;
	out = fdopen(out_fd, "wb");
	if (!out)
		goto done;
	out_fd = -1;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (n > OMAQ_IDENTITY_FILE_MAX - total || fwrite(buf, 1, n, out) != n)
			goto done;
		total += n;
	}
	if (ferror(in) || total != (size_t)st.st_size ||
	    fflush(out) != 0 || fsync(fileno(out)) != 0)
		goto done;
	if (fclose(out) != 0) {
		out = NULL;
		goto done;
	}
	out = NULL;
	if (rename(tmp, dst) != 0)
		goto done;
	rc = 0;
done:
	if (in)
		fclose(in);
	if (out)
		fclose(out);
	if (in_fd >= 0)
		close(in_fd);
	if (out_fd >= 0)
		close(out_fd);
	if (rc != 0 && tmp_created)
		unlink(tmp);
	return rc;
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

int omaq_identity_export_exclusive(const char *home, const char *path)
{
	char src[576], buf[4096];
	FILE *in = NULL, *out = NULL;
	struct stat st;
	size_t n, total = 0;
	int in_fd = -1, out_fd = -1, rc = -1;

	if (!home || !path_ok(path) ||
	    snprintf(src, sizeof(src), "%s/tox.save", home) >= (int)sizeof(src))
		return -1;
	in_fd = open(src, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
	if (in_fd < 0 || fstat(in_fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size <= 0 || (uint64_t)st.st_size > OMAQ_IDENTITY_FILE_MAX)
		goto done;
	in = fdopen(in_fd, "rb");
	if (!in)
		goto done;
	in_fd = -1;
	out_fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK |
		      O_NOFOLLOW, 0600);
	if (out_fd < 0)
		goto done;
	out = fdopen(out_fd, "wb");
	if (!out)
		goto done;
	out_fd = -1;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (n > OMAQ_IDENTITY_FILE_MAX - total || fwrite(buf, 1, n, out) != n)
			goto done;
		total += n;
	}
	if (ferror(in) || total != (size_t)st.st_size ||
	    fflush(out) != 0 || fsync(fileno(out)) != 0)
		goto done;
	rc = 0;
done:
	if (in)
		fclose(in);
	if (out && fclose(out) != 0)
		rc = -1;
	if (in_fd >= 0)
		close(in_fd);
	if (out_fd >= 0)
		close(out_fd);
	if (rc != 0 && (out || out_fd >= 0))
		unlink(path);
	return rc;
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
