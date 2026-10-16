#include "kernel_objects_test_internal.h"

bool kernel_object_test_process_slots_reuse_with_new_generations(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle first = {UINT16_MAX, UINT32_MAX};
    struct micros_process_handle second = {UINT16_MAX, UINT32_MAX};
    struct micros_process_handle replacement = {
        UINT16_MAX,
        UINT32_MAX,
    };
    const struct micros_process *process = NULL;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &second)
    );
    EXPECT_TRUE(first.slot == 0 && first.generation == 1);
    EXPECT_TRUE(second.slot == 1 && second.generation == 1);
    EXPECT_TRUE(fixture->live_process_count == 2);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(fixture, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &replacement)
    );
    EXPECT_TRUE(replacement.slot == first.slot);
    EXPECT_TRUE(replacement.generation == first.generation + 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_resolve(fixture, first, &process)
    );
    EXPECT_TRUE(process == NULL);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_resolve(fixture, replacement, &process)
    );
    EXPECT_TRUE(process == &fixture->processes[replacement.slot]);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}

bool kernel_object_test_process_exact_slot_creation_is_isolated(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle exact = {UINT16_MAX, UINT32_MAX};
    struct micros_process_handle lowest = {UINT16_MAX, UINT32_MAX};
    struct micros_process_handle sentinel = exact;
    struct micros_kernel_objects snapshot;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create_at(fixture, 5, &exact)
    );
    EXPECT_TRUE(exact.slot == 5 && exact.generation == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &lowest)
    );
    EXPECT_TRUE(lowest.slot == 0 && lowest.generation == 1);
    snapshot = *fixture;
    sentinel = (struct micros_process_handle){
        UINT16_MAX,
        UINT32_MAX,
    };
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_create_at(fixture, 5, &sentinel)
    );
    EXPECT_TRUE(
        sentinel.slot == UINT16_MAX
        && sentinel.generation == UINT32_MAX
        && memcmp(fixture, &snapshot, sizeof(*fixture)) == 0
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_create_at(
            fixture,
            MICROS_PROCESS_CAPACITY,
            &sentinel
        )
    );
    EXPECT_TRUE(
        sentinel.slot == UINT16_MAX
        && sentinel.generation == UINT32_MAX
        && memcmp(fixture, &snapshot, sizeof(*fixture)) == 0
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}

bool kernel_object_test_process_address_space_lifecycle_is_exact(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    const uintptr_t root = UINT64_C(0x0000000081000000);
    struct micros_process_handle process = {0};
    struct micros_process_handle stale;
    struct micros_thread_handle thread = {0};
    struct micros_kernel_objects snapshot;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &process)
    );
    stale = process;

    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_attach_address_space(NULL, process, root)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_detach_address_space(NULL, process, root)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_attach_address_space(fixture, process, 0)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_attach_address_space(fixture, process, root)
    );
    EXPECT_TRUE(
        fixture->processes[process.slot].address_space_root == root
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );

    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_attach_address_space(
            fixture,
            process,
            root + UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_detach_address_space(
            fixture,
            process,
            root + UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_release(fixture, process)
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, process, &thread)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_detach_address_space(fixture, process, root)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, thread)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_detach_address_space(fixture, process, root)
    );
    EXPECT_TRUE(
        fixture->processes[process.slot].address_space_root == 0
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(fixture, process)
    );

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &process)
    );
    snapshot = *fixture;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_attach_address_space(fixture, stale, root)
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_detach_address_space(fixture, stale, root)
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    return true;
}
