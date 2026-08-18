#ifdef HAVE_TOX

#define _DEFAULT_SOURCE
#include "tox_adapt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <tox/tox.h>

struct omaq_tox {
	Tox *tox;
	char home[512];
	omaq_on_request on_req;
	omaq_on_message on_msg;
	void *ud;
	int online;
};

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
	size_t n, wr;
	uint8_t *buf;

	if (!t || !t->tox)
		return;
	n = tox_get_savedata_size(t->tox);
	buf = malloc(n);
	if (!buf)
		return;
	tox_get_savedata(t->tox, buf);
	save_path(t->home, path, sizeof(path));
	if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
		free(buf);
		return;
	}
	f = fopen(tmp, "w");
	if (!f) {
		free(buf);
		return;
	}
	wr = fwrite(buf, 1, n, f);
	if (wr != n || fflush(f) != 0 || fsync(fileno(f)) != 0) {
		fclose(f);
		unlink(tmp);
		free(buf);
		return;
	}
	fchmod(fileno(f), 0600);
	fclose(f);
	free(buf);
	if (rename(tmp, path) != 0)
		unlink(tmp);
}

static void on_status(Tox *tox, Tox_Connection st, void *ud)
{
	struct omaq_tox *t = ud;
	(void)tox;
	t->online = (st != TOX_CONNECTION_NONE);
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

struct omaq_tox *omaq_tox_open(const char *home)
{
	struct omaq_tox *t;
	Tox_Options *opt;
	Tox_Err_New err = TOX_ERR_NEW_OK;
	char path[576];
	uint8_t *saved = NULL;
	size_t slen = 0;
	FILE *f;
	struct stat st;

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
	f = fopen(path, "r");
	if (f && fstat(fileno(f), &st) == 0 && st.st_size > 0) {
		saved = malloc((size_t)st.st_size);
		if (saved) {
			slen = fread(saved, 1, (size_t)st.st_size, f);
			tox_options_set_savedata_type(opt, TOX_SAVEDATA_TYPE_TOX_SAVE);
			tox_options_set_savedata_data(opt, saved, slen);
		}
	}
	if (f)
		fclose(f);
	t->tox = tox_new(opt, &err);
	tox_options_free(opt);
	free(saved);
	if (!t->tox) {
		free(t);
		return NULL;
	}
	tox_callback_self_connection_status(t->tox, on_status);
	tox_callback_friend_request(t->tox, on_req);
	tox_callback_friend_message(t->tox, on_msg);
	{
		static const struct {
			const char *host;
			uint16_t port;
			const char *key_hex;
		} nodes[] = {
			{ "85.143.221.42", 33445,
			  "DA4E4ED4B697F2E9B000EEFE3A34B554ACD3F45F5C96EAEA2516DD7FF9AF7B43" },
			{ "205.185.116.116", 33445,
			  "A179B09749AC826FF01F37A9613F6B57118AE014D4196A0E1105A98F93A54702" },
			{ "tox.abilinski.com", 33445,
			  "10C00EB250C3233E343E2AEBA07115A5C28920E9C8D29492F6D00B29049EDC7E" },
		};
		for (size_t i = 0; i < sizeof(nodes) / sizeof(nodes[0]); i++) {
			uint8_t key[TOX_PUBLIC_KEY_SIZE];
			Tox_Err_Bootstrap berr = TOX_ERR_BOOTSTRAP_OK;
			if (hex_in(nodes[i].key_hex, key, TOX_PUBLIC_KEY_SIZE) != 0)
				continue;
			tox_bootstrap(t->tox, nodes[i].host, nodes[i].port, key, &berr);
			tox_add_tcp_relay(t->tox, nodes[i].host, nodes[i].port, key, &berr);
		}
	}
	omaq_tox_save(t);
	return t;
}

void omaq_tox_close(struct omaq_tox *t)
{
	if (!t)
		return;
	omaq_tox_save(t);
	if (t->tox)
		tox_kill(t->tox);
	free(t);
}

void omaq_tox_iterate(struct omaq_tox *t)
{
	if (t && t->tox)
		tox_iterate(t->tox, t);
}

uint32_t omaq_tox_interval_ms(const struct omaq_tox *t)
{
	if (!t || !t->tox)
		return 50;
	return tox_iteration_interval(t->tox);
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

int omaq_tox_friend_add(struct omaq_tox *t, const char *addr_hex, const char *msg)
{
	uint8_t addr[TOX_ADDRESS_SIZE];
	Tox_Err_Friend_Add err = TOX_ERR_FRIEND_ADD_OK;
	if (!t || !addr_hex || !msg)
		return -1;
	if (strlen(addr_hex) != TOX_ADDRESS_SIZE * 2)
		return -1;
	if (hex_in(addr_hex, addr, TOX_ADDRESS_SIZE) != 0)
		return -1;
	tox_friend_add(t->tox, addr, (const uint8_t *)msg, strlen(msg), &err);
	omaq_tox_save(t);
	return err == TOX_ERR_FRIEND_ADD_OK ? 0 : -1;
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

int omaq_tox_online(const struct omaq_tox *t)
{
	return t ? t->online : 0;
}

#endif /* HAVE_TOX */
