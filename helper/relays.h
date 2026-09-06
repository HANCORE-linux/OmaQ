#ifndef OMAQ_RELAYS_H
#define OMAQ_RELAYS_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_RELAY_HOST_MAX 256
#define OMAQ_RELAY_KEY_HEX 64
#define OMAQ_RELAY_MAX 16
#define OMAQ_RELAY_FILE_MAX 8192

typedef struct {
	char host[OMAQ_RELAY_HOST_MAX];
	uint16_t udp_port;
	uint16_t tcp_port;
	char key_hex[OMAQ_RELAY_KEY_HEX + 1];
} omaq_relay;

typedef struct {
	omaq_relay entries[OMAQ_RELAY_MAX];
	size_t count;
	int exclusive; /* 1 = ignore the built-in pinned relays */
} omaq_relay_set;

/* Parse a whole relays.conf body. Comments (#) and blank lines are ignored.
 * An optional "exclusive" line on its own means the built-in pinned relays
 * are not used. Every other line is "<host> <udp-port> <tcp-port> <64-hex
 * public key>". 0 = ok, -1 = invalid. */
int omaq_relays_parse(const char *text, omaq_relay_set *out);

/* Load "$home/relays.conf". 1 = configured, 0 = absent, -1 = invalid or
 * unsafe. Callers must fail closed on -1: quietly ignoring a relay file the
 * user wrote would send traffic through relays they chose to avoid. */
int omaq_relays_load(const char *home, omaq_relay_set *out);

#endif
