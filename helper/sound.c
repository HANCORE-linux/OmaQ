#define _DEFAULT_SOURCE
#include "sound.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#define SOUND_DIRECTORY "custom-sounds"

static int private_directory(const char *home, int create)
{
	struct stat status;
	int home_fd = -1, directory = -1;

	if (!home || home[0] != '/')
		return -1;
	home_fd = open(home, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (home_fd < 0)
		return -1;
	if (fstat(home_fd, &status) != 0 || !S_ISDIR(status.st_mode) ||
	    status.st_uid != geteuid() || (status.st_mode & 0077) != 0)
		goto done;
	if (create && mkdirat(home_fd, SOUND_DIRECTORY, 0700) != 0 && errno != EEXIST)
		goto done;
	directory = openat(home_fd, SOUND_DIRECTORY,
			   O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory < 0)
		goto done;
	if (fstat(directory, &status) != 0 || !S_ISDIR(status.st_mode) ||
	    status.st_uid != geteuid() || (status.st_mode & 0077) != 0) {
		close(directory);
		directory = -1;
	}
done:
	close(home_fd);
	return directory;
}

static int lower_id_ok(const char *id)
{
	if (!id || strlen(id) != OMAQ_SOUND_ID_HEX)
		return 0;
	for (size_t i = 0; i < OMAQ_SOUND_ID_HEX; i++)
		if (!((id[i] >= '0' && id[i] <= '9') ||
		      (id[i] >= 'a' && id[i] <= 'f')))
			return 0;
	return 1;
}

static int private_regular(int fd, off_t maximum, struct stat *out)
{
	struct stat status;

	if (fd < 0 || fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
	    status.st_uid != geteuid() || status.st_nlink != 1 ||
	    (status.st_mode & 0077) != 0 || status.st_size <= 0 ||
	    status.st_size > maximum)
		return -1;
	if (out)
		*out = status;
	return 0;
}

static int source_regular(int fd, struct stat *out)
{
	struct stat status;

	if (fd < 0 || fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
	    status.st_uid != geteuid() || status.st_size <= 0 ||
	    status.st_size > (off_t)OMAQ_SOUND_FILE_MAX)
		return -1;
	if (out)
		*out = status;
	return 0;
}

static uint16_t little_u16(const unsigned char *value)
{
	return (uint16_t)value[0] | (uint16_t)((uint16_t)value[1] << 8);
}

static uint32_t little_u32(const unsigned char *value)
{
	return (uint32_t)value[0] | ((uint32_t)value[1] << 8) |
		((uint32_t)value[2] << 16) | ((uint32_t)value[3] << 24);
}

static int pread_exact(int fd, void *buffer, size_t length, off_t offset)
{
	size_t done = 0;

	while (done < length) {
		ssize_t got = pread(fd, (char *)buffer + done, length - done,
				    offset + (off_t)done);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return -1;
		done += (size_t)got;
	}
	return 0;
}

static int pcm_wav_ok(int fd, off_t file_size)
{
	unsigned char header[12];
	off_t offset = 12;
	uint32_t sample_rate = 0, data_size = 0;
	uint16_t block_align = 0;
	int found_format = 0, found_data = 0;

	if (file_size < 44 || pread_exact(fd, header, sizeof(header), 0) != 0 ||
	    memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0 ||
	    (uint64_t)little_u32(header + 4) + 8u != (uint64_t)file_size)
		return 0;
	while (offset < file_size) {
		unsigned char chunk[8];
		uint32_t length;
		uint64_t next;

		if (file_size - offset < 8 ||
		    pread_exact(fd, chunk, sizeof(chunk), offset) != 0)
			return 0;
		length = little_u32(chunk + 4);
		next = (uint64_t)offset + 8u + length + (length & 1u);
		if (next > (uint64_t)file_size)
			return 0;
		if (memcmp(chunk, "fmt ", 4) == 0) {
			unsigned char format[16];
			uint16_t encoding, channels, bits;
			uint32_t byte_rate;

			if (found_format || found_data || length != sizeof(format) ||
			    pread_exact(fd, format, sizeof(format), offset + 8) != 0)
				return 0;
			encoding = little_u16(format);
			channels = little_u16(format + 2);
			sample_rate = little_u32(format + 4);
			byte_rate = little_u32(format + 8);
			block_align = little_u16(format + 12);
			bits = little_u16(format + 14);
			if ((encoding != 1 && encoding != 3) || channels < 1 || channels > 2 ||
			    sample_rate < 8000 || sample_rate > 192000 ||
			    (bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
			    (encoding == 3 && bits != 32) ||
			    block_align != channels * (bits / 8u) ||
			    byte_rate != sample_rate * block_align)
				return 0;
			found_format = 1;
		} else if (memcmp(chunk, "data", 4) == 0) {
			if (!found_format || found_data || length == 0 ||
			    block_align == 0 || length % block_align != 0)
				return 0;
			data_size = length;
			found_data = 1;
		} else {
			return 0;
		}
		offset = (off_t)next;
	}
	if (!found_format || !found_data || offset != file_size)
		return 0;
	return (uint64_t)data_size <= (uint64_t)sample_rate * block_align * 30u;
}

static int utf8_label_ok(const char *value, size_t length)
{
	const unsigned char *text = (const unsigned char *)value;
	size_t i = 0;

	if (!value || length == 0 || length > OMAQ_SOUND_LABEL_MAX)
		return 0;
	while (i < length) {
		unsigned char c = text[i++];
		if (c == 0 || c < 0x20 || c == 0x7f)
			return 0;
		if (c < 0x80)
			continue;
		if (c >= 0xc2 && c <= 0xdf) {
			if (i >= length || text[i] < 0x80 || text[i] > 0xbf)
				return 0;
			i++;
			continue;
		}
		if (c >= 0xe0 && c <= 0xef) {
			if (i + 1 >= length || text[i] < 0x80 || text[i] > 0xbf ||
			    text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
			    (c == 0xe0 && text[i] < 0xa0) ||
			    (c == 0xed && text[i] > 0x9f))
				return 0;
			i += 2;
			continue;
		}
		if (c >= 0xf0 && c <= 0xf4) {
			if (i + 2 >= length || text[i] < 0x80 || text[i] > 0xbf ||
			    text[i + 1] < 0x80 || text[i + 1] > 0xbf ||
			    text[i + 2] < 0x80 || text[i + 2] > 0xbf ||
			    (c == 0xf0 && text[i] < 0x90) ||
			    (c == 0xf4 && text[i] > 0x8f))
				return 0;
			i += 3;
			continue;
		}
		return 0;
	}
	return 1;
}

static void source_label(const char *source, char *label, size_t capacity)
{
	const char *base = strrchr(source, '/');
	size_t length;

	base = base ? base + 1 : source;
	length = strlen(base);
	if (length > OMAQ_SOUND_LABEL_MAX)
		length = OMAQ_SOUND_LABEL_MAX;
	while (length > 0 && base[length - 1] != '.' && base[length - 1] != '/')
		length--;
	if (length > 1 && base[length - 1] == '.')
		length--;
	else
		length = strlen(base) > OMAQ_SOUND_LABEL_MAX
			? OMAQ_SOUND_LABEL_MAX : strlen(base);
	while (length > 0 && base[length - 1] == ' ')
		length--;
	if (!utf8_label_ok(base, length) || length + 1 > capacity) {
		snprintf(label, capacity, "Custom sound");
		return;
	}
	memcpy(label, base, length);
	label[length] = '\0';
}

static int random_id(char out[OMAQ_SOUND_ID_HEX + 1])
{
	unsigned char bytes[OMAQ_SOUND_ID_HEX / 2];
	size_t offset = 0;
	static const char hex[] = "0123456789abcdef";

	while (offset < sizeof(bytes)) {
		ssize_t got = getrandom(bytes + offset, sizeof(bytes) - offset, 0);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return -1;
		offset += (size_t)got;
	}
	for (size_t i = 0; i < sizeof(bytes); i++) {
		out[i * 2] = hex[bytes[i] >> 4];
		out[i * 2 + 1] = hex[bytes[i] & 15];
	}
	out[OMAQ_SOUND_ID_HEX] = '\0';
	return 0;
}

static int entry_names(const char *id, char *audio, size_t audio_size,
		       char *name, size_t name_size)
{
	if (!lower_id_ok(id) ||
	    snprintf(audio, audio_size, "%s.audio", id) >= (int)audio_size ||
	    snprintf(name, name_size, "%s.name", id) >= (int)name_size)
		return -1;
	return 0;
}

static int commit_name(const char *id, char *commit, size_t commit_size)
{
	return !lower_id_ok(id) ||
		snprintf(commit, commit_size, "%s.commit", id) >= (int)commit_size
		? -1 : 0;
}

static int generated_regular_at(int directory, const char *name, off_t maximum,
				int *missing)
{
	struct stat status;

	if (missing)
		*missing = 0;
	if (fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW) != 0) {
		if (errno == ENOENT && missing) {
			*missing = 1;
			return 0;
		}
		return -1;
	}
	return S_ISREG(status.st_mode) && status.st_uid == geteuid() &&
		status.st_nlink == 1 && (status.st_mode & 0077) == 0 &&
		status.st_size > 0 && status.st_size <= maximum ? 0 : -1;
}

static int recoverable_generated_at(int directory, const char *name,
				    off_t maximum, int *missing, int *incomplete)
{
	struct stat status;

	*missing = 0;
	*incomplete = 0;
	if (fstatat(directory, name, &status, AT_SYMLINK_NOFOLLOW) != 0) {
		if (errno == ENOENT) {
			*missing = 1;
			return 0;
		}
		return -1;
	}
	if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
	    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
	    status.st_size < 0 || status.st_size > maximum)
		return -1;
	*incomplete = status.st_size == 0;
	return 0;
}

