#include "kernel_objects_test_internal.h"

struct kernel_object_test_uninitialized_work {
    struct micros_process_handle process;
    struct micros_process_handle process_output;
    struct micros_thread_handle thread;
    struct micros_thread_handle thread_output;
    struct micros_hart_handle hart;
    struct micros_hart_handle hart_output;
    const struct micros_process *resolved_process;
    const struct micros_thread *resolved_thread;
    const struct micros_hart *resolved_hart;
    struct micros_user_context context;
    struct micros_user_context context_output;
    struct micros_user_context context_before;
    uintptr_t stack_bottom;
    uintptr_t stack_top;
};

struct kernel_object_test_capacity_work {
    struct micros_process_handle processes[MICROS_PROCESS_CAPACITY];
    struct micros_thread_handle threads[MICROS_THREAD_CAPACITY];
    struct micros_hart_handle harts[MICROS_HART_CAPACITY];
    struct micros_process_handle process_output;
    struct micros_thread_handle thread_output;
    struct micros_hart_handle hart_output;
    size_t index;
};

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

static bool kernel_object_test_uninitialized_process_operations(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_uninitialized_work *work
)
{
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_create(fixture, &work->process_output)
    );
    EXPECT_TRUE(
        work->process_output.slot == UINT16_MAX
        && work->process_output.generation == UINT32_MAX
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_release(fixture, work->process)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_attach_address_space(
            fixture,
            work->process,
            UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_detach_address_space(
            fixture,
            work->process,
            UINT64_C(0x1000)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_process_resolve(
            fixture,
            work->process,
            &work->resolved_process
        )
    );
    EXPECT_TRUE(
        work->resolved_process
        == (const struct micros_process *)(uintptr_t)1
    );
    return true;
}

static bool kernel_object_test_uninitialized_thread_context_operations(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_uninitialized_work *work
)
{
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_create(fixture, work->process, &work->thread_output)
    );
    EXPECT_TRUE(
        work->thread_output.slot == UINT16_MAX
        && work->thread_output.generation == UINT32_MAX
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_release(fixture, work->thread)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_resolve(fixture, work->thread, &work->resolved_thread)
    );
    EXPECT_TRUE(
        work->resolved_thread
        == (const struct micros_thread *)(uintptr_t)1
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_attach_execution_context(
            fixture,
            work->thread,
            UINT64_C(0x100000),
            UINT64_C(0x104000),
            &work->context
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_capture_execution_context(
            fixture,
            work->thread,
            &work->context
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_inspect_execution_context(
            fixture,
            work->thread,
            &work->context_output,
            &work->stack_bottom,
            &work->stack_top
        )
    );
    EXPECT_TRUE(kernel_object_test_contexts_equal(&work->context_output, &work->context_before));
    EXPECT_TRUE(work->stack_bottom == UINTPTR_MAX);
    EXPECT_TRUE(work->stack_top == UINTPTR_MAX);
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_thread_detach_execution_context(fixture, work->thread)
    );
    return true;
}

static bool kernel_object_test_uninitialized_hart_operations(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_uninitialized_work *work
)
{
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_register(fixture, 0, &work->hart_output)
    );
    EXPECT_TRUE(
        work->hart_output.slot == UINT16_MAX
        && work->hart_output.generation == UINT32_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_resolve(fixture, work->hart, &work->resolved_hart)
    );
    EXPECT_TRUE(
        work->resolved_hart == (const struct micros_hart *)(uintptr_t)1
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_resolve_context(
            fixture,
            (uintptr_t)&fixture->harts[0],
            &work->resolved_hart
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_hart_install_trap_stacks(
            fixture,
            work->hart,
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
            work->hart,
            &work->thread_output
        )
    );
    return true;
}

bool kernel_object_test_uninitialized_registry_rejects_every_operation(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct kernel_object_test_uninitialized_work work;
    work.process = (struct micros_process_handle){0, 1};
    work.process_output = (struct micros_process_handle){
        UINT16_MAX,
        UINT32_MAX,
    };
    work.thread = (struct micros_thread_handle){0, 1};
    work.thread_output = (struct micros_thread_handle){
        UINT16_MAX,
        UINT32_MAX,
    };
    work.hart = (struct micros_hart_handle){0, 1};
    work.hart_output = (struct micros_hart_handle){
        UINT16_MAX,
        UINT32_MAX,
    };
    work.resolved_process = (const struct micros_process *)(uintptr_t)1;
    work.resolved_thread = (const struct micros_thread *)(uintptr_t)1;
    work.resolved_hart = (const struct micros_hart *)(uintptr_t)1;
    work.context = kernel_object_test_context_pattern(UINT64_C(0x1000));
    work.context_output = kernel_object_test_context_pattern(UINT64_C(0x2000));
    work.context_before = work.context_output;
    work.stack_bottom = UINTPTR_MAX;
    work.stack_top = UINTPTR_MAX;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_NOT_INITIALIZED,
        micros_kernel_objects_validate(fixture)
    );
    if (!kernel_object_test_uninitialized_process_operations(fixture, &work)) {
        return false;
    }

    if (!kernel_object_test_uninitialized_thread_context_operations(fixture, &work)) {
        return false;
    }

    if (!kernel_object_test_uninitialized_hart_operations(fixture, &work)) {
        return false;
    }
    return true;
}

