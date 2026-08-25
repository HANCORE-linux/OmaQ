#define _DEFAULT_SOURCE

#include "receipt.h"
#include "message.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define RECEIPT_OUTBOX_BYTES (4u * 1024u * 1024u)

static int copy_part(char *out, size_t outn, const char *start, size_t len)
{
	if (!out || outn == 0 || !start || len + 1 > outn)
		return -1;
	memcpy(out, start, len);
	out[len] = '\0';
	return 0;
}

static int looks_like_message_id(const char *start, size_t len)
{
	char id[97];

	if (copy_part(id, sizeof(id), start, len) != 0)
		return 0;
	return omaq_message_id_ok(id);
}

int omaq_message_wire_pack(char *out, size_t outn, const char *id,
			   const char *reply, const char *text)
{
	int wr;
	const char *reply_id = reply && reply[0] ? reply : "-";

	if (!out || outn == 0 || !omaq_message_id_ok(id) || !text || strchr(reply_id, '|'))
		return -1;
	wr = snprintf(out, outn, "OQM1|%s|%s|%s", id, reply_id, text);
	return wr < 0 || (size_t)wr >= outn ? -1 : 0;
}

int omaq_message_wire_unpack(const char *wire, char *id, size_t idn,
			     char *reply, size_t replyn, char *text, size_t textn)
{
	const char *start, *sep, *reply_sep;

	if (!wire || strncmp(wire, "OQM1|", 5) != 0)
		return -1;
	start = wire + 5;
	sep = strchr(start, '|');
	if (!sep || sep == start || copy_part(id, idn, start, (size_t)(sep - start)) != 0 ||
	    !omaq_message_id_ok(id))
		return -1;
	reply_sep = strchr(sep + 1, '|');
	if (reply_sep && looks_like_message_id(sep + 1, (size_t)(reply_sep - (sep + 1)))) {
		if (copy_part(reply, replyn, sep + 1, (size_t)(reply_sep - (sep + 1))) != 0)
			return -1;
		if (strcmp(reply, "-") == 0)
			reply[0] = '\0';
		return copy_part(text, textn, reply_sep + 1, strlen(reply_sep + 1));
	}
	if (reply && replyn)
		reply[0] = '\0';
	return copy_part(text, textn, sep + 1, strlen(sep + 1));
}

int omaq_receipt_wire_pack(char *out, size_t outn, const char *id, const char *state)
{
	int wr;

	if (!out || outn == 0 || !omaq_message_id_ok(id) || !state || strchr(state, '|'))
		return -1;
	if (strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0)
		return -1;
	wr = snprintf(out, outn, "OQA1|%s|%s", state, id);
	return wr < 0 || (size_t)wr >= outn ? -1 : 0;
}

int omaq_receipt_wire_unpack(const char *wire, char *id, size_t idn,
			     char *state, size_t staten)
{
	const char *start, *sep;

	if (!wire || strncmp(wire, "OQA1|", 5) != 0)
		return -1;
	start = wire + 5;
	sep = strchr(start, '|');
	if (!sep || sep == start || copy_part(state, staten, start, (size_t)(sep - start)) != 0)
		return -1;
	if ((strcmp(state, "delivered") != 0 && strcmp(state, "read") != 0) ||
	    copy_part(id, idn, sep + 1, strlen(sep + 1)) != 0 || !omaq_message_id_ok(id))
		return -1;
	return 0;
}

static int receipt_target_ok(const char *target)
{
	if (!target || strcmp(target, "-") == 0)
		return target != NULL;
	if (strlen(target) != 64)
		return 0;
	for (size_t i = 0; i < 64; i++)
		if (!((target[i] >= '0' && target[i] <= '9') ||
		      (target[i] >= 'a' && target[i] <= 'f')))
			return 0;
	return 1;
}

int omaq_receipt_confirm_wire_pack(char *out, size_t outn, const char *id,
				   const char *state, const char *target)
{
	int wr;

	if (!out || outn == 0 || !omaq_message_id_ok(id) ||
	    !state || strcmp(state, "read") != 0 || !receipt_target_ok(target))
		return -1;
	wr = snprintf(out, outn, "OQX1|receipt-confirm-v1|%s|%s|%s",
		      state, id, target);
	return wr < 0 || (size_t)wr >= outn ? -1 : 0;
}

