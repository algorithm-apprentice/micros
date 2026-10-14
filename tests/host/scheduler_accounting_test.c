#include "tests/host/scheduler_test_internal.h"

static bool prepare_initial_accounting_fixture(struct scheduler_host_fixture *fixture)
{
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
            &fixture->objects, fixture->harts[0], fixture->threads[1], 3, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_accounting_initialize(
            &fixture->objects, fixture->harts[0], 100
        )
    );
    return true;
}

static bool reject_thread_owned_mutations(struct scheduler_host_fixture *fixture)
{
    EXPECT_TRUE(fixture->objects.harts[fixture->harts[0].slot].kernel_counter_ticks == 20);
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[0])
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
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
            fixture->threads[1],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    return true;
}

static bool account_thread_intervals(struct scheduler_host_fixture *fixture)
{
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_user_trap(
            &fixture->objects, fixture->harts[0], 170
        )
    );
    EXPECT_TRUE(
        fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks == 50
        && fixture->objects.harts[fixture->harts[0].slot].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
        && fixture->objects.harts[fixture->harts[0].slot].kernel_counter_ticks == 20
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_thread(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 200
        )
    );
    EXPECT_TRUE(fixture->objects.harts[fixture->harts[0].slot].kernel_counter_ticks == 50);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_user_trap(
            &fixture->objects, fixture->harts[0], 300
        )
    );
    EXPECT_TRUE(
        fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks == 0
    );
    return true;
}

static bool account_idle_interval(struct scheduler_host_fixture *fixture)
{
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_idle(
            &fixture->objects, fixture->harts[0], 330
        )
    );
    EXPECT_TRUE(fixture->objects.harts[fixture->harts[0].slot].kernel_counter_ticks == 80);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_idle_trap(
            &fixture->objects, fixture->harts[0], 380
        )
    );
    EXPECT_TRUE(
        fixture->objects.harts[fixture->harts[0].slot].idle_counter_ticks == 50
        && fixture->objects.harts[fixture->harts[0].slot].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
    );
    return true;
}

static bool prepare_counter_wrap_fixture(struct scheduler_host_fixture *fixture)
{
    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 7, 30, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_accounting_initialize(
            &fixture->objects,
            fixture->harts[0],
            UINT64_MAX - UINT64_C(29)
        )
    );
    return true;
}

static bool account_counter_wrap_interval(struct scheduler_host_fixture *fixture)
{
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_enter_thread(
            &fixture->objects,
            fixture->harts[0],
            fixture->threads[0],
            UINT64_MAX - UINT64_C(9)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_user_trap(
            &fixture->objects, fixture->harts[0], 10
        )
    );
    EXPECT_TRUE(
        fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks == 10
        && fixture->objects.harts[fixture->harts[0].slot].kernel_counter_ticks == 20
    );
    return true;
}

bool scheduler_host_test_accounting_owners_are_separate(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    struct micros_scheduler_return_plan plan;
    struct micros_scheduler_return_plan rejected_plan;

    if (!prepare_initial_accounting_fixture(fixture)) {
        return false;
    }

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
        micros_scheduler_account_enter_thread(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 120
        )
    );
    rejected_plan = plan;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_hart_plan_user_return(
            &fixture->objects,
            fixture->harts[0],
            &rejected_plan
        )
    );
    EXPECT_TRUE(memcmp(&rejected_plan, &plan, sizeof(plan)) == 0);
    if (!reject_thread_owned_mutations(fixture)) {
        return false;
    }

    if (!account_thread_intervals(fixture)) {
        return false;
    }

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    if (!account_idle_interval(fixture)) {
        return false;
    }

    if (!prepare_counter_wrap_fixture(fixture)) {
        return false;
    }

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_plan_user_return(&fixture->objects, fixture->harts[0], &plan)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_commit_user_return(&fixture->objects, &plan)
    );
    if (!account_counter_wrap_interval(fixture)) {
        return false;
    }

    return true;
}

bool scheduler_host_test_trap_accounting_uses_minimal_route(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    struct micros_scheduler_return_plan plan;
    uint32_t unrelated_flags;

    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_accounting_initialize(
            &fixture->objects, fixture->harts[0], 100
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
        micros_scheduler_account_enter_thread(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 120
        )
    );

    unrelated_flags =
        fixture->objects.threads[fixture->threads[1].slot].runtime_flags;
    fixture->objects.threads[fixture->threads[1].slot].runtime_flags |=
        UINT32_C(0x80000000);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_INVARIANT,
        micros_scheduler_core_validate(&fixture->objects)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_user_trap(
            &fixture->objects, fixture->harts[0], 170
        )
    );
    EXPECT_TRUE(
        fixture->objects.threads[fixture->threads[0].slot].remaining_counter_ticks == 50
        && fixture->objects.harts[fixture->harts[0].slot].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
    );
    fixture->objects.threads[fixture->threads[1].slot].runtime_flags =
        unrelated_flags;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&fixture->objects)
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[0])
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
        micros_scheduler_account_enter_idle(
            &fixture->objects, fixture->harts[0], 200
        )
    );
    fixture->objects.threads[fixture->threads[1].slot].runtime_flags |=
        UINT32_C(0x80000000);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_INVARIANT,
        micros_scheduler_core_validate(&fixture->objects)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_account_idle_trap(
            &fixture->objects, fixture->harts[0], 230
        )
    );
    EXPECT_TRUE(
        fixture->objects.harts[fixture->harts[0].slot].idle_counter_ticks == 30
        && fixture->objects.harts[fixture->harts[0].slot].accounting_owner
            == MICROS_SCHEDULER_ACCOUNTING_KERNEL
    );
    fixture->objects.threads[fixture->threads[1].slot].runtime_flags =
        unrelated_flags;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&fixture->objects)
    );
    return true;
}
