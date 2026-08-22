#define _DEFAULT_SOURCE
#include "store.h"
#include "json_io.h"

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

static int read_lines(const char *path, char ***lines, size_t *n, size_t *cap);

static int replace_string_field(const char *line, const char *field_name,
				const char *value_text, char **out)
{
	const char *field, *value, *end;
	char prefix_text[80], esc[2800];
	char *result;
	size_t prefix, suffix, n, field_len;

	if (!line || !field_name || !value_text || !out ||
	    snprintf(prefix_text, sizeof(prefix_text), "\"%s\":\"", field_name) >= (int)sizeof(prefix_text) ||
	    omaq_json_escape(value_text, esc, sizeof(esc)) != 0)
		return -1;
	field = strstr(line, prefix_text);
	if (!field)
		return -1;
	value = field + strlen(prefix_text);
	end = value;
	while (*end) {
		if (*end == '\\' && end[1]) {
			end += 2;
			continue;
		}
		if (*end == '\"')
			break;
		end++;
	}
	if (*end != '\"')
		return -1;
	prefix = (size_t)(value - line);
	suffix = strlen(end);
	field_len = strlen(esc);
	n = prefix + field_len + suffix + 1;
	result = malloc(n);
	if (!result)
		return -1;
	memcpy(result, line, prefix);
	memcpy(result + prefix, esc, field_len);
	memcpy(result + prefix + field_len, end, suffix + 1);
	*out = result;
	return 0;
}

static int replace_text_field(const char *line, const char *text, char **out)
{
	return replace_string_field(line, "text", text, out);
}

static int append_flag(const char *line, const char *flag, char **out)
{
	size_t len, flag_len;
	char *result;
	char needle[40];
	char *existing;

	if (!line || !flag || !out)
		return -1;
	len = strlen(line);
	flag_len = strlen(flag);
	if (len < 2 || line[len - 1] != '}')
		return -1;
	if (snprintf(needle, sizeof(needle), ",\"%s\":", flag) >= (int)sizeof(needle))
		return -1;
	existing = strstr(line, needle);
	if (existing) {
		result = malloc(len + 1);
		if (!result)
			return -1;
		memcpy(result, line, len + 1);
		existing = result + (existing - line) + strlen(needle);
		if (strncmp(existing, "true", 4) != 0)
			return free(result), -1;
		*out = result;
		return 0;
	}
	result = malloc(len + flag_len + 10);
	if (!result)
		return -1;
	memcpy(result, line, len - 1);
	snprintf(result + len - 1, flag_len + 10, ",\"%s\":true}", flag);
	*out = result;
	return 0;
}

static int append_string_field(const char *line, const char *field, const char *value, char **out)
{
	char needle[80], esc[2800], *result;
	size_t len, extra;

	if (!line || !field || !value || !out ||
	    snprintf(needle, sizeof(needle), ",\"%s\":\"", field) >= (int)sizeof(needle) ||
	    omaq_json_escape(value, esc, sizeof(esc)) != 0)
		return -1;
	if (strstr(line, needle))
		return replace_string_field(line, field, value, out);
	len = strlen(line);
	if (len < 2 || line[len - 1] != '}')
		return -1;
	extra = strlen(needle) - 1 + strlen(esc) + 3;
	result = malloc(len + extra + 1);
	if (!result)
		return -1;
	memcpy(result, line, len - 1);
	snprintf(result + len - 1, extra + 1, ",\"%s\":\"%s\"}", field, esc);
	*out = result;
	return 0;
}

static int update_file_message(const char *path, const char *id, const char *text,
			       int deleted, const char *expected_from)
{
	char **lines = NULL, tmp[640];
	size_t n = 0, cap = 0, i;
	int changed = 0;
	FILE *f;

	if (read_lines(path, &lines, &n, &cap) != 0)
		return -1;
	for (i = 0; i < n; i++) {
		char needle[128], from_needle[128], *replacement = NULL, *marked = NULL;
		if (snprintf(needle, sizeof(needle), "\"id\":\"%s\"", id) >= (int)sizeof(needle) ||
		    (expected_from && snprintf(from_needle, sizeof(from_needle), "\"from\":\"%s\"", expected_from) >= (int)sizeof(from_needle)) ||
		    !strstr(lines[i], needle) ||
		    (expected_from && !strstr(lines[i], from_needle)))
			continue;
		if (replace_text_field(lines[i], deleted ? "" : text, &replacement) != 0)
			break;
		if (append_flag(replacement, deleted ? "deleted" : "edited", &marked) != 0) {
			free(replacement);
			break;
		}
		free(replacement);
		free(lines[i]);
		lines[i] = marked;
		changed = 1;
		break;
	}
	if (!changed) {
		for (i = 0; i < n; i++)
			free(lines[i]);
		free(lines);
		return 0;
	}
	if (snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp))
		goto fail;
	f = fopen(tmp, "w");
	if (!f)
		goto fail;
	if (fchmod(fileno(f), 0600) != 0)
		goto close_fail;
	for (i = 0; i < n; i++)
		if (fprintf(f, "%s\n", lines[i]) < 0)
			goto close_fail;
	if (fclose(f) != 0)
		goto fail_tmp;
	if (rename(tmp, path) != 0)
		goto fail;
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return 1;
close_fail:
	fclose(f);
