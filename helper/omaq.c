#define _DEFAULT_SOURCE

#include "av.h"
#include "file.h"
#include "group.h"
#include "invite.h"
#include "json_io.h"
#include "message.h"
#include "qr.h"
#include "ratchet.h"
#include "rate.h"
#include "safety.h"
#include "surface.h"

#include "identity.h"
#ifdef HAVE_TOX
#include "tox_adapt.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 8

#ifdef HAVE_TOX
static struct omaq_tox *g_tox;
static int g_locked;
#ifdef HAVE_SIGNAL
static struct omaq_ratchet *g_ratchet;
static struct {
	char conv[16];
	char rk[OMAQ_RK_HEX + 1];
} g_rkmap[8];

static void rk_set(const char *conv, const char *rk)
{
	int i, free_i = -1;

	if (!conv || !rk)
		return;
	for (i = 0; i < 8; i++) {
		if (!g_rkmap[i].conv[0]) {
			if (free_i < 0)
				free_i = i;
			continue;
		}
		if (strcmp(g_rkmap[i].conv, conv) == 0) {
			snprintf(g_rkmap[i].rk, sizeof(g_rkmap[i].rk), "%s", rk);
			return;
		}
	}
	if (free_i < 0)
		return;
	snprintf(g_rkmap[free_i].conv, sizeof(g_rkmap[free_i].conv), "%s", conv);
	snprintf(g_rkmap[free_i].rk, sizeof(g_rkmap[free_i].rk), "%s", rk);
}

static const char *rk_get(const char *conv)
{
	int i;
	if (!conv)
		return NULL;
	for (i = 0; i < 8; i++) {
		if (g_rkmap[i].conv[0] && strcmp(g_rkmap[i].conv, conv) == 0)
			return g_rkmap[i].rk;
	}
	return NULL;
}
#endif
static uint8_t g_pending_pk[32];
static int g_have_pending;
static char g_issued_id[OMAQ_INVITE_ID_MAX + 1];
static char g_issued_url[OMAQ_URL_MAX];
static int64_t g_issued_exp;
static int g_issued_is_group;
static char g_issued_group[80];
static omaq_role g_issued_grole;
static int g_have_gpending;
static uint32_t g_gpending_friend;
static uint8_t g_gpending_data[1024];
static size_t g_gpending_len;
#endif
static omaq_rate g_rate;

static int g_lockfd = -1;
static int g_listen = -1;
static int g_clients[MAX_CLIENTS];
static size_t g_ncli;
static char g_cbuf[MAX_CLIENTS][OMAQ_JSON_LINE_MAX];
static size_t g_clen[MAX_CLIENTS];

static const char *home_dir(void)
{
	const char *h = getenv("OMAQ_HOME");
	return h && h[0] ? h : NULL;
}

static const char *state_dir(void)
{
	const char *h = getenv("OMAQ_STATE");
	return h && h[0] ? h : NULL;
}

static int take_lock(void)
{
	const char *home = home_dir();
	char path[512];
	int fd;

	if (!home)
		return -1;
	if (mkdir(home, 0700) != 0 && errno != EEXIST)
		return -1;
	if (snprintf(path, sizeof(path), "%s/omaq.lock", home) >= (int)sizeof(path))
		return -1;
	fd = open(path, O_RDWR | O_CREAT, 0600);
	if (fd < 0)
		return -1;
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		return 2;
	}
	g_lockfd = fd;
	return 0;
}

static int bind_sock(void)
{
	struct sockaddr_un addr;
	char path[512];

	if (snprintf(path, sizeof(path), "%s/omaq.sock", state_dir()) >= (int)sizeof(path))
		return -1;
	if (strlen(path) >= sizeof(addr.sun_path))
		return -1;
	unlink(path);
	g_listen = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (g_listen < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, path, strlen(path) + 1);
	if (bind(g_listen, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(g_listen);
		g_listen = -1;
		return -1;
	}
	if (listen(g_listen, MAX_CLIENTS) != 0) {
		close(g_listen);
		g_listen = -1;
		unlink(path);
		return -1;
	}
	if (chmod(path, 0600) != 0) {
		close(g_listen);
		g_listen = -1;
		unlink(path);
		return -1;
	}
	return 0;
}

static int write_pid(void)
{
	char path[512];
	FILE *f;

	if (snprintf(path, sizeof(path), "%s/omaq.pid", state_dir()) >= (int)sizeof(path))
		return -1;
	f = fopen(path, "w");
	if (!f)
		return -1;
	if (fprintf(f, "%ld\n", (long)getpid()) < 0) {
		fclose(f);
		return -1;
	}
	fclose(f);
	return 0;
}

static void emit_fd(int fd, const char *s)
{
	if (fd < 0)
		return;
	dprintf(fd, "%s\n", s);
}

static void emit(const char *s)
{
	emit_fd(1, s);
	for (size_t i = 0; i < g_ncli; i++)
		emit_fd(g_clients[i], s);
}

static void emit_error(const char *code)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "{\"event\":\"error\",\"code\":\"%s\"}", code);
	emit(buf);
}

#ifdef HAVE_TOX
static void clear_invite(void)
{
	g_issued_id[0] = '\0';
	g_issued_url[0] = '\0';
	g_issued_exp = 0;
	g_issued_is_group = 0;
	g_issued_group[0] = '\0';
	g_have_pending = 0;
}

static void emit_group(const char *gid, const char *action, uint32_t peer)
{
	char ev[160];
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"group.changed\",\"group\":\"%s\",\"action\":\"%s\",\"peer\":\"%u\"}",
		 gid, action, peer);
	emit(ev);
}

