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
#define OMAQ_PROTOCOL_VERSION 8
#ifdef OMAQ_IPC_TEST
#define OMAQ_IPC_TEST_EVENT_SIZE 65500u
#endif

#ifdef HAVE_TOX
static struct omaq_tox *g_tox;
static int g_locked;
static int g_connection_online = -1;
#define FILE_REQUEST_CACHE 8
#define GROUP_CLEANUP_MAX OMAQ_GROUPS_MAX
#define GROUP_INVITE_RESULT_CACHE_MAX 16
#define GROUP_FRIEND_BINDING_MAX (OMAQ_GROUPS_MAX * OMAQ_GROUP_PEERS)
#define GROUP_BIND_EXPECTED_MAX GROUP_FRIEND_BINDING_MAX
#define RECEIPT_CAPABILITY_MAX 320
#define RECEIPT_RETRY_BATCH 16
#define RECEIPT_LEGACY_GRACE_SEC 120
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
static uint64_t g_friend_generation;
#ifdef HAVE_SIGNAL
static struct omaq_ratchet *g_ratchet;
#endif
static uint8_t g_pending_pk[32];
static int g_have_pending;
static int g_pending_announced;
#ifdef HAVE_SIGNAL
static char g_pending_rk[OMAQ_RK_HEX + 1];
static int g_have_pending_rk;
#endif
static char g_issued_id[OMAQ_INVITE_ID_MAX + 1];
static char g_issued_url[OMAQ_URL_MAX];
static int64_t g_issued_exp;
static int g_issued_is_group;
static char g_issued_group[80];
static int g_have_gpending;
static uint32_t g_gpending_friend;
static uint8_t g_gpending_data[1024];
static size_t g_gpending_len;
static int g_gpending_announced;
static int g_have_gauth;
static uint32_t g_gauth_friend;
static int64_t g_gauth_exp;
static int64_t g_gauth_reservation_deadline;
static char g_gauth_group[65];
static char g_gauth_invite_id[OMAQ_INVITE_ID_MAX + 1];
static int g_group_registry_pending;
static char g_group_registry_group[OMAQ_GROUP_ID_MAX];
static int g_group_invite_send_pending;
static uint32_t g_group_invite_send_friend;
static char g_group_invite_send_group[OMAQ_GROUP_ID_MAX];
static char g_group_invite_send_id[OMAQ_INVITE_ID_MAX + 1];
static char g_group_invite_send_friend_key[65];
static char g_group_invite_send_request[80];
static char g_group_invite_send_url[OMAQ_URL_MAX];
static int64_t g_group_invite_send_deadline;
static char g_group_invite_result_cache[GROUP_INVITE_RESULT_CACHE_MAX][340];
static size_t g_group_invite_result_cache_next;
static size_t g_group_invite_result_cache_count;
static struct {
	int used;
	char group[OMAQ_GROUP_ID_MAX];
	char friend_key[65];
	char member_key[65];
} g_group_friend_bindings[GROUP_FRIEND_BINDING_MAX];
static struct {
	int used;
	char group[OMAQ_GROUP_ID_MAX];
	char invite_id[OMAQ_INVITE_ID_MAX + 1];
	char friend_key[65];
	char member_key[65];
	uint32_t friend;
	int64_t expires;
} g_group_bind_expected[GROUP_BIND_EXPECTED_MAX];
static struct {
	int used;
	uint32_t friend;
	char friend_key[65];
	char member_key[65];
	char group[OMAQ_GROUP_ID_MAX];
	char invite_id[OMAQ_INVITE_ID_MAX + 1];
	int pending_accept;
	int direct_confirmed;
	int64_t expires;
	int64_t retry_after;
} g_group_bind_proof;
static omaq_receipt_outbox g_receipt_outbox;
static int g_receipt_outbox_invalid;
static size_t g_receipt_retry_cursor;
static int g_receipt_transaction_pending;
static int g_receipt_recovery_committed;
static int64_t g_receipt_transaction_retry_after;
static struct {
	char conversation[OMAQ_GROUP_ID_MAX];
	char actor[65];
	int64_t seen;
} g_receipt_capabilities[RECEIPT_CAPABILITY_MAX];
static int g_group_registry_pruned;
static char g_group_registry_pruned_ids[OMAQ_GROUPS_MAX][OMAQ_GROUP_ID_MAX];
static int g_group_registry_pruned_count;
static int g_group_registry_unmapped;
static int g_group_registry_sync_warning;
static int g_group_registry_retry;
static int64_t g_group_registry_retry_after;
static struct {
	int used;
	uint32_t group;
	char gid[OMAQ_GROUP_ID_MAX];
	int64_t retry_after;
} g_group_cleanup[GROUP_CLEANUP_MAX];
static struct {
	int used;
	char group[OMAQ_GROUP_ID_MAX];
	char member_key[65];
} g_group_binding_retire[GROUP_BIND_EXPECTED_MAX];
static int g_group_binding_restore_pending;
static struct {
	int used;
	char group[OMAQ_GROUP_ID_MAX];
	uint32_t peer;
	int64_t expires;
} g_group_leave_notice_suppress[GROUP_FRIEND_BINDING_MAX];
static omaq_control_rate g_group_control_rate;
static int g_av_reset_requested;
static int64_t g_av_reset_next;
static int g_av_reset_reported;
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
#ifdef HAVE_TOX
static int group_registry_save(void);
static int lower_hex_key_ok(const char *key);
static int group_bind_invite_id_ok(const char *invite_id);
static int group_bindings_path(char *out, size_t n);
static int group_bindings_save_except(const char *excluded_gid);
static int group_bind_pending_save(void);
static int group_bind_pending_load(void);
static int recover_pending_group_accept(void);
static int queue_unregistered_groups(void);
static int group_binding_expect(const char *group, const char *invite_id,
				const char *friend_key, uint32_t friend, int64_t expires);
static int group_binding_forget_expect(const char *group, const char *invite_id);
#endif

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

static void emit_identity_error(const char *code, const char *request)
{
	char esc_request[160], buf[360];

	if (!request || !request[0] ||
	    omaq_json_escape(request, esc_request, sizeof(esc_request)) != 0) {
		emit_error(code);
		return;
	}
	snprintf(buf, sizeof(buf),
		 "{\"event\":\"error\",\"code\":\"%s\",\"request\":\"%s\"}",
		 code, esc_request);
	emit(buf);
}

static void emit_invite_state(const char *url, int64_t expires,
			      const char *op, const char *request)
{
	char escaped_url[OMAQ_URL_MAX * 6 + 1], escaped_op[64], escaped_request[160];
	char op_field[96] = "", request_field[192] = "";
	char event[OMAQ_URL_MAX * 6 + 512];

	if (omaq_json_escape(url ? url : "", escaped_url,
			     sizeof(escaped_url)) != 0)
		return;
	if (op && op[0] && omaq_json_escape(op, escaped_op, sizeof(escaped_op)) == 0)
		snprintf(op_field, sizeof(op_field), ",\"op\":\"%s\"", escaped_op);
	if (request && request[0] &&
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) == 0)
		snprintf(request_field, sizeof(request_field),
			 ",\"request\":\"%s\"", escaped_request);
	snprintf(event, sizeof(event),
		 "{\"event\":\"invite\",\"url\":\"%s\",\"expires\":%lld%s%s}",
		 escaped_url, (long long)(expires > 0 ? expires : 0),
		 op_field, request_field);
	emit(event);
}

static void emit_identity_action(const char *op, const char *request,
				 const char *path, int protected_state)
{
	char esc_op[64], esc_request[160], esc_path[1024], buf[1536];
	int has_request = request && request[0], has_path = path && path[0], written;

	if (!op || omaq_json_escape(op, esc_op, sizeof(esc_op)) != 0 ||
	    (has_request && omaq_json_escape(request, esc_request,
					     sizeof(esc_request)) != 0) ||
	    (has_path && omaq_json_escape(path, esc_path, sizeof(esc_path)) != 0))
		return;
	if (has_path && has_request)
		written = snprintf(buf, sizeof(buf),
			"{\"event\":\"identity\",\"op\":\"%s\",\"request\":\"%s\",\"path\":\"%s\"}",
			esc_op, esc_request, esc_path);
	else if (has_path)
		written = snprintf(buf, sizeof(buf),
			"{\"event\":\"identity\",\"op\":\"%s\",\"path\":\"%s\"}",
			esc_op, esc_path);
	else if (protected_state >= 0 && has_request)
		written = snprintf(buf, sizeof(buf),
			"{\"event\":\"identity\",\"op\":\"%s\",\"request\":\"%s\",\"protected\":%s}",
			esc_op, esc_request, protected_state ? "true" : "false");
	else if (protected_state >= 0)
		written = snprintf(buf, sizeof(buf),
			"{\"event\":\"identity\",\"op\":\"%s\",\"protected\":%s}",
			esc_op, protected_state ? "true" : "false");
	else if (has_request)
		written = snprintf(buf, sizeof(buf),
			"{\"event\":\"identity\",\"op\":\"%s\",\"request\":\"%s\"}",
			esc_op, esc_request);
	else
		written = snprintf(buf, sizeof(buf),
			"{\"event\":\"identity\",\"op\":\"%s\"}", esc_op);
	if (written >= 0 && (size_t)written < sizeof(buf))
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
#ifdef HAVE_TOX
	if (g_group_registry_pruned) {
		emit_error("group_orphaned");
		g_group_registry_pruned = 0;
	}
	if (g_group_registry_unmapped) {
		emit_error("legacy_group_state_archived");
		g_group_registry_unmapped = 0;
	}
	if (g_group_registry_sync_warning) {
		emit_error("group_registry_sync_failed");
		g_group_registry_sync_warning = 0;
	}
#endif
}

