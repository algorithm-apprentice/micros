#include "tests/host/pm_test_internal.h"

bool test_wait_selection(void)
{
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle first;
    struct micros_pm_process_handle second;
    struct micros_pm_process_handle third;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    uint64_t first_pid;
    uint64_t second_pid;
    uint64_t third_pid;

    EXPECT_TRUE(
        pm_test_initialize_running_init(&table, &init)
        && pm_test_spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002001),
            &first,
            &first_pid
        )
        && pm_test_spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002002),
            &second,
            &second_pid
        )
        && pm_test_spawn_running_child(
            &table,
            init,
            UINT32_C(0x00002003),
            &third,
            &third_pid
        )
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            MICROS_PM_WAIT_NOHANG,
            50,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && outcome.result == MICROS_PM_RESULT_OK
        && outcome.child_pid == 0
        && !table.records[init.slot].wait.active
        && micros_pm_wait_begin(
            &table,
            init,
            UINT64_C(999),
            0,
            51,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.result == MICROS_PM_RESULT_NO_CHILD
    );
    EXPECT_TRUE(
        pm_test_complete_exit(&table, second, 601, 22, &outcomes)
        && outcomes.count == 0
        && pm_test_bytes_are_zero(&outcomes, sizeof(outcomes))
        && pm_test_complete_exit(&table, first, 602, 11, &outcomes)
        && outcomes.count == 0
        && pm_test_bytes_are_zero(&outcomes, sizeof(outcomes))
        && first_pid < second_pid
        && second_pid < third_pid
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            0,
            52,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && outcome.child_pid == first_pid
        && outcome.exit_code == 11
        && micros_pm_wait_reply_complete(&table, init, 52, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_wait_begin(
            &table,
            init,
            0,
            0,
            53,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.child_pid == second_pid
        && outcome.exit_code == 22
        && micros_pm_wait_reply_complete(&table, init, 53, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_wait_begin(
            &table,
            init,
            second_pid,
            0,
            54,
            &outcome
        ) == MICROS_PM_MODEL_OK
        && outcome.result == MICROS_PM_RESULT_NO_CHILD
        && micros_pm_table_validate(&table) == MICROS_PM_MODEL_OK
    );
    return true;
}

struct exit_wait_case {
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    struct micros_pm_wait_outcomes outcomes_sentinel;
    uint64_t pid;
};

static bool stage_exit_wait(struct exit_wait_case *context)
{
    EXPECT_TRUE(
        pm_test_initialize_running_init(&context->table, &context->init)
        && pm_test_spawn_running_child(
            &context->table,
            context->init,
            UINT32_C(0x00002001),
            &context->child,
            &context->pid
        )
        && context->pid == 2
        && micros_pm_wait_begin(
            &context->table,
            context->init,
            context->pid,
            0,
            41,
            &context->outcome
        ) == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
        && context->outcome.reply_token == 41
    );
    EXPECT_TRUE(
        micros_pm_wait_begin(
            &context->table,
            context->init,
            0,
            0,
            42,
            &context->outcome
        ) == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && context->outcome.result == MICROS_PM_RESULT_BUSY
        && context->outcome.reply_token == 42
        && context->table.records[context->init.slot].wait.reply_token == 41
    );

    return true;
}

static bool release_exit_into_wait_result(struct exit_wait_case *context)
{
    memset(&context->outcomes_sentinel, 0xa5, sizeof(context->outcomes_sentinel));
    context->outcomes = context->outcomes_sentinel;
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &context->table,
            context->child,
            501,
            MICROS_PM_EXIT_NORMAL,
            -17,
            &(struct micros_pm_exit_effects){0}
        ) == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->state == MICROS_PM_PROCESS_EXITING
        && context->record->exit_stage == MICROS_PM_EXIT_STAGE_STOPPED
        && context->record->endpoint == UINT32_C(0x00002001)
        && context->record->exit_transaction == 501
        && context->record->exit_kind == MICROS_PM_EXIT_NORMAL
        && context->record->exit_code == -17
        && micros_pm_exit_detach(&context->table, context->child, 500)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_exit_detach(&context->table, context->child, 501)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->exit_stage
            == MICROS_PM_EXIT_STAGE_VM_RELEASE_PENDING
        && context->record->endpoint == MICROS_ENDPOINT_NONE
        && micros_pm_exit_release_process(
            &context->table,
            context->child,
            501,
            &context->outcomes
        ) == MICROS_PM_MODEL_ERROR_STATE
        && memcmp(
            &context->outcomes,
            &context->outcomes_sentinel,
            sizeof(context->outcomes)
        ) == 0
        && micros_pm_exit_release_vm(&context->table, context->child, 501)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_process(
            &context->table,
            context->child,
            501,
            &context->outcomes
        ) == MICROS_PM_MODEL_OK
        && context->outcomes.count == 1
        && context->outcomes.reserved == 0
        && context->outcomes.entries[0].action
            == MICROS_PM_WAIT_ACTION_REPLY
        && context->outcomes.entries[0].result == MICROS_PM_RESULT_OK
        && context->outcomes.entries[0].reply_token == 41
        && context->outcomes.entries[0].child_pid == 2
        && context->outcomes.entries[0].exit_kind == MICROS_PM_EXIT_NORMAL
        && context->outcomes.entries[0].exit_code == -17
        && pm_test_handles_equal(context->outcomes.entries[0].parent, context->init)
        && pm_test_bytes_are_zero(
            &context->outcomes.entries[1],
            sizeof(context->outcomes.entries[1])
        )
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->state == MICROS_PM_PROCESS_ZOMBIE
        && context->record->reap_pending
    );
    return true;
}

