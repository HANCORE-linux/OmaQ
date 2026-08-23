#define _DEFAULT_SOURCE
#include "../helper/avatar.h"
#include "../helper/file.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct omaq_tox { int unused; };

static int fails;

static void fail(const char *message)
{
	fprintf(stderr, "FAIL: %s\n", message);
	fails++;
}

int omaq_tox_file_send(struct omaq_tox *t, uint32_t friend, uint64_t size,
		       const char *name, uint32_t *fnum)
{
	(void)t;
	(void)friend;
	(void)size;
	(void)name;
	if (fnum)
		*fnum = 1;
	return 0;
}

int omaq_tox_file_send_avatar(struct omaq_tox *t, uint32_t friend, uint64_t size,
			      const uint8_t file_id[32], uint32_t *fnum)
{
	(void)t;
	(void)friend;
	(void)size;
	(void)file_id;
	if (fnum)
		*fnum = 2;
	return 0;
}

int omaq_tox_file_chunk(struct omaq_tox *t, uint32_t friend, uint32_t fnum,
			uint64_t pos, const uint8_t *data, size_t len)
{
	(void)t;
	(void)friend;
	(void)fnum;
	(void)pos;
	(void)data;
	(void)len;
	return 0;
}

int omaq_tox_file_control(struct omaq_tox *t, uint32_t friend, uint32_t fnum, int control)
{
	(void)t;
	(void)friend;
	(void)fnum;
	(void)control;
	return 0;
}

int main(void)
{
	char home[] = "/tmp/omaq-file-kind-XXXXXX";
	char avatar_dir[512] = "";
	char dest[512];
	char got[512];
	const uint8_t data[4] = { 1, 2, 3, 4 };

	if (!mkdtemp(home)) {
		fail("mkdtemp");
		goto out;
	}
	if (snprintf(avatar_dir, sizeof(avatar_dir), "%s/avatars", home) >=
	    (int)sizeof(avatar_dir) || mkdir(avatar_dir, 0700) != 0) {
		fail("avatar fixture directory");
		goto out;
	}

	/* A normal file keeps normal failure semantics even at an avatar-looking path. */
	if (snprintf(dest, sizeof(dest), "%s/avatars/7.png", home) >= (int)sizeof(dest) ||
	    omaq_file_recv_begin(home, "7", 7, 10, "photo.png", sizeof(data),
				 dest, got, sizeof(got), 0) != 0)
		fail("normal avatar-like receive begin");
	else {
		if (!omaq_avatar_is_dest(home, dest) || omaq_file_is_avatar(7, 10))
			fail("normal avatar-like path classification");
		if (omaq_file_chunk_in(7, 10, sizeof(data), data, 1, NULL, 0) == 0)
			fail("normal transfer error fixture");
		if (omaq_file_event_for(omaq_file_is_avatar(7, 10), OMAQ_FILE_OUTCOME_ERROR) !=
		    OMAQ_FILE_EVENT_FAILED)
			fail("normal transfer error must report file.failed");
		omaq_file_cancel(NULL, 7, 10);
	}

	/* Error and cancel paths retain the explicit avatar status until cleanup. */
	if (snprintf(dest, sizeof(dest), "%s/avatars/8.png", home) >= (int)sizeof(dest) ||
	    omaq_file_recv_begin(home, "8", 8, 11, "avatar.png", sizeof(data),
				 dest, got, sizeof(got), 1) != 0)
		fail("avatar error receive begin");
	else {
		if (omaq_file_chunk_in(8, 11, sizeof(data), data, 1, NULL, 0) == 0 ||
		    !omaq_file_is_avatar(8, 11) ||
		    omaq_file_event_for(1, OMAQ_FILE_OUTCOME_ERROR) != OMAQ_FILE_EVENT_NONE)
			fail("avatar error suppresses file.failed");
		omaq_file_cancel(NULL, 8, 11);
	}
	if (snprintf(dest, sizeof(dest), "%s/avatars/9.png", home) >= (int)sizeof(dest) ||
	    omaq_file_recv_begin(home, "9", 9, 12, "avatar.png", sizeof(data),
				 dest, got, sizeof(got), 1) != 0)
		fail("avatar cancel receive begin");
	else {
		if (!omaq_file_is_avatar(9, 12) ||
		    omaq_file_event_for(1, OMAQ_FILE_OUTCOME_CANCEL) != OMAQ_FILE_EVENT_NONE)
			fail("avatar cancel suppresses file.failed");
		omaq_file_cancel(NULL, 9, 12);
		if (omaq_file_is_avatar(9, 12))
			fail("avatar cancel cleanup");
	}

	/* Successful completion is classified before chunk_in drops the transfer slot. */
	if (snprintf(dest, sizeof(dest), "%s/avatars/13.png", home) >= (int)sizeof(dest) ||
	    omaq_file_recv_begin(home, "13", 13, 13, "avatar.png", sizeof(data),
				 dest, got, sizeof(got), 1) != 0)
		fail("avatar success receive begin");
	else {
		int avatar = omaq_file_is_avatar(13, 13);
		if (!avatar ||
		    omaq_file_event_for(avatar, OMAQ_FILE_OUTCOME_DONE) != OMAQ_FILE_EVENT_AVATAR ||
		    omaq_file_chunk_in(13, 13, 0, data, sizeof(data), NULL, 0) != 0 ||
		    omaq_file_chunk_in(13, 13, sizeof(data), NULL, 0, got, sizeof(got)) != 1 ||
		    strcmp(got, dest) != 0)
			fail("avatar success uses stored status");
		unlink(dest);
	}

out:
	if (home[0]) {
		for (int id = 7; id <= 13; id++) {
			if (snprintf(dest, sizeof(dest), "%s/avatars/%d.png", home, id) <
			    (int)sizeof(dest))
				unlink(dest);
		}
		if (avatar_dir[0])
			rmdir(avatar_dir);
		rmdir(home);
	}
	if (fails) {
		fprintf(stderr, "file_transfer_test: %d failure(s)\n", fails);
		return 1;
	}
	puts("file_transfer_test: ok");
	return 0;
}
