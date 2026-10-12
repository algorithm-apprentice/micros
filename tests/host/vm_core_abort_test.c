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

bool test_abort_paths(
    struct vm_core_test_context *context
)
{
    struct micros_vm_load_page_action action;
    size_t index;

    EXPECT_TRUE(
        vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT)
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
        && vm_core_test_prepare_minimal_load(context)
        && micros_vm_load_aborted(&context->state, 0, 0)
            == MICROS_VM_CORE_OK
        && context->state.phase == MICROS_VM_LOADER_IDLE
        && vm_core_test_bytes_are_zero(
            context->state.mapping_records,
            sizeof(context->state.mapping_records)
        )
        && vm_core_test_prepare_minimal_load(context)
        && micros_vm_load_bind(
            &context->state,
            (struct micros_process_handle){2, 7},
            UINT64_C(0x2222)
        ) == MICROS_VM_CORE_OK
        && micros_vm_load_next_page(&context->state, &action)
            == MICROS_VM_CORE_OK
        && micros_vm_load_scratch_mapped(&context->state, 0)
            == MICROS_VM_CORE_OK
        && micros_vm_load_fill_scratch(
            &context->state,
            0,
            context->scratch_page,
            sizeof(context->scratch_page)
        ) == MICROS_VM_CORE_OK
        && micros_vm_load_scratch_committed(&context->state, 0)
            == MICROS_VM_CORE_OK
        && micros_vm_load_next_page(&context->state, &action)
            == MICROS_VM_CORE_OK
        && micros_vm_load_scratch_mapped(&context->state, 1)
            == MICROS_VM_CORE_OK
        && micros_vm_load_aborted(
            &context->state,
            UINT64_C(0x2222),
            0
        ) == MICROS_VM_CORE_OK
        && context->state.phase == MICROS_VM_LOADER_IDLE
        && vm_core_test_bytes_are_zero(
            &context->state.mapping_records[2],
            sizeof(context->state.mapping_records[2])
        )
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    for (index = TEST_FIRST_FREE_FRAME; index < 14; ++index) {
        EXPECT_TRUE(
            context->boot_info.frame_states[index] == MICROS_VM_LOCAL_FRAME_FREE
        );
    }

    EXPECT_TRUE(
        vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT)
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
        && vm_core_test_run_to_frozen(context, UINT64_C(0x3333))
        && micros_vm_load_aborted(
            &context->state,
            UINT64_C(0x2223),
            UINT64_C(0x3333)
        ) == MICROS_VM_CORE_ERROR_STATE
        && context->state.phase == MICROS_VM_LOADER_FROZEN
        && micros_vm_load_aborted(
            &context->state,
            UINT64_C(0x2222),
            UINT64_C(0x3333)
        ) == MICROS_VM_CORE_OK
        && context->state.phase == MICROS_VM_LOADER_IDLE
        && vm_core_test_bytes_are_zero(
            &context->state.mapping_records[2],
            sizeof(context->state.mapping_records[2])
        )
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    for (index = TEST_FIRST_FREE_FRAME; index < 14; ++index) {
        EXPECT_TRUE(
            context->boot_info.frame_states[index] == MICROS_VM_LOCAL_FRAME_FREE
        );
    }
    return true;
}
