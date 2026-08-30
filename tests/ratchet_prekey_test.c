#define _DEFAULT_SOURCE
#include "../helper/ratchet.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
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
