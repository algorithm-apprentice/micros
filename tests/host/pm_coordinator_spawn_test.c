#include "tests/host/pm_coordinator_spawn_fixture.h"

bool test_coordinator_directed_init(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_pm_coordinator_child child;
    const uint32_t expected[] = {
        COORDINATOR_MOCK_RESERVE, COORDINATOR_MOCK_GRANT_CREATE,
        COORDINATOR_MOCK_PREPARE_IMAGE, COORDINATOR_MOCK_GRANT_REVOKE,
        COORDINATOR_MOCK_LOAD, COORDINATOR_MOCK_PREPARE_DESCRIPTORS,
        COORDINATOR_MOCK_PREPARE_EXECUTION, COORDINATOR_MOCK_COMMIT_HIDDEN,
        COORDINATOR_MOCK_ACTIVATE, COORDINATOR_MOCK_COMMIT_LOAD,
        COORDINATOR_MOCK_RELEASE_IMAGE,
    };

    COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize(&fixture));
    memset(&child, 0xa5, sizeof(child));
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_init(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            fixture.request, fixture.request_size, &child
        ) == MICROS_PM_COORDINATOR_OK
        && !fixture.violation && child.pid == 1 && child.reserved == 0
        && child.process.slot == 0 && child.process.generation == 1
        && child.endpoint == fixture.table.records[0].endpoint
        && fixture.table.records[0].state == MICROS_PM_PROCESS_RUNNING
        && fixture.table.records[0].reaper && fixture.table.records[0].load_token == 201
        && fixture.table.records[0].mapping_generation == 301
        && fixture.table.records[0].spawn_stage == MICROS_PM_SPAWN_NONE
        && fixture.coordinator.last_spawn_token == 1
        && !fixture.kernel_reserved && !fixture.grant_live && !fixture.image_live
        && fixture.published && fixture.vm_committed && fixture.vfs_process_live
        && fixture.trace_count == sizeof(expected) / sizeof(expected[0])
        && memcmp(fixture.trace, expected, sizeof(expected)) == 0
        && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
    );
    return true;
}

bool test_coordinator_init_failure_rollback(void)
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
        struct micros_pm_coordinator_child child;
        struct micros_pm_coordinator_child sentinel;

        COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize(&fixture));
        fixture.fail_step = failures[index];
        memset(&sentinel, 0xa5, sizeof(sentinel));
        child = sentinel;
        COORDINATOR_EXPECT(
            micros_pm_coordinator_spawn_init(
                &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
                fixture.request, fixture.request_size, &child
            ) == MICROS_PM_COORDINATOR_ERROR_INVARIANT
            && !fixture.violation && coordinator_spawn_fixture_resources_are_empty(&fixture)
            && fixture.table.last_pid == 1 && fixture.table.init_attempted
            && fixture.table.records[0].state == MICROS_PM_PROCESS_FREE
            && fixture.table.records[0].generation == 1
            && fixture.coordinator.last_spawn_token == 1
            && memcmp(&child, &sentinel, sizeof(child)) == 0
            && micros_pm_table_validate(&fixture.table) == MICROS_PM_MODEL_OK
        );
    }
    return true;
}

bool test_coordinator_init_postpublication_failures(void)
{
    const uint32_t failures[] = {
        COORDINATOR_MOCK_COMMIT_LOAD, COORDINATOR_MOCK_RELEASE_IMAGE,
    };
    size_t index;

    for (index = 0; index < sizeof(failures) / sizeof(failures[0]); ++index) {
        struct coordinator_spawn_fixture fixture;
        struct micros_pm_coordinator_child child;
        struct micros_pm_coordinator_child sentinel;
        size_t step;

        COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize(&fixture));
        fixture.fail_step = failures[index];
        memset(&sentinel, 0xa5, sizeof(sentinel));
        child = sentinel;
        COORDINATOR_EXPECT(
            micros_pm_coordinator_spawn_init(
                &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
                fixture.request, fixture.request_size, &child
            ) == MICROS_PM_COORDINATOR_ERROR_INVARIANT
            && !fixture.violation && fixture.published && fixture.execution_live
            && fixture.table.records[0].state == MICROS_PM_PROCESS_RUNNING
            && fixture.table.records[0].reaper && fixture.table.records[0].load_token == 201
            && fixture.table.records[0].mapping_generation == 301
            && memcmp(&child, &sentinel, sizeof(child)) == 0
        );
        for (step = 0; step < fixture.trace_count; ++step) {
            COORDINATOR_EXPECT(fixture.trace[step] <= COORDINATOR_MOCK_RELEASE_IMAGE);
        }
    }
    return true;
}

bool test_coordinator_init_preflight_preserves_pid(void)
{
    struct coordinator_spawn_fixture fixture;
    struct micros_pm_coordinator_child child;
    struct micros_pm_coordinator_child sentinel;
    struct micros_pm_table snapshot;

    COORDINATOR_EXPECT(coordinator_spawn_fixture_initialize(&fixture));
    snapshot = fixture.table;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    child = sentinel;
    fixture.request[0] = 2;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_init(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            fixture.request, fixture.request_size, &child
        ) == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
        && fixture.trace_count == 0 && fixture.coordinator.last_spawn_token == 0
        && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
        && memcmp(&child, &sentinel, sizeof(child)) == 0
    );
    fixture.request[0] = 1;
    fixture.request[256] = 'b';
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_init(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            fixture.request, fixture.request_size, &child
        ) == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
        && fixture.trace_count == 0 && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
    );
    fixture.request[256] = '/';
    fixture.coordinator.bindings.vm = fixture.coordinator.bindings.self;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_spawn_init(
            &fixture.coordinator, coordinator_spawn_fixture_backend(), &fixture,
            fixture.request, fixture.request_size, &child
        ) == MICROS_PM_COORDINATOR_ERROR_INVARIANT
        && fixture.trace_count == 0 && memcmp(&fixture.table, &snapshot, sizeof(snapshot)) == 0
    );
    return true;
}
