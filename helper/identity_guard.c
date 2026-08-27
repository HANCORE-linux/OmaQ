#define _GNU_SOURCE
#include "identity_guard.h"

#include "identity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <unistd.h>

#define GUARD_NAME "identity-presence"
#define RECOVERY_NAME "identity-recovery.save"
#define RECOVERY_PENDING_NAME "identity-recovery.pending"
#define RECOVERY_PENDING_VALUE "restore pending\n"
#define PRIMARY_UNCERTAIN_NAME "identity-primary-uncertain"
#define PRIMARY_UNCERTAIN_VALUE "primary durability uncertain\n"
#define PRIMARY_ACK_NAME "identity-primary-ack.txn"
#define PRIMARY_ACK_VALUE "primary acknowledgement pending\n"
#define RECOVERY_STALE_NAME "identity-recovery.stale"
#define RECOVERY_STALE_VALUE "recovery copy stale\n"
#define GUARD_PREFIX "OMAQIP1\n"

#ifdef OMAQ_IDENTITY_GUARD_TEST
static int test_fail_directory_fsync;
static int test_fail_recovery_write;
#endif
#define GUARD_SIZE (8u + 64u + 1u)

static int fingerprint_ok(const char *fingerprint)
{
	if (!fingerprint || strlen(fingerprint) != 64)
		return 0;
	for (size_t i = 0; i < 64; i++)
		if (!((fingerprint[i] >= '0' && fingerprint[i] <= '9') ||
		      (fingerprint[i] >= 'a' && fingerprint[i] <= 'f')))
			return 0;
	return 1;
}

static int private_file_at(int directory, const char *name, off_t maximum,
			   struct stat *result)
{
	struct stat st;

	if (fstatat(directory, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
	    (st.st_mode & 0777) != 0600 || st.st_size <= 0 || st.st_size > maximum)
		return -1;
	if (result)
		*result = st;
	return 1;
}

static int open_private_directory(const char *path)
{
	struct stat st;
	int fd;

	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0 || fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode) ||
	    st.st_uid != geteuid() || (st.st_mode & 0077) != 0) {
		if (fd >= 0)
			close(fd);
		return -1;
	}
	return fd;
}

static int read_guard_at(int state_fd, char fingerprint[65])
{
	char record[GUARD_SIZE];
	struct stat st;
	ssize_t got;
	int fd;

	fingerprint[0] = '\0';
	fd = openat(state_fd, GUARD_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0777) != 0600 ||
	    st.st_size != (off_t)sizeof(record)) {
		close(fd);
		return -1;
	}
	got = read(fd, record, sizeof(record));
	if (got != (ssize_t)sizeof(record) || close(fd) != 0 ||
	    memcmp(record, GUARD_PREFIX, 8) != 0 || record[72] != '\n')
		return -1;
	memcpy(fingerprint, record + 8, 64);
	fingerprint[64] = '\0';
	return fingerprint_ok(fingerprint) ? 1 : -1;
}