int omaq_receipt_confirm_wire_unpack(const char *wire, char *id, size_t idn,
				     char *state, size_t staten,
				     char *target, size_t targetn)
{
	const char *start, *first, *second;

	if (!wire || strncmp(wire, "OQX1|receipt-confirm-v1|", 24) != 0)
		return -1;
	start = wire + 24;
	first = strchr(start, '|');
	if (!first || first == start ||
	    copy_part(state, staten, start, (size_t)(first - start)) != 0 ||
	    strcmp(state, "read") != 0)
		return -1;
	second = strchr(first + 1, '|');
	if (!second || second == first + 1 ||
	    copy_part(id, idn, first + 1, (size_t)(second - (first + 1))) != 0 ||
	    !omaq_message_id_ok(id) ||
	    copy_part(target, targetn, second + 1, strlen(second + 1)) != 0 ||
	    !receipt_target_ok(target))
		return -1;
	return 0;
}

static int receipt_conversation_ok(const char *conversation)
{
	uint64_t value = 0;
	size_t n;

	if (!conversation || !(n = strlen(conversation)) || n >= 80)
		return 0;
	if (n == 66 && conversation[0] == 'g' && conversation[1] == ':') {
		for (size_t i = 2; i < n; i++)
			if (!((conversation[i] >= '0' && conversation[i] <= '9') ||
			      (conversation[i] >= 'a' && conversation[i] <= 'f')))
				return 0;
		return 1;
	}
	if (n > 10 || (n > 1 && conversation[0] == '0'))
		return 0;
	for (size_t i = 0; i < n; i++) {
		if (!isdigit((unsigned char)conversation[i]))
			return 0;
		value = value * 10u + (uint64_t)(conversation[i] - '0');
	}
	return value <= UINT32_MAX;
}

void omaq_receipt_outbox_init(omaq_receipt_outbox *outbox)
{
	if (outbox)
		memset(outbox, 0, sizeof(*outbox));
}

void omaq_receipt_outbox_destroy(omaq_receipt_outbox *outbox)
{
	if (!outbox)
		return;
	free(outbox->entries);
	memset(outbox, 0, sizeof(*outbox));
}

int omaq_receipt_outbox_clone(omaq_receipt_outbox *destination,
			      const omaq_receipt_outbox *source)
{
	omaq_receipt_outbox copy;

	if (!destination || !source || source->length > OMAQ_RECEIPT_OUTBOX_MAX)
		return -1;
	omaq_receipt_outbox_init(&copy);
	if (source->length) {
		copy.entries = malloc(source->length * sizeof(*copy.entries));
		if (!copy.entries)
			return -1;
		memcpy(copy.entries, source->entries,
		       source->length * sizeof(*copy.entries));
		copy.length = source->length;
		copy.capacity = source->length;
	}
	omaq_receipt_outbox_destroy(destination);
	*destination = copy;
	return 0;
}

static ssize_t receipt_outbox_find(const omaq_receipt_outbox *outbox,
				   const char *conversation, const char *id)
{
	if (!outbox || !conversation || !id)
		return -1;
	for (size_t i = 0; i < outbox->length; i++)
		if (strcmp(outbox->entries[i].conversation, conversation) == 0 &&
		    strcmp(outbox->entries[i].id, id) == 0)
			return (ssize_t)i;
	return -1;
}

