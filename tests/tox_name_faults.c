/* Link-time faults for the native nickname IPC test, never the shipped helper. */
#define _DEFAULT_SOURCE
#include "../helper/tox_adapt.h"
#include <tox/tox.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct omaq_tox *target;
static unsigned group_calls;
static int send_fault_used, save_fault_used;

int __real_main(int, char **);
int __wrap_main(int argc, char **argv)
{
	const char *root = getenv("OMAQ_NAME_TEST_ROOT");
	const char *home = getenv("OMAQ_HOME"), *state = getenv("OMAQ_STATE");
	struct stat st;
	char path[256];

	/* Refuse accidental execution with host identity paths. */
	if (!root || strncmp(root, "/tmp/omaq-name-ipc-", 19) != 0 ||
	    strchr(root + 19, '/') || strlen(root) > 180 ||
	    !getenv("HOME") || strcmp(getenv("HOME"), root) != 0 ||
	    lstat(root, &st) != 0 || !S_ISDIR(st.st_mode) ||
	    st.st_uid != geteuid() || (st.st_mode & 0777) != 0700)
		return 98;
	snprintf(path, sizeof(path), "%s/data", root);
	if (!home || strcmp(home, path) != 0)
		return 98;
	snprintf(path, sizeof(path), "%s/state", root);
	if (!state || strcmp(state, path) != 0)
		return 98;
	return __real_main(argc, argv);
}

bool __wrap_tox_bootstrap(Tox *tox, const char *host, uint16_t port,
                         const Tox_Dht_Id key, Tox_Err_Bootstrap *error)
{
	(void)tox; (void)host; (void)port; (void)key;
	if (error)
		*error = TOX_ERR_BOOTSTRAP_OK;
	return true;
}

bool __wrap_tox_add_tcp_relay(Tox *tox, const char *host, uint16_t port,
                             const Tox_Dht_Id key, Tox_Err_Bootstrap *error)
{
	return __wrap_tox_bootstrap(tox, host, port, key, error);
}

bool __real_tox_group_self_set_name(Tox *, Tox_Group_Number, const uint8_t *,
                                  size_t, Tox_Err_Group_Self_Name_Set *);
bool __wrap_tox_group_self_set_name(Tox *tox, Tox_Group_Number group,
                                  const uint8_t *name, size_t length,
                                  Tox_Err_Group_Self_Name_Set *error)
{
	bool result = __real_tox_group_self_set_name(tox, group, name, length, error);
	group_calls++;
	if (!send_fault_used && length == strlen("Partial name") &&
	    memcmp(name, "Partial name", length) == 0) {
		send_fault_used = 1;
		*error = TOX_ERR_GROUP_SELF_NAME_SET_FAIL_SEND;
		result = false;
	}
	if (!save_fault_used && length == strlen("Second save fault") &&
	    memcmp(name, "Second save fault", length) == 0) {
		save_fault_used = 1;
		omaq_tox_test_fail_before_primary(target);
	}
	return result;
}

int __real_omaq_tox_set_name(struct omaq_tox *, const char *);
int __wrap_omaq_tox_set_name(struct omaq_tox *tox, const char *name)
{
	char before[TOX_MAX_NAME_LENGTH + 1], stored[TOX_MAX_NAME_LENGTH + 1];
	struct omaq_tox *probe;
	int error = 0, result, stored_matches;

	if (omaq_tox_self_name(tox, before, sizeof(before)) != 0)
		exit(97);
	target = tox;
	group_calls = 0;
	if (strcmp(name, "First save fault") == 0)
		omaq_tox_test_fail_before_primary(tox);
	if (strcmp(name, "Primary uncertain") == 0)
		omaq_tox_test_fail_primary_fsync(tox);
	result = __real_omaq_tox_set_name(tox, name);
	/* Read the published save through a separate, non-iterated native object.
	 * It has no bootstrap/relay network and is discarded without saving. */
	probe = omaq_tox_open(getenv("OMAQ_HOME"), NULL, &error);
	if (!probe || omaq_tox_self_name(probe, stored, sizeof(stored)) != 0)
		exit(97);
	stored_matches = strcmp(stored, strcmp(name, "First save fault") == 0 ? before : name) == 0;
	omaq_tox_discard(probe);
	printf("{\"event\":\"test.nickname.audit\",\"result\":%d,\"groupCalls\":%u,"
	       "\"storedMatches\":%s}\n", result, group_calls, stored_matches ? "true" : "false");
	fflush(stdout);
	return result;
}
