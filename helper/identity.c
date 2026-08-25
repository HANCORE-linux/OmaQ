#define _DEFAULT_SOURCE
#include "identity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int omaq_identity_pass_ok(const char *pass)
{
	size_t n;

	if (!pass)
		return 0;
	n = strlen(pass);
	if (n == 0 || n > 128)
		return 0;
	for (const unsigned char *p = (const unsigned char *)pass; *p; p++)
		if ((*p < 0x20 && *p != '\t') || *p == 0x7f)
			return 0;
	return 1;
}

int omaq_identity_new_pass_ok(const char *pass)
{
	const unsigned char *p = (const unsigned char *)pass;
	size_t characters = 0;

	if (!omaq_identity_pass_ok(pass))
		return 0;
	while (*p) {
		if (*p < 0x80) {
			p++;
		} else if (*p >= 0xc2 && *p <= 0xdf &&
			   p[1] >= 0x80 && p[1] <= 0xbf) {
			p += 2;
		} else if (*p >= 0xe0 && *p <= 0xef &&
			   p[1] >= 0x80 && p[1] <= 0xbf &&
			   p[2] >= 0x80 && p[2] <= 0xbf &&
			   !(*p == 0xe0 && p[1] < 0xa0) &&
			   !(*p == 0xed && p[1] >= 0xa0)) {
			p += 3;
		} else if (*p >= 0xf0 && *p <= 0xf4 &&
			   p[1] >= 0x80 && p[1] <= 0xbf &&
			   p[2] >= 0x80 && p[2] <= 0xbf &&
			   p[3] >= 0x80 && p[3] <= 0xbf &&
			   !(*p == 0xf0 && p[1] < 0x90) &&
			   !(*p == 0xf4 && p[1] > 0x8f)) {
			p += 4;
		} else {
			return 0;
		}
		characters++;
	}
	return characters >= 8;
}

#ifdef HAVE_TOX
struct omaq_tox *omaq_identity_load(const char *home, const char *pass, int *err)
{
	if (pass && pass[0] && !omaq_identity_pass_ok(pass)) {
		if (err)
			*err = -1;
		return NULL;
	}
	return omaq_tox_open(home, pass, err);
}

int omaq_identity_protect(struct omaq_tox *t, const char *pass)
{
	if (!omaq_identity_new_pass_ok(pass))
		return -1;
	return omaq_tox_protect(t, pass);
}

int omaq_identity_unprotect(struct omaq_tox *t, const char *pass)
{
	if (!omaq_identity_pass_ok(pass))
		return -1;
	return omaq_tox_unprotect(t, pass);
}

int omaq_identity_protected(const struct omaq_tox *t)
{
	return omaq_tox_protected(t);
}
#endif

static int path_ok(const char *p)
{
	if (!p || p[0] != '/')
		return 0;
	if (strlen(p) >= 512 || strstr(p, "..") || strchr(p, '\n'))
		return 0;
	return 1;
}

