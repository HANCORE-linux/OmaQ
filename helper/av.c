#include "av.h"

#ifdef HAVE_TOX

#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_PULSE
#include <pulse/error.h>
#include <pulse/simple.h>
#endif

#define OMAQ_AUDIO_RATE 48000u
#define OMAQ_AUDIO_FRAME_SAMPLES 960u
#define OMAQ_AUDIO_MAX_SAMPLES 2880u
#define OMAQ_AUDIO_RING_FRAMES 16u

typedef struct {
	size_t samples;
	int16_t pcm[OMAQ_AUDIO_MAX_SAMPLES];
} omaq_audio_frame;

static uint32_t g_call = UINT32_MAX;
static int g_active;
static int g_ending;
static int g_incoming;
static uint32_t g_recent_call = UINT32_MAX;
static struct timespec g_recent_call_end;
static pthread_mutex_t g_audio_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_capture_thread;
static pthread_t g_playback_thread;
static int g_capture_started;
static int g_playback_started;
static int g_audio_running;
static int g_audio_error;
static uint32_t g_audio_error_friend = UINT32_MAX;
static sem_t g_playback_ready;
static int g_playback_sem_ready;
static omaq_audio_frame g_capture_ring[OMAQ_AUDIO_RING_FRAMES];
static unsigned int g_capture_head;
static unsigned int g_capture_count;
static omaq_audio_frame g_playback_ring[OMAQ_AUDIO_RING_FRAMES];
static unsigned int g_playback_head;
static unsigned int g_playback_count;

static void note_audio_error(uint32_t friend)
{
	pthread_mutex_lock(&g_audio_lock);
	g_audio_error = 1;
	g_audio_error_friend = friend;
	pthread_mutex_unlock(&g_audio_lock);
}

static int recent_call_blocked(uint32_t friend)
{
	struct timespec now;
	int64_t elapsed_ms;

	if (friend != g_recent_call ||
	    clock_gettime(CLOCK_MONOTONIC, &now) != 0)
		return 0;
	elapsed_ms = (int64_t)(now.tv_sec - g_recent_call_end.tv_sec) * 1000 +
		(now.tv_nsec - g_recent_call_end.tv_nsec) / 1000000;
	return elapsed_ms >= 0 && elapsed_ms < 2000;
}

static void note_recent_call(uint32_t friend)
{
	g_recent_call = friend;
	if (clock_gettime(CLOCK_MONOTONIC, &g_recent_call_end) != 0)
		memset(&g_recent_call_end, 0, sizeof(g_recent_call_end));
}

static void clear_audio_error(void)
{
	pthread_mutex_lock(&g_audio_lock);
	g_audio_error = 0;
	g_audio_error_friend = UINT32_MAX;
	pthread_mutex_unlock(&g_audio_lock);
}

static int audio_running(void)
{
	int running;

	pthread_mutex_lock(&g_audio_lock);
	running = g_audio_running;
	pthread_mutex_unlock(&g_audio_lock);
	return running;
}

#ifdef HAVE_PULSE
static void pulse_free(void *arg)
{
	pa_simple *stream = arg;

	if (stream)
		pa_simple_free(stream);
}

