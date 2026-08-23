#define _DEFAULT_SOURCE
#include "../helper/stdout_spool.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define OLD_URGENT_LIMIT (4096u * 1024u)

static int fails;

struct sink {
	unsigned char *data;
	size_t len;
	size_t cap;
	size_t max_write;
	int eagain;
};

static void fail(const char *message)
{
	fprintf(stderr, "FAIL: %s\n", message);
	fails++;
}

static int append_bytes(unsigned char **data, size_t *len, size_t *cap,
			const void *src, size_t n)
{
	unsigned char *next;
	size_t need;
	size_t next_cap;

	if (n > SIZE_MAX - *len)
		return -1;
	need = *len + n;
	if (need <= *cap) {
		memcpy(*data + *len, src, n);
		*len = need;
		return 0;
	}
	next_cap = *cap ? *cap : 4096;
	while (next_cap < need) {
		if (next_cap > SIZE_MAX / 2) {
			next_cap = need;
			break;
		}
		next_cap *= 2;
	}
	next = realloc(*data, next_cap);
	if (!next)
		return -1;
	*data = next;
	*cap = next_cap;
	memcpy(*data + *len, src, n);
	*len = need;
	return 0;
}

static ssize_t sink_write(void *ctx, int fd, const void *buf, size_t len)
{
	struct sink *sink = ctx;
	size_t take = len;

	(void)fd;
	if (sink->eagain > 0) {
		sink->eagain--;
		errno = EAGAIN;
		return -1;
	}
	if (sink->max_write && take > sink->max_write)
		take = sink->max_write;
	if (append_bytes(&sink->data, &sink->len, &sink->cap, buf, take) != 0) {
		errno = ENOMEM;
		return -1;
	}
	return (ssize_t)take;
}

static int drain(omaq_stdout_spool *spool)
{
	unsigned int loops = 0;

	while (omaq_stdout_spool_pending(spool)) {
		int rc = omaq_stdout_spool_flush(spool);
		if (rc < 0)
			return -1;
		if (++loops > 1000000) {
			errno = ETIMEDOUT;
			return -1;
		}
	}
	return 0;
}

static int mode_is_0600(const char *dir, const char *name)
{
	char path[512];
	struct stat st;

	if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path))
		return 0;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0600;
}

static int spool_size(const char *dir, off_t *size)
{
	char path[512];
	struct stat st;

	if (snprintf(path, sizeof(path), "%s/%s", dir, OMAQ_STDOUT_SPOOL_FILE) >=
	    (int)sizeof(path) || stat(path, &st) != 0)
		return -1;
	*size = st.st_size;
	return 0;
}