static int write_all(int fd, const void *data, size_t length)
{
	const unsigned char *bytes = data;
	size_t offset = 0;

	while (offset < length) {
		ssize_t written = write(fd, bytes + offset, length - offset);
		if (written < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (written == 0)
			return -1;
		offset += (size_t)written;
	}
	return 0;
}

static int write_guard_at(int state_fd, const char *fingerprint, int replace)
{
	char record[GUARD_SIZE], temporary[64] = "";
	uint32_t nonce;
	int fd = -1, published = 0, rc = -1;

	if (!fingerprint_ok(fingerprint))
		return -1;
	memcpy(record, GUARD_PREFIX, 8);
	memcpy(record + 8, fingerprint, 64);
	record[72] = '\n';
	if (!replace) {
		fd = openat(state_fd, GUARD_NAME,
			    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	} else {
		if (getrandom(&nonce, sizeof(nonce), 0) != (ssize_t)sizeof(nonce) ||
		    snprintf(temporary, sizeof(temporary), ".%s.tmp.%08x",
			     GUARD_NAME, nonce) >= (int)sizeof(temporary))
			return -1;
		fd = openat(state_fd, temporary,
			    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	}
	if (fd < 0)
		return -1;
	{
		int failed = write_all(fd, record, sizeof(record)) != 0 || fsync(fd) != 0;
		if (close(fd) != 0)
			failed = 1;
		fd = -1;
		if (failed)
			goto done;
	}
	if (replace && renameat(state_fd, temporary, state_fd, GUARD_NAME) != 0)
		goto done;
	published = 1;
#ifdef OMAQ_IDENTITY_GUARD_TEST
	if (test_fail_directory_fsync) {
		test_fail_directory_fsync = 0;
		rc = OMAQ_IDENTITY_GUARD_PUBLISHED;
		goto done;
	}
#endif
	if (fsync(state_fd) != 0) {
		rc = OMAQ_IDENTITY_GUARD_PUBLISHED;
		goto done;
	}
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (rc != 0 && !published)
		(void)unlinkat(state_fd, replace ? temporary : GUARD_NAME, 0);
	return rc;
}

static int recovery_pending_at(int state_fd)
{
	char value[sizeof(RECOVERY_PENDING_VALUE) - 1u];
	struct stat st;
	ssize_t got;
	int fd;

	fd = openat(state_fd, RECOVERY_PENDING_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0777) != 0600 ||
	    st.st_size != (off_t)sizeof(value)) {
		close(fd);
		return -1;
	}
	got = read(fd, value, sizeof(value));
	if (got != (ssize_t)sizeof(value) || close(fd) != 0 ||
	    memcmp(value, RECOVERY_PENDING_VALUE, sizeof(value)) != 0)
		return -1;
	return 1;
}

static int persist_recovery_pending(int state_fd)
{
	int fd, rc = -1;

	fd = openat(state_fd, RECOVERY_PENDING_NAME,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		return errno == EEXIST && recovery_pending_at(state_fd) == 1 ? 0 : -1;
	{
		int failed = write_all(fd, RECOVERY_PENDING_VALUE,
				       sizeof(RECOVERY_PENDING_VALUE) - 1u) != 0 ||
			     fsync(fd) != 0;
		if (close(fd) != 0)
			failed = 1;
		fd = -1;
		if (!failed && fsync(state_fd) == 0)
			rc = 0;
	}
	if (rc != 0)
		(void)unlinkat(state_fd, RECOVERY_PENDING_NAME, 0);
	return rc;
}

static int remove_recovery_pending_at(int state_fd)
{
	if (unlinkat(state_fd, RECOVERY_PENDING_NAME, 0) != 0 && errno != ENOENT)
		return -1;
	return fsync(state_fd);
}

static int primary_uncertain_at(int state_fd)
{
	char value[sizeof(PRIMARY_UNCERTAIN_VALUE) - 1u];
	struct stat st;
	ssize_t got;
	int fd;

	fd = openat(state_fd, PRIMARY_UNCERTAIN_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0777) != 0600 ||
	    st.st_size != (off_t)sizeof(value)) {
		close(fd);
		return -1;
	}
	got = read(fd, value, sizeof(value));
	if (got != (ssize_t)sizeof(value) || close(fd) != 0 ||
	    memcmp(value, PRIMARY_UNCERTAIN_VALUE, sizeof(value)) != 0)
		return -1;
	return 1;
}

static int primary_ack_at(int state_fd)
{
	char value[sizeof(PRIMARY_ACK_VALUE) - 1u];
	struct stat st;
	ssize_t got;
	int fd;

	fd = openat(state_fd, PRIMARY_ACK_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0777) != 0600 ||
	    st.st_size != (off_t)sizeof(value)) {
		close(fd);
		return -1;
	}
	got = read(fd, value, sizeof(value));
	if (got != (ssize_t)sizeof(value) || close(fd) != 0 ||
	    memcmp(value, PRIMARY_ACK_VALUE, sizeof(value)) != 0)
		return -1;
	return 1;
}

static int recovery_stale_at(int state_fd)
{
	char value[sizeof(RECOVERY_STALE_VALUE) - 1u];
	struct stat st;
	ssize_t got;
	int fd;

	fd = openat(state_fd, RECOVERY_STALE_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || (st.st_mode & 0777) != 0600 ||
	    st.st_size != (off_t)sizeof(value)) {
		close(fd);
		return -1;
	}
	got = read(fd, value, sizeof(value));
	if (got != (ssize_t)sizeof(value) || close(fd) != 0 ||
	    memcmp(value, RECOVERY_STALE_VALUE, sizeof(value)) != 0)
		return -1;
	return 1;
}

static int known_identity_footprint(int home_fd, int state_fd)
{
	static const char *home_names[] = {
		"tox.save.tmp", "groups.tsv", "group-friends.tsv", "group-registry.pending",
		"direct-friends.tsv", "direct-add.pending", "direct-remove.pending",
		"direct-state-reinvite.required", "history", "avatars", "files", "ratchet"
	};
	static const char *state_names[] = {
		RECOVERY_NAME, RECOVERY_PENDING_NAME, PRIMARY_UNCERTAIN_NAME,
		PRIMARY_ACK_NAME, RECOVERY_STALE_NAME,
		"identity-replace.txn", "unread.tsv", "group-bind.pending",
		"read-receipts.tsv", "read-transaction.tsv", "read-transaction.committed"
	};
	struct stat st;

	for (size_t i = 0; i < sizeof(home_names) / sizeof(home_names[0]); i++) {
		if (fstatat(home_fd, home_names[i], &st, AT_SYMLINK_NOFOLLOW) == 0)
			return 1;
		if (errno != ENOENT)
			return -1;
	}
	for (size_t i = 0; i < sizeof(state_names) / sizeof(state_names[0]); i++) {
		if (fstatat(state_fd, state_names[i], &st, AT_SYMLINK_NOFOLLOW) == 0)
			return 1;
		if (errno != ENOENT)
			return -1;
	}
	return 0;
}

static int copy_recovery_to_home(int state_fd, int home_fd)
{
	char temporary[64] = "";
	unsigned char buffer[4096];
	struct stat st;
	uint32_t nonce;
	size_t total = 0;
	int source = -1, destination = -1, rc = -1;

	source = openat(state_fd, RECOVERY_NAME, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (source < 0 || fstat(source, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 0777) != 0600 ||
	    st.st_size <= 0 || (uint64_t)st.st_size > OMAQ_IDENTITY_FILE_MAX ||
	    fstatat(home_fd, "tox.save", &st, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT ||
	    getrandom(&nonce, sizeof(nonce), 0) != (ssize_t)sizeof(nonce) ||
	    snprintf(temporary, sizeof(temporary), ".tox.save.restore.%08x", nonce) >=
		(int)sizeof(temporary))
		goto done;
	destination = openat(home_fd, temporary,
			     O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (destination < 0)
		goto done;
	for (;;) {
		ssize_t got = read(source, buffer, sizeof(buffer));
		if (got < 0) {
			if (errno == EINTR)
				continue;
			goto done;
		}
		if (got == 0)
			break;
		if ((size_t)got > OMAQ_IDENTITY_FILE_MAX - total ||
		    write_all(destination, buffer, (size_t)got) != 0)
			goto done;
		total += (size_t)got;
	}
	if (total == 0 || fstat(source, &st) != 0 || st.st_size != (off_t)total ||
	    fsync(destination) != 0 || close(destination) != 0)
		goto done;
	destination = -1;
	if (close(source) != 0)
		goto done;
	source = -1;
	if (fstatat(home_fd, "tox.save", &st, AT_SYMLINK_NOFOLLOW) == 0 || errno != ENOENT ||
	    renameat(home_fd, temporary, home_fd, "tox.save") != 0 || fsync(home_fd) != 0)
		goto done;
	rc = 0;
done:
	if (source >= 0)
		close(source);
	if (destination >= 0)
		close(destination);
	if (rc != 0 && temporary[0])
		(void)unlinkat(home_fd, temporary, 0);
	return rc;
}

int omaq_identity_guard_preflight(const char *home, const char *state)
{
	char fingerprint[65];
	int home_fd = -1, state_fd = -1;
	int ack, guard, primary, recovery, pending, stale, uncertainty, footprint;
	int result = OMAQ_IDENTITY_GUARD_INVALID;

	home_fd = open_private_directory(home);
	state_fd = open_private_directory(state);
	if (home_fd < 0 || state_fd < 0)
		goto done;
	guard = read_guard_at(state_fd, fingerprint);
	primary = private_file_at(home_fd, "tox.save", OMAQ_IDENTITY_FILE_MAX, NULL);
	recovery = private_file_at(state_fd, RECOVERY_NAME, OMAQ_IDENTITY_FILE_MAX, NULL);
	pending = recovery_pending_at(state_fd);
	stale = recovery_stale_at(state_fd);
	uncertainty = primary_uncertain_at(state_fd);
	ack = primary_ack_at(state_fd);
	if (guard < 0 || primary < 0 || recovery < 0 || pending < 0 || stale < 0 ||
	    uncertainty < 0 || ack < 0 || (pending == 1 && guard != 1))
		goto done;
	if (primary == 1) {
		if (guard == 0 && (recovery == 1 || pending == 1 || stale == 1 ||
				   uncertainty == 1 || ack == 1))
			result = OMAQ_IDENTITY_GUARD_INVALID;
		else
			result = pending == 1 ? OMAQ_IDENTITY_GUARD_RESTORED :
				OMAQ_IDENTITY_GUARD_EXISTING;
		goto done;
	}
	if (guard == 1) {
		if (recovery == 0 || stale == 1) {
			result = OMAQ_IDENTITY_GUARD_MISSING;
			goto done;
		}
		if ((pending == 1 || persist_recovery_pending(state_fd) == 0) &&
		    copy_recovery_to_home(state_fd, home_fd) == 0) {
			result = OMAQ_IDENTITY_GUARD_RESTORED;
		} else {
			(void)remove_recovery_pending_at(state_fd);
			result = OMAQ_IDENTITY_GUARD_INVALID;
		}
		goto done;
	}
	footprint = known_identity_footprint(home_fd, state_fd);
	result = footprint == 0 ? OMAQ_IDENTITY_GUARD_FRESH :
		(footprint > 0 ? OMAQ_IDENTITY_GUARD_MISSING : OMAQ_IDENTITY_GUARD_INVALID);
done:
	if (home_fd >= 0)
		close(home_fd);
	if (state_fd >= 0)
		close(state_fd);
	return result;
}

int omaq_identity_guard_expected(const char *state, char fingerprint[65])
{
	int state_fd, rc;

	if (!fingerprint || (state_fd = open_private_directory(state)) < 0)
		return -1;
	rc = read_guard_at(state_fd, fingerprint);
	close(state_fd);
	return rc == 1 ? 0 : -1;
}

int omaq_identity_guard_verify_or_create(const char *state, const char *fingerprint)
{
	char existing[65];
	int state_fd, marker, rc = OMAQ_IDENTITY_GUARD_INVALID;

	if (!fingerprint_ok(fingerprint) || (state_fd = open_private_directory(state)) < 0)
		return OMAQ_IDENTITY_GUARD_INVALID;
	marker = read_guard_at(state_fd, existing);
	if (marker == 1)
		rc = strcmp(existing, fingerprint) == 0 ? 0 : OMAQ_IDENTITY_GUARD_MISMATCH;
	else if (marker == 0) {
		int write_rc = write_guard_at(state_fd, fingerprint, 0);
		if (write_rc == 0 || write_rc == OMAQ_IDENTITY_GUARD_PUBLISHED)
			rc = write_rc;
		else if (errno == EEXIST && read_guard_at(state_fd, existing) == 1)
			rc = strcmp(existing, fingerprint) == 0 ? 0 : OMAQ_IDENTITY_GUARD_MISMATCH;
	}
	close(state_fd);
	return rc;
}

int omaq_identity_guard_replace(const char *state, const char *expected,
				const char *replacement)
{
	char existing[65];
	int state_fd, write_rc, rc = OMAQ_IDENTITY_GUARD_INVALID;

	if (!fingerprint_ok(expected) || !fingerprint_ok(replacement) ||
	    (state_fd = open_private_directory(state)) < 0)
		return OMAQ_IDENTITY_GUARD_INVALID;
	if (read_guard_at(state_fd, existing) == 1 && strcmp(existing, expected) == 0) {
		write_rc = write_guard_at(state_fd, replacement, 1);
		if (write_rc == 0 || write_rc == OMAQ_IDENTITY_GUARD_PUBLISHED)
			rc = write_rc;
	} else if (fingerprint_ok(existing) && strcmp(existing, expected) != 0)
		rc = OMAQ_IDENTITY_GUARD_MISMATCH;
	close(state_fd);
	return rc;
}

int omaq_identity_guard_restore(const char *state, const char *fingerprint)
{
	int state_fd, rc;

	if (!fingerprint_ok(fingerprint) || (state_fd = open_private_directory(state)) < 0)
		return OMAQ_IDENTITY_GUARD_INVALID;
	rc = write_guard_at(state_fd, fingerprint, 1);
	if (rc != 0 && rc != OMAQ_IDENTITY_GUARD_PUBLISHED)
		rc = OMAQ_IDENTITY_GUARD_INVALID;
	close(state_fd);
	return rc;
}

int omaq_identity_guard_finish_recovery(const char *state)
{
	int state_fd, rc;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	rc = recovery_pending_at(state_fd);
	if (rc >= 0)
		rc = remove_recovery_pending_at(state_fd);
	close(state_fd);
	return rc < 0 ? -1 : 0;
}

int omaq_identity_guard_reject_recovery(const char *home, const char *state)
{
	struct stat st;
	int home_fd = -1, state_fd = -1, pending, rc = -1;

	home_fd = open_private_directory(home);
	state_fd = open_private_directory(state);
	if (home_fd < 0 || state_fd < 0)
		goto done;
	pending = recovery_pending_at(state_fd);
	if (pending != 1)
		goto done;
	if (fstatat(home_fd, "tox.save", &st, AT_SYMLINK_NOFOLLOW) == 0) {
		if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
		    (st.st_mode & 0777) != 0600 || unlinkat(home_fd, "tox.save", 0) != 0 ||
		    fsync(home_fd) != 0)
			goto done;
	} else if (errno != ENOENT) {
		goto done;
	}
	if (remove_recovery_pending_at(state_fd) == 0)
		rc = 0;
done:
	if (home_fd >= 0)
		close(home_fd);
	if (state_fd >= 0)
		close(state_fd);
	return rc;
}

int omaq_identity_guard_prepare_repair(const char *state)
{
	static const char *names[] = {
		RECOVERY_NAME, RECOVERY_PENDING_NAME, PRIMARY_UNCERTAIN_NAME,
		PRIMARY_ACK_NAME, RECOVERY_STALE_NAME
	};
	struct stat st;
	int state_fd, rc = -1;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		if (fstatat(state_fd, names[i], &st, AT_SYMLINK_NOFOLLOW) != 0) {
			if (errno == ENOENT)
				continue;
			goto done;
		}
		if (!(S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) ||
		    unlinkat(state_fd, names[i], 0) != 0)
			goto done;
	}
	if (fsync(state_fd) == 0)
		rc = 0;
done:
	close(state_fd);
	return rc;
}

int omaq_identity_primary_uncertain_present(const char *state)
{
	int state_fd, rc;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	rc = primary_uncertain_at(state_fd);
	close(state_fd);
	return rc;
}

int omaq_identity_primary_uncertain_persist(const char *state)
{
	int state_fd, fd = -1, rc = -1;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	rc = primary_uncertain_at(state_fd);
	if (rc != 0) {
		close(state_fd);
		return rc > 0 ? 0 : -1;
	}
	rc = -1;
	fd = openat(state_fd, PRIMARY_UNCERTAIN_NAME,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		goto done;
	{
		int failed = write_all(fd, PRIMARY_UNCERTAIN_VALUE,
				       sizeof(PRIMARY_UNCERTAIN_VALUE) - 1u) != 0 ||
			     fsync(fd) != 0;
		if (close(fd) != 0)
			failed = 1;
		fd = -1;
		if (!failed && fsync(state_fd) == 0)
			rc = 0;
	}
done:
	if (fd >= 0)
		close(fd);
	if (rc != 0) {
		(void)unlinkat(state_fd, PRIMARY_UNCERTAIN_NAME, 0);
		(void)fsync(state_fd);
	}
	close(state_fd);
	return rc;
}

int omaq_identity_primary_uncertain_clear(const char *state)
{
	int state_fd, rc;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	if (primary_uncertain_at(state_fd) < 0 ||
	    (unlinkat(state_fd, PRIMARY_UNCERTAIN_NAME, 0) != 0 && errno != ENOENT)) {
		close(state_fd);
		return -1;
	}
	rc = fsync(state_fd);
	close(state_fd);
	return rc;
}

int omaq_identity_primary_ack_present(const char *state)
{
	int state_fd, rc;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	rc = primary_ack_at(state_fd);
	close(state_fd);
	return rc;
}

int omaq_identity_primary_ack_persist(const char *state)
{
	int state_fd, fd = -1, rc = -1;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	rc = primary_ack_at(state_fd);
	if (rc != 0) {
		close(state_fd);
		return rc > 0 ? 0 : -1;
	}
	rc = -1;
	fd = openat(state_fd, PRIMARY_ACK_NAME,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		goto done_ack;
	{
		int failed = write_all(fd, PRIMARY_ACK_VALUE,
				       sizeof(PRIMARY_ACK_VALUE) - 1u) != 0 || fsync(fd) != 0;
		if (close(fd) != 0)
			failed = 1;
		fd = -1;
		if (!failed && fsync(state_fd) == 0)
			rc = 0;
	}
done_ack:
	if (fd >= 0)
		close(fd);
	if (rc != 0) {
		(void)unlinkat(state_fd, PRIMARY_ACK_NAME, 0);
		(void)fsync(state_fd);
	}
	close(state_fd);
	return rc;
}

int omaq_identity_primary_ack_clear(const char *state)
{
	int state_fd, rc;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	if (primary_ack_at(state_fd) < 0 ||
	    (unlinkat(state_fd, PRIMARY_ACK_NAME, 0) != 0 && errno != ENOENT)) {
		close(state_fd);
		return -1;
	}
	rc = fsync(state_fd);
	close(state_fd);
	return rc;
}

int omaq_identity_recovery_stale_present(const char *state)
{
	int state_fd, rc;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	rc = recovery_stale_at(state_fd);
	close(state_fd);
	return rc;
}

int omaq_identity_recovery_stale_persist(const char *state)
{
	int state_fd, fd = -1, rc = -1;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	rc = recovery_stale_at(state_fd);
	if (rc != 0) {
		close(state_fd);
		return rc > 0 ? 0 : -1;
	}
	rc = -1;
	fd = openat(state_fd, RECOVERY_STALE_NAME,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		goto done_stale;
	{
		int failed = write_all(fd, RECOVERY_STALE_VALUE,
				       sizeof(RECOVERY_STALE_VALUE) - 1u) != 0 ||
			     fsync(fd) != 0;
		if (close(fd) != 0)
			failed = 1;
		fd = -1;
		if (!failed && fsync(state_fd) == 0)
			rc = 0;
	}
done_stale:
	if (fd >= 0)
		close(fd);
	if (rc != 0) {
		(void)unlinkat(state_fd, RECOVERY_STALE_NAME, 0);
		(void)fsync(state_fd);
	}
	close(state_fd);
	return rc;
}

int omaq_identity_recovery_stale_clear(const char *state)
{
	int state_fd, rc;

	state_fd = open_private_directory(state);
	if (state_fd < 0)
		return -1;
	if (recovery_stale_at(state_fd) < 0 ||
	    (unlinkat(state_fd, RECOVERY_STALE_NAME, 0) != 0 && errno != ENOENT)) {
		close(state_fd);
		return -1;
	}
	rc = fsync(state_fd);
	close(state_fd);
	return rc;
}

#ifdef OMAQ_IDENTITY_GUARD_TEST
void omaq_identity_guard_test_fail_directory_fsync(void)
{
	test_fail_directory_fsync = 1;
}

void omaq_identity_guard_test_fail_recovery_write(void)
{
	test_fail_recovery_write = 1;
}
#endif

int omaq_identity_recovery_write(const char *state, const void *data, size_t length)
{
	char temporary[64] = "";
	struct stat st;
	uint32_t nonce;
	int state_fd = -1, fd = -1, existing, rc = -1;

#ifdef OMAQ_IDENTITY_GUARD_TEST
	if (test_fail_recovery_write) {
		test_fail_recovery_write = 0;
		return -1;
	}
#endif
	if (!data || length == 0 || length > OMAQ_IDENTITY_FILE_MAX ||
	    (state_fd = open_private_directory(state)) < 0)
		return -1;
	existing = private_file_at(state_fd, RECOVERY_NAME, OMAQ_IDENTITY_FILE_MAX, &st);
	if (existing < 0 || getrandom(&nonce, sizeof(nonce), 0) != (ssize_t)sizeof(nonce) ||
	    snprintf(temporary, sizeof(temporary), ".%s.tmp.%08x", RECOVERY_NAME, nonce) >=
		(int)sizeof(temporary))
		goto done;
	fd = openat(state_fd, temporary,
		    O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		goto done;
	{
		int failed = write_all(fd, data, length) != 0 || fsync(fd) != 0;
		if (close(fd) != 0)
			failed = 1;
		fd = -1;
		if (failed)
			goto done;
	}
	if (renameat(state_fd, temporary, state_fd, RECOVERY_NAME) != 0 ||
	    fsync(state_fd) != 0)
		goto done;
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (rc != 0 && state_fd >= 0 && temporary[0])
		(void)unlinkat(state_fd, temporary, 0);
	if (state_fd >= 0)
		close(state_fd);
	return rc;
}
