#define _DEFAULT_SOURCE
#include "../helper/ratchet.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_SIGNAL

static void fail(const char *message)
{
	fprintf(stderr, "ratchet_prekey_test: %s\n", message);
	exit(1);
}

static void remove_tree(const char *path)
{
	DIR *directory = opendir(path);
	struct dirent *entry;

	if (!directory) {
		(void)unlink(path);
		return;
	}
	while ((entry = readdir(directory)) != NULL) {
		char child[768];
		struct stat st;
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
			continue;
		if (snprintf(child, sizeof(child), "%s/%s", path, entry->d_name) >=
		    (int)sizeof(child))
			fail("cleanup path overflow");
		if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode))
			remove_tree(child);
		else
			(void)unlink(child);
	}
	closedir(directory);
	(void)rmdir(path);
}

static int suffix_entries(const char *path, const char *suffix)
{
	DIR *directory = opendir(path);
	struct dirent *entry;
	int count = 0;
	size_t suffix_length = strlen(suffix);

	if (!directory)
		return errno == ENOENT ? 0 : -1;
	while ((entry = readdir(directory)) != NULL) {
		size_t length = strlen(entry->d_name);
		if (length >= suffix_length &&
		    strcmp(entry->d_name + length - suffix_length, suffix) == 0)
			count++;
	}
	closedir(directory);
	return count;
}

static int mark_active_used(const char *path, const char *peer)
{
	DIR *directory = opendir(path);
	struct dirent *entry;
	size_t peer_length = strlen(peer);
	int marked = 0;

	if (!directory)
		return -1;
	while ((entry = readdir(directory)) != NULL) {
		char marker[768];
		size_t length = strlen(entry->d_name);
		FILE *file;
		if (strncmp(entry->d_name, peer, peer_length) != 0 ||
		    entry->d_name[peer_length] != '-' ||
		    (length > 5 && strcmp(entry->d_name + length - 5, ".used") == 0))
			continue;
		if (snprintf(marker, sizeof(marker), "%s/%s.used", path, entry->d_name) >=
		    (int)sizeof(marker) || !(file = fopen(marker, "wx")) ||
		    fchmod(fileno(file), 0600) != 0 || fwrite("OMAQPU1\n", 1, 8, file) != 8 ||
		    fclose(file) != 0) {
			closedir(directory);
			return -1;
		}
		marked++;
	}
	closedir(directory);
	return marked == 1 ? 0 : -1;
}

static int remove_active_for_peer(const char *path, const char *peer)
{
	DIR *directory = opendir(path);
	struct dirent *entry;
	size_t peer_length = strlen(peer);
	int removed = 0;

	if (!directory)
		return -1;
	while ((entry = readdir(directory)) != NULL) {
		char file[768];
		size_t length = strlen(entry->d_name);
		if (strncmp(entry->d_name, peer, peer_length) != 0 ||
		    entry->d_name[peer_length] != '-' ||
		    (length > 5 && strcmp(entry->d_name + length - 5, ".used") == 0))
			continue;
		if (snprintf(file, sizeof(file), "%s/%s", path, entry->d_name) >=
		    (int)sizeof(file) || unlink(file) != 0) {
			closedir(directory);
			return -1;
		}
		removed++;
	}
	closedir(directory);
	return removed == 1 ? 0 : -1;
}

static int regular_entries(const char *path)
{
	DIR *directory = opendir(path);
	struct dirent *entry;
	int count = 0;

	if (!directory)
		return errno == ENOENT ? 0 : -1;
	while ((entry = readdir(directory)) != NULL)
		if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
			count++;
	closedir(directory);
	return count;
}

static int hex_value(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	return -1;
}

static int varint_read(const unsigned char *data, size_t size, size_t *offset,
		       uint64_t *value)
{
	*value = 0;
	for (unsigned shift = 0; shift < 64 && *offset < size; shift += 7) {
		unsigned char byte = data[(*offset)++];
		*value |= (uint64_t)(byte & 0x7f) << shift;
		if (!(byte & 0x80))
			return 0;
	}
	return -1;
}

/* Forges the preKeyId (protobuf field 1) of the PreKeySignalMessage in an OQR103 frame:
 * `drop` removes it (no one-time prekey), otherwise it becomes 0. The outer protobuf is not
 * authenticated, so an accepted contact can send either form. The forged message is never
 * longer than the original. */
