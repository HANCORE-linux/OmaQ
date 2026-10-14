#ifndef OMAQ_SEND_QUEUE_H
#define OMAQ_SEND_QUEUE_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_SEND_QUEUE_MAX 64
#define OMAQ_SEND_QUEUE_PEER_MAX 16
#define OMAQ_SEND_QUEUE_WAIT_MS 300000
#define OMAQ_SEND_QUEUE_CONVERSATION_MAX 16
#define OMAQ_SEND_QUEUE_ID_MAX 80
#define OMAQ_SEND_QUEUE_TEXT_MAX 512

typedef struct {
	int used;
	int owner;
	uint32_t friend_number;
	uint64_t seq;
	int64_t deadline_ms;
	char conversation[OMAQ_SEND_QUEUE_CONVERSATION_MAX];
	char self_key[65];
	char peer_key[65];
	char request[OMAQ_SEND_QUEUE_ID_MAX];
	char reply[OMAQ_SEND_QUEUE_ID_MAX];
	char text[OMAQ_SEND_QUEUE_TEXT_MAX];
} omaq_send_queue_entry;

typedef struct {
	omaq_send_queue_entry entries[OMAQ_SEND_QUEUE_MAX];
	size_t count;
	uint64_t next_seq;
} omaq_send_queue;

void omaq_send_queue_init(omaq_send_queue *queue);
int omaq_send_queue_add(omaq_send_queue *queue, int owner, uint32_t friend_number,
			const char *conversation, const char *self_key,
			const char *peer_key, const char *request, const char *reply,
			const char *text, int64_t now_ms);
size_t omaq_send_queue_peer_count(const omaq_send_queue *queue, uint32_t friend_number);
int omaq_send_queue_oldest(const omaq_send_queue *queue, const uint32_t *skip,
			   size_t skip_count);
int omaq_send_queue_owned(const omaq_send_queue *queue, int owner);
void omaq_send_queue_remove(omaq_send_queue *queue, int index);

#endif
