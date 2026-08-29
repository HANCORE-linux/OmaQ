#define _DEFAULT_SOURCE

#include "av.h"
#include "auto_open.h"
#include "avatar.h"
#include "file.h"
#include "group.h"
#include "group_file.h"
#include "group_invite.h"
#include "conversation.h"
#include "direct_state.h"
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
#include "sound.h"
#include "store.h"
#include "stdout_spool.h"
#include "state_archive.h"
#include "surface.h"

#include "identity.h"
#include "identity_guard.h"
#ifdef HAVE_TOX
#include "tox_adapt.h"
#include <openssl/evp.h>
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
#ifndef OMAQ_PROTOCOL_VERSION
#define OMAQ_PROTOCOL_VERSION 14
#endif
#ifdef OMAQ_IPC_TEST
#define OMAQ_IPC_TEST_EVENT_SIZE 65500u
#endif

static void emit(const char *value);
static void emit_error(const char *code);
static void emit_identity_primary_state(const char *request);
#ifdef HAVE_TOX
static void group_file_reset(void);
static void group_file_pump(void);
static void group_file_peer_removed(uint32_t group_number, uint32_t peer, int self);
static void hook_group_file_packet(void *ud, uint32_t group_number,
				   uint32_t peer, const uint8_t *data,
				   size_t length, int private_packet);
#endif

#define ATTACHMENT_STAGE_OWNER_MAX 32
static struct {
	int used;
	int owner_fd;
	char request[80];
	char path[640];
} g_attachment_stage_owners[ATTACHMENT_STAGE_OWNER_MAX];

#define SOUND_RESULT_CACHE_MAX 16
static struct {
	int used;
	char request[80];
	char operation[16];
	char selected[OMAQ_SOUND_ID_HEX + 1];
	char error[32];
} g_sound_results[SOUND_RESULT_CACHE_MAX];
static size_t g_sound_result_next;
static size_t g_sound_result_count;
static int g_replaying_sound_results;

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
	int pending_attachment;
	int managed_attachment;
	char kind[8];
	char path[512];
} g_file_requests[FILE_REQUEST_CACHE];
static uint64_t g_file_request_sequence;
#define GROUP_FILE_OUT_MAX 2
#define GROUP_FILE_IN_MAX 8
#define GROUP_FILE_RECIPIENT_MAX (OMAQ_GROUP_PEERS - 1)
typedef struct {
	int used;
	int accepted;
	int done;
	int done_sent;
	int canceled;
	int failed;
	int delivery_unknown;
	uint32_t peer;
	uint64_t offset;
	int64_t last_progress;
	char key[65];
} group_file_recipient;
typedef struct {
	int used;
	int fd;
	uint32_t group_number;
	uint8_t id[OMAQ_GROUP_FILE_ID_BYTES];
	uint8_t hash[32];
	uint64_t size;
	int64_t accept_until;
	int64_t idle_deadline;
	int started;
	int stored;
	unsigned int recipient_cursor;
	int pending_attachment;
	int managed_attachment;
	char group[OMAQ_GROUP_ID_MAX];
	char event_id[3 + OMAQ_GROUP_FILE_ID_HEX + 1];
	char request[80];
	char path[512];
	char name[OMAQ_FILE_NAME_MAX + 1];
	char kind[6];
	group_file_recipient recipients[GROUP_FILE_RECIPIENT_MAX];
} group_file_outgoing;
typedef struct {
	int used;
	int accepted;
	int completed;
	int fd;
	uint32_t group_number;
	uint32_t sender_peer;
	uint8_t id[OMAQ_GROUP_FILE_ID_BYTES];
	uint8_t hash[32];
	uint64_t size;
	uint64_t got;
	int64_t idle_deadline;
	int64_t ack_after;
	char group[OMAQ_GROUP_ID_MAX];
	char event_id[3 + OMAQ_GROUP_FILE_ID_HEX + 1];
	char sender_key[65];
	char sender_name[OMAQ_GROUP_MEMBER_NAME_MAX + 1];
	char name[OMAQ_FILE_NAME_MAX + 1];
	char kind[6];
	char path[512];
} group_file_incoming;
static group_file_outgoing g_group_file_out[GROUP_FILE_OUT_MAX];
static group_file_incoming g_group_file_in[GROUP_FILE_IN_MAX];
static uint64_t g_identity_backup_sequence;
static uint64_t g_friend_generation;
static int g_identity_guard_state = OMAQ_IDENTITY_GUARD_FRESH;
static int g_identity_guard_error;
static int g_identity_guard_replacement_load;
static int g_guarded_restore_loading;
static int g_identity_primary_uncertain;
static int g_resolving_primary_uncertainty;
static int g_identity_recovery_degraded_state = -1;
static int g_direct_state_migration_failed;
static int g_direct_state_reinvite_required;
static int g_avatar_temps_cleaned;
#ifdef HAVE_SIGNAL
static struct omaq_ratchet *g_ratchet;
static struct {
	int used;
	char peer[OMAQ_RATCHET_PEER_MAX];
	int64_t retry_after;
} g_ratchet_recovery[OMAQ_DIRECT_STATE_FRIEND_MAX];
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
static int g_group_invite_native_pending;
static int g_group_invite_cleanup_pending;
static char g_group_invite_cleanup_code[32];
static unsigned g_group_invite_native_attempts;
static int64_t g_group_invite_native_retry_ms;
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
static int group_was_pruned(const char *group);
static int group_binding_forget_friend(const char *friend_key);
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
static omaq_control_rate g_group_typing_rate;
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
static int g_input_owner_fd = -1;
static char g_stdout_buf[CLIENT_OUT_MAX];
static size_t g_stdout_len;
static size_t g_stdout_off;
static omaq_stdout_spool *g_stdout_spool;
static int g_stdout_closed;
static omaq_line_reader g_stdin_reader;
static int g_stdin_closed;
static int g_fatal_io;
static volatile sig_atomic_t g_shutdown_after_drain;
static volatile sig_atomic_t g_shutdown_signal;
static int g_shutdown_ack_fd = -1;
static int g_shutdown_ack_delivered;
static int g_shutdown_ack_failed;
static int64_t g_shutdown_ack_deadline_ms;
static int g_identity_recovered;
static int g_unread_load_failed;
static int g_identity_backup_cleanup_failed;
static char g_unread_error_code[32];
static int g_identity_recovery_required;
static int g_replay_mode;
static int g_backend_started;
static char g_instance_id[33];
#ifdef OMAQ_IPC_TEST
static int g_test_native_group_count;
static int g_test_group_state_uncertain;
#endif

static void drop_client(size_t i);

static void request_shutdown_signal(int signal_number)
{
	(void)signal_number;
	g_shutdown_signal = 1;
	g_shutdown_after_drain = 1;
}

static int shutdown_requested(void)
{
	return g_shutdown_after_drain || g_shutdown_signal;
}

static int64_t monotonic_millis(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return -1;
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int shutdown_ack_status(void)
{
	int64_t now;

	if (g_shutdown_ack_fd < 0 || g_shutdown_ack_delivered)
		return 1;
	if (g_shutdown_ack_failed)
		return -1;
	now = monotonic_millis();
	if (now < 0 || now >= g_shutdown_ack_deadline_ms)
		return -1;
	for (size_t i = 0; i < g_ncli; i++)
		if (g_clients[i] == g_shutdown_ack_fd) {
			if (g_olen[i] > g_ooff[i])
				return 0;
			g_shutdown_ack_delivered = 1;
			return 1;
		}
	return -1;
}

static void cancel_helper_shutdown(void)
{
	int owner_fd = g_shutdown_ack_fd;

	g_shutdown_ack_fd = -1;
	g_shutdown_ack_delivered = 0;
	g_shutdown_ack_failed = 0;
	g_shutdown_ack_deadline_ms = 0;
	for (size_t i = 0; i < g_ncli; i++)
		if (g_clients[i] == owner_fd) {
			drop_client(i);
			break;
		}
#ifdef OMAQ_IPC_TEST
	{
		const char *mode = getenv("OMAQ_IPC_TEST_SAFE_SHUTDOWN_MODE");
		if (mode && strcmp(mode, "ack_fail_signal") == 0)
			request_shutdown_signal(SIGTERM);
	}
#endif
	g_shutdown_after_drain = 0;
}

#ifdef OMAQ_IPC_TEST
static int test_startup_pause(const char *phase)
{
	const char *configured = getenv("OMAQ_IPC_TEST_STARTUP_PHASE");
	const char *ready = getenv("OMAQ_IPC_TEST_STARTUP_READY");
	const char *release = getenv("OMAQ_IPC_TEST_STARTUP_RELEASE");
	int fd;

	if (!configured && !ready && !release)
		return 0;
	if (!configured || strcmp(configured, phase) != 0)
		return 0;
	if (!ready || !release || ready[0] != '/' || release[0] != '/')
		return -1;
	fd = open(ready, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	{
		int ok = write(fd, "ready\n", 6) == 6 && fsync(fd) == 0;
		if (close(fd) != 0)
			ok = 0;
		if (!ok)
			return -1;
	}
	for (int i = 0; i < 400; i++) {
		struct stat st;
		if (lstat(release, &st) == 0)
			return S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
				st.st_nlink == 1 && (st.st_mode & 0077) == 0 ? 0 : -1;
		if (errno != ENOENT)
			return -1;
		usleep(25000);
	}
	return -1;
}
#endif

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

static int init_instance_id(void)
{
	unsigned char bytes[16];
	static const char digits[] = "0123456789abcdef";
	size_t i, offset = 0;

	while (offset < sizeof(bytes)) {
		ssize_t got = getrandom(bytes + offset, sizeof(bytes) - offset, 0);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0)
			return -1;
		offset += (size_t)got;
	}
	for (i = 0; i < sizeof(bytes); i++) {
		g_instance_id[i * 2] = digits[bytes[i] >> 4];
		g_instance_id[i * 2 + 1] = digits[bytes[i] & 0x0f];
	}
	g_instance_id[32] = '\0';
	return 0;
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

static int prepare_surface_state(void)
{
	int legacy = omaq_surface_legacy_direct_present(state_dir());

	if (legacy < 0)
		return -1;
	if (legacy == 0)
		return 0;
	if (omaq_state_archive_copy(state_dir(), "surfaces.jsonl") != 0)
		return -1;
	return omaq_surface_discard_legacy_direct(state_dir()) == 1 ? 0 : -1;
}

static int load_auto_open_state(const char *fingerprint, omaq_auto_open_state *state)
{
	omaq_auto_open_source source;
	char source_name[96];

	if (!fingerprint || !state ||
	    omaq_auto_open_load(state_dir(), fingerprint, state, &source) != 0)
		return -1;
	if (source == OMAQ_AUTO_OPEN_SOURCE_LEGACY_ACTIVE ||
	    source == OMAQ_AUTO_OPEN_SOURCE_LEGACY_GLOBAL) {
		if (omaq_auto_open_source_name(fingerprint, source, source_name,
					       sizeof(source_name)) != 0 ||
		    omaq_state_archive_copy(state_dir(), source_name) != 0 ||
		    omaq_auto_open_save(state_dir(), fingerprint, state) != 0)
			return -1;
	}
	if (source != OMAQ_AUTO_OPEN_SOURCE_LEGACY_GLOBAL &&
	    omaq_state_archive_copy(state_dir(), "auto-open.json") != 0)
		return -1;
	return omaq_auto_open_retire_global(state_dir(), fingerprint);
}

static void cache_sound_result(const char *request, const char *operation,
			       const char *selected, const char *error)
{
	size_t slot;

	if (g_replaying_sound_results || !request || !request[0] || !operation ||
	    (strcmp(operation, "import") != 0 && strcmp(operation, "remove") != 0))
		return;
	for (size_t i = 0; i < g_sound_result_count; i++) {
		slot = (g_sound_result_next + SOUND_RESULT_CACHE_MAX -
			g_sound_result_count + i) % SOUND_RESULT_CACHE_MAX;
		if (g_sound_results[slot].used &&
		    strcmp(g_sound_results[slot].request, request) == 0 &&
		    strcmp(g_sound_results[slot].operation, operation) == 0)
			return;
	}
	slot = g_sound_result_next;
	memset(&g_sound_results[slot], 0, sizeof(g_sound_results[slot]));
	g_sound_results[slot].used = 1;
	snprintf(g_sound_results[slot].request,
		 sizeof(g_sound_results[slot].request), "%s", request);
	snprintf(g_sound_results[slot].operation,
		 sizeof(g_sound_results[slot].operation), "%s", operation);
	snprintf(g_sound_results[slot].selected,
		 sizeof(g_sound_results[slot].selected), "%s", selected ? selected : "");
	snprintf(g_sound_results[slot].error,
		 sizeof(g_sound_results[slot].error), "%s", error ? error : "");
	g_sound_result_next = (g_sound_result_next + 1) % SOUND_RESULT_CACHE_MAX;
	if (g_sound_result_count < SOUND_RESULT_CACHE_MAX)
		g_sound_result_count++;
}

static void emit_sound_state(const char *request, const char *operation,
			     const char *selected, const char *error)
{
	omaq_sound sounds[OMAQ_SOUND_MAX];
	char escaped_request[80 * 6 + 1], escaped_operation[32 * 6 + 1];
	char escaped_selected[(OMAQ_SOUND_ID_HEX + 1) * 6];
	char *event = NULL, *cursor;
	size_t capacity, left;
	int count, written;

	if (!request || !request[0] || !operation || !operation[0] ||
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) != 0 ||
	    omaq_json_escape(operation, escaped_operation,
			     sizeof(escaped_operation)) != 0)
		return;
	if (error) {
		char escaped_error[128], value[1024];
		cache_sound_result(request, operation, selected, error);
		if (omaq_json_escape(error, escaped_error, sizeof(escaped_error)) != 0)
			return;
		snprintf(value, sizeof(value),
			 "{\"event\":\"sound.failed\",\"op\":\"%s\",\"request\":\"%s\",\"code\":\"%s\"}",
			 escaped_operation, escaped_request, escaped_error);
		emit(value);
		return;
	}
	count = omaq_sound_list(home_dir(), sounds, OMAQ_SOUND_MAX);
	if (count < 0) {
		emit_sound_state(request, operation, NULL, "sound_state_failed");
		return;
	}
	if (selected && selected[0] &&
	    omaq_json_escape(selected, escaped_selected, sizeof(escaped_selected)) != 0)
		return;
	capacity = 320u + (size_t)count *
		((OMAQ_SOUND_ID_HEX + OMAQ_SOUND_LABEL_MAX + OMAQ_SOUND_PATH_MAX) * 6u + 128u);
	event = malloc(capacity);
	if (!event)
		return;
	written = snprintf(event, capacity,
			   "{\"event\":\"sound.list\",\"op\":\"%s\",\"request\":\"%s\",\"selected\":\"%s\",\"items\":[",
			   escaped_operation, escaped_request,
			   selected && selected[0] ? escaped_selected : "");
	if (written < 0 || (size_t)written >= capacity) {
		free(event);
		return;
	}
	cursor = event + written;
	left = capacity - (size_t)written;
	for (int i = 0; i < count; i++) {
		char escaped_id[(OMAQ_SOUND_ID_HEX + 1) * 6];
		char escaped_label[(OMAQ_SOUND_LABEL_MAX + 1) * 6];
		char escaped_path[OMAQ_SOUND_PATH_MAX * 6];
		if (omaq_json_escape(sounds[i].id, escaped_id, sizeof(escaped_id)) != 0 ||
		    omaq_json_escape(sounds[i].label, escaped_label,
				     sizeof(escaped_label)) != 0 ||
		    omaq_json_escape(sounds[i].path, escaped_path,
				     sizeof(escaped_path)) != 0) {
			free(event);
			return;
		}
		written = snprintf(cursor, left,
				   "%s{\"id\":\"%s\",\"label\":\"%s\",\"path\":\"%s\",\"size\":%llu}",
				   i ? "," : "", escaped_id, escaped_label, escaped_path,
				   (unsigned long long)sounds[i].size);
		if (written < 0 || (size_t)written >= left) {
			free(event);
			return;
		}
		cursor += written;
		left -= (size_t)written;
	}
	if (left < 3) {
		free(event);
		return;
	}
	memcpy(cursor, "]}", 3);
	cache_sound_result(request, operation, selected, NULL);
	emit(event);
	free(event);
}

static int replay_one_sound_result(const char *operation, const char *request)
{
	for (size_t i = 0; i < g_sound_result_count; i++) {
		size_t slot = (g_sound_result_next + SOUND_RESULT_CACHE_MAX -
			g_sound_result_count + i) % SOUND_RESULT_CACHE_MAX;
		if (!g_sound_results[slot].used ||
		    strcmp(g_sound_results[slot].operation, operation) != 0 ||
		    strcmp(g_sound_results[slot].request, request) != 0)
			continue;
		g_replaying_sound_results = 1;
		emit_sound_state(request, operation,
				 g_sound_results[slot].selected,
				 g_sound_results[slot].error[0]
				 ? g_sound_results[slot].error : NULL);
		g_replaying_sound_results = 0;
		return 1;
	}
	return 0;
}

static void replay_sound_results(void)
{
	g_replaying_sound_results = 1;
	for (size_t i = 0; i < g_sound_result_count; i++) {
		size_t slot = (g_sound_result_next + SOUND_RESULT_CACHE_MAX -
			g_sound_result_count + i) % SOUND_RESULT_CACHE_MAX;
		if (g_sound_results[slot].used)
			emit_sound_state(g_sound_results[slot].request,
					 g_sound_results[slot].operation,
					 g_sound_results[slot].selected,
					 g_sound_results[slot].error[0]
					 ? g_sound_results[slot].error : NULL);
	}
	g_replaying_sound_results = 0;
}

static void emit_auto_open_state(const omaq_auto_open_state *state,
				 const char *request, const char *error)
{
	char escaped_request[80 * 6 + 1];
	char *event, *cursor;
	size_t capacity, left;
	int written;

	if (!request || !request[0] ||
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) != 0)
		return;
	if (error) {
		char escaped_error[128], value[800];
		if (omaq_json_escape(error, escaped_error, sizeof(escaped_error)) != 0)
			return;
		snprintf(value, sizeof(value),
			 "{\"event\":\"settings.auto-open.failed\",\"request\":\"%s\",\"code\":\"%s\"}",
			 escaped_request, escaped_error);
		emit(value);
		return;
	}
	if (!state || state->count > OMAQ_AUTO_OPEN_MAX)
		return;
	capacity = 160u + state->count * 180u;
	event = malloc(capacity);
	if (!event)
		return;
	written = snprintf(event, capacity,
			   "{\"event\":\"settings.auto-open\",\"request\":\"%s\",\"directDefault\":%s,\"items\":[",
			   escaped_request, state->direct_default ? "true" : "false");
	if (written < 0 || (size_t)written >= capacity) {
		free(event);
		return;
	}
	cursor = event + written;
	left = capacity - (size_t)written;
	for (size_t i = 0; i < state->count; i++) {
		char escaped_id[OMAQ_AUTO_OPEN_ID_MAX * 2];
		if (omaq_json_escape(state->entries[i].conversation, escaped_id,
				     sizeof(escaped_id)) != 0)
			continue;
		written = snprintf(cursor, left,
				   "%s{\"conversation\":\"%s\",\"enabled\":%s}",
				   i ? "," : "", escaped_id,
				   state->entries[i].enabled ? "true" : "false");
		if (written < 0 || (size_t)written >= left) {
			free(event);
			return;
		}
		cursor += written;
		left -= (size_t)written;
	}
	if (left < 3) {
		free(event);
		return;
	}
	memcpy(cursor, "]}", 3);
	emit(event);
	free(event);
}

#ifdef HAVE_TOX
static int direct_state_for_friend(uint32_t friend, char *out, size_t out_size)
{
	char key[65];

	return !g_tox || omaq_tox_friend_pk_hex(g_tox, friend, key) != 0 ||
		omaq_direct_state_id(key, out, out_size) != 0 ? -1 : 0;
}

static int stable_storage_id_ok(const char *conversation, char prefix)
{
	if (!conversation || strlen(conversation) != 66 || conversation[0] != prefix ||
	    conversation[1] != ':')
		return 0;
	for (size_t i = 2; i < 66; i++)
		if (!((conversation[i] >= '0' && conversation[i] <= '9') ||
		      (conversation[i] >= 'a' && conversation[i] <= 'f')))
			return 0;
	return 1;
}

static int storage_conversation(const char *conversation, char *out, size_t out_size)
{
	if (!conversation || !out || out_size == 0)
		return -1;
	if (direct_id_ok(conversation))
		return direct_state_for_friend(direct_id_number(conversation), out, out_size);
	if (!stable_storage_id_ok(conversation, 'g') ||
	    snprintf(out, out_size, "%s", conversation) >= (int)out_size)
		return -1;
	return 0;
}

static int direct_conversation_key(const char *conversation, char *key,
				   size_t key_size)
{
	if (!g_tox || !direct_id_ok(conversation) || !key || key_size < 65)
		return -1;
	return omaq_tox_friend_pk_hex(g_tox, direct_id_number(conversation), key);
}

static int direct_operation_binding_matches(const omaq_op *op)
{
	char current[65];

	if (!op || !direct_id_ok(op->conversation) || strlen(op->key) != 64 ||
	    direct_conversation_key(op->conversation, current, sizeof(current)) != 0 ||
	    strcmp(current, op->key) != 0)
		return 0;
	if (strcmp(op->op, "file.accept") == 0 || strcmp(op->op, "file.cancel") == 0) {
		uint32_t friend, file_number;
		if (omaq_file_id_parse(op->id, &friend, &file_number) != 0 ||
		    friend != direct_id_number(op->conversation))
			return 0;
	}
	return 1;
}

static int friend_for_direct_state(const char *conversation, uint32_t *friend)
{
	uint32_t friends[OMAQ_DIRECT_STATE_FRIEND_MAX];
	int count;

	if (!g_tox || !conversation || strlen(conversation) != 66 ||
	    conversation[0] != 'd' || conversation[1] != ':' || !friend)
		return -1;
	count = omaq_tox_friend_list(g_tox, friends, OMAQ_DIRECT_STATE_FRIEND_MAX);
	if (count < 0)
		return -1;
	for (int i = 0; i < count; i++) {
		char key[65];
		if (omaq_tox_friend_pk_hex(g_tox, friends[i], key) == 0 &&
		    strcmp(key, conversation + 2) == 0) {
			*friend = friends[i];
			return 0;
		}
	}
	return -1;
}

static int find_friend_for_direct_state(const char *conversation, uint32_t *friend)
{
	size_t total = 0;
	int result;

	if (!g_tox || omaq_tox_friend_count(g_tox, &total) != 0 ||
	    total > OMAQ_DIRECT_STATE_FRIEND_MAX)
		return -1;
	result = friend_for_direct_state(conversation, friend);
	return result == 0 ? 0 : 1;
}

static int public_conversation(const char *stored, char *out, size_t out_size)
{
	uint32_t friend;

	if (!stored || !out || out_size == 0)
		return -1;
	if (stored[0] == 'd') {
		if (friend_for_direct_state(stored, &friend) != 0 ||
		    snprintf(out, out_size, "%u", friend) >= (int)out_size)
			return -1;
		return 0;
	}
	if (!stable_storage_id_ok(stored, 'g') ||
	    snprintf(out, out_size, "%s", stored) >= (int)out_size)
		return -1;
	return 0;
}

#ifdef HAVE_SIGNAL
static int ratchet_has_session_friend(uint32_t friend)
{
	char peer[OMAQ_DIRECT_STATE_ID_MAX];

	return direct_state_for_friend(friend, peer, sizeof(peer)) == 0 &&
		omaq_ratchet_has_session(g_ratchet, peer);
}

static int set_friend_ratchet_pin(uint32_t friend, const char *pin)
{
	char state_id[OMAQ_DIRECT_STATE_ID_MAX], existing[OMAQ_RK_HEX + 1];
	int state;

	if (!pin || direct_state_for_friend(friend, state_id, sizeof(state_id)) != 0)
		return -1;
	state = omaq_ratchet_pin_get(home_dir(), state_id, existing, sizeof(existing));
	if (state < 0 || (state == 1 && strcmp(existing, pin) != 0))
		return -1;
	return state == 1 ? 0 : omaq_ratchet_pin_set(home_dir(), state_id, pin);
}

static int ratchet_encrypt_friend(uint32_t friend, const char *plain,
				 char *wire, size_t wire_size)
{
	char peer[OMAQ_DIRECT_STATE_ID_MAX];

	return direct_state_for_friend(friend, peer, sizeof(peer)) != 0 ? -1 :
		omaq_ratchet_encrypt(g_ratchet, peer, plain, wire, wire_size);
}
#endif

static int bound_storage_conversation(const char *conversation, char *out,
				      size_t out_size);

static int copy_state_archive(const char *name)
{
	return omaq_state_archive_copy(state_dir(), name);
}