#ifdef HAVE_TOX
static void emit_locked_status(void)
{
	char ev[320];

	snprintf(ev, sizeof(ev),
		 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"locked\":true,\"instance\":\"%s\",\"call\":null}",
		 OMAQ_PROTOCOL_VERSION, omaq_unread_total(&g_unread), g_instance_id);
	emit(ev);
	emit_invite_state("", 0, "status", NULL);
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

#ifdef HAVE_TOX
static int unread_conversation_available(const char *conversation, void *userdata)
{
	char friend_key[65];
	uint32_t number;

	(void)userdata;
	if (!g_tox || !conversation)
		return -1;
	if (conversation[0] == 'g')
		return omaq_group_id_parse(conversation, &number) == 0 ? 1 : 0;
	if (!direct_id_ok(conversation))
		return -1;
	number = direct_id_number(conversation);
	return omaq_tox_friend_pk_hex(g_tox, number, friend_key) == 0 ? 1 : 0;
}

static int prune_unavailable_unread(void)
{
	omaq_unread_state next;
	int removed;

	if (!g_tox || g_unread.length == 0)
		return 0;
	if (omaq_unread_clone(&next, &g_unread) != 0)
		return -1;
	removed = omaq_unread_prune(&next, unread_conversation_available, NULL);
	if (removed < 0 || (removed > 0 &&
	    omaq_store_unread_save(&next, state_dir()) != 0)) {
		omaq_unread_destroy(&next);
		return -1;
	}
	if (removed > 0) {
		omaq_unread_destroy(&g_unread);
		g_unread = next;
	} else {
		omaq_unread_destroy(&next);
	}
	return removed;
}
#endif

static void emit_json_items(const char *event, const char *conversation,
			     const char *items, size_t items_len, const char *request,
			     int include_unread)
{
	char esc_conv[128], esc_request[OMAQ_JSON_STR_MAX], prefix[420];
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
	if (esc_request[0] && include_unread)
		wr = snprintf(prefix, sizeof(prefix),
			      "{\"event\":\"%s\",\"conversation\":\"%s\",\"request\":\"%s\",\"unread\":%u,\"items\":[",
			      event, esc_conv, esc_request,
			      omaq_unread_count(&g_unread, conversation));
	else if (esc_request[0])
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

static void emit_history_failed(const char *conversation, const char *request)
{
	char esc_conv[128], esc_request[OMAQ_JSON_STR_MAX], ev[800];

	if (!conversation || !request ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(request, esc_request, sizeof(esc_request)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"history.failed\",\"conversation\":\"%s\",\"request\":\"%s\",\"code\":\"history_failed\"}",
		 esc_conv, esc_request);
	emit(ev);
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

static void emit_group_message_event(const char *conversation, const char *id,
				     const char *reply, const char *text,
				     const char *sender)
{
	char esc_conv[128], esc_id[128], esc_reply[128], esc_sender[512],
		esc_text[2800], ev[4000];
	int has_reply;

	if (!conversation || !id || !text || !sender ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0 ||
	    omaq_json_escape(sender, esc_sender, sizeof(esc_sender)) != 0 ||
	    omaq_json_escape(text, esc_text, sizeof(esc_text)) != 0)
		return;
	has_reply = reply && reply[0] &&
		omaq_json_escape(reply, esc_reply, sizeof(esc_reply)) == 0;
	if (has_reply)
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"reply\":\"%s\",\"sender\":\"%s\",\"text\":\"%s\",\"dir\":\"in\"}",
			 esc_conv, esc_id, esc_reply, esc_sender, esc_text);
	else
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"sender\":\"%s\",\"text\":\"%s\",\"dir\":\"in\"}",
			 esc_conv, esc_id, esc_sender, esc_text);
	emit(ev);
}

static void emit_group_membership_message(const char *group, const char *name,
					  int joined)
{
	char message[OMAQ_GROUP_MEMBER_NAME_MAX + 40], id[64];
	const char *display_name = name && name[0] ? name : "A member";

	if (!group ||
	    snprintf(message, sizeof(message), "%s %s the group.", display_name,
		     joined ? "joined" : "left") >= (int)sizeof(message))
		return;
	if (omaq_message_append_with_id(home_dir(), group, "system", message, "sys",
					id, sizeof(id)) != 0) {
		emit_error_conv("history_failed", group);
		return;
	}
	emit_message_event(group, id, "", message, "sys");
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

static int group_invite_request_ok(const char *request)
{
	size_t length;

	if (!request || (length = strlen(request)) < 8 || length > 78 ||
	    strncmp(request, "gi-", 3) != 0)
		return 0;
	for (size_t i = 3; i < length; i++)
		if (!((request[i] >= 'a' && request[i] <= 'z') ||
		      (request[i] >= '0' && request[i] <= '9') || request[i] == '-'))
			return 0;
	return 1;
}

static int stable_group_id_syntax(const char *group)
{
	if (!group || strlen(group) != OMAQ_GROUP_ID_MAX - 1 ||
	    group[0] != 'g' || group[1] != ':')
		return 0;
	for (size_t i = 2; group[i]; i++)
		if (!((group[i] >= '0' && group[i] <= '9') ||
		      (group[i] >= 'a' && group[i] <= 'f')))
			return 0;
	return 1;
}

static int direct_invite_action_op(const omaq_op *op)
{
	return op && op->request[0] &&
		((strcmp(op->op, "invite.create") == 0 &&
		  strcmp(op->kind, "direct") == 0) ||
		 strcmp(op->op, "invite.revoke") == 0);
}

static int targeted_group_invite_op(const omaq_op *op)
{
	return op && strcmp(op->op, "invite.create") == 0 &&
		strcmp(op->kind, "group") == 0 && op->id[0] && op->request[0];
}

static void emit_group_invite_terminal(const omaq_op *op, const char *code)
{
	char event[340];

	if (!op || !code || !direct_id_ok(op->id) ||
	    !stable_group_id_syntax(op->group) ||
	    !group_invite_request_ok(op->request)) {
		emit_error(code && code[0] ? code : "unsupported");
		return;
	}
	snprintf(event, sizeof(event),
		 "{\"event\":\"group.invite.failed\",\"group\":\"%s\",\"friend\":\"%u\",\"request\":\"%s\",\"code\":\"%s\"}",
		 op->group, direct_id_number(op->id), op->request, code);
#ifdef HAVE_SIGNAL
	snprintf(g_group_invite_result_cache[g_group_invite_result_cache_next],
		 sizeof(g_group_invite_result_cache[0]), "%s", event);
	g_group_invite_result_cache_next =
		(g_group_invite_result_cache_next + 1) % GROUP_INVITE_RESULT_CACHE_MAX;
	if (g_group_invite_result_cache_count < GROUP_INVITE_RESULT_CACHE_MAX)
		g_group_invite_result_cache_count++;
#endif
	emit(event);
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

static void emit_group_message_reaction(const char *conversation, const char *id,
					const char *emoji, const char *actor)
{
	char esc_conv[128], esc_id[128], esc_emoji[128], esc_actor[512], ev[1100];

	if (!conversation || !id || !emoji || !actor || strlen(actor) != 64 ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0 ||
	    omaq_json_escape(emoji, esc_emoji, sizeof(esc_emoji)) != 0 ||
	    omaq_json_escape(actor, esc_actor, sizeof(esc_actor)) != 0)
		return;
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message.reaction\",\"conversation\":\"%s\",\"id\":\"%s\",\"emoji\":\"%s\",\"actor\":\"%s\"}",
		 esc_conv, esc_id, esc_emoji, esc_actor);
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

static void emit_conversation_read(const char *event, const char *conversation,
				   const char *code)
{
	char esc_conv[128], value[360];

	if (!event || !conversation ||
	    (strcmp(event, "conversation.read") != 0 &&
	     strcmp(event, "conversation.read.failed") != 0) ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0)
		return;
	if (strcmp(event, "conversation.read.failed") == 0)
		snprintf(value, sizeof(value),
			 "{\"event\":\"conversation.read.failed\",\"conversation\":\"%s\",\"code\":\"%s\"}",
			 esc_conv, code && code[0] ? code : "receipt_state_failed");
	else
		snprintf(value, sizeof(value),
			 "{\"event\":\"conversation.read\",\"conversation\":\"%s\"}",
			 esc_conv);
	emit(value);
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

static int send_group_invite_wire(uint32_t friend, const char *url)
{
	char conversation[16], plain[OMAQ_URL_MAX + 8], wire[3600];

	if (!g_tox || !g_ratchet || !url ||
	    snprintf(conversation, sizeof(conversation), "%u", friend) >=
		    (int)sizeof(conversation) ||
	    snprintf(plain, sizeof(plain), "OQGI1|%s", url) >= (int)sizeof(plain) ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire,
				 sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int request_ratchet_session(uint32_t friend)
{
	char conversation[16], bundle[900], message[920];

	if (!g_tox || !g_ratchet ||
	    snprintf(conversation, sizeof(conversation), "%u", friend) >=
		    (int)sizeof(conversation))
		return -1;
	if (omaq_ratchet_has_session(g_ratchet, conversation))
		return 1;
	if (omaq_ratchet_bundle(g_ratchet, bundle, sizeof(bundle)) != 0 ||
	    snprintf(message, sizeof(message), "OQB1%s", bundle) >=
		    (int)sizeof(message) ||
	    omaq_tox_send(g_tox, friend, message) != 0)
		return -1;
	return 0;
}

static void emit_group_invite_result(uint32_t friend, const char *group,
				     const char *request, const char *event_name,
				     const char *code)
{
	char event[340];

	if (!group_invite_request_ok(request))
		return;
	if (code)
		snprintf(event, sizeof(event),
			 "{\"event\":\"%s\",\"group\":\"%s\",\"friend\":\"%u\",\"request\":\"%s\",\"code\":\"%s\"}",
			 event_name, group, friend, request, code);
	else
		snprintf(event, sizeof(event),
			 "{\"event\":\"%s\",\"group\":\"%s\",\"friend\":\"%u\",\"request\":\"%s\"}",
			 event_name, group, friend, request);
	snprintf(g_group_invite_result_cache[g_group_invite_result_cache_next],
		 sizeof(g_group_invite_result_cache[0]), "%s", event);
	g_group_invite_result_cache_next =
		(g_group_invite_result_cache_next + 1) % GROUP_INVITE_RESULT_CACHE_MAX;
	if (g_group_invite_result_cache_count < GROUP_INVITE_RESULT_CACHE_MAX)
		g_group_invite_result_cache_count++;
	emit(event);
}

static void clear_group_invite_results(void)
{
	memset(g_group_invite_result_cache, 0, sizeof(g_group_invite_result_cache));
	g_group_invite_result_cache_next = 0;
	g_group_invite_result_cache_count = 0;
}

static void replay_group_invite_results(void)
{
	size_t oldest = (g_group_invite_result_cache_next +
		GROUP_INVITE_RESULT_CACHE_MAX - g_group_invite_result_cache_count) %
		GROUP_INVITE_RESULT_CACHE_MAX;

	for (size_t i = 0; i < g_group_invite_result_cache_count; i++) {
		size_t index = (oldest + i) % GROUP_INVITE_RESULT_CACHE_MAX;
		if (g_group_invite_result_cache[index][0])
			emit(g_group_invite_result_cache[index]);
	}
}

static void emit_group_invite_op_failure(const omaq_op *op, const char *code)
{
	if (op && direct_id_ok(op->id) && stable_group_id_syntax(op->group) &&
	    group_invite_request_ok(op->request))
		emit_group_invite_result(direct_id_number(op->id), op->group,
					 op->request, "group.invite.failed", code);
	else
		emit_error(code);
}

static void clear_pending_group_invite(void)
{
	g_group_invite_send_pending = 0;
	g_group_invite_send_friend = UINT32_MAX;
	g_group_invite_send_group[0] = '\0';
	g_group_invite_send_id[0] = '\0';
	g_group_invite_send_friend_key[0] = '\0';
	g_group_invite_send_request[0] = '\0';
	g_group_invite_send_url[0] = '\0';
	g_group_invite_send_deadline = 0;
}

static int send_group_invite_response(uint32_t friend, const char *prefix,
				      const char *invite_id, const char *group)
{
	char conversation[16], plain[112], wire[380];

	if (!prefix || !invite_id || !group ||
	    snprintf(conversation, sizeof(conversation), "%u", friend) >=
		    (int)sizeof(conversation) ||
	    snprintf(plain, sizeof(plain), "%s|%s|%s", prefix, invite_id, group) >=
		    (int)sizeof(plain) ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire,
				 sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_group_binding_confirmation(uint32_t friend, const char *group,
					   const char *invite_id,
					   const char *member_key)
{
	char conversation[16], plain[192], wire[520];

	if (!stable_group_id_syntax(group) || !group_bind_invite_id_ok(invite_id) ||
	    !lower_hex_key_ok(member_key) ||
	    snprintf(conversation, sizeof(conversation), "%u", friend) >=
		    (int)sizeof(conversation) ||
	    snprintf(plain, sizeof(plain), "OQX1|gmbd|%s|%s|%s", invite_id,
		     group, member_key) >= (int)sizeof(plain) ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire,
				 sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_group_binding_ready(uint32_t friend, const char *invite_id)
{
	char conversation[16], plain[OMAQ_INVITE_ID_MAX + 12], wire[380];

	if (!group_bind_invite_id_ok(invite_id) ||
	    snprintf(conversation, sizeof(conversation), "%u", friend) >=
		    (int)sizeof(conversation) ||
	    snprintf(plain, sizeof(plain), "OQX1|gmbc|%s", invite_id) >=
		    (int)sizeof(plain) ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire,
				 sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_group_binding_ack(uint32_t friend, const char *invite_id)
{
	char conversation[16], plain[OMAQ_INVITE_ID_MAX + 12], wire[380];

	if (!invite_id || !invite_id[0] ||
	    snprintf(conversation, sizeof(conversation), "%u", friend) >=
		    (int)sizeof(conversation) ||
	    snprintf(plain, sizeof(plain), "OQX1|gmba|%s", invite_id) >=
		    (int)sizeof(plain) ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire,
				 sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int pending_group_invite_friend_matches(uint32_t friend)
{
	char current_key[65];

	return g_group_invite_send_friend_key[0] &&
		omaq_tox_friend_pk_hex(g_tox, friend, current_key) == 0 &&
		strcmp(current_key, g_group_invite_send_friend_key) == 0;
}

static void finish_pending_group_invite(uint32_t friend)
{
	if (!g_group_invite_send_pending || friend != g_group_invite_send_friend)
		return;
	if (!pending_group_invite_friend_matches(friend) ||
	    send_group_invite_wire(friend, g_group_invite_send_url) != 0) {
		emit_group_invite_result(friend, g_group_invite_send_group,
					 g_group_invite_send_request,
					 "group.invite.failed", "forbidden");
		clear_pending_group_invite();
		return;
	}
	g_group_invite_send_deadline = (int64_t)time(NULL) + 30;
}

static void complete_pending_group_invite(uint32_t friend,
					  const char *invite_id,
					  const char *group, int ready)
{
	omaq_role self = ROLE_MEMBER;
	omaq_invite sent_invite;
	int binding_rc = 0;
	int invite_valid = omaq_invite_parse(g_group_invite_send_url, &sent_invite) == 0 &&
		sent_invite.kind == INVITE_GROUP &&
		strcmp(sent_invite.id, invite_id) == 0 &&
		strlen(group) == OMAQ_GROUP_ID_MAX - 1 && group[0] == 'g' && group[1] == ':' &&
		strcmp(sent_invite.group, group + 2) == 0 &&
		!omaq_invite_expired(&sent_invite, (int64_t)time(NULL));

	if (!g_group_invite_send_pending || friend != g_group_invite_send_friend ||
	    strcmp(invite_id, g_group_invite_send_id) != 0 ||
	    strcmp(group, g_group_invite_send_group) != 0)
		return;
	if (!ready) {
		emit_group_invite_result(friend, group, g_group_invite_send_request,
					 "group.invite.failed", "busy");
		clear_pending_group_invite();
		return;
	}
	if (!invite_valid || !pending_group_invite_friend_matches(friend)) {
		emit_group_invite_result(friend, group, g_group_invite_send_request,
					 "group.invite.failed", "forbidden");
		clear_pending_group_invite();
		return;
	}
	if (omaq_group_self_role(g_tox, group, &self) != 0) {
		emit_group_invite_result(friend, group, g_group_invite_send_request,
					 "group.invite.failed", "forbidden");
	} else if ((binding_rc = group_binding_expect(group, invite_id,
					g_group_invite_send_friend_key, friend,
					sent_invite.expiry)) != 0) {
		emit_group_invite_result(friend, group, g_group_invite_send_request,
					 "group.invite.failed",
					 binding_rc == -2 ? "group_registry_failed" : "busy");
	} else if (omaq_group_invite_friend(g_tox, group, friend, self,
					    ROLE_MEMBER) != 0) {
		(void)group_binding_forget_expect(group, invite_id);
		emit_group_invite_result(friend, group, g_group_invite_send_request,
					 "group.invite.failed", "forbidden");
	} else {
		emit_group_invite_result(friend, group, g_group_invite_send_request,
					 "group.invite.sent", NULL);
	}
	clear_pending_group_invite();
}

static void expire_pending_group_invite(void)
{
	if (!g_group_invite_send_pending ||
	    (int64_t)time(NULL) < g_group_invite_send_deadline)
		return;
	emit_group_invite_result(g_group_invite_send_friend,
				 g_group_invite_send_group,
				 g_group_invite_send_request,
				 "group.invite.failed", "ratchet_pending");
	clear_pending_group_invite();
}

static int send_receipt_wire(uint32_t friend, const char *conversation,
			     const char *id, const char *state)
{
	char plain[256], wire[520];

	if (!g_tox || !g_ratchet || !conversation || !id ||
	    omaq_receipt_wire_pack(plain, sizeof(plain), id, state) != 0 ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_receipt_capability_wire(uint32_t friend, const char *conversation)
{
	char wire[520];
	static const char capability[] = "OQX1|receipt-ack-v1";

	if (!g_tox || !g_ratchet || !conversation ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, capability, wire,
				  sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_receipt_confirm_wire(uint32_t friend, const char *conversation,
				     const char *id)
{
	char plain[256], wire[520];

	if (!g_tox || !g_ratchet || !conversation || !id ||
	    omaq_receipt_confirm_wire_pack(plain, sizeof(plain), id, "read", "-") != 0 ||
	    omaq_ratchet_encrypt(g_ratchet, conversation, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}
#endif

static int receipt_capability_actor(uint32_t friend, char *out, size_t outn)
{
	return !g_tox || !out || outn < 65 ||
		omaq_tox_friend_pk_hex(g_tox, friend, out) != 0 ? -1 : 0;
}

static void note_receipt_capability(const char *conversation, const char *actor)
{
	int free_slot = -1, oldest = 0;
	int64_t now = (int64_t)time(NULL);

	if (!conversation || !actor || strlen(actor) != 64)
		return;
	for (int i = 0; i < RECEIPT_CAPABILITY_MAX; i++) {
		if (!g_receipt_capabilities[i].conversation[0]) {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (strcmp(g_receipt_capabilities[i].conversation, conversation) == 0 &&
		    strcmp(g_receipt_capabilities[i].actor, actor) == 0) {
			g_receipt_capabilities[i].seen = now;
			return;
		}
		if (g_receipt_capabilities[i].seen < g_receipt_capabilities[oldest].seen)
			oldest = i;
	}
	if (free_slot < 0)
		free_slot = oldest;
	memset(&g_receipt_capabilities[free_slot], 0,
	       sizeof(g_receipt_capabilities[free_slot]));
	snprintf(g_receipt_capabilities[free_slot].conversation,
		 sizeof(g_receipt_capabilities[free_slot].conversation), "%s", conversation);
	snprintf(g_receipt_capabilities[free_slot].actor,
		 sizeof(g_receipt_capabilities[free_slot].actor), "%s", actor);
	g_receipt_capabilities[free_slot].seen = now;
}

static int receipt_ack_capable(const char *conversation, const char *actor)
{
	int64_t now = (int64_t)time(NULL);

	if (!conversation || !actor)
		return 0;
	for (int i = 0; i < RECEIPT_CAPABILITY_MAX; i++)
		if (strcmp(g_receipt_capabilities[i].conversation, conversation) == 0 &&
		    strcmp(g_receipt_capabilities[i].actor, actor) == 0) {
			if (g_receipt_capabilities[i].seen > 0 &&
			    now - g_receipt_capabilities[i].seen <= 600)
				return 1;
			memset(&g_receipt_capabilities[i], 0,
			       sizeof(g_receipt_capabilities[i]));
			return 0;
		}
	return 0;
}

static void receipt_outbox_note_ack(const char *conversation, const char *id)
{
	if (!conversation || !id)
		return;
	for (size_t i = 0; i < g_receipt_outbox.length; i++)
		if (strcmp(g_receipt_outbox.entries[i].conversation, conversation) == 0 &&
		    strcmp(g_receipt_outbox.entries[i].id, id) == 0) {
			g_receipt_outbox.entries[i].acknowledged = 1;
			return;
		}
}

static void flush_receipt_acknowledgements(void)
{
	omaq_receipt_outbox updated;
	int have_ack = 0;

	if (g_receipt_outbox_invalid)
		return;
	for (size_t i = 0; i < g_receipt_outbox.length; i++)
		if (g_receipt_outbox.entries[i].acknowledged) {
			have_ack = 1;
			break;
		}
	if (!have_ack)
		return;
	omaq_receipt_outbox_init(&updated);
	if (omaq_receipt_outbox_clone(&updated, &g_receipt_outbox) != 0)
		return;
	for (size_t i = updated.length; i > 0; i--)
		if (updated.entries[i - 1u].acknowledged) {
			if (i < updated.length)
				memmove(&updated.entries[i - 1u], &updated.entries[i],
					(updated.length - i) * sizeof(*updated.entries));
			updated.length--;
		}
	if (omaq_receipt_outbox_save(&updated, state_dir()) != 0) {
		omaq_receipt_outbox_destroy(&updated);
		return;
	}
	for (size_t i = 0; i < g_receipt_outbox.length; i++)
		if (g_receipt_outbox.entries[i].acknowledged)
			emit_receipt_event_name("receipt.sent",
				g_receipt_outbox.entries[i].conversation,
				g_receipt_outbox.entries[i].id, "read");
	omaq_receipt_outbox_destroy(&g_receipt_outbox);
	g_receipt_outbox = updated;
	if (g_receipt_outbox.length > 0)
		g_receipt_retry_cursor %= g_receipt_outbox.length;
	else
		g_receipt_retry_cursor = 0;
}

static int receipt_outbox_commit_add(const char *conversation,
				     const omaq_store_message_id *ids, size_t count)
{
	omaq_receipt_outbox updated;
	int changed = 0;

	if (g_receipt_outbox_invalid || (!ids && count))
		return -1;
	omaq_receipt_outbox_init(&updated);
	if (omaq_receipt_outbox_clone(&updated, &g_receipt_outbox) != 0)
		return -1;
	for (size_t i = 0; i < count; i++) {
		int add_rc = omaq_receipt_outbox_add(&updated, conversation, ids[i].id);
		if (add_rc < 0) {
			omaq_receipt_outbox_destroy(&updated);
			return -1;
		}
		if (add_rc > 0)
			changed = 1;
	}
	if (changed && omaq_receipt_outbox_save(&updated, state_dir()) != 0) {
		omaq_receipt_outbox_destroy(&updated);
		return -1;
	}
	omaq_receipt_outbox_destroy(&g_receipt_outbox);
	g_receipt_outbox = updated;
	return 0;
}

static int receipt_outbox_has_capacity(const char *conversation,
				       const omaq_store_message_id *ids, size_t count)
{
	omaq_receipt_outbox prepared;
	int rc = 0;

	omaq_receipt_outbox_init(&prepared);
	if ((!ids && count) || omaq_receipt_outbox_clone(&prepared,
							 &g_receipt_outbox) != 0)
		return 0;
	for (size_t i = 0; i < count; i++)
		if (omaq_receipt_outbox_add(&prepared, conversation, ids[i].id) < 0) {
			rc = -1;
			break;
		}
	omaq_receipt_outbox_destroy(&prepared);
	return rc == 0;
}

static int receipt_transaction_begin(const char *conversation,
				     const omaq_store_message_id *ids, size_t count)
{
	omaq_receipt_outbox transaction;

	if (!conversation || !ids || count == 0)
		return -1;
	omaq_receipt_outbox_init(&transaction);
	for (size_t i = 0; i < count; i++)
		if (omaq_receipt_outbox_add(&transaction, conversation, ids[i].id) != 1) {
			omaq_receipt_outbox_destroy(&transaction);
			return -1;
		}
	if (omaq_receipt_transaction_clear(state_dir()) != 0 ||
	    omaq_receipt_transaction_save(&transaction, state_dir()) != 0) {
		omaq_receipt_outbox_destroy(&transaction);
		return -1;
	}
	omaq_receipt_outbox_destroy(&transaction);
	g_receipt_transaction_pending = 1;
	g_receipt_transaction_retry_after = (int64_t)time(NULL) + 2;
	return 0;
}

static int recover_receipt_transaction(void)
{
	omaq_receipt_outbox transaction;
	omaq_unread_state durable_unread;
	const char *conversation;
	unsigned previous_count;
	int committed, rc = -1;

	g_receipt_recovery_committed = 0;
	omaq_receipt_outbox_init(&transaction);
	omaq_unread_init(&durable_unread);
	if (omaq_receipt_transaction_load(&transaction, state_dir()) != 0)
		goto done;
	committed = omaq_receipt_transaction_committed(state_dir());
	if (committed < 0)
		goto done;
	if (transaction.length == 0) {
		rc = omaq_receipt_transaction_clear(state_dir());
		goto done;
	}
	conversation = transaction.entries[0].conversation;
	for (size_t i = 1; i < transaction.length; i++)
		if (strcmp(transaction.entries[i].conversation, conversation) != 0)
			goto done;
	if (omaq_store_unread_load(&durable_unread, state_dir()) != 0)
		goto done;
	previous_count = omaq_unread_count(&g_unread, conversation);
	if (!committed && omaq_unread_count(&durable_unread, conversation) == 0)
		committed = 1;
	if (committed) {
		omaq_store_message_id *ids = calloc(transaction.length, sizeof(*ids));
		if (!ids)
			goto done;
		for (size_t i = 0; i < transaction.length; i++)
			snprintf(ids[i].id, sizeof(ids[i].id), "%s", transaction.entries[i].id);
		rc = receipt_outbox_commit_add(conversation, ids, transaction.length);
		free(ids);
		if (rc != 0)
			goto done;
	}
	if (omaq_receipt_transaction_clear(state_dir()) != 0)
		goto done;
	omaq_unread_destroy(&g_unread);
	g_unread = durable_unread;
	omaq_unread_init(&durable_unread);
	if (previous_count != omaq_unread_count(&g_unread, conversation))
		emit_unread(conversation);
	g_receipt_recovery_committed = committed;
	rc = 0;
done:
	omaq_unread_destroy(&durable_unread);
	omaq_receipt_outbox_destroy(&transaction);
	g_receipt_transaction_pending = rc != 0;
	g_receipt_transaction_retry_after = rc != 0
		? (int64_t)time(NULL) + 5 : 0;
	return rc;
}

static void retry_receipt_transaction(void)
{
	if (!g_receipt_transaction_pending ||
	    (int64_t)time(NULL) < g_receipt_transaction_retry_after)
		return;
	(void)recover_receipt_transaction();
}

static int receipt_transaction_discard_conversation(const char *conversation)
{
	omaq_receipt_outbox transaction;
	int matches = 1, rc = 0;

	if (!conversation)
		return -1;
	omaq_receipt_outbox_init(&transaction);
	if (omaq_receipt_transaction_load(&transaction, state_dir()) != 0)
		return -1;
	for (size_t i = 0; i < transaction.length; i++)
		if (strcmp(transaction.entries[i].conversation, conversation) != 0) {
			matches = 0;
			break;
		}
	if (transaction.length == 0 || matches)
		rc = omaq_receipt_transaction_clear(state_dir());
	else
		rc = recover_receipt_transaction();
	omaq_receipt_outbox_destroy(&transaction);
	if (rc == 0 && matches) {
		g_receipt_transaction_pending = 0;
		g_receipt_transaction_retry_after = 0;
	}
	return rc;
}

static int receipt_outbox_drop_conversation(const char *conversation)
{
	omaq_receipt_outbox updated;
	int changed = 0;

	if (!conversation || g_receipt_outbox_invalid)
		return -1;
	omaq_receipt_outbox_init(&updated);
	if (omaq_receipt_outbox_clone(&updated, &g_receipt_outbox) != 0) {
		g_receipt_outbox_invalid = 1;
		return -1;
	}
	for (size_t i = updated.length; i > 0; i--)
		if (strcmp(updated.entries[i - 1u].conversation, conversation) == 0) {
			if (i < updated.length)
				memmove(&updated.entries[i - 1u], &updated.entries[i],
					(updated.length - i) * sizeof(*updated.entries));
			updated.length--;
			changed = 1;
		}
	if (changed && omaq_receipt_outbox_save(&updated, state_dir()) != 0) {
		omaq_receipt_outbox_destroy(&updated);
		g_receipt_outbox_invalid = 1;
		return -1;
	}
	omaq_receipt_outbox_destroy(&g_receipt_outbox);
	g_receipt_outbox = updated;
	return 0;
}

static int prune_unavailable_receipts(void)
{
	omaq_receipt_outbox updated;
	int changed = 0;

	if (g_receipt_outbox_invalid)
		return -1;
	omaq_receipt_outbox_init(&updated);
	if (omaq_receipt_outbox_clone(&updated, &g_receipt_outbox) != 0)
		return -1;
	for (size_t i = updated.length; i > 0; i--) {
		int available = unread_conversation_available(
			updated.entries[i - 1u].conversation, NULL);
		if (available < 0) {
			omaq_receipt_outbox_destroy(&updated);
			return -1;
		}
		if (available == 0) {
			if (i < updated.length)
				memmove(&updated.entries[i - 1u], &updated.entries[i],
					(updated.length - i) * sizeof(*updated.entries));
			updated.length--;
			changed = 1;
		}
	}
	if (changed && omaq_receipt_outbox_save(&updated, state_dir()) != 0) {
		omaq_receipt_outbox_destroy(&updated);
		return -1;
	}
	omaq_receipt_outbox_destroy(&g_receipt_outbox);
	g_receipt_outbox = updated;
	return 0;
}

static int group_self_member_key(uint32_t group, char *out, size_t outn)
{
	for (int member = 0; member < omaq_group_peer_count(group); member++) {
		const char *key = omaq_group_peer_key(group, member);
		if (!omaq_group_peer_self(group, member) || !key || strlen(key) != 64)
			continue;
		return snprintf(out, outn, "%s", key) >= (int)outn ? -1 : 0;
	}
	return -1;
}

static void retry_receipt_outbox(void)
{
	int64_t now = (int64_t)time(NULL);
	size_t checked = 0, attempted = 0;

	if (!g_tox || g_receipt_outbox_invalid || g_receipt_outbox.length == 0)
		return;
	while (checked < g_receipt_outbox.length && attempted < RECEIPT_RETRY_BATCH) {
		size_t index = g_receipt_retry_cursor % g_receipt_outbox.length;
		omaq_receipt_outbox_entry *entry = &g_receipt_outbox.entries[index];
		unsigned jitter = (unsigned char)entry->id[0] % 5u;
		int send_rc = -1;

		g_receipt_retry_cursor = (index + 1u) % g_receipt_outbox.length;
		checked++;
		if (entry->next_attempt > now)
			continue;
		attempted++;
		entry->next_attempt = now + 5 + (int64_t)jitter;
		if (entry->conversation[0] == 'g') {
			char receipt[256];
			(void)omaq_group_send(g_tox, entry->conversation,
					      "OQX1|receipt-ack-v1");
			if (omaq_receipt_wire_pack(receipt, sizeof(receipt), entry->id,
						   "read") == 0)
				send_rc = omaq_group_send(g_tox, entry->conversation, receipt);
		} else if (direct_id_ok(entry->conversation)) {
#ifdef HAVE_SIGNAL
			uint32_t friend = direct_id_number(entry->conversation);
			if (omaq_tox_online(g_tox) && omaq_tox_friend_online(g_tox, friend)) {
				(void)send_receipt_capability_wire(friend, entry->conversation);
				send_rc = send_receipt_wire(friend, entry->conversation,
						       entry->id, "read");
			}
#endif
		}
		if (send_rc != 0) {
			entry->next_attempt = now + 10 + (int64_t)jitter;
			continue;
		}
		if (entry->created > 0 && now - entry->created >= RECEIPT_LEGACY_GRACE_SEC)
			receipt_outbox_note_ack(entry->conversation, entry->id);
	}
	flush_receipt_acknowledgements();
}

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
	g_pending_announced = 0;
#ifdef HAVE_SIGNAL
	g_pending_rk[0] = '\0';
	g_have_pending_rk = 0;
#endif
}

static void clear_invite_and_emit(void)
{
	clear_invite();
	emit_invite_state("", 0, "clear", NULL);
}

static void announce_pending_direct(void)
{
	if (!g_have_pending || g_pending_announced)
		return;
	g_pending_announced = 1;
	emit("{\"event\":\"request\",\"kind\":\"direct\"}");
}

static void clear_group_auth(void)
{
	g_have_gauth = 0;
	g_gauth_friend = UINT32_MAX;
	g_gauth_exp = 0;
	g_gauth_reservation_deadline = 0;
	g_gauth_group[0] = '\0';
	g_gauth_invite_id[0] = '\0';
	announce_pending_direct();
}

static void expire_group_auth_reservation(void)
{
	if (!g_have_gauth || (g_have_gpending && g_gpending_announced) ||
	    g_gauth_reservation_deadline == 0 ||
	    (int64_t)time(NULL) < g_gauth_reservation_deadline)
		return;
	clear_group_auth();
}

static void reset_identity_runtime_state(void)
{
	clear_invite();
	clear_group_auth();
	g_have_gpending = 0;
	g_gpending_len = 0;
	g_gpending_announced = 0;
	g_group_registry_pending = 0;
	g_group_registry_group[0] = '\0';
	memset(g_group_friend_bindings, 0, sizeof(g_group_friend_bindings));
	memset(g_group_bind_expected, 0, sizeof(g_group_bind_expected));
	memset(&g_group_bind_proof, 0, sizeof(g_group_bind_proof));
	g_group_invite_send_pending = 0;
	g_group_invite_send_friend = UINT32_MAX;
	g_group_invite_send_group[0] = '\0';
	g_group_invite_send_id[0] = '\0';
	g_group_invite_send_friend_key[0] = '\0';
	g_group_invite_send_request[0] = '\0';
	g_group_invite_send_url[0] = '\0';
	g_group_invite_send_deadline = 0;
	g_group_registry_pruned = 0;
	g_group_registry_pruned_count = 0;
	memset(g_group_registry_pruned_ids, 0,
	       sizeof(g_group_registry_pruned_ids));
	g_group_registry_unmapped = 0;
	g_group_registry_sync_warning = 0;
	g_group_registry_retry = 0;
	g_group_registry_retry_after = 0;
	memset(g_group_cleanup, 0, sizeof(g_group_cleanup));
	memset(g_group_binding_retire, 0, sizeof(g_group_binding_retire));
	g_group_binding_restore_pending = 0;
	memset(&g_group_leave_notice_suppress, 0,
	       sizeof(g_group_leave_notice_suppress));
	memset(g_pending_pk, 0, sizeof(g_pending_pk));
	memset(g_file_requests, 0, sizeof(g_file_requests));
	g_file_request_sequence = 0;
	omaq_control_rate_init(&g_group_control_rate);
	g_av_reset_requested = 0;
	g_av_reset_next = 0;
	g_av_reset_reported = 0;
	omaq_group_reset();
	omaq_file_reset();
#ifdef HAVE_TOX
	omaq_av_reset();
#endif
	emit_invite_state("", 0, "clear", NULL);
}

#define IDENTITY_ARCHIVE_PATHS 11

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

static int identity_stage_entry_ok(const char *name)
{
	static const char *exact[] = {
		"tox.save", "tox.save.tmp", "groups.tsv", "group-friends.tsv",
		"identity.bundle"
	};
	static const char *prefixes[] = {
		"tox.save.tmp.", "groups.tsv.tmp.", "group-friends.tsv.tmp.",
		"identity.bundle.tmp."
	};

	if (!name || !name[0])
		return 0;
	for (size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); i++)
		if (strcmp(name, exact[i]) == 0)
			return 1;
	for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
		size_t n = strlen(prefixes[i]);
		if (strncmp(name, prefixes[i], n) != 0 || !name[n])
			continue;
		for (size_t j = n; name[j]; j++)
			if (name[j] < '0' || name[j] > '9')
				return 0;
		return 1;
	}
	return 0;
}

static int cleanup_identity_stage(const char *stage)
{
	DIR *dir = NULL;
	struct dirent *entry;
	int fd = -1, failed = 0;

	if (!stage)
		return -1;
	fd = open(stage, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	dir = fdopendir(fd);
	if (!dir) {
		close(fd);
		return -1;
	}
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (!identity_stage_entry_ok(entry->d_name) ||
		    unlinkat(fd, entry->d_name, 0) != 0)
			failed = 1;
	}
	if (closedir(dir) != 0)
		failed = 1;
	if (rmdir(stage) != 0 && errno != ENOENT)
		failed = 1;
	return failed ? -1 : 0;
}

static int create_identity_stage(char *stage, size_t n)
{
	if (!stage || snprintf(stage, n, "%s/identity-import-stage-XXXXXX",
			       state_dir()) >= (int)n || !mkdtemp(stage))
		return -1;
	return 0;
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
	const char *home_names[] = {
		"history", "avatars", "files", "ratchet", "groups.tsv", "group-friends.tsv"
	};
	const char *state_names[] = {
		"surfaces.jsonl", "", "auto-open.json", "unread.tsv", "group-bind.pending"
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
		const char *base = i < 6 ? home_dir() : state_dir();
		const char *name = i < 6 ? home_names[i] :
			(i == 7 ? auto_open_name : state_names[i - 6]);
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
		char stage[700];
		struct stat st;
		size_t length;
		int old_numeric = 1, random_suffix = 1;

		if (strncmp(entry->d_name, prefix, sizeof(prefix) - 1) != 0)
			continue;
		suffix = entry->d_name + sizeof(prefix) - 1;
		length = strlen(suffix);
		if (length == 0 || length > 20)
			continue;
		for (size_t i = 0; i < length; i++) {
			if (suffix[i] < '0' || suffix[i] > '9')
				old_numeric = 0;
			if (!((suffix[i] >= '0' && suffix[i] <= '9') ||
			      (suffix[i] >= 'A' && suffix[i] <= 'Z') ||
			      (suffix[i] >= 'a' && suffix[i] <= 'z')))
				random_suffix = 0;
		}
		if (!old_numeric && !(length == 6 && random_suffix))
			continue;
		if (snprintf(stage, sizeof(stage), "%s/%s", state_dir(), entry->d_name) >=
			    (int)sizeof(stage) || lstat(stage, &st) != 0 ||
		    !S_ISDIR(st.st_mode))
			continue;
		if (cleanup_identity_stage(stage) != 0)
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

#ifdef HAVE_TOX
static uint32_t g_group_generation;

static int lower_hex_key_ok(const char *key)
{
	if (!key || strlen(key) != 64)
		return 0;
	for (size_t i = 0; i < 64; i++)
		if (!((key[i] >= '0' && key[i] <= '9') ||
		      (key[i] >= 'a' && key[i] <= 'f')))
			return 0;
	return 1;
}

static const char *group_binding_friend(const char *group, const char *member_key)
{
	for (int i = 0; i < GROUP_FRIEND_BINDING_MAX; i++)
		if (g_group_friend_bindings[i].used &&
		    strcmp(g_group_friend_bindings[i].group, group) == 0 &&
		    strcmp(g_group_friend_bindings[i].member_key, member_key) == 0)
			return g_group_friend_bindings[i].friend_key;
	return "";
}

static const char *group_binding_member(const char *group, const char *friend_key)
{
	for (int i = 0; i < GROUP_FRIEND_BINDING_MAX; i++)
		if (g_group_friend_bindings[i].used &&
		    strcmp(g_group_friend_bindings[i].group, group) == 0 &&
		    strcmp(g_group_friend_bindings[i].friend_key, friend_key) == 0)
			return g_group_friend_bindings[i].member_key;
	return "";
}

static int group_binding_pending_friend(const char *group, const char *friend_key)
{
	int64_t now = (int64_t)time(NULL);
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_bind_expected[i].used &&
		    (g_group_bind_expected[i].expires == 0 ||
		     g_group_bind_expected[i].expires > now) &&
		    strcmp(g_group_bind_expected[i].group, group) == 0 &&
		    strcmp(g_group_bind_expected[i].friend_key, friend_key) == 0)
			return 1;
	return 0;
}

static int group_binding_prune_expired(void)
{
	unsigned char previous[sizeof(g_group_bind_expected)];
	int changed = 0;
	int64_t now = (int64_t)time(NULL);

	memcpy(previous, g_group_bind_expected, sizeof(previous));
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_bind_expected[i].used &&
		    !g_group_bind_expected[i].member_key[0] &&
		    g_group_bind_expected[i].expires > 0 &&
		    g_group_bind_expected[i].expires <= now) {
			memset(&g_group_bind_expected[i], 0,
			       sizeof(g_group_bind_expected[i]));
			changed = 1;
		}
	if (changed && group_bind_pending_save() != 0) {
		memcpy(g_group_bind_expected, previous, sizeof(previous));
		return -1;
	}
	return 0;
}

static int group_binding_debt_pending(void)
{
	if (group_binding_prune_expired() != 0 ||
	    g_group_bind_proof.used || g_group_invite_send_pending ||
	    g_have_gauth || g_have_gpending)
		return 1;
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_bind_expected[i].used)
			return 1;
	return 0;
}

static int group_binding_store(const char *group, const char *friend_key,
			       const char *member_key, int persist)
{
	int slot = -1, group_count = 0;
	struct {
		int used;
		char group[OMAQ_GROUP_ID_MAX];
		char friend_key[65];
		char member_key[65];
	} previous;

	if (!stable_group_id_syntax(group) || !lower_hex_key_ok(friend_key) ||
	    !lower_hex_key_ok(member_key))
		return -1;
	for (int i = 0; i < GROUP_FRIEND_BINDING_MAX; i++) {
		if (g_group_friend_bindings[i].used &&
		    strcmp(g_group_friend_bindings[i].group, group) == 0) {
			group_count++;
			if (strcmp(g_group_friend_bindings[i].member_key, member_key) == 0 &&
			    strcmp(g_group_friend_bindings[i].friend_key, friend_key) != 0)
				return -1;
			if (strcmp(g_group_friend_bindings[i].friend_key, friend_key) == 0)
				slot = i;
		}
		if (!g_group_friend_bindings[i].used && slot < 0)
			slot = i;
	}
	if (slot < 0 || (!g_group_friend_bindings[slot].used &&
			 group_count >= OMAQ_GROUP_PEERS - 1))
		return -1;
	memcpy(&previous, &g_group_friend_bindings[slot], sizeof(previous));
	g_group_friend_bindings[slot].used = 1;
	snprintf(g_group_friend_bindings[slot].group,
		 sizeof(g_group_friend_bindings[slot].group), "%s", group);
	memcpy(g_group_friend_bindings[slot].friend_key, friend_key, 65);
	memcpy(g_group_friend_bindings[slot].member_key, member_key, 65);
	if (persist && group_registry_save() < 0) {
		memcpy(&g_group_friend_bindings[slot], &previous, sizeof(previous));
		return -1;
	}
	return 0;
}

static void group_binding_drop(const char *group, const char *member_key)
{
	for (int i = 0; i < GROUP_FRIEND_BINDING_MAX; i++)
		if (g_group_friend_bindings[i].used &&
		    strcmp(g_group_friend_bindings[i].group, group) == 0 &&
		    (!member_key ||
		     strcmp(g_group_friend_bindings[i].member_key, member_key) == 0))
			memset(&g_group_friend_bindings[i], 0,
			       sizeof(g_group_friend_bindings[i]));
}

static int group_bind_invite_id_ok(const char *invite_id)
{
	if (!invite_id || strlen(invite_id) != 16)
		return 0;
	for (size_t i = 0; i < 16; i++)
		if (!((invite_id[i] >= '0' && invite_id[i] <= '9') ||
		      (invite_id[i] >= 'a' && invite_id[i] <= 'f')))
			return 0;
	return 1;
}

static int group_bind_pending_path(char *out, size_t n)
{
	return !out || snprintf(out, n, "%s/group-bind.pending", state_dir()) >= (int)n
		? -1 : 0;
}

static int group_bind_pending_save(void)
{
	char path[640], tmp[680];
	FILE *file = NULL;
	int fd = -1, rc = -1;
	int64_t now = (int64_t)time(NULL);

	if (group_bind_pending_path(path, sizeof(path)) != 0 ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >=
		    (int)sizeof(tmp))
		return -1;
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0 || !(file = fdopen(fd, "w"))) {
		if (fd >= 0)
			close(fd);
		unlink(tmp);
		return -1;
	}
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_bind_expected[i].used &&
		    (g_group_bind_expected[i].expires == 0 ||
		     g_group_bind_expected[i].expires > now) &&
		    fprintf(file, "E\t%s\t%s\t%s\t%s\t%lld\n",
			    g_group_bind_expected[i].group,
			    g_group_bind_expected[i].invite_id,
			    g_group_bind_expected[i].friend_key,
			    g_group_bind_expected[i].member_key[0]
				    ? g_group_bind_expected[i].member_key : "-",
			    (long long)g_group_bind_expected[i].expires) < 0)
			goto done;
	if (g_group_bind_proof.used &&
	    fprintf(file, "%c\t%s\t%s\t%s\t%s\t%d\t%lld\t0\n",
		    g_group_bind_proof.pending_accept ? 'A' : 'P',
		    g_group_bind_proof.group, g_group_bind_proof.invite_id,
		    g_group_bind_proof.friend_key,
		    g_group_bind_proof.pending_accept ? "-" : g_group_bind_proof.member_key,
		    g_group_bind_proof.direct_confirmed ? 1 : 0,
		    (long long)g_group_bind_proof.retry_after) < 0)
		goto done;
	if (fflush(file) != 0 || fsync(fileno(file)) != 0)
		goto done;
	if (fclose(file) != 0) {
		file = NULL;
		goto done;
	}
	file = NULL;
	if (rename(tmp, path) != 0 || fsync_directory(state_dir()) != 0)
		goto done;
	rc = 0;
done:
	if (file)
		fclose(file);
	if (rc != 0)
		unlink(tmp);
	return rc;
}

static int group_bind_proof_clear(void)
{
	unsigned char previous[sizeof(g_group_bind_proof)];
	int rc;

	memcpy(previous, &g_group_bind_proof, sizeof(previous));
	memset(&g_group_bind_proof, 0, sizeof(g_group_bind_proof));
	rc = group_bind_pending_save();
	if (rc != 0)
		memcpy(&g_group_bind_proof, previous, sizeof(previous));
	return rc;
}

static int group_bind_pending_load(void)
{
	char path[640], line[384];
	struct stat st;
	FILE *file;
	int fd, expected_count = 0, proof_count = 0;
	int64_t now = (int64_t)time(NULL);

	memset(g_group_bind_expected, 0, sizeof(g_group_bind_expected));
	memset(&g_group_bind_proof, 0, sizeof(g_group_bind_proof));
	if (group_bind_pending_path(path, sizeof(path)) != 0)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    st.st_size > 64 * 1024 || !(file = fdopen(fd, "r"))) {
		close(fd);
		return -1;
	}
	while (fgets(line, sizeof(line), file)) {
		char *fields[8] = {0};
		char *cursor = line;
		char *newline = strchr(line, '\n');
		int field_count = 0, is_expected, is_accept;
		char *end = NULL;
		long long expires, retry = 0;

		if (!newline || newline[1] != '\0')
			goto invalid;
		*newline = '\0';
		while (field_count < 8) {
			fields[field_count++] = cursor;
			cursor = strchr(cursor, '\t');
			if (!cursor)
				break;
			*cursor++ = '\0';
		}
		if (cursor || field_count < 4 ||
		    (strcmp(fields[0], "E") != 0 && strcmp(fields[0], "P") != 0 &&
		     strcmp(fields[0], "A") != 0) || !stable_group_id_syntax(fields[1]) ||
		    !group_bind_invite_id_ok(fields[2]) || !lower_hex_key_ok(fields[3]))
			goto invalid;
		is_expected = strcmp(fields[0], "E") == 0;
		is_accept = strcmp(fields[0], "A") == 0;
		if ((is_expected && field_count != 6) || (!is_expected && field_count != 8))
			goto invalid;
		errno = 0;
		expires = strtoll(fields[is_expected ? 5 : 7], &end, 10);
		if (errno || !end || *end || expires < 0 || expires > now + 86400)
			goto invalid;
		if (is_expected) {
			int has_member = strcmp(fields[4], "-") != 0;
			const char *bound_member;
			const char *bound_friend;
			if ((has_member && !lower_hex_key_ok(fields[4])) ||
			    (!has_member && expires == 0))
				goto invalid;
			if (!has_member && expires <= now)
				continue;
			bound_member = group_binding_member(fields[1], fields[3]);
			bound_friend = has_member
				? group_binding_friend(fields[1], fields[4]) : "";
			if ((bound_member[0] &&
			     (!has_member || strcmp(bound_member, fields[4]) != 0)) ||
			    (bound_friend[0] && strcmp(bound_friend, fields[3]) != 0))
				goto invalid;
			if (bound_member[0] && bound_friend[0])
				continue;
			if (++expected_count > GROUP_BIND_EXPECTED_MAX)
				goto invalid;
			for (int i = 0; i < expected_count - 1; i++)
				if (g_group_bind_expected[i].used &&
				    strcmp(g_group_bind_expected[i].group, fields[1]) == 0 &&
				    (strcmp(g_group_bind_expected[i].invite_id, fields[2]) == 0 ||
				     strcmp(g_group_bind_expected[i].friend_key, fields[3]) == 0 ||
				     (has_member && g_group_bind_expected[i].member_key[0] &&
				      strcmp(g_group_bind_expected[i].member_key, fields[4]) == 0)))
					goto invalid;
			g_group_bind_expected[expected_count - 1].used = 1;
			snprintf(g_group_bind_expected[expected_count - 1].group,
				 sizeof(g_group_bind_expected[expected_count - 1].group), "%s",
				 fields[1]);
			snprintf(g_group_bind_expected[expected_count - 1].invite_id,
				 sizeof(g_group_bind_expected[expected_count - 1].invite_id), "%s",
				 fields[2]);
			memcpy(g_group_bind_expected[expected_count - 1].friend_key,
			       fields[3], 65);
			if (has_member)
				memcpy(g_group_bind_expected[expected_count - 1].member_key,
				       fields[4], 65);
			g_group_bind_expected[expected_count - 1].friend = UINT32_MAX;
			g_group_bind_expected[expected_count - 1].expires =
				has_member ? 0 : expires;
		} else {
			errno = 0;
			retry = strtoll(fields[6], &end, 10);
			if (++proof_count > 1 ||
			    (is_accept ? strcmp(fields[4], "-") != 0
				       : !lower_hex_key_ok(fields[4])) ||
			    (strcmp(fields[5], "0") != 0 && strcmp(fields[5], "1") != 0) ||
			    (is_accept && strcmp(fields[5], "0") != 0) ||
			    errno || !end || *end || retry < 0 || retry > now + 300)
				goto invalid;
			g_group_bind_proof.used = 1;
			g_group_bind_proof.pending_accept = is_accept;
			g_group_bind_proof.friend = UINT32_MAX;
			memcpy(g_group_bind_proof.friend_key, fields[3], 65);
			if (!is_accept)
				memcpy(g_group_bind_proof.member_key, fields[4], 65);
			g_group_bind_proof.direct_confirmed = fields[5][0] == '1';
			snprintf(g_group_bind_proof.group, sizeof(g_group_bind_proof.group),
				 "%s", fields[1]);
			snprintf(g_group_bind_proof.invite_id,
				 sizeof(g_group_bind_proof.invite_id), "%s", fields[2]);
			g_group_bind_proof.expires = 0;
			g_group_bind_proof.retry_after = retry > now ? retry : 0;
		}
	}
	if (ferror(file) || fclose(file) != 0)
		return -1;
	return 0;

invalid:
	fclose(file);
	memset(g_group_bind_expected, 0, sizeof(g_group_bind_expected));
	memset(&g_group_bind_proof, 0, sizeof(g_group_bind_proof));
	return -1;
}

static int group_binding_expect(const char *group, const char *invite_id,
				const char *friend_key, uint32_t friend, int64_t expires)
{
	int slot = -1;
	int64_t now = (int64_t)time(NULL);

	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++) {
		if (g_group_bind_expected[i].used &&
		    !g_group_bind_expected[i].member_key[0] &&
		    g_group_bind_expected[i].expires > 0 &&
		    g_group_bind_expected[i].expires <= now)
			memset(&g_group_bind_expected[i], 0,
			       sizeof(g_group_bind_expected[i]));
		if (!g_group_bind_expected[i].used && slot < 0)
			slot = i;
	}
	if (slot < 0 || !stable_group_id_syntax(group) || !invite_id[0] ||
	    !lower_hex_key_ok(friend_key) || expires <= now || expires > now + 86400)
		return -1;
	g_group_bind_expected[slot].used = 1;
	snprintf(g_group_bind_expected[slot].group,
		 sizeof(g_group_bind_expected[slot].group), "%s", group);
	snprintf(g_group_bind_expected[slot].invite_id,
		 sizeof(g_group_bind_expected[slot].invite_id), "%s", invite_id);
	memcpy(g_group_bind_expected[slot].friend_key, friend_key, 65);
	g_group_bind_expected[slot].friend = friend;
	g_group_bind_expected[slot].expires = expires;
	if (group_bind_pending_save() != 0) {
		memset(&g_group_bind_expected[slot], 0,
		       sizeof(g_group_bind_expected[slot]));
		return -2;
	}
	return 0;
}

static int group_binding_forget_expect(const char *group, const char *invite_id)
{
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_bind_expected[i].used &&
		    strcmp(g_group_bind_expected[i].group, group) == 0 &&
		    strcmp(g_group_bind_expected[i].invite_id, invite_id) == 0) {
			unsigned char previous[sizeof(g_group_bind_expected[i])];
			memcpy(previous, &g_group_bind_expected[i], sizeof(previous));
			memset(&g_group_bind_expected[i], 0,
			       sizeof(g_group_bind_expected[i]));
			if (group_bind_pending_save() != 0) {
				memcpy(&g_group_bind_expected[i], previous, sizeof(previous));
				return -1;
			}
			return 0;
		}
	return 0;
}

static int group_binding_forget_member(const char *group, const char *member_key)
{
	unsigned char previous[sizeof(g_group_bind_expected)];
	int changed = 0;
	if (!group || !member_key || !member_key[0])
		return -1;
	memcpy(previous, g_group_bind_expected, sizeof(previous));
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_bind_expected[i].used &&
		    strcmp(g_group_bind_expected[i].group, group) == 0 &&
		    strcmp(g_group_bind_expected[i].member_key, member_key) == 0) {
			memset(&g_group_bind_expected[i], 0,
			       sizeof(g_group_bind_expected[i]));
			changed = 1;
		}
	if (changed && group_bind_pending_save() != 0) {
		memcpy(g_group_bind_expected, previous, sizeof(previous));
		return -1;
	}
	return 0;
}

static int schedule_group_binding_retirement(const char *group,
					      const char *member_key)
{
	int slot = -1;

	if (!stable_group_id_syntax(group) || !lower_hex_key_ok(member_key))
		return -1;
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++) {
		if (g_group_binding_retire[i].used &&
		    strcmp(g_group_binding_retire[i].group, group) == 0 &&
		    strcmp(g_group_binding_retire[i].member_key, member_key) == 0)
			return 0;
		if (!g_group_binding_retire[i].used && slot < 0)
			slot = i;
	}
	if (slot < 0)
		return -1;
	g_group_binding_retire[slot].used = 1;
	snprintf(g_group_binding_retire[slot].group,
		 sizeof(g_group_binding_retire[slot].group), "%s", group);
	memcpy(g_group_binding_retire[slot].member_key, member_key, 65);
	return 0;
}

static int group_binding_forget_friend(const char *friend_key)
{
	unsigned char previous_expected[sizeof(g_group_bind_expected)];
	unsigned char previous_proof[sizeof(g_group_bind_proof)];
	int changed = 0;

	if (!lower_hex_key_ok(friend_key))
		return -1;
	memcpy(previous_expected, g_group_bind_expected,
	       sizeof(previous_expected));
	memcpy(previous_proof, &g_group_bind_proof, sizeof(previous_proof));
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_bind_expected[i].used &&
		    strcmp(g_group_bind_expected[i].friend_key, friend_key) == 0) {
			memset(&g_group_bind_expected[i], 0,
			       sizeof(g_group_bind_expected[i]));
			changed = 1;
		}
	if (g_group_bind_proof.used &&
	    strcmp(g_group_bind_proof.friend_key, friend_key) == 0) {
		memset(&g_group_bind_proof, 0, sizeof(g_group_bind_proof));
		changed = 1;
	}
	if (changed && group_bind_pending_save() != 0) {
		memcpy(g_group_bind_expected, previous_expected,
		       sizeof(previous_expected));
		memcpy(&g_group_bind_proof, previous_proof,
		       sizeof(previous_proof));
		return -1;
	}
	return 0;
}

static int group_binding_establish(const char *group, const char *invite_id,
				   const char *friend_key, uint32_t friend,
				   const char *member_key)
{
	int slot = -1;
	uint32_t group_number, peer;

	if (!stable_group_id_syntax(group) || !group_bind_invite_id_ok(invite_id) ||
	    !lower_hex_key_ok(friend_key) || !lower_hex_key_ok(member_key) ||
	    omaq_group_id_parse(group, &group_number) != 0 ||
	    omaq_group_peer_for_key(group_number, member_key, &peer) != 0)
		return -1;
	{
		const char *bound_member = group_binding_member(group, friend_key);
		const char *bound_friend = group_binding_friend(group, member_key);
		if ((bound_member[0] && strcmp(bound_member, member_key) != 0) ||
		    (bound_friend[0] && strcmp(bound_friend, friend_key) != 0))
			return -1;
	}
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++) {
		if (!g_group_bind_expected[i].used) {
			if (slot < 0)
				slot = i;
			continue;
		}
		if (strcmp(g_group_bind_expected[i].group, group) != 0)
			continue;
		if (strcmp(g_group_bind_expected[i].invite_id, invite_id) == 0) {
			if (strcmp(g_group_bind_expected[i].friend_key, friend_key) != 0 ||
			    (g_group_bind_expected[i].member_key[0] &&
			     strcmp(g_group_bind_expected[i].member_key, member_key) != 0))
				return -1;
			slot = i;
			continue;
		}
		if (strcmp(g_group_bind_expected[i].friend_key, friend_key) == 0 ||
		    (g_group_bind_expected[i].member_key[0] &&
		     strcmp(g_group_bind_expected[i].member_key, member_key) == 0))
			return -1;
	}
	if (slot < 0)
		return -1;
	{
		unsigned char previous[sizeof(g_group_bind_expected[slot])];
		memcpy(previous, &g_group_bind_expected[slot], sizeof(previous));
		memset(&g_group_bind_expected[slot], 0,
		       sizeof(g_group_bind_expected[slot]));
		g_group_bind_expected[slot].used = 1;
		g_group_bind_expected[slot].friend = friend;
		g_group_bind_expected[slot].expires = 0;
		snprintf(g_group_bind_expected[slot].group,
			 sizeof(g_group_bind_expected[slot].group), "%s", group);
		snprintf(g_group_bind_expected[slot].invite_id,
			 sizeof(g_group_bind_expected[slot].invite_id), "%s", invite_id);
		memcpy(g_group_bind_expected[slot].friend_key, friend_key, 65);
		memcpy(g_group_bind_expected[slot].member_key, member_key, 65);
		if (group_bind_pending_save() != 0) {
			memcpy(&g_group_bind_expected[slot], previous, sizeof(previous));
			return -2;
		}
	}
	return slot;
}

static int group_binding_forget_group(const char *group)
{
	unsigned char previous_expected[sizeof(g_group_bind_expected)];
	unsigned char previous_proof[sizeof(g_group_bind_proof)];
	int changed = 0;

	if (!group || !group[0])
		return -1;
	memcpy(previous_expected, g_group_bind_expected,
	       sizeof(previous_expected));
	memcpy(previous_proof, &g_group_bind_proof, sizeof(previous_proof));
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_bind_expected[i].used &&
		    strcmp(g_group_bind_expected[i].group, group) == 0) {
			memset(&g_group_bind_expected[i], 0,
			       sizeof(g_group_bind_expected[i]));
			changed = 1;
		}
	if (g_group_bind_proof.used &&
	    strcmp(g_group_bind_proof.group, group) == 0) {
		memset(&g_group_bind_proof, 0, sizeof(g_group_bind_proof));
		changed = 1;
	}
	if (changed && group_bind_pending_save() != 0) {
		memcpy(g_group_bind_expected, previous_expected,
		       sizeof(previous_expected));
		memcpy(&g_group_bind_proof, previous_proof,
		       sizeof(previous_proof));
		return -1;
	}
	return 0;
}

static int group_binding_expected(const char *group, const char *invite_id)
{
	int64_t now = (int64_t)time(NULL);
	int changed = 0;

	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++) {
		if (g_group_bind_expected[i].used &&
		    !g_group_bind_expected[i].member_key[0] &&
		    g_group_bind_expected[i].expires > 0 &&
		    g_group_bind_expected[i].expires <= now) {
			memset(&g_group_bind_expected[i], 0,
			       sizeof(g_group_bind_expected[i]));
			changed = 1;
		}
		if (g_group_bind_expected[i].used &&
		    strcmp(g_group_bind_expected[i].group, group) == 0 &&
		    strcmp(g_group_bind_expected[i].invite_id, invite_id) == 0)
			return i;
	}
	if (changed)
		(void)group_bind_pending_save();
	return -1;
}

static void emit_groups(void)
{
	char ev[1600];
	uint32_t generation = ++g_group_generation;
	int groups = omaq_group_count();

	snprintf(ev, sizeof(ev),
		 "{\"event\":\"group.list.begin\",\"generation\":\"%u\"}", generation);
	emit(ev);
	for (int i = 0; i < groups; i++) {
		uint32_t gnum = omaq_group_number_at(i);
		char gid[OMAQ_GROUP_ID_MAX];
		char escaped_title[(OMAQ_GROUP_TITLE_MAX + 1) * 6];
		const char *title;
		int members;

		if (gnum == UINT32_MAX ||
		    omaq_group_refresh_id(g_tox, gnum, gid, sizeof(gid)) != 0)
			continue;
		(void)omaq_group_refresh_title(g_tox, gnum);
		title = omaq_group_title(gnum);
		if (!title[0])
			title = "Group";
		if (omaq_json_escape(title, escaped_title, sizeof(escaped_title)) != 0)
			continue;
		members = omaq_group_peer_count(gnum);
		if (omaq_group_limit(gnum) <= 0 || omaq_group_limit(gnum) > OMAQ_GROUP_PEERS)
			continue;
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"group.info\",\"generation\":\"%u\",\"group\":\"%s\",\"title\":\"%s\",\"members\":%d,\"limit\":%d}",
			 generation, gid, escaped_title, members, omaq_group_limit(gnum));
		emit(ev);
		for (int member = 0; member < members; member++) {
			uint32_t peer = omaq_group_peer_at(gnum, member);
			char escaped_name[(OMAQ_GROUP_MEMBER_NAME_MAX + 1) * 6];
			const char *friend_key, *member_key, *name;

			if (peer == UINT32_MAX)
				continue;
			if (omaq_group_peer_online(gnum, member))
				(void)omaq_group_refresh_member(g_tox, gnum, peer);
			name = omaq_group_peer_name(gnum, member);
			if (!name[0])
				name = omaq_group_peer_self(gnum, member) ? "You" : "Member";
			if (omaq_json_escape(name, escaped_name, sizeof(escaped_name)) != 0)
				continue;
			member_key = omaq_group_peer_key(gnum, member);
			friend_key = group_binding_friend(gid, member_key);
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"group.member\",\"generation\":\"%u\",\"group\":\"%s\",\"peer\":\"%u\",\"key\":\"%s\",\"friendKey\":\"%s\",\"name\":\"%s\",\"role\":\"%s\",\"online\":%s,\"self\":%s}",
				 generation, gid, peer, member_key, friend_key,
				 escaped_name, omaq_role_name(omaq_group_peer_cached_role(gnum, member)),
				 omaq_group_peer_online(gnum, member) ? "true" : "false",
				 omaq_group_peer_self(gnum, member) ? "true" : "false");
			emit(ev);
		}
	}
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"group.list.end\",\"generation\":\"%u\"}", generation);
	emit(ev);
}
#endif

static void emit_group(const char *gid, const char *action, uint32_t peer)
{
	char ev[160];
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"group.changed\",\"group\":\"%s\",\"action\":\"%s\",\"peer\":\"%u\"}",
		 gid, action, peer);
	emit(ev);
#ifdef HAVE_TOX
	if (g_group_registry_sync_warning) {
		emit_error("group_registry_sync_failed");
		g_group_registry_sync_warning = 0;
	}
	emit_groups();
#endif
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

static void emit_safety(uint32_t friend, const char *request)
{
	char self[65], peer[65], code[OMAQ_SAFETY_MAX], ev[480], conv[16];
	char escaped_request[160], request_field[192] = "";

	if (!g_tox)
		return;
	if (omaq_tox_self_pk_hex(g_tox, self) != 0)
		return;
	if (omaq_tox_friend_pk_hex(g_tox, friend, peer) != 0)
		return;
	if (omaq_safety_code(self, peer, code, sizeof(code)) != 0)
		return;
	snprintf(conv, sizeof(conv), "%u", friend);
	if (request && request[0] &&
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) == 0)
		snprintf(request_field, sizeof(request_field),
			 ",\"request\":\"%s\"", escaped_request);
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"safety\",\"conversation\":\"%s\",\"code\":\"%s\"%s}",
		 conv, code, request_field);
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
	char event[1200], generation[32];
	int n = 0;

	g_friend_generation++;
	if (g_friend_generation == 0)
		g_friend_generation = 1;
	snprintf(generation, sizeof(generation), "%llu",
		 (unsigned long long)g_friend_generation);
	snprintf(event, sizeof(event),
		 "{\"event\":\"friend.list.begin\",\"generation\":\"%s\"}",
		 generation);
	emit(event);
	if (g_tox)
		n = omaq_tox_friend_list(g_tox, list, 64);
	for (int i = 0; i < n; i++) {
		char name[129], escaped_name[280], id[16], friend_key[65], avatar[512],
			escaped_avatar[600];
		const char *online, *status;
		int friend_status;

		if (omaq_tox_friend_name(g_tox, list[i], name, sizeof(name)) != 0 ||
		    !omaq_group_member_name_bytes_ok(name, strlen(name)))
			snprintf(name, sizeof(name), "Friend %u", list[i]);
		if (omaq_json_escape(name, escaped_name, sizeof(escaped_name)) != 0) {
			snprintf(name, sizeof(name), "Friend %u", list[i]);
			if (omaq_json_escape(name, escaped_name,
					     sizeof(escaped_name)) != 0)
				return;
		}
		snprintf(id, sizeof(id), "%u", list[i]);
		if (omaq_tox_friend_pk_hex(g_tox, list[i], friend_key) != 0)
			friend_key[0] = '\0';
		friend_status = omaq_tox_friend_status(g_tox, list[i]);
		online = friend_status > 0 ? "true" : "false";
		status = friend_status == 2 ? "afk" :
			(friend_status == 1 ? "online" : "offline");
		if (omaq_avatar_dest(home_dir(), id, avatar, sizeof(avatar)) == 0 &&
		    access(avatar, R_OK) == 0 &&
		    omaq_json_escape(avatar, escaped_avatar, sizeof(escaped_avatar)) == 0)
			snprintf(event, sizeof(event),
				 "{\"event\":\"friend.info\",\"generation\":\"%s\",\"id\":\"%s\",\"key\":\"%s\",\"name\":\"%s\",\"avatar\":\"%s\",\"online\":%s,\"status\":\"%s\"}",
				 generation, id, friend_key, escaped_name, escaped_avatar, online,
				 status);
		else
			snprintf(event, sizeof(event),
				 "{\"event\":\"friend.info\",\"generation\":\"%s\",\"id\":\"%s\",\"key\":\"%s\",\"name\":\"%s\",\"online\":%s,\"status\":\"%s\"}",
				 generation, id, friend_key, escaped_name, online, status);
		emit(event);
	}
	snprintf(event, sizeof(event),
		 "{\"event\":\"friend.list.end\",\"generation\":\"%s\"}",
		 generation);
	emit(event);
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

static void hook_friend_status(void *ud, uint32_t friend, int online)
{
	(void)ud;
	(void)friend;
	(void)online;
	emit_friends();
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
	g_pending_announced = 0;
	if (g_issued_is_group) {
		g_pending_announced = 1;
		emit("{\"event\":\"request\",\"kind\":\"group\"}");
	} else if (!g_have_gauth && !g_have_gpending) {
		announce_pending_direct();
	}
}

static void hook_ginv(void *ud, uint32_t friend, const uint8_t *data, size_t len)
{
	char key[32];
	int64_t now = (int64_t)time(NULL);

	(void)ud;
	if (!data || len == 0 || len > sizeof(g_gpending_data) || !g_have_gauth ||
	    !omaq_group_invite_match(g_gauth_friend, friend, g_gauth_exp, now))
		return;
	snprintf(key, sizeof(key), "group:%u", friend);
	if (omaq_rate_allow(&g_rate, key, now) != 0 || g_have_gpending)
		return;
	memcpy(g_gpending_data, data, len);
	g_gpending_len = len;
	g_gpending_friend = friend;
	g_have_gpending = 1;
	g_gpending_announced = 1;
	emit("{\"event\":\"request\",\"kind\":\"group\"}");
}

static int group_cleanup_matches_current(int index)
{
	char chat_id[65], current_gid[OMAQ_GROUP_ID_MAX];

	if (index < 0 || index >= GROUP_CLEANUP_MAX || !g_group_cleanup[index].used)
		return -1;
	if (!g_group_cleanup[index].gid[0])
		return -1;
	if (!g_tox || omaq_tox_group_chat_id_hex(g_tox,
		g_group_cleanup[index].group, chat_id, sizeof(chat_id)) != 0 ||
	    snprintf(current_gid, sizeof(current_gid), "g:%s", chat_id) >=
		(int)sizeof(current_gid))
		return -1;
	return strcmp(current_gid, g_group_cleanup[index].gid) == 0;
}

static int group_cleanup_is_pending(uint32_t group)
{
	for (int i = 0; i < GROUP_CLEANUP_MAX; i++) {
		if (!g_group_cleanup[i].used || g_group_cleanup[i].group != group)
			continue;
		{
			int matches = group_cleanup_matches_current(i);
			if (matches == 0) {
				memset(&g_group_cleanup[i], 0, sizeof(g_group_cleanup[i]));
				continue;
			}
			return 1;
		}
	}
	return 0;
}

static void schedule_group_cleanup(uint32_t group, const char *gid)
{
	char chat_id[65], stable_gid[OMAQ_GROUP_ID_MAX];
	int free_slot = -1;

	if (!gid || !stable_group_id_syntax(gid)) {
		if (!g_tox || omaq_tox_group_chat_id_hex(g_tox, group, chat_id,
						     sizeof(chat_id)) != 0 ||
		    snprintf(stable_gid, sizeof(stable_gid), "g:%s", chat_id) >=
			(int)sizeof(stable_gid))
			return;
		gid = stable_gid;
	}
	for (int i = 0; i < GROUP_CLEANUP_MAX; i++) {
		if (g_group_cleanup[i].used && g_group_cleanup[i].group == group)
			return;
		if (!g_group_cleanup[i].used && free_slot < 0)
			free_slot = i;
	}
	if (free_slot < 0)
		return;
	g_group_cleanup[free_slot].used = 1;
	g_group_cleanup[free_slot].group = group;
	g_group_cleanup[free_slot].retry_after = (int64_t)time(NULL) + 2;
	if (gid)
		snprintf(g_group_cleanup[free_slot].gid,
			 sizeof(g_group_cleanup[free_slot].gid), "%s", gid);
}

static int prepare_group_reinvite(const char *chat_id)
{
	char gid[OMAQ_GROUP_ID_MAX];
	uint32_t group = UINT32_MAX, mapped = UINT32_MAX;
	int leave_rc;

	if (!chat_id || strlen(chat_id) != 64 ||
	    snprintf(gid, sizeof(gid), "g:%s", chat_id) >= (int)sizeof(gid))
		return -1;
	if (omaq_group_id_parse(gid, &mapped) == 0)
		return 1;
	if (!g_tox || omaq_tox_group_by_chat_id(g_tox, chat_id, &group) != 0)
		return 0;
	leave_rc = omaq_tox_group_leave(g_tox, group);
	if (leave_rc < 0)
		return -1;
	omaq_group_mark_dissolved(group);
	for (int i = 0; i < GROUP_CLEANUP_MAX; i++)
		if (g_group_cleanup[i].used && g_group_cleanup[i].group == group &&
		    strcmp(g_group_cleanup[i].gid, gid) == 0)
			memset(&g_group_cleanup[i], 0, sizeof(g_group_cleanup[i]));
	if (leave_rc > 0)
		emit_error("group_registry_sync_failed");
	return 0;
}

static int known_group_id(uint32_t gnum, char *gid, size_t n)
{
	char chat_id[65];
	uint32_t mapped;

	if (!gid || n < OMAQ_GROUP_ID_MAX || group_cleanup_is_pending(gnum) ||
	    omaq_tox_group_chat_id_hex(g_tox, gnum, chat_id, sizeof(chat_id)) != 0 ||
	    snprintf(gid, n, "g:%s", chat_id) >= (int)n ||
	    omaq_group_id_parse(gid, &mapped) != 0 || mapped != gnum)
		return -1;
	return 0;
}

static void hook_gmsg(void *ud, uint32_t gnum, uint32_t peer,
		      const uint8_t *message, size_t length)
{
	char gid[OMAQ_GROUP_ID_MAX], peer_from[96], sender[OMAQ_GROUP_MEMBER_KEY_HEX + 1],
		mid[97], wire_id[97], wire_reply[97], wire_text[1400], text_buffer[1400];
	const char *text;
	const char *display;
	int has_wire_id = 0;
	(void)ud;
	if (!omaq_group_message_bytes_ok(message, length))
		return;
	memcpy(text_buffer, message, length);
	text_buffer[length] = '\0';
	text = text_buffer;
	display = text;
	wire_reply[0] = '\0';
	sender[0] = '\0';
	if (known_group_id(gnum, gid, sizeof(gid)) != 0)
		return;
	(void)omaq_group_refresh_member(g_tox, gnum, peer);
	for (int member = 0; member < omaq_group_peer_count(gnum); member++) {
		if (omaq_group_peer_at(gnum, member) == peer) {
			snprintf(sender, sizeof(sender), "%s", omaq_group_peer_key(gnum, member));
			break;
		}
	}
	if (strlen(sender) != OMAQ_GROUP_MEMBER_KEY_HEX ||
	    snprintf(peer_from, sizeof(peer_from), "member:%s", sender) >=
	    (int)sizeof(peer_from))
		return;
	if (text && strncmp(text, "OQX1|gmb1|", 10) == 0) {
		const char *invite_id = text + 10;
		int expected;

		if (!group_bind_invite_id_ok(invite_id))
			return;
		expected = group_binding_expected(gid, invite_id);
		if (expected >= 0) {
			char bound_friend_key[65];
			int persist_rc;
			if (!g_group_bind_expected[expected].member_key[0] ||
			    strcmp(g_group_bind_expected[expected].member_key, sender) != 0)
				return;
			memcpy(bound_friend_key,
			       g_group_bind_expected[expected].friend_key, 65);
			if (group_binding_store(gid, bound_friend_key, sender, 0) != 0)
				return;
			persist_rc = group_registry_save();
			if (persist_rc != 0) {
				g_group_registry_retry = 1;
				g_group_registry_retry_after = (int64_t)time(NULL) + 2;
				emit_error_conv("group_registry_failed", gid);
				return;
			}
			if (group_binding_forget_expect(gid, invite_id) != 0)
				return;
#ifdef HAVE_SIGNAL
			{
				uint32_t ack_friend = UINT32_MAX;
				if (friend_for_addr(bound_friend_key, &ack_friend) == 0)
					(void)send_group_binding_ack(ack_friend, invite_id);
			}
#endif
			emit_groups();
		} else {
#ifdef HAVE_SIGNAL
			const char *bound_friend_key = group_binding_friend(gid, sender);
			uint32_t ack_friend = UINT32_MAX;
			if (bound_friend_key[0] &&
			    friend_for_addr(bound_friend_key, &ack_friend) == 0)
				(void)send_group_binding_ack(ack_friend, invite_id);
#endif
		}
		return;
	}
	if (text && strcmp(text, "OQX1|receipt-ack-v1") == 0) {
		note_receipt_capability(gid, sender);
		return;
	}
	{
		char confirm_id[97], confirm_state[16], confirm_target[65], self_key[65];
		if (omaq_receipt_confirm_wire_unpack(text, confirm_id, sizeof(confirm_id),
						     confirm_state, sizeof(confirm_state),
						     confirm_target, sizeof(confirm_target)) == 0) {
			if (omaq_control_rate_allow(&g_group_control_rate, 'r', gnum, sender,
						    (int64_t)time(NULL)) == 0 &&
			    group_self_member_key(gnum, self_key, sizeof(self_key)) == 0 &&
			    strcmp(confirm_target, self_key) == 0 &&
			    omaq_store_message_from_matches(home_dir(), gid, confirm_id,
							    peer_from) == 1)
				receipt_outbox_note_ack(gid, confirm_id);
			return;
		}
	}
	if (text && strncmp(text, "OQX1|", 5) == 0) {
		char reaction_id[80], reaction_emoji[32];
		if (strlen(sender) != 64 ||
		    omaq_message_reaction_wire_unpack(text, reaction_id, sizeof(reaction_id),
					      reaction_emoji, sizeof(reaction_emoji)) != 0 ||
		    omaq_control_rate_allow(&g_group_control_rate, 'x', gnum, sender,
					    (int64_t)time(NULL)) != 0)
			return;
		if (omaq_store_update_group_reaction(home_dir(), gid, reaction_id,
						     reaction_emoji, sender) == 0)
			emit_group_message_reaction(gid, reaction_id, reaction_emoji, sender);
		return;
	}
	if (text && strncmp(text, "OQE1|", 5) == 0) {
		char action_id[80], action_text[1400];
		if (strlen(sender) == 64 &&
		    omaq_message_edit_wire_unpack(text, action_id, sizeof(action_id), action_text,
					  sizeof(action_text)) == 0 &&
		    omaq_control_rate_allow(&g_group_control_rate, 'e', gnum, sender,
					    (int64_t)time(NULL)) == 0 &&
		    omaq_message_apply_edit_from(home_dir(), gid, action_id, action_text,
					 peer_from) == 0) {
			emit_message_update(gid, action_id, action_text, 0);
			return;
		}
		return;
	}
	if (text && strncmp(text, "OQD1|", 5) == 0) {
		char action_id[80];
		if (strlen(sender) == 64 &&
		    omaq_message_delete_wire_unpack(text, action_id, sizeof(action_id)) == 0 &&
		    omaq_control_rate_allow(&g_group_control_rate, 'd', gnum, sender,
					    (int64_t)time(NULL)) == 0 &&
		    omaq_message_apply_delete_from(home_dir(), gid, action_id, peer_from) == 0) {
			emit_message_update(gid, action_id, "", 1);
			return;
		}
		return;
	}
	{
		char receipt_id[97], receipt_state[16];
		if (omaq_receipt_wire_unpack(text, receipt_id, sizeof(receipt_id),
					     receipt_state, sizeof(receipt_state)) == 0) {
			int receipt_rc = -1;
			if (strlen(sender) == 64 &&
			    omaq_control_rate_allow(&g_group_control_rate, 'r', gnum, sender,
					    (int64_t)time(NULL)) == 0)
				receipt_rc = omaq_store_update_receipt_changed(home_dir(), gid,
									receipt_id, receipt_state);
			if (receipt_rc == 1)
				emit_receipt_event(gid, receipt_id, receipt_state);
			if (receipt_rc >= 0 && strcmp(receipt_state, "read") == 0 &&
			    receipt_ack_capable(gid, sender)) {
				char confirmation[256];
				if (omaq_receipt_confirm_wire_pack(confirmation, sizeof(confirmation),
							   receipt_id, "read", sender) == 0)
					(void)omaq_group_send(g_tox, gid, confirmation);
			}
			return;
		}
	}
	if (omaq_message_wire_unpack(text, wire_id, sizeof(wire_id), wire_reply, sizeof(wire_reply),
				     wire_text, sizeof(wire_text)) == 0) {
		display = wire_text;
		has_wire_id = 1;
		if (omaq_store_message_id_used(home_dir(), gid, wire_id) != 0 ||
		    omaq_message_append_id_reply(home_dir(), gid, peer_from, display, "in",
					 wire_id, wire_reply) != 0)
			return;
		snprintf(mid, sizeof(mid), "%s", wire_id);
	} else if (omaq_message_append_with_id(home_dir(), gid, peer_from, display, "in", mid, sizeof(mid)) != 0) {
		return;
	}
	(void)note_unread(gid);
	emit_group_message_event(gid, mid, wire_reply, display, sender);
	if (has_wire_id) {
		char receipt[180];
		if (omaq_receipt_wire_pack(receipt, sizeof(receipt), wire_id, "delivered") == 0)
			(void)omaq_group_send(g_tox, gid, receipt);
	}
}

static void persist_forced_group_removal(const char *gid)
{
	if (group_registry_save() >= 0)
		return;
	g_group_registry_retry = 1;
	g_group_registry_retry_after = (int64_t)time(NULL) + 2;
	emit_error_conv("group_registry_failed", gid ? gid : "");
}

static void hook_gpeer(void *ud, uint32_t gnum, uint32_t peer, int joined, int removed)
{
	char gid[OMAQ_GROUP_ID_MAX], removed_key[65] = "", peer_key[65] = "";
	char member_name[OMAQ_GROUP_MEMBER_NAME_MAX + 1] = "";
	int self = 0, member_known = 0, suppress_leave_notice = 0;
	(void)ud;
	if (known_group_id(gnum, gid, sizeof(gid)) != 0)
		return;
	if (removed) {
		self = removed == 2;
		for (int notice = 0; notice < GROUP_FRIEND_BINDING_MAX; notice++)
			if (g_group_leave_notice_suppress[notice].used &&
			    strcmp(g_group_leave_notice_suppress[notice].group, gid) == 0 &&
			    g_group_leave_notice_suppress[notice].peer == peer) {
				suppress_leave_notice =
					g_group_leave_notice_suppress[notice].expires >=
					(int64_t)time(NULL);
				memset(&g_group_leave_notice_suppress[notice], 0,
				       sizeof(g_group_leave_notice_suppress[notice]));
				break;
			}
		for (int member = 0; member < omaq_group_peer_count(gnum); member++)
			if (omaq_group_peer_at(gnum, member) == peer) {
				snprintf(removed_key, sizeof(removed_key), "%s",
					 omaq_group_peer_key(gnum, member));
				snprintf(member_name, sizeof(member_name), "%s",
					 omaq_group_peer_name(gnum, member));
				if (omaq_group_peer_self(gnum, member))
					self = 1;
				break;
			}
		group_binding_drop(gid, self ? NULL : removed_key);
		if (!self && group_binding_forget_member(gid, removed_key) != 0) {
			(void)schedule_group_binding_retirement(gid, removed_key);
			emit_error_conv("group_registry_failed", gid);
		}
		omaq_group_drop_peer(gnum, peer);
		if (self) {
			int leave_rc = omaq_tox_group_leave(g_tox, gnum);
			if (leave_rc < 0)
				schedule_group_cleanup(gnum, gid);
			else if (leave_rc > 0)
				emit_error("group_registry_sync_failed");
			else if (group_binding_forget_group(gid) != 0)
				emit_error_conv("group_registry_failed", gid);
			omaq_group_mark_dissolved(gnum);
			persist_forced_group_removal(gid);
			if (clear_unread(gid) != 0)
				emit_unread_failed(gid, "unread_persist_failed");
			{
				int transaction_rc = receipt_transaction_discard_conversation(gid);
				int outbox_rc = receipt_outbox_drop_conversation(gid);
				if (transaction_rc != 0) {
					g_receipt_outbox_invalid = 1;
					g_receipt_transaction_pending = 0;
				}
				if (transaction_rc != 0 || outbox_rc != 0)
					emit_error_conv("receipt_state_failed", gid);
			}
		} else {
			persist_forced_group_removal(gid);
			if (member_name[0] && !suppress_leave_notice)
				emit_group_membership_message(gid, member_name, 0);
		}
		emit_group(gid, self ? "leave" : "member.leave", peer);
		return;
	}
	if ((joined == 3 || joined == 4) &&
	    omaq_group_validate_limit(g_tox, gnum) != 0) {
		if (omaq_group_leave(g_tox, gid) != 0) {
			schedule_group_cleanup(gnum, gid);
			emit_error_conv("group_limit", gid);
			return;
		}
		persist_forced_group_removal(gid);
		emit_error_conv("group_limit", gid);
		emit_group(gid, "leave", 0);
		return;
	}
	if (omaq_group_refresh_title(g_tox, gnum) != 0) {
		if (joined == 3) {
			if (omaq_group_leave(g_tox, gid) == 0) {
				persist_forced_group_removal(gid);
				emit_group(gid, "leave", 0);
			} else {
				schedule_group_cleanup(gnum, gid);
				emit_error_conv("forbidden", gid);
			}
			emit_error_conv("group_capacity", gid);
		}
		return;
	}
	if (g_group_registry_pending &&
	    strcmp(g_group_registry_group, gid) == 0) {
		if (group_registry_save() < 0) {
			if (omaq_group_leave(g_tox, gid) == 0) {
				persist_forced_group_removal(gid);
				emit_group(gid, "leave", 0);
			} else {
				persist_forced_group_removal(gid);
				schedule_group_cleanup(gnum, gid);
				emit_error_conv("forbidden", gid);
			}
			return;
		}
		g_group_registry_pending = 0;
	}
	if (joined == 1) {
		size_t queried_name_len = 0;
		int queried_role = 0, queried_online = 0, queried_self = 0;
		if (omaq_tox_group_peer_info(g_tox, gnum, peer, peer_key,
					     sizeof(peer_key), member_name,
					     sizeof(member_name), &queried_name_len,
					     &queried_role, &queried_online,
					     &queried_self) == 0) {
			self = queried_self;
			for (int member = 0; member < omaq_group_peer_count(gnum); member++)
				if (strcmp(omaq_group_peer_key(gnum, member), peer_key) == 0) {
					member_known = 1;
					break;
				}
			if (group_binding_friend(gid, peer_key)[0])
				member_known = 1;
		}
	}
	for (int member = 0; member < omaq_group_peer_count(gnum); member++)
		if (omaq_group_peer_at(gnum, member) == peer) {
			member_known = 1;
			if (omaq_group_peer_self(gnum, member))
				self = 1;
		}
	if (joined > 0) {
		(void)omaq_group_refresh_member(g_tox, gnum, peer);
		for (int member = 0; member < omaq_group_peer_count(gnum); member++)
			if (omaq_group_peer_at(gnum, member) == peer) {
				if (omaq_group_peer_self(gnum, member))
					self = 1;
				snprintf(member_name, sizeof(member_name), "%s",
					 omaq_group_peer_name(gnum, member));
			}
	} else {
		omaq_group_mark_peer_offline(gnum, peer);
	}
	if (!self && !member_known && joined == 1)
		emit_group_membership_message(gid, member_name, 1);
	if (self && joined > 0 && g_group_bind_proof.used &&
	    strcmp(g_group_bind_proof.group, gid) == 0) {
		char proof[OMAQ_INVITE_ID_MAX + 12];
		if (g_group_bind_proof.direct_confirmed &&
		    snprintf(proof, sizeof(proof), "OQX1|gmb1|%s",
			     g_group_bind_proof.invite_id) < (int)sizeof(proof)) {
			(void)omaq_group_send(g_tox, gid, proof);
			g_group_bind_proof.retry_after = (int64_t)time(NULL) + 2;
		} else {
			g_group_bind_proof.retry_after = 0;
		}
		(void)group_bind_pending_save();
	}
	emit_group(gid, (joined == 1 || joined == 3) ?
		   (self ? "join" : "member.join") :
		   ((joined == 2 || joined == 4) ? "update" : "offline"), peer);
}

static void hook_msg(void *ud, uint32_t friend, const char *text)
{
	char conv[16], mid[97], wire_id[97], wire_reply[97], wire_text[1400];
	char receipt_id[97], receipt_state[16], reaction_id[97], reaction_emoji[32];
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
			finish_pending_group_invite(friend);
		}
		return;
	}
	if (!text || strncmp(text, "OQR1", 4) != 0 ||
	    !g_ratchet || omaq_ratchet_decrypt(g_ratchet, conv, text, plain, sizeof(plain)) != 0)
		return;
	text = plain;
#endif
	if (text && strncmp(text, "OQX1|gmbd|", 10) == 0) {
#ifdef HAVE_SIGNAL
		const char *invite_id = text + 10;
		const char *group_sep = strchr(invite_id, '|');
		const char *member_sep = group_sep ? strchr(group_sep + 1, '|') : NULL;
		char parsed_id[OMAQ_INVITE_ID_MAX + 1], group[OMAQ_GROUP_ID_MAX];
		char member_key[65], friend_key[65];
		int expected;
		if (!group_sep || !member_sep ||
		    (size_t)(group_sep - invite_id) != 16 ||
		    (size_t)(member_sep - group_sep - 1) != OMAQ_GROUP_ID_MAX - 1 ||
		    strlen(member_sep + 1) != 64)
			return;
		memcpy(parsed_id, invite_id, 16);
		parsed_id[16] = '\0';
		memcpy(group, group_sep + 1, OMAQ_GROUP_ID_MAX - 1);
		group[OMAQ_GROUP_ID_MAX - 1] = '\0';
		memcpy(member_key, member_sep + 1, 65);
		if (omaq_tox_friend_pk_hex(g_tox, friend, friend_key) != 0 ||
		    !lower_hex_key_ok(member_key))
			return;
		expected = group_binding_establish(group, parsed_id, friend_key,
						     friend, member_key);
		if (expected >= 0)
			(void)send_group_binding_ready(friend, parsed_id);
		else if (expected == -2)
			emit_error_conv("group_registry_failed", group);
#endif
		return;
	}
	if (text && strncmp(text, "OQX1|gmbc|", 10) == 0) {
		char friend_key[65], proof[OMAQ_INVITE_ID_MAX + 12];
		if (g_group_bind_proof.used &&
		    omaq_tox_friend_pk_hex(g_tox, friend, friend_key) == 0 &&
		    strcmp(friend_key, g_group_bind_proof.friend_key) == 0 &&
		    strcmp(text + 10, g_group_bind_proof.invite_id) == 0) {
			g_group_bind_proof.direct_confirmed = 1;
			g_group_bind_proof.retry_after = (int64_t)time(NULL) + 2;
			if (group_bind_pending_save() == 0 &&
			    snprintf(proof, sizeof(proof), "OQX1|gmb1|%s",
				     g_group_bind_proof.invite_id) < (int)sizeof(proof))
				(void)omaq_group_send(g_tox, g_group_bind_proof.group, proof);
		}
		return;
	}
	if (text && strncmp(text, "OQX1|gmba|", 10) == 0) {
		char friend_key[65];
		if (g_group_bind_proof.used &&
		    omaq_tox_friend_pk_hex(g_tox, friend, friend_key) == 0 &&
		    strcmp(friend_key, g_group_bind_proof.friend_key) == 0 &&
		    strcmp(text + 10, g_group_bind_proof.invite_id) == 0) {
			(void)group_bind_proof_clear();
		}
		return;
	}
	if (text && (strncmp(text, "OQGIA|", 6) == 0 ||
		     strncmp(text, "OQGIB|", 6) == 0)) {
		const char *invite_id = text + 6;
		const char *separator = strchr(invite_id, '|');
		if (separator && separator > invite_id &&
		    (size_t)(separator - invite_id) <= OMAQ_INVITE_ID_MAX &&
		    strlen(separator + 1) == OMAQ_GROUP_ID_MAX - 1 &&
		    separator[1] == 'g' && separator[2] == ':') {
			char response_id[OMAQ_INVITE_ID_MAX + 1];
			memcpy(response_id, invite_id, (size_t)(separator - invite_id));
			response_id[(size_t)(separator - invite_id)] = '\0';
			complete_pending_group_invite(friend, response_id, separator + 1,
						      text[4] == 'A');
		}
		return;
	}
	if (text && strncmp(text, "OQGI1|", 6) == 0) {
		omaq_invite invite;
		uint32_t mapped_friend = UINT32_MAX;
		int64_t now = (int64_t)time(NULL);

		if (omaq_invite_parse(text + 6, &invite) != 0 ||
		    invite.kind != INVITE_GROUP || omaq_invite_expired(&invite, now) ||
		    strlen(invite.group) != 64 ||
		    friend_for_addr(invite.tox_addr, &mapped_friend) != 0 ||
		    mapped_friend != friend)
			return;
		if (g_have_pending || g_have_gauth || g_have_gpending ||
		    g_group_bind_proof.used) {
			char group_id[OMAQ_GROUP_ID_MAX];
			snprintf(group_id, sizeof(group_id), "g:%s", invite.group);
			(void)send_group_invite_response(friend, "OQGIB", invite.id,
						 group_id);
			return;
		}
		{
			char group_id[OMAQ_GROUP_ID_MAX];
			int prepare_rc = prepare_group_reinvite(invite.group);
			if (prepare_rc != 0) {
				snprintf(group_id, sizeof(group_id), "g:%s", invite.group);
				(void)send_group_invite_response(friend, "OQGIB", invite.id,
							 group_id);
				return;
			}
		}
		g_have_gauth = 1;
		g_gauth_friend = friend;
		g_gauth_exp = invite.expiry;
		g_gauth_reservation_deadline = now + 30;
		snprintf(g_gauth_group, sizeof(g_gauth_group), "%s", invite.group);
		snprintf(g_gauth_invite_id, sizeof(g_gauth_invite_id), "%s", invite.id);
		{
			char group_id[OMAQ_GROUP_ID_MAX];
			snprintf(group_id, sizeof(group_id), "g:%s", invite.group);
			if (send_group_invite_response(friend, "OQGIA", invite.id,
						       group_id) != 0) {
				clear_group_auth();
				return;
			}
		}
		if (g_have_gpending && !g_gpending_announced &&
		    omaq_group_invite_match(g_gauth_friend, g_gpending_friend,
					g_gauth_exp, now)) {
			g_gpending_announced = 1;
			emit("{\"event\":\"request\",\"kind\":\"group\"}");
		}
		return;
	}
	if (text && strcmp(text, "OQX1|receipt-ack-v1") == 0) {
		char actor[65];
		if (receipt_capability_actor(friend, actor, sizeof(actor)) == 0)
			note_receipt_capability(conv, actor);
		return;
	}
	{
		char confirm_id[97], confirm_state[16], confirm_target[65];
		if (omaq_receipt_confirm_wire_unpack(text, confirm_id, sizeof(confirm_id),
						     confirm_state, sizeof(confirm_state),
						     confirm_target, sizeof(confirm_target)) == 0) {
			if (strcmp(confirm_target, "-") == 0)
				receipt_outbox_note_ack(conv, confirm_id);
			return;
		}
	}
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
		int receipt_rc = omaq_store_update_receipt_changed(home_dir(), conv,
							     receipt_id, receipt_state);
		if (receipt_rc == 1)
			emit_receipt_event(conv, receipt_id, receipt_state);
#ifdef HAVE_SIGNAL
		if (receipt_rc != -1 && strcmp(receipt_state, "read") == 0) {
			char actor[65];
			if (receipt_capability_actor(friend, actor, sizeof(actor)) == 0 &&
			    receipt_ack_capable(conv, actor))
				(void)send_receipt_confirm_wire(friend, conv, receipt_id);
		}
#endif
		return;
	}
	wire_reply[0] = '\0';
	if (omaq_message_wire_unpack(text, wire_id, sizeof(wire_id), wire_reply, sizeof(wire_reply),
				     wire_text, sizeof(wire_text)) == 0) {
		display = wire_text;
		has_wire_id = 1;
	}
	if (has_wire_id) {
		if (omaq_store_message_id_used(home_dir(), conv, wire_id) != 0 ||
		    omaq_message_append_id_reply(home_dir(), conv, "peer", display, "in",
					 wire_id, wire_reply) != 0)
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

static void emit_call_state(uint32_t friend, const char *state)
{
	char ev[160];

	snprintf(ev, sizeof(ev),
		 "{\"event\":\"call.state\",\"conversation\":\"%u\",\"state\":\"%s\"}",
		 friend, state);
	emit(ev);
}

static void hook_call(void *ud, uint32_t friend, int state)
{
	char ev[160];

	(void)ud;
	if (state == OMAQ_TOX_CALL_INCOMING) {
		int transition = omaq_av_note_incoming(friend);
		if (transition < 0) {
			(void)omaq_tox_av_hangup(g_tox, friend);
			if (!omaq_av_busy())
				g_av_reset_requested = 1;
		}
		if (transition != 1)
			return;
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"call.incoming\",\"conversation\":\"%u\"}", friend);
		emit(ev);
		return;
	}
	if (state == OMAQ_TOX_CALL_ACTIVE) {
		int transition = omaq_av_note_active(friend);
		if (transition < 0) {
			(void)omaq_tox_av_hangup(g_tox, friend);
			if (!omaq_av_busy())
				g_av_reset_requested = 1;
		} else if (transition == 1)
			emit_call_state(friend, "active");
		return;
	}
	if (omaq_av_note_end(friend) == 1) {
		g_av_reset_requested = 1;
		emit_call_state(friend, "ended");
	}
}

static void hook_audio(void *ud, uint32_t friend, const int16_t *pcm,
		       size_t samples, uint8_t channels, uint32_t rate)
{
	(void)ud;
	omaq_av_receive(friend, pcm, samples, channels, rate);
}

static void reset_call_transport(void)
{
	int64_t now = (int64_t)time(NULL);

	if (!g_av_reset_requested || !g_tox || omaq_av_busy() ||
	    now < g_av_reset_next)
		return;
	if (omaq_tox_av_reset(g_tox) != 0) {
		g_av_reset_next = now + 2;
		if (!g_av_reset_reported) {
			emit_error("audio_unavailable");
			g_av_reset_reported = 1;
		}
		return;
	}
	g_av_reset_requested = 0;
	g_av_reset_next = 0;
	g_av_reset_reported = 0;
	omaq_av_reset();
}

static void pump_call_audio(void)
{
	uint32_t friend = UINT32_MAX;
	char conv[16];

	if (!g_tox)
		return;
	(void)omaq_av_pump(g_tox);
	if (!omaq_av_take_audio_error(&friend) || friend == UINT32_MAX ||
	    !omaq_av_is_current(friend))
		return;
	snprintf(conv, sizeof(conv), "%u", friend);
	{
		int stopped = omaq_av_stop(g_tox, friend);
		if (stopped < 0)
			return;
		g_av_reset_requested = 1;
	}
	emit_error_conv("audio_unavailable", conv);
	emit_call_state(friend, "ended");
}

static int rand_id(char *out, size_t n)
{
	unsigned char b[8];
	static const char *d = "0123456789abcdef";
	size_t offset = 0;

	if (!out || n < sizeof(b) * 2 + 1)
		return -1;
	while (offset < sizeof(b)) {
		ssize_t got = getrandom(b + offset, sizeof(b) - offset, 0);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return -1;
		offset += (size_t)got;
	}
	for (size_t i = 0; i < sizeof(b); i++) {
		out[i * 2] = d[b[i] >> 4];
		out[i * 2 + 1] = d[b[i] & 0xf];
	}
	out[sizeof(b) * 2] = '\0';
	explicit_bzero(b, sizeof(b));
	return 0;
}
#endif

#ifdef HAVE_TOX
static void attach_hooks(void)
{
	if (!g_tox)
		return;
	omaq_tox_set_hooks(g_tox, hook_req, hook_msg, NULL);
	omaq_tox_set_presence_hook(g_tox, hook_presence, NULL);
	omaq_tox_set_friend_status_hook(g_tox, hook_friend_status, NULL);
	omaq_tox_set_typing_hook(g_tox, hook_typing, NULL);
	omaq_tox_set_group_hooks(g_tox, hook_ginv, hook_gmsg, hook_gpeer, NULL);
	omaq_tox_set_file_hooks(g_tox, hook_file_recv, hook_file_creq, hook_file_chunk,
				hook_file_ctrl, NULL);
	omaq_tox_set_avatar_hook(g_tox, hook_avatar, NULL);
	omaq_tox_set_call_hook(g_tox, hook_call, NULL);
	omaq_tox_set_audio_hook(g_tox, hook_audio, NULL);
}

static int legacy_group_id(const char *name)
{
	if (!name || name[0] != 'g' || name[1] < '0' || name[1] > '9')
		return 0;
	for (size_t i = 2; name[i]; i++)
		if (name[i] < '0' || name[i] > '9')
			return 0;
	return 1;
}

static FILE *open_bounded_regular(const char *path, off_t max_size)
{
	struct stat st;
	int fd;

	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return NULL;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    st.st_size > max_size) {
		close(fd);
		return NULL;
	}
	{
		FILE *file = fdopen(fd, "r");
		if (!file)
			close(fd);
		return file;
	}
}

