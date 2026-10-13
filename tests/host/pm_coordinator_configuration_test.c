#include "tests/host/pm_coordinator_test_internal.h"

static struct micros_pm_coordinator_bindings valid_bindings(void)
{
    struct micros_pm_coordinator_bindings bindings = {
        .self = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(2),
        .vm = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(1),
        .vfs = (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | UINT32_C(5),
        .reserved = 0,
    };

    return bindings;
}

static bool coordinator_test_bytes_are_zero(const uint8_t *bytes, size_t size)
{
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

bool test_coordinator_initialization(void)
{
    struct micros_pm_table table;
    struct micros_pm_table table_snapshot;
    struct micros_pm_coordinator coordinator;
    struct micros_pm_coordinator snapshot;
    struct micros_pm_coordinator_bindings bindings = valid_bindings();

    memset(&table, 0, sizeof(table));
    memset(&coordinator, 0, sizeof(coordinator));
    COORDINATOR_EXPECT(micros_pm_table_initialize(&table) == MICROS_PM_MODEL_OK);
    table_snapshot = table;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
            == MICROS_PM_COORDINATOR_OK
        && coordinator.initialization_magic != 0
        && coordinator.table == &table
        && memcmp(&coordinator.bindings, &bindings, sizeof(bindings)) == 0
        && coordinator.last_spawn_token == 0
        && coordinator_test_bytes_are_zero(
            coordinator.request_bytes, sizeof(coordinator.request_bytes)
        )
        && memcmp(&table, &table_snapshot, sizeof(table)) == 0
        && !table.runtime_enabled
    );
    coordinator.last_spawn_token = 9;
    snapshot = coordinator;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
            == MICROS_PM_COORDINATOR_ERROR_STATE
        && memcmp(&coordinator, &snapshot, sizeof(coordinator)) == 0
        && memcmp(&table, &table_snapshot, sizeof(table)) == 0
    );
    return true;
}

bool test_coordinator_embedded_bindings(void)
{
    struct micros_pm_table table;
    struct micros_pm_coordinator coordinator;
    struct micros_pm_coordinator_bindings bindings = valid_bindings();

    memset(&table, 0, sizeof(table));
    memset(&coordinator, 0, sizeof(coordinator));
    coordinator.bindings = bindings;
    COORDINATOR_EXPECT(
        micros_pm_table_initialize(&table) == MICROS_PM_MODEL_OK
        && micros_pm_coordinator_initialize(
            &coordinator, &table, &coordinator.bindings
        ) == MICROS_PM_COORDINATOR_OK
        && memcmp(&coordinator.bindings, &bindings, sizeof(bindings)) == 0
        && coordinator.table == &table
    );
    return true;
}

bool test_coordinator_binding_failures(void)
{
    struct micros_pm_table table;
    struct micros_pm_coordinator coordinator;
    struct micros_pm_coordinator snapshot;
    struct micros_pm_coordinator_bindings bindings = valid_bindings();
    micros_endpoint_t invalid[] = {
        MICROS_ENDPOINT_NONE, MICROS_ENDPOINT_ANY, UINT32_C(1),
        (UINT32_C(1) << MICROS_ENDPOINT_SLOT_BITS) | MICROS_PROCESS_CAPACITY,
    };
    size_t index;

    memset(&table, 0, sizeof(table));
    memset(&coordinator, 0xa5, sizeof(coordinator));
    snapshot = coordinator;
    COORDINATOR_EXPECT(micros_pm_table_initialize(&table) == MICROS_PM_MODEL_OK);
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        bindings = valid_bindings();
        bindings.self = invalid[index];
        COORDINATOR_EXPECT(
            micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
                == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
            && memcmp(&coordinator, &snapshot, sizeof(coordinator)) == 0
        );
        bindings = valid_bindings();
        bindings.vm = invalid[index];
        COORDINATOR_EXPECT(
            micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
                == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
            && memcmp(&coordinator, &snapshot, sizeof(coordinator)) == 0
        );
        bindings = valid_bindings();
        bindings.vfs = invalid[index];
        COORDINATOR_EXPECT(
            micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
                == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
            && memcmp(&coordinator, &snapshot, sizeof(coordinator)) == 0
        );
    }
    bindings = valid_bindings();
    bindings.vm = bindings.self;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
            == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
        && memcmp(&coordinator, &snapshot, sizeof(coordinator)) == 0
    );
    bindings = valid_bindings();
    bindings.reserved = 1;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
            == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
        && memcmp(&coordinator, &snapshot, sizeof(coordinator)) == 0
    );
    return true;
}

bool test_coordinator_table_failures(void)
{
    struct micros_pm_table table;
    struct micros_pm_table table_snapshot;
    struct micros_pm_coordinator coordinator;
    struct micros_pm_coordinator snapshot;
    struct micros_pm_coordinator_bindings bindings = valid_bindings();
    struct micros_pm_process_handle child;
    struct micros_ipc_message event;
    uint64_t pid;

    memset(&table, 0, sizeof(table));
    memset(&coordinator, 0, sizeof(coordinator));
    snapshot = coordinator;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_initialize(NULL, &table, &bindings)
            == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
        && micros_pm_coordinator_initialize(&coordinator, NULL, &bindings)
            == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
        && micros_pm_coordinator_initialize(&coordinator, &table, NULL)
            == MICROS_PM_COORDINATOR_ERROR_ARGUMENT
        && micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
            == MICROS_PM_COORDINATOR_ERROR_INVARIANT
        && memcmp(&coordinator, &snapshot, sizeof(coordinator)) == 0
    );
    COORDINATOR_EXPECT(micros_pm_table_initialize(&table) == MICROS_PM_MODEL_OK);
    memset(&event, 0, sizeof(event));
    event.source = MICROS_ENDPOINT_NONE;
    event.type = MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    event.payload[0] = (uint8_t)MICROS_KERNEL_EVENT_BOOTSTRAP_SEALED;
    COORDINATOR_EXPECT(
        micros_pm_enable_runtime(&table, &event) == MICROS_PM_MODEL_OK
        && micros_pm_begin_init(&table, &child, &pid) == MICROS_PM_MODEL_OK
        && pid == 1
    );
    table_snapshot = table;
    COORDINATOR_EXPECT(
        micros_pm_coordinator_initialize(&coordinator, &table, &bindings)
            == MICROS_PM_COORDINATOR_ERROR_STATE
        && memcmp(&coordinator, &snapshot, sizeof(coordinator)) == 0
        && memcmp(&table, &table_snapshot, sizeof(table)) == 0
    );
    return true;
}
