#define _DEFAULT_SOURCE
#include "../helper/auto_open.h"
#include "../helper/conversation.h"
#include "../helper/direct_state.h"
#include "../helper/avatar.h"
#include "../helper/file.h"
#include "../helper/group.h"
#include "../helper/group_file.h"
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
#include "../helper/sound.h"
#include "../helper/store.h"
#include "../helper/surface.h"
#include "../helper/state_archive.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_AVATAR_DECODERS
#include <jpeglib.h>
#include <png.h>
#include <webp/encode.h>
#endif

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
	static const struct {
		const char *key;
		const char *first_value;
		const char *second_value;
	} key_cases[] = {
		{ "op", "\"status\"", "\"nope\"" },
		{ "kind", "\"one\"", "\"two\"" },
		{ "payload", "\"one\"", "\"two\"" },
		{ "id", "\"one\"", "\"two\"" },
		{ "conversation", "\"one\"", "\"two\"" },
		{ "text", "\"one\"", "\"two\"" },
		{ "reply", "\"one\"", "\"two\"" },
		{ "group", "\"one\"", "\"two\"" },
		{ "member", "\"one\"", "\"two\"" },
		{ "key", "\"one\"", "\"two\"" },
		{ "request", "\"one\"", "\"two\"" },
		{ "role", "\"one\"", "\"two\"" },
		{ "state", "\"one\"", "\"two\"" },
		{ "path", "\"one\"", "\"two\"" },
		{ "title", "\"one\"", "\"two\"" },
		{ "nickname", "\"one\"", "\"two\"" },
		{ "monitor", "\"one\"", "\"two\"" },
		{ "passphrase", "\"one\"", "\"two\"" },
		{ "ttlSec", "1", "2" },
		{ "limit", "1", "2" },
		{ "x", "1", "2" },
		{ "y", "1", "2" },
		{ "accept", "true", "false" },
		{ "replace", "true", "false" },
		{ "pinned", "true", "false" },
		{ "enabled", "true", "false" },
		{ "typing", "true", "false" },
		{ "width", "320", "640" },
		{ "height", "240", "480" },
	};
	omaq_op op;
	uint64_t seen_fields = 0;
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
	for (size_t i = 0; i < sizeof(key_cases) / sizeof(key_cases[0]); i++) {
		char accepted[256], duplicate[384], failure[128];
		uint64_t added_fields;

		if (strcmp(key_cases[i].key, "op") == 0) {
			snprintf(accepted, sizeof(accepted), "{\"op\":%s}",
				 key_cases[i].first_value);
			snprintf(duplicate, sizeof(duplicate), "{\"op\":%s,\"op\":%s}",
				 key_cases[i].first_value, key_cases[i].second_value);
		} else {
			snprintf(accepted, sizeof(accepted), "{\"op\":\"status\",\"%s\":%s}",
				 key_cases[i].key, key_cases[i].first_value);
			snprintf(duplicate, sizeof(duplicate),
				 "{\"op\":\"status\",\"%s\":%s,\"%s\":%s}",
				 key_cases[i].key, key_cases[i].first_value,
				 key_cases[i].key, key_cases[i].second_value);
		}
		if (omaq_json_parse_op(accepted, &op) != 0) {
			snprintf(failure, sizeof(failure), "json whitelist rejected %s",
				 key_cases[i].key);
			fail(failure);
			continue;
		}
		added_fields = op.field_mask & ~OMAQ_JSON_FIELD_OP;
		if (strcmp(key_cases[i].key, "op") == 0) {
			if (op.field_mask != OMAQ_JSON_FIELD_OP)
				fail("json op field mask");
		} else if (added_fields == 0 ||
			   (added_fields & (added_fields - UINT64_C(1))) != 0 ||
			   (seen_fields & added_fields) != 0) {
			snprintf(failure, sizeof(failure), "json field mask collision %s",
				 key_cases[i].key);
			fail(failure);
		}
		seen_fields |= op.field_mask;
		if (omaq_json_parse_op(duplicate, &op) == 0) {
			snprintf(failure, sizeof(failure), "json duplicate accepted %s",
				 key_cases[i].key);
			fail(failure);
		}
	}
	if (seen_fields != ((UINT64_C(1) << 29) - UINT64_C(1)))
		fail("json whitelist field coverage");
	if (omaq_json_parse_op("{\"id\":\"one\",\"op\":\"status\",\"id\":\"two\"}",
			       &op) == 0)
		fail("json nonadjacent duplicate accepted");
	if (omaq_json_parse_op("{\"\\u006f\\u0070\":\"status\"}", &op) == 0 ||
	    omaq_json_parse_op("{\"Op\":\"status\"}", &op) == 0)
		fail("json ambiguous operation key accepted");
	if (omaq_json_parse_op("{\"op\":\"status\",\"id\":\"one\",\"request\":\"two\"}",
			       &op) != 0 ||
	    op.field_mask != (OMAQ_JSON_FIELD_OP | OMAQ_JSON_FIELD_ID |
			      OMAQ_JSON_FIELD_REQUEST))
		fail("json public field masks");
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
		if (omaq_message_append_id(dir, "gr1", "me", "group receipt", "out",
					   "group-receipt-message") != 0 ||
		    omaq_store_update_group_receipt_changed(dir, "gr1", "group-receipt-message",
			"delivered", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 1 ||
		    omaq_store_update_group_receipt_changed(dir, "gr1", "group-receipt-message",
			"delivered", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0 ||
		    omaq_store_update_group_receipt_changed(dir, "gr1", "group-receipt-message",
			"read", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 1 ||
		    omaq_store_update_group_receipt_changed(dir, "gr1", "group-receipt-message",
			"delivered", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") != 0 ||
		    omaq_store_update_group_receipt_changed(dir, "gr1", "group-receipt-message",
			"delivered", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") != 1 ||
		    omaq_message_history(dir, "gr1", 20, &updated, &updated_n) != 0 || !updated ||
		    !strstr(updated, "\"receipt_group_aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\":\"read\"") ||
		    !strstr(updated, "\"receipt_group_bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\":\"delivered\""))
			fail("group receipt actor persistence");
		free(updated);
		updated = NULL;
		for (int receipt_actor = 3; receipt_actor <= 9; receipt_actor++) {
			snprintf(actor, sizeof(actor), "%064x", (unsigned int)receipt_actor);
			if (omaq_store_update_group_receipt_changed(dir, "gr1",
				    "group-receipt-message", "delivered", actor) != 1)
				fail("group receipt actor bound allowance");
		}
		snprintf(actor, sizeof(actor), "%064x", 10u);
		if (omaq_store_update_group_receipt_changed(dir, "gr1", "group-receipt-message",
			"delivered", actor) != -1 ||
		    omaq_store_update_group_receipt_changed(dir, "gr1", "group-receipt-message",
			"read", "short") != -1)
			fail("group receipt actor bound");
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
		    omaq_message_append_attachment_with_id(dir, "c1", "me", "/tmp/picture.png",
							  "out", "image", id, sizeof(id)) != 0 ||
		    omaq_message_append_attachment_with_id(dir, "c1", "me", "/tmp/bad",
							  "out", "video", id, sizeof(id)) == 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"text\":\"/tmp/song.mp3\"") ||
		    !strstr(updated, "\"kind\":\"file\"") ||
		    !strstr(updated, "\"text\":\"/tmp/picture.png\"") ||
		    !strstr(updated, "\"dir\":\"out\",\"kind\":\"image\""))
			fail("attachment message kinds");
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
		char live[640], rot[640];
		if (omaq_store_append(dir, "rotate-fail", "{\"n\":1}") != 0 ||
		    snprintf(live, sizeof(live), "%s/history/rotate-fail/messages.jsonl", dir) >=
			(int)sizeof(live) ||
		    snprintf(rot, sizeof(rot), "%s.1", live) >= (int)sizeof(rot) ||
		    truncate(live, 2 * 1024 * 1024) != 0 || mkdir(rot, 0700) != 0 ||
		    omaq_store_append(dir, "rotate-fail", "{\"n\":2}") == 0)
			fail("store rotation failure propagation");
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
	{
		char message_id[64], sentinel[700], temporary[700], sentinel_body[64];
		FILE *sentinel_file;
		if (omaq_message_append_with_id(dir, "c2", "me", "hardlink test", "out",
						message_id, sizeof(message_id)) != 0 ||
		    snprintf(sentinel, sizeof(sentinel), "%s/sentinel", dir) >=
			(int)sizeof(sentinel) || !(sentinel_file = fopen(sentinel, "w")) ||
		    fputs("do not truncate\n", sentinel_file) < 0 || fclose(sentinel_file) != 0 ||
		    snprintf(temporary, sizeof(temporary),
			     "%s/history/c2/messages.jsonl.tmp.%ld", dir, (long)getpid()) >=
			(int)sizeof(temporary) || link(sentinel, temporary) != 0 ||
		    omaq_message_edit(dir, "c2", message_id, "must fail") == 0 ||
		    read_file(sentinel, sentinel_body, sizeof(sentinel_body)) != 0 ||
		    strcmp(sentinel_body, "do not truncate") != 0)
			fail("history temp hardlink rejection");
		unlink(temporary);
	}
	{
		char history_dir[640], link_path[700];
		if (snprintf(history_dir, sizeof(history_dir), "%s/history/c-symlink", dir) >=
		    (int)sizeof(history_dir) || mkdir(history_dir, 0700) != 0 ||
		    snprintf(link_path, sizeof(link_path), "%s/messages.jsonl", history_dir) >=
		    (int)sizeof(link_path) || symlink("/tmp/omaq-history-target", link_path) != 0 ||
		    omaq_store_append(dir, "c-symlink", "{\"unsafe\":true}") == 0 ||
		    omaq_store_tail(dir, "c-symlink", 10, &out, &n) == 0)
			fail("history symlink rejection");
		free(out);
		out = NULL;
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
	    omaq_unread_increment(&unread,
		"d:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") != 0 ||
	    omaq_unread_increment(&unread, "../bad") == 0 ||
	    omaq_unread_increment(&unread, "01") == 0 ||
	    omaq_unread_increment(&unread, "g01") == 0 ||
	    omaq_unread_increment(&unread,
		"g:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") == 0 ||
	    omaq_unread_increment(&unread,
		"d:BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB") == 0 ||
	    omaq_unread_increment(&unread, "4294967296") == 0 ||
	    omaq_unread_increment(&unread, "g4294967296") == 0 ||
	    omaq_unread_total(&unread) != 4 || omaq_unread_count(&unread, "0") != 2 ||
	    omaq_store_unread_save(&unread, dir) != 0 ||
	    omaq_store_unread_load(&loaded, dir) != 0 ||
	    loaded.length != 3 || omaq_unread_total(&loaded) != 4 ||
	    omaq_unread_clear(&loaded, "0") != 0 || omaq_unread_total(&loaded) != 2)
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
	char registry[256], bindings[256], bundle[256], v1_bundle[256], fresh[256];
	char fresh_tox[300], fresh_registry[300], fresh_bindings[300], fresh_v1[300];
	char fresh_v1_tox[320], fresh_v1_registry[320], fresh_v1_bindings[320];
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
	    snprintf(bindings, sizeof(bindings), "%s/group-friends.tsv", dir) >=
		    (int)sizeof(bindings) ||
	    snprintf(bundle, sizeof(bundle), "%s/bundle.save", dir) >= (int)sizeof(bundle) ||
	    snprintf(v1_bundle, sizeof(v1_bundle), "%s/bundle-v1.save", dir) >=
		    (int)sizeof(v1_bundle) ||
	    snprintf(fresh, sizeof(fresh), "%s/fresh", dir) >= (int)sizeof(fresh) ||
	    snprintf(fresh_tox, sizeof(fresh_tox), "%s/tox.save", fresh) >= (int)sizeof(fresh_tox) ||
	    snprintf(fresh_registry, sizeof(fresh_registry), "%s/groups.tsv", fresh) >=
		    (int)sizeof(fresh_registry) ||
	    snprintf(fresh_bindings, sizeof(fresh_bindings), "%s/group-friends.tsv", fresh) >=
		    (int)sizeof(fresh_bindings) ||
	    snprintf(fresh_v1, sizeof(fresh_v1), "%s/fresh-v1", dir) >=
		    (int)sizeof(fresh_v1) ||
	    snprintf(fresh_v1_tox, sizeof(fresh_v1_tox), "%s/tox.save", fresh_v1) >=
		    (int)sizeof(fresh_v1_tox) ||
	    snprintf(fresh_v1_registry, sizeof(fresh_v1_registry), "%s/groups.tsv", fresh_v1) >=
		    (int)sizeof(fresh_v1_registry) ||
	    snprintf(fresh_v1_bindings, sizeof(fresh_v1_bindings), "%s/group-friends.tsv", fresh_v1) >=
		    (int)sizeof(fresh_v1_bindings) ||
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
	f = fopen(bindings, "w");
	if (!f) {
		fail("id bindings write");
		return;
	}
	fputs("OMAQGF1\n", f);
	fputs("g:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\t"
	      "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\t"
	      "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n", f);
	fclose(f);
	if (omaq_identity_bundle_export(dir, src) == 0 ||
	    omaq_identity_bundle_export(dir, tox_alias) == 0 ||
	    omaq_identity_bundle_export(dir, registry) == 0 ||
	    omaq_identity_bundle_export(dir, bindings) == 0)
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
		    !strstr(copied, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tGroup") ||
		    read_file(fresh_bindings, copied, sizeof(copied)) != 0 ||
		    !strstr(copied, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"))
			fail("identity bundle group registry");
	}
	{
		static const char v1_registry[] =
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\tGroup\n";
		unsigned char header[16] = {
			'O', 'M', 'A', 'Q', 'I', 'D', '1', '\n', 0, 0, 0, 6, 0, 0, 0,
			(unsigned char)(sizeof(v1_registry) - 1)
		};
		char copied[256];
		int fixture_ok = 0;
		f = fopen(v1_bundle, "wb");
		if (f) {
			fixture_ok = fwrite(header, 1, sizeof(header), f) == sizeof(header) &&
				fwrite("SAVE-A", 1, 6, f) == 6 &&
				fwrite(v1_registry, 1, sizeof(v1_registry) - 1, f) ==
					sizeof(v1_registry) - 1;
			if (fclose(f) != 0)
				fixture_ok = 0;
			f = NULL;
		}
		if (!fixture_ok) {
			fail("identity v1 fixture");
		} else if (mkdir(fresh_v1, 0700) != 0 ||
			   omaq_identity_bundle_import(fresh_v1, v1_bundle, 0) != 0 ||
			   read_file(fresh_v1_tox, copied, sizeof(copied)) != 0 ||
			   strcmp(copied, "SAVE-A") != 0 ||
			   read_file(fresh_v1_registry, copied, sizeof(copied)) != 0 ||
			   !strstr(copied, "\tGroup") ||
			   read_file(fresh_v1_bindings, copied, sizeof(copied)) != 0 ||
			   strcmp(copied, "OMAQGF1") != 0) {
			fail("identity v1 compatibility");
		}
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
	if (omaq_identity_pass_ok("has\nnl") ||
	    omaq_identity_pass_ok("has\001control"))
		fail("pass control");
	if (!omaq_identity_pass_ok("ok-pass-1"))
		fail("pass ok");
	if (omaq_identity_new_pass_ok("short7") ||
	    omaq_identity_new_pass_ok("äääääää") ||
	    !omaq_identity_new_pass_ok("eight-ok") ||
	    !omaq_identity_new_pass_ok("ääääääää"))
		fail("new pass minimum");
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
	omaq_control_rate r, typing;
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
	omaq_control_rate_init(&r);
	for (i = 0; i < OMAQ_CONTROL_RATE_PER_KEY; i++)
		if (omaq_control_rate_allow(&r, 't', 7, actor, 5000) != 0)
			fail("group typing rate allowance");
	if (omaq_control_rate_allow(&r, 't', 7, actor, 5000) == 0 ||
	    omaq_control_rate_allow(&r, 'z', 7, actor, 5000) == 0)
		fail("group typing rate limit");
	omaq_control_rate_init(&typing);
	for (i = 0; i < OMAQ_CONTROL_RATE_GLOBAL; i++) {
		snprintf(actor, sizeof(actor), "%064x", (unsigned int)(i + 1));
		if (omaq_control_rate_allow(&typing, 't', (uint32_t)(i % 8), actor, 6000) != 0)
			fail("isolated group typing rate allowance");
	}
	if (omaq_control_rate_allow(&typing, 't', 7, actor, 6000) == 0 ||
	    omaq_control_rate_allow(&r, 'r', 7, actor, 6000) != 0)
		fail("group typing exhausted receipt budget");
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

static void test_group_file_wire(void)
{
	omaq_group_file_offer offer, decoded;
	uint8_t packet[OMAQ_GROUP_FILE_PACKET_MAX + 1];
	uint8_t payload[OMAQ_GROUP_FILE_DATA_MAX];
	uint8_t id[OMAQ_GROUP_FILE_ID_BYTES], parsed[OMAQ_GROUP_FILE_ID_BYTES];
	uint8_t type;
	const uint8_t *data = NULL;
	uint64_t offset = 0;
	size_t data_length = 0;
	char event_id[3 + OMAQ_GROUP_FILE_ID_HEX + 1];
	int length;

	memset(&offer, 0, sizeof(offer));
	for (size_t i = 0; i < sizeof(offer.id); i++)
		offer.id[i] = (uint8_t)i;
	for (size_t i = 0; i < sizeof(offer.hash); i++)
		offer.hash[i] = (uint8_t)(255u - i);
	offer.size = OMAQ_FILE_MAX;
	snprintf(offer.kind, sizeof(offer.kind), "image");
	snprintf(offer.name, sizeof(offer.name), "safe image.png");
	length = omaq_group_file_offer_pack(packet, sizeof(packet), &offer);
	if (length <= 0 ||
	    omaq_group_file_offer_unpack(packet, (size_t)length, &decoded) != 0 ||
	    decoded.size != offer.size || strcmp(decoded.kind, "image") != 0 ||
	    strcmp(decoded.name, offer.name) != 0 ||
	    memcmp(decoded.id, offer.id, sizeof(offer.id)) != 0 ||
	    memcmp(decoded.hash, offer.hash, sizeof(offer.hash)) != 0)
		fail("group file offer roundtrip");
	if (omaq_group_file_offer_unpack(packet, (size_t)length - 1, &decoded) == 0 ||
	    omaq_group_file_offer_unpack(packet, (size_t)length + 1, &decoded) == 0)
		fail("group file offer exact framing");
	packet[0] ^= 1;
	if (omaq_group_file_offer_unpack(packet, (size_t)length, &decoded) == 0)
		fail("group file offer magic");
	packet[0] ^= 1;
	packet[30] = 2;
	if (omaq_group_file_offer_unpack(packet, (size_t)length, &decoded) == 0)
		fail("group file offer kind");
	packet[30] = 1;
	packet[64] = '/';
	if (omaq_group_file_offer_unpack(packet, (size_t)length, &decoded) == 0)
		fail("group file offer unsafe name");
	snprintf(offer.name, sizeof(offer.name), "safe.png");
	offer.size = OMAQ_FILE_MAX + 1u;
	if (omaq_group_file_offer_pack(packet, sizeof(packet), &offer) >= 0)
		fail("group file offer oversized");

	memcpy(id, offer.id, sizeof(id));
	if (omaq_group_file_id_hex(id, event_id + 3) != 0) {
		fail("group file id hex");
	} else {
		memcpy(event_id, "gf:", 3);
		if (omaq_group_file_id_parse(event_id, parsed) != 0 ||
		    memcmp(id, parsed, sizeof(id)) != 0)
			fail("group file id parse");
		event_id[3] = 'A';
		if (omaq_group_file_id_parse(event_id, parsed) == 0)
			fail("group file id uppercase");
	}
	for (type = OMAQ_GROUP_FILE_ACCEPT; type <= OMAQ_GROUP_FILE_FAIL; type++) {
		if (type == OMAQ_GROUP_FILE_DATA || type == OMAQ_GROUP_FILE_OFFER)
			continue;
		length = omaq_group_file_control_pack(packet, sizeof(packet), type, id);
		if (length <= 0 ||
		    omaq_group_file_control_unpack(packet, (size_t)length, &type, parsed) != 0 ||
		    memcmp(id, parsed, sizeof(id)) != 0 ||
		    omaq_group_file_control_unpack(packet, (size_t)length + 1,
						 &type, parsed) == 0)
			fail("group file control framing");
	}
	memset(payload, 0x5a, sizeof(payload));
	length = omaq_group_file_data_pack(packet, sizeof(packet), id, 17,
				      payload, sizeof(payload));
	if (length != OMAQ_GROUP_FILE_PACKET_MAX ||
	    omaq_group_file_data_unpack(packet, (size_t)length, parsed, &offset,
					&data, &data_length) != 0 || offset != 17 ||
	    data_length != OMAQ_GROUP_FILE_DATA_MAX ||
	    memcmp(data, payload, data_length) != 0)
		fail("group file data roundtrip");
	if (!omaq_message_id_reserved("gf:001122") ||
	    omaq_message_id_reserved("GF:001122") || omaq_message_id_reserved("001122"))
		fail("group file reserved message namespace");
	if (omaq_group_file_data_pack(packet, sizeof(packet), id, 0, payload, 0) >= 0 ||
	    omaq_group_file_data_pack(packet, sizeof(packet), id, 0, payload,
				      OMAQ_GROUP_FILE_DATA_MAX + 1u) >= 0 ||
	    omaq_group_file_data_unpack(packet, 30, parsed, &offset, &data,
					&data_length) == 0)
		fail("group file data bounds");
	{
		char state[] = "/tmp/omaq-group-file-ids-XXXXXX";
		char store[512];
		FILE *file;
		uint8_t second[OMAQ_GROUP_FILE_ID_BYTES];
		memset(second, 0xa5, sizeof(second));
		if (!mkdtemp(state) ||
		    snprintf(store, sizeof(store), "%s/group-file-ids.bin", state) >=
			(int)sizeof(store) ||
		    omaq_group_file_id_reserve(state, id) != 0 ||
		    omaq_group_file_id_reserve(state, id) != 1 ||
		    omaq_group_file_id_reserve(state, second) != 0)
			fail("group file durable id reservation");
		if (chmod(store, 0644) != 0 ||
		    omaq_group_file_id_reserve(state, offer.id) != -1 ||
		    chmod(store, 0600) != 0)
			fail("group file id store permissions");
		file = fopen(store, "ab");
		if (!file || fputc(0, file) == EOF || fclose(file) != 0 ||
		    omaq_group_file_id_reserve(state, offer.id) != -1)
			fail("group file malformed id store");
		unlink(store);
		if (symlink("/dev/null", store) != 0 ||
		    omaq_group_file_id_reserve(state, offer.id) != -1)
			fail("group file id store symlink");
		unlink(store);
		rmdir(state);
	}
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

static int write_test_file(const char *path, const char *value)
{
	FILE *file = fopen(path, "w");
	int rc;

	if (!file)
		return -1;
	rc = fputs(value, file) < 0 ? -1 : 0;
	if (fclose(file) != 0)
		rc = -1;
	return rc;
}

static int write_private_test_file(const char *path, const char *value)
{
	return write_test_file(path, value) != 0 || chmod(path, 0600) != 0 ? -1 : 0;
}

static int sound_id_ok_for_test(const char *id)
{
	if (!id || strlen(id) != OMAQ_SOUND_ID_HEX)
		return 0;
	for (size_t i = 0; i < OMAQ_SOUND_ID_HEX; i++)
		if (!((id[i] >= '0' && id[i] <= '9') ||
		      (id[i] >= 'a' && id[i] <= 'f')))
			return 0;
	return 1;
}

static int write_pcm_wav_test(const char *path, int trailing)
{
	static const unsigned char wav[] = {
		'R', 'I', 'F', 'F', 40, 0, 0, 0, 'W', 'A', 'V', 'E',
		'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
		0x40, 0x1f, 0, 0, 0x40, 0x1f, 0, 0, 1, 0, 8, 0,
		'd', 'a', 't', 'a', 4, 0, 0, 0, 0x80, 0x81, 0x82, 0x83
	};
	FILE *file = fopen(path, "wb");
	int result = 0;

	if (!file)
		return -1;
	if (fwrite(wav, 1, sizeof(wav), file) != sizeof(wav) ||
	    (trailing && fwrite("trailing", 1, 8, file) != 8))
		result = -1;
	if (fclose(file) != 0 || chmod(path, 0600) != 0)
		result = -1;
	return result;
}

static void test_sound(void)
{
	char home[] = "/tmp/omaq-sound-XXXXXX";
	char source[512], source_link[512], linked_copy[512], custom[512];
	char invalid[512], polyglot[512], oversized[512];
	char orphan_audio[512], orphan_name[512], empty_audio[512], tombstone[512];
	struct stat source_status, copied_status;
	omaq_sound imported = { 0 }, second = { 0 }, listed[OMAQ_SOUND_MAX];
	int count;

	if (!mkdtemp(home) ||
	    snprintf(source, sizeof(source), "%s/alert.wav", home) >= (int)sizeof(source) ||
	    snprintf(source_link, sizeof(source_link), "%s/alert-link.wav", home) >=
		(int)sizeof(source_link) ||
	    snprintf(linked_copy, sizeof(linked_copy), "%s/linked-copy", home) >=
		(int)sizeof(linked_copy) ||
	    snprintf(custom, sizeof(custom), "%s/custom-sounds", home) >= (int)sizeof(custom) ||
	    snprintf(invalid, sizeof(invalid), "%s/not-a-sound.wav", home) >=
		(int)sizeof(invalid) ||
	    snprintf(polyglot, sizeof(polyglot), "%s/polyglot.wav", home) >=
		(int)sizeof(polyglot) ||
	    snprintf(oversized, sizeof(oversized), "%s/oversized.wav", home) >=
		(int)sizeof(oversized) ||
	    snprintf(orphan_audio, sizeof(orphan_audio),
		     "%s/11111111111111111111111111111111.audio", custom) >=
		(int)sizeof(orphan_audio) ||
	    snprintf(orphan_name, sizeof(orphan_name),
		     "%s/22222222222222222222222222222222.name", custom) >=
		(int)sizeof(orphan_name) ||
	    snprintf(empty_audio, sizeof(empty_audio),
		     "%s/00000000000000000000000000000000.audio", custom) >=
		(int)sizeof(empty_audio) ||
	    snprintf(tombstone, sizeof(tombstone),
		     "%s/33333333333333333333333333333333.audio.delete", custom) >=
		(int)sizeof(tombstone) ||
	    write_pcm_wav_test(source, 0) != 0 ||
	    write_private_test_file(invalid, "not audio\n") != 0 ||
	    write_pcm_wav_test(polyglot, 1) != 0) {
		fail("sound fixture");
		return;
	}
	if (omaq_sound_import(home, source, &imported) != 0 ||
	    strcmp(imported.label, "alert") != 0 || !sound_id_ok_for_test(imported.id)) {
		fail("sound import");
		goto done;
	}
	if (stat(source, &source_status) != 0 || stat(imported.path, &copied_status) != 0 ||
	    source_status.st_size != copied_status.st_size ||
	    strcmp(source, imported.path) == 0) {
		fail("sound managed copy");
		goto done;
	}
	{
		int oversized_fd = open(oversized, O_WRONLY | O_CREAT | O_TRUNC, 0600);
		if (oversized_fd < 0 ||
		    ftruncate(oversized_fd, (off_t)OMAQ_SOUND_FILE_MAX + 1) != 0 ||
		    close(oversized_fd) != 0 ||
		    omaq_sound_import(home, invalid, &second) == 0 ||
		    omaq_sound_import(home, polyglot, &second) == 0 ||
		    omaq_sound_import(home, oversized, &second) == 0)
			fail("sound format and size rejection");
	}
	count = omaq_sound_list(home, listed, OMAQ_SOUND_MAX);
	if (count != 1 || strcmp(listed[0].id, imported.id) != 0 ||
	    strcmp(listed[0].path, imported.path) != 0)
		fail("sound list");
	if (write_private_test_file(orphan_audio, "orphan") != 0 ||
	    write_private_test_file(orphan_name, "orphan\n") != 0 ||
	    write_private_test_file(empty_audio, "") != 0 ||
	    write_private_test_file(tombstone, "tombstone") != 0 ||
	    omaq_sound_list(home, listed, OMAQ_SOUND_MAX) != 1 ||
	    access(orphan_audio, F_OK) == 0 || access(orphan_name, F_OK) == 0 ||
	    access(empty_audio, F_OK) == 0 || access(tombstone, F_OK) == 0)
		fail("sound interrupted transaction recovery");
	if (symlink(source, source_link) != 0 ||
	    omaq_sound_import(home, source_link, &second) == 0)
		fail("sound source symlink rejection");
	unlink(source_link);
	if (link(imported.path, linked_copy) != 0 ||
	    omaq_sound_list(home, listed, OMAQ_SOUND_MAX) >= 0)
		fail("sound managed hardlink rejection");
	unlink(linked_copy);
	if (omaq_sound_import(home, source, &second) != 0 ||
	    omaq_sound_remove(home, "../not-a-managed-sound") == 0 ||
	    omaq_sound_remove(home, imported.id) != 0 || access(source, F_OK) != 0 ||
	    access(imported.path, F_OK) == 0 ||
	    omaq_sound_list(home, listed, OMAQ_SOUND_MAX) != 1 ||
	    strcmp(listed[0].id, second.id) != 0)
		fail("sound scoped remove");
	if (omaq_sound_remove(home, second.id) != 0 ||
	    omaq_sound_list(home, listed, OMAQ_SOUND_MAX) != 0)
		fail("sound remove final");
done:
	unlink(source_link);
	unlink(linked_copy);
	unlink(imported.path);
	unlink(second.path);
	if (sound_id_ok_for_test(imported.id)) {
		char path[512];
		if (snprintf(path, sizeof(path), "%s/%s.name", custom, imported.id) <
		    (int)sizeof(path))
			unlink(path);
	}
	if (sound_id_ok_for_test(second.id)) {
		char path[512];
		if (snprintf(path, sizeof(path), "%s/%s.name", custom, second.id) <
		    (int)sizeof(path))
			unlink(path);
	}
	unlink(source);
	unlink(invalid);
	unlink(polyglot);
	unlink(oversized);
	unlink(orphan_audio);
	unlink(orphan_name);
	unlink(empty_audio);
	unlink(tombstone);
	rmdir(custom);
	rmdir(home);
}

static int make_direct_state_dirs(const char *home)
{
	char path[768];
	const char *paths[] = { "history", "avatars", "ratchet", "ratchet/rk",
		"ratchet/ident", "ratchet/sess" };

	for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		if (snprintf(path, sizeof(path), "%s/%s", home, paths[i]) >=
		    (int)sizeof(path) || mkdir(path, 0700) != 0)
			return -1;
	}
	return 0;
}

static int create_legacy_direct_fixture(const char *home, const char *number,
					const char *history_text)
{
	char path[768];

	if (snprintf(path, sizeof(path), "%s/history/%s", home, number) >=
	    (int)sizeof(path) || mkdir(path, 0700) != 0 ||
	    snprintf(path, sizeof(path), "%s/history/%s/messages.jsonl", home, number) >=
	    (int)sizeof(path) || write_private_test_file(path, history_text) != 0 ||
	    snprintf(path, sizeof(path), "%s/avatars/%s.png", home, number) >=
	    (int)sizeof(path) || write_private_test_file(path, "avatar\n") != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/rk/%s", home, number) >=
	    (int)sizeof(path) || write_private_test_file(path, "pin\n") != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/ident/%s", home, number) >=
	    (int)sizeof(path) || write_private_test_file(path, "identity\n") != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/sess/%s-1", home, number) >=
	    (int)sizeof(path) || write_private_test_file(path, "session\n") != 0)
		return -1;
	return 0;
}

static void test_direct_state(void)
{
	char dir[] = "/tmp/omaq-direct-state-XXXXXX";
	char unbound[] = "/tmp/omaq-direct-unbound-XXXXXX";
	char linked[] = "/tmp/omaq-direct-linked-XXXXXX";
	char removal[] = "/tmp/omaq-direct-removal-XXXXXX";
	char path[768], stable[OMAQ_DIRECT_STATE_ID_MAX], body[64];
	omaq_direct_state_friend current[1];
	const char *key = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const char *other = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	int reinvite = 0;

	if (!mkdtemp(dir) || make_direct_state_dirs(dir) != 0) {
		fail("direct state fixture dirs");
		return;
	}
	current[0].number = 0;
	memcpy(current[0].key, key, 65);
	if (omaq_direct_state_reconcile(dir, current, 1, &reinvite) != 0 || reinvite ||
	    snprintf(path, sizeof(path), "%s/direct-friends.tsv", dir) >=
	    (int)sizeof(path) || access(path, R_OK) != 0)
		fail("direct state binding bootstrap");
	if (omaq_direct_state_reconcile(dir, current, 0, &reinvite) == 0)
		fail("direct state truncation rejection");
	if (snprintf(path, sizeof(path), "%s/direct-friends.tsv", dir) >=
	    (int)sizeof(path)) {
		fail("direct state map path");
	} else {
		char linked_map[800];
		FILE *map;
		if (chmod(path, 0644) != 0 ||
		    omaq_direct_state_reconcile(dir, current, 1, &reinvite) == 0 ||
		    chmod(path, 0600) != 0)
			fail("direct state map permissions");
		if (snprintf(linked_map, sizeof(linked_map), "%s.link", path) >=
		    (int)sizeof(linked_map) || link(path, linked_map) != 0 ||
		    omaq_direct_state_reconcile(dir, current, 1, &reinvite) == 0)
			fail("direct state map hardlink");
		unlink(linked_map);
		map = fopen(path, "wb");
		if (!map || fprintf(map, "OMAQDF1\n0\t%s\n\n", key) < 0 || fclose(map) != 0 ||
		    chmod(path, 0600) != 0 ||
		    omaq_direct_state_reconcile(dir, current, 1, &reinvite) == 0)
			fail("direct state map blank record");
		if (write_private_test_file(path,
			"OMAQDF1\n0\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n") != 0)
			fail("direct state map restore");
		map = fopen(path, "wb");
		if (!map || fwrite("OMAQDF1\n0\t", 1, 10, map) != 10 || fputc('\0', map) == EOF ||
		    fprintf(map, "%s\n", key) < 0 || fclose(map) != 0 || chmod(path, 0600) != 0 ||
		    omaq_direct_state_reconcile(dir, current, 1, &reinvite) == 0)
			fail("direct state map nul rejection");
		if (write_private_test_file(path,
			"OMAQDF1\n0\taaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n") != 0)
			fail("direct state map final restore");
	}
	{
		const char *prefixes[] = { ".direct-friends.tsv.tmp.",
			".direct-add.pending.tmp.", ".direct-remove.pending.tmp." };
		for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
			if (snprintf(path, sizeof(path), "%s/%s%ld", dir, prefixes[i],
				     (long)getpid()) >= (int)sizeof(path) ||
			    write_private_test_file(path, "temporary\n") != 0)
				fail("direct state home temp fixture");
		}
		if (omaq_direct_state_reconcile(dir, current, 1, &reinvite) != 0)
			fail("direct state home temp cleanup");
		for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
			if (snprintf(path, sizeof(path), "%s/%s%ld", dir, prefixes[i],
				     (long)getpid()) >= (int)sizeof(path) || access(path, F_OK) == 0)
				fail("direct state home temp residue");
		}
	}

	/* A durable old binding, not the current numeric handle, owns legacy state. */
	if (create_legacy_direct_fixture(dir, "0", "history A\n") != 0)
		fail("direct state legacy fixture");
	memcpy(current[0].key, other, 65);
	if (omaq_direct_state_reconcile_removed(dir, current, 1, key, &reinvite) != 0 ||
	    reinvite ||
	    omaq_direct_state_id(key, stable, sizeof(stable)) != 0 ||
	    snprintf(path, sizeof(path), "%s/history/%s/messages.jsonl", dir, stable) >=
	    (int)sizeof(path) || read_file(path, body, sizeof(body)) != 0 ||
	    strcmp(body, "history A") != 0)
		fail("direct state reused handle binding");
	if (omaq_direct_state_id(other, stable, sizeof(stable)) != 0 ||
	    snprintf(path, sizeof(path), "%s/history/%s/messages.jsonl", dir, stable) >=
	    (int)sizeof(path) || access(path, F_OK) == 0)
		fail("direct state reused handle isolation");
	if (snprintf(path, sizeof(path), "%s/ratchet/sess/%s-1", dir, stable) >=
	    (int)sizeof(path)) {
		fail("direct state oversized path");
	} else {
		FILE *oversized = fopen(path, "wb");
		if (!oversized || ftruncate(fileno(oversized), OMAQ_RATCHET_RECORD_MAX + 1) != 0 ||
		    fclose(oversized) != 0 || chmod(path, 0600) != 0 ||
		    omaq_direct_state_reconcile(dir, current, 1, &reinvite) == 0)
			fail("direct state oversized record rejection");
		unlink(path);
	}

	/* A collision is archived outside the legacy prefix and remains convergent. */
	if (omaq_direct_state_id(key, stable, sizeof(stable)) != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/rk/%s", dir, stable) >=
	    (int)sizeof(path) || write_private_test_file(path, "current pin\n") != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/rk/0", dir) >= (int)sizeof(path) ||
	    write_private_test_file(path, "stale pin\n") != 0)
		fail("direct state collision fixture");
	if (omaq_direct_state_migrate(dir, "0", key) != 0)
		fail("direct state collision first migration");
	if (omaq_direct_state_migrate(dir, "0", key) != 0)
		fail("direct state collision convergence");
	if (snprintf(path, sizeof(path), "%s/ratchet/rk/.legacy-direct.0.0", dir) >=
	    (int)sizeof(path) || access(path, R_OK) != 0)
		fail("direct state collision archive");
	if (omaq_direct_state_id(key, stable, sizeof(stable)) != 0 ||
	    snprintf(path, sizeof(path), "%s/history/%s/messages.jsonl.tmp.123", dir,
		     stable) >= (int)sizeof(path) ||
	    write_private_test_file(path, "temporary history\n") != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/sess/%s-1.tmp", dir, stable) >=
		(int)sizeof(path) || write_private_test_file(path, "temporary session\n") != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/sess/%s-2.tmp", dir, stable) >=
		(int)sizeof(path) || write_private_test_file(path, "") != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/rk/%s.tmp", dir, stable) >=
		(int)sizeof(path) || write_private_test_file(path, "temporary pin\n") != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/ident/%s.tmp", dir, stable) >=
		(int)sizeof(path) || write_private_test_file(path, "temporary identity\n") != 0 ||
	    omaq_direct_state_migrate(dir, "9", key) != 0)
		fail("direct state temporary cleanup");
	if (snprintf(path, sizeof(path), "%s/history/%s/messages.jsonl.tmp.123", dir,
		     stable) >= (int)sizeof(path) || access(path, F_OK) == 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/sess/%s-1.tmp", dir, stable) >=
		(int)sizeof(path) || access(path, F_OK) == 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/sess/%s-2.tmp", dir, stable) >=
		(int)sizeof(path) || access(path, F_OK) == 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/rk/%s.tmp", dir, stable) >=
		(int)sizeof(path) || access(path, F_OK) == 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/ident/%s.tmp", dir, stable) >=
		(int)sizeof(path) || access(path, F_OK) == 0)
		fail("direct state temporary residue");

	/* No pre-existing binding means numeric state is archived, never assigned. */
	if (!mkdtemp(unbound) || make_direct_state_dirs(unbound) != 0 ||
	    create_legacy_direct_fixture(unbound, "0", "ambiguous\n") != 0) {
		fail("direct state unbound fixture");
		return;
	}
	memcpy(current[0].key, key, 65);
	if (snprintf(path, sizeof(path), "%s/ratchet/sess/0-2.tmp", unbound) >=
	    (int)sizeof(path) || write_private_test_file(path, "") != 0 ||
	    snprintf(path, sizeof(path), "%s/history/0/messages.jsonl.tmp.123", unbound) >=
	    (int)sizeof(path) || write_private_test_file(path, "") != 0)
		fail("direct state legacy temp fixture");
	reinvite = 0;
	if (omaq_direct_state_reconcile(unbound, current, 1, &reinvite) != 0 || !reinvite ||
	    snprintf(path, sizeof(path), "%s/history/.legacy-direct.0.0/messages.jsonl",
		     unbound) >= (int)sizeof(path) || access(path, R_OK) != 0 ||
	    omaq_direct_state_id(key, stable, sizeof(stable)) != 0 ||
	    snprintf(path, sizeof(path), "%s/history/%s", unbound, stable) >=
	    (int)sizeof(path) || access(path, F_OK) == 0)
		fail("direct state ambiguous archive");
	if (snprintf(path, sizeof(path), "%s/ratchet/sess/0-2.tmp", unbound) >=
	    (int)sizeof(path) || access(path, F_OK) == 0 ||
	    snprintf(path, sizeof(path),
		     "%s/history/.legacy-direct.0.0/messages.jsonl.tmp.123", unbound) >=
	    (int)sizeof(path) || access(path, F_OK) == 0)
		fail("direct state legacy temp residue");
	{
		char retained[OMAQ_DIRECT_STATE_ID_MAX];
		if (omaq_direct_state_bound_id(unbound, "0", retained, sizeof(retained)) != 1 ||
		    strcmp(retained, stable) != 0 ||
		    snprintf(path, sizeof(path), "%s/direct-state-reinvite.required", unbound) >=
			(int)sizeof(path) || access(path, R_OK) != 0)
			fail("direct state migration did not retain the current contact binding");
	}

	/* Stable destinations, path components, and session suffixes fail closed. */
	if (snprintf(path, sizeof(path), "%s/ratchet/rk/1", dir) >= (int)sizeof(path) ||
	    symlink("/tmp", path) != 0 || omaq_direct_state_migrate(dir, "1", other) == 0)
		fail("direct state symlink rejection");
	unlink(path);
	memcpy(current[0].key, other, 65);
	if (omaq_direct_state_id(other, stable, sizeof(stable)) != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet/rk/%s", dir, stable) >=
	    (int)sizeof(path) || symlink("/tmp", path) != 0 ||
	    omaq_direct_state_reconcile(dir, current, 1, &reinvite) == 0)
		fail("direct state stable symlink rejection");
	unlink(path);
	if (snprintf(path, sizeof(path), "%s/ratchet/ident/%s.tmp", dir, stable) >=
	    (int)sizeof(path) || symlink("/tmp", path) != 0 ||
	    omaq_direct_state_reconcile(dir, current, 1, &reinvite) == 0)
		fail("direct state temp symlink rejection");
	unlink(path);
	memcpy(current[0].key, key, 65);
	if (snprintf(path, sizeof(path), "%s/ratchet/rk/.no-legacy", dir) >=
	    (int)sizeof(path) || write_private_test_file(path, "reserved\n") != 0 ||
	    omaq_direct_state_reconcile(dir, current, 1, &reinvite) == 0)
		fail("direct state reserved source rejection");
	unlink(path);
	if (snprintf(path, sizeof(path), "%s/ratchet/sess/2-01", dir) >=
	    (int)sizeof(path) || write_private_test_file(path, "bad session\n") != 0 ||
	    omaq_direct_state_migrate(dir, "2", key) == 0)
		fail("direct state session suffix rejection");
	unlink(path);
	if (snprintf(path, sizeof(path), "%s/history/3", dir) >= (int)sizeof(path) ||
	    mkdir(path, 0700) != 0 ||
	    snprintf(path, sizeof(path), "%s/history/3/messages.jsonl", dir) >=
	    (int)sizeof(path) || symlink("/tmp/not-history", path) != 0 ||
	    omaq_direct_state_migrate(dir, "3", key) == 0)
		fail("direct state history symlink rejection");
	if (omaq_direct_state_id("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
				 stable, sizeof(stable)) == 0 ||
	    omaq_direct_state_migrate(dir, "01", key) == 0)
		fail("direct state canonical validation");
	if (snprintf(path, sizeof(path), "%s/ratchet/rk/4", dir) >= (int)sizeof(path) ||
	    write_private_test_file(path, "unsafe pin\n") != 0 || chmod(path, 0666) != 0 ||
	    omaq_direct_state_migrate(dir, "4", key) == 0)
		fail("direct state legacy pin mode rejection");
	unlink(path);
	{
		char sentinel[768];
		if (snprintf(sentinel, sizeof(sentinel), "%s/legacy-pin-sentinel", dir) >=
		    (int)sizeof(sentinel) || write_private_test_file(sentinel, "hardlink pin\n") != 0 ||
		    snprintf(path, sizeof(path), "%s/ratchet/rk/5", dir) >= (int)sizeof(path) ||
		    link(sentinel, path) != 0 || omaq_direct_state_migrate(dir, "5", key) == 0)
			fail("direct state legacy pin hardlink rejection");
		unlink(path);
		unlink(sentinel);
	}
	if (!mkdtemp(linked) ||
	    snprintf(path, sizeof(path), "%s/history", linked) >= (int)sizeof(path) ||
	    mkdir(path, 0700) != 0 ||
	    snprintf(path, sizeof(path), "%s/avatars", linked) >= (int)sizeof(path) ||
	    mkdir(path, 0700) != 0 ||
	    snprintf(path, sizeof(path), "%s/ratchet", linked) >= (int)sizeof(path) ||
	    symlink("/tmp", path) != 0 ||
	    omaq_direct_state_reconcile(linked, current, 1, &reinvite) == 0)
		fail("direct state intermediate symlink rejection");
	{
		char pending_key[65];
		if (!mkdtemp(removal) || make_direct_state_dirs(removal) != 0) {
			fail("direct state removal fixture");
		} else {
			char pending_pin[65], removal_stable[OMAQ_DIRECT_STATE_ID_MAX];
			char extra_dir[768];
			char rk_path[768], ident_path[768], session_path[768];
			char prekey_path[768], boot_path[768], reply_path[768], history_path[768];
			current[0].number = 0;
			memcpy(current[0].key, key, 65);
			if (snprintf(extra_dir, sizeof(extra_dir), "%s/ratchet/pre", removal) >=
				    (int)sizeof(extra_dir) || mkdir(extra_dir, 0700) != 0 ||
			    snprintf(extra_dir, sizeof(extra_dir), "%s/ratchet/boot", removal) >=
				    (int)sizeof(extra_dir) || mkdir(extra_dir, 0700) != 0 ||
			    snprintf(extra_dir, sizeof(extra_dir), "%s/ratchet/reply", removal) >=
				    (int)sizeof(extra_dir) || mkdir(extra_dir, 0700) != 0 ||
			    omaq_direct_state_id(key, removal_stable, sizeof(removal_stable)) != 0 ||
			    snprintf(rk_path, sizeof(rk_path), "%s/ratchet/rk/%s", removal,
				     removal_stable) >= (int)sizeof(rk_path) ||
			    snprintf(ident_path, sizeof(ident_path), "%s/ratchet/ident/%s", removal,
				     removal_stable) >= (int)sizeof(ident_path) ||
			    snprintf(session_path, sizeof(session_path), "%s/ratchet/sess/%s-1", removal,
				     removal_stable) >= (int)sizeof(session_path) ||
			    snprintf(prekey_path, sizeof(prekey_path), "%s/ratchet/pre/%s-42", removal,
				     removal_stable) >= (int)sizeof(prekey_path) ||
			    snprintf(boot_path, sizeof(boot_path), "%s/ratchet/boot/%s", removal,
				     removal_stable) >= (int)sizeof(boot_path) ||
			    snprintf(reply_path, sizeof(reply_path), "%s/ratchet/reply/%s", removal,
				     removal_stable) >= (int)sizeof(reply_path) ||
			    snprintf(history_path, sizeof(history_path), "%s/history/%s", removal,
				     removal_stable) >= (int)sizeof(history_path) ||
			    mkdir(history_path, 0700) != 0 ||
			    write_private_test_file(rk_path, "pin\n") != 0 ||
			    write_private_test_file(ident_path, "identity\n") != 0 ||
			    write_private_test_file(session_path, "session\n") != 0 ||
			    write_private_test_file(prekey_path, "prekey\n") != 0 ||
			    write_private_test_file(boot_path, "boot\n") != 0 ||
			    write_private_test_file(reply_path, "reply\n") != 0 ||
			    omaq_direct_state_reconcile(removal, current, 1, &reinvite) != 0 ||
			    omaq_direct_state_add_begin(removal, key, other) != 0 ||
			    omaq_direct_state_add_pending(removal, pending_key, pending_pin) != 1 ||
			    strcmp(pending_key, key) != 0 || strcmp(pending_pin, other) != 0 ||
			    omaq_direct_state_add_finish(removal) != 0 ||
			    omaq_direct_state_add_pending(removal, pending_key, pending_pin) != 0 ||
			    omaq_direct_state_remove_begin(removal, key) != 0 ||
			    omaq_direct_state_remove_pending(removal, pending_key) != 1 ||
			    strcmp(pending_key, key) != 0 ||
			    omaq_direct_state_reconcile_removed(removal, current, 0, key,
							&reinvite) != 0 ||
			    omaq_direct_state_remove_finish(removal) != 0 ||
			    omaq_direct_state_remove_pending(removal, pending_key) != 0 ||
			    omaq_direct_state_reconcile(removal, current, 0, &reinvite) != 0 ||
			    access(rk_path, F_OK) == 0 || access(ident_path, F_OK) == 0 ||
			    access(session_path, F_OK) == 0 || access(prekey_path, F_OK) == 0 ||
			    access(boot_path, F_OK) == 0 || access(reply_path, F_OK) == 0 ||
			    access(history_path, F_OK) != 0)
				fail("direct state removal journal");
		}
	}
	{
		omaq_direct_state_friend over[OMAQ_DIRECT_STATE_FRIEND_MAX + 1] = { 0 };
		if (omaq_direct_state_reconcile(dir, over,
			OMAQ_DIRECT_STATE_FRIEND_MAX + 1, &reinvite) == 0)
			fail("direct state friend bound");
	}
}

static void test_ratchet_pins(void)
{
	char dir[] = "/tmp/omaq-rk-XXXXXX";
	char got[OMAQ_RK_HEX + 1], path[512];
	const char *stable = "d:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	struct stat st;

	if (!mkdtemp(dir)) {
		fail("rk mkdtemp");
		return;
	}
	if (omaq_ratchet_pin_get(dir, "0", got, sizeof(got)) != -1 ||
	    omaq_ratchet_pin_set(dir, "0",
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0)
		fail("numeric ratchet peer rejected");
	if (!omaq_ratchet_peer_ok(stable) || omaq_ratchet_peer_ok("d:AAAA") ||
	    omaq_ratchet_pin_set(dir, stable,
		"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb") != 0 ||
	    omaq_ratchet_pin_get(dir, stable, got, sizeof(got)) != 1)
		fail("stable ratchet peer");
	if (snprintf(path, sizeof(path), "%s/ratchet/rk/%s", dir, stable) >=
	    (int)sizeof(path))
		fail("rk path test");
	else if (stat(path, &st) != 0 || (st.st_mode & 0777) != 0600)
		fail("rk permissions");
	else {
		FILE *extra = fopen(path, "a");
		if (!extra || fputc('x', extra) == EOF || fclose(extra) != 0 ||
		    omaq_ratchet_pin_get(dir, stable, got, sizeof(got)) != -1 ||
		    truncate(path, OMAQ_RK_HEX + 1) != 0)
			fail("rk trailing data rejection");
	}
	if (omaq_ratchet_pin_set(dir, "../x",
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0)
		fail("rk path escape");
	{
		const char *stable_b =
			"d:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
		char sentinel[512], temporary[700], body[64];
		FILE *file;
		if (snprintf(sentinel, sizeof(sentinel), "%s/sentinel", dir) >=
		    (int)sizeof(sentinel) || !(file = fopen(sentinel, "w")) ||
		    fputs("pin sentinel\n", file) < 0 || fclose(file) != 0 ||
		    snprintf(temporary, sizeof(temporary), "%s/ratchet/rk/%s.tmp", dir,
			     stable_b) >= (int)sizeof(temporary) || symlink(sentinel, temporary) != 0 ||
		    omaq_ratchet_pin_set(dir, stable_b,
			"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa") == 0 ||
		    read_file(sentinel, body, sizeof(body)) != 0 ||
		    strcmp(body, "pin sentinel") != 0)
			fail("rk temp symlink rejection");
		unlink(temporary);
	}
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
	static const char direct[] =
		"d:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	static const char group[] =
		"g:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	char dir[] = "/tmp/omaq-surf-XXXXXX";
	omaq_surface surface, got, listed[2];
	int listed_n;

	if (!mkdtemp(dir)) {
		fail("surface mkdtemp");
		return;
	}
	memset(&surface, 0, sizeof(surface));
	memcpy(surface.conversation, direct, sizeof(direct));
	memcpy(surface.monitor, "DP-1", 5);
	surface.x = 12;
	surface.y = 34;
	surface.width = 460;
	surface.height = 500;
	surface.pinned = 0;
	if (omaq_surface_set(dir, &surface) != 0)
		fail("surface stable set");
	if (omaq_surface_get(dir, direct, &got) != 0)
		fail("surface stable get");
	else if (got.x != 12 || got.y != 34 || got.width != 460 ||
		 got.height != 500 || strcmp(got.monitor, "DP-1") != 0 || got.pinned)
		fail("surface stable fields");
	surface.pinned = 1;
	surface.x = 99;
	if (omaq_surface_set(dir, &surface) != 0)
		fail("surface stable update");
	if (omaq_surface_get(dir, direct, &got) != 0 || !got.pinned || got.x != 99)
		fail("surface stable pinned");
	if (omaq_surface_get(dir, group, &got) == 0)
		fail("surface missing");
	if (omaq_surface_list(dir, listed, 2) != 1 ||
	    strcmp(listed[0].conversation, direct) != 0)
		fail("surface stable list");
	listed_n = omaq_surface_list(dir, listed, 0);
	if (listed_n != -1)
		fail("surface list cap");
	if (omaq_surface_set(dir, &(omaq_surface){ .conversation = "0" }) == 0 ||
	    omaq_surface_set(dir, &(omaq_surface){ .conversation = "a/../b" }) == 0)
		fail("surface unstable id rejection");
	{
		char path[256], body[640];
		int written;

		written = snprintf(body, sizeof(body),
			"{\"conversation\":\"0\",\"monitor\":\"old\",\"x\":1,\"y\":2,\"pinned\":true}\n"
			"{\"conversation\":\"%s\",\"monitor\":\"new\",\"x\":3,\"y\":4,\"pinned\":true}\n",
			group);
		if (written < 0 || (size_t)written >= sizeof(body) ||
		    snprintf(path, sizeof(path), "%s/surfaces.jsonl", dir) >=
			    (int)sizeof(path) ||
		    unlink(path) != 0 || write_private_test_file(path, body) != 0 ||
		    omaq_surface_legacy_direct_present(dir) != 1 ||
		    omaq_surface_list(dir, listed, 2) >= 0 ||
		    omaq_surface_set(dir, &surface) == 0 ||
		    omaq_surface_discard_legacy_direct(dir) != 1 ||
		    omaq_surface_legacy_direct_present(dir) != 0 ||
		    omaq_surface_list(dir, listed, 2) != 1 ||
		    strcmp(listed[0].conversation, group) != 0)
			fail("surface legacy direct discard");
	}
	{
		char path[256], sentinel[256];
		const char *duplicate =
			"{\"conversation\":\"0\",\"conversation\":\"1\",\"monitor\":\"bad\",\"x\":1,\"y\":2,\"pinned\":true}\n";

		if (snprintf(path, sizeof(path), "%s/surfaces.jsonl", dir) >=
			    (int)sizeof(path) ||
		    unlink(path) != 0 || write_private_test_file(path, duplicate) != 0 ||
		    omaq_surface_legacy_direct_present(dir) >= 0 ||
		    omaq_surface_list(dir, listed, 2) >= 0 || unlink(path) != 0 ||
		    write_private_test_file(path,
			"{\"conversation\":\"0\",\"monitor\":\"bad\",\"x\":1,\"y\":2,\"pinned\":true}") != 0 ||
		    omaq_surface_legacy_direct_present(dir) >= 0 || unlink(path) != 0 ||
		    mkfifo(path, 0600) != 0 || omaq_surface_list(dir, listed, 2) >= 0 ||
		    unlink(path) != 0 ||
		    snprintf(sentinel, sizeof(sentinel), "%s/surface-sentinel", dir) >=
			    (int)sizeof(sentinel) ||
		    write_private_test_file(sentinel, "unchanged\n") != 0 ||
		    symlink(sentinel, path) != 0 || omaq_surface_list(dir, listed, 2) >= 0 ||
		    unlink(path) != 0 || unlink(sentinel) != 0 ||
		    omaq_surface_set(dir, &surface) != 0)
			fail("surface malformed input rejection");
	}
	{
		char temporary[256], sentinel[256], content[32];
		if (snprintf(temporary, sizeof(temporary), "%s/surfaces.jsonl.tmp", dir) >=
			    (int)sizeof(temporary) ||
		    write_private_test_file(temporary, "stale\n") != 0 ||
		    omaq_surface_set(dir, &surface) != 0 || access(temporary, F_OK) == 0 ||
		    snprintf(sentinel, sizeof(sentinel), "%s/sentinel", dir) >=
			    (int)sizeof(sentinel) ||
		    write_private_test_file(sentinel, "unchanged\n") != 0 ||
		    symlink(sentinel, temporary) != 0 || omaq_surface_set(dir, &surface) == 0 ||
		    read_file(sentinel, content, sizeof(content)) != 0 ||
		    strcmp(content, "unchanged") != 0)
			fail("surface temporary symlink rejection");
		unlink(temporary);
		unlink(sentinel);
	}
}

static void test_state_archive(void)
{
	char dir[] = "/tmp/omaq-state-archive-XXXXXX";
	char source[256], archive0[288], archive1[288], archive2[288];
	char sentinel[256], content[64];

	if (!mkdtemp(dir)) {
		fail("state archive mkdtemp");
		return;
	}
	if (snprintf(source, sizeof(source), "%s/preferences.json", dir) >=
		    (int)sizeof(source) ||
	    snprintf(archive0, sizeof(archive0),
		     "%s/preferences.json.legacy-direct.0", dir) >= (int)sizeof(archive0) ||
	    snprintf(archive1, sizeof(archive1),
		     "%s/preferences.json.legacy-direct.1", dir) >= (int)sizeof(archive1) ||
	    snprintf(archive2, sizeof(archive2),
		     "%s/preferences.json.legacy-direct.2", dir) >= (int)sizeof(archive2) ||
	    write_private_test_file(source, "legacy\n") != 0 ||
	    omaq_state_archive_copy(dir, "preferences.json") != 0 ||
	    omaq_state_archive_copy(dir, "preferences.json") != 0 ||
	    access(archive1, F_OK) == 0 ||
	    read_file(source, content, sizeof(content)) != 0 ||
	    strcmp(content, "legacy") != 0 ||
	    read_file(archive0, content, sizeof(content)) != 0 ||
	    strcmp(content, "legacy") != 0 ||
	    unlink(source) != 0 || write_private_test_file(source, "new legacy\n") != 0 ||
	    omaq_state_archive_copy(dir, "preferences.json") != 0 ||
	    read_file(archive1, content, sizeof(content)) != 0 ||
	    strcmp(content, "new legacy") != 0)
		fail("state archive copies");
	if (chmod(source, 0666) != 0 ||
	    omaq_state_archive_copy(dir, "preferences.json") == 0 ||
	    chmod(source, 0600) != 0)
		fail("state archive unsafe mode rejection");
	if (unlink(source) != 0 || write_private_test_file(source, "third\n") != 0 ||
	    mkfifo(archive2, 0600) != 0 ||
	    omaq_state_archive_copy(dir, "preferences.json") == 0 ||
	    unlink(archive2) != 0 || unlink(source) != 0 || mkfifo(source, 0600) != 0 ||
	    omaq_state_archive_copy(dir, "preferences.json") == 0 ||
	    unlink(source) != 0)
		fail("state archive fifo rejection");
	if (snprintf(sentinel, sizeof(sentinel), "%s/sentinel", dir) >=
		    (int)sizeof(sentinel) ||
	    write_private_test_file(sentinel, "unchanged\n") != 0 ||
	    symlink(sentinel, source) != 0 ||
	    omaq_state_archive_copy(dir, "preferences.json") == 0 ||
	    read_file(sentinel, content, sizeof(content)) != 0 ||
	    strcmp(content, "unchanged") != 0)
		fail("state archive symlink rejection");
	unlink(source);
	unlink(sentinel);
	unlink(archive1);
	unlink(archive0);
	rmdir(dir);
}

static void test_auto_open(void)
{
	static const char fingerprint[] =
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	static const char group[] =
		"g:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	char dir[] = "/tmp/omaq-auto-open-XXXXXX";
	char active[256], global[256], migrated[320], body[512];
	omaq_auto_open_state settings;
	omaq_auto_open_source source;

	if (!mkdtemp(dir)) {
		fail("auto open mkdtemp");
		return;
	}
	if (snprintf(active, sizeof(active), "%s/auto-open.%s.json", dir,
		     fingerprint) >= (int)sizeof(active) ||
	    snprintf(global, sizeof(global), "%s/auto-open.json", dir) >=
		    (int)sizeof(global) ||
	    snprintf(migrated, sizeof(migrated), "%s/auto-open.migrated.%s.json",
		     dir, fingerprint) >= (int)sizeof(migrated) ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) != 0 ||
	    source != OMAQ_AUTO_OPEN_SOURCE_NONE || !settings.direct_default ||
	    snprintf(body, sizeof(body),
		     "{\"version\":1,\"users\":{\"0\":true,\"%s\":false}}\n",
		     group) >= (int)sizeof(body) ||
	    write_private_test_file(active, body) != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) != 0 ||
	    source != OMAQ_AUTO_OPEN_SOURCE_LEGACY_ACTIVE ||
	    settings.direct_default || settings.count != 1 ||
	    strcmp(settings.entries[0].conversation, group) != 0 ||
	    settings.entries[0].enabled ||
	    omaq_auto_open_set(&settings,
		"d:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
		1) != 0 ||
	    omaq_auto_open_save(dir, fingerprint, &settings) != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) != 0 ||
	    source != OMAQ_AUTO_OPEN_SOURCE_CURRENT || settings.direct_default ||
	    settings.count != 2)
		fail("auto open stable migration");
	if (write_private_test_file(global, "{}\n") != 0 ||
	    omaq_auto_open_retire_global(dir, fingerprint) != 0 ||
	    access(global, F_OK) == 0 || access(migrated, F_OK) != 0)
		fail("auto open legacy retirement");
	unlink(migrated);
	if (unlink(active) != 0 ||
	    write_private_test_file(active,
		"{\"version\":1,\"users\":{\"0\":true},\"users\":{}}\n") != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) == 0 ||
	    unlink(active) != 0 ||
	    write_private_test_file(active,
		"{\"version\":\"2\",\"directDefault\":true,\"users\":{}}\n") != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) == 0 ||
	    unlink(active) != 0 ||
	    write_private_test_file(active,
		"{\"version\":2,\"directDefault\":true,\"users\":{\"0\":true}}\n") != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) == 0 ||
	    unlink(active) != 0 ||
	    write_private_test_file(active,
		"{\"version\":2,\"directDefault\":true,\"users\":{},\"extra\":true}\n") != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) == 0 ||
	    unlink(active) != 0 ||
	    write_private_test_file(active,
		"{\"version\":2,\"directDefault\":true,\"users\":{}}\n") != 0 ||
	    chmod(active, 0644) != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) != 0 ||
	    source != OMAQ_AUTO_OPEN_SOURCE_LEGACY_ACTIVE ||
	    chmod(active, 0666) != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) == 0 ||
	    unlink(active) != 0 || mkfifo(active, 0600) != 0 ||
	    omaq_auto_open_load(dir, fingerprint, &settings, &source) == 0)
		fail("auto open malformed input rejection");
	unlink(active);
	rmdir(dir);
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
	{
		char first[33], second[33];
		int canonical = 1;
		if (omaq_message_id_new(first, sizeof(first)) != 0 ||
		    omaq_message_id_new(second, sizeof(second)) != 0 ||
		    strlen(first) != 32 || strlen(second) != 32 || strcmp(first, second) == 0)
			canonical = 0;
		for (size_t i = 0; canonical && i < 32; i++)
			if (!((first[i] >= '0' && first[i] <= '9') ||
			      (first[i] >= 'a' && first[i] <= 'f')))
				canonical = 0;
		if (!canonical)
			fail("message ids are independent 128-bit lowercase hex values");
	}
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
	{
		omaq_receipt_outbox stable;
		omaq_receipt_outbox_init(&stable);
		if (omaq_receipt_outbox_add(&stable,
			"d:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
			"stable-receipt") != 1 ||
		    omaq_receipt_outbox_add(&stable,
			"d:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
			"bad-stable-receipt") >= 0)
			fail("stable receipt conversation validation");
		omaq_receipt_outbox_destroy(&stable);
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

#ifdef HAVE_AVATAR_DECODERS
static unsigned char *avatar_noise_pixels(uint32_t width, uint32_t height)
{
	unsigned char *pixels = malloc((size_t)width * height * 3u);
	uint32_t state = 0x9e3779b9u;

	if (!pixels)
		return NULL;
	for (size_t i = 0; i < (size_t)width * height * 3u; i++) {
		state = state * 1664525u + 1013904223u;
		pixels[i] = (unsigned char)(state >> 24);
	}
	return pixels;
}

static int write_avatar_jpeg(const char *path, const unsigned char *pixels,
			     uint32_t width, uint32_t height)
{
	struct jpeg_compress_struct compressor;
	struct jpeg_error_mgr error;
	FILE *file = fopen(path, "wb");

	if (!file)
		return -1;
	compressor.err = jpeg_std_error(&error);
	jpeg_create_compress(&compressor);
	jpeg_stdio_dest(&compressor, file);
	compressor.image_width = width;
	compressor.image_height = height;
	compressor.input_components = 3;
	compressor.in_color_space = JCS_RGB;
	jpeg_set_defaults(&compressor);
	jpeg_set_quality(&compressor, 45, TRUE);
	jpeg_start_compress(&compressor, TRUE);
	while (compressor.next_scanline < compressor.image_height) {
		JSAMPROW row = (JSAMPROW)(pixels +
			(size_t)compressor.next_scanline * width * 3u);
		if (jpeg_write_scanlines(&compressor, &row, 1) != 1) {
			jpeg_destroy_compress(&compressor);
			fclose(file);
			return -1;
		}
	}
	jpeg_finish_compress(&compressor);
	jpeg_destroy_compress(&compressor);
	return fclose(file) == 0 && chmod(path, 0600) == 0 ? 0 : -1;
}

static int write_avatar_webp(const char *path, const unsigned char *pixels,
			     uint32_t width, uint32_t height)
{
	uint8_t *encoded = NULL;
	size_t size = WebPEncodeRGB(pixels, (int)width, (int)height,
				    (int)(width * 3u), 45.0f, &encoded);
	FILE *file;
	int rc = -1;

	if (size == 0 || size > OMAQ_AVATAR_MAX || !(file = fopen(path, "wb"))) {
		WebPFree(encoded);
		return -1;
	}
	{
		int write_ok = fwrite(encoded, 1, size, file) == size && fflush(file) == 0;
		int close_ok = fclose(file) == 0;
		if (write_ok && close_ok && chmod(path, 0600) == 0)
			rc = 0;
	}
	WebPFree(encoded);
	return rc;
}
#endif

static void test_avatar(void)
{
	if (!omaq_avatar_id_ok("self") || !omaq_avatar_id_ok("0") || !omaq_avatar_id_ok("12") ||
	    !omaq_avatar_id_ok("d:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"))
		fail("avatar id ok");
	if (omaq_avatar_id_ok("") || omaq_avatar_id_ok("01") ||
	    omaq_avatar_id_ok("d:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA") ||
	    omaq_avatar_id_ok("../x") || omaq_avatar_id_ok("a/b") ||
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
		static const unsigned char image[] = {
			0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
			0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
			0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
			0x08, 0x04, 0x00, 0x00, 0x00, 0xb5, 0x1c, 0x0c,
			0x02, 0x00, 0x00, 0x00, 0x0b, 0x49, 0x44, 0x41,
			0x54, 0x78, 0xda, 0x63, 0x64, 0xf8, 0x0f, 0x00,
			0x01, 0x05, 0x01, 0x01, 0x27, 0x18, 0xe3, 0x66,
			0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44,
			0xae, 0x42, 0x60, 0x82
		};
		FILE *f;
		struct stat st;

		(void)mkdir("/tmp/omaq-av", 0700);
		f = fopen(src, "wb");
		if (!f)
			fail("avatar fixture open");
		else {
			if (fwrite(image, 1, sizeof(image), f) != sizeof(image) ||
			    fwrite("TRAILING-PAYLOAD", 1, 16, f) != 16)
				fail("avatar fixture write");
			fclose(f);
		}
		if (omaq_inline_image_validate_file(src) != 0 ||
		    omaq_inline_image_canonicalize_file(src) != 0)
			fail("inline image validation and canonicalization");
		f = fopen(src, "rb");
		if (!f) {
			fail("inline image canonical open");
		} else {
			unsigned char canonical[1024];
			size_t canonical_size = fread(canonical, 1, sizeof(canonical), f);
			fclose(f);
			if (memmem(canonical, canonical_size, "TRAILING-PAYLOAD", 16) != NULL)
				fail("inline image canonical trailing payload");
		}
		f = fopen(src, "ab");
		if (!f || fwrite("TRAILING-PAYLOAD", 1, 16, f) != 16 || fclose(f) != 0)
			fail("avatar trailing payload restore");
		if (omaq_avatar_install("/tmp/omaq-av", "self", src, d, sizeof(d)) != 0 ||
		    stat("/tmp/omaq-av/avatars/self.png", &st) != 0 || st.st_size <= 0 ||
		    omaq_avatar_validate_file("/tmp/omaq-av/avatars/self.png") != 0)
			fail("avatar install large");
		write_private_test_file("/tmp/omaq-av/avatars/self.png.incoming.1.2.deadbeef",
					"partial");
		write_private_test_file("/tmp/omaq-av/avatars/self.png.tmp.123.deadbeef",
					"partial");
		write_private_test_file("/tmp/omaq-av/avatars/self.png.incoming.01.2.deadbeef",
					"keep");
		write_private_test_file("/tmp/omaq-av/avatars/self.png.tmp.0.deadbeef", "keep");
		if (omaq_avatar_cleanup_temps("/tmp/omaq-av") != 0 ||
		    access("/tmp/omaq-av/avatars/self.png.incoming.1.2.deadbeef", F_OK) == 0 ||
		    access("/tmp/omaq-av/avatars/self.png.tmp.123.deadbeef", F_OK) == 0 ||
		    access("/tmp/omaq-av/avatars/self.png.incoming.01.2.deadbeef", F_OK) != 0 ||
		    access("/tmp/omaq-av/avatars/self.png.tmp.0.deadbeef", F_OK) != 0)
			fail("avatar crash temp cleanup");
		unlink("/tmp/omaq-av/avatars/self.png.incoming.01.2.deadbeef");
		unlink("/tmp/omaq-av/avatars/self.png.tmp.0.deadbeef");
		f = fopen("/tmp/omaq-av/avatars/self.png", "rb");
		if (!f) {
			fail("avatar canonical open");
		} else {
			unsigned char canonical[1024];
			size_t canonical_size = fread(canonical, 1, sizeof(canonical), f);
			fclose(f);
			if (memmem(canonical, canonical_size, "TRAILING-PAYLOAD", 16) != NULL)
				fail("avatar canonical trailing payload");
		}
		f = fopen("/tmp/omaq-av/avatars/self.png", "wb");
		if (!f || fwrite("not-an-image", 1, 12, f) != 12 || fclose(f) != 0 ||
		    omaq_inline_image_validate_file("/tmp/omaq-av/avatars/self.png") == 0 ||
		    omaq_inline_image_canonicalize_file("/tmp/omaq-av/avatars/self.png") == 0 ||
		    omaq_avatar_validate_file("/tmp/omaq-av/avatars/self.png") == 0 ||
		    omaq_avatar_reconcile("/tmp/omaq-av", "self") != -1 ||
		    access("/tmp/omaq-av/avatars/self.png", F_OK) == 0)
			fail("avatar received decode validation");
		f = fopen("/tmp/omaq-inline-invalid.png", "wb");
		if (!f || fwrite(image, 1, 8, f) != 8 ||
		    fwrite("invalid-body", 1, 12, f) != 12 || fclose(f) != 0 ||
		    chmod("/tmp/omaq-inline-invalid.png", 0600) != 0 ||
		    omaq_inline_image_validate_file("/tmp/omaq-inline-invalid.png") == 0 ||
		    omaq_inline_image_canonicalize_file("/tmp/omaq-inline-invalid.png") == 0)
			fail("inline image spoofed magic rejection");
		unlink("/tmp/omaq-inline-invalid.png");
		f = fopen("/tmp/omaq-inline-limit.png", "wb");
		if (!f || fwrite(image, 1, sizeof(image), f) != sizeof(image) ||
		    fflush(f) != 0 || ftruncate(fileno(f), OMAQ_INLINE_IMAGE_SOURCE_MAX) != 0 ||
		    fclose(f) != 0 || chmod("/tmp/omaq-inline-limit.png", 0600) != 0 ||
		    omaq_inline_image_validate_file("/tmp/omaq-inline-limit.png") != 0)
			fail("inline image exact source limit");
		f = fopen("/tmp/omaq-inline-limit.png", "r+b");
		if (!f || ftruncate(fileno(f), OMAQ_INLINE_IMAGE_SOURCE_MAX + 1u) != 0 ||
		    fclose(f) != 0 ||
		    omaq_inline_image_validate_file("/tmp/omaq-inline-limit.png") == 0)
			fail("inline image source over limit");
		unlink("/tmp/omaq-inline-limit.png");
#ifdef HAVE_AVATAR_DECODERS
		{
			char wide[] = "/tmp/omaq-avatar-wide.png";
			png_image image_info;
			unsigned char *pixels = calloc(4097u, 4u);
			memset(&image_info, 0, sizeof(image_info));
			image_info.version = PNG_IMAGE_VERSION;
			image_info.width = 4097;
			image_info.height = 1;
			image_info.format = PNG_FORMAT_RGBA;
			if (!pixels || !png_image_write_to_file(&image_info, wide, 0, pixels, 0, NULL) ||
			    chmod(wide, 0600) != 0 || omaq_avatar_validate_file(wide) == 0 ||
			    rename(wide, "/tmp/omaq-av/avatars/self.png") != 0 ||
			    omaq_avatar_reconcile("/tmp/omaq-av", "self") != -1 ||
			    access("/tmp/omaq-av/avatars/self.png", F_OK) == 0)
				fail("avatar dimension bound");
			free(pixels);
			unlink(wide);
		}
		{
			const uint32_t noisy_width = 768, noisy_height = 768;
			unsigned char *pixels = avatar_noise_pixels(noisy_width, noisy_height);
			const char *sources[2] = {
				"/tmp/omaq-avatar-noise.jpg", "/tmp/omaq-avatar-noise.webp"
			};
			const char *ids[2] = { "60", "61" };
			if (!pixels || write_avatar_jpeg(sources[0], pixels, noisy_width,
						      noisy_height) != 0 ||
			    write_avatar_webp(sources[1], pixels, noisy_width, noisy_height) != 0)
				fail("compressed avatar fixtures");
			for (int format = 0; format < 2; format++) {
				char output[256], inline_output[256];
				struct stat output_status;
				png_image output_image;
				memset(&output_image, 0, sizeof(output_image));
				output_image.version = PNG_IMAGE_VERSION;
				if (snprintf(inline_output, sizeof(inline_output),
					     "/tmp/omaq-inline-%d.png", format) >=
						(int)sizeof(inline_output) ||
				    write_private_test_file(inline_output, "staging") != 0 ||
				    omaq_inline_image_import_file(sources[format], inline_output) != 0 ||
				    omaq_inline_image_validate_file(inline_output) != 0)
					fail("compressed inline image canonical import");
				unlink(inline_output);
				if (omaq_avatar_install("/tmp/omaq-av", ids[format], sources[format],
							output, sizeof(output)) != 0 ||
				    stat(output, &output_status) != 0 ||
				    output_status.st_size > OMAQ_AVATAR_MAX ||
				    !png_image_begin_read_from_file(&output_image, output) ||
				    (output_image.width >= noisy_width &&
				     output_image.height >= noisy_height))
					fail("compressed avatar canonical downscale");
				png_image_free(&output_image);
				unlink(output);
				unlink(sources[format]);
			}
			free(pixels);
		}
#endif
		for (int avatar_index = 0; avatar_index < 70; avatar_index++) {
			char avatar_id[16], avatar_path[256];
			if (snprintf(avatar_id, sizeof(avatar_id), "%d", avatar_index) >=
				    (int)sizeof(avatar_id) ||
			    snprintf(avatar_path, sizeof(avatar_path),
				     "/tmp/omaq-av/avatars/%s.png", avatar_id) >=
				    (int)sizeof(avatar_path) ||
			    !(f = fopen(avatar_path, "wb")) ||
			    fwrite(image, 1, sizeof(image), f) != sizeof(image) || fclose(f) != 0 ||
			    chmod(avatar_path, 0600) != 0 ||
			    omaq_avatar_reconcile("/tmp/omaq-av", avatar_id) != 1 ||
			    access(avatar_path, R_OK) != 0)
				fail("avatar cache churn");
		}
		for (int avatar_index = 0; avatar_index < 70; avatar_index++) {
			char avatar_path[256];
			if (snprintf(avatar_path, sizeof(avatar_path),
				     "/tmp/omaq-av/avatars/%d.png", avatar_index) <
				    (int)sizeof(avatar_path))
				unlink(avatar_path);
		}
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
	test_group_file_wire();
	test_group_invite();
	test_direct_state();
	test_ratchet_pins();
	test_group_id();
	test_group_plan();
	test_surface();
	test_sound();
	test_state_archive();
	test_auto_open();
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
