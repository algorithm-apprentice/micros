#include "tests/host/pm_coordinator_spawn_fixture.h"

static struct micros_ipc_message exit_wait_test_message(
    micros_endpoint_t source, uint32_t type, uint64_t token, uint64_t value
)
{
    struct micros_ipc_message message;

    memset(&message, 0, sizeof(message));
    message.source = source;
    message.type = type;
    message.reply_token = token;
    spawn_fixture_write_word(&message.payload[0], MICROS_PM_PROTOCOL_VERSION);
    if (type == MICROS_PM_MESSAGE_WAIT) {
        spawn_fixture_write_word(&message.payload[8], value);
    } else {
        spawn_fixture_write_word(&message.payload[8], (uint32_t)value);
    }
    return message;
}

static bool exit_wait_test_running_child(struct coordinator_spawn_fixture *fixture)
{
    struct micros_ipc_message request;
    struct micros_pm_coordinator_response response;

    COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize_parent(fixture));
    request = coordinator_spawn_fixture_request(fixture);
    COORDINATOR_EXPECT(micros_pm_coordinator_spawn_request(
        &fixture->coordinator, coordinator_spawn_fixture_backend(), fixture,
        &request, &response
    ) == MICROS_PM_COORDINATOR_OK);
    fixture->trace_count = 0;
    return true;
}

bool test_coordinator_exit_and_blocked_wait(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_ipc_message message;
    struct micros_pm_coordinator_response response;
    const uint32_t expected[] = {
        COORDINATOR_MOCK_EXIT_BEGIN, COORDINATOR_MOCK_VFS_EXIT,
        COORDINATOR_MOCK_EXIT_DETACH, COORDINATOR_MOCK_VM_EXIT,
        COORDINATOR_MOCK_RELEASE_PROCESS, COORDINATOR_MOCK_WAIT_REPLY,
    };

    COORDINATOR_EXPECT(exit_wait_test_running_child(&fixture));
    message = exit_wait_test_message(fixture.table.records[0].endpoint,
                                     MICROS_PM_MESSAGE_WAIT, 901, 2);
    COORDINATOR_EXPECT(
        micros_pm_coordinator_wait_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && response.kind == MICROS_PM_COORDINATOR_RESPONSE_WAIT_BLOCKED
        && fixture.reply_count == 0 && fixture.table.records[0].wait.reply_token == 901
    );
    message = exit_wait_test_message(fixture.table.records[1].endpoint,
                                     MICROS_PM_MESSAGE_EXIT, 778, (uint32_t)-17);
    COORDINATOR_EXPECT(
        micros_pm_coordinator_exit_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && response.kind == MICROS_PM_COORDINATOR_RESPONSE_EXIT_ACCEPTED
        && response.reply_token == 0 && !fixture.violation
        && fixture.reply_count == 1 && fixture.replies[0] == 901 && fixture.reply_pids[0] == 2
        && fixture.table.records[1].state == MICROS_PM_PROCESS_FREE
        && !fixture.table.records[0].wait.active
        && coordinator_spawn_fixture_resources_are_empty(&fixture)
        && fixture.trace_count == sizeof(expected) / sizeof(expected[0])
        && memcmp(fixture.trace, expected, sizeof(expected)) == 0
        && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
    );
    return true;
}

bool test_coordinator_exit_failure_boundaries(void)
{
    const uint32_t failures[] = {
        COORDINATOR_MOCK_EXIT_BEGIN, COORDINATOR_MOCK_VFS_EXIT,
        COORDINATOR_MOCK_EXIT_DETACH, COORDINATOR_MOCK_VM_EXIT,
        COORDINATOR_MOCK_RELEASE_PROCESS,
    };
    size_t index;

    for (index = 0; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        struct coordinator_spawn_fixture fixture;
        struct micros_ipc_message message;
        struct micros_pm_coordinator_response response;
        struct micros_pm_coordinator_response sentinel;
        struct micros_pm_table snapshot;

        COORDINATOR_EXPECT(exit_wait_test_running_child(&fixture));
        fixture.fail_step = failures[index];
        snapshot = fixture.table;
        memset(&sentinel, 0xa5, sizeof(sentinel));
        response = sentinel;
        message = exit_wait_test_message(fixture.table.records[1].endpoint,
                                         MICROS_PM_MESSAGE_EXIT, 778, 23);
        if (failures[index] == COORDINATOR_MOCK_EXIT_BEGIN) {
            COORDINATOR_EXPECT(
                micros_pm_coordinator_exit_request(
                    &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
                    &message, &response
                ) == MICROS_PM_COORDINATOR_OK
                && response.kind == MICROS_PM_COORDINATOR_RESPONSE_REPLY
                && coordinator_spawn_fixture_result_code(&response.message) == MICROS_PM_RESULT_STATE
                && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
                && !fixture.stopped && fixture.published
            );
        } else {
            COORDINATOR_EXPECT(
                micros_pm_coordinator_exit_request(
                    &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
                    &message, &response
                ) == MICROS_PM_COORDINATOR_ERROR_INVARIANT
                && memcmp(&response, &sentinel, sizeof(response)) == 0
                && fixture.stopped && fixture.exit_token_live
                && fixture.table.records[1].state == MICROS_PM_PROCESS_EXITING
                && fixture.reply_count == 0
            );
        }
        COORDINATOR_EXPECT(!fixture.violation);
    }
    return true;
}

