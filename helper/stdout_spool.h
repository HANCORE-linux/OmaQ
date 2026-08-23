#ifndef OMAQ_STDOUT_SPOOL_H
#define OMAQ_STDOUT_SPOOL_H

#include <stddef.h>
#include <sys/types.h>

#define OMAQ_STDOUT_SPOOL_FILE "stdout-critical.spool"
#define OMAQ_STDOUT_CURSOR_FILE "stdout-critical.cursor"
#define OMAQ_STDOUT_RECORD_MAX (8u * 1024u * 1024u)
#ifndef OMAQ_STDOUT_SPOOL_MAX
#define OMAQ_STDOUT_SPOOL_MAX (256u * 1024u * 1024u)
#endif

typedef struct omaq_stdout_spool omaq_stdout_spool;
typedef ssize_t (*omaq_stdout_write_fn)(void *ctx, int fd, const void *buf, size_t len);

enum {
	OMAQ_STDOUT_FLUSH_SPOOL_ERROR = -2,
	OMAQ_STDOUT_FLUSH_OUTPUT_ERROR = -1,
	OMAQ_STDOUT_FLUSH_IDLE = 0,
	OMAQ_STDOUT_FLUSH_PROGRESS = 1
};

/* Open the persistent critical-event FIFO under state_dir. */
omaq_stdout_spool *omaq_stdout_spool_open(const char *state_dir, int output_fd);

/* Append one complete JSON event. The terminating JSONL newline is added here. */
int omaq_stdout_spool_append(omaq_stdout_spool *spool, const char *event);

/* Nonzero while critical bytes remain to be written to output_fd. */
int omaq_stdout_spool_pending(const omaq_stdout_spool *spool);

/* Perform at most one nonblocking output write. */
int omaq_stdout_spool_flush(omaq_stdout_spool *spool);

/* Test seam for partial writes and EAGAIN. NULL restores write(2). */
void omaq_stdout_spool_set_writer(omaq_stdout_spool *spool,
				  omaq_stdout_write_fn writer, void *ctx);

void omaq_stdout_spool_close(omaq_stdout_spool *spool);

#endif
