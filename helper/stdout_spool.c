#define _DEFAULT_SOURCE
#include "stdout_spool.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#define SPOOL_BUFFER_SIZE (64u * 1024u)
#define SPOOL_DISK_RESERVE (16u * 1024u * 1024u)

struct omaq_stdout_spool {
	int dirfd;
	int fd;
	int cursor_fd;
	int output_fd;
	off_t file_end;
	off_t send_offset;
	off_t ack_offset;
	unsigned char buffer[SPOOL_BUFFER_SIZE];
	size_t buffer_len;
	size_t buffer_off;
	omaq_stdout_write_fn writer;
	void *writer_ctx;
	int cursor_warned;
	int failed;
};

static ssize_t output_write(void *ctx, int fd, const void *buf, size_t len)
{
	(void)ctx;
	return write(fd, buf, len);
}

static int secure_regular_file(int fd)
{
	struct stat st;

	if (fstat(fd, &st) != 0)
		return -1;
	if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1) {
		errno = EPERM;
		return -1;
	}
	if (fchmod(fd, 0600) != 0)
		return -1;
	return 0;
}

static int write_all(int fd, const unsigned char *buf, size_t len)
{
	size_t off = 0;

	while (off < len) {
		ssize_t wr = write(fd, buf + off, len - off);
		if (wr > 0) {
			off += (size_t)wr;
			continue;
		}
		if (wr < 0 && errno == EINTR)
			continue;
		if (wr == 0)
			errno = EIO;
		return -1;
	}
	return 0;
}

static int pwrite_all(int fd, const unsigned char *buf, size_t len, off_t start)
{
	size_t off = 0;

	while (off < len) {
		ssize_t wr = pwrite(fd, buf + off, len - off, start + (off_t)off);
		if (wr > 0) {
			off += (size_t)wr;
			continue;
		}
		if (wr < 0 && errno == EINTR)
			continue;
		if (wr == 0)
			errno = EIO;
		return -1;
	}
	return 0;
}

static int persist_cursor(omaq_stdout_spool *spool, off_t offset)
{
	char text[64];
	int len;

	if (!spool || offset < 0) {
		errno = EINVAL;
		return -1;
	}
	len = snprintf(text, sizeof(text), "%lld\n", (long long)offset);
	if (len < 0 || (size_t)len >= sizeof(text)) {
		errno = EOVERFLOW;
		return -1;
	}
	/* An interrupted update becomes malformed and therefore replays from zero. */
	if (ftruncate(spool->cursor_fd, 0) != 0 ||
	    pwrite_all(spool->cursor_fd, (const unsigned char *)text, (size_t)len, 0) != 0 ||
	    ftruncate(spool->cursor_fd, (off_t)len) != 0 ||
	    fdatasync(spool->cursor_fd) != 0)
		return -1;
	return 0;
}

static void warn_cursor(omaq_stdout_spool *spool)
{
	if (!spool->cursor_warned) {
		fprintf(stderr, "omaq: critical stdout spool cursor update failed: %s\n",
			strerror(errno));
		spool->cursor_warned = 1;
	}
}

static int quarantine_incomplete_tail(int dirfd, int fd, off_t start, off_t end)
{
	unsigned char buf[8192];
	char name[96];
	off_t pos = start;
	int out = -1;

	for (unsigned int attempt = 0; attempt < 100; attempt++) {
		if (snprintf(name, sizeof(name), "stdout-critical.incomplete.%ld.%u",
			     (long)getpid(), attempt) >= (int)sizeof(name)) {
			errno = EOVERFLOW;
			return -1;
		}
		out = openat(dirfd, name,
			     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (out >= 0)
			break;
		if (errno != EEXIST)
			return -1;
	}
	if (out < 0) {
		errno = EEXIST;
		return -1;
	}
	if (secure_regular_file(out) != 0)
		goto fail;
	while (pos < end) {
		size_t want = sizeof(buf);
		ssize_t got;

		if (end - pos < (off_t)want)
			want = (size_t)(end - pos);
		got = pread(fd, buf, want, pos);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			if (got == 0)
				errno = EIO;
			goto fail;
		}
		if (write_all(out, buf, (size_t)got) != 0)
			goto fail;
		pos += got;
	}
	if (fdatasync(out) != 0 || fsync(dirfd) != 0)
		goto fail;
	close(out);
	if (ftruncate(fd, start) != 0 || fdatasync(fd) != 0)
		return -1;
	fprintf(stderr, "omaq: quarantined incomplete critical stdout record as %s\n", name);
	return 0;

fail:
	{
		int saved = errno;
		close(out);
		(void)unlinkat(dirfd, name, 0);
		errno = saved;
	}
	return -1;
}

