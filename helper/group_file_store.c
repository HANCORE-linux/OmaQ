#define _DEFAULT_SOURCE
#include "group_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ID_STORE_NAME "group-file-ids.bin"
#define ID_STORE_TEMP ".group-file-ids.tmp"
#define ID_STORE_V1_ENTRIES 65536u

static const uint8_t id_store_v1_header[] = {
	'O', 'Q', 'G', 'F', 'I', 'D', 'S', '1'
};
static const uint8_t id_store_v2_header[] = {
	'O', 'Q', 'G', 'F', 'I', 'D', 'S', '2'
};
_Static_assert(sizeof(id_store_v1_header) == sizeof(id_store_v2_header),
	       "group file id store headers must match");

typedef struct {
	uint8_t *ids;
	size_t count;
	int version;
} id_store;

static int write_all(int fd, const uint8_t *data, size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t written = write(fd, data + offset, length - offset);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return -1;
		offset += (size_t)written;
	}
	return 0;
}

static int state_directory_open(const char *state)
{
	struct stat status;
	int directory;

	if (!state || !state[0])
		return -1;
	directory = open(state, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory < 0 || fstat(directory, &status) != 0 ||
	    !S_ISDIR(status.st_mode) || status.st_uid != geteuid() ||
	    (status.st_mode & 0077) != 0) {
		if (directory >= 0)
			close(directory);
		return -1;
	}
	return directory;
}

static int same_source(const struct stat *before, const struct stat *after)
{
	return before && after && before->st_dev == after->st_dev &&
		before->st_ino == after->st_ino && before->st_size == after->st_size &&
		before->st_mtim.tv_sec == after->st_mtim.tv_sec &&
		before->st_mtim.tv_nsec == after->st_mtim.tv_nsec &&
		before->st_ctim.tv_sec == after->st_ctim.tv_sec &&
		before->st_ctim.tv_nsec == after->st_ctim.tv_nsec;
}

static void id_store_destroy(id_store *store)
{
	if (!store)
		return;
	free(store->ids);
	memset(store, 0, sizeof(*store));
}

static int id_store_load(int directory, id_store *store)
{
	const size_t maximum = sizeof(id_store_v1_header) +
		ID_STORE_V1_ENTRIES * OMAQ_GROUP_FILE_ID_BYTES;
	struct stat before, after;
	uint8_t *contents = NULL;
	size_t length, offset = 0, limit;
	int source = -1, result = -1;

	if (directory < 0 || !store)
		return -1;
	memset(store, 0, sizeof(*store));
	store->version = 2;
	source = openat(directory, ID_STORE_NAME,
			O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (source < 0) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	if (fstat(source, &before) != 0 || !S_ISREG(before.st_mode) ||
	    before.st_uid != geteuid() || before.st_nlink != 1 ||
	    (before.st_mode & 0077) != 0 || before.st_size < 0 ||
	    (size_t)before.st_size < sizeof(id_store_v1_header) ||
	    (size_t)before.st_size > maximum ||
	    ((size_t)before.st_size - sizeof(id_store_v1_header)) %
		OMAQ_GROUP_FILE_ID_BYTES != 0)
		goto done;
	length = (size_t)before.st_size;
	contents = malloc(length);
	if (!contents)
		goto done;
	while (offset < length) {
		ssize_t got = read(source, contents + offset, length - offset);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			goto done;
		offset += (size_t)got;
	}
	{
		uint8_t extra;
		ssize_t got;
		do {
			got = read(source, &extra, 1);
		} while (got < 0 && errno == EINTR);
		if (got != 0)
			goto done;
	}
	if (fstat(source, &after) != 0 || !same_source(&before, &after))
		goto done;
	if (memcmp(contents, id_store_v2_header, sizeof(id_store_v2_header)) == 0) {
		store->version = 2;
		limit = OMAQ_GROUP_FILE_ID_STORE_LIMIT;
	} else if (memcmp(contents, id_store_v1_header,
			  sizeof(id_store_v1_header)) == 0) {
		store->version = 1;
		limit = ID_STORE_V1_ENTRIES;
	} else {
		goto done;
	}
	store->count = (length - sizeof(id_store_v1_header)) /
		OMAQ_GROUP_FILE_ID_BYTES;
	if (store->count > limit)
		goto done;
	if (store->count) {
		store->ids = malloc(store->count * OMAQ_GROUP_FILE_ID_BYTES);
		if (!store->ids)
			goto done;
		memcpy(store->ids, contents + sizeof(id_store_v1_header),
		       store->count * OMAQ_GROUP_FILE_ID_BYTES);
	}
	result = 0;
done:
	if (source >= 0)
		close(source);
	free(contents);
	if (result != 0)
		id_store_destroy(store);
	return result;
}

static int id_store_contains(const id_store *store,
			     const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	if (!store || !id)
		return 0;
	for (size_t i = 0; i < store->count; i++)
		if (memcmp(store->ids + i * OMAQ_GROUP_FILE_ID_BYTES, id,
			   OMAQ_GROUP_FILE_ID_BYTES) == 0)
			return 1;
	return 0;
}

static int id_store_prepare_temp(int directory)
{
	struct stat status;

	if (fstatat(directory, ID_STORE_TEMP, &status, AT_SYMLINK_NOFOLLOW) == 0) {
		if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
		    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
		    unlinkat(directory, ID_STORE_TEMP, 0) != 0)
			return -1;
	} else if (errno != ENOENT) {
		return -1;
	}
	return 0;
}

