#include "rate.h"

#include <stdio.h>
#include <string.h>

void omaq_rate_init(omaq_rate *r)
{
	if (r)
		memset(r, 0, sizeof(*r));
}

static int in_window(const int64_t *ts, int n, int64_t now, int64_t win)
{
	int c = 0;
	int i;

	for (i = 0; i < n; i++) {
		if (ts[i] > 0 && now - ts[i] < win)
			c++;
	}
	return c;
}

static void push_ts(int64_t *ts, int n, int64_t now)
{
	int oldest = 0;
	int i;

	for (i = 0; i < n; i++) {
		if (ts[i] == 0) {
			ts[i] = now;
			return;
		}
		if (ts[i] < ts[oldest])
			oldest = i;
	}
	ts[oldest] = now;
}

static omaq_rate_key *find_key(omaq_rate *r, const char *key)
{
	int free_i = -1;
	int oldest = 0;
	int i;

	for (i = 0; i < OMAQ_RATE_KEY_SLOTS; i++) {
		if (r->keys[i].key[0] == '\0') {
			if (free_i < 0)
				free_i = i;
			continue;
		}
		if (strncmp(r->keys[i].key, key, OMAQ_RATE_KEY_LEN - 1) == 0)
			return &r->keys[i];
		if (r->keys[i].min[0] < r->keys[oldest].min[0])
			oldest = i;
	}
	if (free_i < 0)
		free_i = oldest;
	memset(&r->keys[free_i], 0, sizeof(r->keys[free_i]));
	snprintf(r->keys[free_i].key, sizeof(r->keys[free_i].key), "%s", key);
	return &r->keys[free_i];
}

int omaq_rate_allow(omaq_rate *r, const char *key, int64_t now)
{
	omaq_rate_key *slot;

	if (!r || !key || !key[0] || now < 0)
		return -1;
	if (in_window(r->hour, OMAQ_RATE_PER_HOUR, now, 3600) >= OMAQ_RATE_PER_HOUR)
		return -1;
	slot = find_key(r, key);
	if (in_window(slot->min, OMAQ_RATE_PER_MIN, now, 60) >= OMAQ_RATE_PER_MIN)
		return -1;
	push_ts(slot->min, OMAQ_RATE_PER_MIN, now);
	push_ts(r->hour, OMAQ_RATE_PER_HOUR, now);
	return 0;
}