static int recover_spool_tail(int dirfd, int fd, off_t *size_out)
{
	unsigned char buf[8192];
	off_t pos = 0;
	off_t last_record = 0;
	int line_start = 1;
	size_t line_len = 0;
	unsigned char previous = 0;
	struct stat st;

	if (fstat(fd, &st) != 0)
		return -1;
	while (pos < st.st_size) {
		size_t want = sizeof(buf);
		ssize_t got;

		if (st.st_size - pos < (off_t)want)
			want = (size_t)(st.st_size - pos);
		got = pread(fd, buf, want, pos);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			if (got == 0)
				errno = EIO;
			return -1;
		}
		for (ssize_t i = 0; i < got; i++) {
			unsigned char c = buf[i];

			if (line_start && c != '{') {
				errno = EINVAL;
				return -1;
			}
			if (c == '\n') {
				if (line_start || previous != '}') {
					errno = EINVAL;
					return -1;
				}
				last_record = pos + i + 1;
				line_start = 1;
				line_len = 0;
				previous = 0;
				continue;
			}
			if (c < 0x20) {
				errno = EINVAL;
				return -1;
			}
			if (line_len >= OMAQ_STDOUT_RECORD_MAX) {
				errno = EOVERFLOW;
				return -1;
			}
			line_len++;
			line_start = 0;
			previous = c;
		}
		pos += got;
	}
	if (!line_start) {
		if (quarantine_incomplete_tail(dirfd, fd, last_record, st.st_size) != 0)
			return -1;
		st.st_size = last_record;
	}
	*size_out = st.st_size;
	return 0;
}

static int cursor_boundary_ok(int fd, off_t cursor, off_t file_end)
{
	unsigned char c;

	if (cursor < 0 || cursor > file_end)
		return 0;
	if (cursor == 0)
		return 1;
	return pread(fd, &c, 1, cursor - 1) == 1 && c == '\n';
}

static int load_cursor(omaq_stdout_spool *spool, off_t *cursor_out)
{
	char text[64];
	char *end;
	unsigned long long value;
	struct stat st;
	ssize_t got;
	int valid = 1;

	if (fstat(spool->cursor_fd, &st) != 0)
		return -1;
	if (st.st_size == 0) {
		*cursor_out = 0;
		return persist_cursor(spool, 0);
	}
	if (st.st_size >= (off_t)sizeof(text)) {
		valid = 0;
		got = 0;
	} else {
		got = pread(spool->cursor_fd, text, (size_t)st.st_size, 0);
		if (got != st.st_size)
			valid = 0;
	}
	if (valid) {
		text[got] = '\0';
		errno = 0;
		value = strtoull(text, &end, 10);
		if (errno != 0 || end == text || strcmp(end, "\n") != 0 ||
		    value > (unsigned long long)LLONG_MAX ||
		    !cursor_boundary_ok(spool->fd, (off_t)value, spool->file_end))
			valid = 0;
	}
	if (!valid) {
		fprintf(stderr, "omaq: invalid critical stdout spool cursor; replaying from start\n");
		*cursor_out = 0;
		return persist_cursor(spool, 0);
	}
	*cursor_out = (off_t)value;
	return 0;
}

static int reset_drained(omaq_stdout_spool *spool)
{
	if (spool->send_offset != spool->file_end || spool->ack_offset != spool->file_end)
		return 0;
	if (spool->file_end == 0)
		return 0;
	/* Truncate first: a stale cursor beyond EOF safely resolves to an empty FIFO. */
	if (ftruncate(spool->fd, 0) != 0 || fdatasync(spool->fd) != 0)
		return -1;
	spool->file_end = 0;
	spool->send_offset = 0;
	spool->ack_offset = 0;
	if (persist_cursor(spool, 0) != 0)
		warn_cursor(spool);
	return 0;
}

