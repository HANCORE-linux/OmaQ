#ifdef HAVE_TOX

#define _DEFAULT_SOURCE
#include "tox_adapt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <tox/tox.h>
#include <tox/toxav.h>
#include <tox/toxencryptsave.h>

struct omaq_tox {
	Tox *tox;
	ToxAV *av;
	char home[512];
	omaq_on_request on_req;
	omaq_on_message on_msg;
	omaq_on_group_invite on_ginv;
	omaq_on_group_message on_gmsg;
	omaq_on_group_peer on_gpeer;
	omaq_on_file_recv on_frecv;
	omaq_on_file_chunk_req on_fcreq;
	omaq_on_file_chunk on_fchunk;
	omaq_on_file_ctrl on_fctrl;
	omaq_on_avatar on_avatar;
	omaq_on_presence on_presence;
	omaq_on_typing on_typing;
	omaq_on_call on_call;
	void *ud;
	int online;
	char pass[129];
};

static void wipe_pass(struct omaq_tox *t)
{
	if (!t)
		return;
	explicit_bzero(t->pass, sizeof(t->pass));
}

static int store_pass(struct omaq_tox *t, const char *pass)
{
	size_t n;

	if (!t || !pass)
		return -1;
	n = strlen(pass);
	if (n == 0 || n > 128)
		return -1;
	wipe_pass(t);
	memcpy(t->pass, pass, n + 1);
	return 0;
}

static void hex_of(const uint8_t *in, size_t n, char *out)
{
	static const char *d = "0123456789abcdef";
	for (size_t i = 0; i < n; i++) {
		out[i * 2] = d[in[i] >> 4];
		out[i * 2 + 1] = d[in[i] & 0xf];
	}
	out[n * 2] = '\0';
}

static int hex_in(const char *hex, uint8_t *out, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned int v;
		if (sscanf(hex + i * 2, "%2x", &v) != 1)
			return -1;
		out[i] = (uint8_t)v;
	}
	return 0;
}

static void save_path(const char *home, char *buf, size_t n)
{
	snprintf(buf, n, "%s/tox.save", home);
}

void omaq_tox_save(struct omaq_tox *t)
{
	char path[576], tmp[580];
	FILE *f;
	size_t n, wr, outn;
	uint8_t *plain, *out;
	Tox_Err_Encryption eerr = TOX_ERR_ENCRYPTION_OK;

	if (!t || !t->tox)
		return;
	n = tox_get_savedata_size(t->tox);
	plain = malloc(n);
	if (!plain)
		return;
	tox_get_savedata(t->tox, plain);
	out = plain;
	outn = n;
	if (t->pass[0]) {
		outn = n + tox_pass_encryption_extra_length();
		out = malloc(outn);
		if (!out) {
			explicit_bzero(plain, n);
			free(plain);
			return;
		}
		if (!tox_pass_encrypt(plain, n, (const uint8_t *)t->pass, strlen(t->pass),
				      out, &eerr)) {
			explicit_bzero(plain, n);
			free(plain);
			explicit_bzero(out, outn);
			free(out);
			return;
		}
		explicit_bzero(plain, n);
		free(plain);
		plain = NULL;
	}
	save_path(t->home, path, sizeof(path));
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
		explicit_bzero(out, outn);
		free(out);
		return;
	}
	f = fopen(tmp, "wb");
	if (!f) {
		explicit_bzero(out, outn);
		free(out);
		return;
	}
	wr = fwrite(out, 1, outn, f);
	if (wr != outn || fflush(f) != 0 || fsync(fileno(f)) != 0) {
		fclose(f);
		unlink(tmp);
		explicit_bzero(out, outn);
		free(out);
		return;
	}
	fchmod(fileno(f), 0600);
	fclose(f);
	explicit_bzero(out, outn);
	free(out);
	if (rename(tmp, path) != 0)
		unlink(tmp);
}

static int nickname_ok(const char *name)
{
	size_t i, n;

	if (!name || !name[0])
		return -1;
	n = strlen(name);
	if (n > TOX_MAX_NAME_LENGTH)
		return -1;
	for (i = 0; i < n; i++) {
		unsigned char c = (unsigned char)name[i];
		if (c < 0x20 || c == 0x7f)
			return -1;
	}
	return 0;
}

int omaq_tox_self_name(struct omaq_tox *t, char *out, size_t n)
{
	size_t len;

	if (!t || !t->tox || !out || n == 0)
		return -1;
	len = tox_self_get_name_size(t->tox);
	if (len + 1 > n)
		return -1;
	if (len > 0)
		tox_self_get_name(t->tox, (uint8_t *)out);
	out[len] = '\0';
	return 0;
}