static void pk_hex(const uint8_t *pk, char *out)
{
	static const char *d = "0123456789abcdef";
	int i;

	for (i = 0; i < 32; i++) {
		out[i * 2] = d[pk[i] >> 4];
		out[i * 2 + 1] = d[pk[i] & 0xf];
	}
	out[64] = '\0';
}

static void emit_safety(uint32_t friend)
{
	char self[65], peer[65], code[OMAQ_SAFETY_MAX], ev[320], conv[16];

	if (!g_tox)
		return;
	if (omaq_tox_self_pk_hex(g_tox, self) != 0)
		return;
	if (omaq_tox_friend_pk_hex(g_tox, friend, peer) != 0)
		return;
	if (omaq_safety_code(self, peer, code, sizeof(code)) != 0)
		return;
	snprintf(conv, sizeof(conv), "%u", friend);
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"safety\",\"conversation\":\"%s\",\"code\":\"%s\"}",
		 conv, code);
	emit(ev);
}

static void hook_req(void *ud, const uint8_t *pk32, const char *msg)
{
	char key[65];
	int64_t now = (int64_t)time(NULL);

	(void)ud;
	pk_hex(pk32, key);
	if (omaq_rate_allow(&g_rate, key, now) != 0)
		return;
	if (!g_issued_id[0] || !msg || strcmp(msg, g_issued_id) != 0)
		return;
	if (g_issued_exp && now >= g_issued_exp)
		return;
	if (g_have_pending)
		return;
	memcpy(g_pending_pk, pk32, 32);
	g_have_pending = 1;
	if (g_issued_is_group)
		emit("{\"event\":\"request\",\"kind\":\"group\"}");
	else
		emit("{\"event\":\"request\",\"kind\":\"direct\"}");
}

static void hook_ginv(void *ud, uint32_t friend, const uint8_t *data, size_t len)
{
	(void)ud;
	if (!data || len == 0 || len > sizeof(g_gpending_data))
		return;
	if (g_have_gpending)
		return;
	memcpy(g_gpending_data, data, len);
	g_gpending_len = len;
	g_gpending_friend = friend;
	g_have_gpending = 1;
	emit("{\"event\":\"request\",\"kind\":\"group\"}");
}

static void hook_gmsg(void *ud, uint32_t gnum, uint32_t peer, const char *text)
{
	char gid[OMAQ_GROUP_ID_MAX], esc[2800], ev[3000];
	(void)ud;
	(void)peer;
	if (omaq_group_id_format(gnum, gid, sizeof(gid)) != 0)
		return;
	if (omaq_json_escape(text, esc, sizeof(esc)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message\",\"conversation\":\"%s\",\"text\":\"%s\"}",
		 gid, esc);
	emit(ev);
	omaq_message_append(home_dir(), gid, "peer", text, "in");
}

static void hook_gpeer(void *ud, uint32_t gnum, uint32_t peer, int joined)
{
	char gid[OMAQ_GROUP_ID_MAX];
	(void)ud;
	if (omaq_group_id_format(gnum, gid, sizeof(gid)) != 0)
		return;
	if (joined)
		omaq_group_note_peer(gnum, peer);
	else
		omaq_group_drop_peer(gnum, peer);
	emit_group(gid, joined ? "join" : "leave", peer);
}

static void hook_msg(void *ud, uint32_t friend, const char *text)
{
	char esc[2800], ev[3000], conv[16];
#ifdef HAVE_SIGNAL
	char plain[1400];
#endif
	(void)ud;
	snprintf(conv, sizeof(conv), "%u", friend);
#ifdef HAVE_SIGNAL
	if (g_ratchet && text && strncmp(text, "OQB1", 4) == 0) {
		int had = omaq_ratchet_has_session(g_ratchet, conv);
		if (!had)
			(void)omaq_ratchet_accept_bundle(g_ratchet, conv, text + 4, rk_get(conv));
		if (!had && g_tox) {
			char bun[900], bmsg[920];
			if (omaq_ratchet_bundle(g_ratchet, bun, sizeof(bun)) == 0) {
				snprintf(bmsg, sizeof(bmsg), "OQB1%s", bun);
				(void)omaq_tox_send(g_tox, friend, bmsg);
			}
		}
		return;
	}
	if (g_ratchet && text && strncmp(text, "OQR1", 4) == 0) {
		if (omaq_ratchet_decrypt(g_ratchet, conv, text, plain, sizeof(plain)) != 0)
			return;
		text = plain;
	}
#endif
	if (omaq_json_escape(text, esc, sizeof(esc)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message\",\"conversation\":\"%s\",\"text\":\"%s\"}",
		 conv, esc);
	emit(ev);
	omaq_message_append(home_dir(), conv, "peer", text, "in");
}

static void emit_file(const char *state, uint32_t friend, uint32_t fnum,
		      const char *name, uint64_t size, const char *path)
{
	char id[OMAQ_FILE_ID_MAX], conv[16], ev[900];
	char ename[OMAQ_FILE_NAME_MAX * 2 + 8], epath[OMAQ_JSON_STR_MAX];

	if (omaq_file_id_format(friend, fnum, id, sizeof(id)) != 0)
		return;
	snprintf(conv, sizeof(conv), "%u", friend);
	ename[0] = '\0';
	epath[0] = '\0';
	if (name && omaq_json_escape(name, ename, sizeof(ename)) != 0)
		ename[0] = '\0';
	if (path && omaq_json_escape(path, epath, sizeof(epath)) != 0)
		epath[0] = '\0';
	if (strcmp(state, "offer") == 0) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"file.offer\",\"id\":\"%s\",\"conversation\":\"%s\",\"name\":\"%s\",\"size\":%llu}",
			 id, conv, ename, (unsigned long long)size);
	} else if (strcmp(state, "done") == 0) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"file.done\",\"id\":\"%s\",\"conversation\":\"%s\",\"path\":\"%s\"}",
			 id, conv, epath);
	} else {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"file.failed\",\"id\":\"%s\",\"conversation\":\"%s\"}",
			 id, conv);
	}
	emit(ev);
}

