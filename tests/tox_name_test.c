#define HAVE_TOX 1
#define OMAQ_TOX_TEST 1

#include "../helper/tox_adapt.c"
#include "../helper/group.h"

/* A failed regression must still run the private fixture's cleanup. */
#undef assert
#define assert(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "tox-name:%d: %s\n", __LINE__, #condition); \
		exit(1); \
	} \
} while (0)

static unsigned group_calls;
static unsigned fail_group_call;
static unsigned fail_save_after_call;
static char temporary_home[] = "/tmp/omaq-tox-name-XXXXXX";
static struct omaq_tox *active;

bool __real_tox_group_self_set_name(Tox *, Tox_Group_Number,
                                  const uint8_t *, size_t,
                                  Tox_Err_Group_Self_Name_Set *);

bool __wrap_tox_group_self_set_name(Tox *tox, Tox_Group_Number group,
                                  const uint8_t *name, size_t length,
                                  Tox_Err_Group_Self_Name_Set *error)
{
	bool result;

	group_calls++;
	result = __real_tox_group_self_set_name(tox, group, name, length, error);
	/* toxcore may update its local group name before failing to broadcast. */
	if (group_calls == fail_group_call) {
		*error = TOX_ERR_GROUP_SELF_NAME_SET_FAIL_SEND;
		result = false;
	}
	if (group_calls == fail_save_after_call)
		omaq_tox_test_fail_before_primary(active);
	return result;
}

/* Native local Tox objects, but no bootstrap/DNS/relay traffic in this test. */
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

static void cleanup(void)
{
	char path[sizeof(temporary_home) + 32];
	const char *names[] = { "tox.save", "tox.save.tmp" };

	if (active) {
		omaq_tox_close(active);
		active = NULL;
	}
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", temporary_home, names[i]);
		(void)unlink(path);
	}
	(void)rmdir(temporary_home);
}

static void self_name_is(const char *expected)
{
	char name[TOX_MAX_NAME_LENGTH + 1];
	assert(omaq_tox_self_name(active, name, sizeof(name)) == 0);
	assert(strcmp(name, expected) == 0);
}

static void group_name_is(uint32_t group, const char *expected)
{
	uint8_t name[TOX_MAX_NAME_LENGTH + 1] = {0};
	Tox_Err_Group_Self_Query error = TOX_ERR_GROUP_SELF_QUERY_OK;
	size_t length = tox_group_self_get_name_size(active->tox, group, &error);

	assert(error == TOX_ERR_GROUP_SELF_QUERY_OK && length <= TOX_MAX_NAME_LENGTH);
	assert(tox_group_self_get_name(active->tox, group, name, &error));
	assert(error == TOX_ERR_GROUP_SELF_QUERY_OK);
	assert(length == strlen(expected) && memcmp(name, expected, length) == 0);
}

static uint32_t joining_group(void)
{
	uint8_t id[TOX_GROUP_CHAT_ID_SIZE];
	Tox_Err_Group_Join error = TOX_ERR_GROUP_JOIN_OK;
	Tox_Err_Group_State_Query query_error = TOX_ERR_GROUP_STATE_QUERY_OK;
	uint32_t group;

	/* Generate a valid native chat key, then leave its only member. No peer
	 * can supply shared state when this isolated instance tries to join it. */
	assert(omaq_tox_group_new(active, "Pending fixture", &group) == 0);
	assert(tox_group_get_chat_id(active->tox, group, id, &query_error));
	assert(omaq_tox_group_leave(active, group) == 0);
	omaq_tox_iterate(active);
	group = tox_group_join(active->tox, id, (const uint8_t *)"Joining",
			       7, NULL, 0, &error);
	assert(error == TOX_ERR_GROUP_JOIN_OK && group != UINT32_MAX);
	return group;
}

