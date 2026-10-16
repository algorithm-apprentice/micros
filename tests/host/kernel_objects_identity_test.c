#include "kernel_objects_test_internal.h"

struct kernel_object_test_generation_work {
    uint32_t next;
    struct micros_process_handle process;
    struct micros_process_handle stale_process;
    struct micros_process_handle replacement;
    struct micros_thread_handle thread;
    struct micros_thread_handle stale_thread;
    struct micros_thread_handle final_thread;
    const struct micros_process *resolved_process;
    const struct micros_thread *resolved_thread;
};

static bool kernel_object_test_generation_value_boundaries(
    struct kernel_object_test_generation_work *work
)
{
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_next_generation(0, 0, &work->next)
    );
    EXPECT_TRUE(work->next == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_next_generation(
            0,
            MICROS_PROCESS_GENERATION_MAX - 1,
            &work->next
        )
    );
    EXPECT_TRUE(work->next == MICROS_PROCESS_GENERATION_MAX);
    work->next = UINT32_C(0xfeedface);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            UINT16_C(0x0ffe),
            MICROS_PROCESS_GENERATION_MAX - 1,
            &work->next
        )
    );
    EXPECT_TRUE(work->next == UINT32_C(0xfeedface));
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            UINT16_C(0x0fff),
            MICROS_PROCESS_GENERATION_MAX - 1,
            &work->next
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_process_next_generation(
            0,
            MICROS_PROCESS_GENERATION_MAX,
            &work->next
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_process_next_generation(UINT16_C(0x1000), 0, &work->next)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_next_generation(UINT32_MAX - 1, &work->next)
    );
    EXPECT_TRUE(work->next == UINT32_MAX);
    work->next = UINT32_C(0xfeedface);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_GENERATION_EXHAUSTED,
        micros_thread_next_generation(UINT32_MAX, &work->next)
    );
    EXPECT_TRUE(work->next == UINT32_C(0xfeedface));
    return true;
}

static bool kernel_object_test_thread_generation_quarantine(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_generation_work *work
)
{
    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &work->process)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, work->process, &work->thread)
    );
    fixture->threads[work->thread.slot].generation = UINT32_MAX - 1;
    work->thread.generation = UINT32_MAX - 1;
    work->stale_thread = work->thread;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, work->thread)
    );
    EXPECT_TRUE(
        fixture->threads[work->thread.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_FREE
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, work->process, &work->final_thread)
    );
    EXPECT_TRUE(work->final_thread.slot == work->stale_thread.slot);
    EXPECT_TRUE(work->final_thread.generation == UINT32_MAX);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_resolve(
            fixture,
            work->stale_thread,
            &work->resolved_thread
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, work->final_thread)
    );
    EXPECT_TRUE(
        fixture->threads[work->final_thread.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}

static bool kernel_object_test_process_generation_quarantine(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_generation_work *work
)
{
    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &work->process)
    );
    fixture->processes[work->process.slot].generation =
        MICROS_PROCESS_GENERATION_MAX - 1;
    work->process.generation = MICROS_PROCESS_GENERATION_MAX - 1;
    work->stale_process = work->process;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(fixture, work->process)
    );
    EXPECT_TRUE(
        fixture->processes[work->process.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_FREE
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &work->process)
    );
    EXPECT_TRUE(work->process.slot == work->stale_process.slot);
    EXPECT_TRUE(
        work->process.generation == MICROS_PROCESS_GENERATION_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_process_resolve(
            fixture,
            work->stale_process,
            &work->resolved_process
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(fixture, work->process)
    );
    EXPECT_TRUE(
        fixture->processes[work->process.slot].slot_state
        == MICROS_KERNEL_OBJECT_SLOT_QUARANTINED
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &work->replacement)
    );
    EXPECT_TRUE(work->replacement.slot == 1 && work->replacement.generation == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}

bool kernel_object_test_generation_boundaries_and_quarantine(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct kernel_object_test_generation_work work;
    work.next = UINT32_C(0xfeedface);
    work.process = (struct micros_process_handle){0};
    work.stale_process = (struct micros_process_handle){0};
    work.replacement = (struct micros_process_handle){0};
    work.thread = (struct micros_thread_handle){0};
    work.stale_thread = (struct micros_thread_handle){0};
    work.final_thread = (struct micros_thread_handle){0};
    work.resolved_process = NULL;
    work.resolved_thread = NULL;

    if (!kernel_object_test_generation_value_boundaries(&work)) {
        return false;
    }

    if (!kernel_object_test_thread_generation_quarantine(fixture, &work)) {
        return false;
    }

    if (!kernel_object_test_process_generation_quarantine(fixture, &work)) {
        return false;
    }
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
