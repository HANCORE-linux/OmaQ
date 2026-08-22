#ifndef OMAQ_JSON_IO_H
#define OMAQ_JSON_IO_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_JSON_LINE_MAX 4096
#define OMAQ_JSON_STR_MAX 512

typedef struct {
	char op[32];
	char kind[16];
	char payload[OMAQ_JSON_STR_MAX];
	char id[80];
	char conversation[80];
	char text[OMAQ_JSON_STR_MAX];
	char reply[80];
	char group[80];
	char member[80];
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
	int typing;
	int x, y;
	int has_ttl;
	int has_limit;
	int has_accept;
	int has_replace;
	int has_pinned;
	int has_typing;
} omaq_op;

int omaq_json_parse_op(const char *line, omaq_op *out);
int omaq_json_escape(const char *in, char *out, size_t outn);

#endif