static int legacy_group_state_present(void)
{
	static const char surface_prefix[] = "\"conversation\":\"g";
	char path[640], buffer[4096];
	DIR *dir;
	struct dirent *entry;
	struct stat st;
	FILE *file;

	if (snprintf(path, sizeof(path), "%s/history", home_dir()) < (int)sizeof(path) &&
	    lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && (dir = opendir(path)) != NULL) {
		while ((entry = readdir(dir)) != NULL) {
			if (legacy_group_id(entry->d_name)) {
				closedir(dir);
				return 1;
			}
		}
		closedir(dir);
	}
	if (snprintf(path, sizeof(path), "%s/unread.tsv", state_dir()) <
	    (int)sizeof(path) && (file = open_bounded_regular(path, 1024 * 1024)) != NULL) {
		while (fgets(buffer, sizeof(buffer), file)) {
			char *tab = strchr(buffer, '\t');
			if (tab) {
				*tab = '\0';
				if (legacy_group_id(buffer)) {
					fclose(file);
					return 1;
				}
			}
		}
		fclose(file);
	}
	if (snprintf(path, sizeof(path), "%s/surfaces.jsonl", state_dir()) <
	    (int)sizeof(path) && (file = open_bounded_regular(path, 1024 * 1024)) != NULL) {
		while (fgets(buffer, sizeof(buffer), file)) {
			for (char *found = buffer; (found = strstr(found, surface_prefix)); found++) {
				if (found[sizeof(surface_prefix) - 1] >= '0' &&
				    found[sizeof(surface_prefix) - 1] <= '9') {
					fclose(file);
					return 1;
				}
			}
		}
		fclose(file);
	}
	return 0;
}