static bool kernel_object_test_process_capacity_refusal(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_capacity_work *work
)
{
    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    for (work->index = 0; work->index < MICROS_PROCESS_CAPACITY; ++work->index) {
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_process_create(fixture, &work->processes[work->index])
        );
        EXPECT_TRUE(work->processes[work->index].slot == work->index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED,
        micros_process_create(fixture, &work->process_output)
    );
    EXPECT_TRUE(
        work->process_output.slot == UINT16_MAX
        && work->process_output.generation == UINT32_MAX
    );
    return true;
}

static bool kernel_object_test_thread_capacity_refusal(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_capacity_work *work
)
{
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
        micros_process_create(fixture, &work->processes[0])
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &work->processes[1])
    );
    for (work->index = 0; work->index < MICROS_THREAD_CAPACITY; ++work->index) {
        struct micros_process_handle owner =
            work->processes[work->index % 2];

        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_thread_create(fixture, owner, &work->threads[work->index])
        );
        EXPECT_TRUE(work->threads[work->index].slot == work->index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_EXHAUSTED,
        micros_thread_create(
            fixture,
            work->processes[0],
            &work->thread_output
        )
    );
    EXPECT_TRUE(
        work->thread_output.slot == UINT16_MAX
        && work->thread_output.generation == UINT32_MAX
    );
    return true;
}

static bool kernel_object_test_hart_capacity_refusal(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_capacity_work *work
)
{
    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(
            fixture,
            1,
            MICROS_HART_CAPACITY
        )
    );
    for (work->index = 0; work->index < MICROS_HART_CAPACITY; ++work->index) {
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_hart_register(fixture, work->index, &work->harts[work->index])
        );
        EXPECT_TRUE(work->harts[work->index].slot == work->index);
    }
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_POLICY,
        micros_hart_register(
            fixture,
            MICROS_HART_CAPACITY,
            &work->hart_output
        )
    );
    EXPECT_TRUE(
        work->hart_output.slot == UINT16_MAX
        && work->hart_output.generation == UINT32_MAX
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}

bool kernel_object_test_fixed_capacities_report_exhaustion_atomically(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct kernel_object_test_capacity_work work;
    work.process_output = (struct micros_process_handle){
        UINT16_MAX,
        UINT32_MAX,
    };
    work.thread_output = (struct micros_thread_handle){
        UINT16_MAX,
        UINT32_MAX,
    };
    work.hart_output = (struct micros_hart_handle){
        UINT16_MAX,
        UINT32_MAX,
    };

    if (!kernel_object_test_process_capacity_refusal(fixture, &work)) {
        return false;
    }

    if (!kernel_object_test_thread_capacity_refusal(fixture, &work)) {
        return false;
    }

    if (!kernel_object_test_hart_capacity_refusal(fixture, &work)) {
        return false;
    }
    return true;
}
