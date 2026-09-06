#ifndef OMAQ_PROXY_H
#define OMAQ_PROXY_H

#include <stddef.h>

#define OMAQ_PROXY_HOST_MAX 256
#define OMAQ_PROXY_FILE_MAX 4096

enum omaq_proxy_type {
	OMAQ_PROXY_NONE = 0,
	OMAQ_PROXY_SOCKS5 = 1,
	OMAQ_PROXY_HTTP = 2
};

typedef struct {
	int type;
	char host[OMAQ_PROXY_HOST_MAX];
	unsigned port;
} omaq_proxy;

/* Parse a whole proxy.conf body: comments (#), blank lines, and exactly one
 * "<socks5|http|none> <host> <port>" directive ("none" takes no arguments).
 * 0 = ok, -1 = invalid. */
int omaq_proxy_parse(const char *text, omaq_proxy *out);

/* Load "$home/proxy.conf". 1 = configured, 0 = absent, -1 = invalid or
 * unsafe. Callers must fail closed on -1: silently connecting without the
 * requested proxy would expose the address the user asked to hide. */
int omaq_proxy_load(const char *home, omaq_proxy *out);

#endif