static int group_registry_path(char *out, size_t n)
{
	return !out || snprintf(out, n, "%s/groups.tsv", home_dir()) >= (int)n
		? -1 : 0;
}

static int group_registry_transaction_path(char *out, size_t n)
{
	return !out || snprintf(out, n, "%s/group-registry.pending", home_dir()) >=
		(int)n ? -1 : 0;
}

static int group_registry_transaction_begin(const char *excluded_gid)
{
	char path[640], tmp[680];
	struct stat st;
	FILE *file = NULL;
	int fd = -1, rc = -1;

	if (!stable_group_id_syntax(excluded_gid) ||
	    group_registry_transaction_path(path, sizeof(path)) != 0 ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >=
		(int)sizeof(tmp))
		return -1;
	if (lstat(path, &st) == 0 || errno != ENOENT)
		return -1;
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0 || !(file = fdopen(fd, "w"))) {
		if (fd >= 0)
			close(fd);
		unlink(tmp);
		return -1;
	}
	if (fprintf(file, "R\t%s\n", excluded_gid) < 0 || fflush(file) != 0 ||
	    fsync(fileno(file)) != 0)
		goto done;
	if (fclose(file) != 0) {
		file = NULL;
		goto done;
	}
	file = NULL;
	if (rename(tmp, path) != 0 || fsync_directory(home_dir()) != 0)
		goto done;
	rc = 0;