static bool restore_and_collect_exit_wait(struct exit_wait_case *context)
{
    EXPECT_TRUE(
        micros_pm_wait_reply_complete(&context->table, context->init, 41, false)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->state == MICROS_PM_PROCESS_ZOMBIE
        && !context->record->reap_pending
        && context->table.records[context->init.slot].wait.active
        && !context->table.records[context->init.slot].wait.result_ready
        && micros_pm_wait_resume(&context->table, context->init, &context->outcome)
            == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && context->outcome.child_pid == 2
        && micros_pm_wait_reply_complete(&context->table, context->init, 40, true)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_wait_reply_complete(&context->table, context->init, 41, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_ERROR_STALE
        && !context->table.records[context->init.slot].wait.active
        && micros_pm_table_validate(&context->table) == MICROS_PM_MODEL_OK
    );
    return true;
}

bool test_exit_wait_and_reply_restore(void)
{
    struct exit_wait_case context;

    return (
        stage_exit_wait(&context)
        && release_exit_into_wait_result(&context)
        && restore_and_collect_exit_wait(&context)
    );
}

struct nested_exit_case {
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle grandparent;
    struct micros_pm_process_handle parent;
    struct micros_pm_process_handle child;
    const struct micros_pm_process_record *record;
    struct micros_pm_exit_effects effects;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    uint64_t grandparent_pid;
    uint64_t parent_pid;
    uint64_t child_pid;
};

static bool stage_nested_waits(struct nested_exit_case *context)
{
    EXPECT_TRUE(
        pm_test_initialize_running_init(&context->table, &context->init)
        && pm_test_spawn_running_child(
            &context->table,
            context->init,
            UINT32_C(0x00002001),
            &context->grandparent,
            &context->grandparent_pid
        )
        && pm_test_spawn_running_child(
            &context->table,
            context->grandparent,
            UINT32_C(0x00003001),
            &context->parent,
            &context->parent_pid
        )
        && pm_test_spawn_running_child(
            &context->table,
            context->parent,
            UINT32_C(0x00004001),
            &context->child,
            &context->child_pid
        )
        && pm_test_complete_exit(&context->table, context->child, 801, 33, &context->outcomes)
        && context->outcomes.count == 0
        && micros_pm_wait_begin(
            &context->table,
            context->init,
            0,
            0,
            81,
            &context->outcome
        ) == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
        && micros_pm_wait_begin(
            &context->table,
            context->grandparent,
            context->parent_pid,
            0,
            82,
            &context->outcome
        ) == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
    );
    return true;
}

static bool reparent_nested_exit(struct nested_exit_case *context)
{
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &context->table,
            context->parent,
            802,
            MICROS_PM_EXIT_NORMAL,
            44,
            &context->effects
        ) == MICROS_PM_MODEL_OK
        && !context->effects.aborted_spawn
        && context->effects.reparented_count == 1
        && context->table.records[context->init.slot].wait.active
        && !context->table.records[context->init.slot].wait.result_ready
        && context->table.records[context->grandparent.slot].wait.active
        && !context->table.records[context->grandparent.slot].wait.result_ready
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_OK
        && pm_test_handles_equal(context->record->parent, context->init)
        && !context->record->reap_pending
    );
    return true;
}

