#include "tests/host/pm_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool micros_pm_control_test_run(void);
bool micros_pm_control_execution_test_run(void);

enum {
    PM_MODEL_STEP_COUNT = 4096,
    PM_MODEL_NO_SLOT = UINT16_MAX,
};

struct pm_model_record {
    bool present;
    bool reaper;
    bool reap_pending;
    enum micros_pm_process_state state;
    enum micros_pm_spawn_stage spawn_stage;
    enum micros_pm_exit_stage exit_stage;
    struct micros_pm_process_handle handle;
    uint64_t pid;
    uint64_t transaction;
    uint64_t reply_token;
    micros_endpoint_t endpoint;
    int32_t exit_code;
};

struct pm_model_wait {
    bool active;
    bool result_ready;
    uint32_t flags;
    uint64_t selector;
    uint64_t reply_token;
    uint16_t pending_slot;
};

struct pm_model_reference {
    uint64_t last_pid;
    uint32_t generations[MICROS_PM_PROCESS_CAPACITY];
    struct pm_model_record records[MICROS_PM_PROCESS_CAPACITY];
    struct pm_model_wait wait;
};

static uint64_t pm_model_random(uint64_t *state)
{
    uint64_t value = *state;

    value ^= value << 13;
    value ^= value >> 7;
    value ^= value << 17;
    *state = value;
    return value;
}

