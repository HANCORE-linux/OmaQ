#ifndef OMAQ_AV_H
#define OMAQ_AV_H

#include <stdint.h>

#ifdef HAVE_TOX
#include "tox_adapt.h"

int omaq_av_start(struct omaq_tox *t, uint32_t friend);
int omaq_av_answer(struct omaq_tox *t, uint32_t friend);
int omaq_av_stop(struct omaq_tox *t, uint32_t friend);
void omaq_av_note_end(uint32_t friend);
#endif

#endif