omaq_stdout_spool *omaq_stdout_spool_open(const char *state_dir, int output_fd)
{
	omaq_stdout_spool *spool;
	struct stat st;

	if (!state_dir || !state_dir[0]) {
		errno = EINVAL;
		return NULL;
	}
	spool = calloc(1, sizeof(*spool));
	if (!spool)
		return NULL;
	spool->dirfd = -1;
	spool->fd = -1;
	spool->cursor_fd = -1;
	spool->output_fd = output_fd;
	spool->writer = output_write;
	spool->dirfd = open(state_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (spool->dirfd < 0)
		goto fail;
	if (fstat(spool->dirfd, &st) != 0)
		goto fail;
	if (!S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
		errno = EPERM;
		goto fail;
	}
	if (fchmod(spool->dirfd, 0700) != 0)
		goto fail;
	spool->fd = openat(spool->dirfd, OMAQ_STDOUT_SPOOL_FILE,
			   O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (spool->fd < 0 || secure_regular_file(spool->fd) != 0)
		goto fail;
	if (flock(spool->fd, LOCK_EX | LOCK_NB) != 0 || fsync(spool->dirfd) != 0 ||
	    recover_spool_tail(spool->dirfd, spool->fd, &spool->file_end) != 0)
		goto fail;
	spool->cursor_fd = openat(spool->dirfd, OMAQ_STDOUT_CURSOR_FILE,
				  O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (spool->cursor_fd < 0 || secure_regular_file(spool->cursor_fd) != 0 ||
	    fsync(spool->dirfd) != 0 || load_cursor(spool, &spool->ack_offset) != 0)
		goto fail;
	spool->send_offset = spool->ack_offset;
	if (spool->ack_offset == spool->file_end && reset_drained(spool) != 0)
		goto fail;
	return spool;

fail:
	{
		int saved = errno;
		omaq_stdout_spool_close(spool);
		errno = saved;
	}
	return NULL;
}

static int spool_has_space(int fd, size_t record_len)
{
	struct statvfs fs;
	uint64_t block_size;
	uint64_t available;
	uint64_t required;

	if (fstatvfs(fd, &fs) != 0)
		return -1;
	block_size = fs.f_frsize ? fs.f_frsize : fs.f_bsize;
	if (block_size != 0 && fs.f_bavail > UINT64_MAX / block_size)
		available = UINT64_MAX;
	else
		available = (uint64_t)fs.f_bavail * block_size;
	if (record_len > UINT64_MAX - SPOOL_DISK_RESERVE) {
		errno = EOVERFLOW;
		return -1;
	}
	required = (uint64_t)record_len + SPOOL_DISK_RESERVE;
	if (available < required) {
		errno = ENOSPC;
		return -1;
	}
	return 0;
}

int omaq_stdout_spool_append(omaq_stdout_spool *spool, const char *event)
{
	unsigned char *record;
	size_t len;
	off_t old_end;

	if (!spool || spool->failed || !event || strchr(event, '\n') || strchr(event, '\r')) {
		errno = EINVAL;
		return -1;
	}
	len = strlen(event);
	if (len == 0 || event[0] != '{' || event[len - 1] != '}') {
		errno = EINVAL;
		return -1;
	}
	if (len > OMAQ_STDOUT_RECORD_MAX || len > (size_t)LLONG_MAX ||
	    spool->file_end > (off_t)(LLONG_MAX - (long long)len - 1)) {
		errno = EOVERFLOW;
		return -1;
	}
	{
		uint64_t record_len = (uint64_t)len + 1u;
		if (record_len > OMAQ_STDOUT_SPOOL_MAX ||
		    (uint64_t)spool->file_end > OMAQ_STDOUT_SPOOL_MAX - record_len) {
			errno = ENOSPC;
			return -1;
		}
	}
	if (spool_has_space(spool->fd, len + 1) != 0)
		return -1;
	record = malloc(len + 1);
	if (!record)
		return -1;
	memcpy(record, event, len);
	record[len] = '\n';
	old_end = spool->file_end;
	if (write_all(spool->fd, record, len + 1) != 0) {
		int saved = errno;
		if (ftruncate(spool->fd, old_end) != 0 || fdatasync(spool->fd) != 0)
			fprintf(stderr, "omaq: critical stdout spool rollback failed: %s\n",
				strerror(errno));
		free(record);
		errno = saved;
		return -1;
	}
	free(record);
	spool->file_end += (off_t)(len + 1);
	if (fdatasync(spool->fd) != 0) {
		spool->failed = 1;
		return -1;
	}
	return 0;
}

int omaq_stdout_spool_pending(const omaq_stdout_spool *spool)
{
	return spool && (spool->buffer_off < spool->buffer_len ||
			 spool->send_offset < spool->file_end);
}

static int fill_buffer(omaq_stdout_spool *spool)
{
	ssize_t got;
	size_t want = sizeof(spool->buffer);

	if (spool->buffer_off < spool->buffer_len || spool->send_offset >= spool->file_end)
		return 0;
	if (spool->file_end - spool->send_offset < (off_t)want)
		want = (size_t)(spool->file_end - spool->send_offset);
	for (;;) {
		got = pread(spool->fd, spool->buffer, want, spool->send_offset);
		if (got < 0 && errno == EINTR)
			continue;
		break;
	}
	if (got <= 0) {
		if (got == 0)
			errno = EIO;
		return -1;
	}
	spool->buffer_len = (size_t)got;
	spool->buffer_off = 0;
	for (size_t i = 0; i < spool->buffer_len; i++) {
		if (spool->buffer[i] == '\n') {
			spool->buffer_len = i + 1;
			break;
		}
	}
	return 0;
}

int omaq_stdout_spool_flush(omaq_stdout_spool *spool)
{
	ssize_t wr;
	size_t left;

	if (!spool || spool->failed) {
		errno = EIO;
		return OMAQ_STDOUT_FLUSH_SPOOL_ERROR;
	}
	if (fill_buffer(spool) != 0)
		return OMAQ_STDOUT_FLUSH_SPOOL_ERROR;
	if (spool->buffer_off >= spool->buffer_len)
		return OMAQ_STDOUT_FLUSH_IDLE;
	left = spool->buffer_len - spool->buffer_off;
	wr = spool->writer(spool->writer_ctx, spool->output_fd,
			   spool->buffer + spool->buffer_off, left);
	if (wr < 0 && errno == EINTR)
		return OMAQ_STDOUT_FLUSH_IDLE;
	if (wr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return OMAQ_STDOUT_FLUSH_IDLE;
	if (wr <= 0)
		return OMAQ_STDOUT_FLUSH_OUTPUT_ERROR;
	if ((size_t)wr > left) {
		errno = EIO;
		return OMAQ_STDOUT_FLUSH_SPOOL_ERROR;
	}
	spool->buffer_off += (size_t)wr;
	spool->send_offset += (off_t)wr;
	if (spool->buffer_off == spool->buffer_len) {
		int record_done = spool->buffer[spool->buffer_len - 1] == '\n';

		spool->buffer_off = 0;
		spool->buffer_len = 0;
		if (record_done) {
			spool->ack_offset = spool->send_offset;
			if (persist_cursor(spool, spool->ack_offset) != 0)
				warn_cursor(spool);
			else
				spool->cursor_warned = 0;
			if (spool->send_offset == spool->file_end && reset_drained(spool) != 0) {
				spool->failed = 1;
				return OMAQ_STDOUT_FLUSH_SPOOL_ERROR;
			}
		}
	}
	return OMAQ_STDOUT_FLUSH_PROGRESS;
}

void omaq_stdout_spool_set_writer(omaq_stdout_spool *spool,
				  omaq_stdout_write_fn writer, void *ctx)
{
	if (!spool)
		return;
	spool->writer = writer ? writer : output_write;
	spool->writer_ctx = writer ? ctx : NULL;
}

void omaq_stdout_spool_close(omaq_stdout_spool *spool)
{
	if (!spool)
		return;
	if (spool->cursor_fd >= 0)
		close(spool->cursor_fd);
	if (spool->fd >= 0)
		close(spool->fd);
	if (spool->dirfd >= 0)
		close(spool->dirfd);
	free(spool);
}
