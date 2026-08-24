#include "json_io.h"

#include <ctype.h>
#include <string.h>

static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	return p;
}

static int parse_string(const char **pp, char *dst, size_t dstlen)
{
	const char *p = *pp;
	size_t n = 0;
	if (*p != '"')
		return -1;
	p++;
	while (*p && *p != '"') {
		char c;
		if (*p == '\\') {
			p++;
			if (*p == 'n')
				c = '\n';
			else if (*p == 't')
				c = '\t';
			else if (*p == '"')
				c = '"';
			else if (*p == '\\')
				c = '\\';
			else if (*p == 'u')
				return -1; /* no unicode escapes */
			else
				return -1;
			p++;
		} else {
			if ((unsigned char)*p < 0x20)
				return -1;
			c = *p++;
		}
		if (n + 1 >= dstlen)
			return -1;
		dst[n++] = c;
	}
	if (*p != '"')
		return -1;
	dst[n] = '\0';
	*pp = p + 1;
	return 0;
}

static int parse_int(const char **pp, int *out)
{
	const char *p = *pp;
	int sign = 1;
	long v = 0;
	int any = 0;
	if (*p == '-') {
		sign = -1;
		p++;
	}
	while (*p >= '0' && *p <= '9') {
		int d = *p - '0';
		if (v > (2147483647L - d) / 10)
			return -1;
		v = v * 10 + d;
		any = 1;
		p++;
	}
	if (!any)
		return -1;
	if (sign < 0)
		v = -v;
	*out = (int)v;
	*pp = p;
	return 0;
}

int omaq_json_escape(const char *in, char *out, size_t outn)
{
	size_t o = 0;
	if (!in || !out || outn == 0)
		return -1;
	for (; *in; in++) {
		const char *rep = NULL;
		char buf[2];
		size_t rl;
		if (*in == '"')
			rep = "\\\"";
		else if (*in == '\\')
			rep = "\\\\";
		else if (*in == '\n')
			rep = "\\n";
		else if (*in == '\r')
			rep = "\\r";
		else if (*in == '\t')
			rep = "\\t";
		else if ((unsigned char)*in < 0x20)
			return -1;
		else {
			buf[0] = *in;
			buf[1] = '\0';
			rep = buf;
		}
		rl = strlen(rep);
		if (o + rl + 1 > outn)
			return -1;
		memcpy(out + o, rep, rl);
		o += rl;
	}
	out[o] = '\0';
	return 0;
}

static int parse_bool(const char **pp, int *out)
{
	if (strncmp(*pp, "true", 4) == 0) {
		*out = 1;
		*pp += 4;
		return 0;
	}
	if (strncmp(*pp, "false", 5) == 0) {
		*out = 0;
		*pp += 5;
		return 0;
	}
	return -1;
}

int omaq_json_parse_op(const char *line, omaq_op *out)
{
	const char *p;
	int first = 1;

	if (!line || !out)
		return -1;
	if (strlen(line) >= OMAQ_JSON_LINE_MAX)
		return -1;
	memset(out, 0, sizeof(*out));
	p = skip_ws(line);
	if (*p != '{')
		return -1;
	p++;
	p = skip_ws(p);
	if (*p == '}')
		return -1; /* need at least op */

	while (*p && *p != '}') {
		char key[32];

		if (!first) {
			if (*p != ',')
				return -1;
			p++;
			p = skip_ws(p);
		}
		first = 0;
		if (parse_string(&p, key, sizeof(key)) != 0)
			return -1;
		p = skip_ws(p);
		if (*p != ':')
			return -1;
		p++;
		p = skip_ws(p);

		if (strcmp(key, "op") == 0) {
			if (parse_string(&p, out->op, sizeof(out->op)) != 0)
				return -1;
		} else if (strcmp(key, "kind") == 0) {
			if (parse_string(&p, out->kind, sizeof(out->kind)) != 0)
				return -1;
		} else if (strcmp(key, "payload") == 0) {
			if (parse_string(&p, out->payload, sizeof(out->payload)) != 0)
				return -1;
		} else if (strcmp(key, "id") == 0) {
			if (parse_string(&p, out->id, sizeof(out->id)) != 0)
				return -1;
		} else if (strcmp(key, "conversation") == 0) {
			if (parse_string(&p, out->conversation, sizeof(out->conversation)) != 0)
				return -1;
		} else if (strcmp(key, "text") == 0) {
			if (parse_string(&p, out->text, sizeof(out->text)) != 0)
				return -1;
			out->has_text = 1;
		} else if (strcmp(key, "reply") == 0) {
			if (parse_string(&p, out->reply, sizeof(out->reply)) != 0)
				return -1;
		} else if (strcmp(key, "group") == 0) {
			if (parse_string(&p, out->group, sizeof(out->group)) != 0)
				return -1;
		} else if (strcmp(key, "member") == 0) {
			if (parse_string(&p, out->member, sizeof(out->member)) != 0)
				return -1;
		} else if (strcmp(key, "role") == 0) {
			if (parse_string(&p, out->role, sizeof(out->role)) != 0)
				return -1;
		} else if (strcmp(key, "state") == 0) {
			if (parse_string(&p, out->state, sizeof(out->state)) != 0)
				return -1;
		} else if (strcmp(key, "path") == 0) {
			if (parse_string(&p, out->path, sizeof(out->path)) != 0)
				return -1;
		} else if (strcmp(key, "title") == 0) {
			if (parse_string(&p, out->title, sizeof(out->title)) != 0)
				return -1;
		} else if (strcmp(key, "nickname") == 0) {
			if (parse_string(&p, out->nickname, sizeof(out->nickname)) != 0)
				return -1;
		} else if (strcmp(key, "monitor") == 0) {
			if (parse_string(&p, out->monitor, sizeof(out->monitor)) != 0)
				return -1;
		} else if (strcmp(key, "passphrase") == 0) {
			if (parse_string(&p, out->passphrase, sizeof(out->passphrase)) != 0)
				return -1;
		} else if (strcmp(key, "ttlSec") == 0) {
			if (parse_int(&p, &out->ttl_sec) != 0)
				return -1;
			out->has_ttl = 1;
		} else if (strcmp(key, "limit") == 0) {
			if (parse_int(&p, &out->limit) != 0)
				return -1;
			out->has_limit = 1;
		} else if (strcmp(key, "x") == 0) {
			if (parse_int(&p, &out->x) != 0)
				return -1;
		} else if (strcmp(key, "y") == 0) {
			if (parse_int(&p, &out->y) != 0)
				return -1;
		} else if (strcmp(key, "accept") == 0) {
			if (parse_bool(&p, &out->accept) != 0)
				return -1;
			out->has_accept = 1;
		} else if (strcmp(key, "replace") == 0) {
			if (parse_bool(&p, &out->replace) != 0)
				return -1;
			out->has_replace = 1;
		} else if (strcmp(key, "pinned") == 0) {
			if (parse_bool(&p, &out->pinned) != 0)
				return -1;
			out->has_pinned = 1;
		} else if (strcmp(key, "typing") == 0) {
			if (parse_bool(&p, &out->typing) != 0)
				return -1;
			out->has_typing = 1;
		} else {
			return -1; /* unknown key */
		}
		p = skip_ws(p);
	}
	if (*p != '}')
		return -1;
	p++;
	p = skip_ws(p);
	if (*p != '\0')
		return -1;
	if (out->op[0] == '\0')
		return -1;
	return 0;
}
