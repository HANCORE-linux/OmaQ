#define _DEFAULT_SOURCE
#include "proxy.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int host_char_ok(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_' ||
	       c == ':';
}

static int host_ok(const char *host, size_t n)
{
	size_t i;

	if (n == 0 || n >= OMAQ_PROXY_HOST_MAX)
		return 0;
	for (i = 0; i < n; i++) {
		if (!host_char_ok(host[i]))
			return 0;
	}
	return 1;
}

static int parse_port(const char *text, size_t n, unsigned *out)
{
	unsigned value = 0;
	size_t i;

	if (n == 0 || n > 5)
		return -1;
	for (i = 0; i < n; i++) {
		if (text[i] < '0' || text[i] > '9')
			return -1;
		value = value * 10u + (unsigned)(text[i] - '0');
	}
	if (value == 0 || value > 65535u)
		return -1;
	*out = value;
	return 0;
}

static size_t token(const char *line, size_t from, size_t length, size_t *end)
{
	size_t start = from;

	while (start < length && (line[start] == ' ' || line[start] == '\t'))
		start++;
	*end = start;
	while (*end < length && line[*end] != ' ' && line[*end] != '\t')
		(*end)++;
	return start;
}

static int parse_directive(const char *line, size_t length, omaq_proxy *out)
{
	size_t start, end, next_start, next_end;
	omaq_proxy parsed;

	memset(&parsed, 0, sizeof(parsed));
	start = token(line, 0, length, &end);
	if (start == end)
		return -1;
	if (end - start == 4 && memcmp(line + start, "none", 4) == 0) {
		parsed.type = OMAQ_PROXY_NONE;
		next_start = token(line, end, length, &next_end);
		if (next_start != next_end)
			return -1;
		*out = parsed;
		return 0;
	}
	if (end - start == 6 && memcmp(line + start, "socks5", 6) == 0)
		parsed.type = OMAQ_PROXY_SOCKS5;
	else if (end - start == 4 && memcmp(line + start, "http", 4) == 0)
		parsed.type = OMAQ_PROXY_HTTP;
	else
		return -1;

	start = token(line, end, length, &end);
	if (start == end || !host_ok(line + start, end - start))
		return -1;
	memcpy(parsed.host, line + start, end - start);
	parsed.host[end - start] = '\0';

	start = token(line, end, length, &end);
	if (start == end || parse_port(line + start, end - start, &parsed.port) != 0)
		return -1;

	/* Nothing may follow the port: an unparsed trailing field would mean the
	 * file says something the helper is not honouring. */
	next_start = token(line, end, length, &next_end);
	if (next_start != next_end)
		return -1;
	*out = parsed;
	return 0;
}

int omaq_proxy_parse(const char *text, omaq_proxy *out)
{
	const char *cursor;
	omaq_proxy parsed;
	int found = 0;

	if (!text || !out)
		return -1;
	memset(&parsed, 0, sizeof(parsed));
	cursor = text;
	while (*cursor) {
		const char *newline = strchr(cursor, '\n');
		size_t length = newline ? (size_t)(newline - cursor) : strlen(cursor);
		size_t start, end;

		if (length > 0 && cursor[length - 1] == '\r')
			length--;
		start = token(cursor, 0, length, &end);
		if (start != end && cursor[start] != '#') {
			if (found)
				return -1; /* one directive only */
			if (parse_directive(cursor, length, &parsed) != 0)
				return -1;
			found = 1;
		}
		if (!newline)
			break;
		cursor = newline + 1;
	}
	if (!found)
		return -1;
	*out = parsed;
	return 0;
}

int omaq_proxy_load(const char *home, omaq_proxy *out)
{
	char path[512];
	char body[OMAQ_PROXY_FILE_MAX + 1];
	struct stat st;
	ssize_t got;
	int fd;

	if (!home || !out)
		return -1;
	if (snprintf(path, sizeof(path), "%s/proxy.conf", home) >= (int)sizeof(path))
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	/* Only a genuinely absent file means "no proxy". A symlink (ELOOP under
	 * O_NOFOLLOW) or any other error fails closed. */
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0022) ||
	    st.st_size > (off_t)OMAQ_PROXY_FILE_MAX) {
		close(fd);
		return -1;
	}
	got = read(fd, body, sizeof(body) - 1);
	close(fd);
	if (got < 0)
		return -1;
	body[got] = '\0';
	if ((size_t)got != strlen(body))
		return -1; /* embedded NUL */
	if (omaq_proxy_parse(body, out) != 0)
		return -1;
	return 1;
}
