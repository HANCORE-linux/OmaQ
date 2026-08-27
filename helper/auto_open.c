#define _DEFAULT_SOURCE
#include "auto_open.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define AUTO_OPEN_FILE_MAX 65536

static const char *skip_ws(const char *text)
{
	while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
		text++;
	return text;
}

static int fingerprint_ok(const char *fingerprint)
{
	if (!fingerprint || strlen(fingerprint) != 64)
		return 0;
	for (size_t i = 0; i < 64; i++)
		if (!((fingerprint[i] >= '0' && fingerprint[i] <= '9') ||
		      (fingerprint[i] >= 'a' && fingerprint[i] <= 'f')))
			return 0;
	return 1;
}

static int stable_id_ok(const char *conversation)
{
	if (!conversation || strlen(conversation) != 66 || conversation[1] != ':' ||
	    (conversation[0] != 'd' && conversation[0] != 'g'))
		return 0;
	for (size_t i = 2; i < 66; i++)
		if (!((conversation[i] >= '0' && conversation[i] <= '9') ||
		      (conversation[i] >= 'a' && conversation[i] <= 'f')))
			return 0;
	return 1;
}

static int legacy_id_ok(const char *conversation)
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

static int parse_string(const char **cursor, char *out, size_t out_size)
{
	const char *text;
	size_t length = 0;

	if (!cursor || !*cursor || !out || out_size == 0 || **cursor != '"')
		return -1;
	text = *cursor + 1;
	while (*text && *text != '"') {
		unsigned char value = (unsigned char)*text++;
		if (value == '\\' || value < 0x20 || length + 1 >= out_size)
			return -1;
		out[length++] = (char)value;
	}
	if (*text != '"')
		return -1;
	out[length] = '\0';
	*cursor = text + 1;
	return 0;
}

static int parse_bool(const char **cursor, int *value)
{
	if (!cursor || !*cursor || !value)
		return -1;
	if (strncmp(*cursor, "true", 4) == 0) {
		*value = 1;
		*cursor += 4;
		return 0;
	}
	if (strncmp(*cursor, "false", 5) == 0) {
		*value = 0;
		*cursor += 5;
		return 0;
	}
	return -1;
}

static int parse_version(const char **cursor, int *version)
{
	if (!cursor || !*cursor || !version ||
	    ((*cursor)[0] != '1' && (*cursor)[0] != '2'))
		return -1;
	*version = (*cursor)[0] - '0';
	(*cursor)++;
	if ((*cursor)[0] >= '0' && (*cursor)[0] <= '9')
		return -1;
	return 0;
}

static int consume_key(const char **cursor, char *key, size_t key_size)
{
	const char *text;

	if (!cursor || !*cursor)
		return -1;
	text = skip_ws(*cursor);
	if (parse_string(&text, key, key_size) != 0)
		return -1;
	text = skip_ws(text);
	if (*text++ != ':')
		return -1;
	*cursor = skip_ws(text);
	return 0;
}

void omaq_auto_open_init(omaq_auto_open_state *state)
{
	if (!state)
		return;
	memset(state, 0, sizeof(*state));
	state->direct_default = 1;
}

static int add_entry(omaq_auto_open_state *state, const char *conversation,
		     int enabled)
{
	if (!state || !stable_id_ok(conversation) || (enabled != 0 && enabled != 1) ||
	    state->count >= OMAQ_AUTO_OPEN_MAX)
		return -1;
	for (size_t i = 0; i < state->count; i++)
		if (strcmp(state->entries[i].conversation, conversation) == 0)
			return -1;
	snprintf(state->entries[state->count].conversation,
		 sizeof(state->entries[state->count].conversation), "%s", conversation);
	state->entries[state->count].enabled = enabled;
	state->count++;
	return 0;
}

