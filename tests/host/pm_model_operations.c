#include "tests/host/pm_model_test_internal.h"

static bool model_reserve_spawn(struct pm_model_context *context)
{
    uint16_t slot = PM_MODEL_NO_SLOT;
    struct micros_pm_process_handle child;
    uint64_t pid;
    uint64_t reply_token = UINT64_C(0x500000) + context->step;

    if (
        pm_model_select_state(
            &context->reference,
            MICROS_PM_PROCESS_SPAWNING,
            0
        ) == PM_MODEL_NO_SLOT
    ) {
        PM_MODEL_EXPECT(context,
            micros_pm_begin_spawn(
                &context->table,
                context->init,
                reply_token,
                &child,
                &pid
            ) == MICROS_PM_MODEL_OK
        );
        slot = child.slot;
        context->operation_trace[context->step] |= (uint32_t)slot << 16;
        PM_MODEL_EXPECT(context,
            !context->reference.records[slot].present
            && child.generation
                == context->reference.generations[slot] + 1
            && pid == context->reference.last_pid + 1
        );
        context->reference.generations[slot] = child.generation;
        context->reference.last_pid = pid;
        context->reference.records[slot].present = true;
        context->reference.records[slot].state
            = MICROS_PM_PROCESS_SPAWNING;
        context->reference.records[slot].spawn_stage
            = MICROS_PM_SPAWN_PM_RECORD_RESERVED;
        context->reference.records[slot].handle = child;
        context->reference.records[slot].pid = pid;
        context->reference.records[slot].reply_token = reply_token;
        context->reference.records[slot].endpoint
            = MICROS_ENDPOINT_NONE;
    }
    return true;
}

static bool activate_selected_spawn(
    struct pm_model_context *context,
    struct pm_model_record *record,
    micros_endpoint_t endpoint
)
{
    uint64_t reply_token = 0;

    PM_MODEL_EXPECT(context,
        micros_pm_spawn_activate(
            &context->table,
            record->handle,
            endpoint,
            &reply_token
        ) == MICROS_PM_MODEL_OK
        && reply_token == record->reply_token
    );
    record->state = MICROS_PM_PROCESS_RUNNING;
    record->spawn_stage = MICROS_PM_SPAWN_NONE;
    record->endpoint = endpoint;
    return true;
}

static bool advance_selected_spawn(struct pm_model_context *context, uint16_t slot)
{
    struct pm_model_record *record
        = &context->reference.records[slot];
    uint64_t token = UINT64_C(0x100000) + context->step;
    micros_endpoint_t endpoint;

    context->operation_trace[context->step] |= (uint32_t)slot << 16;
    if (
        record->spawn_stage
        == MICROS_PM_SPAWN_PM_RECORD_RESERVED
    ) {
        PM_MODEL_EXPECT(context,
            micros_pm_spawn_reserve_kernel(
                &context->table,
                record->handle,
                token
            ) == MICROS_PM_MODEL_OK
        );
        record->spawn_stage
            = MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED;
    } else if (
        record->spawn_stage
        == MICROS_PM_SPAWN_KERNEL_PROCESS_RESERVED
    ) {
        PM_MODEL_EXPECT(context,
            micros_pm_spawn_prepare_image(
                &context->table,
                record->handle,
                token
            ) == MICROS_PM_MODEL_OK
        );
        record->spawn_stage
            = MICROS_PM_SPAWN_VFS_IMAGE_PREPARED;
    } else if (
        record->spawn_stage
        == MICROS_PM_SPAWN_VFS_IMAGE_PREPARED
    ) {
        PM_MODEL_EXPECT(context,
            micros_pm_spawn_freeze_mappings(
                &context->table,
                record->handle,
                token,
                token + 1
            ) == MICROS_PM_MODEL_OK
        );
        record->spawn_stage
            = MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
        record->load_token = token;
        record->mapping_generation = token + 1;
    } else if (
        record->spawn_stage
        == MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN
    ) {
        PM_MODEL_EXPECT(context,
            micros_pm_spawn_prepare_descriptors(
                &context->table,
                record->handle,
                token
            ) == MICROS_PM_MODEL_OK
        );
        record->spawn_stage
            = MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED;
    } else if (
        record->spawn_stage
        == MICROS_PM_SPAWN_VFS_DESCRIPTORS_PREPARED
    ) {
        endpoint = (
            record->handle.generation
            << MICROS_ENDPOINT_SLOT_BITS
        ) | record->handle.slot;
        PM_MODEL_EXPECT(context,
            micros_pm_spawn_prepare_execution(
                &context->table,
                record->handle,
                token,
                endpoint
            ) == MICROS_PM_MODEL_OK
        );
        record->spawn_stage
            = MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED;
    } else if (
        record->spawn_stage
        == MICROS_PM_SPAWN_KERNEL_EXECUTION_PREPARED
    ) {
        PM_MODEL_EXPECT(context,
            micros_pm_spawn_commit_hidden(
                &context->table,
                record->handle,
                token
            ) == MICROS_PM_MODEL_OK
        );
        record->spawn_stage
            = MICROS_PM_SPAWN_PM_VFS_COMMITTED;
    } else {
        endpoint = (
            record->handle.generation
            << MICROS_ENDPOINT_SLOT_BITS
        ) | record->handle.slot;
        return activate_selected_spawn(context, record, endpoint);
    }
    return true;
}

