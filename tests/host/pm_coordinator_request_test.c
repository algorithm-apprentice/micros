#include "tests/host/pm_coordinator_spawn_fixture.h"

bool test_coordinator_public_spawn(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_ipc_message message;
    struct micros_pm_coordinator_response response;
    struct micros_pm_process_record parent;

    COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize_parent(&fixture));
    parent = fixture.table.records[0];
    message = coordinator_spawn_fixture_request(&fixture);
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && !fixture.violation && response.kind == MICROS_PM_COORDINATOR_RESPONSE_REPLY
        && response.reserved == 0 && response.reply_token == fixture.parent_reply
        && response.message.type == MICROS_PM_MESSAGE_RESULT
        && coordinator_spawn_fixture_result_code(&response.message) == MICROS_PM_RESULT_OK
        && coordinator_spawn_fixture_result_pid(&response.message) == 2
        && fixture.table.records[1].state == MICROS_PM_PROCESS_RUNNING
        && fixture.table.records[1].parent.slot == 0
        && fixture.table.records[1].parent.generation == parent.generation
        && fixture.table.records[1].load_token == 201
        && fixture.table.records[1].mapping_generation == 301
        && fixture.table.last_pid == 2 && fixture.coordinator.last_spawn_token == 2
        && memcmp(&fixture.table.records[0], &parent, sizeof(parent)) == 0
        && fixture.published && fixture.vm_committed && !fixture.image_live
        && !fixture.grant_live && !fixture.coordinator.spawn_pending
        && fixture.coordinator.pending_parent_reply == 0
        && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
    );
    return true;
}

bool test_coordinator_public_failure_results(void)
{
    const uint32_t failures[] = {
        COORDINATOR_MOCK_RESERVE, COORDINATOR_MOCK_GRANT_CREATE,
        COORDINATOR_MOCK_PREPARE_IMAGE, COORDINATOR_MOCK_LOAD,
        COORDINATOR_MOCK_PREPARE_DESCRIPTORS, COORDINATOR_MOCK_PREPARE_EXECUTION,
        COORDINATOR_MOCK_COMMIT_HIDDEN, COORDINATOR_MOCK_ACTIVATE,
    };
    size_t index;

    for (index = 0; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        struct coordinator_spawn_fixture fixture;
        struct micros_ipc_message message;
        struct micros_pm_coordinator_response response;
        enum micros_pm_result expected = MICROS_PM_RESULT_NO_SPACE;

        COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize_parent(&fixture));
        fixture.fail_step = failures[index];
        if (failures[index] == COORDINATOR_MOCK_PREPARE_IMAGE) {
            expected = MICROS_PM_RESULT_IO;
        } else if (failures[index] == COORDINATOR_MOCK_LOAD) {
            expected = MICROS_PM_RESULT_EXECUTABLE;
        }
        message = coordinator_spawn_fixture_request(&fixture);
        COORDINATOR_EXPECT(
            micros_pm_coordinator_spawn_request(
                &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
                &message, &response
            ) == MICROS_PM_COORDINATOR_OK
            && !fixture.violation && response.kind == MICROS_PM_COORDINATOR_RESPONSE_REPLY
            && response.reply_token == fixture.parent_reply
            && coordinator_spawn_fixture_result_code(&response.message) == expected
            && coordinator_spawn_fixture_result_pid(&response.message) == 0
            && coordinator_spawn_fixture_resources_are_empty(&fixture)
            && fixture.table.records[1].state == MICROS_PM_PROCESS_FREE
            && fixture.table.last_pid == 2 && fixture.coordinator.last_spawn_token == 2
            && !fixture.coordinator.spawn_pending && !fixture.coordinator.parent_lost
            && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
        );
    }
    return true;
}