int omaq_tox_set_name(struct omaq_tox *t, const char *name)
{
	Tox_Err_Set_Info error = TOX_ERR_SET_INFO_OK;

	if (!t || !t->tox || nickname_ok(name) != 0)
		return -1;
	if (!tox_self_set_name(t->tox, (const uint8_t *)name, strlen(name), &error))
		return -1;
	omaq_tox_save(t);
	return 0;
}

static void on_status(Tox *tox, Tox_Connection st, void *ud)
{
	struct omaq_tox *t = ud;
	(void)tox;
	t->online = (st != TOX_CONNECTION_NONE);
}

static void on_friend_conn(Tox *tox, uint32_t friend_number, Tox_Connection st, void *ud)
{
	struct omaq_tox *t = ud;

	(void)tox;
	if (t->on_presence)
		t->on_presence(t->ud, friend_number, st != TOX_CONNECTION_NONE);
}

static void on_friend_typing(Tox *tox, uint32_t friend_number, bool typing, void *ud)
{
	struct omaq_tox *t = ud;

	(void)tox;
	if (t->on_typing)
		t->on_typing(t->ud, friend_number, typing ? 1 : 0);
}

static void on_req(Tox *tox, const uint8_t *pk, const uint8_t *msg, size_t len, void *ud)
{
	struct omaq_tox *t = ud;
	char tmp[922];
	(void)tox;
	if (len >= sizeof(tmp))
		len = sizeof(tmp) - 1;
	memcpy(tmp, msg, len);
	tmp[len] = '\0';
	if (t->on_req)
		t->on_req(t->ud, pk, tmp);
}

static void on_msg(Tox *tox, uint32_t friend_number, Tox_Message_Type type,
		   const uint8_t *message, size_t length, void *ud)
{
	struct omaq_tox *t = ud;
	char tmp[1400];
	(void)tox;
	(void)type;
	if (length >= sizeof(tmp))
		length = sizeof(tmp) - 1;
	memcpy(tmp, message, length);
	tmp[length] = '\0';
	if (t->on_msg)
		t->on_msg(t->ud, friend_number, tmp);
}

static void on_ginv(Tox *tox, uint32_t friend_number, const uint8_t *data, size_t len,
		    const uint8_t *gname, size_t glen, void *ud)
{
	struct omaq_tox *t = ud;
	(void)tox;
	(void)gname;
	(void)glen;
	if (t->on_ginv)
		t->on_ginv(t->ud, friend_number, data, len);
}

static void on_gmsg(Tox *tox, uint32_t gnum, uint32_t peer, Tox_Message_Type type,
		    const uint8_t *message, size_t length, uint32_t mid, void *ud)
{
	struct omaq_tox *t = ud;
	char tmp[1400];
	(void)tox;
	(void)type;
	(void)mid;
	if (length >= sizeof(tmp))
		length = sizeof(tmp) - 1;
	memcpy(tmp, message, length);
	tmp[length] = '\0';
	if (t->on_gmsg)
		t->on_gmsg(t->ud, gnum, peer, tmp);
}

static void on_gjoin(Tox *tox, uint32_t gnum, uint32_t peer, void *ud)
{
	struct omaq_tox *t = ud;
	(void)tox;
	if (t->on_gpeer)
		t->on_gpeer(t->ud, gnum, peer, 1);
}

static void on_gexit(Tox *tox, uint32_t gnum, uint32_t peer, Tox_Group_Exit_Type xt,
		     const uint8_t *name, size_t nlen, const uint8_t *part, size_t plen, void *ud)
{
	struct omaq_tox *t = ud;
	(void)tox;
	(void)xt;
	(void)name;
	(void)nlen;
	(void)part;
	(void)plen;
	if (t->on_gpeer)
		t->on_gpeer(t->ud, gnum, peer, 0);
}

static int role_to_tox(int r)
{
	if (r == 2)
		return (int)TOX_GROUP_ROLE_FOUNDER;
	if (r == 1)
		return (int)TOX_GROUP_ROLE_MODERATOR;
	return (int)TOX_GROUP_ROLE_USER;
}

static int role_from_tox(Tox_Group_Role r)
{
	if (r == TOX_GROUP_ROLE_FOUNDER)
		return 2;
	if (r == TOX_GROUP_ROLE_MODERATOR)
		return 1;
	return 0;
}

static void on_file_recv(Tox *tox, uint32_t friend, uint32_t fnum, uint32_t kind, uint64_t size,
			 const uint8_t *filename, size_t flen, void *ud)
{
	struct omaq_tox *t = ud;
	char name[129];

	(void)tox;
	if (kind == TOX_FILE_KIND_AVATAR) {
		if (t->on_avatar)
			t->on_avatar(t->ud, friend, fnum, size);
		else
			tox_file_control(t->tox, friend, fnum, TOX_FILE_CONTROL_CANCEL, NULL);
		return;
	}
	if (kind != TOX_FILE_KIND_DATA) {
		tox_file_control(t->tox, friend, fnum, TOX_FILE_CONTROL_CANCEL, NULL);
		return;
	}
	if (flen >= sizeof(name))
		flen = sizeof(name) - 1;
	memcpy(name, filename, flen);
	name[flen] = '\0';
	if (t->on_frecv)
		t->on_frecv(t->ud, friend, fnum, name, size);
}

