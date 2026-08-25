#define _DEFAULT_SOURCE
#include "../helper/conversation.h"
#include "../helper/avatar.h"
#include "../helper/file.h"
#include "../helper/group.h"
#include "../helper/group_invite.h"
#include "../helper/identity.h"
#include "../helper/invite.h"
#include "../helper/json_io.h"
#include "../helper/line_reader.h"
#include "../helper/message_action.h"
#include "../helper/message.h"
#include "../helper/qr.h"
#include "../helper/ratchet.h"
#include "../helper/ratchet_pin.h"
#include "../helper/presence.h"
#include "../helper/receipt.h"
#include "../helper/rate.h"
#include "../helper/roles.h"
#include "../helper/safety.h"
#include "../helper/store.h"
#include "../helper/surface.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;

static void fail(const char *msg)
{
	fprintf(stderr, "FAIL: %s\n", msg);
	fails++;
}

static int read_file(const char *path, char *buf, size_t n)
{
	FILE *f = fopen(path, "r");
	size_t got;
	if (!f)
		return -1;
	got = fread(buf, 1, n - 1, f);
	fclose(f);
	buf[got] = '\0';
	while (got && (buf[got - 1] == '\n' || buf[got - 1] == '\r'))
		buf[--got] = '\0';
	return 0;
}

static void test_invite_file(const char *path)
{
	char body[1024];
	char url[1024];
	char expect[128];
	char *nl;
	omaq_invite inv;
	int rc;

	if (read_file(path, body, sizeof(body)) != 0) {
		fail(path);
		return;
	}
	nl = strchr(body, '\n');
	if (!nl) {
		fail(path);
		return;
	}
	*nl = '\0';
	snprintf(url, sizeof(url), "%s", body);
	snprintf(expect, sizeof(expect), "%s", nl + 1);
	rc = omaq_invite_parse(url, &inv);
	if (strcmp(expect, "err") == 0) {
		if (rc == 0)
			fail(path);
		return;
	}
	if (rc != 0) {
		fail(path);
		return;
	}
	if (strncmp(expect, "direct ", 7) == 0) {
		char id[80], rk[80];
		int n = sscanf(expect + 7, "%79s %79s", id, rk);
		if (inv.kind != INVITE_DIRECT || strcmp(inv.id, id) != 0)
			fail(path);
		else if (n == 2 && strcmp(inv.rk, rk) != 0)
			fail(path);
		return;
	}
	if (strncmp(expect, "group", 5) == 0) {
		if (inv.kind != INVITE_GROUP)
			fail(path);
		return;
	}
	fail(path);
}

static void test_invites(void)
{
	DIR *d = opendir("tests/gold/invite");
	struct dirent *e;
	if (!d) {
		fail("open tests/gold/invite");
		return;
	}
	while ((e = readdir(d))) {
		char path[512];
		size_t n = strlen(e->d_name);
		if (n < 5 || strcmp(e->d_name + n - 4, ".txt") != 0)
			continue;
		snprintf(path, sizeof(path), "tests/gold/invite/%s", e->d_name);
		test_invite_file(path);
	}
	closedir(d);
}

static void test_roles(void)
{
	DIR *d = opendir("tests/gold/roles");
	struct dirent *e;
	if (!d) {
		fail("open tests/gold/roles");
		return;
	}
	while ((e = readdir(d))) {
		char path[512], body[256], actor[16], act[16], tgt[16];
		int may, want;
		omaq_role ar, tr;
		omaq_action ac;
		size_t n = strlen(e->d_name);
		if (n < 5 || strcmp(e->d_name + n - 4, ".txt") != 0)
			continue;
		snprintf(path, sizeof(path), "tests/gold/roles/%s", e->d_name);
		if (read_file(path, body, sizeof(body)) != 0) {
			fail(path);
			continue;
		}
		if (sscanf(body, "%15s %15s %15s %d", actor, act, tgt, &want) != 4) {
			fail(path);
			continue;
		}
		if (omaq_role_parse(actor, &ar) != 0 || omaq_action_parse(act, &ac) != 0 ||
		    omaq_role_parse(tgt, &tr) != 0) {
			fail(path);
			continue;
		}
		may = omaq_role_may(ar, ac, tr) ? 1 : 0;
		if (may != want)
			fail(path);
	}
	closedir(d);
}

static void test_json(void)
{
	omaq_op op;
	if (omaq_json_parse_op("{\"op\":\"status\"}", &op) != 0 || strcmp(op.op, "status") != 0)
		fail("json status");
	if (omaq_json_parse_op("{\"op\":\"invite.create\",\"ttlSec\":86400,\"kind\":\"direct\"}", &op) != 0)
		fail("json create");
	if (omaq_json_parse_op("{\"op\":\"typing.set\",\"conversation\":\"7\",\"typing\":true}", &op) != 0 ||
	    !op.has_typing || !op.typing || strcmp(op.conversation, "7") != 0)
		fail("json typing");
	if (omaq_json_parse_op("{\"op\":\"nickname.set\",\"nickname\":\"Alice\"}", &op) != 0 ||
	    strcmp(op.nickname, "Alice") != 0)
		fail("json nickname");
	if (omaq_json_parse_op("{\"op\":\"message.react\",\"id\":\"msg-1\",\"text\":\"\"}", &op) != 0 ||
	    !op.has_text || strcmp(op.text, "") != 0)
		fail("json empty reaction text");
	if (omaq_json_parse_op("{\"op\":\"contact.remove\",\"id\":\"7\",\"key\":\"abcdef\",\"request\":\"gi-test-1\"}", &op) != 0 ||
	    strcmp(op.id, "7") != 0 || strcmp(op.key, "abcdef") != 0 ||
	    strcmp(op.request, "gi-test-1") != 0)
		fail("json contact key");
	if (omaq_json_parse_op("{\"op\":\"nope\"}", &op) != 0)
		fail("json unknown op still parses");
	if (omaq_json_parse_op("{", &op) == 0)
		fail("json incomplete");
	if (omaq_json_parse_op("{\"foo\":1}", &op) == 0)
		fail("json unknown key");
	if (omaq_json_parse_op("{\"op\":\"x\",\"ttlSec\":99999999999999999999}", &op) == 0)
		fail("json ttl overflow");
	if (omaq_json_parse_op("{\"op\":\"x\",\"limit\":2147483648}", &op) == 0)
		fail("json limit overflow");
	if (omaq_json_parse_op("{\"op\":\"sta\rtus\"}", &op) == 0)
		fail("json embedded carriage return");
	if (omaq_json_validate("{\"id\":\"ok\",\"items\":[true,null,-1.5e2]}") != 0 ||
	    omaq_json_validate("{bad}") == 0 ||
	    omaq_json_validate("{\"id\":01}") == 0 ||
	    omaq_json_validate("{\"id\":\"\xc0\x80\"}") == 0)
		fail("json history validation");
}

struct line_capture {
	char lines[4][128];
	int count;
};

static int capture_line(char *line, void *ctx)
{
	struct line_capture *capture = ctx;

	if (capture->count >= 4 || strlen(line) >= sizeof(capture->lines[0]))
		return -1;
	snprintf(capture->lines[capture->count], sizeof(capture->lines[0]), "%s", line);
	capture->count++;
	return 0;
}

