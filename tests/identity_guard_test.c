#define _XOPEN_SOURCE 700
#include "../helper/identity_guard.h"
#include "../helper/tox_adapt.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

static void check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "identity-guard: %s\n", message);
		failures++;
	}
}

static int remove_entry(const char *path, const struct stat *st, int type,
			struct FTW *walk)
{
	(void)st;
	(void)type;
	(void)walk;
	return remove(path);
}

static void cleanup(char *root)
{
	if (root && root[0])
		(void)nftw(root, remove_entry, 16, FTW_DEPTH | FTW_PHYS);
}

static int hex_key(const char *hex, unsigned char key[32])
{
	for (size_t i = 0; i < 32; i++) {
		unsigned int high, low;
		char a = hex[i * 2], b = hex[i * 2 + 1];
		if (!((a >= '0' && a <= '9') || (a >= 'a' && a <= 'f')) ||
		    !((b >= '0' && b <= '9') || (b >= 'a' && b <= 'f')))
			return -1;
		high = a <= '9' ? (unsigned int)(a - '0') : (unsigned int)(a - 'a' + 10);
		low = b <= '9' ? (unsigned int)(b - '0') : (unsigned int)(b - 'a' + 10);
		key[i] = (unsigned char)((high << 4) | low);
	}
	return 0;
}

static int make_layout(char *root, char *home, size_t home_size,
		       char *state, size_t state_size)
{
	if (!mkdtemp(root) || snprintf(home, home_size, "%s/home", root) >= (int)home_size ||
	    snprintf(state, state_size, "%s/state", root) >= (int)state_size ||
	    mkdir(home, 0700) != 0 || mkdir(state, 0700) != 0)
		return -1;
	return 0;
}