static int parse_entries(const char **cursor, omaq_auto_open_state *state,
			 int allow_legacy, int *legacy)
{
	const char *text;
	int first = 1;

	if (!cursor || !*cursor || !state || !legacy)
		return -1;
	text = skip_ws(*cursor);
	if (*text++ != '{')
		return -1;
	text = skip_ws(text);
	while (*text && *text != '}') {
		char conversation[OMAQ_AUTO_OPEN_ID_MAX];
		int enabled;

		if (!first) {
			if (*text++ != ',')
				return -1;
			text = skip_ws(text);
		}
		first = 0;
		if (consume_key(&text, conversation, sizeof(conversation)) != 0 ||
		    parse_bool(&text, &enabled) != 0)
			return -1;
		text = skip_ws(text);
		if (stable_id_ok(conversation)) {
			if (add_entry(state, conversation, enabled) != 0)
				return -1;
		} else if (allow_legacy && legacy_id_ok(conversation)) {
			*legacy = 1;
		} else {
			return -1;
		}
	}
	if (*text++ != '}')
		return -1;
	*cursor = skip_ws(text);
	return 0;
}

static int parse_document(const char *document, omaq_auto_open_state *state,
			  int *version, int *legacy)
{
	const char *text;
	char first_key[OMAQ_AUTO_OPEN_ID_MAX];

	if (!document || !state || !version || !legacy)
		return -1;
	omaq_auto_open_init(state);
	*version = 0;
	*legacy = 0;
	text = skip_ws(document);
	if (*text++ != '{')
		return -1;
	text = skip_ws(text);
	if (*text == '}') {
		text = skip_ws(text + 1);
		return *text == '\0' ? 0 : -1;
	}
	if (consume_key(&text, first_key, sizeof(first_key)) != 0)
		return -1;
	if (strcmp(first_key, "version") == 0) {
		char key[32];
		if (parse_version(&text, version) != 0)
			return -1;
		text = skip_ws(text);
		if (*text++ != ',')
			return -1;
		if (consume_key(&text, key, sizeof(key)) != 0)
			return -1;
		if (*version == 2) {
			if (strcmp(key, "directDefault") != 0 ||
			    parse_bool(&text, &state->direct_default) != 0)
				return -1;
			text = skip_ws(text);
			if (*text++ != ',' || consume_key(&text, key, sizeof(key)) != 0)
				return -1;
		}
		if (strcmp(key, "users") != 0 ||
		    parse_entries(&text, state, *version < 2, legacy) != 0)
			return -1;
		text = skip_ws(text);
		if (*text++ != '}')
			return -1;
		return *skip_ws(text) == '\0' ? 0 : -1;
	}
	{
		int enabled;
		if (parse_bool(&text, &enabled) != 0)
			return -1;
		if (stable_id_ok(first_key)) {
			if (add_entry(state, first_key, enabled) != 0)
				return -1;
		} else if (legacy_id_ok(first_key)) {
			*legacy = 1;
		} else {
			return -1;
		}
	}
	text = skip_ws(text);
	while (*text == ',') {
		char conversation[OMAQ_AUTO_OPEN_ID_MAX];
		int enabled;
		text++;
		if (consume_key(&text, conversation, sizeof(conversation)) != 0 ||
		    parse_bool(&text, &enabled) != 0)
			return -1;
		if (stable_id_ok(conversation)) {
			if (add_entry(state, conversation, enabled) != 0)
				return -1;
		} else if (legacy_id_ok(conversation)) {
			*legacy = 1;
		} else {
			return -1;
		}
		text = skip_ws(text);
	}
	if (*text++ != '}')
		return -1;
	return *skip_ws(text) == '\0' ? 0 : -1;
}

static int state_directory(const char *state_dir)
{
	struct stat status;
	int directory;

	if (!state_dir || !state_dir[0])
		return -1;
	directory = open(state_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory < 0)
		return -1;
	if (fstat(directory, &status) != 0 || !S_ISDIR(status.st_mode) ||
	    status.st_uid != geteuid() || (status.st_mode & 0077) != 0) {
		close(directory);
		return -1;
	}
	return directory;
}

