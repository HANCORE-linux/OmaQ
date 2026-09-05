#include "av.h"

#ifdef HAVE_TOX

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_PULSE
#include <pulse/pulseaudio.h>
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
static pthread_t g_audio_thread;
static int g_audio_started;
static int g_audio_running;
static int g_audio_error;
static uint32_t g_audio_error_friend = UINT32_MAX;
static omaq_audio_frame g_capture_ring[OMAQ_AUDIO_RING_FRAMES];
static unsigned int g_capture_head;
static unsigned int g_capture_count;
static omaq_audio_frame g_playback_ring[OMAQ_AUDIO_RING_FRAMES];
static unsigned int g_playback_head;
static unsigned int g_playback_count;
#ifdef HAVE_PULSE
static pa_mainloop *g_pulse_mainloop;
#endif

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
typedef struct {
	uint32_t friend;
	size_t capture_samples;
	int16_t capture[OMAQ_AUDIO_FRAME_SAMPLES];
} omaq_pulse_state;

static void pulse_wakeup(void)
{
	pa_mainloop *mainloop;

	pthread_mutex_lock(&g_audio_lock);
	mainloop = g_pulse_mainloop;
	if (mainloop)
		pa_mainloop_wakeup(mainloop);
	pthread_mutex_unlock(&g_audio_lock);
}

static void capture_push(const int16_t *pcm)
{
	unsigned int slot;

	pthread_mutex_lock(&g_audio_lock);
	if (!g_audio_running) {
		pthread_mutex_unlock(&g_audio_lock);
		return;
	}
	if (g_capture_count == OMAQ_AUDIO_RING_FRAMES) {
		g_capture_head = (g_capture_head + 1u) % OMAQ_AUDIO_RING_FRAMES;
		g_capture_count--;
	}
	slot = (g_capture_head + g_capture_count) % OMAQ_AUDIO_RING_FRAMES;
	g_capture_ring[slot].samples = OMAQ_AUDIO_FRAME_SAMPLES;
	memcpy(g_capture_ring[slot].pcm, pcm,
	       OMAQ_AUDIO_FRAME_SAMPLES * sizeof(int16_t));
	g_capture_count++;
	pthread_mutex_unlock(&g_audio_lock);
}

static void capture_accumulate(omaq_pulse_state *state, const int16_t *pcm,
			       size_t samples)
{
	while (samples > 0) {
		size_t available = OMAQ_AUDIO_FRAME_SAMPLES - state->capture_samples;
		size_t take = samples < available ? samples : available;

		if (pcm) {
			memcpy(state->capture + state->capture_samples, pcm,
			       take * sizeof(int16_t));
			pcm += take;
		} else {
			memset(state->capture + state->capture_samples, 0,
			       take * sizeof(int16_t));
		}
		state->capture_samples += take;
		samples -= take;
		if (state->capture_samples == OMAQ_AUDIO_FRAME_SAMPLES) {
			capture_push(state->capture);
			state->capture_samples = 0;
		}
	}
}

static void pulse_capture_read(pa_stream *stream, size_t bytes, void *userdata)
{
	omaq_pulse_state *state = userdata;
	const void *data = NULL;
	size_t length = 0;

	(void)bytes;
	if (pa_stream_peek(stream, &data, &length) != 0) {
		if (audio_running())
			note_audio_error(state->friend);
		return;
	}
	if (length == 0)
		return;
	if (length % sizeof(int16_t) != 0) {
		(void)pa_stream_drop(stream);
		if (audio_running())
			note_audio_error(state->friend);
		return;
	}
	capture_accumulate(state, data, length / sizeof(int16_t));
	if (pa_stream_drop(stream) != 0 && audio_running())
		note_audio_error(state->friend);
}