int main(void)
{
	char dir[] = "/tmp/omaq-spool-XXXXXX";
	char path[512];
	char quarantine_path[512] = "";
	omaq_stdout_spool *spool = NULL;
	struct sink sink = { 0 };
	const char ordered[] =
		"{\"event\":\"first\"}\n"
		"{\"event\":\"second\"}\n";
	off_t size = 0;
	unsigned char *expected = NULL;
	size_t expected_len = 0;
	size_t expected_cap = 0;

	if (!mkdtemp(dir)) {
		fail("mkdtemp");
		goto out;
	}
	spool = omaq_stdout_spool_open(dir, -1);
	if (!spool) {
		fail("spool open");
		goto out;
	}
	if (!mode_is_0600(dir, OMAQ_STDOUT_SPOOL_FILE) ||
	    !mode_is_0600(dir, OMAQ_STDOUT_CURSOR_FILE))
		fail("spool mode 0600");
	{
		omaq_stdout_spool *second = omaq_stdout_spool_open(dir, -1);
		if (second || (errno != EWOULDBLOCK && errno != EAGAIN))
			fail("spool rejects a concurrent writer");
		omaq_stdout_spool_close(second);
	}
	if (omaq_stdout_spool_append(spool, "{\"event\":\"first\"}") != 0 ||
	    omaq_stdout_spool_append(spool, "{\"event\":\"second\"}") != 0)
		fail("ordered append");
	sink.max_write = 3;
	sink.eagain = 1;
	omaq_stdout_spool_set_writer(spool, sink_write, &sink);
	if (omaq_stdout_spool_flush(spool) != OMAQ_STDOUT_FLUSH_IDLE ||
	    !omaq_stdout_spool_pending(spool))
		fail("EAGAIN preserves pending data");
	if (drain(spool) != 0 || sink.len != sizeof(ordered) - 1 ||
	    memcmp(sink.data, ordered, sizeof(ordered) - 1) != 0)
		fail("partial writes preserve FIFO output");
	if (spool_size(dir, &size) != 0 || size != 0)
		fail("drained spool reset");
	free(sink.data);
	memset(&sink, 0, sizeof(sink));

	/* A partially written record is replayed in full on a fresh output stream. */
	if (omaq_stdout_spool_append(spool, "{\"event\":\"first\"}") != 0 ||
	    omaq_stdout_spool_append(spool, "{\"event\":\"second\"}") != 0)
		fail("restart append");
	sink.max_write = 5;
	omaq_stdout_spool_set_writer(spool, sink_write, &sink);
	if (omaq_stdout_spool_flush(spool) != OMAQ_STDOUT_FLUSH_PROGRESS || sink.len != 5)
		fail("restart partial prefix");
	omaq_stdout_spool_close(spool);
	spool = omaq_stdout_spool_open(dir, -1);
	if (!spool) {
		fail("restart spool open");
		goto out;
	}
	free(sink.data);
	memset(&sink, 0, sizeof(sink));
	sink.max_write = 7;
	sink.eagain = 1;
	omaq_stdout_spool_set_writer(spool, sink_write, &sink);
	if (drain(spool) != 0 || sink.len != sizeof(ordered) - 1 ||
	    memcmp(sink.data, ordered, sizeof(ordered) - 1) != 0)
		fail("restart replays complete critical records");
	free(sink.data);
	memset(&sink, 0, sizeof(sink));

	/* Exceed the former 4 MiB urgent queue while output remains blocked. */
	for (int i = 0; i < 1100; i++) {
		char event[4096];
		int prefix = snprintf(event, sizeof(event),
				      "{\"event\":\"message\",\"seq\":%d,\"text\":\"", i);
		size_t event_len = 4080;

		if (prefix < 0 || (size_t)prefix + 2 >= event_len) {
			fail("overflow fixture prefix");
			break;
		}
		memset(event + prefix, 'x', event_len - (size_t)prefix - 2);
		event[event_len - 2] = '"';
		event[event_len - 1] = '}';
		event[event_len] = '\0';
		if (omaq_stdout_spool_append(spool, event) != 0 ||
		    append_bytes(&expected, &expected_len, &expected_cap, event, event_len) != 0 ||
		    append_bytes(&expected, &expected_len, &expected_cap, "\n", 1) != 0) {
			fail("overflow append");
			break;
		}
	}
	if (expected_len <= OLD_URGENT_LIMIT || spool_size(dir, &size) != 0 ||
	    size != (off_t)expected_len)
		fail("blocked stdout spool exceeds old limit");
	{
		size_t event_len = OMAQ_STDOUT_SPOOL_MAX - expected_len - 1u;
		char *event = malloc(event_len + 1u);

		if (!event || event_len < 2 || event_len > OMAQ_STDOUT_RECORD_MAX) {
			fail("aggregate spool limit fixture");
		} else {
			memset(event, 'y', event_len);
			event[0] = '{';
			event[event_len - 1u] = '}';
			event[event_len] = '\0';
			if (omaq_stdout_spool_append(spool, event) != 0 ||
			    append_bytes(&expected, &expected_len, &expected_cap, event, event_len) != 0 ||
			    append_bytes(&expected, &expected_len, &expected_cap, "\n", 1) != 0 ||
			    spool_size(dir, &size) != 0 || size != (off_t)OMAQ_STDOUT_SPOOL_MAX)
				fail("aggregate spool accepts exact limit");
			if (omaq_stdout_spool_append(spool, "{}") == 0 || errno != ENOSPC ||
			    spool_size(dir, &size) != 0 || size != (off_t)OMAQ_STDOUT_SPOOL_MAX)
				fail("aggregate spool rejects above limit");
		}
		free(event);
	}
	omaq_stdout_spool_close(spool);
	spool = omaq_stdout_spool_open(dir, -1);
	if (!spool) {
		fail("overflow replay open");
		goto out;
	}
	sink.max_write = 137;
	sink.eagain = 2;
	omaq_stdout_spool_set_writer(spool, sink_write, &sink);
	if (drain(spool) != 0 || sink.len != expected_len ||
	    memcmp(sink.data, expected, expected_len) != 0)
		fail("overflow replay preserves all critical FIFO events");
	if (spool_size(dir, &size) != 0 || size != 0)
		fail("overflow replay drains spool");
	{
		char *aggregate = malloc(OMAQ_STDOUT_SPOOL_MAX + 1u);
		if (!aggregate)
			fail("aggregate oversized record allocation");
		else {
			memset(aggregate, 'z', OMAQ_STDOUT_SPOOL_MAX);
			aggregate[0] = '{';
			aggregate[OMAQ_STDOUT_SPOOL_MAX - 1u] = '}';
			aggregate[OMAQ_STDOUT_SPOOL_MAX] = '\0';
			if (omaq_stdout_spool_append(spool, aggregate) == 0 || errno != ENOSPC)
				fail("single record cannot underflow aggregate cap");
			free(aggregate);
		}
	}
	{
		char *oversized = malloc(OMAQ_STDOUT_RECORD_MAX + 2u);
		if (!oversized)
			fail("oversized record allocation");
		else {
			memset(oversized, 'x', OMAQ_STDOUT_RECORD_MAX + 1u);
			oversized[0] = '{';
			oversized[OMAQ_STDOUT_RECORD_MAX] = '}';
			oversized[OMAQ_STDOUT_RECORD_MAX + 1u] = '\0';
			if (omaq_stdout_spool_append(spool, oversized) == 0 || errno != EOVERFLOW)
				fail("oversized critical record rejected");
			free(oversized);
		}
	}

	/* A crash-truncated record is quarantined without blocking committed replay. */
	{
		static const char committed[] = "{\"event\":\"committed\"}";
		if (omaq_stdout_spool_append(spool, committed) != 0)
			fail("committed record before incomplete tail");
	}
	free(sink.data);
	memset(&sink, 0, sizeof(sink));
	omaq_stdout_spool_close(spool);
	spool = NULL;
	if (snprintf(path, sizeof(path), "%s/%s", dir, OMAQ_STDOUT_SPOOL_FILE) >=
	    (int)sizeof(path) ||
	    snprintf(quarantine_path, sizeof(quarantine_path),
		     "%s/stdout-critical.incomplete.%ld.0", dir, (long)getpid()) >=
	    (int)sizeof(quarantine_path)) {
		fail("incomplete spool path");
	} else {
		static const char incomplete[] = "{\"event\":";
		char quarantined[sizeof(incomplete)] = "";
		struct stat st;
		int fd = open(path, O_WRONLY | O_APPEND);

		if (fd < 0 || write(fd, incomplete, sizeof(incomplete) - 1) !=
		    (ssize_t)(sizeof(incomplete) - 1) || fdatasync(fd) != 0) {
			fail("incomplete spool fixture");
		} else {
			close(fd);
			fd = -1;
			spool = omaq_stdout_spool_open(dir, -1);
			fd = open(quarantine_path, O_RDONLY);
			if (!spool || spool_size(dir, &size) != 0 ||
			    size != (off_t)sizeof("{\"event\":\"committed\"}\n") - 1 || fd < 0 ||
			    read(fd, quarantined, sizeof(incomplete) - 1) !=
				(ssize_t)(sizeof(incomplete) - 1) ||
			    memcmp(quarantined, incomplete, sizeof(incomplete) - 1) != 0 ||
			    fstat(fd, &st) != 0 || (st.st_mode & 0777) != 0600)
				fail("incomplete spool quarantine recovery");
			if (spool) {
				static const char committed_line[] = "{\"event\":\"committed\"}\n";
				sink.max_write = 4;
				omaq_stdout_spool_set_writer(spool, sink_write, &sink);
				if (drain(spool) != 0 || sink.len != sizeof(committed_line) - 1 ||
				    memcmp(sink.data, committed_line, sizeof(committed_line) - 1) != 0)
					fail("committed replay survives incomplete tail");
			}
		}
		if (fd >= 0)
			close(fd);
	}

out:
	omaq_stdout_spool_close(spool);
	free(sink.data);
	free(expected);
	if (dir[0]) {
		if (snprintf(path, sizeof(path), "%s/%s", dir, OMAQ_STDOUT_SPOOL_FILE) <
		    (int)sizeof(path))
			unlink(path);
		if (snprintf(path, sizeof(path), "%s/%s", dir, OMAQ_STDOUT_CURSOR_FILE) <
		    (int)sizeof(path))
			unlink(path);
		if (quarantine_path[0])
			unlink(quarantine_path);
		rmdir(dir);
	}
	if (fails) {
		fprintf(stderr, "stdout_spool_test: %d failure(s)\n", fails);
		return 1;
	}
	puts("stdout_spool_test: ok");
	return 0;
}
