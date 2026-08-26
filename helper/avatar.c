#define _DEFAULT_SOURCE
#include "avatar.h"
#include "file.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_AVATAR_DECODERS
#include <jpeglib.h>
#include <png.h>
#include <webp/decode.h>
#endif

#define OMAQ_AVATAR_DIMENSION_MAX 4096u
#define OMAQ_AVATAR_DECODED_MAX (16u * 1024u * 1024u)
#define OMAQ_AVATAR_CACHE_MAX 65

_Static_assert(OMAQ_INLINE_IMAGE_SOURCE_MAX == OMAQ_FILE_MAX,
	       "inline image and file-transfer limits must match");

static struct {
	int used;
	char path[512];
	dev_t device;
	ino_t inode;
	off_t size;
	struct timespec modified;
} avatar_cache[OMAQ_AVATAR_CACHE_MAX];
static unsigned int avatar_cache_next;

static int mkdir_p(const char *path)
{
	struct stat st;

	if (lstat(path, &st) == 0)
		return S_ISDIR(st.st_mode) && st.st_uid == geteuid() &&
		       (st.st_mode & 0077) == 0 ? 0 : -1;
	if (mkdir(path, 0700) != 0)
		return -1;
	return 0;
}

int omaq_avatar_id_ok(const char *id)
{
	size_t i, n;

	if (!id || !id[0])
		return 0;
	if (strcmp(id, "self") == 0)
		return 1;
	n = strlen(id);
	if (n > OMAQ_AVATAR_ID_MAX)
		return 0;
	if (n == 66 && id[0] == 'd' && id[1] == ':') {
		for (i = 2; i < n; i++)
			if (!((id[i] >= '0' && id[i] <= '9') ||
			      (id[i] >= 'a' && id[i] <= 'f')))
				return 0;
		return 1;
	}
	if (id[0] == '0' && id[1])
		return 0;
	for (i = 0; i < n; i++) {
		if (id[i] < '0' || id[i] > '9')
			return 0;
	}
	return 1;
}

int omaq_avatar_src_ok(const char *path)
{
	const char *dot;

	if (!omaq_file_path_ok(path))
		return 0;
	dot = strrchr(path, '.');
	if (!dot)
		return 0;
	if (strcasecmp(dot, ".png") == 0 || strcasecmp(dot, ".jpg") == 0 ||
	    strcasecmp(dot, ".jpeg") == 0 || strcasecmp(dot, ".webp") == 0)
		return 1;
	return 0;
}

int omaq_avatar_dest(const char *home, const char *id, char *out, size_t n)
{
	int wr;

	if (!home || !home[0] || !omaq_avatar_id_ok(id) || !out || n < 8)
		return -1;
	wr = snprintf(out, n, "%s/avatars/%s.png", home, id);
	if (wr < 0 || (size_t)wr >= n)
		return -1;
	return 0;
}

int omaq_avatar_is_dest(const char *home, const char *path)
{
	char prefix[512], idbuf[OMAQ_AVATAR_ID_MAX + 8];
	const char *id;
	size_t n;
	int wr;

	if (!path || !home)
		return 0;
	wr = snprintf(prefix, sizeof(prefix), "%s/avatars/", home);
	if (wr < 0 || (size_t)wr >= sizeof(prefix))
		return 0;
	if (strncmp(path, prefix, (size_t)wr) != 0)
		return 0;
	id = path + wr;
	n = strlen(id);
	if (n < 5 || strcmp(id + n - 4, ".png") != 0)
		return 0;
	n -= 4;
	if (n >= sizeof(idbuf))
		return 0;
	memcpy(idbuf, id, n);
	idbuf[n] = '\0';
	return omaq_avatar_id_ok(idbuf);
}

static int read_private_image_limited(const char *path, unsigned char **buffer,
				      size_t *size, uint64_t maximum)
{
	struct stat st;
	unsigned char *data;
	size_t offset = 0;
	int fd;

	if (!path || !buffer || !size || maximum == 0)
		return -1;
	*buffer = NULL;
	*size = 0;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
	    st.st_nlink != 1 || st.st_size <= 0 || (uint64_t)st.st_size > maximum) {
		close(fd);
		return -1;
	}
	data = malloc((size_t)st.st_size);
	if (!data) {
		close(fd);
		return -1;
	}
	while (offset < (size_t)st.st_size) {
		ssize_t got = read(fd, data + offset, (size_t)st.st_size - offset);
		if (got < 0 && errno == EINTR)
			continue;
		if (got <= 0) {
			free(data);
			close(fd);
			return -1;
		}
		offset += (size_t)got;
	}
	close(fd);
	*buffer = data;
	*size = offset;
	return 0;
}

