#include "servers/pm/pm_internal.h"

static void clear_wait_outcomes(struct micros_pm_wait_outcomes *outcomes)
{
    pm_common_zero_bytes(outcomes, sizeof(*outcomes));
}

enum micros_pm_model_error pm_exit_prepare_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    enum micros_pm_exit_kind exit_kind,
    int32_t exit_code,
    struct pm_exit_begin_plan *plan
)
{
    const struct micros_pm_process_record *resolved;
    struct pm_exit_begin_plan candidate;
    size_t index;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || plan == NULL
        || (
            exit_kind != MICROS_PM_EXIT_NORMAL
            && exit_kind != MICROS_PM_EXIT_FAULT
        )
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_record(table, process, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (resolved->state != MICROS_PM_PROCESS_RUNNING) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->reaper) {
        return MICROS_PM_MODEL_ERROR_INVARIANT;
    }

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *child
            = &table->records[index];

        if (
            child->state == MICROS_PM_PROCESS_SPAWNING
            && pm_common_handles_equal(child->parent, process)
        ) {
            return MICROS_PM_MODEL_ERROR_BUSY;
        }
    }

    pm_common_zero_bytes(&candidate, sizeof(candidate));
    candidate.table = table;
    candidate.record = &table->records[process.slot];
    candidate.process = process;
    candidate.reaper = table->reaper;
    candidate.exit_kind = exit_kind;
    candidate.exit_code = exit_code;
    *plan = candidate;
    return MICROS_PM_MODEL_OK;
}

void pm_exit_commit_begin(
    const struct pm_exit_begin_plan *plan,
    uint64_t transaction,
    struct micros_pm_exit_effects *effects
)
{
    struct micros_pm_table *table = plan->table;
    struct micros_pm_process_record *record = plan->record;
    struct micros_pm_exit_effects committed;
    size_t index;

    pm_common_zero_bytes(&committed, sizeof(committed));
    if (record->wait.result_ready) {
        table->records[
            record->wait.pending_child.slot
        ].reap_pending = false;
    }
    pm_common_clear_wait(&record->wait);

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        struct micros_pm_process_record *child
            = &table->records[index];

        if (
            pm_common_handles_equal(child->parent, plan->process)
            && (
                child->state == MICROS_PM_PROCESS_RUNNING
                || child->state == MICROS_PM_PROCESS_EXITING
                || child->state == MICROS_PM_PROCESS_ZOMBIE
            )
        ) {
            child->parent = plan->reaper;
            ++committed.reparented_count;
        }
    }

    record->state = MICROS_PM_PROCESS_EXITING;
    record->exit_stage = MICROS_PM_EXIT_STAGE_STOPPED;
    record->exit_kind = plan->exit_kind;
    record->exit_code = plan->exit_code;
    record->exit_transaction = transaction;
    *effects = committed;
}

enum micros_pm_model_error micros_pm_exit_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction,
    enum micros_pm_exit_kind exit_kind,
    int32_t exit_code,
    struct micros_pm_exit_effects *effects
)
{
    struct pm_exit_begin_plan plan;
    enum micros_pm_model_error error;

    if (effects == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = pm_exit_prepare_begin(
        table, process, exit_kind, exit_code, &plan
    );
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    pm_exit_commit_begin(&plan, transaction, effects);
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_exit_detach(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction
)
{
    struct micros_pm_process_record *record;
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    if (table == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_record(table, process, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_EXITING
        || resolved->exit_stage != MICROS_PM_EXIT_STAGE_STOPPED
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->exit_transaction != transaction) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    record = &table->records[process.slot];
    record->endpoint = MICROS_ENDPOINT_NONE;
    record->exit_stage = MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_exit_release_vm(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction
)
{
    struct micros_pm_process_record *record;
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    if (table == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_record(table, process, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_EXITING
        || resolved->exit_stage
            != MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->exit_transaction != transaction) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    record = &table->records[process.slot];
    record->load_token = 0;
    record->mapping_generation = 0;
    record->exit_stage
        = MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_exit_release_process(
    struct micros_pm_table *table,
    struct micros_pm_process_handle process,
    uint64_t transaction,
    struct micros_pm_wait_outcomes *outcomes
)
{
    struct micros_pm_wait_outcomes committed;
    struct micros_pm_process_record *record;
    const struct micros_pm_process_record *resolved;
    struct micros_pm_process_handle parent;
    enum micros_pm_model_error error;

    if (table == NULL || outcomes == NULL || transaction == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_record(table, process, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_EXITING
        || resolved->exit_stage
            != MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->exit_transaction != transaction) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    clear_wait_outcomes(&committed);
    record = &table->records[process.slot];
    parent = record->parent;
    record->state = MICROS_PM_PROCESS_ZOMBIE;
    record->exit_stage = MICROS_PM_EXIT_STAGE_NONE;
    record->exit_transaction = 0;
    if (
        pm_wait_stage_result(
            table,
            table->reaper,
            &committed.entries[committed.count]
        )
    ) {
        ++committed.count;
    }
    if (
        !pm_common_handles_equal(parent, table->reaper)
        && pm_wait_stage_result(
            table,
            parent,
            &committed.entries[committed.count]
        )
    ) {
        ++committed.count;
    }
    *outcomes = committed;
    return MICROS_PM_MODEL_OK;
}
