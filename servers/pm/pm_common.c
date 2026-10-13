#include "servers/pm/pm_internal.h"

void pm_common_zero_bytes(void *value, size_t size)
{
    uint8_t *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

bool pm_common_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

uint64_t pm_common_read_u64_le(const uint8_t *bytes)
{
    uint64_t value = 0;
    size_t index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

void pm_common_write_u64_le(uint8_t *bytes, uint64_t value)
{
    size_t index;

    for (index = 0; index < 8; ++index) {
        bytes[index] = (uint8_t)(value >> (index * 8));
    }
}

bool pm_common_handle_is_none(struct micros_pm_process_handle handle)
{
    return (
        handle.slot == 0
        && handle.reserved == 0
        && handle.generation == 0
    );
}

bool pm_common_handles_equal(
    struct micros_pm_process_handle left,
    struct micros_pm_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.reserved == right.reserved
        && left.generation == right.generation
    );
}

bool pm_common_endpoint_is_concrete(micros_endpoint_t endpoint)
{
    uint32_t slot_mask =
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) - 1;
    uint32_t slot = endpoint & slot_mask;
    uint32_t generation = endpoint >> MICROS_ENDPOINT_SLOT_BITS;

    return (
        endpoint != MICROS_ENDPOINT_NONE
        && endpoint != MICROS_ENDPOINT_ANY
        && slot < MICROS_PROCESS_CAPACITY
        && generation != 0
        && generation <= MICROS_ENDPOINT_GENERATION_MAX
    );
}

bool pm_common_endpoint_is_published_elsewhere(
    const struct micros_pm_table *table,
    micros_endpoint_t endpoint,
    uint16_t excluded_slot
)
{
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            index != excluded_slot
            && table->records[index].endpoint == endpoint
            && (
                table->records[index].state
                    == MICROS_PM_PROCESS_RUNNING
                || table->records[index].state
                    == MICROS_PM_PROCESS_EXITING
            )
        ) {
            return true;
        }
    }
    return false;
}

void pm_common_clear_record(
    struct micros_pm_process_record *record,
    enum micros_pm_process_state state
)
{
    uint32_t generation = record->generation;

    pm_common_zero_bytes(record, sizeof(*record));
    record->state = state;
    record->generation = generation;
    record->endpoint = MICROS_ENDPOINT_NONE;
    record->spawn.prepared_endpoint = MICROS_ENDPOINT_NONE;
}

struct micros_pm_process_handle pm_common_record_handle(
    uint16_t slot,
    const struct micros_pm_process_record *record
)
{
    struct micros_pm_process_handle handle;

    pm_common_zero_bytes(&handle, sizeof(handle));
    handle.slot = slot;
    handle.generation = record->generation;
    return handle;
}

enum micros_pm_model_error pm_common_resolve_record(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    const struct micros_pm_process_record **record
)
{
    const struct micros_pm_process_record *candidate;

    if (
        table == NULL
        || record == NULL
        || handle.reserved != 0
        || handle.generation == 0
        || handle.slot >= MICROS_PM_PROCESS_CAPACITY
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    candidate = &table->records[handle.slot];
    if (
        candidate->state == MICROS_PM_PROCESS_FREE
        || candidate->state == MICROS_PM_PROCESS_QUARANTINED
        || candidate->generation != handle.generation
    ) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    *record = candidate;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error pm_common_resolve_spawning_record(
    struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    enum micros_pm_spawn_stage expected_stage,
    struct micros_pm_process_record **record
)
{
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    error = pm_common_resolve_record(table, handle, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_SPAWNING
        || resolved->spawn_stage != expected_stage
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    *record = &table->records[handle.slot];
    return MICROS_PM_MODEL_OK;
}

void pm_common_release_record(struct micros_pm_process_record *record)
{
    if (record->generation == UINT32_MAX) {
        pm_common_clear_record(record, MICROS_PM_PROCESS_QUARANTINED);
    } else {
        pm_common_clear_record(record, MICROS_PM_PROCESS_FREE);
    }
}

void pm_common_clear_wait(struct micros_pm_wait_state *wait)
{
    pm_common_zero_bytes(wait, sizeof(*wait));
}

bool pm_common_wait_matches_pid(
    const struct micros_pm_wait_state *wait,
    uint64_t pid
)
{
    return wait->selector == 0 || wait->selector == pid;
}

bool pm_common_record_is_child_of(
    const struct micros_pm_process_record *record,
    struct micros_pm_process_handle parent
)
{
    return (
        record->state != MICROS_PM_PROCESS_FREE
        && record->state != MICROS_PM_PROCESS_QUARANTINED
        && pm_common_handles_equal(record->parent, parent)
    );
}

bool pm_common_has_matching_child(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t selector
)
{
    size_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record
            = &table->records[index];

        if (
            pm_common_record_is_child_of(record, parent)
            && (selector == 0 || selector == record->pid)
        ) {
            return true;
        }
    }
    return false;
}