static void hook_file_recv(void *ud, uint32_t friend, uint32_t fnum,
			   const char *name, uint64_t size)
{
	(void)ud;
	if (omaq_file_offer_store(friend, fnum, name, size) != 0) {
		omaq_file_cancel(g_tox, friend, fnum);
		return;
	}
	emit_file("offer", friend, fnum, name, size, NULL);
}

static void hook_file_creq(void *ud, uint32_t friend, uint32_t fnum, uint64_t pos, size_t len)
{
	(void)ud;
	if (omaq_file_chunk_out(g_tox, friend, fnum, pos, len) != 0) {
		omaq_file_cancel(g_tox, friend, fnum);
		emit_file("failed", friend, fnum, NULL, 0, NULL);
		return;
	}
	if (len == 0)
		emit_file("done", friend, fnum, NULL, 0, NULL);
}

static void hook_file_chunk(void *ud, uint32_t friend, uint32_t fnum, uint64_t pos,
			    const uint8_t *data, size_t len)
{
	char dest[512];
	int rc;

	(void)ud;
	rc = omaq_file_chunk_in(friend, fnum, pos, data, len, dest, sizeof(dest));
	if (rc < 0) {
		omaq_file_cancel(g_tox, friend, fnum);
		emit_file("failed", friend, fnum, NULL, 0, NULL);
		return;
	}
	if (rc == 1) {
		char conv[16];
		snprintf(conv, sizeof(conv), "%u", friend);
		omaq_message_append(home_dir(), conv, "peer", dest, "in");
		emit_file("done", friend, fnum, NULL, 0, dest);
	}
}

static void hook_file_ctrl(void *ud, uint32_t friend, uint32_t fnum, int control)
{
	(void)ud;
	if (control != OMAQ_TOX_FILE_CANCEL)
		return;
	omaq_file_offer_drop(friend, fnum);
	omaq_file_cancel(g_tox, friend, fnum);
	emit_file("failed", friend, fnum, NULL, 0, NULL);
}

static void hook_call(void *ud, uint32_t friend, int incoming)
{
	char conv[16], ev[160];

	(void)ud;
	snprintf(conv, sizeof(conv), "%u", friend);
	if (incoming) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"call.incoming\",\"conversation\":\"%s\"}", conv);
		emit(ev);
		return;
	}
	omaq_av_note_end(friend);
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"call.state\",\"conversation\":\"%s\",\"state\":\"ended\"}", conv);
	emit(ev);
}

static void rand_id(char *out, size_t n)
{
	unsigned char b[8];
	static const char *d = "0123456789abcdef";
	if (getrandom(b, sizeof(b), 0) != (ssize_t)sizeof(b))
		memset(b, 0x11, sizeof(b));
	for (size_t i = 0; i < sizeof(b) && i * 2 + 1 < n; i++) {
		out[i * 2] = d[b[i] >> 4];
		out[i * 2 + 1] = d[b[i] & 0xf];
	}
	out[16] = '\0';
}
#endif

#ifdef HAVE_TOX
static void attach_hooks(void)
{
	if (!g_tox)
		return;
	omaq_tox_set_hooks(g_tox, hook_req, hook_msg, NULL);
	omaq_tox_set_group_hooks(g_tox, hook_ginv, hook_gmsg, hook_gpeer, NULL);
	omaq_tox_set_file_hooks(g_tox, hook_file_recv, hook_file_creq, hook_file_chunk,
				hook_file_ctrl, NULL);
	omaq_tox_set_call_hook(g_tox, hook_call, NULL);
}

static int load_tox(const char *pass)
{
	int err = 0;

	g_tox = omaq_identity_load(home_dir(), pass, &err);
	if (err == OMAQ_TOX_LOCKED) {
		g_locked = 1;
		return 1;
	}
	g_locked = 0;
	if (!g_tox)
		return -1;
	attach_hooks();
	return 0;
}
#endif

static int handle_op(const omaq_op *op)
{
	if (strcmp(op->op, "status") == 0) {
#ifdef HAVE_TOX
		char addr[77];
		char ev[192];
		if (g_locked && !g_tox) {
			emit("{\"event\":\"snapshot\",\"unread\":0,\"locked\":true}");
			return 0;
		}
		if (g_tox && omaq_tox_self_addr_hex(g_tox, addr) == 0) {
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"unread\":0,\"online\":%s,\"addr\":\"%s\",\"protected\":%s}",
				 omaq_tox_online(g_tox) ? "true" : "false", addr,
				 omaq_identity_protected(g_tox) ? "true" : "false");
			emit(ev);
			return 0;
		}
#endif
		emit("{\"event\":\"snapshot\",\"unread\":0,\"conversations\":[]}");
		return 0;
	}
#ifdef HAVE_TOX
	if (g_locked && !g_tox &&
	    strcmp(op->op, "identity.unlock") != 0) {
		emit_error("locked");
		return 0;
	}
	if (strcmp(op->op, "identity.unlock") == 0) {
		int rc;

		if (g_tox) {
			emit("{\"event\":\"identity\",\"op\":\"unlock\"}");
			return 0;
		}
		if (!omaq_identity_pass_ok(op->passphrase)) {
			emit_error("forbidden");
			return 0;
		}
		rc = load_tox(op->passphrase);
		if (rc != 0) {
			emit_error("locked");
			return 0;
		}
		emit("{\"event\":\"identity\",\"op\":\"unlock\"}");
		return 0;
	}
	if (strcmp(op->op, "identity.protect") == 0) {
		if (!g_tox || omaq_identity_protect(g_tox, op->passphrase) != 0) {
			emit_error("forbidden");
			return 0;
		}
		emit("{\"event\":\"identity\",\"op\":\"protect\",\"protected\":true}");
		return 0;
	}
	if (strcmp(op->op, "identity.unprotect") == 0) {
		if (!g_tox || omaq_identity_unprotect(g_tox, op->passphrase) != 0) {
			emit_error("forbidden");
			return 0;
		}
		emit("{\"event\":\"identity\",\"op\":\"protect\",\"protected\":false}");
		return 0;
	}
