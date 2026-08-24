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
		char id[64], *updated = NULL;
		size_t updated_n = 0;
		if (omaq_message_append_with_id(dir, "c1", "me", "editable", "out", id, sizeof(id)) != 0 ||
		    omaq_message_edit(dir, "c1", id, "edited") != 0 ||
		    omaq_store_update_receipt(dir, "c1", id, "delivered") != 0 ||
		    omaq_store_update_receipt(dir, "c1", id, "read") != 0 ||
		    omaq_store_update_receipt(dir, "c1", id, "delivered") != 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"text\":\"edited\"") || !strstr(updated, "\"edited\":true") ||
		    !strstr(updated, "\"receipt\":\"read\"}"))
			fail("message edit");
		free(updated);
		updated = NULL;
		if (omaq_store_message_exists(dir, "c1", id) != 1 ||
		    omaq_store_update_reaction(dir, "c1", id, "❤️", "me") != 0 ||
		    omaq_store_update_reaction(dir, "c1", id, "🔥", "peer") != 0 ||
		    omaq_store_update_reaction(dir, "c1", id, "🔥", "peer") != 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"reaction_me\":\"❤️\"") ||
		    !strstr(updated, "\"reaction_peer\":\"🔥\"") ||
		    omaq_store_message_exists(dir, "c1", "missing") != 0 ||
		    omaq_store_update_reaction(dir, "c1", "missing", "👍", "me") != -2)
			fail("message reaction store");
		free(updated);
		if (omaq_store_append(dir, "reaction-bad", "{\"id\":\"broken-1\",\"text\":\"broken\"") != 0 ||
		    omaq_store_update_reaction(dir, "reaction-bad", "broken-1", "👍", "peer") != -1)
			fail("message malformed reaction store");
		if (omaq_message_append_id(dir, "c1", "peer", "peer text", "in", "peer-1") != 0 ||
		    omaq_message_apply_delete(dir, "c1", "peer-1") != 0 ||
		    omaq_message_history(dir, "c1", 20, &updated, &updated_n) != 0 ||
		    !updated || !strstr(updated, "\"id\":\"peer-1\"") || !strstr(updated, "\"deleted\":true"))
			fail("message delete");
		free(updated);
		updated = NULL;
		if (omaq_message_append_file_with_id(dir, "c1", "peer", "song.mp3", "in",
						     id, sizeof(id)) == 0)
			fail("file message path validation");
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
	    omaq_unread_increment(&unread, "g7") != 0 ||
	    omaq_unread_increment(&unread, "../bad") == 0 ||
	    omaq_unread_increment(&unread, "01") == 0 ||
	    omaq_unread_increment(&unread, "g01") == 0 ||
	    omaq_unread_increment(&unread, "4294967296") == 0 ||
	    omaq_unread_increment(&unread, "g4294967296") == 0 ||
	    omaq_unread_total(&unread) != 3 || omaq_unread_count(&unread, "0") != 2 ||
	    omaq_store_unread_save(&unread, dir) != 0 ||
	    omaq_store_unread_load(&loaded, dir) != 0 ||
	    loaded.length != 2 || omaq_unread_total(&loaded) != 3 ||
	    omaq_unread_clear(&loaded, "0") != 0 || omaq_unread_total(&loaded) != 1)
		fail("unread state");
	if (snprintf(path, sizeof(path), "%s/unread.tsv", dir) >= (int)sizeof(path) ||
	    !(f = fopen(path, "w"))) {
		fail("unread malformed fixture");
		return;
	}
	fputs("0\t1\n0\t2\n", f);
	fclose(f);
	if (omaq_store_unread_load(&loaded, dir) == 0 || loaded.length != 0)
		fail("unread duplicate rejection");
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
	FILE *f;

	if (!mkdtemp(dir)) {
		fail("id mkdtemp");
		return;
	}
	if (snprintf(src, sizeof(src), "%s/tox.save", dir) >= (int)sizeof(src) ||
	    snprintf(dst, sizeof(dst), "%s/backup.save", dir) >= (int)sizeof(dst) ||
	    snprintf(other, sizeof(other), "%s/other.save", dir) >= (int)sizeof(other) ||
	    snprintf(linkpath, sizeof(linkpath), "%s/link.save", dir) >= (int)sizeof(linkpath) ||
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
	char id[16];
	uint32_t n;

	if (omaq_group_id_format(0, id, sizeof(id)) != 0 || strcmp(id, "g0") != 0)
		fail("group id format");
	if (omaq_group_id_parse("g0", &n) != 0 || n != 0)
		fail("group id parse 0");
	if (omaq_group_id_parse("g12", &n) != 0 || n != 12)
		fail("group id parse 12");
	if (omaq_group_id_parse("0", &n) == 0)
		fail("group id not g");
	if (omaq_group_id_parse("gx", &n) == 0)
		fail("group id junk");
	omaq_group_note_peer(0, 7);
	omaq_group_mark_dissolved(0);
	omaq_group_reset();
	if (omaq_group_peer_count(0) != 0 || omaq_group_is_dissolved(0))
		fail("group identity reset");
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
	char wire[256], expected[256], id[64], reply[64], text[160], state[16];
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