int main(void)
{
	uint32_t groups[OMAQ_GROUPS_MAX + 1], pending;
	int error = 0;

	assert(mkdtemp(temporary_home));
	assert(atexit(cleanup) == 0);
	active = omaq_tox_open(temporary_home, NULL, &error);
	assert(active);
	pending = joining_group();
	assert(tox_group_get_number_groups(active->tox) == 0);
	assert(omaq_tox_set_name(active, "Pending only") == 0 && group_calls == 1);
	group_name_is(pending, "Pending only");
	assert(omaq_tox_group_leave(active, pending) == 0);
	omaq_tox_iterate(active);
	group_calls = 0;
	assert(omaq_tox_set_name(active, "Before") == 0 && group_calls == 0);
	for (size_t i = 0; i < 2; i++)
		assert(omaq_tox_group_new(active, "Name test", &groups[i]) == 0);

	assert(omaq_tox_set_name(active, "After") == 0);
	assert(group_calls == 2);
	self_name_is("After");
	for (size_t i = 0; i < 2; i++)
		group_name_is(groups[i], "After");

	group_calls = 0;
	assert(omaq_tox_set_name(active, "") == OMAQ_TOX_NAME_INVALID);
	assert(omaq_tox_set_name(active, "1234567890123456789") == OMAQ_TOX_NAME_INVALID);
	assert(omaq_tox_set_name(active, "bad\xff") == OMAQ_TOX_NAME_INVALID);
	assert(group_calls == 0);
	self_name_is("After");

	omaq_tox_test_fail_before_primary(active);
	assert(omaq_tox_set_name(active, "Not saved") == OMAQ_TOX_NAME_SAVE_FAILED);
	assert(group_calls == 0);
	self_name_is("After");
	for (size_t i = 0; i < 2; i++)
		group_name_is(groups[i], "After");

	fail_group_call = 1;
	assert(omaq_tox_set_name(active, "Partial") == OMAQ_TOX_NAME_GROUPS_UNCONFIRMED);
	assert(group_calls == 2); /* One failure must not skip the remaining groups. */
	self_name_is("Partial");
	for (size_t i = 0; i < 2; i++)
		group_name_is(groups[i], "Partial");
	fail_group_call = 0;
	group_calls = 0;
	assert(omaq_tox_set_name(active, "Partial") == 0 && group_calls == 2);

	group_calls = 0;
	fail_save_after_call = 2;
	assert(omaq_tox_set_name(active, "Save warning") == OMAQ_TOX_NAME_GROUPS_UNCONFIRMED);
	self_name_is("Save warning");
	fail_save_after_call = 0;
	group_calls = 0;
	assert(omaq_tox_set_name(active, "Save warning") == 0 && group_calls == 2);

	/* Sparse native group numbers and the complete supported group bound. */
	assert(omaq_tox_group_leave(active, groups[0]) == 0);
	omaq_tox_iterate(active); /* Native deletion completes during iteration. */
	assert(tox_group_get_number_groups(active->tox) == 1);
	group_calls = 0;
	assert(omaq_tox_set_name(active, "Sparse") == 0 && group_calls == 1);
	group_name_is(groups[1], "Sparse");
	pending = joining_group();
	assert(pending < groups[1] && tox_group_get_number_groups(active->tox) == 1);
	group_calls = 0;
	assert(omaq_tox_set_name(active, "Joining rename") == 0 && group_calls == 2);
	group_name_is(pending, "Joining rename");
	group_name_is(groups[1], "Joining rename");
	assert(omaq_tox_group_leave(active, pending) == 0);
	omaq_tox_iterate(active);
	for (size_t i = 0; i < OMAQ_GROUPS_MAX - 1; i++)
		assert(omaq_tox_group_new(active, "More names", &groups[i + 2]) == 0);
	group_calls = 0;
	assert(omaq_tox_set_name(active, "\xc3\x84lice renamed") == 0);
	assert(group_calls == OMAQ_GROUPS_MAX);
	for (size_t i = 1; i <= OMAQ_GROUPS_MAX; i++)
		group_name_is(groups[i], "\xc3\x84lice renamed");
	pending = joining_group();
	assert(tox_group_get_number_groups(active->tox) == OMAQ_GROUPS_MAX);
	group_calls = 0;
	assert(omaq_tox_set_name(active, "Bound warning") == OMAQ_TOX_NAME_GROUPS_UNCONFIRMED);
	assert(group_calls == 0);
	self_name_is("Bound warning");

	/* Uncertain primary publication is an error, not a fictitious rollback. */
	omaq_tox_test_fail_primary_fsync(active);
	assert(omaq_tox_set_name(active, "Uncertain") == OMAQ_TOX_NAME_SAVE_FAILED);
	assert(omaq_tox_primary_uncertain(active) && group_calls == 0);
	self_name_is("Uncertain");
	assert(omaq_tox_set_name(active, "Blocked") == OMAQ_TOX_NAME_SAVE_FAILED);
	self_name_is("Uncertain");

	cleanup();
	puts("tox-name: ok");
	return 0;
}