static uint16_t pm_model_select_state(
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

static uint16_t pm_model_select_present(
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

static bool pm_model_find_zombie(
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

static bool pm_model_has_child(
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

static void pm_model_set_wait_result(
    struct pm_model_reference *reference,
    uint16_t slot
)
{
    reference->wait.result_ready = true;
    reference->wait.pending_slot = slot;
    reference->records[slot].reap_pending = true;
}

static bool pm_model_compare(
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

static bool pm_model_failure(
    const char *expression,
    int line,
    uint64_t seed,
    size_t step,
    const uint32_t *trace
)
{
    size_t index;

    fprintf(
        stderr,
        "%s:%d: PM model failure: %s seed=0x%016llx step=%zu\n",
        __FILE__,
        line,
        expression,
        (unsigned long long)seed,
        step
    );
    fprintf(stderr, "operation trace:");
    for (index = 0; index <= step; ++index) {
        if ((index % 8) == 0) {
            fputc('\n', stderr);
        }
        fprintf(stderr, " %08x", trace[index]);
    }
    fputc('\n', stderr);
    return false;
}

#define PM_MODEL_EXPECT(expression) \
    do { \
        if (!(expression)) { \
            return pm_model_failure( \
                #expression, \
                __LINE__, \
                seed, \
                step, \
                operation_trace \
            ); \
        } \
    } while (false)

static bool test_replayable_transition_model(void)
{
    const uint64_t seed = UINT64_C(0x4d4943524f53504d);
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct pm_model_reference reference;
    uint32_t operation_trace[PM_MODEL_STEP_COUNT];
    uint64_t random_state = seed;
    size_t step;

    memset(&reference, 0, sizeof(reference));
    memset(operation_trace, 0, sizeof(operation_trace));
    reference.wait.pending_slot = PM_MODEL_NO_SLOT;
    step = 0;
    PM_MODEL_EXPECT(pm_test_initialize_running_init(&table, &init));
    reference.last_pid = 1;
    reference.generations[init.slot] = init.generation;
    reference.records[init.slot].present = true;
    reference.records[init.slot].reaper = true;
    reference.records[init.slot].state = MICROS_PM_PROCESS_RUNNING;
    reference.records[init.slot].handle = init;
    reference.records[init.slot].pid = 1;
    reference.records[init.slot].endpoint = UINT32_C(0x00001000);

    for (step = 0; step < PM_MODEL_STEP_COUNT; ++step) {
        uint64_t choice = pm_model_random(&random_state);
        uint32_t operation = (uint32_t)(choice % 8);
        uint16_t slot = PM_MODEL_NO_SLOT;

        operation_trace[step] = operation << 24;
        if (operation == 0) {
            struct micros_pm_process_handle child;
            uint64_t pid;
            uint64_t reply_token = UINT64_C(0x500000) + step;

            if (
                pm_model_select_state(
                    &reference,
                    MICROS_PM_PROCESS_SPAWNING,
                    0
                ) == PM_MODEL_NO_SLOT
            ) {
                PM_MODEL_EXPECT(
                    micros_pm_begin_spawn(
                        &table,
                        init,
                        reply_token,
                        &child,
                        &pid
                    ) == MICROS_PM_MODEL_OK
                );
                slot = child.slot;
                operation_trace[step] |= (uint32_t)slot << 16;
                PM_MODEL_EXPECT(
                    !reference.records[slot].present
                    && child.generation
                        == reference.generations[slot] + 1
                    && pid == reference.last_pid + 1
                );
                reference.generations[slot] = child.generation;
                reference.last_pid = pid;
                reference.records[slot].present = true;
                reference.records[slot].state
                    = MICROS_PM_PROCESS_SPAWNING;
                reference.records[slot].spawn_stage
                    = MICROS_PM_SPAWN_PM_RECORD_RESERVED;
                reference.records[slot].handle = child;
                reference.records[slot].pid = pid;
                reference.records[slot].reply_token = reply_token;
                reference.records[slot].endpoint
                    = MICROS_ENDPOINT_NONE;
            }
        } else if (operation == 1) {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_SPAWNING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct pm_model_record *record
                    = &reference.records[slot];
                uint64_t token = UINT64_C(0x100000) + step;
                micros_endpoint_t endpoint;

                operation_trace[step] |= (uint32_t)slot << 16;
                if (
                    record->spawn_stage
                    == MICROS_PM_SPAWN_PM_RECORD_RESERVED
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_reserve_kernel(
                            &table,
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
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_prepare_image(
                            &table,
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
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_freeze_mappings(
                            &table,
                            record->handle,
                            token,
                            token + 1
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->spawn_stage
                        = MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN;
                } else if (
                    record->spawn_stage
                    == MICROS_PM_SPAWN_VM_MAPPINGS_FROZEN
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_prepare_descriptors(
                            &table,
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
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_prepare_execution(
                            &table,
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
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_commit_hidden(
                            &table,
                            record->handle,
                            token
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->spawn_stage
                        = MICROS_PM_SPAWN_PM_VFS_COMMITTED;
                } else {
                    uint64_t reply_token = 0;

                    endpoint = (
                        record->handle.generation
                        << MICROS_ENDPOINT_SLOT_BITS
                    ) | record->handle.slot;
                    PM_MODEL_EXPECT(
                        micros_pm_spawn_activate(
                            &table,
                            record->handle,
                            endpoint,
                            &reply_token
                        ) == MICROS_PM_MODEL_OK
                        && reply_token == record->reply_token
                    );
                    record->state = MICROS_PM_PROCESS_RUNNING;
                    record->spawn_stage = MICROS_PM_SPAWN_NONE;
                    record->endpoint = endpoint;
                }
            }
        } else if (operation == 2) {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_SPAWNING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct pm_model_record *record
                    = &reference.records[slot];
                uint32_t released = UINT32_MAX;

                operation_trace[step] |= (uint32_t)slot << 16;
                PM_MODEL_EXPECT(
                    pm_test_rollback_spawn(
                        &table,
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
        } else if (operation == 3) {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_RUNNING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct pm_model_record *record
                    = &reference.records[slot];
                struct micros_pm_exit_effects effects;
                uint64_t transaction
                    = UINT64_C(0x200000) + step;
                int32_t exit_code = -(int32_t)(step + 1);

                operation_trace[step] |= (uint32_t)slot << 16;
                PM_MODEL_EXPECT(
                    micros_pm_exit_begin(
                        &table,
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
        } else if (operation == 4) {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_EXITING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct pm_model_record *record
                    = &reference.records[slot];
                struct micros_pm_wait_outcomes outcomes;

                operation_trace[step] |= (uint32_t)slot << 16;
                if (
                    record->exit_stage
                    == MICROS_PM_EXIT_STAGE_STOPPED
                ) {
                    PM_MODEL_EXPECT(
                        micros_pm_exit_detach(
                            &table,
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
                    PM_MODEL_EXPECT(
                        micros_pm_exit_release_vm(
                            &table,
                            record->handle,
                            record->transaction
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->exit_stage
                        = MICROS_PM_EXIT_STAGE_KERNEL_PROCESS_EMPTY;
                } else {
                    uint16_t zombie_slot;

                    PM_MODEL_EXPECT(
                        micros_pm_exit_release_process(
                            &table,
                            record->handle,
                            record->transaction,
                            &outcomes
                        ) == MICROS_PM_MODEL_OK
                    );
                    record->state = MICROS_PM_PROCESS_ZOMBIE;
                    record->exit_stage = MICROS_PM_EXIT_STAGE_NONE;
                    record->transaction = 0;
                    if (
                        reference.wait.active
                        && !reference.wait.result_ready
                        && pm_model_find_zombie(
                            &reference,
                            reference.wait.selector,
                            &zombie_slot
                        )
                    ) {
                        pm_model_set_wait_result(
                            &reference,
                            zombie_slot
                        );
                        PM_MODEL_EXPECT(
                            outcomes.count == 1
                            && outcomes.entries[0].action
                                == MICROS_PM_WAIT_ACTION_REPLY
                            && outcomes.entries[0].reply_token
                                == reference.wait.reply_token
                            && outcomes.entries[0].child_pid
                                == reference.records[zombie_slot].pid
                            && pm_test_handles_equal(
                                outcomes.entries[0].parent,
                                init
                            )
                        );
                    } else {
                        PM_MODEL_EXPECT(outcomes.count == 0);
                    }
                }
            }
        } else if (operation == 5) {
            struct micros_pm_wait_outcome outcome;
            uint64_t selector;
            uint32_t flags;
            uint64_t reply_token = UINT64_C(0x300000) + step;
            uint16_t selected = pm_model_select_present(
                &reference,
                choice >> 8
            );
            uint16_t zombie_slot;
            bool has_child;

            if ((choice & UINT64_C(0x100)) == 0) {
                selector = 0;
            } else if (selected != PM_MODEL_NO_SLOT) {
                selector = reference.records[selected].pid;
            } else {
                selector = reference.last_pid + 100;
            }
            flags = (
                (choice & UINT64_C(0x200)) != 0
                    ? MICROS_PM_WAIT_NOHANG
                    : 0
            );
            if (
                pm_model_select_state(
                    &reference,
                    MICROS_PM_PROCESS_SPAWNING,
                    0
                ) != PM_MODEL_NO_SLOT
            ) {
                flags = MICROS_PM_WAIT_NOHANG;
            }
            operation_trace[step] |= (uint32_t)flags << 16;
            PM_MODEL_EXPECT(
                micros_pm_wait_begin(
                    &table,
                    init,
                    selector,
                    flags,
                    reply_token,
                    &outcome
                ) == MICROS_PM_MODEL_OK
            );
            if (reference.wait.active) {
                PM_MODEL_EXPECT(
                    outcome.action == MICROS_PM_WAIT_ACTION_REPLY
                    && outcome.result == MICROS_PM_RESULT_BUSY
                    && outcome.reply_token == reply_token
                );
            } else if (
                pm_model_find_zombie(
                    &reference,
                    selector,
                    &zombie_slot
                )
            ) {
                reference.wait.active = true;
                reference.wait.result_ready = true;
                reference.wait.flags = flags;
                reference.wait.selector = selector;
                reference.wait.reply_token = reply_token;
                reference.wait.pending_slot = zombie_slot;
                reference.records[zombie_slot].reap_pending = true;
                PM_MODEL_EXPECT(
                    outcome.action == MICROS_PM_WAIT_ACTION_REPLY
                    && outcome.result == MICROS_PM_RESULT_OK
                    && outcome.child_pid
                        == reference.records[zombie_slot].pid
                );
            } else {
                has_child = pm_model_has_child(&reference, selector);
                if (!has_child) {
                    PM_MODEL_EXPECT(
                        outcome.action
                            == MICROS_PM_WAIT_ACTION_REPLY
                        && outcome.result
                            == MICROS_PM_RESULT_NO_CHILD
                    );
                } else if ((flags & MICROS_PM_WAIT_NOHANG) != 0) {
                    PM_MODEL_EXPECT(
                        outcome.action
                            == MICROS_PM_WAIT_ACTION_REPLY
                        && outcome.result == MICROS_PM_RESULT_OK
                        && outcome.child_pid == 0
                    );
                } else {
                    reference.wait.active = true;
                    reference.wait.flags = flags;
                    reference.wait.selector = selector;
                    reference.wait.reply_token = reply_token;
                    reference.wait.pending_slot = PM_MODEL_NO_SLOT;
                    PM_MODEL_EXPECT(
                        outcome.action
                            == MICROS_PM_WAIT_ACTION_BLOCKED
                    );
                }
            }
        } else if (operation == 6) {
            if (reference.wait.active) {
                struct micros_pm_wait_outcome outcome;

                if (reference.wait.result_ready) {
                    bool accepted = (choice & UINT64_C(0x100)) != 0;
                    uint16_t pending_slot
                        = reference.wait.pending_slot;

                    operation_trace[step] |= (
                        accepted ? UINT32_C(0x00010000) : 0
                    );
                    PM_MODEL_EXPECT(
                        micros_pm_wait_reply_complete(
                            &table,
                            init,
                            reference.wait.reply_token,
                            accepted
                        ) == MICROS_PM_MODEL_OK
                    );
                    if (accepted) {
                        memset(
                            &reference.records[pending_slot],
                            0,
                            sizeof(reference.records[pending_slot])
                        );
                        memset(
                            &reference.wait,
                            0,
                            sizeof(reference.wait)
                        );
                        reference.wait.pending_slot
                            = PM_MODEL_NO_SLOT;
                    } else {
                        reference.records[pending_slot].reap_pending
                            = false;
                        reference.wait.result_ready = false;
                        reference.wait.pending_slot
                            = PM_MODEL_NO_SLOT;
                    }
                } else {
                    uint16_t zombie_slot;
                    bool found = pm_model_find_zombie(
                        &reference,
                        reference.wait.selector,
                        &zombie_slot
                    );

                    PM_MODEL_EXPECT(
                        micros_pm_wait_resume(
                            &table,
                            init,
                            &outcome
                        ) == MICROS_PM_MODEL_OK
                    );
                    if (found) {
                        pm_model_set_wait_result(
                            &reference,
                            zombie_slot
                        );
                        PM_MODEL_EXPECT(
                            outcome.action
                                == MICROS_PM_WAIT_ACTION_REPLY
                            && outcome.child_pid
                                == reference.records[zombie_slot].pid
                        );
                    } else {
                        PM_MODEL_EXPECT(
                            outcome.action
                                == MICROS_PM_WAIT_ACTION_BLOCKED
                        );
                    }
                }
            }
        } else {
            slot = pm_model_select_state(
                &reference,
                MICROS_PM_PROCESS_RUNNING,
                choice >> 8
            );
            if (slot != PM_MODEL_NO_SLOT) {
                struct micros_pm_process_handle process;
                uint64_t pid;

                operation_trace[step] |= (uint32_t)slot << 16;
                PM_MODEL_EXPECT(
                    micros_pm_find_running_by_endpoint(
                        &table,
                        reference.records[slot].endpoint,
                        &process,
                        &pid
                    ) == MICROS_PM_MODEL_OK
                    && process.slot == slot
                    && process.generation
                        == reference.records[slot].handle.generation
                    && pid == reference.records[slot].pid
                );
            }
        }

        PM_MODEL_EXPECT(
            micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
            && pm_model_compare(&table, &reference, init)
        );
    }
    return true;
}

int main(void)
{
    if (
        !micros_pm_control_test_run()
        || !micros_pm_control_execution_test_run()
        || !test_protocol_contract()
        || !test_runtime_gate_and_spawn()
        || !test_reverse_rollback_and_identity()
        || !test_rollback_acknowledgment_identity()
        || !test_generation_and_pid_exhaustion()
        || !test_failed_init_is_terminal()
        || !test_endpoint_lookup_and_failure_preservation()
        || !test_orphaned_lifecycle_is_invalid()
        || !test_exit_wait_and_reply_restore()
        || !test_wait_selection()
        || !test_parent_exit_reparents_and_aborts()
        || !test_nested_parent_exit_stages_independent_waits()
        || !test_replayable_transition_model()
    ) {
        return 1;
    }
    return 0;
}
