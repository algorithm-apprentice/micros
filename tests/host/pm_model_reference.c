#include "tests/host/pm_model_test_internal.h"

uint16_t pm_model_select_state(
    const struct pm_model_reference *reference,
    enum micros_pm_process_state state,
    uint64_t choice
)
{
    uint16_t slots[MICROS_PM_PROCESS_CAPACITY];
    uint16_t count = 0;
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            reference->records[index].present
            && !reference->records[index].reaper
            && reference->records[index].state == state
        ) {
            slots[count] = index;
            ++count;
        }
    }
    if (count == 0) {
        return PM_MODEL_NO_SLOT;
    }
    return slots[choice % count];
}

uint16_t pm_model_select_present(
    const struct pm_model_reference *reference,
    uint64_t choice
)
{
    uint16_t slots[MICROS_PM_PROCESS_CAPACITY];
    uint16_t count = 0;
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        if (
            reference->records[index].present
            && !reference->records[index].reaper
        ) {
            slots[count] = index;
            ++count;
        }
    }
    if (count == 0) {
        return PM_MODEL_NO_SLOT;
    }
    return slots[choice % count];
}

bool pm_model_find_zombie(
    const struct pm_model_reference *reference,
    uint64_t selector,
    uint16_t *slot
)
{
    uint64_t lowest_pid = UINT64_MAX;
    uint16_t index;
    bool found = false;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct pm_model_record *record
            = &reference->records[index];

        if (
            record->present
            && !record->reaper
            && record->state == MICROS_PM_PROCESS_ZOMBIE
            && !record->reap_pending
            && (selector == 0 || selector == record->pid)
            && (!found || record->pid < lowest_pid)
        ) {
            found = true;
            lowest_pid = record->pid;
            *slot = index;
        }
    }
    return found;
}

bool pm_model_has_child(
    const struct pm_model_reference *reference,
    uint64_t selector
)
{
    uint16_t index;

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct pm_model_record *record
            = &reference->records[index];

        if (
            record->present
            && !record->reaper
            && (selector == 0 || selector == record->pid)
        ) {
            return true;
        }
    }
    return false;
}

void pm_model_set_wait_result(
    struct pm_model_reference *reference,
    uint16_t slot
)
{
    reference->wait.result_ready = true;
    reference->wait.pending_slot = slot;
    reference->records[slot].reap_pending = true;
}

bool pm_model_compare(
    const struct micros_pm_table *table,
    const struct pm_model_reference *reference,
    struct micros_pm_process_handle init
)
{
    const struct micros_pm_process_record *init_record
        = &table->records[init.slot];
    uint16_t index;

    if (
        table->last_pid != reference->last_pid
        || !table->runtime_enabled
        || !pm_test_handles_equal(table->reaper, init)
        || init_record->state != MICROS_PM_PROCESS_RUNNING
        || !init_record->reaper
        || init_record->pid != 1
        || init_record->endpoint != UINT32_C(0x00001000)
        || init_record->wait.active != reference->wait.active
        || init_record->wait.result_ready
            != reference->wait.result_ready
        || init_record->wait.flags != reference->wait.flags
        || init_record->wait.selector != reference->wait.selector
        || init_record->wait.reply_token
            != reference->wait.reply_token
    ) {
        return false;
    }
    if (reference->wait.pending_slot == PM_MODEL_NO_SLOT) {
        if (!pm_test_handle_is_none(init_record->wait.pending_child)) {
            return false;
        }
    } else {
        const struct pm_model_record *pending
            = &reference->records[reference->wait.pending_slot];

        if (
            init_record->wait.pending_child.slot
                != pending->handle.slot
            || init_record->wait.pending_child.generation
                != pending->handle.generation
        ) {
            return false;
        }
    }

    for (index = 0; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        const struct pm_model_record *expected
            = &reference->records[index];
        const struct micros_pm_process_record *actual
            = &table->records[index];

        if (index == init.slot) {
            continue;
        }
        if (!expected->present) {
            if (
                actual->state != MICROS_PM_PROCESS_FREE
                || actual->generation
                    != reference->generations[index]
            ) {
                return false;
            }
            continue;
        }
        if (
            actual->state != expected->state
            || actual->generation != expected->handle.generation
            || actual->pid != expected->pid
            || actual->parent.slot != init.slot
            || actual->parent.generation != init.generation
            || actual->reaper
            || actual->reap_pending != expected->reap_pending
            || actual->spawn_stage != expected->spawn_stage
            || (
                expected->state == MICROS_PM_PROCESS_SPAWNING
                && actual->spawn.parent_reply_token
                    != expected->reply_token
            )
            || actual->exit_stage != expected->exit_stage
            || actual->exit_transaction != expected->transaction
            || actual->endpoint != expected->endpoint
        ) {
            return false;
        }
        if (
            (
                expected->state == MICROS_PM_PROCESS_EXITING
                || expected->state == MICROS_PM_PROCESS_ZOMBIE
            )
            && (
                actual->exit_kind != MICROS_PM_EXIT_NORMAL
                || actual->exit_code != expected->exit_code
            )
        ) {
            return false;
        }
    }
    return true;
}