static void on_file_chunk_req(Tox *tox, uint32_t friend, uint32_t fnum, uint64_t pos,
			      size_t len, void *ud)
{
	struct omaq_tox *t = ud;

	(void)tox;
	if (t->on_fcreq)
		t->on_fcreq(t->ud, friend, fnum, pos, len);
}

static void on_file_chunk(Tox *tox, uint32_t friend, uint32_t fnum, uint64_t pos,
			  const uint8_t *data, size_t len, void *ud)
{
	struct omaq_tox *t = ud;

	(void)tox;
	if (t->on_fchunk)
		t->on_fchunk(t->ud, friend, fnum, pos, data, len);
}

static void on_file_ctrl(Tox *tox, uint32_t friend, uint32_t fnum, Tox_File_Control control,
			 void *ud)
{
	struct omaq_tox *t = ud;

	(void)tox;
	if (t->on_fctrl)
		t->on_fctrl(t->ud, friend, fnum, (int)control);
}

static void on_av_audio(ToxAV *av, uint32_t friend, const int16_t *pcm, size_t samples,
			uint8_t channels, uint32_t rate, void *ud)
{
	(void)av;
	(void)friend;
	(void)pcm;
	(void)samples;
	(void)channels;
	(void)rate;
	(void)ud;
}

static void on_av_call(ToxAV *av, uint32_t friend, bool audio, bool video, void *ud)
{
	struct omaq_tox *t = ud;

	(void)av;
	(void)audio;
	(void)video;
	if (t->on_call)
		t->on_call(t->ud, friend, 1);
}

static void on_av_state(ToxAV *av, uint32_t friend, uint32_t state, void *ud)
{
	struct omaq_tox *t = ud;

	(void)av;
	if (state == TOXAV_FRIEND_CALL_STATE_FINISHED ||
	    state == TOXAV_FRIEND_CALL_STATE_ERROR) {
		if (t->on_call)
			t->on_call(t->ud, friend, 0);
	}
}

struct omaq_tox *omaq_tox_open(const char *home, const char *pass, int *err_out)
{
	struct omaq_tox *t;
	Tox_Options *opt;
	Tox_Err_New err = TOX_ERR_NEW_OK;
	char path[576];
	uint8_t *saved = NULL, *plain = NULL;
	size_t slen = 0;
	FILE *f;
	struct stat st;
	int encrypted = 0;

