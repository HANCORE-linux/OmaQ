#define _DEFAULT_SOURCE
#include "seal.h"

#include <errno.h>
#include <fcntl.h>
#include <sodium.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SEAL_NONCE crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
#define SEAL_TAG crypto_aead_xchacha20poly1305_ietf_ABYTES
#define SEAL_SALT crypto_pwhash_SALTBYTES
#define SEAL_BLOB_MAGIC "OMAQSEAL1"
#define SEAL_BLOB_MAGIC_LEN 9
#define SEAL_KEY_MAGIC "OMAQSEALKEY1"
#define SEAL_KEY_MAGIC_LEN 12
#define SEAL_KEY_AAD "omaq-seal-key-v1"
/* Derivation cost: one Argon2id run per unlock. Interactive memory keeps the
 * helper usable on small machines; the operation limit is raised to
 * compensate. The parameters are stored in the key file so they can change
 * without breaking existing installations. */
#define SEAL_OPSLIMIT 4u
#define SEAL_MEMLIMIT (64u * 1024u * 1024u)

int omaq_seal_ready(void)
{
	static int state;

	if (state == 0)
		state = sodium_init() < 0 ? -1 : 1;
	return state == 1;
}

void omaq_seal_key_clear(omaq_seal_key *key)
{
	if (!key)
		return;
	sodium_memzero(key->key, sizeof(key->key));
	key->active = 0;
}

size_t omaq_seal_blob_overhead(void)
{
	return SEAL_BLOB_MAGIC_LEN + SEAL_NONCE + SEAL_TAG;
}

size_t omaq_seal_record_overhead(void)
{
	/* base64 of nonce+tag plus the record prefix, rounded generously. */
	return OMAQ_SEAL_RECORD_PREFIX_LEN +
	       sodium_base64_ENCODED_LEN(SEAL_NONCE + SEAL_TAG,
					 sodium_base64_VARIANT_URLSAFE_NO_PADDING);
}

static int seal_key_path(const char *home, char *out, size_t n)
{
	if (!home || !out)
		return -1;
	if (snprintf(out, n, "%s/seal.key", home) >= (int)n)
		return -1;
	return 0;
}

static int derive_wrapping_key(const char *pass, const unsigned char *salt,
			       unsigned long long ops, size_t mem,
			       unsigned char out[OMAQ_SEAL_KEY_BYTES])
{
	if (!pass || !pass[0])
		return -1;
	if (crypto_pwhash(out, OMAQ_SEAL_KEY_BYTES, pass, strlen(pass), salt,
			  ops, mem, crypto_pwhash_ALG_ARGON2ID13) != 0)
		return -1;
	return 0;
}

/* Key file layout:
 *   magic(12) ops(8, big endian) mem(8, big endian) salt(16) nonce(24)
 *   AEAD(data key)(32+16) */
#define SEAL_KEY_FILE_BYTES \
	(SEAL_KEY_MAGIC_LEN + 8 + 8 + SEAL_SALT + SEAL_NONCE + \
	 OMAQ_SEAL_KEY_BYTES + SEAL_TAG)

static void put_be64(unsigned char *out, unsigned long long value)
{
	int i;

	for (i = 0; i < 8; i++)
		out[i] = (unsigned char)(value >> (56 - i * 8));
}

static unsigned long long get_be64(const unsigned char *in)
{
	unsigned long long value = 0;
	int i;

	for (i = 0; i < 8; i++)
		value = (value << 8) | in[i];
	return value;
}

static int write_private_file(const char *path, const unsigned char *data,
			      size_t len)
{
	char tmp[512];
	int fd, dir_fd;
	char dir[512];
	char *slash;
	ssize_t wrote;

	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return -1;
	(void)unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	if (fchmod(fd, 0600) != 0) {
		close(fd);
		(void)unlink(tmp);
		return -1;
	}
	wrote = write(fd, data, len);
	if (wrote < 0 || (size_t)wrote != len || fsync(fd) != 0 ||
	    close(fd) != 0) {
		(void)unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) != 0) {
		(void)unlink(tmp);
		return -1;
	}
	if (snprintf(dir, sizeof(dir), "%s", path) >= (int)sizeof(dir))
		return -1;
	slash = strrchr(dir, '/');
	if (slash) {
		*slash = '\0';
		dir_fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
		if (dir_fd >= 0) {
			(void)fsync(dir_fd);
			close(dir_fd);
		}
	}
	return 0;
}