static int read_private_image(const char *path, unsigned char **buffer, size_t *size)
{
	return read_private_image_limited(path, buffer, size, OMAQ_AVATAR_MAX);
}

static int decoded_size_ok(uint32_t width, uint32_t height, size_t *bytes)
{
	uint64_t total;

	if (width == 0 || height == 0 || width > OMAQ_AVATAR_DIMENSION_MAX ||
	    height > OMAQ_AVATAR_DIMENSION_MAX)
		return 0;
	total = (uint64_t)width * (uint64_t)height * 4u;
	if (total > OMAQ_AVATAR_DECODED_MAX || total > SIZE_MAX)
		return 0;
	*bytes = (size_t)total;
	return 1;
}

#ifdef HAVE_AVATAR_DECODERS
struct jpeg_failure {
	struct jpeg_error_mgr base;
	jmp_buf jump;
};

static void jpeg_fail(j_common_ptr common)
{
	struct jpeg_failure *failure = (struct jpeg_failure *)common->err;
	longjmp(failure->jump, 1);
}

static int decode_png(const unsigned char *input, size_t input_size,
		      unsigned char **rgba, uint32_t *width, uint32_t *height)
{
	png_image image;
	size_t bytes;
	unsigned char *decoded;

	memset(&image, 0, sizeof(image));
	image.version = PNG_IMAGE_VERSION;
	if (!png_image_begin_read_from_memory(&image, input, input_size) ||
	    !decoded_size_ok(image.width, image.height, &bytes)) {
		png_image_free(&image);
		return -1;
	}
	image.format = PNG_FORMAT_RGBA;
	decoded = malloc(bytes);
	if (!decoded) {
		png_image_free(&image);
		return -1;
	}
	if (!png_image_finish_read(&image, NULL, decoded, 0, NULL)) {
		free(decoded);
		png_image_free(&image);
		return -1;
	}
	*rgba = decoded;
	*width = image.width;
	*height = image.height;
	png_image_free(&image);
	return 0;
}

static int decode_jpeg(const unsigned char *input, size_t input_size,
		       unsigned char **rgba, uint32_t *width, uint32_t *height)
{
	struct jpeg_decompress_struct jpeg;
	struct jpeg_failure failure;
	unsigned char *volatile decoded = NULL;
	unsigned char *volatile row = NULL;
	size_t bytes = 0;
	volatile int created = 0;
	int rc = -1;

	memset(&jpeg, 0, sizeof(jpeg));
	jpeg.err = jpeg_std_error(&failure.base);
	failure.base.error_exit = jpeg_fail;
	if (setjmp(failure.jump))
		goto done;
	jpeg_create_decompress(&jpeg);
	created = 1;
	jpeg_mem_src(&jpeg, input, input_size);
	if (jpeg_read_header(&jpeg, TRUE) != JPEG_HEADER_OK ||
	    !decoded_size_ok(jpeg.image_width, jpeg.image_height, &bytes))
		goto done;
	jpeg.out_color_space = JCS_RGB;
	if (!jpeg_start_decompress(&jpeg))
		goto done;
	if (jpeg.output_components != 3 ||
	    !decoded_size_ok(jpeg.output_width, jpeg.output_height, &bytes))
		goto done;
	decoded = malloc(bytes);
	row = malloc((size_t)jpeg.output_width * 3u);
	if (!decoded || !row)
		goto done;
	while (jpeg.output_scanline < jpeg.output_height) {
		JSAMPROW rows[1] = { row };
		uint32_t y = jpeg.output_scanline;
		if (jpeg_read_scanlines(&jpeg, rows, 1) != 1)
			goto done;
		for (uint32_t x = 0; x < jpeg.output_width; x++) {
			size_t source = (size_t)x * 3u;
			size_t target = ((size_t)y * jpeg.output_width + x) * 4u;
			decoded[target] = row[source];
			decoded[target + 1] = row[source + 1];
			decoded[target + 2] = row[source + 2];
			decoded[target + 3] = 0xff;
		}
	}
	if (!jpeg_finish_decompress(&jpeg))
		goto done;
	*rgba = (unsigned char *)decoded;
	*width = jpeg.output_width;
	*height = jpeg.output_height;
	decoded = NULL;
	rc = 0;
done:
	free((unsigned char *)row);
	free((unsigned char *)decoded);
	if (created)
		jpeg_destroy_decompress(&jpeg);
	return rc;
}