int main(void)
{
	char root_a[] = "/tmp/omaq-identity-guard-a-XXXXXX";
	char root_b[] = "/tmp/omaq-identity-guard-b-XXXXXX";
	char root_c[] = "/tmp/omaq-identity-guard-c-XXXXXX";
	char home_a[256], state_a[256], home_b[256], state_b[256];
	char home_c[256], state_c[256], path[320], fingerprint_a[65], fingerprint_b[65];
	char restored_fingerprint[65], hidden_state[320], kept_primary[320];
	unsigned char peer_key[32];
	struct omaq_tox *a = NULL, *b = NULL;
	size_t count = 0;
	int err = 0;

	if (make_layout(root_a, home_a, sizeof(home_a), state_a, sizeof(state_a)) != 0 ||
	    make_layout(root_b, home_b, sizeof(home_b), state_b, sizeof(state_b)) != 0 ||
	    make_layout(root_c, home_c, sizeof(home_c), state_c, sizeof(state_c)) != 0) {
		fprintf(stderr, "identity-guard: setup failed\n");
		cleanup(root_a);
		cleanup(root_b);
		cleanup(root_c);
		return 1;
	}

	check(omaq_identity_guard_preflight(home_a, state_a) == OMAQ_IDENTITY_GUARD_FRESH,
	      "empty private directories were not recognized as a first run");
	a = omaq_tox_open(home_a, NULL, &err);
	b = omaq_tox_open(home_b, NULL, &err);
	check(a && b, "sandbox identities could not be created");
	if (!a || !b)
		goto done;
	check(omaq_tox_self_pk_hex(a, fingerprint_a) == 0 &&
	      omaq_tox_self_pk_hex(b, fingerprint_b) == 0 &&
	      hex_key(fingerprint_b, peer_key) == 0,
	      "sandbox fingerprints could not be read");
	check(omaq_identity_guard_verify_or_create(state_a, fingerprint_a) == 0,
	      "identity-presence marker could not be created");
	check(omaq_tox_enable_recovery(a, state_a, 0) == 0 &&
	      omaq_identity_primary_uncertain_persist(state_a) == 0 &&
	      omaq_tox_enable_recovery(a, state_a, 1) == 0,
	      "recovery copy or acknowledgement fixture could not be enabled");
	check(snprintf(hidden_state, sizeof(hidden_state), "%s-hidden", state_a) <
	      (int)sizeof(hidden_state) && rename(state_a, hidden_state) == 0,
	      "recovery fault fixture could not hide the state directory");
	if (access(hidden_state, F_OK) == 0) {
		FILE *blocked = fopen(state_a, "w");
		int blocked_ready = blocked != NULL;
		if (blocked && fclose(blocked) != 0)
			blocked_ready = 0;
		check(blocked_ready && omaq_tox_primary_acknowledged(a) != 0,
		      "acknowledgement cleared after a pre-publication save failure");
		if (blocked_ready)
			check(unlink(state_a) == 0,
			      "blocked recovery path could not be removed");
		check(rename(hidden_state, state_a) == 0,
		      "recovery fault fixture could not restore the state directory");
	}
	check(omaq_tox_primary_acknowledged(a) == 0,
	      "acknowledgement could not retry after storage recovery");
	check(rename(state_a, hidden_state) == 0,
	      "mutation fault fixture could not hide the state directory");
	if (access(hidden_state, F_OK) == 0) {
		FILE *blocked = fopen(state_a, "w");
		int blocked_ready = blocked != NULL;
		if (blocked && fclose(blocked) != 0)
			blocked_ready = 0;
		check(blocked_ready && omaq_tox_friend_accept(a, peer_key) != 0 &&
		      omaq_tox_friend_count(a, &count) == 0 && count == 0,
		      "primary mutation proceeded without a durable write-ahead marker");
		if (blocked_ready)
			check(unlink(state_a) == 0,
			      "blocked mutation path could not be removed");
		check(rename(hidden_state, state_a) == 0,
		      "mutation fault fixture could not restore the state directory");
	}
	omaq_identity_guard_test_fail_recovery_write();
	check(omaq_tox_friend_accept(a, peer_key) == 0 &&
	      omaq_tox_recovery_degraded(a) &&
	      omaq_identity_recovery_stale_present(state_a) == 1,
	      "durable primary commit was rolled back or stale recovery stayed trusted");
	omaq_tox_test_fail_before_primary(a);
	check(omaq_tox_save(a) != 0 &&
	      omaq_identity_recovery_stale_present(state_a) == 1,
	      "pre-publication primary failure cleared a pre-existing stale marker");
	check(snprintf(path, sizeof(path), "%s/tox.save", home_a) < (int)sizeof(path) &&
	      snprintf(kept_primary, sizeof(kept_primary), "%s/tox.save.kept", home_a) <
		(int)sizeof(kept_primary) && rename(path, kept_primary) == 0 &&
	      omaq_identity_guard_preflight(home_a, state_a) == OMAQ_IDENTITY_GUARD_MISSING &&
	      rename(kept_primary, path) == 0,
	      "stale recovery copy was automatically restored after primary loss");
	check(omaq_tox_save(a) == 0 && !omaq_tox_recovery_degraded(a) &&
	      omaq_identity_recovery_stale_present(state_a) == 0,
	      "recovery copy did not catch up after storage became writable");
	check(omaq_tox_friend_count(a, &count) == 0 && count == 1,
	      "sandbox contact was not present before recovery");
	omaq_identity_guard_test_fail_recovery_write();
	check(omaq_tox_protect(a, "guard-test-pass") == 0 &&
	      omaq_identity_recovery_stale_present(state_a) == 1 &&
	      snprintf(path, sizeof(path), "%s/tox.save", home_a) < (int)sizeof(path) &&
	      rename(path, kept_primary) == 0 &&
	      omaq_identity_guard_preflight(home_a, state_a) == OMAQ_IDENTITY_GUARD_MISSING &&
	      rename(kept_primary, path) == 0,
	      "stale plaintext recovery remained trusted after identity protection");
	check(omaq_tox_save(a) == 0 && omaq_tox_unprotect(a, "guard-test-pass") == 0,
	      "protected recovery fixture could not return to its original state");
	omaq_tox_close(a);
	a = NULL;
	check(omaq_identity_guard_verify_or_create(state_b, fingerprint_b) == 0 &&
	      omaq_tox_enable_recovery(b, state_b, 0) == 0,
	      "primary fsync fixture could not enable guarded saves");
	omaq_tox_test_fail_primary_fsync(b);
	check(omaq_tox_nospam_rotate(b) != 0 && omaq_tox_primary_uncertain(b) &&
	      omaq_identity_primary_uncertain_present(state_b) == 1,
	      "primary-directory fsync failure was acknowledged or lost across restart");
	check(omaq_identity_primary_uncertain_clear(state_b) == 0,
	      "primary uncertainty fixture could not be acknowledged");
	omaq_tox_discard(b);
	b = NULL;

	check(snprintf(path, sizeof(path), "%s/identity-presence", state_a) <
	      (int)sizeof(path) && unlink(path) == 0 &&
	      omaq_identity_guard_preflight(home_a, state_a) ==
		OMAQ_IDENTITY_GUARD_INVALID &&
	      omaq_identity_guard_restore(state_a, fingerprint_a) == 0,
	      "guard-era recovery state was silently rebound after its guard disappeared");
	check(snprintf(path, sizeof(path), "%s/tox.save", home_a) < (int)sizeof(path) &&
	      unlink(path) == 0, "primary identity could not be removed for recovery test");
	check(omaq_identity_guard_preflight(home_a, state_a) == OMAQ_IDENTITY_GUARD_RESTORED,
	      "missing primary identity was not restored from the helper recovery copy");
	a = omaq_tox_open(home_a, NULL, &err);
	check(a && omaq_tox_friend_count(a, &count) == 0 && count == 1,
	      "restored identity did not retain its contact list");
	if (a) {
		check(omaq_tox_self_pk_hex(a, restored_fingerprint) == 0 &&
		      strcmp(fingerprint_a, restored_fingerprint) == 0,
		      "restored identity fingerprint changed");
		check(omaq_identity_guard_verify_or_create(state_a, restored_fingerprint) == 0 &&
		      omaq_tox_enable_recovery(a, state_a, 0) == 0,
		      "restored identity could not resume guarded saves");
		omaq_tox_close(a);
		a = NULL;
	}

	check(snprintf(path, sizeof(path), "%s/tox.save", home_a) < (int)sizeof(path) &&
	      unlink(path) == 0, "primary identity could not be removed for missing test");
	check(snprintf(path, sizeof(path), "%s/identity-recovery.save", state_a) <
	      (int)sizeof(path) && unlink(path) == 0,
	      "recovery identity could not be removed for missing test");
	check(omaq_identity_guard_preflight(home_a, state_a) == OMAQ_IDENTITY_GUARD_MISSING,
	      "missing guarded identity did not fail closed");
	check(snprintf(path, sizeof(path), "%s/tox.save", home_a) < (int)sizeof(path) &&
	      access(path, F_OK) != 0,
	      "preflight silently created a replacement identity");
	check(snprintf(path, sizeof(path), "%s/identity-recovery.save", state_a) <
	      (int)sizeof(path) && symlink("missing-target", path) == 0,
	      "unsafe recovery-copy fixture could not be created");
	check(omaq_identity_guard_preflight(home_a, state_a) == OMAQ_IDENTITY_GUARD_INVALID,
	      "symlinked recovery copy did not fail closed");
	check(unlink(path) == 0, "unsafe recovery-copy fixture could not be removed");
	{
		FILE *file = fopen(path, "w");
		check(file && fputs("not tox savedata\n", file) >= 0 && fclose(file) == 0 &&
		      chmod(path, 0600) == 0,
		      "malformed recovery-copy fixture could not be written");
	}
	check(omaq_identity_guard_preflight(home_a, state_a) == OMAQ_IDENTITY_GUARD_RESTORED,
	      "bounded malformed recovery copy did not enter validation staging");
	check(omaq_identity_guard_reject_recovery(home_a, state_a) == 0 &&
	      snprintf(path, sizeof(path), "%s/tox.save", home_a) < (int)sizeof(path) &&
	      access(path, F_OK) != 0,
	      "rejected recovery copy permanently occupied the primary identity path");
	check(snprintf(path, sizeof(path), "%s/identity-recovery.save", state_a) <
	      (int)sizeof(path) && access(path, F_OK) == 0,
	      "rejected recovery source was not preserved for diagnosis or repair");

	check(snprintf(path, sizeof(path), "%s/omaq.protocol", state_c) <
	      (int)sizeof(path), "runtime fixture path overflow");
	if (snprintf(path, sizeof(path), "%s/omaq.protocol", state_c) < (int)sizeof(path)) {
		FILE *file = fopen(path, "w");
		check(file && fputs("runtime only\n", file) >= 0 && fclose(file) == 0 &&
		      omaq_identity_guard_preflight(home_c, state_c) ==
			OMAQ_IDENTITY_GUARD_FRESH && unlink(path) == 0,
		      "runtime-only state was treated as proof of an established identity");
	}
	check(snprintf(path, sizeof(path), "%s/surfaces.jsonl", state_c) <
	      (int)sizeof(path), "preference fixture path overflow");
	if (snprintf(path, sizeof(path), "%s/surfaces.jsonl", state_c) < (int)sizeof(path)) {
		FILE *file = fopen(path, "w");
		check(file && fputs("preference only\n", file) >= 0 && fclose(file) == 0 &&
		      omaq_identity_guard_preflight(home_c, state_c) ==
			OMAQ_IDENTITY_GUARD_FRESH && unlink(path) == 0,
		      "preference-only state was treated as proof of an established identity");
	}
	check(snprintf(path, sizeof(path), "%s/attachments", home_c) < (int)sizeof(path) &&
	      mkdir(path, 0700) == 0 &&
	      omaq_identity_guard_preflight(home_c, state_c) == OMAQ_IDENTITY_GUARD_FRESH &&
	      rmdir(path) == 0,
	      "attachment staging alone was treated as proof of an established identity");

	check(snprintf(path, sizeof(path), "%s/direct-friends.tsv", home_c) <
	      (int)sizeof(path), "footprint path overflow");
	if (snprintf(path, sizeof(path), "%s/direct-friends.tsv", home_c) < (int)sizeof(path)) {
		FILE *file = fopen(path, "w");
		check(file && fputs("OMAQDF1\n", file) >= 0 && fclose(file) == 0,
		      "legacy footprint fixture could not be written");
	}
	check(omaq_identity_guard_preflight(home_c, state_c) == OMAQ_IDENTITY_GUARD_MISSING,
	      "identity-bound state without tox.save was treated as a first run");
	check(omaq_identity_primary_uncertain_persist(state_c) == 0 &&
	      omaq_identity_primary_uncertain_present(state_c) == 1 &&
	      omaq_identity_primary_uncertain_clear(state_c) == 0 &&
	      omaq_identity_primary_uncertain_present(state_c) == 0,
	      "primary-durability warning did not persist until acknowledgement");

	check(omaq_identity_guard_replace(state_b, fingerprint_b, fingerprint_a) == 0,
	      "guard fsync fixture marker could not be prepared");
	omaq_identity_guard_test_fail_directory_fsync();
	check(omaq_identity_guard_replace(state_b, fingerprint_a, fingerprint_b) ==
		OMAQ_IDENTITY_GUARD_PUBLISHED &&
	      omaq_identity_guard_expected(state_b, restored_fingerprint) == 0 &&
	      strcmp(restored_fingerprint, fingerprint_b) == 0 &&
	      omaq_identity_guard_restore(state_b, fingerprint_a) == 0 &&
	      omaq_identity_guard_expected(state_b, restored_fingerprint) == 0 &&
	      strcmp(restored_fingerprint, fingerprint_a) == 0,
	      "published guard with uncertain directory durability could not roll back");
	check(omaq_identity_guard_verify_or_create(state_b, fingerprint_b) ==
		OMAQ_IDENTITY_GUARD_MISMATCH,
	      "fingerprint-mismatched identity was not rejected");
	check(snprintf(path, sizeof(path), "%s/identity-recovery.pending", state_b) <
	      (int)sizeof(path), "foreign recovery pending path overflow");
	if (snprintf(path, sizeof(path), "%s/identity-recovery.pending", state_b) <
	    (int)sizeof(path)) {
		FILE *file = fopen(path, "w");
		check(file && fputs("restore pending\n", file) >= 0 && fclose(file) == 0 &&
		      chmod(path, 0600) == 0 &&
		      omaq_identity_guard_reject_recovery(home_b, state_b) == 0,
		      "foreign recovery candidate could not be rejected safely");
	}
	check(snprintf(path, sizeof(path), "%s/tox.save", home_b) < (int)sizeof(path) &&
	      access(path, F_OK) != 0,
	      "fingerprint-mismatched recovery candidate remained primary");
	check(snprintf(path, sizeof(path), "%s/identity-presence", state_b) <
	      (int)sizeof(path) && unlink(path) == 0 && symlink("missing-target", path) == 0,
	      "unsafe marker fixture could not be created");
	check(omaq_identity_guard_preflight(home_b, state_b) == OMAQ_IDENTITY_GUARD_INVALID,
	      "symlinked identity marker did not fail closed");

done:
	if (a)
		omaq_tox_discard(a);
	if (b)
		omaq_tox_discard(b);
	cleanup(root_a);
	cleanup(root_b);
	cleanup(root_c);
	if (failures)
		return 1;
	puts("identity-guard: ok");
	return 0;
}