#endif
	if (strcmp(op->op, "invite.create") == 0) {
		if (strcmp(op->kind, "group") == 0) {
#ifdef HAVE_TOX
			if (g_tox) {
				omaq_invite inv;
				char url[OMAQ_URL_MAX];
				char ev[OMAQ_URL_MAX + 64];
				omaq_role self = ROLE_MEMBER;
				omaq_role granted = ROLE_MEMBER;
				int ttl = op->has_ttl ? op->ttl_sec : 86400;
				if (!op->group[0] || omaq_group_self_role(g_tox, op->group, &self) != 0) {
					emit_error("forbidden");
					return 0;
				}
				if (op->role[0] && omaq_role_parse(op->role, &granted) != 0) {
					emit_error("unsupported");
					return 0;
				}
				memset(&inv, 0, sizeof(inv));
				omaq_tox_self_addr_hex(g_tox, inv.tox_addr);
				rand_id(inv.id, sizeof(inv.id));
				inv.expiry = (int64_t)time(NULL) + ttl;
				inv.kind = INVITE_GROUP;
				if (strlen(op->group) >= sizeof(inv.group)) {
					emit_error("unsupported");
					return 0;
				}
				memcpy(inv.group, op->group, strlen(op->group) + 1);
				snprintf(inv.role, sizeof(inv.role), "%s", omaq_role_name(granted));
				if (omaq_invite_format(&inv, url, sizeof(url)) != 0) {
					emit_error("unsupported");
					return 0;
				}
				if (!omaq_role_may(self, ACT_INVITE, granted)) {
					emit_error("forbidden");
					return 0;
				}
				if (op->id[0] &&
				    omaq_group_invite_friend(g_tox, op->group,
							     (uint32_t)atoi(op->id),
							     self, granted) != 0) {
					emit_error("forbidden");
					return 0;
				}
				snprintf(g_issued_id, sizeof(g_issued_id), "%s", inv.id);
				snprintf(g_issued_url, sizeof(g_issued_url), "%s", url);
				g_issued_exp = inv.expiry;
				g_issued_is_group = 1;
				snprintf(g_issued_group, sizeof(g_issued_group), "%s", op->group);
				g_issued_grole = granted;
				snprintf(ev, sizeof(ev), "{\"event\":\"invite\",\"url\":\"%s\"}", url);
				emit(ev);
				return 0;
			}
#endif
			emit_error("unsupported");
			return 0;
		}
#ifdef HAVE_TOX
		if (g_tox) {
			omaq_invite inv;
			char url[OMAQ_URL_MAX];
			char ev[OMAQ_URL_MAX + 64];
			int ttl = op->has_ttl ? op->ttl_sec : 86400;
			memset(&inv, 0, sizeof(inv));
			omaq_tox_self_addr_hex(g_tox, inv.tox_addr);
			rand_id(inv.id, sizeof(inv.id));
			inv.expiry = (int64_t)time(NULL) + ttl;
			inv.kind = INVITE_DIRECT;
#ifdef HAVE_SIGNAL
			if (g_ratchet)
				(void)omaq_ratchet_local_rk(g_ratchet, inv.rk);
#endif
			if (omaq_invite_format(&inv, url, sizeof(url)) != 0) {
				emit_error("unsupported");
				return 0;
			}
			snprintf(g_issued_id, sizeof(g_issued_id), "%s", inv.id);
			snprintf(g_issued_url, sizeof(g_issued_url), "%s", url);
			g_issued_exp = inv.expiry;
			snprintf(ev, sizeof(ev), "{\"event\":\"invite\",\"url\":\"%s\"}", url);
			emit(ev);
			return 0;
		}
#endif
		emit("{\"event\":\"invite\",\"kind\":\"direct\"}");
		return 0;
	}
	if (strcmp(op->op, "invite.redeem") == 0) {
		omaq_invite inv;
		if (omaq_invite_parse(op->payload, &inv) != 0) {
			emit_error("unsupported");
			return 0;
		}
		if (omaq_invite_expired(&inv, (int64_t)time(NULL))) {
			emit_error("invite_expired");
			return 0;
		}
		if (inv.kind == INVITE_GROUP) {
#ifdef HAVE_TOX
			if (g_tox) {
				if (omaq_tox_friend_add(g_tox, inv.tox_addr, inv.id, NULL) != 0) {
					/* already a friend: group Tox invite must come from the owner */
					emit("{\"event\":\"snapshot\",\"unread\":0}");
					return 0;
				}
				emit("{\"event\":\"snapshot\",\"unread\":0}");
				return 0;
			}
#endif
			emit_error("unsupported");
			return 0;
		}
#ifdef HAVE_TOX
		if (g_tox) {
			uint32_t fn = 0;
			if (omaq_tox_friend_add(g_tox, inv.tox_addr, inv.id, &fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
#ifdef HAVE_SIGNAL
			if (inv.rk[0] && omaq_rk_ok(inv.rk)) {
				char conv[16];
				snprintf(conv, sizeof(conv), "%u", fn);
				rk_set(conv, inv.rk);
			}
#endif
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			emit_safety(fn);
			return 0;
		}
#endif
		emit("{\"event\":\"request\",\"kind\":\"direct\"}");
		return 0;
	}
	if (strcmp(op->op, "invite.revoke") == 0) {
#ifdef HAVE_TOX
		clear_invite();
#endif
		emit("{\"event\":\"snapshot\",\"unread\":0}");
		return 0;
	}
	if (strcmp(op->op, "invite.qr") == 0) {
		const char *url = op->payload[0] ? op->payload : NULL;
		char ev[OMAQ_URL_MAX + OMAQ_JSON_STR_MAX + 48];
		char esc_path[OMAQ_JSON_STR_MAX];
#ifdef HAVE_TOX
		if (!url)
			url = g_issued_url[0] ? g_issued_url : NULL;
#endif
		if (!url || !op->path[0]) {
			emit_error("unsupported");
			return 0;
		}
		if (omaq_qr_write_png(url, op->path) != 0) {
			emit_error("forbidden");
			return 0;
		}
		if (omaq_json_escape(op->path, esc_path, sizeof(esc_path)) != 0) {
			emit_error("unsupported");
			return 0;
		}
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"invite\",\"url\":\"%s\",\"qr\":\"%s\"}",
			 url, esc_path);
		emit(ev);
		return 0;
	}
	if (strcmp(op->op, "contact.decide") == 0) {
#ifdef HAVE_TOX
		if (g_tox && g_have_gpending) {
			if (op->has_accept && op->accept) {
				uint32_t gnum;
				char gid[OMAQ_GROUP_ID_MAX];
				if (omaq_tox_group_invite_accept(g_tox, g_gpending_friend,
								 g_gpending_data, g_gpending_len,
								 &gnum) != 0) {
					emit_error("forbidden");
					return 0;
				}
				g_have_gpending = 0;
				if (omaq_group_id_format(gnum, gid, sizeof(gid)) == 0)
					emit_group(gid, "join", 0);
				else
					emit("{\"event\":\"snapshot\",\"unread\":0}");
				return 0;
			}
			g_have_gpending = 0;
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
		if (g_tox && g_have_pending) {
			if (op->has_accept && op->accept) {
				uint32_t fn;
				if (omaq_tox_friend_accept(g_tox, g_pending_pk) != 0) {
					emit_error("forbidden");
					return 0;
				}
				fn = omaq_tox_friend_by_pk(g_tox, g_pending_pk);
				if (g_issued_is_group && g_issued_group[0] && fn != UINT32_MAX)
					(void)omaq_group_invite_friend(g_tox, g_issued_group, fn,
								       ROLE_OWNER, g_issued_grole);
				clear_invite();
				emit("{\"event\":\"snapshot\",\"unread\":0}");
				if (fn != UINT32_MAX)
					emit_safety(fn);
				return 0;
			}
			g_have_pending = 0;
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
#endif
		emit("{\"event\":\"snapshot\",\"unread\":0,\"conversations\":[]}");
		return 0;
	}
	if (strcmp(op->op, "contact.remove") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *cid = op->id[0] ? op->id : op->conversation;
			uint32_t fn = (uint32_t)atoi(cid[0] ? cid : "0");
			if (omaq_tox_friend_delete(g_tox, fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "nospam.rotate") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			char addr[77];
			char ev[160];
			if (omaq_tox_nospam_rotate(g_tox) != 0) {
				emit_error("forbidden");
				return 0;
			}
			clear_invite();
			if (omaq_tox_self_addr_hex(g_tox, addr) == 0) {
				snprintf(ev, sizeof(ev),
					 "{\"event\":\"snapshot\",\"unread\":0,\"online\":%s,\"addr\":\"%s\"}",
					 omaq_tox_online(g_tox) ? "true" : "false", addr);
				emit(ev);
			} else {
				emit("{\"event\":\"snapshot\",\"unread\":0}");
			}
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "group.create") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			char gid[OMAQ_GROUP_ID_MAX];
			const char *title = op->title[0] ? op->title : (op->text[0] ? op->text : "group");
			if (omaq_group_create(g_tox, title, gid, sizeof(gid)) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit_group(gid, "create", 0);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "group.dissolve") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			omaq_role self = ROLE_MEMBER;
			const char *gid = op->group[0] ? op->group : op->conversation;
			if (omaq_group_self_role(g_tox, gid, &self) != 0 ||
			    omaq_group_dissolve(g_tox, gid, self) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit_group(gid, "dissolve", 0);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "group.member.setRole") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			omaq_role self = ROLE_MEMBER;
			omaq_role next = ROLE_MEMBER;
			const char *gid = op->group[0] ? op->group : op->conversation;
			uint32_t peer = (uint32_t)atoi(op->member[0] ? op->member : (op->id[0] ? op->id : "0"));
			if (op->role[0] && omaq_role_parse(op->role, &next) != 0) {
				emit_error("unsupported");
				return 0;
			}
			if (omaq_group_self_role(g_tox, gid, &self) != 0 ||
			    omaq_group_set_role(g_tox, gid, peer, self, next) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit_group(gid, "role", peer);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "group.member.remove") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			omaq_role self = ROLE_MEMBER;
			omaq_role victim = ROLE_MEMBER;
			const char *gid = op->group[0] ? op->group : op->conversation;
			uint32_t peer = (uint32_t)atoi(op->member[0] ? op->member : (op->id[0] ? op->id : "0"));
			uint32_t gnum;
			if (omaq_group_self_role(g_tox, gid, &self) != 0)
				self = ROLE_MEMBER;
			if (omaq_group_id_parse(gid, &gnum) == 0)
				(void)omaq_tox_group_peer_role(g_tox, gnum, peer, (int *)&victim);
			if (omaq_group_kick(g_tox, gid, peer, self, victim) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit_group(gid, "kick", peer);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "group.leave") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *gid = op->group[0] ? op->group : op->conversation;
			if (omaq_group_leave(g_tox, gid) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit_group(gid, "leave", 0);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "surface.set") == 0) {
		omaq_surface s;
		memset(&s, 0, sizeof(s));
		snprintf(s.conversation, sizeof(s.conversation), "%s",
			 op->conversation[0] ? op->conversation : "0");
		snprintf(s.monitor, sizeof(s.monitor), "%s", op->monitor);
		s.x = op->x;
		s.y = op->y;
		s.pinned = op->has_pinned ? op->pinned : 0;
		if (omaq_surface_set(state_dir(), &s) != 0) {
			emit_error("forbidden");
			return 0;
		}
		{
			char ev[320], em[128];
			if (omaq_json_escape(s.monitor, em, sizeof(em)) != 0)
				em[0] = '\0';
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"surface\",\"conversation\":\"%s\",\"monitor\":\"%s\",\"x\":%d,\"y\":%d,\"pinned\":%s}",
				 s.conversation, em, s.x, s.y, s.pinned ? "true" : "false");
			emit(ev);
		}
		return 0;
	}
	if (strcmp(op->op, "surface.get") == 0) {
		omaq_surface s;
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (omaq_surface_get(state_dir(), cid, &s) != 0) {
			emit("{\"event\":\"surface\",\"conversation\":\"\",\"pinned\":false}");
			return 0;
		}
		{
			char ev[320], em[128];
			if (omaq_json_escape(s.monitor, em, sizeof(em)) != 0)
				em[0] = '\0';
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"surface\",\"conversation\":\"%s\",\"monitor\":\"%s\",\"x\":%d,\"y\":%d,\"pinned\":%s}",
				 s.conversation, em, s.x, s.y, s.pinned ? "true" : "false");
			emit(ev);
		}
		return 0;
	}
	if (strcmp(op->op, "safety.get") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			emit_safety((uint32_t)atoi(cid));
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "msg.send") == 0) {
#ifdef HAVE_TOX
		if (g_tox && op->text[0]) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			if (cid[0] == 'g') {
				if (omaq_group_send(g_tox, cid, op->text) != 0) {
					emit_error("forbidden");
					return 0;
				}
			} else {
				uint32_t fn = (uint32_t)atoi(cid);
#ifdef HAVE_SIGNAL
				if (g_ratchet) {
					char bun[900], wire[2800];
					if (!omaq_ratchet_has_session(g_ratchet, cid) &&
					    omaq_ratchet_bundle(g_ratchet, bun, sizeof(bun)) == 0) {
						char bmsg[920];
						snprintf(bmsg, sizeof(bmsg), "OQB1%s", bun);
						(void)omaq_tox_send(g_tox, fn, bmsg);
					}
					if (!omaq_ratchet_has_session(g_ratchet, cid)) {
						emit("{\"event\":\"snapshot\",\"unread\":0}");
						return 0;
					}
					if (omaq_ratchet_encrypt(g_ratchet, cid, op->text,
								wire, sizeof(wire)) != 0) {
						emit_error("forbidden");
						return 0;
					}
					if (omaq_tox_send(g_tox, fn, wire) != 0) {
						emit_error("forbidden");
						return 0;
					}
					omaq_message_append(home_dir(), cid, "me", op->text, "out");
					emit("{\"event\":\"snapshot\",\"unread\":0}");
					return 0;
				}
#endif
				if (omaq_tox_send(g_tox, fn, op->text) != 0) {
					emit_error("forbidden");
					return 0;
				}
			}
			omaq_message_append(home_dir(), cid, "me", op->text, "out");
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
#endif
		emit("{\"event\":\"snapshot\",\"unread\":0,\"conversations\":[]}");
		return 0;
	}
	if (strcmp(op->op, "history") == 0) {
		char *out = NULL;
		size_t n = 0;
		int lim = op->has_limit ? op->limit : 50;
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (omaq_message_history(home_dir(), cid, lim, &out, &n) == 0 && out) {
			char ev[OMAQ_JSON_LINE_MAX];
			char *p = ev;
			size_t left = sizeof(ev);
			int first = 1;
			char *line = out;
			char esc_cid[128];
			if (omaq_json_escape(cid, esc_cid, sizeof(esc_cid)) != 0) {
				free(out);
				emit("{\"event\":\"history\",\"conversation\":\"0\",\"items\":[]}");
				return 0;
			}
			int wr = snprintf(p, left,
					 "{\"event\":\"history\",\"conversation\":\"%s\",\"items\":[",
					 esc_cid);
			if (wr < 0 || (size_t)wr >= left) {
				free(out);
				emit_error("unsupported");
				return 0;
			}
			p += wr;
			left -= (size_t)wr;
			while (*line) {
				char *nl = strchr(line, '\n');
				size_t ln = nl ? (size_t)(nl - line) : strlen(line);
				if (!first) {
					if (left < 2)
						break;
					*p++ = ',';
					left--;
				}
				first = 0;
				if (ln + 1 >= left)
					break;
				memcpy(p, line, ln);
				p += ln;
				left -= ln;
				line += ln + (nl ? 1 : 0);
			}
			if (left < 3) {
				free(out);
				snprintf(ev, sizeof(ev),
					 "{\"event\":\"history\",\"conversation\":\"%s\",\"items\":[]}",
					 esc_cid);
				emit(ev);
				return 0;
			}
			memcpy(p, "]}", 3);
			emit(ev);
			free(out);
			return 0;
		}
		{
			char esc_cid[128];
			char ev[192];
			if (omaq_json_escape(cid, esc_cid, sizeof(esc_cid)) != 0)
				emit("{\"event\":\"history\",\"conversation\":\"0\",\"items\":[]}");
			else {
				snprintf(ev, sizeof(ev),
					 "{\"event\":\"history\",\"conversation\":\"%s\",\"items\":[]}",
					 esc_cid);
				emit(ev);
			}
		}
		return 0;
	}
	if (strcmp(op->op, "search") == 0) {
		char *out = NULL;
		size_t n = 0;
		int lim = op->has_limit ? op->limit : 20;
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (!op->text[0]) {
			emit("{\"event\":\"search\",\"items\":[]}");
			return 0;
		}
		if (omaq_message_search(home_dir(), cid, op->text, lim, &out, &n) == 0 && out) {
			char ev[OMAQ_JSON_LINE_MAX];
			char *p = ev;
			size_t left = sizeof(ev);
			int first = 1;
			char *line = out;
			int wr = snprintf(p, left, "{\"event\":\"search\",\"items\":[");
			if (wr < 0 || (size_t)wr >= left) {
				free(out);
				emit_error("unsupported");
				return 0;
			}
			p += wr;
			left -= (size_t)wr;
			while (*line) {
				char *nl = strchr(line, '\n');
				size_t ln = nl ? (size_t)(nl - line) : strlen(line);
				if (!first) {
					if (left < 2)
						break;
					*p++ = ',';
					left--;
				}
				first = 0;
				if (ln + 1 >= left)
					break;
				memcpy(p, line, ln);
				p += ln;
				left -= ln;
				line += ln + (nl ? 1 : 0);
			}
			if (left < 3) {
				free(out);
				emit("{\"event\":\"search\",\"items\":[]}");
				return 0;
			}
			memcpy(p, "]}", 3);
			emit(ev);
			free(out);
			return 0;
		}
		emit("{\"event\":\"search\",\"items\":[]}");
		return 0;
	}
	if (strcmp(op->op, "identity.export") == 0) {
		char dest[512];
		char ev[576], esc[512];
		const char *path = op->path[0] ? op->path : dest;
		if (!op->path[0]) {
			if (snprintf(dest, sizeof(dest), "%s/omaq-identity.save", state_dir()) >= (int)sizeof(dest)) {
				emit_error("unsupported");
				return 0;
			}
		}
		if (omaq_identity_export(home_dir(), path) != 0) {
			emit_error("forbidden");
			return 0;
		}
		if (omaq_json_escape(path, esc, sizeof(esc)) != 0) {
			emit_error("unsupported");
			return 0;
		}
		snprintf(ev, sizeof(ev), "{\"event\":\"identity\",\"op\":\"export\",\"path\":\"%s\"}", esc);
		emit(ev);
		return 0;
	}
	if (strcmp(op->op, "file.send") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			uint32_t fn, fnum;
			char name[OMAQ_FILE_NAME_MAX + 1];

			if (cid[0] == 'g') {
				emit_error("forbidden");
				return 0;
			}
			if (!op->path[0] || omaq_file_basename(op->path, name, sizeof(name)) != 0) {
				emit_error("unsupported");
				return 0;
			}
			fn = (uint32_t)atoi(cid);
			if (omaq_file_send_begin(g_tox, fn, op->path, &fnum) != 0) {
				emit_error("forbidden");
				return 0;
			}
			(void)fnum;
			(void)name;
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "file.accept") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			uint32_t fn, fnum;
			char name[OMAQ_FILE_NAME_MAX + 1];
			char dest[512];
			char conv[16];
			uint64_t size = 0;
			const char *over = op->path[0] ? op->path : NULL;

			if (omaq_file_id_parse(op->id, &fn, &fnum) != 0) {
				emit_error("unsupported");
				return 0;
			}
			if (omaq_file_offer_lookup(fn, fnum, name, sizeof(name), &size) != 0) {
				emit_error("forbidden");
				return 0;
			}
			snprintf(conv, sizeof(conv), "%u", fn);
			if (omaq_file_recv_begin(home_dir(), conv, fn, fnum, name, size,
						 over, dest, sizeof(dest)) != 0) {
				omaq_file_cancel(g_tox, fn, fnum);
				emit_error("forbidden");
				return 0;
			}
			if (omaq_tox_file_control(g_tox, fn, fnum, OMAQ_TOX_FILE_RESUME) != 0) {
				omaq_file_cancel(g_tox, fn, fnum);
				emit_error("forbidden");
				return 0;
			}
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "file.cancel") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			uint32_t fn, fnum;

			if (omaq_file_id_parse(op->id, &fn, &fnum) != 0) {
				emit_error("unsupported");
				return 0;
			}
			omaq_file_cancel(g_tox, fn, fnum);
			emit_file("failed", fn, fnum, NULL, 0, NULL);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "call.start") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			uint32_t fn;
			char ev[160];

			if (cid[0] == 'g') {
				emit_error("forbidden");
				return 0;
			}
			fn = (uint32_t)atoi(cid);
			if (omaq_av_start(g_tox, fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"call.state\",\"conversation\":\"%s\",\"state\":\"ringing\"}",
				 cid);
			emit(ev);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "call.answer") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			uint32_t fn;
			char ev[160];

			if (cid[0] == 'g') {
				emit_error("forbidden");
				return 0;
			}
			fn = (uint32_t)atoi(cid);
			if (omaq_av_answer(g_tox, fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"call.state\",\"conversation\":\"%s\",\"state\":\"active\"}",
				 cid);
			emit(ev);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "call.stop") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			uint32_t fn;
			char ev[160];

			if (cid[0] == 'g') {
				emit_error("forbidden");
				return 0;
			}
			fn = (uint32_t)atoi(cid);
			(void)omaq_av_stop(g_tox, fn);
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"call.state\",\"conversation\":\"%s\",\"state\":\"ended\"}",
				 cid);
			emit(ev);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "identity.import") == 0) {
		int rc;
		if (!op->path[0]) {
			emit_error("unsupported");
			return 0;
		}
		rc = omaq_identity_import(home_dir(), op->path, op->has_replace && op->replace);
		if (rc == 1) {
			emit_error("identity_exists");
			return 0;
		}
		if (rc != 0) {
			emit_error("forbidden");
			return 0;
		}
#ifdef HAVE_TOX
		if (g_tox) {
			omaq_tox_discard(g_tox);
			g_tox = NULL;
			(void)load_tox(NULL);
		}
#endif
		emit("{\"event\":\"identity\",\"op\":\"import\"}");
		return 0;
	}
	emit_error("unsupported");
	return 0;
}

