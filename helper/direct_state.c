#define _GNU_SOURCE
#include "direct_state.h"
#include "ratchet.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int lower_hex_key(const char *key)
{
	size_t i;

	if (!key || strlen(key) != 64)
		return 0;
	for (i = 0; i < 64; i++)
		if (!((key[i] >= '0' && key[i] <= '9') ||
		      (key[i] >= 'a' && key[i] <= 'f')))
			return 0;
	return 1;
}

static int decimal_u32_canonical(const char *value)
{
	uint64_t number = 0;
	size_t i;

	if (!value || !value[0] || (value[0] == '0' && value[1]))
		return 0;
	for (i = 0; value[i]; i++) {
		if (!isdigit((unsigned char)value[i]))
			return 0;
		number = number * 10u + (uint64_t)(value[i] - '0');
		if (number > UINT32_MAX)
			return 0;
	}
	return 1;
}

int omaq_direct_state_id(const char *public_key, char *out, size_t out_size)
{
	if (!lower_hex_key(public_key) || !out || out_size < OMAQ_DIRECT_STATE_ID_MAX ||
	    snprintf(out, out_size, "d:%s", public_key) != OMAQ_DIRECT_STATE_ID_MAX - 1)
		return -1;
	return 0;
}

static int private_directory_fd(int fd)
{
	struct stat st;
	return fd >= 0 && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode) &&
		st.st_uid == geteuid() && (st.st_mode & 0077) == 0;
}