static int decode_webp(const unsigned char *input, size_t input_size,
		       unsigned char **rgba, uint32_t *width, uint32_t *height)
{
	unsigned char *decoded;
	size_t bytes;
	int image_width, image_height;

	if (!WebPGetInfo(input, input_size, &image_width, &image_height) ||
	    image_width <= 0 || image_height <= 0 ||
	    !decoded_size_ok((uint32_t)image_width, (uint32_t)image_height, &bytes))
		return -1;
	decoded = malloc(bytes);
	if (!decoded)
		return -1;
	if (!WebPDecodeRGBAInto(input, input_size, decoded, bytes,
				 (int)((uint32_t)image_width * 4u))) {
		free(decoded);
		return -1;
	}
	*rgba = decoded;
	*width = (uint32_t)image_width;
	*height = (uint32_t)image_height;
	return 0;
}
#endif

static int decode_image(const unsigned char *input, size_t input_size,
			unsigned char **rgba, uint32_t *width, uint32_t *height)
{
#ifndef HAVE_AVATAR_DECODERS
	(void)input;
	(void)input_size;
	(void)rgba;
	(void)width;
	(void)height;
	return -1;
#else
	if (!input || !rgba || !width || !height)
		return -1;
	*rgba = NULL;
	if (input_size >= 8 && png_sig_cmp((png_bytep)input, 0, 8) == 0)
		return decode_png(input, input_size, rgba, width, height);
	if (input_size >= 3 && input[0] == 0xff && input[1] == 0xd8 && input[2] == 0xff)
		return decode_jpeg(input, input_size, rgba, width, height);
	if (input_size >= 12 && memcmp(input, "RIFF", 4) == 0 &&
	    memcmp(input + 8, "WEBP", 4) == 0)
		return decode_webp(input, input_size, rgba, width, height);
	return -1;
#endif
}

static int fsync_parent(const char *path)
{
	char directory[512], *slash;
	int fd, rc;

	if (!path || strlen(path) >= sizeof(directory))
		return -1;
	memcpy(directory, path, strlen(path) + 1);
	slash = strrchr(directory, '/');
	if (!slash || slash == directory)
		return -1;
	*slash = '\0';
	fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return -1;
	rc = fsync(fd);
	close(fd);
	return rc;
}

static unsigned char *downscale_rgba_half(const unsigned char *source,
					  uint32_t width, uint32_t height,
					  uint32_t *new_width, uint32_t *new_height)
{
	unsigned char *result;
	size_t bytes;

	if (!source || !new_width || !new_height || width == 0 || height == 0)
		return NULL;
	*new_width = width > 1 ? (width + 1) / 2 : 1;
	*new_height = height > 1 ? (height + 1) / 2 : 1;
	if ((size_t)*new_width > SIZE_MAX / 4u / (size_t)*new_height)
		return NULL;
	bytes = (size_t)*new_width * (size_t)*new_height * 4u;
	result = malloc(bytes);
	if (!result)
		return NULL;
	for (uint32_t y = 0; y < *new_height; y++) {
		uint32_t source_y = (uint32_t)(((uint64_t)y * height) / *new_height);
		for (uint32_t x = 0; x < *new_width; x++) {
			uint32_t source_x = (uint32_t)(((uint64_t)x * width) / *new_width);
			memcpy(result + ((size_t)y * *new_width + x) * 4u,
			       source + ((size_t)source_y * width + source_x) * 4u, 4u);
		}
	}
	return result;
}

