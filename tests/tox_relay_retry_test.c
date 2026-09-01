#define HAVE_TOX 1

static unsigned bootstrap_calls;
static unsigned relay_calls;
static unsigned bootstrap_mask;
static unsigned relay_mask;
static unsigned tuple_errors;

#include "../helper/tox_adapt.c"

static char temporary_home[] = "/tmp/omaq-relay-retry-XXXXXX";

static int remove_home(void)
{
	char path[sizeof(temporary_home) + 16];
	const char *names[] = { "tox.save", "tox.save.tmp" };

	if (!temporary_home[0])
		return 0;
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		snprintf(path, sizeof(path), "%s/%s", temporary_home, names[i]);
		if (unlink(path) != 0 && errno != ENOENT)
			return -1;
	}
	if (rmdir(temporary_home) != 0)
		return -1;
	temporary_home[0] = '\0';
	return 0;
}

static void cleanup_home(void)
{
	(void)remove_home();
}

static int matching_node(const char *host, uint16_t port,
			 const Tox_Dht_Id public_key, int tcp)
{
	for (size_t i = 0; i < sizeof(bootstrap_nodes) / sizeof(bootstrap_nodes[0]); i++) {
		uint8_t expected[TOX_PUBLIC_KEY_SIZE];
		uint16_t expected_port = tcp ? bootstrap_nodes[i].tcp_port :
			bootstrap_nodes[i].udp_port;

		if (hex_in(bootstrap_nodes[i].key_hex, expected, sizeof(expected)) == 0 &&
		    strcmp(host, bootstrap_nodes[i].host) == 0 && port == expected_port &&
		    memcmp(public_key, expected, sizeof(expected)) == 0)
			return (int)i;
	}
	return -1;
}

bool __wrap_tox_bootstrap(Tox *tox, const char *host, uint16_t port,
			  const Tox_Dht_Id public_key,
			  Tox_Err_Bootstrap *error)
{
	int node;

	(void)tox;
	bootstrap_calls++;
	node = matching_node(host, port, public_key, 0);
	if (node < 0)
		tuple_errors++;
	else
		bootstrap_mask |= 1u << (unsigned)node;
	if (error)
		*error = TOX_ERR_BOOTSTRAP_OK;
	return true;
}

bool __wrap_tox_add_tcp_relay(Tox *tox, const char *host, uint16_t port,
			      const Tox_Dht_Id public_key,
			      Tox_Err_Bootstrap *error)
{
	int node;

	(void)tox;
	relay_calls++;
	node = matching_node(host, port, public_key, 1);
	if (node < 0)
		tuple_errors++;
	else
		relay_mask |= 1u << (unsigned)node;
	if (error)
		*error = TOX_ERR_BOOTSTRAP_OK;
	return true;
}

static void reset_counts(void)
{
	bootstrap_calls = 0;
	relay_calls = 0;
	bootstrap_mask = 0;
	relay_mask = 0;
	tuple_errors = 0;
}

static void require_stage(const char *stage)
{
	if (bootstrap_calls != 3 || relay_calls != 3 ||
	    bootstrap_mask != 0x7 || relay_mask != 0x7 || tuple_errors != 0) {
		fprintf(stderr,
			"tox-relay-retry: %s bootstrap=%u relay=%u "
			"bootstrap_mask=0x%x relay_mask=0x%x tuple_errors=%u\n",
			stage, bootstrap_calls, relay_calls, bootstrap_mask, relay_mask,
			tuple_errors);
		exit(1);
	}
}

int main(void)
{
	struct omaq_tox *tox;
	int error = 0;

	if (!mkdtemp(temporary_home)) {
		perror("tox-relay-retry: temporary home");
		return 1;
	}
	if (atexit(cleanup_home) != 0) {
		(void)remove_home();
		fprintf(stderr, "tox-relay-retry: cannot register cleanup\n");
		return 1;
	}
	tox = omaq_tox_open(temporary_home, NULL, &error);
	if (!tox) {
		fprintf(stderr, "tox-relay-retry: open failed: %d\n", error);
		return 1;
	}
	require_stage("startup");

	reset_counts();
	tox->online = 0;
	tox->next_bootstrap = time(NULL) - 1;
	omaq_tox_iterate(tox);
	require_stage("offline periodic retry");
	if (tox->next_bootstrap < time(NULL) + 9 ||
	    tox->next_bootstrap > time(NULL) + 10) {
		fprintf(stderr, "tox-relay-retry: offline retry interval changed\n");
		omaq_tox_close(tox);
		return 1;
	}

	reset_counts();
	tox->online = 1;
	tox->next_bootstrap = time(NULL) - 1;
	omaq_tox_iterate(tox);
	require_stage("online periodic retry");
	if (tox->next_bootstrap < time(NULL) + 59 ||
	    tox->next_bootstrap > time(NULL) + 60) {
		fprintf(stderr, "tox-relay-retry: online retry interval changed\n");
		omaq_tox_close(tox);
		return 1;
	}

	omaq_tox_close(tox);
	if (remove_home() != 0) {
		perror("tox-relay-retry: cleanup");
		return 1;
	}
	puts("tox-relay-retry: ok startup=3/3 offline=3/3 online=3/3");
	return 0;
}
