#include "conversation.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OMAQ_UNREAD_COUNT_MAX 999999u

void omaq_conv_init(omaq_conv *c, const char *id, omaq_conv_kind kind)
{
	memset(c, 0, sizeof(*c));
	if (id) {
		strncpy(c->id, id, sizeof(c->id) - 1);
	}
	c->kind = kind;
	c->unread = 0;
}

void omaq_conv_note(omaq_conv *c, const char *last, int incoming)
{
	if (!c)
		return;
	if (last) {
		strncpy(c->last, last, sizeof(c->last) - 1);
		c->last[sizeof(c->last) - 1] = '\0';
	}
	if (incoming)
		c->unread++;
}

static int unread_id_ok(const char *id)
{
	size_t i, digits;
	uint32_t value = 0;

	if (!id || !id[0] || strlen(id) >= OMAQ_CONV_ID_MAX)
		return 0;
	if (id[0] == 'g') {
		if (strlen(id) != 66 || id[1] != ':')
			return 0;
		for (i = 2; i < 66; i++)
			if (!((id[i] >= '0' && id[i] <= '9') ||
			      (id[i] >= 'a' && id[i] <= 'f')))
				return 0;
		return 1;
	}
	digits = strlen(id);
	if (digits > 1 && id[0] == '0')
		return 0;
	for (i = 0; id[i]; i++) {
		uint32_t digit;
		if (id[i] < '0' || id[i] > '9')
			return 0;
		digit = (uint32_t)(id[i] - '0');
		if (value > (UINT32_MAX - digit) / 10u)
			return 0;
		value = value * 10u + digit;
	}
	return 1;
}

static int unread_find(const omaq_unread_state *state, const char *conversation)
{
	size_t i;

	if (!state || !unread_id_ok(conversation))
		return -1;
	for (i = 0; i < state->length; i++) {
		if (strcmp(state->entries[i].id, conversation) == 0)
			return (int)i;
	}
	return -1;
}

void omaq_unread_init(omaq_unread_state *state)
{
	if (state)
		memset(state, 0, sizeof(*state));
}

void omaq_unread_destroy(omaq_unread_state *state)
{
	if (!state)
		return;
	free(state->entries);
	memset(state, 0, sizeof(*state));
}

static int unread_reserve(omaq_unread_state *state, size_t needed)
{
	omaq_unread_entry *entries;
	size_t capacity;

	if (!state)
		return -1;
	if (needed <= state->capacity)
		return 0;
	capacity = state->capacity ? state->capacity : 16;
	while (capacity < needed) {
		if (capacity > SIZE_MAX / 2)
			return -1;
		capacity *= 2;
	}
	if (capacity > SIZE_MAX / sizeof(*entries))
		return -1;
	entries = realloc(state->entries, capacity * sizeof(*entries));
	if (!entries)
		return -1;
	memset(entries + state->capacity, 0,
		(capacity - state->capacity) * sizeof(*entries));
	state->entries = entries;
	state->capacity = capacity;
	return 0;
}

int omaq_unread_clone(omaq_unread_state *destination, const omaq_unread_state *source)
{
	if (!destination || !source)
		return -1;
	omaq_unread_init(destination);
	if (unread_reserve(destination, source->length) != 0)
		return -1;
	if (source->length)
		memcpy(destination->entries, source->entries,
		       source->length * sizeof(source->entries[0]));
	destination->length = source->length;
	return 0;
}

int omaq_unread_set(omaq_unread_state *state, const char *conversation, unsigned count)
{
	if (!state || !unread_id_ok(conversation) || count == 0 ||
	    count > OMAQ_UNREAD_COUNT_MAX || unread_find(state, conversation) >= 0 ||
	    unread_reserve(state, state->length + 1) != 0)
		return -1;
	snprintf(state->entries[state->length].id,
		 sizeof(state->entries[state->length].id), "%s", conversation);
	state->entries[state->length].count = count;
	state->length++;
	return 0;
}

int omaq_unread_increment(omaq_unread_state *state, const char *conversation)
{
	int index;

	if (!state || !unread_id_ok(conversation))
		return -1;
	index = unread_find(state, conversation);
	if (index >= 0) {
		if (state->entries[index].count < OMAQ_UNREAD_COUNT_MAX)
			state->entries[index].count++;
		return 0;
	}
	if (unread_reserve(state, state->length + 1) != 0)
		return -1;
	snprintf(state->entries[state->length].id,
		 sizeof(state->entries[state->length].id), "%s", conversation);
	state->entries[state->length].count = 1;
	state->length++;
	return 0;
}

int omaq_unread_clear(omaq_unread_state *state, const char *conversation)
{
	int index;

	if (!state || !unread_id_ok(conversation))
		return -1;
	index = unread_find(state, conversation);
	if (index < 0)
		return 0;
	if ((size_t)index + 1 < state->length) {
		memmove(&state->entries[index], &state->entries[index + 1],
			(state->length - (size_t)index - 1) * sizeof(state->entries[0]));
	}
	state->length--;
	memset(&state->entries[state->length], 0, sizeof(state->entries[0]));
	return 0;
}

int omaq_unread_prune(omaq_unread_state *state, omaq_unread_available_fn available,
		      void *userdata)
{
	omaq_unread_state next;
	int removed = 0;

	if (!state || !available)
		return -1;
	omaq_unread_init(&next);
	for (size_t i = 0; i < state->length; i++) {
		int keep = available(state->entries[i].id, userdata);

		if (keep < 0 || (keep > 0 &&
		    omaq_unread_set(&next, state->entries[i].id,
				    state->entries[i].count) != 0)) {
			omaq_unread_destroy(&next);
			return -1;
		}
		if (keep == 0)
			removed++;
	}
	omaq_unread_destroy(state);
	*state = next;
	return removed;
}

unsigned omaq_unread_count(const omaq_unread_state *state, const char *conversation)
{
	int index = unread_find(state, conversation);

	return index >= 0 ? state->entries[index].count : 0;
}

unsigned omaq_unread_total(const omaq_unread_state *state)
{
	size_t i;
	unsigned total = 0;

	if (!state)
		return 0;
	for (i = 0; i < state->length; i++) {
		if (UINT_MAX - total < state->entries[i].count)
			return UINT_MAX;
		total += state->entries[i].count;
	}
	return total;
}