static int recover_sound_directory(int directory)
{
	DIR *stream = NULL;
	struct dirent *entry;
	int changed = 0, seen = 0, result = -1;

	{
		int scan = openat(directory, ".",
				  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (scan < 0)
			return -1;
		stream = fdopendir(scan);
		if (!stream) {
			close(scan);
			return -1;
		}
	}
	while ((entry = readdir(stream))) {
		char id[OMAQ_SOUND_ID_HEX + 1], counterpart[64], required[64];
		const char *suffix;
		size_t length = strlen(entry->d_name);
		off_t maximum;
		int missing = 0, required_missing = 0, current_missing = 0;
		int incomplete = 0, required_incomplete = 0, current_incomplete = 0;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (++seen > 256)
			goto done;
		if (length < OMAQ_SOUND_ID_HEX + 5)
			continue;
		memcpy(id, entry->d_name, OMAQ_SOUND_ID_HEX);
		id[OMAQ_SOUND_ID_HEX] = '\0';
		if (!lower_id_ok(id))
			continue;
		suffix = entry->d_name + OMAQ_SOUND_ID_HEX;
		if (strcmp(suffix, ".audio.delete") == 0 ||
		    strcmp(suffix, ".name.delete") == 0 ||
		    strcmp(suffix, ".commit.delete") == 0) {
			maximum = strcmp(suffix, ".audio.delete") == 0
				? OMAQ_SOUND_FILE_MAX : (strcmp(suffix, ".name.delete") == 0
				? OMAQ_SOUND_LABEL_MAX + 1 : 32);
			if (recoverable_generated_at(directory, entry->d_name, maximum,
						   &current_missing,
						   &current_incomplete) != 0)
				goto done;
			if (!current_missing) {
				if (unlinkat(directory, entry->d_name, 0) != 0)
					goto done;
				changed = 1;
			}
			continue;
		}
		if (strcmp(suffix, ".audio") == 0) {
			maximum = OMAQ_SOUND_FILE_MAX;
			if (snprintf(counterpart, sizeof(counterpart), "%s.name", id) >=
			    (int)sizeof(counterpart) || commit_name(id, required,
							 sizeof(required)) != 0)
				goto done;
		} else if (strcmp(suffix, ".name") == 0) {
			maximum = OMAQ_SOUND_LABEL_MAX + 1;
			if (snprintf(counterpart, sizeof(counterpart), "%s.audio", id) >=
			    (int)sizeof(counterpart) || commit_name(id, required,
							 sizeof(required)) != 0)
				goto done;
		} else if (strcmp(suffix, ".commit") == 0) {
			maximum = 32;
			if (snprintf(counterpart, sizeof(counterpart), "%s.audio", id) >=
			    (int)sizeof(counterpart) ||
			    snprintf(required, sizeof(required), "%s.name", id) >=
			    (int)sizeof(required))
				goto done;
		} else {
			continue;
		}
		if (recoverable_generated_at(directory, entry->d_name, maximum,
					   &current_missing,
					   &current_incomplete) != 0)
			goto done;
		if (current_missing)
			continue;
		if (recoverable_generated_at(directory, counterpart,
			strcmp(suffix, ".audio") == 0 ? OMAQ_SOUND_LABEL_MAX + 1 :
			OMAQ_SOUND_FILE_MAX, &missing, &incomplete) != 0 ||
		    recoverable_generated_at(directory, required,
			strcmp(suffix, ".commit") == 0 ? OMAQ_SOUND_LABEL_MAX + 1 : 32,
			&required_missing, &required_incomplete) != 0)
			goto done;
		if (current_incomplete || missing || incomplete || required_missing ||
		    required_incomplete) {
			if (unlinkat(directory, entry->d_name, 0) != 0)
				goto done;
			changed = 1;
		}
	}
	if (changed && fsync(directory) != 0)
		goto done;
	result = 0;
done:
	closedir(stream);
	return result;
}

static int read_exact(int fd, char *buffer, size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t got = read(fd, buffer + offset, length - offset);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return -1;
		offset += (size_t)got;
	}
	return 0;
}

