#define _DEFAULT_SOURCE
#include "group_file.h"
#include "file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const uint8_t magic[] = { 'O', 'Q', 'G', 'F', '1' };
#define COMMON (sizeof(magic) + 1u + OMAQ_GROUP_FILE_ID_BYTES)
#define OFFER_FIXED (COMMON + 8u + 1u + 1u + 32u)
#define DATA_FIXED (COMMON + 8u)

static void put_u64(uint8_t *out, uint64_t value)
{
	for (unsigned int i = 0; i < 8; i++)
		out[i] = (uint8_t)(value >> (56u - i * 8u));
}

static uint64_t get_u64(const uint8_t *in)
{
	uint64_t value = 0;
	for (unsigned int i = 0; i < 8; i++)
		value = (value << 8) | in[i];
	return value;
}

static int common_ok(const uint8_t *packet, size_t length, uint8_t type)
{
	return packet && length >= COMMON && memcmp(packet, magic, sizeof(magic)) == 0 &&
		packet[sizeof(magic)] == type;
}

int omaq_group_file_id_hex(const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES],
			   char out[OMAQ_GROUP_FILE_ID_HEX + 1])
{
	static const char digits[] = "0123456789abcdef";

	if (!id || !out)
		return -1;
	for (size_t i = 0; i < OMAQ_GROUP_FILE_ID_BYTES; i++) {
		out[i * 2] = digits[id[i] >> 4];
		out[i * 2 + 1] = digits[id[i] & 0x0f];
	}
	out[OMAQ_GROUP_FILE_ID_HEX] = '\0';
	return 0;
}

int omaq_group_file_id_parse(const char *id,
			     uint8_t out[OMAQ_GROUP_FILE_ID_BYTES])
{
	const char *hex;

	if (!id || !out || strncmp(id, "gf:", 3) != 0 ||
	    strlen(id + 3) != OMAQ_GROUP_FILE_ID_HEX)
		return -1;
	hex = id + 3;
	for (size_t i = 0; i < OMAQ_GROUP_FILE_ID_BYTES; i++) {
		unsigned int value = 0;
		for (size_t j = 0; j < 2; j++) {
			unsigned char c = (unsigned char)hex[i * 2 + j];
			value <<= 4;
			if (c >= '0' && c <= '9')
				value |= c - '0';
			else if (c >= 'a' && c <= 'f')
				value |= c - 'a' + 10;
			else
				return -1;
		}
		out[i] = (uint8_t)value;
	}
	return 0;
}

int omaq_group_file_offer_pack(uint8_t *out, size_t outn,
			       const omaq_group_file_offer *offer)
{
	size_t name_length, length;
	uint8_t kind;

	if (!out || !offer || offer->size == 0 || offer->size > OMAQ_FILE_MAX)
		return -1;
	name_length = strlen(offer->name);
	if (!omaq_file_name_bytes_ok((const uint8_t *)offer->name, name_length) ||
	    (strcmp(offer->kind, "file") != 0 && strcmp(offer->kind, "image") != 0))
		return -1;
	length = OFFER_FIXED + name_length;
	if (length > outn || length > OMAQ_GROUP_FILE_PACKET_MAX)
		return -1;
	kind = strcmp(offer->kind, "image") == 0 ? 1 : 0;
	memcpy(out, magic, sizeof(magic));
	out[sizeof(magic)] = OMAQ_GROUP_FILE_OFFER;
	memcpy(out + sizeof(magic) + 1, offer->id, sizeof(offer->id));
	put_u64(out + COMMON, offer->size);
	out[COMMON + 8] = kind;
	out[COMMON + 9] = (uint8_t)name_length;
	memcpy(out + COMMON + 10, offer->hash, sizeof(offer->hash));
	memcpy(out + OFFER_FIXED, offer->name, name_length);
	return (int)length;
}