int omaq_auto_open_retire_global(const char *state_dir, const char *fingerprint)
{
	char migrated[96];
	struct stat source, destination;
	int directory, result = -1;

	if (!fingerprint_ok(fingerprint) ||
	    snprintf(migrated, sizeof(migrated), "auto-open.migrated.%s.json",
		     fingerprint) >= (int)sizeof(migrated))
		return -1;
	directory = state_directory(state_dir);
	if (directory < 0)
		return -1;
	if (fstatat(directory, "auto-open.json", &source, AT_SYMLINK_NOFOLLOW) != 0) {
		result = errno == ENOENT ? 0 : -1;
		goto done;
	}
	if (!S_ISREG(source.st_mode) || source.st_uid != geteuid() ||
	    source.st_nlink != 1 || (source.st_mode & 0022) != 0 ||
	    source.st_size <= 0 || source.st_size > AUTO_OPEN_FILE_MAX)
		goto done;
	if (fstatat(directory, migrated, &destination, AT_SYMLINK_NOFOLLOW) == 0) {
		if (!S_ISREG(destination.st_mode) || destination.st_uid != geteuid() ||
		    destination.st_nlink != 1 || (destination.st_mode & 0077) != 0 ||
		    unlinkat(directory, "auto-open.json", 0) != 0)
			goto done;
	} else if (errno != ENOENT ||
		   renameat(directory, "auto-open.json", directory, migrated) != 0) {
		goto done;
	}
	result = fsync(directory);
done:
	close(directory);
	return result;
}

int omaq_auto_open_source_name(const char *fingerprint, omaq_auto_open_source source,
			       char *out, size_t out_size)
{
	if (!fingerprint_ok(fingerprint) || !out || out_size == 0)
		return -1;
	if (source == OMAQ_AUTO_OPEN_SOURCE_LEGACY_GLOBAL)
		return snprintf(out, out_size, "auto-open.json") < (int)out_size ? 0 : -1;
	if (source == OMAQ_AUTO_OPEN_SOURCE_CURRENT ||
	    source == OMAQ_AUTO_OPEN_SOURCE_LEGACY_ACTIVE)
		return snprintf(out, out_size, "auto-open.%s.json", fingerprint) <
			(int)out_size ? 0 : -1;
	return -1;
}

static int read_document(int directory, const char *name, char *document,
			 size_t document_size, int *private_mode)
{
	struct stat status, current;
	size_t offset = 0;
	int fd, result = -1;

	if (directory < 0 || !name || !document || document_size < 2 || !private_mode)
		return -1;
	fd = openat(directory, name,
		    O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
	    status.st_uid != geteuid() || status.st_nlink != 1 ||
	    (status.st_mode & 0022) != 0 || status.st_size <= 0 ||
	    status.st_size > AUTO_OPEN_FILE_MAX ||
	    status.st_size >= (off_t)document_size)
		goto done;
	while (offset < (size_t)status.st_size) {
		ssize_t got = read(fd, document + offset,
				   (size_t)status.st_size - offset);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			goto done;
		}
		if (got == 0)
			goto done;
		offset += (size_t)got;
	}
	if (fstat(fd, &current) != 0 || current.st_dev != status.st_dev ||
	    current.st_ino != status.st_ino || current.st_size != status.st_size ||
	    current.st_mtim.tv_sec != status.st_mtim.tv_sec ||
	    current.st_mtim.tv_nsec != status.st_mtim.tv_nsec ||
	    memchr(document, '\0', offset) != NULL)
		goto done;
	document[offset] = '\0';
	*private_mode = (status.st_mode & 0077) == 0;
	result = 1;
done:
	close(fd);
	return result;
}

int omaq_auto_open_load(const char *state_dir, const char *fingerprint,
			omaq_auto_open_state *state, omaq_auto_open_source *source)
{
	char active[96], document[AUTO_OPEN_FILE_MAX + 1];
	int directory, found, version, legacy, private_mode = 1;

	if (!state || !source || !fingerprint_ok(fingerprint) ||
	    omaq_auto_open_source_name(fingerprint, OMAQ_AUTO_OPEN_SOURCE_CURRENT,
				       active, sizeof(active)) != 0)
		return -1;
	omaq_auto_open_init(state);
	*source = OMAQ_AUTO_OPEN_SOURCE_NONE;
	directory = state_directory(state_dir);
	if (directory < 0)
		return -1;
	found = read_document(directory, active, document, sizeof(document),
			      &private_mode);
	if (found == 0) {
		found = read_document(directory, "auto-open.json", document,
				      sizeof(document), &private_mode);
		if (found == 1)
			*source = OMAQ_AUTO_OPEN_SOURCE_LEGACY_GLOBAL;
	} else if (found == 1) {
		*source = OMAQ_AUTO_OPEN_SOURCE_CURRENT;
	}
	close(directory);
	if (found < 0)
		return -1;
	if (found == 0)
		return 0;
	if (parse_document(document, state, &version, &legacy) != 0)
		return -1;
	if (version < 2 ||
	    (*source == OMAQ_AUTO_OPEN_SOURCE_CURRENT && !private_mode)) {
		if (*source == OMAQ_AUTO_OPEN_SOURCE_CURRENT)
			*source = OMAQ_AUTO_OPEN_SOURCE_LEGACY_ACTIVE;
		if (legacy)
			state->direct_default = 0;
	} else if (*source == OMAQ_AUTO_OPEN_SOURCE_LEGACY_GLOBAL) {
		/* A global file is legacy regardless of its internal schema. */
		state->direct_default = state->direct_default ? 1 : 0;
	}
	return 0;
}

