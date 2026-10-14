#include "servers/pm/pm_internal.h"

static bool spawn_resources_are_valid(
    enum micros_pm_spawn_stage stage,
    const struct micros_pm_spawn_resources *resources
)
{
    bool kernel_process;
    bool image;
    bool mappings;
    bool descriptors;
    bool execution;
    bool committed;

    if (
        stage < MICROS_PM_SPAWN_PM_RECORD_RESERVED
        || stage > MICROS_PM_SPAWN_PM_VFS_COMMITTED
        || resources == NULL
        || resources->reserved0 != 0
        || resources->reserved != 0
        || (
            resources->rollback_parent_lost
            && !resources->rollback_active
        )
    ) {
        return false;
    }

    kernel_process = stage >= MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED;
    image = stage >= MICROS_PM_SPAWN_VFS_IMAGE_PREPARED;
    mappings = stage >= MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
    descriptors = stage >= MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED;
    execution = stage >= MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED;
    committed = stage >= MICROS_PM_SPAWN_PM_VFS_COMMITTED;

    return (
        (kernel_process == (resources->kernel_transaction != 0))
        && (image == (resources->vfs_image_token != 0))
        && (mappings == (resources->vm_load_token != 0))
        && (mappings == (resources->mapping_generation != 0))
        && (descriptors == (resources->vfs_descriptor_token != 0))
        && (execution == (resources->kernel_prepared_token != 0))
        && (committed == (resources->pm_vfs_commit_token != 0))
        && (
            execution
                ? pm_common_endpoint_is_concrete(resources->prepared_endpoint)
                : resources->prepared_endpoint == MICROS_ENDPOINT_NONE
        )
    );
}

static bool spawn_is_clear(
    const struct micros_pm_process_record *record
)
{
    return (
        record->spawn_stage == MICROS_PM_SPAWN_NONE
        && record->spawn.parent_reply_token == 0
        && record->spawn.kernel_transaction == 0
        && record->spawn.vfs_image_token == 0
        && record->spawn.vm_load_token == 0
        && record->spawn.mapping_generation == 0
        && record->spawn.vfs_descriptor_token == 0
        && record->spawn.kernel_prepared_token == 0
        && record->spawn.pm_vfs_commit_token == 0
        && record->spawn.prepared_endpoint == MICROS_ENDPOINT_NONE
        && !record->spawn.rollback_active
        && !record->spawn.rollback_parent_lost
        && record->spawn.reserved0 == 0
        && record->spawn.reserved == 0
    );
}

static bool exit_is_clear(
    const struct micros_pm_process_record *record
)
{
    return (
        record->exit_stage == MICROS_PM_EXIT_STAGE_NONE
        && record->exit_kind == MICROS_PM_EXIT_NONE
        && record->exit_code == 0
        && record->reserved1 == 0
        && record->exit_transaction == 0
    );
}

static bool wait_is_clear(const struct micros_pm_wait_state *wait)
{
    return (
        !wait->active
        && !wait->result_ready
        && wait->reserved == 0
        && wait->flags == 0
        && wait->selector == 0
        && wait->reply_token == 0
        && pm_common_handle_is_none(wait->pending_child)
    );
}

struct pm_validation_counts {
    size_t spawning;
    size_t reaper;
    size_t non_init;
    size_t active;
};

static bool validation_header_is_valid(const struct micros_pm_table *table)
{
    return (
        table != NULL
        && table->initialization_magic == MICROS_PM_INITIALIZATION_MAGIC
        && pm_common_bytes_are_zero(table->reserved0, sizeof(table->reserved0))
        && table->reaper.reserved == 0
        && table->pid_exhausted == (table->last_pid == UINT64_MAX)
        && (table->init_attempted || table->last_pid == 0)
    );
}

static bool validation_reserved_fields_are_clear(
    const struct micros_pm_process_record *record
)
{
    return (
        record->reserved0 == 0
        && record->parent.reserved == 0
        && record->spawn.reserved == 0
        && record->reserved1 == 0
        && record->wait.reserved == 0
        && record->wait.pending_child.reserved == 0
        && pm_common_bytes_are_zero(record->reserved2, sizeof(record->reserved2))
    );
}

static bool validation_mapping_identity_is_valid(
    const struct micros_pm_process_record *record
)
{
    bool mapped = (
        record->state == MICROS_PM_PROCESS_RUNNING
        || (
            record->state == MICROS_PM_PROCESS_EXITING
            && (
                record->exit_stage == MICROS_PM_EXIT_STAGE_STOPPED
                || record->exit_stage == MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
            )
        )
    );

    return mapped
        ? record->load_token != 0 && record->mapping_generation != 0
        : record->load_token == 0 && record->mapping_generation == 0;
}