static int pulse_playback_write(pa_stream *stream)
{
	for (;;) {
		omaq_audio_frame frame;
		size_t writable;
		size_t bytes;

		pthread_mutex_lock(&g_audio_lock);
		if (g_playback_count == 0) {
			pthread_mutex_unlock(&g_audio_lock);
			return 0;
		}
		frame = g_playback_ring[g_playback_head];
		bytes = frame.samples * sizeof(int16_t);
		writable = pa_stream_writable_size(stream);
		if (writable == (size_t)-1) {
			pthread_mutex_unlock(&g_audio_lock);
			return -1;
		}
		if (writable < bytes) {
			pthread_mutex_unlock(&g_audio_lock);
			return 0;
		}
		g_playback_head = (g_playback_head + 1u) % OMAQ_AUDIO_RING_FRAMES;
		g_playback_count--;
		pthread_mutex_unlock(&g_audio_lock);
		if (pa_stream_write(stream, frame.pcm,
				    frame.samples * sizeof(int16_t), NULL, 0,
				    PA_SEEK_RELATIVE) != 0)
			return -1;
	}
}

static int pulse_iterate(pa_mainloop *mainloop)
{
	int retval = 0;

	return pa_mainloop_iterate(mainloop, 1, &retval) < 0 ? -1 : 0;
}

static int pulse_context_wait(pa_mainloop *mainloop, pa_context *context)
{
	while (audio_running()) {
		pa_context_state_t state = pa_context_get_state(context);

		if (state == PA_CONTEXT_READY)
			return 0;
		if (!PA_CONTEXT_IS_GOOD(state) || pulse_iterate(mainloop) != 0)
			return -1;
	}
	return -1;
}

static int pulse_streams_wait(pa_mainloop *mainloop, pa_stream *capture,
			      pa_stream *playback)
{
	while (audio_running()) {
		pa_stream_state_t capture_state = pa_stream_get_state(capture);
		pa_stream_state_t playback_state = pa_stream_get_state(playback);

		if (capture_state == PA_STREAM_READY && playback_state == PA_STREAM_READY)
			return 0;
		if (!PA_STREAM_IS_GOOD(capture_state) ||
		    !PA_STREAM_IS_GOOD(playback_state) || pulse_iterate(mainloop) != 0)
			return -1;
	}
	return -1;
}

static int pulse_backend_ready(pa_context *context, pa_stream *capture,
			       pa_stream *playback)
{
	return pa_context_get_state(context) == PA_CONTEXT_READY &&
		pa_stream_get_state(capture) == PA_STREAM_READY &&
		pa_stream_get_state(playback) == PA_STREAM_READY;
}

static void *audio_main(void *arg)
{
	omaq_pulse_state state = {
		.friend = (uint32_t)(uintptr_t)arg,
	};
	pa_sample_spec spec = {
		.format = PA_SAMPLE_S16LE,
		.rate = OMAQ_AUDIO_RATE,
		.channels = 1,
	};
	pa_buffer_attr capture_attr = {
		.maxlength = UINT32_MAX,
		.tlength = UINT32_MAX,
		.prebuf = UINT32_MAX,
		.minreq = UINT32_MAX,
		.fragsize = OMAQ_AUDIO_FRAME_SAMPLES * sizeof(int16_t),
	};
	pa_buffer_attr playback_attr = {
		.maxlength = UINT32_MAX,
		.tlength = OMAQ_AUDIO_FRAME_SAMPLES * sizeof(int16_t) * 4u,
		.prebuf = OMAQ_AUDIO_FRAME_SAMPLES * sizeof(int16_t) * 2u,
		.minreq = OMAQ_AUDIO_FRAME_SAMPLES * sizeof(int16_t),
		.fragsize = UINT32_MAX,
	};
	pa_mainloop *mainloop = NULL;
	pa_context *context = NULL;
	pa_stream *capture = NULL;
	pa_stream *playback = NULL;
	int failed = 0;

	mainloop = pa_mainloop_new();
	if (!mainloop)
		failed = 1;
	if (!failed)
		context = pa_context_new(pa_mainloop_get_api(mainloop), "OmaQ");
	if (!context || pa_context_connect(context, NULL, PA_CONTEXT_NOFLAGS, NULL) != 0)
		failed = 1;
	pthread_mutex_lock(&g_audio_lock);
	if (!failed && g_audio_running)
		g_pulse_mainloop = mainloop;
	pthread_mutex_unlock(&g_audio_lock);
	if (!failed && pulse_context_wait(mainloop, context) != 0)
		failed = 1;
	if (!failed) {
		capture = pa_stream_new(context, "Voice call", &spec, NULL);
		playback = pa_stream_new(context, "Voice call", &spec, NULL);
		if (!capture || !playback)
			failed = 1;
	}
	if (!failed) {
		pa_stream_set_read_callback(capture, pulse_capture_read, &state);
		if (pa_stream_connect_record(capture, NULL, &capture_attr,
					     PA_STREAM_ADJUST_LATENCY) != 0 ||
		    pa_stream_connect_playback(playback, NULL, &playback_attr,
					       PA_STREAM_ADJUST_LATENCY,
					       NULL, NULL) != 0 ||
		    pulse_streams_wait(mainloop, capture, playback) != 0)
			failed = 1;
	}
	while (!failed && audio_running()) {
		if (!pulse_backend_ready(context, capture, playback) ||
		    pulse_playback_write(playback) != 0 ||
		    pulse_iterate(mainloop) != 0)
			failed = 1;
	}
	pthread_mutex_lock(&g_audio_lock);
	if (g_pulse_mainloop == mainloop)
		g_pulse_mainloop = NULL;
	pthread_mutex_unlock(&g_audio_lock);
	if (capture) {
		pa_stream_set_read_callback(capture, NULL, NULL);
		(void)pa_stream_disconnect(capture);
		pa_stream_unref(capture);
	}
	if (playback) {
		(void)pa_stream_disconnect(playback);
		pa_stream_unref(playback);
	}
	if (context) {
		pa_context_disconnect(context);
		pa_context_unref(context);
	}
	if (mainloop)
		pa_mainloop_free(mainloop);
	if (failed && audio_running())
		note_audio_error(state.friend);
	return NULL;
}
#endif