static int write_canonical_png_limited(const char *path, const unsigned char *rgba,
				       uint32_t width, uint32_t height,
				       size_t maximum)
{
#ifndef HAVE_AVATAR_DECODERS
	(void)path;
	(void)rgba;
	(void)width;
	(void)height;
	return -1;
#else
	png_image image;
	const unsigned char *pixels = rgba;
	unsigned char *scaled = NULL, *encoded = NULL;
	png_alloc_size_t encoded_size = 0;
	char temporary[576] = { 0 };
	uint32_t pixel_width = width, pixel_height = height, nonce;
	size_t offset = 0;
	int fd = -1, rc = -1;

	if (!path || !rgba || maximum == 0)
		return -1;
	for (;;) {
		uint32_t next_width, next_height;
		unsigned char *next;
		memset(&image, 0, sizeof(image));
		image.version = PNG_IMAGE_VERSION;
		image.width = pixel_width;
		image.height = pixel_height;
		image.format = PNG_FORMAT_RGBA;
		encoded_size = 0;
		if (!png_image_write_to_memory(&image, NULL, &encoded_size, 0, pixels, 0, NULL) ||
		    encoded_size == 0)
			goto done;
		if (encoded_size <= maximum)
			break;
		if (pixel_width == 1 && pixel_height == 1)
			goto done;
		next = downscale_rgba_half(pixels, pixel_width, pixel_height,
					   &next_width, &next_height);
		if (!next)
			goto done;
		free(scaled);
		scaled = next;
		pixels = scaled;
		pixel_width = next_width;
		pixel_height = next_height;
	}
	encoded = malloc(encoded_size);
	if (!encoded || !png_image_write_to_memory(&image, encoded, &encoded_size, 0,
						    pixels, 0, NULL) ||
	    getrandom(&nonce, sizeof(nonce), 0) != (ssize_t)sizeof(nonce) ||
	    snprintf(temporary, sizeof(temporary), "%s.tmp.%ld.%08x", path,
		     (long)getpid(), nonce) >= (int)sizeof(temporary))
		goto done;
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		goto done;
	while (offset < encoded_size) {
		ssize_t written = write(fd, encoded + offset, encoded_size - offset);
		if (written < 0 && errno == EINTR)
			continue;
		if (written <= 0)
			goto done;
		offset += (size_t)written;
	}
	if (fsync(fd) != 0 || close(fd) != 0) {
		fd = -1;
		goto done;
	}
	fd = -1;
	if (rename(temporary, path) != 0 || fsync_parent(path) != 0)
		goto done;
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (rc != 0 && temporary[0])
		unlink(temporary);
	free(encoded);
	free(scaled);
	return rc;
#endif
}

static int write_canonical_png(const char *path, const unsigned char *rgba,
			       uint32_t width, uint32_t height)
{
	return write_canonical_png_limited(path, rgba, width, height,
					   OMAQ_AVATAR_MAX);
}

static int avatar_cache_matches(const char *path, const struct stat *status)
{
	for (int i = 0; i < OMAQ_AVATAR_CACHE_MAX; i++)
		if (avatar_cache[i].used && strcmp(avatar_cache[i].path, path) == 0)
			return avatar_cache[i].device == status->st_dev &&
			       avatar_cache[i].inode == status->st_ino &&
			       avatar_cache[i].size == status->st_size &&
			       avatar_cache[i].modified.tv_sec == status->st_mtim.tv_sec &&
			       avatar_cache[i].modified.tv_nsec == status->st_mtim.tv_nsec;
	return 0;
}

static int avatar_cache_store(const char *path, const struct stat *status)
{
	int free_index = -1;

	for (int i = 0; i < OMAQ_AVATAR_CACHE_MAX; i++) {
		if (!avatar_cache[i].used && free_index < 0)
			free_index = i;
		if (avatar_cache[i].used && strcmp(avatar_cache[i].path, path) == 0) {
			free_index = i;
			break;
		}
	}
	if (free_index < 0) {
		free_index = (int)(avatar_cache_next % OMAQ_AVATAR_CACHE_MAX);
		avatar_cache_next = (avatar_cache_next + 1) % OMAQ_AVATAR_CACHE_MAX;
	}
	if (snprintf(avatar_cache[free_index].path,
					 sizeof(avatar_cache[free_index].path), "%s", path) >=
		(int)sizeof(avatar_cache[free_index].path))
		return -1;
	avatar_cache[free_index].used = 1;
	avatar_cache[free_index].device = status->st_dev;
	avatar_cache[free_index].inode = status->st_ino;
	avatar_cache[free_index].size = status->st_size;
	avatar_cache[free_index].modified = status->st_mtim;
	return 0;
}

