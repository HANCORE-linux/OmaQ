#ifndef OMAQ_INVITE_H
#define OMAQ_INVITE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define OMAQ_TOX_ADDR_LEN 76
#define OMAQ_INVITE_ID_MAX 64
#define OMAQ_URL_MAX 512

typedef enum { INVITE_DIRECT = 0, INVITE_GROUP = 1 } omaq_invite_kind;

typedef struct {
	char tox_addr[OMAQ_TOX_ADDR_LEN + 1];
	char id[OMAQ_INVITE_ID_MAX + 1];
	int64_t expiry;
	omaq_invite_kind kind;
	char group[65];
	char role[16];
} omaq_invite;

/* 0 = ok, -1 = invalid. Never half-accepts. */
int omaq_invite_parse(const char *url, omaq_invite *out);

/* Write URL. Returns 0 or -1. */
int omaq_invite_format(const omaq_invite *inv, char *buf, size_t buflen);

bool omaq_invite_expired(const omaq_invite *inv, int64_t now);

#endif
