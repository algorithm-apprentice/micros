#include "kernel_objects_test_internal.h"

struct kernel_object_test_context_work {
    struct micros_process_handle process;
    struct micros_thread_handle first;
    struct micros_thread_handle second;
    struct micros_thread_handle stale;
    struct micros_hart_handle hart;
    struct micros_hart_handle second_hart;
    struct micros_user_context initial;
    struct micros_user_context captured;
    struct micros_user_context observed;
    struct micros_user_context unchanged;
    uintptr_t observed_bottom;
    uintptr_t observed_top;
    struct micros_kernel_objects snapshot;
};

static bool kernel_object_test_prepare_context_peers(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_context_work *work,
    uintptr_t idle_bottom,
    uintptr_t idle_top,
    uintptr_t emergency_bottom,
    uintptr_t emergency_top
)
{
    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 2, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 0, &work->hart)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 1, &work->second_hart)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_install_trap_stacks(
            fixture,
            work->hart,
            idle_bottom,
            idle_top,
            emergency_bottom,
            emergency_top
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &work->process)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, work->process, &work->first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, work->process, &work->second)
    );
    work->stale = work->first;
    return true;
}

static bool kernel_object_test_first_context_attach_and_inspect(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_context_work *work,
    uintptr_t first_stack_bottom,
    uintptr_t first_stack_top
)
{
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_ARGUMENT,
        micros_thread_attach_execution_context(
            fixture,
            work->first,
            first_stack_bottom,
            first_stack_top,
            NULL
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STACK,
        micros_thread_attach_execution_context(
            fixture,
            work->first,
            first_stack_bottom + 1,
            first_stack_top,
            &work->initial
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STACK,
        micros_thread_attach_execution_context(
            fixture,
            work->first,
            first_stack_bottom,
            first_stack_bottom
                + MICROS_THREAD_KERNEL_STACK_SIZE - 16,
            &work->initial
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_attach_execution_context(
            fixture,
            work->first,
            first_stack_bottom,
            first_stack_top,
            &work->initial
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_inspect_execution_context(
            fixture,
            work->first,
            &work->observed,
            &work->observed_bottom,
            &work->observed_top
        )
    );
    EXPECT_TRUE(kernel_object_test_contexts_equal(&work->observed, &work->initial));
    EXPECT_TRUE(work->observed_bottom == first_stack_bottom);
    EXPECT_TRUE(work->observed_top == first_stack_top);
    return true;
}

static bool kernel_object_test_context_peer_stack_conflicts(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_context_work *work,
    uintptr_t first_stack_bottom,
    uintptr_t first_stack_top,
    uintptr_t second_stack_bottom,
    uintptr_t second_stack_top,
    uintptr_t idle_bottom,
    uintptr_t idle_top,
    uintptr_t emergency_bottom
)
{
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_attach_execution_context(
            fixture,
            work->first,
            second_stack_bottom,
            second_stack_top,
            &work->initial
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STACK,
        micros_thread_attach_execution_context(
            fixture,
            work->second,
            first_stack_bottom + 16,
            first_stack_top + 16,
            &work->initial
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STACK,
        micros_thread_attach_execution_context(
            fixture,
            work->second,
            idle_bottom,
            idle_top,
            &work->initial
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STACK,
        micros_thread_attach_execution_context(
            fixture,
            work->second,
            emergency_bottom,
            emergency_bottom + MICROS_THREAD_KERNEL_STACK_SIZE,
            &work->initial
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_attach_execution_context(
            fixture,
            work->second,
            second_stack_bottom,
            second_stack_top,
            &work->initial
        )
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_release(fixture, work->first)
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_capture_execution_context(
            fixture,
            work->first,
            &work->captured
        )
    );
    return true;
}

static bool kernel_object_test_context_current_capture(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_context_work *work,
    uintptr_t first_stack_bottom,
    uintptr_t first_stack_top,
    uintptr_t idle_bottom,
    uintptr_t idle_top
)
{
    {
        struct micros_scheduler_return_plan plan;

        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_thread_scheduler_admit(
                fixture, work->hart, work->first, 7, 100, true
            )
        );
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_hart_plan_user_return(fixture, work->hart, &plan)
        );
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_hart_commit_user_return(fixture, &plan)
        );
    }
    EXPECT_TRUE(
        fixture->harts[work->hart.slot].trap.primary_stack_bottom
            == first_stack_bottom
    );
    EXPECT_TRUE(
        fixture->harts[work->hart.slot].trap.primary_stack_top
            == first_stack_top
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STACK,
        micros_hart_install_trap_stacks(
            fixture,
            work->second_hart,
            idle_bottom,
            idle_top,
            UINT64_C(0x0000000000500000),
            UINT64_C(0x0000000000501000)
        )
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_capture_execution_context(
            fixture,
            work->first,
            &work->captured
        )
    );
    work->observed = work->unchanged;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_inspect_execution_context(
            fixture,
            work->first,
            &work->observed,
            &work->observed_bottom,
            &work->observed_top
        )
    );
    EXPECT_TRUE(kernel_object_test_contexts_equal(&work->observed, &work->captured));
    return true;
}

static bool kernel_object_test_context_idle_detach_and_stale(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_context_work *work,
    uintptr_t idle_bottom,
    uintptr_t idle_top,
    uintptr_t first_stack_bottom,
    uintptr_t first_stack_top
)
{
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_thread_detach_execution_context(fixture, work->first)
    );
    {
        struct micros_scheduler_return_plan plan;

        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_thread_scheduler_hold(fixture, work->first)
        );
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_hart_plan_user_return(fixture, work->hart, &plan)
        );
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_hart_commit_user_return(fixture, &plan)
        );
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_thread_scheduler_remove(fixture, work->first)
        );
    }
    EXPECT_TRUE(
        fixture->harts[work->hart.slot].trap.primary_stack_bottom
            == idle_bottom
    );
    EXPECT_TRUE(
        fixture->harts[work->hart.slot].trap.primary_stack_top == idle_top
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_detach_execution_context(fixture, work->first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, work->first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, work->process, &work->first)
    );
    work->snapshot = *fixture;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_attach_execution_context(
            fixture,
            work->stale,
            first_stack_bottom,
            first_stack_top,
            &work->initial
        )
    );
    EXPECT_TRUE(memcmp(fixture, &work->snapshot, sizeof(*fixture)) == 0);
    work->observed = work->unchanged;
    work->observed_bottom = UINTPTR_MAX;
    work->observed_top = UINTPTR_MAX;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_inspect_execution_context(
            fixture,
            work->stale,
            &work->observed,
            &work->observed_bottom,
            &work->observed_top
        )
    );
    EXPECT_TRUE(kernel_object_test_contexts_equal(&work->observed, &work->unchanged));
    EXPECT_TRUE(work->observed_bottom == UINTPTR_MAX);
    EXPECT_TRUE(work->observed_top == UINTPTR_MAX);
    return true;
}