static void *capture_main(void *arg)
{
	uint32_t friend = (uint32_t)(uintptr_t)arg;
	pa_sample_spec spec = {
		.format = PA_SAMPLE_S16LE,
		.rate = OMAQ_AUDIO_RATE,
		.channels = 1,
	};
	pa_buffer_attr attr = {
		.maxlength = UINT32_MAX,
		.tlength = UINT32_MAX,
		.prebuf = UINT32_MAX,
		.minreq = UINT32_MAX,
		.fragsize = OMAQ_AUDIO_FRAME_SAMPLES * sizeof(int16_t),
	};
	pa_simple *stream;
	int error = 0;
	int16_t frame[OMAQ_AUDIO_FRAME_SAMPLES];

	stream = pa_simple_new(NULL, "OmaQ", PA_STREAM_RECORD, NULL,
			       "Voice call", &spec, NULL, &attr, &error);
	if (!stream) {
		note_audio_error(friend);
		return NULL;
	}
	pthread_cleanup_push(pulse_free, stream);
	while (audio_running()) {
		unsigned int slot;

		if (pa_simple_read(stream, frame, sizeof(frame), &error) < 0) {
			if (audio_running())
				note_audio_error(friend);
			break;
		}
		pthread_mutex_lock(&g_audio_lock);
		if (!g_audio_running) {
			pthread_mutex_unlock(&g_audio_lock);
			break;
		}
		if (g_capture_count == OMAQ_AUDIO_RING_FRAMES) {
			g_capture_head = (g_capture_head + 1u) % OMAQ_AUDIO_RING_FRAMES;
			g_capture_count--;
		}
		slot = (g_capture_head + g_capture_count) % OMAQ_AUDIO_RING_FRAMES;
		g_capture_ring[slot].samples = OMAQ_AUDIO_FRAME_SAMPLES;
		memcpy(g_capture_ring[slot].pcm, frame, sizeof(frame));
		g_capture_count++;
		pthread_mutex_unlock(&g_audio_lock);
	}
	pthread_cleanup_pop(1);
	return NULL;
}

static void *playback_main(void *arg)
{
	uint32_t friend = (uint32_t)(uintptr_t)arg;
	pa_sample_spec spec = {
		.format = PA_SAMPLE_S16LE,
		.rate = OMAQ_AUDIO_RATE,
		.channels = 1,
	};
	pa_buffer_attr attr = {
		.maxlength = UINT32_MAX,
		.tlength = OMAQ_AUDIO_FRAME_SAMPLES * sizeof(int16_t) * 4u,
		.prebuf = 0,
		.minreq = UINT32_MAX,
		.fragsize = UINT32_MAX,
	};
	pa_simple *stream;
	int error = 0;

	stream = pa_simple_new(NULL, "OmaQ", PA_STREAM_PLAYBACK, NULL,
			       "Voice call", &spec, NULL, &attr, &error);
	if (!stream) {
		note_audio_error(friend);
		return NULL;
	}
	pthread_cleanup_push(pulse_free, stream);
	for (;;) {
		omaq_audio_frame frame;
		int have_frame = 0;
		int more_frames = 0;

		while (sem_wait(&g_playback_ready) != 0 && errno == EINTR)
			;
		pthread_mutex_lock(&g_audio_lock);
		if (g_playback_count > 0) {
			frame = g_playback_ring[g_playback_head];
			g_playback_head = (g_playback_head + 1u) % OMAQ_AUDIO_RING_FRAMES;
			g_playback_count--;
			have_frame = 1;
			more_frames = g_playback_count > 0;
		}
		if (!g_audio_running && !have_frame) {
			pthread_mutex_unlock(&g_audio_lock);
			break;
		}
		pthread_mutex_unlock(&g_audio_lock);
		if (more_frames && g_playback_sem_ready)
			(void)sem_post(&g_playback_ready);
		if (have_frame && pa_simple_write(stream, frame.pcm,
					       frame.samples * sizeof(int16_t), &error) < 0) {
			if (audio_running())
				note_audio_error(friend);
			break;
		}
	}
	pthread_cleanup_pop(1);
	return NULL;
}
#endif