static int read_private_file(const char *path, unsigned char *out, size_t want)
{
	struct stat st;
	int fd;
	ssize_t got;

	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_uid != geteuid() || st.st_nlink != 1 ||
	    (st.st_mode & 0077) != 0 || (size_t)st.st_size != want) {
		close(fd);
		return -1;
	}
	got = read(fd, out, want);
	close(fd);
	if (got < 0 || (size_t)got != want)
		return -1;
	return 1;
}

int omaq_seal_key_present(const char *home)
{
	char path[512];
	struct stat st;

	if (seal_key_path(home, path, sizeof(path)) != 0)
		return 0;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int omaq_seal_key_create(const char *home, const char *pass, omaq_seal_key *out)
{
	unsigned char file[SEAL_KEY_FILE_BYTES];
	unsigned char wrapping[OMAQ_SEAL_KEY_BYTES];
	unsigned char data_key[OMAQ_SEAL_KEY_BYTES];
	unsigned char *salt, *nonce, *body;
	unsigned long long body_len = 0;
	char path[512];
	int rc = -1;

	if (!omaq_seal_ready() || !out || !pass || !pass[0])
		return -1;
	if (seal_key_path(home, path, sizeof(path)) != 0)
		return -1;
	if (omaq_seal_key_present(home))
		return -1;
	memset(file, 0, sizeof(file));
	memcpy(file, SEAL_KEY_MAGIC, SEAL_KEY_MAGIC_LEN);
	put_be64(file + SEAL_KEY_MAGIC_LEN, SEAL_OPSLIMIT);
	put_be64(file + SEAL_KEY_MAGIC_LEN + 8, SEAL_MEMLIMIT);
	salt = file + SEAL_KEY_MAGIC_LEN + 16;
	nonce = salt + SEAL_SALT;
	body = nonce + SEAL_NONCE;
	randombytes_buf(salt, SEAL_SALT);
	randombytes_buf(nonce, SEAL_NONCE);
	randombytes_buf(data_key, sizeof(data_key));
	if (derive_wrapping_key(pass, salt, SEAL_OPSLIMIT, SEAL_MEMLIMIT,
				wrapping) != 0)
		goto done;
	if (crypto_aead_xchacha20poly1305_ietf_encrypt(
		    body, &body_len, data_key, sizeof(data_key),
		    (const unsigned char *)SEAL_KEY_AAD, strlen(SEAL_KEY_AAD),
		    NULL, nonce, wrapping) != 0)
		goto done;
	if (body_len != OMAQ_SEAL_KEY_BYTES + SEAL_TAG)
		goto done;
	if (write_private_file(path, file, sizeof(file)) != 0)
		goto done;
	memcpy(out->key, data_key, sizeof(data_key));
	out->active = 1;
	rc = 0;
done:
	sodium_memzero(wrapping, sizeof(wrapping));
	sodium_memzero(data_key, sizeof(data_key));
	sodium_memzero(file, sizeof(file));
	return rc;
}

int omaq_seal_key_open(const char *home, const char *pass, omaq_seal_key *out)
{
	unsigned char file[SEAL_KEY_FILE_BYTES];
	unsigned char wrapping[OMAQ_SEAL_KEY_BYTES];
	unsigned char data_key[OMAQ_SEAL_KEY_BYTES];
	const unsigned char *salt, *nonce, *body;
	unsigned long long plain_len = 0;
	unsigned long long ops;
	unsigned long long mem;
	char path[512];
	int present, rc = -1;

	if (!omaq_seal_ready() || !out)
		return -1;
	if (seal_key_path(home, path, sizeof(path)) != 0)
		return -1;
	present = read_private_file(path, file, sizeof(file));
	if (present <= 0)
		return present;
	if (!pass || !pass[0])
		goto done;
	if (sodium_memcmp(file, SEAL_KEY_MAGIC, SEAL_KEY_MAGIC_LEN) != 0)
		goto done;
	ops = get_be64(file + SEAL_KEY_MAGIC_LEN);
	mem = get_be64(file + SEAL_KEY_MAGIC_LEN + 8);
	/* Bound the stored cost so a tampered key file cannot turn an unlock
	 * into a denial of service. */
	if (ops == 0 || ops > 16 || mem < 8u * 1024u * 1024u ||
	    mem > 512u * 1024u * 1024u)
		goto done;
	salt = file + SEAL_KEY_MAGIC_LEN + 16;
	nonce = salt + SEAL_SALT;
	body = nonce + SEAL_NONCE;
	if (derive_wrapping_key(pass, salt, ops, (size_t)mem, wrapping) != 0)
		goto done;
	if (crypto_aead_xchacha20poly1305_ietf_decrypt(
		    data_key, &plain_len, NULL, body,
		    OMAQ_SEAL_KEY_BYTES + SEAL_TAG,
		    (const unsigned char *)SEAL_KEY_AAD, strlen(SEAL_KEY_AAD),
		    nonce, wrapping) != 0)
		goto done;
	if (plain_len != OMAQ_SEAL_KEY_BYTES)
		goto done;
	memcpy(out->key, data_key, sizeof(data_key));
	out->active = 1;
	rc = 1;
done:
	sodium_memzero(wrapping, sizeof(wrapping));
	sodium_memzero(data_key, sizeof(data_key));
	sodium_memzero(file, sizeof(file));
	return rc;
}

int omaq_seal_key_destroy(const char *home)
{
	char path[512];

	if (seal_key_path(home, path, sizeof(path)) != 0)
		return -1;
	if (unlink(path) != 0 && errno != ENOENT)
		return -1;
	return 0;
}

int omaq_seal_is_record(const char *line)
{
	return line && strncmp(line, OMAQ_SEAL_RECORD_PREFIX,
			       OMAQ_SEAL_RECORD_PREFIX_LEN) == 0;
}

int omaq_seal_record(const omaq_seal_key *key, const char *context,
		     const char *plain, char *out, size_t out_size)
{
	unsigned char *buffer;
	unsigned long long cipher_len = 0;
	size_t plain_len, buffer_len, encoded_len;
	int rc = -1;

	if (!omaq_seal_ready() || !key || !key->active || !context || !plain ||
	    !out)
		return -1;
	plain_len = strlen(plain);
	buffer_len = SEAL_NONCE + plain_len + SEAL_TAG;
	encoded_len = sodium_base64_ENCODED_LEN(
		buffer_len, sodium_base64_VARIANT_URLSAFE_NO_PADDING);
	if (out_size <= OMAQ_SEAL_RECORD_PREFIX_LEN + encoded_len)
		return -1;
	buffer = malloc(buffer_len);
	if (!buffer)
		return -1;
	randombytes_buf(buffer, SEAL_NONCE);
	if (crypto_aead_xchacha20poly1305_ietf_encrypt(
		    buffer + SEAL_NONCE, &cipher_len,
		    (const unsigned char *)plain, plain_len,
		    (const unsigned char *)context, strlen(context), NULL,
		    buffer, key->key) != 0)
		goto done;
	if (cipher_len != plain_len + SEAL_TAG)
		goto done;
	memcpy(out, OMAQ_SEAL_RECORD_PREFIX, OMAQ_SEAL_RECORD_PREFIX_LEN);
	if (!sodium_bin2base64(out + OMAQ_SEAL_RECORD_PREFIX_LEN,
			       out_size - OMAQ_SEAL_RECORD_PREFIX_LEN, buffer,
			       SEAL_NONCE + (size_t)cipher_len,
			       sodium_base64_VARIANT_URLSAFE_NO_PADDING))
		goto done;
	rc = 0;
done:
	sodium_memzero(buffer, buffer_len);
	free(buffer);
	return rc;
}

int omaq_seal_open_record(const omaq_seal_key *key, const char *context,
			  const char *line, char *out, size_t out_size)
{
	unsigned char *buffer;
	const char *encoded;
	size_t encoded_len, buffer_cap, buffer_len = 0;
	unsigned long long plain_len = 0;
	int rc = -1;

	if (!omaq_seal_ready() || !key || !key->active || !context ||
	    !omaq_seal_is_record(line) || !out)
		return -1;
	encoded = line + OMAQ_SEAL_RECORD_PREFIX_LEN;
	encoded_len = strlen(encoded);
	buffer_cap = encoded_len / 4 * 3 + 4;
	if (buffer_cap < SEAL_NONCE + SEAL_TAG)
		return -1;
	buffer = malloc(buffer_cap);
	if (!buffer)
		return -1;
	if (sodium_base642bin(buffer, buffer_cap, encoded, encoded_len, NULL,
			      &buffer_len, NULL,
			      sodium_base64_VARIANT_URLSAFE_NO_PADDING) != 0)
		goto done;
	if (buffer_len < SEAL_NONCE + SEAL_TAG)
		goto done;
	if (out_size <= buffer_len - SEAL_NONCE - SEAL_TAG)
		goto done;
	if (crypto_aead_xchacha20poly1305_ietf_decrypt(
		    (unsigned char *)out, &plain_len, NULL, buffer + SEAL_NONCE,
		    buffer_len - SEAL_NONCE, (const unsigned char *)context,
		    strlen(context), buffer, key->key) != 0)
		goto done;
	out[plain_len] = '\0';
	/* A record must not smuggle a newline or NUL back into a line store. */
	if (strlen(out) != plain_len || memchr(out, '\n', (size_t)plain_len))
		goto done;
	rc = 0;
done:
	sodium_memzero(buffer, buffer_cap);
	free(buffer);
	return rc;
}

int omaq_seal_is_blob(const unsigned char *data, size_t len)
{
	return data && len >= SEAL_BLOB_MAGIC_LEN &&
	       memcmp(data, SEAL_BLOB_MAGIC, SEAL_BLOB_MAGIC_LEN) == 0;
}

int omaq_seal_blob(const omaq_seal_key *key, const char *context,
		   const unsigned char *plain, size_t plain_len,
		   unsigned char *out, size_t out_size, size_t *out_len)
{
	unsigned long long cipher_len = 0;

	if (!omaq_seal_ready() || !key || !key->active || !context || !out ||
	    !out_len || (!plain && plain_len))
		return -1;
	if (out_size < SEAL_BLOB_MAGIC_LEN + SEAL_NONCE + plain_len + SEAL_TAG)
		return -1;
	memcpy(out, SEAL_BLOB_MAGIC, SEAL_BLOB_MAGIC_LEN);
	randombytes_buf(out + SEAL_BLOB_MAGIC_LEN, SEAL_NONCE);
	if (crypto_aead_xchacha20poly1305_ietf_encrypt(
		    out + SEAL_BLOB_MAGIC_LEN + SEAL_NONCE, &cipher_len, plain,
		    plain_len, (const unsigned char *)context, strlen(context),
		    NULL, out + SEAL_BLOB_MAGIC_LEN, key->key) != 0)
		return -1;
	*out_len = SEAL_BLOB_MAGIC_LEN + SEAL_NONCE + (size_t)cipher_len;
	return 0;
}

int omaq_seal_open_blob(const omaq_seal_key *key, const char *context,
			const unsigned char *data, size_t len,
			unsigned char *out, size_t out_size, size_t *out_len)
{
	unsigned long long plain_len = 0;
	size_t body;

	if (!omaq_seal_ready() || !key || !key->active || !context || !out ||
	    !out_len || !omaq_seal_is_blob(data, len))
		return -1;
	if (len < SEAL_BLOB_MAGIC_LEN + SEAL_NONCE + SEAL_TAG)
		return -1;
	body = len - SEAL_BLOB_MAGIC_LEN - SEAL_NONCE;
	if (out_size < body - SEAL_TAG)
		return -1;
	if (crypto_aead_xchacha20poly1305_ietf_decrypt(
		    out, &plain_len, NULL,
		    data + SEAL_BLOB_MAGIC_LEN + SEAL_NONCE, body,
		    (const unsigned char *)context, strlen(context),
		    data + SEAL_BLOB_MAGIC_LEN, key->key) != 0)
		return -1;
	*out_len = (size_t)plain_len;
	return 0;
}