int omaq_group_file_offer_unpack(const uint8_t *packet, size_t length,
				 omaq_group_file_offer *offer)
{
	size_t name_length;

	if (!offer || !common_ok(packet, length, OMAQ_GROUP_FILE_OFFER) ||
	    length < OFFER_FIXED)
		return -1;
	name_length = packet[COMMON + 9];
	if (name_length == 0 || name_length > 128 || length != OFFER_FIXED + name_length ||
	    packet[COMMON + 8] > 1)
		return -1;
	memset(offer, 0, sizeof(*offer));
	memcpy(offer->id, packet + sizeof(magic) + 1, sizeof(offer->id));
	offer->size = get_u64(packet + COMMON);
	if (offer->size == 0 || offer->size > OMAQ_FILE_MAX ||
	    !omaq_file_name_bytes_ok(packet + OFFER_FIXED, name_length))
		return -1;
	memcpy(offer->hash, packet + COMMON + 10, sizeof(offer->hash));
	memcpy(offer->name, packet + OFFER_FIXED, name_length);
	offer->name[name_length] = '\0';
	memcpy(offer->kind, packet[COMMON + 8] ? "image" : "file",
	       packet[COMMON + 8] ? 6 : 5);
	return 0;
}

int omaq_group_file_control_pack(uint8_t *out, size_t outn, uint8_t type,
				 const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	if (!out || !id || outn < COMMON ||
	    (type != OMAQ_GROUP_FILE_ACCEPT && type != OMAQ_GROUP_FILE_CANCEL &&
	     type != OMAQ_GROUP_FILE_DONE && type != OMAQ_GROUP_FILE_ACK &&
	     type != OMAQ_GROUP_FILE_FAIL))
		return -1;
	memcpy(out, magic, sizeof(magic));
	out[sizeof(magic)] = type;
	memcpy(out + sizeof(magic) + 1, id, OMAQ_GROUP_FILE_ID_BYTES);
	return (int)COMMON;
}

int omaq_group_file_control_unpack(const uint8_t *packet, size_t length,
				   uint8_t *type,
				   uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	uint8_t packet_type;

	if (!packet || !type || !id || length != COMMON ||
	    memcmp(packet, magic, sizeof(magic)) != 0)
		return -1;
	packet_type = packet[sizeof(magic)];
	if (packet_type != OMAQ_GROUP_FILE_ACCEPT &&
	    packet_type != OMAQ_GROUP_FILE_CANCEL &&
	    packet_type != OMAQ_GROUP_FILE_DONE &&
	    packet_type != OMAQ_GROUP_FILE_ACK &&
	    packet_type != OMAQ_GROUP_FILE_FAIL)
		return -1;
	*type = packet_type;
	memcpy(id, packet + sizeof(magic) + 1, OMAQ_GROUP_FILE_ID_BYTES);
	return 0;
}

int omaq_group_file_data_pack(uint8_t *out, size_t outn,
			      const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES],
			      uint64_t offset, const uint8_t *data, size_t length)
{
	size_t packet_length = DATA_FIXED + length;

	if (!out || !id || !data || length == 0 || length > OMAQ_GROUP_FILE_DATA_MAX ||
	    packet_length > outn || packet_length > OMAQ_GROUP_FILE_PACKET_MAX)
		return -1;
	memcpy(out, magic, sizeof(magic));
	out[sizeof(magic)] = OMAQ_GROUP_FILE_DATA;
	memcpy(out + sizeof(magic) + 1, id, OMAQ_GROUP_FILE_ID_BYTES);
	put_u64(out + COMMON, offset);
	memcpy(out + DATA_FIXED, data, length);
	return (int)packet_length;
}

int omaq_group_file_data_unpack(const uint8_t *packet, size_t length,
				uint8_t id[OMAQ_GROUP_FILE_ID_BYTES],
				uint64_t *offset, const uint8_t **data,
				size_t *data_length)
{
	if (!id || !offset || !data || !data_length ||
	    !common_ok(packet, length, OMAQ_GROUP_FILE_DATA) ||
	    length <= DATA_FIXED || length > OMAQ_GROUP_FILE_PACKET_MAX ||
	    length - DATA_FIXED > OMAQ_GROUP_FILE_DATA_MAX)
		return -1;
	memcpy(id, packet + sizeof(magic) + 1, OMAQ_GROUP_FILE_ID_BYTES);
	*offset = get_u64(packet + COMMON);
	*data = packet + DATA_FIXED;
	*data_length = length - DATA_FIXED;
	return 0;
}

