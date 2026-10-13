#ifndef MICROS_SERVERS_PM_INTERNAL_H
#define MICROS_SERVERS_PM_INTERNAL_H

#include <stddef.h>

#include "servers/pm/pm_core.h"

#define MICROS_PM_INITIALIZATION_MAGIC UINT64_C(0x4d4943524f53504d)

struct pm_spawn_activation_plan {
    struct micros_pm_table *table;
    struct micros_pm_process_record *record;
    struct micros_pm_process_handle child;
    micros_endpoint_t endpoint;
    bool init_candidate;
    uint64_t parent_reply_token;
    uint64_t load_token;
    uint64_t mapping_generation;
};

struct pm_exit_begin_plan {
    struct micros_pm_table *table;
    struct micros_pm_process_record *record;
    struct micros_pm_process_handle process;
    struct micros_pm_process_handle reaper;
    enum micros_pm_exit_kind exit_kind;
    int32_t exit_code;
};

void pm_common_zero_bytes(void *value, size_t size);
bool pm_common_bytes_are_zero(const uint8_t *bytes, size_t size);
uint64_t pm_common_read_u64_le(const uint8_t *bytes);
void pm_common_write_u64_le(uint8_t *bytes, uint64_t value);
bool pm_common_handle_is_none(struct micros_pm_process_handle handle);
bool pm_common_handles_equal(
    struct micros_pm_process_handle left,
    struct micros_pm_process_handle right
);
bool pm_common_endpoint_is_concrete(micros_endpoint_t endpoint);
bool pm_common_endpoint_is_published_elsewhere(
    const struct micros_pm_table *table,
    micros_endpoint_t endpoint,
    uint16_t excluded_slot
);
void pm_common_clear_record(
    struct micros_pm_process_record *record,
    enum micros_pm_process_state state
);
struct micros_pm_process_handle pm_common_record_handle(
    uint16_t slot,
    const struct micros_pm_process_record *record
);
enum micros_pm_model_error pm_common_resolve_record(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    const struct micros_pm_process_record **record
);
enum micros_pm_model_error pm_common_resolve_spawning_record(
    struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    enum micros_pm_spawn_stage expected_stage,
    struct micros_pm_process_record **record
);
void pm_common_release_record(struct micros_pm_process_record *record);
void pm_common_clear_wait(struct micros_pm_wait_state *wait);
bool pm_common_wait_matches_pid(
    const struct micros_pm_wait_state *wait,
    uint64_t pid
);
bool pm_common_record_is_child_of(
    const struct micros_pm_process_record *record,
    struct micros_pm_process_handle parent
);
bool pm_common_has_matching_child(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t selector
);
bool pm_wait_stage_result(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    struct micros_pm_wait_outcome *outcome
);
enum micros_pm_model_error pm_spawn_prepare_activation(
    struct micros_pm_table *table,
    struct micros_pm_process_handle child,
    micros_endpoint_t endpoint,
    struct pm_spawn_activation_plan *plan
);
void pm_spawn_commit_activation(
    const struct pm_spawn_activation_plan *plan,
    uint64_t *reply_token
);
enum micros_pm_model_error pm_exit_prepare_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    enum micros_pm_exit_kind exit_kind,
    int32_t exit_code,
    struct pm_exit_begin_plan *plan
);
void pm_exit_commit_begin(
    const struct pm_exit_begin_plan *plan,
    uint64_t transaction,
    struct micros_pm_exit_effects *effects
);

#endif