static int persist_reinvite_marker(void)
{
	struct stat st;
	int dir_fd, fd, rc = -1;
	static const char value[] = "reinvite required\n";

	dir_fd = open(home_dir(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (dir_fd < 0)
		return -1;
	fd = openat(dir_fd, "direct-state-reinvite.required",
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0) {
		if (errno == EEXIST &&
		    fstatat(dir_fd, "direct-state-reinvite.required", &st,
			    AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(st.st_mode) &&
		    st.st_uid == geteuid() && (st.st_mode & 0777) == 0600 && st.st_nlink == 1)
			rc = 0;
		close(dir_fd);
		return rc;
	}
	if (write(fd, value, sizeof(value) - 1) == (ssize_t)(sizeof(value) - 1) &&
	    fsync(fd) == 0 && fsync(dir_fd) == 0)
		rc = 0;
	close(fd);
	if (rc != 0)
		(void)unlinkat(dir_fd, "direct-state-reinvite.required", 0);
	close(dir_fd);
	return rc;
}

static int remove_reinvite_marker(void)
{
	int dir_fd = open(home_dir(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	int rc;

	if (dir_fd < 0)
		return -1;
	rc = unlinkat(dir_fd, "direct-state-reinvite.required", 0);
	if (rc != 0 && errno != ENOENT) {
		close(dir_fd);
		return -1;
	}
	rc = fsync(dir_fd);
	close(dir_fd);
	return rc;
}

static int reinvite_marker_present(void)
{
	struct stat st;
	int dir_fd = open(home_dir(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	int present;

	if (dir_fd < 0)
		return -1;
	if (fstatat(dir_fd, "direct-state-reinvite.required", &st,
		    AT_SYMLINK_NOFOLLOW) != 0) {
		present = errno == ENOENT ? 0 : -1;
	} else {
		present = S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
			(st.st_mode & 0777) == 0600 && st.st_nlink == 1 ? 1 : -1;
	}
	close(dir_fd);
	return present;
}

static int canonical_receipt_outbox(const omaq_receipt_outbox *source,
				    omaq_receipt_outbox *destination,
				    int *changed, int *dropped)
{
	if (!source || !destination || !changed || !dropped)
		return -1;
	omaq_receipt_outbox_init(destination);
	for (size_t i = 0; i < source->length; i++) {
		const omaq_receipt_outbox_entry *entry = &source->entries[i];
		char stored[OMAQ_DIRECT_STATE_ID_MAX];
		const char *conversation = entry->conversation;
		int bound = bound_storage_conversation(conversation, stored, sizeof(stored));
		int add;

		if (direct_id_ok(conversation)) {
			if (bound < 0)
				goto fail;
			*changed = 1;
			if (bound == 0) {
				*dropped = 1;
				continue;
			}
			conversation = stored;
		} else if (bound != 1) {
			goto fail;
		}
		add = omaq_receipt_outbox_add(destination, conversation, entry->id);
		if (add < 0)
			goto fail;
		if (add == 0) {
			omaq_receipt_outbox_entry *existing = NULL;
			*changed = 1;
			for (size_t j = 0; j < destination->length; j++)
				if (strcmp(destination->entries[j].conversation, conversation) == 0 &&
				    strcmp(destination->entries[j].id, entry->id) == 0) {
					existing = &destination->entries[j];
					break;
				}
			if (!existing)
				goto fail;
			if (entry->created > 0 &&
			    (existing->created <= 0 || entry->created < existing->created))
				existing->created = entry->created;
			if (entry->next_attempt > 0 &&
			    (existing->next_attempt <= 0 || entry->next_attempt < existing->next_attempt))
				existing->next_attempt = entry->next_attempt;
			existing->acknowledged |= entry->acknowledged;
		} else {
			omaq_receipt_outbox_entry *added =
				&destination->entries[destination->length - 1u];
			added->created = entry->created;
			added->next_attempt = entry->next_attempt;
			added->acknowledged = entry->acknowledged;
		}
	}
	return 0;
fail:
	omaq_receipt_outbox_destroy(destination);
	return -1;
}

static int bound_storage_conversation(const char *conversation, char *out,
				      size_t out_size)
{
	if (!conversation || !out || out_size == 0)
		return -1;
	if (direct_id_ok(conversation))
		return omaq_direct_state_bound_id(home_dir(), conversation, out, out_size);
	if (conversation[0] == 'd') {
		char canonical[OMAQ_DIRECT_STATE_ID_MAX];
		if (omaq_direct_state_id(conversation + 2, canonical, sizeof(canonical)) == 0 &&
		    strcmp(canonical, conversation) == 0 &&
		    snprintf(out, out_size, "%s", conversation) < (int)out_size)
			return 1;
		return -1;
	}
	if (stable_storage_id_ok(conversation, 'g') &&
	    snprintf(out, out_size, "%s", conversation) < (int)out_size)
		return 1;
	return -1;
}

static int migrate_unread_bindings(int *reinvite_required)
{
	omaq_unread_state next;
	int changed = 0, dropped = 0;

	if (!reinvite_required)
		return -1;
	omaq_unread_init(&next);
	for (size_t i = 0; i < g_unread.length; i++) {
		char stored[OMAQ_DIRECT_STATE_ID_MAX];
		const char *target = g_unread.entries[i].id;
		int bound;
		unsigned existing;
		if (target[0] == 'g' && group_was_pruned(target)) {
			changed = 1;
			continue;
		}
		bound = bound_storage_conversation(target, stored, sizeof(stored));
		if (direct_id_ok(target)) {
			if (bound < 0) {
				omaq_unread_destroy(&next);
				return -1;
			}
			if (bound == 0) {
				*reinvite_required = 1;
				changed = 1;
				dropped = 1;
				continue;
			}
			target = stored;
			changed = 1;
		} else if (bound != 1) {
			omaq_unread_destroy(&next);
			return -1;
		}
		existing = omaq_unread_count(&next, target);
		if (existing > OMAQ_UNREAD_COUNT_MAX - g_unread.entries[i].count) {
			omaq_unread_destroy(&next);
			return -1;
		}
		if (existing) {
			for (size_t j = 0; j < next.length; j++)
				if (strcmp(next.entries[j].id, target) == 0) {
					next.entries[j].count += g_unread.entries[i].count;
					changed = 1;
					break;
				}
		} else if (omaq_unread_set(&next, target, g_unread.entries[i].count) != 0) {
			omaq_unread_destroy(&next);
			return -1;
		}
	}
	if (dropped && (copy_state_archive("unread.tsv") != 0 ||
			persist_reinvite_marker() != 0)) {
		omaq_unread_destroy(&next);
		return -1;
	}
	if (changed && omaq_store_unread_save(&next, state_dir()) != 0) {
		omaq_unread_destroy(&next);
		return -1;
	}
	omaq_unread_destroy(&g_unread);
	g_unread = next;
	return 0;
}

static int migrate_receipt_bindings(int *reinvite_required)
{
	omaq_receipt_outbox next, transaction, canonical_transaction;
	int changed = 0, dropped = 0;
	int transaction_changed = 0, transaction_dropped = 0;

	if (!reinvite_required || g_receipt_outbox_invalid)
		return g_receipt_outbox_invalid ? -1 : 0;
	omaq_receipt_outbox_init(&next);
	omaq_receipt_outbox_init(&transaction);
	omaq_receipt_outbox_init(&canonical_transaction);
	if (canonical_receipt_outbox(&g_receipt_outbox, &next, &changed, &dropped) != 0)
		goto fail;
	if (dropped && (copy_state_archive("read-receipts.tsv") != 0 ||
			persist_reinvite_marker() != 0))
		goto fail;
	if (changed && omaq_receipt_outbox_save(&next, state_dir()) != 0)
		goto fail;
	if (omaq_receipt_transaction_load(&transaction, state_dir()) != 0 ||
	    canonical_receipt_outbox(&transaction, &canonical_transaction,
				     &transaction_changed, &transaction_dropped) != 0)
		goto fail;
	if (transaction_dropped &&
	    (copy_state_archive("read-transaction.tsv") != 0 ||
	     copy_state_archive("read-transaction.committed") != 0 ||
	     persist_reinvite_marker() != 0))
		goto fail;
	if (transaction_changed) {
		if (canonical_transaction.length == 0) {
			if (omaq_receipt_transaction_clear(state_dir()) != 0)
				goto fail;
		} else if (omaq_receipt_transaction_save(&canonical_transaction,
						    state_dir()) != 0) {
			goto fail;
		}
	}
	omaq_receipt_outbox_destroy(&g_receipt_outbox);
	g_receipt_outbox = next;
	omaq_receipt_outbox_init(&next);
	omaq_receipt_outbox_destroy(&transaction);
	omaq_receipt_outbox_destroy(&canonical_transaction);
	if (dropped || transaction_dropped) {
		*reinvite_required = 1;
		if (persist_reinvite_marker() != 0)
			return -1;
	}
	return 0;
fail:
	omaq_receipt_outbox_destroy(&next);
	omaq_receipt_outbox_destroy(&transaction);
	omaq_receipt_outbox_destroy(&canonical_transaction);
	return -1;
}

static int migrate_direct_state_with_removal(const char *removed_key)
{
	uint32_t friends[OMAQ_DIRECT_STATE_FRIEND_MAX];
	char pending_added_key[65], pending_added_pin[65], pending_removed[65];
	const char *authorized_removed = removed_key;
	omaq_direct_state_friend current[OMAQ_DIRECT_STATE_FRIEND_MAX];
	size_t total = 0;
	int count, i;

	int add_state, auxiliary_reinvite, marker_state, pending_state, state_reinvite = 0;

	g_direct_state_reinvite_required = 0;
	if (!g_avatar_temps_cleaned) {
		if (omaq_avatar_cleanup_temps(home_dir()) != 0)
			return -1;
		g_avatar_temps_cleaned = 1;
	}
	add_state = omaq_direct_state_add_pending(home_dir(), pending_added_key,
						 pending_added_pin);
	if (add_state < 0)
		return -1;
	if (add_state == 1) {
		char added_state_id[OMAQ_DIRECT_STATE_ID_MAX], existing_pin[OMAQ_RK_HEX + 1];
		uint32_t added_friend;
		if (omaq_direct_state_id(pending_added_key, added_state_id,
					 sizeof(added_state_id)) != 0)
			return -1;
		{
			int friend_state = find_friend_for_direct_state(added_state_id, &added_friend);
			if (friend_state < 0)
				return -1;
			if (friend_state == 0) {
				int pin_state = omaq_ratchet_pin_get(home_dir(), added_state_id,
						       existing_pin, sizeof(existing_pin));
				if (pin_state < 0 ||
				    (pin_state == 1 && strcmp(existing_pin, pending_added_pin) != 0) ||
				    (pin_state == 0 && omaq_ratchet_pin_set(home_dir(), added_state_id,
							     pending_added_pin) != 0))
					return -1;
			}
		}
	}
	pending_state = omaq_direct_state_remove_pending(home_dir(), pending_removed);
	if (pending_state < 0 || (pending_state == 1 && removed_key &&
				    strcmp(pending_removed, removed_key) != 0))
		return -1;
	if (pending_state == 1) {
		char pending_state_id[OMAQ_DIRECT_STATE_ID_MAX];
		uint32_t pending_friend;
		authorized_removed = pending_removed;
		if (omaq_direct_state_id(pending_removed, pending_state_id,
					 sizeof(pending_state_id)) != 0)
			return -1;
		if (friend_for_direct_state(pending_state_id, &pending_friend) == 0) {
			if (g_resolving_primary_uncertainty) {
				if (omaq_direct_state_remove_finish(home_dir()) != 0)
					return -1;
				pending_state = 0;
				authorized_removed = removed_key;
			} else if (omaq_tox_friend_delete(g_tox, pending_friend) != 0) {
				return -1;
			}
		}
		if (pending_state == 1 && group_binding_forget_friend(pending_removed) != 0)
			return -1;
	}
	marker_state = reinvite_marker_present();
	if (marker_state < 0)
		return -1;
	auxiliary_reinvite = marker_state > 0;
	if (!g_tox || migrate_unread_bindings(&auxiliary_reinvite) != 0 ||
	    migrate_receipt_bindings(&auxiliary_reinvite) != 0 ||
	    omaq_tox_friend_count(g_tox, &total) != 0 ||
	    total > OMAQ_DIRECT_STATE_FRIEND_MAX)
		return -1;
	count = omaq_tox_friend_list(g_tox, friends, total ? total : 1);
	if (count < 0 || (size_t)count != total)
		return -1;
	for (i = 0; i < count; i++) {
		current[i].number = friends[i];
		if (omaq_tox_friend_pk_hex(g_tox, friends[i], current[i].key) != 0)
			return -1;
	}
	if ((authorized_removed ?
	     omaq_direct_state_reconcile_removed(home_dir(), current, (size_t)count,
						 authorized_removed, &state_reinvite) :
	     omaq_direct_state_reconcile(home_dir(), current, (size_t)count,
						&state_reinvite)) != 0)
		return -1;
	g_direct_state_reinvite_required = auxiliary_reinvite || state_reinvite;
	if (g_direct_state_reinvite_required && persist_reinvite_marker() != 0)
		return -1;
	if (add_state == 1 && omaq_direct_state_add_finish(home_dir()) != 0)
		return -1;
	if (pending_state == 1 && omaq_direct_state_remove_finish(home_dir()) != 0)
		return -1;
	return 0;
}

static int migrate_direct_state(void)
{
	return migrate_direct_state_with_removal(NULL);
}

static int direct_friend_capacity_available(void)
{
	size_t count = 0;
	return g_tox && omaq_tox_friend_count(g_tox, &count) == 0 &&
		count < OMAQ_DIRECT_STATE_FRIEND_MAX;
}

static void fail_direct_state_backend(void)
{
	int primary_uncertain = g_tox && omaq_tox_primary_uncertain(g_tox);

	if (primary_uncertain) {
		(void)omaq_identity_primary_uncertain_persist(state_dir());
		emit_error("identity_primary_uncertain");
		g_identity_primary_uncertain = 1;
		emit_identity_primary_state(NULL);
		g_identity_guard_error = OMAQ_IDENTITY_GUARD_INVALID;
		g_shutdown_after_drain = 1;
	}
	g_direct_state_migration_failed = 1;
#ifdef HAVE_SIGNAL
	if (g_ratchet) {
		omaq_ratchet_close(g_ratchet);
		g_ratchet = NULL;
	}
#endif
	if (g_tox) {
		group_file_reset();
		omaq_tox_discard(g_tox);
		g_tox = NULL;
	}
	g_connection_online = 0;
}

static void fail_uncertain_primary(void)
{
	if (!g_tox || !omaq_tox_primary_uncertain(g_tox))
		return;
	fail_direct_state_backend();
}
#endif

static int ensure_private_directory(const char *path, const char *label)
{
	struct stat status;

	if (!path || !label || path[0] != '/') {
		fprintf(stderr, "omaq: %s must be an absolute path\n", label ? label : "directory");
		return -1;
	}
	if (mkdir(path, 0700) != 0 && errno != EEXIST) {
		fprintf(stderr, "omaq: could not create %s at %s: %s\n",
			label, path, strerror(errno));
		return -1;
	}
	if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode) ||
	    status.st_uid != geteuid()) {
		fprintf(stderr, "omaq: %s must be a real directory owned by uid %lu: %s\n",
			label, (unsigned long)geteuid(), path);
		return -1;
	}
	if ((status.st_mode & 0077) != 0) {
		fprintf(stderr,
			"omaq: %s permissions are %03o; run: chmod 700 -- '%s'\n",
			label, (unsigned)(status.st_mode & 0777), path);
		return -1;
	}
	return 0;
}

static int ensure_state_dir(void)
{
	const char *state = state_dir();
	char parent[512];
	char *slash;
	size_t len;
	int fd;

	if (!state || strlen(state) >= sizeof(parent) ||
	    ensure_private_directory(state, "OMAQ_STATE") != 0)
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

static int startup_executable_matches(const char *argument)
{
	struct stat expected, running;
	char *resolved;
	int matches;

	if (!argument || !strchr(argument, '/'))
		return 0;
	resolved = realpath(argument, NULL);
	if (!resolved)
		return 0;
	matches = stat(resolved, &expected) == 0 && stat("/proc/self/exe", &running) == 0 &&
		(expected.st_dev == running.st_dev && expected.st_ino == running.st_ino);
	free(resolved);
	return matches;
}

static int uninstall_marker_present(void)
{
	char path[512];
#ifdef OMAQ_IPC_TEST
	const char *ignore = getenv("OMAQ_IPC_TEST_IGNORE_UNINSTALL_MARKER");

	if (ignore && strcmp(ignore, "1") == 0)
		return 0;
#endif
	struct stat status;

	if (snprintf(path, sizeof(path), "%s/omaq.uninstalling", state_dir()) >=
	    (int)sizeof(path))
		return -1;
	if (lstat(path, &status) != 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
	    status.st_nlink != 1 || (status.st_mode & 0077) != 0) {
		fprintf(stderr, "omaq: unsafe uninstall marker: %s\n", path);
		return -1;
	}
	return 1;
}

static int take_lock(void)
{
	const char *home = home_dir();
	char path[512];
	int fd;

	if (!home)
		return -1;
	if (snprintf(path, sizeof(path), "%s/omaq.lock", home) >= (int)sizeof(path))
		return -1;
	fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	{
		struct stat status;
		if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) ||
		    status.st_uid != geteuid() || status.st_nlink != 1 ||
		    fchmod(fd, 0600) != 0) {
			close(fd);
			return -1;
		}
	}
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
	struct stat st;
	FILE *f;
	int fd;

	if (snprintf(path, sizeof(path), "%s/omaq.pid", state_dir()) >= (int)sizeof(path))
		return -1;
	fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || fchmod(fd, 0600) != 0 || ftruncate(fd, 0) != 0) {
		close(fd);
		return -1;
	}
	f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		return -1;
	}
	{
		int failed = fprintf(f, "%ld\n", (long)getpid()) < 0;
		if (!failed && fflush(f) != 0)
			failed = 1;
		if (!failed && fsync(fileno(f)) != 0)
			failed = 1;
		if (fclose(f) != 0)
			failed = 1;
		return failed ? -1 : 0;
	}
}

static int process_start_ticks(unsigned long long *start_ticks)
{
	char buffer[4096], *cursor, *end = NULL;
	ssize_t length;
	int fd;

	if (!start_ticks)
		return -1;
	fd = open("/proc/self/stat", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	length = read(fd, buffer, sizeof(buffer) - 1);
	close(fd);
	if (length <= 0 || length >= (ssize_t)sizeof(buffer))
		return -1;
	buffer[length] = '\0';
	cursor = strrchr(buffer, ')');
	if (!cursor || cursor[1] != ' ')
		return -1;
	cursor += 2;
	for (int field = 3; field < 22; field++) {
		cursor = strchr(cursor, ' ');
		if (!cursor)
			return -1;
		cursor++;
	}
	errno = 0;
	*start_ticks = strtoull(cursor, &end, 10);
	if (errno != 0 || end == cursor || (*end != ' ' && *end != '\n') ||
	    *start_ticks == 0)
		return -1;
	return 0;
}

static int write_protocol_marker(void)
{
	char path[512], tmp[560];
	const char *nonce = getenv("OMAQ_PROTOCOL_NONCE");
	FILE *f;
	size_t i;
	unsigned long long start_ticks;

	if (process_start_ticks(&start_ticks) != 0)
		return -1;
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
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld.%s", path, (long)getpid(),
		     g_instance_id) >= (int)sizeof(tmp))
		return -1;
	{
		int fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
		if (fd < 0)
			return -1;
		f = fdopen(fd, "w");
		if (!f) {
			close(fd);
			unlink(tmp);
			return -1;
		}
	}
	{
		int failed = fchmod(fileno(f), 0600) != 0;
		if (!failed && fprintf(f,
		    "{\"pid\":%ld,\"start\":%llu,\"version\":%d,\"instance\":\"%s\",\"nonce\":\"%s\"}\n",
		    (long)getpid(), start_ticks, OMAQ_PROTOCOL_VERSION,
		    g_instance_id, nonce) < 0)
			failed = 1;
		if (!failed && fflush(f) != 0)
			failed = 1;
		if (!failed && fsync(fileno(f)) != 0)
			failed = 1;
		if (fclose(f) != 0)
			failed = 1;
		if (failed) {
			unlink(tmp);
			return -1;
		}
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

static void emit_identity_primary_state(const char *request)
{
#ifdef HAVE_TOX
	char escaped_request[160], request_field[192] = "", event[320];

	if (request && request[0] &&
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) == 0)
		snprintf(request_field, sizeof(request_field),
			 ",\"request\":\"%s\"", escaped_request);
	snprintf(event, sizeof(event),
		 "{\"event\":\"identity.primary\",\"uncertain\":%s%s}",
		 g_identity_primary_uncertain ? "true" : "false", request_field);
	emit(event);
#else
	(void)request;
#endif
}

static void emit_identity_recovery_state(int force)
{
#ifdef HAVE_TOX
	int degraded = g_tox && omaq_tox_recovery_degraded(g_tox);

	if (!force && degraded == g_identity_recovery_degraded_state)
		return;
	g_identity_recovery_degraded_state = degraded;
	emit(degraded
		? "{\"event\":\"identity.recovery\",\"degraded\":true}"
		: "{\"event\":\"identity.recovery\",\"degraded\":false}");
#else
	(void)force;
#endif
}

static void emit_direct_reinvite_state(int required, const char *request)
{
	char escaped_request[160], request_field[192] = "", event[384];

	if (request && request[0] &&
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) == 0)
		snprintf(request_field, sizeof(request_field),
			 ",\"request\":\"%s\"", escaped_request);
	snprintf(event, sizeof(event),
		 "{\"event\":\"direct.reinvite\",\"required\":%s,"
		 "\"scope\":\"existing_contacts\",\"identityRetained\":true,"
		 "\"contactsRetained\":true%s}",
		 required ? "true" : "false", request_field);
	emit(event);
}

static void emit_invite_redeemed(const char *kind, const char *request)
{
	char escaped_request[160], event[320];

	if (!kind || !request || !request[0] ||
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) != 0)
		return;
	snprintf(event, sizeof(event),
		 "{\"event\":\"invite.redeemed\",\"kind\":\"%s\",\"request\":\"%s\"}",
		 kind, escaped_request);
	emit(event);
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

static void emit_unread_public(const char *conversation, const char *public_id)
{
	char esc_conv[128], key_field[96] = "", ev[420];

	if (!conversation || !public_id ||
	    omaq_json_escape(public_id, esc_conv, sizeof(esc_conv)) != 0)
		return;
	if (strlen(conversation) == OMAQ_DIRECT_STATE_ID_MAX - 1u &&
	    conversation[0] == 'd' && conversation[1] == ':')
		snprintf(key_field, sizeof(key_field), ",\"key\":\"%s\"",
			 conversation + 2);
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"unread\",\"conversation\":\"%s\"%s,\"count\":%u,\"total\":%u}",
		 esc_conv, key_field, omaq_unread_count(&g_unread, conversation),
		 omaq_unread_total(&g_unread));
	emit(ev);
}

static void emit_unread(const char *conversation)
{
	char public_id[OMAQ_DIRECT_STATE_ID_MAX];

	if (!conversation)
		return;
#ifdef HAVE_TOX
	if (g_tox) {
		if (public_conversation(conversation, public_id, sizeof(public_id)) != 0)
			return;
	} else
#endif
	if (snprintf(public_id, sizeof(public_id), "%s", conversation) >=
	    (int)sizeof(public_id))
		return;
	emit_unread_public(conversation, public_id);
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
	const char *state_id = conversation;
	int saved;
#ifdef HAVE_TOX
	char stored[OMAQ_DIRECT_STATE_ID_MAX];

	if (g_tox && storage_conversation(conversation, stored, sizeof(stored)) != 0)
		return -1;
	if (g_tox)
		state_id = stored;
#endif
	if (omaq_unread_clone(&next, &g_unread) != 0 ||
	    omaq_unread_increment(&next, state_id) != 0) {
		omaq_unread_destroy(&next);
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
		emit_unread_failed(conversation, g_unread_error_code);
		return -1;
	}
	saved = omaq_store_unread_save(&next, state_dir());
	omaq_unread_destroy(&g_unread);
	g_unread = next;
	emit_unread(state_id);
	if (saved != 0) {
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
		emit_unread_failed(conversation, g_unread_error_code);
	} else {
		g_unread_error_code[0] = '\0';
	}
	return saved == 0 ? 0 : -1;
}

static int clear_unread_stored(const char *state_id, const char *public_id)
{
	omaq_unread_state next;

	if (!state_id || !public_id || omaq_unread_clone(&next, &g_unread) != 0 ||
	    omaq_unread_clear(&next, state_id) != 0) {
		omaq_unread_destroy(&next);
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
		emit_unread_public(state_id, public_id);
		return -1;
	}
	if (omaq_store_unread_save(&next, state_dir()) != 0) {
		omaq_unread_destroy(&next);
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
		emit_unread_public(state_id, public_id);
		return -1;
	}
	omaq_unread_destroy(&g_unread);
	g_unread = next;
	g_unread_error_code[0] = '\0';
	emit_unread_public(state_id, public_id);
	return 0;
}

static int clear_unread(const char *conversation)
{
	const char *state_id = conversation;
#ifdef HAVE_TOX
	char stored[OMAQ_DIRECT_STATE_ID_MAX];

	if (g_tox && storage_conversation(conversation, stored, sizeof(stored)) != 0)
		return -1;
	if (g_tox)
		state_id = stored;
#endif
	return clear_unread_stored(state_id, conversation);
}

#ifdef HAVE_TOX
static int unread_conversation_available(const char *conversation, void *userdata)
{
	uint32_t number;

	(void)userdata;
	if (!g_tox || !conversation)
		return -1;
	if (conversation[0] == 'g')
		return group_was_pruned(conversation) ? 0 :
			(omaq_group_id_parse(conversation, &number) == 0 ? 1 : 0);
	if (conversation[0] == 'd')
		return friend_for_direct_state(conversation, &number) == 0 ? 1 : 0;
	if (direct_id_ok(conversation))
		return 0;
	return -1;
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
			     const char *unread_id, const char *binding_id)
{
	char esc_conv[128], esc_request[OMAQ_JSON_STR_MAX], key_field[96] = "";
	char prefix[520];
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
	if (binding_id && strlen(binding_id) == OMAQ_DIRECT_STATE_ID_MAX - 1u &&
	    binding_id[0] == 'd' && binding_id[1] == ':')
		snprintf(key_field, sizeof(key_field), ",\"key\":\"%s\"", binding_id + 2);
	if (esc_request[0] && unread_id)
		wr = snprintf(prefix, sizeof(prefix),
			      "{\"event\":\"%s\",\"conversation\":\"%s\"%s,\"request\":\"%s\",\"unread\":%u,\"items\":[",
			      event, esc_conv, key_field, esc_request,
			      omaq_unread_count(&g_unread, unread_id));
	else if (esc_request[0])
		wr = snprintf(prefix, sizeof(prefix),
			      "{\"event\":\"%s\",\"conversation\":\"%s\"%s,\"request\":\"%s\",\"items\":[",
			      event, esc_conv, key_field, esc_request);
	else
		wr = snprintf(prefix, sizeof(prefix),
			      "{\"event\":\"%s\",\"conversation\":\"%s\"%s,\"items\":[",
			      event, esc_conv, key_field);
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

static void direct_event_key_field(const char *conversation, char *field,
				   size_t field_size)
{
	if (!field || field_size == 0)
		return;
	field[0] = '\0';
#ifdef HAVE_TOX
	if (g_tox && direct_id_ok(conversation)) {
		char key[65];
		if (direct_conversation_key(conversation, key, sizeof(key)) == 0)
			(void)snprintf(field, field_size, ",\"key\":\"%s\"", key);
	}
#else
	(void)conversation;
#endif
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
		esc_text[2800], key_field[96], ev[3900];
	int has_id, has_reply, has_request, is_attachment;

	if (!conversation || !text || !dir ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(text, esc_text, sizeof(esc_text)) != 0)
		return;
	has_id = id && id[0] && omaq_json_escape(id, esc_id, sizeof(esc_id)) == 0;
	has_reply = reply && reply[0] && omaq_json_escape(reply, esc_reply, sizeof(esc_reply)) == 0;
	has_request = request && request[0] &&
		omaq_json_escape(request, esc_request, sizeof(esc_request)) == 0;
	is_attachment = kind &&
		(strcmp(kind, "file") == 0 || strcmp(kind, "image") == 0);
	direct_event_key_field(conversation, key_field, sizeof(key_field));
	if (has_id && has_reply && has_request) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\"%s,\"id\":\"%s\",\"reply\":\"%s\",\"request\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, key_field, esc_id, esc_reply, esc_request, esc_text, dir);
	} else if (has_id && has_request) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\"%s,\"id\":\"%s\",\"request\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, key_field, esc_id, esc_request, esc_text, dir);
	} else if (has_id && has_reply) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\"%s,\"id\":\"%s\",\"reply\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, key_field, esc_id, esc_reply, esc_text, dir);
	} else if (has_id && is_attachment) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\"%s,\"id\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\",\"kind\":\"%s\"}",
			 esc_conv, key_field, esc_id, esc_text, dir, kind);
	} else if (has_id) {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\"%s,\"id\":\"%s\",\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, key_field, esc_id, esc_text, dir);
	} else {
		snprintf(ev, sizeof(ev),
			 "{\"event\":\"message\",\"conversation\":\"%s\"%s,\"text\":\"%s\",\"dir\":\"%s\"}",
			 esc_conv, key_field, esc_text, dir);
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

static int direct_invite_redeem_op(const omaq_op *op)
{
	return op && strcmp(op->op, "invite.redeem") == 0 && op->id[0];
}

static int direct_reinvite_clear_op(const omaq_op *op)
{
	return op && strcmp(op->op, "direct.reinvite.clear") == 0 && op->id[0];
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
	char esc_conv[128], esc_id[128], esc_text[2800], key_field[96], ev[3300];

	if (!conversation || !id || !text ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0 ||
	    omaq_json_escape(text, esc_text, sizeof(esc_text)) != 0)
		return;
	direct_event_key_field(conversation, key_field, sizeof(key_field));
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message.updated\",\"conversation\":\"%s\"%s,\"id\":\"%s\",\"text\":\"%s\",\"deleted\":%s,\"edited\":%s}",
		 esc_conv, key_field, esc_id, esc_text, deleted ? "true" : "false",
		 deleted ? "false" : "true");
	emit(ev);
}

static void emit_message_reaction(const char *conversation, const char *id,
                                  const char *emoji, const char *actor)
{
	char esc_conv[128], esc_id[128], esc_emoji[128], key_field[96], ev[620];

	if (!conversation || !id || !emoji || !actor ||
	    (strcmp(actor, "me") != 0 && strcmp(actor, "peer") != 0) ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0 ||
	    omaq_json_escape(emoji, esc_emoji, sizeof(esc_emoji)) != 0)
		return;
	direct_event_key_field(conversation, key_field, sizeof(key_field));
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"message.reaction\",\"conversation\":\"%s\"%s,\"id\":\"%s\",\"emoji\":\"%s\",\"actor\":\"%s\"}",
		 esc_conv, key_field, esc_id, esc_emoji, actor);
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
	char esc_conv[128], esc_id[128], key_field[96], ev[460];

	if (!event || (strcmp(event, "receipt") != 0 && strcmp(event, "receipt.sent") != 0) ||
	    !conversation || !id || !state ||
	    (strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0) ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0)
		return;
	direct_event_key_field(conversation, key_field, sizeof(key_field));
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"%s\",\"conversation\":\"%s\"%s,\"id\":\"%s\",\"state\":\"%s\"}",
		 event, esc_conv, key_field, esc_id, state);
	emit(ev);
}

static void emit_receipt_event(const char *conversation, const char *id, const char *state)
{
	emit_receipt_event_name("receipt", conversation, id, state);
}