done:
	if (file)
		fclose(file);
	if (rc != 0)
		unlink(tmp);
	return rc;
}

static int group_registry_transaction_clear(void)
{
	char path[640];

	if (group_registry_transaction_path(path, sizeof(path)) != 0 ||
	    (unlink(path) != 0 && errno != ENOENT) || fsync_directory(home_dir()) != 0)
		return -1;
	return 0;
}

static int group_registry_transaction_present(void)
{
	char path[640];
	struct stat st;

	if (group_registry_transaction_path(path, sizeof(path)) != 0)
		return 1;
	if (lstat(path, &st) != 0)
		return errno == ENOENT ? 0 : 1;
	return 1;
}

static int group_registry_filter_file(const char *path, const char *excluded_gid,
				      int bindings)
{
	char tmp[700], line[256], original[256];
	struct stat st;
	FILE *input = NULL, *output = NULL;
	int input_fd = -1, output_fd = -1, rc = -1;
	int first = 1;

	tmp[0] = '\0';
	input_fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (input_fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(input_fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    st.st_size > 64 * 1024 || !(input = fdopen(input_fd, "r"))) {
		close(input_fd);
		return -1;
	}
	if (snprintf(tmp, sizeof(tmp), "%s.recover.%ld", path, (long)getpid()) >=
	    (int)sizeof(tmp))
		goto done;
	unlink(tmp);
	output_fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
			 0600);
	if (output_fd < 0 || !(output = fdopen(output_fd, "w"))) {
		if (output_fd >= 0)
			close(output_fd);
		goto done;
	}
	while (fgets(line, sizeof(line), input)) {
		char *first_tab, *second_tab, *newline;
		int skip = 0;

		if (strlen(line) >= sizeof(original))
			goto done;
		snprintf(original, sizeof(original), "%s", line);
		if (bindings && first) {
			first = 0;
			if (strcmp(line, "OMAQGF1\n") != 0 || fputs(original, output) == EOF)
				goto done;
			continue;
		}
		first = 0;
		first_tab = strchr(line, '\t');
		second_tab = first_tab ? strchr(first_tab + 1, '\t') : NULL;
		newline = strchr(line, '\n');
		if (!first_tab || !newline || newline[1] != '\0')
			goto done;
		*first_tab++ = '\0';
		*newline = '\0';
		if (bindings) {
			if (!second_tab || strchr(second_tab + 1, '\t'))
				goto done;
			*second_tab++ = '\0';
			if (!stable_group_id_syntax(line) || !lower_hex_key_ok(first_tab) ||
			    !lower_hex_key_ok(second_tab))
				goto done;
			skip = strcmp(line, excluded_gid) == 0;
		} else {
			char gid[OMAQ_GROUP_ID_MAX];
			char *proof = second_tab;
			if (strlen(line) != 64)
				goto done;
			if (snprintf(gid, sizeof(gid), "g:%s", line) >= (int)sizeof(gid) ||
			    !stable_group_id_syntax(gid))
				goto done;
			if (proof) {
				*proof++ = '\0';
				if (strchr(proof, '\t') || !lower_hex_key_ok(proof))
					goto done;
			}
			if (!omaq_group_title_ok(first_tab))
				goto done;
			skip = strcmp(gid, excluded_gid) == 0;
		}
		if (!skip && fputs(original, output) == EOF)
			goto done;
	}
	if ((bindings && first) || ferror(input) || fflush(output) != 0 ||
	    fsync(fileno(output)) != 0)
		goto done;
	if (fclose(output) != 0) {
		output = NULL;
		goto done;
	}
	output = NULL;
	if (fclose(input) != 0) {
		input = NULL;
		goto done;
	}
	input = NULL;
	if (rename(tmp, path) != 0)
		goto done;
	rc = 0;
done:
	if (output)
		fclose(output);
	if (input)
		fclose(input);
	if (rc != 0 && tmp[0])
		unlink(tmp);
	return rc;
}