bool test_coordinator_immediate_wait_reply_restore(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_ipc_message message;
    struct micros_pm_coordinator_response response;

    COORDINATOR_EXPECT(exit_wait_test_running_child(&fixture));
    message = exit_wait_test_message(fixture.table.records[1].endpoint,
                                     MICROS_PM_MESSAGE_EXIT, 778, 7);
    COORDINATOR_EXPECT(
        micros_pm_coordinator_exit_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && fixture.table.records[1].state == MICROS_PM_PROCESS_ZOMBIE
        && fixture.reply_count == 0 && coordinator_spawn_fixture_resources_are_empty(&fixture)
    );
    fixture.fail_reply = 901;
    message = exit_wait_test_message(fixture.table.records[0].endpoint,
                                     MICROS_PM_MESSAGE_WAIT, 901, 2);
    COORDINATOR_EXPECT(
        micros_pm_coordinator_wait_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && response.kind == MICROS_PM_COORDINATOR_RESPONSE_WAIT_REPLY_COMPLETED
        && fixture.reply_count == 1 && fixture.replies[0] == 901
        && fixture.table.records[1].state == MICROS_PM_PROCESS_ZOMBIE
        && !fixture.table.records[1].reap_pending && fixture.table.records[0].wait.active
        && !fixture.table.records[0].wait.result_ready
        && fixture.table.records[0].wait.reply_token == 901
        && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
    );
    return true;
}

static bool exit_wait_test_add_child(
    struct micros_pm_table *table, struct micros_pm_process_handle parent,
    micros_endpoint_t endpoint, struct micros_pm_process_handle *child
)
{
    uint64_t pid;
    uint64_t reply;

    COORDINATOR_EXPECT(
        micros_pm_begin_spawn(table, parent, 42, child, &pid) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_reserve_kernel(table, *child, 1000 + pid) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_image(table, *child, 5000 + pid) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_freeze_mappings(table, *child, 2000 + pid, 3000 + pid) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_descriptors(table, *child, 6000 + pid) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_prepare_execution(table, *child, 4000 + pid, endpoint) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_commit_hidden(table, *child, 8000 + pid) == MICROS_PM_MODEL_OK
        && micros_pm_spawn_activate(table, *child, endpoint, &reply) == MICROS_PM_MODEL_OK
        && reply == 42
    );
    return true;
}

bool test_coordinator_nested_independent_wait_replies(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_pm_process_handle init = {0, 0, 1};
    struct micros_pm_process_handle c;
    struct micros_pm_process_handle a;
    struct micros_pm_process_handle b;
    struct micros_pm_wait_outcomes outcomes;
    struct micros_ipc_message message;
    struct micros_pm_coordinator_response response;
    const micros_endpoint_t base = UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS;

    COORDINATOR_EXPECT(
        coordinator_spawn_fixture_initialize_parent(&fixture)
        && exit_wait_test_add_child(&fixture.table, init, base | 7, &c)
        && exit_wait_test_add_child(&fixture.table, c, base | 8, &a)
        && exit_wait_test_add_child(&fixture.table, a, base | 9, &b)
        && micros_pm_exit_begin(&fixture.table, b, 888, MICROS_PM_EXIT_NORMAL, 9,
                                &(struct micros_pm_exit_effects){0}) == MICROS_PM_MODEL_OK
        && micros_pm_exit_detach(&fixture.table, b, 888) == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_vm(&fixture.table, b, 888) == MICROS_PM_MODEL_OK
        && micros_pm_exit_release_process(&fixture.table, b, 888, &outcomes) == MICROS_PM_MODEL_OK
        && outcomes.count == 0
    );
    message = exit_wait_test_message(fixture.table.records[0].endpoint,
                                     MICROS_PM_MESSAGE_WAIT, 901, 0);
    COORDINATOR_EXPECT(micros_pm_coordinator_wait_request(
        &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture, &message, &response
    ) == MICROS_PM_COORDINATOR_OK && response.kind == MICROS_PM_COORDINATOR_RESPONSE_WAIT_BLOCKED);
    message = exit_wait_test_message(fixture.table.records[c.slot].endpoint,
                                     MICROS_PM_MESSAGE_WAIT, 902, fixture.table.records[a.slot].pid);
    COORDINATOR_EXPECT(micros_pm_coordinator_wait_request(
        &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture, &message, &response
    ) == MICROS_PM_COORDINATOR_OK && response.kind == MICROS_PM_COORDINATOR_RESPONSE_WAIT_BLOCKED);
    fixture.child_slot = a.slot;
    fixture.published = true;
    fixture.execution_live = true;
    fixture.vm_committed = true;
    fixture.vfs_process_live = true;
    fixture.descriptors_live = true;
    fixture.fail_reply = 901;
    message = exit_wait_test_message(fixture.table.records[a.slot].endpoint,
                                     MICROS_PM_MESSAGE_EXIT, 778, (uint32_t)-17);
    COORDINATOR_EXPECT(
        micros_pm_coordinator_exit_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && !fixture.violation && response.kind == MICROS_PM_COORDINATOR_RESPONSE_EXIT_ACCEPTED
        && fixture.reply_count == 2 && fixture.replies[0] == 901 && fixture.replies[1] == 902
        && fixture.reply_pids[0] == fixture.table.records[b.slot].pid && fixture.reply_pids[1] == 3
        && fixture.table.records[a.slot].state == MICROS_PM_PROCESS_FREE
        && fixture.table.records[b.slot].state == MICROS_PM_PROCESS_ZOMBIE
        && fixture.table.records[b.slot].parent.slot == 0 && !fixture.table.records[b.slot].reap_pending
        && fixture.table.records[0].wait.active && !fixture.table.records[0].wait.result_ready
        && !fixture.table.records[c.slot].wait.active
        && coordinator_spawn_fixture_resources_are_empty(&fixture)
        && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
    );
    return true;
}