static bool publish_nested_wait_outcomes(struct nested_exit_case *context)
{
    EXPECT_TRUE(
        micros_pm_exit_detach(&context->table, context->parent, 802)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_vm(&context->table, context->parent, 802)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_process(
            &context->table,
            context->parent,
            802,
            &context->outcomes
        ) == MICROS_PM_MODEL_OK
        && context->outcomes.count == 2
        && context->outcomes.reserved == 0
        && context->outcomes.entries[0].action
            == MICROS_PM_WAIT_ACTION_REPLY
        && context->outcomes.entries[0].reply_token == 81
        && context->outcomes.entries[0].child_pid == context->child_pid
        && context->outcomes.entries[0].exit_code == 33
        && pm_test_handles_equal(context->outcomes.entries[0].parent, context->init)
        && pm_test_handles_equal(context->outcomes.entries[0].child, context->child)
        && context->outcomes.entries[1].action
            == MICROS_PM_WAIT_ACTION_REPLY
        && context->outcomes.entries[1].reply_token == 82
        && context->outcomes.entries[1].child_pid == context->parent_pid
        && context->outcomes.entries[1].exit_code == 44
        && pm_test_handles_equal(
            context->outcomes.entries[1].parent,
            context->grandparent
        )
        && pm_test_handles_equal(context->outcomes.entries[1].child, context->parent)
    );
    return true;
}

static bool complete_nested_waits_independently(struct nested_exit_case *context)
{
    EXPECT_TRUE(
        micros_pm_wait_reply_complete(&context->table, context->init, 81, false)
            == MICROS_PM_MODEL_OK
        && context->table.records[context->init.slot].wait.active
        && !context->table.records[context->init.slot].wait.result_ready
        && context->table.records[context->grandparent.slot].wait.result_ready
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_OK
        && !context->record->reap_pending
        && micros_pm_process_resolve(&context->table, context->parent, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->reap_pending
        && micros_pm_wait_reply_complete(
            &context->table,
            context->grandparent,
            82,
            true
        ) == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&context->table, context->parent, &context->record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_wait_resume(&context->table, context->init, &context->outcome)
            == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && context->outcome.reply_token == 81
        && context->outcome.child_pid == context->child_pid
        && pm_test_handles_equal(context->outcome.parent, context->init)
        && micros_pm_wait_reply_complete(&context->table, context->init, 81, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&context->table, context->child, &context->record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_table_validate(&context->table) == MICROS_PM_MODEL_OK
    );
    (void)context->grandparent_pid;
    return true;
}

bool test_nested_parent_exit_stages_independent_waits(void)
{
    struct nested_exit_case context;

    return (
        stage_nested_waits(&context)
        && reparent_nested_exit(&context)
        && publish_nested_wait_outcomes(&context)
        && complete_nested_waits_independently(&context)
    );
}

struct reparent_case {
    struct micros_pm_table table;
    struct micros_pm_process_handle init;
    struct micros_pm_process_handle parent;
    struct micros_pm_process_handle zombie;
    struct micros_pm_process_handle running;
    struct micros_pm_process_handle hidden;
    const struct micros_pm_process_record *record;
    struct micros_pm_table snapshot;
    struct micros_pm_exit_effects effects;
    struct micros_pm_exit_effects effects_sentinel;
    struct micros_pm_wait_outcome outcome;
    struct micros_pm_wait_outcomes outcomes;
    uint32_t released;
    uint64_t parent_pid;
    uint64_t zombie_pid;
    uint64_t running_pid;
    uint64_t hidden_pid;
};

static bool stage_reparent_children(struct reparent_case *context)
{
    EXPECT_TRUE(
        pm_test_initialize_running_init(&context->table, &context->init)
        && pm_test_spawn_running_child(
            &context->table,
            context->init,
            UINT32_C(0x00002001),
            &context->parent,
            &context->parent_pid
        )
        && pm_test_spawn_running_child(
            &context->table,
            context->parent,
            UINT32_C(0x00003001),
            &context->zombie,
            &context->zombie_pid
        )
        && pm_test_spawn_running_child(
            &context->table,
            context->parent,
            UINT32_C(0x00003002),
            &context->running,
            &context->running_pid
        )
        && micros_pm_wait_begin(
            &context->table,
            context->parent,
            context->zombie_pid,
            0,
            70,
            &context->outcome
        ) == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
        && pm_test_complete_exit(&context->table, context->zombie, 701, 33, &context->outcomes)
        && context->outcomes.count == 1
        && context->outcomes.entries[0].child_pid == context->zombie_pid
        && micros_pm_begin_spawn(
            &context->table,
            context->parent,
            72,
            &context->hidden,
            &context->hidden_pid
        ) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_reserve_kernel(&context->table, context->hidden, 702)
            == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_image(&context->table, context->hidden, 703)
            == MICROS_PM_MODEL_OK
        && micros_pm_wait_begin(
            &context->table,
            context->init,
            0,
            0,
            71,
            &context->outcome
        ) == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_BLOCKED
    );
    return true;
}

static bool reject_parent_exit_then_rollback(struct reparent_case *context)
{
    context->snapshot = context->table;
    memset(&context->effects_sentinel, 0xa5, sizeof(context->effects_sentinel));
    context->effects = context->effects_sentinel;
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &context->table,
            context->parent,
            704,
            MICROS_PM_EXIT_NORMAL,
            44,
            &context->effects
        ) == MICROS_PM_MODEL_ERROR_BUSY
        && memcmp(&context->effects, &context->effects_sentinel, sizeof(context->effects)) == 0
        && memcmp(&context->table, &context->snapshot, sizeof(context->table)) == 0
        && pm_test_rollback_spawn(
            &context->table,
            context->hidden,
            MICROS_PM_SPAWN_VFS_IMAGE_PREPARED,
            72,
            true,
            &context->released
        )
        && context->released
            == (
                MICROS_PM_RESOURCE_KERNEL_PROCESS
                | MICROS_PM_RESOURCE_VFS_IMAGE
            )
    );
    return true;
}

