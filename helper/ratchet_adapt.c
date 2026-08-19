#ifdef HAVE_SIGNAL

#define _DEFAULT_SOURCE
#include "ratchet.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <signal/curve.h>
#include <signal/key_helper.h>
#include <signal/protocol.h>
#include <signal/session_builder.h>
#include <signal/session_cipher.h>
#include <signal/signal_protocol.h>

#define SLOTS 16
#define PREKEYS 8

struct rec {
	int used;
	char name[32];
	uint32_t id;
	signal_buffer *buf;
};

struct omaq_ratchet {
	signal_context *ctx;
	signal_protocol_store_context *store;
	signal_buffer *id_pub;
	signal_buffer *id_priv;
	uint32_t reg_id;
	uint32_t spk_id;
	struct rec sess[SLOTS];
	struct rec pre[PREKEYS];
	struct rec spk[4];
	struct rec ident[SLOTS];
};

static int hex_in(const char *hex, uint8_t *out, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		unsigned int v;
		if (sscanf(hex + i * 2, "%2x", &v) != 1)
			return -1;
		out[i] = (uint8_t)v;
	}
	return 0;
}

static void hex_of(const uint8_t *in, size_t n, char *out)
{
	static const char *d = "0123456789abcdef";
	size_t i;
	for (i = 0; i < n; i++) {
		out[i * 2] = d[in[i] >> 4];
		out[i * 2 + 1] = d[in[i] & 0xf];
	}
	out[n * 2] = '\0';
}

static int rnd(uint8_t *data, size_t len, void *ud)
{
	(void)ud;
	return RAND_bytes(data, (int)len) == 1 ? 0 : -1;
}

static int hmac_init(void **hctx, const uint8_t *key, size_t key_len, void *ud)
{
	EVP_MAC *mac;
	EVP_MAC_CTX *c;
	OSSL_PARAM params[2];
	char digest[] = "SHA256";
	(void)ud;
	mac = EVP_MAC_fetch(NULL, "HMAC", NULL);
	if (!mac)
		return -1;
	c = EVP_MAC_CTX_new(mac);
	EVP_MAC_free(mac);
	if (!c)
		return SG_ERR_NOMEM;
	params[0] = OSSL_PARAM_construct_utf8_string(OSSL_MAC_PARAM_DIGEST, digest, 0);
	params[1] = OSSL_PARAM_construct_end();
	if (EVP_MAC_init(c, key, key_len, params) != 1) {
		EVP_MAC_CTX_free(c);
		return -1;
	}
	*hctx = c;
	return 0;
}

static int hmac_upd(void *hctx, const uint8_t *data, size_t n, void *ud)
{
	(void)ud;
	return EVP_MAC_update(hctx, data, n) == 1 ? 0 : -1;
}

static int hmac_fin(void *hctx, signal_buffer **out, void *ud)
{
	unsigned char md[64];
	size_t len = 0;
	(void)ud;
	if (EVP_MAC_final(hctx, md, &len, sizeof(md)) != 1)
		return -1;
	*out = signal_buffer_create(md, len);
	return *out ? 0 : SG_ERR_NOMEM;
}

static void hmac_clean(void *hctx, void *ud)
{
	(void)ud;
	EVP_MAC_CTX_free(hctx);
}

static int sha_init(void **dctx, void *ud)
{
	EVP_MD_CTX *c = EVP_MD_CTX_new();
	(void)ud;
	if (!c)
		return SG_ERR_NOMEM;
	if (EVP_DigestInit_ex(c, EVP_sha512(), NULL) != 1) {
		EVP_MD_CTX_free(c);
		return -1;
	}
	*dctx = c;
	return 0;
}

static int sha_upd(void *dctx, const uint8_t *data, size_t n, void *ud)
{
	(void)ud;
	return EVP_DigestUpdate(dctx, data, n) == 1 ? 0 : -1;
}

static int sha_fin(void *dctx, signal_buffer **out, void *ud)
{
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int len = 0;
	(void)ud;
	if (EVP_DigestFinal_ex(dctx, md, &len) != 1)
		return -1;
	*out = signal_buffer_create(md, len);
	return *out ? 0 : SG_ERR_NOMEM;
}

