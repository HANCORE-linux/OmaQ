#define _DEFAULT_SOURCE
#include "surface.h"
#include "json_io.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SURFACE_FILE_MAX 65536
#define SURFACE_LINE_MAX 511

static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

static int parse_string(const char **cursor, char *out, size_t out_size)
{
	const char *p;
	size_t length = 0;

	if (!cursor || !*cursor || !out || out_size == 0 || **cursor != '"')
		return -1;
	p = *cursor + 1;
	while (*p && *p != '"') {
		unsigned char value = (unsigned char)*p++;

		if (value == '\\') {
			value = (unsigned char)*p++;
			if (value == 'n')
				value = '\n';
			else if (value == 'r')
				value = '\r';
			else if (value == 't')
				value = '\t';
			else if (value != '"' && value != '\\')
				return -1;
		} else if (value < 0x20) {
			return -1;
		}
		if (length + 1 >= out_size)
			return -1;
		out[length++] = (char)value;
	}
	if (*p != '"')
		return -1;
	out[length] = '\0';
	*cursor = p + 1;
	return 0;
}

static int parse_int(const char **cursor, int *out)
{
	const char *p;
	long value = 0;
	int negative = 0, any = 0;

	if (!cursor || !*cursor || !out)
		return -1;
	p = *cursor;
	if (*p == '-') {
		negative = 1;
		p++;
	}
	while (*p >= '0' && *p <= '9') {
		int digit = *p++ - '0';
		if (value > (INT_MAX - digit) / 10L)
			return -1;
		value = value * 10L + digit;
		any = 1;
	}
	if (!any)
		return -1;
	*out = negative ? -(int)value : (int)value;
	*cursor = p;
	return 0;
}

static int parse_bool(const char **cursor, int *out)
{
	if (!cursor || !*cursor || !out)
		return -1;
	if (strncmp(*cursor, "true", 4) == 0) {
		*out = 1;
		*cursor += 4;
		return 0;
	}
	if (strncmp(*cursor, "false", 5) == 0) {
		*out = 0;
		*cursor += 5;
		return 0;
	}
	return -1;
}

static int stable_conversation_ok(const char *conversation)
{
	size_t length;

	if (!conversation || (length = strlen(conversation)) != 66 ||
	    (conversation[0] != 'd' && conversation[0] != 'g') ||
	    conversation[1] != ':')
		return 0;
	for (size_t i = 2; i < length; i++)
		if (!((conversation[i] >= '0' && conversation[i] <= '9') ||
		      (conversation[i] >= 'a' && conversation[i] <= 'f')))
			return 0;
	return 1;
}

static int legacy_direct_ok(const char *conversation)
{
	uint64_t value = 0;

	if (!conversation || !conversation[0] ||
	    (conversation[0] == '0' && conversation[1]))
		return 0;
	for (size_t i = 0; conversation[i]; i++) {
		uint32_t digit;
		if (conversation[i] < '0' || conversation[i] > '9')
			return 0;
		digit = (uint32_t)(conversation[i] - '0');
		if (value > (UINT32_MAX - digit) / 10u)
			return 0;
		value = value * 10u + digit;
	}
	return 1;
}

static int parse_line(const char *line, omaq_surface *surface)
{
	const char *p;
	unsigned fields = 0;
	int first = 1;

	if (!line || !surface || omaq_json_validate(line) != 0)
		return -1;
	memset(surface, 0, sizeof(*surface));
	p = skip_ws(line);
	if (*p++ != '{')
		return -1;
	p = skip_ws(p);
	while (*p && *p != '}') {
		char key[32];
		unsigned bit;

		if (!first) {
			if (*p++ != ',')
				return -1;
			p = skip_ws(p);
		}
		first = 0;
		if (parse_string(&p, key, sizeof(key)) != 0)
			return -1;
		p = skip_ws(p);
		if (*p++ != ':')
			return -1;
		p = skip_ws(p);
		if (strcmp(key, "conversation") == 0) {
			bit = 1u << 0;
			if ((fields & bit) ||
			    parse_string(&p, surface->conversation,
					 sizeof(surface->conversation)) != 0)
				return -1;
		} else if (strcmp(key, "monitor") == 0) {
			bit = 1u << 1;
			if ((fields & bit) ||
			    parse_string(&p, surface->monitor, sizeof(surface->monitor)) != 0)
				return -1;
		} else if (strcmp(key, "x") == 0) {
			bit = 1u << 2;
			if ((fields & bit) || parse_int(&p, &surface->x) != 0)
				return -1;
		} else if (strcmp(key, "y") == 0) {
			bit = 1u << 3;
			if ((fields & bit) || parse_int(&p, &surface->y) != 0)
				return -1;
		} else if (strcmp(key, "pinned") == 0) {
			bit = 1u << 4;
			if ((fields & bit) || parse_bool(&p, &surface->pinned) != 0)
				return -1;
		} else {
			return -1;
		}
		fields |= bit;
		p = skip_ws(p);
	}
	if (*p++ != '}' || *skip_ws(p) != '\0' || fields != 0x1fu)
		return -1;
	return stable_conversation_ok(surface->conversation) ||
		legacy_direct_ok(surface->conversation) ? 0 : -1;
}