static void emit_group_receipt_event(const char *conversation, const char *id,
				     const char *state, const char *actor)
{
	char esc_conv[128], esc_id[128], event[560];
	if (!conversation || !id || !state || !actor || strlen(actor) != 64 ||
	    (strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0) ||
	    omaq_json_escape(conversation, esc_conv, sizeof(esc_conv)) != 0 ||
	    omaq_json_escape(id, esc_id, sizeof(esc_id)) != 0)
		return;
	for (size_t i = 0; i < 64; i++)
		if (!((actor[i] >= '0' && actor[i] <= '9') ||
		      (actor[i] >= 'a' && actor[i] <= 'f')))
			return;
	snprintf(event, sizeof(event),
		 "{\"event\":\"receipt\",\"conversation\":\"%s\",\"id\":\"%s\",\"state\":\"%s\",\"actor\":\"%s\"}",
		 esc_conv, esc_id, state, actor);
	emit(event);
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
	if (ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_message_reaction_wire(uint32_t friend, const char *conversation,
                                      const char *id, const char *emoji)
{
	char plain[256], wire[560];

	if (!g_tox || !g_ratchet || !conversation || !id ||
	    omaq_message_reaction_wire_pack(plain, sizeof(plain), id, emoji) != 0 ||
	    ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_group_invite_wire(uint32_t friend, const char *url)
{
	char plain[OMAQ_URL_MAX + 8], wire[3600];

	if (!g_tox || !g_ratchet || !url ||
	    snprintf(plain, sizeof(plain), "OQGI1|%s", url) >= (int)sizeof(plain) ||
	    ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_ratchet_bundle_frame(uint32_t friend, const char *peer,
				     const char *kind, int legacy)
{
	char bundle[900], message[940];

	if (!g_tox || !g_ratchet || !peer)
		return -1;
	if (kind && strcmp(kind, "q") == 0) {
		if (omaq_ratchet_request_bundle(g_ratchet, peer, bundle,
						 sizeof(bundle)) != 0)
			return -1;
	} else if (omaq_ratchet_bundle(g_ratchet, peer, bundle, sizeof(bundle)) != 0) {
		return -1;
	}
	if (legacy) {
		if (snprintf(message, sizeof(message), "OQB1%s", bundle) >=
		    (int)sizeof(message))
			return -1;
	} else if (!kind || (strcmp(kind, "q") != 0 && strcmp(kind, "r") != 0) ||
		   snprintf(message, sizeof(message), "OQB2|%s|%s", kind, bundle) >=
		   (int)sizeof(message)) {
		return -1;
	}
	return omaq_tox_send(g_tox, friend, message);
}

static int send_ratchet_response_frame(uint32_t friend, const char *peer,
				       const char *request_bundle)
{
	char bundle[900], message[940];

	if (!g_tox || !g_ratchet ||
	    omaq_ratchet_response_bundle(g_ratchet, peer, request_bundle, bundle,
					 sizeof(bundle)) < 0 ||
	    snprintf(message, sizeof(message), "OQB2|r|%s", bundle) >=
		    (int)sizeof(message))
		return -1;
	return omaq_tox_send(g_tox, friend, message);
}

static int ratchet_recovery_allowed(const char *peer)
{
	int free_index = -1;
	int64_t now = (int64_t)time(NULL);

	if (!omaq_ratchet_peer_ok(peer))
		return 0;
	for (int i = 0; i < OMAQ_DIRECT_STATE_FRIEND_MAX; i++) {
		if (!g_ratchet_recovery[i].used) {
			if (free_index < 0)
				free_index = i;
			continue;
		}
		if (strcmp(g_ratchet_recovery[i].peer, peer) != 0)
			continue;
		if (now < g_ratchet_recovery[i].retry_after)
			return 0;
		g_ratchet_recovery[i].retry_after = now + 10;
		return 1;
	}
	if (free_index < 0)
		return 0;
	g_ratchet_recovery[free_index].used = 1;
	g_ratchet_recovery[free_index].retry_after = now + 10;
	snprintf(g_ratchet_recovery[free_index].peer,
		 sizeof(g_ratchet_recovery[free_index].peer), "%s", peer);
	return 1;
}

static int request_ratchet_session(uint32_t friend)
{
	char peer[OMAQ_DIRECT_STATE_ID_MAX];
	int modern, legacy;

	if (!g_tox || !g_ratchet)
		return -1;
	if (ratchet_has_session_friend(friend))
		return 1;
	if (direct_state_for_friend(friend, peer, sizeof(peer)) != 0)
		return -1;
	modern = send_ratchet_bundle_frame(friend, peer, "q", 0);
	legacy = send_ratchet_bundle_frame(friend, peer, NULL, 1);
	return modern == 0 || legacy == 0 ? 0 : -1;
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
	g_group_invite_native_pending = 0;
	g_group_invite_cleanup_pending = 0;
	g_group_invite_cleanup_code[0] = '\0';
	g_group_invite_native_attempts = 0;
	g_group_invite_native_retry_ms = 0;
}

static int send_group_invite_response(uint32_t friend, const char *prefix,
				      const char *invite_id, const char *group)
{
	char plain[112], wire[380];

	if (!prefix || !invite_id || !group ||
	    snprintf(plain, sizeof(plain), "%s|%s|%s", prefix, invite_id, group) >=
		    (int)sizeof(plain) ||
	    ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_group_binding_confirmation(uint32_t friend, const char *group,
					   const char *invite_id,
					   const char *member_key)
{
	char plain[192], wire[520];

	if (!stable_group_id_syntax(group) || !group_bind_invite_id_ok(invite_id) ||
	    !lower_hex_key_ok(member_key) ||
	    snprintf(plain, sizeof(plain), "OQX1|gmbd|%s|%s|%s", invite_id,
		     group, member_key) >= (int)sizeof(plain) ||
	    ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_group_binding_ready(uint32_t friend, const char *invite_id)
{
	char plain[OMAQ_INVITE_ID_MAX + 12], wire[380];

	if (!group_bind_invite_id_ok(invite_id) ||
	    snprintf(plain, sizeof(plain), "OQX1|gmbc|%s", invite_id) >=
		    (int)sizeof(plain) ||
	    ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_group_binding_ack(uint32_t friend, const char *invite_id)
{
	char plain[OMAQ_INVITE_ID_MAX + 12], wire[380];

	if (!invite_id || !invite_id[0] ||
	    snprintf(plain, sizeof(plain), "OQX1|gmba|%s", invite_id) >=
		    (int)sizeof(plain) ||
	    ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
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
	} else {
		/* Native delivery runs from the main loop after this tox callback returns. */
		g_group_invite_native_pending = 1;
		g_group_invite_native_attempts = 0;
		g_group_invite_native_retry_ms = 0;
		g_group_invite_send_deadline = (int64_t)time(NULL) + 30;
		return;
	}
	clear_pending_group_invite();
}

static void fail_pending_native_group_invite(const char *code)
{
	g_group_invite_native_pending = 0;
	if (group_binding_forget_expect(g_group_invite_send_group,
					g_group_invite_send_id) != 0) {
		g_group_invite_cleanup_pending = 1;
		snprintf(g_group_invite_cleanup_code,
			 sizeof(g_group_invite_cleanup_code), "%s", code);
		g_group_invite_native_retry_ms = monotonic_millis() + 250;
		return;
	}
	emit_group_invite_result(g_group_invite_send_friend,
				 g_group_invite_send_group,
				 g_group_invite_send_request,
				 "group.invite.failed", code);
	clear_pending_group_invite();
}

static void retry_pending_native_group_invite(void)
{
	omaq_role self = ROLE_MEMBER;
	int64_t now;
	int rc;

	if (!g_group_invite_send_pending ||
	    (!g_group_invite_native_pending && !g_group_invite_cleanup_pending))
		return;
	now = monotonic_millis();
	if (now < 0) {
		fail_pending_native_group_invite("forbidden");
		return;
	}
	if (g_group_invite_native_retry_ms > now)
		return;
	if (g_group_invite_cleanup_pending) {
		if (group_binding_forget_expect(g_group_invite_send_group,
						g_group_invite_send_id) != 0) {
			g_group_invite_native_retry_ms = now + 250;
			return;
		}
		emit_group_invite_result(g_group_invite_send_friend,
					 g_group_invite_send_group,
					 g_group_invite_send_request,
					 "group.invite.failed",
					 g_group_invite_cleanup_code[0]
					 ? g_group_invite_cleanup_code : "forbidden");
		clear_pending_group_invite();
		return;
	}
	if (!pending_group_invite_friend_matches(g_group_invite_send_friend) ||
	    omaq_group_self_role(g_tox, g_group_invite_send_group, &self) != 0) {
		fail_pending_native_group_invite("forbidden");
		return;
	}
	rc = omaq_group_invite_friend(g_tox, g_group_invite_send_group,
				      g_group_invite_send_friend, self, ROLE_MEMBER);
	g_group_invite_native_attempts++;
	if (rc == 0) {
		emit_group_invite_result(g_group_invite_send_friend,
					 g_group_invite_send_group,
					 g_group_invite_send_request,
					 "group.invite.sent", NULL);
		clear_pending_group_invite();
		return;
	}
	if (rc < 0 || g_group_invite_native_attempts >= 25) {
		fail_pending_native_group_invite("forbidden");
		return;
	}
	g_group_invite_native_retry_ms = now + 40;
}

static void expire_pending_group_invite(void)
{
	if (!g_group_invite_send_pending || g_group_invite_cleanup_pending ||
	    (int64_t)time(NULL) < g_group_invite_send_deadline)
		return;
	if (g_group_invite_native_pending) {
		fail_pending_native_group_invite("forbidden");
		return;
	}
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
	    ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_receipt_capability_wire(uint32_t friend, const char *conversation)
{
	char wire[520];
	static const char capability[] = "OQX1|receipt-ack-v1";

	if (!g_tox || !g_ratchet || !conversation ||
	    ratchet_encrypt_friend(friend, capability, wire, sizeof(wire)) != 0)
		return -1;
	return omaq_tox_send(g_tox, friend, wire);
}

static int send_receipt_confirm_wire(uint32_t friend, const char *conversation,
				     const char *id)
{
	char plain[256], wire[520];

	if (!g_tox || !g_ratchet || !conversation || !id ||
	    omaq_receipt_confirm_wire_pack(plain, sizeof(plain), id, "read", "-") != 0 ||
	    ratchet_encrypt_friend(friend, plain, wire, sizeof(wire)) != 0)
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
		if (g_receipt_outbox.entries[i].acknowledged) {
			char public_id[OMAQ_DIRECT_STATE_ID_MAX];
			if (public_conversation(g_receipt_outbox.entries[i].conversation,
						public_id, sizeof(public_id)) == 0)
				emit_receipt_event_name("receipt.sent", public_id,
					g_receipt_outbox.entries[i].id, "read");
		}
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
		} else if (entry->conversation[0] == 'd') {
#ifdef HAVE_SIGNAL
			uint32_t friend;
			if (friend_for_direct_state(entry->conversation, &friend) == 0 &&
			    omaq_tox_online(g_tox) && omaq_tox_friend_online(g_tox, friend)) {
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
	g_group_invite_native_pending = 0;
	g_group_invite_cleanup_pending = 0;
	g_group_invite_cleanup_code[0] = '\0';
	g_group_invite_native_attempts = 0;
	g_group_invite_native_retry_ms = 0;
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
	g_avatar_temps_cleaned = 0;
	g_identity_recovery_degraded_state = -1;
#ifdef HAVE_SIGNAL
	memset(g_ratchet_recovery, 0, sizeof(g_ratchet_recovery));
#endif
	omaq_control_rate_init(&g_group_control_rate);
	omaq_control_rate_init(&g_group_typing_rate);
	g_av_reset_requested = 0;
	g_av_reset_next = 0;
	g_av_reset_reported = 0;
	group_file_reset();
	omaq_group_reset();
	omaq_file_reset();
	omaq_receipt_outbox_destroy(&g_receipt_outbox);
	omaq_receipt_outbox_init(&g_receipt_outbox);
	g_receipt_outbox_invalid = 0;
	g_receipt_retry_cursor = 0;
	g_receipt_transaction_pending = 0;
	g_receipt_transaction_retry_after = 0;
	g_receipt_recovery_committed = 0;
	memset(g_receipt_capabilities, 0, sizeof(g_receipt_capabilities));
#ifdef HAVE_TOX
	omaq_av_reset();
#endif
	emit_invite_state("", 0, "clear", NULL);
}

#define IDENTITY_ARCHIVE_PATHS 18

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
		"history", "avatars", "files", "ratchet", "groups.tsv", "group-friends.tsv",
		"direct-friends.tsv", "direct-add.pending", "direct-remove.pending",
		"direct-state-reinvite.required"
	};
	const char *state_names[] = {
		"surfaces.jsonl", "", "auto-open.json", "unread.tsv", "group-bind.pending",
		"read-receipts.tsv", "read-transaction.tsv", "read-transaction.committed"
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
		const char *base = i < 10 ? home_dir() : state_dir();
		const char *name = i < 10 ? home_names[i] :
			(i == 11 ? auto_open_name : state_names[i - 10]);
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

static int identity_guard_restore_marker_path(char *path, size_t path_size)
{
	return !path || snprintf(path, path_size, "%s/identity-guard-restore.txn",
				 state_dir()) >= (int)path_size ? -1 : 0;
}

static int write_identity_guard_restore_marker(const char *token, int had_primary,
					 int had_uncertainty, int had_stale, int had_ack)
{
	char path[640];
	FILE *file = NULL;
	int rc = -1;

	if (!identity_token_ok(token) || (had_primary != 0 && had_primary != 1) ||
	    (had_uncertainty != 0 && had_uncertainty != 1) ||
	    (had_stale != 0 && had_stale != 1) || (had_ack != 0 && had_ack != 1) ||
	    identity_guard_restore_marker_path(path, sizeof(path)) != 0)
		return -1;
	file = fopen(path, "wx");
	if (!file)
		return -1;
	{
		int failed = fchmod(fileno(file), 0600) != 0 ||
			fprintf(file, "OMAQIGR1\n%s\n%d\n%d\n%d\n%d\n", token,
				had_primary, had_uncertainty, had_stale, had_ack) <= 0 ||
			fflush(file) != 0 || fsync(fileno(file)) != 0;
		if (fclose(file) != 0)
			failed = 1;
		file = NULL;
		if (!failed && fsync_directory(state_dir()) == 0)
			rc = 0;
	}
	if (rc != 0) {
		(void)unlink(path);
		(void)fsync_directory(state_dir());
	}
	return rc;
}

static int remove_identity_guard_restore_marker(void)
{
	char path[640];

	if (identity_guard_restore_marker_path(path, sizeof(path)) != 0 ||
	    (unlink(path) != 0 && errno != ENOENT) || fsync_directory(state_dir()) != 0)
		return -1;
	return 0;
}

static int rollback_identity_guard_restore(const char *token, int had_primary,
					    int had_uncertainty, int had_stale, int had_ack)
{
	char backup[700], primary[600];
	struct stat st;

	if (!identity_token_ok(token) || (had_primary != 0 && had_primary != 1) ||
	    (had_uncertainty != 0 && had_uncertainty != 1) ||
	    (had_stale != 0 && had_stale != 1) || (had_ack != 0 && had_ack != 1) ||
	    snprintf(backup, sizeof(backup), "%s/tox.save.guard-backup.%s",
		     home_dir(), token) >= (int)sizeof(backup) ||
	    snprintf(primary, sizeof(primary), "%s/tox.save", home_dir()) >=
		(int)sizeof(primary))
		return -1;
	if (had_primary) {
		if (omaq_identity_import(home_dir(), backup, 1) != 0)
			return -1;
	} else if (lstat(primary, &st) == 0) {
		if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
		    (st.st_mode & 0777) != 0600 || unlink(primary) != 0)
			return -1;
	} else if (errno != ENOENT) {
		return -1;
	}
	if (fsync_directory(home_dir()) != 0 ||
	    (had_uncertainty && omaq_identity_primary_uncertain_persist(state_dir()) != 0) ||
	    omaq_identity_recovery_stale_persist(state_dir()) != 0 ||
	    (had_ack && omaq_identity_primary_ack_persist(state_dir()) != 0) ||
	    remove_identity_guard_restore_marker() != 0)
		return -1;
	if (had_primary && ((unlink(backup) != 0 && errno != ENOENT) ||
			    fsync_directory(home_dir()) != 0))
		g_identity_backup_cleanup_failed = 1;
	return 0;
}

static int recover_identity_guard_restore(void)
{
	char marker[640], header[16], token[96], had_line[8], uncertainty_line[8];
	char stale_line[8], ack_line[8], extra[2], backup[700];
	struct stat st;
	FILE *file;
	int fd, had_ack, had_primary, had_stale, had_uncertainty;

	if (identity_guard_restore_marker_path(marker, sizeof(marker)) != 0)
		return -1;
	fd = open(marker, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0777) != 0600 || st.st_size <= 0 ||
	    st.st_size > 160) {
		close(fd);
		return -1;
	}
	file = fdopen(fd, "r");
	if (!file) {
		close(fd);
		return -1;
	}
	{
		int invalid = !fgets(header, sizeof(header), file) ||
			strcmp(header, "OMAQIGR1\n") != 0 ||
			!fgets(token, sizeof(token), file) || !strchr(token, '\n') ||
			!fgets(had_line, sizeof(had_line), file) || !strchr(had_line, '\n') ||
			!fgets(uncertainty_line, sizeof(uncertainty_line), file) ||
			!strchr(uncertainty_line, '\n') ||
			!fgets(stale_line, sizeof(stale_line), file) || !strchr(stale_line, '\n') ||
			!fgets(ack_line, sizeof(ack_line), file) || !strchr(ack_line, '\n') ||
			(fgets(extra, sizeof(extra), file) != NULL) || !feof(file);
		if (fclose(file) != 0)
			invalid = 1;
		if (invalid)
			return -1;
	}
	token[strcspn(token, "\n")] = '\0';
	had_line[strcspn(had_line, "\n")] = '\0';
	uncertainty_line[strcspn(uncertainty_line, "\n")] = '\0';
	stale_line[strcspn(stale_line, "\n")] = '\0';
	ack_line[strcspn(ack_line, "\n")] = '\0';
	if (!identity_token_ok(token) ||
	    (strcmp(had_line, "0") != 0 && strcmp(had_line, "1") != 0) ||
	    (strcmp(uncertainty_line, "0") != 0 && strcmp(uncertainty_line, "1") != 0) ||
	    (strcmp(stale_line, "0") != 0 && strcmp(stale_line, "1") != 0) ||
	    (strcmp(ack_line, "0") != 0 && strcmp(ack_line, "1") != 0))
		return -1;
	had_primary = had_line[0] == '1';
	had_uncertainty = uncertainty_line[0] == '1';
	had_stale = stale_line[0] == '1';
	had_ack = ack_line[0] == '1';
	if (had_primary &&
	    (snprintf(backup, sizeof(backup), "%s/tox.save.guard-backup.%s",
		      home_dir(), token) >= (int)sizeof(backup) || lstat(backup, &st) != 0 ||
	     !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
	     (st.st_mode & 0777) != 0600))
		return -1;
	return rollback_identity_guard_restore(token, had_primary, had_uncertainty,
					       had_stale, had_ack) == 0 ? 1 : -1;
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
	    fprintf(f, "%s\n%s\nprepared\n", token, fingerprint) <= 0 ||
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

static int update_identity_marker_phase(const char *token, const char *fingerprint,
					const char *phase)
{
	char path[640], temporary[700];
	FILE *file = NULL;
	uint32_t nonce;
	int fd = -1, rc = -1;

	if (!identity_token_ok(token) || !fingerprint || strlen(fingerprint) != 64 ||
	    !phase || (strcmp(phase, "prepared") != 0 && strcmp(phase, "replacement") != 0) ||
	    identity_marker_path(path, sizeof(path)) != 0 ||
	    getrandom(&nonce, sizeof(nonce), 0) != (ssize_t)sizeof(nonce) ||
	    snprintf(temporary, sizeof(temporary), "%s.tmp.%08x", path, nonce) >=
		(int)sizeof(temporary))
		return -1;
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	file = fdopen(fd, "w");
	if (!file) {
		close(fd);
		unlink(temporary);
		return -1;
	}
	fd = -1;
	{
		int failed = fprintf(file, "%s\n%s\n%s\n", token, fingerprint, phase) <= 0;
		if (!failed && fflush(file) != 0)
			failed = 1;
		if (!failed && fsync(fileno(file)) != 0)
			failed = 1;
		if (fclose(file) != 0)
			failed = 1;
		file = NULL;
		if (failed)
			goto done;
	}
	if (rename(temporary, path) != 0 || fsync_directory(state_dir()) != 0)
		goto done;
	rc = 0;
done:
	if (file)
		fclose(file);
	if (fd >= 0)
		close(fd);
	if (rc != 0)
		unlink(temporary);
	return rc;
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
	static const char *prefixes[] = {
		"tox.save.replace-backup.", "tox.save.guard-backup."
	};
	DIR *dir;
	struct dirent *entry;
	int failed = 0;

	dir = opendir(home_dir());
	if (!dir)
		return -1;
	while ((entry = readdir(dir)) != NULL) {
		const char *token = NULL;
		char path[760];
		for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
			size_t length = strlen(prefixes[i]);
			if (strncmp(entry->d_name, prefixes[i], length) == 0) {
				token = entry->d_name + length;
				break;
			}
		}
		if (!token || !identity_backup_token_ok(token))
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
	char marker[640], token[96], fingerprint[80], phase[32], backup[700];
	identity_state_archive archive;
	struct stat st;
	FILE *f;
	int fd, i;

	if (identity_marker_path(marker, sizeof(marker)) != 0)
		return -1;
	fd = open(marker, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0077) != 0 || st.st_size <= 0 ||
	    st.st_size > 192) {
		close(fd);
		return -1;
	}
	f = fdopen(fd, "r");
	if (!f) {
		close(fd);
		return -1;
	}
	if (!fgets(token, sizeof(token), f) || !strchr(token, '\n') ||
	    strchr(token, '\n')[1] != '\0' ||
	    !fgets(fingerprint, sizeof(fingerprint), f) || !strchr(fingerprint, '\n') ||
	    strchr(fingerprint, '\n')[1] != '\0') {
		fclose(f);
		return -1;
	}
	if (!fgets(phase, sizeof(phase), f)) {
		if (!feof(f)) {
			fclose(f);
			return -1;
		}
		snprintf(phase, sizeof(phase), "prepared");
	} else if (!strchr(phase, '\n') || strchr(phase, '\n')[1] != '\0' ||
		   fgetc(f) != EOF) {
		fclose(f);
		return -1;
	}
	if (fclose(f) != 0)
		return -1;
	token[strcspn(token, "\n")] = '\0';
	fingerprint[strcspn(fingerprint, "\n")] = '\0';
	phase[strcspn(phase, "\n")] = '\0';
	if (!identity_token_ok(token) ||
	    (strcmp(phase, "prepared") != 0 && strcmp(phase, "replacement") != 0) ||
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
	if ((strcmp(phase, "replacement") == 0 && remove_reinvite_marker() != 0) ||
	    omaq_identity_import(home_dir(), backup, 1) != 0 ||
	    restore_identity_state(&archive) != 0 || fsync_directory(home_dir()) != 0 ||
	    omaq_identity_guard_restore(state_dir(), fingerprint) != 0 ||
	    (strcmp(phase, "replacement") == 0 &&
	     omaq_identity_recovery_stale_persist(state_dir()) != 0) ||
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

static void emit_groups(const char *request)
{
	char ev[2400], escaped_request[80 * 6 + 1], request_field[80 * 6 + 32];
	uint32_t generation = ++g_group_generation;
	int groups = omaq_group_count();
	int total_members = 0;

	if (!request)
		request = "";
	if (groups < 0 || groups > OMAQ_GROUPS_MAX ||
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) != 0) {
		emit_error("group_projection_failed");
		return;
	}
	request_field[0] = '\0';
	if (escaped_request[0])
		snprintf(request_field, sizeof(request_field),
			 ",\"request\":\"%s\"", escaped_request);
	for (int i = 0; i < groups; i++) {
		uint32_t gnum = omaq_group_number_at(i);
		int members;
		if (gnum == UINT32_MAX || (members = omaq_group_peer_count(gnum)) < 0 ||
		    members > OMAQ_GROUP_PEERS || total_members > 100 - members) {
			emit_error("group_projection_failed");
			return;
		}
		total_members += members;
	}
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"group.list.begin\",\"generation\":\"%u\",\"instance\":\"%s\"%s,\"groups\":%d,\"members\":%d}",
		 generation, g_instance_id, request_field, groups, total_members);
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
			 "{\"event\":\"group.info\",\"generation\":\"%u\",\"instance\":\"%s\"%s,\"group\":\"%s\",\"title\":\"%s\",\"members\":%d,\"limit\":%d}",
			 generation, g_instance_id, request_field, gid, escaped_title,
			 members, omaq_group_limit(gnum));
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
				 "{\"event\":\"group.member\",\"generation\":\"%u\",\"instance\":\"%s\"%s,\"group\":\"%s\",\"peer\":\"%u\",\"key\":\"%s\",\"friendKey\":\"%s\",\"name\":\"%s\",\"role\":\"%s\",\"online\":%s,\"self\":%s}",
				 generation, g_instance_id, request_field, gid, peer,
				 member_key, friend_key, escaped_name,
				 omaq_role_name(omaq_group_peer_cached_role(gnum, member)),
				 omaq_group_peer_online(gnum, member) ? "true" : "false",
				 omaq_group_peer_self(gnum, member) ? "true" : "false");
			emit(ev);
		}
	}
	snprintf(ev, sizeof(ev),
		 "{\"event\":\"group.list.end\",\"generation\":\"%u\",\"instance\":\"%s\"%s,\"groups\":%d,\"members\":%d}",
		 generation, g_instance_id, request_field, groups, total_members);
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
	emit_groups(NULL);
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
		char name[129], escaped_name[280], id[16], friend_key[65];
		char avatar_id[OMAQ_DIRECT_STATE_ID_MAX], avatar[512], escaped_avatar[600];
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
		if (omaq_direct_state_id(friend_key, avatar_id, sizeof(avatar_id)) == 0 &&
		    omaq_avatar_reconcile(home_dir(), avatar_id) == 1 &&
		    omaq_avatar_dest(home_dir(), avatar_id, avatar, sizeof(avatar)) == 0 &&
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

	if (omaq_avatar_reconcile(home_dir(), "self") == 1 &&
	    omaq_avatar_dest(home_dir(), "self", dest, sizeof(dest)) == 0 &&
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
	char key[65], event[240];

	(void)ud;
	if (omaq_tox_friend_pk_hex(g_tox, friend, key) != 0)
		return;
	snprintf(event, sizeof(event),
		 "{\"event\":\"typing\",\"conversation\":\"%u\",\"key\":\"%s\",\"typing\":%s}",
		 friend, key, typing ? "true" : "false");
	emit(event);
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
			emit_groups(NULL);
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
				receipt_rc = omaq_store_update_group_receipt_changed(home_dir(), gid,
									      receipt_id, receipt_state,
									      sender);
			if (receipt_rc == 1)
				emit_group_receipt_event(gid, receipt_id, receipt_state, sender);
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
		if (omaq_message_id_reserved(wire_id))
			return;
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
		group_file_peer_removed(gnum, peer, self);
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
	char conv[16], state_conv[OMAQ_DIRECT_STATE_ID_MAX];
	char mid[97], wire_id[97], wire_reply[97], wire_text[1400];
	char receipt_id[97], receipt_state[16], reaction_id[97], reaction_emoji[32];
	const char *display = text;
	int has_wire_id = 0;
#ifdef HAVE_SIGNAL
	char plain[1400];
#endif
	(void)ud;
	snprintf(conv, sizeof(conv), "%u", friend);
	if (direct_state_for_friend(friend, state_conv, sizeof(state_conv)) != 0)
		return;
#ifdef HAVE_SIGNAL
	if (text && (strncmp(text, "OQB1", 4) == 0 ||
		     strncmp(text, "OQB2|q|", 7) == 0 ||
		     strncmp(text, "OQB2|r|", 7) == 0)) {
		char expected[OMAQ_RK_HEX + 1];
		const char *bundle = text + (text[3] == '2' ? 7 : 4);
		int modern_request = strncmp(text, "OQB2|q|", 7) == 0;
		int modern_response = strncmp(text, "OQB2|r|", 7) == 0;
		int accepted;
		if (!g_ratchet || omaq_ratchet_pin_get(home_dir(), state_conv, expected,
						 sizeof(expected)) != 1)
			return;
		accepted = omaq_ratchet_accept_bundle(g_ratchet, state_conv, bundle, expected);
		if (accepted < 0)
			return;
		if (modern_request) {
			/* Requests are idempotent: replay the exact durable response even
			 * after its private one-time prekey has been consumed. */
			(void)send_ratchet_response_frame(friend, state_conv, bundle);
			finish_pending_group_invite(friend);
		} else if (!modern_response && accepted == 0) {
			(void)send_ratchet_bundle_frame(friend, state_conv, NULL, 1);
			finish_pending_group_invite(friend);
		} else if (modern_response) {
			finish_pending_group_invite(friend);
		}
		return;
	}
	if (!text || strncmp(text, "OQR1", 4) != 0 || !g_ratchet)
		return;
	{
		int decrypt_result = omaq_ratchet_decrypt(g_ratchet, state_conv, text,
						   plain, sizeof(plain));
		if (decrypt_result != 0) {
			if (decrypt_result == OMAQ_RATCHET_DECRYPT_RECOVER &&
			    ratchet_recovery_allowed(state_conv) &&
			    omaq_ratchet_reset_session(g_ratchet, state_conv) == 0)
				(void)request_ratchet_session(friend);
			return;
		}
	}
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
				receipt_outbox_note_ack(state_conv, confirm_id);
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
		if (omaq_store_update_reaction(home_dir(), state_conv, reaction_id,
					       reaction_emoji, "peer") == 0)
			emit_message_reaction(conv, reaction_id, reaction_emoji, "peer");
		return;
	}
	if (text && strncmp(text, "OQE1|", 5) == 0) {
		char action_id[80], action_text[1400];
		if (omaq_message_edit_wire_unpack(text, action_id, sizeof(action_id), action_text, sizeof(action_text)) == 0 &&
		    omaq_message_apply_edit(home_dir(), state_conv, action_id, action_text) == 0) {
			emit_message_update(conv, action_id, action_text, 0);
			return;
		}
		return;
	}
	if (text && strncmp(text, "OQD1|", 5) == 0) {
		char action_id[80];
		if (omaq_message_delete_wire_unpack(text, action_id, sizeof(action_id)) == 0 &&
		    omaq_message_apply_delete(home_dir(), state_conv, action_id) == 0) {
			emit_message_update(conv, action_id, "", 1);
			return;
		}
		return;
	}
	if (omaq_receipt_wire_unpack(text, receipt_id, sizeof(receipt_id),
				     receipt_state, sizeof(receipt_state)) == 0) {
		int receipt_rc = omaq_store_update_receipt_changed(home_dir(), state_conv,
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
		if (omaq_store_message_id_used(home_dir(), state_conv, wire_id) != 0 ||
		    omaq_message_append_id_reply(home_dir(), state_conv, "peer", display, "in",
					 wire_id, wire_reply) != 0)
			return;
		snprintf(mid, sizeof(mid), "%s", wire_id);
	} else if (omaq_message_append_with_id(home_dir(), state_conv, "peer", display,
					       "in", mid, sizeof(mid)) != 0) {
		return;
	}
	(void)note_unread(conv);
	emit_message_event(conv, mid, wire_reply, display, "in");
#ifdef HAVE_SIGNAL
	if (has_wire_id)
		(void)send_receipt_wire(friend, conv, wire_id, "delivered");
#endif
}
#endif

static int attachment_directory_open(char *path, size_t pathn)
{
	struct stat status;
	int fd;

	if (!path || snprintf(path, pathn, "%s/attachments", home_dir()) >= (int)pathn)
		return -1;
	if (mkdir(path, 0700) != 0 && errno != EEXIST)
		return -1;
	if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode) ||
	    status.st_uid != geteuid() || (status.st_mode & 0077) != 0)
		return -1;
	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0 || fstat(fd, &status) != 0 || !S_ISDIR(status.st_mode) ||
	    status.st_uid != geteuid() || (status.st_mode & 0077) != 0) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	return fd;
}

static int attachment_stage_names(const char *request, char *staging,
				  size_t stagingn, char *final, size_t finaln)
{
	if (!omaq_message_id_ok(request) || strlen(request) >= 80 ||
	    snprintf(staging, stagingn, ".staging-%s", request) >= (int)stagingn ||
	    snprintf(final, finaln, "%s.png", request) >= (int)finaln)
		return -1;
	return 0;
}

static int attachment_pending_name(const char *request, char *pending, size_t pendingn)
{
	if (!omaq_message_id_ok(request) || strlen(request) >= 80 ||
	    snprintf(pending, pendingn, ".pending-%s", request) >= (int)pendingn)
		return -1;
	return 0;
}

static int attachment_stage_create(const char *request, char *path, size_t pathn)
{
	char directory[512], staging[128], final[128], pending[128];
	struct stat status;
	int directory_fd, fd = -1, created = 0, rc = -1;

	if (attachment_stage_names(request, staging, sizeof(staging), final,
				   sizeof(final)) != 0 ||
	    attachment_pending_name(request, pending, sizeof(pending)) != 0 ||
	    (directory_fd = attachment_directory_open(directory, sizeof(directory))) < 0)
		return -1;
	if (fstatat(directory_fd, final, &status, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
		goto done;
	if (fstatat(directory_fd, pending, &status, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT)
		goto done;
	fd = openat(directory_fd, staging,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd >= 0)
		created = 1;
	if (fd < 0 || fsync(fd) != 0 || close(fd) != 0) {
		fd = -1;
		goto done;
	}
	fd = -1;
	if (fsync(directory_fd) != 0 ||
	    snprintf(path, pathn, "%s/%s", directory, staging) >= (int)pathn)
		goto done;
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (rc != 0 && created)
		(void)unlinkat(directory_fd, staging, 0);
	close(directory_fd);
	return rc;
}

static int attachment_stage_path_matches(const char *request, const char *path,
					 int final_path, char *expected,
					 size_t expectedn, int *directory_fd_out,
					 char *name, size_t namen)
{
	char directory[512], staging[128], final[128];
	int directory_fd;

	if (!path || attachment_stage_names(request, staging, sizeof(staging), final,
					    sizeof(final)) != 0 ||
	    (directory_fd = attachment_directory_open(directory, sizeof(directory))) < 0)
		return -1;
	if (snprintf(name, namen, "%s", final_path ? final : staging) >= (int)namen ||
	    snprintf(expected, expectedn, "%s/%s", directory, name) >= (int)expectedn ||
	    strcmp(path, expected) != 0) {
		close(directory_fd);
		return -1;
	}
	*directory_fd_out = directory_fd;
	return 0;
}

static int attachment_stage_commit(const char *request, const char *path,
				   char *final_path, size_t final_pathn)
{
	char expected[512], staging[128], final[128], pending[128];
	struct stat status;
	int directory_fd, pending_fd = -1, linked = 0, rc = -1;

	if (attachment_stage_path_matches(request, path, 0, expected, sizeof(expected),
					  &directory_fd, staging, sizeof(staging)) != 0 ||
	    attachment_stage_names(request, staging, sizeof(staging), final,
				   sizeof(final)) != 0 ||
	    attachment_pending_name(request, pending, sizeof(pending)) != 0)
		return -1;
	if (fstatat(directory_fd, staging, &status, AT_SYMLINK_NOFOLLOW) != 0 ||
	    !S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
	    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
	    status.st_size <= 0 || status.st_size > OMAQ_FILE_MAX ||
	    omaq_inline_image_canonicalize_file(expected) != 0)
		goto done;
	pending_fd = openat(directory_fd, pending,
			    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (pending_fd < 0 || fsync(pending_fd) != 0 || close(pending_fd) != 0) {
		pending_fd = -1;
		goto done;
	}
	pending_fd = -1;
	if (linkat(directory_fd, staging, directory_fd, final, 0) != 0)
		goto done;
	linked = 1;
	if (unlinkat(directory_fd, staging, 0) != 0 || fsync(directory_fd) != 0 ||
	    snprintf(final_path, final_pathn, "%s/attachments/%s", home_dir(), final) >=
		(int)final_pathn)
		goto done;
	rc = 0;
done:
	if (pending_fd >= 0)
		close(pending_fd);
	if (rc != 0) {
		(void)unlinkat(directory_fd, staging, 0);
		if (linked)
			(void)unlinkat(directory_fd, final, 0);
		(void)unlinkat(directory_fd, pending, 0);
		(void)fsync(directory_fd);
	}
	close(directory_fd);
	return rc;
}

static int attachment_stage_discard(const char *request, const char *path)
{
	char directory[512], staging[128], final[128], pending[128];
	char staging_path[512], final_path[512];
	struct stat status;
	int directory_fd, pending_exists = 0;

	if (attachment_stage_names(request, staging, sizeof(staging), final,
				   sizeof(final)) != 0 ||
	    attachment_pending_name(request, pending, sizeof(pending)) != 0 ||
	    (directory_fd = attachment_directory_open(directory, sizeof(directory))) < 0)
		return -1;
	if (snprintf(staging_path, sizeof(staging_path), "%s/%s", directory, staging) >=
		(int)sizeof(staging_path) ||
	    snprintf(final_path, sizeof(final_path), "%s/%s", directory, final) >=
		(int)sizeof(final_path) ||
	    (path && path[0] && strcmp(path, staging_path) != 0 &&
	     strcmp(path, final_path) != 0))
		goto invalid;
	if (fstatat(directory_fd, pending, &status, AT_SYMLINK_NOFOLLOW) == 0) {
		if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
		    status.st_nlink != 1 || (status.st_mode & 0077) != 0)
			goto invalid;
		pending_exists = 1;
	} else if (errno != ENOENT) {
		goto invalid;
	}
	if (fstatat(directory_fd, staging, &status, AT_SYMLINK_NOFOLLOW) == 0) {
		if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
		    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
		    unlinkat(directory_fd, staging, 0) != 0)
			goto invalid;
	} else if (errno != ENOENT) {
		goto invalid;
	}
	if (fstatat(directory_fd, final, &status, AT_SYMLINK_NOFOLLOW) == 0) {
		if (!pending_exists || !S_ISREG(status.st_mode) ||
		    status.st_uid != geteuid() || status.st_nlink != 1 ||
		    (status.st_mode & 0077) != 0 || unlinkat(directory_fd, final, 0) != 0)
			goto invalid;
	} else if (errno != ENOENT) {
		goto invalid;
	}
	if (pending_exists && unlinkat(directory_fd, pending, 0) != 0)
		goto invalid;
	if (fsync(directory_fd) != 0)
		goto invalid;
	close(directory_fd);
	return 0;
invalid:
	close(directory_fd);
	return -1;
}

static int attachment_stage_owner_find(const char *request)
{
	for (int i = 0; request && i < ATTACHMENT_STAGE_OWNER_MAX; i++)
		if (g_attachment_stage_owners[i].used &&
		    strcmp(g_attachment_stage_owners[i].request, request) == 0)
			return i;
	return -1;
}

static int attachment_stage_owner_add(const char *request, const char *path, int owner_fd)
{
	int slot = -1;
	if (owner_fd < 0)
		return 0;
	if (!request || !path || strlen(request) >= sizeof(g_attachment_stage_owners[0].request) ||
	    strlen(path) >= sizeof(g_attachment_stage_owners[0].path) ||
	    attachment_stage_owner_find(request) >= 0)
		return -1;
	for (int i = 0; i < ATTACHMENT_STAGE_OWNER_MAX; i++)
		if (!g_attachment_stage_owners[i].used) {
			slot = i;
			break;
		}
	if (slot < 0)
		return -1;
	memset(&g_attachment_stage_owners[slot], 0,
	       sizeof(g_attachment_stage_owners[slot]));
	g_attachment_stage_owners[slot].used = 1;
	g_attachment_stage_owners[slot].owner_fd = owner_fd;
	snprintf(g_attachment_stage_owners[slot].request,
		 sizeof(g_attachment_stage_owners[slot].request), "%s", request);
	snprintf(g_attachment_stage_owners[slot].path,
		 sizeof(g_attachment_stage_owners[slot].path), "%s", path);
	return 0;
}

/* Returns 1 for this owner, 0 when no live owner exists, and -1 for another owner. */
static int attachment_stage_owner_match(const char *request, int owner_fd)
{
	int slot = attachment_stage_owner_find(request);
	if (slot < 0)
		return 0;
	return owner_fd >= 0 && g_attachment_stage_owners[slot].owner_fd == owner_fd ? 1 : -1;
}

static int attachment_stage_owner_update(const char *request, const char *path,
					 int owner_fd)
{
	int slot = attachment_stage_owner_find(request);
	if (owner_fd < 0)
		return 0;
	if (slot < 0 || g_attachment_stage_owners[slot].owner_fd != owner_fd ||
	    !path || strlen(path) >= sizeof(g_attachment_stage_owners[slot].path))
		return -1;
	snprintf(g_attachment_stage_owners[slot].path,
		 sizeof(g_attachment_stage_owners[slot].path), "%s", path);
	return 0;
}

/* Returns 1 for this owner, 0 for an unmanaged path, and -1 for another owner. */
static int attachment_stage_path_owner(const char *path, int owner_fd)
{
	for (int i = 0; path && i < ATTACHMENT_STAGE_OWNER_MAX; i++)
		if (g_attachment_stage_owners[i].used &&
		    strcmp(g_attachment_stage_owners[i].path, path) == 0)
			return owner_fd >= 0 &&
				g_attachment_stage_owners[i].owner_fd == owner_fd ? 1 : -1;
	return 0;
}

static int attachment_stage_owner_adopt(const char *path, int owner_fd)
{
	int ownership = attachment_stage_path_owner(path, owner_fd);
	if (ownership < 0)
		return -1;
	if (ownership == 0)
		return 0;
	for (int i = 0; i < ATTACHMENT_STAGE_OWNER_MAX; i++)
		if (g_attachment_stage_owners[i].used &&
		    g_attachment_stage_owners[i].owner_fd == owner_fd &&
		    strcmp(g_attachment_stage_owners[i].path, path) == 0) {
			memset(&g_attachment_stage_owners[i], 0,
			       sizeof(g_attachment_stage_owners[i]));
			return 0;
		}
	return -1;
}

static void attachment_stage_owner_forget_request(const char *request)
{
	int slot = attachment_stage_owner_find(request);
	if (slot >= 0)
		memset(&g_attachment_stage_owners[slot], 0,
		       sizeof(g_attachment_stage_owners[slot]));
}

static void attachment_stage_owner_disconnect(int owner_fd)
{
	for (int i = 0; i < ATTACHMENT_STAGE_OWNER_MAX; i++) {
		if (!g_attachment_stage_owners[i].used ||
		    g_attachment_stage_owners[i].owner_fd != owner_fd)
			continue;
		(void)attachment_stage_discard(g_attachment_stage_owners[i].request,
					       g_attachment_stage_owners[i].path);
		memset(&g_attachment_stage_owners[i], 0,
		       sizeof(g_attachment_stage_owners[i]));
	}
}

static int attachment_stage_cleanup(void)
{
	char directory[512];
	struct dirent *entry;
	struct stat status;
	DIR *stream;
	int directory_fd, scan_fd, rc = 0;

	directory_fd = attachment_directory_open(directory, sizeof(directory));
	if (directory_fd < 0)
		return -1;
	scan_fd = dup(directory_fd);
	stream = scan_fd >= 0 ? fdopendir(scan_fd) : NULL;
	if (!stream) {
		if (scan_fd >= 0)
			close(scan_fd);
		close(directory_fd);
		return -1;
	}
	while ((entry = readdir(stream)) != NULL) {
		char staging[128], final[128];
		const char *request;
		int pending = 0;

		if (strncmp(entry->d_name, ".staging-", 9) == 0)
			request = entry->d_name + 9;
		else if (strncmp(entry->d_name, ".pending-", 9) == 0) {
			request = entry->d_name + 9;
			pending = 1;
		} else {
			continue;
		}
		if (!omaq_message_id_ok(request) || strlen(request) >= 80 ||
		    attachment_stage_names(request, staging, sizeof(staging), final,
					   sizeof(final)) != 0 ||
		    fstatat(directory_fd, entry->d_name, &status, AT_SYMLINK_NOFOLLOW) != 0 ||
		    !S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
		    status.st_nlink != 1 || (status.st_mode & 0077) != 0) {
			rc = -1;
			continue;
		}
		if (pending && fstatat(directory_fd, final, &status,
				       AT_SYMLINK_NOFOLLOW) == 0) {
			if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
			    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
			    unlinkat(directory_fd, final, 0) != 0) {
				rc = -1;
				continue;
			}
		} else if (pending && errno != ENOENT) {
			rc = -1;
			continue;
		}
		if (unlinkat(directory_fd, entry->d_name, 0) != 0)
			rc = -1;
	}
	if (closedir(stream) != 0 || fsync(directory_fd) != 0)
		rc = -1;
	close(directory_fd);
	return rc;
}

static int attachment_pending_update(const char *path, int finish_mode)
{
	char directory[512], expected[512], staging[128], final[128];
	char pending[128], request[80];
	const char *basename;
	struct stat status;
	size_t request_length, path_prefix;
	int directory_fd, rc = -1;

	if (!path || finish_mode < 0 || finish_mode > 2 ||
	    (directory_fd = attachment_directory_open(directory,
							 sizeof(directory))) < 0)
		return -1;
	path_prefix = strlen(directory);
	if (strncmp(path, directory, path_prefix) != 0 || path[path_prefix] != '/') {
		close(directory_fd);
		return 0;
	}
	basename = path + path_prefix + 1;
	request_length = strlen(basename);
	if (request_length <= 4 || strcmp(basename + request_length - 4, ".png") != 0 ||
	    request_length - 4 >= sizeof(request)) {
		close(directory_fd);
		return 0;
	}
	request_length -= 4;
	memcpy(request, basename, request_length);
	request[request_length] = '\0';
	if (attachment_stage_names(request, staging, sizeof(staging), final,
				   sizeof(final)) != 0 ||
	    attachment_pending_name(request, pending, sizeof(pending)) != 0 ||
	    snprintf(expected, sizeof(expected), "%s/%s", directory, final) >=
		(int)sizeof(expected) || strcmp(path, expected) != 0) {
		close(directory_fd);
		return 0;
	}
	if (fstatat(directory_fd, pending, &status, AT_SYMLINK_NOFOLLOW) != 0) {
		rc = errno == ENOENT ? 0 : -1;
		goto done;
	}
	if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
	    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
	    fstatat(directory_fd, final, &status, AT_SYMLINK_NOFOLLOW) != 0 ||
	    !S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
	    status.st_nlink != 1 || (status.st_mode & 0077) != 0)
		goto done;
	if (finish_mode == 0) {
		rc = 1;
		goto done;
	}
	if ((finish_mode == 2 && unlinkat(directory_fd, final, 0) != 0) ||
	    unlinkat(directory_fd, pending, 0) != 0 || fsync(directory_fd) != 0)
		goto done;
	rc = 1;
done:
	close(directory_fd);
	return rc;
}

static int attachment_managed_remove(const char *path)
{
	char directory[512], expected[512], staging[128], final[128], request[80];
	const char *basename;
	struct stat status;
	size_t prefix, length;
	int directory_fd;

	if (!path || (directory_fd = attachment_directory_open(directory,
							 sizeof(directory))) < 0)
		return -1;
	prefix = strlen(directory);
	if (strncmp(path, directory, prefix) != 0 || path[prefix] != '/')
		goto invalid;
	basename = path + prefix + 1;
	length = strlen(basename);
	if (length <= 4 || strcmp(basename + length - 4, ".png") != 0 ||
	    length - 4 >= sizeof(request))
		goto invalid;
	length -= 4;
	memcpy(request, basename, length);
	request[length] = '\0';
	if (attachment_stage_names(request, staging, sizeof(staging), final,
				   sizeof(final)) != 0 ||
	    snprintf(expected, sizeof(expected), "%s/%s", directory, final) >=
		(int)sizeof(expected) || strcmp(expected, path) != 0 ||
	    fstatat(directory_fd, final, &status, AT_SYMLINK_NOFOLLOW) != 0 ||
	    !S_ISREG(status.st_mode) || status.st_uid != geteuid() ||
	    status.st_nlink != 1 || (status.st_mode & 0077) != 0 ||
	    unlinkat(directory_fd, final, 0) != 0 || fsync(directory_fd) != 0)
		goto invalid;
	close(directory_fd);
	return 0;
invalid:
	close(directory_fd);
	return -1;
}

static void emit_attachment_discarded(const char *request)
{
	char escaped_request[80 * 6 + 1], event[640];

	if (!request || !request[0] ||
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) != 0)
		return;
	snprintf(event, sizeof(event),
		 "{\"event\":\"attachment.discarded\",\"request\":\"%s\"}",
		 escaped_request);
	emit(event);
}

static void emit_attachment_stage(const char *request, const char *path)
{
	char escaped_request[80 * 6 + 1], escaped_path[OMAQ_JSON_STR_MAX * 6 + 1];
	char event[OMAQ_JSON_STR_MAX * 6 + 800];

	if (!request || !request[0] || !path ||
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) != 0 ||
	    omaq_json_escape(path, escaped_path, sizeof(escaped_path)) != 0)
		return;
	snprintf(event, sizeof(event),
		 "{\"event\":\"attachment.stage\",\"request\":\"%s\",\"path\":\"%s\"}",
		 escaped_request, escaped_path);
	emit(event);
}

static void emit_attachment_inspection(const char *request, const char *path,
				       int accepted)
{
	char escaped_request[80 * 6 + 1], escaped_path[OMAQ_JSON_STR_MAX * 6 + 1];
	char event[OMAQ_JSON_STR_MAX * 6 + 800];

	if (!request || !request[0] || !path ||
	    omaq_json_escape(request, escaped_request, sizeof(escaped_request)) != 0 ||
	    omaq_json_escape(path, escaped_path, sizeof(escaped_path)) != 0)
		return;
	if (accepted)
		snprintf(event, sizeof(event),
			 "{\"event\":\"attachment.inspected\",\"request\":\"%s\",\"path\":\"%s\",\"kind\":\"image\"}",
			 escaped_request, escaped_path);
	else
		snprintf(event, sizeof(event),
			 "{\"event\":\"attachment.rejected\",\"request\":\"%s\",\"path\":\"%s\",\"code\":\"unsupported_image\"}",
			 escaped_request, escaped_path);
	emit(event);
}

#ifdef HAVE_TOX
static int group_file_event_id(const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES],
			       char *out, size_t outn)
{
	char hex[OMAQ_GROUP_FILE_ID_HEX + 1];

	if (!out || outn < 3 + sizeof(hex) || omaq_group_file_id_hex(id, hex) != 0)
		return -1;
	return snprintf(out, outn, "gf:%s", hex) < (int)outn ? 0 : -1;
}

static int group_file_hash_fd(int fd, uint64_t size, uint8_t hash[32])
{
	EVP_MD_CTX *context = NULL;
	struct stat before, after;
	uint8_t buffer[65536];
	uint64_t offset = 0;
	unsigned int hash_length = 0;
	int result = -1;

	if (fd < 0 || !hash || size == 0 || size > OMAQ_FILE_MAX ||
	    fstat(fd, &before) != 0 || !S_ISREG(before.st_mode) ||
	    before.st_size < 0 || (uint64_t)before.st_size != size)
		return -1;
	context = EVP_MD_CTX_new();
	if (!context || EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1)
		goto done;
	while (offset < size) {
		size_t wanted = (size_t)(size - offset);
		ssize_t got;
		if (wanted > sizeof(buffer))
			wanted = sizeof(buffer);
		got = pread(fd, buffer, wanted, (off_t)offset);
		if (got != (ssize_t)wanted ||
		    EVP_DigestUpdate(context, buffer, wanted) != 1)
			goto done;
		offset += wanted;
	}
	if (EVP_DigestFinal_ex(context, hash, &hash_length) != 1 || hash_length != 32 ||
	    fstat(fd, &after) != 0 || before.st_dev != after.st_dev ||
	    before.st_ino != after.st_ino || before.st_size != after.st_size ||
	    before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
	    before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
	    before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
	    before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
		goto done;
	result = 0;
done:
	explicit_bzero(buffer, sizeof(buffer));
	EVP_MD_CTX_free(context);
	return result;
}

static int group_file_group_binding_ok(uint32_t group_number, const char *group)
{
	char current[OMAQ_GROUP_ID_MAX];
	return group && omaq_group_id_format(group_number, current, sizeof(current)) == 0 &&
		strcmp(current, group) == 0;
}

static int group_file_peer_identity(uint32_t group_number, uint32_t peer,
				    char key[65], char *name, size_t namen,
				    int *self)
{
	char local_name[OMAQ_GROUP_MEMBER_NAME_MAX + 1];
	size_t name_length = 0;
	int role = 0, online = 0, local_self = 0;

	if (!g_tox || !key || omaq_tox_group_peer_info(g_tox, group_number, peer,
			key, 65, local_name, sizeof(local_name), &name_length,
			&role, &online, &local_self) != 0 || !online ||
	    strlen(key) != 64 ||
	    !omaq_group_member_name_bytes_ok(local_name, name_length))
		return -1;
	if (name && namen && snprintf(name, namen, "%s", local_name) >= (int)namen)
		return -1;
	if (self)
		*self = local_self;
	return 0;
}

static void emit_group_file(const char *state, const char *group,
			    const char *id, const char *name, uint64_t size,
			    const char *path, const char *dir, const char *request,
			    const char *kind, const char *sender, const char *code)
{
	char event[OMAQ_JSON_STR_MAX * 6 + 2200];
	char escaped_group[128], escaped_id[256], escaped_name[OMAQ_FILE_NAME_MAX * 6 + 1];
	char escaped_path[OMAQ_JSON_STR_MAX * 6 + 1], escaped_request[80 * 6 + 1];
	char escaped_sender[512], escaped_code[128];
	const char *safe_kind = kind && strcmp(kind, "image") == 0 ? "image" : "file";
	int written;

	if (!state || !group || !id || !dir ||
	    (strcmp(dir, "in") != 0 && strcmp(dir, "out") != 0) ||
	    omaq_json_escape(group, escaped_group, sizeof(escaped_group)) != 0 ||
	    omaq_json_escape(id, escaped_id, sizeof(escaped_id)) != 0)
		return;
	escaped_name[0] = escaped_path[0] = escaped_request[0] = '\0';
	escaped_sender[0] = escaped_code[0] = '\0';
	if ((name && omaq_json_escape(name, escaped_name, sizeof(escaped_name)) != 0) ||
	    (path && omaq_json_escape(path, escaped_path, sizeof(escaped_path)) != 0) ||
	    (request && omaq_json_escape(request, escaped_request,
					sizeof(escaped_request)) != 0) ||
	    (sender && omaq_json_escape(sender, escaped_sender,
				       sizeof(escaped_sender)) != 0) ||
	    (code && omaq_json_escape(code, escaped_code, sizeof(escaped_code)) != 0))
		return;
	if (strcmp(state, "offer") == 0)
		written = snprintf(event, sizeof(event),
			"{\"event\":\"file.offer\",\"id\":\"%s\",\"conversation\":\"%s\",\"name\":\"%s\",\"size\":%llu,\"kind\":\"%s\",\"sender\":\"%s\",\"dir\":\"in\"}",
			escaped_id, escaped_group, escaped_name,
			(unsigned long long)size, safe_kind, escaped_sender);
	else if (strcmp(state, "sending") == 0)
		written = snprintf(event, sizeof(event),
			"{\"event\":\"file.sending\",\"id\":\"%s\",\"conversation\":\"%s\",\"kind\":\"%s\",\"dir\":\"out\",\"request\":\"%s\"}",
			escaped_id, escaped_group, safe_kind, escaped_request);
	else if (strcmp(state, "done") == 0) {
		if (escaped_code[0])
			written = snprintf(event, sizeof(event),
				"{\"event\":\"file.done\",\"id\":\"%s\",\"conversation\":\"%s\",\"path\":\"%s\",\"kind\":\"%s\",\"sender\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\",\"code\":\"%s\"}",
				escaped_id, escaped_group, escaped_path, safe_kind,
				escaped_sender, dir, escaped_request, escaped_code);
		else
			written = snprintf(event, sizeof(event),
				"{\"event\":\"file.done\",\"id\":\"%s\",\"conversation\":\"%s\",\"path\":\"%s\",\"kind\":\"%s\",\"sender\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
				escaped_id, escaped_group, escaped_path, safe_kind,
				escaped_sender, dir, escaped_request);
	}
	else if (strcmp(state, "canceled") == 0)
		written = snprintf(event, sizeof(event),
			"{\"event\":\"file.canceled\",\"id\":\"%s\",\"conversation\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			escaped_id, escaped_group, dir, escaped_request);
	else if (strcmp(state, "failed") == 0)
		written = snprintf(event, sizeof(event),
			"{\"event\":\"file.failed\",\"id\":\"%s\",\"conversation\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\",\"code\":\"%s\"}",
			escaped_id, escaped_group, dir, escaped_request,
			escaped_code[0] ? escaped_code : "group_file_failed");
	else
		return;
	if (written >= 0 && (size_t)written < sizeof(event))
		emit(event);
}

static void emit_group_attachment_message(const char *group, const char *id,
					  const char *path, const char *dir,
					  const char *kind, const char *sender)
{
	char event[OMAQ_JSON_STR_MAX * 6 + 1500];
	char escaped_group[128], escaped_id[256], escaped_path[OMAQ_JSON_STR_MAX * 6 + 1];
	char escaped_sender[512];
	const char *safe_kind = kind && strcmp(kind, "image") == 0 ? "image" : "file";
	int written;

	if (!group || !id || !path || !dir ||
	    omaq_json_escape(group, escaped_group, sizeof(escaped_group)) != 0 ||
	    omaq_json_escape(id, escaped_id, sizeof(escaped_id)) != 0 ||
	    omaq_json_escape(path, escaped_path, sizeof(escaped_path)) != 0)
		return;
	if (strcmp(dir, "in") == 0) {
		if (!sender || omaq_json_escape(sender, escaped_sender,
					      sizeof(escaped_sender)) != 0)
			return;
		written = snprintf(event, sizeof(event),
			"{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"sender\":\"%s\",\"text\":\"%s\",\"dir\":\"in\",\"kind\":\"%s\"}",
			escaped_group, escaped_id, escaped_sender, escaped_path, safe_kind);
	} else {
		written = snprintf(event, sizeof(event),
			"{\"event\":\"message\",\"conversation\":\"%s\",\"id\":\"%s\",\"text\":\"%s\",\"dir\":\"out\",\"kind\":\"%s\"}",
			escaped_group, escaped_id, escaped_path, safe_kind);
	}
	if (written >= 0 && (size_t)written < sizeof(event))
		emit(event);
}

static int group_file_send_control(uint32_t group_number, uint32_t peer,
				   uint8_t type,
				   const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	uint8_t packet[64];
	int length = omaq_group_file_control_pack(packet, sizeof(packet), type, id);

	if (length < 0)
		return -1;
	return omaq_tox_group_custom_private_send(g_tox, group_number, peer,
						  packet, (size_t)length);
}

static void group_file_in_drop(int index, int remove_path)
{
	if (index < 0 || index >= GROUP_FILE_IN_MAX || !g_group_file_in[index].used)
		return;
	if (g_group_file_in[index].fd >= 0)
		close(g_group_file_in[index].fd);
	if (remove_path && g_group_file_in[index].path[0])
		unlink(g_group_file_in[index].path);
	memset(&g_group_file_in[index], 0, sizeof(g_group_file_in[index]));
	g_group_file_in[index].fd = -1;
}

static void group_file_out_drop(int index)
{
	if (index < 0 || index >= GROUP_FILE_OUT_MAX || !g_group_file_out[index].used)
		return;
	if (g_group_file_out[index].fd >= 0)
		close(g_group_file_out[index].fd);
	memset(&g_group_file_out[index], 0, sizeof(g_group_file_out[index]));
	g_group_file_out[index].fd = -1;
}

static int group_file_in_find(uint32_t group_number,
			      const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	for (int i = 0; i < GROUP_FILE_IN_MAX; i++)
		if (g_group_file_in[i].used &&
		    g_group_file_in[i].group_number == group_number &&
		    memcmp(g_group_file_in[i].id, id, OMAQ_GROUP_FILE_ID_BYTES) == 0)
			return i;
	return -1;
}

static int group_file_out_find(uint32_t group_number,
			       const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	for (int i = 0; i < GROUP_FILE_OUT_MAX; i++)
		if (g_group_file_out[i].used &&
		    g_group_file_out[i].group_number == group_number &&
		    memcmp(g_group_file_out[i].id, id, OMAQ_GROUP_FILE_ID_BYTES) == 0)
			return i;
	return -1;
}

static void group_file_in_fail(int index, const char *state, const char *code,
			       int notify_sender)
{
	group_file_incoming *incoming;
	char current_key[65];

	if (index < 0 || index >= GROUP_FILE_IN_MAX || !g_group_file_in[index].used)
		return;
	incoming = &g_group_file_in[index];
	if (notify_sender &&
	    group_file_group_binding_ok(incoming->group_number, incoming->group) &&
	    group_file_peer_identity(incoming->group_number, incoming->sender_peer,
				     current_key, NULL, 0, NULL) == 0 &&
	    strcmp(current_key, incoming->sender_key) == 0)
		(void)group_file_send_control(incoming->group_number, incoming->sender_peer,
					      strcmp(state, "canceled") == 0
					      ? OMAQ_GROUP_FILE_CANCEL : OMAQ_GROUP_FILE_FAIL,
					      incoming->id);
	emit_group_file(state, incoming->group, incoming->event_id, NULL, 0, NULL,
			"in", NULL, incoming->kind, incoming->sender_key, code);
	group_file_in_drop(index, 1);
}

static int group_file_source_current(const group_file_outgoing *outgoing)
{
	struct stat descriptor_status, path_status;
	uint8_t hash[32];

	if (!outgoing || outgoing->fd < 0 ||
	    fstat(outgoing->fd, &descriptor_status) != 0 ||
	    lstat(outgoing->path, &path_status) != 0 ||
	    !S_ISREG(path_status.st_mode) ||
	    descriptor_status.st_dev != path_status.st_dev ||
	    descriptor_status.st_ino != path_status.st_ino ||
	    descriptor_status.st_size < 0 ||
	    (uint64_t)descriptor_status.st_size != outgoing->size ||
	    group_file_hash_fd(outgoing->fd, outgoing->size, hash) != 0 ||
	    memcmp(hash, outgoing->hash, sizeof(hash)) != 0)
		return 0;
	return 1;
}

static void group_file_out_finish(int index, const char *state, const char *code)
{
	group_file_outgoing *outgoing;
	int stored = 0, source_current = 0;
	const char *terminal_code = code;

	if (index < 0 || index >= GROUP_FILE_OUT_MAX || !g_group_file_out[index].used)
		return;
	outgoing = &g_group_file_out[index];
	if (strcmp(state, "done") == 0) {
		source_current = group_file_source_current(outgoing);
		if (!source_current)
			terminal_code = "local_source_changed";
	}
	if (outgoing->fd >= 0) {
		close(outgoing->fd);
		outgoing->fd = -1;
	}
	if (strcmp(state, "done") == 0) {
		int attachment_ready = !outgoing->pending_attachment;
		if (outgoing->pending_attachment &&
		    attachment_pending_update(outgoing->path, 1) == 1) {
			outgoing->pending_attachment = 0;
			outgoing->managed_attachment = 1;
			attachment_ready = 1;
		}
		if (source_current && attachment_ready &&
		    omaq_message_append_attachment_id(home_dir(), outgoing->group, "me",
			outgoing->path, "out", outgoing->kind,
			outgoing->event_id) == 0) {
			stored = 1;
			outgoing->managed_attachment = 0;
			emit_group_attachment_message(outgoing->group, outgoing->event_id,
				outgoing->path, "out", outgoing->kind, NULL);
		}
		if (!stored && source_current)
			terminal_code = "local_history_failed";
		emit_group_file("done", outgoing->group, outgoing->event_id, NULL, 0,
			source_current ? outgoing->path : "", "out", outgoing->request,
			outgoing->kind, NULL, terminal_code);
		if (!stored) {
			if (outgoing->pending_attachment)
				(void)attachment_pending_update(outgoing->path, 2);
			else if (outgoing->managed_attachment)
				(void)attachment_managed_remove(outgoing->path);
			if (source_current)
				emit_error_conv("history_failed", outgoing->group);
		}
	} else {
		if (outgoing->pending_attachment)
			(void)attachment_pending_update(outgoing->path, 2);
		emit_group_file(state, outgoing->group, outgoing->event_id, NULL, 0,
			NULL, "out", outgoing->request, outgoing->kind, NULL, code);
	}
	group_file_out_drop(index);
}

static int group_file_recipient_count(const group_file_outgoing *outgoing)
{
	int count = 0;
	for (int i = 0; outgoing && i < GROUP_FILE_RECIPIENT_MAX; i++)
		if (outgoing->recipients[i].used && outgoing->recipients[i].accepted)
			count++;
	return count;
}

static int group_file_all_responded(const group_file_outgoing *outgoing)
{
	int expected = 0;
	for (int i = 0; outgoing && i < GROUP_FILE_RECIPIENT_MAX; i++) {
		const group_file_recipient *recipient = &outgoing->recipients[i];
		if (!recipient->used)
			continue;
		expected++;
		if (!recipient->accepted && !recipient->canceled && !recipient->failed)
			return 0;
	}
	return expected > 0;
}

static void group_file_accept_packet(int index, uint32_t peer)
{
	group_file_outgoing *outgoing;
	char key[65];
	int self = 0, slot = -1;
	int64_t now = monotonic_millis();

	if (index < 0 || index >= GROUP_FILE_OUT_MAX || !g_group_file_out[index].used)
		return;
	outgoing = &g_group_file_out[index];
	if (monotonic_millis() >= outgoing->accept_until ||
	    group_file_peer_identity(outgoing->group_number, peer, key, NULL, 0,
				     &self) != 0 || self)
		return;
	for (int i = 0; i < GROUP_FILE_RECIPIENT_MAX; i++)
		if (outgoing->recipients[i].used &&
		    strcmp(outgoing->recipients[i].key, key) == 0) {
			if (outgoing->recipients[i].accepted ||
			    outgoing->recipients[i].canceled ||
			    outgoing->recipients[i].failed)
				return;
			slot = i;
			break;
		}
	if (slot < 0) {
		(void)group_file_send_control(outgoing->group_number, peer,
					      OMAQ_GROUP_FILE_CANCEL, outgoing->id);
		return;
	}
	outgoing->recipients[slot].used = 1;
	outgoing->recipients[slot].accepted = 1;
	outgoing->recipients[slot].peer = peer;
	outgoing->recipients[slot].last_progress = now;
	snprintf(outgoing->recipients[slot].key,
		 sizeof(outgoing->recipients[slot].key), "%s", key);
	outgoing->idle_deadline = now + 30000;
}

static void group_file_ack_packet(int index, uint32_t peer)
{
	group_file_outgoing *outgoing;
	char key[65];

	if (index < 0 || index >= GROUP_FILE_OUT_MAX || !g_group_file_out[index].used)
		return;
	outgoing = &g_group_file_out[index];
	if (!group_file_group_binding_ok(outgoing->group_number, outgoing->group) ||
	    group_file_peer_identity(outgoing->group_number, peer, key, NULL, 0,
				     NULL) != 0)
		return;
	for (int i = 0; i < GROUP_FILE_RECIPIENT_MAX; i++)
		if (outgoing->recipients[i].used &&
		    outgoing->recipients[i].done_sent &&
		    strcmp(outgoing->recipients[i].key, key) == 0) {
			outgoing->recipients[i].done = 1;
			outgoing->recipients[i].last_progress = monotonic_millis();
			return;
		}
}

static void group_file_receive_offer(uint32_t group_number, uint32_t peer,
				     const uint8_t *data, size_t length)
{
	omaq_group_file_offer offer;
	group_file_incoming *incoming;
	char group[OMAQ_GROUP_ID_MAX], key[65], name[OMAQ_GROUP_MEMBER_NAME_MAX + 1];
	char event_id[3 + OMAQ_GROUP_FILE_ID_HEX + 1];
	int self = 0, slot = -1;

	if (omaq_group_file_offer_unpack(data, length, &offer) != 0 ||
	    omaq_group_id_format(group_number, group, sizeof(group)) != 0 ||
	    group_file_peer_identity(group_number, peer, key, name, sizeof(name),
				     &self) != 0 || self ||
	    group_file_event_id(offer.id, event_id, sizeof(event_id)) != 0 ||
	    omaq_store_message_id_used(home_dir(), group, event_id) != 0)
		return;
	if (group_file_in_find(group_number, offer.id) >= 0)
		return;
	for (int i = 0; i < GROUP_FILE_IN_MAX; i++) {
		if (g_group_file_in[i].used &&
		    strcmp(g_group_file_in[i].group, group) == 0) {
			(void)group_file_send_control(group_number, peer,
					      OMAQ_GROUP_FILE_CANCEL, offer.id);
			return;
		}
		if (!g_group_file_in[i].used && slot < 0)
			slot = i;
	}
	if (slot < 0 ||
	    omaq_group_file_id_reserve(state_dir(), offer.id) != 0) {
		(void)group_file_send_control(group_number, peer, OMAQ_GROUP_FILE_CANCEL,
					      offer.id);
		return;
	}
	incoming = &g_group_file_in[slot];
	memset(incoming, 0, sizeof(*incoming));
	incoming->used = 1;
	incoming->fd = -1;
	incoming->group_number = group_number;
	incoming->sender_peer = peer;
	incoming->size = offer.size;
	incoming->idle_deadline = monotonic_millis() + 120000;
	memcpy(incoming->id, offer.id, sizeof(incoming->id));
	memcpy(incoming->hash, offer.hash, sizeof(incoming->hash));
	snprintf(incoming->group, sizeof(incoming->group), "%s", group);
	snprintf(incoming->event_id, sizeof(incoming->event_id), "%s", event_id);
	snprintf(incoming->sender_key, sizeof(incoming->sender_key), "%s", key);
	snprintf(incoming->sender_name, sizeof(incoming->sender_name), "%s", name);
	snprintf(incoming->name, sizeof(incoming->name), "%s", offer.name);
	snprintf(incoming->kind, sizeof(incoming->kind), "%s", offer.kind);
	emit_group_file("offer", group, event_id, offer.name, offer.size, NULL, "in",
			NULL, offer.kind, key, NULL);
}

static int group_file_accept(const char *group, const char *event_id,
			     const char *destination)
{
	uint8_t id[OMAQ_GROUP_FILE_ID_BYTES];
	uint32_t group_number;
	int index, fd;
	char key[65];

	if (!group || !event_id || omaq_group_id_parse(group, &group_number) != 0 ||
	    omaq_group_file_id_parse(event_id, id) != 0 ||
	    (index = group_file_in_find(group_number, id)) < 0 ||
	    strcmp(g_group_file_in[index].group, group) != 0 ||
	    g_group_file_in[index].accepted ||
	    group_file_peer_identity(group_number, g_group_file_in[index].sender_peer,
				     key, NULL, 0, NULL) != 0 ||
	    strcmp(key, g_group_file_in[index].sender_key) != 0)
		return -1;
	fd = omaq_file_download_create(g_group_file_in[index].name, destination,
				       g_group_file_in[index].path,
				       sizeof(g_group_file_in[index].path));
	if (fd < 0) {
		group_file_in_drop(index, 0);
		return -1;
	}
	g_group_file_in[index].fd = fd;
	if (group_file_send_control(group_number, g_group_file_in[index].sender_peer,
				    OMAQ_GROUP_FILE_ACCEPT, id) != 0) {
		group_file_in_drop(index, 1);
		return -1;
	}
	g_group_file_in[index].accepted = 1;
	g_group_file_in[index].idle_deadline = monotonic_millis() + 30000;
	return 0;
}

static int group_file_cancel(const char *group, const char *event_id)
{
	uint8_t id[OMAQ_GROUP_FILE_ID_BYTES];
	uint32_t group_number;
	int index;

	if (!group || !event_id || omaq_group_id_parse(group, &group_number) != 0 ||
	    omaq_group_file_id_parse(event_id, id) != 0)
		return -1;
	index = group_file_in_find(group_number, id);
	if (index >= 0 && strcmp(g_group_file_in[index].group, group) == 0) {
		if (g_group_file_in[index].completed) {
			(void)group_file_send_control(group_number,
						      g_group_file_in[index].sender_peer,
						      OMAQ_GROUP_FILE_ACK, id);
			group_file_in_drop(index, 0);
			return 0;
		}
		(void)group_file_send_control(group_number,
					      g_group_file_in[index].sender_peer,
					      OMAQ_GROUP_FILE_CANCEL, id);
		emit_group_file("canceled", group, event_id, NULL, 0, NULL, "in",
				NULL, g_group_file_in[index].kind,
				g_group_file_in[index].sender_key, NULL);
		group_file_in_drop(index, 1);
		return 0;
	}
	index = group_file_out_find(group_number, id);
	if (index >= 0 && strcmp(g_group_file_out[index].group, group) == 0) {
		uint8_t packet[64];
		int packet_length = omaq_group_file_control_pack(packet, sizeof(packet),
							 OMAQ_GROUP_FILE_CANCEL, id);
		if (packet_length >= 0)
			(void)omaq_tox_group_custom_send(g_tox, group_number, packet,
							(size_t)packet_length);
		group_file_out_finish(index, "canceled", NULL);
		return 0;
	}
	return -1;
}

static int group_file_send_begin(const char *group, const char *path,
				 const char *kind, const char *request)
{
	omaq_group_file_offer offer;
	group_file_outgoing prepared;
	struct stat status;
	uint8_t packet[OMAQ_GROUP_FILE_PACKET_MAX];
	uint32_t group_number;
	int slot = -1, packet_length, unique_id = 0, expected_peers = 0;

	memset(&prepared, 0, sizeof(prepared));
	prepared.fd = -1;
	if (!group || !path || !omaq_file_path_ok(path) || !request ||
	    !omaq_message_id_ok(request) ||
	    (strcmp(kind, "file") != 0 && strcmp(kind, "image") != 0) ||
	    omaq_group_id_parse(group, &group_number) != 0 ||
	    omaq_file_basename(path, prepared.name, sizeof(prepared.name)) != 0 ||
	    (strcmp(kind, "image") == 0 &&
	     omaq_inline_image_validate_file(path) != 0))
		return -1;
	for (int i = 0; i < GROUP_FILE_OUT_MAX; i++) {
		if (g_group_file_out[i].used &&
		    strcmp(g_group_file_out[i].group, group) == 0)
			return -2;
		if (!g_group_file_out[i].used && slot < 0)
			slot = i;
	}
	if (slot < 0)
		return -2;
	prepared.fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
	if (prepared.fd < 0 || fstat(prepared.fd, &status) != 0 ||
	    !S_ISREG(status.st_mode) || status.st_size <= 0 ||
	    (uint64_t)status.st_size > OMAQ_FILE_MAX) {
		if (prepared.fd >= 0)
			close(prepared.fd);
		return -1;
	}
	prepared.size = (uint64_t)status.st_size;
	if (group_file_hash_fd(prepared.fd, prepared.size, prepared.hash) != 0) {
		close(prepared.fd);
		return -1;
	}
	for (int attempt = 0; attempt < 8 && !unique_id; attempt++) {
		size_t random_offset = 0;
		int used;
		while (random_offset < sizeof(prepared.id)) {
			ssize_t got = getrandom(prepared.id + random_offset,
						sizeof(prepared.id) - random_offset, 0);
			if (got < 0 && errno == EINTR)
				continue;
			if (got <= 0) {
				close(prepared.fd);
				return -1;
			}
			random_offset += (size_t)got;
		}
		if (group_file_event_id(prepared.id, prepared.event_id,
					sizeof(prepared.event_id)) != 0) {
			close(prepared.fd);
			return -1;
		}
		used = omaq_store_message_id_used(home_dir(), group, prepared.event_id);
		if (used < 0) {
			close(prepared.fd);
			return -1;
		}
		if (used == 0) {
			int reserved = omaq_group_file_id_reserve(state_dir(), prepared.id);
			if (reserved < 0) {
				close(prepared.fd);
				return -1;
			}
			unique_id = reserved == 0;
		}
	}
	if (!unique_id ||
	    snprintf(prepared.group, sizeof(prepared.group), "%s", group) >=
		(int)sizeof(prepared.group) ||
	    snprintf(prepared.request, sizeof(prepared.request), "%s", request) >=
		(int)sizeof(prepared.request) ||
	    snprintf(prepared.path, sizeof(prepared.path), "%s", path) >=
		(int)sizeof(prepared.path) ||
	    snprintf(prepared.kind, sizeof(prepared.kind), "%s", kind) >=
		(int)sizeof(prepared.kind)) {
		close(prepared.fd);
		return -1;
	}
	prepared.used = 1;
	prepared.group_number = group_number;
	prepared.accept_until = monotonic_millis() + 30000;
	prepared.idle_deadline = prepared.accept_until;
	for (int member = 0; member < omaq_group_peer_count(group_number) &&
	     expected_peers < GROUP_FILE_RECIPIENT_MAX; member++) {
		uint32_t peer = omaq_group_peer_at(group_number, member);
		char peer_key[65];
		int self = 0;
		if (group_file_peer_identity(group_number, peer, peer_key, NULL, 0,
					     &self) != 0 || self)
			continue;
		prepared.recipients[expected_peers].used = 1;
		prepared.recipients[expected_peers].peer = peer;
		prepared.recipients[expected_peers].last_progress = monotonic_millis();
		snprintf(prepared.recipients[expected_peers].key,
			 sizeof(prepared.recipients[expected_peers].key), "%s", peer_key);
		expected_peers++;
	}
	if (expected_peers == 0) {
		close(prepared.fd);
		return -1;
	}
	memset(&offer, 0, sizeof(offer));
	memcpy(offer.id, prepared.id, sizeof(offer.id));
	memcpy(offer.hash, prepared.hash, sizeof(offer.hash));
	offer.size = prepared.size;
	snprintf(offer.name, sizeof(offer.name), "%s", prepared.name);
	snprintf(offer.kind, sizeof(offer.kind), "%s", prepared.kind);
	packet_length = omaq_group_file_offer_pack(packet, sizeof(packet), &offer);
	if (packet_length < 0 ||
	    omaq_tox_group_custom_send(g_tox, group_number, packet,
					(size_t)packet_length) != 0) {
		close(prepared.fd);
		return -1;
	}
	{
		int pending = attachment_pending_update(path, 0);
		if (pending < 0) {
			uint8_t cancel[64];
			int cancel_length = omaq_group_file_control_pack(cancel, sizeof(cancel),
								 OMAQ_GROUP_FILE_CANCEL,
								 prepared.id);
			if (cancel_length >= 0)
				(void)omaq_tox_group_custom_send(g_tox, group_number, cancel,
								(size_t)cancel_length);
			close(prepared.fd);
			return -1;
		}
		prepared.pending_attachment = pending == 1;
		prepared.managed_attachment = 0;
	}
	g_group_file_out[slot] = prepared;
	emit_group_file("sending", group, prepared.event_id, NULL, 0, NULL, "out",
			request, kind, NULL, NULL);
	return 0;
}

static void group_file_receive_data(uint32_t group_number, uint32_t peer,
				    const uint8_t *packet, size_t length)
{
	uint8_t id[OMAQ_GROUP_FILE_ID_BYTES];
	uint64_t offset;
	const uint8_t *data;
	size_t data_length;
	char key[65];
	int index;

	if (omaq_group_file_data_unpack(packet, length, id, &offset, &data,
					&data_length) != 0 ||
	    (index = group_file_in_find(group_number, id)) < 0)
		return;
	if (peer != g_group_file_in[index].sender_peer ||
	    group_file_peer_identity(group_number, peer, key, NULL, 0, NULL) != 0 ||
	    strcmp(key, g_group_file_in[index].sender_key) != 0)
		return;
	if (g_group_file_in[index].completed)
		return;
	if (!g_group_file_in[index].accepted || g_group_file_in[index].fd < 0 ||
	    !group_file_group_binding_ok(group_number, g_group_file_in[index].group) ||
	    offset != g_group_file_in[index].got ||
	    data_length > g_group_file_in[index].size - g_group_file_in[index].got) {
		group_file_in_fail(index, "failed", "invalid_transfer", 1);
		return;
	}
	for (size_t written = 0; written < data_length;) {
		ssize_t count = write(g_group_file_in[index].fd, data + written,
				      data_length - written);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0) {
			group_file_in_fail(index, "failed", "write_failed", 1);
			return;
		}
		written += (size_t)count;
	}
	g_group_file_in[index].got += data_length;
	g_group_file_in[index].idle_deadline = monotonic_millis() + 30000;
}

static void group_file_receive_done(uint32_t group_number, uint32_t peer,
				    const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	group_file_incoming *incoming;
	uint8_t hash[32];
	char key[65];
	int index, stored;

	index = group_file_in_find(group_number, id);
	if (index < 0)
		return;
	incoming = &g_group_file_in[index];
	if (peer != incoming->sender_peer ||
	    group_file_peer_identity(group_number, peer, key, NULL, 0, NULL) != 0 ||
	    strcmp(key, incoming->sender_key) != 0)
		return;
	if (incoming->completed) {
		int ack = group_file_send_control(group_number, peer, OMAQ_GROUP_FILE_ACK,
					  incoming->id);
		if (ack == 0)
			group_file_in_drop(index, 0);
		else
			incoming->ack_after = monotonic_millis() + 250;
		return;
	}
	if (!incoming->accepted || incoming->fd < 0 ||
	    !group_file_group_binding_ok(group_number, incoming->group) ||
	    incoming->got != incoming->size ||
	    fsync(incoming->fd) != 0 ||
	    group_file_hash_fd(incoming->fd, incoming->size, hash) != 0 ||
	    memcmp(hash, incoming->hash, sizeof(hash)) != 0) {
		group_file_in_fail(index, "failed", "integrity_failed", 1);
		return;
	}
	close(incoming->fd);
	incoming->fd = -1;
	if (strcmp(incoming->kind, "image") == 0 &&
	    omaq_inline_image_canonicalize_file(incoming->path) != 0) {
		group_file_in_fail(index, "failed", "invalid_image", 1);
		return;
	}
	stored = omaq_message_append_attachment_id(home_dir(), incoming->group,
			incoming->sender_key, incoming->path, "in", incoming->kind,
			incoming->event_id) == 0;
	if (!stored) {
		group_file_in_fail(index, "failed", "local_history_failed", 1);
		return;
	}
	(void)note_unread(incoming->group);
	emit_group_attachment_message(incoming->group, incoming->event_id,
		incoming->path, "in", incoming->kind, incoming->sender_key);
	emit_group_file("done", incoming->group, incoming->event_id, NULL, 0,
			incoming->path, "in", NULL, incoming->kind,
			incoming->sender_key, NULL);
	incoming->completed = 1;
	incoming->idle_deadline = monotonic_millis() + 30000;
	incoming->ack_after = 0;
	if (group_file_send_control(group_number, peer, OMAQ_GROUP_FILE_ACK,
				    incoming->id) == 0)
		group_file_in_drop(index, 0);
	else
		incoming->ack_after = monotonic_millis() + 250;
}

static void group_file_receive_cancel(uint32_t group_number, uint32_t peer,
				      const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	char key[65];
	int incoming_index = group_file_in_find(group_number, id);
	int outgoing_index = group_file_out_find(group_number, id);

	if (group_file_peer_identity(group_number, peer, key, NULL, 0, NULL) != 0)
		return;
	if (incoming_index >= 0 &&
	    group_file_group_binding_ok(group_number, g_group_file_in[incoming_index].group) &&
	    peer == g_group_file_in[incoming_index].sender_peer &&
	    strcmp(key, g_group_file_in[incoming_index].sender_key) == 0) {
		if (g_group_file_in[incoming_index].completed) {
			(void)group_file_send_control(group_number, peer, OMAQ_GROUP_FILE_ACK, id);
			group_file_in_drop(incoming_index, 0);
		} else {
			group_file_in_fail(incoming_index, "canceled", NULL, 0);
		}
		return;
	}
	if (outgoing_index >= 0) {
		group_file_outgoing *outgoing = &g_group_file_out[outgoing_index];
		if (!group_file_group_binding_ok(group_number, outgoing->group))
			return;
		for (int i = 0; i < GROUP_FILE_RECIPIENT_MAX; i++)
			if (outgoing->recipients[i].used &&
			    strcmp(outgoing->recipients[i].key, key) == 0) {
				outgoing->recipients[i].canceled = 1;
				return;
			}
		/* A decline from a peer that never accepted is local to that peer. */
	}
}

static void group_file_receive_fail(uint32_t group_number, uint32_t peer,
				    const uint8_t id[OMAQ_GROUP_FILE_ID_BYTES])
{
	char key[65];
	int index = group_file_out_find(group_number, id);

	if (index < 0 ||
	    !group_file_group_binding_ok(group_number, g_group_file_out[index].group) ||
	    group_file_peer_identity(group_number, peer, key, NULL, 0, NULL) != 0)
		return;
	for (int i = 0; i < GROUP_FILE_RECIPIENT_MAX; i++)
		if (g_group_file_out[index].recipients[i].used &&
		    strcmp(g_group_file_out[index].recipients[i].key, key) == 0) {
			g_group_file_out[index].recipients[i].failed = 1;
			return;
		}
}

static const uint8_t group_typing_magic[] = { 'O', 'Q', 'G', 'T', '1' };

static int group_typing_receive(uint32_t group_number, uint32_t peer,
				const uint8_t *data, size_t length)
{
	char group[OMAQ_GROUP_ID_MAX], key[65], event[320];
	if (!data || length != sizeof(group_typing_magic) + 1u ||
	    memcmp(data, group_typing_magic, sizeof(group_typing_magic)) != 0 ||
	    (data[sizeof(group_typing_magic)] != 0 &&
	     data[sizeof(group_typing_magic)] != 1))
		return 0;
	if (known_group_id(group_number, group, sizeof(group)) != 0 ||
	    group_file_peer_identity(group_number, peer, key, NULL, 0, NULL) != 0 ||
	    omaq_control_rate_allow(&g_group_typing_rate, 't', group_number, key,
				    (int64_t)time(NULL)) != 0)
		return 1;
	snprintf(event, sizeof(event),
		 "{\"event\":\"typing\",\"conversation\":\"%s\",\"actor\":\"%s\",\"typing\":%s}",
		 group, key, data[sizeof(group_typing_magic)] ? "true" : "false");
	emit(event);
	return 1;
}

static void hook_group_file_packet(void *ud, uint32_t group_number,
				   uint32_t peer, const uint8_t *data,
				   size_t length, int private_packet)
{
	uint8_t type, id[OMAQ_GROUP_FILE_ID_BYTES];
	(void)ud;

	if (!data || length < 6 || length > OMAQ_GROUP_FILE_PACKET_MAX)
		return;
	if (!private_packet) {
		if (group_typing_receive(group_number, peer, data, length))
			return;
		if (data[5] == OMAQ_GROUP_FILE_OFFER)
			group_file_receive_offer(group_number, peer, data, length);
		else if (omaq_group_file_control_unpack(data, length, &type, id) == 0 &&
			 type == OMAQ_GROUP_FILE_CANCEL)
			group_file_receive_cancel(group_number, peer, id);
		return;
	}
	if (data[5] == OMAQ_GROUP_FILE_DATA) {
		group_file_receive_data(group_number, peer, data, length);
		return;
	}
	if (omaq_group_file_control_unpack(data, length, &type, id) != 0)
		return;
	if (type == OMAQ_GROUP_FILE_ACCEPT) {
		int index = group_file_out_find(group_number, id);
		if (index >= 0)
			group_file_accept_packet(index, peer);
	} else if (type == OMAQ_GROUP_FILE_CANCEL) {
		group_file_receive_cancel(group_number, peer, id);
	} else if (type == OMAQ_GROUP_FILE_DONE) {
		group_file_receive_done(group_number, peer, id);
	} else if (type == OMAQ_GROUP_FILE_ACK) {
		int index = group_file_out_find(group_number, id);
		if (index >= 0)
			group_file_ack_packet(index, peer);
	} else if (type == OMAQ_GROUP_FILE_FAIL) {
		group_file_receive_fail(group_number, peer, id);
	}
}

static int group_file_recipient_identity_ok(group_file_outgoing *outgoing,
					    group_file_recipient *recipient)
{
	char key[65];
	return outgoing && recipient && recipient->used &&
		group_file_group_binding_ok(outgoing->group_number, outgoing->group) &&
		group_file_peer_identity(outgoing->group_number, recipient->peer,
					 key, NULL, 0, NULL) == 0 &&
		strcmp(key, recipient->key) == 0;
}

static void group_file_pump(void)
{
	uint8_t packet[OMAQ_GROUP_FILE_PACKET_MAX], data[OMAQ_GROUP_FILE_DATA_MAX];
	int64_t now = monotonic_millis();

	for (int i = 0; i < GROUP_FILE_IN_MAX; i++) {
		group_file_incoming *incoming = &g_group_file_in[i];
		if (!incoming->used)
			continue;
		if (incoming->completed) {
			if (now >= incoming->idle_deadline) {
				group_file_in_drop(i, 0);
				continue;
			}
			if (now >= incoming->ack_after) {
				char key[65];
				if (!group_file_group_binding_ok(incoming->group_number,
							 incoming->group) ||
				    group_file_peer_identity(incoming->group_number,
					incoming->sender_peer, key, NULL, 0, NULL) != 0 ||
				    strcmp(key, incoming->sender_key) != 0) {
					group_file_in_drop(i, 0);
					continue;
				}
				int ack = group_file_send_control(incoming->group_number,
					incoming->sender_peer, OMAQ_GROUP_FILE_ACK, incoming->id);
				if (ack <= 0) {
					group_file_in_drop(i, 0);
					continue;
				}
				incoming->ack_after = now + 250;
			}
			continue;
		}
		if (now >= incoming->idle_deadline)
			group_file_in_fail(i, "failed", "timeout", incoming->accepted);
	}
	for (int i = 0; i < GROUP_FILE_OUT_MAX; i++) {
		group_file_outgoing *outgoing = &g_group_file_out[i];
		int active = 0, done = 0, canceled = 0, failed = 0, unknown = 0;
		int pending_response = 0;
		if (!outgoing->used)
			continue;
		if (!outgoing->started) {
			int recipients = group_file_recipient_count(outgoing);
			if (recipients == 0 && !group_file_all_responded(outgoing)) {
				if (now >= outgoing->accept_until)
					group_file_out_finish(i, "failed", "no_recipient");
				continue;
			}
			outgoing->started = 1;
		}
		for (int budget = 0; budget < 32; budget++) {
			group_file_recipient *recipient = NULL;
			for (int searched = 0; searched < GROUP_FILE_RECIPIENT_MAX; searched++) {
				unsigned int candidate = outgoing->recipient_cursor++ %
					GROUP_FILE_RECIPIENT_MAX;
				if (outgoing->recipients[candidate].used &&
				    outgoing->recipients[candidate].accepted &&
				    !outgoing->recipients[candidate].done &&
				    !outgoing->recipients[candidate].done_sent &&
				    !outgoing->recipients[candidate].canceled &&
				    !outgoing->recipients[candidate].failed &&
				    !outgoing->recipients[candidate].delivery_unknown) {
					recipient = &outgoing->recipients[candidate];
					break;
				}
			}
			if (!recipient)
				break;
			if (!group_file_recipient_identity_ok(outgoing, recipient) ||
			    now - recipient->last_progress > 30000) {
				recipient->failed = 1;
				continue;
			}
			if (recipient->offset < outgoing->size) {
				size_t wanted = (size_t)(outgoing->size - recipient->offset);
				ssize_t got;
				int packet_length, sent;
				if (wanted > sizeof(data))
					wanted = sizeof(data);
				got = pread(outgoing->fd, data, wanted, (off_t)recipient->offset);
				packet_length = got == (ssize_t)wanted
					? omaq_group_file_data_pack(packet, sizeof(packet), outgoing->id,
								  recipient->offset, data, wanted)
					: -1;
				if (packet_length < 0) {
					recipient->failed = 1;
					continue;
				}
				sent = omaq_tox_group_custom_private_send(g_tox,
					outgoing->group_number, recipient->peer, packet,
					(size_t)packet_length);
				if (sent == 0) {
					recipient->offset += wanted;
					recipient->last_progress = now;
				} else if (sent < 0) {
					recipient->failed = 1;
				} else {
					break;
				}
			} else {
				int sent = group_file_send_control(outgoing->group_number,
					recipient->peer, OMAQ_GROUP_FILE_DONE, outgoing->id);
				if (sent == 0) {
					recipient->done_sent = 1;
					recipient->last_progress = now;
				} else if (sent < 0) {
					recipient->failed = 1;
				} else {
					break;
				}
			}
		}
		for (int r = 0; r < GROUP_FILE_RECIPIENT_MAX; r++) {
			if (!outgoing->recipients[r].used)
				continue;
			if (!outgoing->recipients[r].accepted) {
				if (outgoing->recipients[r].failed)
					failed++;
				else if (outgoing->recipients[r].canceled)
					canceled++;
				else
					pending_response++;
				continue;
			}
			if (outgoing->recipients[r].done_sent &&
			    now - outgoing->recipients[r].last_progress > 30000)
				outgoing->recipients[r].delivery_unknown = 1;
			if (outgoing->recipients[r].done)
				done++;
			else if (outgoing->recipients[r].delivery_unknown)
				unknown++;
			else if (outgoing->recipients[r].failed)
				failed++;
			else if (outgoing->recipients[r].canceled)
				canceled++;
			else
				active++;
		}
		if (outgoing->used && active == 0 &&
		    (pending_response == 0 || now >= outgoing->accept_until) &&
		    done + canceled + failed + unknown > 0) {
			if (done > 0)
				group_file_out_finish(i, "done",
					unknown > 0 ? "partial_delivery_unknown" :
					(failed > 0 ? "partial_failed" : NULL));
			else if (unknown > 0)
				group_file_out_finish(i, "failed", "delivery_unknown");
			else if (failed > 0)
				group_file_out_finish(i, "failed", "group_file_failed");
			else
				group_file_out_finish(i, "canceled", NULL);
		}
	}
}

static void group_file_self_exit(int index)
{
	group_file_outgoing *outgoing;
	int done = 0, uncertain = 0, incomplete = 0;

	if (index < 0 || index >= GROUP_FILE_OUT_MAX || !g_group_file_out[index].used)
		return;
	outgoing = &g_group_file_out[index];
	for (int i = 0; i < GROUP_FILE_RECIPIENT_MAX; i++) {
		group_file_recipient *recipient = &outgoing->recipients[i];
		if (!recipient->used)
			continue;
		if (recipient->done)
			done++;
		else if (recipient->done_sent)
			uncertain++;
		else if (recipient->accepted && !recipient->canceled)
			incomplete++;
	}
	if (done > 0)
		group_file_out_finish(index, "done",
			uncertain > 0 ? "partial_delivery_unknown" :
			(incomplete > 0 ? "partial_failed" : NULL));
	else if (uncertain > 0)
		group_file_out_finish(index, "failed", "delivery_unknown");
	else
		group_file_out_finish(index, "failed", "peer_left");
}

static void group_file_peer_removed(uint32_t group_number, uint32_t peer, int self)
{
	for (int i = 0; i < GROUP_FILE_IN_MAX; i++)
		if (g_group_file_in[i].used &&
		    g_group_file_in[i].group_number == group_number &&
		    (self || g_group_file_in[i].sender_peer == peer)) {
			if (g_group_file_in[i].completed)
				group_file_in_drop(i, 0);
			else
				group_file_in_fail(i, "failed", "peer_left", 0);
		}
	for (int i = 0; i < GROUP_FILE_OUT_MAX; i++) {
		if (!g_group_file_out[i].used ||
		    g_group_file_out[i].group_number != group_number)
			continue;
		if (self) {
			group_file_self_exit(i);
			continue;
		}
		for (int r = 0; r < GROUP_FILE_RECIPIENT_MAX; r++)
			if (g_group_file_out[i].recipients[r].used &&
			    g_group_file_out[i].recipients[r].peer == peer) {
				if (g_group_file_out[i].recipients[r].done_sent &&
				    !g_group_file_out[i].recipients[r].done)
					g_group_file_out[i].recipients[r].delivery_unknown = 1;
				else
					g_group_file_out[i].recipients[r].failed = 1;
			}
	}
}

static int group_file_group_active(const char *group)
{
	for (int i = 0; group && i < GROUP_FILE_OUT_MAX; i++)
		if (g_group_file_out[i].used && strcmp(g_group_file_out[i].group, group) == 0)
			return 1;
	for (int i = 0; group && i < GROUP_FILE_IN_MAX; i++)
		if (g_group_file_in[i].used && strcmp(g_group_file_in[i].group, group) == 0)
			return 1;
	return 0;
}

static int group_file_member_active(const char *group, const char *member_key)
{
	for (int i = 0; group && member_key && i < GROUP_FILE_OUT_MAX; i++) {
		if (!g_group_file_out[i].used || strcmp(g_group_file_out[i].group, group) != 0)
			continue;
		for (int r = 0; r < GROUP_FILE_RECIPIENT_MAX; r++)
			if (g_group_file_out[i].recipients[r].used &&
			    !g_group_file_out[i].recipients[r].done &&
			    !g_group_file_out[i].recipients[r].canceled &&
			    strcmp(g_group_file_out[i].recipients[r].key, member_key) == 0)
				return 1;
	}
	for (int i = 0; group && member_key && i < GROUP_FILE_IN_MAX; i++)
		if (g_group_file_in[i].used && strcmp(g_group_file_in[i].group, group) == 0 &&
		    strcmp(g_group_file_in[i].sender_key, member_key) == 0)
			return 1;
	return 0;
}

static void group_file_reset(void)
{
	for (int i = 0; i < GROUP_FILE_IN_MAX; i++)
		if (g_group_file_in[i].used)
			group_file_in_drop(i, !g_group_file_in[i].completed);
	for (int i = 0; i < GROUP_FILE_OUT_MAX; i++)
		if (g_group_file_out[i].used) {
			if (g_group_file_out[i].pending_attachment)
				(void)attachment_pending_update(g_group_file_out[i].path, 2);
			group_file_out_drop(i);
		}
}

static void emit_file(const char *state, uint32_t friend, uint32_t fnum,
		      const char *name, uint64_t size, const char *path, const char *dir,
		      const char *request)
{
	char id[OMAQ_FILE_ID_MAX], conv[16], key[65];
	char ev[OMAQ_JSON_STR_MAX * 6 + 1120];
	char ename[OMAQ_FILE_NAME_MAX * 6 + 1], epath[OMAQ_JSON_STR_MAX * 6 + 1];
	char erequest[80 * 6 + 1];
	int wr;

	if (omaq_file_id_format(friend, fnum, id, sizeof(id)) != 0 ||
	    omaq_tox_friend_pk_hex(g_tox, friend, key) != 0 ||
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
			      "{\"event\":\"file.offer\",\"id\":\"%s\",\"conversation\":\"%s\",\"key\":\"%s\",\"name\":\"%s\",\"size\":%llu,\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, key, ename, (unsigned long long)size, dir, erequest);
	} else if (strcmp(state, "sending") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.sending\",\"id\":\"%s\",\"conversation\":\"%s\",\"key\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, key, dir, erequest);
	} else if (strcmp(state, "done") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.done\",\"id\":\"%s\",\"conversation\":\"%s\",\"key\":\"%s\",\"path\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, key, epath, dir, erequest);
	} else if (strcmp(state, "canceled") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.canceled\",\"id\":\"%s\",\"conversation\":\"%s\",\"key\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, key, dir, erequest);
	} else if (strcmp(state, "failed") == 0) {
		wr = snprintf(ev, sizeof(ev),
			      "{\"event\":\"file.failed\",\"id\":\"%s\",\"conversation\":\"%s\",\"key\":\"%s\",\"dir\":\"%s\",\"request\":\"%s\"}",
			      id, conv, key, dir, erequest);
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

static int file_request_begin(uint32_t friend, uint32_t fnum,
			      const char *request, const char *path,
			      const char *kind)
{
	int i, slot = -1;
	uint64_t oldest = UINT64_MAX;

	if (!request || !request[0] || !path || !path[0] || !kind ||
	    (strcmp(kind, "file") != 0 && strcmp(kind, "image") != 0))
		return -1;
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
	snprintf(g_file_requests[slot].kind, sizeof(g_file_requests[slot].kind),
		 "%s", kind);
	if (snprintf(g_file_requests[slot].path,
		     sizeof(g_file_requests[slot].path), "%s", path) >=
	    (int)sizeof(g_file_requests[slot].path)) {
		memset(&g_file_requests[slot], 0, sizeof(g_file_requests[slot]));
		return -1;
	}
	return 0;
}

static int file_request_path(uint32_t friend, uint32_t fnum, char *path, size_t pathn)
{
	for (int i = 0; i < FILE_REQUEST_CACHE; i++) {
		if (!g_file_requests[i].used || g_file_requests[i].friend != friend ||
		    g_file_requests[i].fnum != fnum || !g_file_requests[i].path[0])
			continue;
		return snprintf(path, pathn, "%s", g_file_requests[i].path) < (int)pathn
			? 0 : -1;
	}
	return -1;
}

static const char *file_request_kind(uint32_t friend, uint32_t fnum)
{
	for (int i = 0; i < FILE_REQUEST_CACHE; i++)
		if (g_file_requests[i].used && g_file_requests[i].friend == friend &&
		    g_file_requests[i].fnum == fnum && g_file_requests[i].kind[0])
			return g_file_requests[i].kind;
	return "file";
}

static int file_request_mark_pending(uint32_t friend, uint32_t fnum, int pending)
{
	for (int i = 0; i < FILE_REQUEST_CACHE; i++)
		if (g_file_requests[i].used && g_file_requests[i].friend == friend &&
		    g_file_requests[i].fnum == fnum &&
		    strcmp(g_file_requests[i].state, "sending") == 0) {
			g_file_requests[i].pending_attachment = pending ? 1 : 0;
			g_file_requests[i].managed_attachment = pending ? 1 : 0;
			return 0;
		}
	return -1;
}

static int file_request_finish_pending(uint32_t friend, uint32_t fnum, int keep)
{
	for (int i = 0; i < FILE_REQUEST_CACHE; i++) {
		if (!g_file_requests[i].used || g_file_requests[i].friend != friend ||
		    g_file_requests[i].fnum != fnum ||
		    !g_file_requests[i].pending_attachment)
			continue;
		if (attachment_pending_update(g_file_requests[i].path, keep ? 1 : 2) != 1)
			return -1;
		g_file_requests[i].pending_attachment = 0;
		if (!keep)
			g_file_requests[i].managed_attachment = 0;
		return 0;
	}
	return 0;
}

static int file_request_remove_managed(uint32_t friend, uint32_t fnum)
{
	for (int i = 0; i < FILE_REQUEST_CACHE; i++) {
		if (!g_file_requests[i].used || g_file_requests[i].friend != friend ||
		    g_file_requests[i].fnum != fnum ||
		    !g_file_requests[i].managed_attachment)
			continue;
		if (attachment_managed_remove(g_file_requests[i].path) != 0)
			return -1;
		g_file_requests[i].managed_attachment = 0;
		return 1;
	}
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

static int file_request_friend_busy(uint32_t friend)
{
	for (int i = 0; i < FILE_REQUEST_CACHE; i++)
		if (g_file_requests[i].used && g_file_requests[i].friend == friend &&
		    strcmp(g_file_requests[i].state, "sending") == 0)
			return 1;
	return 0;
}

static void file_request_forget_friend(uint32_t friend)
{
	for (int i = 0; i < FILE_REQUEST_CACHE; i++)
		if (g_file_requests[i].used && g_file_requests[i].friend == friend)
			memset(&g_file_requests[i], 0, sizeof(g_file_requests[i]));
}

static void cancel_file_after_error(uint32_t friend, uint32_t fnum)
{
	if (omaq_file_cancel(g_tox, friend, fnum) != 0)
		omaq_file_drop(friend, fnum);
}

static void hook_file_recv(void *ud, uint32_t friend, uint32_t fnum,
			   const char *name, uint64_t size)
{
	(void)ud;
	if (omaq_file_offer_store(friend, fnum, name, size) != 0) {
		cancel_file_after_error(friend, fnum);
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
		cancel_file_after_error(friend, fnum);
		(void)file_request_finish_pending(friend, fnum, 0);
		request = file_request_finish(friend, fnum, "failed");
		if (event == OMAQ_FILE_EVENT_FAILED)
			emit_file("failed", friend, fnum, NULL, 0, NULL, "out", request);
		return;
	}
	event = omaq_file_event_for(avatar, OMAQ_FILE_OUTCOME_DONE);
	if (len == 0 && event == OMAQ_FILE_EVENT_DONE) {
		char conv[16], state_conv[OMAQ_DIRECT_STATE_ID_MAX], path[512], mid[64];
		const char *kind;
		int stored = 0;

		path[0] = '\0';
		(void)file_request_path(friend, fnum, path, sizeof(path));
		snprintf(conv, sizeof(conv), "%u", friend);
		kind = file_request_kind(friend, fnum);
		if (file_request_finish_pending(friend, fnum, 1) == 0 && path[0] &&
		    direct_state_for_friend(friend, state_conv,
						     sizeof(state_conv)) == 0 &&
		    omaq_message_append_attachment_with_id(home_dir(), state_conv, "me",
						   path, "out", kind, mid,
						   sizeof(mid)) == 0) {
			stored = 1;
			(void)file_request_mark_pending(friend, fnum, 0);
			emit_message_event_kind(conv, mid, "", path, "out", kind, NULL);
		} else if (file_request_remove_managed(friend, fnum) == 1) {
			path[0] = '\0';
		}
		request = file_request_finish(friend, fnum, "done");
		emit_file("done", friend, fnum, NULL, 0, path, "out", request);
		if (!stored)
			emit_error_conv("history_failed", conv);
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
		cancel_file_after_error(friend, fnum);
		if (event == OMAQ_FILE_EVENT_FAILED)
			emit_file("failed", friend, fnum, NULL, 0, NULL, "in", NULL);
		return;
	}
	if (rc == 1) {
		char conv[16], state_conv[OMAQ_DIRECT_STATE_ID_MAX], mid[64];
		int stored;
		event = omaq_file_event_for(avatar, OMAQ_FILE_OUTCOME_DONE);
		snprintf(conv, sizeof(conv), "%u", friend);
		if (event == OMAQ_FILE_EVENT_AVATAR) {
			char avatar_id[OMAQ_DIRECT_STATE_ID_MAX], installed[512];
			if (direct_state_for_friend(friend, avatar_id, sizeof(avatar_id)) != 0 ||
			    omaq_avatar_commit_received(home_dir(), avatar_id, dest, installed,
						       sizeof(installed)) != 0) {
				unlink(dest);
				emit_avatar(conv, "");
				return;
			}
			emit_avatar(conv, installed);
			emit_friends();
			return;
		}
		{
			const char *kind = omaq_inline_image_canonicalize_file(dest) == 0
				? "image" : "file";
			stored = direct_state_for_friend(friend, state_conv, sizeof(state_conv)) == 0 &&
				omaq_message_append_attachment_with_id(home_dir(), state_conv,
					"peer", dest, "in", kind, mid, sizeof(mid)) == 0;
			if (stored) {
				(void)note_unread(conv);
				emit_message_event_kind(conv, mid, "", dest, "in", kind, NULL);
			}
		}
		emit_file("done", friend, fnum, NULL, 0, dest, "in", NULL);
		if (!stored)
			emit_error_conv("history_failed", conv);
	}
}

static void hook_avatar(void *ud, uint32_t friend, uint32_t fnum, uint64_t size)
{
	char dest[512], id[16], state_id[OMAQ_DIRECT_STATE_ID_MAX], dir[512], got[512];

	(void)ud;
	snprintf(id, sizeof(id), "%u", friend);
	if (direct_state_for_friend(friend, state_id, sizeof(state_id)) != 0) {
		(void)omaq_tox_file_control(g_tox, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		return;
	}
	if (size == 0) {
		if (omaq_avatar_dest(home_dir(), state_id, dest, sizeof(dest)) == 0)
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
	if (omaq_avatar_dest(home_dir(), state_id, dest, sizeof(dest)) != 0) {
		(void)omaq_tox_file_control(g_tox, friend, fnum, OMAQ_TOX_FILE_CANCEL);
		return;
	}
	if (omaq_file_recv_begin(home_dir(), state_id, friend, fnum, "avatar.png", size,
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
	cancel_file_after_error(friend, fnum);
	if (sending)
		(void)file_request_finish_pending(friend, fnum, 0);
	request = sending ? file_request_finish(friend, fnum,
			event == OMAQ_FILE_EVENT_CANCELED ? "canceled" : "failed") : NULL;
	if (event == OMAQ_FILE_EVENT_CANCELED)
		emit_file("canceled", friend, fnum, NULL, 0, NULL,
			  sending ? "out" : "in", request);
	else if (event == OMAQ_FILE_EVENT_FAILED)
		emit_file("failed", friend, fnum, NULL, 0, NULL,
			  sending ? "out" : "in", request);
}

static void emit_call_state(uint32_t friend, const char *state)
{
	char key[65], event[260];

	if (omaq_tox_friend_pk_hex(g_tox, friend, key) != 0)
		return;
	snprintf(event, sizeof(event),
		 "{\"event\":\"call.state\",\"conversation\":\"%u\",\"key\":\"%s\",\"state\":\"%s\"}",
		 friend, key, state);
	emit(event);
}

static void hook_call(void *ud, uint32_t friend, int state)
{
	char key[65], event[260];

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
		if (omaq_tox_friend_pk_hex(g_tox, friend, key) != 0)
			return;
		snprintf(event, sizeof(event),
			 "{\"event\":\"call.incoming\",\"conversation\":\"%u\",\"key\":\"%s\"}",
			 friend, key);
		emit(event);
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
	omaq_tox_set_group_packet_hook(g_tox, hook_group_file_packet, NULL);
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

static int identity_guard_import_allowed(void)
{
	char expected[65];
	int expected_ok = omaq_identity_guard_expected(state_dir(), expected) == 0;

	if (g_identity_primary_uncertain)
		return g_identity_guard_error == OMAQ_IDENTITY_GUARD_INVALID && expected_ok;
	return g_identity_guard_error == OMAQ_IDENTITY_GUARD_MISSING ||
		g_identity_guard_error == OMAQ_IDENTITY_GUARD_MISMATCH ||
		(g_identity_guard_error == OMAQ_IDENTITY_GUARD_INVALID && expected_ok);
}

static int identity_uncertainty_allowed_op(const omaq_op *op)
{
	return op &&
		(strcmp(op->op, "identity.primary.acknowledge") == 0 ||
		 strcmp(op->op, "identity.ready") == 0 ||
		 strcmp(op->op, "identity.unlock") == 0 ||
		 strcmp(op->op, "identity.inspect") == 0 ||
		 strcmp(op->op, "history") == 0 || strcmp(op->op, "search") == 0 ||
		 strcmp(op->op, "safety.get") == 0 ||
		 (strcmp(op->op, "identity.import") == 0 &&
		  identity_guard_import_allowed()));
}

static const char *identity_guard_error_code(void)
{
	if (g_identity_guard_error == OMAQ_IDENTITY_GUARD_MISSING)
		return "identity_missing";
	if (g_identity_guard_error == OMAQ_IDENTITY_GUARD_MISMATCH)
		return "identity_mismatch";
	return "identity_guard_invalid";
}

static int activate_identity_guard(struct omaq_tox *tox)
{
	char fingerprint[65];
	int enable_rc, rc;

	if (!tox || omaq_tox_self_pk_hex(tox, fingerprint) != 0)
		return -1;
	rc = omaq_identity_guard_verify_or_create(state_dir(), fingerprint);
	if (rc != 0)
		return rc == OMAQ_IDENTITY_GUARD_PUBLISHED
			? OMAQ_IDENTITY_GUARD_INVALID : rc;
	enable_rc = omaq_tox_enable_recovery(tox, state_dir(),
					      g_identity_primary_uncertain);
	if (enable_rc < 0)
		return enable_rc == -2 ? OMAQ_IDENTITY_GUARD_PUBLISHED :
			OMAQ_IDENTITY_GUARD_INVALID;
	if (g_identity_guard_state == OMAQ_IDENTITY_GUARD_RESTORED &&
	    omaq_identity_guard_finish_recovery(state_dir()) != 0)
		return OMAQ_IDENTITY_GUARD_INVALID;
	g_identity_guard_state = OMAQ_IDENTITY_GUARD_EXISTING;
	return 0;
}

static int load_tox(const char *pass)
{
	int err = 0, guard_rc;

	g_tox = omaq_identity_load(home_dir(), pass, &err);
	if (err == OMAQ_TOX_LOCKED) {
		g_locked = 1;
		return 1;
	}
	g_locked = 0;
	if (!g_tox) {
		if (g_identity_guard_state == OMAQ_IDENTITY_GUARD_RESTORED)
			(void)omaq_identity_guard_reject_recovery(home_dir(), state_dir());
		g_identity_guard_error = OMAQ_IDENTITY_GUARD_INVALID;
		return -1;
	}
	if (!g_identity_guard_replacement_load) {
		guard_rc = activate_identity_guard(g_tox);
		if (guard_rc != 0) {
			if (guard_rc == OMAQ_IDENTITY_GUARD_PUBLISHED) {
				g_identity_primary_uncertain = 1;
				g_shutdown_after_drain = 1;
				emit_error("identity_primary_uncertain");
			} else {
				if (g_identity_guard_state == OMAQ_IDENTITY_GUARD_RESTORED)
					(void)omaq_identity_guard_reject_recovery(home_dir(), state_dir());
				g_identity_guard_error = guard_rc;
			}
			omaq_tox_discard(g_tox);
			g_tox = NULL;
			return -1;
		}
		g_identity_guard_error = 0;
	}
	g_connection_online = -1;
	attach_hooks();
	if (!g_identity_primary_uncertain && !g_guarded_restore_loading &&
	    (recover_group_registry_transaction() != 0 ||
	     rebuild_group_cache() != 0 || group_bind_pending_load() != 0 ||
	     recover_pending_group_accept() != 0)) {
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

#ifdef HAVE_TOX
static void reconcile_loaded_identity_state(void)
{
	if (!g_tox || g_direct_state_migration_failed || g_identity_primary_uncertain)
		return;
	if (!g_receipt_outbox_invalid)
		(void)recover_receipt_transaction();
	if (prune_unavailable_unread() < 0)
		snprintf(g_unread_error_code, sizeof(g_unread_error_code),
			 "unread_persist_failed");
	if (prune_unavailable_receipts() < 0)
		g_receipt_outbox_invalid = 1;
}

static int restore_guarded_identity(const char *stage_save, const char *pass,
				    const char *candidate_fingerprint)
{
	char expected[65], primary[600], backup[700], token[96];
	struct stat st;
	int had_ack, had_primary = 0, had_stale, had_uncertainty;
	int marker = 0, original_error = g_identity_guard_error;
	int load_rc, rollback_ok = 1;

	if (!stage_save || !candidate_fingerprint ||
	    omaq_identity_guard_expected(state_dir(), expected) != 0 ||
	    strcmp(expected, candidate_fingerprint) != 0)
		return -3;
	if (snprintf(primary, sizeof(primary), "%s/tox.save", home_dir()) >=
	    (int)sizeof(primary))
		return -1;
	had_uncertainty = omaq_identity_primary_uncertain_present(state_dir()) != 0;
	had_stale = omaq_identity_recovery_stale_present(state_dir()) != 0;
	had_ack = omaq_identity_primary_ack_present(state_dir()) != 0;
	if (lstat(primary, &st) == 0) {
		if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
		    (st.st_mode & 0777) != 0600)
			return -1;
		had_primary = 1;
	} else if (errno != ENOENT) {
		return -1;
	}
	g_identity_backup_sequence++;
	if (snprintf(token, sizeof(token), "%s-%llu", g_instance_id,
		     (unsigned long long)g_identity_backup_sequence) >= (int)sizeof(token) ||
	    snprintf(backup, sizeof(backup), "%s/tox.save.guard-backup.%s",
		     home_dir(), token) >= (int)sizeof(backup))
		return -1;
	if (had_primary &&
	    (omaq_identity_export_exclusive(home_dir(), backup) != 0 ||
	     fsync_directory(home_dir()) != 0))
		return -1;
	if (write_identity_guard_restore_marker(token, had_primary, had_uncertainty,
					       had_stale, had_ack) != 0) {
		if (had_primary) {
			(void)unlink(backup);
			(void)fsync_directory(home_dir());
		}
		return -1;
	}
	marker = 1;
	if (omaq_identity_guard_prepare_repair(state_dir()) != 0 ||
	    omaq_identity_import(home_dir(), stage_save, 1) != 0 ||
	    fsync_directory(home_dir()) != 0)
		goto rollback;
	reset_identity_runtime_state();
	if (omaq_receipt_outbox_load(&g_receipt_outbox, state_dir()) != 0)
		g_receipt_outbox_invalid = 1;
	g_identity_guard_error = 0;
	g_identity_guard_state = OMAQ_IDENTITY_GUARD_EXISTING;
	g_identity_guard_replacement_load = 1;
	g_guarded_restore_loading = 1;
	load_rc = load_tox(pass);
	g_guarded_restore_loading = 0;
	g_identity_guard_replacement_load = 0;
	if (load_rc != 0)
		goto rollback;
	if (init_instance_id() != 0)
		goto rollback;
	load_rc = omaq_tox_enable_recovery(g_tox, state_dir(), 0);
	if (load_rc == -2) {
		g_identity_recovery_required = 1;
		g_shutdown_after_drain = 1;
		return -2;
	}
	if (load_rc < 0 || remove_identity_guard_restore_marker() != 0)
		goto rollback;
	marker = 0;
	g_identity_primary_uncertain = 0;
	emit_identity_primary_state(NULL);
	if (recover_group_registry_transaction() != 0 || rebuild_group_cache() != 0 ||
	    group_bind_pending_load() != 0 || recover_pending_group_accept() != 0 ||
	    ((g_direct_state_migration_failed = migrate_direct_state() != 0))) {
		fail_direct_state_backend();
		return -5;
	}
#ifdef HAVE_SIGNAL
	g_ratchet = omaq_ratchet_open(home_dir());
	if (!g_ratchet) {
		fail_direct_state_backend();
		return -5;
	}
#endif
	reconcile_loaded_identity_state();
	if (had_primary && (unlink(backup) != 0 || fsync_directory(home_dir()) != 0))
		g_identity_backup_cleanup_failed = 1;
	g_identity_requires_ready = 1;
	g_stdin_identity_ready = 0;
	for (size_t i = 0; i < g_ncli; i++)
		g_client_identity_ready[i] = 0;
	return 0;

rollback:
#ifdef HAVE_SIGNAL
	if (g_ratchet) {
		omaq_ratchet_close(g_ratchet);
		g_ratchet = NULL;
	}
#endif
	if (g_tox) {
		omaq_tox_discard(g_tox);
		g_tox = NULL;
	}
	if (marker && rollback_identity_guard_restore(token, had_primary,
						      had_uncertainty, had_stale, had_ack) != 0)
		rollback_ok = 0;
	g_identity_guard_state = omaq_identity_guard_preflight(home_dir(), state_dir());
	g_identity_guard_error = original_error;
	return rollback_ok ? -1 : -2;
}
#endif

#ifdef HAVE_TOX
static int operation_uses_direct_conversation(const char *name)
{
	static const char *operations[] = {
		"msg.send", "history", "search", "history.clear",
		"message.edit", "message.delete", "message.react",
		"conversation.read", "unread.clear", "receipt.send", "typing.set",
		"surface.set", "surface.get", "file.send", "file.status",
		"file.accept", "file.cancel", "call.start", "call.answer", "call.stop"
	};

	if (!name)
		return 0;
	for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++)
		if (strcmp(name, operations[i]) == 0)
			return 1;
	return 0;
}

static int operation_allows_group_conversation(const char *name)
{
	static const char *operations[] = {
		"msg.send", "history", "search", "history.clear", "message.edit",
		"message.delete", "message.react", "conversation.read", "unread.clear",
		"receipt.send", "typing.set", "surface.set", "surface.get", "file.send",
		"file.status", "file.accept", "file.cancel"
	};

	if (!name)
		return 0;
	for (size_t i = 0; i < sizeof(operations) / sizeof(operations[0]); i++)
		if (strcmp(name, operations[i]) == 0)
			return 1;
	return 0;
}

static void reject_direct_operation_binding(const omaq_op *op)
{
	const char *conversation = op && op->conversation[0] ? op->conversation : "0";

	if (!op)
		return;
	if (strcmp(op->op, "msg.send") == 0)
		emit_message_failed(conversation, op->id, "identity_changed", 0);
	else if (strcmp(op->op, "file.send") == 0)
		emit_file_rejected(conversation, op->id, "identity_changed");
	else if (strcmp(op->op, "history") == 0)
		emit_history_failed(conversation, op->id);
	else if (strcmp(op->op, "conversation.read") == 0 ||
		 strcmp(op->op, "unread.clear") == 0)
		emit_conversation_read("conversation.read.failed", conversation,
				       "identity_changed");
	else if (strcmp(op->op, "message.react") == 0)
		emit_message_reaction_failed(conversation, op->id, "identity_changed");
	else
		emit_error_conv("identity_changed", conversation);
}
#endif

static int helper_shutdown_group_state(int *groups)
{
	size_t native_groups = 0;
	int cached_groups = omaq_group_count();
	int uncertain = 0;
	int reported;

#ifdef HAVE_TOX
	if (!g_tox) {
		uncertain = 1;
	} else {
		g_group_registry_unmapped = 0;
		if (rebuild_group_cache() != 0 ||
		    omaq_tox_group_count(g_tox, &native_groups) != 0 ||
		    g_group_registry_unmapped)
			uncertain = 1;
		for (int i = 0; i < GROUP_CLEANUP_MAX; i++)
			if (g_group_cleanup[i].used) {
				uncertain = 1;
				break;
			}
	}
#endif
#ifdef OMAQ_IPC_TEST
	if ((size_t)g_test_native_group_count > native_groups)
		native_groups = (size_t)g_test_native_group_count;
	if (g_test_group_state_uncertain)
		uncertain = 1;
#endif
	reported = cached_groups;
	if (native_groups > (size_t)reported)
		reported = native_groups > 1024 ? 1024 : (int)native_groups;
	*groups = reported;
	if (reported > 0)
		return 1;
#ifdef HAVE_TOX
	if (!uncertain && omaq_tox_save(g_tox) != 0)
		uncertain = 1;
#endif
	return uncertain ? 2 : 0;
}

static void begin_helper_shutdown(int owner_fd, const char *escaped_request)
{
	char event[768];
	int queued = 0;
	int64_t now = monotonic_millis();

	g_shutdown_after_drain = 1;
	g_shutdown_ack_fd = owner_fd;
	g_shutdown_ack_delivered = 0;
	g_shutdown_ack_failed = 0;
	g_shutdown_ack_deadline_ms = now < 0 ? 0 : now + 5000;
#ifdef OMAQ_IPC_TEST
	emit("{\"event\":\"test.noise\"}");
#endif
	snprintf(event, sizeof(event),
		 "{\"event\":\"helper.shutdown\",\"instance\":\"%s\",\"request\":\"%s\"}",
		 g_instance_id, escaped_request);
	for (size_t i = 0; i < g_ncli; i++)
		if (g_clients[i] == owner_fd) {
			queued = queue_client(i, event) == 0;
			break;
		}
	if (!queued)
		g_shutdown_ack_failed = 1;
#ifdef OMAQ_IPC_TEST
	{
		const char *mode = getenv("OMAQ_IPC_TEST_SAFE_SHUTDOWN_MODE");
		if (mode && (strcmp(mode, "ack_fail") == 0 ||
			     strcmp(mode, "ack_fail_signal") == 0))
			g_shutdown_ack_failed = 1;
	}
#endif
}

static int handle_op(const omaq_op *op, int *identity_ready, int owner_fd)
{
	if (shutdown_requested())
		return 0;
	if (strcmp(op->op, "helper.probe") == 0) {
		char escaped_request[80 * 6 + 1], event[768];
		if (op->field_mask != (OMAQ_JSON_FIELD_OP | OMAQ_JSON_FIELD_ID |
				       OMAQ_JSON_FIELD_REQUEST) ||
		    !op->id[0] || strcmp(op->id, g_instance_id) != 0 ||
		    !omaq_message_id_ok(op->request) ||
		    omaq_json_escape(op->request, escaped_request,
				     sizeof(escaped_request)) != 0) {
			emit_error("forbidden");
			return 0;
		}
#ifdef OMAQ_IPC_TEST
		emit("{\"event\":\"test.noise\"}");
#endif
		snprintf(event, sizeof(event),
			 "{\"event\":\"helper.probe\",\"instance\":\"%s\",\"request\":\"%s\"}",
			 g_instance_id, escaped_request);
		emit(event);
		return 0;
	}
	if (strcmp(op->op, "helper.shutdown_if_no_groups") == 0) {
		char escaped_request[80 * 6 + 1];
		int groups, group_state;

		if (op->field_mask != (OMAQ_JSON_FIELD_OP | OMAQ_JSON_FIELD_ID |
				       OMAQ_JSON_FIELD_REQUEST) ||
		    !op->id[0] || strcmp(op->id, g_instance_id) != 0 ||
		    !omaq_message_id_ok(op->request) ||
		    omaq_json_escape(op->request, escaped_request,
				     sizeof(escaped_request)) != 0) {
			emit_error("forbidden");
			return 0;
		}
#ifdef OMAQ_IPC_TEST
		{
			const char *mode = getenv("OMAQ_IPC_TEST_SAFE_SHUTDOWN_MODE");
			if (mode && strcmp(mode, "unsupported") == 0) {
				emit_error("unsupported");
				return 0;
			}
			if (mode && strcmp(mode, "silent") == 0)
				return 0;
			if (mode && strcmp(mode, "malformed") == 0) {
				char event[768];
				snprintf(event, sizeof(event),
					 "{\"event\":\"helper.shutdown_blocked\",\"instance\":\"%s\",\"request\":\"%s\",\"reason\":\"active_groups\",\"groups\":0}",
					 g_instance_id, escaped_request);
				emit(event);
				return 0;
			}
		}
#endif
		group_state = helper_shutdown_group_state(&groups);
		if (group_state != 0) {
			char event[768];
			const char *reason = group_state == 1 ? "active_groups" :
				"group_state_uncertain";
			snprintf(event, sizeof(event),
				 "{\"event\":\"helper.shutdown_blocked\",\"instance\":\"%s\",\"request\":\"%s\",\"reason\":\"%s\",\"groups\":%d}",
				 g_instance_id, escaped_request, reason, groups);
			emit(event);
			return 0;
		}
		begin_helper_shutdown(owner_fd, escaped_request);
		return 0;
	}
	if (strcmp(op->op, "helper.shutdown") == 0) {
		char escaped_request[80 * 6 + 1];
		if (op->field_mask != (OMAQ_JSON_FIELD_OP | OMAQ_JSON_FIELD_ID |
				       OMAQ_JSON_FIELD_REQUEST) ||
		    !op->id[0] || strcmp(op->id, g_instance_id) != 0 ||
		    !omaq_message_id_ok(op->request) ||
		    omaq_json_escape(op->request, escaped_request,
				     sizeof(escaped_request)) != 0) {
			emit_error("forbidden");
			return 0;
		}
		begin_helper_shutdown(owner_fd, escaped_request);
		return 0;
	}
#ifdef HAVE_TOX
	if (g_identity_recovery_required) {
		if (targeted_group_invite_op(op))
			emit_group_invite_terminal(op, "identity_changed");
		else if (direct_invite_action_op(op))
			emit_identity_error("identity_changed", op->request);
		else if (direct_invite_redeem_op(op) || direct_reinvite_clear_op(op))
			emit_identity_error("identity_changed", op->id);
		return 0;
	}
#endif
#ifdef OMAQ_IPC_TEST
#if defined(HAVE_TOX) && defined(OMAQ_TOX_TEST)
	if (strcmp(op->op, "test.tox.fail_save") == 0) {
		if (op->field_mask != OMAQ_JSON_FIELD_OP || !g_tox) {
			emit_error("forbidden");
			return 0;
		}
		omaq_tox_test_fail_before_primary(g_tox);
		emit("{\"event\":\"test.tox.save_failure_armed\"}");
		return 0;
	}
#endif
	if (strcmp(op->op, "test.group.activate") == 0 ||
	    strcmp(op->op, "test.group.native_unmapped") == 0 ||
	    strcmp(op->op, "test.group.uncertain") == 0 ||
	    strcmp(op->op, "test.group.cache.reset") == 0) {
		if (op->field_mask != OMAQ_JSON_FIELD_OP) {
			emit_error("forbidden");
			return 0;
		}
		if (strcmp(op->op, "test.group.activate") == 0)
			omaq_group_note_peer(0, 0);
		else if (strcmp(op->op, "test.group.native_unmapped") == 0)
			g_test_native_group_count = 1;
		else if (strcmp(op->op, "test.group.uncertain") == 0)
			g_test_group_state_uncertain = 1;
		else {
			omaq_group_reset();
#ifdef HAVE_TOX
			g_group_registry_unmapped = 1;
#endif
		}
		emit("{\"event\":\"test.group.active\",\"groups\":1}");
		return 0;
	}
	if (strcmp(op->op, "test.attachment.adopt") == 0) {
		char event[256];
		if (attachment_stage_path_owner(op->path, owner_fd) != 1 ||
		    attachment_stage_owner_adopt(op->path, owner_fd) != 0) {
			emit_error("forbidden");
			return 0;
		}
		snprintf(event, sizeof(event),
			 "{\"event\":\"test.attachment.adopted\",\"id\":\"%s\"}", op->id);
		emit(event);
		return 0;
	}
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
		char addr[77], nickname[129], escaped_nickname[260], call_field[260];
		char ev[1200];
		uint32_t call_friend = UINT32_MAX;
		const char *call_state = NULL;

		if (omaq_av_status(&call_friend, &call_state)) {
			char call_key[65];
			if (!g_tox || omaq_tox_friend_pk_hex(g_tox, call_friend, call_key) != 0)
				snprintf(call_field, sizeof(call_field), ",\"call\":null");
			else
				snprintf(call_field, sizeof(call_field),
					 ",\"call\":{\"conversation\":\"%u\",\"key\":\"%s\",\"state\":\"%s\"}",
					 call_friend, call_key, call_state);
		} else {
			snprintf(call_field, sizeof(call_field), ",\"call\":null");
		}
		if (g_locked && !g_tox) {
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"snapshot\",\"protocol\":%d,\"unread\":%u,\"locked\":true,\"instance\":\"%s\",\"call\":null%s}",
				 OMAQ_PROTOCOL_VERSION, omaq_unread_total(&g_unread), g_instance_id,
				 request_field);
			emit(ev);
			emit_invite_state("", 0, "status", NULL);
			emit_identity_primary_state(NULL);
			emit_all_unread();
			if (g_identity_primary_uncertain)
				emit_error("identity_primary_uncertain");
			replay_sound_results();
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
			emit_groups(op->id);
			emit_self_avatar();
			emit_identity_recovery_state(1);
			emit_identity_primary_state(NULL);
			if (g_issued_url[0] && g_issued_exp > (int64_t)time(NULL))
				emit_invite_state(g_issued_url, g_issued_exp, "status", NULL);
			else {
				clear_invite();
				emit_invite_state("", 0, "status", NULL);
			}
			emit_all_unread();
			emit_direct_reinvite_state(g_direct_state_reinvite_required, NULL);
			if (g_identity_primary_uncertain)
				emit_error("identity_primary_uncertain");
			else if (g_direct_state_migration_failed)
				emit_error("direct_state_migration_failed");
			else if (g_direct_state_reinvite_required)
				emit_error("direct_state_reinvite_required");
#ifdef HAVE_SIGNAL
			replay_group_invite_results();
#endif
			replay_sound_results();
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
#ifdef HAVE_TOX
			if (g_identity_guard_error)
				emit_error(identity_guard_error_code());
			else if (g_direct_state_migration_failed)
				emit_error("direct_state_migration_failed");
#endif
			replay_sound_results();
		}
		return 0;
	}
#ifdef HAVE_TOX
	if (g_identity_primary_uncertain && !identity_uncertainty_allowed_op(op)) {
		if (strcmp(op->op, "msg.send") == 0)
			emit_message_failed(op->conversation, op->id,
					    "identity_primary_uncertain", 0);
		else if (strncmp(op->op, "identity.", 9) == 0 ||
			 direct_invite_redeem_op(op) || direct_reinvite_clear_op(op))
			emit_identity_error("identity_primary_uncertain", op->id);
		else if (targeted_group_invite_op(op))
			emit_group_invite_terminal(op, "identity_primary_uncertain");
		else
			emit_error("identity_primary_uncertain");
		return 0;
	}
	if (g_identity_guard_error && strcmp(op->op, "identity.inspect") != 0 &&
	    !(strcmp(op->op, "identity.import") == 0 &&
	      identity_guard_import_allowed())) {
		if (strncmp(op->op, "identity.", 9) == 0 || direct_invite_redeem_op(op) ||
		    direct_reinvite_clear_op(op))
			emit_identity_error(identity_guard_error_code(), op->id);
		else
			emit_error(identity_guard_error_code());
		return 0;
	}
	if (g_direct_state_migration_failed &&
	    strncmp(op->op, "identity.", 9) != 0 &&
	    strcmp(op->op, "invite.revoke") != 0) {
		if (direct_invite_redeem_op(op) || direct_reinvite_clear_op(op))
			emit_identity_error("direct_state_migration_failed", op->id);
		else
			emit_error("direct_state_migration_failed");
		return 0;
	}
#endif
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
		else if (direct_invite_redeem_op(op) || direct_reinvite_clear_op(op))
			emit_identity_error("identity_changed", op->id);
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
		else if (direct_invite_redeem_op(op) || direct_reinvite_clear_op(op))
			emit_identity_error("locked", op->id);
		else
			emit_error("locked");
		return 0;
	}
	if (g_tox && operation_uses_direct_conversation(op->op) &&
	    !(op->conversation[0] == 'g' &&
	      operation_allows_group_conversation(op->op))) {
#if OMAQ_PROTOCOL_VERSION >= 11
		if (!direct_operation_binding_matches(op)) {
#else
		if (op->key[0] && direct_id_ok(op->conversation) &&
		    !direct_operation_binding_matches(op)) {
#endif
			reject_direct_operation_binding(op);
			return 0;
		}
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
		if (g_identity_primary_uncertain) {
			emit_identity_action("unlock", op->id, NULL, -1);
			return 0;
		}
		g_direct_state_migration_failed = migrate_direct_state() != 0;
		if (g_direct_state_migration_failed) {
			omaq_tox_discard(g_tox);
			g_tox = NULL;
			g_locked = 1;
			emit_identity_error("direct_state_migration_failed", op->id);
			return 0;
		}
#ifdef HAVE_SIGNAL
		if (!g_ratchet)
			g_ratchet = omaq_ratchet_open(home_dir());
		if (!g_ratchet) {
			omaq_tox_discard(g_tox);
			g_tox = NULL;
			g_locked = 1;
			emit_identity_error("direct_state_migration_failed", op->id);
			return 0;
		}
#endif
		if (!g_receipt_outbox_invalid)
			(void)recover_receipt_transaction();
		rc = prune_unavailable_unread();
		if (prune_unavailable_receipts() < 0) {
			g_receipt_outbox_invalid = 1;
			emit_error("receipt_state_invalid");
		}
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
	if (strcmp(op->op, "identity.primary.acknowledge") == 0) {
#ifdef HAVE_TOX
		if (!omaq_message_id_ok(op->id)) {
			emit_identity_error("request_required", op->id);
			return 0;
		}
		if (omaq_identity_primary_ack_persist(state_dir()) != 0 ||
		    omaq_identity_primary_uncertain_clear(state_dir()) != 0) {
			emit_identity_error("identity_guard_invalid", op->id);
			return 0;
		}
		{
			int ack_rc = omaq_tox_primary_acknowledged(g_tox);
			if (ack_rc == -2) {
				emit_identity_error("identity_primary_uncertain", op->id);
				fail_uncertain_primary();
				return 0;
			}
			if (ack_rc < 0) {
				emit_identity_error("identity_guard_invalid", op->id);
				return 0;
			}
		}
		{
			int group_recovery_failed = recover_group_registry_transaction() != 0 ||
				rebuild_group_cache() != 0 || group_bind_pending_load() != 0 ||
				recover_pending_group_accept() != 0;
			g_resolving_primary_uncertainty = 1;
			g_direct_state_migration_failed = migrate_direct_state() != 0;
			g_resolving_primary_uncertainty = 0;
			if (group_recovery_failed || g_direct_state_migration_failed) {
				fail_direct_state_backend();
				g_shutdown_after_drain = 1;
				emit_identity_error("direct_state_migration_failed", op->id);
				return 0;
			}
		}
#ifdef HAVE_SIGNAL
		if (!g_ratchet)
			g_ratchet = omaq_ratchet_open(home_dir());
		if (!g_ratchet) {
			fail_direct_state_backend();
			g_shutdown_after_drain = 1;
			emit_identity_error("direct_state_migration_failed", op->id);
			return 0;
		}
#endif
		g_identity_primary_uncertain = 0;
		reconcile_loaded_identity_state();
		if (omaq_identity_primary_ack_clear(state_dir()) != 0) {
			g_identity_primary_uncertain = 1;
			emit_identity_error("identity_guard_invalid", op->id);
			return 0;
		}
		emit_identity_primary_state(op->id);
		return 0;
#else
		emit_identity_error("unsupported", op->id);
		return 0;
#endif
	}
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
			emit_identity_error("unsupported", op->id);
			return 0;
		}
		if (omaq_invite_expired(&inv, (int64_t)time(NULL))) {
			emit_identity_error("invite_expired", op->id);
			return 0;
		}
		if (inv.kind == INVITE_GROUP) {
#ifdef HAVE_TOX
			if (g_tox) {
				uint32_t fn = UINT32_MAX;
				int add_rc, added_friend = 0;
				if (g_have_pending || g_have_gauth || g_have_gpending ||
				    g_group_bind_proof.used) {
					emit_identity_error("busy", op->id);
					return 0;
				}
				if (strlen(inv.group) != 64 ||
				    strlen(inv.group) >= sizeof(g_gauth_group)) {
					emit_identity_error("unsupported", op->id);
					return 0;
				}
				if (friend_for_addr(inv.tox_addr, &fn) != 0) {
					if (!direct_friend_capacity_available()) {
						emit_identity_error("contact_limit", op->id);
						return 0;
					}
					add_rc = omaq_tox_friend_add(g_tox, inv.tox_addr, inv.id, &fn);
					if (add_rc != 0) {
						if (add_rc == OMAQ_TOX_ADD_STATE_FAILED)
							fail_direct_state_backend();
						emit_identity_error("invite_rejected", op->id);
						return 0;
					}
					added_friend = 1;
				}
				if (added_friend && migrate_direct_state() != 0) {
					int rollback = omaq_tox_friend_delete(g_tox, fn) == 0 &&
						migrate_direct_state() == 0;
					if (!rollback)
						fail_direct_state_backend();
					emit_identity_error("direct_state_migration_failed", op->id);
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
				emit_invite_redeemed("group", op->id);
				emit("{\"event\":\"snapshot\",\"unread\":0}");
				return 0;
			}
#endif
			emit_identity_error("unsupported", op->id);
			return 0;
		}
#ifdef HAVE_TOX
		if (g_tox) {
			uint32_t fn = 0;
			char contact_key[65], self_key[65];
			char request[OMAQ_INVITE_ID_MAX + OMAQ_RK_HEX + 8];
			int add_rc;
#ifdef HAVE_SIGNAL
			char local_rk[OMAQ_RK_HEX + 1];
			if (!g_ratchet || !inv.rk[0] ||
			    omaq_ratchet_local_rk(g_ratchet, local_rk) != 0) {
				emit_identity_error("no_ratchet", op->id);
				return 0;
			}
			if (snprintf(request, sizeof(request), "%s|rk=%s", inv.id, local_rk) >=
			    (int)sizeof(request)) {
				emit_identity_error("unsupported", op->id);
				return 0;
			}
#else
			emit_identity_error("no_ratchet", op->id);
			return 0;
#endif
			memcpy(contact_key, inv.tox_addr, 64);
			contact_key[64] = '\0';
			if (omaq_tox_self_pk_hex(g_tox, self_key) != 0) {
				emit_identity_error("invite_rejected", op->id);
				return 0;
			}
			if (strcmp(contact_key, self_key) == 0) {
				emit_identity_error("invite_self", op->id);
				return 0;
			}
			if (friend_for_addr(inv.tox_addr, &fn) == 0) {
				emit_identity_error("contact_exists", op->id);
				return 0;
			}
			if (omaq_direct_state_add_begin(home_dir(), contact_key, inv.rk) != 0) {
				emit_identity_error("direct_state_migration_failed", op->id);
				return 0;
			}
			if (!direct_friend_capacity_available()) {
				(void)omaq_direct_state_add_finish(home_dir());
				emit_identity_error("contact_limit", op->id);
				return 0;
			}
			add_rc = omaq_tox_friend_add(g_tox, inv.tox_addr, request, &fn);
			if (add_rc != 0) {
				if (add_rc == OMAQ_TOX_ADD_STATE_FAILED)
					fail_direct_state_backend();
				else
					(void)omaq_direct_state_add_finish(home_dir());
				emit_identity_error("invite_rejected", op->id);
				return 0;
			}
#ifdef HAVE_SIGNAL
			{
				if (set_friend_ratchet_pin(fn, inv.rk) != 0) {
					if (omaq_tox_friend_delete(g_tox, fn) != 0 ||
					    migrate_direct_state() != 0)
						fail_direct_state_backend();
					emit_identity_error("safety_key_changed", op->id);
					return 0;
				}
			}
#endif
			if (migrate_direct_state() != 0) {
				fail_direct_state_backend();
				emit_identity_error("direct_state_migration_failed", op->id);
				return 0;
			}
			emit_invite_redeemed("direct", op->id);
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
	if (strcmp(op->op, "direct.reinvite.clear") == 0) {
#ifdef HAVE_TOX
		if (!omaq_message_id_ok(op->id)) {
			emit_identity_error("request_required", op->id);
			return 0;
		}
		if (remove_reinvite_marker() != 0) {
			emit_identity_error("direct_state_migration_failed", op->id);
			return 0;
		}
		g_direct_state_reinvite_required = 0;
		emit_direct_reinvite_state(0, op->id);
		return 0;
#else
		emit_identity_error("unsupported", op->id);
		return 0;
#endif
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
				int accept_rc, add_journal = 0;
#ifdef HAVE_SIGNAL
				char pending_key[65];
				if (!g_issued_is_group) {
					if (!g_have_pending_rk) {
						emit_error("no_ratchet");
						return 0;
					}
					pk_hex(g_pending_pk, pending_key);
					if (omaq_direct_state_add_begin(home_dir(), pending_key,
								g_pending_rk) != 0) {
						emit_error("direct_state_migration_failed");
						return 0;
					}
					add_journal = 1;
				}
#endif
				if (!direct_friend_capacity_available()) {
					if (add_journal)
						(void)omaq_direct_state_add_finish(home_dir());
					emit_error("forbidden");
					return 0;
				}
				accept_rc = omaq_tox_friend_accept(g_tox, g_pending_pk);
				if (accept_rc != 0) {
					if (add_journal && accept_rc == OMAQ_TOX_ADD_STATE_FAILED)
						fail_direct_state_backend();
					else if (add_journal)
						(void)omaq_direct_state_add_finish(home_dir());
					emit_error("forbidden");
					return 0;
				}
				fn = omaq_tox_friend_by_pk(g_tox, g_pending_pk);
#ifdef HAVE_SIGNAL
				if (!g_issued_is_group) {
					if (fn == UINT32_MAX ||
					    set_friend_ratchet_pin(fn, g_pending_rk) != 0) {
						if (fn == UINT32_MAX || omaq_tox_friend_delete(g_tox, fn) != 0 ||
						    migrate_direct_state() != 0)
							fail_direct_state_backend();
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
				if (migrate_direct_state() != 0) {
					fail_direct_state_backend();
					emit_error("direct_state_migration_failed");
					return 0;
				}
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
			char current_key[65], state_cid[OMAQ_DIRECT_STATE_ID_MAX];
			uint32_t fn;
			if (!direct_id_ok(cid)) {
				emit_error("unsupported");
				return 0;
			}
			fn = direct_id_number(cid);
			if (strlen(op->key) != 64 ||
			    omaq_tox_friend_pk_hex(g_tox, fn, current_key) != 0 ||
			    strcmp(op->key, current_key) != 0 ||
			    omaq_direct_state_id(current_key, state_cid, sizeof(state_cid)) != 0) {
				emit_error("forbidden");
				return 0;
			}
			if (recover_receipt_transaction() != 0) {
				emit_error_conv("receipt_state_failed", cid);
				return 0;
			}
			if (omaq_file_friend_active(fn) || file_request_friend_busy(fn) ||
			    omaq_av_friend_busy(fn)) {
				emit_error_conv("busy", cid);
				return 0;
			}
			if (omaq_direct_state_remove_begin(home_dir(), current_key) != 0) {
				emit_error("direct_state_migration_failed");
				return 0;
			}
			if (omaq_tox_friend_delete(g_tox, fn) != 0) {
				fail_direct_state_backend();
				emit_error("forbidden");
				return 0;
			}
			file_request_forget_friend(fn);
			omaq_av_forget_friend(fn);
#ifdef HAVE_SIGNAL
			if (g_ratchet)
				omaq_ratchet_release_peer_cache(g_ratchet, state_cid);
#endif
			if (group_binding_forget_friend(current_key) != 0)
				emit_error("group_registry_failed");
			if (migrate_direct_state_with_removal(current_key) != 0) {
				fail_direct_state_backend();
				emit_error("direct_state_migration_failed");
				return 0;
			}
			if (clear_unread_stored(state_cid, cid) != 0)
				emit_unread_failed(cid, "unread_persist_failed");
			if (receipt_outbox_drop_conversation(state_cid) != 0)
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
	if (strcmp(op->op, "group.list") == 0) {
#ifdef HAVE_TOX
		if (g_tox && op->id[0]) {
			emit_groups(op->id);
			return 0;
		}
#endif
		emit_error("forbidden");
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
			if (group_file_group_active(gid)) {
				emit_error_conv("busy", gid);
				return 0;
			}
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
			if (group_file_member_active(gid, member_key)) {
				emit_error_conv("busy", gid);
				return 0;
			}
			if (strlen(gid) >= sizeof(g_group_leave_notice_suppress[0].group) ||
			    omaq_group_self_role(g_tox, gid, &self) != 0 ||
			    omaq_group_resolve_member(g_tox, gid, member_key, &peer,
						      &victim) != 0) {
				emit_error("forbidden");
				return 0;
			}
			if (!omaq_role_may(self, ACT_KICK, victim)) {
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
			if (group_file_group_active(gid)) {
				emit_error_conv("busy", gid);
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
		omaq_surface surface;
		const char *public_id = op->conversation[0] ? op->conversation : "0";
		char current_key[65] = "";

		memset(&surface, 0, sizeof(surface));
		if (prepare_surface_state() != 0) {
			emit_error_conv("surface_state_failed", public_id);
			return 0;
		}
		if (direct_id_ok(public_id)) {
#ifdef HAVE_TOX
			if (!g_tox || direct_conversation_key(public_id, current_key,
							 sizeof(current_key)) != 0 ||
			    omaq_direct_state_id(current_key, surface.conversation,
						 sizeof(surface.conversation)) != 0) {
				emit_error_conv("identity_changed", public_id);
				return 0;
			}
#else
			emit_error_conv("unsupported", public_id);
			return 0;
#endif
		} else if (!stable_group_id_syntax(public_id) ||
			   snprintf(surface.conversation, sizeof(surface.conversation), "%s",
				    public_id) >= (int)sizeof(surface.conversation)) {
			emit_error_conv("forbidden", public_id);
			return 0;
		}
		snprintf(surface.monitor, sizeof(surface.monitor), "%s", op->monitor);
		surface.x = op->x;
		surface.y = op->y;
		surface.width = op->has_width ? op->width : 420;
		surface.height = op->has_height ? op->height : 420;
		surface.pinned = op->has_pinned ? op->pinned : 0;
		if (omaq_surface_set(state_dir(), &surface) != 0) {
			emit_error_conv("surface_state_failed", public_id);
			return 0;
		}
		{
			char event[900], escaped_monitor[384];
			if (omaq_json_escape(surface.monitor, escaped_monitor,
					     sizeof(escaped_monitor)) != 0)
				escaped_monitor[0] = '\0';
			snprintf(event, sizeof(event),
				 "{\"event\":\"surface\",\"conversation\":\"%s\",\"key\":\"%s\",\"monitor\":\"%s\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"pinned\":%s}",
				 public_id, current_key, escaped_monitor, surface.x, surface.y,
				 surface.width, surface.height,
				 surface.pinned ? "true" : "false");
			emit(event);
		}
		return 0;
	}
	if (strcmp(op->op, "surface.list") == 0) {
		omaq_surface surfaces[OMAQ_SURFACE_MAX];
		int count;
		size_t capacity;
		char *event, *cursor;
		size_t left;
		int first = 1;

		if (prepare_surface_state() != 0) {
			emit_error("surface_state_failed");
			emit("{\"event\":\"surfaces\",\"items\":[]}");
			return 0;
		}
		count = omaq_surface_list(state_dir(), surfaces, OMAQ_SURFACE_MAX);
		capacity = 64u + (size_t)(count > 0 ? count : 0) * 800u;
		event = malloc(capacity);
		if (count < 0 || !event) {
			free(event);
			emit("{\"event\":\"surfaces\",\"items\":[]}");
			return 0;
		}
		cursor = event;
		left = capacity;
		if (snprintf(cursor, left, "{\"event\":\"surfaces\",\"items\":[") >=
		    (int)left) {
			free(event);
			emit("{\"event\":\"surfaces\",\"items\":[]}");
			return 0;
		}
		cursor += strlen(cursor);
		left = capacity - (size_t)(cursor - event);
		for (int i = 0; i < count; i++) {
			char public_id[OMAQ_DIRECT_STATE_ID_MAX], current_key[65] = "";
			char escaped_id[160], escaped_monitor[384];
			int written;

			if (surfaces[i].conversation[0] == 'd') {
#ifdef HAVE_TOX
				if (!g_tox || public_conversation(surfaces[i].conversation,
							      public_id, sizeof(public_id)) != 0)
					continue;
				memcpy(current_key, surfaces[i].conversation + 2, 65);
#else
				continue;
#endif
			} else if (snprintf(public_id, sizeof(public_id), "%s",
					    surfaces[i].conversation) >= (int)sizeof(public_id)) {
				continue;
			}
			if (omaq_json_escape(public_id, escaped_id, sizeof(escaped_id)) != 0 ||
			    omaq_json_escape(surfaces[i].monitor, escaped_monitor,
					     sizeof(escaped_monitor)) != 0)
				continue;
			written = snprintf(cursor, left,
					   "%s{\"conversation\":\"%s\",\"key\":\"%s\",\"monitor\":\"%s\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"pinned\":%s}",
					   first ? "" : ",", escaped_id, current_key,
					   escaped_monitor, surfaces[i].x, surfaces[i].y,
					   surfaces[i].width, surfaces[i].height,
					   surfaces[i].pinned ? "true" : "false");
			if (written < 0 || (size_t)written >= left)
				break;
			cursor += written;
			left -= (size_t)written;
			first = 0;
		}
		if (left < 3) {
			free(event);
			emit("{\"event\":\"surfaces\",\"items\":[]}");
			return 0;
		}
		memcpy(cursor, "]}", 3);
		emit(event);
		free(event);
		return 0;
	}
	if (strcmp(op->op, "surface.get") == 0) {
		omaq_surface surface;
		const char *public_id = op->conversation[0] ? op->conversation : "0";
		char stored_id[OMAQ_DIRECT_STATE_ID_MAX], current_key[65] = "";

		if (prepare_surface_state() != 0) {
			emit_error_conv("surface_state_failed", public_id);
			emit("{\"event\":\"surface\",\"conversation\":\"\",\"pinned\":false}");
			return 0;
		}
		if (direct_id_ok(public_id)) {
#ifdef HAVE_TOX
			if (!g_tox || direct_conversation_key(public_id, current_key,
							 sizeof(current_key)) != 0 ||
			    omaq_direct_state_id(current_key, stored_id, sizeof(stored_id)) != 0) {
				emit("{\"event\":\"surface\",\"conversation\":\"\",\"pinned\":false}");
				return 0;
			}
#else
			emit("{\"event\":\"surface\",\"conversation\":\"\",\"pinned\":false}");
			return 0;
#endif
		} else if (!stable_group_id_syntax(public_id) ||
			   snprintf(stored_id, sizeof(stored_id), "%s", public_id) >=
				   (int)sizeof(stored_id)) {
			emit("{\"event\":\"surface\",\"conversation\":\"\",\"pinned\":false}");
			return 0;
		}
		if (omaq_surface_get(state_dir(), stored_id, &surface) != 0) {
			emit("{\"event\":\"surface\",\"conversation\":\"\",\"pinned\":false}");
			return 0;
		}
		{
			char event[900], escaped_monitor[384];
			if (omaq_json_escape(surface.monitor, escaped_monitor,
					     sizeof(escaped_monitor)) != 0)
				escaped_monitor[0] = '\0';
			snprintf(event, sizeof(event),
				 "{\"event\":\"surface\",\"conversation\":\"%s\",\"key\":\"%s\",\"monitor\":\"%s\",\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d,\"pinned\":%s}",
				 public_id, current_key, escaped_monitor, surface.x, surface.y,
				 surface.width, surface.height,
				 surface.pinned ? "true" : "false");
			emit(event);
		}
		return 0;
	}
	if (strcmp(op->op, "sound.list") == 0 ||
	    strcmp(op->op, "sound.import") == 0 ||
	    strcmp(op->op, "sound.remove") == 0) {
		const char *operation = op->op + strlen("sound.");
		omaq_sound imported;

		if (!omaq_message_id_ok(op->request)) {
			emit_error("request_required");
			return 0;
		}
		if (replay_one_sound_result(operation, op->request))
			return 0;
		if (strcmp(op->op, "sound.import") == 0) {
			if (omaq_sound_import(home_dir(), op->path, &imported) != 0) {
				emit_sound_state(op->request, operation, NULL, "invalid_sound");
				return 0;
			}
			emit_sound_state(op->request, operation, imported.id, NULL);
			return 0;
		}
		if (strcmp(op->op, "sound.remove") == 0) {
			if (omaq_sound_remove(home_dir(), op->id) != 0) {
				emit_sound_state(op->request, operation, NULL, "sound_remove_failed");
				return 0;
			}
			emit_sound_state(op->request, operation, NULL, NULL);
			return 0;
		}
		emit_sound_state(op->request, operation, NULL, NULL);
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
	if (strcmp(op->op, "settings.auto-open.get") == 0 ||
	    strcmp(op->op, "settings.auto-open.set") == 0) {
#ifdef HAVE_TOX
		omaq_auto_open_state settings;
		char fingerprint[65];
		int setting = strcmp(op->op, "settings.auto-open.set") == 0;

		if (!g_tox || !omaq_message_id_ok(op->request) ||
		    omaq_tox_self_pk_hex(g_tox, fingerprint) != 0 ||
		    load_auto_open_state(fingerprint, &settings) != 0) {
			emit_auto_open_state(NULL, op->request, "settings_failed");
			return 0;
		}
		if (setting) {
			uint32_t ignored;
			int valid = op->has_enabled &&
				((op->conversation[0] == 'd' &&
				  find_friend_for_direct_state(op->conversation, &ignored) == 0) ||
				 (op->conversation[0] == 'g' &&
				  omaq_group_id_parse(op->conversation, &ignored) == 0));
			if (!valid || omaq_auto_open_set(&settings, op->conversation,
						      op->enabled) != 0 ||
			    omaq_auto_open_save(state_dir(), fingerprint, &settings) != 0) {
				emit_auto_open_state(NULL, op->request, "settings_failed");
				return 0;
			}
		}
		emit_auto_open_state(&settings, op->request, NULL);
		return 0;
#else
		emit_error("unsupported");
		return 0;
#endif
	}
	if (strcmp(op->op, "settings.auto-open.migrated") == 0) {
#ifdef HAVE_TOX
		char fingerprint[65];
		if (!g_tox || strlen(op->id) != 64 ||
		    omaq_tox_self_pk_hex(g_tox, fingerprint) != 0 ||
		    strcmp(fingerprint, op->id) != 0 ||
		    omaq_state_archive_copy(state_dir(), "auto-open.json") != 0 ||
		    omaq_auto_open_retire_global(state_dir(), fingerprint) != 0) {
			emit_error("forbidden");
			return 0;
		}
		return 0;
#else
		emit_error("unsupported");
		return 0;
#endif
	}
	if (strcmp(op->op, "typing.set") == 0) {
#ifdef HAVE_TOX
		const char *cid = op->conversation[0] ? op->conversation : "0";
		if (!g_tox || !op->has_typing) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
		if (cid[0] == 'g') {
			uint8_t packet[sizeof(group_typing_magic) + 1u];
			uint32_t group_number;
			if (omaq_group_id_parse(cid, &group_number) != 0) {
				emit_error_conv("forbidden", cid);
				return 0;
			}
			memcpy(packet, group_typing_magic, sizeof(group_typing_magic));
			packet[sizeof(group_typing_magic)] = op->typing ? 1 : 0;
			if (omaq_tox_group_custom_send(g_tox, group_number, packet,
						       sizeof(packet)) != 0)
				emit_error_conv("offline", cid);
			return 0;
		}
		if (!direct_id_ok(cid) ||
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
		char reaction_rate_key[32], state_cid[OMAQ_DIRECT_STATE_ID_MAX];
		int reaction_rc;
		if (!g_tox || !direct_id_ok(cid) || !op->id[0] || !op->has_text ||
		    !omaq_message_reaction_ok(op->text)) {
			emit_message_reaction_failed(cid, op->id, "invalid");
			return 0;
		}
		fn = direct_id_number(cid);
		if (direct_state_for_friend(fn, state_cid, sizeof(state_cid)) != 0) {
			emit_message_reaction_failed(cid, op->id, "forbidden");
			return 0;
		}
		if (!omaq_tox_online(g_tox) || !omaq_tox_friend_online(g_tox, fn)) {
			emit_message_reaction_failed(cid, op->id, "offline");
			return 0;
		}
		if (!g_ratchet || !ratchet_has_session_friend(fn)) {
			emit_message_reaction_failed(cid, op->id, "ratchet_pending");
			return 0;
		}
		if (omaq_store_message_exists(home_dir(), state_cid, op->id) != 1) {
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
		if (omaq_store_update_reaction(home_dir(), state_cid, op->id, op->text,
					       "me") != 0) {
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
		const char *history_cid = cid;
		char state_cid[OMAQ_DIRECT_STATE_ID_MAX];
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
			if (direct_state_for_friend(fn, state_cid, sizeof(state_cid)) != 0) {
				emit_error_conv("forbidden", cid);
				return 0;
			}
			history_cid = state_cid;
			if (!omaq_tox_online(g_tox) || !omaq_tox_friend_online(g_tox, fn)) {
				emit_error_conv("offline", cid);
				return 0;
			}
#ifndef HAVE_SIGNAL
			emit_error_conv("no_ratchet", cid);
			return 0;
#else
			if (!g_ratchet || !ratchet_has_session_friend(fn)) {
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
		update = deleted ? omaq_message_delete(home_dir(), history_cid, op->id) :
			omaq_message_edit(home_dir(), history_cid, op->id, op->text);
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
		char history_cid[OMAQ_DIRECT_STATE_ID_MAX];
		omaq_store_message_id *ids = NULL;
		size_t id_count = 0;
		unsigned unread;

		if (!g_tox || !conversation_id_ok(cid) ||
		    storage_conversation(cid, history_cid, sizeof(history_cid)) != 0 ||
		    g_receipt_outbox_invalid) {
			emit_conversation_read("conversation.read.failed", cid,
					       g_receipt_outbox_invalid ? "receipt_state_invalid" : "forbidden");
			return 0;
		}
		if (recover_receipt_transaction() != 0) {
			emit_conversation_read("conversation.read.failed", cid,
					       "receipt_state_failed");
			return 0;
		}
		unread = omaq_unread_count(&g_unread, history_cid);
		if (omaq_store_unread_receipt_ids(home_dir(), history_cid, unread, &ids,
						  &id_count) != 0 ||
		    (id_count > 0 && (!receipt_outbox_has_capacity(history_cid, ids, id_count) ||
				      receipt_transaction_begin(history_cid, ids, id_count) != 0))) {
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
		     receipt_outbox_commit_add(history_cid, ids, id_count) != 0 ||
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
			char state_cid[OMAQ_DIRECT_STATE_ID_MAX];
			if (!g_tox || !conversation_id_ok(cid) || !op->id[0] ||
			    storage_conversation(cid, state_cid, sizeof(state_cid)) != 0 ||
			    snprintf(receipt_id.id, sizeof(receipt_id.id), "%s", op->id) >=
				(int)sizeof(receipt_id.id) ||
			    receipt_outbox_commit_add(state_cid, &receipt_id, 1) != 0) {
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
				char state_cid[OMAQ_DIRECT_STATE_ID_MAX];
				if (!direct_id_ok(cid)) {
					emit_message_failed(cid, op->id, "unsupported", 0);
					return 0;
				}
				fn = direct_id_number(cid);
				if (direct_state_for_friend(fn, state_cid, sizeof(state_cid)) != 0) {
					emit_message_failed(cid, op->id, "forbidden", 0);
					return 0;
				}
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
					char packed[3200], wire[3600], mid[64];
					if (!ratchet_has_session_friend(fn)) {
						int request_rc = request_ratchet_session(fn);
						if (request_rc < 0) {
							emit_message_failed(cid, op->id,
								!omaq_tox_friend_online(g_tox, fn)
									? "offline" : "no_ratchet", 0);
							return 0;
						}
						emit_message_failed(cid, op->id, "ratchet_pending", 0);
						return 0;
					}
					if (omaq_message_id_new(mid, sizeof(mid)) != 0 ||
					    omaq_message_wire_pack(packed, sizeof(packed), mid, op->reply, op->text) != 0 ||
					    ratchet_encrypt_friend(fn, packed, wire, sizeof(wire)) != 0) {
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
						if (omaq_message_append_id_reply(home_dir(), state_cid, "me",
									 op->text, "out", mid, op->reply) != 0) {
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
		const char *history_cid = cid;
		char esc_cid[128], key_field[96] = "", ev[300];
#ifdef HAVE_TOX
		char state_cid[OMAQ_DIRECT_STATE_ID_MAX];
#endif
		if (!conversation_id_ok(cid)) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
#ifdef HAVE_TOX
		if (g_tox && storage_conversation(cid, state_cid, sizeof(state_cid)) != 0) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
		if (g_tox)
			history_cid = state_cid;
		if (recover_receipt_transaction() != 0) {
			emit_error_conv("receipt_state_failed", cid);
			return 0;
		}
#endif
		if (omaq_store_clear(home_dir(), history_cid) != 0) {
			emit_error_conv("forbidden", cid);
			return 0;
		}
		if (clear_unread(cid) != 0)
			emit_unread_failed(cid, "unread_persist_failed");
#ifdef HAVE_TOX
		if (receipt_outbox_drop_conversation(history_cid) != 0)
			emit_error_conv("receipt_state_failed", cid);
#endif
		if (history_cid[0] == 'd' && history_cid[1] == ':')
			snprintf(key_field, sizeof(key_field), ",\"key\":\"%s\"",
				 history_cid + 2);
		if (omaq_json_escape(cid, esc_cid, sizeof(esc_cid)) != 0)
			emit("{\"event\":\"history\",\"conversation\":\"0\",\"cleared\":true,\"items\":[]}");
		else {
			snprintf(ev, sizeof(ev),
				 "{\"event\":\"history\",\"conversation\":\"%s\"%s,\"cleared\":true,\"items\":[]}",
				 esc_cid, key_field);
			emit(ev);
		}
		return 0;
	}
	if (strcmp(op->op, "history") == 0) {
		char *out = NULL;
		size_t n = 0;
		int lim = op->has_limit ? op->limit : 50;
		const char *cid = op->conversation[0] ? op->conversation : "0";
		const char *history_cid = cid;
#ifdef HAVE_TOX
		char state_cid[OMAQ_DIRECT_STATE_ID_MAX];
		if (g_tox && storage_conversation(cid, state_cid, sizeof(state_cid)) != 0) {
			emit_history_failed(cid, op->id);
			return 0;
		}
		if (g_tox)
			history_cid = state_cid;
#endif
		if (omaq_message_history(home_dir(), history_cid, lim, &out, &n) == 0 && out) {
			emit_json_items("history", cid, out, n, op->id, history_cid,
					 history_cid);
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
		const char *history_cid = cid;
#ifdef HAVE_TOX
		char state_cid[OMAQ_DIRECT_STATE_ID_MAX];
		if (g_tox && storage_conversation(cid, state_cid, sizeof(state_cid)) != 0) {
			emit_json_items("search", cid, "", 0, op->id, NULL, NULL);
			return 0;
		}
		if (g_tox)
			history_cid = state_cid;
#endif
		if (!op->text[0]) {
			emit_json_items("search", cid, "", 0, op->id, NULL, history_cid);
			return 0;
		}
		if (omaq_message_search(home_dir(), history_cid, op->text, lim, &out, &n) == 0 && out) {
			emit_json_items("search", cid, out, n, op->id, NULL, history_cid);
			free(out);
			return 0;
		}
		emit_json_items("search", cid, "", 0, op->id, NULL, history_cid);
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
	if (strcmp(op->op, "attachment.stage.create") == 0) {
		char path[512];
		path[0] = '\0';
		if (!op->id[0] || attachment_stage_owner_find(op->id) >= 0 ||
		    attachment_stage_create(op->id, path, sizeof(path)) != 0 ||
		    attachment_stage_owner_add(op->id, path, owner_fd) != 0) {
			if (op->id[0] && path[0])
				(void)attachment_stage_discard(op->id, path);
			emit_attachment_inspection(op->id, "", 0);
		} else {
			emit_attachment_stage(op->id, path);
		}
		return 0;
	}
	if (strcmp(op->op, "attachment.stage.commit") == 0) {
		char final_path[512];
		int owned = attachment_stage_owner_match(op->id, owner_fd);
		if (!op->id[0] || (owner_fd >= 0 && owned != 1) ||
		    attachment_stage_commit(op->id, op->path,
					    final_path, sizeof(final_path)) != 0 ||
		    attachment_stage_owner_update(op->id, final_path, owner_fd) != 0)
			emit_attachment_inspection(op->id, op->path, 0);
		else
			emit_attachment_inspection(op->id, final_path, 1);
		return 0;
	}
	if (strcmp(op->op, "attachment.stage.discard") == 0) {
		int owned = attachment_stage_owner_match(op->id, owner_fd);
		if (owned >= 0 && attachment_stage_discard(op->id, op->path) == 0) {
			attachment_stage_owner_forget_request(op->id);
			emit_attachment_discarded(op->id);
		}
		return 0;
	}
	if (strcmp(op->op, "attachment.inspect") == 0) {
		char staging[512], final_path[512];
		int accepted = 0, created_here = 0, owner_added = 0;

		staging[0] = '\0';
		if (op->id[0] && omaq_message_id_ok(op->id) &&
		    omaq_file_path_ok(op->path) &&
		    attachment_stage_owner_find(op->id) < 0 &&
		    attachment_stage_create(op->id, staging, sizeof(staging)) == 0) {
			created_here = 1;
			if (attachment_stage_owner_add(op->id, staging, owner_fd) == 0) {
				owner_added = owner_fd >= 0;
				if (omaq_inline_image_import_file(op->path, staging) == 0 &&
				    attachment_stage_commit(op->id, staging, final_path,
						    sizeof(final_path)) == 0 &&
				    attachment_stage_owner_update(op->id, final_path,
							  owner_fd) == 0)
					accepted = 1;
			}
		}
		if (accepted)
			emit_attachment_inspection(op->id, final_path, 1);
		else {
			if (created_here)
				(void)attachment_stage_discard(op->id, staging);
			if (owner_added)
				attachment_stage_owner_forget_request(op->id);
			emit_attachment_inspection(op->id, op->path, 0);
		}
		return 0;
	}
	if (strcmp(op->op, "file.send") == 0) {
#ifdef HAVE_TOX
		if (g_tox) {
			const char *cid = op->conversation[0] ? op->conversation : "0";
			uint32_t fn, fnum;
			char name[OMAQ_FILE_NAME_MAX + 1];
			int attachment_owner = attachment_stage_path_owner(op->path, owner_fd);

			if (attachment_owner < 0) {
				emit_file_rejected(cid, op->id, "forbidden");
				return 0;
			}
			if (cid[0] == 'g') {
				int group_rc = group_file_send_begin(cid, op->path,
					op->kind[0] ? op->kind : "file", op->id);
				if (group_rc != 0)
					emit_file_rejected(cid, op->id,
						group_rc == -2 ? "busy" : "forbidden");
				else
					(void)attachment_stage_owner_adopt(op->path, owner_fd);
				return 0;
			}
			if (!direct_id_ok(cid)) {
				emit_file_rejected(cid, op->id, "forbidden");
				return 0;
			}
			fn = direct_id_number(cid);
			if (!op->path[0] || omaq_file_basename(op->path, name, sizeof(name)) != 0 ||
			    (op->kind[0] && strcmp(op->kind, "file") != 0 &&
			     strcmp(op->kind, "image") != 0)) {
				emit_file_rejected(cid, op->id, "unsupported");
				return 0;
			}
			if (strcmp(op->kind, "image") == 0 &&
			    omaq_inline_image_validate_file(op->path) != 0) {
				emit_file_rejected(cid, op->id, "invalid_image");
				return 0;
			}
			if (omaq_file_send_begin(g_tox, fn, op->path, &fnum) != 0) {
				emit_file_rejected(cid, op->id, "forbidden");
				return 0;
			}
			if (file_request_begin(fn, fnum, op->id, op->path,
					       op->kind[0] ? op->kind : "file") != 0) {
				cancel_file_after_error(fn, fnum);
				emit_file_rejected(cid, op->id, "busy");
				return 0;
			}
			{
				int pending_attachment = attachment_pending_update(op->path, 0);
				if (pending_attachment < 0 ||
				    file_request_mark_pending(fn, fnum,
						      pending_attachment == 1) != 0) {
					cancel_file_after_error(fn, fnum);
					(void)file_request_finish(fn, fnum, "failed");
					emit_file_rejected(cid, op->id, "forbidden");
					return 0;
				}
			}
			(void)name;
			(void)attachment_stage_owner_adopt(op->path, owner_fd);
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

			if (cid[0] == 'g') {
				int group_index = -1;
				for (int i = 0; i < GROUP_FILE_OUT_MAX; i++)
					if (g_group_file_out[i].used &&
					    strcmp(g_group_file_out[i].group, cid) == 0 &&
					    strcmp(g_group_file_out[i].request, op->id) == 0) {
						group_index = i;
						break;
					}
				if (!op->id[0] || group_index < 0) {
					emit_file_rejected(cid, op->id, "transfer_unknown");
					return 0;
				}
				emit_group_file("sending", cid,
					g_group_file_out[group_index].event_id, NULL, 0, NULL,
					"out", g_group_file_out[group_index].request,
					g_group_file_out[group_index].kind, NULL, NULL);
				return 0;
			}
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

			if (op->conversation[0] == 'g') {
				if (group_file_accept(op->conversation, op->id, over) != 0)
					emit_group_file("failed", op->conversation, op->id, NULL, 0,
						NULL, "in", NULL, "file", NULL,
						"accept_failed");
				return 0;
			}
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
				cancel_file_after_error(fn, fnum);
				emit_error("forbidden");
				return 0;
			}
			if (omaq_tox_file_control(g_tox, fn, fnum, OMAQ_TOX_FILE_RESUME) != 0) {
				cancel_file_after_error(fn, fnum);
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

			if (op->conversation[0] == 'g') {
				if (group_file_cancel(op->conversation, op->id) != 0)
					emit_error_conv("forbidden", op->conversation);
				return 0;
			}
			if (omaq_file_id_parse(op->id, &fn, &fnum) != 0) {
				emit_error("unsupported");
				return 0;
			}
			if (!omaq_file_can_cancel(fn, fnum)) {
				emit_error("forbidden");
				return 0;
			}
			if (omaq_file_is_avatar(fn, fnum)) {
				cancel_file_after_error(fn, fnum);
				return 0;
			}
			{
				int sending = omaq_file_is_sending(fn, fnum);
				const char *request;
				if (sending)
					(void)file_request_finish_pending(fn, fnum, 0);
				if (omaq_file_cancel(g_tox, fn, fnum) != 0) {
					omaq_file_drop(fn, fnum);
					request = sending ? file_request_finish(fn, fnum, "failed") : NULL;
					emit_file("failed", fn, fnum, NULL, 0, NULL,
						  sending ? "out" : "in", request);
				} else {
					request = sending ? file_request_finish(fn, fnum, "canceled") : NULL;
					emit_file("canceled", fn, fnum, NULL, 0, NULL,
						  sending ? "out" : "in", request);
				}
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

			if (!direct_id_ok(cid)) {
				emit_error("forbidden");
				return 0;
			}
			fn = direct_id_number(cid);
			if (omaq_av_start(g_tox, fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit_call_state(fn, "ringing");
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

			if (!direct_id_ok(cid)) {
				emit_error("forbidden");
				return 0;
			}
			fn = direct_id_number(cid);
			if (omaq_av_answer(g_tox, fn) != 0) {
				emit_error("forbidden");
				return 0;
			}
			emit_call_state(fn, "active");
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
					emit_call_state(fn, "ended");
					return 0;
				}
				stopped = omaq_av_stop(g_tox, fn);
				if (stopped < 0) {
					emit_error_conv("forbidden", cid);
					return 0;
				}
				g_av_reset_requested = 1;
			}
			emit_call_state(fn, "ended");
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
		if (replacing && !g_identity_guard_error &&
		    (recover_receipt_transaction() != 0 ||
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
			char old_address[77], old_fingerprint[65], replacement_fingerprint[65];
			char candidate_fingerprint[65];
			struct omaq_tox *candidate;
			identity_state_archive identity_archive;
			const char *failure_code = "identity_import_failed";
			int candidate_error = 0, swapped = 0, archived = 0, marker = 0;
			int unread_reset = 0, stage_created = 0, guard_swapped = 0;
			int replacement_reinvite_marker_created = 0;
			int archive_rc, guard_replace_rc, marker_rc;
			int rollback_load = 0, rollback_ok = 1;
			size_t replacement_friend_count = 0;

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
			if (!candidate || identity_group_files_validate(candidate, stage) != 0 ||
			    omaq_tox_self_pk_hex(candidate, candidate_fingerprint) != 0) {
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
			if (g_identity_guard_error) {
				int restore_rc = restore_guarded_identity(stage_save, op->passphrase,
								 candidate_fingerprint);
				(void)cleanup_identity_stage(stage);
				if (restore_rc == 0) {
					emit_identity_action("import", op->id, NULL, -1);
				} else if (restore_rc == -3) {
					emit_identity_error("identity_mismatch", op->id);
				} else if (restore_rc == -2) {
					g_identity_recovery_required = 1;
					g_shutdown_after_drain = 1;
				} else if (restore_rc == -5) {
					emit_identity_error("direct_state_migration_failed", op->id);
				} else {
					emit_identity_error("identity_import_failed", op->id);
				}
				return 0;
			}
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
			g_identity_guard_replacement_load = 1;
			candidate_error = load_tox(op->passphrase);
			g_identity_guard_replacement_load = 0;
			if (candidate_error != 0 ||
			    ((g_direct_state_migration_failed = migrate_direct_state() != 0))) {
				goto identity_import_rollback;
			}
#ifdef HAVE_SIGNAL
			g_ratchet = omaq_ratchet_open(home_dir());
			if (!g_ratchet) {
				goto identity_import_rollback;
			}
#endif
			if (omaq_tox_friend_count(g_tox, &replacement_friend_count) != 0 ||
			    omaq_tox_self_pk_hex(g_tox, replacement_fingerprint) != 0 ||
			    update_identity_marker_phase(token, old_fingerprint, "replacement") != 0 ||
			    (replacement_friend_count > 0 && persist_reinvite_marker() != 0)) {
				failure_code = "identity_state_archive_failed";
				goto identity_import_rollback;
			}
			replacement_reinvite_marker_created = replacement_friend_count > 0;
			g_direct_state_reinvite_required = replacement_friend_count > 0;
			omaq_unread_destroy(&g_unread);
			omaq_unread_init(&g_unread);
			unread_reset = 1;
			if (omaq_store_unread_save(&g_unread, state_dir()) != 0 ||
			    fsync_directory(home_dir()) != 0 || fsync_directory(state_dir()) != 0) {
				failure_code = "identity_state_archive_failed";
				goto identity_import_rollback;
			}
			guard_replace_rc = omaq_identity_guard_replace(state_dir(), old_fingerprint,
							 replacement_fingerprint);
			if (guard_replace_rc != 0) {
				if (guard_replace_rc == OMAQ_IDENTITY_GUARD_PUBLISHED)
					guard_swapped = 1;
				failure_code = "identity_state_archive_failed";
				goto identity_import_rollback;
			}
			guard_swapped = 1;
			guard_replace_rc = omaq_tox_enable_recovery(g_tox, state_dir(), 0);
			if (guard_replace_rc < 0) {
				g_identity_recovery_required = 1;
				g_shutdown_after_drain = 1;
				(void)cleanup_identity_stage(stage);
				return 0;
			}
			if (remove_identity_marker() != 0) {
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
			if (replacement_reinvite_marker_created && remove_reinvite_marker() != 0)
				rollback_ok = 0;
			replacement_reinvite_marker_created = 0;
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
			if (guard_swapped &&
			    omaq_identity_guard_restore(state_dir(), old_fingerprint) != 0)
				rollback_ok = 0;
			guard_swapped = 0;
			if (swapped && omaq_identity_recovery_stale_persist(state_dir()) != 0)
				rollback_ok = 0;
			if (swapped) {
				reset_identity_runtime_state();
				if (omaq_receipt_outbox_load(&g_receipt_outbox, state_dir()) != 0) {
					g_receipt_outbox_invalid = 1;
					rollback_ok = 0;
				}
				rollback_load = load_tox(NULL);
				if (rollback_load < 0 ||
				    (g_tox && migrate_direct_state() != 0))
					rollback_ok = 0;
				else
					g_direct_state_migration_failed = 0;
			}
#ifdef HAVE_SIGNAL
			if (!g_ratchet && g_tox && rollback_ok)
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
		int rc = handle_op(&op, identity_ready, g_input_owner_fd);
#ifdef HAVE_TOX
		fail_uncertain_primary();
#endif
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
	attachment_stage_owner_disconnect(g_clients[i]);
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
	g_input_owner_fd = g_clients[i];
	if (omaq_line_reader_feed(&g_creader[i], tmp, (size_t)r,
				  serve_input_line, &g_client_identity_ready[i]) != 0) {
		g_input_owner_fd = -1;
		drop_client(i);
		return;
	}
	g_input_owner_fd = -1;
}

static int read_stdin_lines(void)
{
	char tmp[512];
	g_input_owner_fd = -1;

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
	if (!g_identity_guard_error)
		(void)load_tox(NULL);
	g_direct_state_migration_failed = g_tox && !g_identity_primary_uncertain &&
		migrate_direct_state() != 0;
	if (g_direct_state_migration_failed)
		fail_direct_state_backend();
	reconcile_loaded_identity_state();
#endif
#ifdef HAVE_SIGNAL
	g_ratchet = g_tox && !g_direct_state_migration_failed &&
		!g_identity_primary_uncertain ? omaq_ratchet_open(home_dir()) : NULL;
	if (g_tox && !g_identity_primary_uncertain && !g_ratchet)
		fail_direct_state_backend();
#endif
	g_backend_started = 1;
}

int main(int argc, char **argv)
{
	int hold = 0;
#ifdef HAVE_TOX
	int ack_recovery_rc = 0, recovery_rc = 0, guard_recovery_rc = 0;
	int uncertainty_rc = 0;
#endif

	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, request_shutdown_signal);
	signal(SIGINT, request_shutdown_signal);
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
	if (ensure_private_directory(home_dir(), "OMAQ_HOME") != 0 ||
	    ensure_state_dir() != 0)
		return 1;
	rc = uninstall_marker_present();
	if (rc != 0)
		return rc > 0 ? 0 : 1;
#ifdef OMAQ_IPC_TEST
	if (test_startup_pause("before-lock") != 0)
		return 1;
#endif
	rc = take_state_lock();
	if (rc == 2)
		return 2;
	if (rc != 0)
		return 1;
	rc = uninstall_marker_present();
	if (rc != 0)
		return rc > 0 ? 0 : 1;
	if (!startup_executable_matches(argv[0]))
		return 1;
	rc = take_lock();
	if (rc == 2)
		return 2;
	if (rc != 0)
		return 1;
#ifdef OMAQ_IPC_TEST
	if (test_startup_pause("after-lock") != 0)
		return 1;
#endif
#ifdef HAVE_TOX
	if (cleanup_orphan_identity_stages() != 0)
		recovery_rc = -1;
	else
		recovery_rc = recover_identity_replacement();
	if (recovery_rc >= 0) {
		guard_recovery_rc = recover_identity_guard_restore();
		if (guard_recovery_rc < 0)
			recovery_rc = -1;
	}
	if (recovery_rc < 0) {
		fprintf(stderr, "omaq: identity replacement recovery failed\n");
		g_identity_recovery_required = 1;
		g_shutdown_after_drain = 1;
	} else {
		if (recovery_rc > 0 || guard_recovery_rc > 0)
			g_identity_recovered = 1;
		g_identity_backup_cleanup_failed =
			cleanup_orphan_identity_backups() != 0;
	}
	if (!g_identity_recovery_required) {
		g_identity_guard_state = omaq_identity_guard_preflight(home_dir(), state_dir());
		if (g_identity_guard_state < 0) {
			g_identity_guard_error = g_identity_guard_state;
			fprintf(stderr, "omaq: existing identity is unavailable; refusing to create a replacement\n");
		} else if (g_identity_guard_state == OMAQ_IDENTITY_GUARD_RESTORED) {
			g_identity_recovered = 1;
		}
		uncertainty_rc = omaq_identity_primary_uncertain_present(state_dir());
		ack_recovery_rc = omaq_identity_primary_ack_present(state_dir());
		if (uncertainty_rc < 0 || ack_recovery_rc < 0)
			g_identity_guard_error = OMAQ_IDENTITY_GUARD_INVALID;
		else
			g_identity_primary_uncertain = uncertainty_rc > 0 || ack_recovery_rc > 0;
	}
#endif
#ifdef HAVE_TOX
	if (!g_identity_guard_error && attachment_stage_cleanup() != 0) {
#else
	if (attachment_stage_cleanup() != 0) {
#endif
		fprintf(stderr, "omaq: attachment staging cleanup failed\n");
		return 1;
	}
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
	if (init_instance_id() != 0) {
		fprintf(stderr, "omaq: secure instance-id generation failed\n");
		return 1;
	}
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
	if (!g_replay_mode && !shutdown_requested())
		start_backend();
	if (hold && !shutdown_requested()) {
		while (!shutdown_requested()) {
#ifdef HAVE_TOX
			if (g_tox && !g_identity_primary_uncertain) {
				omaq_tox_iterate(g_tox);
				group_file_pump();
				flush_receipt_acknowledgements();
				expire_group_auth_reservation();
#ifdef HAVE_SIGNAL
				expire_pending_group_invite();
				retry_pending_native_group_invite();
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
				fail_uncertain_primary();
				emit_identity_recovery_state(0);
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

		if (shutdown_requested()) {
			int ack_status = g_shutdown_signal ? 1 : shutdown_ack_status();

			if (ack_status < 0) {
				cancel_helper_shutdown();
			} else if (ack_status > 0) {
				if (g_listen >= 0) {
					close(g_listen);
					g_listen = -1;
				}
				if (g_stdout_closed ||
				    (!omaq_stdout_spool_pending(g_stdout_spool) &&
				     g_stdout_len <= g_stdout_off))
					break;
			}
		}
		if (g_replay_mode && !omaq_stdout_spool_pending(g_stdout_spool)) {
			g_replay_mode = 0;
			if (!shutdown_requested())
				start_backend();
		}
#ifdef HAVE_TOX
		if (g_tox)
			ms = (int)omaq_tox_interval_ms(g_tox);
#endif
		if (!g_replay_mode && !shutdown_requested() && !g_stdin_closed) {
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
		if (!g_replay_mode && !shutdown_requested() && g_listen >= 0) {
			listen_idx = nf;
			pf[nf].fd = g_listen;
			pf[nf].events = POLLIN;
			nf++;
		}
		for (size_t i = 0; i < g_ncli; i++) {
			pf[nf].fd = g_clients[i];
			pf[nf].events = (shutdown_requested() ? 0 : POLLIN) |
				(g_olen[i] > g_ooff[i] ? POLLOUT : 0);
			nf++;
		}
		pr = poll(pf, (nfds_t)nf, ms);
#ifdef HAVE_TOX
		if (g_tox && !shutdown_requested() && !g_identity_recovery_required &&
		    !g_identity_primary_uncertain) {
			omaq_tox_iterate(g_tox);
			group_file_pump();
			flush_receipt_acknowledgements();
			expire_group_auth_reservation();
#ifdef HAVE_SIGNAL
			expire_pending_group_invite();
			retry_pending_native_group_invite();
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
			fail_uncertain_primary();
			emit_identity_recovery_state(0);
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
		if (!shutdown_requested() && listen_idx >= 0 &&
		    (pf[listen_idx].revents & POLLIN))
			accept_client();
		for (size_t i = 0; i < g_ncli; ) {
			int fd = g_clients[i];
			short revents = 0;
			for (int k = 0; k < nf; k++) {
				if (pf[k].fd == fd)
					revents |= pf[k].revents;
			}
			if (!shutdown_requested() &&
			    (revents & (POLLIN | POLLHUP | POLLERR)))
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
	group_file_reset();
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
	{
		char runtime_path[512];
		const char *runtime_names[] = { "omaq.sock", "omaq.pid", "omaq.protocol" };
		for (size_t i = 0; i < sizeof(runtime_names) / sizeof(runtime_names[0]); i++)
			if (snprintf(runtime_path, sizeof(runtime_path), "%s/%s", state_dir(),
				     runtime_names[i]) < (int)sizeof(runtime_path))
				(void)unlink(runtime_path);
	}
	if (g_lockfd >= 0)
		close(g_lockfd);
	if (g_state_lockfd >= 0)
		close(g_state_lockfd);
	return g_fatal_io || g_identity_recovery_required ? 1 : 0;
}