#define ID_STORE_NAME "group-file-ids.bin"
#define ID_STORE_TEMP ".group-file-ids.tmp"
#define ID_STORE_ENTRIES 65536u
static const uint8_t id_store_header[] = { 'O', 'Q', 'G', 'F', 'I', 'D', 'S', '1' };

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

int omaq_group_file_id_reserve(const char *state,
			       const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	const size_t maximum = sizeof(id_store_header) +
		ID_STORE_ENTRIES * OMAQ_GROUP_FILE_ID_BYTES;
	struct stat directory_status, status;
	uint8_t *contents = NULL;
	size_t length = 0, offset = 0;
	int directory = -1, source = -1, output = -1, result = -1;

	if (!state || !state[0] || !id)
		return -1;
	directory = open(state, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (directory < 0 || fstat(directory, &directory_status) != 0 ||
	    !S_ISDIR(directory_status.st_mode) || directory_status.st_uid != geteuid() ||
	    (directory_status.st_mode & 0077) != 0)
		goto done;
	source = openat(directory, ID_STORE_NAME,
			O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW);
	if (source >= 0) {
		if (fstat(source, &status) != 0 || !S_ISREG(status.st_mode) ||
		    status.st_uid != geteuid() || status.st_nlink != 1 ||
		    (status.st_mode & 0077) != 0 || status.st_size < 0 ||
		    (size_t)status.st_size < sizeof(id_store_header) ||
		    (size_t)status.st_size > maximum ||
		    ((size_t)status.st_size - sizeof(id_store_header)) %
			OMAQ_GROUP_FILE_ID_BYTES != 0)
			goto done;
		length = (size_t)status.st_size;
		contents = malloc(length + OMAQ_GROUP_FILE_ID_BYTES);
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
		if (memcmp(contents, id_store_header, sizeof(id_store_header)) != 0)
			goto done;
		for (offset = sizeof(id_store_header); offset < length;
		     offset += OMAQ_GROUP_FILE_ID_BYTES)
			if (memcmp(contents + offset, id, OMAQ_GROUP_FILE_ID_BYTES) == 0) {
				result = 1;
				goto done;
			}
	} else {
		if (errno != ENOENT)
			goto done;
		length = sizeof(id_store_header);
		contents = malloc(length + OMAQ_GROUP_FILE_ID_BYTES);
		if (!contents)
			goto done;
		memcpy(contents, id_store_header, sizeof(id_store_header));
	}
	if (length > maximum - OMAQ_GROUP_FILE_ID_BYTES)
		goto done;
	memcpy(contents + length, id, OMAQ_GROUP_FILE_ID_BYTES);
	{
		struct stat temporary_status;
		if (fstatat(directory, ID_STORE_TEMP, &temporary_status,
			    AT_SYMLINK_NOFOLLOW) == 0) {
			if (!S_ISREG(temporary_status.st_mode) ||
			    temporary_status.st_uid != geteuid() ||
			    (temporary_status.st_mode & 0077) != 0 ||
			    unlinkat(directory, ID_STORE_TEMP, 0) != 0)
				goto done;
		} else if (errno != ENOENT) {
			goto done;
		}
	}
	output = openat(directory, ID_STORE_TEMP,
			O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (output < 0 || write_all(output, contents,
				    length + OMAQ_GROUP_FILE_ID_BYTES) != 0 ||
	    fsync(output) != 0) {
		if (output >= 0)
			close(output);
		output = -1;
		(void)unlinkat(directory, ID_STORE_TEMP, 0);
		goto done;
	}
	if (close(output) != 0) {
		output = -1;
		(void)unlinkat(directory, ID_STORE_TEMP, 0);
		goto done;
	}
	output = -1;
	if (renameat(directory, ID_STORE_TEMP, directory, ID_STORE_NAME) != 0 ||
	    fsync(directory) != 0) {
		(void)unlinkat(directory, ID_STORE_TEMP, 0);
		goto done;
	}
	result = 0;
done:
	if (source >= 0)
		close(source);
	if (output >= 0)
		close(output);
	if (directory >= 0)
		close(directory);
	free(contents);
	return result;
}
