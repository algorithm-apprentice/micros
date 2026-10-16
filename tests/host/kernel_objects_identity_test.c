#include "kernel_objects_test_internal.h"

bool kernel_object_test_generation_boundaries_and_quarantine(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    uint32_t next = UINT32_C(0xfeedface);
    struct micros_process_handle process = {0};
    struct micros_process_handle stale_process = {0};
    struct micros_process_handle replacement = {0};
    struct micros_thread_handle thread = {0};
    struct micros_thread_handle stale_thread = {0};
    struct micros_thread_handle final_thread = {0};
    const struct micros_process *resolved_process = NULL;
    const struct micros_thread *resolved_thread = NULL;

    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_next_generation(0, 0, &next)
    );
    EXPECT_TRUE(next == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_next_generation(
            0,
            MICROS_PROCESS_GENERATION_MAX - 1,
            &next
        )
    );
    EXPECT_TRUE(next == MICROS_PROCESS_GENERATION_MAX);
    next = UINT32_C(0xfeedface);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            UINT16_C(0x0ffe),
            MICROS_PROCESS_GENERATION_MAX - 1,
            &next
        )
    );
    EXPECT_TRUE(next == UINT32_C(0xfeedface));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            UINT16_C(0x0fff),
            MICROS_PROCESS_GENERATION_MAX - 1,
            &next
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            0,
            MICROS_PROCESS_GENERATION_MAX,
            &next
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_next_generation(UINT16_C(0x1000), 0, &next)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_next_generation(UINT32_MAX - 1, &next)
    );
    EXPECT_TRUE(next == UINT32_MAX);
    next = UINT32_C(0xfeedface);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_thread_next_generation(UINT32_MAX, &next)
    );
    EXPECT_TRUE(next == UINT32_C(0xfeedface));

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &process)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, process, &thread)
    );
    fixture->threads[thread.slot].generation = UINT32_MAX - 1;
    thread.generation = UINT32_MAX - 1;
    stale_thread = thread;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, thread)
    );
    EXPECT_TRUE(
        fixture->threads[thread.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_FREE
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, process, &final_thread)
    );
    EXPECT_TRUE(final_thread.slot == stale_thread.slot);
    EXPECT_TRUE(final_thread.generation == UINT32_MAX);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_resolve(
            fixture,
            stale_thread,
            &resolved_thread
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, final_thread)
    );
    EXPECT_TRUE(
        fixture->threads[final_thread.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &process)
    );
    fixture->processes[process.slot].generation =
        MICROS_PROCESS_GENERATION_MAX - 1;
    process.generation = MICROS_PROCESS_GENERATION_MAX - 1;
    stale_process = process;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(fixture, process)
    );
    EXPECT_TRUE(
        fixture->processes[process.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_FREE
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &process)
    );
    EXPECT_TRUE(process.slot == stale_process.slot);
    EXPECT_TRUE(
        process.generation == MICROS_PROCESS_GENERATION_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_resolve(
            fixture,
            stale_process,
            &resolved_process
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(fixture, process)
    );
    EXPECT_TRUE(
        fixture->processes[process.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &replacement)
    );
    EXPECT_TRUE(replacement.slot == 1 && replacement.generation == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}

bool kernel_object_test_invalid_arguments_and_stale_handles_preserve_outputs(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle process = {0};
    struct micros_process_handle stale_process = {0};
    struct micros_thread_handle thread = {0};
    struct micros_thread_handle stale_thread = {0};
    struct micros_hart_handle hart = {0};
    struct micros_hart_handle stale_hart = {0};
    const struct micros_process *process_output =
        (const struct micros_process *)(uintptr_t)1;
    const struct micros_thread *thread_output =
        (const struct micros_thread *)(uintptr_t)1;
    const struct micros_hart *hart_output =
        (const struct micros_hart *)(uintptr_t)1;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &process)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, process, &thread)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 0, &hart)
    );
    stale_process = process;
    ++stale_process.generation;
    stale_thread = thread;
    ++stale_thread.generation;
    stale_hart = hart;
    ++stale_hart.generation;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_resolve(
            fixture,
            stale_process,
            &process_output
        )
    );
    EXPECT_TRUE(
        process_output
        == (const struct micros_process *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_resolve(fixture, stale_thread, &thread_output)
    );
    EXPECT_TRUE(
        thread_output
        == (const struct micros_thread *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_hart_resolve(fixture, stale_hart, &hart_output)
    );
    EXPECT_TRUE(
        hart_output == (const struct micros_hart *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_hart_resolve(fixture, hart, NULL)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_create(fixture, NULL)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_create(fixture, process, NULL)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_hart_register(fixture, 1, NULL)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_next_generation(0, 0, NULL)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_next_generation(0, NULL)
    );
    return true;
}