static int id_store_replace(int directory, const uint8_t *ids, size_t count)
{
	int output = -1, result = -1;

	if (directory < 0 || (!ids && count) ||
	    count > OMAQ_GROUP_FILE_ID_STORE_LIMIT ||
	    id_store_prepare_temp(directory) != 0)
		return -1;
	output = openat(directory, ID_STORE_TEMP,
			O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (output < 0 ||
	    write_all(output, id_store_v2_header, sizeof(id_store_v2_header)) != 0 ||
	    (count && write_all(output, ids,
				count * OMAQ_GROUP_FILE_ID_BYTES) != 0) ||
	    fsync(output) != 0)
		goto done;
	if (close(output) != 0) {
		output = -1;
		goto done;
	}
	output = -1;
	if (renameat(directory, ID_STORE_TEMP, directory, ID_STORE_NAME) != 0 ||
	    fsync(directory) != 0)
		goto done;
	result = 0;
done:
	if (output >= 0)
		close(output);
	if (result != 0)
		(void)unlinkat(directory, ID_STORE_TEMP, 0);
	return result;
}

static int id_store_migrate_v1(int directory, const id_store *store,
			       const uint8_t preserve[OMAQ_GROUP_FILE_ID_BYTES])
{
	uint8_t *compacted = NULL;
	size_t retained, start;
	int result;

	if (!store || store->version != 1)
		return 0;
	retained = store->count;
	if (retained > OMAQ_GROUP_FILE_ID_STORE_LIMIT)
		retained = OMAQ_GROUP_FILE_ID_STORE_LIMIT;
	start = store->count - retained;
	if (preserve && retained == OMAQ_GROUP_FILE_ID_STORE_LIMIT &&
	    !id_store_contains(&(id_store){
		.ids = store->ids + start * OMAQ_GROUP_FILE_ID_BYTES,
		.count = retained,
	    }, preserve)) {
		compacted = malloc(retained * OMAQ_GROUP_FILE_ID_BYTES);
		if (!compacted)
			return -1;
		memcpy(compacted,
		       store->ids + (start + 1u) * OMAQ_GROUP_FILE_ID_BYTES,
		       (retained - 1u) * OMAQ_GROUP_FILE_ID_BYTES);
		memcpy(compacted + (retained - 1u) * OMAQ_GROUP_FILE_ID_BYTES,
		       preserve, OMAQ_GROUP_FILE_ID_BYTES);
		result = id_store_replace(directory, compacted, retained);
		free(compacted);
		return result;
	}
	return id_store_replace(directory,
		retained ? store->ids + start * OMAQ_GROUP_FILE_ID_BYTES : NULL,
		retained);
}

int omaq_group_file_id_seen(const char *state,
			    const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	id_store store;
	int directory, result;

	if (!id)
		return -1;
	directory = state_directory_open(state);
	if (directory < 0)
		return -1;
	if (id_store_load(directory, &store) != 0) {
		close(directory);
		return -1;
	}
	result = id_store_contains(&store, id);
	if (id_store_migrate_v1(directory, &store, result ? id : NULL) != 0)
		result = -1;
	id_store_destroy(&store);
	close(directory);
	return result;
}

int omaq_group_file_id_reserve(const char *state,
			       const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	id_store store;
	uint8_t *updated = NULL;
	size_t retained, start;
	int directory = -1, result = -1;

	if (!id)
		return -1;
	directory = state_directory_open(state);
	if (directory < 0 || id_store_load(directory, &store) != 0)
		goto done;
	if (id_store_contains(&store, id)) {
		result = id_store_migrate_v1(directory, &store, id) == 0 ? 1 : -1;
		goto done_store;
	}
	retained = store.count;
	if (retained >= OMAQ_GROUP_FILE_ID_STORE_LIMIT)
		retained = OMAQ_GROUP_FILE_ID_STORE_LIMIT - 1;
	start = store.count - retained;
	updated = malloc((retained + 1) * OMAQ_GROUP_FILE_ID_BYTES);
	if (!updated)
		goto done_store;
	if (retained)
		memcpy(updated, store.ids + start * OMAQ_GROUP_FILE_ID_BYTES,
		       retained * OMAQ_GROUP_FILE_ID_BYTES);
	memcpy(updated + retained * OMAQ_GROUP_FILE_ID_BYTES, id,
	       OMAQ_GROUP_FILE_ID_BYTES);
	if (id_store_replace(directory, updated, retained + 1) != 0)
		goto done_store;
	result = 0;
done_store:
	id_store_destroy(&store);
done:
	free(updated);
	if (directory >= 0)
		close(directory);
	return result;
}
