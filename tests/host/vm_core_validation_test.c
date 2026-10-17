#include "tests/host/vm_core_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/sv39.h"

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

bool test_capacity_and_invariants(
    struct vm_core_test_context *context
)
{
    struct micros_application_stack_values values = vm_core_test_stack_values(7);
    struct micros_vm_load_page_action action;
    struct micros_vm_load_page_action sentinel;
    size_t executable_size;
    size_t request_size;

    EXPECT_TRUE(
        vm_core_test_initialize_boot_info(context, TEST_FIRST_FREE_FRAME + 7)
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
    );
    executable_size = vm_core_test_build_elf(context, 1, 1, 1);
    request_size = vm_core_test_build_request(context);
    EXPECT_TRUE(
        micros_vm_load_prepare(
            &context->state,
            UINT64_C(0x1111),
            values.pid,
            context->executable,
            executable_size,
            context->request,
            request_size,
            &values
        ) == MICROS_VM_CORE_ERROR_NO_SPACE
        && context->state.phase == MICROS_VM_LOADER_IDLE
        && vm_core_test_bytes_are_zero(
            context->state.mapping_records,
            sizeof(context->state.mapping_records)
        )
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );

    EXPECT_TRUE(
        vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT)
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
    );
    executable_size = vm_core_test_build_elf(context, 5, 5, 5);
    request_size = vm_core_test_build_request(context);
    EXPECT_TRUE(
        micros_vm_load_prepare(
            &context->state,
            UINT64_C(0x1111),
            values.pid,
            context->executable,
            executable_size,
            context->request,
            request_size,
            &values
        ) == MICROS_VM_CORE_OK
        && context->state.transaction.page_count
            == MICROS_APPLICATION_RESIDENT_PAGE_MAX
        && micros_vm_load_bind(
            &context->state,
            (struct micros_process_handle){2, 7},
            UINT64_C(0x2222)
        ) == MICROS_VM_CORE_OK
        && context->state.mapping_records[2].entry_count
            == MICROS_VM_MAPPING_ENTRY_CAPACITY
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    context->state.mapping_records[2].entries[4].physical_address =
        context->state.mapping_records[2].entries[5].physical_address;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(
        vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT)
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
    );
    memset(&sentinel, 0xa5, sizeof(sentinel));
    action = sentinel;
    EXPECT_TRUE(
        micros_vm_load_next_page(&context->state, &action)
            == MICROS_VM_CORE_ERROR_STATE
        && memcmp(&action, &sentinel, sizeof(action)) == 0
    );
    return true;
}

static bool initialize_valid_state(
    struct vm_core_test_context *context,
    size_t managed_frame_count
)
{
    return (
        vm_core_test_initialize_boot_info(
            context,
            managed_frame_count
        )
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
        && micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_OK
    );
}

static bool valid_managed_count_boundaries(
    struct vm_core_test_context *context
)
{
    static const size_t counts[] = {
        TEST_FIRST_FREE_FRAME,
        63,
        64,
        65,
        28904,
        MICROS_VM_MAX_MANAGED_FRAMES - 1,
        MICROS_VM_MAX_MANAGED_FRAMES,
    };
    size_t index;

    for (index = 0; index < sizeof(counts) / sizeof(counts[0]); ++index) {
        EXPECT_TRUE(initialize_valid_state(context, counts[index]));
    }
    return true;
}

static bool invalid_managed_counts_are_bounded(
    struct vm_core_test_context *context
)
{
    uint64_t invalid_count;

    EXPECT_TRUE(initialize_valid_state(context, TEST_MANAGED_FRAME_COUNT));
    context->boot_info.header.managed_frame_count = 0;
    context->state.boot_summary.managed_frame_count = 0;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
        && context->boot_info.header.managed_frame_count == 0
        && context->state.boot_summary.managed_frame_count == 0
    );

    invalid_count = MICROS_VM_MAX_MANAGED_FRAMES + UINT64_C(1);
    EXPECT_TRUE(initialize_valid_state(context, TEST_MANAGED_FRAME_COUNT));
    context->boot_info.header.managed_frame_count = invalid_count;
    context->state.boot_summary.managed_frame_count = invalid_count;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
        && context->boot_info.header.managed_frame_count
            == invalid_count
        && context->state.boot_summary.managed_frame_count
            == invalid_count
    );
    return true;
}

static bool inactive_frame_state_corruption_is_rejected(
    struct vm_core_test_context *context
)
{
    EXPECT_TRUE(initialize_valid_state(context, 63));
    context->boot_info.frame_states[63] = MICROS_VM_FRAME_FREE;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(initialize_valid_state(context, TEST_FIRST_FREE_FRAME));
    context->boot_info.frame_states[
        MICROS_VM_MAX_MANAGED_FRAMES - 1
    ] = MICROS_VM_FRAME_FREE;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );
    return true;
}

