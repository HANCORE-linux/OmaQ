#ifndef OMAQ_RATCHET_H
#define OMAQ_RATCHET_H

#include <stddef.h>

#include "seal.h"

#define OMAQ_RK_HEX 64
#define OMAQ_RATCHET_PEER_MAX 67
#define OMAQ_RATCHET_RECORD_MAX (1024u * 1024u)
#define OMAQ_RATCHET_DECRYPT_RECOVER (-2)

/* Install or clear the passphrase-derived Ratchet-state sealing key. */
void omaq_ratchet_set_seal_key(const omaq_seal_key *key);
void omaq_ratchet_set_seal_writes(int enabled);
int omaq_ratchet_seal_active(void);

/* Rewrite every blob under $home/ratchet so it matches the current sealing
 * state. Each file is replaced atomically. Returns files rewritten, or -1. */
long omaq_ratchet_reseal_all(const char *home);

/* 1 when $home/ratchet holds sealed blobs the installed key cannot open, so
 * the store must be quarantined before it is opened. */
int omaq_ratchet_sealed_locked(const char *home);

int omaq_rk_ok(const char *hex64);
int omaq_ratchet_peer_ok(const char *peer);

#ifdef HAVE_SIGNAL
struct omaq_ratchet;

struct omaq_ratchet *omaq_ratchet_open(const char *home); /* persists under $home/ratchet */
void omaq_ratchet_close(struct omaq_ratchet *r);
int omaq_ratchet_local_rk(struct omaq_ratchet *r, char hex64[OMAQ_RK_HEX + 1]);
int omaq_ratchet_bundle(struct omaq_ratchet *r, const char *peer,
			char *out, size_t n);
int omaq_ratchet_request_bundle(struct omaq_ratchet *r, const char *peer,
				char *out, size_t n);
int omaq_ratchet_response_bundle(struct omaq_ratchet *r, const char *peer,
				 const char *request_bundle, char *out, size_t n);
int omaq_ratchet_accept_bundle(struct omaq_ratchet *r, const char *peer,
			       const char *hex, const char *expect_rk);
int omaq_ratchet_has_session(struct omaq_ratchet *r, const char *peer);
void omaq_ratchet_release_peer_cache(struct omaq_ratchet *r, const char *peer);
int omaq_ratchet_reset_session(struct omaq_ratchet *r, const char *peer);
int omaq_ratchet_encrypt(struct omaq_ratchet *r, const char *peer,
			 const char *plain, char *out, size_t n);
int omaq_ratchet_decrypt(struct omaq_ratchet *r, const char *peer,
			 const char *wire, char *out, size_t n,
			 size_t *out_length);
#endif

#endif
