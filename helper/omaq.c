#define _DEFAULT_SOURCE

#include "invite.h"
#include "json_io.h"
#include "message.h"

#ifdef HAVE_TOX
#include "identity.h"
#include "tox_adapt.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static uint8_t g_pending_pk[32];
static int g_have_pending;
static char g_issued_id[OMAQ_INVITE_ID_MAX + 1];
#endif

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
static void hook_req(void *ud, const uint8_t *pk32, const char *msg)
{
	(void)ud;
	if (!g_issued_id[0] || !msg || strcmp(msg, g_issued_id) != 0)
		return;
	memcpy(g_pending_pk, pk32, 32);
	g_have_pending = 1;
	emit("{\"event\":\"request\",\"kind\":\"direct\"}");
}

static void hook_msg(void *ud, uint32_t friend, const char *text)
{
	char esc[2800], ev[3000], conv[16];
	(void)ud;
	snprintf(conv, sizeof(conv), "%u", friend);
	if (omaq_json_escape(text, esc, sizeof(esc)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message\",\"conversation\":\"%s\",\"text\":\"%s\"}",
		 conv, esc);
	emit(ev);
	omaq_message_append(home_dir(), conv, "peer", text, "in");
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
			emit_error("unsupported");
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
	if (strcmp(op->op, "invite.revoke") == 0) {
#ifdef HAVE_TOX
		g_issued_id[0] = '\0';
		g_have_pending = 0;
#endif
		emit("{\"event\":\"snapshot\",\"unread\":0}");
		return 0;
	}
	if (strcmp(op->op, "contact.decide") == 0) {
#ifdef HAVE_TOX
		if (g_tox && op->accept && g_have_pending) {
			omaq_tox_friend_accept(g_tox, g_pending_pk);
			g_have_pending = 0;
			g_issued_id[0] = '\0';
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
			const char *cid = op->conversation[0] ? op->conversation : "0";
			uint32_t fn = (uint32_t)atoi(cid);
			if (omaq_tox_send(g_tox, fn, op->text) != 0) {
				emit_error("forbidden");
				return 0;
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
			int wr = snprintf(p, left, "{\"event\":\"history\",\"items\":[");
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
				emit("{\"event\":\"history\",\"items\":[]}");
				return 0;
			}
			memcpy(p, "]}", 3);
			emit(ev);
			free(out);
			return 0;
		}
		emit("{\"event\":\"history\",\"items\":[]}");
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
	return handle_op(&op);
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
	if (g_lockfd >= 0)
		close(g_lockfd);
	return 0;
}
