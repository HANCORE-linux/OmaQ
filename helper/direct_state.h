#ifndef OMAQ_DIRECT_STATE_H
#define OMAQ_DIRECT_STATE_H

#include <stddef.h>
#include <stdint.h>

#define OMAQ_DIRECT_STATE_ID_MAX 67
#define OMAQ_DIRECT_STATE_FRIEND_MAX 64

typedef struct {
    uint32_t number;
    char key[65];
} omaq_direct_state_friend;

int omaq_direct_state_id(const char *public_key, char *out, size_t out_size);
int omaq_direct_state_migrate(const char *home, const char *legacy_number,
                              const char *public_key);
int omaq_direct_state_archive_legacy(const char *home, const char *legacy_number);
int omaq_direct_state_bound_id(const char *home, const char *legacy_number,
                               char *out, size_t out_size);
int omaq_direct_state_add_begin(const char *home, const char *key, const char *pin);
int omaq_direct_state_add_pending(const char *home, char key[65], char pin[65]);
int omaq_direct_state_add_finish(const char *home);
int omaq_direct_state_remove_begin(const char *home, const char *key);
int omaq_direct_state_remove_pending(const char *home, char key[65]);
int omaq_direct_state_remove_finish(const char *home);
int omaq_direct_state_reconcile(const char *home,
                                const omaq_direct_state_friend *current,
                                size_t current_count, int *reinvite_required);
int omaq_direct_state_reconcile_removed(const char *home,
                                        const omaq_direct_state_friend *current,
                                        size_t current_count,
                                        const char *removed_key,
                                        int *reinvite_required);

#endif