static int serve_line(char *line)
{
	omaq_op op;
	size_t n = strlen(line);
	if (n && line[n - 1] == '\n')
		line[--n] = '\0';
	if (n && line[n - 1] == '\r')
		line[--n] = '\0';
	if (line[0] == '\0')
		return 0;
	if (omaq_json_parse_op(line, &op) != 0) {
		emit_error("unsupported");
		return 0;
	}
	{
		int rc = handle_op(&op);
		explicit_bzero(op.passphrase, sizeof(op.passphrase));
		explicit_bzero(line, n + 1);
		return rc;
	}
}

static void accept_client(void)
{
	int c = accept(g_listen, NULL, NULL);
	if (c < 0)
		return;
	fcntl(c, F_SETFD, FD_CLOEXEC);
	if (g_ncli >= MAX_CLIENTS) {
		close(c);
		return;
	}
	g_clients[g_ncli] = c;
	g_cbuf[g_ncli][0] = '\0';
	g_clen[g_ncli] = 0;
	g_ncli++;
}

static void drop_client(size_t i)
{
	close(g_clients[i]);
	if (i + 1 < g_ncli) {
		g_clients[i] = g_clients[g_ncli - 1];
		g_clen[i] = g_clen[g_ncli - 1];
		memcpy(g_cbuf[i], g_cbuf[g_ncli - 1], g_clen[i] + 1);
	}
	g_ncli--;
}

