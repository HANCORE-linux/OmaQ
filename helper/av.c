#include "av.h"

#ifdef HAVE_TOX

#include <stdint.h>

static uint32_t g_call = UINT32_MAX;

int omaq_av_start(struct omaq_tox *t, uint32_t friend)
{
	if (!t || g_call != UINT32_MAX)
		return -1;
	if (omaq_tox_av_call(t, friend) != 0)
		return -1;
	g_call = friend;
	return 0;
}

int omaq_av_answer(struct omaq_tox *t, uint32_t friend)
{
	if (!t)
		return -1;
	if (g_call != UINT32_MAX && g_call != friend)
		return -1;
	if (omaq_tox_av_answer(t, friend) != 0)
		return -1;
	g_call = friend;
	return 0;
}

int omaq_av_stop(struct omaq_tox *t, uint32_t friend)
{
	uint32_t who = friend;

	if (!t)
		return -1;
	if (who == UINT32_MAX)
		who = g_call;
	if (who == UINT32_MAX)
		return -1;
	(void)omaq_tox_av_hangup(t, who);
	g_call = UINT32_MAX;
	return 0;
}

void omaq_av_note_end(uint32_t friend)
{
	if (g_call == friend)
		g_call = UINT32_MAX;
}

#endif /* HAVE_TOX */