static bool commit_parent_reparent(struct reparent_case *context)
{
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &context->table,
            context->parent,
            704,
            MICROS_PM_EXIT_NORMAL,
            44,
            &context->effects
        ) == MICROS_PM_MODEL_OK
        && !context->effects.aborted_spawn
        && context->effects.reparented_count == 2
        && !context->table.records[context->parent.slot].wait.active
        && context->table.records[context->init.slot].wait.active
        && !context->table.records[context->init.slot].wait.result_ready
        && micros_pm_process_resolve(&context->table, context->hidden, &context->record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_process_resolve(&context->table, context->zombie, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->parent.slot == context->init.slot
        && context->record->parent.generation == context->init.generation
        && !context->record->reap_pending
        && micros_pm_process_resolve(&context->table, context->running, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->parent.slot == context->init.slot
        && context->record->parent.generation == context->init.generation
        && micros_pm_process_resolve(&context->table, context->parent, &context->record)
            == MICROS_PM_MODEL_OK
        && context->record->state == MICROS_PM_PROCESS_EXITING
        && context->record->parent.slot == context->init.slot
        && context->record->parent.generation == context->init.generation
    );
    return true;
}

static bool release_reparented_parent(struct reparent_case *context)
{
    EXPECT_TRUE(
        micros_pm_exit_begin(
            &context->table,
            context->init,
            705,
            MICROS_PM_EXIT_NORMAL,
            0,
            &context->effects
        ) == MICROS_PM_MODEL_ERROR_INVARIANT
        && micros_pm_exit_detach(&context->table, context->parent, 704)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_vm(&context->table, context->parent, 704)
            == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_process(
            &context->table,
            context->parent,
            704,
            &context->outcomes
        ) == MICROS_PM_MODEL_OK
        && context->outcomes.count == 1
        && context->outcomes.reserved == 0
        && context->outcomes.entries[0].action
            == MICROS_PM_WAIT_ACTION_REPLY
        && context->outcomes.entries[0].reply_token == 71
        && context->outcomes.entries[0].child_pid == context->parent_pid
        && context->outcomes.entries[0].exit_kind
            == MICROS_PM_EXIT_NORMAL
        && context->outcomes.entries[0].exit_code == 44
        && pm_test_handles_equal(context->outcomes.entries[0].parent, context->init)
        && pm_test_handles_equal(context->outcomes.entries[0].child, context->parent)
        && pm_test_bytes_are_zero(
            &context->outcomes.entries[1],
            sizeof(context->outcomes.entries[1])
        )
        && micros_pm_process_resolve(&context->table, context->zombie, &context->record)
            == MICROS_PM_MODEL_OK
        && !context->record->reap_pending
    );
    return true;
}

static bool collect_reparented_results(struct reparent_case *context)
{
    EXPECT_TRUE(
        micros_pm_wait_reply_complete(&context->table, context->init, 71, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_process_resolve(&context->table, context->parent, &context->record)
            == MICROS_PM_MODEL_ERROR_STALE
        && micros_pm_wait_begin(
            &context->table,
            context->init,
            0,
            0,
            73,
            &context->outcome
        ) == MICROS_PM_MODEL_OK
        && context->outcome.action == MICROS_PM_WAIT_ACTION_REPLY
        && context->outcome.child_pid == context->zombie_pid
        && micros_pm_wait_reply_complete(&context->table, context->init, 73, true)
            == MICROS_PM_MODEL_OK
        && micros_pm_table_validate(&context->table) == MICROS_PM_MODEL_OK
    );
    (void)context->running_pid;
    (void)context->hidden_pid;
    return true;
}

bool test_parent_exit_reparents_and_aborts(void)
{
    struct reparent_case context;

    return (
        stage_reparent_children(&context)
        && reject_parent_exit_then_rollback(&context)
        && commit_parent_reparent(&context)
        && release_reparented_parent(&context)
        && collect_reparented_results(&context)
    );
}
