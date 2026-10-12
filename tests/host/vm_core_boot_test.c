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

bool test_record_abi(
    struct vm_core_test_context *context
)
{
    return (
        sizeof(struct micros_vm_mapping_entry) == 24
        && sizeof(struct micros_vm_mapping_record) == 512
        && sizeof(context->state.mapping_records) == 32768
        && sizeof(context->state.initial_free_bitmap) == 32768
        && sizeof(struct micros_vm_state)
            <= 50 * MICROS_APPLICATION_STACK_PAGE_SIZE
        && offsetof(struct micros_vm_mapping_record, entries) == 32
        && MICROS_VM_MAPPING_ENTRY_CAPACITY == 20
    );
}

bool test_boot_import(
    struct vm_core_test_context *context
)
{
    struct micros_vm_state sentinel;

    EXPECT_TRUE(vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT));
    memset(&context->state, 0xa5, sizeof(context->state));
    sentinel = context->state;
    context->boot_info.header.digest ^= 1;
    EXPECT_TRUE(
        vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_ERROR_BOOT
        && memcmp(&context->state, &sentinel, sizeof(context->state)) == 0
    );
    context->boot_info.header.digest ^= 1;
    EXPECT_TRUE(
        micros_vm_state_initialize(
            &context->state,
            &context->boot_info,
            TEST_VM_SERVICE_ID + 1,
            TEST_VM_ENDPOINT,
            TEST_BOOT_ADDRESS_SPACE_COUNT
        ) == MICROS_VM_CORE_ERROR_BOOT
        && memcmp(&context->state, &sentinel, sizeof(context->state)) == 0
        && micros_vm_state_initialize(
            &context->state,
            &context->boot_info,
            TEST_VM_SERVICE_ID,
            TEST_VM_ENDPOINT + 1,
            TEST_BOOT_ADDRESS_SPACE_COUNT
        ) == MICROS_VM_CORE_ERROR_BOOT
        && memcmp(&context->state, &sentinel, sizeof(context->state)) == 0
        && micros_vm_state_initialize(
            &context->state,
            &context->boot_info,
            TEST_VM_SERVICE_ID,
            TEST_VM_ENDPOINT,
            TEST_BOOT_ADDRESS_SPACE_COUNT + 1
        ) == MICROS_VM_CORE_ERROR_BOOT
        && memcmp(&context->state, &sentinel, sizeof(context->state)) == 0
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
        && context->state.phase == MICROS_VM_LOADER_IDLE
        && context->state.boot_info == &context->boot_info
        && context->state.boot_summary.managed_frame_count
            == TEST_MANAGED_FRAME_COUNT
        && context->state.initial_free_bitmap[0]
            == (
                (
                (UINT64_C(1) << TEST_MANAGED_FRAME_COUNT) - 1
                ) & ~(
                    (UINT64_C(1) << TEST_FIRST_FREE_FRAME) - 1
                )
            )
        && vm_core_test_bytes_are_zero(
            &context->state.initial_free_bitmap[1],
            sizeof(context->state.initial_free_bitmap)
                - sizeof(context->state.initial_free_bitmap[0])
        )
        && vm_core_test_bytes_are_zero(
            context->state.mapping_records,
            sizeof(context->state.mapping_records)
        )
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    return true;
}
