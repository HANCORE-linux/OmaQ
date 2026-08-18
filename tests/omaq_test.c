#define _DEFAULT_SOURCE
#include "../helper/conversation.h"
#include "../helper/file.h"
#include "../helper/group.h"
#include "../helper/identity.h"
#include "../helper/invite.h"
#include "../helper/json_io.h"
#include "../helper/message.h"
#include "../helper/qr.h"
#include "../helper/rate.h"
#include "../helper/roles.h"
#include "../helper/safety.h"
#include "../helper/store.h"
#include "../helper/surface.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
		if (inv.kind != INVITE_DIRECT || strcmp(inv.id, expect + 7) != 0)
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
		else if (omaq_store_tail(dir, "c1", 3, &out2, &n2) != 0)
			fail("store tail after rotate");
		else if (!out2 || !strstr(out2, "\"n\":99") || !strstr(out2, "say \\\"hi\\\""))
			fail("store tail rotated content");
		free(out2);
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
	omaq_conv_init(&c, "x", CONV_DIRECT);
	omaq_conv_note(&c, "hello", 1);
	if (c.unread != 1 || strcmp(c.last, "hello") != 0)
		fail("conv note");
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
	char src[256], dst[256], other[256];
	FILE *f;

	if (!mkdtemp(dir)) {
		fail("id mkdtemp");
		return;
	}
	if (snprintf(src, sizeof(src), "%s/tox.save", dir) >= (int)sizeof(src) ||
	    snprintf(dst, sizeof(dst), "%s/backup.save", dir) >= (int)sizeof(dst) ||
	    snprintf(other, sizeof(other), "%s/other.save", dir) >= (int)sizeof(other)) {
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
	if (omaq_identity_import(dir, dst, 0) != 1)
		fail("id exists");
	f = fopen(other, "w");
	if (!f) {
		fail("id other");
		return;
	}
	fputs("SAVE-B", f);
	fclose(f);
	if (omaq_identity_import(dir, other, 1) != 0)
		fail("id replace");
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
	omaq_surface s, g;

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

int main(void)
{
	test_invites();
	test_roles();
	test_json();
	test_store();
	test_search();
	test_identity_files();
	test_mutate();
	test_conv();
	test_expire();
	test_rate_gold();
	test_rate_hour();
	test_safety();
	test_group_id();
	test_group_plan();
	test_surface();
	test_qr_path();
	test_file();
	if (fails) {
		fprintf(stderr, "omaq_test: %d failure(s)\n", fails);
		return 1;
	}
	puts("omaq_test: ok");
	return 0;
}