	if (err_out)
		*err_out = 0;
	if (!home)
		return NULL;
	t = calloc(1, sizeof(*t));
	if (!t)
		return NULL;
	snprintf(t->home, sizeof(t->home), "%s", home);
	opt = tox_options_new(NULL);
	if (!opt) {
		free(t);
		return NULL;
	}
	tox_options_default(opt);
	tox_options_set_local_discovery_enabled(opt, true);
	save_path(home, path, sizeof(path));
	f = fopen(path, "rb");
	if (f && fstat(fileno(f), &st) == 0 && st.st_size > 0) {
		saved = malloc((size_t)st.st_size);
		if (saved) {
			slen = fread(saved, 1, (size_t)st.st_size, f);
			if (slen >= tox_pass_encryption_extra_length() &&
			    tox_is_data_encrypted(saved)) {
				Tox_Err_Decryption derr = TOX_ERR_DECRYPTION_OK;
				size_t pn;

				encrypted = 1;
				if (!pass || !pass[0]) {
					if (f)
						fclose(f);
					free(saved);
					tox_options_free(opt);
					free(t);
					if (err_out)
						*err_out = OMAQ_TOX_LOCKED;
					return NULL;
				}
				pn = slen - tox_pass_encryption_extra_length();
				plain = malloc(pn);
				if (!plain ||
				    !tox_pass_decrypt(saved, slen, (const uint8_t *)pass,
						      strlen(pass), plain, &derr)) {
					if (f)
						fclose(f);
					explicit_bzero(saved, slen);
					free(saved);
					if (plain) {
						explicit_bzero(plain, pn);
						free(plain);
					}
					tox_options_free(opt);
					free(t);
					if (err_out)
						*err_out = OMAQ_TOX_LOCKED;
					return NULL;
				}
				explicit_bzero(saved, slen);
				free(saved);
				saved = plain;
				plain = NULL;
				slen = pn;
				if (store_pass(t, pass) != 0) {
					if (f)
						fclose(f);
					explicit_bzero(saved, slen);
					free(saved);
					tox_options_free(opt);
					wipe_pass(t);
					free(t);
					return NULL;
				}
			}
			tox_options_set_savedata_type(opt, TOX_SAVEDATA_TYPE_TOX_SAVE);
			tox_options_set_savedata_data(opt, saved, slen);
		}
	}
	if (f)
		fclose(f);
	(void)encrypted;
	t->tox = tox_new(opt, &err);
	tox_options_free(opt);
	if (saved) {
		explicit_bzero(saved, slen);
		free(saved);
	}
	if (!t->tox) {
		wipe_pass(t);
		free(t);
		return NULL;
	}
	tox_callback_self_connection_status(t->tox, on_status);
	tox_callback_friend_connection_status(t->tox, on_friend_conn);
	tox_callback_friend_typing(t->tox, on_friend_typing);
	tox_callback_friend_request(t->tox, on_req);
	tox_callback_friend_message(t->tox, on_msg);
	tox_callback_group_invite(t->tox, on_ginv);
	tox_callback_group_message(t->tox, on_gmsg);
	tox_callback_group_peer_join(t->tox, on_gjoin);
	tox_callback_group_peer_exit(t->tox, on_gexit);
	tox_callback_file_recv(t->tox, on_file_recv);
	tox_callback_file_chunk_request(t->tox, on_file_chunk_req);
	tox_callback_file_recv_chunk(t->tox, on_file_chunk);
	tox_callback_file_recv_control(t->tox, on_file_ctrl);
	{
		Toxav_Err_New aerr = TOXAV_ERR_NEW_OK;
		t->av = toxav_new(t->tox, &aerr);
		if (t->av) {
			toxav_callback_audio_receive_frame(t->av, on_av_audio, t);
			toxav_callback_call(t->av, on_av_call, t);
			toxav_callback_call_state(t->av, on_av_state, t);
		}
	}
	if (tox_self_get_name_size(t->tox) == 0)
		(void)omaq_tox_set_name(t, "omaq");
	{
		/* Live nodes from nodes.tox.chat (status_udp+status_tcp).
		 * TCP ports are outbound-only; no inbound listen required. */
		static const struct {
			const char *host;
			uint16_t udp_port;
			uint16_t tcp_port;
			const char *key_hex;
		} nodes[] = {
			{ "144.217.167.73", 33445, 3389,
			  "7E5668E0EE09E19F320AD47902419331FFEE147BB3606769CFBE921A2A2FD34C" },
			{ "tox1.mf-net.eu", 33445, 33445,
			  "B3E5FA80DC8EBD1149AD2AB35ED8B85BD546DEDE261CA593234C619249419506" },
			{ "139.162.110.188", 33445, 443,
			  "F76A11284547163889DDC89A7738CF271797BF5E5E220643E97AD3C7E7903D55" },
		};
		for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
			uint8_t key[TOX_PUBLIC_KEY_SIZE];
			Tox_Err_Bootstrap berr = TOX_ERR_BOOTSTRAP_OK;
			if (hex_in(nodes[i].key_hex, key, TOX_PUBLIC_KEY_SIZE) != 0)
				continue;
			tox_bootstrap(t->tox, nodes[i].host, nodes[i].udp_port, key, &berr);
			tox_add_tcp_relay(t->tox, nodes[i].host, nodes[i].tcp_port, key, &berr);
		}
	}
	omaq_tox_save(t);
	return t;
}

void omaq_tox_discard(struct omaq_tox *t)
{
	if (!t)
		return;
	if (t->av) {
		toxav_kill(t->av);
		t->av = NULL;
	}
	if (t->tox) {
		tox_kill(t->tox);
		t->tox = NULL;
	}
	wipe_pass(t);
	free(t);
}

void omaq_tox_close(struct omaq_tox *t)
{
	if (!t)
		return;
	omaq_tox_save(t);
	omaq_tox_discard(t);
}

int omaq_tox_protect(struct omaq_tox *t, const char *pass)
{
	if (!t || !t->tox)
		return -1;
	if (store_pass(t, pass) != 0)
		return -1;
	omaq_tox_save(t);
	return 0;
}

int omaq_tox_unprotect(struct omaq_tox *t, const char *pass)
{
	if (!t || !t->tox || !t->pass[0] || !pass)
		return -1;
	if (strcmp(t->pass, pass) != 0)
		return -1;
	wipe_pass(t);
	omaq_tox_save(t);
	return 0;
}

int omaq_tox_protected(const struct omaq_tox *t)
{
	return t && t->pass[0] ? 1 : 0;
}

void omaq_tox_iterate(struct omaq_tox *t)
{
	if (!t || !t->tox)
		return;
	tox_iterate(t->tox, t);
	if (t->av)
		toxav_iterate(t->av);
}