static int write_exact(int fd, const char *buffer, size_t length)
{
	size_t offset = 0;

	while (offset < length) {
		ssize_t written = write(fd, buffer + offset, length - offset);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			return -1;
		offset += (size_t)written;
	}
	return 0;
}

static int load_entry(int directory, const char *home, const char *id,
		      omaq_sound *out)
{
	char audio_name[48], label_name[48], committed_name[48];
	char label[OMAQ_SOUND_LABEL_MAX + 2];
	struct stat audio_status, label_status;
	int audio = -1, metadata = -1, committed = -1, result = -1;

	if (!out || entry_names(id, audio_name, sizeof(audio_name),
				label_name, sizeof(label_name)) != 0 ||
	    commit_name(id, committed_name, sizeof(committed_name)) != 0)
		return -1;
	committed = openat(directory, committed_name,
			   O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (private_regular(committed, 32, NULL) != 0)
		goto done;
	metadata = openat(directory, label_name,
			  O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (private_regular(metadata, OMAQ_SOUND_LABEL_MAX + 1, &label_status) != 0 ||
	    read_exact(metadata, label, (size_t)label_status.st_size) != 0)
		goto done;
	label[label_status.st_size] = '\0';
	if (label_status.st_size < 2 || label[label_status.st_size - 1] != '\n')
		goto done;
	label[label_status.st_size - 1] = '\0';
	if (!utf8_label_ok(label, (size_t)label_status.st_size - 1))
		goto done;
	audio = openat(directory, audio_name,
		       O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (private_regular(audio, OMAQ_SOUND_FILE_MAX, &audio_status) != 0)
		goto done;
	memset(out, 0, sizeof(*out));
	memcpy(out->id, id, OMAQ_SOUND_ID_HEX + 1);
	memcpy(out->label, label, (size_t)label_status.st_size);
	if (snprintf(out->path, sizeof(out->path), "%s/%s/%s", home,
		     SOUND_DIRECTORY, audio_name) >= (int)sizeof(out->path))
		goto done;
	out->size = (uint64_t)audio_status.st_size;
	result = 0;
done:
	if (audio >= 0)
		close(audio);
	if (metadata >= 0)
		close(metadata);
	if (committed >= 0)
		close(committed);
	return result;
}

static int sound_compare(const void *left, const void *right)
{
	const omaq_sound *a = left;
	const omaq_sound *b = right;
	int label = strcmp(a->label, b->label);
	return label != 0 ? label : strcmp(a->id, b->id);
}

int omaq_sound_list(const char *home, omaq_sound *out, int capacity)
{
	DIR *stream = NULL;
	struct dirent *entry;
	int directory = -1, count = 0, result = -1;

	if (!out || capacity <= 0 || capacity > OMAQ_SOUND_MAX)
		return -1;
	directory = private_directory(home, 1);
	if (directory < 0 || recover_sound_directory(directory) != 0) {
		if (directory >= 0)
			close(directory);
		return -1;
	}
	{
		int scan = openat(directory, ".",
				  O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (scan < 0)
			goto done;
		stream = fdopendir(scan);
		if (!stream) {
			close(scan);
			goto done;
		}
	}
	while ((entry = readdir(stream))) {
		size_t length = strlen(entry->d_name);
		char id[OMAQ_SOUND_ID_HEX + 1];

		if (length != OMAQ_SOUND_ID_HEX + 5 ||
		    strcmp(entry->d_name + OMAQ_SOUND_ID_HEX, ".name") != 0)
			continue;
		memcpy(id, entry->d_name, OMAQ_SOUND_ID_HEX);
		id[OMAQ_SOUND_ID_HEX] = '\0';
		if (!lower_id_ok(id) || count >= capacity ||
		    load_entry(directory, home, id, &out[count]) != 0)
			goto done;
		count++;
	}
	qsort(out, (size_t)count, sizeof(*out), sound_compare);
	result = count;
done:
	if (stream)
		closedir(stream);
	close(directory);
	return result;
}

static int copy_source(int source, int destination, off_t expected)
{
	char buffer[16384];
	off_t total = 0;

	while (total < expected) {
		size_t wanted = (size_t)(expected - total);
		ssize_t got;
		if (wanted > sizeof(buffer))
			wanted = sizeof(buffer);
		got = read(source, buffer, wanted);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return -1;
		for (ssize_t offset = 0; offset < got;) {
			ssize_t written = write(destination, buffer + offset,
						(size_t)(got - offset));
			if (written < 0 && errno == EINTR)
				continue;
			if (written <= 0)
				return -1;
			offset += written;
		}
		total += got;
	}
	{
		char extra;
		if (read(source, &extra, 1) != 0)
			return -1;
	}
	return fsync(destination);
}

int omaq_sound_import(const char *home, const char *source_path, omaq_sound *out)
{
	omaq_sound existing[OMAQ_SOUND_MAX];
	char id[OMAQ_SOUND_ID_HEX + 1] = "", audio_name[48] = "", label_name[48] = "";
	char committed_name[48] = "";
	char label[OMAQ_SOUND_LABEL_MAX + 1], metadata[OMAQ_SOUND_LABEL_MAX + 2];
	struct stat before, after;
	int directory = -1, source = -1, audio = -1, name = -1, committed = -1;
	int result = -1;
	int existing_count;

	if (!source_path || source_path[0] != '/' || !out)
		return -1;
	existing_count = omaq_sound_list(home, existing, OMAQ_SOUND_MAX);
	if (existing_count < 0 || existing_count >= OMAQ_SOUND_MAX)
		return -1;
	directory = private_directory(home, 1);
	if (directory < 0)
		return -1;
	source = open(source_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (source_regular(source, &before) != 0 ||
	    !pcm_wav_ok(source, before.st_size))
		goto done;
	source_label(source_path, label, sizeof(label));
	for (int attempt = 0; attempt < 8; attempt++) {
		if (random_id(id) != 0 || entry_names(id, audio_name, sizeof(audio_name),
						 label_name, sizeof(label_name)) != 0 ||
		    commit_name(id, committed_name, sizeof(committed_name)) != 0)
			goto done;
		audio = openat(directory, audio_name,
			       O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (audio >= 0)
			break;
		if (errno != EEXIST)
			goto done;
	}
	if (audio < 0 || copy_source(source, audio, before.st_size) != 0 ||
	    !pcm_wav_ok(audio, before.st_size) ||
	    fstat(source, &after) != 0 || before.st_dev != after.st_dev ||
	    before.st_ino != after.st_ino || before.st_size != after.st_size ||
	    before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
	    before.st_mtim.tv_nsec != after.st_mtim.tv_nsec)
		goto done;
	if (close(audio) != 0)
		goto done;
	audio = -1;
	if (snprintf(metadata, sizeof(metadata), "%s\n", label) >= (int)sizeof(metadata))
		goto done;
	name = openat(directory, label_name,
		      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (name < 0 || write_exact(name, metadata, strlen(metadata)) != 0 ||
	    fsync(name) != 0)
		goto done;
	if (close(name) != 0) {
		name = -1;
		goto done;
	}
	name = -1;
	if (fsync(directory) != 0)
		goto done;
	committed = openat(directory, committed_name,
			   O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (committed < 0 || write_exact(committed, "committed\n", 10) != 0 ||
	    fsync(committed) != 0)
		goto done;
	if (close(committed) != 0) {
		committed = -1;
		goto done;
	}
	committed = -1;
	if (fsync(directory) != 0 || load_entry(directory, home, id, out) != 0)
		goto done;
	result = 0;
done:
	if (committed >= 0)
		close(committed);
	if (name >= 0)
		close(name);
	if (audio >= 0)
		close(audio);
	if (result != 0 && directory >= 0 && id[0]) {
		(void)unlinkat(directory, committed_name, 0);
		(void)unlinkat(directory, label_name, 0);
		(void)unlinkat(directory, audio_name, 0);
		(void)fsync(directory);
	}
	if (source >= 0)
		close(source);
	if (directory >= 0)
		close(directory);
	return result;
}

int omaq_sound_remove(const char *home, const char *id)
{
	omaq_sound entry;
	char audio[48], name[48], committed[48];
	char audio_deleted[64], name_deleted[64], committed_deleted[64];
	int directory = -1, result = -1;
	int audio_missing = 0, name_missing = 0, committed_missing = 0;

	if (entry_names(id, audio, sizeof(audio), name, sizeof(name)) != 0 ||
	    commit_name(id, committed, sizeof(committed)) != 0 ||
	    snprintf(audio_deleted, sizeof(audio_deleted), "%s.delete", audio) >=
		(int)sizeof(audio_deleted) ||
	    snprintf(name_deleted, sizeof(name_deleted), "%s.delete", name) >=
		(int)sizeof(name_deleted) ||
	    snprintf(committed_deleted, sizeof(committed_deleted), "%s.delete",
		     committed) >= (int)sizeof(committed_deleted))
		return -1;
	directory = private_directory(home, 0);
	if (directory < 0 || recover_sound_directory(directory) != 0 ||
	    generated_regular_at(directory, audio, OMAQ_SOUND_FILE_MAX,
				 &audio_missing) != 0 ||
	    generated_regular_at(directory, name, OMAQ_SOUND_LABEL_MAX + 1,
				 &name_missing) != 0 ||
	    generated_regular_at(directory, committed, 32,
				 &committed_missing) != 0)
		goto done;
	if (audio_missing && name_missing && committed_missing) {
		result = 0;
		goto done;
	}
	if (audio_missing || name_missing || committed_missing ||
	    load_entry(directory, home, id, &entry) != 0)
		goto done;
	if (renameat(directory, committed, directory, committed_deleted) != 0)
		goto done;
	if (fsync(directory) != 0) {
		if (renameat(directory, committed_deleted, directory, committed) == 0)
			(void)fsync(directory);
		goto done;
	}
	/* The missing durable commit marker makes removal authoritative. */
	result = 0;
	(void)renameat(directory, name, directory, name_deleted);
	(void)renameat(directory, audio, directory, audio_deleted);
	(void)fsync(directory);
	(void)unlinkat(directory, committed_deleted, 0);
	(void)unlinkat(directory, name_deleted, 0);
	(void)unlinkat(directory, audio_deleted, 0);
	(void)fsync(directory);
done:
	if (directory >= 0)
		close(directory);
	return result;
}