static int state_directory(const char *state, int create)
{
	struct stat status;
	int fd;

	if (!state || !state[0])
		return -1;
	if (create && mkdir(state, 0700) != 0 && errno != EEXIST)
		return -1;
	fd = open(state, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	if (fstat(fd, &status) != 0 || !S_ISDIR(status.st_mode) ||
	    status.st_uid != geteuid() || (status.st_mode & 0077) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int load_all_fd(int directory, omaq_surface *surfaces, int *count,
		       int *legacy_direct)
{
	struct stat status;
	FILE *file = NULL;
	char line[SURFACE_LINE_MAX + 2];
	int fd = -1, result = -1;

	if (directory < 0 || !surfaces || !count || !legacy_direct)
		return -1;
	*count = 0;
	*legacy_direct = 0;
	fd = openat(directory, "surfaces.jsonl",
		    O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
	    status.st_uid != geteuid() || status.st_nlink != 1 ||
	    (status.st_mode & 0077) != 0 || status.st_size < 0 ||
	    status.st_size > SURFACE_FILE_MAX)
		goto done;
	file = fdopen(fd, "r");
	if (!file)
		goto done;
	fd = -1;
	while (fgets(line, sizeof(line), file)) {
		size_t length = strlen(line);
		omaq_surface parsed;

		if (length == 0 || length > SURFACE_LINE_MAX || line[length - 1] != '\n' ||
		    *count >= OMAQ_SURFACE_MAX)
			goto done;
		line[--length] = '\0';
		if (parse_line(line, &parsed) != 0)
			goto done;
		for (int i = 0; i < *count; i++)
			if (strcmp(surfaces[i].conversation, parsed.conversation) == 0)
				goto done;
		surfaces[(*count)++] = parsed;
		if (legacy_direct_ok(parsed.conversation))
			*legacy_direct = 1;
	}
	if (ferror(file))
		goto done;
	result = 0;
done:
	if (file) {
		if (fclose(file) != 0)
			result = -1;
	} else if (fd >= 0) {
		close(fd);
	}
	if (result != 0) {
		*count = 0;
		*legacy_direct = 0;
	}
	return result;
}

static int remove_safe_temporary(int directory)
{
	struct stat status;

	if (fstatat(directory, "surfaces.jsonl.tmp", &status,
		    AT_SYMLINK_NOFOLLOW) != 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
	    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
	    status.st_size < 0 || status.st_size > SURFACE_FILE_MAX)
		return -1;
	return unlinkat(directory, "surfaces.jsonl.tmp", 0);
}

static int write_all_fd(int directory, const omaq_surface *surfaces, int count)
{
	FILE *file = NULL;
	int fd = -1, result = -1;

	if (directory < 0 || !surfaces || count < 0 || count > OMAQ_SURFACE_MAX ||
	    remove_safe_temporary(directory) != 0)
		return -1;
	fd = openat(directory, "surfaces.jsonl.tmp",
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	file = fdopen(fd, "w");
	if (!file)
		goto done;
	fd = -1;
	for (int i = 0; i < count; i++) {
		char escaped_conversation[160], escaped_monitor[128];

		if (!stable_conversation_ok(surfaces[i].conversation) ||
		    omaq_json_escape(surfaces[i].conversation, escaped_conversation,
				     sizeof(escaped_conversation)) != 0 ||
		    omaq_json_escape(surfaces[i].monitor, escaped_monitor,
				     sizeof(escaped_monitor)) != 0 ||
		    fprintf(file,
			    "{\"conversation\":\"%s\",\"monitor\":\"%s\",\"x\":%d,\"y\":%d,\"pinned\":%s}\n",
			    escaped_conversation, escaped_monitor, surfaces[i].x,
			    surfaces[i].y, surfaces[i].pinned ? "true" : "false") < 0)
			goto done;
	}
	if (fflush(file) != 0 || fsync(fileno(file)) != 0 || fclose(file) != 0) {
		file = NULL;
		goto done;
	}
	file = NULL;
	if (renameat(directory, "surfaces.jsonl.tmp", directory,
		     "surfaces.jsonl") != 0 || fsync(directory) != 0)
		goto done;
	result = 0;
done:
	if (file)
		fclose(file);
	else if (fd >= 0)
		close(fd);
	if (result != 0)
		(void)unlinkat(directory, "surfaces.jsonl.tmp", 0);
	return result;
}

int omaq_surface_legacy_direct_present(const char *state)
{
	omaq_surface surfaces[OMAQ_SURFACE_MAX];
	int directory, count = 0, legacy = 0, result;

	directory = state_directory(state, 0);
	if (directory < 0)
		return errno == ENOENT ? 0 : -1;
	result = load_all_fd(directory, surfaces, &count, &legacy);
	close(directory);
	return result == 0 ? legacy : -1;
}

int omaq_surface_discard_legacy_direct(const char *state)
{
	omaq_surface surfaces[OMAQ_SURFACE_MAX], stable[OMAQ_SURFACE_MAX];
	int directory, count = 0, stable_count = 0, legacy = 0, result = -1;

	directory = state_directory(state, 0);
	if (directory < 0)
		return -1;
	if (load_all_fd(directory, surfaces, &count, &legacy) != 0)
		goto done;
	if (!legacy) {
		result = 0;
		goto done;
	}
	for (int i = 0; i < count; i++)
		if (stable_conversation_ok(surfaces[i].conversation))
			stable[stable_count++] = surfaces[i];
	result = write_all_fd(directory, stable, stable_count) == 0 ? 1 : -1;
done:
	close(directory);
	return result;
}

int omaq_surface_set(const char *state, const omaq_surface *surface)
{
	omaq_surface surfaces[OMAQ_SURFACE_MAX];
	int directory, count = 0, legacy = 0, found = 0, result = -1;

	if (!surface || !stable_conversation_ok(surface->conversation))
		return -1;
	directory = state_directory(state, 1);
	if (directory < 0)
		return -1;
	if (load_all_fd(directory, surfaces, &count, &legacy) != 0 || legacy)
		goto done;
	for (int i = 0; i < count; i++)
		if (strcmp(surfaces[i].conversation, surface->conversation) == 0) {
			surfaces[i] = *surface;
			found = 1;
			break;
		}
	if (!found) {
		if (count >= OMAQ_SURFACE_MAX)
			goto done;
		surfaces[count++] = *surface;
	}
	result = write_all_fd(directory, surfaces, count);
done:
	close(directory);
	return result;
}

int omaq_surface_list(const char *state, omaq_surface *out, int capacity)
{
	omaq_surface surfaces[OMAQ_SURFACE_MAX];
	int directory, count = 0, legacy = 0, copy;

	if (!out || capacity <= 0)
		return -1;
	directory = state_directory(state, 0);
	if (directory < 0)
		return errno == ENOENT ? 0 : -1;
	if (load_all_fd(directory, surfaces, &count, &legacy) != 0 || legacy) {
		close(directory);
		return -1;
	}
	close(directory);
	copy = count < capacity ? count : capacity;
	if (copy > 0)
		memcpy(out, surfaces, (size_t)copy * sizeof(*out));
	return copy;
}

int omaq_surface_get(const char *state, const char *conversation, omaq_surface *surface)
{
	omaq_surface surfaces[OMAQ_SURFACE_MAX];
	int directory, count = 0, legacy = 0, result = -1;

	if (!surface || !stable_conversation_ok(conversation))
		return -1;
	directory = state_directory(state, 0);
	if (directory < 0)
		return -1;
	if (load_all_fd(directory, surfaces, &count, &legacy) != 0 || legacy)
		goto done;
	for (int i = 0; i < count; i++)
		if (strcmp(surfaces[i].conversation, conversation) == 0) {
			*surface = surfaces[i];
			result = 0;
			break;
		}
done:
	close(directory);
	return result;
}
