#include "tests/host/pm_test_internal.h"

bool test_orphaned_lifecycle_is_invalid(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    struct micros_pm_wait_outcomes outcomes;
    uint64_t pid;

    EXPECT_TRUE(
        pm_test_initialize_running_init(&table, &init)
        && pm_test_spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002001),
            &child,
            &pid
        )
        && pm_test_complete_exit(&table, child, 901, 9, &outcomes)
        && outcomes.count == 0
    );
    memset(
        &table.records[child.slot].parent,
        0,
        sizeof(table.records[child.slot].parent)
    );
    EXPECT_TRUE(
        micros_pm_table_validate(&table)
            == MICROS_PM_MODEL_ERROR_INVARIANT
    );
    return true;
}

struct failure_preservation_case {
    struct micros_pm_table table;
    struct micros_pm_table snapshot;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    struct micros_pm_process_handle found;
    struct micros_pm_process_handle found_sentinel;
    struct micros_pm_exit_effects effects;
    struct micros_pm_exit_effects effects_sentinel;
    struct micros_pm_spawn_rollback_action rollback;
    struct micros_pm_spawn_rollback_action rollback_sentinel;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcome outcome_sentinel;
    uint64_t pid;
    uint64_t found_pid;
    uint64_t found_pid_sentinel;
};

static bool check_endpoint_identity(struct failure_preservation_case *context)
{
    EXPECT_TRUE(
        pm_test_initialize_running_init(&context->table, &context->init)
        && micros_pm_find_running_by_endpoint(
            &context->table,
            UINT32_C(0x00001000),
            &context->found,
            &context->found_pid
        ) == MICROS_PM_MODEL_OK
        && context->found.slot == context->init.slot
        && context->found.generation == context->init.generation
        && context->found_pid == 1
        && micros_pm_begin_spawn(
            &context->table,
            context->init,
            800,
            &context->child,
            &context->pid
        )
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_reserve_kernel(&context->table, context->child, 801)
            == MICROS_PM_MODEL_OK
    );

    memset(&context->found_sentinel, 0xa5, sizeof(context->found_sentinel));
    context->found = context->found_sentinel;
    context->found_pid = context->found_pid_sentinel;
    EXPECT_TRUE(
        micros_pm_find_running_by_endpoint(
            &context->table,
            UINT32_C(0x00002001),
            &context->found,
            &context->found_pid
        ) == MICROS_PM_MODEL_ERROR_STALE
        && memcmp(&context->found, &context->found_sentinel, sizeof(context->found)) == 0
        && context->found_pid == context->found_pid_sentinel
    );
    return true;
}

static bool check_spawn_failure_preservation(struct failure_preservation_case *context)
{
    context->snapshot = context->table;
    memset(&context->rollback_sentinel, 0xa5, sizeof(context->rollback_sentinel));
    context->rollback = context->rollback_sentinel;
    EXPECT_TRUE(
        micros_pm_spawn_rollback_begin(
            &context->table,
            context->child,
            MICROS_PM_SPAWN_VFS_IMAGE_PREPARED,
            800,
            false,
            &context->rollback
        ) == MICROS_PM_MODEL_ERROR_STATE
        && memcmp(
            &context->rollback,
            &context->rollback_sentinel,
            sizeof(context->rollback)
        ) == 0
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
    );
    EXPECT_TRUE(
        micros_pm_spawn_prepare_image(&context->table, context->child, 0)
            == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
    );
    EXPECT_TRUE(
        micros_pm_spawn_prepare_image(&context->table, context->child, 802)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_freeze_mappings(
            &context->table,
            context->child,
            803,
            804
        ) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_descriptors(&context->table, context->child, 805)
            == MICROS_PM_MODEL_OK
    );
    context->snapshot = context->table;
    EXPECT_TRUE(
        micros_pm_spawn_prepare_execution(&context->table, context->child, 806, 1)
            == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
        && micros_pm_spawn_prepare_execution(
            &context->table,
            context->child,
            806,
            UINT32_C(0x00001000)
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
    );

    return true;
}

static bool check_exit_wait_failure_preservation(
    struct failure_preservation_case *context
)
{
    memset(&context->effects_sentinel, 0xa5, sizeof(context->effects_sentinel));
    context->effects = context->effects_sentinel;
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &context->table,
            context->init,
            802,
            MICROS_PM_EXIT_NORMAL,
            0,
            &context->effects
        ) == MICROS_PM_MODEL_ERROR_INVARIANT
        && memcmp(&context->effects, &context->effects_sentinel, sizeof(context->effects)) == 0
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
    );

    memset(&context->outcome_sentinel, 0xa5, sizeof(context->outcome_sentinel));
    context->outcome = context->outcome_sentinel;
    EXPECT_TRUE(
        micros_pm_wait_begin(
            &context->table,
            context->init,
            0,
            UINT32_C(0x2),
            803,
            &context->outcome
        ) == MICROS_PM_MODEL_ERROR_ARGUMENT
        && memcmp(&context->outcome, &context->outcome_sentinel, sizeof(context->outcome)) == 0
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
        && micros_pm_table_validate(&context->table) == MICROS_PM_MODEL_OK
    );
    return true;
}

bool test_endpoint_lookup_and_failure_preservation(void)
{
    struct failure_preservation_case context;

    context.found_pid_sentinel = UINT64_C(0xa5a5a5a5a5a5a5a5);
    return (
        check_endpoint_identity(&context)
        && check_spawn_failure_preservation(&context)
        && check_exit_wait_failure_preservation(&context)
    );
}