uint32_t omaq_tox_interval_ms(const struct omaq_tox *t)
{
	uint32_t a, b;

	if (!t || !t->tox)
		return 50;
	a = tox_iteration_interval(t->tox);
	b = t->av ? toxav_iteration_interval(t->av) : a;
	return a < b ? a : b;
}

int omaq_tox_self_addr_hex(struct omaq_tox *t, char *hex76)
{
	uint8_t addr[TOX_ADDRESS_SIZE];
	if (!t || !t->tox || !hex76)
		return -1;
	tox_self_get_address(t->tox, addr);
	hex_of(addr, TOX_ADDRESS_SIZE, hex76);
	return 0;
}

int omaq_tox_self_pk_hex(struct omaq_tox *t, char *hex64)
{
	uint8_t pk[TOX_PUBLIC_KEY_SIZE];
	if (!t || !t->tox || !hex64)
		return -1;
	tox_self_get_public_key(t->tox, pk);
	hex_of(pk, TOX_PUBLIC_KEY_SIZE, hex64);
	return 0;
}

int omaq_tox_friend_pk_hex(struct omaq_tox *t, uint32_t friend_number, char *hex64)
{
	uint8_t pk[TOX_PUBLIC_KEY_SIZE];
	Tox_Err_Friend_Get_Public_Key err = TOX_ERR_FRIEND_GET_PUBLIC_KEY_OK;
	if (!t || !t->tox || !hex64)
		return -1;
	if (!tox_friend_get_public_key(t->tox, friend_number, pk, &err))
		return -1;
	hex_of(pk, TOX_PUBLIC_KEY_SIZE, hex64);
	return 0;
}

int omaq_tox_friend_delete(struct omaq_tox *t, uint32_t friend_number)
{
	Tox_Err_Friend_Delete err = TOX_ERR_FRIEND_DELETE_OK;
	if (!t || !t->tox)
		return -1;
	if (!tox_friend_delete(t->tox, friend_number, &err))
		return -1;
	omaq_tox_save(t);
	return 0;
}

int omaq_tox_friend_list(struct omaq_tox *t, uint32_t *out, size_t max)
{
	size_t n;
	uint32_t *all;

	if (!t || !t->tox || !out || max == 0)
		return 0;
	n = tox_self_get_friend_list_size(t->tox);
	if (n == 0)
		return 0;
	all = calloc(n, sizeof(*all));
	if (!all)
		return 0;
	tox_self_get_friend_list(t->tox, all);
	if (n > max)
		n = max;
	memcpy(out, all, n * sizeof(*all));
	free(all);
	return (int)n;
}

int omaq_tox_friend_online(struct omaq_tox *t, uint32_t friend_number)
{
	Tox_Err_Friend_Query err = TOX_ERR_FRIEND_QUERY_OK;
	Tox_Connection st;

	if (!t || !t->tox)
		return 0;
	st = tox_friend_get_connection_status(t->tox, friend_number, &err);
	if (err != TOX_ERR_FRIEND_QUERY_OK)
		return 0;
	return st != TOX_CONNECTION_NONE;
}

int omaq_tox_friend_name(struct omaq_tox *t, uint32_t friend_number, char *out, size_t n)
{
	Tox_Err_Friend_Query err = TOX_ERR_FRIEND_QUERY_OK;
	uint8_t raw[TOX_MAX_NAME_LENGTH];
	size_t sz, i;

	if (!t || !t->tox || !out || n < 2)
		return -1;
	sz = tox_friend_get_name_size(t->tox, friend_number, &err);
	if (err != TOX_ERR_FRIEND_QUERY_OK || sz == 0 ||
	    !tox_friend_get_name(t->tox, friend_number, raw, &err)) {
		if (snprintf(out, n, "Friend %u", friend_number) >= (int)n)
			return -1;
		return 0;
	}
	if (sz >= n)
		sz = n - 1;
	memcpy(out, raw, sz);
	out[sz] = '\0';
	for (i = 0; out[i]; i++) {
		if (out[i] == '\n' || out[i] == '\r')
			out[i] = ' ';
	}
	return 0;
}

uint32_t omaq_tox_friend_by_pk(struct omaq_tox *t, const uint8_t *pk32)
{
	Tox_Err_Friend_By_Public_Key err = TOX_ERR_FRIEND_BY_PUBLIC_KEY_OK;
	uint32_t fn;

	if (!t || !t->tox || !pk32)
		return UINT32_MAX;
	fn = tox_friend_by_public_key(t->tox, pk32, &err);
	if (err != TOX_ERR_FRIEND_BY_PUBLIC_KEY_OK)
		return UINT32_MAX;
	return fn;
}

