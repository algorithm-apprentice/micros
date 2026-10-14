#include "tests/host/scheduler_test_internal.h"

bool scheduler_host_test_priority_queues_and_tail_order(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    struct micros_thread_handle selected = {
        UINT16_MAX,
        UINT32_MAX,
    };

    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_EMPTY,
        micros_hart_pick_ready(&fixture->objects, fixture->harts[0], &selected)
    );
    EXPECT_TRUE(
        selected.slot == UINT16_MAX
        && selected.generation == UINT32_MAX
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_thread_scheduler_admit(
            &fixture->objects,
            fixture->harts[0],
            fixture->threads[0],
            MICROS_SCHEDULER_PRIORITY_COUNT,
            100,
            true
        )
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_thread_scheduler_admit(
            &fixture->objects,
            fixture->harts[0],
            fixture->threads[0],
            7,
            0,
            true
        )
    );
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
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[2], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_pick_ready(&fixture->objects, fixture->harts[0], &selected)
    );
    EXPECT_TRUE(
        scheduler_host_handles_equal(selected, fixture->threads[1])
        && scheduler_host_handles_equal(fixture->objects.harts[fixture->harts[0].slot].ready_head[7], fixture->threads[0])
        && scheduler_host_handles_equal(fixture->objects.harts[fixture->harts[0].slot].ready_tail[7], fixture->threads[2])
        && scheduler_host_handles_equal(
            fixture->objects.threads[fixture->threads[0].slot].ready_next,
            fixture->threads[2]
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_pick_ready(&fixture->objects, fixture->harts[0], &selected)
    );
    EXPECT_TRUE(scheduler_host_handles_equal(selected, fixture->threads[0]));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->threads[1],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_pick_ready(&fixture->objects, fixture->harts[0], &selected)
    );
    EXPECT_TRUE(scheduler_host_handles_equal(selected, fixture->threads[1]));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&fixture->objects)
    );
    return true;
}

bool scheduler_host_test_runtime_flag_zero_boundary(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    EXPECT_TRUE(scheduler_host_setup_fixture(fixture));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[0], fixture->threads[0], 7, 100, true
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_set(
            &fixture->objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_NO_QUANTUM
        )
    );
    EXPECT_TRUE(
        !fixture->objects.threads[fixture->threads[0].slot].ready_linked
        && fixture->objects.harts[fixture->harts[0].slot].ready_head[7].generation == 0
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_set(
            &fixture->objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_NO_QUANTUM
        )
    );
    EXPECT_TRUE(
        fixture->objects.threads[fixture->threads[0].slot].runtime_flags
            == MICROS_THREAD_RTS_INACTIVE
        && !fixture->objects.threads[fixture->threads[0].slot].ready_linked
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_TRUE(
        fixture->objects.threads[fixture->threads[0].slot].runtime_flags == 0
        && fixture->objects.threads[fixture->threads[0].slot].ready_linked
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_runtime_flags_set(
            &fixture->objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_PREEMPTED
        )
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->threads[0],
            MICROS_THREAD_RTS_PREEMPTED
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_remove(&fixture->objects, fixture->threads[0])
    );
    EXPECT_TRUE(
        !fixture->objects.threads[fixture->threads[0].slot].scheduler_assigned
        && fixture->objects.threads[fixture->threads[0].slot].runtime_flags
            == MICROS_THREAD_RTS_INACTIVE
    );
    return true;
}

bool scheduler_host_test_policy_replacement_is_atomic(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    struct micros_thread_handle selected;

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
        micros_thread_install_policy(
            &fixture->objects,
            fixture->threads[1],
            2,
            55
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_pick_ready(&fixture->objects, fixture->harts[0], &selected)
    );
    EXPECT_TRUE(
        scheduler_host_handles_equal(selected, fixture->threads[1])
        && fixture->objects.threads[fixture->threads[1].slot].remaining_counter_ticks == 55
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_scheduler_hold(&fixture->objects, fixture->threads[1])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_install_policy(
            &fixture->objects,
            fixture->threads[1],
            9,
            77
        )
    );
    EXPECT_TRUE(
        !fixture->objects.threads[fixture->threads[1].slot].ready_linked
        && fixture->objects.threads[fixture->threads[1].slot].scheduler_priority == 9
        && fixture->objects.threads[fixture->threads[1].slot].remaining_counter_ticks == 77
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_runtime_flags_unset(
            &fixture->objects,
            fixture->threads[1],
            MICROS_THREAD_RTS_INACTIVE
        )
    );
    EXPECT_TRUE(
        scheduler_host_handles_equal(fixture->objects.harts[fixture->harts[0].slot].ready_head[9], fixture->threads[1])
    );
    EXPECT_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_thread_install_policy(
            &fixture->objects,
            fixture->threads[1],
            MICROS_SCHEDULER_PRIORITY_COUNT,
            77
        )
    );
    return true;
}

bool scheduler_host_test_corrupt_queue_state_is_rejected(void)
{
    struct scheduler_host_fixture *fixture =
        scheduler_host_fixture();
    struct micros_kernel_objects snapshot;
    struct micros_thread_handle output = {
        UINT16_MAX,
        UINT32_MAX,
    };

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
        micros_thread_scheduler_admit(
            &fixture->objects, fixture->harts[1], fixture->threads[2], 5, 100, true
        )
    );
    snapshot = fixture->objects;

#define EXPECT_CORRUPTION(statement) \
    do { \
        struct micros_thread_handle before = output; \
        fixture->objects = snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_KERNEL_OBJECT_ERROR_INVARIANT, \
            micros_scheduler_core_validate(&fixture->objects) \
        ); \
        EXPECT_ERROR( \
            MICROS_KERNEL_OBJECT_ERROR_INVARIANT, \
            micros_hart_pick_ready(&fixture->objects, fixture->harts[0], &output) \
        ); \
        EXPECT_TRUE(scheduler_host_handles_equal(output, before)); \
    } while (false)

    EXPECT_CORRUPTION(
        fixture->objects.harts[fixture->harts[0].slot].ready_head[7].generation =
            UINT32_MAX
    );
    EXPECT_CORRUPTION(
        fixture->objects.harts[fixture->harts[0].slot].ready_tail[7].generation =
            UINT32_MAX
    );
    EXPECT_CORRUPTION(
        fixture->objects.threads[fixture->threads[0].slot].ready_next.generation =
            UINT32_MAX
    );
    EXPECT_CORRUPTION(
        fixture->objects.threads[fixture->threads[0].slot].scheduler_hart = fixture->harts[1]
    );
    EXPECT_CORRUPTION(
        fixture->objects.threads[fixture->threads[0].slot].runtime_flags =
            MICROS_THREAD_RTS_PREEMPTED
    );
    EXPECT_CORRUPTION(
        fixture->objects.harts[fixture->harts[0].slot].current_thread = fixture->threads[0]
    );
    EXPECT_CORRUPTION(
        fixture->objects.harts[fixture->harts[0].slot].accounting_owner =
            MICROS_SCHEDULER_ACCOUNTING_THREAD
    );

#undef EXPECT_CORRUPTION
    fixture->objects = snapshot;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_scheduler_core_validate(&fixture->objects)
    );
    return true;
}
