#ifndef OMAQ_RATE_H
#define OMAQ_RATE_H

#include <stdint.h>

#define OMAQ_RATE_PER_MIN 5
#define OMAQ_RATE_PER_HOUR 20
#define OMAQ_RATE_KEY_SLOTS 32
#define OMAQ_RATE_KEY_LEN 65

typedef struct {
	char key[OMAQ_RATE_KEY_LEN];
	int64_t min[OMAQ_RATE_PER_MIN];
} omaq_rate_key;

typedef struct {
	omaq_rate_key keys[OMAQ_RATE_KEY_SLOTS];
	int64_t hour[OMAQ_RATE_PER_HOUR];
} omaq_rate;

#define OMAQ_CONTROL_RATE_SLOTS 320
#define OMAQ_CONTROL_RATE_PER_KEY 30
#define OMAQ_CONTROL_RATE_RECEIPT_PER_KEY 60
#define OMAQ_CONTROL_RATE_GLOBAL 120
#define OMAQ_CONTROL_RATE_KEY_LEN 80

typedef struct {
	struct {
		char key[OMAQ_CONTROL_RATE_KEY_LEN];
		int64_t window;
		unsigned int count;
	} entries[OMAQ_CONTROL_RATE_SLOTS];
	int64_t global_window;
	unsigned int global_count;
} omaq_control_rate;

#define OMAQ_GROUP_FILE_OFFER_RATE_SLOTS 64
#define OMAQ_GROUP_FILE_OFFER_RATE_PER_KEY 5
#define OMAQ_GROUP_FILE_OFFER_RATE_PER_ACTOR 10
#define OMAQ_GROUP_FILE_OFFER_RATE_GLOBAL 30
#define OMAQ_GROUP_FILE_OFFER_RATE_KEY_LEN 136

typedef struct {
	struct {
		char key[OMAQ_GROUP_FILE_OFFER_RATE_KEY_LEN];
		int64_t window;
		unsigned int count;
	} entries[OMAQ_GROUP_FILE_OFFER_RATE_SLOTS];
	struct {
		char key[65];
		int64_t window;
		unsigned int count;
	} actors[OMAQ_GROUP_FILE_OFFER_RATE_SLOTS];
	int64_t global_window;
	unsigned int global_count;
} omaq_group_file_offer_rate;

#define OMAQ_MESSAGE_RATE_SLOTS 320
#define OMAQ_MESSAGE_RATE_KEY_LEN 136
#define OMAQ_MESSAGE_RATE_BURST_SECONDS 10
#define OMAQ_MESSAGE_RATE_BURST_PER_KEY 10
#define OMAQ_MESSAGE_RATE_PER_MINUTE 30
#define OMAQ_MESSAGE_RATE_GLOBAL_BURST 40
#define OMAQ_MESSAGE_RATE_GLOBAL_MINUTE 120

typedef struct {
	struct {
		char key[OMAQ_MESSAGE_RATE_KEY_LEN];
		int64_t burst_window;
		int64_t minute_window;
		unsigned int burst_count;
		unsigned int minute_count;
	} entries[OMAQ_MESSAGE_RATE_SLOTS];
	int64_t global_burst_window;
	int64_t global_minute_window;
	unsigned int global_burst_count;
	unsigned int global_minute_count;
} omaq_message_rate;

void omaq_rate_init(omaq_rate *r);
/* 0 = allow and record, -1 = deny. key is an opaque id (e.g. peer pk hex). */
int omaq_rate_allow(omaq_rate *r, const char *key, int64_t now);
/* Per-key window only, for protocol-specific independent limiters. */
int omaq_rate_allow_key_only(omaq_rate *r, const char *key, int64_t now);
void omaq_control_rate_init(omaq_control_rate *r);
int omaq_control_rate_allow(omaq_control_rate *r, char kind, uint32_t group,
			    const char *actor, int64_t now);
void omaq_group_file_offer_rate_init(omaq_group_file_offer_rate *r);
/* Per stable group/member key plus a separate process-wide offer budget. */
int omaq_group_file_offer_rate_allow(omaq_group_file_offer_rate *r,
				     const char *group, const char *actor,
				     int64_t now);
void omaq_message_rate_init(omaq_message_rate *r);
/* Apply before duplicate lookup, unread persistence, history, and event fan-out. */
int omaq_message_rate_allow(omaq_message_rate *r, const char *conversation,
			    const char *actor, int64_t now);

#endif