int omaq_tox_nospam_rotate(struct omaq_tox *t)
{
	uint32_t nospam = 0;
	if (!t || !t->tox)
		return -1;
	if (getentropy(&nospam, sizeof(nospam)) != 0)
		nospam = (uint32_t)getpid() ^ (uint32_t)time(NULL);
	if (nospam == tox_self_get_nospam(t->tox))
		nospam ^= 0x9e3779b9u;
	tox_self_set_nospam(t->tox, nospam);
	omaq_tox_save(t);
	return 0;
}

int omaq_tox_friend_add(struct omaq_tox *t, const char *addr_hex, const char *msg, uint32_t *fn_out)
{
	uint8_t addr[TOX_ADDRESS_SIZE];
	Tox_Err_Friend_Add err = TOX_ERR_FRIEND_ADD_OK;
	uint32_t fn;
	if (!t || !addr_hex || !msg)
		return -1;
	if (strlen(addr_hex) != TOX_ADDRESS_SIZE * 2)
		return -1;
	if (hex_in(addr_hex, addr, TOX_ADDRESS_SIZE) != 0)
		return -1;
	fn = tox_friend_add(t->tox, addr, (const uint8_t *)msg, strlen(msg), &err);
	if (err != TOX_ERR_FRIEND_ADD_OK)
		return -1;
	omaq_tox_save(t);
	if (fn_out)
		*fn_out = fn;
	return 0;
}

int omaq_tox_friend_accept(struct omaq_tox *t, const uint8_t *pk32)
{
	Tox_Err_Friend_Add err = TOX_ERR_FRIEND_ADD_OK;
	if (!t || !pk32)
		return -1;
	tox_friend_add_norequest(t->tox, pk32, &err);
	omaq_tox_save(t);
	return err == TOX_ERR_FRIEND_ADD_OK ? 0 : -1;
}

int omaq_tox_send(struct omaq_tox *t, uint32_t friend_number, const char *text)
{
	Tox_Err_Friend_Send_Message err = TOX_ERR_FRIEND_SEND_MESSAGE_OK;
	if (!t || !text)
		return -1;
	tox_friend_send_message(t->tox, friend_number, TOX_MESSAGE_TYPE_NORMAL,
				(const uint8_t *)text, strlen(text), &err);
	return err == TOX_ERR_FRIEND_SEND_MESSAGE_OK ? 0 : -1;
}

void omaq_tox_set_hooks(struct omaq_tox *t, omaq_on_request req, omaq_on_message msg, void *ud)
{
	if (!t)
		return;
	t->on_req = req;
	t->on_msg = msg;
	t->ud = ud;
}

void omaq_tox_set_presence_hook(struct omaq_tox *t, omaq_on_presence cb, void *ud)
{
	if (!t)
		return;
	t->on_presence = cb;
	if (ud)
		t->ud = ud;
}

void omaq_tox_set_typing_hook(struct omaq_tox *t, omaq_on_typing cb, void *ud)
{
	if (!t)
		return;
	t->on_typing = cb;
	if (ud)
		t->ud = ud;
}

int omaq_tox_set_typing(struct omaq_tox *t, uint32_t friend_number, int typing)
{
	Tox_Err_Set_Typing err = TOX_ERR_SET_TYPING_OK;

	if (!t || !t->tox || !tox_self_set_typing(t->tox, friend_number, typing != 0, &err))
		return -1;
	return err == TOX_ERR_SET_TYPING_OK ? 0 : -1;
}

int omaq_tox_online(const struct omaq_tox *t)
{
	return t ? t->online : 0;
}

void omaq_tox_set_group_hooks(struct omaq_tox *t, omaq_on_group_invite inv,
			      omaq_on_group_message msg, omaq_on_group_peer peer, void *ud)
{
	if (!t)
		return;
	t->on_ginv = inv;
	t->on_gmsg = msg;
	t->on_gpeer = peer;
	if (ud)
		t->ud = ud;
}

int omaq_tox_group_new(struct omaq_tox *t, const char *title, uint32_t *gnum)
{
	Tox_Err_Group_New err = TOX_ERR_GROUP_NEW_OK;
	uint32_t n;
	static const uint8_t nick[] = "omaq";

	if (!t || !t->tox || !title || !gnum)
		return -1;
	n = tox_group_new(t->tox, TOX_GROUP_PRIVACY_STATE_PRIVATE,
			  (const uint8_t *)title, strlen(title), nick, 4, &err);
	if (err != TOX_ERR_GROUP_NEW_OK || n == UINT32_MAX)
		return -1;
	*gnum = n;
	omaq_tox_save(t);
	return 0;
}