static void sha_clean(void *dctx, void *ud)
{
	(void)ud;
	EVP_MD_CTX_free(dctx);
}

static const EVP_CIPHER *aes_of(int cipher, size_t key_len)
{
	if (cipher == SG_CIPHER_AES_CBC_PKCS5) {
		if (key_len == 16)
			return EVP_aes_128_cbc();
		if (key_len == 32)
			return EVP_aes_256_cbc();
	} else if (cipher == SG_CIPHER_AES_CTR_NOPADDING) {
		if (key_len == 16)
			return EVP_aes_128_ctr();
		if (key_len == 32)
			return EVP_aes_256_ctr();
	}
	return NULL;
}

static int aes_crypt(int enc, signal_buffer **output, int cipher,
		     const uint8_t *key, size_t key_len,
		     const uint8_t *iv, size_t iv_len,
		     const uint8_t *in, size_t in_len)
{
	EVP_CIPHER_CTX *c;
	const EVP_CIPHER *evp;
	uint8_t *buf;
	int outl = 0, fin = 0;

	evp = aes_of(cipher, key_len);
	if (!evp || iv_len != 16)
		return SG_ERR_INVAL;
	c = EVP_CIPHER_CTX_new();
	if (!c)
		return SG_ERR_NOMEM;
	if (EVP_CipherInit_ex(c, evp, NULL, key, iv, enc) != 1) {
		EVP_CIPHER_CTX_free(c);
		return -1;
	}
	if (cipher == SG_CIPHER_AES_CTR_NOPADDING)
		EVP_CIPHER_CTX_set_padding(c, 0);
	buf = malloc(in_len + 32);
	if (!buf) {
		EVP_CIPHER_CTX_free(c);
		return SG_ERR_NOMEM;
	}
	if (EVP_CipherUpdate(c, buf, &outl, in, (int)in_len) != 1 ||
	    EVP_CipherFinal_ex(c, buf + outl, &fin) != 1) {
		free(buf);
		EVP_CIPHER_CTX_free(c);
		return -1;
	}
	*output = signal_buffer_create(buf, (size_t)(outl + fin));
	free(buf);
	EVP_CIPHER_CTX_free(c);
	return *output ? 0 : SG_ERR_NOMEM;
}

static int enc_fn(signal_buffer **o, int cipher, const uint8_t *k, size_t kn,
		  const uint8_t *iv, size_t ivn, const uint8_t *p, size_t pn, void *ud)
{
	(void)ud;
	return aes_crypt(1, o, cipher, k, kn, iv, ivn, p, pn);
}

static int dec_fn(signal_buffer **o, int cipher, const uint8_t *k, size_t kn,
		  const uint8_t *iv, size_t ivn, const uint8_t *p, size_t pn, void *ud)
{
	(void)ud;
	return aes_crypt(0, o, cipher, k, kn, iv, ivn, p, pn);
}

static struct rec *rec_find(struct rec *a, int n, const char *name, uint32_t id, int create)
{
	int i, free_i = -1;
	for (i = 0; i < n; i++) {
		if (!a[i].used) {
			if (free_i < 0)
				free_i = i;
			continue;
		}
		if (a[i].id == id && (!name || strcmp(a[i].name, name) == 0))
			return &a[i];
	}
	if (!create || free_i < 0)
		return NULL;
	memset(&a[free_i], 0, sizeof(a[free_i]));
	a[free_i].used = 1;
	a[free_i].id = id;
	if (name)
		snprintf(a[free_i].name, sizeof(a[free_i].name), "%s", name);
	return &a[free_i];
}

static int sess_load(signal_buffer **record, signal_buffer **user_record,
		     const signal_protocol_address *address, void *ud)
{
	struct omaq_ratchet *r = ud;
	char name[32];
	struct rec *s;
	(void)user_record;
	if (address->name_len >= sizeof(name))
		return 0;
	memcpy(name, address->name, address->name_len);
	name[address->name_len] = '\0';
	s = rec_find(r->sess, SLOTS, name, (uint32_t)address->device_id, 0);
	if (!s)
		return 0;
	*record = signal_buffer_copy(s->buf);
	return *record ? 1 : SG_ERR_NOMEM;
}

