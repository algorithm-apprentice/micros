#include "servers/pm/pm_internal.h"

#include <stddef.h>

enum micros_pm_model_error micros_pm_table_initialize(
    struct micros_pm_table *table
)
{
    size_t index;

    if (table == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    pm_common_zero_bytes(table, sizeof(*table));
    table->initialization_magic = MICROS_PM_INITIALIZATION_MAGIC;
    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        table->records[index].endpoint = MICROS_ENDPOINT_NONE;
        table->records[index].spawn.prepared_endpoint
            = MICROS_ENDPOINT_NONE;
    }
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_enable_runtime(
    struct micros_pm_table *table,
    const struct micros_ipc_message *message
)
{
    enum micros_pm_model_error error;

    if (table == NULL || message == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (table->runtime_enabled) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (
        message->source != MICROS_ENDPOINT_NONE
        || message->type != MICROS_IPC_TYPE_KERNEL_NOTIFICATION
        || message->reply_token != 0
        || pm_common_read_u64_le(&message->payload[0])
            != MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED
        || !pm_common_bytes_are_zero(&message->payload[8], 40)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }

    table->runtime_enabled = true;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_process_resolve(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle handle,
    const struct micros_pm_process_record **record
)
{
    enum micros_pm_model_error error;

    if (table == NULL || record == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    return pm_common_resolve_record(table, handle, record);
}

enum micros_pm_model_error micros_pm_find_running_by_endpoint(
    const struct micros_pm_table *table,
    micros_endpoint_t endpoint,
    struct micros_pm_process_handle *process,
    uint64_t *pid
)
{
    struct micros_pm_process_handle found;
    uint64_t found_pid;
    uint16_t index;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || process == NULL
        || pid == NULL
        || !pm_common_endpoint_is_concrete(endpoint)
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record
            = &table->records[index];

        if (
            record->state == MICROS_PM_PROCESS_RUNNING
            && record->endpoint == endpoint
        ) {
            found = pm_common_record_handle(index, record);
            found_pid = record->pid;
            *process = found;
            *pid = found_pid;
            return MICROS_PM_MODEL_OK;
        }
    }
    return MICROS_PM_MODEL_ERROR_STALE;
}
