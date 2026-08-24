#define _DEFAULT_SOURCE

#include "av.h"
#include "avatar.h"
#include "file.h"
#include "group.h"
#include "group_invite.h"
#include "conversation.h"
#include "invite.h"
#include "json_io.h"
#include "line_reader.h"
#include "message.h"
#include "message_action.h"
#include "qr.h"
#include "presence.h"
#include "receipt.h"
#include "ratchet.h"
#include "ratchet_pin.h"
#include "rate.h"
#include "safety.h"
#include "store.h"
#include "stdout_spool.h"
#include "surface.h"

#include "identity.h"
#ifdef HAVE_TOX
#include "tox_adapt.h"
#endif

#include <dirent.h>
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
#include <signal.h>
#include <time.h>
#include <unistd.h>

#define MAX_CLIENTS 8
#define CLIENT_OUT_MAX (OMAQ_JSON_LINE_MAX * 128u)
#define OMAQ_PROTOCOL_VERSION 3
#ifdef OMAQ_IPC_TEST
#define OMAQ_IPC_TEST_EVENT_SIZE 65500u
#endif

#ifdef HAVE_TOX
static struct omaq_tox *g_tox;
static int g_locked;
static int g_connection_online = -1;
#define FILE_REQUEST_CACHE 8
static struct {
	int used;
	uint32_t friend;
	uint32_t fnum;
	uint64_t sequence;
	char request[80];
	char state[12];
} g_file_requests[FILE_REQUEST_CACHE];
static uint64_t g_file_request_sequence;
static uint64_t g_identity_backup_sequence;
#ifdef HAVE_SIGNAL
static struct omaq_ratchet *g_ratchet;
#endif
static uint8_t g_pending_pk[32];
static int g_have_pending;
#ifdef HAVE_SIGNAL
static char g_pending_rk[OMAQ_RK_HEX + 1];
static int g_have_pending_rk;
#endif
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
static int g_gpending_announced;
static int g_have_gauth;
static uint32_t g_gauth_friend;
static int64_t g_gauth_exp;
static char g_gauth_group[OMAQ_GROUP_ID_MAX];
#endif
static int g_identity_requires_ready;
static int g_stdin_identity_ready = 1;
static omaq_rate g_rate;
static omaq_rate g_reaction_rate;
static omaq_rate g_reaction_out_rate;
static omaq_unread_state g_unread;

static int g_lockfd = -1;
static int g_state_lockfd = -1;
static int g_listen = -1;
static int g_clients[MAX_CLIENTS];
static size_t g_ncli;
static omaq_line_reader g_creader[MAX_CLIENTS];
static char g_obuf[MAX_CLIENTS][CLIENT_OUT_MAX];
static size_t g_olen[MAX_CLIENTS];
static size_t g_ooff[MAX_CLIENTS];
static int g_drop[MAX_CLIENTS];
static int g_client_identity_ready[MAX_CLIENTS];
static char g_stdout_buf[CLIENT_OUT_MAX];
static size_t g_stdout_len;
static size_t g_stdout_off;
static omaq_stdout_spool *g_stdout_spool;
static int g_stdout_closed;
static omaq_line_reader g_stdin_reader;
static int g_stdin_closed;
static int g_fatal_io;
static int g_shutdown_after_drain;
static int g_identity_recovered;
static int g_unread_load_failed;
static int g_identity_backup_cleanup_failed;
static char g_unread_error_code[32];
static int g_identity_recovery_required;
static int g_replay_mode;
static int g_backend_started;
static char g_instance_id[33];

static void drop_client(size_t i);
static void emit_unread_failed(const char *conversation, const char *code);

static void init_instance_id(void)
{
	unsigned char bytes[16];
	static const char digits[] = "0123456789abcdef";
	size_t i;

	if (getrandom(bytes, sizeof(bytes), 0) != (ssize_t)sizeof(bytes)) {
		struct timespec now;
		if (clock_gettime(CLOCK_REALTIME, &now) != 0)
			memset(bytes, 0, sizeof(bytes));
		else {
			for (i = 0; i < sizeof(bytes); i++)
				bytes[i] = (unsigned char)(((uint64_t)now.tv_nsec >> ((i % 8) * 8)) ^
							   ((uint64_t)getpid() >> ((i % 4) * 8)) ^ i);
		}
	}
	for (i = 0; i < sizeof(bytes); i++) {
		g_instance_id[i * 2] = digits[bytes[i] >> 4];
		g_instance_id[i * 2 + 1] = digits[bytes[i] & 0x0f];
	}
	g_instance_id[32] = '\0';
}

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

static int decimal_u32(const char *text, uint32_t *out)
{
	uint64_t value = 0;
	size_t i;

	if (!text || !text[0] || (text[0] == '0' && text[1]))
		return 0;
	for (i = 0; text[i]; i++) {
		uint32_t digit;
		if (text[i] < '0' || text[i] > '9')
			return 0;
		digit = (uint32_t)(text[i] - '0');
		if (value > (UINT32_MAX - digit) / 10u)
			return 0;
		value = value * 10u + digit;
	}
	if (out)
		*out = (uint32_t)value;
	return 1;
}

static int direct_id_ok(const char *id)
{
	return decimal_u32(id, NULL);
}

static uint32_t direct_id_number(const char *id)
{
	uint32_t value = 0;
	(void)decimal_u32(id, &value);
	return value;
}

static int conversation_id_ok(const char *id)
{
	uint32_t group_number;

	if (direct_id_ok(id))
		return 1;
	return id && id[0] == 'g' && omaq_group_id_parse(id, &group_number) == 0;
}

static int ensure_state_dir(void)
{
	const char *state = state_dir();
	char parent[512];
	char *slash;
	size_t len;
	int fd;

	if (!state || strlen(state) >= sizeof(parent))
		return -1;
	if (mkdir(state, 0700) != 0 && errno != EEXIST)
		return -1;
	memcpy(parent, state, strlen(state) + 1);
	len = strlen(parent);
	while (len > 1 && parent[len - 1] == '/')
		parent[--len] = '\0';
	slash = strrchr(parent, '/');
	if (!slash) {
		memcpy(parent, ".", 2);
	} else if (slash == parent) {
		parent[1] = '\0';
	} else {
		*slash = '\0';
	}
	fd = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	if (fsync(fd) != 0) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
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

static int take_state_lock(void)
{
	char path[512];
	struct stat st;
	int fd;

	if (snprintf(path, sizeof(path), "%s/omaq-state.lock", state_dir()) >=
	    (int)sizeof(path))
		return -1;
	fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || fchmod(fd, 0600) != 0) {
		close(fd);
		return -1;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		return 2;
	}
	g_state_lockfd = fd;
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

static int write_protocol_marker(void)
{
	char path[512], tmp[560];
	const char *nonce = getenv("OMAQ_PROTOCOL_NONCE");
	FILE *f;
	size_t i;

	if (!nonce)
		nonce = "";
	if (strlen(nonce) > 80)
		return -1;
	for (i = 0; nonce[i]; i++) {
		if (!((nonce[i] >= 'a' && nonce[i] <= 'z') ||
		      (nonce[i] >= '0' && nonce[i] <= '9') || nonce[i] == '-'))
			return -1;
	}
	if (snprintf(path, sizeof(path), "%s/omaq.protocol", state_dir()) >= (int)sizeof(path) ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp))
		return -1;
	f = fopen(tmp, "w");
	if (!f)
		return -1;
	if (fchmod(fileno(f), 0600) != 0 ||
	    fprintf(f, "{\"pid\":%ld,\"version\":%d,\"instance\":\"%s\",\"nonce\":\"%s\"}\n",
		    (long)getpid(), OMAQ_PROTOCOL_VERSION, g_instance_id, nonce) < 0 ||
	    fflush(f) != 0 || fsync(fileno(f)) != 0 || fclose(f) != 0) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) != 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}

static int queue_output(char *buf, size_t *len, size_t *off, size_t cap,
			const char *s)
{
	size_t n;

	if (!buf || !len || !off || !s)
		return -1;
	n = strlen(s) + 1;
	if (n > cap)
		return -1;
	if (*off > 0) {
		if (*off == *len) {
			*off = 0;
			*len = 0;
		} else {
			memmove(buf, buf + *off, *len - *off);
			*len -= *off;
			*off = 0;
		}
	}
	if (*len > cap - n)
		return -1;
	memcpy(buf + *len, s, n - 1);
	buf[*len + n - 1] = '\n';
	*len += n;
	return 0;
}

static int flush_output(int fd, char *buf, size_t *len, size_t *off)
{
	ssize_t wr;

	if (*off >= *len) {
		*off = *len = 0;
		return 0;
	}
	wr = write(fd, buf + *off, *len - *off);
	if (wr > 0) {
		*off += (size_t)wr;
		if (*off == *len)
			*off = *len = 0;
		return 0;
	}
	if (wr < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	return -1;
}

static int queue_client(size_t i, const char *s)
{
	if (i >= g_ncli)
		return -1;
	return queue_output(g_obuf[i], &g_olen[i], &g_ooff[i], CLIENT_OUT_MAX, s);
}

static void flush_client(size_t i)
{
	if (i >= g_ncli)
		return;
	if (flush_output(g_clients[i], g_obuf[i], &g_olen[i], &g_ooff[i]) != 0)
		g_drop[i] = 1;
}

static int stdout_event_transient(const char *s)
{
	static const char prefix[] = "{\"event\":\"typing\",";

	return s && strncmp(s, prefix, sizeof(prefix) - 1) == 0;
}

static void queue_stdout(const char *s)
{
	if (!s)
		return;
	/* Typing is transient, RAM-only, and may be dropped while stdout is stalled. */
	if (stdout_event_transient(s)) {
		/* Do not compact a record after any prefix has reached the pipe. */
		if (!g_stdout_closed && g_stdout_off == 0)
			(void)queue_output(g_stdout_buf, &g_stdout_len, &g_stdout_off,
						CLIENT_OUT_MAX, s);
		return;
	}
	/* Critical events remain durable even after the current stdout pipe closes. */
	if (!g_stdout_spool || omaq_stdout_spool_append(g_stdout_spool, s) != 0) {
		fprintf(stderr, "omaq: critical stdout spool append failed: %s\n",
			strerror(errno));
		g_fatal_io = 1;
	}
}

static void flush_stdout(void)
{
	int rc;

	if (g_stdout_closed)
		return;
	/* Never interleave a critical record into a partially written typing record. */
	if (g_stdout_off > 0) {
		if (flush_output(STDOUT_FILENO, g_stdout_buf,
				 &g_stdout_len, &g_stdout_off) != 0)
			g_stdout_closed = 1;
		return;
	}
	if (omaq_stdout_spool_pending(g_stdout_spool)) {
		rc = omaq_stdout_spool_flush(g_stdout_spool);
		if (rc == OMAQ_STDOUT_FLUSH_OUTPUT_ERROR)
			g_stdout_closed = 1;
		else if (rc == OMAQ_STDOUT_FLUSH_SPOOL_ERROR) {
			fprintf(stderr, "omaq: critical stdout spool read failed: %s\n",
				strerror(errno));
			g_fatal_io = 1;
		}
		return;
	}
	if (g_stdout_len > g_stdout_off &&
	    flush_output(STDOUT_FILENO, g_stdout_buf,
				  &g_stdout_len, &g_stdout_off) != 0)
		g_stdout_closed = 1;
}

static void emit(const char *s)
{
	queue_stdout(s);
	for (size_t i = 0; i < g_ncli; i++) {
		if (queue_client(i, s) != 0)
			g_drop[i] = 1;
	}
}

static void emit_error(const char *code)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "{\"event\":\"error\",\"code\":\"%s\"}", code);
	emit(buf);
}

static void emit_unread(const char *conversation)
{
	char esc_conv[128], ev[320];

	if (!conversation ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"unread\",\"conversation\":\"%s\",\"count\":%u,\"total\":%u}",
		 esc_conv, omaq_unread_count(&g_unread, conversation),
		 omaq_unread_total(&g_unread));
	emit(ev);
}

static void emit_all_unread(void)
{
	size_t i;

	for (i = 0; i < g_unread.length; i++)
		emit_unread(g_unread.entries[i].id);
	if (g_unread_error_code[0])
		emit_unread_failed("", g_unread_error_code);
	if (g_identity_backup_cleanup_failed)
		emit_error("identity_backup_cleanup_failed");
}

#ifdef HAVE_TOX
static void emit_locked_status(void)
{
	char ev[320];

	snprintf(ev, sizeof(ev),
		 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"locked\":true,\"instance\":\"%s\"}",
		 OMAQ_PROTOCOL_VERSION, omaq_unread_total(&g_unread), g_instance_id);
	emit(ev);
	emit_all_unread();
}
#endif

static int note_unread(const char *conversation)
{
	omaq_unread_state next;
	int saved;

	if (omaq_unread_clone(&next, &g_unread) != 0 ||
	    omaq_unread_increment(&next, conversation) != 0) {
		omaq_unread_destroy(&next);
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
		emit_unread_failed(conversation, g_unread_error_code);
		return -1;
	}
	saved = omaq_store_unread_save(&next, state_dir());
	omaq_unread_destroy(&g_unread);
	g_unread = next;
	emit_unread(conversation);
	if (saved != 0) {
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
		emit_unread_failed(conversation, g_unread_error_code);
	} else {
		g_unread_error_code[0] = '\0';
	}
	return saved == 0 ? 0 : -1;
}

static int clear_unread(const char *conversation)
{
	omaq_unread_state next;

	if (omaq_unread_clone(&next, &g_unread) != 0 ||
	    omaq_unread_clear(&next, conversation) != 0) {
		omaq_unread_destroy(&next);
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
		emit_unread(conversation);
		return -1;
	}
	if (omaq_store_unread_save(&next, state_dir()) != 0) {
		omaq_unread_destroy(&next);
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
		emit_unread(conversation);
		return -1;
	}
	omaq_unread_destroy(&g_unread);
	g_unread = next;
	g_unread_error_code[0] = '\0';
	emit_unread(conversation);
	return 0;
}

