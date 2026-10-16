#include "kernel_objects_test_internal.h"

struct kernel_object_test_corruption_work {
    struct micros_process_handle owner;
    struct micros_thread_handle first;
    struct micros_thread_handle second;
    struct micros_hart_handle hart_zero;
    struct micros_hart_handle hart_one;
    struct micros_kernel_objects snapshot;
};

#define EXPECT_CORRUPTION(statement) \
    do { \
        *fixture = work->snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_KERNEL_OBJECT_ERROR_INVARIANT, \
            micros_kernel_objects_validate(fixture) \
        ); \
    } while (false)

static bool kernel_object_test_prepare_corruption_snapshot(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_corruption_work *work
)
{
    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 2, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 0, &work->hart_zero)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 1, &work->hart_one)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &work->owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, work->owner, &work->first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, work->owner, &work->second)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    work->snapshot = *fixture;
    return true;
}

static bool kernel_object_test_corrupt_relationships_and_counts(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_corruption_work *work
)
{
    EXPECT_CORRUPTION(
        ++fixture->processes[work->owner.slot].live_thread_count
    );
    EXPECT_CORRUPTION(
        ++fixture->threads[work->second.slot].owner.generation
    );
    EXPECT_CORRUPTION(fixture->live_process_count = 0);
    EXPECT_CORRUPTION(fixture->live_thread_count = 0);
    EXPECT_CORRUPTION(
        fixture->harts[work->hart_one.slot].hardware_id =
            fixture->harts[work->hart_zero.slot].hardware_id
    );
    EXPECT_CORRUPTION(fixture->registered_hart_count = 1);
    EXPECT_CORRUPTION(fixture->max_harts = 1);
    EXPECT_CORRUPTION(fixture->max_threads_per_process = 1);
    return true;
}

static bool kernel_object_test_corrupt_generations_and_context(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_corruption_work *work
)
{
    EXPECT_CORRUPTION(fixture->processes[work->owner.slot].generation = 0);
    EXPECT_CORRUPTION(
        fixture->processes[work->owner.slot].generation =
            MICROS_PROCESS_GENERATION_MAX + 1
    );
    EXPECT_CORRUPTION(fixture->threads[work->second.slot].generation = 0);
    EXPECT_CORRUPTION(
        fixture->threads[2].owner = work->owner
    );
    EXPECT_CORRUPTION(
        fixture->threads[2].context_attached = true
    );
    EXPECT_CORRUPTION(
        fixture->threads[work->second.slot].context_attached = false;
        fixture->threads[work->second.slot].kernel_stack_bottom =
            UINT64_C(0x300000);
        fixture->threads[work->second.slot].kernel_stack_top =
            UINT64_C(0x304000)
    );
    return true;
}

static bool kernel_object_test_corrupt_released_payload_and_hart(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_corruption_work *work
)
{
    EXPECT_CORRUPTION(
        fixture->processes[1].address_space_root =
            UINT64_C(0x1000)
    );
    EXPECT_CORRUPTION(
        fixture->processes[1].slot_state =
            MICROS_KERNEL_OBJECT_SLOT_QUARANTINED;
        fixture->processes[1].generation = 1;
        fixture->processes[1].address_space_root =
            UINT64_C(0x1000)
    );
    EXPECT_CORRUPTION(
        fixture->threads[2].slot_state =
            MICROS_KERNEL_OBJECT_SLOT_QUARANTINED;
        fixture->threads[2].generation = 1;
        fixture->threads[2].owner = work->owner
    );
    EXPECT_CORRUPTION(
        fixture->harts[2].current_thread = work->first
    );
    EXPECT_CORRUPTION(
        fixture->harts[work->hart_zero.slot].trap_installed = true
    );
    EXPECT_CORRUPTION(
        fixture->harts[work->hart_zero.slot].idle_primary_stack_bottom =
            UINT64_C(0x1000)
    );
    EXPECT_CORRUPTION(
        fixture->harts[work->hart_one.slot].generation = 0
    );
    return true;
}

#undef EXPECT_CORRUPTION

bool kernel_object_test_validator_rejects_corrupt_relationships(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct kernel_object_test_corruption_work work;
    work.owner = (struct micros_process_handle){0};
    work.first = (struct micros_thread_handle){0};
    work.second = (struct micros_thread_handle){0};
    work.hart_zero = (struct micros_hart_handle){0};
    work.hart_one = (struct micros_hart_handle){0};

    if (!kernel_object_test_prepare_corruption_snapshot(fixture, &work)) {
        return false;
    }

    if (!kernel_object_test_corrupt_relationships_and_counts(fixture, &work)) {
        return false;
    }

    if (!kernel_object_test_corrupt_generations_and_context(fixture, &work)) {
        return false;
    }

    if (!kernel_object_test_corrupt_released_payload_and_hart(fixture, &work)) {
        return false;
    }
    return true;
}