int omaq_receipt_outbox_add(omaq_receipt_outbox *outbox,
			    const char *conversation, const char *id)
{
	omaq_receipt_outbox_entry *grown;
	size_t next_capacity;

	if (!outbox || !receipt_conversation_ok(conversation) ||
	    !omaq_message_id_ok(id))
		return -1;
	if (receipt_outbox_find(outbox, conversation, id) >= 0)
		return 0;
	if (outbox->length >= OMAQ_RECEIPT_OUTBOX_MAX)
		return -1;
	if (outbox->length == outbox->capacity) {
		next_capacity = outbox->capacity ? outbox->capacity * 2u : 16u;
		if (next_capacity > OMAQ_RECEIPT_OUTBOX_MAX)
			next_capacity = OMAQ_RECEIPT_OUTBOX_MAX;
		grown = realloc(outbox->entries, next_capacity * sizeof(*grown));
		if (!grown)
			return -1;
		outbox->entries = grown;
		outbox->capacity = next_capacity;
	}
	memset(&outbox->entries[outbox->length], 0,
	       sizeof(outbox->entries[outbox->length]));
	snprintf(outbox->entries[outbox->length].conversation,
		 sizeof(outbox->entries[outbox->length].conversation), "%s", conversation);
	snprintf(outbox->entries[outbox->length].id,
		 sizeof(outbox->entries[outbox->length].id), "%s", id);
	outbox->entries[outbox->length].created = (int64_t)time(NULL);
	outbox->length++;
	return 1;
}

int omaq_receipt_outbox_remove(omaq_receipt_outbox *outbox,
			       const char *conversation, const char *id)
{
	ssize_t index = receipt_outbox_find(outbox, conversation, id);

	if (index < 0)
		return 0;
	if ((size_t)index + 1u < outbox->length)
		memmove(&outbox->entries[index], &outbox->entries[index + 1],
			(outbox->length - (size_t)index - 1u) * sizeof(*outbox->entries));
	outbox->length--;
	memset(&outbox->entries[outbox->length], 0, sizeof(*outbox->entries));
	return 1;
}

static int receipt_state_path(const char *state_dir, const char *name,
			      char *out, size_t outn)
{
	if (!state_dir || !state_dir[0] || !name || !name[0] || !out || outn == 0 ||
	    snprintf(out, outn, "%s/%s", state_dir, name) >= (int)outn)
		return -1;
	return 0;
}

static int receipt_state_load(omaq_receipt_outbox *outbox, const char *state_dir,
			      const char *name)
{
	omaq_receipt_outbox loaded;
	char path[640], line[192];
	struct stat st;
	FILE *file = NULL;
	int fd = -1, rc = -1;

	if (!outbox || receipt_state_path(state_dir, name, path, sizeof(path)) != 0)
		return -1;
	omaq_receipt_outbox_init(&loaded);
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0) {
		if (errno == ENOENT) {
			omaq_receipt_outbox_destroy(outbox);
			*outbox = loaded;
			return 0;
		}
		goto done;
	}
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 ||
	    (uint64_t)st.st_size > RECEIPT_OUTBOX_BYTES || !(file = fdopen(fd, "r")))
		goto done;
	fd = -1;
	while (fgets(line, sizeof(line), file)) {
		char *tab, *created_text, *newline, *end = NULL;
		long long created;
		size_t len = strlen(line);
		if (len == 0 || line[len - 1] != '\n' ||
		    !(newline = strrchr(line, '\n')) || strchr(line, '\0') != line + len)
			goto done;
		*newline = '\0';
		tab = strchr(line, '\t');
		if (!tab || tab == line)
			goto done;
		*tab++ = '\0';
		created_text = strchr(tab, '\t');
		if (!created_text || created_text == tab || strchr(created_text + 1, '\t'))
			goto done;
		*created_text++ = '\0';
		errno = 0;
		created = strtoll(created_text, &end, 10);
		if (errno != 0 || !end || *end || created <= 0 ||
		    omaq_receipt_outbox_add(&loaded, line, tab) != 1)
			goto done;
		loaded.entries[loaded.length - 1u].created = (int64_t)created;
	}
	{
		int stream_error = ferror(file);
		int close_error = fclose(file);
		file = NULL;
		if (stream_error || close_error != 0)
			goto done;
	}
	omaq_receipt_outbox_destroy(outbox);
	*outbox = loaded;
	return 0;
done:
	if (file)
		fclose(file);
	if (fd >= 0)
		close(fd);
	omaq_receipt_outbox_destroy(&loaded);
	return rc;
}