static void read_client(size_t i)
{
	char tmp[512];
	ssize_t r = read(g_clients[i], tmp, sizeof(tmp));
	size_t k;
	if (r <= 0) {
		drop_client(i);
		return;
	}
	for (k = 0; k < (size_t)r; k++) {
		if (g_clen[i] + 1 >= OMAQ_JSON_LINE_MAX) {
			g_clen[i] = 0;
			continue;
		}
		g_cbuf[i][g_clen[i]++] = tmp[k];
		if (tmp[k] == '\n') {
			g_cbuf[i][g_clen[i]] = '\0';
			serve_line(g_cbuf[i]);
			g_clen[i] = 0;
		}
	}
}

int main(int argc, char **argv)
{
	int hold = 0;
	int rc;
	char line[OMAQ_JSON_LINE_MAX];

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--hold") == 0)
			hold = 1;
	}
	if (!home_dir() || !state_dir()) {
		fprintf(stderr, "omaq: OMAQ_HOME and OMAQ_STATE required\n");
		return 1;
	}
	omaq_rate_init(&g_rate);
	if (mkdir(state_dir(), 0700) != 0 && errno != EEXIST)
		return 1;
	rc = take_lock();
	if (rc == 2)
		return 2;
	if (rc != 0)
		return 1;
	if (bind_sock() != 0)
		return 1;
	if (write_pid() != 0)
		return 1;