static bool inactive_bitmap_corruption_is_rejected(
    struct vm_core_test_context *context
)
{
    EXPECT_TRUE(initialize_valid_state(context, 63));
    context->state.initial_free_bitmap[0] |= UINT64_C(1) << 63;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(initialize_valid_state(context, 64));
    context->state.initial_free_bitmap[1] |= UINT64_C(1);
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(initialize_valid_state(context, 65));
    context->state.initial_free_bitmap[1] |= UINT64_C(1) << 1;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(initialize_valid_state(context, 65));
    context->state.initial_free_bitmap[2] |= UINT64_C(1);
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(
        initialize_valid_state(context, TEST_FIRST_FREE_FRAME)
    );
    context->state.initial_free_bitmap[
        MICROS_VM_INITIAL_FREE_BITMAP_WORDS - 1
    ] |= UINT64_C(1) << 63;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );
    return true;
}

static bool active_boot_database_corruption_is_rejected(
    struct vm_core_test_context *context
)
{
    EXPECT_TRUE(
        initialize_valid_state(context, TEST_MANAGED_FRAME_COUNT)
    );
    context->boot_info.frame_states[0] =
        MICROS_VM_FRAME_SELF_WIRED;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );

    EXPECT_TRUE(
        initialize_valid_state(context, TEST_MANAGED_FRAME_COUNT)
    );
    context->state.initial_free_bitmap[0] ^=
        UINT64_C(1) << TEST_FIRST_FREE_FRAME;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );
    return true;
}

bool test_staged_inputs_and_immutable_database(
    struct vm_core_test_context *context
)
{
    struct micros_application_stack_values values = vm_core_test_stack_values(7);
    size_t executable_size;
    size_t request_size;

    EXPECT_TRUE(
        valid_managed_count_boundaries(context)
        && invalid_managed_counts_are_bounded(context)
        && inactive_frame_state_corruption_is_rejected(context)
        && inactive_bitmap_corruption_is_rejected(context)
        && active_boot_database_corruption_is_rejected(context)
    );

    EXPECT_TRUE(
        vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT)
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
    );
    executable_size = vm_core_test_build_elf(context, 1, 1, 1);
    request_size = vm_core_test_build_request(context);
    memcpy(context->state.executable_bytes, context->executable, executable_size);
    memcpy(context->state.request_bytes, context->request, request_size);
    EXPECT_TRUE(
        micros_vm_load_prepare(
            &context->state,
            UINT64_C(0x1111),
            values.pid,
            context->state.executable_bytes,
            executable_size,
            context->state.request_bytes,
            request_size,
            &values
        ) == MICROS_VM_CORE_OK
        && micros_vm_load_aborted(&context->state, 0, 0)
            == MICROS_VM_CORE_OK
        && vm_core_test_bytes_are_zero(
            context->state.executable_bytes,
            sizeof(context->state.executable_bytes)
        )
        && vm_core_test_bytes_are_zero(
            context->state.request_bytes,
            sizeof(context->state.request_bytes)
        )
    );

    executable_size = vm_core_test_build_elf(context, 1, 1, 1);
    request_size = vm_core_test_build_request(context);
    context->executable[0] = 0;
    memcpy(context->state.executable_bytes, context->executable, executable_size);
    memcpy(context->state.request_bytes, context->request, request_size);
    EXPECT_TRUE(
        micros_vm_load_prepare(
            &context->state,
            UINT64_C(0x1111),
            values.pid,
            context->state.executable_bytes,
            executable_size,
            context->state.request_bytes,
            request_size,
            &values
        ) == MICROS_VM_CORE_ERROR_EXECUTABLE
        && context->state.phase == MICROS_VM_LOADER_IDLE
        && vm_core_test_bytes_are_zero(
            context->state.executable_bytes,
            sizeof(context->state.executable_bytes)
        )
        && vm_core_test_bytes_are_zero(
            context->state.request_bytes,
            sizeof(context->state.request_bytes)
        )
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );

    context->boot_info.mappings[0].physical_address += MICROS_FRAME_SIZE;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );
    context->boot_info.mappings[0].physical_address -= MICROS_FRAME_SIZE;
    EXPECT_TRUE(micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK);
    context->state.initial_free_bitmap[0] ^= UINT64_C(1) << TEST_FIRST_FREE_FRAME;
    EXPECT_TRUE(
        micros_vm_state_validate(&context->state)
            == MICROS_VM_CORE_ERROR_INVARIANT
    );
    return true;
}
