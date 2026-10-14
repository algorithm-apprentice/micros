#include "tests/host/scheduler_test_internal.h"

bool scheduler_host_test_current_selection_and_preemption_repair(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    struct micros_scheduler_return_plan plan;
    struct micros_thread_handle current;

    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[1], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.action == MICROS_SCHEDULER_RETURN_SELECT_THREAD
        && scheduler_host_handles_equal(plan.selected, fixture->threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_current_thread(&fixture->objects, fixture->harts[0], &current)
    );
    EXPECT_TRUE(
        scheduler_host_handles_equal(current, fixture->threads[0])
        && scheduler_host_handles_equal(
            fixture->objects.harts[fixture->harts[0].slot].ready_head[7],
            fixture->threads[0]
        )
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[2], 3, 100, true
        )
    );
    EXPECT_TRUE(
        (
            fixture->objects.threads[fixture->threads[0].slot].runtime_flags
            & MICROS_THREAD_RTS_PREEMPTED
        ) != 0
        && !fixture->objects.threads[fixture->threads[0].slot].ready_linked
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.repair_preempted
        && scheduler_host_handles_equal(plan.selected, fixture->threads[2])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_TRUE(
        scheduler_host_handles_equal(
            fixture->objects.harts[fixture->harts[0].slot].current_thread,
            fixture->threads[2]
        )
        && scheduler_host_handles_equal(
            fixture->objects.harts[fixture->harts[0].slot].ready_head[7],
            fixture->threads[0]
        )
        && fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks
            == 100
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[2])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_TRUE(scheduler_host_handles_equal(plan.selected, fixture->threads[0]));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&fixture->objects)
    );
    return true;
}

bool scheduler_host_test_quantum_rotation_and_nonpreemptible_current(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    struct micros_scheduler_return_plan plan;

    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[1], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks = 0;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.renew_quantum
        && scheduler_host_handles_equal(plan.selected, fixture->threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_TRUE(
        scheduler_host_handles_equal(
            fixture->objects.harts[fixture->harts[0].slot].current_thread,
            fixture->threads[1]
        )
        && scheduler_host_handles_equal(
            fixture->objects.harts[fixture->harts[0].slot].ready_tail[7],
            fixture->threads[0]
        )
        && fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks
            == 100
    );

    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 7, 100, false
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[1], 1, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.action == MICROS_SCHEDULER_RETURN_KEEP_CURRENT
        && scheduler_host_handles_equal(plan.selected, fixture->threads[0])
    );
    fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks = 0;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_TRUE(
        plan.action == MICROS_SCHEDULER_RETURN_KEEP_CURRENT
        && plan.renew_quantum
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_TRUE(
        fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks == 100
    );
    return true;
}

bool scheduler_host_test_current_block_and_stale_plan_are_atomic(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    struct micros_scheduler_return_plan plan;

    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_set(
            &fixture->objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_TRUE(plan.action == MICROS_SCHEDULER_RETURN_ENTER_IDLE);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_TRUE(
        fixture->objects.harts[fixture->harts[0].slot].current_thread.generation == 0
        && fixture->objects.harts[fixture->harts[0].slot].trap.primary_stack_bottom
            == fixture->objects.harts[fixture->harts[0].slot].idle_primary_stack_bottom
    );

    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[1], 7, 100, true
        )
    );
    fixture->objects.harts[fixture->harts[0].slot].reschedule_pending = true;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[1])
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_TRUE(fixture->objects.harts[fixture->harts[0].slot].reschedule_pending);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    EXPECT_TRUE(!fixture->objects.harts[fixture->harts[0].slot].reschedule_pending);
    return true;
}