#ifdef HAVE_TOX
	(void)load_tox(NULL);
#endif
#ifdef HAVE_SIGNAL
	g_ratchet = omaq_ratchet_open(home_dir());
#endif
	if (hold) {
		for (;;) {
#ifdef HAVE_TOX
			if (g_tox) {
				omaq_tox_iterate(g_tox);
				usleep(omaq_tox_interval_ms(g_tox) * 1000);
				continue;
			}
#endif
			pause();
		}
	}

	while (1) {
		struct pollfd pf[2 + MAX_CLIENTS];
		int nf = 0;
		int ms = 250;
		int pr;

#ifdef HAVE_TOX
		if (g_tox)
			ms = (int)omaq_tox_interval_ms(g_tox);
#endif
		pf[nf].fd = 0;
		pf[nf].events = POLLIN;
		nf++;
		if (g_listen >= 0) {
			pf[nf].fd = g_listen;
			pf[nf].events = POLLIN;
			nf++;
		}
		for (size_t i = 0; i < g_ncli; i++) {
			pf[nf].fd = g_clients[i];
			pf[nf].events = POLLIN;
			nf++;
		}
		pr = poll(pf, (nfds_t)nf, ms);
#ifdef HAVE_TOX
		if (g_tox)
			omaq_tox_iterate(g_tox);
#endif
		if (pr < 0 && errno != EINTR)
			break;
		if (pr <= 0)
			continue;
		if (pf[0].revents & POLLIN) {
			if (!fgets(line, sizeof(line), stdin))
				break;
			serve_line(line);
		}
		if (g_listen >= 0 && pf[1].revents & POLLIN)
			accept_client();
		for (size_t i = 0; i < g_ncli; ) {
			int hit = 0;
			for (int k = 0; k < nf; k++) {
				if (pf[k].fd == g_clients[i] &&
				    (pf[k].revents & (POLLIN | POLLHUP | POLLERR)))
					hit = 1;
			}
			if (hit)
				read_client(i);
			else
				i++;
		}
	}
#ifdef HAVE_TOX
	if (g_tox)
		omaq_tox_close(g_tox);
#endif
#ifdef HAVE_SIGNAL
	if (g_ratchet)
		omaq_ratchet_close(g_ratchet);
#endif
	if (g_lockfd >= 0)
		close(g_lockfd);
	return 0;
}