static bool model_advance_spawn(struct pm_model_context *context, uint64_t choice)
{
    uint16_t slot = PM_MODEL_NO_SLOT;
    slot = pm_model_select_state(
        &context->reference,
        MICROS_PM_PROCESS_SPAWNING,
        choice >> 8
    );
    if (slot != PM_MODEL_NO_SLOT) {
        return advance_selected_spawn(context, slot);
    }
    return true;
}

static bool model_abort_spawn(struct pm_model_context *context, uint64_t choice)
{
    uint16_t slot = PM_MODEL_NO_SLOT;
    slot = pm_model_select_state(
        &context->reference,
        MICROS_PM_PROCESS_SPAWNING,
        choice >> 8
    );
    if (slot != PM_MODEL_NO_SLOT) {
        struct pm_model_record *record
            = &context->reference.records[slot];
        uint32_t released = UINT32_MAX;

        context->operation_trace[context->step] |= (uint32_t)slot << 16;
        PM_MODEL_EXPECT(context,
            pm_test_rollback_spawn(
                &context->table,
                record->handle,
                record->spawn_stage,
                record->reply_token,
                false,
                &released
            )
            && released
                == pm_test_expected_resources(record->spawn_stage)
        );
        memset(record, 0, sizeof(*record));
    }
    return true;
}

static bool model_begin_exit(struct pm_model_context *context, uint64_t choice)
{
    uint16_t slot = PM_MODEL_NO_SLOT;
    slot = pm_model_select_state(
        &context->reference,
        MICROS_PM_PROCESS_RUNNING,
        choice >> 8
    );
    if (slot != PM_MODEL_NO_SLOT) {
        struct pm_model_record *record
            = &context->reference.records[slot];
        struct micros_pm_exit_effects effects;
        uint64_t transaction
            = UINT64_C(0x200000) + context->step;
        int32_t exit_code = -(int32_t)(context->step + 1);

        context->operation_trace[context->step] |= (uint32_t)slot << 16;
        PM_MODEL_EXPECT(context,
            micros_pm_exit_begin(
                &context->table,
                record->handle,
                transaction,
                MICROS_PM_EXIT_NORMAL,
                exit_code,
                &effects
            ) == MICROS_PM_MODEL_OK
            && !effects.aborted_spawn
            && effects.reparented_count == 0
        );
        record->state = MICROS_PM_PROCESS_EXITING;
        record->exit_stage = MICROS_PM_EXIT_STAGE_STOPPED;
        record->transaction = transaction;
        record->exit_code = exit_code;
    }
    return true;
}