bool test_coordinator_public_preflight_results(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_ipc_message message;
    struct micros_pm_coordinator_response response;
    struct micros_pm_table snapshot;

    COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize_parent(&fixture));
    snapshot = fixture.table;
    message = coordinator_spawn_fixture_request(&fixture);
    fixture.parent_grant_error = true;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && coordinator_spawn_fixture_result_code(&response.message) == MICROS_PM_RESULT_GRANT
        && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
        && fixture.coordinator.last_spawn_token == 1 && fixture.trace_count == 0
    );
    fixture.parent_grant_error = false;
    fixture.request[0] = 2;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && coordinator_spawn_fixture_result_code(&response.message) == MICROS_PM_RESULT_MALFORMED
        && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
    );
    fixture.request[0] = 1;
    message.payload[0] = 2;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && coordinator_spawn_fixture_result_code(&response.message) == MICROS_PM_RESULT_BAD_VERSION
        && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
    );
    message = coordinator_spawn_fixture_request(&fixture);
    message.type = UINT32_C(0x1234);
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && coordinator_spawn_fixture_result_code(&response.message) == MICROS_PM_RESULT_BAD_TYPE
        && response.message.payload[4] == UINT8_C(0x34)
        && response.message.payload[5] == UINT8_C(0x12)
        && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
    );
    message = coordinator_spawn_fixture_request(&fixture);
    message.source += 2;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && coordinator_spawn_fixture_result_code(&response.message) == MICROS_PM_RESULT_CALLER
        && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
    );
    return true;
}

bool test_coordinator_parent_loss_rollback(void)
{
    const uint32_t cancellations[] = {
        COORDINATOR_MOCK_RESERVE, COORDINATOR_MOCK_GRANT_CREATE,
        COORDINATOR_MOCK_PREPARE_IMAGE, COORDINATOR_MOCK_LOAD,
        COORDINATOR_MOCK_PREPARE_DESCRIPTORS, COORDINATOR_MOCK_PREPARE_EXECUTION,
        COORDINATOR_MOCK_COMMIT_HIDDEN,
    };
    size_t index;

    for (index = 0; index < sizeof(cancellations) / sizeof(cancellations[0]); ++index) {
        struct coordinator_spawn_fixture fixture;
        struct micros_ipc_message message;
        struct micros_pm_coordinator_response response;

        COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize_parent(&fixture));
        fixture.cancel_step = cancellations[index];
        message = coordinator_spawn_fixture_request(&fixture);
        COORDINATOR_EXPECT(
            micros_pm_coordinator_spawn_request(
                &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
                &message, &response
            ) == MICROS_PM_COORDINATOR_OK
            && !fixture.violation
            && response.kind == MICROS_PM_COORDINATOR_RESPONSE_PARENT_LOST
            && response.reply_token == 0
            && coordinator_spawn_fixture_resources_are_empty(&fixture)
            && fixture.table.records[1].state == MICROS_PM_PROCESS_FREE
            && fixture.table.last_pid == 2 && fixture.coordinator.last_spawn_token == 2
            && !fixture.coordinator.spawn_pending && !fixture.coordinator.parent_lost
            && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
        );
    }
    return true;
}

