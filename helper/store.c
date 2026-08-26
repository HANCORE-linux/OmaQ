#define _DEFAULT_SOURCE
#include "store.h"
#include "json_io.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ROTATE_BYTES (2 * 1024 * 1024)
#define UNREAD_STATE_BYTES (1024 * 1024)
#define STORE_LINE_MAX 16384u
#define GROUP_REACTION_ACTORS_MAX 32

static int open_parent_dir(const char *path, char *base, size_t base_size)
{
	char parent[PATH_MAX], *slash, *cursor, *next;
	int fd;

	if (!path || path[0] != '/' || strlen(path) >= sizeof(parent) || !base ||
	    base_size == 0)
		return -1;
	memcpy(parent, path, strlen(path) + 1);
	slash = strrchr(parent, '/');
	if (!slash || !slash[1] || snprintf(base, base_size, "%s", slash + 1) >=
	    (int)base_size)
		return -1;
	*slash = '\0';
	fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	cursor = parent + 1;
	while (*cursor) {
		int child;
		next = strchr(cursor, '/');
		if (next)
			*next = '\0';
		if (!cursor[0] || strcmp(cursor, ".") == 0 || strcmp(cursor, "..") == 0) {
			close(fd);
			return -1;
		}
		child = openat(fd, cursor,
			       O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (child < 0) {
			close(fd);
			return -1;
		}
		close(fd);
		fd = child;
		if (!next)
			break;
		cursor = next + 1;
	}
	return fd;
}

static FILE *safe_fopen(const char *path, const char *mode)
{
	char base[NAME_MAX + 1];
	struct stat st;
	int parent_fd, fd, flags;
	FILE *file;

	if (!mode || !mode[0] || mode[1])
		return NULL;
	flags = mode[0] == 'r' ? O_RDONLY :
		mode[0] == 'a' ? O_WRONLY | O_CREAT | O_APPEND :
		mode[0] == 'w' ? O_WRONLY | O_CREAT | O_EXCL : -1;
	if (flags < 0)
		return NULL;
	parent_fd = open_parent_dir(path, base, sizeof(base));
	if (parent_fd < 0)
		return NULL;
	fd = openat(parent_fd, base, flags | O_CLOEXEC | O_NOFOLLOW, 0600);
	close(parent_fd);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1) {
		close(fd);
		return NULL;
	}
	file = fdopen(fd, mode);
	if (!file)
		close(fd);
	return file;
}

static int safe_stat(const char *path, struct stat *st)
{
	char base[NAME_MAX + 1];
	int parent_fd = open_parent_dir(path, base, sizeof(base));
	int rc;

	if (parent_fd < 0)
		return -1;
	rc = fstatat(parent_fd, base, st, AT_SYMLINK_NOFOLLOW);
	close(parent_fd);
	if (rc == 0 && (!S_ISREG(st->st_mode) || st->st_uid != geteuid() ||
			st->st_nlink != 1)) {
		errno = EINVAL;
		return -1;
	}
	return rc;
}

static int safe_rename(const char *old_path, const char *new_path)
{
	char old_base[NAME_MAX + 1], new_base[NAME_MAX + 1];
	struct stat old_parent_st, new_parent_st;
	int old_fd = open_parent_dir(old_path, old_base, sizeof(old_base));
	int new_fd = open_parent_dir(new_path, new_base, sizeof(new_base));
	int rc = -1;

	if (old_fd < 0 || new_fd < 0 || fstat(old_fd, &old_parent_st) != 0 ||
	    fstat(new_fd, &new_parent_st) != 0 ||
	    old_parent_st.st_dev != new_parent_st.st_dev ||
	    old_parent_st.st_ino != new_parent_st.st_ino)
		goto done;
	rc = renameat(old_fd, old_base, new_fd, new_base);
done:
	if (old_fd >= 0)
		close(old_fd);
	if (new_fd >= 0)
		close(new_fd);
	return rc;
}

