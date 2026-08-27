#ifndef OMAQ_IDENTITY_GUARD_H
#define OMAQ_IDENTITY_GUARD_H

#include <stddef.h>

#define OMAQ_IDENTITY_GUARD_FRESH 0
#define OMAQ_IDENTITY_GUARD_EXISTING 1
#define OMAQ_IDENTITY_GUARD_RESTORED 2
#define OMAQ_IDENTITY_GUARD_INVALID (-1)
#define OMAQ_IDENTITY_GUARD_MISSING (-2)
#define OMAQ_IDENTITY_GUARD_MISMATCH (-3)
#define OMAQ_IDENTITY_GUARD_PUBLISHED (-4)

int omaq_identity_guard_preflight(const char *home, const char *state);
int omaq_identity_guard_expected(const char *state, char fingerprint[65]);
int omaq_identity_guard_verify_or_create(const char *state, const char *fingerprint);
int omaq_identity_guard_replace(const char *state, const char *expected,
                                const char *replacement);
int omaq_identity_guard_restore(const char *state, const char *fingerprint);
int omaq_identity_guard_finish_recovery(const char *state);
int omaq_identity_guard_reject_recovery(const char *home, const char *state);
int omaq_identity_guard_prepare_repair(const char *state);
int omaq_identity_primary_uncertain_present(const char *state);
int omaq_identity_primary_uncertain_persist(const char *state);
int omaq_identity_primary_uncertain_clear(const char *state);
int omaq_identity_primary_ack_present(const char *state);
int omaq_identity_primary_ack_persist(const char *state);
int omaq_identity_primary_ack_clear(const char *state);
int omaq_identity_recovery_stale_present(const char *state);
int omaq_identity_recovery_stale_persist(const char *state);
int omaq_identity_recovery_stale_clear(const char *state);
#ifdef OMAQ_IDENTITY_GUARD_TEST
void omaq_identity_guard_test_fail_directory_fsync(void);
void omaq_identity_guard_test_fail_recovery_write(void);
#endif
int omaq_identity_recovery_write(const char *state, const void *data, size_t length);

#endif