static void audio_stop(void)
{
#ifdef HAVE_PULSE
	pa_mainloop *mainloop;
#endif

	pthread_mutex_lock(&g_audio_lock);
	g_audio_running = 0;
#ifdef HAVE_PULSE
	mainloop = g_pulse_mainloop;
	if (mainloop)
		pa_mainloop_wakeup(mainloop);
#endif
	pthread_mutex_unlock(&g_audio_lock);
	if (g_audio_started)
		(void)pthread_join(g_audio_thread, NULL);
	g_audio_started = 0;
	pthread_mutex_lock(&g_audio_lock);
	memset(g_capture_ring, 0, sizeof(g_capture_ring));
	memset(g_playback_ring, 0, sizeof(g_playback_ring));
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
	if (pthread_create(&g_audio_thread, NULL, audio_main,
			   (void *)(uintptr_t)friend) != 0) {
		pthread_mutex_lock(&g_audio_lock);
		g_audio_running = 0;
		pthread_mutex_unlock(&g_audio_lock);
		note_audio_error(friend);
		return -1;
	}
	g_audio_started = 1;
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

int omaq_av_local_stopped(void)
{
	int stopped;

	pthread_mutex_lock(&g_audio_lock);
	stopped = !g_audio_running && !g_audio_started &&
		g_capture_count == 0 && g_playback_count == 0;
	pthread_mutex_unlock(&g_audio_lock);
	return stopped && g_call == UINT32_MAX && !g_active && !g_incoming;
}

void omaq_av_receive(uint32_t friend, const int16_t *pcm, size_t samples,
		     uint8_t channels, uint32_t rate)
{
	unsigned int slot;
	omaq_audio_frame *frame;

	if (!pcm || samples == 0 || samples > OMAQ_AUDIO_MAX_SAMPLES ||
	    (channels != 1 && channels != 2) || rate != OMAQ_AUDIO_RATE ||
	    !g_active || friend != g_call)
		return;
	pthread_mutex_lock(&g_audio_lock);
	if (!g_audio_running) {
		pthread_mutex_unlock(&g_audio_lock);
		return;
	}
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
#ifdef HAVE_PULSE
	pulse_wakeup();
#endif
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

int omaq_av_friend_busy(uint32_t friend)
{
	return g_call == friend;
}

void omaq_av_forget_friend(uint32_t friend)
{
	/* Keep the short callback cooldown across number reuse. */
	pthread_mutex_lock(&g_audio_lock);
	if (g_audio_error_friend == friend) {
		g_audio_error = 0;
		g_audio_error_friend = UINT32_MAX;
	}
	pthread_mutex_unlock(&g_audio_lock);
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