static void audio_stop(void)
{
	pthread_mutex_lock(&g_audio_lock);
	g_audio_running = 0;
	pthread_mutex_unlock(&g_audio_lock);
	if (g_playback_sem_ready)
		(void)sem_post(&g_playback_ready);
	if (g_capture_started)
		(void)pthread_cancel(g_capture_thread);
	if (g_playback_started)
		(void)pthread_cancel(g_playback_thread);
	if (g_capture_started)
		(void)pthread_join(g_capture_thread, NULL);
	if (g_playback_started)
		(void)pthread_join(g_playback_thread, NULL);
	g_capture_started = 0;
	g_playback_started = 0;
	if (g_playback_sem_ready) {
		(void)sem_destroy(&g_playback_ready);
		g_playback_sem_ready = 0;
	}
	pthread_mutex_lock(&g_audio_lock);
	g_capture_head = 0;
	g_capture_count = 0;
	g_playback_head = 0;
	g_playback_count = 0;
	pthread_mutex_unlock(&g_audio_lock);
}

static int audio_start(uint32_t friend)
{
	audio_stop();
	pthread_mutex_lock(&g_audio_lock);
	g_audio_running = 1;
	g_audio_error = 0;
	g_audio_error_friend = UINT32_MAX;
	pthread_mutex_unlock(&g_audio_lock);
#ifndef HAVE_PULSE
	note_audio_error(friend);
	return -1;
#else
	if (sem_init(&g_playback_ready, 0, 0) != 0) {
		note_audio_error(friend);
		return -1;
	}
	g_playback_sem_ready = 1;
	if (pthread_create(&g_playback_thread, NULL, playback_main,
			   (void *)(uintptr_t)friend) != 0) {
		note_audio_error(friend);
		audio_stop();
		return -1;
	}
	g_playback_started = 1;
	if (pthread_create(&g_capture_thread, NULL, capture_main,
			   (void *)(uintptr_t)friend) != 0) {
		note_audio_error(friend);
		audio_stop();
		return -1;
	}
	g_capture_started = 1;
	return 0;
#endif
}

int omaq_av_busy(void)
{
	return g_call != UINT32_MAX;
}

int omaq_av_note_incoming(uint32_t friend)
{
	if ((g_call == UINT32_MAX && recent_call_blocked(friend)) ||
	    (g_call != UINT32_MAX && g_call != friend))
		return -1;
	if (g_call == friend)
		return 0;
	g_call = friend;
	g_active = 0;
	g_ending = 0;
	g_incoming = 1;
	return 1;
}

int omaq_av_note_active(uint32_t friend)
{
	if ((g_call == UINT32_MAX && recent_call_blocked(friend)) ||
	    (g_call != UINT32_MAX && g_call != friend))
		return -1;
	if (g_ending)
		return 0;
	g_call = friend;
	if (g_active)
		return 0;
	g_active = 1;
	(void)audio_start(friend);
	return 1;
}

int omaq_av_start(struct omaq_tox *t, uint32_t friend)
{
	if (!t || g_call != UINT32_MAX || recent_call_blocked(friend))
		return -1;
	if (omaq_tox_av_call(t, friend) != 0)
		return -1;
	g_call = friend;
	g_active = 0;
	g_ending = 0;
	g_incoming = 0;
	return 0;
}

int omaq_av_answer(struct omaq_tox *t, uint32_t friend)
{
	if (!t || g_ending)
		return -1;
	if (g_call != UINT32_MAX && g_call != friend)
		return -1;
	if (omaq_tox_av_answer(t, friend) != 0)
		return -1;
	g_call = friend;
	(void)omaq_av_note_active(friend);
	return 0;
}

int omaq_av_stop(struct omaq_tox *t, uint32_t friend)
{
	uint32_t who = friend;

	if (!t)
		return -1;
	if (who == UINT32_MAX)
		who = g_call;
	if (who == UINT32_MAX || g_call == UINT32_MAX || who != g_call)
		return -1;
	audio_stop();
	g_active = 0;
	clear_audio_error();
	{
		int control_failed = omaq_tox_av_hangup(t, who) != 0;
		g_call = UINT32_MAX;
		g_ending = 0;
		g_incoming = 0;
		note_recent_call(who);
		return control_failed ? 1 : 0;
	}
}