static int safe_unlink(const char *path)
{
	char base[NAME_MAX + 1];
	int parent_fd = open_parent_dir(path, base, sizeof(base));
	int rc;

	if (parent_fd < 0)
		return -1;
	rc = unlinkat(parent_fd, base, 0);
	close(parent_fd);
	return rc;
}

static int fsync_dir(const char *path)
{
	int fd, rc;

	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	rc = fsync(fd);
	close(fd);
	return rc;
}

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

static int unread_file(const char *state_dir, char *out, size_t outn)
{
	if (!state_dir || !state_dir[0] || !out || outn == 0 ||
	    snprintf(out, outn, "%s/unread.tsv", state_dir) >= (int)outn)
		return -1;
	return 0;
}

static int replace_string_field(const char *line, const char *field_name,
				const char *value_text, char **out)
{
	const char *field, *value, *end;
	char prefix_text[128], esc[2800];
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
	char needle[128], esc[2800], *result;
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
	f = safe_fopen(tmp, "w");
	if (!f)
		goto fail;
	if (fchmod(fileno(f), 0600) != 0)
		goto close_fail;
	for (i = 0; i < n; i++)
		if (fprintf(f, "%s\n", lines[i]) < 0)
			goto close_fail;
	if (fclose(f) != 0)
		goto fail_tmp;
	if (safe_rename(tmp, path) != 0)
		goto fail;
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return 1;
close_fail:
	fclose(f);
fail_tmp:
	safe_unlink(tmp);
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
		if ((strcmp(state, "read") == 0 &&
		     strstr(lines[i], "\"receipt\":\"read\"")) ||
		    (strcmp(state, "delivered") == 0 &&
		     (strstr(lines[i], "\"receipt\":\"delivered\"") ||
		      strstr(lines[i], "\"receipt\":\"read\""))))
			break;
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
		return matched ? 2 : 0;
	}
	if (snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp) ||
	    !(f = safe_fopen(tmp, "w")))
		goto fail;
	if (fchmod(fileno(f), 0600) != 0)
		goto close_fail;
	for (i = 0; i < n; i++)
		if (fprintf(f, "%s\n", lines[i]) < 0)
			goto close_fail;
	if (fclose(f) != 0)
		goto fail_tmp;
	if (safe_rename(tmp, path) != 0)
		goto fail;
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return 1;
close_fail:
	fclose(f);
fail_tmp:
	safe_unlink(tmp);
fail:
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return -1;
}

static int group_reaction_field_count(const char *line)
{
	static const char marker[] = ",\"reaction_group_";
	const char *cursor = line;
	int count = 0;

	while (cursor && (cursor = strstr(cursor, marker)) != NULL) {
		const char *key = cursor + sizeof(marker) - 1;
		int valid = strlen(key) >= 66;
		for (size_t i = 0; valid && i < 64; i++)
			if (!((key[i] >= '0' && key[i] <= '9') ||
			      (key[i] >= 'a' && key[i] <= 'f'))) {
				valid = 0;
				break;
			}
		if (valid && key[64] == '"' && key[65] == ':')
			count++;
		cursor = key;
	}
	return count;
}

