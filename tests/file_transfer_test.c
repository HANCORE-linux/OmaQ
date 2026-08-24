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
	uint8_t utf8_name[129];

	if (!mkdtemp(home)) {
		fail("mkdtemp");
		goto out;
	}
	if (snprintf(avatar_dir, sizeof(avatar_dir), "%s/avatars", home) >=
	    (int)sizeof(avatar_dir) || mkdir(avatar_dir, 0700) != 0) {
		fail("avatar fixture directory");
		goto out;
	}

	memset(utf8_name, 'a', sizeof(utf8_name));
	utf8_name[126] = 0xc3;
	utf8_name[127] = 0xa9;
	if (!omaq_file_name_bytes_ok(utf8_name, 128) ||
	    omaq_file_name_bytes_ok(utf8_name, 129) ||
	    omaq_file_name_bytes_ok((const uint8_t *)"bad\200", 4) ||
	    omaq_file_name_bytes_ok((const uint8_t *)"a\0b", 3) ||
	    omaq_file_name_bytes_ok((const uint8_t *)"\302\200", 2) ||
	    omaq_file_name_bytes_ok((const uint8_t *)"\342\200\256", 3) ||
	    omaq_file_name_bytes_ok((const uint8_t *)"\341\240\216", 3) ||
	    omaq_file_name_bytes_ok((const uint8_t *)"\357\277\271", 3) ||
	    omaq_file_name_bytes_ok((const uint8_t *)"\363\240\200\201", 4))
		fail("file name UTF-8 validation");

	/* A normal file keeps normal failure semantics even at an avatar-looking path. */
	if (snprintf(dest, sizeof(dest), "%s/avatars/7.png", home) >= (int)sizeof(dest) ||
	    omaq_file_recv_begin(home, "7", 7, 10, "photo.png", sizeof(data),
				 dest, got, sizeof(got), 0) != 0)
		fail("normal avatar-like receive begin");
	else {
		if (!omaq_avatar_is_dest(home, dest) || omaq_file_is_avatar(7, 10))
			fail("normal avatar-like path classification");
		if (!omaq_file_can_cancel(7, 10))
			fail("active normal transfer can be canceled");
		if (omaq_file_chunk_in(7, 10, sizeof(data), data, 1, NULL, 0) == 0)
			fail("normal transfer error fixture");
		if (omaq_file_event_for(omaq_file_is_avatar(7, 10), OMAQ_FILE_OUTCOME_ERROR) !=
		    OMAQ_FILE_EVENT_FAILED)
			fail("normal transfer error must report file.failed");
		omaq_file_cancel(NULL, 7, 10);
		if (omaq_file_can_cancel(7, 10))
			fail("normal cancel cleanup");
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

	/* Identity reset removes partial incoming files and all in-memory slots. */
	if (snprintf(dest, sizeof(dest), "%s/reset-partial.bin", home) >= (int)sizeof(dest) ||
	    omaq_file_recv_begin(home, "10", 10, 14, "reset-partial.bin", sizeof(data),
			 dest, got, sizeof(got), 0) != 0 ||
	    omaq_file_chunk_in(10, 14, 0, data, 2, NULL, 0) != 0 ||
	    access(dest, F_OK) != 0 || !omaq_file_busy())
		fail("identity reset partial fixture");
	else {
		omaq_file_reset();
		if (access(dest, F_OK) == 0 || omaq_file_can_cancel(10, 14) || omaq_file_busy())
			fail("identity reset partial cleanup");
	}
	if (omaq_file_offer_store(11, 15, "bad\001name.bin", 4) == 0)
		fail("file offer control character rejection");

	/* Collision suffixes preserve the extension used for audio classification. */
	{
		char download_dir[512], existing[512], expected[512];
		FILE *fixture = NULL;
		if (snprintf(download_dir, sizeof(download_dir), "%s/omaq", home) >=
		    (int)sizeof(download_dir) || mkdir(download_dir, 0700) != 0 ||
		    snprintf(existing, sizeof(existing), "%s/song.mp3", download_dir) >=
		    (int)sizeof(existing) ||
		    snprintf(expected, sizeof(expected), "%s/song.1.mp3", download_dir) >=
		    (int)sizeof(expected)) {
			fail("download collision fixture paths");
		} else if (!(fixture = fopen(existing, "wb"))) {
			fail("download collision fixture open");
		} else {
			int write_ok = fwrite(data, 1, sizeof(data), fixture) == sizeof(data);
			int close_ok = fclose(fixture) == 0;
			fixture = NULL;
			if (!write_ok || !close_ok || setenv("OMAQ_DOWNLOAD_DIR", home, 1) != 0 ||
			    omaq_file_recv_begin(home, "30", 30, 30, "song.mp3", sizeof(data),
					 NULL, got, sizeof(got), 0) != 0 || strcmp(got, expected) != 0) {
				fail("download collision keeps extension");
			} else {
				omaq_file_cancel(NULL, 30, 30);
			}
			unlink(existing);
			unlink(expected);
			rmdir(download_dir);
		}
		if (fixture)
			fclose(fixture);
	}

	/* An outgoing transfer remains addressable until local cancellation. */
	if (snprintf(dest, sizeof(dest), "%s/outgoing.bin", home) >= (int)sizeof(dest)) {
		fail("outgoing fixture path");
	} else {
		FILE *outgoing = fopen(dest, "wb");
		uint32_t outgoing_fnum = 0;
		if (!outgoing) {
			fail("outgoing fixture open");
		} else {
			int write_ok = fwrite(data, 1, sizeof(data), outgoing) == sizeof(data);
			int close_ok = fclose(outgoing) == 0;
			if (!write_ok || !close_ok ||
			    omaq_file_send_begin(NULL, 21, dest, &outgoing_fnum) != 0 ||
			    outgoing_fnum != 1 || !omaq_file_can_cancel(21, outgoing_fnum) ||
			    !omaq_file_is_sending(21, outgoing_fnum)) {
				fail("outgoing transfer cancellation setup");
			} else {
				omaq_file_cancel(NULL, 21, outgoing_fnum);
				if (omaq_file_can_cancel(21, outgoing_fnum))
					fail("outgoing transfer cancel cleanup");
			}
		}
		unlink(dest);
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