static int copy_file(const char *src, const char *dst)
{
	char tmp[640], buf[4096];
	FILE *in = NULL, *out = NULL;
	struct stat st;
	size_t n, total = 0;
	int in_fd = -1, out_fd = -1, tmp_created = 0, rc = -1;

	if (snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", dst, (long)getpid()) >= (int)sizeof(tmp))
		return -1;
	if (lstat(dst, &st) == 0) {
		if (!S_ISREG(st.st_mode))
			return -1;
	} else if (errno != ENOENT) {
		return -1;
	}
	in_fd = open(src, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
	if (in_fd < 0 || fstat(in_fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size <= 0 || (uint64_t)st.st_size > OMAQ_IDENTITY_FILE_MAX)
		goto done;
	in = fdopen(in_fd, "rb");
	if (!in)
		goto done;
	in_fd = -1;
	out_fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK |
		      O_NOFOLLOW, 0600);
	if (out_fd < 0)
		goto done;
	tmp_created = 1;
	out = fdopen(out_fd, "wb");
	if (!out)
		goto done;
	out_fd = -1;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (n > OMAQ_IDENTITY_FILE_MAX - total || fwrite(buf, 1, n, out) != n)
			goto done;
		total += n;
	}
	if (ferror(in) || total != (size_t)st.st_size ||
	    fflush(out) != 0 || fsync(fileno(out)) != 0)
		goto done;
	if (fclose(out) != 0) {
		out = NULL;
		goto done;
	}
	out = NULL;
	if (rename(tmp, dst) != 0)
		goto done;
	rc = 0;
done:
	if (in)
		fclose(in);
	if (out)
		fclose(out);
	if (in_fd >= 0)
		close(in_fd);
	if (out_fd >= 0)
		close(out_fd);
	if (rc != 0 && tmp_created)
		unlink(tmp);
	return rc;
}

int omaq_identity_export(const char *home, const char *path)
{
	char src[576];

	if (!home || !path_ok(path))
		return -1;
	if (snprintf(src, sizeof(src), "%s/tox.save", home) >= (int)sizeof(src))
		return -1;
	return copy_file(src, path);
}

int omaq_identity_export_exclusive(const char *home, const char *path)
{
	char src[576], buf[4096];
	FILE *in = NULL, *out = NULL;
	struct stat st;
	size_t n, total = 0;
	int in_fd = -1, out_fd = -1, rc = -1;

	if (!home || !path_ok(path) ||
	    snprintf(src, sizeof(src), "%s/tox.save", home) >= (int)sizeof(src))
		return -1;
	in_fd = open(src, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
	if (in_fd < 0 || fstat(in_fd, &st) != 0 || !S_ISREG(st.st_mode) ||
	    st.st_size <= 0 || (uint64_t)st.st_size > OMAQ_IDENTITY_FILE_MAX)
		goto done;
	in = fdopen(in_fd, "rb");
	if (!in)
		goto done;
	in_fd = -1;
	out_fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK |
		      O_NOFOLLOW, 0600);
	if (out_fd < 0)
		goto done;
	out = fdopen(out_fd, "wb");
	if (!out)
		goto done;
	out_fd = -1;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
		if (n > OMAQ_IDENTITY_FILE_MAX - total || fwrite(buf, 1, n, out) != n)
			goto done;
		total += n;
	}
	if (ferror(in) || total != (size_t)st.st_size ||
	    fflush(out) != 0 || fsync(fileno(out)) != 0)
		goto done;
	rc = 0;
done:
	if (in)
		fclose(in);
	if (out && fclose(out) != 0)
		rc = -1;
	if (in_fd >= 0)
		close(in_fd);
	if (out_fd >= 0)
		close(out_fd);
	if (rc != 0 && (out || out_fd >= 0))
		unlink(path);
	return rc;
}

int omaq_identity_import(const char *home, const char *path, int replace)
{
	char dst[576];
	struct stat st;

	if (!home || !path_ok(path))
		return -1;
	if (snprintf(dst, sizeof(dst), "%s/tox.save", home) >= (int)sizeof(dst))
		return -1;
	if (stat(dst, &st) == 0 && !replace)
		return 1; /* exists */
	return copy_file(path, dst);
}

#define OMAQ_IDENTITY_BUNDLE_V1_HEADER 16u
#define OMAQ_IDENTITY_BUNDLE_V2_HEADER 20u
#define OMAQ_IDENTITY_REGISTRY_MAX (64u * 1024u)

static const uint8_t identity_bundle_v1_magic[8] = {
	'O', 'M', 'A', 'Q', 'I', 'D', '1', '\n'
};
static const uint8_t identity_bundle_v2_magic[8] = {
	'O', 'M', 'A', 'Q', 'I', 'D', '2', '\n'
};
static const uint8_t empty_group_bindings[] = "OMAQGF1\n";

static uint32_t load_u32_be(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void store_u32_be(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)(value >> 24);
	p[1] = (uint8_t)(value >> 16);
	p[2] = (uint8_t)(value >> 8);
	p[3] = (uint8_t)value;
}

static int read_private_file(const char *path, size_t max, int allow_missing,
			     uint8_t **out, size_t *out_len)
{
	struct stat st;
	uint8_t *data = NULL;
	size_t used = 0;
	int fd;

	*out = NULL;
	*out_len = 0;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
	if (fd < 0)
		return allow_missing && errno == ENOENT ? 1 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    (uint64_t)st.st_size > max) {
		close(fd);
		return -1;
	}
	if (st.st_size > 0) {
		data = malloc((size_t)st.st_size);
		if (!data) {
			close(fd);
			return -1;
		}
	}
	while (used < (size_t)st.st_size) {
		ssize_t got = read(fd, data + used, (size_t)st.st_size - used);
		if (got <= 0) {
			free(data);
			close(fd);
			return -1;
		}
		used += (size_t)got;
	}
	if (close(fd) != 0) {
		free(data);
		return -1;
	}
	*out = data;
	*out_len = used;
	return 0;
}

