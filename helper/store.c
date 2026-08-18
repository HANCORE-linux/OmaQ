#define _DEFAULT_SOURCE
#include "store.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ROTATE_BYTES (2 * 1024 * 1024)

static int mkdir_p(const char *path)
{
	if (mkdir(path, 0700) == 0 || errno == EEXIST)
		return 0;
	return -1;
}

static int hist_dir(const char *home, const char *conv_id, char *buf, size_t n)
{
	if (!home || !conv_id || !buf)
		return -1;
	if (strchr(conv_id, '/') || strstr(conv_id, ".."))
		return -1;
	if (snprintf(buf, n, "%s/history/%s", home, conv_id) >= (int)n)
		return -1;
	return 0;
}

static int hist_file(const char *home, const char *conv_id, char *buf, size_t n)
{
	char dir[512];
	if (hist_dir(home, conv_id, dir, sizeof(dir)) != 0)
		return -1;
	if (snprintf(buf, n, "%s/messages.jsonl", dir) >= (int)n)
		return -1;
	return 0;
}

int omaq_store_append(const char *home, const char *conv_id, const char *line)
{
	char dir[512], path[576], rot[580];
	char root[512];
	FILE *f;
	struct stat st;

	if (!line || strchr(line, '\n'))
		return -1;
	if (hist_dir(home, conv_id, dir, sizeof(dir)) != 0)
		return -1;
	if (snprintf(root, sizeof(root), "%s/history", home) >= (int)sizeof(root))
		return -1;
	if (mkdir_p(root) != 0)
		return -1;
	if (mkdir_p(dir) != 0)
		return -1;
	if (hist_file(home, conv_id, path, sizeof(path)) != 0)
		return -1;
	if (stat(path, &st) == 0 && st.st_size >= ROTATE_BYTES) {
		if (snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
			return -1;
		rename(path, rot);
	}
	f = fopen(path, "a");
	if (!f)
		return -1;
	if (fchmod(fileno(f), 0600) != 0) {
		fclose(f);
		return -1;
	}
	if (fprintf(f, "%s\n", line) < 0) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static int read_lines(const char *path, char ***lines, size_t *n, size_t *cap)
{
	FILE *f;
	char buf[4096];

	f = fopen(path, "r");
	if (!f)
		return 0;
	while (fgets(buf, sizeof(buf), f)) {
		size_t len = strlen(buf);
		char *copy;
		if (len && buf[len - 1] == '\n')
			buf[--len] = '\0';
		if (*n == *cap) {
			size_t ncap = *cap ? *cap * 2 : 16;
			char **nl = realloc(*lines, ncap * sizeof(*nl));
			if (!nl) {
				fclose(f);
				return -1;
			}
			*lines = nl;
			*cap = ncap;
		}
		copy = malloc(len + 1);
		if (!copy) {
			fclose(f);
			return -1;
		}
		memcpy(copy, buf, len + 1);
		(*lines)[(*n)++] = copy;
	}
	fclose(f);
	return 0;
}

int omaq_store_tail(const char *home, const char *conv_id, int limit, char **out, size_t *out_len)
{
	char path[576], rot[580];
	char **lines = NULL;
	size_t n = 0, cap = 0;
	size_t i, start;
	size_t total = 0;
	char *acc;

	if (!out || !out_len || limit < 0)
		return -1;
	*out = NULL;
	*out_len = 0;
	if (limit == 0) {
		*out = calloc(1, 1);
		return *out ? 0 : -1;
	}
	if (hist_file(home, conv_id, path, sizeof(path)) != 0)
		return -1;
	if (snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	if (read_lines(rot, &lines, &n, &cap) != 0)
		goto fail;
	if (read_lines(path, &lines, &n, &cap) != 0)
		goto fail;
	if (n == 0) {
		*out = calloc(1, 1);
		return *out ? 0 : -1;
	}
	start = n > (size_t)limit ? n - (size_t)limit : 0;
	for (i = start; i < n; i++)
		total += strlen(lines[i]) + 1;
	acc = malloc(total + 1);
	if (!acc)
		goto fail;
	acc[0] = '\0';
	for (i = start; i < n; i++) {
		strcat(acc, lines[i]);
		if (i + 1 < n)
			strcat(acc, "\n");
	}
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	*out = acc;
	*out_len = strlen(acc);
	return 0;
fail:
	if (lines) {
		for (i = 0; i < n; i++)
			free(lines[i]);
		free(lines);
	}
	return -1;
}

static int ci_has(const char *hay, const char *needle)
{
	size_t nlen, hlen, i, j;

	if (!hay || !needle || !needle[0])
		return 0;
	nlen = strlen(needle);
	hlen = strlen(hay);
	if (nlen > hlen)
		return 0;
	for (i = 0; i + nlen <= hlen; i++) {
		for (j = 0; j < nlen; j++) {
			unsigned char a = (unsigned char)hay[i + j];
			unsigned char b = (unsigned char)needle[j];
			if (tolower(a) != tolower(b))
				break;
		}
		if (j == nlen)
			return 1;
	}
	return 0;
}

int omaq_store_search(const char *home, const char *conv_id, const char *needle,
		      int limit, char **out, size_t *out_len)
{
	char path[576], rot[580];
	char **lines = NULL;
	size_t n = 0, cap = 0, i, total = 0;
	size_t *hit = NULL;
	size_t nhit = 0;
	char *acc;

	if (!out || !out_len || !needle || strchr(needle, '\n'))
		return -1;
	*out = NULL;
	*out_len = 0;
	if (limit <= 0)
		limit = 20;
	if (limit > 20)
		limit = 20;
	if (hist_file(home, conv_id, path, sizeof(path)) != 0)
		return -1;
	if (snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	if (read_lines(rot, &lines, &n, &cap) != 0)
		goto sfail;
	if (read_lines(path, &lines, &n, &cap) != 0)
		goto sfail;
	hit = calloc(n ? n : 1, sizeof(*hit));
	if (!hit)
		goto sfail;
	for (i = 0; i < n; i++) {
		if (ci_has(lines[i], needle))
			hit[nhit++] = i;
	}
	if (nhit > (size_t)limit) {
		memmove(hit, hit + (nhit - (size_t)limit), (size_t)limit * sizeof(*hit));
		nhit = (size_t)limit;
	}
	if (nhit == 0) {
		free(hit);
		for (i = 0; i < n; i++)
			free(lines[i]);
		free(lines);
		*out = calloc(1, 1);
		return *out ? 0 : -1;
	}
	for (i = 0; i < nhit; i++)
		total += strlen(lines[hit[i]]) + 1;
	acc = malloc(total + 1);
	if (!acc)
		goto sfail;
	acc[0] = '\0';
	for (i = 0; i < nhit; i++) {
		strcat(acc, lines[hit[i]]);
		if (i + 1 < nhit)
			strcat(acc, "\n");
	}
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	free(hit);
	*out = acc;
	*out_len = strlen(acc);
	return 0;
sfail:
	if (lines) {
		for (i = 0; i < n; i++)
			free(lines[i]);
		free(lines);
	}
	free(hit);
	return -1;
}