static void emit_json_items(const char *event, const char *conversation,
			     const char *items, size_t items_len, const char *request)
{
	char esc_conv[128], esc_request[128], prefix[420];
	char *ev, *p, *line;
	size_t cap, left;
	int first = 1;
	int wr;

	if (!event || !conversation || !items ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    items_len > (SIZE_MAX - 512u) / 2u) {
		emit_error("unsupported");
		return;
	}
	esc_request[0] = '\0';
	if (request && request[0] &&
	    omaq_json_escape(request, esc_request, sizeof(esc_request)) != 0) {
		emit_error("unsupported");
		return;
	}
	if (esc_request[0])
		wr = snprintf(prefix, sizeof(prefix),
			      "{\"event\":\"%s\",\"conversation\":\"%s\",\"request\":\"%s\",\"items\":[",
			      event, esc_conv, esc_request);
	else
		wr = snprintf(prefix, sizeof(prefix),
			      "{\"event\":\"%s\",\"conversation\":\"%s\",\"items\":[",
			      event, esc_conv);
	if (wr < 0 || (size_t)wr >= sizeof(prefix)) {
		emit_error("unsupported");
		return;
	}
	cap = items_len * 2u + (size_t)wr + 3u;
	ev = malloc(cap);
	if (!ev) {
		emit_error("unsupported");
		return;
	}
	memcpy(ev, prefix, (size_t)wr);
	p = ev + wr;
	left = cap - (size_t)wr;
	line = (char *)items;
	while (*line) {
		char *nl = strchr(line, '\n');
		size_t len = nl ? (size_t)(nl - line) : strlen(line);
		if (len == 0) {
			line += nl ? 1 : 0;
			continue;
		}
		if (!first) {
			if (left < 2)
				break;
			*p++ = ',';
			left--;
		}
		if (len + 2 > left)
			break;
		memcpy(p, line, len);
		p += len;
		left -= len;
		first = 0;
		line += len + (nl ? 1 : 0);
	}
	if (*line || left < 3) {
		free(ev);
		emit_error("unsupported");
		return;
	}
	memcpy(p, "]}", 3);
	emit(ev);
	free(ev);
}

static void emit_error_conv(const char *code, const char *conversation)
{
	char esc[128], buf[320];

	if (!conversation || omaq_json_escape(conversation, esc, sizeof(esc)) != 0) {
		emit_error(code);
		return;
	}
	snprintf(buf, sizeof(buf),
		 "{\"event\":\"error\",\"code\":\"%s\",\"conversation\":\"%s\"}",
		 code, esc);
	emit(buf);
}

static void emit_unread_failed(const char *conversation, const char *code)
{
	char esc_conv[128], ev[360];

	if (!conversation || !code ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"unread.failed\",\"conversation\":\"%s\",\"code\":\"%s\"}",
		 esc_conv, code);
	emit(ev);
}

static void emit_message_event_kind(const char *conversation, const char *id,
				     const char *reply, const char *text, const char *dir,
				     const char *kind, const char *request)
{
	char esc_conv[128], esc_id[128], esc_reply[128], esc_request[512],
		esc_text[2800], ev[3800];
	int has_id, has_reply, has_request, is_file;

	if (!conversation || !text || !dir ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(text, esc_text, sizeof(esc_text)) != 0)
		return;
	has_id = id && id[0] && omaq_json_escape(id, esc_id, sizeof(esc_id)) == 0;
	has_reply = reply && reply[0] && omaq_json_escape(reply, esc_reply, sizeof(esc_reply)) == 0;
	has_request = request && request[0] &&
		omaq_json_escape(request, esc_request, sizeof(esc_request)) == 0;
	is_file = kind && strcmp(kind, "file") == 0;
	if (has_id && has_reply && has_request) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"reply\":\"%s\",\"request\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, esc_id, esc_reply, esc_request, esc_text, dir);
	} else if (has_id && has_request) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"request\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, esc_id, esc_request, esc_text, dir);
	} else if (has_id && has_reply) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"reply\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, esc_id, esc_reply, esc_text, dir);
	} else if (has_id && is_file) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\",\"kind\":\"file\"}",
			 esc_conv, esc_id, esc_text, dir);
	} else if (has_id) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, esc_id, esc_text, dir);
	} else {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, esc_text, dir);
	}
	emit(ev);
}

static void emit_message_event(const char *conversation, const char *id,
				const char *reply, const char *text, const char *dir)
{
	emit_message_event_kind(conversation, id, reply, text, dir, NULL, NULL);
}

static void emit_message_event_request(const char *conversation, const char *id,
				       const char *reply, const char *text,
				       const char *request)
{
	emit_message_event_kind(conversation, id, reply, text, "out", NULL, request);
}