static int sess_sub(signal_int_list **sessions, const char *name, size_t name_len, void *ud)
{
	struct omaq_ratchet *r = ud;
	signal_int_list *list = signal_int_list_alloc();
	int i;
	(void)name;
	(void)name_len;
	if (!list)
		return SG_ERR_NOMEM;
	for (i = 0; i < SLOTS; i++) {
		if (r->sess[i].used)
			signal_int_list_push_back(list, (int)r->sess[i].id);
	}
	*sessions = list;
	return 0;
}

static int sess_store(const signal_protocol_address *address, uint8_t *record, size_t record_len,
		      uint8_t *user_record, size_t user_len, void *ud)
{
	struct omaq_ratchet *r = ud;
	char name[32];
	struct rec *s;
	(void)user_record;
	(void)user_len;
	if (address->name_len >= sizeof(name))
		return -1;
	memcpy(name, address->name, address->name_len);
	name[address->name_len] = '\0';
	s = rec_find(r->sess, SLOTS, name, (uint32_t)address->device_id, 1);
	if (!s)
		return SG_ERR_NOMEM;
	if (s->buf)
		signal_buffer_free(s->buf);
	s->buf = signal_buffer_create(record, record_len);
	return s->buf ? 0 : SG_ERR_NOMEM;
}

static int sess_has(const signal_protocol_address *address, void *ud)
{
	struct omaq_ratchet *r = ud;
	char name[32];
	if (address->name_len >= sizeof(name))
		return 0;
	memcpy(name, address->name, address->name_len);
	name[address->name_len] = '\0';
	return rec_find(r->sess, SLOTS, name, (uint32_t)address->device_id, 0) ? 1 : 0;
}

static int sess_del(const signal_protocol_address *address, void *ud)
{
	struct omaq_ratchet *r = ud;
	char name[32];
	struct rec *s;
	if (address->name_len >= sizeof(name))
		return 0;
	memcpy(name, address->name, address->name_len);
	name[address->name_len] = '\0';
	s = rec_find(r->sess, SLOTS, name, (uint32_t)address->device_id, 0);
	if (!s)
		return 0;
	if (s->buf)
		signal_buffer_free(s->buf);
	memset(s, 0, sizeof(*s));
	return 1;
}

static int sess_del_all(const char *name, size_t name_len, void *ud)
{
	(void)name;
	(void)name_len;
	(void)ud;
	return 0;
}

static void sess_destroy(void *ud)
{
	(void)ud;
}

static int pre_load(signal_buffer **record, uint32_t id, void *ud)
{
	struct omaq_ratchet *r = ud;
	struct rec *s = rec_find(r->pre, PREKEYS, NULL, id, 0);
	if (!s)
		return SG_ERR_INVALID_KEY_ID;
	*record = signal_buffer_copy(s->buf);
	return *record ? SG_SUCCESS : SG_ERR_NOMEM;
}

static int pre_store(uint32_t id, uint8_t *record, size_t len, void *ud)
{
	struct omaq_ratchet *r = ud;
	struct rec *s = rec_find(r->pre, PREKEYS, NULL, id, 1);
	if (!s)
		return SG_ERR_NOMEM;
	if (s->buf)
		signal_buffer_free(s->buf);
	s->buf = signal_buffer_create(record, len);
	return s->buf ? 0 : SG_ERR_NOMEM;
}

static int pre_has(uint32_t id, void *ud)
{
	struct omaq_ratchet *r = ud;
	return rec_find(r->pre, PREKEYS, NULL, id, 0) ? 1 : 0;
}

static int pre_rm(uint32_t id, void *ud)
{
	struct omaq_ratchet *r = ud;
	struct rec *s = rec_find(r->pre, PREKEYS, NULL, id, 0);
	if (s) {
		if (s->buf)
			signal_buffer_free(s->buf);
		memset(s, 0, sizeof(*s));
	}
	return 0;
}

