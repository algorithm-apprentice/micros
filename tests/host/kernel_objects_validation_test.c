#include "kernel_objects_test_internal.h"

bool kernel_object_test_validator_rejects_corrupt_relationships(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle owner = {0};
    struct micros_thread_handle first = {0};
    struct micros_thread_handle second = {0};
    struct micros_hart_handle hart_zero = {0};
    struct micros_hart_handle hart_one = {0};
    struct micros_kernel_objects snapshot;

#define EXPECT_CORRUPTION(statement) \
    do { \
        *fixture = snapshot; \
        statement; \
        EXPECT_ERROR( \
            MICROS_KERNEL_OBJECT_ERROR_INVARIANT, \
            micros_kernel_objects_validate(fixture) \
        ); \
    } while (false)

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 2, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 0, &hart_zero)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 1, &hart_one)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, owner, &first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, owner, &second)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    snapshot = *fixture;

    EXPECT_CORRUPTION(
        ++fixture->processes[owner.slot].live_thread_count
    );
    EXPECT_CORRUPTION(
        ++fixture->threads[second.slot].owner.generation
    );
    EXPECT_CORRUPTION(fixture->live_process_count = 0);
    EXPECT_CORRUPTION(fixture->live_thread_count = 0);
    EXPECT_CORRUPTION(
        fixture->harts[hart_one.slot].hardware_id =
            fixture->harts[hart_zero.slot].hardware_id
    );
    EXPECT_CORRUPTION(fixture->registered_hart_count = 1);
    EXPECT_CORRUPTION(fixture->max_harts = 1);
    EXPECT_CORRUPTION(fixture->max_threads_per_process = 1);
    EXPECT_CORRUPTION(fixture->processes[owner.slot].generation = 0);
    EXPECT_CORRUPTION(
        fixture->processes[owner.slot].generation =
            MICROS_PROCESS_GENERATION_MAX + 1
    );
    EXPECT_CORRUPTION(fixture->threads[second.slot].generation = 0);
    EXPECT_CORRUPTION(
        fixture->threads[2].owner = owner
    );
    EXPECT_CORRUPTION(
        fixture->threads[2].context_attached = true
    );
    EXPECT_CORRUPTION(
        fixture->threads[second.slot].context_attached = false;
        fixture->threads[second.slot].kernel_stack_bottom =
            UINT64_C(0x300000);
        fixture->threads[second.slot].kernel_stack_top =
            UINT64_C(0x304000)
    );
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
        fixture->threads[2].owner = owner
    );
    EXPECT_CORRUPTION(
        fixture->harts[2].current_thread = first
    );
    EXPECT_CORRUPTION(
        fixture->harts[hart_zero.slot].trap_installed = true
    );
    EXPECT_CORRUPTION(
        fixture->harts[hart_zero.slot].idle_primary_stack_bottom =
            UINT64_C(0x1000)
    );
    EXPECT_CORRUPTION(
        fixture->harts[hart_one.slot].generation = 0
    );

#undef EXPECT_CORRUPTION
    return true;
}
