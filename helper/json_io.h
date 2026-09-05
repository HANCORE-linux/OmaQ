#ifndef OMAQ_JSON_IO_H
#define OMAQ_JSON_IO_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_JSON_LINE_MAX 4096
#define OMAQ_JSON_STR_MAX 512
#define OMAQ_JSON_FIELD_OP (UINT64_C(1) << 0)
#define OMAQ_JSON_FIELD_ID (UINT64_C(1) << 3)
#define OMAQ_JSON_FIELD_CONVERSATION (UINT64_C(1) << 4)
#define OMAQ_JSON_FIELD_KEY (UINT64_C(1) << 9)
#define OMAQ_JSON_FIELD_REQUEST (UINT64_C(1) << 10)
#define OMAQ_JSON_FIELD_CALL_ID (UINT64_C(1) << 29)

typedef struct {
	uint64_t field_mask;
	char op[32];
	char kind[16];
	char payload[OMAQ_JSON_STR_MAX];
	char id[80];
	char conversation[80];
	char text[OMAQ_JSON_STR_MAX];
	char reply[80];
	char group[80];
	char member[80];
	char key[80];
	char request[80];
	char call_id[80];
	char role[16];
	char state[16];
	char path[OMAQ_JSON_STR_MAX];
	char title[80];
	char nickname[129];
	char monitor[64];
	char passphrase[129];
	int ttl_sec;
	int limit;
	int accept;
	int replace;
	int pinned;
	int enabled;
	int typing;
	int x, y, width, height;
	int has_text;
	int has_ttl;
	int has_limit;
	int has_accept;
	int has_replace;
	int has_pinned;
	int has_enabled;
	int has_typing;
	int has_width;
	int has_height;
} omaq_op;

int omaq_json_parse_op(const char *line, omaq_op *out);
int omaq_json_escape(const char *in, char *out, size_t outn);
int omaq_json_validate(const char *json);

#endif
