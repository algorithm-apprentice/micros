#include "kernel/tests/execution/user_execution_test_internal.h"

#include "micros/sv39.h"

static uint32_t code_permissions(void)
{
    return (
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_EXECUTE
    );
}

static uint32_t stack_permissions(void)
{
    return (
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
    );
}

static bool create_first_process(
    struct micros_user_execution_test_context *context
)
{
    return (
        micros_process_create(
            context->objects,
            &context->test_process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_user_address_space_create(context->test_process)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_allocate_page(
            context->test_process,
            MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions(),
            &context->code_physical_address
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_allocate_page(
            context->test_process,
            MICROS_USER_EXECUTION_TEST_STACK_VIRTUAL_ADDRESS,
            stack_permissions(),
            &context->stack_physical_address
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && micros_thread_create(
            context->objects,
            context->test_process,
            &context->test_thread
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

static bool create_second_process(
    struct micros_user_execution_test_context *context
)
{
    return (
        micros_process_create(
            context->objects,
            &context->second_process
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_user_address_space_create(context->second_process)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_allocate_page(
            context->second_process,
            MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS,
            code_permissions(),
            &context->second_code_physical_address
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && micros_user_address_space_allocate_page(
            context->second_process,
            MICROS_USER_EXECUTION_TEST_STACK_VIRTUAL_ADDRESS,
            stack_permissions(),
            &context->second_stack_physical_address
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && micros_thread_create(
            context->objects,
            context->second_process,
            &context->second_thread
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

static bool kernel_stack_layout_is_valid(
    struct micros_user_execution_test_context *context
)
{
    return (
        micros_user_execution_test_stack_bounds(
            context->test_thread,
            &context->kernel_stack_bottom,
            &context->kernel_stack_top
        )
        && micros_user_execution_test_stack_bounds(
            context->second_thread,
            &context->second_kernel_stack_bottom,
            &context->second_kernel_stack_top
        )
        && (uintptr_t)__thread_kernel_stacks_end
                - (uintptr_t)__thread_kernel_stacks_start
            == (
                MICROS_THREAD_CAPACITY
                * MICROS_THREAD_KERNEL_STACK_SIZE
            )
        && context->kernel_stack_bottom
            == (
                (uintptr_t)__thread_kernel_stacks_start
                + (
                    context->test_thread.slot
                    * MICROS_THREAD_KERNEL_STACK_SIZE
                )
            )
        && context->kernel_stack_top
            == context->kernel_stack_bottom
                + MICROS_THREAD_KERNEL_STACK_SIZE
        && context->second_kernel_stack_bottom
            == (
                (uintptr_t)__thread_kernel_stacks_start
                + (
                    context->second_thread.slot
                    * MICROS_THREAD_KERNEL_STACK_SIZE
                )
            )
        && context->second_kernel_stack_top
            == context->second_kernel_stack_bottom
                + MICROS_THREAD_KERNEL_STACK_SIZE
        && context->kernel_stack_top
            <= context->second_kernel_stack_bottom
    );
}

bool micros_user_execution_test_create_fixture(
    struct micros_user_execution_test_context *context
)
{
    return (
        context != NULL
        && create_first_process(context)
        && create_second_process(context)
        && kernel_stack_layout_is_valid(context)
    );
}

bool micros_user_execution_test_install_payload(
    struct micros_user_execution_test_context *context
)
{
    size_t payload_size;

    if (context == NULL) {
        return false;
    }
    payload_size =
        (uintptr_t)micros_user_execution_payload_end
        - (uintptr_t)micros_user_execution_payload_start;
    if (
        payload_size == 0
        || payload_size > MICROS_SV39_PAGE_SIZE
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        (void *)(uintptr_t)context->code_physical_address,
        micros_user_execution_payload_start,
        payload_size
    );
    micros_user_execution_test_copy_bytes(
        (void *)(uintptr_t)context->second_code_physical_address,
        micros_user_execution_payload_start,
        payload_size
    );
    __asm__ volatile("fence.i" : : : "memory");
    return true;
}

bool micros_user_execution_test_initialize_prepare_state(
    struct micros_user_execution_test_context *context
)
{
    if (context == NULL) {
        return false;
    }
    micros_user_execution_test_fill_context_pattern(
        &context->initial_context,
        UINT64_C(0x100)
    );
    context->initial_context.sepc =
        MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS;
    context->initial_context.sp =
        MICROS_USER_EXECUTION_TEST_STACK_VIRTUAL_ADDRESS
        + MICROS_SV39_PAGE_SIZE;
    context->initial_context.sstatus = 0;
    context->initial_context.t0 = UINT64_C(0x80200000);
    micros_user_execution_test_fill_stack(
        context->kernel_stack_bottom,
        UINT64_C(0xa5a5a5a5a5a5a5a5)
    );
    micros_user_execution_test_fill_stack(
        context->second_kernel_stack_bottom,
        UINT64_C(0x5a5a5a5a5a5a5a5a)
    );
    context->live_sstatus_before_prepare =
        micros_user_execution_test_read_sstatus()
        | MICROS_USER_EXECUTION_TEST_SSTATUS_FS;
    micros_user_execution_test_write_sstatus(
        context->live_sstatus_before_prepare
    );
    context->live_sstatus_before_prepare =
        micros_user_execution_test_read_sstatus();
    return (
        (
            context->live_sstatus_before_prepare
            & MICROS_USER_EXECUTION_TEST_SSTATUS_FS
        ) == MICROS_USER_EXECUTION_TEST_SSTATUS_FS
        && (
            context->live_sstatus_before_prepare
            & MICROS_USER_EXECUTION_TEST_SSTATUS_SD
        ) != 0
    );
}

static bool preparation_is_rejected(
    struct micros_user_execution_test_context *context,
    const struct micros_user_context *candidate,
    enum micros_user_execution_error expected_error
)
{
    return (
        micros_user_execution_test_snapshot_prepare_state(context)
        && micros_user_execution_prepare(
            context->test_thread,
            candidate
        ) == expected_error
        && micros_user_execution_test_prepare_state_matches(context)
    );
}

bool micros_user_execution_test_reject_invalid_preparations(
    struct micros_user_execution_test_context *context
)
{
    struct micros_user_context invalid_context;

    if (context == NULL) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_context,
        &context->initial_context,
        sizeof(invalid_context)
    );
    invalid_context.sstatus = 1;
    if (
        !preparation_is_rejected(
            context,
            &invalid_context,
            MICROS_USER_EXECUTION_ERROR_CONTEXT
        )
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_context,
        &context->initial_context,
        sizeof(invalid_context)
    );
    ++invalid_context.sepc;
    if (
        !preparation_is_rejected(
            context,
            &invalid_context,
            MICROS_USER_EXECUTION_ERROR_CONTEXT
        )
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_context,
        &context->initial_context,
        sizeof(invalid_context)
    );
    invalid_context.sepc =
        MICROS_USER_EXECUTION_TEST_STACK_VIRTUAL_ADDRESS;
    if (
        !preparation_is_rejected(
            context,
            &invalid_context,
            MICROS_USER_EXECUTION_ERROR_MAPPING
        )
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_context,
        &context->initial_context,
        sizeof(invalid_context)
    );
    --invalid_context.sp;
    if (
        !preparation_is_rejected(
            context,
            &invalid_context,
            MICROS_USER_EXECUTION_ERROR_CONTEXT
        )
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_context,
        &context->initial_context,
        sizeof(invalid_context)
    );
    invalid_context.sp =
        MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS
        + MICROS_SV39_PAGE_SIZE;
    if (
        !preparation_is_rejected(
            context,
            &invalid_context,
            MICROS_USER_EXECUTION_ERROR_MAPPING
        )
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &invalid_context,
        &context->initial_context,
        sizeof(invalid_context)
    );
    invalid_context.sp =
        MICROS_USER_VIRTUAL_BASE + UINT64_C(0x00009000);
    return preparation_is_rejected(
        context,
        &invalid_context,
        MICROS_USER_EXECUTION_ERROR_MAPPING
    );
}

static bool prepare_first_thread(
    struct micros_user_execution_test_context *context
)
{
    struct micros_user_context observed;

    if (
        micros_user_execution_prepare(
            context->test_thread,
            &context->initial_context
        ) != MICROS_USER_EXECUTION_OK
        || !micros_user_execution_test_stack_has_value(
            context->kernel_stack_bottom,
            0
        )
        || !micros_user_execution_test_stack_has_value(
            context->second_kernel_stack_bottom,
            UINT64_C(0x5a5a5a5a5a5a5a5a)
        )
        || micros_user_execution_inspect(
            context->test_thread,
            &observed
        ) != MICROS_USER_EXECUTION_OK
        || !micros_user_execution_test_context_has_valid_status(
            context,
            &observed
        )
        || observed.sepc != context->initial_context.sepc
        || observed.sp != context->initial_context.sp
    ) {
        return false;
    }
    micros_user_execution_test_copy_bytes(
        &context->prepared_context,
        &observed,
        sizeof(context->prepared_context)
    );
    return true;
}

static bool duplicate_prepare_is_rejected(
    struct micros_user_execution_test_context *context
)
{
    return (
        micros_user_execution_test_snapshot_prepare_state(context)
        && micros_user_execution_prepare(
            context->test_thread,
            &context->initial_context
        ) == MICROS_USER_EXECUTION_ERROR_STATE
        && micros_user_execution_test_prepare_state_matches(context)
    );
}

static bool prepare_second_thread(
    struct micros_user_execution_test_context *context
)
{
    micros_user_execution_test_fill_stack(
        context->kernel_stack_bottom,
        UINT64_C(0x3c3c3c3c3c3c3c3c)
    );
    if (
        micros_user_execution_prepare(
            context->second_thread,
            &context->initial_context
        ) != MICROS_USER_EXECUTION_OK
        || !micros_user_execution_test_stack_has_value(
            context->second_kernel_stack_bottom,
            0
        )
        || !micros_user_execution_test_stack_has_value(
            context->kernel_stack_bottom,
            UINT64_C(0x3c3c3c3c3c3c3c3c)
        )
    ) {
        return false;
    }
    micros_user_execution_test_fill_stack(
        context->kernel_stack_bottom,
        0
    );
    return true;
}

bool micros_user_execution_test_prepare_threads(
    struct micros_user_execution_test_context *context
)
{
    return (
        context != NULL
        && prepare_first_thread(context)
        && duplicate_prepare_is_rejected(context)
        && prepare_second_thread(context)
    );
}
