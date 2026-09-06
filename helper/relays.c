#define _DEFAULT_SOURCE
#include "relays.h"

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

	if (n == 0 || n >= OMAQ_RELAY_HOST_MAX)
		return 0;
	for (i = 0; i < n; i++) {
		if (!host_char_ok(host[i]))
			return 0;
	}
	return 1;
}

static int parse_port(const char *text, size_t n, uint16_t *out)
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
	*out = (uint16_t)value;
	return 0;
}

static int key_ok(const char *text, size_t n, char *out)
{
	size_t i;

	if (n != OMAQ_RELAY_KEY_HEX)
		return 0;
	for (i = 0; i < n; i++) {
		char c = text[i];

		if (c >= 'a' && c <= 'f')
			c = (char)(c - 'a' + 'A');
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')))
			return 0;
		out[i] = c;
	}
	out[n] = '\0';
	return 1;
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

static int parse_entry(const char *line, size_t length, omaq_relay *out)
{
	omaq_relay parsed;
	size_t start, end;

	memset(&parsed, 0, sizeof(parsed));
	start = token(line, 0, length, &end);
	if (start == end || !host_ok(line + start, end - start))
		return -1;
	memcpy(parsed.host, line + start, end - start);
	parsed.host[end - start] = '\0';

	start = token(line, end, length, &end);
	if (start == end || parse_port(line + start, end - start, &parsed.udp_port) != 0)
		return -1;

	start = token(line, end, length, &end);
	if (start == end || parse_port(line + start, end - start, &parsed.tcp_port) != 0)
		return -1;

	start = token(line, end, length, &end);
	if (start == end || !key_ok(line + start, end - start, parsed.key_hex))
		return -1;

	/* Nothing may follow the key: an unparsed field would mean the file says
	 * something the helper is not honouring. */
	start = token(line, end, length, &end);
	if (start != end)
		return -1;
	*out = parsed;
	return 0;
}

int omaq_relays_parse(const char *text, omaq_relay_set *out)
{
	omaq_relay_set parsed;
	const char *cursor;

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
			if (end - start == 9 &&
			    memcmp(cursor + start, "exclusive", 9) == 0) {
				size_t next_start, next_end;

				next_start = token(cursor, end, length, &next_end);
				if (next_start != next_end || parsed.exclusive)
					return -1;
				parsed.exclusive = 1;
			} else {
				size_t i;

				if (parsed.count >= OMAQ_RELAY_MAX)
					return -1;
				if (parse_entry(cursor, length,
						&parsed.entries[parsed.count]) != 0)
					return -1;
				for (i = 0; i < parsed.count; i++) {
					if (strcmp(parsed.entries[i].key_hex,
						   parsed.entries[parsed.count].key_hex) == 0)
						return -1; /* duplicate relay */
				}
				parsed.count++;
			}
		}
		if (!newline)
			break;
		cursor = newline + 1;
	}
	/* "exclusive" with no relay would leave the client with no way to reach
	 * the network at all; refuse rather than silently going offline. */
	if (parsed.count == 0)
		return -1;
	*out = parsed;
	return 0;
}

int omaq_relays_load(const char *home, omaq_relay_set *out)
{
	char path[512];
	char body[OMAQ_RELAY_FILE_MAX + 1];
	struct stat st;
	ssize_t got;
	int fd;

	if (!home || !out)
		return -1;
	if (snprintf(path, sizeof(path), "%s/relays.conf", home) >= (int)sizeof(path))
		return -1;
	/* Only a genuinely absent file means "use the pinned relays". A symlink
	 * (ELOOP under O_NOFOLLOW) or any other error fails closed. */
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0022) ||
	    st.st_size > (off_t)OMAQ_RELAY_FILE_MAX) {
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
	if (omaq_relays_parse(body, out) != 0)
		return -1;
	return 1;
}