static int recover_group_registry_transaction(void)
{
	char marker[640], line[128], groups[640], bindings[640];
	struct stat st;
	FILE *file;
	int fd;

	if (group_registry_transaction_path(marker, sizeof(marker)) != 0)
		return -1;
	fd = open(marker, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    st.st_size > 256 || !(file = fdopen(fd, "r"))) {
		close(fd);
		return -1;
	}
	if (!fgets(line, sizeof(line), file) || strncmp(line, "R\t", 2) != 0 ||
	    !strchr(line, '\n') || strchr(line, '\n')[1] != '\0') {
		fclose(file);
		return -1;
	}
	line[strlen(line) - 1] = '\0';
	if (!stable_group_id_syntax(line + 2) || fgetc(file) != EOF ||
	    fclose(file) != 0)
		return -1;
	if (group_registry_path(groups, sizeof(groups)) != 0 ||
	    group_bindings_path(bindings, sizeof(bindings)) != 0 ||
	    group_registry_filter_file(groups, line + 2, 0) != 0 ||
	    group_registry_filter_file(bindings, line + 2, 1) != 0 ||
	    fsync_directory(home_dir()) != 0 || group_registry_transaction_clear() != 0)
		return -1;
	return 0;
}

static int group_registry_save_except(const char *excluded_gid)
{
	char path[640], tmp[680];
	FILE *file;
	int fd;

	if (group_registry_path(path, sizeof(path)) != 0 ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >=
	    (int)sizeof(tmp))
		return -1;
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0 || !(file = fdopen(fd, "w"))) {
		if (fd >= 0)
			close(fd);
		unlink(tmp);
		return -1;
	}
	for (int i = 0; i < omaq_group_count(); i++) {
		uint32_t group = omaq_group_number_at(i);
		char gid[OMAQ_GROUP_ID_MAX], proof[65];
		const char *title = omaq_group_title(group);

		if (group == UINT32_MAX ||
		    omaq_group_id_format(group, gid, sizeof(gid)) != 0)
			goto fail;
		if (group_cleanup_is_pending(group))
			continue;
		if (excluded_gid && strcmp(gid, excluded_gid) == 0)
			continue;
		if (!omaq_group_title_ok(title) ||
		    omaq_tox_group_registry_proof(g_tox, gid + 2, proof,
					 sizeof(proof)) != 0 ||
		    fprintf(file, "%s\t%s\t%s\n", gid + 2, title, proof) < 0)
			goto fail;
	}
	{
		int write_failed = fflush(file) != 0;
		if (!write_failed && fsync(fileno(file)) != 0)
			write_failed = 1;
		if (fclose(file) != 0)
			write_failed = 1;
		file = NULL;
		if (write_failed) {
			unlink(tmp);
			return -1;
		}
	}
	if (excluded_gid && group_registry_transaction_begin(excluded_gid) != 0) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) != 0) {
		unlink(tmp);
		if (excluded_gid)
			(void)group_registry_transaction_clear();
		return -1;
	}
	{
		int warning = 0;
		int bindings_rc;
		if (fsync_directory(home_dir()) != 0) {
			g_group_registry_sync_warning = 1;
			warning = 1;
		}
		bindings_rc = group_bindings_save_except(excluded_gid);
		if (bindings_rc < 0)
			return -1;
		if (bindings_rc > 0)
			warning = 1;
		if (excluded_gid && group_registry_transaction_clear() != 0)
			return -1;
		return warning;
	}

fail:
	fclose(file);
	unlink(tmp);
	return -1;
}

static int group_registry_save(void)
{
	return group_registry_save_except(NULL);
}

static void retry_group_binding_proof(void)
{
	char proof[OMAQ_INVITE_ID_MAX + 12];
	uint32_t inviter = UINT32_MAX;
	int64_t now = (int64_t)time(NULL);

	if (!g_group_bind_proof.used)
		return;
	if (g_group_bind_proof.pending_accept) {
		(void)recover_pending_group_accept();
		return;
	}
	if (g_group_bind_proof.retry_after > now)
		return;
	if (!g_group_bind_proof.direct_confirmed) {
#ifdef HAVE_SIGNAL
		if (friend_for_addr(g_group_bind_proof.friend_key, &inviter) == 0)
			(void)send_group_binding_confirmation(inviter,
				g_group_bind_proof.group, g_group_bind_proof.invite_id,
				g_group_bind_proof.member_key);
#endif
	} else if (snprintf(proof, sizeof(proof), "OQX1|gmb1|%s",
			    g_group_bind_proof.invite_id) < (int)sizeof(proof)) {
		(void)omaq_group_send(g_tox, g_group_bind_proof.group, proof);
	}
	g_group_bind_proof.retry_after = now + 2;
}

static void retry_group_binding_cleanup(void)
{
	uint32_t group_number, friend;

	if (g_group_binding_restore_pending) {
		if (group_bind_pending_save() == 0)
			g_group_binding_restore_pending = 0;
		return;
	}
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++)
		if (g_group_binding_retire[i].used) {
			if (group_binding_forget_member(
				    g_group_binding_retire[i].group,
				    g_group_binding_retire[i].member_key) == 0)
				memset(&g_group_binding_retire[i], 0,
				       sizeof(g_group_binding_retire[i]));
			return;
		}
	for (int i = 0; i < GROUP_BIND_EXPECTED_MAX; i++) {
		char group[OMAQ_GROUP_ID_MAX], friend_key[65];
		if (!g_group_bind_expected[i].used)
			continue;
		snprintf(group, sizeof(group), "%s",
			 g_group_bind_expected[i].group);
		memcpy(friend_key, g_group_bind_expected[i].friend_key, 65);
		if (omaq_group_id_parse(group, &group_number) != 0) {
			(void)group_binding_forget_group(group);
			return;
		}
		if (friend_for_addr(friend_key, &friend) != 0) {
			(void)group_binding_forget_friend(friend_key);
			return;
		}
	}
	if (g_group_bind_proof.used && !g_group_bind_proof.pending_accept) {
		char group[OMAQ_GROUP_ID_MAX], friend_key[65];
		snprintf(group, sizeof(group), "%s", g_group_bind_proof.group);
		memcpy(friend_key, g_group_bind_proof.friend_key, 65);
		if (omaq_group_id_parse(group, &group_number) != 0)
			(void)group_binding_forget_group(group);
		else if (friend_for_addr(friend_key, &friend) != 0)
			(void)group_binding_forget_friend(friend_key);
	}
	(void)group_binding_prune_expired();
}

static void retry_group_registry(void)
{
	int64_t now = (int64_t)time(NULL);

	if (!g_group_registry_retry || now < g_group_registry_retry_after)
		return;
	if (group_registry_transaction_present()) {
		if (recover_group_registry_transaction() == 0) {
			g_group_registry_retry = 0;
			g_group_registry_retry_after = 0;
		} else {
			g_group_registry_retry_after = now + 2;
		}
		return;
	}
	if (group_registry_save() >= 0) {
		g_group_registry_retry = 0;
		g_group_registry_retry_after = 0;
	} else {
		g_group_registry_retry_after = now + 2;
	}
}

static void retry_group_cleanup(void)
{
	int64_t now = (int64_t)time(NULL);

	if (!g_tox)
		return;
	if (group_registry_transaction_present()) {
		g_group_registry_retry = 1;
		g_group_registry_retry_after = now;
		return;
	}
	for (int i = 0; i < GROUP_CLEANUP_MAX; i++) {
		char gid[OMAQ_GROUP_ID_MAX];

		if (!g_group_cleanup[i].used ||
		    now < g_group_cleanup[i].retry_after)
			continue;
		{
			int matches = group_cleanup_matches_current(i);
			if (matches == 0) {
				memset(&g_group_cleanup[i], 0, sizeof(g_group_cleanup[i]));
				continue;
			}
			if (matches < 0) {
				g_group_cleanup[i].retry_after = now + 2;
				continue;
			}
		}
		{
			int leave_rc = omaq_tox_group_leave(g_tox, g_group_cleanup[i].group);
			if (leave_rc < 0) {
				g_group_cleanup[i].retry_after = now + 2;
				continue;
			}
			if (leave_rc > 0)
				emit_error("group_registry_failed");
		}
		omaq_group_mark_dissolved(g_group_cleanup[i].group);
		snprintf(gid, sizeof(gid), "%s", g_group_cleanup[i].gid);
		memset(&g_group_cleanup[i], 0, sizeof(g_group_cleanup[i]));
		if (gid[0]) {
			group_binding_drop(gid, NULL);
			if (group_registry_save_except(gid) < 0) {
				g_group_registry_retry = 1;
				g_group_registry_retry_after = now + 2;
				emit_error_conv("group_registry_failed", gid);
			}
			if (group_binding_forget_group(gid) != 0)
				emit_error_conv("group_registry_failed", gid);
		} else {
			persist_forced_group_removal(gid);
		}
		if (gid[0]) {
			if (clear_unread(gid) != 0)
				emit_unread_failed(gid, "unread_persist_failed");
			emit_group(gid, "leave", 0);
		}
		if (group_registry_transaction_present())
			return;
	}
	(void)queue_unregistered_groups();
}

static int group_bindings_path(char *out, size_t n)
{
	return !out || snprintf(out, n, "%s/group-friends.tsv", home_dir()) >= (int)n
		? -1 : 0;
}

static int group_bindings_save_except(const char *excluded_gid)
{
	char path[640], tmp[680];
	FILE *file = NULL;
	int fd = -1, rc = -1;

	if (group_bindings_path(path, sizeof(path)) != 0 ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >=
		    (int)sizeof(tmp))
		return -1;
	unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0 || !(file = fdopen(fd, "w"))) {
		if (fd >= 0)
			close(fd);
		unlink(tmp);
		return -1;
	}
	if (fputs("OMAQGF1\n", file) == EOF)
		goto done;
	for (int i = 0; i < GROUP_FRIEND_BINDING_MAX; i++) {
		uint32_t group_number;
		if (!g_group_friend_bindings[i].used ||
		    (excluded_gid &&
		     strcmp(g_group_friend_bindings[i].group, excluded_gid) == 0) ||
		    omaq_group_id_parse(g_group_friend_bindings[i].group,
					&group_number) != 0)
			continue;
		if (fprintf(file, "%s\t%s\t%s\n",
			    g_group_friend_bindings[i].group,
			    g_group_friend_bindings[i].friend_key,
			    g_group_friend_bindings[i].member_key) < 0)
			goto done;
	}
	if (fflush(file) != 0 || fsync(fileno(file)) != 0)
		goto done;
	if (fclose(file) != 0) {
		file = NULL;
		goto done;
	}
	file = NULL;
	if (rename(tmp, path) != 0)
		goto done;
	if (fsync_directory(home_dir()) != 0) {
		g_group_registry_sync_warning = 1;
		rc = 1;
	} else {
		rc = 0;
	}
	done:
	if (file)
		fclose(file);
	if (rc < 0)
		unlink(tmp);
	return rc;
}

static void remember_pruned_group(const char *chat_id)
{
	if (!chat_id || strlen(chat_id) != 64 ||
	    g_group_registry_pruned_count >= OMAQ_GROUPS_MAX)
		return;
	(void)snprintf(g_group_registry_pruned_ids[g_group_registry_pruned_count],
		       OMAQ_GROUP_ID_MAX, "g:%s", chat_id);
	g_group_registry_pruned_count++;
}

static int group_was_pruned(const char *group)
{
	for (int i = 0; i < g_group_registry_pruned_count; i++)
		if (strcmp(g_group_registry_pruned_ids[i], group) == 0)
			return 1;
	return 0;
}

static int group_bindings_load(void)
{
	char path[640], line[256];
	char seen_groups[GROUP_FRIEND_BINDING_MAX][OMAQ_GROUP_ID_MAX];
	char seen_friends[GROUP_FRIEND_BINDING_MAX][65];
	char seen_members[GROUP_FRIEND_BINDING_MAX][65];
	struct stat st;
	FILE *file;
	int fd, entries = 0;

	memset(g_group_friend_bindings, 0, sizeof(g_group_friend_bindings));
	if (group_bindings_path(path, sizeof(path)) != 0)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    st.st_size > 64 * 1024 || !(file = fdopen(fd, "r"))) {
		close(fd);
		return -1;
	}
	if (!fgets(line, sizeof(line), file) || strcmp(line, "OMAQGF1\n") != 0)
		goto invalid;
	while (fgets(line, sizeof(line), file)) {
		char *friend_key = strchr(line, '\t');
		char *member_key = friend_key ? strchr(friend_key + 1, '\t') : NULL;
		char *newline = strchr(line, '\n');
		uint32_t group_number;

		if (++entries > GROUP_FRIEND_BINDING_MAX || !friend_key || !member_key ||
		    strchr(member_key + 1, '\t') || !newline || newline[1] != '\0')
			goto invalid;
		*friend_key++ = '\0';
		*member_key++ = '\0';
		*newline = '\0';
		if (!stable_group_id_syntax(line) || !lower_hex_key_ok(friend_key) ||
		    !lower_hex_key_ok(member_key))
			goto invalid;
		{
			int group_count = 0;
			for (int i = 0; i < entries - 1; i++)
				if (strcmp(seen_groups[i], line) == 0) {
					group_count++;
					if (strcmp(seen_friends[i], friend_key) == 0 ||
					    strcmp(seen_members[i], member_key) == 0)
						goto invalid;
				}
			if (group_count >= OMAQ_GROUP_PEERS - 1)
				goto invalid;
			memcpy(seen_groups[entries - 1], line, OMAQ_GROUP_ID_MAX);
			memcpy(seen_friends[entries - 1], friend_key, 65);
			memcpy(seen_members[entries - 1], member_key, 65);
		}
		if (omaq_group_id_parse(line, &group_number) != 0) {
			if (group_was_pruned(line))
				continue;
			goto invalid;
		}
		if (group_binding_member(line, friend_key)[0] ||
		    group_binding_friend(line, member_key)[0] ||
		    group_binding_store(line, friend_key, member_key, 0) != 0)
			goto invalid;
	}
	if (ferror(file) || fclose(file) != 0)
		return -1;
	return 0;

invalid:
	fclose(file);
	memset(g_group_friend_bindings, 0, sizeof(g_group_friend_bindings));
	return -1;
}

static int identity_group_files_validate(struct omaq_tox *candidate, const char *home)
{
	char path[700], line[256], ids[OMAQ_GROUPS_MAX][65];
	char binding_groups[GROUP_FRIEND_BINDING_MAX][OMAQ_GROUP_ID_MAX];
	char binding_friends[GROUP_FRIEND_BINDING_MAX][65];
	char binding_members[GROUP_FRIEND_BINDING_MAX][65];
	struct stat st;
	FILE *file;
	int fd, groups = 0, bindings = 0;

	if (!candidate || !home ||
	    snprintf(path, sizeof(path), "%s/groups.tsv", home) >= (int)sizeof(path))
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd >= 0) {
		if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
		    st.st_size > 4096 || !(file = fdopen(fd, "r"))) {
			close(fd);
			return -1;
		}
		while (fgets(line, sizeof(line), file)) {
			char *title = strchr(line, '\t');
			char *proof = title ? strchr(title + 1, '\t') : NULL;
			char *newline = strchr(line, '\n');
			char expected[65];

			if (groups >= OMAQ_GROUPS_MAX || !title || title == line ||
			    (proof && strchr(proof + 1, '\t')) || !newline || newline[1] != '\0')
				goto invalid_groups;
			*title++ = '\0';
			*newline = '\0';
			if (proof)
				*proof++ = '\0';
			if (!lower_hex_key_ok(line) || !omaq_group_title_ok(title) ||
			    (proof && !lower_hex_key_ok(proof)) ||
			    omaq_tox_group_registry_proof(candidate, line, expected,
							 sizeof(expected)) != 0 ||
			    (proof && strcmp(proof, expected) != 0))
				goto invalid_groups;
			for (int i = 0; i < groups; i++)
				if (strcmp(ids[i], line) == 0)
					goto invalid_groups;
			memcpy(ids[groups++], line, 65);
		}
		if (ferror(file) || fclose(file) != 0)
			return -1;
	} else if (errno != ENOENT) {
		return -1;
	}

	if (snprintf(path, sizeof(path), "%s/group-friends.tsv", home) >=
		    (int)sizeof(path))
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    st.st_size > 64 * 1024 || !(file = fdopen(fd, "r"))) {
		close(fd);
		return -1;
	}
	if (!fgets(line, sizeof(line), file) || strcmp(line, "OMAQGF1\n") != 0)
		goto invalid_bindings;
	while (fgets(line, sizeof(line), file)) {
		char *friend_key = strchr(line, '\t');
		char *member_key = friend_key ? strchr(friend_key + 1, '\t') : NULL;
		char *newline = strchr(line, '\n');
		int known_group = 0, group_bindings = 0;

		if (bindings >= GROUP_FRIEND_BINDING_MAX || !friend_key || !member_key ||
		    strchr(member_key + 1, '\t') || !newline || newline[1] != '\0')
			goto invalid_bindings;
		*friend_key++ = '\0';
		*member_key++ = '\0';
		*newline = '\0';
		if (!stable_group_id_syntax(line) || !lower_hex_key_ok(friend_key) ||
		    !lower_hex_key_ok(member_key))
			goto invalid_bindings;
		for (int i = 0; i < groups; i++)
			if (strcmp(ids[i], line + 2) == 0) {
				known_group = 1;
				break;
			}
		if (!known_group)
			goto invalid_bindings;
		for (int i = 0; i < bindings; i++)
			if (strcmp(binding_groups[i], line) == 0) {
				group_bindings++;
				if (strcmp(binding_friends[i], friend_key) == 0 ||
				    strcmp(binding_members[i], member_key) == 0)
					goto invalid_bindings;
			}
		if (group_bindings >= OMAQ_GROUP_PEERS - 1)
			goto invalid_bindings;
		memcpy(binding_groups[bindings], line, OMAQ_GROUP_ID_MAX);
		memcpy(binding_friends[bindings], friend_key, 65);
		memcpy(binding_members[bindings], member_key, 65);
		bindings++;
	}
	if (ferror(file) || fclose(file) != 0)
		return -1;
	return 0;

invalid_groups:
	fclose(file);
	return -1;
invalid_bindings:
	fclose(file);
	return -1;
}

static int group_registry_reconcile(void)
{
	char path[640], line[192];
	char ids[OMAQ_GROUPS_MAX][65];
	char proofs[OMAQ_GROUPS_MAX][65];
	struct stat st;
	FILE *file;
	int fd;
	int entries = 0;

	g_group_registry_pruned_count = 0;
	memset(g_group_registry_pruned_ids, 0,
	       sizeof(g_group_registry_pruned_ids));
	if (group_registry_path(path, sizeof(path)) != 0)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (fd < 0)
		return errno == ENOENT ? group_bindings_load() : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    st.st_size > 4096 || !(file = fdopen(fd, "r"))) {
		close(fd);
		return -1;
	}
	while (fgets(line, sizeof(line), file)) {
		char *tab = strchr(line, '\t');
		char *proof = tab ? strchr(tab + 1, '\t') : NULL;
		char *newline = strchr(line, '\n');

		if (entries >= OMAQ_GROUPS_MAX || !tab || tab == line ||
		    (proof && strchr(proof + 1, '\t')) || !newline || newline[1] != '\0')
			goto invalid;
		*tab = '\0';
		*newline = '\0';
		if (proof)
			*proof++ = '\0';
		if (strlen(line) != 64 || !omaq_group_title_ok(tab + 1) ||
		    (proof && strlen(proof) != 64))
			goto invalid;
		for (size_t i = 0; i < 64; i++) {
			if (!((line[i] >= '0' && line[i] <= '9') ||
			      (line[i] >= 'a' && line[i] <= 'f')))
				goto invalid;
			if (proof && !((proof[i] >= '0' && proof[i] <= '9') ||
				       (proof[i] >= 'a' && proof[i] <= 'f')))
				goto invalid;
		}
		for (int i = 0; i < entries; i++)
			if (strcmp(ids[i], line) == 0)
				goto invalid;
		memcpy(ids[entries], line, 65);
		if (proof)
			memcpy(proofs[entries], proof, 65);
		else
			proofs[entries][0] = '\0';
		entries++;
	}
	if (ferror(file) || fclose(file) != 0)
		return -1;

	for (int i = 0; i < entries; i++) {
		char gid[OMAQ_GROUP_ID_MAX], expected_proof[65];
		uint32_t group = UINT32_MAX, existing, self_peer = UINT32_MAX;

		if (snprintf(gid, sizeof(gid), "g:%s", ids[i]) >= (int)sizeof(gid))
			return -1;
		if (omaq_tox_group_registry_proof(g_tox, ids[i], expected_proof,
						 sizeof(expected_proof)) != 0 ||
		    (proofs[i][0] && strcmp(proofs[i], expected_proof) != 0)) {
			g_group_registry_pruned = 1;
			remember_pruned_group(ids[i]);
			continue;
		}
		if (omaq_group_id_parse(gid, &existing) == 0)
			continue;
		if (omaq_tox_group_by_chat_id(g_tox, ids[i], &group) != 0) {
			g_group_registry_pruned = 1;
			remember_pruned_group(ids[i]);
			continue;
		}
		if (omaq_group_refresh_id(g_tox, group, gid, sizeof(gid)) != 0 ||
		    omaq_group_refresh_title(g_tox, group) != 0 ||
		    omaq_group_validate_limit(g_tox, group) != 0) {
			{
				int leave_rc = omaq_tox_group_leave(g_tox, group);
				if (leave_rc >= 0) {
					omaq_group_mark_dissolved(group);
					if (leave_rc > 0)
						g_group_registry_sync_warning = 1;
				} else {
					schedule_group_cleanup(group, gid);
				}
			}
			g_group_registry_pruned = 1;
			g_group_registry_unmapped = 1;
			remember_pruned_group(ids[i]);
			continue;
		}
		if (omaq_tox_group_self_peer(g_tox, group, &self_peer) == 0)
			(void)omaq_group_refresh_member(g_tox, group, self_peer);
	}
	return group_bindings_load();

invalid:
	fclose(file);
	return -1;
}

static int queue_unregistered_groups(void)
{
	uint32_t groups[64];
	size_t count = 0;

	if (!g_tox || omaq_tox_group_numbers(g_tox, groups,
					      sizeof(groups) / sizeof(groups[0]),
					      &count) != 0)
		return -1;
	for (size_t i = 0; i < count; i++) {
		char gid[OMAQ_GROUP_ID_MAX];
		if (omaq_group_id_format(groups[i], gid, sizeof(gid)) != 0)
			schedule_group_cleanup(groups[i], NULL);
	}
	return 0;
}

static int rebuild_group_cache(void)
{
	size_t tox_groups = 0;

	if (!g_tox || omaq_tox_group_count(g_tox, &tox_groups) != 0 ||
	    group_registry_reconcile() != 0 || queue_unregistered_groups() != 0)
		return -1;
	if ((size_t)omaq_group_count() < tox_groups)
		g_group_registry_unmapped = 1;
	if (legacy_group_state_present())
		g_group_registry_unmapped = 1;
	{
		int save_rc = group_registry_save();
		return save_rc < 0 ? -1 : 0;
	}
}

