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

int omaq_rate_allow_key_only(omaq_rate *r, const char *key, int64_t now)
{
	omaq_rate_key *slot;

	if (!r || !key || !key[0] || now < 0)
		return -1;
	slot = find_key(r, key);
	if (in_window(slot->min, OMAQ_RATE_PER_MIN, now, 60) >= OMAQ_RATE_PER_MIN)
		return -1;
	push_ts(slot->min, OMAQ_RATE_PER_MIN, now);
	return 0;
}

void omaq_control_rate_init(omaq_control_rate *r)
{
	if (r)
		memset(r, 0, sizeof(*r));
}

int omaq_control_rate_allow(omaq_control_rate *r, char kind, uint32_t group,
			    const char *actor, int64_t now)
{
	char key[OMAQ_CONTROL_RATE_KEY_LEN];
	int slot = -1;
	int free_slot = -1;
	int oldest = 0;

	if (!r || !actor ||
	    (kind != 'r' && kind != 'x' && kind != 'e' && kind != 'd' && kind != 't') ||
	    now < 0 ||
	    snprintf(key, sizeof(key), "%c%u:%.64s", kind, group, actor) >=
	    (int)sizeof(key))
		return -1;
	for (int i = 0; i < OMAQ_CONTROL_RATE_SLOTS; i++) {
		if (r->entries[i].key[0] == '\0') {
			if (free_slot < 0)
				free_slot = i;
			continue;
		}
		if (strcmp(r->entries[i].key, key) == 0) {
			slot = i;
			break;
		}
		if (r->entries[i].window < r->entries[oldest].window)
			oldest = i;
	}
	if (slot < 0) {
		slot = free_slot >= 0 ? free_slot : oldest;
		memset(&r->entries[slot], 0, sizeof(r->entries[slot]));
		snprintf(r->entries[slot].key, sizeof(r->entries[slot].key), "%s", key);
		r->entries[slot].window = now;
	} else if (now < r->entries[slot].window ||
		   now - r->entries[slot].window >= 60) {
		r->entries[slot].window = now;
		r->entries[slot].count = 0;
	}
	if (r->entries[slot].count >=
	    (kind == 'r' ? OMAQ_CONTROL_RATE_RECEIPT_PER_KEY :
	     OMAQ_CONTROL_RATE_PER_KEY))
		return -1;
	if (now < r->global_window || now - r->global_window >= 60) {
		r->global_window = now;
		r->global_count = 0;
	}
	if (r->global_count >= OMAQ_CONTROL_RATE_GLOBAL)
		return -1;
	r->entries[slot].count++;
	r->global_count++;
	return 0;
}

int omaq_rate_allow(omaq_rate *r, const char *key, int64_t now)
{
	if (!r || !key || !key[0] || now < 0)
		return -1;
	if (in_window(r->hour, OMAQ_RATE_PER_HOUR, now, 3600) >= OMAQ_RATE_PER_HOUR)
		return -1;
	if (omaq_rate_allow_key_only(r, key, now) != 0)
		return -1;
	push_ts(r->hour, OMAQ_RATE_PER_HOUR, now);
	return 0;
}
