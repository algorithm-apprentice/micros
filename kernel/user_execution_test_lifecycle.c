#include "kernel/user_execution_test_internal.h"

#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"

static bool release_completed_thread(
    struct micros_user_execution_test_context *context,
    struct micros_kernel_objects *objects
)
{
    struct micros_user_context observed;

    return (
        context->test_state == MICROS_USER_EXECUTION_TEST_SUPERVISOR
        && micros_user_execution_inspect(
            context->test_thread,
            &observed
        ) == MICROS_USER_EXECUTION_OK
        && observed.a0 == UINT64_C(0x0000000000000abd)
        && observed.a7 == 2
        && observed.sepc
            == micros_user_execution_test_user_address_of(
                micros_user_execution_payload_second_ecall
            )
        && micros_thread_scheduler_remove(
            objects,
            context->test_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && micros_user_execution_detach(context->test_thread)
            == MICROS_USER_EXECUTION_OK
        && micros_thread_release(objects, context->test_thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool create_replacement_thread(
    struct micros_user_execution_test_context *context,
    struct micros_kernel_objects *objects
)
{
    micros_user_execution_test_fill_stack(
        context->kernel_stack_bottom,
        UINT64_C(0xa5a5a5a5a5a5a5a5)
    );
    micros_user_execution_test_fill_stack(
        context->second_kernel_stack_bottom,
        UINT64_C(0x5a5a5a5a5a5a5a5a)
    );
    return (
        micros_thread_create(
            objects,
            context->test_process,
            &context->replacement_thread
        ) == MICROS_KERNEL_OBJECT_OK
        && context->replacement_thread.slot
            == context->test_thread.slot
        && context->replacement_thread.generation
            == context->test_thread.generation + 1
    );
}

static bool stale_thread_operations_are_rejected(
    struct micros_user_execution_test_context *context
)
{
    struct micros_user_context observed;
    struct micros_user_context unchanged;

    micros_user_execution_test_fill_context_pattern(
        &unchanged,
        UINT64_C(0xdead0000)
    );
    micros_user_execution_test_copy_bytes(
        &observed,
        &unchanged,
        sizeof(observed)
    );
    return (
        micros_user_execution_test_snapshot_prepare_state(context)
        && micros_user_execution_prepare(
            context->test_thread,
            &context->initial_context
        ) == MICROS_USER_EXECUTION_ERROR_STALE
        && micros_user_execution_inspect(
            context->test_thread,
            &observed
        ) == MICROS_USER_EXECUTION_ERROR_STALE
        && micros_user_execution_test_bytes_equal(
            &observed,
            &unchanged,
            sizeof(observed)
        )
        && micros_user_execution_detach(context->test_thread)
            == MICROS_USER_EXECUTION_ERROR_STALE
        && micros_user_execution_test_prepare_state_matches(context)
    );
}

static bool replacement_thread_lifecycle_is_valid(
    struct micros_user_execution_test_context *context,
    struct micros_kernel_objects *objects
)
{
    return (
        micros_user_execution_prepare(
            context->replacement_thread,
            &context->initial_context
        ) == MICROS_USER_EXECUTION_OK
        && micros_user_execution_test_stack_has_value(
            context->kernel_stack_bottom,
            0
        )
        && micros_user_execution_detach(
            context->replacement_thread
        ) == MICROS_USER_EXECUTION_OK
        && micros_thread_release(
            objects,
            context->replacement_thread
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

static bool second_thread_is_released(
    struct micros_user_execution_test_context *context,
    struct micros_kernel_objects *objects
)
{
    return (
        micros_user_execution_detach(context->second_thread)
            == MICROS_USER_EXECUTION_OK
        && micros_thread_release(objects, context->second_thread)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool second_process_is_released(
    struct micros_user_execution_test_context *context,
    struct micros_kernel_objects *objects
)
{
    uint64_t released;

    return (
        micros_user_address_space_release_page(
            context->second_process,
            MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == context->second_code_physical_address
        && micros_user_address_space_release_page(
            context->second_process,
            MICROS_USER_EXECUTION_TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == context->second_stack_physical_address
        && micros_user_address_space_destroy(context->second_process)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_process_release(objects, context->second_process)
            == MICROS_KERNEL_OBJECT_OK
    );
}

static bool test_process_is_released(
    struct micros_user_execution_test_context *context,
    struct micros_kernel_objects *objects
)
{
    uint64_t released;

    return (
        micros_user_address_space_release_page(
            context->test_process,
            MICROS_USER_EXECUTION_TEST_CODE_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == context->code_physical_address
        && micros_user_address_space_release_page(
            context->test_process,
            MICROS_USER_EXECUTION_TEST_STACK_VIRTUAL_ADDRESS,
            &released
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && released == context->stack_physical_address
        && micros_user_address_space_destroy(context->test_process)
            == MICROS_USER_ADDRESS_SPACE_OK
        && micros_frame_ownership_runtime_release_process(
            objects,
            context->test_process
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

static bool baseline_is_restored(
    const struct micros_user_execution_test_context *context,
    const struct micros_frame_ownership *ledger,
    struct micros_kernel_objects *objects
)
{
    return (
        ledger->owned_frame_count == context->baseline_owned
        && ledger->allocator->free_frame_count
            == context->baseline_free
        && micros_kernel_objects_validate(objects)
            == MICROS_KERNEL_OBJECT_OK
        && micros_frame_ownership_runtime_validate(objects)
            == MICROS_FRAME_OWNERSHIP_OK
    );
}

bool micros_user_execution_test_finish(
    struct micros_user_execution_test_context *context
)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    return (
        context != NULL
        && ledger != NULL
        && objects != NULL
        && release_completed_thread(context, objects)
        && create_replacement_thread(context, objects)
        && stale_thread_operations_are_rejected(context)
        && replacement_thread_lifecycle_is_valid(context, objects)
        && second_thread_is_released(context, objects)
        && second_process_is_released(context, objects)
        && test_process_is_released(context, objects)
        && baseline_is_restored(context, ledger, objects)
    );
}
