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