static int recover_pending_group_accept(void)
{
	uint32_t group = UINT32_MAX, friend = UINT32_MAX;
	char self_key[65];
	int leave_rc;

	if (!g_group_bind_proof.used)
		return 0;
	if (!g_tox || !stable_group_id_syntax(g_group_bind_proof.group))
		return -1;
	if (!g_group_bind_proof.pending_accept &&
	    omaq_group_id_parse(g_group_bind_proof.group, &group) == 0 &&
	    group_self_member_key(group, self_key, sizeof(self_key)) == 0 &&
	    strcmp(self_key, g_group_bind_proof.member_key) == 0 &&
	    friend_for_addr(g_group_bind_proof.friend_key, &friend) == 0)
		return 0;
	g_group_bind_proof.pending_accept = 1;
	g_group_bind_proof.member_key[0] = '\0';
	g_group_bind_proof.direct_confirmed = 0;
	g_group_bind_proof.retry_after = 0;
	group = UINT32_MAX;
	if (omaq_tox_group_by_chat_id(g_tox, g_group_bind_proof.group + 2,
				      &group) == 0) {
		leave_rc = omaq_tox_group_leave(g_tox, group);
		if (leave_rc != 0)
			return -1;
		omaq_group_mark_dissolved(group);
	}
	group_binding_drop(g_group_bind_proof.group, NULL);
	if (group_registry_save() < 0)
		return -1;
	return group_bind_proof_clear();
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
	if (recover_group_registry_transaction() != 0 ||
	    rebuild_group_cache() != 0 || group_bind_pending_load() != 0 ||
	    recover_pending_group_accept() != 0) {
		omaq_tox_discard(g_tox);
		g_tox = NULL;
		return -1;
	}
	if (!omaq_tox_av_available(g_tox)) {
		g_av_reset_requested = 1;
		g_av_reset_next = (int64_t)time(NULL) + 2;
		g_av_reset_reported = 0;
	}
	return 0;
}
#endif

static int handle_op(const omaq_op *op, int *identity_ready)
{
#ifdef HAVE_TOX
	if (g_identity_recovery_required) {
		if (targeted_group_invite_op(op))
			emit_group_invite_terminal(op, "identity_changed");
		else if (direct_invite_action_op(op))
			emit_identity_error("identity_changed", op->request);
		return 0;
	}
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
		char addr[77], nickname[129], escaped_nickname[260], call_field[160];
		char ev[1200];
		uint32_t call_friend = UINT32_MAX;
		const char *call_state = NULL;

		if (omaq_av_status(&call_friend, &call_state))
			snprintf(call_field, sizeof(call_field),
				 ",\"call\":{\"conversation\":\"%u\",\"state\":\"%s\"}",
				 call_friend, call_state);
		else
			snprintf(call_field, sizeof(call_field), ",\"call\":null");
		if (g_locked && !g_tox) {
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"locked\":true,\"instance\":\"%s\",\"call\":null%s}",
				 OMAQ_PROTOCOL_VERSION, omaq_unread_total(&g_unread), g_instance_id,
				 request_field);
			emit(ev);
			emit_invite_state("", 0, "status", NULL);
			emit_all_unread();
			return 0;
		}
		if (g_tox && omaq_tox_self_addr_hex(g_tox, addr) == 0) {
			if (omaq_tox_self_name(g_tox, nickname, sizeof(nickname)) != 0 ||
			    omaq_json_escape(nickname, escaped_nickname, sizeof(escaped_nickname)) != 0)
				escaped_nickname[0] = '\0';
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"online\":%s,\"addr\":\"%s\",\"nickname\":\"%s\",\"protected\":%s,\"instance\":\"%s\"%s%s}",
				 OMAQ_PROTOCOL_VERSION, omaq_unread_total(&g_unread),
				 omaq_tox_online(g_tox) ? "true" : "false", addr,
				 escaped_nickname, omaq_identity_protected(g_tox) ? "true" : "false",
				 g_instance_id, call_field, request_field);
			emit(ev);
			emit_friends();
			emit_groups();
			emit_self_avatar();
			if (g_issued_url[0] && g_issued_exp > (int64_t)time(NULL))
				emit_invite_state(g_issued_url, g_issued_exp, "status", NULL);
			else {
				clear_invite();
				emit_invite_state("", 0, "status", NULL);
			}
			emit_all_unread();
#ifdef HAVE_SIGNAL
			replay_group_invite_results();
#endif
			return 0;
		}
#endif
		{
			char ev[640];
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"conversations\":[],\"instance\":\"%s\",\"call\":null%s}",
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
		else if (strncmp(op->op, "identity.", 9) == 0)
			emit_identity_error("identity_changed", op->id);
		else if (targeted_group_invite_op(op))
			emit_group_invite_terminal(op, "identity_changed");
		else if (direct_invite_action_op(op))
			emit_identity_error("identity_changed", op->request);
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
		else if (strncmp(op->op, "identity.", 9) == 0)
			emit_identity_error("locked", op->id);
		else if (targeted_group_invite_op(op))
			emit_group_invite_terminal(op, "locked");
		else if (direct_invite_action_op(op))
			emit_identity_error("locked", op->request);
		else
			emit_error("locked");
		return 0;
	}
	if (strcmp(op->op, "identity.unlock") == 0) {
		int rc;

		if (g_tox) {
			emit_identity_action("unlock", op->id, NULL, -1);
			return 0;
		}
		if (!omaq_identity_pass_ok(op->passphrase)) {
			emit_identity_error("forbidden", op->id);
			return 0;
		}
		rc = load_tox(op->passphrase);
		if (rc != 0) {
			emit_identity_error("locked", op->id);
			return 0;
		}
		rc = prune_unavailable_unread();
		if (rc < 0) {
			snprintf(g_unread_error_code, sizeof(g_unread_error_code),
				 "unread_persist_failed");
			emit_unread_failed("", g_unread_error_code);
		} else if (rc > 0) {
			emit_all_unread();
		}
		emit_identity_action("unlock", op->id, NULL, -1);
		return 0;
	}
	if (strcmp(op->op, "identity.protect") == 0) {
		if (!g_tox || omaq_identity_protect(g_tox, op->passphrase) != 0) {
			emit_identity_error("forbidden", op->id);
			return 0;
		}
		emit_identity_action("protect", op->id, NULL, 1);
		return 0;
	}
	if (strcmp(op->op, "identity.unprotect") == 0) {
		if (!g_tox || omaq_identity_unprotect(g_tox, op->passphrase) != 0) {
			emit_identity_error("forbidden", op->id);
			return 0;
		}
		emit_identity_action("unprotect", op->id, NULL, 0);
		return 0;
	}
#endif
	if (strcmp(op->op, "invite.create") == 0) {
		if (strcmp(op->kind, "group") == 0) {
#ifdef HAVE_TOX
			if (g_tox) {
				omaq_invite inv;
				char url[OMAQ_URL_MAX];
				omaq_role self = ROLE_MEMBER;
				omaq_role granted = ROLE_MEMBER;
				uint32_t group_number;
				uint32_t invited_friend = UINT32_MAX;
				uint32_t existing_peer = UINT32_MAX;
				char current_friend_key[65];
				int ttl = op->has_ttl ? op->ttl_sec : 86400;
				if (op->id[0]) {
					if (!direct_id_ok(op->id)) {
						emit_error("unsupported");
						return 0;
					}
					invited_friend = direct_id_number(op->id);
					if (strlen(op->key) != 64 || !group_invite_request_ok(op->request) ||
					    omaq_tox_friend_pk_hex(g_tox, invited_friend,
								   current_friend_key) != 0 ||
					    strcmp(op->key, current_friend_key) != 0) {
						emit_group_invite_op_failure(op, "forbidden");
						return 0;
					}
				}
				if (!op->group[0] ||
				    omaq_group_id_parse(op->group, &group_number) != 0 ||
				    omaq_group_self_role(g_tox, op->group, &self) != 0) {
					emit_group_invite_op_failure(op, "forbidden");
					return 0;
				}
				if (op->id[0]) {
					const char *member_key = group_binding_member(op->group,
									      current_friend_key);
					if (member_key[0] &&
					    omaq_group_peer_for_key(group_number, member_key,
								    &existing_peer) == 0) {
						emit_group_invite_op_failure(op, "already_member");
						return 0;
					}
					if (group_binding_pending_friend(op->group,
								 current_friend_key)) {
						emit_group_invite_op_failure(op, "busy");
						return 0;
					}
				}
				if (op->role[0] && omaq_role_parse(op->role, &granted) != 0) {
					emit_group_invite_op_failure(op, "unsupported");
					return 0;
				}
				if (granted != ROLE_MEMBER) {
					emit_group_invite_op_failure(op, "unsupported");
					return 0;
				}
				memset(&inv, 0, sizeof(inv));
				omaq_tox_self_addr_hex(g_tox, inv.tox_addr);
				if (rand_id(inv.id, sizeof(inv.id)) != 0) {
					emit_group_invite_op_failure(op, "forbidden");
					return 0;
				}
				inv.expiry = (int64_t)time(NULL) + ttl;
				inv.kind = INVITE_GROUP;
				if (omaq_tox_group_chat_id_hex(g_tox, group_number, inv.group,
							    sizeof(inv.group)) != 0) {
					emit_group_invite_op_failure(op, "forbidden");
					return 0;
				}
				snprintf(inv.role, sizeof(inv.role), "%s", omaq_role_name(granted));
				if (omaq_invite_format(&inv, url, sizeof(url)) != 0) {
					emit_group_invite_op_failure(op, "unsupported");
					return 0;
				}
				if (!omaq_role_may(self, ACT_INVITE, granted)) {
					emit_group_invite_op_failure(op, "forbidden");
					return 0;
				}
				if (op->id[0]) {
#ifdef HAVE_SIGNAL
					int session_rc;
					if (g_group_invite_send_pending) {
						emit_group_invite_result(invited_friend, op->group, op->request,
								 "group.invite.failed", "busy");
						return 0;
					}
					session_rc = request_ratchet_session(invited_friend);
					if (session_rc < 0) {
						emit_group_invite_result(invited_friend, op->group, op->request,
								 "group.invite.failed", "forbidden");
						return 0;
					}
					g_group_invite_send_pending = 1;
					g_group_invite_send_friend = invited_friend;
					g_group_invite_send_deadline = (int64_t)time(NULL) + 30;
					memcpy(g_group_invite_send_group, op->group,
					       strlen(op->group) + 1);
					memcpy(g_group_invite_send_friend_key, op->key, 65);
					memcpy(g_group_invite_send_request, op->request,
					       strlen(op->request) + 1);
					snprintf(g_group_invite_send_id,
						 sizeof(g_group_invite_send_id), "%s", inv.id);
					snprintf(g_group_invite_send_url,
						 sizeof(g_group_invite_send_url), "%s", url);
					if (session_rc == 1)
						finish_pending_group_invite(invited_friend);
					return 0;
#else
					emit_group_invite_result(direct_id_number(op->id), op->group,
							 op->request, "group.invite.failed", "no_ratchet");
					return 0;
#endif
				}
				snprintf(g_issued_id, sizeof(g_issued_id), "%s", inv.id);
				snprintf(g_issued_url, sizeof(g_issued_url), "%s", url);
				g_issued_exp = inv.expiry;
				g_issued_is_group = 1;
				snprintf(g_issued_group, sizeof(g_issued_group), "%s", op->group);
				emit_invite_state(url, inv.expiry, "create", op->request);
				return 0;
			}
#endif
			if (op->id[0] && op->request[0]) {
#ifdef HAVE_SIGNAL
				emit_group_invite_op_failure(op, "unsupported");
#else
				emit_group_invite_terminal(op, "unsupported");
#endif
			} else {
				emit_error("unsupported");
			}
			return 0;
		}
		if (strcmp(op->kind, "direct") != 0) {
			emit_identity_error("unsupported", op->request);
			return 0;
		}
#ifdef HAVE_TOX
		if (g_tox) {
			omaq_invite inv;
			char url[OMAQ_URL_MAX];
			const int ttl = 86400;
			memset(&inv, 0, sizeof(inv));
			omaq_tox_self_addr_hex(g_tox, inv.tox_addr);
			if (rand_id(inv.id, sizeof(inv.id)) != 0) {
				emit_identity_error("forbidden", op->request);
				return 0;
			}
			inv.expiry = (int64_t)time(NULL) + ttl;
			inv.kind = INVITE_DIRECT;
#ifdef HAVE_SIGNAL
			if (!g_ratchet || omaq_ratchet_local_rk(g_ratchet, inv.rk) != 0) {
				emit_identity_error("no_ratchet", op->request);
				return 0;
			}
#else
			emit_identity_error("no_ratchet", op->request);
			return 0;
#endif
			if (omaq_invite_format(&inv, url, sizeof(url)) != 0) {
				emit_identity_error("unsupported", op->request);
				return 0;
			}
			snprintf(g_issued_id, sizeof(g_issued_id), "%s", inv.id);
			snprintf(g_issued_url, sizeof(g_issued_url), "%s", url);
			g_issued_exp = inv.expiry;
			emit_invite_state(url, inv.expiry, "create", op->request);
			return 0;
		}
#endif
		emit_identity_error("unsupported", op->request);
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
				uint32_t fn = UINT32_MAX;
				if (g_have_pending || g_have_gauth || g_have_gpending ||
				    g_group_bind_proof.used) {
					emit_error("busy");
					return 0;
				}
				if (strlen(inv.group) != 64 ||
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
				g_gauth_reservation_deadline = (int64_t)time(NULL) + 30;
				memcpy(g_gauth_group, inv.group, strlen(inv.group) + 1);
				snprintf(g_gauth_invite_id, sizeof(g_gauth_invite_id), "%s", inv.id);
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
		emit_invite_state("", 0, "revoke", op->request);
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
				char gid[OMAQ_GROUP_ID_MAX] = "", chat_id[65] = "";
				int binding_ready = 0, accepted = 0;

				memset(&g_group_bind_proof, 0, sizeof(g_group_bind_proof));
				g_group_bind_proof.used = 1;
				g_group_bind_proof.pending_accept = 1;
				g_group_bind_proof.friend = g_gauth_friend;
				if (!g_gauth_invite_id[0] ||
				    omaq_tox_friend_pk_hex(g_tox, g_gauth_friend,
							   g_group_bind_proof.friend_key) != 0 ||
				    snprintf(g_group_bind_proof.group,
					     sizeof(g_group_bind_proof.group), "g:%s",
					     g_gauth_group) >= (int)sizeof(g_group_bind_proof.group) ||
				    snprintf(g_group_bind_proof.invite_id,
					     sizeof(g_group_bind_proof.invite_id), "%s",
					     g_gauth_invite_id) >= (int)sizeof(g_group_bind_proof.invite_id) ||
				    group_bind_pending_save() != 0) {
					memset(&g_group_bind_proof, 0, sizeof(g_group_bind_proof));
					g_have_gpending = 0;
					g_gpending_announced = 0;
					clear_group_auth();
					emit_error("group_registry_failed");
					return 0;
				}
				accepted = omaq_tox_group_invite_accept(g_tox, g_gpending_friend,
									 g_gpending_data, g_gpending_len,
									 &gnum) == 0;
				int have_chat_id = accepted &&
					omaq_tox_group_chat_id_hex(g_tox, gnum, chat_id,
							       sizeof(chat_id)) == 0;
				int have_gid = have_chat_id &&
					omaq_group_set_chat_id(gnum, chat_id) == 0 &&
					omaq_group_id_format(gnum, gid, sizeof(gid)) == 0;
				if (!have_gid || !g_have_gauth ||
				    strcmp(chat_id, g_gauth_group) != 0) {
					if (accepted && have_gid) {
						if (omaq_group_leave(g_tox, gid) == 0)
							persist_forced_group_removal(gid);
						else
							schedule_group_cleanup(gnum, gid);
					} else if (accepted) {
						int leave_rc = omaq_tox_group_leave(g_tox, gnum);
						if (leave_rc < 0) {
							schedule_group_cleanup(gnum, NULL);
							emit_error("group_registry_failed");
						} else {
							omaq_group_mark_dissolved(gnum);
							if (leave_rc > 0)
								emit_error("group_registry_failed");
						}
					}
					(void)recover_pending_group_accept();
					g_have_gpending = 0;
					g_gpending_announced = 0;
					clear_group_auth();
					emit_error("forbidden");
					return 0;
				}
				if (!omaq_group_title(gnum)[0] &&
				    omaq_group_set_title(gnum, "Group") != 0) {
					if (omaq_group_leave(g_tox, gid) != 0) {
						schedule_group_cleanup(gnum, gid);
						emit_error_conv("forbidden", gid);
					}
					persist_forced_group_removal(gid);
					(void)recover_pending_group_accept();
					g_have_gpending = 0;
					g_gpending_announced = 0;
					clear_group_auth();
					emit_error("group_registry_failed");
					return 0;
				}
				if (omaq_group_limit(gnum) <= 0)
					omaq_group_set_limit(gnum, OMAQ_GROUP_PEERS);
				if (group_registry_save() < 0) {
					if (omaq_group_leave(g_tox, gid) != 0) {
						schedule_group_cleanup(gnum, gid);
						emit_error_conv("forbidden", gid);
					}
					persist_forced_group_removal(gid);
					(void)recover_pending_group_accept();
					g_have_gpending = 0;
					g_gpending_announced = 0;
					clear_group_auth();
					emit_error("group_registry_failed");
					return 0;
				}
				{
					int tox_save_rc = omaq_tox_save(g_tox);
					if (tox_save_rc < 0) {
						if (omaq_group_leave(g_tox, gid) != 0)
							schedule_group_cleanup(gnum, gid);
						persist_forced_group_removal(gid);
						(void)recover_pending_group_accept();
						g_have_gpending = 0;
						g_gpending_announced = 0;
						clear_group_auth();
						emit_error("group_registry_failed");
						return 0;
					}
					if (tox_save_rc > 0)
						emit_error("group_registry_sync_failed");
				}
				g_group_registry_pending = 1;
				memcpy(g_group_registry_group, gid,
				       sizeof(g_group_registry_group));
				if (g_gauth_invite_id[0]) {
					uint32_t self_peer = UINT32_MAX;
					if (omaq_tox_group_self_peer(g_tox, gnum, &self_peer) == 0 &&
					    omaq_group_refresh_member(g_tox, gnum, self_peer) == 0 &&
					    group_self_member_key(gnum, g_group_bind_proof.member_key,
							  sizeof(g_group_bind_proof.member_key)) == 0) {
						g_group_bind_proof.pending_accept = 0;
						g_group_bind_proof.direct_confirmed = 0;
						g_group_bind_proof.expires = 0;
						g_group_bind_proof.retry_after = 0;
						binding_ready = group_bind_pending_save() == 0;
					}
				}
				if (!binding_ready) {
					g_group_bind_proof.pending_accept = 1;
					g_group_bind_proof.member_key[0] = '\0';
					g_group_bind_proof.direct_confirmed = 0;
					g_group_bind_proof.retry_after = 0;
					g_group_registry_pending = 0;
					g_group_registry_group[0] = '\0';
					if (omaq_group_leave(g_tox, gid) != 0)
						schedule_group_cleanup(gnum, gid);
					persist_forced_group_removal(gid);
					(void)recover_pending_group_accept();
					g_have_gpending = 0;
					g_gpending_announced = 0;
					clear_group_auth();
					emit_error("group_registry_failed");
					return 0;
				}
#ifdef HAVE_SIGNAL
				(void)send_group_binding_confirmation(g_gauth_friend, gid,
					g_group_bind_proof.invite_id,
					g_group_bind_proof.member_key);
#endif
				g_group_bind_proof.retry_after = (int64_t)time(NULL) + 2;
				g_have_gpending = 0;
				g_gpending_announced = 0;
				clear_group_auth();
				return 0;
			}
			g_have_gpending = 0;
			g_gpending_announced = 0;
			clear_group_auth();
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
						clear_invite_and_emit();
						emit_error("forbidden");
						return 0;
					}
				}