int omaq_tox_group_invite_friend(struct omaq_tox *t, uint32_t gnum, uint32_t friend)
{
	int i;
	if (!t || !t->tox)
		return -1;
	for (i = 0; i < 25; i++) {
		Tox_Err_Group_Invite_Friend err = TOX_ERR_GROUP_INVITE_FRIEND_OK;
		if (tox_group_invite_friend(t->tox, gnum, friend, &err))
			return 0;
		if (err != TOX_ERR_GROUP_INVITE_FRIEND_DISCONNECTED &&
		    err != TOX_ERR_GROUP_INVITE_FRIEND_FAIL_SEND &&
		    err != TOX_ERR_GROUP_INVITE_FRIEND_INVITE_FAIL)
			return -1;
		tox_iterate(t->tox, t);
		usleep(40000);
	}
	return -1;
}

int omaq_tox_group_invite_accept(struct omaq_tox *t, uint32_t friend,
				 const uint8_t *data, size_t len, uint32_t *gnum)
{
	Tox_Err_Group_Invite_Accept err = TOX_ERR_GROUP_INVITE_ACCEPT_OK;
	uint32_t n;
	static const uint8_t nick[] = "omaq";

	if (!t || !t->tox || !data || !gnum)
		return -1;
	n = tox_group_invite_accept(t->tox, friend, data, len, nick, 4, NULL, 0, &err);
	if (err != TOX_ERR_GROUP_INVITE_ACCEPT_OK || n == UINT32_MAX)
		return -1;
	*gnum = n;
	omaq_tox_save(t);
	return 0;
}

int omaq_tox_group_set_role(struct omaq_tox *t, uint32_t gnum, uint32_t peer, int omaq_role)
{
	Tox_Err_Group_Set_Role err = TOX_ERR_GROUP_SET_ROLE_OK;
	if (!t || !t->tox)
		return -1;
	if (!tox_group_set_role(t->tox, gnum, peer, (Tox_Group_Role)role_to_tox(omaq_role), &err))
		return -1;
	return 0;
}

int omaq_tox_group_kick(struct omaq_tox *t, uint32_t gnum, uint32_t peer)
{
	Tox_Err_Group_Kick_Peer err = TOX_ERR_GROUP_KICK_PEER_OK;
	if (!t || !t->tox)
		return -1;
	if (!tox_group_kick_peer(t->tox, gnum, peer, &err))
		return -1;
	return 0;
}

int omaq_tox_group_leave(struct omaq_tox *t, uint32_t gnum)
{
	Tox_Err_Group_Leave err = TOX_ERR_GROUP_LEAVE_OK;
	if (!t || !t->tox)
		return -1;
	if (!tox_group_leave(t->tox, gnum, NULL, 0, &err))
		return -1;
	omaq_tox_save(t);
	return 0;
}

int omaq_tox_group_send(struct omaq_tox *t, uint32_t gnum, const char *text)
{
	Tox_Err_Group_Send_Message err = TOX_ERR_GROUP_SEND_MESSAGE_OK;
	if (!t || !t->tox || !text || !text[0])
		return -1;
	tox_group_send_message(t->tox, gnum, TOX_MESSAGE_TYPE_NORMAL,
			       (const uint8_t *)text, strlen(text), &err);
	return err == TOX_ERR_GROUP_SEND_MESSAGE_OK ? 0 : -1;
}

int omaq_tox_group_self_role(struct omaq_tox *t, uint32_t gnum, int *omaq_role)
{
	Tox_Err_Group_Self_Query err = TOX_ERR_GROUP_SELF_QUERY_OK;
	Tox_Group_Role r;
	if (!t || !t->tox || !omaq_role)
		return -1;
	r = tox_group_self_get_role(t->tox, gnum, &err);
	if (err != TOX_ERR_GROUP_SELF_QUERY_OK)
		return -1;
	*omaq_role = role_from_tox(r);
	return 0;
}

int omaq_tox_group_peer_role(struct omaq_tox *t, uint32_t gnum, uint32_t peer, int *omaq_role)
{
	Tox_Err_Group_Peer_Query err = TOX_ERR_GROUP_PEER_QUERY_OK;
	Tox_Group_Role r;
	if (!t || !t->tox || !omaq_role)
		return -1;
	r = tox_group_peer_get_role(t->tox, gnum, peer, &err);
	if (err != TOX_ERR_GROUP_PEER_QUERY_OK)
		return -1;
	*omaq_role = role_from_tox(r);
	return 0;
}

int omaq_tox_group_self_peer(struct omaq_tox *t, uint32_t gnum, uint32_t *peer)
{
	Tox_Err_Group_Self_Query err = TOX_ERR_GROUP_SELF_QUERY_OK;
	uint32_t p;
	if (!t || !t->tox || !peer)
		return -1;
	p = tox_group_self_get_peer_id(t->tox, gnum, &err);
	if (err != TOX_ERR_GROUP_SELF_QUERY_OK)
		return -1;
	*peer = p;
	return 0;
}