static void pre_destroy(void *ud)
{
	(void)ud;
}

static int spk_load(signal_buffer **record, uint32_t id, void *ud)
{
	struct omaq_ratchet *r = ud;
	struct rec *s = rec_find(r->spk, 4, NULL, id, 0);
	if (!s)
		return SG_ERR_INVALID_KEY_ID;
	*record = signal_buffer_copy(s->buf);
	return *record ? SG_SUCCESS : SG_ERR_NOMEM;
}

static int spk_store(uint32_t id, uint8_t *record, size_t len, void *ud)
{
	struct omaq_ratchet *r = ud;
	struct rec *s = rec_find(r->spk, 4, NULL, id, 1);
	if (!s)
		return SG_ERR_NOMEM;
	if (s->buf)
		signal_buffer_free(s->buf);
	s->buf = signal_buffer_create(record, len);
	return s->buf ? 0 : SG_ERR_NOMEM;
}

static int spk_has(uint32_t id, void *ud)
{
	struct omaq_ratchet *r = ud;
	return rec_find(r->spk, 4, NULL, id, 0) ? 1 : 0;
}

static int spk_rm(uint32_t id, void *ud)
{
	(void)id;
	(void)ud;
	return 0;
}

static void spk_destroy(void *ud)
{
	(void)ud;
}

static int id_pair(signal_buffer **pub, signal_buffer **priv, void *ud)
{
	struct omaq_ratchet *r = ud;
	*pub = signal_buffer_copy(r->id_pub);
	*priv = signal_buffer_copy(r->id_priv);
	return (*pub && *priv) ? 0 : SG_ERR_NOMEM;
}

static int id_reg(void *ud, uint32_t *reg)
{
	struct omaq_ratchet *r = ud;
	*reg = r->reg_id;
	return 0;
}

static int id_save(const signal_protocol_address *address, uint8_t *key, size_t len, void *ud)
{
	struct omaq_ratchet *r = ud;
	char name[32];
	struct rec *s;
	if (address->name_len >= sizeof(name))
		return -1;
	memcpy(name, address->name, address->name_len);
	name[address->name_len] = '\0';
	s = rec_find(r->ident, SLOTS, name, 0, 1);
	if (!s)
		return SG_ERR_NOMEM;
	if (s->buf)
		signal_buffer_free(s->buf);
	s->buf = (key && len) ? signal_buffer_create(key, len) : NULL;
	return 0;
}

static int id_trust(const signal_protocol_address *address, uint8_t *key, size_t len, void *ud)
{
	struct omaq_ratchet *r = ud;
	char name[32];
	struct rec *s;
	if (address->name_len >= sizeof(name))
		return 0;
	memcpy(name, address->name, address->name_len);
	name[address->name_len] = '\0';
	s = rec_find(r->ident, SLOTS, name, 0, 0);
	if (!s || !s->buf)
		return 1;
	if (signal_buffer_len(s->buf) != len)
		return 0;
	return memcmp(signal_buffer_data(s->buf), key, len) == 0 ? 1 : 0;
}

static void id_destroy(void *ud)
{
	(void)ud;
}

static int sk_store(const signal_protocol_sender_key_name *n, uint8_t *rec, size_t len,
		    uint8_t *ur, size_t ul, void *ud)
{
	(void)n;
	(void)rec;
	(void)len;
	(void)ur;
	(void)ul;
	(void)ud;
	return 0;
}

static int sk_load(signal_buffer **record, signal_buffer **user_record,
		   const signal_protocol_sender_key_name *n, void *ud)
{
	(void)record;
	(void)user_record;
	(void)n;
	(void)ud;
	return 0;
}

static void sk_destroy(void *ud)
{
	(void)ud;
}

static void addr_of(signal_protocol_address *a, const char *peer)
{
	a->name = peer;
	a->name_len = strlen(peer);
	a->device_id = 1;
}