bool test_coordinator_parent_loss_during_rollback(void)
{
    const uint32_t cleanup[] = {
        COORDINATOR_MOCK_REVERT_HIDDEN, COORDINATOR_MOCK_ABORT_PREPARED,
        COORDINATOR_MOCK_ABORT_DESCRIPTORS, COORDINATOR_MOCK_ABORT_LOAD,
        COORDINATOR_MOCK_RELEASE_IMAGE, COORDINATOR_MOCK_ABORT_RESERVED,
    };
    const struct micros_ipc_message no_reply = {.source = MICROS_ENDPOINT_NONE};
    size_t index;

    for (index = 0; index < sizeof(cleanup) / sizeof(cleanup[0]); ++index) {
        struct coordinator_spawn_fixture fixture;
        struct micros_ipc_message message;
        struct micros_pm_coordinator_response response;
        struct micros_pm_process_record parent;

        COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize_parent(&fixture));
        parent = fixture.table.records[0];
        fixture.fail_step = COORDINATOR_MOCK_ACTIVATE;
        fixture.cancel_step = cleanup[index];
        message = coordinator_spawn_fixture_request(&fixture);
        COORDINATOR_EXPECT(
            micros_pm_coordinator_spawn_request(
                &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
                &message, &response
            ) == MICROS_PM_COORDINATOR_OK
            && !fixture.violation
            && response.kind == MICROS_PM_COORDINATOR_RESPONSE_PARENT_LOST
            && response.reply_token == 0
            && memcmp(&response.message, &no_reply, sizeof(no_reply)) == 0
            && coordinator_spawn_fixture_resources_are_empty(&fixture)
            && fixture.table.records[1].state == MICROS_PM_PROCESS_FREE
            && memcmp(&fixture.table.records[0], &parent, sizeof(parent)) == 0
            && fixture.table.last_pid == 2 && fixture.coordinator.last_spawn_token == 2
            && !fixture.coordinator.spawn_pending && !fixture.coordinator.parent_lost
            && fixture.coordinator.pending_parent_reply == 0
            && fixture.trace_count >= sizeof(cleanup) / sizeof(cleanup[0])
            && memcmp(&fixture.trace[fixture.trace_count - sizeof(cleanup) / sizeof(cleanup[0])],
                      cleanup, sizeof(cleanup)) == 0
            && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
        );
    }
    return true;
}

bool test_coordinator_public_capacity_and_unknown_grant(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_ipc_message message;
    struct micros_pm_coordinator_response response;
    struct micros_pm_coordinator_response sentinel;
    struct micros_pm_table snapshot;
    size_t index;

    COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize_parent(&fixture));
    message = coordinator_spawn_fixture_request(&fixture);
    fixture.parent_grant_status = -99;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    response = sentinel;
    snapshot = fixture.table;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_ERROR_INVARIANT
        && memcmp(&response, &sentinel, sizeof(response)) == 0
        && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
        && fixture.trace_count == 0
    );
    fixture.parent_grant_status = MICROS_SYSCALL_ABI_OK;
    for (index = 1; index < MICROS_PM_PROCESS_CAPACITY; ++index) {
        fixture.table.records[index].generation = UINT32_MAX;
        fixture.table.records[index].state = MICROS_PM_PROCESS_QUARANTINED;
    }
    snapshot = fixture.table;
    COORDINATOR_EXPECT(
        micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
        && micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && coordinator_spawn_fixture_result_code(&response.message) == MICROS_PM_RESULT_NO_SPACE
        && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
        && fixture.trace_count == 0 && fixture.coordinator.last_spawn_token == 1
    );
    return true;
}

bool test_coordinator_parent_loss_after_publication(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_ipc_message message;
    struct micros_pm_coordinator_response response;
    size_t index;

    COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize_parent(&fixture));
    fixture.cancel_step = COORDINATOR_MOCK_ACTIVATE;
    message = coordinator_spawn_fixture_request(&fixture);
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_request(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            &message, &response
        ) == MICROS_PM_COORDINATOR_OK
        && !fixture.violation && response.kind == MICROS_PM_COORDINATOR_RESPONSE_PARENT_LOST
        && response.reply_token == 0 && fixture.published && fixture.vm_committed
        && !fixture.image_live && fixture.vfs_process_live
        && fixture.table.records[1].state == MICROS_PM_PROCESS_RUNNING
        && fixture.table.records[1].load_token == 201
        && !fixture.coordinator.spawn_pending && !fixture.coordinator.parent_lost
        && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
    );
    for (index = 0; index < fixture.trace_count; ++index) {
        COORDINATOR_EXPECT(fixture.trace[index] <= COORDINATOR_MOCK_RELEASE_IMAGE);
    }
    return true;
}