static int remove_safe_temporary(int directory, const char *temporary)
{
	struct stat status;

	if (fstatat(directory, temporary, &status, AT_SYMLINK_NOFOLLOW) != 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
	    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
	    status.st_size < 0 || status.st_size > AUTO_OPEN_FILE_MAX)
		return -1;
	return unlinkat(directory, temporary, 0);
}

int omaq_auto_open_save(const char *state_dir, const char *fingerprint,
			const omaq_auto_open_state *state)
{
	char name[96], temporary[128];
	FILE *file = NULL;
	int directory = -1, fd = -1, result = -1;

	if (!state || state->count > OMAQ_AUTO_OPEN_MAX ||
	    (state->direct_default != 0 && state->direct_default != 1) ||
	    omaq_auto_open_source_name(fingerprint, OMAQ_AUTO_OPEN_SOURCE_CURRENT,
				       name, sizeof(name)) != 0 ||
	    snprintf(temporary, sizeof(temporary), ".%s.tmp", name) >=
		    (int)sizeof(temporary))
		return -1;
	for (size_t i = 0; i < state->count; i++) {
		if (!stable_id_ok(state->entries[i].conversation) ||
		    (state->entries[i].enabled != 0 && state->entries[i].enabled != 1))
			return -1;
		for (size_t j = 0; j < i; j++)
			if (strcmp(state->entries[i].conversation,
				   state->entries[j].conversation) == 0)
				return -1;
	}
	directory = state_directory(state_dir);
	if (directory < 0 || remove_safe_temporary(directory, temporary) != 0)
		goto done;
	fd = openat(directory, temporary,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		goto done;
	file = fdopen(fd, "w");
	if (!file)
		goto done;
	fd = -1;
	if (fprintf(file, "{\"version\":2,\"directDefault\":%s,\"users\":{",
		    state->direct_default ? "true" : "false") < 0)
		goto done;
	for (size_t i = 0; i < state->count; i++)
		if (fprintf(file, "%s\"%s\":%s", i ? "," : "",
			    state->entries[i].conversation,
			    state->entries[i].enabled ? "true" : "false") < 0)
			goto done;
	if (fprintf(file, "}}\n") < 0 || fflush(file) != 0 ||
	    fsync(fileno(file)) != 0 || fclose(file) != 0) {
		file = NULL;
		goto done;
	}
	file = NULL;
	if (renameat(directory, temporary, directory, name) != 0 ||
	    fsync(directory) != 0)
		goto done;
	result = 0;
done:
	if (file)
		fclose(file);
	else if (fd >= 0)
		close(fd);
	if (result != 0 && directory >= 0)
		(void)unlinkat(directory, temporary, 0);
	if (directory >= 0)
		close(directory);
	return result;
}

int omaq_auto_open_set(omaq_auto_open_state *state, const char *conversation,
		       int enabled)
{
	if (!state || !stable_id_ok(conversation) || (enabled != 0 && enabled != 1))
		return -1;
	for (size_t i = 0; i < state->count; i++)
		if (strcmp(state->entries[i].conversation, conversation) == 0) {
			state->entries[i].enabled = enabled;
			return 0;
		}
	return add_entry(state, conversation, enabled);
}
