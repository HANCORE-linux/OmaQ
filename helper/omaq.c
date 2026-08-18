/* Offline helper: lock election + JSON ops. No toxcore. */

#include "invite.h"
#include "json_io.h"
#include "message.h"
#include "roles.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *home_dir(void)
{
	const char *h = getenv("OMAQ_HOME");
	return h && h[0] ? h : NULL;
}

static const char *state_dir(void)
{
	const char *h = getenv("OMAQ_STATE");
	return h && h[0] ? h : NULL;
}

static int take_lock(void)
{
	const char *home = home_dir();
	char path[512];
	int fd;

	if (!home)
		return -1;
	if (mkdir(home, 0700) != 0 && errno != EEXIST)
		return -1;
	if (snprintf(path, sizeof(path), "%s/omaq.lock", home) >= (int)sizeof(path))
		return -1;
	fd = open(path, O_RDWR | O_CREAT, 0600);
	if (fd < 0)
		return -1;
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		close(fd);
		return 2;
	}
	/* keep fd open for the process lifetime */
	return 0;
}

static void emit(const char *s)
{
	fputs(s, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

static void emit_error(const char *code)
{
	char buf[128];
	snprintf(buf, sizeof(buf), "{\"event\":\"error\",\"code\":\"%s\"}", code);
	emit(buf);
}

static int handle_op(const omaq_op *op)
{
	if (strcmp(op->op, "status") == 0) {
		emit("{\"event\":\"snapshot\",\"unread\":0,\"conversations\":[]}");
		return 0;
	}
	if (strcmp(op->op, "invite.create") == 0) {
		if (strcmp(op->kind, "group") == 0) {
			emit_error("unsupported");
			return 0;
		}
		if (op->kind[0] && strcmp(op->kind, "direct") != 0) {
			emit_error("unsupported");
			return 0;
		}
		emit("{\"event\":\"invite\",\"kind\":\"direct\"}");
		return 0;
	}
	if (strcmp(op->op, "invite.redeem") == 0) {
		omaq_invite inv;
		if (omaq_invite_parse(op->payload, &inv) != 0) {
			emit_error("invite_expired");
			return 0;
		}
		if (inv.kind == INVITE_GROUP) {
			emit_error("unsupported");
			return 0;
		}
		if (omaq_invite_expired(&inv, (int64_t)time(NULL))) {
			emit_error("invite_expired");
			return 0;
		}
		emit("{\"event\":\"request\",\"kind\":\"direct\"}");
		return 0;
	}
	if (strcmp(op->op, "invite.revoke") == 0 ||
	    strcmp(op->op, "contact.decide") == 0 ||
	    strcmp(op->op, "contact.remove") == 0 ||
	    strcmp(op->op, "msg.send") == 0 ||
	    strcmp(op->op, "history") == 0 ||
	    strcmp(op->op, "nospam.rotate") == 0) {
		emit("{\"event\":\"snapshot\",\"unread\":0,\"conversations\":[]}");
		return 0;
	}
	if (strncmp(op->op, "group.", 6) == 0 ||
	    strncmp(op->op, "identity.", 9) == 0 ||
	    strncmp(op->op, "surface.", 8) == 0) {
		emit_error("unsupported");
		return 0;
	}
	emit_error("unsupported");
	return 0;
}

int main(int argc, char **argv)
{
	int hold = 0;
	int rc;
	char line[OMAQ_JSON_LINE_MAX];

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--hold") == 0)
			hold = 1;
	}
	if (!home_dir() || !state_dir()) {
		fprintf(stderr, "omaq: OMAQ_HOME and OMAQ_STATE required\n");
		return 1;
	}
	rc = take_lock();
	if (rc == 2)
		return 2;
	if (rc != 0)
		return 1;
	if (hold) {
		for (;;)
			pause();
	}
	while (fgets(line, sizeof(line), stdin)) {
		omaq_op op;
		size_t n = strlen(line);
		if (n && line[n - 1] == '\n')
			line[n - 1] = '\0';
		if (line[0] == '\0')
			continue;
		if (omaq_json_parse_op(line, &op) != 0) {
			emit_error("unsupported");
			continue;
		}
		handle_op(&op);
	}
	return 0;
}