static int decimal_component_ok(const char *text, size_t length, int positive)
{
	uint64_t value = 0;

	if (!text || length == 0 || length > 10 ||
	    (length > 1 && text[0] == '0'))
		return 0;
	for (size_t i = 0; i < length; i++) {
		if (text[i] < '0' || text[i] > '9')
			return 0;
		value = value * 10 + (uint64_t)(text[i] - '0');
		if (value > UINT32_MAX)
			return 0;
	}
	return !positive || value > 0;
}

static int hex_nonce_ok(const char *text)
{
	if (!text || strlen(text) != 8)
		return 0;
	for (size_t i = 0; i < 8; i++)
		if (!((text[i] >= '0' && text[i] <= '9') ||
		      (text[i] >= 'a' && text[i] <= 'f')))
			return 0;
	return 1;
}

static int avatar_temp_name_ok(const char *name)
{
	const char *suffix = strstr(name, ".png.incoming.");
	const char *first, *second;
	char id[OMAQ_AVATAR_ID_MAX + 1];
	size_t id_length;

	if (!suffix) {
		suffix = strstr(name, ".png.tmp.");
		if (!suffix)
			return 0;
		id_length = (size_t)(suffix - name);
		if (id_length == 0 || id_length >= sizeof(id))
			return 0;
		memcpy(id, name, id_length);
		id[id_length] = '\0';
		if (!omaq_avatar_id_ok(id))
			return 0;
		first = suffix + strlen(".png.tmp.");
		second = strchr(first, '.');
		return second && decimal_component_ok(first, (size_t)(second - first), 1) &&
		       hex_nonce_ok(second + 1);
	}
	id_length = (size_t)(suffix - name);
	if (id_length == 0 || id_length >= sizeof(id))
		return 0;
	memcpy(id, name, id_length);
	id[id_length] = '\0';
	if (!omaq_avatar_id_ok(id))
		return 0;
	first = suffix + strlen(".png.incoming.");
	second = strchr(first, '.');
	if (!second || !decimal_component_ok(first, (size_t)(second - first), 0))
		return 0;
	first = second + 1;
	second = strchr(first, '.');
	return second && decimal_component_ok(first, (size_t)(second - first), 0) &&
	       hex_nonce_ok(second + 1);
}

int omaq_avatar_cleanup_temps(const char *home)
{
	char path[512];
	struct stat status;
	DIR *directory = NULL;
	struct dirent *entry;
	int fd = -1, removed = 0, rc = -1;

	if (!home || snprintf(path, sizeof(path), "%s/avatars", home) >= (int)sizeof(path))
		return -1;
	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &status) != 0 || !S_ISDIR(status.st_mode) ||
	    status.st_uid != geteuid() || (status.st_mode & 0077) != 0)
		goto done;
	directory = fdopendir(fd);
	if (!directory)
		goto done;
	fd = -1;
	for (;;) {
		errno = 0;
		entry = readdir(directory);
		if (!entry)
			break;
		if (!avatar_temp_name_ok(entry->d_name))
			continue;
		if (unlinkat(dirfd(directory), entry->d_name, 0) != 0)
			goto done;
		removed = 1;
	}
	if (errno != 0)
		goto done;
	rc = 0;
done:
	if (directory) {
		if (removed && fsync(dirfd(directory)) != 0)
			rc = -1;
		if (closedir(directory) != 0)
			rc = -1;
	}
	if (fd >= 0 && close(fd) != 0)
		rc = -1;
	return rc;
}

int omaq_avatar_reconcile(const char *home, const char *id)
{
	char path[512];
	unsigned char *input = NULL, *rgba = NULL;
	uint32_t width = 0, height = 0;
	size_t input_size = 0;
	struct stat status;
	int rc = -1;

	if (omaq_avatar_dest(home, id, path, sizeof(path)) != 0)
		return -1;
	if (lstat(path, &status) != 0)
		return errno == ENOENT ? 0 : -1;
	if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() || status.st_nlink != 1 ||
	    (status.st_mode & 0077) != 0 || status.st_size <= 0 ||
	    (uint64_t)status.st_size > OMAQ_AVATAR_MAX)
		goto invalid;
	if (avatar_cache_matches(path, &status))
		return 1;
	if (read_private_image(path, &input, &input_size) != 0 ||
	    decode_image(input, input_size, &rgba, &width, &height) != 0 ||
	    write_canonical_png(path, rgba, width, height) != 0 ||
	    lstat(path, &status) != 0 || !S_ISREG(status.st_mode) ||
	    avatar_cache_store(path, &status) != 0)
		goto invalid;
	rc = 1;
	goto done;