static int write_private_file(const char *path, const uint8_t *data, size_t len)
{
	char tmp[640];
	struct stat st;
	size_t used = 0;
	int fd = -1, created = 0, rc = -1;

	if (!path_ok(path) || (!data && len) ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >=
		    (int)sizeof(tmp))
		return -1;
	if (lstat(path, &st) == 0) {
		if (!S_ISREG(st.st_mode))
			return -1;
	} else if (errno != ENOENT) {
		return -1;
	}
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK |
		  O_NOFOLLOW, 0600);
	if (fd < 0)
		return -1;
	created = 1;
	while (used < len) {
		ssize_t written = write(fd, data + used, len - used);
		if (written <= 0)
			goto done;
		used += (size_t)written;
	}
	if (fsync(fd) != 0) {
		close(fd);
		fd = -1;
		goto done;
	}
	if (close(fd) != 0) {
		fd = -1;
		goto done;
	}
	fd = -1;
	if (rename(tmp, path) != 0)
		goto done;
	created = 0;
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (created)
		unlink(tmp);
	return rc;
}

static int destination_aliases_source(const char *destination, const char *source)
{
	struct stat destination_stat, source_stat;

	if (stat(destination, &destination_stat) != 0)
		return errno == ENOENT ? 0 : -1;
	if (stat(source, &source_stat) != 0)
		return errno == ENOENT ? 0 : -1;
	return destination_stat.st_dev == source_stat.st_dev &&
	       destination_stat.st_ino == source_stat.st_ino;
}

int omaq_identity_bundle_export(const char *home, const char *path)
{
	char tox_path[576], registry_path[576], bindings_path[576];
	uint8_t *tox_data = NULL, *registry_data = NULL, *bindings_data = NULL;
	uint8_t *bundle = NULL;
	size_t tox_len = 0, registry_len = 0, bindings_len = 0, bundle_len;
	int registry_rc, bindings_rc, rc = -1;

	if (!home || !path_ok(path) ||
	    snprintf(tox_path, sizeof(tox_path), "%s/tox.save", home) >=
		    (int)sizeof(tox_path) ||
	    snprintf(registry_path, sizeof(registry_path), "%s/groups.tsv", home) >=
		    (int)sizeof(registry_path) ||
	    snprintf(bindings_path, sizeof(bindings_path), "%s/group-friends.tsv", home) >=
		    (int)sizeof(bindings_path) ||
	    destination_aliases_source(path, tox_path) != 0 ||
	    destination_aliases_source(path, registry_path) != 0 ||
	    destination_aliases_source(path, bindings_path) != 0 ||
	    read_private_file(tox_path, OMAQ_IDENTITY_FILE_MAX, 0,
			      &tox_data, &tox_len) != 0 || tox_len == 0)
		goto done;
	registry_rc = read_private_file(registry_path, OMAQ_IDENTITY_REGISTRY_MAX, 1,
					&registry_data, &registry_len);
	bindings_rc = read_private_file(bindings_path, OMAQ_IDENTITY_REGISTRY_MAX, 1,
					&bindings_data, &bindings_len);
	if (registry_rc < 0 || bindings_rc < 0)
		goto done;
	bundle_len = OMAQ_IDENTITY_BUNDLE_V2_HEADER + tox_len + registry_len +
		bindings_len;
	bundle = malloc(bundle_len);
	if (!bundle)
		goto done;
	memcpy(bundle, identity_bundle_v2_magic, sizeof(identity_bundle_v2_magic));
	store_u32_be(bundle + 8, (uint32_t)tox_len);
	store_u32_be(bundle + 12, (uint32_t)registry_len);
	store_u32_be(bundle + 16, (uint32_t)bindings_len);
	memcpy(bundle + OMAQ_IDENTITY_BUNDLE_V2_HEADER, tox_data, tox_len);
	if (registry_len)
		memcpy(bundle + OMAQ_IDENTITY_BUNDLE_V2_HEADER + tox_len,
		       registry_data, registry_len);
	if (bindings_len)
		memcpy(bundle + OMAQ_IDENTITY_BUNDLE_V2_HEADER + tox_len + registry_len,
		       bindings_data, bindings_len);
	rc = write_private_file(path, bundle, bundle_len);
done:
	free(bundle);
	free(bindings_data);
	free(registry_data);
	if (tox_data) {
		memset(tox_data, 0, tox_len);
		free(tox_data);
	}
	return rc;
}