fail_tmp:
	unlink(tmp);
fail:
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return -1;
}

static int update_file_receipt(const char *path, const char *id, const char *state)
{
	char **lines = NULL, tmp[640];
	size_t n = 0, cap = 0, i;
	int changed = 0, matched = 0;
	FILE *f;

	if (read_lines(path, &lines, &n, &cap) != 0)
		return -1;
	for (i = 0; i < n; i++) {
		char needle[128], *marked = NULL;
		if (snprintf(needle, sizeof(needle), "\"id\":\"%s\"", id) >= (int)sizeof(needle) ||
		    !strstr(lines[i], needle) || !strstr(lines[i], "\"from\":\"me\""))
			continue;
		matched = 1;
		if (strcmp(state, "delivered") == 0 && strstr(lines[i], "\"receipt\":\"read\""))
			continue;
		if (append_string_field(lines[i], "receipt", state, &marked) != 0)
			break;
		free(lines[i]);
		lines[i] = marked;
		changed = 1;
		break;
	}
	if (!changed) {
		for (i = 0; i < n; i++)
			free(lines[i]);
		free(lines);
		return matched ? 1 : 0;
	}
	if (snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp) ||
	    !(f = fopen(tmp, "w")))
		goto fail;
	if (fchmod(fileno(f), 0600) != 0)
		goto close_fail;
	for (i = 0; i < n; i++)
		if (fprintf(f, "%s\n", lines[i]) < 0)
			goto close_fail;
	if (fclose(f) != 0)
		goto fail_tmp;
	if (rename(tmp, path) != 0)
		goto fail;
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return 1;
close_fail:
	fclose(f);
fail_tmp:
	unlink(tmp);
fail:
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return -1;
}

int omaq_store_update_receipt(const char *home, const char *conv_id, const char *id,
			       const char *state)
{
	char path[576], rot[580];
	int result;

	if (!home || !conv_id || !id || !id[0] || !state ||
	    (strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0) ||
	    hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	result = update_file_receipt(path, id, state);
	if (result != 0)
		return result > 0 ? 0 : -1;
	result = update_file_receipt(rot, id, state);
	if (result < 0)
		return -1;
	return result > 0 ? 0 : -2;
}

int omaq_store_update_message(const char *home, const char *conv_id, const char *id,
			       const char *text, int deleted, const char *expected_from)
{
	char path[576], rot[580];
	int result;

	if (!home || !conv_id || !id || !id[0] || (!text && !deleted))
		return -1;
	if (hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	result = update_file_message(path, id, text ? text : "", deleted, expected_from);
	if (result != 0)
		return result > 0 ? 0 : -1;
	result = update_file_message(rot, id, text ? text : "", deleted, expected_from);
	if (result < 0)
		return -1;
	return result > 0 ? 0 : -2;
}

int omaq_store_clear(const char *home, const char *conv_id)
{
	char dir[512], path[576], rot[580];
	int rc = 0;

	if (hist_dir(home, conv_id, dir, sizeof(dir)) != 0 ||
	    hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	if (unlink(path) != 0 && errno != ENOENT)
		rc = -1;
	if (unlink(rot) != 0 && errno != ENOENT)
		rc = -1;
	if (rmdir(dir) != 0 && errno != ENOENT && errno != ENOTEMPTY)
		rc = -1;
	return rc;
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

static int line_text_value(const char *line, char *out, size_t outn)
{
	const char *p;
	size_t n = 0;

	if (!line || !out || outn == 0)
		return -1;
	p = strstr(line, "\"text\":\"");
	if (!p)
		return -1;
	p += strlen("\"text\":\"");
	while (*p && *p != '\"') {
		char c = *p++;
		if (c == '\\') {
			if (!*p)
				return -1;
			c = *p++;
			if (c == 'n') c = '\n';
			else if (c == 'r') c = '\r';
			else if (c == 't') c = '\t';
			else if (c != '\\' && c != '\"') return -1;
		}
		if (n + 1 >= outn)
			return -1;
		out[n++] = c;
	}
	if (*p != '\"')
		return -1;
	out[n] = '\0';
	return 0;
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
		char text[2800];
		if (line_text_value(lines[i], text, sizeof(text)) == 0 && ci_has(text, needle))
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