static void test_fragmented_stdin(void)
{
	omaq_line_reader reader;
	struct line_capture capture = { 0 };
	char oversized[OMAQ_JSON_LINE_MAX + 32];

	omaq_line_reader_init(&reader);
	if (omaq_line_reader_feed(&reader, "{\"op\":\"sta", sizeof("{\"op\":\"sta") - 1,
				  capture_line, &capture) != 0 ||
	    capture.count != 0 ||
	    omaq_line_reader_feed(&reader, "tus\"}\r", sizeof("tus\"}\r") - 1,
				  capture_line, &capture) != 0 ||
	    capture.count != 0 ||
	    omaq_line_reader_feed(&reader, "\n{\"op\":\"his", sizeof("\n{\"op\":\"his") - 1,
				  capture_line, &capture) != 0 ||
	    omaq_line_reader_feed(&reader, "tory\"}\n", sizeof("tory\"}\n") - 1,
				  capture_line, &capture) != 0 ||
	    capture.count != 2 || strcmp(capture.lines[0], "{\"op\":\"status\"}") != 0 ||
	    strcmp(capture.lines[1], "{\"op\":\"history\"}") != 0)
		fail("fragmented stdin lines");

	memset(oversized, 'x', sizeof(oversized));
	oversized[sizeof(oversized) - 2] = '\n';
	oversized[sizeof(oversized) - 1] = '\0';
	if (omaq_line_reader_feed(&reader, "{\"op\":\"sta\rtus\"}\n",
				  sizeof("{\"op\":\"sta\rtus\"}\n") - 1,
				  capture_line, &capture) != 0 ||
	    capture.count != 3 || strchr(capture.lines[2], '\r') == NULL)
		fail("embedded stdin carriage return preserved");
	capture.count = 2;
	if (omaq_line_reader_feed(&reader, oversized, sizeof(oversized) - 1,
				  capture_line, &capture) != 0 ||
	    omaq_line_reader_feed(&reader, "{\"op\":\"status\"}\n",
				  sizeof("{\"op\":\"status\"}\n") - 1,
				  capture_line, &capture) != 0 ||
	    capture.count != 3 || strcmp(capture.lines[2], "{\"op\":\"status\"}") != 0)
		fail("oversized fragmented stdin recovery");
	{
		static const char nul_line[] = "{\"op\":\"status\"}\0garbage\n";
		if (omaq_line_reader_feed(&reader, nul_line, sizeof(nul_line) - 1,
					  capture_line, &capture) != 0 || capture.count != 3 ||
		    omaq_line_reader_feed(&reader, "{\"op\":\"status\"}\n",
					  sizeof("{\"op\":\"status\"}\n") - 1,
					  capture_line, &capture) != 0 || capture.count != 4)
			fail("stdin NUL line discarded and recovered");
	}
}

static void test_store(void)
{
	char dir[] = "/tmp/omaq-store-XXXXXX";
	char *out = NULL;
	size_t n = 0;
	int i;

	if (!mkdtemp(dir)) {
		fail("mkdtemp");
		return;
	}
	for (i = 0; i < 5; i++) {
		char line[32];
		snprintf(line, sizeof(line), "{\"n\":%d}", i);
		if (omaq_store_append(dir, "c1", line) != 0)
			fail("store append");
	}
	if (omaq_store_append(dir, "c2", "{\"keep\":true}") != 0)
		fail("store second conversation");
	if (omaq_store_tail(dir, "c1", 2, &out, &n) != 0)
		fail("store tail");
	else if (!out || !strstr(out, "\"n\":3") || !strstr(out, "\"n\":4") || strstr(out, "\"n\":2"))
		fail("store tail content");
	free(out);
	if (omaq_message_append(dir, "c1", "me", "hi", "out") != 0)
		fail("message append");
	if (omaq_message_append(dir, "c1", "me", "say \"hi\" \\ok", "out") != 0)
		fail("message escape append");
	{
		char id[64], actor[65], escaped_text[1400], *updated = NULL;
		size_t updated_n = 0;
		if (omaq_message_append_with_id(dir, "c1", "me", "editable", "out", id, sizeof(id)) != 0 ||
		    omaq_message_edit(dir, "c1", id, "edited") != 0 ||
		    omaq_store_update_receipt_changed(dir, "c1", id, "delivered") != 1 ||
		    omaq_store_update_receipt_changed(dir, "c1", id, "delivered") != 0 ||
		    omaq_store_update_receipt_changed(dir, "c1", id, "read") != 1 ||
		    omaq_store_update_receipt_changed(dir, "c1", id, "read") != 0 ||
		    omaq_store_update_receipt_changed(dir, "c1", id, "delivered") != 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"text\":\"edited\"") || !strstr(updated, "\"edited\":true") ||
		    !strstr(updated, "\"receipt\":\"read\"}"))
			fail("message edit");
		free(updated);
		updated = NULL;
		{
			static const char marker[] = "\"reaction_group_";
			size_t used = 0;
			for (int marker_index = 0; marker_index < 32; marker_index++) {
				memcpy(escaped_text + used, marker, sizeof(marker) - 1);
				used += sizeof(marker) - 1;
			}
			memset(escaped_text + used, '"', sizeof(escaped_text) - 1 - used);
			escaped_text[sizeof(escaped_text) - 1] = '\0';
		}
		if (omaq_message_edit(dir, "c1", id, escaped_text) != 0)
			fail("message maximal escape edit");
		if (omaq_store_message_exists(dir, "c1", id) != 1 ||
		    omaq_message_append_id(dir, "c1", "peer", "collision", "in", id) == 0 ||
		    omaq_store_update_reaction(dir, "c1", id, "❤️", "me") != 0 ||
		    omaq_store_update_reaction(dir, "c1", id, "🔥", "peer") != 0 ||
		    omaq_store_update_reaction(dir, "c1", id, "🔥", "peer") != 0 ||
		    omaq_store_update_group_reaction(dir, "c1", id, "👍",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0 ||
		    omaq_store_update_group_reaction(dir, "c1", id, "🎉",
			"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") != 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"reaction_me\":\"❤️\"") ||
		    !strstr(updated, "\"reaction_peer\":\"🔥\"") ||
		    !strstr(updated, "\"reaction_group_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\":\"👍\"") ||
		    !strstr(updated, "\"reaction_group_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\":\"🎉\"") ||
		    omaq_store_message_exists(dir, "c1", "missing") != 0 ||
		    omaq_store_update_reaction(dir, "c1", "missing", "👍", "me") != -2)
			fail("message reaction store");
		free(updated);
		updated = NULL;
		for (int actor_index = 1; actor_index <= 30; actor_index++) {
			snprintf(actor, sizeof(actor), "%064x", (unsigned int)actor_index);
			if (omaq_store_update_group_reaction(dir, "c1", id, "👍", actor) != 0)
				fail("group reaction actor bound allowance");
		}
		snprintf(actor, sizeof(actor), "%064x", 31u);
		if (omaq_store_update_group_reaction(dir, "c1", id, "👍", actor) != -1 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"reaction_group_000000000000000000000000000000000000000000000000000000000000001e\":\"👍\""))
			fail("group reaction actor bound");
		free(updated);
		updated = NULL;
		if (omaq_store_update_group_reaction(dir, "c1", id, "",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated,
			"\"reaction_group_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\":\"\"") ||
		    !strstr(updated,
			"\"reaction_group_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\":\"🎉\""))
			fail("group reaction isolation");
		free(updated);
		if (omaq_store_append(dir, "reaction-bad", "{\"id\":\"broken-1\",\"text\":\"broken\"") != 0 ||
		    omaq_store_update_reaction(dir, "reaction-bad", "broken-1", "👍", "peer") != -1)
			fail("message malformed reaction store");
		{
			char truncated_path[512];
			FILE *truncated;
			if (snprintf(truncated_path, sizeof(truncated_path),
				     "%s/history/reaction-bad/messages.jsonl", dir) >=
				    (int)sizeof(truncated_path) ||
			    !(truncated = fopen(truncated_path, "a"))) {
				fail("truncated history fixture");
			} else {
				fputs("partial", truncated);
				fclose(truncated);
				for (int retry = 0; retry < 100; retry++)
					if (omaq_store_message_id_used(dir, "reaction-bad",
							       "broken-1") != -1)
						fail("truncated history rejection");
			}
		}
		if (omaq_message_append_id(dir, "c1", "peer", "peer text", "in", "peer-1") != 0 ||
		    omaq_message_apply_delete(dir, "c1", "peer-1") != 0 ||
		    omaq_message_append_id(dir, "c1", "peer", "collision", "in", "peer-1") == 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"id\":\"peer-1\"") || !strstr(updated, "\"deleted\":true"))
			fail("message delete");
		free(updated);
		updated = NULL;
		if (omaq_message_append_file_with_id(dir, "c1", "peer", "song.mp3", "in",
						     id, sizeof(id)) == 0)
			fail("file message path validation");
		if (omaq_message_append_id(dir,
			"g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"peer", "old group", "in", "group-old-1") != 0 ||
		    omaq_store_message_exists(dir,
			"g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"group-old-1") != 1 ||
		    omaq_store_message_exists(dir,
			"g:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
			"group-old-1") != 0)
			fail("stable group history isolation");
		if (omaq_message_append_file_with_id(dir, "c1", "peer", "/tmp/song.mp3", "in",
						     id, sizeof(id)) != 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"text\":\"/tmp/song.mp3\"") ||
		    !strstr(updated, "\"kind\":\"file\""))
			fail("file message kind");
		free(updated);
	}
	{
		char live[640], rot[640];
		char *out2 = NULL;
		size_t n2 = 0;
		if (snprintf(live, sizeof(live), "%s/history/c1/messages.jsonl", dir) >= (int)sizeof(live))
			fail("store rotate path");
		else if (snprintf(rot, sizeof(rot), "%s.1", live) >= (int)sizeof(rot))
			fail("store rotate dest");
		else if (rename(live, rot) != 0)
			fail("store rotate rename");
		else if (omaq_store_append(dir, "c1", "{\"n\":99}") != 0)
			fail("store append after rotate");
		else if (omaq_store_tail(dir, "c1", 6, &out2, &n2) != 0)
			fail("store tail after rotate");
		else if (!out2 || !strstr(out2, "\"n\":99") || !strstr(out2, "say \\\"hi\\\""))
			fail("store tail rotated content");
		free(out2);
	}
	{
		omaq_store_message_id *ids = NULL;
		omaq_receipt_outbox outbox, loaded;
		size_t count = 0;
		omaq_receipt_outbox_init(&outbox);
		omaq_receipt_outbox_init(&loaded);
		if (omaq_store_append(dir, "7",
			"{\"id\":\"peer-read-1\",\"from\":\"peer\",\"text\":\"one\",\"dir\":\"in\"}") != 0 ||
		    omaq_store_append(dir, "7",
			"{\"id\":\"mine-1\",\"from\":\"me\",\"text\":\"out\",\"dir\":\"out\"}") != 0 ||
		    omaq_store_append(dir, "7",
			"{\"id\":\"local-file-1\",\"from\":\"peer\",\"text\":\"/tmp/a\",\"dir\":\"in\",\"kind\":\"file\"}") != 0 ||
		    omaq_store_append(dir, "7",
			"{\"id\":\"peer-read-2\",\"from\":\"member:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"text\":\"two\",\"dir\":\"in\"}") != 0 ||
		    omaq_store_unread_receipt_ids(dir, "7", 2, &ids, &count) != 0 ||
		    count != 1 || strcmp(ids[0].id, "peer-read-2") != 0 ||
		    omaq_store_message_from_matches(dir, "7", "peer-read-2",
			"member:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 1 ||
		    omaq_store_message_from_matches(dir, "7", "peer-read-2", "peer") != 0)
			fail("authoritative unread receipt ids");
		free(ids);
		ids = NULL;
		if (omaq_store_unread_receipt_ids(dir, "7", 5, &ids, &count) != 0 ||
		    count != 2)
			fail("unread receipt history underflow recovery");
		free(ids);
		ids = NULL;
		{
			char unread_history[640];
			if (snprintf(unread_history, sizeof(unread_history),
				     "%s/history/7/messages.jsonl", dir) >= (int)sizeof(unread_history) ||
			    chmod(unread_history, 0000) != 0 ||
			    omaq_store_unread_receipt_ids(dir, "7", 1, &ids, &count) == 0 ||
			    chmod(unread_history, 0600) != 0)
				fail("unread receipt history open failure");
		}
		for (size_t overflow_index = 0;
		     overflow_index <= OMAQ_STORE_READ_IDS_MAX; overflow_index++) {
			char overflow_line[192];
			snprintf(overflow_line, sizeof(overflow_line),
				 "{\"id\":\"overflow-%zu\",\"from\":\"peer\",\"text\":\"x\",\"dir\":\"in\"}",
				 overflow_index);
			if (omaq_store_append(dir, "8", overflow_line) != 0) {
				fail("receipt overflow history fixture");
				break;
			}
		}
		if (omaq_store_unread_receipt_ids(dir, "8",
			OMAQ_STORE_READ_IDS_MAX + 1u, &ids, &count) == 0)
			fail("receipt debt overflow accepted");
		free(ids);
		ids = NULL;
		if (omaq_receipt_outbox_add(&outbox, "7", "peer-read-2") != 1 ||
		    omaq_receipt_outbox_add(&outbox, "7", "peer-read-2") != 0 ||
		    omaq_receipt_outbox_add(&outbox, "07", "peer-read-3") >= 0 ||
		    omaq_receipt_outbox_save(&outbox, dir) != 0 ||
		    omaq_receipt_outbox_load(&loaded, dir) != 0 || loaded.length != 1 ||
		    omaq_receipt_transaction_save(&loaded, dir) != 0 ||
		    omaq_receipt_transaction_committed(dir) != 0 ||
		    omaq_receipt_transaction_mark_committed(dir) != 0 ||
		    omaq_receipt_transaction_committed(dir) != 1 ||
		    omaq_receipt_outbox_remove(&loaded, "7", "peer-read-2") != 1 ||
		    loaded.length != 0 || omaq_receipt_transaction_load(&loaded, dir) != 0 ||
		    loaded.length != 1 || omaq_receipt_transaction_clear(dir) != 0 ||
		    omaq_receipt_transaction_load(&loaded, dir) != 0 || loaded.length != 0)
			fail("persistent receipt outbox transaction");
		omaq_receipt_outbox_destroy(&outbox);
		omaq_receipt_outbox_destroy(&loaded);
	}
	if (omaq_store_clear(dir, "c1") != 0)
		fail("store clear return");
	else {
		char c1[640], c2[640];
		snprintf(c1, sizeof(c1), "%s/history/c1/messages.jsonl", dir);
		snprintf(c2, sizeof(c2), "%s/history/c2/messages.jsonl", dir);
		if (access(c1, F_OK) == 0 || access(c2, F_OK) != 0)
			fail("store clear scope");
	}
}