#endif
				if (g_issued_is_group && g_issued_group[0] && fn != UINT32_MAX) {
					omaq_role self_role;
					if (omaq_group_self_role(g_tox, g_issued_group, &self_role) != 0) {
						clear_invite_and_emit();
						emit_error("forbidden");
						return 0;
					}
					if (omaq_group_invite_friend(g_tox, g_issued_group, fn,
								     self_role, ROLE_MEMBER) != 0) {
						clear_invite_and_emit();
						emit_error("forbidden");
						return 0;
					}
				}
				clear_invite_and_emit();
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
			g_pending_announced = 0;
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
			char current_key[65];
			uint32_t fn;
			if (!direct_id_ok(cid)) {
				emit_error("unsupported");
				return 0;
			}
			fn = direct_id_number(cid);
			if (strlen(op->key) != 64 ||
			    omaq_tox_friend_pk_hex(g_tox, fn, current_key) != 0 ||
			    strcmp(op->key, current_key) != 0) {
				emit_error("forbidden");
				return 0;
			}
			if (recover_receipt_transaction() != 0) {
				emit_error_conv("receipt_state_failed", cid);
				return 0;
			}
			if (omaq_tox_friend_delete(g_tox, fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
			if (group_binding_forget_friend(current_key) != 0)
				emit_error("group_registry_failed");
			if (clear_unread(cid) != 0)
				emit_unread_failed(cid, "unread_persist_failed");
			if (receipt_outbox_drop_conversation(cid) != 0)
				emit_error_conv("receipt_state_failed", cid);
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
		char escaped[260], escaped_request[160], request_field[192] = "";
		char ev[560];

		if (!g_tox || omaq_tox_set_name(g_tox, op->nickname) != 0) {
			emit_identity_error("nickname_invalid", op->id);
			return 0;
		}
		if (omaq_json_escape(op->nickname, escaped, sizeof(escaped)) != 0) {
			emit_identity_error("nickname_invalid", op->id);
			return 0;
		}
		if (op->id[0] &&
		    omaq_json_escape(op->id, escaped_request, sizeof(escaped_request)) == 0)
			snprintf(request_field, sizeof(request_field),
				 ",\"request\":\"%s\"", escaped_request);
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"nickname\",\"value\":\"%s\"%s}",
			 escaped, request_field);
		emit(ev);
		return 0;
#endif
		emit_identity_error("unsupported", op->id);
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
			clear_invite_and_emit();
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
			char gid[OMAQ_GROUP_ID_MAX] = "";
			uint32_t cleanup_group = UINT32_MAX;
			const char *title = op->title[0] ? op->title : (op->text[0] ? op->text : "group");
			int create_rc = omaq_group_create(g_tox, title, gid, sizeof(gid),
							 &cleanup_group);
			if (create_rc != 0) {
				if (create_rc == -2 && gid[0]) {
					schedule_group_cleanup(cleanup_group, gid);
					persist_forced_group_removal(gid);
				} else if (create_rc == -3) {
					schedule_group_cleanup(cleanup_group, NULL);
				}
				emit_error("forbidden");
				return 0;
			}
			if (group_registry_save() < 0) {
				if (omaq_group_leave(g_tox, gid) != 0) {
					if (omaq_group_id_parse(gid, &cleanup_group) == 0)
						schedule_group_cleanup(cleanup_group, gid);
					emit_error_conv("forbidden", gid);
				}
				persist_forced_group_removal(gid);
				emit_error("group_registry_failed");
				return 0;
			}
			{
				int tox_save_rc = omaq_tox_save(g_tox);
				if (tox_save_rc < 0) {
					if (omaq_group_leave(g_tox, gid) != 0)
						schedule_group_cleanup(cleanup_group, gid);
					persist_forced_group_removal(gid);
					emit_error("group_registry_failed");
					return 0;
				}
				if (tox_save_rc > 0)
					emit_error("group_registry_sync_failed");
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
			if (recover_receipt_transaction() != 0) {
				emit_error_conv("receipt_state_failed", gid);
				return 0;
			}
			if (group_registry_save_except(gid) < 0) {
				emit_error_conv("group_registry_failed", gid);
				return 0;
			}
			if (omaq_group_dissolve(g_tox, gid, self) != 0) {
				persist_forced_group_removal(gid);
				emit_error("forbidden");
				return 0;
			}
			group_binding_drop(gid, NULL);
			if (group_binding_forget_group(gid) != 0)
				emit_error_conv("group_registry_failed", gid);
			if (clear_unread(gid) != 0)
				emit_unread_failed(gid, "unread_persist_failed");
			if (receipt_outbox_drop_conversation(gid) != 0)
				emit_error_conv("receipt_state_failed", gid);
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
			omaq_role current = ROLE_MEMBER;
			const char *gid = op->group[0] ? op->group : op->conversation;
			const char *member_key = op->member[0] ? op->member : op->id;
			uint32_t peer;
			if (op->role[0] && omaq_role_parse(op->role, &next) != 0) {
				emit_error("unsupported");
				return 0;
			}
			if (omaq_group_self_role(g_tox, gid, &self) != 0 ||
			    omaq_group_resolve_member(g_tox, gid, member_key, &peer,
						      &current) != 0 ||
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
			const char *member_key = op->member[0] ? op->member : op->id;
			unsigned char expected_snapshot[sizeof(g_group_bind_expected)];
			char member_name[OMAQ_GROUP_MEMBER_NAME_MAX + 1] = "";
			uint32_t peer, group_number;
			int notice_slot = -1;
			if (strlen(gid) >= sizeof(g_group_leave_notice_suppress[0].group) ||
			    omaq_group_self_role(g_tox, gid, &self) != 0 ||
			    omaq_group_resolve_member(g_tox, gid, member_key, &peer,
						      &victim) != 0) {
				emit_error("forbidden");
				return 0;
			}
			if (omaq_group_id_parse(gid, &group_number) == 0)
				for (int member = 0; member < omaq_group_peer_count(group_number); member++)
					if (omaq_group_peer_at(group_number, member) == peer) {
						snprintf(member_name, sizeof(member_name), "%s",
							 omaq_group_peer_name(group_number, member));
						break;
					}
			for (int notice = 0; notice < GROUP_FRIEND_BINDING_MAX; notice++) {
				if (g_group_leave_notice_suppress[notice].used &&
				    strcmp(g_group_leave_notice_suppress[notice].group, gid) == 0 &&
				    g_group_leave_notice_suppress[notice].peer == peer) {
					notice_slot = notice;
					break;
				}
				if (notice_slot < 0 &&
				    (!g_group_leave_notice_suppress[notice].used ||
				     g_group_leave_notice_suppress[notice].expires < (int64_t)time(NULL)))
					notice_slot = notice;
			}
			if (notice_slot < 0) {
				emit_error("busy");
				return 0;
			}
			memset(&g_group_leave_notice_suppress[notice_slot], 0,
			       sizeof(g_group_leave_notice_suppress[notice_slot]));
			g_group_leave_notice_suppress[notice_slot].used = 1;
			g_group_leave_notice_suppress[notice_slot].peer = peer;
			g_group_leave_notice_suppress[notice_slot].expires = (int64_t)time(NULL) + 10;
			memcpy(g_group_leave_notice_suppress[notice_slot].group, gid,
			       strlen(gid) + 1u);
			memcpy(expected_snapshot, g_group_bind_expected,
			       sizeof(expected_snapshot));
			if (group_binding_forget_member(gid, member_key) != 0) {
				memset(&g_group_leave_notice_suppress[notice_slot], 0,
				       sizeof(g_group_leave_notice_suppress[notice_slot]));
				emit_error_conv("group_registry_failed", gid);
				return 0;
			}
			if (omaq_group_kick(g_tox, gid, peer, self, victim) != 0) {
				memset(&g_group_leave_notice_suppress[notice_slot], 0,
				       sizeof(g_group_leave_notice_suppress[notice_slot]));
				memcpy(g_group_bind_expected, expected_snapshot,
				       sizeof(expected_snapshot));
				g_group_binding_restore_pending =
					group_bind_pending_save() != 0;
				emit_error("forbidden");
				return 0;
			}
			group_binding_drop(gid, member_key);
			persist_forced_group_removal(gid);
			emit_group_membership_message(gid, member_name, 0);
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
			if (recover_receipt_transaction() != 0) {
				emit_error_conv("receipt_state_failed", gid);
				return 0;
			}
			if (group_registry_save_except(gid) < 0) {
				emit_error_conv("group_registry_failed", gid);
				return 0;
			}
			if (omaq_group_leave(g_tox, gid) != 0) {
				persist_forced_group_removal(gid);
				emit_error("forbidden");
				return 0;
			}
			group_binding_drop(gid, NULL);
			if (group_binding_forget_group(gid) != 0)
				emit_error_conv("group_registry_failed", gid);
			if (clear_unread(gid) != 0)
				emit_unread_failed(gid, "unread_persist_failed");
			if (receipt_outbox_drop_conversation(gid) != 0)
				emit_error_conv("receipt_state_failed", gid);
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
			char current_key[65];
			uint32_t friend;
			if (!direct_id_ok(cid) || strlen(op->key) != 64) {
				emit_identity_error("forbidden", op->id);
				return 0;
			}
			friend = direct_id_number(cid);
			if (omaq_tox_friend_pk_hex(g_tox, friend, current_key) != 0 ||
			    strcmp(current_key, op->key) != 0) {
				emit_identity_error("forbidden", op->id);
				return 0;
			}
			emit_safety(friend, op->id);
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
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (cid[0] == 'g') {
			char wire[256], reaction_rate_key[48];
			uint32_t group_number;
			int reaction_rc;

			if (!g_tox || omaq_group_id_parse(cid, &group_number) != 0 ||
			    !op->id[0] || !op->has_text ||
			    !omaq_message_reaction_ok(op->text) ||
			    omaq_store_message_exists(home_dir(), cid, op->id) != 1) {
				emit_message_reaction_failed(cid, op->id, "invalid");
				return 0;
			}
			snprintf(reaction_rate_key, sizeof(reaction_rate_key),
				 "group-reaction:%u", group_number);
			if (omaq_rate_allow_key_only(&g_reaction_out_rate, reaction_rate_key,
					     (int64_t)time(NULL)) != 0) {
				emit_message_reaction_failed(cid, op->id, "rate_limited");
				return 0;
			}
			if (omaq_message_reaction_wire_pack(wire, sizeof(wire), op->id,
						    op->text) != 0) {
				emit_message_reaction_failed(cid, op->id, "invalid");
				return 0;
			}
			reaction_rc = omaq_group_send(g_tox, cid, wire);
			if (reaction_rc != 0) {
				emit_message_reaction_failed(cid, op->id,
					reaction_rc == -2 ? "offline" : "forbidden");
				return 0;
			}
			if (omaq_store_update_reaction(home_dir(), cid, op->id,
						       op->text, "me") != 0) {
				emit_message_reaction(cid, op->id, op->text, "me");
				emit_message_reaction_failed(cid, op->id, "history_failed");
				return 0;
			}
			emit_message_reaction(cid, op->id, op->text, "me");
			return 0;
		}
#ifdef HAVE_SIGNAL
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
	if (strcmp(op->op, "conversation.read") == 0 ||
	    strcmp(op->op, "unread.clear") == 0) {
#ifdef HAVE_TOX
		const char *cid = op->conversation[0] ? op->conversation : "0";
		omaq_store_message_id *ids = NULL;
		size_t id_count = 0;
		unsigned unread;

		if (!g_tox || !conversation_id_ok(cid) || g_receipt_outbox_invalid) {
			emit_conversation_read("conversation.read.failed", cid,
					       g_receipt_outbox_invalid ? "receipt_state_invalid" : "forbidden");
			return 0;
		}
		if (recover_receipt_transaction() != 0) {
			emit_conversation_read("conversation.read.failed", cid,
					       "receipt_state_failed");
			return 0;
		}
		unread = omaq_unread_count(&g_unread, cid);
		if (omaq_store_unread_receipt_ids(home_dir(), cid, unread, &ids,
						  &id_count) != 0 ||
		    (id_count > 0 && (!receipt_outbox_has_capacity(cid, ids, id_count) ||
				      receipt_transaction_begin(cid, ids, id_count) != 0))) {
			free(ids);
			emit_conversation_read("conversation.read.failed", cid,
					       "receipt_state_failed");
			return 0;
		}
		if (clear_unread(cid) != 0) {
			free(ids);
			if (id_count > 0 && recover_receipt_transaction() == 0 &&
			    g_receipt_recovery_committed) {
				emit_conversation_read("conversation.read", cid, NULL);
				retry_receipt_outbox();
				return 0;
			}
			emit_conversation_read("conversation.read.failed", cid,
					       "unread_persist_failed");
			return 0;
		}
		if (id_count > 0 &&
		    (omaq_receipt_transaction_mark_committed(state_dir()) != 0 ||
		     receipt_outbox_commit_add(cid, ids, id_count) != 0 ||
		     omaq_receipt_transaction_clear(state_dir()) != 0)) {
			free(ids);
			emit_conversation_read("conversation.read.failed", cid,
					       "receipt_state_failed");
			return 0;
		}
		if (id_count > 0) {
			g_receipt_transaction_pending = 0;
			g_receipt_transaction_retry_after = 0;
		}
		free(ids);
		emit_conversation_read("conversation.read", cid, NULL);
		retry_receipt_outbox();
		return 0;
#else
		emit_error("unsupported");
		return 0;
#endif
	}
	if (strcmp(op->op, "receipt.send") == 0) {
#ifdef HAVE_TOX
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (strcmp(op->state, "read") == 0) {
			omaq_store_message_id receipt_id;
			if (!g_tox || !conversation_id_ok(cid) || !op->id[0] ||
			    snprintf(receipt_id.id, sizeof(receipt_id.id), "%s", op->id) >=
				(int)sizeof(receipt_id.id) ||
			    receipt_outbox_commit_add(cid, &receipt_id, 1) != 0) {
				emit_receipt_failed(cid, op->id, op->state,
						    g_receipt_outbox_invalid ? "receipt_state_invalid" : "forbidden");
				return 0;
			}
			retry_receipt_outbox();
			return 0;
		}
		if (cid[0] == 'g') {
			emit_receipt_failed(cid, op->id, op->state, "forbidden");
			return 0;
		}
#ifdef HAVE_SIGNAL
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
	if (strcmp(op->op, "history.clear") == 0) {
		const char *cid = op->conversation[0] ? op->conversation : "0";
		char esc_cid[128], ev[192];
		if (!conversation_id_ok(cid)) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
#ifdef HAVE_TOX
		if (recover_receipt_transaction() != 0) {
			emit_error_conv("receipt_state_failed", cid);
			return 0;
		}
#endif
		if (omaq_store_clear(home_dir(), cid) != 0) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
		if (clear_unread(cid) != 0)
			emit_unread_failed(cid, "unread_persist_failed");
#ifdef HAVE_TOX
		if (receipt_outbox_drop_conversation(cid) != 0)
			emit_error_conv("receipt_state_failed", cid);
#endif
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
			emit_json_items("history", cid, out, n, op->id, 1);
			free(out);
			return 0;
		}
		emit_history_failed(cid, op->id);
		return 0;
	}
	if (strcmp(op->op, "search") == 0) {
		char *out = NULL;
		size_t n = 0;
		int lim = op->has_limit ? op->limit : 20;
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (!op->text[0]) {
			emit_json_items("search", cid, "", 0, op->id, 0);
			return 0;
		}
		if (omaq_message_search(home_dir(), cid, op->text, lim, &out, &n) == 0 && out) {
			emit_json_items("search", cid, out, n, op->id, 0);
			free(out);
			return 0;
		}
		emit_json_items("search", cid, "", 0, op->id, 0);
		return 0;
	}
	if (strcmp(op->op, "identity.export") == 0) {
		char dest[512];
		const char *path = op->path[0] ? op->path : dest;
		if (!op->path[0]) {
			if (snprintf(dest, sizeof(dest), "%s/omaq-identity.save", state_dir()) >= (int)sizeof(dest)) {
				emit_identity_error("unsupported", op->id);
				return 0;
			}
		}
#ifdef HAVE_TOX
		if (group_binding_debt_pending() ||
		    group_registry_transaction_present()) {
			emit_identity_error("busy", op->id);
			return 0;
		}
		if (group_registry_save() < 0) {
			emit_identity_error("group_registry_failed", op->id);
			return 0;
		}
#endif
		if (omaq_identity_bundle_export(home_dir(), path) != 0) {
			emit_identity_error("forbidden", op->id);
			return 0;
		}
		emit_identity_action("export", op->id, path, -1);
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
			{
				uint32_t active_friend = UINT32_MAX;
				const char *active_state = NULL;
				int has_active = omaq_av_status(&active_friend, &active_state);
				int stopped;

				(void)active_state;
				if (has_active == 1 && active_friend != fn) {
					emit_error_conv("forbidden", cid);
					return 0;
				}
				if (has_active == 0) {
					snprintf(ev, sizeof(ev),
						 "{\"event\":\"call.state\",\"conversation\":\"%s\",\"state\":\"ended\"}",
						 cid);
					emit(ev);
					return 0;
				}
				stopped = omaq_av_stop(g_tox, fn);
				if (stopped < 0) {
					emit_error_conv("forbidden", cid);
					return 0;
				}
				g_av_reset_requested = 1;
			}
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"call.state\",\"conversation\":\"%u\",\"state\":\"ended\"}",
				 fn);
			emit(ev);
			return 0;
		}
#endif
		emit_error("unsupported");
		return 0;
	}
	if (strcmp(op->op, "identity.inspect") == 0) {
#ifndef HAVE_TOX
		emit_identity_error("unsupported", op->id);
		return 0;
#else
		char stage[640], stage_save[700], stage_groups[700], stage_bindings[700];
		char stage_bundle[700];
		struct omaq_tox *candidate = NULL;
		int candidate_error = 0, stage_created = 0, valid = 1;

		stage[0] = '\0';
		stage_save[0] = '\0';
		stage_groups[0] = '\0';
		stage_bindings[0] = '\0';
		stage_bundle[0] = '\0';
		if (!op->path[0] || create_identity_stage(stage, sizeof(stage)) != 0 ||
		    ((stage_created = 1), 0) ||
		    snprintf(stage_save, sizeof(stage_save), "%s/tox.save", stage) >=
			     (int)sizeof(stage_save) ||
		    snprintf(stage_groups, sizeof(stage_groups), "%s/groups.tsv", stage) >=
			     (int)sizeof(stage_groups) ||
		    snprintf(stage_bindings, sizeof(stage_bindings), "%s/group-friends.tsv", stage) >=
			     (int)sizeof(stage_bindings) ||
		    snprintf(stage_bundle, sizeof(stage_bundle), "%s/identity.bundle", stage) >=
			     (int)sizeof(stage_bundle) ||
		    omaq_identity_bundle_snapshot(op->path, stage_bundle) != 0 ||
		    omaq_identity_bundle_import(stage, stage_bundle, 1) != 0) {
			valid = 0;
		} else {
			candidate = omaq_identity_load(stage, op->passphrase, &candidate_error);
			if (!candidate) {
				valid = 0;
			} else {
				if (identity_group_files_validate(candidate, stage) != 0)
					valid = 0;
				omaq_tox_discard(candidate);
			}
		}
		if (stage_created && cleanup_identity_stage(stage) != 0)
			valid = 0;
		if (!valid) {
			emit_identity_error(
				candidate_error == OMAQ_TOX_LOCKED && !op->passphrase[0]
					? "identity_passphrase_required" : "identity_import_failed",
				op->id);
			return 0;
		}
		emit_identity_action("inspect", op->id, op->path, -1);
		return 0;
#endif
	}
	if (strcmp(op->op, "identity.import") == 0) {
		int replacing = op->has_replace && op->replace;
		if (!op->path[0]) {
			emit_identity_error("unsupported", op->id);
			return 0;
		}
#ifdef HAVE_TOX
		if (replacing && (recover_receipt_transaction() != 0 ||
				  group_registry_transaction_present() ||
				  group_binding_debt_pending() ||
				  g_group_invite_send_pending ||
				  g_receipt_outbox.length > 0 || g_receipt_outbox_invalid)) {
			emit_identity_error("busy", op->id);
			return 0;
		}
#endif
#ifndef HAVE_TOX
		{
			int import_rc = omaq_identity_bundle_import(home_dir(), op->path, replacing);
			if (import_rc == 1)
				emit_identity_error("identity_exists", op->id);
			else if (import_rc != 0)
				emit_identity_error("identity_import_failed", op->id);
			else
				emit_identity_action("import", op->id, NULL, -1);
			return 0;
		}
#else
		{
			char stage[640], stage_save[700], stage_groups[700], stage_bindings[700];
			char stage_bundle[700], token[96], backup[700];
			char old_address[77], old_fingerprint[65];
			struct omaq_tox *candidate;
			identity_state_archive identity_archive;
			const char *failure_code = "identity_import_failed";
			int candidate_error = 0, swapped = 0, archived = 0, marker = 0;
			int unread_reset = 0, stage_created = 0;
			int archive_rc, marker_rc, rollback_load = 0, rollback_ok = 1;

			stage[0] = '\0';
			stage_save[0] = '\0';
			stage_groups[0] = '\0';
			stage_bindings[0] = '\0';
			stage_bundle[0] = '\0';
			backup[0] = '\0';
			if (!replacing) {
				emit_identity_error("identity_exists", op->id);
				return 0;
			}
			if (omaq_av_busy() || omaq_file_busy()) {
				emit_identity_error("busy", op->id);
				return 0;
			}
			if (create_identity_stage(stage, sizeof(stage)) != 0 ||
			    ((stage_created = 1), 0) ||
			    snprintf(stage_save, sizeof(stage_save), "%s/tox.save", stage) >=
				     (int)sizeof(stage_save) ||
			    snprintf(stage_groups, sizeof(stage_groups), "%s/groups.tsv", stage) >=
				     (int)sizeof(stage_groups) ||
			    snprintf(stage_bindings, sizeof(stage_bindings), "%s/group-friends.tsv", stage) >=
				     (int)sizeof(stage_bindings) ||
			    snprintf(stage_bundle, sizeof(stage_bundle), "%s/identity.bundle", stage) >=
				     (int)sizeof(stage_bundle) ||
			    omaq_identity_bundle_snapshot(op->path, stage_bundle) != 0 ||
			    omaq_identity_bundle_import(stage, stage_bundle, 1) != 0) {
				if (stage_created)
					(void)cleanup_identity_stage(stage);
				emit_identity_error("identity_import_failed", op->id);
				return 0;
			}
			candidate = omaq_identity_load(stage, op->passphrase, &candidate_error);
			if (!candidate || identity_group_files_validate(candidate, stage) != 0) {
				if (candidate)
					omaq_tox_discard(candidate);
				(void)cleanup_identity_stage(stage);
				emit_identity_error(
					candidate_error == OMAQ_TOX_LOCKED && !op->passphrase[0]
						? "identity_passphrase_required" : "identity_import_failed",
					op->id);
				return 0;
			}
			omaq_tox_discard(candidate);
			if (!g_tox || omaq_tox_self_addr_hex(g_tox, old_address) != 0) {
				(void)cleanup_identity_stage(stage);
				emit_identity_error("identity_backup_failed", op->id);
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
				(void)cleanup_identity_stage(stage);
				emit_identity_error("identity_backup_failed", op->id);
				return 0;
			}
			marker_rc = write_identity_marker(token, old_fingerprint);
			if (marker_rc != 0) {
				if (marker_rc == -1) {
					unlink(backup);
					(void)fsync_directory(home_dir());
					emit_identity_error("identity_backup_failed", op->id);
				} else {
					g_identity_recovery_required = 1;
					g_shutdown_after_drain = 1;
				}
				(void)cleanup_identity_stage(stage);
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
			if (omaq_identity_bundle_import(home_dir(), stage_bundle, 1) != 0) {
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
			reset_identity_runtime_state();
			if (load_tox(op->passphrase) != 0) {
				goto identity_import_rollback;
			}
#ifdef HAVE_SIGNAL
			g_ratchet = omaq_ratchet_open(home_dir());
			if (!g_ratchet) {
				goto identity_import_rollback;
			}
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
#ifdef HAVE_SIGNAL
			clear_group_invite_results();
#endif
			init_instance_id();
			g_identity_requires_ready = 1;
			g_stdin_identity_ready = 0;
			for (size_t client_index = 0; client_index < g_ncli; client_index++)
				g_client_identity_ready[client_index] = 0;
			if (unlink(backup) != 0 || fsync_directory(home_dir()) != 0) {
				g_identity_backup_cleanup_failed = 1;
				emit_error("identity_backup_cleanup_failed");
			}
			(void)cleanup_identity_stage(stage);
			emit_identity_action("import", op->id, NULL, -1);
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
				reset_identity_runtime_state();
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
			(void)cleanup_identity_stage(stage);
			if (rollback_ok) {
				emit_identity_error(failure_code, op->id);
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
	if (strncmp(op->op, "identity.", 9) == 0)
		emit_identity_error("unsupported", op->id);
	else
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
	if (g_tox && prune_unavailable_unread() < 0)
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
	if (g_tox && prune_unavailable_receipts() < 0)
		g_receipt_outbox_invalid = 1;
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
#ifdef HAVE_TOX
	omaq_receipt_outbox_init(&g_receipt_outbox);
	if (!g_identity_recovery_required &&
	    omaq_receipt_outbox_load(&g_receipt_outbox, state_dir()) != 0) {
		fprintf(stderr, "omaq: read receipt state is invalid; preserving it\n");
		g_receipt_outbox_invalid = 1;
	}
#endif
	if (!g_identity_recovery_required && omaq_store_unread_load(&g_unread, state_dir()) != 0) {
		fprintf(stderr, "omaq: unread state is invalid; starting empty\n");
		omaq_unread_init(&g_unread);
		g_unread_load_failed = 1;
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_state_invalid");
	}
#ifdef HAVE_TOX
	if (!g_identity_recovery_required && !g_receipt_outbox_invalid)
		(void)recover_receipt_transaction();
#endif
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
#ifdef HAVE_TOX
	if (g_receipt_outbox_invalid)
		emit_error("receipt_state_invalid");
#endif
	if (g_identity_backup_cleanup_failed)
		emit_error("identity_backup_cleanup_failed");
	if (!g_replay_mode && !g_shutdown_after_drain)
		start_backend();
	if (hold && !g_shutdown_after_drain) {
		for (;;) {
#ifdef HAVE_TOX
			if (g_tox) {
				omaq_tox_iterate(g_tox);
				flush_receipt_acknowledgements();
				expire_group_auth_reservation();
#ifdef HAVE_SIGNAL
				expire_pending_group_invite();
#endif
				if (omaq_group_take_save_error())
					emit_error("group_registry_failed");
				retry_group_binding_proof();
				retry_group_binding_cleanup();
				retry_group_registry();
				retry_group_cleanup();
				retry_receipt_transaction();
				retry_receipt_outbox();
				pump_call_audio();
				reset_call_transport();
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
			flush_receipt_acknowledgements();
			expire_group_auth_reservation();
#ifdef HAVE_SIGNAL
			expire_pending_group_invite();
#endif
			if (omaq_group_take_save_error())
				emit_error("group_registry_failed");
			retry_group_binding_proof();
			retry_group_binding_cleanup();
			retry_group_registry();
			retry_group_cleanup();
			retry_receipt_transaction();
			retry_receipt_outbox();
			pump_call_audio();
			reset_call_transport();
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
	omaq_av_reset();
	if (g_tox) {
		if (g_identity_recovery_required)
			omaq_tox_discard(g_tox);
		else
			omaq_tox_close(g_tox);
	}
	omaq_receipt_outbox_destroy(&g_receipt_outbox);
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
