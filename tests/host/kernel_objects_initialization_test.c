#include "kernel_objects_test_internal.h"

bool kernel_object_test_initialization_is_checked_and_one_shot(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_kernel_objects snapshot;

    kernel_object_test_reset_objects(fixture);
    snapshot = *fixture;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_kernel_objects_initialize(NULL, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_kernel_objects_initialize(fixture, 0, 1)
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_kernel_objects_initialize(
            fixture,
            MICROS_THREAD_CAPACITY + 1,
            1
        )
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_kernel_objects_initialize(fixture, 1, 0)
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_kernel_objects_initialize(
            fixture,
            1,
            MICROS_HART_CAPACITY + 1
        )
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);

    fixture->processes[0].generation = 1;
    snapshot = *fixture;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STORAGE,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_TRUE(fixture->max_threads_per_process == 1);
    EXPECT_TRUE(fixture->max_harts == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    snapshot = *fixture;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ALREADY_INITIALIZED,
        micros_kernel_objects_initialize(fixture, 2, 2)
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    return true;
}

bool kernel_object_test_uninitialized_registry_rejects_every_operation(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle process = {0, 1};
    struct micros_process_handle process_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_thread_handle thread = {0, 1};
    struct micros_thread_handle thread_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_hart_handle hart = {0, 1};
    struct micros_hart_handle hart_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    const struct micros_process *resolved_process =
        (const struct micros_process *)(uintptr_t)1;
    const struct micros_thread *resolved_thread =
        (const struct micros_thread *)(uintptr_t)1;
    const struct micros_hart *resolved_hart =
        (const struct micros_hart *)(uintptr_t)1;
    struct micros_user_context context =
        kernel_object_test_context_pattern(UINT64_C(0x1000));
    struct micros_user_context context_output =
        kernel_object_test_context_pattern(UINT64_C(0x2000));
    struct micros_user_context context_before = context_output;
    uintptr_t stack_bottom = UINTPTR_MAX;
    uintptr_t stack_top = UINTPTR_MAX;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_kernel_objects_validate(fixture)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_create(fixture, &process_output)
    );
    EXPECT_TRUE(
        process_output.slot == UINT16_MAX
        && process_output.generation == UINT32_MAX
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_release(fixture, process)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_attach_address_space(
            fixture,
            process,
            UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_detach_address_space(
            fixture,
            process,
            UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_resolve(
            fixture,
            process,
            &resolved_process
        )
    );
    EXPECT_TRUE(
        resolved_process
        == (const struct micros_process *)(uintptr_t)1
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_create(fixture, process, &thread_output)
    );
    EXPECT_TRUE(
        thread_output.slot == UINT16_MAX
        && thread_output.generation == UINT32_MAX
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_release(fixture, thread)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_resolve(fixture, thread, &resolved_thread)
    );
    EXPECT_TRUE(
        resolved_thread
        == (const struct micros_thread *)(uintptr_t)1
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_attach_execution_context(
            fixture,
            thread,
            UINT64_C(0x100000),
            UINT64_C(0x104000),
            &context
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_capture_execution_context(
            fixture,
            thread,
            &context
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_inspect_execution_context(
            fixture,
            thread,
            &context_output,
            &stack_bottom,
            &stack_top
        )
    );
    EXPECT_TRUE(kernel_object_test_contexts_equal(&context_output, &context_before));
    EXPECT_TRUE(stack_bottom == UINTPTR_MAX);
    EXPECT_TRUE(stack_top == UINTPTR_MAX);
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_detach_execution_context(fixture, thread)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_register(fixture, 0, &hart_output)
    );
    EXPECT_TRUE(
        hart_output.slot == UINT16_MAX
        && hart_output.generation == UINT32_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_resolve(fixture, hart, &resolved_hart)
    );
    EXPECT_TRUE(
        resolved_hart == (const struct micros_hart *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_resolve_context(
            fixture,
            (uintptr_t)&fixture->harts[0],
            &resolved_hart
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_install_trap_stacks(
            fixture,
            hart,
            UINT64_C(0x1000),
            UINT64_C(0x5000),
            UINT64_C(0x6000),
            UINT64_C(0x7000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_current_thread(
            fixture,
            hart,
            &thread_output
        )
    );
    return true;
}

bool kernel_object_test_fixed_capacities_report_exhaustion_atomically(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle
        processes[MICROS_PROCESS_CAPACITY];
    struct micros_thread_handle threads[MICROS_THREAD_CAPACITY];
    struct micros_hart_handle harts[MICROS_HART_CAPACITY];
    struct micros_process_handle process_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_thread_handle thread_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_hart_handle hart_output = {
        UINT16_MAX,
        UINT32_MAX,
    };
    size_t index;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_process_create(fixture, &processes[index])
        );
        EXPECT_TRUE(processes[index].slot == index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED,
        micros_process_create(fixture, &process_output)
    );
    EXPECT_TRUE(
        process_output.slot == UINT16_MAX
        && process_output.generation == UINT32_MAX
    );

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(
            fixture,
            MICROS_THREAD_CAPACITY,
            1
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &processes[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &processes[1])
    );
    for (index = 0; index < MICROS_THREAD_CAPACITY; ++index) {
        struct micros_process_handle owner =
            processes[index % 2];

        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_thread_create(fixture, owner, &threads[index])
        );
        EXPECT_TRUE(threads[index].slot == index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED,
        micros_thread_create(
            fixture,
            processes[0],
            &thread_output
        )
    );
    EXPECT_TRUE(
        thread_output.slot == UINT16_MAX
        && thread_output.generation == UINT32_MAX
    );

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(
            fixture,
            1,
            MICROS_HART_CAPACITY
        )
    );
    for (index = 0; index < MICROS_HART_CAPACITY; ++index) {
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_hart_register(fixture, index, &harts[index])
        );
        EXPECT_TRUE(harts[index].slot == index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_hart_register(
            fixture,
            MICROS_HART_CAPACITY,
            &hart_output
        )
    );
    EXPECT_TRUE(
        hart_output.slot == UINT16_MAX
        && hart_output.generation == UINT32_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}
