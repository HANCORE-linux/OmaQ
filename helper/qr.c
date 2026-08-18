#define _DEFAULT_SOURCE
#include "qr.h"
#include "invite.h"

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

int omaq_qr_path_ok(const char *path)
{
	size_t n;

	if (!path || path[0] != '/')
		return -1;
	n = strlen(path);
	if (n < 5 || n >= 512)
		return -1;
	if (strcmp(path + n - 4, ".png") != 0)
		return -1;
	if (strchr(path, '\n') || strchr(path, '\r'))
		return -1;
	if (strstr(path, ".."))
		return -1;
	return 0;
}

int omaq_qr_write_png(const char *url, const char *path)
{
	omaq_invite inv;
	const char *bin;
	pid_t pid;
	int st;

	if (!url || omaq_invite_parse(url, &inv) != 0)
		return -1;
	if (omaq_qr_path_ok(path) != 0)
		return -1;
	bin = getenv("OMAQ_QRENCODE");
	if (!bin || !bin[0])
		bin = "/usr/bin/qrencode";
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		execl(bin, "qrencode", "-o", path, "-t", "PNG", "-s", "6", "--",
		      url, (char *)NULL);
		_exit(127);
	}
	if (waitpid(pid, &st, 0) != pid)
		return -1;
	if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
		return -1;
	if (chmod(path, 0600) != 0)
		return -1;
	return 0;
}