static int open_relative_dir(const char *home, const char *relative)
{
	char component[32];
	const char *cursor = relative;
	int fd;

	if (!home || !home[0] || !relative || !relative[0])
		return -1;
	fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (!private_directory_fd(fd)) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	while (*cursor) {
		const char *slash = strchr(cursor, '/');
		size_t length = slash ? (size_t)(slash - cursor) : strlen(cursor);
		int next;

		if (length == 0 || length >= sizeof(component)) {
			close(fd);
			return -1;
		}
		memcpy(component, cursor, length);
		component[length] = '\0';
		next = openat(fd, component,
			      O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (next < 0) {
			int missing = errno == ENOENT;
			close(fd);
			return missing ? -2 : -1;
		}
		if (!private_directory_fd(next)) {
			close(next);
			close(fd);
			return -1;
		}
		close(fd);
		fd = next;
		if (!slash)
			break;
		cursor = slash + 1;
	}
	return fd;
}

static int entry_type_ok(const struct stat *st, int directory)
{
	return st && (directory ? S_ISDIR(st->st_mode) : S_ISREG(st->st_mode));
}

static int archive_name(int parent_fd, const char *old_name, char *out, size_t out_size)
{
	struct stat st;
	unsigned int i;

	for (i = 0; i < 10000; i++) {
		if (snprintf(out, out_size, ".legacy-direct.%s.%u", old_name, i) >=
		    (int)out_size)
			return -1;
		if (fstatat(parent_fd, out, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno == ENOENT)
				return 0;
			return -1;
		}
	}
	return -1;
}

static int migrate_entry(const char *home, const char *relative,
			 const char *old_name, const char *new_name, int directory)
{
	struct stat old_st, new_st;
	char archive[NAME_MAX + 1];
	int fd, rc = -1;

	fd = open_relative_dir(home, relative);
	if (fd == -2)
		return 0;
	if (fd < 0)
		return -1;
	if (new_name && fstatat(fd, new_name, &new_st, AT_SYMLINK_NOFOLLOW) == 0) {
		if (!entry_type_ok(&new_st, directory))
			goto out;
	} else if (new_name && errno != ENOENT) {
		goto out;
	}
	if (fstatat(fd, old_name, &old_st, AT_SYMLINK_NOFOLLOW) != 0) {
		rc = errno == ENOENT ? 0 : -1;
		goto out;
	}
	if (!entry_type_ok(&old_st, directory))
		goto out;
	if (new_name && fstatat(fd, new_name, &new_st, AT_SYMLINK_NOFOLLOW) != 0) {
		if (errno != ENOENT || renameat(fd, old_name, fd, new_name) != 0)
			goto out;
	} else {
		if (archive_name(fd, old_name, archive, sizeof(archive)) != 0 ||
		    renameat(fd, old_name, fd, archive) != 0)
			goto out;
	}
	rc = fsync(fd);
out:
	close(fd);
	return rc;
}

static int session_names(const char *home, const char *legacy_number,
			 char ***names_out, size_t *count_out)
{
	char prefix[32];
	char **names = NULL;
	size_t count = 0, capacity = 0, prefix_length;
	struct dirent *entry;
	DIR *directory;
	int fd;

	if (!names_out || !count_out ||
	    snprintf(prefix, sizeof(prefix), "%s-", legacy_number) >= (int)sizeof(prefix))
		return -1;
	*names_out = NULL;
	*count_out = 0;
	prefix_length = strlen(prefix);
	fd = open_relative_dir(home, "ratchet/sess");
	if (fd == -2)
		return 0;
	if (fd < 0)
		return -1;
	directory = fdopendir(fd);
	if (!directory) {
		close(fd);
		return -1;
	}
	errno = 0;
	while ((entry = readdir(directory)) != NULL) {
		const char *suffix;
		char device[32], *copy;
		struct stat st;
		size_t length;
		if (strncmp(entry->d_name, prefix, prefix_length) != 0)
			continue;
		suffix = entry->d_name + prefix_length;
		length = strlen(suffix);
		if (length > 4 && strcmp(suffix + length - 4, ".tmp") == 0) {
			length -= 4;
			if (length == 0 || length >= sizeof(device))
				goto fail;
			memcpy(device, suffix, length);
			device[length] = '\0';
			if (!decimal_u32_canonical(device) ||
			    fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
			    !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
			    (st.st_mode & 0077) != 0 || st.st_size < 0 ||
			    st.st_size > OMAQ_RATCHET_RECORD_MAX ||
			    unlinkat(fd, entry->d_name, 0) != 0)
				goto fail;
			continue;
		}
		if (!decimal_u32_canonical(suffix))
			goto fail;
		if (count == capacity) {
			size_t next = capacity ? capacity * 2 : 4;
			char **grown;
			if (next > 1024)
				goto fail;
			grown = realloc(names, next * sizeof(*grown));
			if (!grown)
				goto fail;
			names = grown;
			capacity = next;
		}
		copy = strdup(entry->d_name);
		if (!copy)
			goto fail;
		names[count++] = copy;
	}
	if (errno != 0 || fsync(fd) != 0)
		goto fail;
	closedir(directory);
	*names_out = names;
	*count_out = count;
	return 0;
fail:
	closedir(directory);
	for (size_t i = 0; i < count; i++)
		free(names[i]);
	free(names);
	return -1;
}

static int validate_history(const char *home, const char *name)
{
	struct dirent *entry;
	struct stat st;
	DIR *directory;
	int history_fd, conversation_fd;

	history_fd = open_relative_dir(home, "history");
	if (history_fd == -2)
		return 0;
	if (history_fd < 0)
		return -1;
	conversation_fd = openat(history_fd, name,
				 O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	close(history_fd);
	if (conversation_fd < 0)
		return errno == ENOENT ? 0 : -1;
	directory = fdopendir(conversation_fd);
	if (!directory) {
		close(conversation_fd);
		return -1;
	}
	errno = 0;
	while ((entry = readdir(directory)) != NULL) {
		int temporary = 0;
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (strncmp(entry->d_name, "messages.jsonl.tmp.", 19) == 0 &&
		    decimal_u32_canonical(entry->d_name + 19))
			temporary = 1;
		else if (strcmp(entry->d_name, "messages.jsonl") != 0 &&
			 strcmp(entry->d_name, "messages.jsonl.1") != 0) {
			closedir(directory);
			return -1;
		}
		if (fstatat(conversation_fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
		    !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
		    (st.st_mode & 0077) != 0 || st.st_size < 0 ||
		    st.st_size > 3 * 1024 * 1024 ||
		    (temporary && unlinkat(conversation_fd, entry->d_name, 0) != 0)) {
			closedir(directory);
			return -1;
		}
	}
	if (errno != 0 || fsync(conversation_fd) != 0) {
		closedir(directory);
		return -1;
	}
	closedir(directory);
	return 0;
}

static int migrate_sessions(const char *home, const char *legacy_number,
			    const char *stable_id)
{
	char **names = NULL;
	size_t count = 0, i;
	int rc = -1;

	if (session_names(home, legacy_number, &names, &count) != 0)
		return -1;
	for (i = 0; i < count; i++) {
		const char *suffix = strchr(names[i], '-');
		char new_name[NAME_MAX + 1];
		if (!suffix || snprintf(new_name, sizeof(new_name), "%s%s", stable_id,
					 suffix) >= (int)sizeof(new_name) ||
		    migrate_entry(home, "ratchet/sess", names[i], new_name, 0) != 0)
			goto done;
	}
	rc = 0;
done:
	for (i = 0; i < count; i++)
		free(names[i]);
	free(names);
	return rc;
}

static int archive_sessions(const char *home, const char *legacy_number)
{
	char **names = NULL;
	size_t count = 0, i;
	int rc = -1;

	if (session_names(home, legacy_number, &names, &count) != 0)
		return -1;
	for (i = 0; i < count; i++)
		if (migrate_entry(home, "ratchet/sess", names[i], NULL, 0) != 0)
			goto done;
	rc = 0;
done:
	for (i = 0; i < count; i++)
		free(names[i]);
	free(names);
	return rc;
}

static int validate_stable_sessions(const char *home, const char *stable_id);
static int validate_entry(const char *home, const char *relative,
			  const char *name, int directory);
static int cleanup_producer_temp(const char *home, const char *relative,
				 const char *name);

int omaq_direct_state_migrate(const char *home, const char *legacy_number,
			      const char *public_key)
{
	char stable_id[OMAQ_DIRECT_STATE_ID_MAX];
	char legacy_avatar[32], stable_avatar[96];

	if (!decimal_u32_canonical(legacy_number) ||
	    omaq_direct_state_id(public_key, stable_id, sizeof(stable_id)) != 0 ||
	    snprintf(legacy_avatar, sizeof(legacy_avatar), "%s.png", legacy_number) >=
		(int)sizeof(legacy_avatar) ||
	    snprintf(stable_avatar, sizeof(stable_avatar), "%s.png", stable_id) >=
		(int)sizeof(stable_avatar))
		return -1;
	if (validate_history(home, legacy_number) != 0 ||
	    validate_history(home, stable_id) != 0 ||
	    cleanup_producer_temp(home, "ratchet/rk", legacy_number) != 0 ||
	    cleanup_producer_temp(home, "ratchet/ident", legacy_number) != 0 ||
	    validate_entry(home, "ratchet/rk", legacy_number, 0) != 0 ||
	    validate_entry(home, "ratchet/ident", legacy_number, 0) != 0 ||
	    cleanup_producer_temp(home, "ratchet/rk", stable_id) != 0 ||
	    cleanup_producer_temp(home, "ratchet/ident", stable_id) != 0 ||
	    validate_entry(home, "avatars", legacy_avatar, 0) != 0 ||
	    migrate_entry(home, "history", legacy_number, stable_id, 1) != 0 ||
	    validate_history(home, stable_id) != 0 ||
	    migrate_entry(home, "avatars", legacy_avatar, stable_avatar, 0) != 0 ||
	    validate_entry(home, "avatars", stable_avatar, 0) != 0 ||
	    migrate_entry(home, "ratchet/rk", legacy_number, stable_id, 0) != 0 ||
	    migrate_entry(home, "ratchet/ident", legacy_number, stable_id, 0) != 0 ||
	    validate_entry(home, "ratchet/rk", stable_id, 0) != 0 ||
	    validate_entry(home, "ratchet/ident", stable_id, 0) != 0 ||
	    validate_stable_sessions(home, stable_id) != 0 ||
	    migrate_sessions(home, legacy_number, stable_id) != 0 ||
	    validate_stable_sessions(home, stable_id) != 0)
		return -1;
	return 0;
}

int omaq_direct_state_archive_legacy(const char *home, const char *legacy_number)
{
	char legacy_avatar[32];

	if (!decimal_u32_canonical(legacy_number) ||
	    snprintf(legacy_avatar, sizeof(legacy_avatar), "%s.png", legacy_number) >=
		(int)sizeof(legacy_avatar))
		return -1;
	if (cleanup_producer_temp(home, "ratchet/rk", legacy_number) != 0 ||
	    cleanup_producer_temp(home, "ratchet/ident", legacy_number) != 0 ||
	    validate_history(home, legacy_number) != 0 ||
	    migrate_entry(home, "history", legacy_number, NULL, 1) != 0 ||
	    migrate_entry(home, "avatars", legacy_avatar, NULL, 0) != 0 ||
	    migrate_entry(home, "ratchet/rk", legacy_number, NULL, 0) != 0 ||
	    migrate_entry(home, "ratchet/ident", legacy_number, NULL, 0) != 0 ||
	    archive_sessions(home, legacy_number) != 0)
		return -1;
	return 0;
}


typedef struct {
	uint32_t number;
	char key[65];
} saved_friend;

static int saved_friend_valid(const saved_friend *friends, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		if (!lower_hex_key(friends[i].key))
			return 0;
		for (size_t j = 0; j < i; j++)
			if (friends[i].number == friends[j].number ||
			    strcmp(friends[i].key, friends[j].key) == 0)
				return 0;
	}
	return 1;
}

static int load_friend_map(const char *home, saved_friend *friends, size_t *count)
{
	char buffer[8192], *line, *save = NULL;
	struct stat st;
	ssize_t got;
	int home_fd, fd, result = -1;

	if (!home || !friends || !count)
		return -1;
	*count = 0;
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (home_fd < 0)
		return -1;
	fd = openat(home_fd, "direct-friends.tsv", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	close(home_fd);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    (st.st_mode & 0777) != 0600 || st.st_nlink != 1 || st.st_size <= 0 ||
	    st.st_size >= (off_t)sizeof(buffer))
		goto done;
	got = read(fd, buffer, sizeof(buffer) - 1);
	if (got != st.st_size || got <= 0 || buffer[got - 1] != '\n' ||
	    memchr(buffer, '\0', (size_t)got) != NULL ||
	    (got > 1 && strstr(buffer, "\n\n") != NULL))
		goto done;
	buffer[got] = '\0';
	line = strtok_r(buffer, "\n", &save);
	if (!line || strcmp(line, "OMAQDF1") != 0)
		goto done;
	while ((line = strtok_r(NULL, "\n", &save)) != NULL) {
		char *tab = strchr(line, '\t'), *end = NULL;
		unsigned long value;
		if (!tab || strchr(tab + 1, '\t') || *count >= OMAQ_DIRECT_STATE_FRIEND_MAX)
			goto done;
		*tab++ = '\0';
		if (!decimal_u32_canonical(line) || !lower_hex_key(tab))
			goto done;
		errno = 0;
		value = strtoul(line, &end, 10);
		if (errno || !end || *end || value > UINT32_MAX)
			goto done;
		friends[*count].number = (uint32_t)value;
		memcpy(friends[*count].key, tab, 65);
		(*count)++;
	}
	result = saved_friend_valid(friends, *count) ? 1 : -1;
done:
	close(fd);
	return result;
}

static int friend_number_compare(const void *left, const void *right)
{
	const saved_friend *a = left, *b = right;
	return a->number < b->number ? -1 : a->number > b->number ? 1 : 0;
}

static int save_friend_map(const char *home, const omaq_direct_state_friend *current,
			   size_t current_count)
{
	saved_friend sorted[OMAQ_DIRECT_STATE_FRIEND_MAX];
	char temporary[64];
	int home_fd = -1, fd = -1, rc = -1;

	if (!home || !current || current_count > OMAQ_DIRECT_STATE_FRIEND_MAX)
		return -1;
	for (size_t i = 0; i < current_count; i++) {
		sorted[i].number = current[i].number;
		memcpy(sorted[i].key, current[i].key, 65);
	}
	if (!saved_friend_valid(sorted, current_count))
		return -1;
	qsort(sorted, current_count, sizeof(sorted[0]), friend_number_compare);
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (home_fd < 0 || snprintf(temporary, sizeof(temporary),
				    ".direct-friends.tsv.tmp.%ld", (long)getpid()) >=
				    (int)sizeof(temporary))
		goto done;
	fd = openat(home_fd, temporary,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		goto done;
	if (dprintf(fd, "OMAQDF1\n") < 0)
		goto done;
	for (size_t i = 0; i < current_count; i++)
		if (dprintf(fd, "%u\t%s\n", sorted[i].number, sorted[i].key) < 0)
			goto done;
	if (fsync(fd) != 0 || close(fd) != 0) {
		fd = -1;
		goto done;
	}
	fd = -1;
	if (renameat(home_fd, temporary, home_fd, "direct-friends.tsv") != 0 ||
	    fsync(home_fd) != 0)
		goto done;
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (rc != 0 && home_fd >= 0)
		unlinkat(home_fd, temporary, 0);
	if (home_fd >= 0)
		close(home_fd);
	return rc;
}

static int add_legacy_number(uint32_t *numbers, size_t *count, uint32_t number)
{
	for (size_t i = 0; i < *count; i++)
		if (numbers[i] == number)
			return 0;
	if (*count >= 1024)
		return -1;
	numbers[(*count)++] = number;
	return 0;
}

static int stable_hex_name(const char *name, char prefix)
{
	if (!name || strlen(name) != 66 || name[0] != prefix || name[1] != ':')
		return 0;
	for (size_t i = 2; i < 66; i++)
		if (!((name[i] >= '0' && name[i] <= '9') ||
		      (name[i] >= 'a' && name[i] <= 'f')))
			return 0;
	return 1;
}

static int stable_nonlegacy_name(const char *name, int kind)
{
	char base[96];
	const char *suffix;
	size_t length;

	if (kind == 0)
		return stable_hex_name(name, 'd') || stable_hex_name(name, 'g') ||
			(name[0] == 'g' && decimal_u32_canonical(name + 1));
	if (kind == 1) {
		if (strcmp(name, "self.png") == 0)
			return 1;
		suffix = strrchr(name, '.');
		if (!suffix || strcmp(suffix, ".png") != 0)
			return 0;
		length = (size_t)(suffix - name);
		if (length >= sizeof(base))
			return 0;
		memcpy(base, name, length);
		base[length] = '\0';
		return stable_hex_name(base, 'd');
	}
	if (kind == 3) {
		length = strlen(name);
		if (length > 4 && strcmp(name + length - 4, ".tmp") == 0) {
			if (length - 4 >= sizeof(base))
				return 0;
			memcpy(base, name, length - 4);
			base[length - 4] = '\0';
			return stable_hex_name(base, 'd');
		}
		return stable_hex_name(name, 'd');
	}
	if (kind == 2) {
		suffix = strrchr(name, '-');
		if (!suffix || (size_t)(suffix - name) >= sizeof(base))
			return 0;
		memcpy(base, name, (size_t)(suffix - name));
		base[(size_t)(suffix - name)] = '\0';
		length = strlen(suffix + 1);
		if (length > 4 && strcmp(suffix + 1 + length - 4, ".tmp") == 0) {
			char device[32];
			length -= 4;
			if (length == 0 || length >= sizeof(device))
				return 0;
			memcpy(device, suffix + 1, length);
			device[length] = '\0';
			return stable_hex_name(base, 'd') && decimal_u32_canonical(device);
		}
		return stable_hex_name(base, 'd') && decimal_u32_canonical(suffix + 1);
	}
	return 0;
}

static int legacy_name_number(const char *name, int kind, uint32_t *number)
{
	char value[128], *end = NULL;
	const char *suffix;
	size_t length;
	unsigned long parsed;
	int session_suffix_ok = 0;

	if (!name || !name[0])
		return -1;
	if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
		return 0;
	if (strncmp(name, ".legacy-direct.", 15) == 0) {
		const char *body = name + 15;
		const char *index = strrchr(body, '.');
		uint32_t ignored;
		if (!index || !decimal_u32_canonical(index + 1) ||
		    (size_t)(index - body) == 0 || (size_t)(index - body) >= sizeof(value))
			return -1;
		memcpy(value, body, (size_t)(index - body));
		value[(size_t)(index - body)] = '\0';
		return legacy_name_number(value, kind, &ignored) == 1 ? 0 : -1;
	}
	if (stable_nonlegacy_name(name, kind))
		return 0;
	suffix = kind == 1 ? strrchr(name, '.') :
		 kind == 2 ? strchr(name, '-') : kind == 3 ? strstr(name, ".tmp") : NULL;
	if (kind == 2 && suffix) {
		char device[32];
		const char *value = suffix + 1;
		size_t device_length = strlen(value);
		if (device_length > 4 && strcmp(value + device_length - 4, ".tmp") == 0)
			device_length -= 4;
		if (device_length > 0 && device_length < sizeof(device)) {
			memcpy(device, value, device_length);
			device[device_length] = '\0';
			session_suffix_ok = decimal_u32_canonical(device);
		}
	}
	length = suffix ? (size_t)(suffix - name) : strlen(name);
	if ((kind == 1 && (!suffix || strcmp(suffix, ".png") != 0)) ||
	    (kind == 2 && (!suffix || !session_suffix_ok)) ||
	    (kind == 3 && suffix && strcmp(suffix, ".tmp") != 0))
		return -1;
	if (length == 0 || length >= sizeof(value))
		return -1;
	memcpy(value, name, length);
	value[length] = '\0';
	if (!decimal_u32_canonical(value))
		return -1;
	errno = 0;
	parsed = strtoul(value, &end, 10);
	if (errno || !end || *end || parsed > UINT32_MAX)
		return -1;
	*number = (uint32_t)parsed;
	return 1;
}

static int collect_legacy_dir(const char *home, const char *relative, int kind,
			      uint32_t *numbers, size_t *count)
{
	struct dirent *entry;
	DIR *directory;
	int fd = open_relative_dir(home, relative);

	if (fd == -2)
		return 0;
	if (fd < 0)
		return -1;
	directory = fdopendir(fd);
	if (!directory) {
		close(fd);
		return -1;
	}
	errno = 0;
	while ((entry = readdir(directory)) != NULL) {
		uint32_t number;
		struct stat st;
		int parsed = legacy_name_number(entry->d_name, kind, &number);
		if (parsed < 0 || (parsed > 0 && add_legacy_number(numbers, count, number) != 0)) {
			closedir(directory);
			return -1;
		}
		if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0 &&
		    (fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
		     (kind == 0 ? !S_ISDIR(st.st_mode) : !S_ISREG(st.st_mode)) ||
		     st.st_uid != geteuid() || (kind != 0 && st.st_nlink != 1) ||
		     (st.st_mode & 0077) != 0 ||
		     (kind != 0 && (st.st_size < 0 || st.st_size > OMAQ_RATCHET_RECORD_MAX)))) {
			closedir(directory);
			return -1;
		}
	}
	if (errno != 0) {
		closedir(directory);
		return -1;
	}
	closedir(directory);
	return 0;
}

static int collect_legacy_numbers(const char *home, uint32_t *numbers, size_t *count)
{
	*count = 0;
	return collect_legacy_dir(home, "history", 0, numbers, count) != 0 ||
	       collect_legacy_dir(home, "avatars", 1, numbers, count) != 0 ||
	       collect_legacy_dir(home, "ratchet/rk", 3, numbers, count) != 0 ||
	       collect_legacy_dir(home, "ratchet/ident", 3, numbers, count) != 0 ||
	       collect_legacy_dir(home, "ratchet/sess", 2, numbers, count) != 0 ? -1 : 0;
}

static const char *saved_key_for_number(const saved_friend *saved, size_t count,
					uint32_t number)
{
	for (size_t i = 0; i < count; i++)
		if (saved[i].number == number)
			return saved[i].key;
	return NULL;
}

static int cleanup_producer_temp(const char *home, const char *relative,
				 const char *name)
{
	char temporary[NAME_MAX + 1];
	struct stat st;
	int fd, rc = -1;

	if (!name || snprintf(temporary, sizeof(temporary), "%s.tmp", name) >=
	    (int)sizeof(temporary))
		return -1;
	fd = open_relative_dir(home, relative);
	if (fd == -2)
		return 0;
	if (fd < 0)
		return -1;
	if (fstatat(fd, temporary, &st, AT_SYMLINK_NOFOLLOW) != 0) {
		rc = errno == ENOENT ? 0 : -1;
	} else if (S_ISREG(st.st_mode) && st.st_uid == geteuid() && st.st_nlink == 1 &&
		   (st.st_mode & 0077) == 0 && st.st_size >= 0 &&
		   st.st_size <= OMAQ_RATCHET_RECORD_MAX &&
		   unlinkat(fd, temporary, 0) == 0) {
		rc = fsync(fd);
	}
	close(fd);
	return rc;
}

static int validate_entry(const char *home, const char *relative,
			  const char *name, int directory)
{
	struct stat st;
	int fd = open_relative_dir(home, relative);
	int rc = -1;

	if (fd == -2)
		return 0;
	if (fd < 0)
		return -1;
	if (fstatat(fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
		rc = errno == ENOENT ? 0 : -1;
	else if (!entry_type_ok(&st, directory))
		rc = -1;
	else if (!directory && (st.st_uid != geteuid() || st.st_nlink != 1 ||
			       (st.st_mode & 0077) != 0 || st.st_size <= 0 ||
			       st.st_size > OMAQ_RATCHET_RECORD_MAX))
		rc = -1;
	else
		rc = 0;
	close(fd);
	return rc;
}

static int validate_stable_sessions(const char *home, const char *stable_id)
{
	char prefix[OMAQ_DIRECT_STATE_ID_MAX + 2];
	struct dirent *entry;
	struct stat st;
	DIR *directory;
	int fd = open_relative_dir(home, "ratchet/sess");
	size_t prefix_length;

	if (fd == -2)
		return 0;
	if (fd < 0 || snprintf(prefix, sizeof(prefix), "%s-", stable_id) >=
			(int)sizeof(prefix)) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	prefix_length = strlen(prefix);
	directory = fdopendir(fd);
	if (!directory) {
		close(fd);
		return -1;
	}
	errno = 0;
	while ((entry = readdir(directory)) != NULL) {
		char device[32];
		const char *suffix;
		size_t length;
		int temporary = 0;
		if (strncmp(entry->d_name, prefix, prefix_length) != 0)
			continue;
		suffix = entry->d_name + prefix_length;
		length = strlen(suffix);
		if (length > 4 && strcmp(suffix + length - 4, ".tmp") == 0) {
			temporary = 1;
			length -= 4;
		}
		if (length == 0 || length >= sizeof(device)) {
			closedir(directory);
			return -1;
		}
		memcpy(device, suffix, length);
		device[length] = '\0';
		if (!decimal_u32_canonical(device) ||
		    fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
		    !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
		    (st.st_mode & 0077) != 0 || st.st_size < 0 ||
		    (!temporary && st.st_size == 0) ||
		    st.st_size > OMAQ_RATCHET_RECORD_MAX ||
		    (temporary && unlinkat(fd, entry->d_name, 0) != 0)) {
			closedir(directory);
			return -1;
		}
	}
	if (errno != 0 || fsync(fd) != 0) {
		closedir(directory);
		return -1;
	}
	closedir(directory);
	return 0;
}

static int peer_record_suffix_ok(const char *suffix, int prekey)
{
	const char *end = suffix;

	if (!suffix || !suffix[0] || (suffix[0] == '0' && suffix[1]))
		return 0;
	while (*end >= '0' && *end <= '9')
		end++;
	if (end == suffix)
		return 0;
	if (!*end)
		return 1;
	if (strcmp(end, ".tmp") == 0)
		return 1;
	return prekey && strcmp(end, ".used") == 0;
}

static int remove_peer_records(const char *home, const char *relative,
			       const char *stable, int prefixed, int prekey)
{
	int fd = open_relative_dir(home, relative);
	DIR *directory;
	struct dirent *entry;
	size_t stable_length = strlen(stable);

	if (fd == -2)
		return 0;
	if (fd < 0)
		return -1;
	if (!prefixed) {
		struct stat st;
		if (fstatat(fd, stable, &st, AT_SYMLINK_NOFOLLOW) != 0) {
			int missing = errno == ENOENT;
			close(fd);
			return missing ? 0 : -1;
		}
		if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
		    (st.st_mode & 0077) != 0 || st.st_size <= 0 ||
		    st.st_size > OMAQ_RATCHET_RECORD_MAX || unlinkat(fd, stable, 0) != 0 ||
		    fsync(fd) != 0) {
			close(fd);
			return -1;
		}
		close(fd);
		return 0;
	}
	directory = fdopendir(fd);
	if (!directory) {
		close(fd);
		return -1;
	}
	errno = 0;
	while ((entry = readdir(directory)) != NULL) {
		struct stat st;
		const char *suffix;
		if (strncmp(entry->d_name, stable, stable_length) != 0 ||
		    entry->d_name[stable_length] != '-')
			continue;
		suffix = entry->d_name + stable_length + 1;
		if (!peer_record_suffix_ok(suffix, prekey) ||
		    fstatat(fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
		    !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
		    (st.st_mode & 0077) != 0 || st.st_size < 0 ||
		    st.st_size > OMAQ_RATCHET_RECORD_MAX ||
		    unlinkat(fd, entry->d_name, 0) != 0) {
			closedir(directory);
			return -1;
		}
	}
	if (errno != 0 || fsync(fd) != 0) {
		closedir(directory);
		return -1;
	}
	closedir(directory);
	return 0;
}

static int forget_removed_ratchet(const char *home, const char *key)
{
	char stable[OMAQ_DIRECT_STATE_ID_MAX];

	if (omaq_direct_state_id(key, stable, sizeof(stable)) != 0 ||
	    remove_peer_records(home, "ratchet/rk", stable, 0, 0) != 0 ||
	    remove_peer_records(home, "ratchet/ident", stable, 0, 0) != 0 ||
	    remove_peer_records(home, "ratchet/boot", stable, 0, 0) != 0 ||
	    remove_peer_records(home, "ratchet/reply", stable, 0, 0) != 0 ||
	    remove_peer_records(home, "ratchet/sess", stable, 1, 0) != 0 ||
	    remove_peer_records(home, "ratchet/pre", stable, 1, 1) != 0)
		return -1;
	return 0;
}

static int validate_stable_state(const char *home, const char *key)
{
	char stable[OMAQ_DIRECT_STATE_ID_MAX], avatar[96];

	if (omaq_direct_state_id(key, stable, sizeof(stable)) != 0 ||
	    snprintf(avatar, sizeof(avatar), "%s.png", stable) >= (int)sizeof(avatar) ||
	    validate_entry(home, "history", stable, 1) != 0 ||
	    validate_history(home, stable) != 0 ||
	    validate_entry(home, "avatars", avatar, 0) != 0 ||
	    cleanup_producer_temp(home, "ratchet/rk", stable) != 0 ||
	    cleanup_producer_temp(home, "ratchet/ident", stable) != 0 ||
	    validate_entry(home, "ratchet/rk", stable, 0) != 0 ||
	    validate_entry(home, "ratchet/ident", stable, 0) != 0 ||
	    validate_stable_sessions(home, stable) != 0)
		return -1;
	return 0;
}

int omaq_direct_state_add_pending(const char *home, char key[65], char pin[65])
{
	char buffer[160];
	struct stat st;
	ssize_t got;
	int home_fd, fd, rc = -1;

	if (!home || !key || !pin)
		return -1;
	key[0] = '\0';
	pin[0] = '\0';
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (!private_directory_fd(home_fd)) {
		if (home_fd >= 0)
			close(home_fd);
		return -1;
	}
	fd = openat(home_fd, "direct-add.pending", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	close(home_fd);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    (st.st_mode & 0777) != 0600 || st.st_nlink != 1 || st.st_size != 138)
		goto done;
	got = read(fd, buffer, sizeof(buffer) - 1);
	if (got != 138 || memchr(buffer, '\0', (size_t)got) != NULL ||
	    strncmp(buffer, "OMAQDA1\n", 8) != 0 || buffer[72] != '\t' ||
	    buffer[137] != '\n')
		goto done;
	buffer[72] = '\0';
	buffer[137] = '\0';
	if (!lower_hex_key(buffer + 8) || !lower_hex_key(buffer + 73))
		goto done;
	memcpy(key, buffer + 8, 65);
	memcpy(pin, buffer + 73, 65);
	rc = 1;
done:
	close(fd);
	return rc;
}

int omaq_direct_state_add_begin(const char *home, const char *key, const char *pin)
{
	char existing_key[65], existing_pin[65], temporary[64];
	int state, home_fd = -1, fd = -1, rc = -1;

	if (!home || !lower_hex_key(key) || !lower_hex_key(pin))
		return -1;
	state = omaq_direct_state_add_pending(home, existing_key, existing_pin);
	if (state < 0 || (state == 1 &&
	    (strcmp(existing_key, key) != 0 || strcmp(existing_pin, pin) != 0)))
		return -1;
	if (state == 1)
		return 0;
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (!private_directory_fd(home_fd) ||
	    snprintf(temporary, sizeof(temporary), ".direct-add.pending.tmp.%ld",
		     (long)getpid()) >= (int)sizeof(temporary))
		goto done;
	fd = openat(home_fd, temporary,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0 || dprintf(fd, "OMAQDA1\n%s\t%s\n", key, pin) != 138 ||
	    fsync(fd) != 0 || close(fd) != 0) {
		fd = -1;
		goto done;
	}
	fd = -1;
	if (renameat(home_fd, temporary, home_fd, "direct-add.pending") != 0 ||
	    fsync(home_fd) != 0)
		goto done;
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (rc != 0 && home_fd >= 0)
		(void)unlinkat(home_fd, temporary, 0);
	if (home_fd >= 0)
		close(home_fd);
	return rc;
}

int omaq_direct_state_add_finish(const char *home)
{
	int home_fd, rc;

	if (!home)
		return -1;
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (!private_directory_fd(home_fd)) {
		if (home_fd >= 0)
			close(home_fd);
		return -1;
	}
	rc = unlinkat(home_fd, "direct-add.pending", 0);
	if (rc != 0 && errno != ENOENT) {
		close(home_fd);
		return -1;
	}
	rc = fsync(home_fd);
	close(home_fd);
	return rc;
}

int omaq_direct_state_remove_pending(const char *home, char key[65])
{
	char buffer[96];
	struct stat st;
	ssize_t got;
	int home_fd, fd, rc = -1;

	if (!home || !key)
		return -1;
	key[0] = '\0';
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (home_fd < 0)
		return -1;
	fd = openat(home_fd, "direct-remove.pending", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	close(home_fd);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    (st.st_mode & 0777) != 0600 || st.st_nlink != 1 ||
	    st.st_size <= 0 || st.st_size >= (off_t)sizeof(buffer))
		goto done;
	got = read(fd, buffer, sizeof(buffer) - 1);
	if (got != st.st_size || memchr(buffer, '\0', (size_t)got) != NULL)
		goto done;
	buffer[got] = '\0';
	if (got != 73 || strncmp(buffer, "OMAQDR1\n", 8) != 0 || buffer[72] != '\n')
		goto done;
	buffer[72] = '\0';
	if (!lower_hex_key(buffer + 8))
		goto done;
	memcpy(key, buffer + 8, 64);
	key[64] = '\0';
	rc = 1;
done:
	close(fd);
	return rc;
}

int omaq_direct_state_remove_begin(const char *home, const char *key)
{
	char existing[65], temporary[64];
	int state, home_fd = -1, fd = -1, rc = -1;

	if (!home || !lower_hex_key(key))
		return -1;
	state = omaq_direct_state_remove_pending(home, existing);
	if (state < 0 || (state == 1 && strcmp(existing, key) != 0))
		return -1;
	if (state == 1)
		return 0;
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (home_fd < 0 || snprintf(temporary, sizeof(temporary),
				    ".direct-remove.pending.tmp.%ld", (long)getpid()) >=
				    (int)sizeof(temporary))
		goto done;
	fd = openat(home_fd, temporary,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0 || dprintf(fd, "OMAQDR1\n%s\n", key) != 73 || fsync(fd) != 0 ||
	    close(fd) != 0) {
		fd = -1;
		goto done;
	}
	fd = -1;
	if (renameat(home_fd, temporary, home_fd, "direct-remove.pending") != 0 ||
	    fsync(home_fd) != 0)
		goto done;
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (rc != 0 && home_fd >= 0)
		(void)unlinkat(home_fd, temporary, 0);
	if (home_fd >= 0)
		close(home_fd);
	return rc;
}

int omaq_direct_state_remove_finish(const char *home)
{
	int home_fd, rc;

	if (!home)
		return -1;
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (home_fd < 0)
		return -1;
	rc = unlinkat(home_fd, "direct-remove.pending", 0);
	if (rc != 0 && errno != ENOENT) {
		close(home_fd);
		return -1;
	}
	rc = fsync(home_fd);
	close(home_fd);
	return rc;
}

int omaq_direct_state_bound_id(const char *home, const char *legacy_number,
			       char *out, size_t out_size)
{
	saved_friend saved[OMAQ_DIRECT_STATE_FRIEND_MAX];
	size_t count = 0;
	unsigned long number;
	char *end = NULL;
	int state;

	if (!decimal_u32_canonical(legacy_number) || !out ||
	    out_size < OMAQ_DIRECT_STATE_ID_MAX)
		return -1;
	state = load_friend_map(home, saved, &count);
	if (state <= 0)
		return state;
	errno = 0;
	number = strtoul(legacy_number, &end, 10);
	if (errno || !end || *end || number > UINT32_MAX)
		return -1;
	for (size_t i = 0; i < count; i++)
		if (saved[i].number == (uint32_t)number)
			return omaq_direct_state_id(saved[i].key, out, out_size) == 0 ? 1 : -1;
	return 0;
}

static int cleanup_home_temporaries(const char *home)
{
	static const char *prefixes[] = {
		".direct-friends.tsv.tmp.", ".direct-add.pending.tmp.",
		".direct-remove.pending.tmp."
	};
	struct dirent *entry;
	struct stat st;
	DIR *directory;
	int home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);

	if (!private_directory_fd(home_fd)) {
		if (home_fd >= 0)
			close(home_fd);
		return -1;
	}
	directory = fdopendir(dup(home_fd));
	if (!directory) {
		close(home_fd);
		return -1;
	}
	errno = 0;
	while ((entry = readdir(directory)) != NULL) {
		for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
			size_t length = strlen(prefixes[i]);
			if (strncmp(entry->d_name, prefixes[i], length) != 0)
				continue;
			if (!decimal_u32_canonical(entry->d_name + length) ||
			    fstatat(home_fd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0 ||
			    !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
			    (st.st_mode & 0077) != 0 || st.st_size < 0 || st.st_size > 8192 ||
			    unlinkat(home_fd, entry->d_name, 0) != 0) {
				closedir(directory);
				close(home_fd);
				return -1;
			}
			break;
		}
	}
	if (errno != 0 || fsync(home_fd) != 0) {
		closedir(directory);
		close(home_fd);
		return -1;
	}
	closedir(directory);
	close(home_fd);
	return 0;
}

static int persist_home_reinvite_marker(const char *home)
{
	struct stat st;
	int home_fd, fd, rc = -1;
	static const char value[] = "reinvite required\n";

	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (home_fd < 0)
		return -1;
	fd = openat(home_fd, "direct-state-reinvite.required",
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		if (errno == EEXIST &&
		    fstatat(home_fd, "direct-state-reinvite.required", &st,
			    AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode) &&
		    st.st_uid == geteuid() && (st.st_mode & 0777) == 0600 && st.st_nlink == 1)
			rc = 0;
		close(home_fd);
		return rc;
	}
	if (write(fd, value, sizeof(value) - 1) == (ssize_t)(sizeof(value) - 1) &&
	    fsync(fd) == 0 && fsync(home_fd) == 0)
		rc = 0;
	close(fd);
	if (rc != 0)
		(void)unlinkat(home_fd, "direct-state-reinvite.required", 0);
	close(home_fd);
	return rc;
}

static int reconcile_friend_state(const char *home,
				  const omaq_direct_state_friend *current,
				  size_t current_count, const char *removed_key,
				  int *reinvite_required)
{
	saved_friend saved[OMAQ_DIRECT_STATE_FRIEND_MAX];
	uint32_t legacy[1024];
	size_t saved_count = 0, legacy_count = 0;
	int map_state;

	if (!home || !current || current_count > OMAQ_DIRECT_STATE_FRIEND_MAX ||
	    (removed_key && !lower_hex_key(removed_key)) || !reinvite_required ||
	    cleanup_home_temporaries(home) != 0)
		return -1;
	*reinvite_required = 0;
	for (size_t i = 0; i < current_count; i++) {
		if ((removed_key && strcmp(current[i].key, removed_key) == 0) ||
		    !lower_hex_key(current[i].key) || validate_stable_state(home, current[i].key) != 0)
			return -1;
		for (size_t j = 0; j < i; j++)
			if (current[i].number == current[j].number ||
			    strcmp(current[i].key, current[j].key) == 0)
				return -1;
	}
	map_state = load_friend_map(home, saved, &saved_count);
	if (map_state < 0 || collect_legacy_numbers(home, legacy, &legacy_count) != 0)
		return -1;
	if (map_state == 1)
		for (size_t i = 0; i < saved_count; i++) {
			int present = 0;
			for (size_t j = 0; j < current_count; j++)
				if (strcmp(saved[i].key, current[j].key) == 0) {
					present = 1;
					break;
				}
			if (!present && (!removed_key || strcmp(saved[i].key, removed_key) != 0))
				return -1;
		}
	if (map_state == 0) {
		if (legacy_count > 0 && persist_home_reinvite_marker(home) != 0)
			return -1;
		for (size_t i = 0; i < legacy_count; i++) {
			char number[16];
			if (snprintf(number, sizeof(number), "%u", legacy[i]) >=
				(int)sizeof(number) ||
			    omaq_direct_state_archive_legacy(home, number) != 0)
				return -1;
		}
		*reinvite_required = legacy_count > 0;
	} else {
		for (size_t i = 0; i < legacy_count; i++) {
			char number[16];
			const char *key = saved_key_for_number(saved, saved_count, legacy[i]);
			if (snprintf(number, sizeof(number), "%u", legacy[i]) >=
				(int)sizeof(number))
				return -1;
			if (key) {
				if (omaq_direct_state_migrate(home, number, key) != 0)
					return -1;
			} else {
				if (persist_home_reinvite_marker(home) != 0 ||
				    omaq_direct_state_archive_legacy(home, number) != 0)
					return -1;
				*reinvite_required = 1;
			}
		}
	}
	if (removed_key && forget_removed_ratchet(home, removed_key) != 0)
		return -1;
	return save_friend_map(home, current, current_count);
}

int omaq_direct_state_reconcile(const char *home,
				const omaq_direct_state_friend *current,
				size_t current_count, int *reinvite_required)
{
	return reconcile_friend_state(home, current, current_count, NULL,
				      reinvite_required);
}

int omaq_direct_state_reconcile_removed(const char *home,
					const omaq_direct_state_friend *current,
					size_t current_count, const char *removed_key,
					int *reinvite_required)
{
	return reconcile_friend_state(home, current, current_count, removed_key,
				      reinvite_required);
}