invalid:
	if (unlink(path) == 0)
		(void)fsync_parent(path);
done:
	free(rgba);
	free(input);
	return rc;
}

int omaq_avatar_validate_file(const char *path)
{
	unsigned char *input = NULL, *rgba = NULL;
	uint32_t width = 0, height = 0;
	size_t input_size = 0;
	int rc = -1;

	if (read_private_image(path, &input, &input_size) == 0 &&
	    decode_image(input, input_size, &rgba, &width, &height) == 0)
		rc = 0;
	free(rgba);
	free(input);
	return rc;
}

int omaq_inline_image_validate_file(const char *path)
{
	unsigned char *input = NULL, *rgba = NULL;
	uint32_t width = 0, height = 0;
	size_t input_size = 0;
	int rc = -1;

	if (read_private_image_limited(path, &input, &input_size,
				       OMAQ_INLINE_IMAGE_SOURCE_MAX) == 0 &&
	    decode_image(input, input_size, &rgba, &width, &height) == 0)
		rc = 0;
	free(rgba);
	free(input);
	return rc;
}

int omaq_inline_image_import_file(const char *source, const char *destination)
{
	unsigned char *input = NULL, *rgba = NULL;
	uint32_t width = 0, height = 0;
	size_t input_size = 0;
	int rc = -1;

	if (read_private_image_limited(source, &input, &input_size,
				       OMAQ_INLINE_IMAGE_SOURCE_MAX) == 0 &&
	    decode_image(input, input_size, &rgba, &width, &height) == 0 &&
	    write_canonical_png_limited(destination, rgba, width, height,
					OMAQ_FILE_MAX) == 0)
		rc = 0;
	free(rgba);
	free(input);
	return rc;
}

int omaq_inline_image_canonicalize_file(const char *path)
{
	return omaq_inline_image_import_file(path, path);
}

int omaq_avatar_commit_received(const char *home, const char *id,
				const char *staging, char *dest, size_t destn)
{
	char directory[512], output[512];
	unsigned char *input = NULL, *rgba = NULL;
	uint32_t width = 0, height = 0;
	size_t input_size = 0;
	int rc = -1;

	if (!home || !id || !staging ||
	    snprintf(directory, sizeof(directory), "%s/avatars", home) >=
	    (int)sizeof(directory) || mkdir_p(directory) != 0 ||
	    omaq_avatar_dest(home, id, output, sizeof(output)) != 0 ||
	    read_private_image(staging, &input, &input_size) != 0 ||
	    decode_image(input, input_size, &rgba, &width, &height) != 0 ||
	    write_canonical_png(output, rgba, width, height) != 0)
		goto done;
	if (dest && destn && snprintf(dest, destn, "%s", output) >= (int)destn)
		goto done;
	rc = 0;
done:
	free(rgba);
	free(input);
	unlink(staging);
	return rc;
}

int omaq_avatar_install(const char *home, const char *id, const char *src,
			char *dest, size_t destn)
{
	char directory[512], output[512];
	unsigned char *input = NULL, *rgba = NULL;
	uint32_t width = 0, height = 0;
	size_t input_size = 0;
	int rc = -1;

	if (!omaq_avatar_src_ok(src) ||
	    snprintf(directory, sizeof(directory), "%s/avatars", home) >=
	    (int)sizeof(directory) || mkdir_p(directory) != 0 ||
	    omaq_avatar_dest(home, id, output, sizeof(output)) != 0 ||
	    read_private_image(src, &input, &input_size) != 0 ||
	    decode_image(input, input_size, &rgba, &width, &height) != 0 ||
	    write_canonical_png(output, rgba, width, height) != 0)
		goto done;
	if (dest && destn && snprintf(dest, destn, "%s", output) >= (int)destn)
		goto done;
	rc = 0;
done:
	free(rgba);
	free(input);
	return rc;
}