static bool validation_released_record_is_valid(
    const struct micros_pm_process_record *record
)
{
    return (
        record->pid == 0
        && pm_common_handle_is_none(record->parent)
        && !record->init_candidate
        && !record->reaper
        && record->endpoint == MICROS_ENDPOINT_NONE
        && spawn_is_clear(record)
        && exit_is_clear(record)
        && wait_is_clear(&record->wait)
        && !record->reap_pending
        && (
            record->state != MICROS_PM_PROCESS_FREE
            || record->generation != UINT32_MAX
        )
        && (
            record->state != MICROS_PM_PROCESS_QUARANTINED
            || record->generation == UINT32_MAX
        )
    );
}

static bool validation_identity_is_unique(
    const struct micros_pm_table *table,
    const struct micros_pm_process_record *record,
    size_t index
)
{
    size_t other;

    for (other = index + 1; other < MICROS_PM_PROCESS_CAPACITY; ++other) {
        const struct micros_pm_process_record *candidate = &table->records[other];

        if (
            candidate->state != MICROS_PM_PROCESS_FREE
            && candidate->state != MICROS_PM_PROCESS_QUARANTINED
            && candidate->pid == record->pid
        ) {
            return false;
        }
        if (
            pm_common_endpoint_is_concrete(record->endpoint)
            && pm_common_endpoint_is_concrete(candidate->endpoint)
            && candidate->endpoint == record->endpoint
        ) {
            return false;
        }
    }
    return true;
}

static bool validation_spawning_record_is_valid(
    const struct micros_pm_table *table,
    const struct micros_pm_process_record *record,
    size_t index
)
{
    return (
        !record->reaper
        && record->endpoint == MICROS_ENDPOINT_NONE
        && exit_is_clear(record)
        && wait_is_clear(&record->wait)
        && !record->reap_pending
        && spawn_resources_are_valid(record->spawn_stage, &record->spawn)
        && (
            record->spawn_stage < MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED
            || !pm_common_endpoint_is_published_elsewhere(
                table, record->spawn.prepared_endpoint, (uint16_t)index
            )
        )
        && (
            !record->init_candidate
            || (
                record->pid == 1
                && pm_common_handle_is_none(record->parent)
                && record->spawn.parent_reply_token == 0
            )
        )
        && (
            record->init_candidate
            || (
                !pm_common_handle_is_none(record->parent)
                && record->spawn.parent_reply_token != 0
            )
        )
        && (
            !record->spawn.rollback_active
            || record->spawn_stage != MICROS_PM_SPAWN_PM_RECORD_RESERVED
        )
    );
}

static bool validation_running_record_is_valid(
    const struct micros_pm_process_record *record
)
{
    return (
        !record->init_candidate
        && pm_common_endpoint_is_concrete(record->endpoint)
        && spawn_is_clear(record)
        && exit_is_clear(record)
        && !record->reap_pending
        && (
            !record->reaper
            || (record->pid == 1 && pm_common_handle_is_none(record->parent))
        )
        && (record->reaper || !pm_common_handle_is_none(record->parent))
    );
}

static bool validation_wait_is_valid(
    const struct micros_pm_table *table,
    const struct micros_pm_process_record *record,
    size_t index
)
{
    const struct micros_pm_process_record *pending;
    enum micros_pm_model_error error;

    if (!record->wait.active) {
        return wait_is_clear(&record->wait);
    }
    if (
        record->wait.reply_token == 0
        || (record->wait.flags & ~MICROS_PM_WAIT_NOHANG) != 0
    ) {
        return false;
    }
    if (record->wait.result_ready) {
        error = pm_common_resolve_record(table, record->wait.pending_child, &pending);
        return (
            error == MICROS_PM_MODEL_OK
            && pending->state == MICROS_PM_PROCESS_ZOMBIE
            && pending->reap_pending
            && pm_common_handles_equal(
                pending->parent, pm_common_record_handle((uint16_t)index, record)
            )
            && pm_common_wait_matches_pid(&record->wait, pending->pid)
        );
    }
    return (
        pm_common_handle_is_none(record->wait.pending_child)
        && pm_common_has_matching_child(
            table, pm_common_record_handle((uint16_t)index, record),
            record->wait.selector
        )
    );
}

static bool validation_exiting_record_is_valid(
    const struct micros_pm_process_record *record
)
{
    return (
        !record->init_candidate
        && !record->reaper
        && !pm_common_handle_is_none(record->parent)
        && spawn_is_clear(record)
        && wait_is_clear(&record->wait)
        && !record->reap_pending
        && record->exit_transaction != 0
        && (
            record->exit_kind == MICROS_PM_EXIT_NORMAL
            || record->exit_kind == MICROS_PM_EXIT_FAULT
        )
        && (
            record->exit_stage != MICROS_PM_EXIT_STAGE_STOPPED
            || pm_common_endpoint_is_concrete(record->endpoint)
        )
        && (
            record->exit_stage == MICROS_PM_EXIT_STAGE_STOPPED
            || record->endpoint == MICROS_ENDPOINT_NONE
        )
        && (
            record->exit_stage == MICROS_PM_EXIT_STAGE_STOPPED
            || record->exit_stage == MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
            || record->exit_stage == MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY
        )
    );
}