int omaq_identity_bundle_snapshot(const char *source, const char *destination)
{
	uint8_t *bundle = NULL;
	size_t bundle_len = 0;
	int rc;

	if (!path_ok(source) || !path_ok(destination) ||
	    read_private_file(source,
		OMAQ_IDENTITY_BUNDLE_V2_HEADER + OMAQ_IDENTITY_FILE_MAX +
		OMAQ_IDENTITY_REGISTRY_MAX * 2u, 0, &bundle, &bundle_len) != 0 ||
	    bundle_len == 0)
		return -1;
	rc = write_private_file(destination, bundle, bundle_len);
	memset(bundle, 0, bundle_len);
	free(bundle);
	return rc;
}

int omaq_identity_bundle_import(const char *home, const char *path, int replace)
{
	char tox_path[576], registry_path[576], bindings_path[576];
	uint8_t *bundle = NULL;
	size_t bundle_len = 0, header_len, payload_len;
	uint32_t tox_len, registry_len, bindings_len = 0;
	struct stat st;
	int is_v1, is_v2, read_rc, rc = -1;

	if (!home || !path_ok(path) ||
	    snprintf(tox_path, sizeof(tox_path), "%s/tox.save", home) >=
		    (int)sizeof(tox_path) ||
	    snprintf(registry_path, sizeof(registry_path), "%s/groups.tsv", home) >=
		    (int)sizeof(registry_path) ||
	    snprintf(bindings_path, sizeof(bindings_path), "%s/group-friends.tsv", home) >=
		    (int)sizeof(bindings_path))
		return -1;
	if (lstat(tox_path, &st) == 0 && !replace)
		return S_ISREG(st.st_mode) ? 1 : -1;
	read_rc = read_private_file(path,
		OMAQ_IDENTITY_BUNDLE_V2_HEADER + OMAQ_IDENTITY_FILE_MAX +
		OMAQ_IDENTITY_REGISTRY_MAX * 2u, 0, &bundle, &bundle_len);
	if (read_rc != 0)
		return -1;
	is_v1 = bundle_len >= sizeof(identity_bundle_v1_magic) &&
		memcmp(bundle, identity_bundle_v1_magic,
		       sizeof(identity_bundle_v1_magic)) == 0;
	is_v2 = bundle_len >= sizeof(identity_bundle_v2_magic) &&
		memcmp(bundle, identity_bundle_v2_magic,
		       sizeof(identity_bundle_v2_magic)) == 0;
	if (!is_v1 && !is_v2) {
		if (bundle_len >= 6 && memcmp(bundle, "OMAQID", 6) == 0)
			goto done;
		memset(bundle, 0, bundle_len);
		free(bundle);
		bundle = NULL;
		if (write_private_file(registry_path, NULL, 0) != 0 ||
		    write_private_file(bindings_path, empty_group_bindings,
				       sizeof(empty_group_bindings) - 1) != 0)
			return -1;
		return omaq_identity_import(home, path, replace);
	}
	header_len = is_v2 ? OMAQ_IDENTITY_BUNDLE_V2_HEADER :
		OMAQ_IDENTITY_BUNDLE_V1_HEADER;
	if (bundle_len < header_len)
		goto done;
	tox_len = load_u32_be(bundle + 8);
	registry_len = load_u32_be(bundle + 12);
	if (is_v2)
		bindings_len = load_u32_be(bundle + 16);
	payload_len = (size_t)tox_len + (size_t)registry_len + (size_t)bindings_len;
	if (tox_len == 0 || tox_len > OMAQ_IDENTITY_FILE_MAX ||
	    registry_len > OMAQ_IDENTITY_REGISTRY_MAX ||
	    bindings_len > OMAQ_IDENTITY_REGISTRY_MAX ||
	    payload_len != bundle_len - header_len)
		goto done;
	if (write_private_file(registry_path, bundle + header_len + tox_len,
			       registry_len) != 0 ||
	    (is_v1
		? write_private_file(bindings_path, empty_group_bindings,
				     sizeof(empty_group_bindings) - 1)
		: write_private_file(bindings_path,
				     bundle + header_len + tox_len + registry_len,
				     bindings_len)) != 0 ||
	    write_private_file(tox_path, bundle + header_len, tox_len) != 0)
		goto done;
	rc = 0;
done:
	if (bundle) {
		memset(bundle, 0, bundle_len);
		free(bundle);
	}
	return rc;
}