static int forge_prekey_id(const char *wire, int drop, char *out, size_t out_size)
{
	static const char digits[] = "0123456789abcdef";
	unsigned char raw[800], forged[800];
	size_t hex_length = strlen(wire), size, offset = 1, length = 1;
	int found = 0;

	if (strncmp(wire, "OQR103", 6) != 0 || hex_length % 2 != 0 ||
	    (hex_length - 6) / 2 > sizeof(raw) || hex_length < 8)
		return -1;
	size = (hex_length - 6) / 2;
	for (size_t i = 0; i < size; i++) {
		int high = hex_value(wire[6 + 2 * i]), low = hex_value(wire[7 + 2 * i]);
		if (high < 0 || low < 0)
			return -1;
		raw[i] = (unsigned char)(high << 4 | low);
	}
	forged[0] = raw[0]; /* version byte */
	while (offset < size) {
		size_t start = offset;
		uint64_t key, value;
		if (varint_read(raw, size, &offset, &key) != 0)
			return -1;
		if ((key & 7) == 0) {
			if (varint_read(raw, size, &offset, &value) != 0)
				return -1;
			if (key >> 3 == 1) {
				found = 1;
				if (!drop) {
					forged[length++] = 0x08;
					forged[length++] = 0x00;
				}
				continue;
			}
		} else if ((key & 7) == 2) {
			if (varint_read(raw, size, &offset, &value) != 0 || value > size - offset)
				return -1;
			offset += (size_t)value;
		} else {
			return -1;
		}
		memcpy(forged + length, raw + start, offset - start);
		length += offset - start;
	}
	if (!found || 6 + 2 * length + 1 > out_size)
		return -1;
	memcpy(out, "OQR103", 6);
	for (size_t i = 0; i < length; i++) {
		out[6 + 2 * i] = digits[forged[i] >> 4];
		out[7 + 2 * i] = digits[forged[i] & 0x0f];
	}
	out[6 + 2 * length] = '\0';
	return 0;
}

/* Prekey message without a usable one-time-prekey id (missing, or 0: the "any id" sentinel of the
 * reservation lookup) is refused (-1, never RECOVER), on the first message as on an existing
 * session, and consumes no reservation. */
static void prekey_id_guard(const char *root)
{
	const char *peer_x = "d:eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
	const char *peer_y = "d:ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
	const char *peer_z = "d:0000000000000000000000000000000000000000000000000000000000000000";
	char home_x[640], home_y[640], pre_dir[680];
	char bundle_x[900], bundle_z[900], bundle_y[900];
	char rk_x[OMAQ_RK_HEX + 1], rk_y[OMAQ_RK_HEX + 1];
	char wire[1600], forged[1600], plain[128];
	struct omaq_ratchet *x, *y;
	size_t plain_length = 0;

	if (snprintf(home_x, sizeof(home_x), "%s/x", root) >= (int)sizeof(home_x) ||
	    snprintf(home_y, sizeof(home_y), "%s/y", root) >= (int)sizeof(home_y) ||
	    snprintf(pre_dir, sizeof(pre_dir), "%s/ratchet/pre", home_x) >= (int)sizeof(pre_dir) ||
	    mkdir(home_x, 0700) != 0 || mkdir(home_y, 0700) != 0)
		fail("prekey id guard homes");
	x = omaq_ratchet_open(home_x);
	y = omaq_ratchet_open(home_y);
	if (!x || !y || omaq_ratchet_local_rk(x, rk_x) != 0 || omaq_ratchet_local_rk(y, rk_y) != 0 ||
	    omaq_ratchet_bundle(x, peer_y, bundle_x, sizeof(bundle_x)) != 0 ||
	    omaq_ratchet_bundle(x, peer_z, bundle_z, sizeof(bundle_z)) != 0 ||
	    omaq_ratchet_bundle(y, peer_x, bundle_y, sizeof(bundle_y)) != 0 ||
	    omaq_ratchet_accept_bundle(x, peer_y, bundle_y, rk_y) != 0 ||
	    omaq_ratchet_accept_bundle(y, peer_x, bundle_x, rk_x) != 0 ||
	    omaq_ratchet_encrypt(y, peer_x, "first", wire, sizeof(wire)) != 0)
		fail("prekey id guard sessions");
	for (int drop = 1; drop >= 0; drop--)
		if (forge_prekey_id(wire, drop, forged, sizeof(forged)) != 0 ||
		    omaq_ratchet_decrypt(x, peer_y, forged, plain, sizeof(plain), &plain_length) != -1)
			fail(drop ? "first prekey message without prekey id accepted"
				  : "first prekey message with prekey id 0 accepted");
	if (regular_entries(pre_dir) != 2 || suffix_entries(pre_dir, ".used") != 0)
		fail("forged first message consumed a prekey");
	if (omaq_ratchet_decrypt(x, peer_y, wire, plain, sizeof(plain), &plain_length) != 0 ||
	    strcmp(plain, "first") != 0 || suffix_entries(pre_dir, ".used") != 1)
		fail("genuine first prekey message");
	/* Until y hears back from x, its messages stay prekey messages on the session just built. */
	if (omaq_ratchet_encrypt(y, peer_x, "second", wire, sizeof(wire)) != 0 ||
	    strncmp(wire, "OQR103", 6) != 0)
		fail("follow-up prekey message");
	for (int drop = 1; drop >= 0; drop--)
		if (forge_prekey_id(wire, drop, forged, sizeof(forged)) != 0 ||
		    omaq_ratchet_decrypt(x, peer_y, forged, plain, sizeof(plain), &plain_length) != -1)
			fail(drop ? "follow-up prekey message without prekey id accepted"
				  : "follow-up prekey message with prekey id 0 accepted");
	if (regular_entries(pre_dir) != 2 || suffix_entries(pre_dir, ".used") != 1 ||
	    omaq_ratchet_decrypt(x, peer_y, wire, plain, sizeof(plain), &plain_length) != 0 ||
	    strcmp(plain, "second") != 0)
		fail("genuine follow-up prekey message");
	omaq_ratchet_close(y);
	omaq_ratchet_close(x);
}