static bool validation_zombie_record_is_valid(
    const struct micros_pm_process_record *record
)
{
    return (
        !record->init_candidate
        && !record->reaper
        && !pm_common_handle_is_none(record->parent)
        && record->endpoint == MICROS_ENDPOINT_NONE
        && spawn_is_clear(record)
        && record->exit_stage == MICROS_PM_EXIT_STAGE_NONE
        && (
            record->exit_kind == MICROS_PM_EXIT_NORMAL
            || record->exit_kind == MICROS_PM_EXIT_FAULT
        )
        && record->reserved1 == 0
        && record->exit_transaction == 0
        && wait_is_clear(&record->wait)
    );
}

static bool validation_live_state_is_valid(
    const struct micros_pm_table *table,
    const struct micros_pm_process_record *record,
    size_t index,
    struct pm_validation_counts *counts
)
{
    if (record->state == MICROS_PM_PROCESS_SPAWNING) {
        ++counts->spawning;
        if (!validation_spawning_record_is_valid(table, record, index)) {
            return false;
        }
        if (!record->init_candidate) {
            ++counts->non_init;
        }
    } else if (record->state == MICROS_PM_PROCESS_RUNNING) {
        if (
            !validation_running_record_is_valid(record)
            || !validation_wait_is_valid(table, record, index)
        ) {
            return false;
        }
        if (record->reaper) {
            ++counts->reaper;
            if (!pm_common_handles_equal(
                table->reaper, pm_common_record_handle((uint16_t)index, record)
            )) {
                return false;
            }
        } else {
            ++counts->non_init;
        }
    } else if (record->state == MICROS_PM_PROCESS_EXITING) {
        if (!validation_exiting_record_is_valid(record)) {
            return false;
        }
        ++counts->non_init;
    } else if (record->state == MICROS_PM_PROCESS_ZOMBIE) {
        if (!validation_zombie_record_is_valid(record)) {
            return false;
        }
        ++counts->non_init;
    } else {
        return false;
    }
    return true;
}

static bool validation_relationships_are_valid(
    const struct micros_pm_table *table,
    const struct micros_pm_process_record *record,
    size_t index
)
{
    const struct micros_pm_process_record *parent;
    enum micros_pm_model_error error;

    if (!pm_common_handle_is_none(record->parent)) {
        if (pm_common_handles_equal(
            record->parent, pm_common_record_handle((uint16_t)index, record)
        )) {
            return false;
        }
        error = pm_common_resolve_record(table, record->parent, &parent);
        if (error != MICROS_PM_MODEL_OK || parent->state != MICROS_PM_PROCESS_RUNNING) {
            return false;
        }
    }
    if (record->reap_pending) {
        if (record->state != MICROS_PM_PROCESS_ZOMBIE) {
            return false;
        }
        error = pm_common_resolve_record(table, record->parent, &parent);
        if (
            error != MICROS_PM_MODEL_OK
            || !parent->wait.active
            || !parent->wait.result_ready
            || !pm_common_handles_equal(
                parent->wait.pending_child,
                pm_common_record_handle((uint16_t)index, record)
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool validation_global_counts_are_valid(
    const struct micros_pm_table *table,
    const struct pm_validation_counts *counts
)
{
    return (
        counts->spawning <= 1
        && counts->reaper <= 1
        && (counts->active == 0 || table->runtime_enabled)
        && (counts->reaper != 0 || counts->non_init == 0)
        && (counts->reaper != 0 || pm_common_handle_is_none(table->reaper))
        && (counts->reaper != 1 || !pm_common_handle_is_none(table->reaper))
    );
}

enum micros_pm_model_error micros_pm_table_validate(
    const struct micros_pm_table *table
)
{
    struct pm_validation_counts counts = {0};
    size_t index;

    if (!validation_header_is_valid(table)) {
        return MICROS_PM_MODEL_ERROR_INVARIANT;
    }
    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record = &table->records[index];

        if (
            !validation_reserved_fields_are_clear(record)
            || !validation_mapping_identity_is_valid(record)
        ) {
            return MICROS_PM_MODEL_ERROR_INVARIANT;
        }
        if (
            record->state == MICROS_PM_PROCESS_FREE
            || record->state == MICROS_PM_PROCESS_QUARANTINED
        ) {
            if (!validation_released_record_is_valid(record)) {
                return MICROS_PM_MODEL_ERROR_INVARIANT;
            }
            continue;
        }
        if (
            record->generation == 0 || record->pid == 0
            || record->pid > table->last_pid
        ) {
            return MICROS_PM_MODEL_ERROR_INVARIANT;
        }
        ++counts.active;
        if (
            !validation_identity_is_unique(table, record, index)
            || !validation_live_state_is_valid(table, record, index, &counts)
            || !validation_relationships_are_valid(table, record, index)
        ) {
            return MICROS_PM_MODEL_ERROR_INVARIANT;
        }
    }
    return validation_global_counts_are_valid(table, &counts)
        ? MICROS_PM_MODEL_OK
        : MICROS_PM_MODEL_ERROR_INVARIANT;
}