static int setup_keys(struct omaq_ratchet *r)
{
	ratchet_identity_key_pair *idp = NULL;
	session_signed_pre_key *spk = NULL;
	session_pre_key *pk = NULL;
	signal_protocol_key_helper_pre_key_list_node *head = NULL, *n;
	signal_buffer *ser = NULL;
	ec_public_key *pub;
	ec_private_key *priv;

	if (signal_protocol_key_helper_generate_identity_key_pair(&idp, r->ctx) != 0)
		return -1;
	pub = ratchet_identity_key_pair_get_public(idp);
	priv = ratchet_identity_key_pair_get_private(idp);
	if (ec_public_key_serialize(&r->id_pub, pub) != 0 ||
	    ec_private_key_serialize(&r->id_priv, priv) != 0) {
		SIGNAL_UNREF(idp);
		return -1;
	}
	r->reg_id = 7;
	r->spk_id = 1;
	if (signal_protocol_key_helper_generate_signed_pre_key(&spk, idp, r->spk_id, 1, r->ctx) != 0) {
		SIGNAL_UNREF(idp);
		return -1;
	}
	if (session_signed_pre_key_serialize(&ser, spk) != 0 ||
	    signal_protocol_signed_pre_key_store_key(r->store, spk) != 0) {
		SIGNAL_UNREF(spk);
		SIGNAL_UNREF(idp);
		return -1;
	}
	signal_buffer_free(ser);
	SIGNAL_UNREF(spk);
	if (signal_protocol_key_helper_generate_pre_keys(&head, 1, 4, r->ctx) != 0) {
		SIGNAL_UNREF(idp);
		return -1;
	}
	for (n = head; n; n = signal_protocol_key_helper_key_list_next(n)) {
		pk = signal_protocol_key_helper_key_list_element(n);
		if (signal_protocol_pre_key_store_key(r->store, pk) != 0) {
			signal_protocol_key_helper_key_list_free(head);
			SIGNAL_UNREF(idp);
			return -1;
		}
	}
	signal_protocol_key_helper_key_list_free(head);
	SIGNAL_UNREF(idp);
	return 0;
}

struct omaq_ratchet *omaq_ratchet_open(const char *home)
{
	struct omaq_ratchet *r;
	signal_crypto_provider prov = {
		.random_func = rnd,
		.hmac_sha256_init_func = hmac_init,
		.hmac_sha256_update_func = hmac_upd,
		.hmac_sha256_final_func = hmac_fin,
		.hmac_sha256_cleanup_func = hmac_clean,
		.sha512_digest_init_func = sha_init,
		.sha512_digest_update_func = sha_upd,
		.sha512_digest_final_func = sha_fin,
		.sha512_digest_cleanup_func = sha_clean,
		.encrypt_func = enc_fn,
		.decrypt_func = dec_fn,
		.user_data = NULL
	};
	signal_protocol_session_store ss = {
		.load_session_func = sess_load,
		.get_sub_device_sessions_func = sess_sub,
		.store_session_func = sess_store,
		.contains_session_func = sess_has,
		.delete_session_func = sess_del,
		.delete_all_sessions_func = sess_del_all,
		.destroy_func = sess_destroy,
		.user_data = NULL
	};
	signal_protocol_pre_key_store ps = {
		.load_pre_key = pre_load,
		.store_pre_key = pre_store,
		.contains_pre_key = pre_has,
		.remove_pre_key = pre_rm,
		.destroy_func = pre_destroy,
		.user_data = NULL
	};
	signal_protocol_signed_pre_key_store sps = {
		.load_signed_pre_key = spk_load,
		.store_signed_pre_key = spk_store,
		.contains_signed_pre_key = spk_has,
		.remove_signed_pre_key = spk_rm,
		.destroy_func = spk_destroy,
		.user_data = NULL
	};
	signal_protocol_identity_key_store ids = {
		.get_identity_key_pair = id_pair,
		.get_local_registration_id = id_reg,
		.save_identity = id_save,
		.is_trusted_identity = id_trust,
		.destroy_func = id_destroy,
		.user_data = NULL
	};
	signal_protocol_sender_key_store sks = {
		.store_sender_key = sk_store,
		.load_sender_key = sk_load,
		.destroy_func = sk_destroy,
		.user_data = NULL
	};

