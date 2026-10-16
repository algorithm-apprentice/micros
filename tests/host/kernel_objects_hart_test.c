#include "kernel_objects_test_internal.h"

bool kernel_object_test_hart_policy_and_stack_validation_are_atomic(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_hart_handle hart = {0};
    struct micros_hart_handle unchanged = {UINT16_MAX, UINT32_MAX};
    struct micros_kernel_objects snapshot;
    const struct micros_hart *resolved = NULL;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 0, &hart)
    );
    snapshot = *fixture;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_hart_register(fixture, 1, &unchanged)
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STACK,
        micros_hart_install_trap_stacks(
            fixture,
            hart,
            UINT64_C(0x1001),
            UINT64_C(0x5000),
            UINT64_C(0x6000),
            UINT64_C(0x7000)
        )
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_install_trap_stacks(
            fixture,
            hart,
            UINT64_C(0x1000),
            UINT64_C(0x5000),
            UINT64_C(0x6000),
            UINT64_C(0x7000)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_resolve_context(
            fixture,
            (uintptr_t)&fixture->harts[hart.slot],
            &resolved
        )
    );
    EXPECT_TRUE(resolved == &fixture->harts[hart.slot]);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_hart_resolve_context(
            fixture,
            (uintptr_t)&fixture->harts[hart.slot] + 1,
            &resolved
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}
