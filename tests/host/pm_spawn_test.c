#include "tests/host/pm_test_internal.h"

bool test_runtime_gate_and_spawn(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    struct micros_ipc_message event = pm_test_sealed_event();
    uint64_t pid = 0;

    memset(&table, 0, sizeof(table));
    EXPECT_TRUE(
        micros_pm_table_initialize(&table) == MICROS_PM_MODEL_OK
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
        && micros_pm_begin_init(&table, &init, &pid)
            == MICROS_PM_MODEL_ERROR_STATE
    );
    event.source = UINT32_C(1);
    EXPECT_TRUE(
        micros_pm_enable_runtime(&table, &event)
            == MICROS_PM_MODEL_ERROR_ARGUMENT
        && !table.runtime_enabled
    );
    event = pm_test_sealed_event();
    EXPECT_TRUE(
        micros_pm_enable_runtime(&table, &event)
            == MICROS_PM_MODEL_OK
        && table.runtime_enabled
        && micros_pm_enable_runtime(&table, &event)
            == MICROS_PM_MODEL_ERROR_STATE
    );
    EXPECT_TRUE(
        micros_pm_begin_init(&table, &init, &pid)
            == MICROS_PM_MODEL_OK
        && pid == 1
        && micros_pm_process_resolve(&table, init, &record)
            == MICROS_PM_MODEL_OK
        && record->state == MICROS_PM_PROCESS_SPAWNING
        && record->spawn_stage
            == MICROS_PM_SPAWN_PM_RECORD_RESERVED
        && record->endpoint == MICROS_ENDPOINT_NONE
        && record->load_token == 0
        && record->mapping_generation == 0
        && table.reaper.generation == 0
    );
    EXPECT_TRUE(
        pm_test_complete_spawn(&table, init, UINT32_C(0x00001000), 0)
        && micros_pm_process_resolve(&table, init, &record)
            == MICROS_PM_MODEL_OK
        && record->state == MICROS_PM_PROCESS_RUNNING
        && record->reaper
        && record->endpoint == UINT32_C(0x00001000)
        && record->load_token == 13
        && record->mapping_generation == 14
        && table.reaper.slot == init.slot
        && table.reaper.generation == init.generation
    );
    table.records[init.slot].load_token = 0;
    EXPECT_TRUE(
        micros_pm_table_validate(&table) == MICROS_PM_MODEL_ERROR_INVARIANT
    );
    table.records[init.slot].load_token = 13;
    table.records[init.slot].mapping_generation = 0;
    EXPECT_TRUE(
        micros_pm_table_validate(&table) == MICROS_PM_MODEL_ERROR_INVARIANT
    );
    table.records[init.slot].mapping_generation = 14;
    EXPECT_TRUE(
        micros_pm_begin_spawn(&table, init, 101, &child, &pid)
            == MICROS_PM_MODEL_OK
        && pid == 2
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_OK
        && record->spawn.parent_reply_token == 101
        && micros_pm_spawn_prepare_image(&table, child, 1)
            == MICROS_PM_MODEL_ERROR_STATE
        && pm_test_complete_spawn(
            &table,
            child,
            UINT32_C(0x00002001),
            101
        )
        && record->load_token == 13
        && record->mapping_generation == 14
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

bool test_reverse_rollback_and_identity(void)
{
    enum micros_pm_spawn_stage stage;

    for (
        stage = MICROS_PM_SPAWN_PM_RECORD_RESERVED;
        stage <= MICROS_PM_SPAWN_PM_VFS_COMMITTED;
        ++stage
    ) {
        struct micros_pm_table table;
        struct micros_pm_process_handle init;
        struct micros_pm_process_handle child;
        struct micros_pm_process_handle next;
        const struct micros_pm_process_record *record;
        uint32_t released = UINT32_MAX;
        uint64_t pid;

        EXPECT_TRUE(pm_test_initialize_running_init(&table, &init));
        EXPECT_TRUE(
            micros_pm_begin_spawn(&table, init, 100, &child, &pid)
                == MICROS_PM_MODEL_OK
            && pid == 2
            && pm_test_advance_to_stage(&table, child, stage)
            && pm_test_rollback_spawn(
                &table,
                child,
                stage,
                100,
                false,
                &released
            )
            && released == pm_test_expected_resources(stage)
            && micros_pm_process_resolve(&table, child, &record)
                == MICROS_PM_MODEL_ERROR_STALE
            && micros_pm_begin_spawn(
                &table,
                init,
                101,
                &next,
                &pid
            )
                == MICROS_PM_MODEL_OK
            && pid == 3
            && next.slot == child.slot
            && next.generation == child.generation + 1
            && micros_pm_table_validate(&table)
                == MICROS_PM_MODEL_OK
        );
    }
    return true;
}

bool test_generation_and_pid_exhaustion(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    uint32_t released;
    uint64_t pid;

    EXPECT_TRUE(pm_test_initialize_running_init(&table, &init));
    table.last_pid = UINT64_MAX - 1;
    EXPECT_TRUE(
        micros_pm_begin_spawn(&table, init, 200, &child, &pid)
            == MICROS_PM_MODEL_OK
        && pid == UINT64_MAX
        && table.pid_exhausted
        && pm_test_rollback_spawn(
            &table,
            child,
            MICROS_PM_SPAWN_PM_RECORD_RESERVED,
            200,
            false,
            &released
        )
        && micros_pm_begin_spawn(
            &table,
            init,
            201,
            &child,
            &pid
        )
            == MICROS_PM_MODEL_ERROR_CAPACITY
    );

    EXPECT_TRUE(pm_test_initialize_running_init(&table, &init));
    table.records[1].generation = UINT32_MAX - 1;
    EXPECT_TRUE(
        micros_pm_begin_spawn(&table, init, 202, &child, &pid)
            == MICROS_PM_MODEL_OK
        && child.slot == 1
        && child.generation == UINT32_MAX
        && pm_test_rollback_spawn(
            &table,
            child,
            MICROS_PM_SPAWN_PM_RECORD_RESERVED,
            202,
            false,
            &released
        )
        && table.records[1].state
            == MICROS_PM_PROCESS_QUARANTINED
        && micros_pm_process_resolve(&table, child, &record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_table_validate(&table)
            == MICROS_PM_MODEL_OK
    );
    return true;
}

bool test_failed_init_is_terminal(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_ipc_message event = pm_test_sealed_event();
    uint32_t released = UINT32_MAX;
    uint64_t pid = 0;

    memset(&table, 0, sizeof(table));
    EXPECT_TRUE(
        micros_pm_table_initialize(&table) == MICROS_PM_MODEL_OK
        && micros_pm_enable_runtime(&table, &event)
            == MICROS_PM_MODEL_OK
        && micros_pm_begin_init(&table, &init, &pid)
            == MICROS_PM_MODEL_OK
        && pid == 1
        && pm_test_rollback_spawn(
            &table,
            init,
            MICROS_PM_SPAWN_PM_RECORD_RESERVED,
            0,
            false,
            &released
        )
        && released == 0
        && table.init_attempted
        && table.last_pid == 1
        && table.reaper.generation == 0
        && micros_pm_begin_init(&table, &init, &pid)
            == MICROS_PM_MODEL_ERROR_STATE
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

struct rollback_identity_case {
    struct micros_pm_table table;
    struct micros_pm_table snapshot;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    struct micros_pm_process_handle next_child;
    const struct micros_pm_process_record *record;
    struct micros_pm_spawn_rollback_action action;
    struct micros_pm_spawn_rollback_action output;
    struct micros_pm_spawn_rollback_action output_sentinel;
    uint64_t pid;
};

static bool check_rejected_rollback_identity(struct rollback_identity_case *context)
{
    EXPECT_TRUE(
        pm_test_initialize_running_init(&context->table, &context->init)
        && micros_pm_begin_spawn(
            &context->table,
            context->init,
            300,
            &context->child,
            &context->pid
        ) == MICROS_PM_MODEL_OK
        && pm_test_advance_to_stage(
            &context->table,
            context->child,
            MICROS_PM_SPAWN_PM_VFS_COMMITTED
        )
    );

    context->snapshot = context->table;
    memset(&context->output_sentinel, 0xa5, sizeof(context->output_sentinel));
    context->output = context->output_sentinel;
    EXPECT_TRUE(
        micros_pm_spawn_rollback_begin(
            &context->table,
            context->child,
            MICROS_PM_SPAWN_PM_VFS_COMMITTED,
            301,
            false,
            &context->output
        ) == MICROS_PM_MODEL_ERROR_STALE
        && memcmp(&context->output, &context->output_sentinel, sizeof(context->output)) == 0
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
        && micros_pm_spawn_rollback_begin(
            &context->table,
            context->child,
            MICROS_PM_SPAWN_PM_VFS_COMMITTED,
            300,
            false,
            &context->action
        ) == MICROS_PM_MODEL_OK
        && context->action.resource == MICROS_PM_ROLLBACK_PM_VFS_COMMIT
        && context->action.expected_stage
            == MICROS_PM_SPAWN_PM_VFS_COMMITTED
        && context->action.token == 27
        && !context->action.complete
    );

    context->snapshot = context->table;
    context->output = context->output_sentinel;
    EXPECT_TRUE(
        micros_pm_spawn_rollback_ack(
            &context->table,
            context->child,
            context->action.expected_stage,
            context->action.resource,
            context->action.token + 1,
            context->action.secondary_token,
            context->action.endpoint,
            &context->output
        ) == MICROS_PM_MODEL_ERROR_STALE
        && memcmp(&context->output, &context->output_sentinel, sizeof(context->output)) == 0
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->spawn.rollback_active
        && micros_pm_begin_spawn(
            &context->table,
            context->init,
            302,
            &context->next_child,
            &context->pid
        ) == MICROS_PM_MODEL_ERROR_STATE
    );

    return true;
}

static bool complete_rollback_identity(struct rollback_identity_case *context)
{
    enum micros_pm_spawn_stage current_stage = MICROS_PM_SPAWN_PM_VFS_COMMITTED;
    uint32_t released = 0;

    while (!context->action.complete) {
        EXPECT_TRUE(
            context->action.expected_stage == current_stage
            && context->action.resource
                == pm_test_rollback_resource_for_stage(current_stage)
        );
        released |= pm_test_rollback_resource_bit(context->action.resource);
        EXPECT_TRUE(
            micros_pm_spawn_rollback_ack(
                &context->table,
                context->child,
                context->action.expected_stage,
                context->action.resource,
                context->action.token,
                context->action.secondary_token,
                context->action.endpoint,
                &context->action
            ) == MICROS_PM_MODEL_OK
        );
        current_stage = (
            enum micros_pm_spawn_stage
        )((unsigned int)current_stage - 1U);
        if (!context->action.complete) {
            EXPECT_TRUE(
                micros_pm_process_resolve(&context->table, context->child, &context->record)
                    == MICROS_PM_MODEL_OK
                && context->record->spawn.rollback_active
            );
        }
    }
    EXPECT_TRUE(
        released
            == pm_test_expected_resources(MICROS_PM_SPAWN_PM_VFS_COMMITTED)
        && context->action.reply_token == 300
        && !context->action.parent_lost
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_table_validate(&context->table) == MICROS_PM_MODEL_OK
    );
    return true;
}

bool test_rollback_acknowledgment_identity(void)
{
    struct rollback_identity_case context;

    return (
        check_rejected_rollback_identity(&context)
        && complete_rollback_identity(&context)
    );
}