static bool kernel_object_test_context_lifecycle_cleanup(
    struct micros_kernel_objects *fixture,
    struct kernel_object_test_context_work *work
)
{
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_detach_execution_context(fixture, work->second)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, work->second)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, work->first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(fixture, work->process)
    );
    return true;
}

bool kernel_object_test_thread_execution_context_lifecycle_is_exact(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    const uintptr_t idle_bottom = UINT64_C(0x0000000000100000);
    const uintptr_t idle_top =
        idle_bottom + MICROS_PRIMARY_TRAP_STACK_MIN_SIZE;
    const uintptr_t emergency_bottom =
        UINT64_C(0x0000000000200000);
    const uintptr_t emergency_top =
        emergency_bottom + MICROS_EMERGENCY_TRAP_STACK_MIN_SIZE;
    const uintptr_t first_stack_bottom =
        UINT64_C(0x0000000000300000);
    const uintptr_t first_stack_top =
        first_stack_bottom + MICROS_THREAD_KERNEL_STACK_SIZE;
    const uintptr_t second_stack_bottom =
        UINT64_C(0x0000000000400000);
    const uintptr_t second_stack_top =
        second_stack_bottom
        + (2 * MICROS_THREAD_KERNEL_STACK_SIZE);
    struct kernel_object_test_context_work work;
    work.process = (struct micros_process_handle){0};
    work.first = (struct micros_thread_handle){0};
    work.second = (struct micros_thread_handle){0};
    work.hart = (struct micros_hart_handle){0};
    work.second_hart = (struct micros_hart_handle){0};
    work.initial = kernel_object_test_context_pattern(UINT64_C(0x1000));
    work.captured = kernel_object_test_context_pattern(UINT64_C(0x2000));
    work.observed = kernel_object_test_context_pattern(UINT64_C(0xdead0000));
    work.unchanged = work.observed;
    work.observed_bottom = UINTPTR_MAX;
    work.observed_top = UINTPTR_MAX;

    if (!kernel_object_test_prepare_context_peers(
        fixture, &work, idle_bottom,
        idle_top, emergency_bottom, emergency_top
    )) {
        return false;
    }

    if (!kernel_object_test_first_context_attach_and_inspect(
        fixture, &work, first_stack_bottom,
        first_stack_top
    )) {
        return false;
    }

    if (!kernel_object_test_context_peer_stack_conflicts(
        fixture, &work, first_stack_bottom,
        first_stack_top, second_stack_bottom, second_stack_top,
        idle_bottom, idle_top, emergency_bottom
    )) {
        return false;
    }

    if (!kernel_object_test_context_current_capture(
        fixture, &work, first_stack_bottom,
        first_stack_top, idle_bottom, idle_top
    )) {
        return false;
    }

    if (!kernel_object_test_context_idle_detach_and_stale(
        fixture, &work, idle_bottom,
        idle_top, first_stack_bottom, first_stack_top
    )) {
        return false;
    }

    if (!kernel_object_test_context_lifecycle_cleanup(
        fixture, &work
    )) {
        return false;
    }
    return true;
}

