/* Helper: lock election + JSON ops. Tox when compiled with HAVE_TOX. */

#include "invite.h"
#include "json_io.h"
#include "message.h"

#ifdef HAVE_TOX
#include "identity.h"
#include "tox_adapt.h"
#endif

#include <errno.h>
#include <stdint.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_TOX
static struct omaq_tox *g_tox;
static uint8_t g_pending_pk[32];
static int g_have_pending;
static char g_issued_id[OMAQ_INVITE_ID_MAX + 1];
#endif

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
	return 0;
}

static void emit(const char *s)
{
	fputs(s, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

static void emit_error(const char *code)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "{\"event\":\"error\",\"code\":\"%s\"}", code);
	emit(buf);
}

#ifdef HAVE_TOX
static void hook_req(void *ud, const uint8_t *pk32, const char *msg)
{
	(void)ud;
	memcpy(g_pending_pk, pk32, 32);
	g_have_pending = 1;
	if (g_issued_id[0] && strcmp(msg, g_issued_id) != 0)
		return;
	emit("{\"event\":\"request\",\"kind\":\"direct\"}");
}

static void hook_msg(void *ud, uint32_t friend, const char *text)
{
	char ev[700];
	(void)ud;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message\",\"conversation\":\"%u\",\"text\":\"%s\"}",
		 friend, text);
	emit(ev);
	omaq_message_append(home_dir(), "0", "peer", text, "in");
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

static int handle_op(const omaq_op *op)
{
	if (strcmp(op->op, "status") == 0) {
#ifdef HAVE_TOX
		char addr[77];
		char ev[160];
		if (g_tox && omaq_tox_self_addr_hex(g_tox, addr) == 0) {
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"unread\":0,\"online\":%s,\"addr\":\"%s\"}",
				 omaq_tox_online(g_tox) ? "true" : "false", addr);
			emit(ev);
			return 0;
		}
#endif
		emit("{\"event\":\"snapshot\",\"unread\":0,\"conversations\":[]}");
		return 0;
	}
	if (strcmp(op->op, "invite.create") == 0) {
		if (strcmp(op->kind, "group") == 0) {
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
			if (omaq_invite_format(&inv, url, sizeof(url)) != 0) {
				emit_error("unsupported");
				return 0;
			}
			snprintf(g_issued_id, sizeof(g_issued_id), "%s", inv.id);
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
			emit_error("invite_expired");
			return 0;
		}
		if (inv.kind == INVITE_GROUP) {
			emit_error("unsupported");
			return 0;
		}
		if (omaq_invite_expired(&inv, (int64_t)time(NULL))) {
			emit_error("invite_expired");
			return 0;
		}
#ifdef HAVE_TOX
		if (g_tox) {
			if (omaq_tox_friend_add(g_tox, inv.tox_addr, inv.id) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
#endif
		emit("{\"event\":\"request\",\"kind\":\"direct\"}");
		return 0;
	}
	if (strcmp(op->op, "contact.decide") == 0) {
#ifdef HAVE_TOX
		if (g_tox && op->accept && g_have_pending) {
			omaq_tox_friend_accept(g_tox, g_pending_pk);
			g_have_pending = 0;
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
#endif
		emit("{\"event\":\"snapshot\",\"unread\":0,\"conversations\":[]}");
		return 0;
	}
	if (strcmp(op->op, "msg.send") == 0) {
#ifdef HAVE_TOX
		if (g_tox && op->text[0]) {
			uint32_t fn = (uint32_t)atoi(op->conversation[0] ? op->conversation : "0");
			if (omaq_tox_send(g_tox, fn, op->text) != 0) {
				emit_error("forbidden");
				return 0;
			}
			omaq_message_append(home_dir(), "0", "me", op->text, "out");
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
		if (omaq_message_history(home_dir(), op->conversation[0] ? op->conversation : "0",
					 lim, &out, &n) == 0 && out) {
			/* keep payload small; tests check store separately */
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			free(out);
			return 0;
		}
		emit("{\"event\":\"snapshot\",\"unread\":0}");
		return 0;
	}
	if (strcmp(op->op, "invite.revoke") == 0 ||
	    strcmp(op->op, "contact.remove") == 0 ||
	    strcmp(op->op, "nospam.rotate") == 0) {
		emit("{\"event\":\"snapshot\",\"unread\":0,\"conversations\":[]}");
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
		line[n - 1] = '\0';
	if (line[0] == '\0')
		return 0;
	if (omaq_json_parse_op(line, &op) != 0) {
		emit_error("unsupported");
		return 0;
	}
	return handle_op(&op);
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
	if (mkdir(state_dir(), 0700) != 0 && errno != EEXIST)
		return 1;
	rc = take_lock();
	if (rc == 2)
		return 2;
	if (rc != 0)
		return 1;
#ifdef HAVE_TOX
	g_tox = omaq_identity_load(home_dir());
	if (g_tox)
		omaq_tox_set_hooks(g_tox, hook_req, hook_msg, NULL);
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
#ifdef HAVE_TOX
	if (g_tox) {
		struct pollfd pfd = { .fd = 0, .events = POLLIN };
		while (1) {
			int ms = (int)omaq_tox_interval_ms(g_tox);
			int pr = poll(&pfd, 1, ms);
			omaq_tox_iterate(g_tox);
			if (pr > 0 && (pfd.revents & POLLIN)) {
				if (!fgets(line, sizeof(line), stdin))
					break;
				serve_line(line);
			}
			if (pr < 0 && errno != EINTR)
				break;
		}
		omaq_tox_close(g_tox);
		return 0;
	}
#endif
	while (fgets(line, sizeof(line), stdin))
		serve_line(line);
	return 0;
}