int omaq_av_note_end(uint32_t friend)
{
	if (g_call != friend)
		return -1;
	audio_stop();
	g_call = UINT32_MAX;
	g_active = 0;
	g_ending = 0;
	g_incoming = 0;
	note_recent_call(friend);
	clear_audio_error();
	return 1;
}

void omaq_av_receive(uint32_t friend, const int16_t *pcm, size_t samples,
		     uint8_t channels, uint32_t rate)
{
	unsigned int slot;
	omaq_audio_frame *frame;
	int wake;

	if (!pcm || samples == 0 || samples > OMAQ_AUDIO_MAX_SAMPLES ||
	    (channels != 1 && channels != 2) || rate != OMAQ_AUDIO_RATE ||
	    !g_active || friend != g_call)
		return;
	pthread_mutex_lock(&g_audio_lock);
	if (!g_audio_running) {
		pthread_mutex_unlock(&g_audio_lock);
		return;
	}
	wake = g_playback_count == 0;
	if (g_playback_count == OMAQ_AUDIO_RING_FRAMES) {
		g_playback_head = (g_playback_head + 1u) % OMAQ_AUDIO_RING_FRAMES;
		g_playback_count--;
	}
	slot = (g_playback_head + g_playback_count) % OMAQ_AUDIO_RING_FRAMES;
	frame = &g_playback_ring[slot];
	frame->samples = samples;
	if (channels == 1) {
		memcpy(frame->pcm, pcm, samples * sizeof(int16_t));
	} else {
		for (size_t i = 0; i < samples; i++) {
			int32_t mixed = (int32_t)pcm[i * 2] + (int32_t)pcm[i * 2 + 1];
			frame->pcm[i] = (int16_t)(mixed / 2);
		}
	}
	g_playback_count++;
	pthread_mutex_unlock(&g_audio_lock);
	if (wake && g_playback_sem_ready)
		(void)sem_post(&g_playback_ready);
}

int omaq_av_pump(struct omaq_tox *t)
{
	for (;;) {
		omaq_audio_frame frame;
		int have_frame = 0;

		pthread_mutex_lock(&g_audio_lock);
		if (g_capture_count > 0) {
			frame = g_capture_ring[g_capture_head];
			g_capture_head = (g_capture_head + 1u) % OMAQ_AUDIO_RING_FRAMES;
			g_capture_count--;
			have_frame = 1;
		}
		pthread_mutex_unlock(&g_audio_lock);
		if (!have_frame)
			break;
		if (g_call == UINT32_MAX || !g_active ||
		    omaq_tox_av_audio_send(t, g_call, frame.pcm, frame.samples,
					   1, OMAQ_AUDIO_RATE) != 0) {
			note_audio_error(g_call);
			return -1;
		}
	}
	return 0;
}

int omaq_av_is_current(uint32_t friend)
{
	return g_call != UINT32_MAX && g_call == friend;
}

int omaq_av_status(uint32_t *friend, const char **state)
{
	if (g_call == UINT32_MAX || g_ending)
		return 0;
	if (friend)
		*friend = g_call;
	if (state)
		*state = g_active ? "active" : (g_incoming ? "incoming" : "ringing");
	return 1;
}

int omaq_av_take_audio_error(uint32_t *friend)
{
	int failed;

	pthread_mutex_lock(&g_audio_lock);
	failed = g_audio_error;
	if (failed) {
		if (friend)
			*friend = g_audio_error_friend;
		g_audio_error = 0;
		g_audio_error_friend = UINT32_MAX;
	}
	pthread_mutex_unlock(&g_audio_lock);
	return failed;
}

void omaq_av_reset(void)
{
	audio_stop();
	g_call = UINT32_MAX;
	g_active = 0;
	g_ending = 0;
	g_incoming = 0;
	g_recent_call = UINT32_MAX;
	memset(&g_recent_call_end, 0, sizeof(g_recent_call_end));
	clear_audio_error();
}

#endif /* HAVE_TOX */
