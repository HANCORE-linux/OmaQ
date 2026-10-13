#define _DEFAULT_SOURCE
#include "send_queue.h"

#include <string.h>

static int text_fits(const char *text, size_t size, int required)
{
	size_t length;

	if (!text)
		return 0;
	length = strlen(text);
	return (!required || length > 0) && length < size;
}

static int key_ok(const char *key)
{
	if (!key || strlen(key) != 64)
		return 0;
	for (size_t i = 0; i < 64; i++)
		if (!((key[i] >= '0' && key[i] <= '9') || (key[i] >= 'a' && key[i] <= 'f')))
			return 0;
	return 1;
}

void omaq_send_queue_init(omaq_send_queue *queue)
{
	if (queue)
		explicit_bzero(queue, sizeof(*queue));
}

size_t omaq_send_queue_peer_count(const omaq_send_queue *queue, uint32_t friend_number)
{
	size_t count = 0;

	if (!queue)
		return 0;
	for (size_t i = 0; i < OMAQ_SEND_QUEUE_MAX; i++)
		if (queue->entries[i].used && queue->entries[i].friend_number == friend_number)
			count++;
	return count;
}

int omaq_send_queue_add(omaq_send_queue *queue, int owner, uint32_t friend_number,
			const char *conversation, const char *self_key,
			const char *peer_key, const char *request, const char *reply,
			const char *text, int64_t now_ms)
{
	omaq_send_queue_entry *entry = NULL;

	if (!reply)
		reply = "";
	if (!queue || now_ms < 0 || now_ms > INT64_MAX - OMAQ_SEND_QUEUE_WAIT_MS ||
	    !key_ok(self_key) || !key_ok(peer_key) ||
	    !text_fits(conversation, OMAQ_SEND_QUEUE_CONVERSATION_MAX, 1) ||
	    !text_fits(request, OMAQ_SEND_QUEUE_ID_MAX, 1) ||
	    !text_fits(reply, OMAQ_SEND_QUEUE_ID_MAX, 0) ||
	    !text_fits(text, OMAQ_SEND_QUEUE_TEXT_MAX, 1))
		return -1;
	if (queue->count >= OMAQ_SEND_QUEUE_MAX ||
	    omaq_send_queue_peer_count(queue, friend_number) >= OMAQ_SEND_QUEUE_PEER_MAX)
		return -2;
	for (size_t i = 0; i < OMAQ_SEND_QUEUE_MAX; i++)
		if (!queue->entries[i].used) {
			entry = &queue->entries[i];
			break;
		}
	if (!entry)
		return -2;
	entry->used = 1;
	entry->owner = owner;
	entry->friend_number = friend_number;
	entry->seq = ++queue->next_seq;
	entry->deadline_ms = now_ms + OMAQ_SEND_QUEUE_WAIT_MS;
	memcpy(entry->conversation, conversation, strlen(conversation) + 1);
	memcpy(entry->self_key, self_key, 65);
	memcpy(entry->peer_key, peer_key, 65);
	memcpy(entry->request, request, strlen(request) + 1);
	memcpy(entry->reply, reply, strlen(reply) + 1);
	memcpy(entry->text, text, strlen(text) + 1);
	queue->count++;
	return 0;
}

int omaq_send_queue_oldest(const omaq_send_queue *queue, const uint32_t *skip,
			   size_t skip_count)
{
	int oldest = -1;

	if (!queue)
		return -1;
	for (int i = 0; i < OMAQ_SEND_QUEUE_MAX; i++) {
		const omaq_send_queue_entry *entry = &queue->entries[i];
		int skipped = 0;

		if (!entry->used)
			continue;
		for (size_t j = 0; skip && j < skip_count; j++)
			if (skip[j] == entry->friend_number) {
				skipped = 1;
				break;
			}
		if (!skipped && (oldest < 0 || entry->seq < queue->entries[oldest].seq))
			oldest = i;
	}
	return oldest;
}

int omaq_send_queue_owned(const omaq_send_queue *queue, int owner)
{
	if (!queue)
		return -1;
	for (int i = 0; i < OMAQ_SEND_QUEUE_MAX; i++)
		if (queue->entries[i].used && queue->entries[i].owner == owner)
			return i;
	return -1;
}

void omaq_send_queue_remove(omaq_send_queue *queue, int index)
{
	if (!queue || index < 0 || index >= OMAQ_SEND_QUEUE_MAX ||
	    !queue->entries[index].used)
		return;
	explicit_bzero(&queue->entries[index], sizeof(queue->entries[index]));
	queue->count--;
}