	(void)home;
	r = calloc(1, sizeof(*r));
	if (!r)
		return NULL;
	if (signal_context_create(&r->ctx, r) != 0) {
		free(r);
		return NULL;
	}
	if (signal_context_set_crypto_provider(r->ctx, &prov) != 0) {
		signal_context_destroy(r->ctx);
		free(r);
		return NULL;
	}
	ss.user_data = r;
	ps.user_data = r;
	sps.user_data = r;
	ids.user_data = r;
	sks.user_data = r;
	if (signal_protocol_store_context_create(&r->store, r->ctx) != 0 ||
	    signal_protocol_store_context_set_session_store(r->store, &ss) != 0 ||
	    signal_protocol_store_context_set_pre_key_store(r->store, &ps) != 0 ||
	    signal_protocol_store_context_set_signed_pre_key_store(r->store, &sps) != 0 ||
	    signal_protocol_store_context_set_identity_key_store(r->store, &ids) != 0 ||
	    signal_protocol_store_context_set_sender_key_store(r->store, &sks) != 0 ||
	    setup_keys(r) != 0) {
		omaq_ratchet_close(r);
		return NULL;
	}
	return r;
}

void omaq_ratchet_close(struct omaq_ratchet *r)
{
	int i;
	if (!r)
		return;
	for (i = 0; i < SLOTS; i++) {
		if (r->sess[i].buf)
			signal_buffer_free(r->sess[i].buf);
		if (r->ident[i].buf)
			signal_buffer_free(r->ident[i].buf);
	}
	for (i = 0; i < PREKEYS; i++) {
		if (r->pre[i].buf)
			signal_buffer_free(r->pre[i].buf);
	}
	for (i = 0; i < 4; i++) {
		if (r->spk[i].buf)
			signal_buffer_free(r->spk[i].buf);
	}
	if (r->id_pub)
		signal_buffer_bzero_free(r->id_pub);
	if (r->id_priv)
		signal_buffer_bzero_free(r->id_priv);
	if (r->store)
		signal_protocol_store_context_destroy(r->store);
	if (r->ctx)
		signal_context_destroy(r->ctx);
	free(r);
}

int omaq_ratchet_local_rk(struct omaq_ratchet *r, char hex64[OMAQ_RK_HEX + 1])
{
	const uint8_t *p;
	size_t n;
	if (!r || !r->id_pub || !hex64)
		return -1;
	p = signal_buffer_const_data(r->id_pub);
	n = signal_buffer_len(r->id_pub);
	/* serialized DJB public is 33 bytes (type + 32). rk is the 32-byte key. */
	if (n == 33)
		hex_of(p + 1, 32, hex64);
	else if (n == 32)
		hex_of(p, 32, hex64);
	else
		return -1;
	return 0;
}

int omaq_ratchet_bundle(struct omaq_ratchet *r, char *out, size_t n)
{
	session_signed_pre_key *spk = NULL;
	session_pre_key *pk = NULL;
	ec_public_key *spub, *ppub;
	signal_buffer *sbuf = NULL, *pbuf = NULL;
	uint8_t raw[8 + 33 + 33 + 64 + 33];
	size_t off = 0;
	uint32_t pkid = 1;

	if (!r || !out || n < 400)
		return -1;
	if (signal_protocol_signed_pre_key_load_key(r->store, &spk, r->spk_id) != 0)
		return -1;
	if (signal_protocol_pre_key_load_key(r->store, &pk, pkid) != 0) {
		SIGNAL_UNREF(spk);
		return -1;
	}
	spub = ec_key_pair_get_public(session_signed_pre_key_get_key_pair(spk));
	ppub = ec_key_pair_get_public(session_pre_key_get_key_pair(pk));
	if (ec_public_key_serialize(&sbuf, spub) != 0 ||
	    ec_public_key_serialize(&pbuf, ppub) != 0) {
		SIGNAL_UNREF(pk);
		SIGNAL_UNREF(spk);
		return -1;
	}
	memcpy(raw + off, &r->reg_id, 4);
	off += 4;
	memcpy(raw + off, &r->spk_id, 4);
	off += 4;
	memcpy(raw + off, &pkid, 4);
	off += 4;
	memcpy(raw + off, signal_buffer_data(r->id_pub), signal_buffer_len(r->id_pub));
	off += signal_buffer_len(r->id_pub);
	memcpy(raw + off, signal_buffer_data(sbuf), signal_buffer_len(sbuf));
	off += signal_buffer_len(sbuf);
	memcpy(raw + off, session_signed_pre_key_get_signature(spk), 64);
	off += 64;
	memcpy(raw + off, signal_buffer_data(pbuf), signal_buffer_len(pbuf));
	off += signal_buffer_len(pbuf);
	if (off * 2 + 1 > n) {
		signal_buffer_free(sbuf);
		signal_buffer_free(pbuf);
		SIGNAL_UNREF(pk);
		SIGNAL_UNREF(spk);
		return -1;
	}
	hex_of(raw, off, out);
	signal_buffer_free(sbuf);
	signal_buffer_free(pbuf);
	SIGNAL_UNREF(pk);
	SIGNAL_UNREF(spk);
	return 0;
}