bool kernel_object_test_thread_slots_reuse_with_new_generations(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle owner = {0};
    struct micros_thread_handle first = {0};
    struct micros_thread_handle replacement = {0};
    struct micros_hart_handle hart = {0};
    const struct micros_thread *resolved = NULL;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
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
        micros_thread_release(fixture, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, owner, &replacement)
    );
    EXPECT_TRUE(replacement.slot == first.slot);
    EXPECT_TRUE(replacement.generation == first.generation + 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_resolve(fixture, first, &resolved)
    );
    EXPECT_TRUE(resolved == NULL);
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_release(fixture, first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_hart_register(fixture, 0, &hart)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_resolve(fixture, replacement, &resolved)
    );
    EXPECT_TRUE(resolved == &fixture->threads[replacement.slot]);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}

bool kernel_object_test_thread_ownership_and_production_limit(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle owner = {0};
    struct micros_process_handle other = {0};
    struct micros_thread_handle first = {0};
    struct micros_thread_handle second = {
        UINT16_MAX,
        UINT32_MAX,
    };
    struct micros_kernel_objects snapshot;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 1, 1)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &other)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, owner, &first)
    );
    EXPECT_TRUE(fixture->processes[owner.slot].live_thread_count == 1);
    snapshot = *fixture;
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT,
        micros_thread_create(fixture, owner, &second)
    );
    EXPECT_TRUE(memcmp(fixture, &snapshot, sizeof(*fixture)) == 0);
    EXPECT_TRUE(
        second.slot == UINT16_MAX
        && second.generation == UINT32_MAX
    );
    EXPECT_ERROR_UNCHANGED(
        MICROS_KERNEL_OBJECT_ERROR_STATE,
        micros_process_release(fixture, owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_create(fixture, other, &second)
    );
    EXPECT_TRUE(second.slot == 1 && second.generation == 1);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_thread_release(fixture, first)
    );
    EXPECT_TRUE(fixture->processes[owner.slot].live_thread_count == 0);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_release(fixture, owner)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_STALE,
        micros_thread_create(fixture, owner, &first)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}

bool kernel_object_test_model_policy_supports_multiple_threads(void)
{
    struct micros_kernel_objects *fixture = kernel_object_test_registry();
    struct micros_process_handle owner = {0};
    struct micros_thread_handle threads[4];
    struct micros_thread_handle unchanged = {UINT16_MAX, UINT32_MAX};
    size_t index;

    kernel_object_test_reset_objects(fixture);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_initialize(fixture, 4, 2)
    );
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_process_create(fixture, &owner)
    );
    for (index = 0; index < 4; ++index) {
        EXPECT_ERROR(
            MICROS_KERNEL_OBJECT_OK,
            micros_thread_create(fixture, owner, &threads[index])
        );
        EXPECT_TRUE(threads[index].slot == index);
    }
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_ERROR_THREAD_LIMIT,
        micros_thread_create(fixture, owner, &unchanged)
    );
    EXPECT_TRUE(fixture->processes[owner.slot].live_thread_count == 4);
    EXPECT_ERROR(
        MICROS_KERNEL_OBJECT_OK,
        micros_kernel_objects_validate(fixture)
    );
    return true;
}
