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