int omaq_ratchet_accept_bundle(struct omaq_ratchet *r, const char *peer,
			       const char *hex, const char *expect_rk)
{
	uint8_t raw[256];
	size_t n, off = 0, idlen, splen, plen;
	uint32_t reg = 0, spkid = 0, pkid = 0;
	ec_public_key *idk = NULL, *spk = NULL, *pk = NULL;
	session_pre_key_bundle *bundle = NULL;
	session_builder *b = NULL;
	signal_protocol_address addr;
	const uint8_t *sig;
	char got_rk[OMAQ_RK_HEX + 1];

	if (!r || !peer || !hex || strlen(hex) < 80)
		return -1;
	n = strlen(hex);
	if (n % 2 != 0 || n / 2 > sizeof(raw))
		return -1;
	if (hex_in(hex, raw, n / 2) != 0)
		return -1;
	n = n / 2;
	if (n < 4 + 4 + 4 + 33 + 33 + 64 + 33)
		return -1;
	memcpy(&reg, raw + off, 4);
	off += 4;
	memcpy(&spkid, raw + off, 4);
	off += 4;
	memcpy(&pkid, raw + off, 4);
	off += 4;
	idlen = 33;
	if (curve_decode_point(&idk, raw + off, idlen, r->ctx) != 0)
		return -1;
	hex_of(raw + off + 1, 32, got_rk);
	off += idlen;
	if (expect_rk && expect_rk[0] && strcmp(got_rk, expect_rk) != 0) {
		SIGNAL_UNREF(idk);
		return -1;
	}
	splen = 33;
	if (curve_decode_point(&spk, raw + off, splen, r->ctx) != 0) {
		SIGNAL_UNREF(idk);
		return -1;
	}
	off += splen;
	sig = raw + off;
	off += 64;
	plen = 33;
	if (curve_decode_point(&pk, raw + off, plen, r->ctx) != 0) {
		SIGNAL_UNREF(spk);
		SIGNAL_UNREF(idk);
		return -1;
	}
	if (session_pre_key_bundle_create(&bundle, reg, 1, pkid, pk, spkid, spk, sig, 64, idk) != 0) {
		SIGNAL_UNREF(pk);
		SIGNAL_UNREF(spk);
		SIGNAL_UNREF(idk);
		return -1;
	}
	addr_of(&addr, peer);
	if (session_builder_create(&b, r->store, &addr, r->ctx) != 0) {
		SIGNAL_UNREF(bundle);
		SIGNAL_UNREF(pk);
		SIGNAL_UNREF(spk);
		SIGNAL_UNREF(idk);
		return -1;
	}
	if (session_builder_process_pre_key_bundle(b, bundle) != 0) {
		session_builder_free(b);
		SIGNAL_UNREF(bundle);
		SIGNAL_UNREF(pk);
		SIGNAL_UNREF(spk);
		SIGNAL_UNREF(idk);
		return -1;
	}
	session_builder_free(b);
	SIGNAL_UNREF(bundle);
	SIGNAL_UNREF(pk);
	SIGNAL_UNREF(spk);
	SIGNAL_UNREF(idk);
	return 0;
}

