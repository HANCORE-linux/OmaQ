#define _DEFAULT_SOURCE
#include "surface.h"
#include "json_io.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int mkdir_p(const char *path)
{
	if (mkdir(path, 0700) == 0 || errno == EEXIST)
		return 0;
	return -1;
}

static int surf_path(const char *state, char *buf, size_t n)
{
	if (!state || !state[0] || !buf)
		return -1;
	if (snprintf(buf, n, "%s/surfaces.jsonl", state) >= (int)n)
		return -1;
	return 0;
}

static int conv_ok(const char *c)
{
	size_t len;

	if (!c || !c[0] || (len = strlen(c)) >= 80)
		return 0;
	if (c[0] == 'g') {
		if (len != 66 || c[1] != ':')
			return 0;
		for (size_t i = 2; i < len; i++)
			if (!((c[i] >= '0' && c[i] <= '9') ||
			      (c[i] >= 'a' && c[i] <= 'f')))
				return 0;
		return 1;
	}
	if (len > 1 && c[0] == '0')
		return 0;
	for (size_t i = 0; i < len; i++)
		if (c[i] < '0' || c[i] > '9')
			return 0;
	return 1;
}

static int parse_line(const char *line, omaq_surface *s)
{
	const char *p;
	char pinned[8];

	if (!line || !s)
		return -1;
	memset(s, 0, sizeof(*s));
	p = strstr(line, "\"conversation\":\"");
	if (!p)
		return -1;
	p += 16;
	if (sscanf(p, "%79[^\"]", s->conversation) != 1)
		return -1;
	p = strstr(line, "\"monitor\":\"");
	if (p) {
		p += 11;
		sscanf(p, "%63[^\"]", s->monitor);
	}
	p = strstr(line, "\"x\":");
	if (p)
		s->x = atoi(p + 4);
	p = strstr(line, "\"y\":");
	if (p)
		s->y = atoi(p + 4);
	p = strstr(line, "\"pinned\":");
	if (p) {
		if (sscanf(p + 9, "%7s", pinned) == 1 && pinned[0] == 't')
			s->pinned = 1;
	}
	return s->conversation[0] ? 0 : -1;
}

static int load_all(const char *path, omaq_surface *arr, int *n)
{
	FILE *f;
	char buf[512];

	*n = 0;
	f = fopen(path, "r");
	if (!f)
		return 0;
	while (fgets(buf, sizeof(buf), f)) {
		omaq_surface s;
		if (parse_line(buf, &s) != 0 || !conv_ok(s.conversation))
			continue;
		if (*n >= OMAQ_SURFACE_MAX)
			break;
		arr[(*n)++] = s;
	}
	fclose(f);
	return 0;
}

static int write_all(const char *path, const omaq_surface *arr, int n)
{
	char tmp[580];
	FILE *f;
	int fd, i;

	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -1;
	{
		struct stat temporary_stat;
		if (lstat(tmp, &temporary_stat) == 0) {
			if (!S_ISREG(temporary_stat.st_mode) ||
			    temporary_stat.st_uid != geteuid() || temporary_stat.st_nlink != 1 ||
			    (temporary_stat.st_mode & 0077) != 0 || temporary_stat.st_size < 0 ||
			    temporary_stat.st_size > 65536 || unlink(tmp) != 0)
				return -1;
		} else if (errno != ENOENT) {
			return -1;
		}
	}
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		unlink(tmp);
		return -1;
	}
	if (fchmod(fileno(f), 0600) != 0) {
		fclose(f);
		unlink(tmp);
		return -1;
	}
	for (i = 0; i < n; i++) {
		char ec[160], em[128];
		if (omaq_json_escape(arr[i].conversation, ec, sizeof(ec)) != 0 ||
		    omaq_json_escape(arr[i].monitor, em, sizeof(em)) != 0) {
			fclose(f);
			unlink(tmp);
			return -1;
		}
		if (fprintf(f, "{\"conversation\":\"%s\",\"monitor\":\"%s\",\"x\":%d,\"y\":%d,\"pinned\":%s}\n",
			    ec, em, arr[i].x, arr[i].y, arr[i].pinned ? "true" : "false") < 0) {
			fclose(f);
			unlink(tmp);
			return -1;
		}
	}
	if (fflush(f) != 0 || fsync(fileno(f)) != 0) {
		fclose(f);
		unlink(tmp);
		return -1;
	}
	fclose(f);
	if (rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

int omaq_surface_set(const char *state, const omaq_surface *s)
{
	char path[576];
	omaq_surface arr[OMAQ_SURFACE_MAX];
	int n = 0, i, found = 0;

	if (!s || !conv_ok(s->conversation))
		return -1;
	if (surf_path(state, path, sizeof(path)) != 0)
		return -1;
	if (mkdir_p(state) != 0)
		return -1;
	if (load_all(path, arr, &n) != 0)
		return -1;
	for (i = 0; i < n; i++) {
		if (strcmp(arr[i].conversation, s->conversation) == 0) {
			arr[i] = *s;
			found = 1;
			break;
		}
	}
	if (!found) {
		if (n >= OMAQ_SURFACE_MAX)
			return -1;
		arr[n++] = *s;
	}
	return write_all(path, arr, n);
}

int omaq_surface_list(const char *state, omaq_surface *out, int cap)
{
	char path[576];
	omaq_surface all[OMAQ_SURFACE_MAX];
	int n = 0, copy;

	if (!out || cap <= 0 || surf_path(state, path, sizeof(path)) != 0)
		return -1;
	if (load_all(path, all, &n) != 0)
		return -1;
	copy = n < cap ? n : cap;
	if (copy > 0)
		memcpy(out, all, (size_t)copy * sizeof(*out));
	return copy;
}

int omaq_surface_get(const char *state, const char *conv, omaq_surface *s)
{
	char path[576];
	omaq_surface arr[OMAQ_SURFACE_MAX];
	int n = 0, i;

	if (!s || !conv_ok(conv))
		return -1;
	if (surf_path(state, path, sizeof(path)) != 0)
		return -1;
	if (load_all(path, arr, &n) != 0)
		return -1;
	for (i = 0; i < n; i++) {
		if (strcmp(arr[i].conversation, conv) == 0) {
			*s = arr[i];
			return 0;
		}
	}
	return -1;
}
