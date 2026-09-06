#ifndef OMAQ_SEAL_H
#define OMAQ_SEAL_H

#include <stddef.h>

/* Passphrase-derived encryption for local state that the Tox savedata
 * passphrase does not cover: chat history and Ratchet state.
 *
 * A random 32-byte data key is generated once per home and wrapped in
 * "$home/seal.key" with a key derived from the identity passphrase using
 * Argon2id. History records and Ratchet blobs are then sealed with
 * XChaCha20-Poly1305. Nothing is sealed while no passphrase is set, which
 * matches the existing behaviour for unprotected identities.
 *
 * Everything here fails closed: a sealed record can never be read as
 * plaintext, and an authentication failure is an error, never a skip. */

#define OMAQ_SEAL_KEY_BYTES 32
#define OMAQ_SEAL_RECORD_PREFIX "#1:"
#define OMAQ_SEAL_RECORD_PREFIX_LEN 3

/* Session key material. Callers keep one of these; it never reaches disk. */
typedef struct {
	unsigned char key[OMAQ_SEAL_KEY_BYTES];
	int active;
} omaq_seal_key;

/* 1 = library ready, 0 = unavailable. Safe to call repeatedly. */
int omaq_seal_ready(void);

/* Wipe key material. */
void omaq_seal_key_clear(omaq_seal_key *key);

/* Create "$home/seal.key" wrapping a fresh random data key under the
 * passphrase, and return that data key. Refuses to overwrite an existing
 * key file. 0 = ok, -1 = failure. */
int omaq_seal_key_create(const char *home, const char *pass, omaq_seal_key *out);

/* Load and unwrap "$home/seal.key". 1 = loaded, 0 = no key file (nothing is
 * sealed in this home), -1 = present but unusable (wrong passphrase or
 * tampering). */
int omaq_seal_key_open(const char *home, const char *pass, omaq_seal_key *out);

/* Remove "$home/seal.key". 0 = ok (also when already absent). */
int omaq_seal_key_destroy(const char *home);

/* 1 when "$home/seal.key" exists. */
int omaq_seal_key_present(const char *home);

/* Record sealing for line-oriented stores. `context` binds the record to its
 * conversation so a record cannot be replayed into another conversation.
 * omaq_seal_record writes OMAQ_SEAL_RECORD_PREFIX + base64 into `out`. */
int omaq_seal_record(const omaq_seal_key *key, const char *context,
		     const char *plain, char *out, size_t out_size);

/* 1 when the line is a sealed record. */
int omaq_seal_is_record(const char *line);

/* Reverse of omaq_seal_record. 0 = ok, -1 = not authentic or malformed. */
int omaq_seal_open_record(const omaq_seal_key *key, const char *context,
			  const char *line, char *out, size_t out_size);

/* Whole-buffer sealing for blob files (Ratchet state). */
int omaq_seal_blob(const omaq_seal_key *key, const char *context,
		   const unsigned char *plain, size_t plain_len,
		   unsigned char *out, size_t out_size, size_t *out_len);

/* 1 when the buffer carries the blob header. */
int omaq_seal_is_blob(const unsigned char *data, size_t len);

int omaq_seal_open_blob(const omaq_seal_key *key, const char *context,
			const unsigned char *data, size_t len,
			unsigned char *out, size_t out_size, size_t *out_len);

/* Ciphertext growth over plaintext for blobs and records. */
size_t omaq_seal_blob_overhead(void);
size_t omaq_seal_record_overhead(void);

#endif