static void emit_message_failed(const char *conversation, const char *request,
				const char *code, int delivered)
{
	char esc_conv[80 * 6 + 1], esc_request[80 * 6 + 1], esc_code[128], ev[1280];

	if (!conversation || !request || !code ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(request, esc_request, sizeof(esc_request)) != 0 ||
	    omaq_json_escape(code, esc_code, sizeof(esc_code)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message.failed\",\"conversation\":\"%s\",\"request\":\"%s\",\"code\":\"%s\",\"delivered\":%s}",
		 esc_conv, esc_request, esc_code, delivered ? "true" : "false");
	emit(ev);
}

static void emit_file_rejected(const char *conversation, const char *request,
			       const char *code)
{
	char econv[80 * 6 + 1], erequest[80 * 6 + 1], ev[1200];
	const char *error_code = code && code[0] ? code : "file_failed";
	int wr;

	if (!conversation || !request ||
	    omaq_json_escape(conversation, econv, sizeof(econv)) != 0 ||
	    omaq_json_escape(request, erequest, sizeof(erequest)) != 0)
		return;
	wr = snprintf(ev, sizeof(ev),
		      "{\"event\":\"file.failed\",\"id\":\"\",\"conversation\":\"%s\",\"dir\":\"out\",\"request\":\"%s\",\"code\":\"%s\"}",
		      econv, erequest, error_code);
	if (wr >= 0 && (size_t)wr < sizeof(ev))
		emit(ev);
}

#ifdef HAVE_TOX
static void emit_message_update(const char *conversation, const char *id, const char *text, int deleted)
{
	char esc_conv[128], esc_id[128], esc_text[2800], ev[3200];

	if (!conversation || !id || !text ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0 ||
	    omaq_json_escape(text, esc_text, sizeof(esc_text)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message.updated\",\"conversation\":\"%s\",\"id\":\"%s\",\"text\":\"%s\",\"deleted\":%s,\"edited\":%s}",
		 esc_conv, esc_id, esc_text, deleted ? "true" : "false", deleted ? "false" : "true");
	emit(ev);
}

static void emit_message_reaction(const char *conversation, const char *id,
                                  const char *emoji, const char *actor)
{
	char esc_conv[128], esc_id[128], esc_emoji[128], ev[520];

	if (!conversation || !id || !emoji || !actor ||
	    (strcmp(actor, "me") != 0 && strcmp(actor, "peer") != 0) ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0 ||
	    omaq_json_escape(emoji, esc_emoji, sizeof(esc_emoji)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message.reaction\",\"conversation\":\"%s\",\"id\":\"%s\",\"emoji\":\"%s\",\"actor\":\"%s\"}",
		 esc_conv, esc_id, esc_emoji, actor);
	emit(ev);
}

static void emit_message_reaction_failed(const char *conversation, const char *id,
                                         const char *code)
{
	char esc_conv[128], esc_id[128], ev[520];

	if (!conversation || !id || !code ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message.reaction.failed\",\"conversation\":\"%s\",\"id\":\"%s\",\"code\":\"%s\"}",
		 esc_conv, esc_id, code);
	emit(ev);
}

static void emit_receipt_event_name(const char *event, const char *conversation,
				    const char *id, const char *state)
{
	char esc_conv[128], esc_id[128], ev[360];

	if (!event || (strcmp(event, "receipt") != 0 && strcmp(event, "receipt.sent") != 0) ||
	    !conversation || !id || !state ||
	    (strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0) ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"%s\",\"conversation\":\"%s\",\"id\":\"%s\",\"state\":\"%s\"}",
		 event, esc_conv, esc_id, state);
	emit(ev);
}

static void emit_receipt_event(const char *conversation, const char *id, const char *state)
{
	emit_receipt_event_name("receipt", conversation, id, state);
}

static void emit_receipt_failed(const char *conversation, const char *id,
				const char *state, const char *code)
{
	char esc_conv[128], esc_id[128], ev[420];
	const char *receipt_state = state &&
		(strcmp(state, "delivered") == 0 || strcmp(state, "read") == 0)
		? state : "invalid";
	const char *error_code = code && code[0] ? code : "forbidden";

	if (!conversation || !id || !id[0] ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"receipt.failed\",\"conversation\":\"%s\",\"id\":\"%s\",\"state\":\"%s\",\"code\":\"%s\"}",
		 esc_conv, esc_id, receipt_state, error_code);
	emit(ev);
}

#ifdef HAVE_SIGNAL
static int send_message_action_wire(uint32_t friend, const char *conversation,
				      const char *id, const char *text, int deleted)
{
	char plain[3200], wire[3600];

	if (!g_tox || !conversation || !id)
		return -1;
	if (deleted) {
		if (omaq_message_delete_wire_pack(plain, sizeof(plain), id) != 0)
			return -1;
	} else if (omaq_message_edit_wire_pack(plain, sizeof(plain), id, text) != 0) {
		return -1;
	}
	if (omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_message_reaction_wire(uint32_t friend, const char *conversation,
                                      const char *id, const char *emoji)
{
	char plain[256], wire[560];

	if (!g_tox || !g_ratchet || !conversation || !id ||
	    omaq_message_reaction_wire_pack(plain, sizeof(plain), id, emoji) != 0 ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_receipt_wire(uint32_t friend, const char *conversation,
			     const char *id, const char *state)
{
	char plain[180], wire[420];

	if (!g_tox || !g_ratchet || !conversation || !id ||
	    omaq_receipt_wire_pack(plain, sizeof(plain), id, state) != 0 ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}
#endif

static int send_message_action(uint32_t friend, const char *conversation,
			       const char *id, const char *text, int deleted)
{
	char plain[3200];

	if (!g_tox || !conversation || !id)
		return -1;
	if (conversation[0] == 'g') {
		if ((deleted && omaq_message_delete_wire_pack(plain, sizeof(plain), id) != 0) ||
		    (!deleted && omaq_message_edit_wire_pack(plain, sizeof(plain), id, text) != 0))
			return -1;
		return omaq_group_send(g_tox, conversation, plain);
	}
#ifdef HAVE_SIGNAL
	return send_message_action_wire(friend, conversation, id, text, deleted);
#else
	return -1;
#endif
}

static void clear_invite(void)
{
	g_issued_id[0] = '\0';
	g_issued_url[0] = '\0';
	g_issued_exp = 0;
	g_issued_is_group = 0;
	g_issued_group[0] = '\0';
	g_have_pending = 0;
#ifdef HAVE_SIGNAL
	g_pending_rk[0] = '\0';
	g_have_pending_rk = 0;
#endif
}

static void clear_group_auth(void)
{
	g_have_gauth = 0;
	g_gauth_friend = UINT32_MAX;
	g_gauth_exp = 0;
	g_gauth_group[0] = '\0';
}

static void reset_identity_runtime_state(void)
{
	clear_invite();
	clear_group_auth();
	g_have_gpending = 0;
	g_gpending_len = 0;
	g_gpending_announced = 0;
	memset(g_pending_pk, 0, sizeof(g_pending_pk));
	memset(g_file_requests, 0, sizeof(g_file_requests));
	g_file_request_sequence = 0;
	omaq_group_reset();
	omaq_file_reset();
}

#define IDENTITY_ARCHIVE_PATHS 8

typedef struct {
	char source[IDENTITY_ARCHIVE_PATHS][700];
	char archived[IDENTITY_ARCHIVE_PATHS][780];
	char aborted[IDENTITY_ARCHIVE_PATHS][780];
	int moved[IDENTITY_ARCHIVE_PATHS];
} identity_state_archive;

static int fsync_directory(const char *path)
{
	int fd, rc;

	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	rc = fsync(fd);
	close(fd);
	return rc;
}

static int identity_token_ok(const char *token)
{
	size_t i, n;

	if (!token)
		return 0;
	n = strlen(token);
	if (n == 0 || n > 80)
		return 0;
	for (i = 0; i < n; i++) {
		if (!((token[i] >= '0' && token[i] <= '9') ||
		      (token[i] >= 'a' && token[i] <= 'f') || token[i] == '-'))
			return 0;
	}
	return 1;
}

static int identity_backup_token_ok(const char *token)
{
	size_t i, n;

	if (!token)
		return 0;
	n = strlen(token);
	if (n < 34 || n > 80 || token[32] != '-')
		return 0;
	for (i = 0; i < 32; i++) {
		if (!((token[i] >= '0' && token[i] <= '9') ||
		      (token[i] >= 'a' && token[i] <= 'f')))
			return 0;
	}
	if (token[33] < '1' || token[33] > '9')
		return 0;
	for (i = 34; i < n; i++) {
		if (token[i] < '0' || token[i] > '9')
			return 0;
	}
	return 1;
}

static int prepare_identity_archive(identity_state_archive *archive, const char *token,
                                    const char *fingerprint)
{
	const char *home_names[] = { "history", "avatars", "files", "ratchet" };
	const char *state_names[] = {
		"surfaces.jsonl", "", "auto-open.json", "unread.tsv"
	};
	char auto_open_name[96];
	int i;

	if (!archive || !identity_token_ok(token) || !fingerprint ||
	    strlen(fingerprint) != 64 ||
	    snprintf(auto_open_name, sizeof(auto_open_name), "auto-open.%s.json",
		     fingerprint) >= (int)sizeof(auto_open_name))
		return -1;
	for (i = 0; i < 64; i++) {
		if (!((fingerprint[i] >= '0' && fingerprint[i] <= '9') ||
		      (fingerprint[i] >= 'a' && fingerprint[i] <= 'f')))
			return -1;
	}
	memset(archive, 0, sizeof(*archive));
	for (i = 0; i < IDENTITY_ARCHIVE_PATHS; i++) {
		const char *base = i < 4 ? home_dir() : state_dir();
		const char *name = i < 4 ? home_names[i] :
			(i == 5 ? auto_open_name : state_names[i - 4]);
		if (snprintf(archive->source[i], sizeof(archive->source[i]), "%s/%s",
			     base, name) >= (int)sizeof(archive->source[i]) ||
		    snprintf(archive->archived[i], sizeof(archive->archived[i]),
			     "%s/%s.before-identity-%s", base, name, token) >=
			     (int)sizeof(archive->archived[i]) ||
		    snprintf(archive->aborted[i], sizeof(archive->aborted[i]),
			     "%s/%s.aborted-identity-%s", base, name, token) >=
			     (int)sizeof(archive->aborted[i]))
			return -1;
	}
	return 0;
}

static int restore_identity_state(identity_state_archive *archive)
{
	int i, failed = 0;

	if (!archive)
		return -1;
	for (i = IDENTITY_ARCHIVE_PATHS - 1; i >= 0; i--) {
		struct stat st;
		int displaced = 0;
		if (!archive->moved[i])
			continue;
		if (lstat(archive->source[i], &st) == 0) {
			if (lstat(archive->aborted[i], &st) == 0 || errno != ENOENT ||
			    rename(archive->source[i], archive->aborted[i]) != 0) {
				failed = 1;
				continue;
			}
			displaced = 1;
		} else if (errno != ENOENT) {
			failed = 1;
			continue;
		}
		if (rename(archive->archived[i], archive->source[i]) != 0) {
			if (displaced)
				(void)rename(archive->aborted[i], archive->source[i]);
			failed = 1;
			continue;
		}
		archive->moved[i] = 0;
	}
	if (fsync_directory(home_dir()) != 0 || fsync_directory(state_dir()) != 0)
		failed = 1;
	return failed ? -1 : 0;
}

static int identity_archive_has_moved(const identity_state_archive *archive)
{
	int i;

	if (!archive)
		return 0;
	for (i = 0; i < IDENTITY_ARCHIVE_PATHS; i++) {
		if (archive->moved[i])
			return 1;
	}
	return 0;
}

static int archive_identity_state(identity_state_archive *archive, const char *token,
                                  const char *fingerprint)
{
	int i;
	struct stat st;

	if (prepare_identity_archive(archive, token, fingerprint) != 0)
		return -1;
	for (i = 0; i < IDENTITY_ARCHIVE_PATHS; i++) {
		if (lstat(archive->archived[i], &st) == 0 || errno != ENOENT)
			goto fail;
		if (rename(archive->source[i], archive->archived[i]) == 0)
			archive->moved[i] = 1;
		else if (errno != ENOENT)
			goto fail;
	}
	if (fsync_directory(home_dir()) != 0 || fsync_directory(state_dir()) != 0)
		goto fail;
	return 0;
fail:
	if (restore_identity_state(archive) != 0)
		return -2;
	return -1;
}

static int identity_marker_path(char *path, size_t path_size)
{
	return !path || snprintf(path, path_size, "%s/identity-replace.txn", state_dir()) >=
		(int)path_size ? -1 : 0;
}

static int write_identity_marker(const char *token, const char *fingerprint)
{
	char path[640];
	FILE *f;
	int rc = -1;

	if (!identity_token_ok(token) || !fingerprint || strlen(fingerprint) != 64 ||
	    identity_marker_path(path, sizeof(path)) != 0)
		return -1;
	f = fopen(path, "wx");
	if (!f)
		return -1;
	if (fchmod(fileno(f), 0600) != 0 ||
	    fprintf(f, "%s\n%s\n", token, fingerprint) <= 0 ||
	    fflush(f) != 0 || fsync(fileno(f)) != 0)
		goto marker_done;
	if (fclose(f) != 0) {
		f = NULL;
		goto marker_done;
	}
	f = NULL;
	if (fsync_directory(state_dir()) == 0)
		rc = 0;
marker_done:
	if (f)
		fclose(f);
	if (rc == 0)
		return 0;
	if (unlink(path) == 0) {
		if (fsync_directory(state_dir()) == 0)
			return -1;
		return -2;
	}
	return errno == ENOENT ? -1 : -2;
}

static int remove_identity_marker(void)
{
	char path[640];

	if (identity_marker_path(path, sizeof(path)) != 0 ||
	    (unlink(path) != 0 && errno != ENOENT) || fsync_directory(state_dir()) != 0)
		return -1;
	return 0;
}

static int cleanup_orphan_identity_stages(void)
{
	static const char prefix[] = "identity-import-stage-";
	DIR *dir;
	struct dirent *entry;
	int failed = 0;

	dir = opendir(state_dir());
	if (!dir)
		return -1;
	while ((entry = readdir(dir)) != NULL) {
		const char *suffix;
		char stage[700], save[760], save_tmp[780], save_tmp_old[780];
		struct stat st;
		size_t i;
		if (strncmp(entry->d_name, prefix, sizeof(prefix) - 1) != 0)
			continue;
		suffix = entry->d_name + sizeof(prefix) - 1;
		if (!suffix[0])
			continue;
		for (i = 0; suffix[i] >= '0' && suffix[i] <= '9'; i++)
			;
		if (suffix[i] != '\0' ||
		    snprintf(stage, sizeof(stage), "%s/%s", state_dir(), entry->d_name) >=
			    (int)sizeof(stage) ||
		    snprintf(save, sizeof(save), "%s/tox.save", stage) >= (int)sizeof(save) ||
		    snprintf(save_tmp, sizeof(save_tmp), "%s/tox.save.tmp.%s", stage, suffix) >=
			    (int)sizeof(save_tmp) ||
		    snprintf(save_tmp_old, sizeof(save_tmp_old), "%s/tox.save.tmp", stage) >=
			    (int)sizeof(save_tmp_old) || lstat(stage, &st) != 0 || !S_ISDIR(st.st_mode))
			continue;
		if ((unlink(save) != 0 && errno != ENOENT) ||
		    (unlink(save_tmp) != 0 && errno != ENOENT) ||
		    (unlink(save_tmp_old) != 0 && errno != ENOENT) ||
		    (rmdir(stage) != 0 && errno != ENOENT))
			failed = 1;
	}
	if (closedir(dir) != 0 || fsync_directory(state_dir()) != 0)
		failed = 1;
	return failed ? -1 : 0;
}

static int cleanup_orphan_identity_backups(void)
{
	static const char prefix[] = "tox.save.replace-backup.";
	DIR *dir;
	struct dirent *entry;
	int failed = 0;

	dir = opendir(home_dir());
	if (!dir)
		return -1;
	while ((entry = readdir(dir)) != NULL) {
		const char *token;
		char path[760];
		if (strncmp(entry->d_name, prefix, sizeof(prefix) - 1) != 0)
			continue;
		token = entry->d_name + sizeof(prefix) - 1;
		if (!identity_backup_token_ok(token))
			continue;
		if (snprintf(path, sizeof(path), "%s/%s", home_dir(), entry->d_name) >=
			    (int)sizeof(path) || unlink(path) != 0)
			failed = 1;
	}
	if (closedir(dir) != 0 || fsync_directory(home_dir()) != 0)
		failed = 1;
	return failed ? -1 : 0;
}

static int recover_identity_replacement(void)
{
	char marker[640], token[96], fingerprint[80], backup[700];
	identity_state_archive archive;
	struct stat st;
	FILE *f;
	int i;

	if (identity_marker_path(marker, sizeof(marker)) != 0)
		return -1;
	f = fopen(marker, "r");
	if (!f)
		return errno == ENOENT ? 0 : -1;
	if (!fgets(token, sizeof(token), f) || !strchr(token, '\n') ||
	    strchr(token, '\n')[1] != '\0' ||
	    !fgets(fingerprint, sizeof(fingerprint), f) || !strchr(fingerprint, '\n') ||
	    strchr(fingerprint, '\n')[1] != '\0' || fgetc(f) != EOF) {
		fclose(f);
		return -1;
	}
	if (fclose(f) != 0)
		return -1;
	token[strcspn(token, "\n")] = '\0';
	fingerprint[strcspn(fingerprint, "\n")] = '\0';
	if (!identity_token_ok(token) ||
	    snprintf(backup, sizeof(backup), "%s/tox.save.replace-backup.%s",
		     home_dir(), token) >= (int)sizeof(backup) ||
	    prepare_identity_archive(&archive, token, fingerprint) != 0)
		return -1;
	for (i = 0; i < IDENTITY_ARCHIVE_PATHS; i++) {
		if (lstat(archive.archived[i], &st) == 0)
			archive.moved[i] = 1;
		else if (errno != ENOENT)
			return -1;
	}
	if (omaq_identity_import(home_dir(), backup, 1) != 0 ||
	    restore_identity_state(&archive) != 0 || fsync_directory(home_dir()) != 0 ||
	    remove_identity_marker() != 0)
		return -1;
	if ((unlink(backup) != 0 && errno != ENOENT) || fsync_directory(home_dir()) != 0)
		g_identity_backup_cleanup_failed = 1;
	return 1;
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

static int friend_for_addr(const char *addr, uint32_t *out)
{
	uint32_t list[64];
	char pk[65];
	int n, i;

	if (!g_tox || !addr || strlen(addr) < 64 || !out)
		return -1;
	n = omaq_tox_friend_list(g_tox, list, 64);
	for (i = 0; i < n; i++) {
		if (omaq_tox_friend_pk_hex(g_tox, list[i], pk) == 0 &&
		    strncasecmp(pk, addr, 64) == 0) {
			*out = list[i];
			return 0;
		}
	}
	return -1;
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

static void sync_connection_state(void)
{
	char ev[96];
	int online;

	if (!g_tox)
		return;
	online = omaq_tox_online(g_tox) ? 1 : 0;
	if (online == g_connection_online)
		return;
	g_connection_online = online;
	if (omaq_presence_connection_event(ev, sizeof(ev), online) == 0)
		emit(ev);
}

static void emit_friends(void)
{
	uint32_t list[64];
	int n, i, wr, first = 1;
	char ev[OMAQ_JSON_LINE_MAX];
	char *p = ev;
	size_t left = sizeof(ev);

	if (!g_tox) {
		emit("{\"event\":\"friends\",\"items\":[]}");
		return;
	}
	n = omaq_tox_friend_list(g_tox, list, 64);
	wr = snprintf(p, left, "{\"event\":\"friends\",\"items\":[");
	if (wr < 0 || (size_t)wr >= left) {
		emit("{\"event\":\"friends\",\"items\":[]}");
		return;
	}
	p += wr;
	left -= (size_t)wr;
	for (i = 0; i < n; i++) {
		char name[129], esc[280], id[16], adest[512], apath[600];
		const char *on;

		if (omaq_tox_friend_name(g_tox, list[i], name, sizeof(name)) != 0)
			snprintf(name, sizeof(name), "Friend %u", list[i]);
		if (omaq_json_escape(name, esc, sizeof(esc)) != 0)
			continue;
		snprintf(id, sizeof(id), "%u", list[i]);
		on = omaq_tox_friend_online(g_tox, list[i]) ? "true" : "false";
		if (omaq_avatar_dest(home_dir(), id, adest, sizeof(adest)) == 0 &&
		    access(adest, R_OK) == 0 &&
		    omaq_json_escape(adest, apath, sizeof(apath)) == 0)
			wr = snprintf(p, left,
				      "%s{\"id\":\"%s\",\"name\":\"%s\",\"avatar\":\"%s\",\"online\":%s}",
				      first ? "" : ",", id, esc, apath, on);
		else
			wr = snprintf(p, left,
				      "%s{\"id\":\"%s\",\"name\":\"%s\",\"online\":%s}",
				      first ? "" : ",", id, esc, on);
		if (wr < 0 || (size_t)wr >= left)
			break;
		p += wr;
		left -= (size_t)wr;
		first = 0;
	}
	if (left < 3) {
		emit("{\"event\":\"friends\",\"items\":[]}");
		return;
	}
	memcpy(p, "]}", 3);
	emit(ev);
}

static void emit_avatar(const char *id, const char *path)
{
	char eid[32], epath[600], ev[700];

	if (!id || omaq_json_escape(id, eid, sizeof(eid)) != 0)
		return;
	if (path && path[0]) {
		if (omaq_json_escape(path, epath, sizeof(epath)) != 0)
			return;
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"avatar\",\"id\":\"%s\",\"path\":\"%s\"}", eid, epath);
	} else {
		snprintf(ev, sizeof(ev), "{\"event\":\"avatar\",\"id\":\"%s\",\"path\":\"\"}", eid);
	}
	emit(ev);
}

static void emit_self_avatar(void)
{
	char dest[512];

	if (omaq_avatar_dest(home_dir(), "self", dest, sizeof(dest)) == 0 &&
	    access(dest, R_OK) == 0)
		emit_avatar("self", dest);
	else
		emit_avatar("self", "");
}

static int avatar_hash_file(const char *path, uint8_t out[32])
{
	unsigned char *buf;
	FILE *f;
	size_t n;
	int rc;

	buf = malloc(OMAQ_AVATAR_MAX);
	if (!buf)
		return -1;
	f = fopen(path, "rb");
	if (!f) {
		free(buf);
		return -1;
	}
	n = fread(buf, 1, OMAQ_AVATAR_MAX, f);
	fclose(f);
	if (!n) {
		free(buf);
		return -1;
	}
	rc = omaq_tox_hash(buf, n, out);
	free(buf);
	return rc;
}

static void avatar_broadcast(const char *path)
{
	uint32_t list[64];
	uint8_t hid[32];
	int n, i;

	if (!g_tox || !path || !path[0])
		return;
	if (avatar_hash_file(path, hid) != 0)
		return;
	n = omaq_tox_friend_list(g_tox, list, 64);
	for (i = 0; i < n; i++)
		(void)omaq_file_send_avatar_begin(g_tox, list[i], path, hid, NULL);
}

static void hook_presence(void *ud, uint32_t friend, int online)
{
	char selfav[512];
	uint8_t hid[32];

	(void)ud;
	emit_friends();
	if (!online || !g_tox)
		return;
	if (omaq_avatar_dest(home_dir(), "self", selfav, sizeof(selfav)) == 0 &&
	    access(selfav, R_OK) == 0 &&
	    avatar_hash_file(selfav, hid) == 0)
		(void)omaq_file_send_avatar_begin(g_tox, friend, selfav, hid, NULL);
}

static void hook_typing(void *ud, uint32_t friend, int typing)
{
	char conv[16], ev[180];

	(void)ud;
	snprintf(conv, sizeof(conv), "%u", friend);
	if (omaq_presence_typing_event(ev, sizeof(ev), conv, typing) == 0)
		emit(ev);
}

static void hook_req(void *ud, const uint8_t *pk32, const char *msg)
{
	char key[65];
	const char *sep;
	int64_t now = (int64_t)time(NULL);

	(void)ud;
	pk_hex(pk32, key);
	if (omaq_rate_allow(&g_rate, key, now) != 0)
		return;
	if (!g_issued_id[0] || !msg)
		return;
	sep = strstr(msg, "|rk=");
	if (sep) {
#ifdef HAVE_SIGNAL
		if (g_issued_is_group || (size_t)(sep - msg) != strlen(g_issued_id) ||
		    strncmp(msg, g_issued_id, (size_t)(sep - msg)) != 0 ||
		    strlen(sep + 4) != OMAQ_RK_HEX || !omaq_rk_ok(sep + 4))
			return;
		snprintf(g_pending_rk, sizeof(g_pending_rk), "%s", sep + 4);
		g_have_pending_rk = 1;
#else
		return;
#endif
	} else {
		if (strcmp(msg, g_issued_id) != 0)
			return;
#ifdef HAVE_SIGNAL
		/* Direct Ratchet contacts must return their own identity pin. */
		if (!g_issued_is_group)
			return;
#endif
	}
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
	char key[32];
	int64_t now = (int64_t)time(NULL);

	(void)ud;
	if (!data || len == 0 || len > sizeof(g_gpending_data))
		return;
	snprintf(key, sizeof(key), "group:%u", friend);
	if (omaq_rate_allow(&g_rate, key, now) != 0 ||
	    (g_have_gpending && g_gpending_announced))
		return;
	memcpy(g_gpending_data, data, len);
	g_gpending_len = len;
	g_gpending_friend = friend;
	g_have_gpending = 1;
	g_gpending_announced = 0;
	if (g_have_gauth && omaq_group_invite_match(g_gauth_friend, friend, g_gauth_exp, now)) {
		g_gpending_announced = 1;
		emit("{\"event\":\"request\",\"kind\":\"group\"}");
	}
}

static void hook_gmsg(void *ud, uint32_t gnum, uint32_t peer, const char *text)
{
	char gid[OMAQ_GROUP_ID_MAX], peer_from[48], mid[64], wire_id[64], wire_reply[80], wire_text[1400];
	const char *display = text;
	(void)ud;
	(void)peer;
	wire_reply[0] = '\0';
	if (omaq_group_id_format(gnum, gid, sizeof(gid)) != 0 ||
	    snprintf(peer_from, sizeof(peer_from), "peer:%u", peer) >= (int)sizeof(peer_from))
		return;
	if (text && strncmp(text, "OQE1|", 5) == 0) {
		char action_id[80], action_text[1400];
		if (omaq_message_edit_wire_unpack(text, action_id, sizeof(action_id), action_text, sizeof(action_text)) == 0 &&
		    omaq_message_apply_edit_from(home_dir(), gid, action_id, action_text, peer_from) == 0) {
			emit_message_update(gid, action_id, action_text, 0);
			return;
		}
		return;
	}
	if (text && strncmp(text, "OQD1|", 5) == 0) {
		char action_id[80];
		if (omaq_message_delete_wire_unpack(text, action_id, sizeof(action_id)) == 0 &&
		    omaq_message_apply_delete_from(home_dir(), gid, action_id, peer_from) == 0) {
			emit_message_update(gid, action_id, "", 1);
			return;
		}
		return;
	}
	if (omaq_message_wire_unpack(text, wire_id, sizeof(wire_id), wire_reply, sizeof(wire_reply),
				     wire_text, sizeof(wire_text)) == 0) {
		display = wire_text;
		if (omaq_message_append_id_reply(home_dir(), gid, peer_from, display, "in", wire_id, wire_reply) != 0)
			return;
		snprintf(mid, sizeof(mid), "%s", wire_id);
	} else if (omaq_message_append_with_id(home_dir(), gid, peer_from, display, "in", mid, sizeof(mid)) != 0) {
		return;
	}
	(void)note_unread(gid);
	emit_message_event(gid, mid, wire_reply, display, "in");
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
	char conv[16], mid[64], wire_id[64], wire_reply[80], wire_text[1400];
	char receipt_id[64], receipt_state[16], reaction_id[80], reaction_emoji[32];
	const char *display = text;
	int has_wire_id = 0;
#ifdef HAVE_SIGNAL
	char plain[1400];
#endif
	(void)ud;
	snprintf(conv, sizeof(conv), "%u", friend);
#ifdef HAVE_SIGNAL
	if (text && strncmp(text, "OQB1", 4) == 0) {
		char expected[OMAQ_RK_HEX + 1];
		if (!g_ratchet)
			return;
		int had = omaq_ratchet_has_session(g_ratchet, conv);
		int pin = omaq_ratchet_pin_get(home_dir(), conv, expected, sizeof(expected));
		if (pin != 1)
			return;
		if (!had && omaq_ratchet_accept_bundle(g_ratchet, conv, text + 4, expected) != 0)
			return;
		if (!had && g_tox) {
			char bun[900], bmsg[920];
			if (omaq_ratchet_bundle(g_ratchet, bun, sizeof(bun)) == 0) {
				snprintf(bmsg, sizeof(bmsg), "OQB1%s", bun);
				(void)omaq_tox_send(g_tox, friend, bmsg);
			}
		}
		return;
	}
	if (!text || strncmp(text, "OQR1", 4) != 0 ||
	    !g_ratchet || omaq_ratchet_decrypt(g_ratchet, conv, text, plain, sizeof(plain)) != 0)
		return;
	text = plain;
#endif
	if (text && strncmp(text, "OQX1|", 5) == 0) {
		char reaction_rate_key[32];
		if (omaq_message_reaction_wire_unpack(text, reaction_id, sizeof(reaction_id),
					      reaction_emoji, sizeof(reaction_emoji)) != 0)
			return;
		snprintf(reaction_rate_key, sizeof(reaction_rate_key), "reaction:%u", friend);
		if (omaq_rate_allow_key_only(&g_reaction_rate, reaction_rate_key,
					     (int64_t)time(NULL)) != 0)
			return;
		if (omaq_store_update_reaction(home_dir(), conv, reaction_id,
					       reaction_emoji, "peer") == 0)
			emit_message_reaction(conv, reaction_id, reaction_emoji, "peer");
		return;
	}
	if (text && strncmp(text, "OQE1|", 5) == 0) {
		char action_id[80], action_text[1400];
		if (omaq_message_edit_wire_unpack(text, action_id, sizeof(action_id), action_text, sizeof(action_text)) == 0 &&
		    omaq_message_apply_edit(home_dir(), conv, action_id, action_text) == 0) {
			emit_message_update(conv, action_id, action_text, 0);
			return;
		}
		return;
	}
	if (text && strncmp(text, "OQD1|", 5) == 0) {
		char action_id[80];
		if (omaq_message_delete_wire_unpack(text, action_id, sizeof(action_id)) == 0 &&
		    omaq_message_apply_delete(home_dir(), conv, action_id) == 0) {
			emit_message_update(conv, action_id, "", 1);
			return;
		}
		return;
	}
	if (omaq_receipt_wire_unpack(text, receipt_id, sizeof(receipt_id),
				     receipt_state, sizeof(receipt_state)) == 0) {
		(void)omaq_store_update_receipt(home_dir(), conv, receipt_id, receipt_state);
		emit_receipt_event(conv, receipt_id, receipt_state);
		return;
	}
	wire_reply[0] = '\0';
	if (omaq_message_wire_unpack(text, wire_id, sizeof(wire_id), wire_reply, sizeof(wire_reply),
				     wire_text, sizeof(wire_text)) == 0) {
		display = wire_text;
		has_wire_id = 1;
	}
	if (has_wire_id) {
		if (omaq_message_append_id_reply(home_dir(), conv, "peer", display, "in", wire_id, wire_reply) != 0)
			return;
		snprintf(mid, sizeof(mid), "%s", wire_id);
	} else if (omaq_message_append_with_id(home_dir(), conv, "peer", display, "in", mid, sizeof(mid)) != 0) {
		return;
	}
	(void)note_unread(conv);
	emit_message_event(conv, mid, wire_reply, display, "in");
#ifdef HAVE_SIGNAL
	if (has_wire_id)
		(void)send_receipt_wire(friend, conv, wire_id, "delivered");
#endif
}

static void emit_file(const char *state, uint32_t friend, uint32_t fnum,
		      const char *name, uint64_t size, const char *path, const char *dir,
		      const char *request)
{
	char id[OMAQ_FILE_ID_MAX], conv[16], ev[OMAQ_JSON_STR_MAX * 6 + 1024];
	char ename[OMAQ_FILE_NAME_MAX * 6 + 1], epath[OMAQ_JSON_STR_MAX * 6 + 1];
	char erequest[80 * 6 + 1];
	int wr;

	if (omaq_file_id_format(friend, fnum, id, sizeof(id)) != 0 ||
	    (!dir || (strcmp(dir, "in") != 0 && strcmp(dir, "out") != 0)))
		return;
	snprintf(conv, sizeof(conv), "%u", friend);
	ename[0] = '\0';
	epath[0] = '\0';
	erequest[0] = '\0';
	if (name && omaq_json_escape(name, ename, sizeof(ename)) != 0)
		return;
	if (path && omaq_json_escape(path, epath, sizeof(epath)) != 0)
		return;
	if (request && omaq_json_escape(request, erequest, sizeof(erequest)) != 0)
		return;
	if (strcmp(state, "offer") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.offer\",\"id\":\"%s\",\"conversation\":\"%s\",\"name\":\"%s\",\"size\":%llu,\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, ename, (unsigned long long)size, dir, erequest);
	} else if (strcmp(state, "sending") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.sending\",\"id\":\"%s\",\"conversation\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, dir, erequest);
	} else if (strcmp(state, "done") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.done\",\"id\":\"%s\",\"conversation\":\"%s\",\"path\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, epath, dir, erequest);
	} else if (strcmp(state, "canceled") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.canceled\",\"id\":\"%s\",\"conversation\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, dir, erequest);
	} else if (strcmp(state, "failed") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.failed\",\"id\":\"%s\",\"conversation\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, dir, erequest);
	} else {
		return;
	}
	if (wr < 0 || (size_t)wr >= sizeof(ev))
		return;
	emit(ev);
}

static int file_request_find(const char *request)
{
	int i;

	if (!request || !request[0])
		return -1;
	for (i = 0; i < FILE_REQUEST_CACHE; i++) {
		if (g_file_requests[i].used &&
		    strcmp(g_file_requests[i].request, request) == 0)
			return i;
	}
	return -1;
}

static int file_request_begin(uint32_t friend, uint32_t fnum, const char *request)
{
	int i, slot = -1;
	uint64_t oldest = UINT64_MAX;

	if (!request || !request[0])
		return 0;
	i = file_request_find(request);
	if (i >= 0)
		return -1;
	for (i = 0; i < FILE_REQUEST_CACHE; i++) {
		if (!g_file_requests[i].used || g_file_requests[i].friend != friend ||
		    g_file_requests[i].fnum != fnum)
			continue;
		if (strcmp(g_file_requests[i].state, "sending") == 0)
			return -1;
		slot = i;
		break;
	}
	if (slot < 0) {
		for (i = 0; i < FILE_REQUEST_CACHE; i++) {
			if (!g_file_requests[i].used) {
				slot = i;
				break;
			}
			if (strcmp(g_file_requests[i].state, "sending") != 0 &&
			    g_file_requests[i].sequence < oldest) {
				oldest = g_file_requests[i].sequence;
				slot = i;
			}
		}
	}
	if (slot < 0)
		return -1;
	memset(&g_file_requests[slot], 0, sizeof(g_file_requests[slot]));
	g_file_requests[slot].used = 1;
	g_file_requests[slot].friend = friend;
	g_file_requests[slot].fnum = fnum;
	g_file_requests[slot].sequence = ++g_file_request_sequence;
	snprintf(g_file_requests[slot].request, sizeof(g_file_requests[slot].request),
		 "%s", request);
	snprintf(g_file_requests[slot].state, sizeof(g_file_requests[slot].state),
		 "sending");
	return 0;
}

static const char *file_request_finish(uint32_t friend, uint32_t fnum, const char *state)
{
	int i, slot = -1;
	uint64_t newest = 0;

	for (i = 0; i < FILE_REQUEST_CACHE; i++) {
		if (!g_file_requests[i].used || g_file_requests[i].friend != friend ||
		    g_file_requests[i].fnum != fnum ||
		    strcmp(g_file_requests[i].state, "sending") != 0)
			continue;
		if (slot < 0 || g_file_requests[i].sequence > newest) {
			slot = i;
			newest = g_file_requests[i].sequence;
		}
	}
	if (slot < 0)
		return NULL;
	snprintf(g_file_requests[slot].state, sizeof(g_file_requests[slot].state),
		 "%s", state);
	g_file_requests[slot].sequence = ++g_file_request_sequence;
	return g_file_requests[slot].request;
}

static void hook_file_recv(void *ud, uint32_t friend, uint32_t fnum,
			   const char *name, uint64_t size)
{
	(void)ud;
	if (omaq_file_offer_store(friend, fnum, name, size) != 0) {
		omaq_file_cancel(g_tox, friend, fnum);
		return;
	}
	emit_file("offer", friend, fnum, name, size, NULL, "in", NULL);
}

static void hook_file_creq(void *ud, uint32_t friend, uint32_t fnum, uint64_t pos, size_t len)
{
	int avatar = omaq_file_is_avatar(friend, fnum);
	omaq_file_event event;
	const char *request;

	(void)ud;
	if (omaq_file_chunk_out(g_tox, friend, fnum, pos, len) != 0) {
		event = omaq_file_event_for(avatar, OMAQ_FILE_OUTCOME_ERROR);
		omaq_file_cancel(g_tox, friend, fnum);
		request = file_request_finish(friend, fnum, "failed");
		if (event == OMAQ_FILE_EVENT_FAILED)
			emit_file("failed", friend, fnum, NULL, 0, NULL, "out", request);
		return;
	}
	event = omaq_file_event_for(avatar, OMAQ_FILE_OUTCOME_DONE);
	if (len == 0 && event == OMAQ_FILE_EVENT_DONE) {
		request = file_request_finish(friend, fnum, "done");
		emit_file("done", friend, fnum, NULL, 0, NULL, "out", request);
	}
}

static void hook_file_chunk(void *ud, uint32_t friend, uint32_t fnum, uint64_t pos,
			    const uint8_t *data, size_t len)
{
	char dest[512];
	int avatar;
	int rc;
	omaq_file_event event;

	(void)ud;
	avatar = omaq_file_is_avatar(friend, fnum);
	rc = omaq_file_chunk_in(friend, fnum, pos, data, len, dest, sizeof(dest));
	if (rc < 0) {
		event = omaq_file_event_for(avatar, OMAQ_FILE_OUTCOME_ERROR);
		omaq_file_cancel(g_tox, friend, fnum);
		if (event == OMAQ_FILE_EVENT_FAILED)
			emit_file("failed", friend, fnum, NULL, 0, NULL, "in", NULL);
		return;
	}
	if (rc == 1) {
		char conv[16], mid[64];
		int stored;
		event = omaq_file_event_for(avatar, OMAQ_FILE_OUTCOME_DONE);
		snprintf(conv, sizeof(conv), "%u", friend);
		if (event == OMAQ_FILE_EVENT_AVATAR) {
			emit_avatar(conv, dest);
			emit_friends();
			return;
		}
		stored = omaq_message_append_file_with_id(home_dir(), conv, "peer", dest, "in",
						  mid, sizeof(mid)) == 0;
		if (stored) {
			(void)note_unread(conv);
			emit_message_event_kind(conv, mid, "", dest, "in", "file", NULL);
		}
		emit_file("done", friend, fnum, NULL, 0, dest, "in", NULL);
		if (!stored)
			emit_error_conv("history_failed", conv);
	}
}

static void hook_avatar(void *ud, uint32_t friend, uint32_t fnum, uint64_t size)
{
	char dest[512], id[16], dir[512], got[512];

	(void)ud;
	snprintf(id, sizeof(id), "%u", friend);
	if (size == 0) {
		if (omaq_avatar_dest(home_dir(), id, dest, sizeof(dest)) == 0)
			unlink(dest);
		emit_avatar(id, "");
		(void)omaq_tox_file_control(g_tox, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		return;
	}
	if (size > OMAQ_AVATAR_MAX) {
		(void)omaq_tox_file_control(g_tox, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		return;
	}
	if (snprintf(dir, sizeof(dir), "%s/avatars", home_dir()) >= (int)sizeof(dir))
		return;
	(void)mkdir(dir, 0700);
	if (omaq_avatar_dest(home_dir(), id, dest, sizeof(dest)) != 0) {
		(void)omaq_tox_file_control(g_tox, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		return;
	}
	if (omaq_file_recv_begin(home_dir(), id, friend, fnum, "avatar.png", size,
				 dest, got, sizeof(got), 1) != 0) {
		(void)omaq_tox_file_control(g_tox, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		return;
	}
	(void)omaq_tox_file_control(g_tox, friend, fnum, OMAQ_TOX_FILE_RESUME);
}

static void hook_file_ctrl(void *ud, uint32_t friend, uint32_t fnum, int control)
{
	omaq_file_event event;
	int sending;
	const char *request;

	(void)ud;
	if (control != OMAQ_TOX_FILE_CANCEL)
		return;
	sending = omaq_file_is_sending(friend, fnum);
	event = omaq_file_event_for(omaq_file_is_avatar(friend, fnum),
				    OMAQ_FILE_OUTCOME_CANCEL);
	omaq_file_offer_drop(friend, fnum);
	omaq_file_cancel(g_tox, friend, fnum);
	request = sending ? file_request_finish(friend, fnum, "failed") : NULL;
	if (event == OMAQ_FILE_EVENT_FAILED)
		emit_file("failed", friend, fnum, NULL, 0, NULL,
			  sending ? "out" : "in", request);
}

static void hook_call(void *ud, uint32_t friend, int incoming)
{
	char conv[16], ev[160];

	(void)ud;
	snprintf(conv, sizeof(conv), "%u", friend);
	if (incoming) {
		omaq_av_note_incoming(friend);
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
	omaq_tox_set_presence_hook(g_tox, hook_presence, NULL);
	omaq_tox_set_typing_hook(g_tox, hook_typing, NULL);
	omaq_tox_set_group_hooks(g_tox, hook_ginv, hook_gmsg, hook_gpeer, NULL);
	omaq_tox_set_file_hooks(g_tox, hook_file_recv, hook_file_creq, hook_file_chunk,
				hook_file_ctrl, NULL);
	omaq_tox_set_avatar_hook(g_tox, hook_avatar, NULL);
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
	g_connection_online = -1;
	attach_hooks();
	return 0;
}
#endif

static int handle_op(const omaq_op *op, int *identity_ready)
{
#ifdef HAVE_TOX
	if (g_identity_recovery_required)
		return 0;
#endif
#ifdef OMAQ_IPC_TEST
	if (strcmp(op->op, "test.emit") == 0) {
		char *ev = malloc(OMAQ_IPC_TEST_EVENT_SIZE + 1u);
		int prefix;

		if (!ev) {
			emit_error("unsupported");
			return 0;
		}
		prefix = snprintf(ev, OMAQ_IPC_TEST_EVENT_SIZE + 1u,
				  "{\"event\":\"test\",\"id\":\"%s\",\"padding\":\"", op->id);
		if (prefix < 0 || (size_t)prefix + 2u > OMAQ_IPC_TEST_EVENT_SIZE) {
			free(ev);
			emit_error("unsupported");
			return 0;
		}
		memset(ev + prefix, 'x', OMAQ_IPC_TEST_EVENT_SIZE - (size_t)prefix - 2u);
		ev[OMAQ_IPC_TEST_EVENT_SIZE - 2u] = '"';
		ev[OMAQ_IPC_TEST_EVENT_SIZE - 1u] = '}';
		ev[OMAQ_IPC_TEST_EVENT_SIZE] = '\0';
		emit(ev);
		free(ev);
		return 0;
	}
#endif
	if (strcmp(op->op, "status") == 0) {
		char escaped_request[80 * 6 + 1], request_field[80 * 6 + 32];

		request_field[0] = '\0';
		if (op->id[0]) {
			if (omaq_json_escape(op->id, escaped_request, sizeof(escaped_request)) != 0)
				return 0;
			snprintf(request_field, sizeof(request_field),
				 ",\"request\":\"%s\"", escaped_request);
		}
#ifdef HAVE_TOX
		char addr[77], nickname[129], escaped_nickname[260];
		char ev[1024];
		if (g_locked && !g_tox) {
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"locked\":true,\"instance\":\"%s\"%s}",
				 OMAQ_PROTOCOL_VERSION, omaq_unread_total(&g_unread), g_instance_id,
				 request_field);
			emit(ev);
			emit_all_unread();
			return 0;
		}
		if (g_tox && omaq_tox_self_addr_hex(g_tox, addr) == 0) {
			if (omaq_tox_self_name(g_tox, nickname, sizeof(nickname)) != 0 ||
			    omaq_json_escape(nickname, escaped_nickname, sizeof(escaped_nickname)) != 0)
				escaped_nickname[0] = '\0';
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"online\":%s,\"addr\":\"%s\",\"nickname\":\"%s\",\"protected\":%s,\"instance\":\"%s\"%s}",
				 OMAQ_PROTOCOL_VERSION, omaq_unread_total(&g_unread),
				 omaq_tox_online(g_tox) ? "true" : "false", addr,
				 escaped_nickname, omaq_identity_protected(g_tox) ? "true" : "false",
				 g_instance_id, request_field);
			emit(ev);
			emit_friends();
			emit_self_avatar();
			emit_all_unread();
			return 0;
		}
#endif
		{
			char ev[640];
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"conversations\":[],\"instance\":\"%s\"%s}",
				 OMAQ_PROTOCOL_VERSION, omaq_unread_total(&g_unread), g_instance_id,
				 request_field);
			emit(ev);
			emit_all_unread();
		}
		return 0;
	}
	if (strcmp(op->op, "msg.send") == 0 && !op->id[0]) {
		emit_error_conv("request_required",
				op->conversation[0] ? op->conversation : "0");
		return 0;
	}
#ifdef HAVE_TOX
	if (strcmp(op->op, "identity.ready") == 0) {
		if (!identity_ready || !op->id[0] || strcmp(op->id, g_instance_id) != 0) {
			emit_error("identity_changed");
			return 0;
		}
		*identity_ready = 1;
		return 0;
	}
	if (g_identity_requires_ready && (!identity_ready || !*identity_ready)) {
		if (strcmp(op->op, "msg.send") == 0)
			emit_message_failed(op->conversation, op->id, "identity_changed", 0);
		else
			emit_error("identity_changed");
		return 0;
	}
	if (g_locked && !g_tox &&
	    strcmp(op->op, "identity.unlock") != 0) {
		if (strcmp(op->op, "file.send") == 0)
			emit_file_rejected(op->conversation, op->id, "locked");
		else if (strcmp(op->op, "msg.send") == 0)
			emit_message_failed(op->conversation, op->id, "locked", 0);
		else
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
				if (op->id[0] && !direct_id_ok(op->id)) {
					emit_error("unsupported");
					return 0;
				}
				if (op->id[0] &&
				    omaq_group_invite_friend(g_tox, op->group,
							     direct_id_number(op->id),
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
			if (!g_ratchet || omaq_ratchet_local_rk(g_ratchet, inv.rk) != 0) {
				emit_error("no_ratchet");
				return 0;
			}
#else
			emit_error("no_ratchet");
			return 0;
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
				uint32_t fn = UINT32_MAX, gnum;
				if (omaq_group_id_parse(inv.group, &gnum) != 0 ||
				    strlen(inv.group) >= sizeof(g_gauth_group)) {
					emit_error("unsupported");
					return 0;
				}
				if (omaq_tox_friend_add(g_tox, inv.tox_addr, inv.id, &fn) != 0 &&
				    friend_for_addr(inv.tox_addr, &fn) != 0) {
					emit_error("forbidden");
					return 0;
				}
				g_have_gauth = 1;
				g_gauth_friend = fn;
				g_gauth_exp = inv.expiry;
				memcpy(g_gauth_group, inv.group, strlen(inv.group) + 1);
				if (g_have_gpending && !g_gpending_announced &&
				    omaq_group_invite_match(g_gauth_friend, g_gpending_friend,
								g_gauth_exp, (int64_t)time(NULL))) {
					g_gpending_announced = 1;
					emit("{\"event\":\"request\",\"kind\":\"group\"}");
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
			char request[OMAQ_INVITE_ID_MAX + OMAQ_RK_HEX + 8];
#ifdef HAVE_SIGNAL
			char local_rk[OMAQ_RK_HEX + 1];
			if (!g_ratchet || !inv.rk[0] ||
			    omaq_ratchet_local_rk(g_ratchet, local_rk) != 0) {
				emit_error("no_ratchet");
				return 0;
			}
			if (snprintf(request, sizeof(request), "%s|rk=%s", inv.id, local_rk) >=
			    (int)sizeof(request)) {
				emit_error("unsupported");
				return 0;
			}
#else
			emit_error("no_ratchet");
			return 0;
#endif
			if (omaq_tox_friend_add(g_tox, inv.tox_addr, request, &fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
#ifdef HAVE_SIGNAL
			{
				char conv[16];
				snprintf(conv, sizeof(conv), "%u", fn);
				if (omaq_ratchet_pin_set(home_dir(), conv, inv.rk) != 0) {
					(void)omaq_tox_friend_delete(g_tox, fn);
					emit_error("forbidden");
					return 0;
				}
			}
#endif
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			emit_friends();
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
		if (g_tox && g_have_gpending && g_gpending_announced && g_have_gauth) {
			if (op->has_accept && op->accept) {
				uint32_t gnum = UINT32_MAX;
				char gid[OMAQ_GROUP_ID_MAX] = "";
				int accepted = omaq_tox_group_invite_accept(g_tox, g_gpending_friend,
									g_gpending_data, g_gpending_len,
									&gnum) == 0;
				int have_gid = accepted && omaq_group_id_format(gnum, gid, sizeof(gid)) == 0;
				if (!have_gid || !g_have_gauth || strcmp(gid, g_gauth_group) != 0) {
					if (have_gid)
						(void)omaq_group_leave(g_tox, gid);
					g_have_gpending = 0;
					g_gpending_announced = 0;
					clear_group_auth();
					emit_error("forbidden");
					return 0;
				}
				g_have_gpending = 0;
				g_gpending_announced = 0;
				clear_group_auth();
				emit_group(gid, "join", 0);
				return 0;
			}
			g_have_gpending = 0;
			g_gpending_announced = 0;
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
#ifdef HAVE_SIGNAL
				if (!g_issued_is_group && g_have_pending_rk) {
					char pin_conv[16];
					if (fn == UINT32_MAX ||
					    snprintf(pin_conv, sizeof(pin_conv), "%u", fn) >=
					    (int)sizeof(pin_conv) ||
					    omaq_ratchet_pin_set(home_dir(), pin_conv, g_pending_rk) != 0) {
						if (fn != UINT32_MAX)
							(void)omaq_tox_friend_delete(g_tox, fn);
						clear_invite();
						emit_error("forbidden");
						return 0;
					}
				}
#endif
				if (g_issued_is_group && g_issued_group[0] && fn != UINT32_MAX)
					(void)omaq_group_invite_friend(g_tox, g_issued_group, fn,
								       ROLE_OWNER, g_issued_grole);
				clear_invite();
				emit("{\"event\":\"snapshot\",\"unread\":0}");
				emit_friends();
				if (fn != UINT32_MAX) {
					char selfav[512];
					uint8_t hid[32];

					if (omaq_avatar_dest(home_dir(), "self", selfav, sizeof(selfav)) == 0 &&
					    access(selfav, R_OK) == 0 &&
					    avatar_hash_file(selfav, hid) == 0)
						(void)omaq_file_send_avatar_begin(g_tox, fn, selfav, hid, NULL);
				}
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
			uint32_t fn;
			if (!direct_id_ok(cid)) {
				emit_error("unsupported");
				return 0;
			}
			fn = direct_id_number(cid);
			if (omaq_tox_friend_delete(g_tox, fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
			if (clear_unread(cid) != 0)
				emit_unread_failed(cid, "unread_persist_failed");
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			emit_friends();
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "nickname.set") == 0) {
#ifdef HAVE_TOX
		char escaped[260], ev[340];

		if (!g_tox || omaq_tox_set_name(g_tox, op->nickname) != 0) {
			emit_error("nickname_invalid");
			return 0;
		}
		if (omaq_json_escape(op->nickname, escaped, sizeof(escaped)) != 0) {
			emit_error("nickname_invalid");
			return 0;
		}
		snprintf(ev, sizeof(ev), "{\"event\":\"nickname\",\"value\":\"%s\"}", escaped);
		emit(ev);
		return 0;
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "avatar.set") == 0) {
#ifdef HAVE_TOX
		char dest[512];

		if (!g_tox) {
			emit_error("unsupported");
			return 0;
		}
		if (omaq_avatar_install(home_dir(), "self", op->path, dest, sizeof(dest)) != 0) {
			emit_error("avatar_failed");
			return 0;
		}
		emit_avatar("self", dest);
		avatar_broadcast(dest);
		return 0;
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
			if (omaq_group_self_role(g_tox, gid, &self) != 0) {
				emit_error("forbidden");
				return 0;
			}
			if (omaq_group_dissolve(g_tox, gid, self) != 0) {
				emit_error("forbidden");
				return 0;
			}
			if (clear_unread(gid) != 0)
				emit_unread_failed(gid, "unread_persist_failed");
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
			uint32_t peer;
			if (!decimal_u32(op->member[0] ? op->member : (op->id[0] ? op->id : "0"), &peer)) {
				emit_error("forbidden");
				return 0;
			}
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
			uint32_t peer;
			if (!decimal_u32(op->member[0] ? op->member : (op->id[0] ? op->id : "0"), &peer)) {
				emit_error("forbidden");
				return 0;
			}
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
			if (clear_unread(gid) != 0)
				emit_unread_failed(gid, "unread_persist_failed");
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
	if (strcmp(op->op, "surface.list") == 0) {
		omaq_surface surfaces[OMAQ_SURFACE_MAX];
		int n = omaq_surface_list(state_dir(), surfaces, OMAQ_SURFACE_MAX);
		size_t cap = 64u + (size_t)(n > 0 ? n : 0) * 260u;
		char *ev = malloc(cap);
		char *p;
		size_t left;
		int i, first = 1;
		if (n < 0 || !ev) {
			free(ev);
			emit("{\"event\":\"surfaces\",\"items\":[]}");
			return 0;
		}
		p = ev;
		left = cap;
		if (snprintf(p, left, "{\"event\":\"surfaces\",\"items\":[") >= (int)left) {
			free(ev);
			emit("{\"event\":\"surfaces\",\"items\":[]}");
			return 0;
		}
		p += strlen(p);
		left = cap - (size_t)(p - ev);
		for (i = 0; i < n; i++) {
			char ec[160], em[128];
			int wr;
			if (omaq_json_escape(surfaces[i].conversation, ec, sizeof(ec)) != 0 ||
			    omaq_json_escape(surfaces[i].monitor, em, sizeof(em)) != 0)
				continue;
			wr = snprintf(p, left, "%s{\"conversation\":\"%s\",\"monitor\":\"%s\",\"x\":%d,\"y\":%d,\"pinned\":%s}",
				      first ? "" : ",", ec, em, surfaces[i].x, surfaces[i].y,
				      surfaces[i].pinned ? "true" : "false");
			if (wr < 0 || (size_t)wr >= left)
				break;
			p += wr;
			left -= (size_t)wr;
			first = 0;
		}
		if (left < 3) {
			free(ev);
			emit("{\"event\":\"surfaces\",\"items\":[]}");
			return 0;
		}
		memcpy(p, "]}", 3);
		emit(ev);
		free(ev);
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
			if (!direct_id_ok(cid)) {
				emit_error("forbidden");
				return 0;
			}
			emit_safety(direct_id_number(cid));
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "settings.auto-open.migrated") == 0) {
#ifdef HAVE_TOX
		char address[77], source[640], migrated[760];
		struct stat st;
		if (!g_tox || strlen(op->id) != 64 ||
		    omaq_tox_self_addr_hex(g_tox, address) != 0 ||
		    strncmp(address, op->id, 64) != 0 ||
		    snprintf(source, sizeof(source), "%s/auto-open.json", state_dir()) >=
			    (int)sizeof(source) ||
		    snprintf(migrated, sizeof(migrated), "%s/auto-open.migrated.%s.json",
			     state_dir(), op->id) >= (int)sizeof(migrated)) {
			emit_error("forbidden");
			return 0;
		}
		if (lstat(source, &st) != 0) {
			if (errno != ENOENT)
				emit_error("forbidden");
			return 0;
		}
		if (lstat(migrated, &st) == 0) {
			if (unlink(source) != 0 || fsync_directory(state_dir()) != 0)
				emit_error("forbidden");
			return 0;
		}
		if (errno != ENOENT || rename(source, migrated) != 0 ||
		    fsync_directory(state_dir()) != 0)
			emit_error("forbidden");
		return 0;
#else
		emit_error("unsupported");
		return 0;
#endif
	}
	if (strcmp(op->op, "typing.set") == 0) {
#ifdef HAVE_TOX
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (!g_tox || !direct_id_ok(cid) || !op->has_typing ||
		    omaq_tox_set_typing(g_tox, direct_id_number(cid), op->typing) != 0) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
		return 0;
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "message.react") == 0) {
#ifdef HAVE_TOX
#ifdef HAVE_SIGNAL
		const char *cid = op->conversation[0] ? op->conversation : "0";
		uint32_t fn;
		char reaction_rate_key[32];
		int reaction_rc;
		if (!g_tox || !direct_id_ok(cid) || !op->id[0] || !op->has_text ||
		    !omaq_message_reaction_ok(op->text)) {
			emit_message_reaction_failed(cid, op->id, "invalid");
			return 0;
		}
		fn = direct_id_number(cid);
		if (!omaq_tox_online(g_tox) || !omaq_tox_friend_online(g_tox, fn)) {
			emit_message_reaction_failed(cid, op->id, "offline");
			return 0;
		}
		if (!g_ratchet || !omaq_ratchet_has_session(g_ratchet, cid)) {
			emit_message_reaction_failed(cid, op->id, "ratchet_pending");
			return 0;
		}
		if (omaq_store_message_exists(home_dir(), cid, op->id) != 1) {
			emit_message_reaction_failed(cid, op->id, "not_found");
			return 0;
		}
		snprintf(reaction_rate_key, sizeof(reaction_rate_key), "reaction:%u", fn);
		if (omaq_rate_allow_key_only(&g_reaction_out_rate, reaction_rate_key,
					     (int64_t)time(NULL)) != 0) {
			emit_message_reaction_failed(cid, op->id, "rate_limited");
			return 0;
		}
		reaction_rc = send_message_reaction_wire(fn, cid, op->id, op->text);
		if (reaction_rc != 0) {
			emit_message_reaction_failed(cid, op->id,
						    reaction_rc == -2 ? "offline" : "forbidden");
			return 0;
		}
		if (omaq_store_update_reaction(home_dir(), cid, op->id, op->text, "me") != 0) {
			emit_message_reaction(cid, op->id, op->text, "me");
			emit_message_reaction_failed(cid, op->id, "history_failed");
			return 0;
		}
		emit_message_reaction(cid, op->id, op->text, "me");
		return 0;
#else
		emit_message_reaction_failed(op->conversation[0] ? op->conversation : "0",
					     op->id, "no_ratchet");
		return 0;
#endif
#else
		emit_error("unsupported");
		return 0;
#endif
	}
	if (strcmp(op->op, "message.edit") == 0 || strcmp(op->op, "message.delete") == 0) {
#ifdef HAVE_TOX
		const char *cid = op->conversation[0] ? op->conversation : "0";
		int deleted = strcmp(op->op, "message.delete") == 0;
		uint32_t fn;
		int update;
		if (!op->id[0] || (!deleted && !op->text[0])) {
			emit_error_conv("invalid", cid);
			return 0;
		}
		if (cid[0] == 'g') {
			int action_rc = send_message_action(UINT32_MAX, cid, op->id, deleted ? "" : op->text, deleted);
			if (action_rc != 0) {
				emit_error_conv(action_rc == -2 ? "offline" : "forbidden", cid);
				return 0;
			}
		} else {
			if (!direct_id_ok(cid)) {
				emit_error_conv("unsupported", cid);
				return 0;
			}
			fn = direct_id_number(cid);
			if (!omaq_tox_online(g_tox) || !omaq_tox_friend_online(g_tox, fn)) {
				emit_error_conv("offline", cid);
				return 0;
			}
#ifndef HAVE_SIGNAL
			emit_error_conv("no_ratchet", cid);
			return 0;
#else
			if (!g_ratchet || !omaq_ratchet_has_session(g_ratchet, cid)) {
				emit_error_conv("ratchet_pending", cid);
				return 0;
			}
			{
				int action_rc = send_message_action(fn, cid, op->id, deleted ? "" : op->text, deleted);
				if (action_rc != 0) {
					emit_error_conv(action_rc == -2 ? "offline" : "forbidden", cid);
					return 0;
				}
			}
#endif
		}
		update = deleted ? omaq_message_delete(home_dir(), cid, op->id) :
			omaq_message_edit(home_dir(), cid, op->id, op->text);
		if (update != 0) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
		emit_message_update(cid, op->id, deleted ? "" : op->text, deleted);
		emit("{\"event\":\"snapshot\",\"unread\":0}");
		return 0;
#else
		emit_error("unsupported");
		return 0;
#endif
	}
	if (strcmp(op->op, "receipt.send") == 0) {
#ifdef HAVE_TOX
#ifdef HAVE_SIGNAL
		const char *cid = op->conversation[0] ? op->conversation : "0";
		uint32_t fn;
		if (!g_tox || !direct_id_ok(cid) || !op->id[0] || !op->state[0]) {
			emit_receipt_failed(cid, op->id, op->state, "forbidden");
			return 0;
		}
		fn = direct_id_number(cid);
		if (!omaq_tox_online(g_tox) || !omaq_tox_friend_online(g_tox, fn)) {
			emit_receipt_failed(cid, op->id, op->state, "offline");
			return 0;
		}
		{
			int receipt_rc = send_receipt_wire(fn, cid, op->id, op->state);
			if (receipt_rc != 0) {
				emit_receipt_failed(cid, op->id, op->state,
						    receipt_rc == -2 ? "offline" : "forbidden");
				return 0;
			}
		}
		emit_receipt_event_name("receipt.sent", cid, op->id, op->state);
		return 0;
#else
		emit_receipt_failed(op->conversation[0] ? op->conversation : "0",
				    op->id, op->state, "no_ratchet");
		return 0;
#endif
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "msg.send") == 0) {
#ifdef HAVE_TOX
		if (g_tox && op->text[0]) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			if (cid[0] == 'g') {
				char mid[64], packed[3200];
				int group_rc;
				if (omaq_message_id_new(mid, sizeof(mid)) != 0 ||
				    omaq_message_wire_pack(packed, sizeof(packed), mid, op->reply, op->text) != 0) {
					emit_message_failed(cid, op->id, "forbidden", 0);
					return 0;
				}
				group_rc = omaq_group_send(g_tox, cid, packed);
				if (group_rc != 0) {
					emit_message_failed(cid, op->id,
						group_rc == -2 ? "offline" : "forbidden", 0);
					return 0;
				}
				if (omaq_message_append_id_reply(home_dir(), cid, "me", op->text, "out", mid, op->reply) != 0) {
					emit_message_event_request(cid, mid, op->reply, op->text, op->id);
					emit_message_failed(cid, op->id, "history_failed", 1);
					return 0;
				}
				emit_message_event_request(cid, mid, op->reply, op->text, op->id);
				emit("{\"event\":\"snapshot\",\"unread\":0}");
				return 0;
			} else {
				uint32_t fn;
				if (!direct_id_ok(cid)) {
					emit_message_failed(cid, op->id, "unsupported", 0);
					return 0;
				}
				fn = direct_id_number(cid);
				if (!omaq_tox_online(g_tox) || !omaq_tox_friend_online(g_tox, fn)) {
					emit_message_failed(cid, op->id, "offline", 0);
					return 0;
				}
#ifdef HAVE_SIGNAL
				if (!g_ratchet) {
					emit_message_failed(cid, op->id, "no_ratchet", 0);
					return 0;
				}
				{
					char bun[900], packed[3200], wire[3600], mid[64];
					if (!omaq_ratchet_has_session(g_ratchet, cid)) {
						char bmsg[920];
						if (omaq_ratchet_bundle(g_ratchet, bun, sizeof(bun)) != 0) {
							emit_message_failed(cid, op->id, "no_ratchet", 0);
							return 0;
						}
						snprintf(bmsg, sizeof(bmsg), "OQB1%s", bun);
						{
							int send_rc = omaq_tox_send(g_tox, fn, bmsg);
							if (send_rc != 0) {
								emit_message_failed(cid, op->id,
									send_rc == -2 ? "offline" : "forbidden", 0);
								return 0;
							}
						}
						emit_message_failed(cid, op->id, "ratchet_pending", 0);
						return 0;
					}
					if (omaq_message_id_new(mid, sizeof(mid)) != 0 ||
					    omaq_message_wire_pack(packed, sizeof(packed), mid, op->reply, op->text) != 0 ||
					    omaq_ratchet_encrypt(g_ratchet, cid, packed,
								wire, sizeof(wire)) != 0) {
						emit_message_failed(cid, op->id, "forbidden", 0);
						return 0;
					}
					{
						int send_rc = omaq_tox_send(g_tox, fn, wire);
						if (send_rc != 0) {
							emit_message_failed(cid, op->id,
								send_rc == -2 ? "offline" : "forbidden", 0);
							return 0;
						}
					}
					{
						if (omaq_message_append_id_reply(home_dir(), cid, "me", op->text, "out", mid, op->reply) != 0) {
							emit_message_event_request(cid, mid, op->reply, op->text, op->id);
							emit_message_failed(cid, op->id, "history_failed", 1);
							return 0;
						}
						emit_message_event_request(cid, mid, op->reply, op->text, op->id);
					}
					emit("{\"event\":\"snapshot\",\"unread\":0}");
					return 0;
				}
#endif
#ifndef HAVE_SIGNAL
				emit_message_failed(cid, op->id, "no_ratchet", 0);
				return 0;
#endif
			}
			{
				char mid[64];
				if (omaq_message_append_with_id(home_dir(), cid, "me", op->text, "out", mid, sizeof(mid)) != 0) {
					emit_message_failed(cid, op->id, "history_failed", 0);
					return 0;
				}
				emit_message_event_request(cid, mid, op->reply, op->text, op->id);
			}
			emit("{\"event\":\"snapshot\",\"unread\":0}");
			return 0;
		}
#endif
		emit_message_failed(op->conversation[0] ? op->conversation : "0",
				    op->id, "unsupported", 0);
		return 0;
	}
	if (strcmp(op->op, "unread.clear") == 0) {
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (!conversation_id_ok(cid) || clear_unread(cid) != 0)
			emit_unread_failed(cid, "unread_persist_failed");
		return 0;
	}
	if (strcmp(op->op, "history.clear") == 0) {
		const char *cid = op->conversation[0] ? op->conversation : "0";
		char esc_cid[128], ev[192];
		if (!conversation_id_ok(cid) || omaq_store_clear(home_dir(), cid) != 0) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
		if (clear_unread(cid) != 0)
			emit_unread_failed(cid, "unread_persist_failed");
		if (omaq_json_escape(cid, esc_cid, sizeof(esc_cid)) != 0)
			emit("{\"event\":\"history\",\"conversation\":\"0\",\"cleared\":true,\"items\":[]}");
		else {
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"history\",\"conversation\":\"%s\",\"cleared\":true,\"items\":[]}",
				 esc_cid);
			emit(ev);
		}
		return 0;
	}
	if (strcmp(op->op, "history") == 0) {
		char *out = NULL;
		size_t n = 0;
		int lim = op->has_limit ? op->limit : 50;
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (omaq_message_history(home_dir(), cid, lim, &out, &n) == 0 && out) {
			emit_json_items("history", cid, out, n, op->id);
			free(out);
			return 0;
		}
		emit_json_items("history", cid, "", 0, op->id);
		return 0;
	}
	if (strcmp(op->op, "search") == 0) {
		char *out = NULL;
		size_t n = 0;
		int lim = op->has_limit ? op->limit : 20;
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (!op->text[0]) {
			char esc_cid[128];
			if (omaq_json_escape(cid, esc_cid, sizeof(esc_cid)) == 0) {
				char empty[220];
				snprintf(empty, sizeof(empty), "{\"event\":\"search\",\"conversation\":\"%s\",\"items\":[]}", esc_cid);
				emit(empty);
			} else {
				emit("{\"event\":\"search\",\"conversation\":\"0\",\"items\":[]}");
			}
			return 0;
		}
		if (omaq_message_search(home_dir(), cid, op->text, lim, &out, &n) == 0 && out) {
			emit_json_items("search", cid, out, n, NULL);
			free(out);
			return 0;
		}
		{
			char esc_cid[128], empty[220];
			if (omaq_json_escape(cid, esc_cid, sizeof(esc_cid)) == 0) {
				snprintf(empty, sizeof(empty), "{\"event\":\"search\",\"conversation\":\"%s\",\"items\":[]}", esc_cid);
				emit(empty);
			} else {
				emit("{\"event\":\"search\",\"conversation\":\"0\",\"items\":[]}");
			}
		}
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

			if (!direct_id_ok(cid)) {
				emit_file_rejected(cid, op->id, "forbidden");
				return 0;
			}
			fn = direct_id_number(cid);
			if (!op->path[0] || omaq_file_basename(op->path, name, sizeof(name)) != 0) {
				emit_file_rejected(cid, op->id, "unsupported");
				return 0;
			}
			if (omaq_file_send_begin(g_tox, fn, op->path, &fnum) != 0) {
				emit_file_rejected(cid, op->id, "forbidden");
				return 0;
			}
			if (file_request_begin(fn, fnum, op->id) != 0) {
				omaq_file_cancel(g_tox, fn, fnum);
				emit_file_rejected(cid, op->id, "busy");
				return 0;
			}
			(void)name;
			emit_file("sending", fn, fnum, NULL, 0, NULL, "out", op->id);
			return 0;
		}
#endif
		emit_file_rejected(op->conversation[0] ? op->conversation : "0",
				   op->id, "unsupported");
		return 0;
	}
	if (strcmp(op->op, "file.status") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			int request_index;
			uint32_t fn;

			if (!direct_id_ok(cid) || !op->id[0]) {
				emit_file_rejected(cid, op->id, "forbidden");
				return 0;
			}
			fn = direct_id_number(cid);
			request_index = file_request_find(op->id);
			if (request_index < 0 || g_file_requests[request_index].friend != fn) {
				emit_file_rejected(cid, op->id, "transfer_unknown");
				return 0;
			}
			emit_file(g_file_requests[request_index].state,
				  g_file_requests[request_index].friend,
				  g_file_requests[request_index].fnum,
				  NULL, 0, NULL, "out",
				  g_file_requests[request_index].request);
			return 0;
		}
#endif
		emit_file_rejected(op->conversation[0] ? op->conversation : "0",
				   op->id, "unsupported");
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
						 over, dest, sizeof(dest), 0) != 0) {
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
			if (!omaq_file_can_cancel(fn, fnum)) {
				emit_error("forbidden");
				return 0;
			}
			if (omaq_file_is_avatar(fn, fnum)) {
				omaq_file_cancel(g_tox, fn, fnum);
				return 0;
			}
			{
				int sending = omaq_file_is_sending(fn, fnum);
				const char *request;
				omaq_file_cancel(g_tox, fn, fnum);
				request = sending ? file_request_finish(fn, fnum, "canceled") : NULL;
				emit_file("canceled", fn, fnum, NULL, 0, NULL,
					  sending ? "out" : "in", request);
			}
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

			if (!direct_id_ok(cid)) {
				emit_error("forbidden");
				return 0;
			}
			fn = direct_id_number(cid);
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

			if (!direct_id_ok(cid)) {
				emit_error("forbidden");
				return 0;
			}
			fn = direct_id_number(cid);
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

			if (!direct_id_ok(cid)) {
				emit_error("forbidden");
				return 0;
			}
			fn = direct_id_number(cid);
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
		int replacing = op->has_replace && op->replace;
		if (!op->path[0]) {
			emit_error("unsupported");
			return 0;
		}
#ifndef HAVE_TOX
		{
			int import_rc = omaq_identity_import(home_dir(), op->path, replacing);
			if (import_rc == 1)
				emit_error("identity_exists");
			else if (import_rc != 0)
				emit_error("identity_import_failed");
			else
				emit("{\"event\":\"identity\",\"op\":\"import\"}");
			return 0;
		}
#else
		{
			char stage[640], stage_save[700], token[96], backup[700];
			char old_address[77], old_fingerprint[65];
			struct omaq_tox *candidate;
			identity_state_archive identity_archive;
			const char *failure_code = "identity_import_failed";
			int candidate_error = 0, swapped = 0, archived = 0, marker = 0;
			int unread_reset = 0;
			int archive_rc, marker_rc, rollback_load = 0, rollback_ok = 1;

			backup[0] = '\0';
			if (!replacing) {
				emit_error("identity_exists");
				return 0;
			}
			if (omaq_av_busy() || omaq_file_busy()) {
				emit_error("busy");
				return 0;
			}
			if (snprintf(stage, sizeof(stage), "%s/identity-import-stage-%ld",
				     state_dir(), (long)getpid()) >= (int)sizeof(stage) ||
			    snprintf(stage_save, sizeof(stage_save), "%s/tox.save", stage) >=
				     (int)sizeof(stage_save) ||
			    (mkdir(stage, 0700) != 0 && errno != EEXIST) ||
			    omaq_identity_import(stage, op->path, 1) != 0) {
				emit_error("identity_import_failed");
				return 0;
			}
			candidate = omaq_identity_load(stage, op->passphrase, &candidate_error);
			if (!candidate) {
				unlink(stage_save);
				rmdir(stage);
				emit_error(candidate_error == OMAQ_TOX_LOCKED && !op->passphrase[0]
					   ? "identity_passphrase_required" : "identity_import_failed");
				return 0;
			}
			omaq_tox_discard(candidate);
			if (!g_tox || omaq_tox_self_addr_hex(g_tox, old_address) != 0) {
				unlink(stage_save);
				rmdir(stage);
				emit_error("identity_backup_failed");
				return 0;
			}
			memcpy(old_fingerprint, old_address, 64);
			old_fingerprint[64] = '\0';
			g_identity_backup_sequence++;
			if (snprintf(token, sizeof(token), "%s-%llu", g_instance_id,
				     (unsigned long long)g_identity_backup_sequence) >= (int)sizeof(token) ||
			    snprintf(backup, sizeof(backup), "%s/tox.save.replace-backup.%s",
				     home_dir(), token) >= (int)sizeof(backup) ||
			    omaq_identity_export_exclusive(home_dir(), backup) != 0 ||
			    fsync_directory(home_dir()) != 0) {
				if (backup[0])
					unlink(backup);
				(void)fsync_directory(home_dir());
				unlink(stage_save);
				rmdir(stage);
				emit_error("identity_backup_failed");
				return 0;
			}
			marker_rc = write_identity_marker(token, old_fingerprint);
			if (marker_rc != 0) {
				if (marker_rc == -1) {
					unlink(backup);
					(void)fsync_directory(home_dir());
					emit_error("identity_backup_failed");
				} else {
					g_identity_recovery_required = 1;
					g_shutdown_after_drain = 1;
				}
				unlink(stage_save);
				rmdir(stage);
				return 0;
			}
			marker = 1;
#ifdef HAVE_SIGNAL
			if (g_ratchet) {
				omaq_ratchet_close(g_ratchet);
				g_ratchet = NULL;
			}
#endif
			archive_rc = archive_identity_state(&identity_archive, token, old_fingerprint);
			if (archive_rc != 0) {
				archived = identity_archive_has_moved(&identity_archive);
				failure_code = "identity_state_archive_failed";
				goto identity_import_rollback;
			}
			archived = 1;
			if (omaq_identity_import(home_dir(), stage_save, 1) != 0) {
				failure_code = "identity_import_failed";
				goto identity_import_rollback;
			}
			swapped = 1;
			if (fsync_directory(home_dir()) != 0) {
				failure_code = "identity_import_failed";
				goto identity_import_rollback;
			}
			if (g_tox) {
				omaq_tox_discard(g_tox);
				g_tox = NULL;
			}
			if (load_tox(op->passphrase) != 0)
				goto identity_import_rollback;
#ifdef HAVE_SIGNAL
			g_ratchet = omaq_ratchet_open(home_dir());
			if (!g_ratchet)
				goto identity_import_rollback;
#endif
			omaq_unread_destroy(&g_unread);
			omaq_unread_init(&g_unread);
			unread_reset = 1;
			if (omaq_store_unread_save(&g_unread, state_dir()) != 0 ||
			    fsync_directory(home_dir()) != 0 || fsync_directory(state_dir()) != 0 ||
			    remove_identity_marker() != 0) {
				failure_code = "identity_state_archive_failed";
				goto identity_import_rollback;
			}
			marker = 0;
			g_unread_error_code[0] = '\0';
			reset_identity_runtime_state();
			init_instance_id();
			g_identity_requires_ready = 1;
			g_stdin_identity_ready = 0;
			for (size_t client_index = 0; client_index < g_ncli; client_index++)
				g_client_identity_ready[client_index] = 0;
			if (unlink(backup) != 0 || fsync_directory(home_dir()) != 0) {
				g_identity_backup_cleanup_failed = 1;
				emit_error("identity_backup_cleanup_failed");
			}
			unlink(stage_save);
			rmdir(stage);
			emit("{\"event\":\"identity\",\"op\":\"import\"}");
			return 0;

identity_import_rollback:
#ifdef HAVE_SIGNAL
			if (g_ratchet) {
				omaq_ratchet_close(g_ratchet);
				g_ratchet = NULL;
			}
#endif
			if (swapped && g_tox) {
				omaq_tox_discard(g_tox);
				g_tox = NULL;
			}
			if (swapped && omaq_identity_import(home_dir(), backup, 1) != 0)
				rollback_ok = 0;
			if (archived && restore_identity_state(&identity_archive) != 0)
				rollback_ok = 0;
			if (unread_reset) {
				omaq_unread_destroy(&g_unread);
				omaq_unread_init(&g_unread);
				if (omaq_store_unread_load(&g_unread, state_dir()) != 0)
					rollback_ok = 0;
				else
					g_unread_error_code[0] = '\0';
			}
			if (swapped) {
				rollback_load = load_tox(NULL);
				if (rollback_load < 0)
					rollback_ok = 0;
			}
#ifdef HAVE_SIGNAL
			if (!g_ratchet)
				g_ratchet = omaq_ratchet_open(home_dir());
			if (!g_ratchet)
				rollback_ok = 0;
#endif
			if (rollback_ok && marker && remove_identity_marker() == 0) {
				marker = 0;
				if (unlink(backup) != 0 || fsync_directory(home_dir()) != 0) {
					g_identity_backup_cleanup_failed = 1;
					emit_error("identity_backup_cleanup_failed");
				}
			} else if (marker) {
				rollback_ok = 0;
			}
			unlink(stage_save);
			rmdir(stage);
			if (rollback_ok) {
				emit_error(failure_code);
			} else {
				g_identity_recovery_required = 1;
				g_shutdown_after_drain = 1;
			}
			if (rollback_ok && rollback_load == 1)
				emit_locked_status();
			return 0;
		}
#endif
	}
	emit_error("unsupported");
	return 0;
}

static int serve_line(char *line, int *identity_ready)
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
		int rc = handle_op(&op, identity_ready);
		explicit_bzero(op.passphrase, sizeof(op.passphrase));
		explicit_bzero(line, n + 1);
		return rc;
	}
}

static int serve_input_line(char *line, void *ctx)
{
	return serve_line(line, (int *)ctx);
}

static void accept_client(void)
{
	int c = accept(g_listen, NULL, NULL);
	if (c < 0)
		return;
	fcntl(c, F_SETFD, FD_CLOEXEC);
	{
		int flags = fcntl(c, F_GETFL, 0);
		if (flags < 0 || fcntl(c, F_SETFL, flags | O_NONBLOCK) != 0) {
			close(c);
			return;
		}
	}
	if (g_ncli >= MAX_CLIENTS) {
		close(c);
		return;
	}
	g_clients[g_ncli] = c;
	omaq_line_reader_init(&g_creader[g_ncli]);
	g_olen[g_ncli] = 0;
	g_ooff[g_ncli] = 0;
	g_drop[g_ncli] = 0;
	g_client_identity_ready[g_ncli] = g_identity_requires_ready ? 0 : 1;
	g_ncli++;
}

static void drop_client(size_t i)
{
	close(g_clients[i]);
	if (i + 1 < g_ncli) {
		g_clients[i] = g_clients[g_ncli - 1];
		g_creader[i] = g_creader[g_ncli - 1];
		g_olen[i] = g_olen[g_ncli - 1];
		g_ooff[i] = g_ooff[g_ncli - 1];
		memcpy(g_obuf[i], g_obuf[g_ncli - 1], g_olen[i]);
		g_drop[i] = g_drop[g_ncli - 1];
		g_client_identity_ready[i] = g_client_identity_ready[g_ncli - 1];
	}
	g_ncli--;
}

static void read_client(size_t i)
{
	char tmp[512];
	ssize_t r = read(g_clients[i], tmp, sizeof(tmp));

	if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return;
	if (r <= 0) {
		drop_client(i);
		return;
	}
	if (omaq_line_reader_feed(&g_creader[i], tmp, (size_t)r,
				  serve_input_line, &g_client_identity_ready[i]) != 0)
		drop_client(i);
}

static int read_stdin_lines(void)
{
	char tmp[512];

	for (;;) {
		ssize_t r = read(STDIN_FILENO, tmp, sizeof(tmp));
		if (r > 0) {
			if (omaq_line_reader_feed(&g_stdin_reader, tmp, (size_t)r,
						  serve_input_line, &g_stdin_identity_ready) != 0)
				return -1;
			continue;
		}
		if (r == 0)
			return -1;
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return 0;
		return -1;
	}
}

static void start_backend(void)
{
	if (g_backend_started)
		return;
#ifdef HAVE_TOX
	(void)load_tox(NULL);
#endif
#ifdef HAVE_SIGNAL
	g_ratchet = omaq_ratchet_open(home_dir());
#endif
	g_backend_started = 1;
}

int main(int argc, char **argv)
{
	int hold = 0;
#ifdef HAVE_TOX
	int recovery_rc = 0;
#endif

	signal(SIGPIPE, SIG_IGN);
	{
		int flags = fcntl(STDOUT_FILENO, F_GETFL, 0);
		if (flags >= 0)
			(void)fcntl(STDOUT_FILENO, F_SETFL, flags | O_NONBLOCK);
		flags = fcntl(STDIN_FILENO, F_GETFL, 0);
		if (flags >= 0)
			(void)fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
	}
	int rc;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--hold") == 0)
			hold = 1;
	}
	if (!home_dir() || !state_dir()) {
		fprintf(stderr, "omaq: OMAQ_HOME and OMAQ_STATE required\n");
		return 1;
	}
	omaq_rate_init(&g_rate);
	omaq_rate_init(&g_reaction_rate);
	omaq_rate_init(&g_reaction_out_rate);
	omaq_line_reader_init(&g_stdin_reader);
	if (ensure_state_dir() != 0)
		return 1;
	rc = take_state_lock();
	if (rc == 2)
		return 2;
	if (rc != 0)
		return 1;
	rc = take_lock();
	if (rc == 2)
		return 2;
	if (rc != 0)
		return 1;
#ifdef HAVE_TOX
	if (cleanup_orphan_identity_stages() != 0)
		recovery_rc = -1;
	else
		recovery_rc = recover_identity_replacement();
	if (recovery_rc < 0) {
		fprintf(stderr, "omaq: identity replacement recovery failed\n");
		g_identity_recovery_required = 1;
		g_shutdown_after_drain = 1;
	} else {
		if (recovery_rc > 0)
			g_identity_recovered = 1;
		g_identity_backup_cleanup_failed =
			cleanup_orphan_identity_backups() != 0;
	}
#endif
	omaq_unread_init(&g_unread);
	if (!g_identity_recovery_required && omaq_store_unread_load(&g_unread, state_dir()) != 0) {
		fprintf(stderr, "omaq: unread state is invalid; starting empty\n");
		omaq_unread_init(&g_unread);
		g_unread_load_failed = 1;
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_state_invalid");
	}
	init_instance_id();
	if (bind_sock() != 0)
		return 1;
	if (write_pid() != 0 || write_protocol_marker() != 0)
		return 1;
	g_stdout_spool = omaq_stdout_spool_open(state_dir(), STDOUT_FILENO);
	if (!g_stdout_spool) {
		fprintf(stderr, "omaq: critical stdout spool open failed: %s\n",
			strerror(errno));
		return 1;
	}
	g_replay_mode = omaq_stdout_spool_pending(g_stdout_spool);
	if (g_identity_recovery_required)
		emit_error("identity_rollback_failed");
	else if (g_identity_recovered)
		emit("{\"event\":\"identity\",\"op\":\"recovered\"}");
	if (g_unread_load_failed)
		emit_unread_failed("", "unread_state_invalid");
	if (g_identity_backup_cleanup_failed)
		emit_error("identity_backup_cleanup_failed");
	if (!g_replay_mode && !g_shutdown_after_drain)
		start_backend();
	if (hold && !g_shutdown_after_drain) {
		for (;;) {
#ifdef HAVE_TOX
			if (g_tox) {
				omaq_tox_iterate(g_tox);
				sync_connection_state();
				usleep(omaq_tox_interval_ms(g_tox) * 1000);
				continue;
			}
#endif
			pause();
		}
	}

	while (!g_fatal_io) {
		struct pollfd pf[3 + MAX_CLIENTS];
		int nf = 0;
		int stdin_idx = -1;
		int stdout_idx;
		int listen_idx = -1;
		int ms = 250;
		int pr;

		if (g_shutdown_after_drain &&
		    (g_stdout_closed || (!omaq_stdout_spool_pending(g_stdout_spool) &&
		     g_stdout_len <= g_stdout_off)))
			break;
		if (g_replay_mode && !omaq_stdout_spool_pending(g_stdout_spool)) {
			g_replay_mode = 0;
			if (!g_shutdown_after_drain)
				start_backend();
		}
#ifdef HAVE_TOX
		if (g_tox)
			ms = (int)omaq_tox_interval_ms(g_tox);
#endif
		if (!g_replay_mode && !g_stdin_closed) {
			stdin_idx = nf;
			pf[nf].fd = STDIN_FILENO;
			pf[nf].events = POLLIN;
			nf++;
		}
		stdout_idx = -1;
		if (!g_stdout_closed) {
			stdout_idx = nf;
			pf[nf].fd = STDOUT_FILENO;
			pf[nf].events = (omaq_stdout_spool_pending(g_stdout_spool) ||
					g_stdout_len > g_stdout_off) ? POLLOUT : 0;
			nf++;
		}
		if (!g_replay_mode && g_listen >= 0) {
			listen_idx = nf;
			pf[nf].fd = g_listen;
			pf[nf].events = POLLIN;
			nf++;
		}
		for (size_t i = 0; i < g_ncli; i++) {
			pf[nf].fd = g_clients[i];
			pf[nf].events = POLLIN | (g_olen[i] > g_ooff[i] ? POLLOUT : 0);
			nf++;
		}
		pr = poll(pf, (nfds_t)nf, ms);
#ifdef HAVE_TOX
		if (g_tox && !g_identity_recovery_required) {
			omaq_tox_iterate(g_tox);
			sync_connection_state();
		}
#endif
		if (pr < 0 && errno != EINTR)
			break;
		if (pr <= 0)
			continue;
		if (stdin_idx >= 0 &&
		    (pf[stdin_idx].revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL))) {
			if (read_stdin_lines() != 0)
				g_stdin_closed = 1;
		}
		if (stdout_idx >= 0 && (pf[stdout_idx].revents & (POLLOUT | POLLERR | POLLHUP | POLLNVAL))) {
			if (pf[stdout_idx].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				g_stdout_closed = 1;
				g_stdout_len = 0;
				g_stdout_off = 0;
			} else {
				flush_stdout();
			}
			if (g_replay_mode && g_stdout_closed)
				g_fatal_io = 1;
		}
		if (listen_idx >= 0 && (pf[listen_idx].revents & POLLIN))
			accept_client();
		for (size_t i = 0; i < g_ncli; ) {
			int fd = g_clients[i];
			short revents = 0;
			for (int k = 0; k < nf; k++) {
				if (pf[k].fd == fd)
					revents |= pf[k].revents;
			}
			if (revents & (POLLIN | POLLHUP | POLLERR))
				read_client(i);
			if (i >= g_ncli || g_clients[i] != fd)
				continue;
			if (revents & POLLOUT)
				flush_client(i);
			if (g_drop[i]) {
				drop_client(i);
				continue;
			}
			i++;
		}
		for (size_t i = 0; i < g_ncli; ) {
			if (g_drop[i])
				drop_client(i);
			else
				i++;
		}
	}
#ifdef HAVE_TOX
	if (g_tox) {
		if (g_identity_recovery_required)
			omaq_tox_discard(g_tox);
		else
			omaq_tox_close(g_tox);
	}
#endif
#ifdef HAVE_SIGNAL
	if (g_ratchet)
		omaq_ratchet_close(g_ratchet);
#endif
	omaq_unread_destroy(&g_unread);
	omaq_stdout_spool_close(g_stdout_spool);
	if (g_lockfd >= 0)
		close(g_lockfd);
	if (g_state_lockfd >= 0)
		close(g_state_lockfd);
	return g_fatal_io || g_identity_recovery_required ? 1 : 0;
}