static int receipt_state_save(const omaq_receipt_outbox *outbox,
			      const char *state_dir, const char *name)
{
	char path[640], tmp[680];
	FILE *file = NULL;
	int fd = -1, dirfd = -1, rc = -1;

	if (!outbox || outbox->length > OMAQ_RECEIPT_OUTBOX_MAX ||
	    receipt_state_path(state_dir, name, path, sizeof(path)) != 0 ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >= (int)sizeof(tmp))
		return -1;
	(void)unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0 || !(file = fdopen(fd, "w")))
		goto done;
	fd = -1;
	for (size_t i = 0; i < outbox->length; i++) {
		if (!receipt_conversation_ok(outbox->entries[i].conversation) ||
		    !omaq_message_id_ok(outbox->entries[i].id) ||
		    outbox->entries[i].created <= 0 ||
		    fprintf(file, "%s\t%s\t%lld\n", outbox->entries[i].conversation,
			    outbox->entries[i].id,
			    (long long)outbox->entries[i].created) < 0)
			goto done;
	}
	if (fflush(file) != 0 || fsync(fileno(file)) != 0)
		goto done;
	{
		int close_error = fclose(file);
		file = NULL;
		if (close_error != 0)
			goto done;
	}
	if (rename(tmp, path) != 0)
		goto done;
	dirfd = open(state_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0 || fsync(dirfd) != 0)
		goto done;
	rc = 0;
done:
	if (file)
		fclose(file);
	if (fd >= 0)
		close(fd);
	if (dirfd >= 0)
		close(dirfd);
	if (rc != 0)
		unlink(tmp);
	return rc;
}

int omaq_receipt_outbox_load(omaq_receipt_outbox *outbox, const char *state_dir)
{
	return receipt_state_load(outbox, state_dir, "read-receipts.tsv");
}

int omaq_receipt_outbox_save(const omaq_receipt_outbox *outbox, const char *state_dir)
{
	return receipt_state_save(outbox, state_dir, "read-receipts.tsv");
}

int omaq_receipt_transaction_load(omaq_receipt_outbox *transaction,
				  const char *state_dir)
{
	return receipt_state_load(transaction, state_dir, "read-transaction.tsv");
}

int omaq_receipt_transaction_save(const omaq_receipt_outbox *transaction,
				  const char *state_dir)
{
	return receipt_state_save(transaction, state_dir, "read-transaction.tsv");
}

int omaq_receipt_transaction_mark_committed(const char *state_dir)
{
	char path[640], tmp[680];
	int fd = -1, dirfd = -1, rc = -1;

	if (receipt_state_path(state_dir, "read-transaction.committed", path,
			       sizeof(path)) != 0 ||
	    snprintf(tmp, sizeof(tmp), "%s.tmp.%ld", path, (long)getpid()) >=
		(int)sizeof(tmp))
		return -1;
	(void)unlink(tmp);
	fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
	if (fd < 0)
		goto done;
	if (write(fd, "1\n", 2) != 2 || fsync(fd) != 0) {
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
	dirfd = open(state_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0 || fsync(dirfd) != 0)
		goto done;
	rc = 0;
done:
	if (fd >= 0)
		close(fd);
	if (dirfd >= 0)
		close(dirfd);
	if (rc != 0)
		unlink(tmp);
	return rc;
}

int omaq_receipt_transaction_committed(const char *state_dir)
{
	char path[640], value[3];
	struct stat st;
	int fd;
	ssize_t count;

	if (receipt_state_path(state_dir, "read-transaction.committed", path,
			       sizeof(path)) != 0)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		return errno == ENOENT ? 0 : -1;
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size != 2) {
		close(fd);
		return -1;
	}
	count = read(fd, value, sizeof(value));
	close(fd);
	return count == 2 && value[0] == '1' && value[1] == '\n' ? 1 : -1;
}

int omaq_receipt_transaction_clear(const char *state_dir)
{
	char path[640], committed[640];
	int dirfd;

	if (receipt_state_path(state_dir, "read-transaction.tsv", path, sizeof(path)) != 0 ||
	    receipt_state_path(state_dir, "read-transaction.committed", committed,
			       sizeof(committed)) != 0 ||
	    (unlink(path) != 0 && errno != ENOENT) ||
	    (unlink(committed) != 0 && errno != ENOENT))
		return -1;
	dirfd = open(state_dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (dirfd < 0)
		return -1;
	if (fsync(dirfd) != 0) {
		close(dirfd);
		return -1;
	}
	close(dirfd);
	return 0;
}
