#ifndef MICROS_SERVERS_PM_INTERNAL_H
#define MICROS_SERVERS_PM_INTERNAL_H

#include <stddef.h>

#include "servers/pm/pm_core.h"

#define MICROS_PM_INITIALIZATION_MAGIC UINT64_C(0x4d4943524f53504d)

void pm_common_zero_bytes(void *value, size_t size);
bool pm_common_bytes_are_zero(const uint8_t *bytes, size_t size);
uint64_t pm_common_read_u64_le(const uint8_t *bytes);
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

#endif