static int update_file_reaction(const char *path, const char *id, const char *emoji,
                                const char *field)
{
	char **lines = NULL, tmp[640];
	size_t n = 0, cap = 0, i;
	int changed = 0, unchanged = 0, failed = 0;
	FILE *f;

	if (read_lines(path, &lines, &n, &cap) != 0)
		return -1;
	for (i = 0; i < n; i++) {
		char needle[128], value_needle[256], escaped_emoji[128], *marked = NULL;
		if (snprintf(needle, sizeof(needle), "\"id\":\"%s\"", id) >= (int)sizeof(needle) ||
		    !strstr(lines[i], needle) || strstr(lines[i], "\"deleted\":true"))
			continue;
		if (omaq_json_escape(emoji, escaped_emoji, sizeof(escaped_emoji)) != 0 ||
		    snprintf(value_needle, sizeof(value_needle), "\"%s\":\"%s\"",
			     field, escaped_emoji) >= (int)sizeof(value_needle)) {
			failed = 1;
			break;
		}
		if (strstr(lines[i], value_needle)) {
			unchanged = 1;
			break;
		}
		if (strncmp(field, "reaction_group_", 15) == 0) {
			char field_needle[128];
			if (snprintf(field_needle, sizeof(field_needle), "\"%s\":", field) >=
			    (int)sizeof(field_needle) ||
			    (!strstr(lines[i], field_needle) &&
			     group_reaction_field_count(lines[i]) >= GROUP_REACTION_ACTORS_MAX)) {
				failed = 1;
				break;
			}
		}
		if (append_string_field(lines[i], field, emoji, &marked) != 0) {
			failed = 1;
			break;
		}
		free(lines[i]);
		lines[i] = marked;
		changed = 1;
		break;
	}
	if (!changed) {
		for (i = 0; i < n; i++)
			free(lines[i]);
		free(lines);
		return failed ? -1 : (unchanged ? 1 : 0);
	}
	if (snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp) ||
	    !(f = safe_fopen(tmp, "w")))
		goto fail;
	if (fchmod(fileno(f), 0600) != 0)
		goto close_fail;
	for (i = 0; i < n; i++) {
		if (fprintf(f, "%s\n", lines[i]) < 0)
			goto close_fail;
	}
	if (fclose(f) != 0)
		goto fail_tmp;
	if (safe_rename(tmp, path) != 0)
		goto fail;
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return 1;
close_fail:
	fclose(f);
fail_tmp:
	safe_unlink(tmp);
fail:
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return -1;
}

static int file_message_id(const char *path, const char *id, int include_deleted)
{
	char **lines = NULL;
	size_t n = 0, cap = 0, i;
	int found = 0;

	if (read_lines(path, &lines, &n, &cap) != 0)
		return -1;
	for (i = 0; i < n; i++) {
		char needle[128];
		if (snprintf(needle, sizeof(needle), "\"id\":\"%s\"", id) < (int)sizeof(needle) &&
		    strstr(lines[i], needle) &&
		    (include_deleted || !strstr(lines[i], "\"deleted\":true"))) {
			found = 1;
			break;
		}
	}
	for (i = 0; i < n; i++)
		free(lines[i]);
	free(lines);
	return found;
}

