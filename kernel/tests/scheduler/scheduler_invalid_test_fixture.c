#include "kernel/tests/scheduler/scheduler_invalid_test_internal.h"

#include "micros/kernel_object_runtime.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"

bool scheduler_invalid_test_preflight_payload(
    const struct micros_kernel_objects *objects,
    size_t payload_size
)
{
    if (
        objects == NULL
        || payload_size == 0
        || payload_size > MICROS_SV39_PAGE_SIZE
    ) {
        return false;
    }
    return true;
}

bool scheduler_invalid_test_reserve_process(
    struct scheduler_invalid_test_context *fixture,
    struct micros_kernel_objects *objects,
    size_t index,
    uint32_t code_permissions,
    uint32_t stack_permissions
)
{
    if (
        micros_process_create(objects, &fixture->processes[index])
            != MICROS_KERNEL_OBJECT_OK
        || micros_user_address_space_create(fixture->processes[index])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions,
            &fixture->code_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            TEST_COUNTER_VIRTUAL_ADDRESS,
            stack_permissions,
            &fixture->counter_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_user_address_space_allocate_page(
            fixture->processes[index],
            TEST_STACK_VIRTUAL_ADDRESS,
            stack_permissions,
            &fixture->stack_physical[index]
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || micros_thread_create(
            objects,
            fixture->processes[index],
            &fixture->threads[index]
        ) != MICROS_KERNEL_OBJECT_OK
        || !micros_user_execution_test_stack_bounds(
            fixture->threads[index],
            &fixture->kernel_stack_bottom[index],
            &fixture->kernel_stack_top[index]
        )
    ) {
        return false;
    }
    return true;
}

bool scheduler_invalid_test_prepare_image(
    struct scheduler_invalid_test_context *fixture,
    size_t index,
    size_t payload_size
)
{
    struct micros_user_context context;

    scheduler_invalid_test_copy_bytes(
        (void *)(uintptr_t)fixture->code_physical[index],
        micros_scheduler_payload_start,
        payload_size
    );
    scheduler_invalid_test_fill_context_pattern(
        &context,
        UINT64_C(0x1000) + index * UINT64_C(0x1000)
    );
    context.sepc = TEST_CODE_VIRTUAL_ADDRESS;
    context.sp =
        TEST_STACK_VIRTUAL_ADDRESS + MICROS_SV39_PAGE_SIZE;
    context.s0 = TEST_COUNTER_VIRTUAL_ADDRESS;
    context.s1 = 0;
    context.t1 = 1;
    context.sstatus = 0;
    if (
        micros_user_execution_prepare(
            fixture->threads[index],
            &context
        ) != MICROS_USER_EXECUTION_OK
    ) {
        return false;
    }
    return true;
}

bool scheduler_invalid_test_admit_threads(
    struct scheduler_invalid_test_context *fixture
)
{
    if (
        micros_scheduler_initialize(TEST_TIMER_INTERVAL)
            != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            fixture->threads[0],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            TEST_QUANTUM
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            fixture->threads[1],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            TEST_QUANTUM
        ) != MICROS_SCHEDULER_OK
    ) {
        return false;
    }
    return true;
}

#ifdef MICROS_BUILD_SCHEDULER_INVALID_NEXT_TEST
void scheduler_invalid_test_corrupt_next_context(
    struct scheduler_invalid_test_context *fixture,
    struct micros_kernel_objects *objects
)
{
    objects->threads[fixture->threads[1].slot].user_context.sp = 1;
}
#endif
