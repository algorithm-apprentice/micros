#include "servers/pm/pm_internal.h"

static void clear_wait_outcome(struct micros_pm_wait_outcome *outcome)
{
    pm_common_zero_bytes(outcome, sizeof(*outcome));
    outcome->result = MICROS_PM_RESULT_OK;
}

static bool find_lowest_matching_zombie(
    const struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t selector,
    uint16_t *slot
)
{
    uint64_t lowest_pid = UINT64_MAX;
    uint16_t selected_slot = 0;
    uint16_t index;
    bool found = false;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct micros_pm_process_record *record
            = &table->records[index];

        if (
            record->state == MICROS_PM_PROCESS_ZOMBIE
            && !record->reap_pending
            && pm_common_record_is_child_of(record, parent)
            && (selector == 0 || selector == record->pid)
            && (!found || record->pid < lowest_pid)
        ) {
            found = true;
            lowest_pid = record->pid;
            selected_slot = index;
        }
    }
    if (found) {
        *slot = selected_slot;
    }
    return found;
}

static void populate_wait_outcome(
    struct micros_pm_process_handle parent_handle,
    const struct micros_pm_process_record *parent,
    const struct micros_pm_process_record *child,
    struct micros_pm_process_handle child_handle,
    struct micros_pm_wait_outcome *outcome
)
{
    clear_wait_outcome(outcome);
    outcome->action = MICROS_PM_WAIT_ACTION_REPLY;
    outcome->reply_token = parent->wait.reply_token;
    outcome->child_pid = child->pid;
    outcome->exit_kind = child->exit_kind;
    outcome->exit_code = child->exit_code;
    outcome->parent = parent_handle;
    outcome->child = child_handle;
}

bool pm_wait_stage_result(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent_handle,
    struct micros_pm_wait_outcome *outcome
)
{
    struct micros_pm_process_record *parent;
    struct micros_pm_process_record *child;
    struct micros_pm_process_handle child_handle;
    uint16_t child_slot;

    parent = &table->records[parent_handle.slot];
    if (
        !parent->wait.active
        || parent->wait.result_ready
        || !find_lowest_matching_zombie(
            table,
            parent_handle,
            parent->wait.selector,
            &child_slot
        )
    ) {
        return false;
    }

    child = &table->records[child_slot];
    child_handle = pm_common_record_handle(child_slot, child);
    child->reap_pending = true;
    parent->wait.result_ready = true;
    parent->wait.pending_child = child_handle;
    populate_wait_outcome(
        parent_handle,
        parent,
        child,
        child_handle,
        outcome
    );
    return true;
}

enum micros_pm_model_error micros_pm_wait_begin(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t selector,
    uint32_t flags,
    uint64_t reply_token,
    struct micros_pm_wait_outcome *outcome
)
{
    struct micros_pm_wait_outcome committed;
    struct micros_pm_process_record *parent_record;
    struct micros_pm_process_record *child_record;
    struct micros_pm_process_handle child_handle;
    const struct micros_pm_process_record *resolved;
    uint16_t child_slot;
    enum micros_pm_model_error error;

    if (
        table == NULL
        || outcome == NULL
        || reply_token == 0
        || (flags & ~MICROS_PM_WAIT_NOHANG) != 0
    ) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_record(table, parent, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (resolved->state != MICROS_PM_PROCESS_RUNNING) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }

    clear_wait_outcome(&committed);
    committed.parent = parent;
    committed.reply_token = reply_token;
    if (resolved->wait.active) {
        committed.action = MICROS_PM_WAIT_ACTION_REPLY;
        committed.result = MICROS_PM_RESULT_BUSY;
        *outcome = committed;
        return MICROS_PM_MODEL_OK;
    }

    parent_record = &table->records[parent.slot];
    if (
        find_lowest_matching_zombie(
            table,
            parent,
            selector,
            &child_slot
        )
    ) {
        child_record = &table->records[child_slot];
        child_handle = pm_common_record_handle(child_slot, child_record);
        parent_record->wait.active = true;
        parent_record->wait.result_ready = true;
        parent_record->wait.flags = flags;
        parent_record->wait.selector = selector;
        parent_record->wait.reply_token = reply_token;
        parent_record->wait.pending_child = child_handle;
        child_record->reap_pending = true;
        populate_wait_outcome(
            parent,
            parent_record,
            child_record,
            child_handle,
            &committed
        );
    } else if (!pm_common_has_matching_child(table, parent, selector)) {
        committed.action = MICROS_PM_WAIT_ACTION_REPLY;
        committed.result = MICROS_PM_RESULT_NO_CHILD;
    } else if ((flags & MICROS_PM_WAIT_NOHANG) != 0) {
        committed.action = MICROS_PM_WAIT_ACTION_REPLY;
    } else {
        parent_record->wait.active = true;
        parent_record->wait.flags = flags;
        parent_record->wait.selector = selector;
        parent_record->wait.reply_token = reply_token;
        committed.action = MICROS_PM_WAIT_ACTION_BLOCKED;
    }
    *outcome = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_wait_resume(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    struct micros_pm_wait_outcome *outcome
)
{
    struct micros_pm_wait_outcome committed;
    struct micros_pm_process_record *parent_record;
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    if (table == NULL || outcome == NULL) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_record(table, parent, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_RUNNING
        || !resolved->wait.active
        || resolved->wait.result_ready
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }

    clear_wait_outcome(&committed);
    committed.parent = parent;
    parent_record = &table->records[parent.slot];
    committed.action = MICROS_PM_WAIT_ACTION_BLOCKED;
    committed.reply_token = parent_record->wait.reply_token;
    (void)pm_wait_stage_result(table, parent, &committed);
    *outcome = committed;
    return MICROS_PM_MODEL_OK;
}

enum micros_pm_model_error micros_pm_wait_reply_complete(
    struct micros_pm_table *table,
    struct micros_pm_process_handle parent,
    uint64_t reply_token,
    bool accepted
)
{
    struct micros_pm_process_record *parent_record;
    struct micros_pm_process_record *child_record;
    const struct micros_pm_process_record *resolved;
    enum micros_pm_model_error error;

    if (table == NULL || reply_token == 0) {
        return MICROS_PM_MODEL_ERROR_ARGUMENT;
    }
    error = micros_pm_table_validate(table);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    error = pm_common_resolve_record(table, parent, &resolved);
    if (error != MICROS_PM_MODEL_OK) {
        return error;
    }
    if (
        resolved->state != MICROS_PM_PROCESS_RUNNING
        || !resolved->wait.active
        || !resolved->wait.result_ready
    ) {
        return MICROS_PM_MODEL_ERROR_STATE;
    }
    if (resolved->wait.reply_token != reply_token) {
        return MICROS_PM_MODEL_ERROR_STALE;
    }

    parent_record = &table->records[parent.slot];
    child_record = &table->records[
        parent_record->wait.pending_child.slot
    ];
    if (!accepted) {
        child_record->reap_pending = false;
        parent_record->wait.result_ready = false;
        pm_common_zero_bytes(
            &parent_record->wait.pending_child,
            sizeof(parent_record->wait.pending_child)
        );
        return MICROS_PM_MODEL_OK;
    }

    pm_common_clear_wait(&parent_record->wait);
    pm_common_release_record(child_record);
    return MICROS_PM_MODEL_OK;
}