static bool model_advance_exit(struct pm_model_context *context, uint64_t choice)
{
    uint16_t slot = PM_MODEL_NO_SLOT;
    slot = pm_model_select_state(
        &context->reference,
        MICROS_PM_PROCESS_EXITING,
        choice >> 8
    );
    if (slot != PM_MODEL_NO_SLOT) {
        struct pm_model_record *record
            = &context->reference.records[slot];
        struct micros_pm_wait_outcomes outcomes;

        context->operation_trace[context->step] |= (uint32_t)slot << 16;
        if (
            record->exit_stage
            == MICROS_PM_EXIT_STAGE_STOPPED
        ) {
            PM_MODEL_EXPECT(context,
                micros_pm_exit_detach(
                    &context->table,
                    record->handle,
                    record->transaction
                ) == MICROS_PM_MODEL_OK
            );
            record->exit_stage
                = MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING;
            record->endpoint = MICROS_ENDPOINT_NONE;
        } else if (
            record->exit_stage
            == MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
        ) {
            PM_MODEL_EXPECT(context,
                micros_pm_exit_release_vm(
                    &context->table,
                    record->handle,
                    record->transaction
                ) == MICROS_PM_MODEL_OK
            );
            record->exit_stage
                = MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY;
            record->load_token = 0;
            record->mapping_generation = 0;
        } else {
            uint16_t zombie_slot;

            PM_MODEL_EXPECT(context,
                micros_pm_exit_release_process(
                    &context->table,
                    record->handle,
                    record->transaction,
                    &outcomes
                ) == MICROS_PM_MODEL_OK
            );
            record->state = MICROS_PM_PROCESS_ZOMBIE;
            record->exit_stage = MICROS_PM_EXIT_STAGE_NONE;
            record->transaction = 0;
            if (
                context->reference.wait.active
                && !context->reference.wait.result_ready
                && pm_model_find_zombie(
                    &context->reference,
                    context->reference.wait.selector,
                    &zombie_slot
                )
            ) {
                pm_model_set_wait_result(
                    &context->reference,
                    zombie_slot
                );
                PM_MODEL_EXPECT(context,
                    outcomes.count == 1
                    && outcomes.entries[0].action
                        == MICROS_PM_WAIT_ACTION_REPLY
                    && outcomes.entries[0].reply_token
                        == context->reference.wait.reply_token
                    && outcomes.entries[0].child_pid
                        == context->reference.records[zombie_slot].pid
                    && pm_test_handles_equal(
                        outcomes.entries[0].parent,
                        context->init
                    )
                );
            } else {
                PM_MODEL_EXPECT(context, outcomes.count == 0);
            }
        }
    }
    return true;
}

static bool model_begin_wait(struct pm_model_context *context, uint64_t choice)
{
    struct micros_pm_wait_outcome outcome;
    uint64_t selector;
    uint32_t flags;
    uint64_t reply_token = UINT64_C(0x300000) + context->step;
    uint16_t selected = pm_model_select_present(
        &context->reference,
        choice >> 8
    );
    uint16_t zombie_slot;
    bool has_child;

    if ((choice & UINT64_C(0x100)) == 0) {
        selector = 0;
    } else if (selected != PM_MODEL_NO_SLOT) {
        selector = context->reference.records[selected].pid;
    } else {
        selector = context->reference.last_pid + 100;
    }
    flags = (
        (choice & UINT64_C(0x200)) != 0
            ? MICROS_PM_WAIT_NOHANG
            : 0
    );
    if (
        pm_model_select_state(
            &context->reference,
            MICROS_PM_PROCESS_SPAWNING,
            0
        ) != PM_MODEL_NO_SLOT
    ) {
        flags = MICROS_PM_WAIT_NOHANG;
    }
    context->operation_trace[context->step] |= (uint32_t)flags << 16;
    PM_MODEL_EXPECT(context,
        micros_pm_wait_begin(
            &context->table,
            context->init,
            selector,
            flags,
            reply_token,
            &outcome
        ) == MICROS_PM_MODEL_OK
    );
    if (context->reference.wait.active) {
        PM_MODEL_EXPECT(context,
            outcome.action == MICROS_PM_WAIT_ACTION_REPLY
            && outcome.result == MICROS_PM_RESULT_BUSY
            && outcome.reply_token == reply_token
        );
    } else if (
        pm_model_find_zombie(
            &context->reference,
            selector,
            &zombie_slot
        )
    ) {
        context->reference.wait.active = true;
        context->reference.wait.result_ready = true;
        context->reference.wait.flags = flags;
        context->reference.wait.selector = selector;
        context->reference.wait.reply_token = reply_token;
        context->reference.wait.pending_slot = zombie_slot;
        context->reference.records[zombie_slot].reap_pending = true;
        PM_MODEL_EXPECT(context,
            outcome.action == MICROS_PM_WAIT_ACTION_REPLY
            && outcome.result == MICROS_PM_RESULT_OK
            && outcome.child_pid
                == context->reference.records[zombie_slot].pid
        );
    } else {
        has_child = pm_model_has_child(&context->reference, selector);
        if (!has_child) {
            PM_MODEL_EXPECT(context,
                outcome.action
                    == MICROS_PM_WAIT_ACTION_REPLY
                && outcome.result
                    == MICROS_PM_RESULT_NO_CHILD
            );
        } else if ((flags & MICROS_PM_WAIT_NOHANG) != 0) {
            PM_MODEL_EXPECT(context,
                outcome.action
                    == MICROS_PM_WAIT_ACTION_REPLY
                && outcome.result == MICROS_PM_RESULT_OK
                && outcome.child_pid == 0
            );
        } else {
            context->reference.wait.active = true;
            context->reference.wait.flags = flags;
            context->reference.wait.selector = selector;
            context->reference.wait.reply_token = reply_token;
            context->reference.wait.pending_slot = PM_MODEL_NO_SLOT;
            PM_MODEL_EXPECT(context,
                outcome.action
                    == MICROS_PM_WAIT_ACTION_BLOCKED
            );
        }
    }
    return true;
}

