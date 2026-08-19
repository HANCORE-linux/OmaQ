#include "invite.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int is_hex(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int is_id_char(char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '_' || c == '-';
}

static void lower_copy(char *dst, const char *src, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++)
		dst[i] = (char)tolower((unsigned char)src[i]);
	dst[n] = '\0';
}

static int parse_i64(const char *s, int64_t *out)
{
	int64_t v = 0;
	int any = 0;
	int digits = 0;
	if (*s == '-')
		return -1;
	while (*s) {
		int d;
		if (*s < '0' || *s > '9')
			return -1;
		d = *s - '0';
		if (v > (INT64_MAX - d) / 10)
			return -1;
		v = v * 10 + d;
		any = 1;
		digits++;
		if (digits > 19)
			return -1;
		s++;
	}
	if (!any)
		return -1;
	*out = v;
	return 0;
}

int omaq_invite_parse(const char *url, omaq_invite *out)
{
	const char *p;
	const char *q;
	int seen_i = 0, seen_e = 0, seen_k = 0, seen_g = 0, seen_r = 0, seen_rk = 0;

	if (!url || !out)
		return -1;
	memset(out, 0, sizeof(*out));

	if (strncmp(url, "omaq://invite/", 14) != 0)
		return -1;
	p = url + 14;
	if (strlen(p) < OMAQ_TOX_ADDR_LEN)
		return -1;
	for (int i = 0; i < OMAQ_TOX_ADDR_LEN; i++) {
		if (!is_hex(p[i]))
			return -1;
	}
	if (p[OMAQ_TOX_ADDR_LEN] != '?')
		return -1;
	lower_copy(out->tox_addr, p, OMAQ_TOX_ADDR_LEN);
	p += OMAQ_TOX_ADDR_LEN + 1;

	while (*p) {
		const char *eq;
		const char *amp;
		size_t klen, vlen;
		char key[8];
		char val[80];

		eq = strchr(p, '=');
		if (!eq)
			return -1;
		klen = (size_t)(eq - p);
		if (klen == 0 || klen > 7)
			return -1;
		memcpy(key, p, klen);
		key[klen] = '\0';
		q = eq + 1;
		amp = strchr(q, '&');
		if (amp)
			vlen = (size_t)(amp - q);
		else
			vlen = strlen(q);
		if (vlen >= sizeof(val))
			return -1;
		memcpy(val, q, vlen);
		val[vlen] = '\0';

		if (strcmp(key, "i") == 0) {
			if (seen_i)
				return -1;
			seen_i = 1;
			if (vlen < 1 || vlen > OMAQ_INVITE_ID_MAX)
				return -1;
			for (size_t i = 0; i < vlen; i++) {
				if (!is_id_char(val[i]))
					return -1;
			}
			memcpy(out->id, val, vlen + 1);
		} else if (strcmp(key, "e") == 0) {
			if (seen_e)
				return -1;
			seen_e = 1;
			if (parse_i64(val, &out->expiry) != 0)
				return -1;
		} else if (strcmp(key, "k") == 0) {
			if (seen_k)
				return -1;
			seen_k = 1;
			if (strcmp(val, "direct") == 0)
				out->kind = INVITE_DIRECT;
			else if (strcmp(val, "group") == 0)
				out->kind = INVITE_GROUP;
			else
				return -1;
		} else if (strcmp(key, "g") == 0) {
			if (seen_g)
				return -1;
			seen_g = 1;
			if (vlen < 1 || vlen > 64)
				return -1;
			memcpy(out->group, val, vlen + 1);
		} else if (strcmp(key, "r") == 0) {
			if (seen_r)
				return -1;
			seen_r = 1;
			if (strcmp(val, "member") != 0 && strcmp(val, "admin") != 0)
				return -1;
			memcpy(out->role, val, vlen + 1);
		} else if (strcmp(key, "rk") == 0) {
			size_t i;
			if (seen_rk)
				return -1;
			seen_rk = 1;
			if (vlen != 64)
				return -1;
			for (i = 0; i < 64; i++) {
				if (!is_hex(val[i]))
					return -1;
			}
			lower_copy(out->rk, val, 64);
		} else {
			return -1;
		}

		if (!amp)
			break;
		p = amp + 1;
		if (*p == '\0')
			return -1;
	}

	if (!seen_i || !seen_e || !seen_k)
		return -1;
	if (out->kind == INVITE_DIRECT) {
		if (seen_g || seen_r)
			return -1;
	} else {
		if (!seen_g)
			return -1;
		if (seen_rk)
			return -1;
		if (!seen_r)
			memcpy(out->role, "member", 7);
	}
	return 0;
}

int omaq_invite_format(const omaq_invite *inv, char *buf, size_t buflen)
{
	int n;
	if (!inv || !buf || buflen < 32)
		return -1;
	if (inv->kind == INVITE_DIRECT) {
		if (inv->rk[0])
			n = snprintf(buf, buflen,
				     "omaq://invite/%s?i=%s&e=%lld&k=direct&rk=%s",
				     inv->tox_addr, inv->id, (long long)inv->expiry, inv->rk);
		else
			n = snprintf(buf, buflen, "omaq://invite/%s?i=%s&e=%lld&k=direct",
				     inv->tox_addr, inv->id, (long long)inv->expiry);
	} else {
		n = snprintf(buf, buflen, "omaq://invite/%s?i=%s&e=%lld&k=group&g=%s&r=%s",
			     inv->tox_addr, inv->id, (long long)inv->expiry,
			     inv->group, inv->role[0] ? inv->role : "member");
	}
	if (n < 0 || (size_t)n >= buflen)
		return -1;
	return 0;
}

bool omaq_invite_expired(const omaq_invite *inv, int64_t now)
{
	if (!inv)
		return true;
	return now >= inv->expiry;
}
