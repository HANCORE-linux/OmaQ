#define _DEFAULT_SOURCE
#include "../helper/store.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

enum fault { NONE, WRITE, FLUSH, FILE_SYNC, CLOSE, DIR_SYNC_1, DIR_SYNC_2, DIR_SYNC_3 };
static enum fault fault;
static int armed, triggered, file_fd, directory_syncs, file_syncs, flushed;
static char home[256], history[320], conversation[384], path[448], rotated[452];
static const char *first = "{\"id\":\"first\",\"text\":\"hello\"}";
static const char *second = "{\"id\":\"second\",\"text\":\"world\"}";

int __real_fflush(FILE *);
int __real_fsync(int);
int __real_fclose(FILE *);

static int inject(enum fault point)
{
	if (!armed || fault != point || triggered)
		return 0;
	triggered = 1;
	errno = EIO;
	return 1;
}

int __wrap_fprintf(FILE *f, const char *format, ...)
{
	va_list args;
	int rc;

	if (armed)
		file_fd = fileno(f);
	if (inject(WRITE))
		return -1;
	va_start(args, format);
	rc = vfprintf(f, format, args);
	va_end(args);
	return rc;
}

int __wrap_fflush(FILE *f)
{
	if (inject(FLUSH))
		return EOF;
	int rc = __real_fflush(f);
	if (armed && rc == 0)
		flushed = 1;
	return rc;
}

int __wrap_fsync(int fd)
{
	struct stat st, expected;

	if (!armed)
		return __real_fsync(fd);
	assert(fstat(fd, &st) == 0);
	if (S_ISREG(st.st_mode)) {
		assert(flushed);
		file_syncs++;
		if (inject(FILE_SYNC))
			return -1;
	} else {
		const char *parents[] = { conversation, history, home };
		assert(S_ISDIR(st.st_mode));
		assert(file_syncs == 1 && directory_syncs < 3);
		assert(stat(parents[directory_syncs], &expected) == 0);
		assert(st.st_dev == expected.st_dev && st.st_ino == expected.st_ino);
		directory_syncs++;
		if (inject((enum fault)(DIR_SYNC_1 + directory_syncs - 1)))
			return -1;
	}
	return __real_fsync(fd);
}

int __wrap_fclose(FILE *f)
{
	int rc = __real_fclose(f);
	if (inject(CLOSE))
		return EOF;
	return rc;
}

static void setup(void)
{
	strcpy(home, "/tmp/omaq-append-test-XXXXXX");
	assert(mkdtemp(home));
	snprintf(history, sizeof(history), "%s/history", home);
	snprintf(conversation, sizeof(conversation), "%s/c1", history);
	snprintf(path, sizeof(path), "%s/messages.jsonl", conversation);
	snprintf(rotated, sizeof(rotated), "%s.1", path);
}

static void cleanup(void)
{
	armed = 0;
	omaq_store_message_index_reset();
	assert(omaq_store_clear(home, "c1") == 0);
	assert(rmdir(history) == 0);
	assert(rmdir(home) == 0);
}

static int append(const char *line, enum fault point)
{
	fault = point;
	triggered = file_syncs = directory_syncs = flushed = 0;
	file_fd = -1;
	armed = 1;
	int rc = omaq_store_append(home, "c1", line);
	armed = 0;
	assert(file_fd >= 0);
	errno = 0;
	assert(fcntl(file_fd, F_GETFD) == -1 && errno == EBADF);
	if (point == NONE) {
		assert(rc == 0 && file_syncs == 1 && directory_syncs == 3);
	} else {
		assert(triggered && rc == -1);
	}
	return rc;
}

static void check_mode(const char *name, mode_t expected)
{
	struct stat st;
	assert(stat(name, &st) == 0 && (st.st_mode & 0777) == expected);
}

static void test_append_and_reopen(void)
{
	char *out = NULL;
	size_t size;
	setup();
	append(first, NONE);
	append(second, NONE);
	check_mode(history, 0700);
	check_mode(conversation, 0700);
	check_mode(path, 0600);
	pid_t child = fork();
	assert(child >= 0);
	if (child == 0) {
		omaq_store_message_index_reset();
		assert(omaq_store_tail(home, "c1", 2, &out, &size) == 0);
		assert(strstr(out, first) && strstr(out, second));
		assert(omaq_store_message_id_used(home, "c1", "first") == 1);
		free(out);
		_exit(0);
	}
	int status;
	assert(waitpid(child, &status, 0) == child);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	cleanup();
}

static void test_failure(enum fault point, int fresh)
{
	setup();
	if (!fresh) {
		append(first, NONE);
		assert(omaq_store_message_id_used(home, "c1", "first") == 1);
	}
	append(second, point);
	/* A failure is ambiguous: bytes may already exist. Simulate losing the
	 * unconfirmed append, then ensure it was not published in the warm index. */
	assert(truncate(path, fresh ? 0 : (off_t)strlen(first) + 1) == 0);
	assert(omaq_store_message_id_used(home, "c1", "second") == 0);
	/* Retry must sync even directories created by the failed attempt. */
	append(second, NONE);
	assert(omaq_store_message_id_used(home, "c1", "second") == 1);
	cleanup();
}

static void test_rotation(enum fault point)
{
	setup();
	append(first, NONE);
	int fd = open(path, O_WRONLY | O_APPEND);
	assert(fd >= 0);
	/* Valid JSON lines fill the existing file to the 2 MiB rotation boundary. */
	char block[8192];
	memset(block, ' ', sizeof(block));
	memcpy(block, "{\"id\":\"padding\"}", strlen("{\"id\":\"padding\"}"));
	block[sizeof(block) - 1] = '\n';
	for (int i = 0; i < 256; i++)
		assert(write(fd, block, sizeof(block)) == (ssize_t)sizeof(block));
	assert(close(fd) == 0);
	append(second, point);
	if (point != NONE) {
		/* Model loss of the unconfirmed active record after the rename. */
		assert(truncate(path, 0) == 0);
		append(second, NONE);
	}
	struct stat st;
	assert(stat(rotated, &st) == 0 && st.st_size >= 2 * 1024 * 1024);
	assert(stat(path, &st) == 0 && st.st_size == (off_t)strlen(second) + 1);
	check_mode(rotated, 0600);
	check_mode(path, 0600);
	char *out = NULL;
	size_t size;
	assert(omaq_store_tail(home, "c1", 1, &out, &size) == 0);
	assert(strstr(out, second));
	free(out);
	cleanup();
}

int main(void)
{
	test_append_and_reopen();
	for (int f = WRITE; f <= DIR_SYNC_3; f++) {
		test_failure((enum fault)f, 0);
		test_failure((enum fault)f, 1);
	}
	test_rotation(NONE);
	test_rotation(DIR_SYNC_1);
	puts("store_append_test: ok (append/reopen, rotation, 15 injected failures/retries)");
	return 0;
}