int omaq_ratchet_has_session(struct omaq_ratchet *r, const char *peer)
{
	signal_protocol_address addr;
	if (!r || !peer)
		return 0;
	addr_of(&addr, peer);
	return signal_protocol_session_contains_session(r->store, &addr) == 1;
}

int omaq_ratchet_encrypt(struct omaq_ratchet *r, const char *peer,
			 const char *plain, char *out, size_t n)
{
	session_cipher *c = NULL;
	ciphertext_message *msg = NULL;
	signal_buffer *ser;
	signal_protocol_address addr;
	int typ;
	size_t sl;
	char *p;

	if (!r || !peer || !plain || !out)
		return -1;
	addr_of(&addr, peer);
	if (session_cipher_create(&c, r->store, &addr, r->ctx) != 0)
		return -1;
	if (session_cipher_encrypt(c, (const uint8_t *)plain, strlen(plain), &msg) != 0) {
		session_cipher_free(c);
		return -1;
	}
	typ = ciphertext_message_get_type(msg);
	ser = ciphertext_message_get_serialized(msg);
	sl = signal_buffer_len(ser);
	if (5 + 2 + sl * 2 + 1 > n) {
		SIGNAL_UNREF(msg);
		session_cipher_free(c);
		return -1;
	}
	memcpy(out, "OQR1", 4);
	p = out + 4;
	hex_of((uint8_t[]){ (uint8_t)typ }, 1, p);
	hex_of(signal_buffer_data(ser), sl, p + 2);
	SIGNAL_UNREF(msg);
	session_cipher_free(c);
	return 0;
}

int omaq_ratchet_decrypt(struct omaq_ratchet *r, const char *peer,
			 const char *wire, char *out, size_t n)
{
	session_cipher *c = NULL;
	signal_protocol_address addr;
	uint8_t typ, *raw;
	size_t hexn, rawn;
	signal_buffer *plain = NULL;
	int rc = -1;

	if (!r || !peer || !wire || !out || strncmp(wire, "OQR1", 4) != 0)
		return -1;
	hexn = strlen(wire + 4);
	if (hexn < 4 || hexn % 2 != 0)
		return -1;
	if (hex_in(wire + 4, &typ, 1) != 0)
		return -1;
	rawn = (hexn - 2) / 2;
	raw = malloc(rawn);
	if (!raw)
		return -1;
	if (hex_in(wire + 6, raw, rawn) != 0) {
		free(raw);
		return -1;
	}
	addr_of(&addr, peer);
	if (session_cipher_create(&c, r->store, &addr, r->ctx) != 0) {
		free(raw);
		return -1;
	}
	if (typ == CIPHERTEXT_PREKEY_TYPE) {
		pre_key_signal_message *m = NULL;
		if (pre_key_signal_message_deserialize(&m, raw, rawn, r->ctx) == 0 &&
		    session_cipher_decrypt_pre_key_signal_message(c, m, NULL, &plain) == 0)
			rc = 0;
		if (m)
			SIGNAL_UNREF(m);
	} else {
		signal_message *m = NULL;
		if (signal_message_deserialize(&m, raw, rawn, r->ctx) == 0 &&
		    session_cipher_decrypt_signal_message(c, m, NULL, &plain) == 0)
			rc = 0;
		if (m)
			SIGNAL_UNREF(m);
	}
	free(raw);
	session_cipher_free(c);
	if (rc != 0 || !plain)
		return -1;
	if (signal_buffer_len(plain) + 1 > n) {
		signal_buffer_bzero_free(plain);
		return -1;
	}
	memcpy(out, signal_buffer_data(plain), signal_buffer_len(plain));
	out[signal_buffer_len(plain)] = '\0';
	signal_buffer_bzero_free(plain);
	return 0;
}

#endif /* HAVE_SIGNAL */
