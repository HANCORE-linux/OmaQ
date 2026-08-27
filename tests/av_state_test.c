#include "../helper/av.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;
static int hangup_result;
static unsigned int hangups;

static void check(int condition, const char *name)
{
	if (condition)
		return;
	fprintf(stderr, "av_state_test: %s\n", name);
	failures++;
}

int omaq_tox_av_call(struct omaq_tox *t, uint32_t friend)
{
	(void)t;
	(void)friend;
	return 0;
}

int omaq_tox_av_answer(struct omaq_tox *t, uint32_t friend)
{
	(void)t;
	(void)friend;
	return 0;
}

int omaq_tox_av_hangup(struct omaq_tox *t, uint32_t friend)
{
	(void)t;
	(void)friend;
	hangups++;
	return hangup_result;
}

int omaq_tox_av_audio_send(struct omaq_tox *t, uint32_t friend,
			   const int16_t *pcm, size_t samples,
			   uint8_t channels, uint32_t rate)
{
	(void)t;
	(void)friend;
	(void)pcm;
	(void)samples;
	(void)channels;
	(void)rate;
	return 0;
}

int main(void)
{
	struct omaq_tox *tox = (struct omaq_tox *)(uintptr_t)1;
	uint32_t status_friend = UINT32_MAX;
	const char *status_state = NULL;

	omaq_av_reset();
	check(omaq_av_note_incoming(7) == 1, "incoming transition");
	check(omaq_av_note_incoming(7) == 0, "incoming idempotence");
	check(omaq_av_friend_busy(7) && !omaq_av_friend_busy(8),
	      "friend-specific call busy state");
	check(omaq_av_status(&status_friend, &status_state) == 1 && status_friend == 7 &&
	      status_state && strcmp(status_state, "incoming") == 0, "incoming status");
	check(omaq_av_note_active(7) == 1, "active transition");
	check(omaq_av_note_active(7) == 0, "active idempotence");
	check(omaq_av_status(&status_friend, &status_state) == 1 &&
	      status_state && strcmp(status_state, "active") == 0, "active status");
	check(omaq_av_note_end(8) == -1 && omaq_av_is_current(7),
	      "stale end isolation");

	hangup_result = -1;
	check(omaq_av_stop(tox, 7) == 1, "local stop survives hangup failure");
	check(hangups == 1 && !omaq_av_busy() && !omaq_av_is_current(7),
	      "failed hangup finalizes local state");
	check(omaq_av_status(&status_friend, &status_state) == 0,
	      "failed hangup omitted from status");
	check(omaq_av_note_active(7) == -1, "delayed active blocked after failed hangup");
	check(omaq_av_note_end(7) == -1, "delayed end ignored after failed hangup");
	check(omaq_av_start(tox, 7) == -1, "same-friend redial cooldown");
	check(omaq_av_start(tox, 8) == 0, "different-friend call after end");
	check(omaq_av_note_end(7) == -1 && omaq_av_is_current(8),
	      "delayed prior callback cannot end current friend");

	omaq_av_reset();
	check(omaq_av_start(tox, 7) == 0, "reset clears redial cooldown");
	check(omaq_av_stop(tox, 8) == -1 && omaq_av_is_current(7),
	      "stale local stop isolation");
	omaq_av_reset();
	hangup_result = 0;
	check(omaq_av_start(tox, 7) == 0 && omaq_av_stop(tox, 7) == 0 &&
	      !omaq_av_busy(), "successful cancel enters cooldown");
	check(omaq_av_note_active(7) == -1 && omaq_av_note_incoming(7) == -1 &&
	      omaq_av_start(tox, 7) == -1, "delayed same-friend callbacks blocked");
	check(omaq_av_start(tox, 8) == 0, "cooldown is friend-specific");
	omaq_av_reset();
	check(omaq_av_start(tox, 7) == 0 && omaq_av_stop(tox, 7) == 0,
	      "friend forget fixture");
	omaq_av_forget_friend(7);
	check(omaq_av_start(tox, 7) == -1 &&
	      omaq_av_note_active(7) == -1 && omaq_av_note_end(7) == -1,
	      "friend forget preserves delayed-callback cooldown");
	omaq_av_reset();

	if (failures)
		return 1;
	puts("av_state_test: ok");
	return 0;
}