static void test_mutate(void)
{
	DIR *d = opendir("tests/gold/invite/mutate");
	struct dirent *e;
	if (!d)
		return;
	while ((e = readdir(d))) {
		char path[512], body[8192];
		omaq_invite inv;
		size_t n = strlen(e->d_name);
		if (e->d_name[0] == '.')
			continue;
		(void)n;
		snprintf(path, sizeof(path), "tests/gold/invite/mutate/%s", e->d_name);
		if (read_file(path, body, sizeof(body)) != 0)
			continue;
		if (omaq_invite_parse(body, &inv) == 0)
			fail(path);
	}
	closedir(d);
}

static int unread_available_fixture(const char *conversation, void *userdata)
{
	int fail_closed = userdata ? *(int *)userdata : 0;

	if (fail_closed && strcmp(conversation, "1") == 0)
		return -1;
	return conversation[0] == 'g' ? 0 : 1;
}

static void test_conv(void)
{
	omaq_conv c;
	omaq_unread_state unread, loaded;
	char dir[] = "/tmp/omaq-unread-XXXXXX", path[256];
	FILE *f;
	int i;

	omaq_conv_init(&c, "x", CONV_DIRECT);
	omaq_conv_note(&c, "hello", 1);
	if (c.unread != 1 || strcmp(c.last, "hello") != 0)
		fail("conv note");
	if (!mkdtemp(dir)) {
		fail("unread mkdtemp");
		return;
	}
	omaq_unread_init(&unread);
	omaq_unread_init(&loaded);
	if (omaq_unread_increment(&unread, "0") != 0 ||
	    omaq_unread_increment(&unread, "0") != 0 ||
	    omaq_unread_increment(&unread,
		"g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0 ||
	    omaq_unread_increment(&unread, "../bad") == 0 ||
	    omaq_unread_increment(&unread, "01") == 0 ||
	    omaq_unread_increment(&unread, "g01") == 0 ||
	    omaq_unread_increment(&unread,
		"g:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") == 0 ||
	    omaq_unread_increment(&unread, "4294967296") == 0 ||
	    omaq_unread_increment(&unread, "g4294967296") == 0 ||
	    omaq_unread_total(&unread) != 3 || omaq_unread_count(&unread, "0") != 2 ||
	    omaq_store_unread_save(&unread, dir) != 0 ||
	    omaq_store_unread_load(&loaded, dir) != 0 ||
	    loaded.length != 2 || omaq_unread_total(&loaded) != 3 ||
	    omaq_unread_clear(&loaded, "0") != 0 || omaq_unread_total(&loaded) != 1)
		fail("unread state");
	{
		omaq_unread_state pruned;
		int fail_closed = 1;

		omaq_unread_init(&pruned);
		if (omaq_unread_increment(&pruned, "0") != 0 ||
		    omaq_unread_increment(&pruned,
			"g:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") != 0 ||
		    omaq_unread_prune(&pruned, unread_available_fixture, NULL) != 1 ||
		    pruned.length != 1 || omaq_unread_total(&pruned) != 1 ||
		    omaq_unread_increment(&pruned, "1") != 0 ||
		    omaq_unread_prune(&pruned, unread_available_fixture, &fail_closed) != -1 ||
		    pruned.length != 2 || omaq_unread_total(&pruned) != 2)
			fail("unread unavailable prune");
		omaq_unread_destroy(&pruned);
	}
	if (snprintf(path, sizeof(path), "%s/unread.tsv", dir) >= (int)sizeof(path) ||
	    !(f = fopen(path, "w"))) {
		fail("unread malformed fixture");
		return;
	}
	fputs("0\t1\n0\t2\n", f);
	fclose(f);
	if (omaq_store_unread_load(&loaded, dir) == 0 || loaded.length != 0)
		fail("unread duplicate rejection");
	f = fopen(path, "w");
	if (!f)
		fail("unread legacy fixture");
	else {
		fputs("g7\t9\n0\t1\n", f);
		fclose(f);
		if (omaq_store_unread_load(&loaded, dir) != 0 || loaded.length != 1 ||
		    omaq_unread_total(&loaded) != 1)
			fail("unread legacy group discard");
	}
	omaq_unread_destroy(&loaded);
	omaq_unread_init(&loaded);
	for (i = 0; i < 200; i++) {
		char conversation[32];
		snprintf(conversation, sizeof(conversation), "%d", i + 100);
		if (omaq_unread_increment(&loaded, conversation) != 0) {
			fail("unread dynamic capacity");
			break;
		}
	}
	omaq_unread_destroy(&loaded);
	omaq_unread_init(&loaded);
	f = fopen(path, "w");
	if (!f)
		fail("unread numeric fixture");
	else {
		fputs("0\t+1\n", f);
		fclose(f);
		if (omaq_store_unread_load(&loaded, dir) == 0 || loaded.length != 0)
			fail("unread numeric rejection");
	}
	unlink(path);
	if (symlink("/dev/null", path) != 0 || omaq_store_unread_load(&loaded, dir) == 0)
		fail("unread symlink rejection");
	unlink(path);
	if (mkfifo(path, 0600) != 0 || omaq_store_unread_load(&loaded, dir) == 0)
		fail("unread fifo rejection");
	unlink(path);
	f = fopen(path, "w");
	if (!f || ftruncate(fileno(f), 1024 * 1024 + 1) != 0) {
		if (f)
			fclose(f);
		fail("unread oversized fixture");
	} else {
		fclose(f);
		if (omaq_store_unread_load(&loaded, dir) == 0)
			fail("unread oversized rejection");
	}
	omaq_unread_destroy(&unread);
	omaq_unread_destroy(&loaded);
}

static void test_search(void)
{
	char dir[] = "/tmp/omaq-search-XXXXXX";
	char *out = NULL;
	size_t n = 0;

	if (!mkdtemp(dir)) {
		fail("search mkdtemp");
		return;
	}
	if (omaq_store_append(dir, "c1", "{\"text\":\"alpha one\"}") != 0 ||
	    omaq_store_append(dir, "c1", "{\"text\":\"bravo two\"}") != 0 ||
	    omaq_store_append(dir, "c1", "{\"text\":\"ALPHA three\"}") != 0)
		fail("search append");
	if (omaq_store_search(dir, "c1", "alpha", 20, &out, &n) != 0)
		fail("search");
	else if (!out || !strstr(out, "alpha one") || !strstr(out, "ALPHA three") ||
		 strstr(out, "bravo"))
		fail("search hits");
	free(out);
}

static void test_identity_files(void)
{
	char dir[] = "/tmp/omaq-id-XXXXXX";
	char src[256], dst[256], other[256], linkpath[256], temp_path[320];
	char registry[256], bundle[256], fresh[256], fresh_tox[300], fresh_registry[300];
	char tox_alias[300];
	FILE *f;

	if (!mkdtemp(dir)) {
		fail("id mkdtemp");
		return;
	}
	if (snprintf(src, sizeof(src), "%s/tox.save", dir) >= (int)sizeof(src) ||
	    snprintf(dst, sizeof(dst), "%s/backup.save", dir) >= (int)sizeof(dst) ||
	    snprintf(other, sizeof(other), "%s/other.save", dir) >= (int)sizeof(other) ||
	    snprintf(linkpath, sizeof(linkpath), "%s/link.save", dir) >= (int)sizeof(linkpath) ||
	    snprintf(registry, sizeof(registry), "%s/groups.tsv", dir) >= (int)sizeof(registry) ||
	    snprintf(bundle, sizeof(bundle), "%s/bundle.save", dir) >= (int)sizeof(bundle) ||
	    snprintf(fresh, sizeof(fresh), "%s/fresh", dir) >= (int)sizeof(fresh) ||
	    snprintf(fresh_tox, sizeof(fresh_tox), "%s/tox.save", fresh) >= (int)sizeof(fresh_tox) ||
	    snprintf(fresh_registry, sizeof(fresh_registry), "%s/groups.tsv", fresh) >=
		    (int)sizeof(fresh_registry) ||
	    snprintf(tox_alias, sizeof(tox_alias), "%s/./tox.save", dir) >=
		    (int)sizeof(tox_alias) ||
	    snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld", linkpath, (long)getpid()) >=
		    (int)sizeof(temp_path)) {
		fail("id path");
		return;
	}
	f = fopen(src, "w");
	if (!f) {
		fail("id write");
		return;
	}
	fputs("SAVE-A", f);
	fclose(f);
	if (omaq_identity_export(dir, dst) != 0)
		fail("id export");
	f = fopen(registry, "w");
	if (!f) {
		fail("id registry write");
		return;
	}
	fputs("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tGroup\n", f);
	fclose(f);
	if (omaq_identity_bundle_export(dir, src) == 0 ||
	    omaq_identity_bundle_export(dir, tox_alias) == 0 ||
	    omaq_identity_bundle_export(dir, registry) == 0)
		fail("identity bundle source alias");
	if (mkdir(fresh, 0700) != 0 ||
	    omaq_identity_bundle_export(dir, bundle) != 0 ||
	    omaq_identity_bundle_import(fresh, bundle, 0) != 0)
		fail("identity bundle roundtrip");
	else {
		char copied[256];
		if (read_file(fresh_tox, copied, sizeof(copied)) != 0 ||
		    strcmp(copied, "SAVE-A") != 0 ||
		    read_file(fresh_registry, copied, sizeof(copied)) != 0 ||
		    !strstr(copied, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tGroup"))
			fail("identity bundle group registry");
	}
	if (omaq_identity_export_exclusive(dir, dst) == 0 ||
	    omaq_identity_export_exclusive(dir, other) != 0)
		fail("id exclusive export");
	f = fopen(temp_path, "w");
	if (!f) {
		fail("id preexisting temp fixture");
		return;
	}
	fputs("KEEP", f);
	fclose(f);
	if (omaq_identity_export(dir, linkpath) == 0 || access(temp_path, F_OK) != 0)
		fail("id preexisting temp preserved");
	unlink(temp_path);
	if (omaq_identity_import(dir, dst, 0) != 1)
		fail("id exists");
	f = fopen(other, "w");
	if (!f) {
		fail("id other");
		return;
	}
	fclose(f);
	if (omaq_identity_import(dir, other, 1) == 0)
		fail("id empty import");
	f = fopen(other, "w");
	if (!f || ftruncate(fileno(f), (off_t)OMAQ_IDENTITY_FILE_MAX + 1) != 0) {
		if (f)
			fclose(f);
		fail("id oversized fixture");
		return;
	}
	fclose(f);
	if (omaq_identity_import(dir, other, 1) == 0)
		fail("id oversized import");
	unlink(other);
	if (mkfifo(other, 0600) != 0 || omaq_identity_import(dir, other, 1) == 0 ||
	    omaq_identity_export(dir, other) == 0)
		fail("id fifo path rejection");
	if (symlink(other, linkpath) != 0 || omaq_identity_import(dir, linkpath, 1) == 0 ||
	    omaq_identity_export(dir, linkpath) == 0)
		fail("id symlink path rejection");
	unlink(linkpath);
	unlink(other);
	f = fopen(other, "w");
	if (!f) {
		fail("id other rewrite");
		return;
	}
	fputs("SAVE-B", f);
	fclose(f);
	if (omaq_identity_import(dir, other, 1) != 0)
		fail("id replace");
}

static void test_pass_ok(void)
{
	if (omaq_identity_pass_ok("") || omaq_identity_pass_ok(NULL))
		fail("pass empty");
	if (omaq_identity_pass_ok("has\nnl"))
		fail("pass newline");
	if (!omaq_identity_pass_ok("ok-pass-1"))
		fail("pass ok");
}

static void test_expire(void)
{
	omaq_invite inv;

	memset(&inv, 0, sizeof(inv));
	inv.expiry = 10;
	if (!omaq_invite_expired(&inv, 10))
		fail("expire at e");
	if (!omaq_invite_expired(&inv, 11))
		fail("expire after e");
	if (omaq_invite_expired(&inv, 9))
		fail("expire before e");
}

static void test_rate_gold(void)
{
	char body[1024];
	char *line;
	omaq_rate r;

	if (read_file("tests/gold/rate/same-key-sixth.txt", body, sizeof(body)) != 0) {
		fail("rate gold missing");
		return;
	}
	omaq_rate_init(&r);
	line = body;
	while (*line) {
		char *nl = strchr(line, '\n');
		long t;
		char key[32];
		char want[16];
		int rc;
		if (nl)
			*nl = '\0';
		if (line[0] && line[0] != '#') {
			if (sscanf(line, "%ld %31s %15s", &t, key, want) != 3) {
				fail("rate gold parse");
			} else {
				rc = omaq_rate_allow(&r, key, (int64_t)t);
				if (strcmp(want, "allow") == 0 && rc != 0)
					fail("rate gold allow");
				if (strcmp(want, "deny") == 0 && rc == 0)
					fail("rate gold deny");
			}
		}
		if (!nl)
			break;
		line = nl + 1;
	}
}

static void test_rate_hour(void)
{
	omaq_rate r;
	int i;
	char key[16];

	omaq_rate_init(&r);
	for (i = 0; i < OMAQ_RATE_PER_HOUR; i++) {
		snprintf(key, sizeof(key), "k%d", i);
		if (omaq_rate_allow(&r, key, 1000 + i) != 0)
			fail("rate hour allow");
	}
	if (omaq_rate_allow(&r, "extra", 1020) == 0)
		fail("rate hour deny");
	if (omaq_rate_allow(&r, "later", 1000 + 3600) != 0)
		fail("rate hour rolled");
}

static void test_rate_key_only(void)
{
	omaq_rate r;
	int i;

	omaq_rate_init(&r);
	for (i = 0; i < OMAQ_RATE_PER_MIN; i++) {
		if (omaq_rate_allow_key_only(&r, "friend-0", 2000 + i) != 0)
			fail("rate key-only allow");
	}
	if (omaq_rate_allow_key_only(&r, "friend-0", 2005) == 0)
		fail("rate key-only deny");
	if (omaq_rate_allow_key_only(&r, "friend-1", 2005) != 0)
		fail("rate key-only isolation");
}

static void test_control_rate(void)
{
	omaq_control_rate r;
	char actor[65];
	int i;

	omaq_control_rate_init(&r);
	for (i = 0; i < OMAQ_CONTROL_RATE_GLOBAL; i++) {
		snprintf(actor, sizeof(actor), "%064x", (unsigned int)(i + 1));
		if (omaq_control_rate_allow(&r, i % 2 ? 'r' : 'x',
					    (uint32_t)(i % 8), actor, 3000) != 0)
			fail("control rate global allowance");
	}
	snprintf(actor, sizeof(actor), "%064x",
		 (unsigned int)(OMAQ_CONTROL_RATE_GLOBAL + 1));
	if (omaq_control_rate_allow(&r, 'r', 0, actor, 3000) == 0)
		fail("control rate global limit");
	if (omaq_control_rate_allow(&r, 'r', 0, actor, 3060) != 0)
		fail("control rate global reset");

	omaq_control_rate_init(&r);
	memset(actor, 'a', 64);
	actor[64] = '\0';
	for (i = 0; i < OMAQ_CONTROL_RATE_RECEIPT_PER_KEY; i++)
		if (omaq_control_rate_allow(&r, 'r', 7, actor, 4000) != 0)
			fail("control rate actor allowance");
	if (omaq_control_rate_allow(&r, 'r', 7, actor, 4000) == 0)
		fail("control rate actor limit");
	omaq_control_rate_init(&r);
	if (omaq_control_rate_allow(&r, 'x', 7, actor, 4000) != 0)
		fail("control rate kind isolation");
	for (i = 0; i < OMAQ_CONTROL_RATE_PER_KEY; i++)
		if (omaq_control_rate_allow(&r, 'e', 7, actor, 4000) != 0 ||
		    omaq_control_rate_allow(&r, 'd', 7, actor, 4000) != 0)
			fail("control action rate allowance");
	if (omaq_control_rate_allow(&r, 'e', 7, actor, 4000) == 0 ||
	    omaq_control_rate_allow(&r, 'd', 7, actor, 4000) == 0)
		fail("control action rate limit");
}

static void test_safety(void)
{
	char body[512];
	char *nl1, *nl2;
	char a[65], b[65], expect[200];
	char got[OMAQ_SAFETY_MAX];
	char got2[OMAQ_SAFETY_MAX];

	if (read_file("tests/gold/safety/order.txt", body, sizeof(body)) != 0) {
		fail("safety gold missing");
		return;
	}
	nl1 = strchr(body, '\n');
	if (!nl1) {
		fail("safety gold");
		return;
	}
	*nl1 = '\0';
	if (strlen(body) != 64) {
		fail("safety gold a");
		return;
	}
	memcpy(a, body, 65);
	nl2 = strchr(nl1 + 1, '\n');
	if (!nl2) {
		fail("safety gold");
		return;
	}
	*nl2 = '\0';
	if (strlen(nl1 + 1) != 64) {
		fail("safety gold b");
		return;
	}
	memcpy(b, nl1 + 1, 65);
	if (strlen(nl2 + 1) >= sizeof(expect)) {
		fail("safety gold expect");
		return;
	}
	memcpy(expect, nl2 + 1, strlen(nl2 + 1) + 1);
	if (omaq_safety_code(a, b, got, sizeof(got)) != 0)
		fail("safety code");
	else if (strcmp(got, expect) != 0)
		fail("safety gold match");
	if (omaq_safety_code(b, a, got2, sizeof(got2)) != 0 || strcmp(got, got2) != 0)
		fail("safety order");
	if (omaq_safety_code("nope", b, got, sizeof(got)) == 0)
		fail("safety short");
}

static void test_group_invite(void)
{
	int64_t now = 1000;

	if (!omaq_group_invite_match(7, 7, now + 1, now))
		fail("group invite match");
	if (omaq_group_invite_match(7, 8, now + 1, now))
		fail("group invite friend mismatch");
	if (omaq_group_invite_match(7, 7, now, now))
		fail("group invite expiry");
	if (omaq_group_invite_match(UINT32_MAX, 7, now + 1, now))
		fail("group invite without redemption");
}

static void test_ratchet_pins(void)
{
	char dir[] = "/tmp/omaq-rk-XXXXXX";
	char got[OMAQ_RK_HEX + 1], conv[16], path[512];
	struct stat st;
	int i;

	if (!mkdtemp(dir)) {
		fail("rk mkdtemp");
		return;
	}
	if (omaq_ratchet_pin_get(dir, "0", got, sizeof(got)) != 0)
		fail("rk missing");
	for (i = 0; i < 12; i++) {
		snprintf(conv, sizeof(conv), "%d", i);
		if (omaq_ratchet_pin_set(dir, conv,
				"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0)
			fail("rk set");
	}
	if (omaq_ratchet_pin_get(dir, "11", got, sizeof(got)) != 1 ||
	    strcmp(got, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0)
		fail("rk get persisted");
	if (snprintf(path, sizeof(path), "%s/ratchet/rk/11", dir) >= (int)sizeof(path))
		fail("rk path test");
	else if (stat(path, &st) != 0 || (st.st_mode & 0777) != 0600)
		fail("rk permissions");
	if (omaq_ratchet_pin_set(dir, "../x",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0)
		fail("rk path escape");
}

static void test_group_id(void)
{
	char id[OMAQ_GROUP_ID_MAX], title48[49], title49[50];
	const char embedded_nul[] = { 'a', '\0', 'b' };
	uint8_t group_message[1401];
	const char *chat_a =
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const char *stable_a =
		"g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const char *chat_b =
		"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	const char *stable_b =
		"g:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	uint32_t n;

	memset(title48, 'a', sizeof(title48) - 1);
	title48[sizeof(title48) - 1] = '\0';
	memset(title49, 'b', sizeof(title49) - 1);
	title49[sizeof(title49) - 1] = '\0';
	memset(group_message, 'a', sizeof(group_message));
	if (!omaq_group_message_bytes_ok(group_message, 1399) ||
	    omaq_group_message_bytes_ok(group_message, 1400) ||
	    omaq_group_message_bytes_ok(group_message, 1401))
		fail("group message length validation");
	group_message[10] = '\0';
	if (omaq_group_message_bytes_ok(group_message, 1399))
		fail("group message nul validation");
	group_message[0] = 0xe0;
	group_message[1] = 0xc0;
	group_message[2] = 0x80;
	group_message[10] = 'a';
	if (omaq_group_message_bytes_ok(group_message, 1399))
		fail("group message utf8 validation");
	if (!omaq_group_title_ok("Test room") || !omaq_group_title_ok("Grüppe 🎉") ||
	    !omaq_group_title_ok(title48) || omaq_group_title_ok(title49) ||
	    omaq_group_title_ok("") || omaq_group_title_ok("bad\nroom") ||
	    omaq_group_title_ok("bad\xc0\x80") ||
	    omaq_group_title_ok("bad\xed\xa0\x80") ||
	    omaq_group_title_ok("bad\xc2\x85") ||
	    omaq_group_title_ok("bad\xe0\xc0\x80") ||
	    omaq_group_title_ok("bad\xed\x7f\x80") ||
	    omaq_group_title_ok("bad\xf0\xff\x80\x80") ||
	    omaq_group_title_ok("bad\xf4\x7f\x80\x80") ||
	    omaq_group_title_bytes_ok(embedded_nul, sizeof(embedded_nul)) ||
	    omaq_group_member_name_bytes_ok("bad\xc2\x85", 5) ||
	    omaq_group_member_name_bytes_ok(embedded_nul, sizeof(embedded_nul)))
		fail("group title validation");
	omaq_group_reset();
	if (omaq_group_set_chat_id(0, chat_a) != 0 ||
	    omaq_group_id_format(0, id, sizeof(id)) != 0 || strcmp(id, stable_a) != 0)
		fail("group id format");
	if (omaq_group_id_parse(stable_a, &n) != 0 || n != 0)
		fail("group id parse");
	if (omaq_group_id_parse("g0", &n) == 0 || omaq_group_id_parse("gx", &n) == 0 ||
	    omaq_group_set_chat_id(1,
		"AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") == 0)
		fail("group id validation");
	if (omaq_group_set_title(0, "Test room") != 0)
		fail("group title cache");
	omaq_group_set_limit(0, OMAQ_GROUP_PEERS);
	for (uint32_t peer = 0; peer < OMAQ_GROUP_PEERS; peer++) {
		char key[65], name[32];
		snprintf(key, sizeof(key), "%064x", peer + 1);
		snprintf(name, sizeof(name), "Member %u", peer);
		if (omaq_group_note_member(0, peer, key, name,
					   peer == 0 ? ROLE_OWNER : ROLE_MEMBER,
					   1, peer == 0) != 0)
			fail("group member add");
	}
	if (omaq_group_note_member(0, OMAQ_GROUP_PEERS,
		"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
		"Too many", ROLE_MEMBER, 1, 0) == 0)
		fail("group member cap");
	if (omaq_group_count() != 1 || omaq_group_number_at(0) != 0 ||
	    strcmp(omaq_group_title(0), "Test room") != 0 ||
	    omaq_group_peer_count(0) != OMAQ_GROUP_PEERS ||
	    strcmp(omaq_group_peer_name(0, 0), "Member 0") != 0 ||
	    omaq_group_peer_cached_role(0, 0) != ROLE_OWNER ||
	    !omaq_group_peer_self(0, 0) || !omaq_group_peer_online(0, 1))
		fail("group member snapshot");
	omaq_group_mark_peer_offline(0, 1);
	if (omaq_group_peer_online(0, 1))
		fail("group member offline");
	omaq_group_drop_peer(0, 1);
	if (omaq_group_peer_count(0) != OMAQ_GROUP_PEERS - 1)
		fail("group member remove");
	{
		uint32_t reused_peer = UINT32_MAX;
		if (omaq_group_note_member(0, 1,
			"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
			"Replacement", ROLE_MEMBER, 1, 0) != 0 ||
		    omaq_group_peer_for_key(0,
			"0000000000000000000000000000000000000000000000000000000000000002",
			&reused_peer) == 0 ||
		    omaq_group_peer_for_key(0,
			"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
			&reused_peer) != 0 || reused_peer != 1)
			fail("group stable key peer reuse");
	}
	omaq_group_mark_dissolved(0);
	if (omaq_group_set_chat_id(0, chat_b) != 0 ||
	    omaq_group_id_parse(stable_a, &n) == 0 ||
	    omaq_group_id_parse(stable_b, &n) != 0 || n != 0)
		fail("group handle reuse identity isolation");
	omaq_group_reset();
	if (omaq_group_peer_count(0) != 0 || omaq_group_is_dissolved(0))
		fail("group identity reset");
	for (uint32_t group = 0; group < OMAQ_GROUPS_MAX; group++) {
		if (!omaq_group_can_create() ||
		    omaq_group_set_title(group, "Capacity") != 0)
			fail("group cache capacity fill");
	}
	if (omaq_group_can_create())
		fail("group cache ninth simultaneous");
	omaq_group_mark_dissolved(3);
	if (!omaq_group_can_create() || omaq_group_set_title(OMAQ_GROUPS_MAX, "Reclaimed") != 0 ||
	    omaq_group_count() != OMAQ_GROUPS_MAX)
		fail("group cache reclaim");
	omaq_group_reset();
}

static void test_group_plan(void)
{
	DIR *d = opendir("tests/gold/group");
	struct dirent *e;
	if (!d) {
		fail("open tests/gold/group");
		return;
	}
	while ((e = readdir(d))) {
		char path[512], body[256];
		char *nl1, *nl2;
		char actor[16], roles_s[128], expect[64];
		omaq_role self, roles[8];
		int kick[8], nkick = 0, nroles = 0;
		char *tok;
		size_t n = strlen(e->d_name);
		if (n < 5 || strcmp(e->d_name + n - 4, ".txt") != 0)
			continue;
		snprintf(path, sizeof(path), "tests/gold/group/%s", e->d_name);
		if (read_file(path, body, sizeof(body)) != 0) {
			fail(path);
			continue;
		}
		nl1 = strchr(body, '\n');
		if (!nl1) {
			fail(path);
			continue;
		}
		*nl1 = '\0';
		if (strlen(body) >= sizeof(actor)) {
			fail(path);
			continue;
		}
		memcpy(actor, body, strlen(body) + 1);
		nl2 = strchr(nl1 + 1, '\n');
		if (!nl2) {
			fail(path);
			continue;
		}
		*nl2 = '\0';
		if (strlen(nl1 + 1) >= sizeof(roles_s) || strlen(nl2 + 1) >= sizeof(expect)) {
			fail(path);
			continue;
		}
		memcpy(roles_s, nl1 + 1, strlen(nl1 + 1) + 1);
		memcpy(expect, nl2 + 1, strlen(nl2 + 1) + 1);
		if (omaq_role_parse(actor, &self) != 0) {
			fail(path);
			continue;
		}
		tok = strtok(roles_s, " ");
		while (tok && nroles < 8) {
			if (omaq_role_parse(tok, &roles[nroles]) != 0) {
				fail(path);
				nroles = -1;
				break;
			}
			nroles++;
			tok = strtok(NULL, " ");
		}
		if (nroles < 0)
			continue;
		if (strcmp(expect, "err") == 0) {
			if (omaq_group_dissolve_plan(self, roles, nroles, kick, &nkick) == 0)
				fail(path);
			continue;
		}
		if (omaq_group_dissolve_plan(self, roles, nroles, kick, &nkick) != 0) {
			fail(path);
			continue;
		}
		{
			char got[64] = "";
			int i;
			for (i = 0; i < nroles; i++) {
				int k, hit = 0;
				for (k = 0; k < nkick; k++) {
					if (kick[k] == i)
						hit = 1;
				}
				if (got[0])
					strcat(got, " ");
				strcat(got, hit ? "1" : "0");
			}
			if (strcmp(got, expect) != 0)
				fail(path);
		}
	}
	closedir(d);
}

static void test_surface(void)
{
	char dir[] = "/tmp/omaq-surf-XXXXXX";
	omaq_surface s, g, listed[2];
	int listed_n;

	if (!mkdtemp(dir)) {
		fail("surface mkdtemp");
		return;
	}
	memset(&s, 0, sizeof(s));
	memcpy(s.conversation, "0", 2);
	memcpy(s.monitor, "DP-1", 5);
	s.x = 12;
	s.y = 34;
	s.pinned = 0;
	if (omaq_surface_set(dir, &s) != 0)
		fail("surface set");
	if (omaq_surface_get(dir, "0", &g) != 0)
		fail("surface get");
	else if (g.x != 12 || g.y != 34 || strcmp(g.monitor, "DP-1") != 0 || g.pinned)
		fail("surface fields");
	s.pinned = 1;
	s.x = 99;
	if (omaq_surface_set(dir, &s) != 0)
		fail("surface update");
	if (omaq_surface_get(dir, "0", &g) != 0 || !g.pinned || g.x != 99)
		fail("surface pinned");
	if (omaq_surface_get(dir, "missing", &g) == 0)
		fail("surface missing");
	if (omaq_surface_list(dir, listed, 2) != 1 || strcmp(listed[0].conversation, "0") != 0)
		fail("surface list");
	listed_n = omaq_surface_list(dir, listed, 0);
	if (listed_n != -1)
		fail("surface list cap");
	if (omaq_surface_set(dir, &(omaq_surface){ .conversation = "a/../b" }) == 0)
		fail("surface path escape");
}

static void test_qr_path(void)
{
	if (omaq_qr_path_ok("/tmp/x.png") != 0)
		fail("qr path ok");
	if (omaq_qr_path_ok("/tmp/../etc/x.png") == 0)
		fail("qr path ..");
	if (omaq_qr_path_ok("rel.png") == 0)
		fail("qr path rel");
	if (omaq_qr_path_ok("/tmp/x.txt") == 0)
		fail("qr path ext");
}

static void test_file(void)
{
	DIR *d = opendir("tests/gold/file");
	struct dirent *e;
	char name[OMAQ_FILE_NAME_MAX + 1];
	char id[OMAQ_FILE_ID_MAX];
	uint32_t fn, fnum;

	if (!d) {
		fail("open tests/gold/file");
		return;
	}
	while ((e = readdir(d))) {
		char path[512], body[256];
		char *nl, expect[128];
		size_t n = strlen(e->d_name);

		if (n < 5 || strcmp(e->d_name + n - 4, ".txt") != 0)
			continue;
		snprintf(path, sizeof(path), "tests/gold/file/%s", e->d_name);
		if (read_file(path, body, sizeof(body)) != 0) {
			fail(path);
			continue;
		}
		nl = strchr(body, '\n');
		if (!nl) {
			fail(path);
			continue;
		}
		*nl = '\0';
		snprintf(expect, sizeof(expect), "%s", nl + 1);
		if (strncmp(e->d_name, "basename-", 9) == 0) {
			if (omaq_file_basename(body, name, sizeof(name)) != 0 ||
			    strcmp(name, expect) != 0)
				fail(path);
			continue;
		}
		if (strncmp(e->d_name, "path-", 5) == 0) {
			if (strcmp(expect, "err") == 0) {
				if (omaq_file_path_ok(body))
					fail(path);
			} else if (!omaq_file_path_ok(body))
				fail(path);
			continue;
		}
		if (strncmp(e->d_name, "id-", 3) == 0) {
			if (strcmp(expect, "err") == 0) {
				if (omaq_file_id_parse(body, &fn, &fnum) == 0)
					fail(path);
			} else if (omaq_file_id_parse(body, &fn, &fnum) != 0)
				fail(path);
			else {
				unsigned int a = 0, b = 0;
				if (sscanf(expect, "%u %u", &a, &b) != 2 || fn != a || fnum != b)
					fail(path);
			}
			continue;
		}
		fail(path);
	}
	closedir(d);
	if (omaq_file_id_format(3, 7, id, sizeof(id)) != 0 || strcmp(id, "3:7") != 0)
		fail("file id format");
	if (omaq_file_path_ok("/tmp/ok.bin") != 1)
		fail("file path ok");
}

static void test_receipts(void)
{
	char wire[256], expected[256], id[97], reply[97], text[160], state[16], target[65];
	char too_long[98];

	memset(too_long, 'a', sizeof(too_long) - 1);
	too_long[sizeof(too_long) - 1] = '\0';
	if (!omaq_message_id_ok("msg-1") || omaq_message_id_ok("msg|1") ||
	    omaq_message_id_ok("msg\n1") || omaq_message_id_ok(too_long))
		fail("message id validation");
	if (read_file("tests/gold/receipt/message-wire.txt", expected, sizeof(expected)) != 0 ||
	    omaq_message_wire_pack(wire, sizeof(wire), "msg-1", "reply-1", "hello|world") != 0 ||
	    strcmp(wire, expected) != 0 ||
	    omaq_message_wire_unpack(wire, id, sizeof(id), reply, sizeof(reply), text, sizeof(text)) != 0 ||
	    strcmp(id, "msg-1") != 0 || strcmp(reply, "reply-1") != 0 || strcmp(text, "hello|world") != 0)
		fail("message wire envelope");
	if (read_file("tests/gold/receipt/read-wire.txt", expected, sizeof(expected)) != 0 ||
	    omaq_receipt_wire_pack(wire, sizeof(wire), "msg-1", "read") != 0 ||
	    strcmp(wire, expected) != 0 ||
	    omaq_receipt_wire_unpack(wire, id, sizeof(id), state, sizeof(state)) != 0 ||
	    strcmp(id, "msg-1") != 0 || strcmp(state, "read") != 0)
		fail("receipt wire envelope");
	if (omaq_receipt_confirm_wire_pack(wire, sizeof(wire), "msg-1", "read",
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0 ||
	    omaq_receipt_confirm_wire_unpack(wire, id, sizeof(id), state, sizeof(state),
					     target, sizeof(target)) != 0 ||
	    strcmp(wire, "OQX1|receipt-confirm-v1|read|msg-1|aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0 ||
	    strcmp(id, "msg-1") != 0 || strcmp(state, "read") != 0 ||
	    strcmp(target, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0 ||
	    omaq_receipt_confirm_wire_unpack("OQX1|receipt-confirm-v1|read|msg-1|short", id, sizeof(id),
					     state, sizeof(state), target, sizeof(target)) == 0)
		fail("receipt confirmation envelope");
	{
		omaq_receipt_outbox boundary;
		omaq_receipt_outbox_init(&boundary);
		for (size_t i = 0; i < OMAQ_RECEIPT_OUTBOX_MAX; i++) {
			char boundary_id[32];
			snprintf(boundary_id, sizeof(boundary_id), "boundary-%zu", i);
			if (omaq_receipt_outbox_add(&boundary, "7", boundary_id) != 1) {
				fail("receipt outbox boundary fill");
				break;
			}
		}
		if (boundary.length != OMAQ_RECEIPT_OUTBOX_MAX ||
		    omaq_receipt_outbox_add(&boundary, "7", "boundary-overflow") >= 0 ||
		    omaq_receipt_outbox_remove(&boundary, "7", "boundary-0") != 1 ||
		    omaq_receipt_outbox_add(&boundary, "7", "boundary-replacement") != 1)
			fail("receipt outbox max boundary");
		omaq_receipt_outbox_destroy(&boundary);
	}
	if (omaq_receipt_wire_pack(wire, sizeof(wire), "msg|1", "read") == 0 ||
	    omaq_message_delete_wire_pack(wire, sizeof(wire), "msg|1") == 0 ||
	    omaq_receipt_wire_pack(wire, sizeof(wire), "msg-1", "bad") == 0 ||
	    omaq_message_wire_unpack("plain", id, sizeof(id), reply, sizeof(reply), text, sizeof(text)) == 0)
		fail("receipt wire validation");
	if (read_file("tests/gold/actions/edit-wire.txt", expected, sizeof(expected)) != 0 ||
	    omaq_message_edit_wire_pack(wire, sizeof(wire), "msg-1", "edited text|with pipe") != 0 ||
	    strcmp(wire, expected) != 0 ||
	    omaq_message_edit_wire_unpack(wire, id, sizeof(id), text, sizeof(text)) != 0 ||
	    strcmp(id, "msg-1") != 0 || strcmp(text, "edited text|with pipe") != 0)
		fail("message edit wire");
	if (read_file("tests/gold/actions/delete-wire.txt", expected, sizeof(expected)) != 0 ||
	    omaq_message_delete_wire_pack(wire, sizeof(wire), "msg-1") != 0 ||
	    strcmp(wire, expected) != 0 || omaq_message_delete_wire_unpack(wire, id, sizeof(id)) != 0 ||
	    strcmp(id, "msg-1") != 0)
		fail("message delete wire");
	if (read_file("tests/gold/actions/reaction-wire.txt", expected, sizeof(expected)) != 0 ||
	    omaq_message_reaction_wire_pack(wire, sizeof(wire), "msg-1", "❤️") != 0 ||
	    strcmp(wire, expected) != 0 ||
	    omaq_message_reaction_wire_unpack(wire, id, sizeof(id), text, sizeof(text)) != 0 ||
	    strcmp(id, "msg-1") != 0 || strcmp(text, "❤️") != 0 ||
	    omaq_message_reaction_wire_pack(wire, sizeof(wire), "msg-1", "not-an-emoji") == 0 ||
	    omaq_message_reaction_wire_unpack("OQX1|msg-1|❤️|extra", id, sizeof(id),
					      text, sizeof(text)) == 0)
		fail("message reaction wire");
}

static void test_presence(void)
{
	char event[160], expected[160];

	if (omaq_presence_connection_event(event, sizeof(event), 0) != 0 ||
	    strcmp(event, "{\"event\":\"connection\",\"state\":\"connecting\"}") != 0 ||
	    omaq_presence_connection_event(event, sizeof(event), 1) != 0 ||
	    strcmp(event, "{\"event\":\"connection\",\"state\":\"online\"}") != 0)
		fail("connection state event");
	if (read_file("tests/gold/presence/typing-on.json", expected, sizeof(expected)) != 0 ||
	    omaq_presence_typing_event(event, sizeof(event), "7", 1) != 0 ||
	    strcmp(event, expected) != 0)
		fail("typing event on");
	if (read_file("tests/gold/presence/typing-off.json", expected, sizeof(expected)) != 0 ||
	    omaq_presence_typing_event(event, sizeof(event), "7", 0) != 0 ||
	    strcmp(event, expected) != 0)
		fail("typing event off");
	if (omaq_presence_typing_event(event, sizeof(event), "g0", 1) == 0 ||
	    omaq_presence_typing_event(event, sizeof(event), "", 1) == 0)
		fail("typing event conversation validation");
}

static void test_avatar(void)
{
	if (!omaq_avatar_id_ok("self") || !omaq_avatar_id_ok("0") || !omaq_avatar_id_ok("12"))
		fail("avatar id ok");
	if (omaq_avatar_id_ok("") || omaq_avatar_id_ok("../x") || omaq_avatar_id_ok("a/b") ||
	    omaq_avatar_id_ok("self/../x"))
		fail("avatar id bad");
	if (!omaq_avatar_src_ok("/tmp/face.png") || !omaq_avatar_src_ok("/tmp/face.JPG"))
		fail("avatar src ok");
	if (omaq_avatar_src_ok("face.png") || omaq_avatar_src_ok("/tmp/../etc/x.png") ||
	    omaq_avatar_src_ok("/tmp/x.bin"))
		fail("avatar src bad");
	{
		char d[256];
		char src[] = "/tmp/omaq-avatar-large.png";
		unsigned char block[4096] = { 0 };
		FILE *f;
		struct stat st;
		int i;

		block[0] = 0x89;
		block[1] = 'P';
		block[2] = 'N';
		block[3] = 'G';
		(void)mkdir("/tmp/omaq-av", 0700);
		f = fopen(src, "wb");
		if (!f)
			fail("avatar fixture open");
		else {
			for (i = 0; i < 25; i++)
				if (fwrite(block, 1, sizeof(block), f) != sizeof(block))
					fail("avatar fixture write");
			fclose(f);
		}
		if (omaq_avatar_install("/tmp/omaq-av", "self", src, d, sizeof(d)) != 0 ||
		    stat("/tmp/omaq-av/avatars/self.png", &st) != 0 || st.st_size != 25 * (off_t)sizeof(block))
			fail("avatar install large");
		unlink(src);
		unlink("/tmp/omaq-av/avatars/self.png");
		rmdir("/tmp/omaq-av/avatars");
		rmdir("/tmp/omaq-av");

		if (omaq_avatar_dest("/tmp/omaq-av", "self", d, sizeof(d)) != 0 ||
		    strcmp(d, "/tmp/omaq-av/avatars/self.png") != 0)
			fail("avatar dest self");
		if (omaq_avatar_is_dest("/tmp/omaq-av", "/tmp/omaq-av/avatars/self.png") != 1)
			fail("avatar is dest");
		if (omaq_avatar_is_dest("/tmp/omaq-av", "/tmp/omaq-av/files/0/x.png") != 0)
			fail("avatar is dest other");
	}
}

int main(void)
{
	test_invites();
	test_roles();
	test_json();
	test_fragmented_stdin();
	test_store();
	test_search();
	test_identity_files();
	test_pass_ok();
	if (!omaq_rk_ok("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"))
		fail("rk ok");
	if (omaq_rk_ok("aa") || omaq_rk_ok(""))
		fail("rk bad");
	test_mutate();
	test_conv();
	test_expire();
	test_rate_gold();
	test_rate_hour();
	test_rate_key_only();
	test_control_rate();
	test_safety();
	test_group_invite();
	test_ratchet_pins();
	test_group_id();
	test_group_plan();
	test_surface();
	test_qr_path();
	test_file();
	test_receipts();
	test_presence();
	test_avatar();
	if (fails) {
		fprintf(stderr, "omaq_test: %d failure(s)\n", fails);
		return 1;
	}
	puts("omaq_test: ok");
	return 0;
}
