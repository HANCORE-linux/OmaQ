#define _DEFAULT_SOURCE
#include "state_archive.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int same_regular_file(int source, const struct stat *source_status,
			     int directory, const char *name)
{
	char left[4096], right[4096];
	struct stat status;
	off_t offset = 0;
	int candidate, result = -1;

	if (source < 0 || !source_status || directory < 0 || !name)
		return -1;
	candidate = openat(directory, name,
			   O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (candidate < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(candidate, &status) != 0 || !S_ISREG(status.st_mode) ||
	    status.st_uid != geteuid() || status.st_nlink != 1 ||
	    (status.st_mode & 0077) != 0)
		goto done;
	if (status.st_size != source_status->st_size) {
		result = 0;
		goto done;
	}
	while (offset < source_status->st_size) {
		size_t wanted = (size_t)(source_status->st_size - offset);
		ssize_t left_count, right_count;

		if (wanted > sizeof(left))
			wanted = sizeof(left);
		left_count = pread(source, left, wanted, offset);
		right_count = pread(candidate, right, wanted, offset);
		if (left_count != (ssize_t)wanted || right_count != (ssize_t)wanted)
			goto done;
		if (memcmp(left, right, wanted) != 0) {
			result = 0;
			goto done;
		}
		offset += (off_t)wanted;
	}
	{
		struct stat current;
		if (fstat(source, &current) != 0 || current.st_dev != source_status->st_dev ||
		    current.st_ino != source_status->st_ino ||
		    current.st_size != source_status->st_size ||
		    current.st_mtim.tv_sec != source_status->st_mtim.tv_sec ||
		    current.st_mtim.tv_nsec != source_status->st_mtim.tv_nsec)
			goto done;
	}
	result = 1;
done:
	close(candidate);
	return result;
}

int omaq_state_archive_copy(const char *state, const char *name)
{
	char archive[160], temporary[160], buffer[4096];
	struct stat directory_status, source_status, existing;
	int directory = -1, source = -1, output = -1, result = -1;
	unsigned int suffix;

	temporary[0] = '\0';
	if (!state || !state[0] || !name || !name[0] || strchr(name, '/'))
		return -1;
	directory = open(state, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory < 0 || fstat(directory, &directory_status) != 0 ||
	    !S_ISDIR(directory_status.st_mode) || directory_status.st_uid != geteuid() ||
	    (directory_status.st_mode & 0077) != 0)
		goto done;
	source = openat(directory, name,
			O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (source < 0) {
		result = errno == ENOENT ? 0 : -1;
		goto done;
	}
	if (fstat(source, &source_status) != 0 || !S_ISREG(source_status.st_mode) ||
	    source_status.st_uid != geteuid() || source_status.st_nlink != 1 ||
	    (source_status.st_mode & 0022) != 0 || source_status.st_size < 0 ||
	    source_status.st_size > 8 * 1024 * 1024)
		goto done;
	for (suffix = 0; suffix < 10000; suffix++) {
		int same;
		if (snprintf(archive, sizeof(archive), "%s.legacy-direct.%u", name, suffix) >=
		    (int)sizeof(archive))
			goto done;
		if (fstatat(directory, archive, &existing, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno == ENOENT)
				break;
			goto done;
		}
		same = same_regular_file(source, &source_status, directory, archive);
		if (same < 0)
			goto done;
		if (same == 1) {
			result = 0;
			goto done;
		}
	}
	if (suffix == 10000 ||
	    snprintf(temporary, sizeof(temporary), ".%s.legacy-direct.tmp.%ld", name,
		     (long)getpid()) >= (int)sizeof(temporary))
		goto done;
	output = openat(directory, temporary,
			O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (output < 0)
		goto done;
	for (;;) {
		ssize_t got = read(source, buffer, sizeof(buffer));
		if (got < 0) {
			if (errno == EINTR)
				continue;
			goto done;
		}
		if (got == 0)
			break;
		for (ssize_t offset = 0; offset < got;) {
			ssize_t written = write(output, buffer + offset, (size_t)(got - offset));
			if (written < 0) {
				if (errno == EINTR)
					continue;
				goto done;
			}
			offset += written;
		}
	}
	{
		struct stat current;
		if (fstat(source, &current) != 0 || current.st_dev != source_status.st_dev ||
		    current.st_ino != source_status.st_ino ||
		    current.st_size != source_status.st_size ||
		    current.st_mtim.tv_sec != source_status.st_mtim.tv_sec ||
		    current.st_mtim.tv_nsec != source_status.st_mtim.tv_nsec)
			goto done;
	}
	if (fsync(output) != 0 || close(output) != 0)
		goto done;
	output = -1;
	if (renameat(directory, temporary, directory, archive) != 0 ||
	    fsync(directory) != 0)
		goto done;
	result = 0;
done:
	if (output >= 0)
		close(output);
	if (result != 0 && directory >= 0 && temporary[0])
		(void)unlinkat(directory, temporary, 0);
	if (source >= 0)
		close(source);
	if (directory >= 0)
		close(directory);
	return result;
}