int main(void)
{
	char root[] = "/tmp/omaq-ratchet-prekey-XXXXXX";
	char home_a[640], home_b[640], home_c[640], pre_dir[680];
	char bundle_b[900], bundle_c[900], reply_b[900], reply_c[900], replay[900];
	char malformed_bundle[900], appended_bundle[900];
	char recovery_bundle[900], recovery_wire[1600];
	char wire_b[1600], wire_b2[1600], wire_c[1600], wire_attack[1600], plain[128];
	char rk_a[OMAQ_RK_HEX + 1], rk_b[OMAQ_RK_HEX + 1], rk_c[OMAQ_RK_HEX + 1];
	const char *peer_a = "d:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const char *peer_b = "d:bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
	const char *peer_c = "d:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc";
	const char *peer_d = "d:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
	struct omaq_ratchet *a = NULL, *b = NULL, *c = NULL;
	size_t plain_length = 0;

	if (!mkdtemp(root) ||
	    snprintf(home_a, sizeof(home_a), "%s/a", root) >= (int)sizeof(home_a) ||
	    snprintf(home_b, sizeof(home_b), "%s/b", root) >= (int)sizeof(home_b) ||
	    snprintf(home_c, sizeof(home_c), "%s/c", root) >= (int)sizeof(home_c) ||
	    mkdir(home_a, 0700) != 0 || mkdir(home_b, 0700) != 0 || mkdir(home_c, 0700) != 0)
		fail("temporary homes");
	a = omaq_ratchet_open(home_a);
	b = omaq_ratchet_open(home_b);
	c = omaq_ratchet_open(home_c);
	if (!a || !b || !c || omaq_ratchet_local_rk(a, rk_a) != 0 ||
	    omaq_ratchet_local_rk(b, rk_b) != 0 || omaq_ratchet_local_rk(c, rk_c) != 0)
		fail("open ratchets");
	if (omaq_ratchet_bundle(a, peer_b, bundle_b, sizeof(bundle_b)) != 0 ||
	    omaq_ratchet_bundle(a, peer_c, bundle_c, sizeof(bundle_c)) != 0 ||
	    strcmp(bundle_b, bundle_c) == 0)
		fail("unique peer bundles");
	if (snprintf(pre_dir, sizeof(pre_dir), "%s/ratchet/pre", home_a) >=
	    (int)sizeof(pre_dir) || regular_entries(pre_dir) != 2)
		fail("persisted prekeys");
	if (snprintf(malformed_bundle, sizeof(malformed_bundle), "%s", bundle_b) >=
		    (int)sizeof(malformed_bundle) ||
	    snprintf(appended_bundle, sizeof(appended_bundle), "%sx", bundle_b) >=
		    (int)sizeof(appended_bundle))
		fail("malformed bundle fixtures");
	malformed_bundle[9] = 'g';
	if (omaq_ratchet_accept_bundle(b, peer_a, malformed_bundle, rk_a) != -1 ||
	    omaq_ratchet_accept_bundle(b, peer_a, appended_bundle, rk_a) != -1 ||
	    omaq_ratchet_response_bundle(a, peer_b, malformed_bundle, replay,
					 sizeof(replay)) != -1 ||
	    omaq_ratchet_response_bundle(a, peer_b, appended_bundle, replay,
					 sizeof(replay)) != -1 ||
	    regular_entries(pre_dir) != 2)
		fail("strict bundle encoding");
	if (omaq_ratchet_bundle(b, peer_a, reply_b, sizeof(reply_b)) != 0 ||
	    omaq_ratchet_bundle(c, peer_a, reply_c, sizeof(reply_c)) != 0 ||
	    omaq_ratchet_accept_bundle(a, peer_b, reply_b, rk_b) != 0 ||
	    omaq_ratchet_accept_bundle(a, peer_c, reply_c, rk_c) != 0)
		fail("trust remote identities");
	if (omaq_ratchet_accept_bundle(a, peer_b, reply_b, rk_b) != 1)
		fail("duplicate bundle suppression");
	{
		char boot_dir[680], boot_record[768];
		int boot_fd;
		if (snprintf(boot_dir, sizeof(boot_dir), "%s/ratchet/boot", home_a) >=
			    (int)sizeof(boot_dir) ||
		    snprintf(boot_record, sizeof(boot_record), "%s/%s", boot_dir, peer_d) >=
			    (int)sizeof(boot_record) ||
		    (boot_fd = open(boot_record, O_WRONLY | O_CREAT | O_EXCL, 0600)) < 0 ||
		    close(boot_fd) != 0)
			fail("invalid bootstrap fixture");
		errno = ENOENT;
		if (omaq_ratchet_accept_bundle(a, peer_d, reply_b, rk_b) != -1 ||
		    omaq_ratchet_has_session(a, peer_d) || unlink(boot_record) != 0 ||
		    chmod(boot_dir, 0500) != 0 ||
		    omaq_ratchet_accept_bundle(a, peer_d, reply_b, rk_b) != -1 ||
		    omaq_ratchet_has_session(a, peer_d) || chmod(boot_dir, 0700) != 0)
			fail("bundle acceptance rollback");
	}
	if (omaq_ratchet_response_bundle(a, peer_b, reply_b, replay, sizeof(replay)) != 0 ||
	    strcmp(replay, bundle_b) != 0)
		fail("durable request response");
	{
		char response_cache[768];
		if (snprintf(response_cache, sizeof(response_cache), "%s/ratchet/reply/%s",
			     home_a, peer_b) >= (int)sizeof(response_cache) ||
		    truncate(response_cache, 32 + strlen(bundle_b) - 1) != 0 ||
		    omaq_ratchet_response_bundle(a, peer_b, reply_b, replay,
						 sizeof(replay)) != -1 ||
		    truncate(response_cache, 0) != 0)
			fail("strict response cache fixture");
		errno = ENOENT;
		if (omaq_ratchet_response_bundle(a, peer_b, reply_b, replay,
						 sizeof(replay)) != -1 ||
		    unlink(response_cache) != 0 ||
		    omaq_ratchet_response_bundle(a, peer_b, reply_b, replay,
						 sizeof(replay)) != 0 ||
		    strcmp(replay, bundle_b) != 0)
			fail("strict response cache");
	}
	if (omaq_ratchet_accept_bundle(c, peer_a, bundle_b, rk_a) != 0 ||
	    omaq_ratchet_encrypt(c, peer_a, "wrong-peer", wire_attack,
				 sizeof(wire_attack)) != 0 ||
	    omaq_ratchet_decrypt(a, peer_c, wire_attack, plain, sizeof(plain), &plain_length) == 0 ||
	    regular_entries(pre_dir) != 2)
		fail("peer-bound prekey reservation");

	omaq_ratchet_close(a);
	a = omaq_ratchet_open(home_a);
	if (!a || regular_entries(pre_dir) != 2)
		fail("reload persisted prekeys");
	if (omaq_ratchet_accept_bundle(a, peer_b, reply_b, rk_b) != 1 ||
	    omaq_ratchet_response_bundle(a, peer_b, reply_b, replay, sizeof(replay)) != 1 ||
	    strcmp(replay, bundle_b) != 0)
		fail("replayable response after restart");
	if (omaq_ratchet_accept_bundle(b, peer_a, bundle_b, rk_a) != 0 ||
	    omaq_ratchet_accept_bundle(c, peer_a, bundle_c, rk_a) != 0 ||
	    omaq_ratchet_encrypt(b, peer_a, "from-b", wire_b, sizeof(wire_b)) != 0 ||
	    omaq_ratchet_encrypt(b, peer_a, "from-b-2", wire_b2, sizeof(wire_b2)) != 0 ||
	    omaq_ratchet_encrypt(c, peer_a, "from-c", wire_c, sizeof(wire_c)) != 0)
		fail("build remote sessions");
	if (omaq_ratchet_decrypt(a, peer_b, wire_b, plain, sizeof(plain), &plain_length) != 0 ||
	    plain_length != strlen("from-b") || strcmp(plain, "from-b") != 0 ||
	    regular_entries(pre_dir) != 2 ||
	    suffix_entries(pre_dir, ".used") != 1)
		fail("consume first persisted prekey");

	omaq_ratchet_close(a);
	a = omaq_ratchet_open(home_a);
	if (!a || regular_entries(pre_dir) != 2 ||
	    omaq_ratchet_response_bundle(a, peer_b, reply_b, replay, sizeof(replay)) != 1 ||
	    strcmp(replay, bundle_b) != 0 || suffix_entries(pre_dir, ".used") != 1 ||
	    omaq_ratchet_decrypt(a, peer_b, wire_b2, plain, sizeof(plain), &plain_length) != 0 ||
	    plain_length != strlen("from-b-2") || strcmp(plain, "from-b-2") != 0 ||
	    omaq_ratchet_decrypt(a, peer_c, wire_c, plain, sizeof(plain), &plain_length) != 0 ||
	    plain_length != strlen("from-c") || strcmp(plain, "from-c") != 0 ||
	    regular_entries(pre_dir) != 2 ||
	    suffix_entries(pre_dir, ".used") != 2)
		fail("consume second prekey after restart");

	if (omaq_ratchet_bundle(a, peer_b, recovery_bundle, sizeof(recovery_bundle)) != 0 ||
	    mark_active_used(pre_dir, peer_b) != 0)
		fail("interrupted prekey consumption fixture");
	omaq_ratchet_close(a);
	a = omaq_ratchet_open(home_a);
	if (!a || suffix_entries(pre_dir, ".used") != 2)
		fail("interrupted prekey consumption recovery");
	if (omaq_ratchet_request_bundle(a, peer_b, recovery_bundle,
					 sizeof(recovery_bundle)) != 0 ||
	    omaq_ratchet_response_bundle(a, peer_b, reply_b, replay,
					 sizeof(replay)) != 0 ||
	    strcmp(replay, bundle_b) == 0 ||
	    remove_active_for_peer(pre_dir, peer_b) != 0)
		fail("lost prekey recovery fixture");
	omaq_ratchet_close(a);
	a = omaq_ratchet_open(home_a);
	if (!a || omaq_ratchet_accept_bundle(b, peer_a, recovery_bundle, rk_a) != 0 ||
	    omaq_ratchet_encrypt(b, peer_a, "recover-needed", recovery_wire,
				 sizeof(recovery_wire)) != 0 ||
	    omaq_ratchet_decrypt(a, peer_b, recovery_wire, plain, sizeof(plain), &plain_length) !=
		OMAQ_RATCHET_DECRYPT_RECOVER)
		fail("lost prekey recovery signal");
	{
		char session_dir[680];
		if (snprintf(session_dir, sizeof(session_dir), "%s/ratchet/sess", home_a) >=
			    (int)sizeof(session_dir) || chmod(session_dir, 0500) != 0 ||
		    omaq_ratchet_reset_session(a, peer_b) == 0 ||
		    !omaq_ratchet_has_session(a, peer_b) || chmod(session_dir, 0700) != 0 ||
		    omaq_ratchet_reset_session(a, peer_b) != 0 ||
		    omaq_ratchet_has_session(a, peer_b))
			fail("session reset durability failure");
	}

	omaq_ratchet_close(c);
	omaq_ratchet_close(b);
	omaq_ratchet_close(a);
	prekey_id_guard(root);
	remove_tree(root);
	puts("ratchet_prekey_test: ok");
	return 0;
}

#else
int main(void)
{
	puts("ratchet_prekey_test: skipped");
	return 0;
}
#endif