static bool model_complete_wait(struct pm_model_context *context, uint64_t choice)
{
    if (context->reference.wait.active) {
        struct micros_pm_wait_outcome outcome;

        if (context->reference.wait.result_ready) {
            bool accepted = (choice & UINT64_C(0x100)) != 0;
            uint16_t pending_slot
                = context->reference.wait.pending_slot;

            context->operation_trace[context->step] |= (
                accepted ? UINT32_C(0x00010000) : 0
            );
            PM_MODEL_EXPECT(context,
                micros_pm_wait_reply_complete(
                    &context->table,
                    context->init,
                    context->reference.wait.reply_token,
                    accepted
                ) == MICROS_PM_MODEL_OK
            );
            if (accepted) {
                memset(
                    &context->reference.records[pending_slot],
                    0,
                    sizeof(context->reference.records[pending_slot])
                );
                memset(
                    &context->reference.wait,
                    0,
                    sizeof(context->reference.wait)
                );
                context->reference.wait.pending_slot
                    = PM_MODEL_NO_SLOT;
            } else {
                context->reference.records[pending_slot].reap_pending
                    = false;
                context->reference.wait.result_ready = false;
                context->reference.wait.pending_slot
                    = PM_MODEL_NO_SLOT;
            }
        } else {
            uint16_t zombie_slot;
            bool found = pm_model_find_zombie(
                &context->reference,
                context->reference.wait.selector,
                &zombie_slot
            );

            PM_MODEL_EXPECT(context,
                micros_pm_wait_resume(
                    &context->table,
                    context->init,
                    &outcome
                ) == MICROS_PM_MODEL_OK
            );
            if (found) {
                pm_model_set_wait_result(
                    &context->reference,
                    zombie_slot
                );
                PM_MODEL_EXPECT(context,
                    outcome.action
                        == MICROS_PM_WAIT_ACTION_REPLY
                    && outcome.child_pid
                        == context->reference.records[zombie_slot].pid
                );
            } else {
                PM_MODEL_EXPECT(context,
                    outcome.action
                        == MICROS_PM_WAIT_ACTION_BLOCKED
                );
            }
        }
    }
    return true;
}

static bool model_lookup_endpoint(struct pm_model_context *context, uint64_t choice)
{
    uint16_t slot = PM_MODEL_NO_SLOT;
    slot = pm_model_select_state(
        &context->reference,
        MICROS_PM_PROCESS_RUNNING,
        choice >> 8
    );
    if (slot != PM_MODEL_NO_SLOT) {
        struct micros_pm_process_handle process;
        uint64_t pid;

        context->operation_trace[context->step] |= (uint32_t)slot << 16;
        PM_MODEL_EXPECT(context,
            micros_pm_find_running_by_endpoint(
                &context->table,
                context->reference.records[slot].endpoint,
                &process,
                &pid
            ) == MICROS_PM_MODEL_OK
            && process.slot == slot
            && process.generation
                == context->reference.records[slot].handle.generation
            && pid == context->reference.records[slot].pid
        );
    }
    return true;
}

bool pm_model_run_operation(
    struct pm_model_context *context,
    uint32_t operation,
    uint64_t choice
)
{
    switch (operation) {

    case 0:
        return model_reserve_spawn(context);

    case 1:
        return model_advance_spawn(context, choice);

    case 2:
        return model_abort_spawn(context, choice);

    case 3:
        return model_begin_exit(context, choice);

    case 4:
        return model_advance_exit(context, choice);

    case 5:
        return model_begin_wait(context, choice);

    case 6:
        return model_complete_wait(context, choice);

    case 7:
        return model_lookup_endpoint(context, choice);

    default:
        return pm_model_failure(
            "invalid model operation", __LINE__, context->seed,
            context->step, context->operation_trace
        );
    }
}