int omaq_store_message_exists(const char *home, const char *conv_id, const char *id)
{
	char path[576], rot[580];
	int result;

	if (!home || !conv_id || !id || !id[0] ||
	    hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	result = file_message_id(path, id, 0);
	if (result != 0)
		return result;
	return file_message_id(rot, id, 0);
}

int omaq_store_message_id_used(const char *home, const char *conv_id, const char *id)
{
	char path[576], rot[580];
	int result;

	if (!home || !conv_id || !id || !id[0] ||
	    hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	result = file_message_id(path, id, 1);
	if (result != 0)
		return result;
	return file_message_id(rot, id, 1);
}

int omaq_store_update_reaction(const char *home, const char *conv_id, const char *id,
                                const char *emoji, const char *actor)
{
	char path[576], rot[580], field[32];
	int result;

	if (!home || !conv_id || !id || !id[0] || !emoji || !actor ||
	    (strcmp(actor, "me") != 0 && strcmp(actor, "peer") != 0) ||
	    snprintf(field, sizeof(field), "reaction_%s", actor) >= (int)sizeof(field) ||
	    hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	result = update_file_reaction(path, id, emoji, field);
	if (result != 0)
		return result > 0 ? 0 : -1;
	result = update_file_reaction(rot, id, emoji, field);
	if (result < 0)
		return -1;
	return result > 0 ? 0 : -2;
}

int omaq_store_update_group_reaction(const char *home, const char *conv_id,
				     const char *id, const char *emoji,
				     const char *actor_key)
{
	char path[576], rot[580], field[96];
	int result;

	if (!home || !conv_id || !id || !id[0] || !emoji || !actor_key ||
	    strlen(actor_key) != 64)
		return -1;
	for (size_t i = 0; i < 64; i++)
		if (!((actor_key[i] >= '0' && actor_key[i] <= '9') ||
		      (actor_key[i] >= 'a' && actor_key[i] <= 'f')))
			return -1;
	if (snprintf(field, sizeof(field), "reaction_group_%s", actor_key) >=
	    (int)sizeof(field) || hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	result = update_file_reaction(path, id, emoji, field);
	if (result != 0)
		return result > 0 ? 0 : -1;
	result = update_file_reaction(rot, id, emoji, field);
	if (result < 0)
		return -1;
	return result > 0 ? 0 : -2;
}

int omaq_store_update_receipt_changed(const char *home, const char *conv_id,
				      const char *id, const char *state)
{
	char path[576], rot[580];
	int result;

	if (!home || !conv_id || !id || !id[0] || !state ||
	    (strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0) ||
	    hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	result = update_file_receipt(path, id, state);
	if (result == 1)
		return 1;
	if (result == 2)
		return 0;
	if (result < 0)
		return -1;
	result = update_file_receipt(rot, id, state);
	if (result == 1)
		return 1;
	if (result == 2)
		return 0;
	return result < 0 ? -1 : -2;
}

int omaq_store_update_receipt(const char *home, const char *conv_id, const char *id,
			       const char *state)
{
	int result = omaq_store_update_receipt_changed(home, conv_id, id, state);

	return result >= 0 ? 0 : result;
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

int omaq_store_unread_load(omaq_unread_state *state, const char *state_dir)
{
	char path[576], line[128];
	omaq_unread_state loaded;
	struct stat st;
	FILE *f;
	int fd;
	size_t bytes = 0;

	if (!state || unread_file(state_dir, path, sizeof(path)) != 0)
		return -1;
	omaq_unread_init(&loaded);
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0) {
		if (errno != ENOENT)
			return -1;
		omaq_unread_destroy(state);
		omaq_unread_init(state);
		return 0;
	}
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    st.st_size > UNREAD_STATE_BYTES || !(f = fdopen(fd, "r"))) {
		close(fd);
		omaq_unread_destroy(state);
		omaq_unread_init(state);
		return -1;
	}
	while (fgets(line, sizeof(line), f)) {
		char *tab, *count_text, *end;
		unsigned long count;
		size_t i, line_size = strlen(line);

		if (line_size > UNREAD_STATE_BYTES - bytes)
			goto invalid;
		bytes += line_size;
		end = strchr(line, '\n');
		tab = strchr(line, '\t');
		if (!end || end[1] != '\0' || !tab || tab == line || strchr(tab + 1, '\t'))
			goto invalid;
		*tab = '\0';
		*end = '\0';
		count_text = tab + 1;
		if (!count_text[0] || (count_text[0] == '0' && count_text[1]))
			goto invalid;
		for (i = 0; count_text[i]; i++) {
			if (count_text[i] < '0' || count_text[i] > '9')
				goto invalid;
		}
		errno = 0;
		count = strtoul(count_text, &end, 10);
		if (errno != 0 || !end || *end != '\0' || count > UINT_MAX)
			goto invalid;
		if (line[0] == 'g' && line[1] >= '0' && line[1] <= '9') {
			for (i = 1; line[i]; i++)
				if (line[i] < '0' || line[i] > '9')
					goto invalid;
			continue;
		}
		if (omaq_unread_set(&loaded, line, (unsigned)count) != 0)
			goto invalid;
	}
	if (ferror(f) || bytes != (size_t)st.st_size || fclose(f) != 0) {
		omaq_unread_destroy(&loaded);
		omaq_unread_destroy(state);
		omaq_unread_init(state);
		return -1;
	}
	omaq_unread_destroy(state);
	*state = loaded;
	return 0;
invalid:
	fclose(f);
	omaq_unread_destroy(&loaded);
	omaq_unread_destroy(state);
	omaq_unread_init(state);
	return -1;
}

int omaq_store_unread_save(const omaq_unread_state *state, const char *state_dir)
{
	char path[576], tmp[600];
	omaq_unread_state validated;
	FILE *f;
	size_t i, bytes = 0;

	if (!state || unread_file(state_dir, path, sizeof(path)) != 0 ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp))
		return -1;
	omaq_unread_init(&validated);
	for (i = 0; i < state->length; i++) {
		if (omaq_unread_set(&validated, state->entries[i].id,
				    state->entries[i].count) != 0) {
			omaq_unread_destroy(&validated);
			return -1;
		}
	}
	f = safe_fopen(tmp, "w");
	if (!f) {
		omaq_unread_destroy(&validated);
		return -1;
	}
	if (fchmod(fileno(f), 0600) != 0)
		goto fail;
	for (i = 0; i < validated.length; i++) {
		char line[128];
		int line_size = snprintf(line, sizeof(line), "%s\t%u\n",
					 validated.entries[i].id, validated.entries[i].count);
		if (line_size < 0 || (size_t)line_size >= sizeof(line) ||
		    (size_t)line_size > UNREAD_STATE_BYTES - bytes ||
		    fwrite(line, 1, (size_t)line_size, f) != (size_t)line_size)
			goto fail;
		bytes += (size_t)line_size;
	}
	if (fflush(f) != 0 || fsync(fileno(f)) != 0 || fclose(f) != 0) {
		safe_unlink(tmp);
		omaq_unread_destroy(&validated);
		return -1;
	}
	if (safe_rename(tmp, path) != 0) {
		safe_unlink(tmp);
		omaq_unread_destroy(&validated);
		return -1;
	}
	if (fsync_dir(state_dir) != 0) {
		omaq_unread_destroy(&validated);
		return -1;
	}
	omaq_unread_destroy(&validated);
	return 0;
fail:
	fclose(f);
	safe_unlink(tmp);
	omaq_unread_destroy(&validated);
	return -1;
}

int omaq_store_clear(const char *home, const char *conv_id)
{
	char dir[512], path[576], rot[580];
	int rc = 0;

	if (hist_dir(home, conv_id, dir, sizeof(dir)) != 0 ||
	    hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
		return -1;
	if (safe_unlink(path) != 0 && errno != ENOENT)
		rc = -1;
	if (safe_unlink(rot) != 0 && errno != ENOENT)
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
	if (safe_stat(path, &st) == 0 && st.st_size >= ROTATE_BYTES) {
		if (snprintf(rot, sizeof(rot), "%s.1", path) >= (int)sizeof(rot))
			return -1;
		if (safe_rename(path, rot) != 0)
			return -1;
	}
	f = safe_fopen(path, "a");
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
	return fclose(f) == 0 ? 0 : -1;
}

static void reset_read_lines(char ***lines, size_t *n, size_t *cap)
{
	if (!lines || !n || !cap)
		return;
	for (size_t i = 0; i < *n; i++)
		free((*lines)[i]);
	free(*lines);
	*lines = NULL;
	*n = 0;
	*cap = 0;
}

static int read_lines(const char *path, char ***lines, size_t *n, size_t *cap)
{
	FILE *f;
	char buf[STORE_LINE_MAX + 2];

	f = safe_fopen(path, "r");
	if (!f)
		return errno == ENOENT ? 0 : -1;
	while (fgets(buf, sizeof(buf), f)) {
		size_t len = strlen(buf);
		char *copy;
		if (len == 0 || len > STORE_LINE_MAX || buf[len - 1] != '\n') {
			fclose(f);
			reset_read_lines(lines, n, cap);
			return -1;
		}
		buf[--len] = '\0';
		if (omaq_json_validate(buf) != 0) {
			fclose(f);
			reset_read_lines(lines, n, cap);
			return -1;
		}
		if (*n == *cap) {
			size_t ncap = *cap ? *cap * 2 : 16;
			char **nl = realloc(*lines, ncap * sizeof(*nl));
			if (!nl) {
				fclose(f);
				reset_read_lines(lines, n, cap);
				return -1;
			}
			*lines = nl;
			*cap = ncap;
		}
		copy = malloc(len + 1);
		if (!copy) {
			fclose(f);
			reset_read_lines(lines, n, cap);
			return -1;
		}
		memcpy(copy, buf, len + 1);
		(*lines)[(*n)++] = copy;
	}
	if (ferror(f)) {
		fclose(f);
		reset_read_lines(lines, n, cap);
		return -1;
	}
	if (fclose(f) != 0) {
		reset_read_lines(lines, n, cap);
		return -1;
	}
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

static int history_lines(const char *home, const char *conv_id,
			 char ***lines, size_t *count, size_t *capacity)
{
	char path[576], rotated[580];

	if (!lines || !count || !capacity ||
	    hist_file(home, conv_id, path, sizeof(path)) != 0 ||
	    snprintf(rotated, sizeof(rotated), "%s.1", path) >= (int)sizeof(rotated) ||
	    read_lines(rotated, lines, count, capacity) != 0 ||
	    read_lines(path, lines, count, capacity) != 0) {
		reset_read_lines(lines, count, capacity);
		return -1;
	}
	return 0;
}

static int history_line_id(const char *line, char *out, size_t outn)
{
	const char *start, *end;
	size_t length;

	if (!line || !out || outn == 0 || !(start = strstr(line, "\"id\":\"")))
		return -1;
	start += strlen("\"id\":\"");
	end = strchr(start, '\"');
	if (!end || (length = (size_t)(end - start)) == 0 || length + 1u > outn)
		return -1;
	for (size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)start[i];
		if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') &&
		    !(c >= '0' && c <= '9') && c != '-' && c != '_' && c != ':' && c != '.')
			return -1;
	}
	memcpy(out, start, length);
	out[length] = '\0';
	return 0;
}

int omaq_store_unread_receipt_ids(const char *home, const char *conv_id,
				  unsigned unread, omaq_store_message_id **ids,
				  size_t *count)
{
	char **lines = NULL;
	omaq_store_message_id *result = NULL;
	size_t line_count = 0, capacity = 0, incoming = 0, result_count = 0, slots;
	int overflow = 0, rc = -1;

	if (!ids || !count)
		return -1;
	*ids = NULL;
	*count = 0;
	if (unread == 0)
		return 0;
	if (history_lines(home, conv_id, &lines, &line_count, &capacity) != 0)
		return -1;
	slots = unread > OMAQ_STORE_READ_IDS_MAX ? OMAQ_STORE_READ_IDS_MAX : unread;
	result = calloc(slots, sizeof(*result));
	if (!result)
		goto done;
	for (size_t offset = 0; offset < line_count && incoming < unread; offset++) {
		const char *line = lines[line_count - offset - 1u];
		if (!strstr(line, "\"dir\":\"in\""))
			continue;
		incoming++;
		if (strstr(line, "\"kind\":\"file\""))
			continue;
		if (result_count >= slots) {
			overflow = 1;
			continue;
		}
		if (history_line_id(line, result[result_count].id,
				    sizeof(result[result_count].id)) != 0)
			goto done;
		result_count++;
	}
	if (overflow)
		goto done;
	*ids = result;
	*count = result_count;
	result = NULL;
	rc = 0;
done:
	free(result);
	reset_read_lines(&lines, &line_count, &capacity);
	return rc;
}

int omaq_store_message_from_matches(const char *home, const char *conv_id,
				    const char *id, const char *expected_from)
{
	char **lines = NULL, id_needle[128], from_needle[192];
	size_t count = 0, capacity = 0;
	int result = -2;

	if (!id || !expected_from ||
	    snprintf(id_needle, sizeof(id_needle), "\"id\":\"%s\"", id) >=
		(int)sizeof(id_needle) ||
	    snprintf(from_needle, sizeof(from_needle), "\"from\":\"%s\"", expected_from) >=
		(int)sizeof(from_needle) ||
	    history_lines(home, conv_id, &lines, &count, &capacity) != 0)
		return -1;
	for (size_t i = 0; i < count; i++) {
		if (!strstr(lines[i], id_needle))
			continue;
		result = strstr(lines[i], from_needle) ? 1 : 0;
		break;
	}
	reset_read_lines(&lines, &count, &capacity);
	return result;
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