int omaq_tox_file_send(struct omaq_tox *t, uint32_t friend, uint64_t size,
		       const char *name, uint32_t *fnum)
{
	Tox_Err_File_Send err = TOX_ERR_FILE_SEND_OK;
	uint32_t n;

	if (!t || !t->tox || !name || !fnum || !name[0])
		return -1;
	n = tox_file_send(t->tox, friend, TOX_FILE_KIND_DATA, size, NULL,
			  (const uint8_t *)name, strlen(name), &err);
	if (err != TOX_ERR_FILE_SEND_OK)
		return -1;
	*fnum = n;
	return 0;
}

int omaq_tox_hash(const uint8_t *data, size_t n, uint8_t out32[32])
{
	if (!data || !out32)
		return -1;
	tox_hash(out32, data, n);
	return 0;
}

int omaq_tox_file_send_avatar(struct omaq_tox *t, uint32_t friend, uint64_t size,
			      const uint8_t file_id[32], uint32_t *fnum)
{
	Tox_Err_File_Send err = TOX_ERR_FILE_SEND_OK;
	uint32_t n;
	static const uint8_t name[] = "avatar.png";

	if (!t || !t->tox || !fnum)
		return -1;
	n = tox_file_send(t->tox, friend, TOX_FILE_KIND_AVATAR, size, file_id,
			  name, sizeof(name) - 1, &err);
	if (err != TOX_ERR_FILE_SEND_OK)
		return -1;
	*fnum = n;
	return 0;
}

int omaq_tox_file_chunk(struct omaq_tox *t, uint32_t friend, uint32_t fnum,
			uint64_t pos, const uint8_t *data, size_t len)
{
	Tox_Err_File_Send_Chunk err = TOX_ERR_FILE_SEND_CHUNK_OK;

	if (!t || !t->tox)
		return -1;
	if (len && !data)
		return -1;
	if (!tox_file_send_chunk(t->tox, friend, fnum, pos, data, len, &err))
		return -1;
	return 0;
}

int omaq_tox_file_control(struct omaq_tox *t, uint32_t friend, uint32_t fnum, int control)
{
	Tox_Err_File_Control err = TOX_ERR_FILE_CONTROL_OK;
	Tox_File_Control c;

	if (!t || !t->tox)
		return -1;
	if (control == OMAQ_TOX_FILE_PAUSE)
		c = TOX_FILE_CONTROL_PAUSE;
	else if (control == OMAQ_TOX_FILE_CANCEL)
		c = TOX_FILE_CONTROL_CANCEL;
	else
		c = TOX_FILE_CONTROL_RESUME;
	if (!tox_file_control(t->tox, friend, fnum, c, &err))
		return -1;
	return 0;
}

void omaq_tox_set_file_hooks(struct omaq_tox *t, omaq_on_file_recv recv,
			     omaq_on_file_chunk_req req, omaq_on_file_chunk chunk,
			     omaq_on_file_ctrl ctrl, void *ud)
{
	if (!t)
		return;
	t->on_frecv = recv;
	t->on_fcreq = req;
	t->on_fchunk = chunk;
	t->on_fctrl = ctrl;
	if (ud)
		t->ud = ud;
}

void omaq_tox_set_avatar_hook(struct omaq_tox *t, omaq_on_avatar cb, void *ud)
{
	if (!t)
		return;
	t->on_avatar = cb;
	if (ud)
		t->ud = ud;
}

int omaq_tox_av_call(struct omaq_tox *t, uint32_t friend)
{
	Toxav_Err_Call err = TOXAV_ERR_CALL_OK;

	if (!t || !t->av)
		return -1;
	if (!toxav_call(t->av, friend, 48, 0, &err))
		return -1;
	return 0;
}

int omaq_tox_av_answer(struct omaq_tox *t, uint32_t friend)
{
	Toxav_Err_Answer err = TOXAV_ERR_ANSWER_OK;

	if (!t || !t->av)
		return -1;
	if (!toxav_answer(t->av, friend, 48, 0, &err))
		return -1;
	return 0;
}

int omaq_tox_av_hangup(struct omaq_tox *t, uint32_t friend)
{
	Toxav_Err_Call_Control err = TOXAV_ERR_CALL_CONTROL_OK;

	if (!t || !t->av)
		return -1;
	if (!toxav_call_control(t->av, friend, TOXAV_CALL_CONTROL_CANCEL, &err))
		return -1;
	return 0;
}

void omaq_tox_set_call_hook(struct omaq_tox *t, omaq_on_call cb, void *ud)
{
	if (!t)
		return;
	t->on_call = cb;
	if (ud)
		t->ud = ud;
}

#endif /* HAVE_TOX */
