#ifndef OMAQ_AV_H
#define OMAQ_AV_H

#include <stddef.h>
#include <stdint.h>

#ifdef HAVE_TOX
#include "tox_adapt.h"

int omaq_av_start(struct omaq_tox *t, uint32_t friend);
int omaq_av_answer(struct omaq_tox *t, uint32_t friend);
int omaq_av_stop(struct omaq_tox *t, uint32_t friend);
int omaq_av_busy(void);
int omaq_av_note_incoming(uint32_t friend);
int omaq_av_note_active(uint32_t friend);
int omaq_av_note_end(uint32_t friend);
void omaq_av_receive(uint32_t friend, const int16_t *pcm, size_t samples,
		     uint8_t channels, uint32_t rate);
int omaq_av_pump(struct omaq_tox *t);
int omaq_av_is_current(uint32_t friend);
int omaq_av_take_audio_error(uint32_t *friend);
int omaq_av_status(uint32_t *friend, const char **state);
int omaq_av_friend_busy(uint32_t friend);
void omaq_av_forget_friend(uint32_t friend);
void omaq_av_reset(void);
#endif

#endif
