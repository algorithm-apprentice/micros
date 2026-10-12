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

static bool prepare_and_bind_load(
    struct vm_core_test_context *context
)
{
    struct micros_vm_freeze_plan freeze_plan;
    const struct micros_vm_mapping_record *record;
    size_t index;

    EXPECT_TRUE(
        vm_core_test_initialize_boot_info(context, TEST_MANAGED_FRAME_COUNT)
        && vm_core_test_initialize_state(context)
            == MICROS_VM_CORE_OK
        && vm_core_test_prepare_minimal_load(context)
        && context->state.phase == MICROS_VM_LOADER_INPUT_COPIED
        && context->state.transaction.page_count == 4
    );
    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        EXPECT_TRUE(
            context->state.transaction.table_frames[index]
                == vm_core_test_frame_address(TEST_FIRST_FREE_FRAME + index)
            && context->boot_info.frame_states[TEST_FIRST_FREE_FRAME + index]
                == MICROS_VM_LOCAL_FRAME_FREE
        );
    }
    for (index = 0; index < context->state.transaction.page_count; ++index) {
        EXPECT_TRUE(
            context->state.transaction.data_frames[index]
                == vm_core_test_frame_address(
                    TEST_FIRST_FREE_FRAME
                        + MICROS_VM_PAGE_TABLE_FRAME_COUNT
                        + index
                )
        );
    }
    EXPECT_TRUE(
        micros_vm_load_bind(
            &context->state,
            (struct micros_process_handle){2, 7},
            UINT64_C(0x2222)
        ) == MICROS_VM_CORE_OK
        && context->state.phase == MICROS_VM_LOADER_LOADING
    );
    record = &context->state.mapping_records[2];
    EXPECT_TRUE(
        record->load_token == UINT64_C(0x2222)
        && record->semantic_pid == 7
        && record->process_generation == 7
        && record->process_slot == 2
        && record->phase == MICROS_VM_MAPPING_RECORD_LOADING
        && record->entry_count == 8
    );
    memset(&freeze_plan, 0xa5, sizeof(freeze_plan));
    {
        struct micros_vm_freeze_plan sentinel = freeze_plan;

        EXPECT_TRUE(
            micros_vm_load_freeze_plan(&context->state, &freeze_plan)
                == MICROS_VM_CORE_ERROR_STATE
            && memcmp(
                &freeze_plan,
                &sentinel,
                sizeof(freeze_plan)
            ) == 0
        );
    }
    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        EXPECT_TRUE(
            context->boot_info.frame_states[TEST_FIRST_FREE_FRAME + index]
                == MICROS_VM_LOCAL_FRAME_LOAD_RESERVED
        );
    }
    return true;
}

static bool fill_and_commit_pages(
    struct vm_core_test_context *context
)
{
    struct micros_vm_load_page_action action;
    uint32_t page_index;

    for (page_index = 0; page_index < 4; ++page_index) {
        memset(&action, 0xa5, sizeof(action));
        EXPECT_TRUE(
            micros_vm_load_next_page(&context->state, &action)
                == MICROS_VM_CORE_OK
            && action.page_index == page_index
            && action.physical_address
                == context->state.transaction.data_frames[page_index]
            && action.kind
                == (
                    page_index < 3
                        ? MICROS_VM_LOAD_PAGE_IMAGE
                        : MICROS_VM_LOAD_PAGE_STACK
                )
            && micros_vm_load_scratch_mapped(&context->state, page_index)
                == MICROS_VM_CORE_OK
            && context->boot_info.frame_states[
                TEST_FIRST_FREE_FRAME
                    + MICROS_VM_PAGE_TABLE_FRAME_COUNT
                    + page_index
            ] == MICROS_VM_LOCAL_FRAME_TRANSFERABLE
            && micros_vm_load_scratch_committed(&context->state, page_index)
                == MICROS_VM_CORE_ERROR_STATE
            && context->state.transaction.completed_page_count == page_index
            && context->state.transaction.scratch_page
                == page_index + 1
            && context->state.transaction.scratch_filled == 0
            && micros_vm_load_fill_scratch(
                &context->state,
                page_index,
                context->scratch_page,
                sizeof(context->scratch_page)
            ) == MICROS_VM_CORE_OK
            && context->state.transaction.scratch_filled == 1
        );
        if (page_index == 0) {
            EXPECT_TRUE(memcmp(
                context->scratch_page,
                context->executable,
                sizeof(context->scratch_page)
            ) == 0);
        } else if (page_index == 2) {
            EXPECT_TRUE(
                memcmp(
                    context->scratch_page,
                    &context->executable[8192],
                    1024
                ) == 0
                && vm_core_test_bytes_are_zero(
                    &context->scratch_page[1024],
                    sizeof(context->scratch_page) - 1024
                )
            );
        } else if (page_index == 3) {
            EXPECT_TRUE(
                context->scratch_page[
                    context->state.transaction.stack_plan.argc_offset
                ] == 1
            );
        }
        EXPECT_TRUE(
            micros_vm_load_scratch_committed(&context->state, page_index)
                == MICROS_VM_CORE_OK
            && context->state.transaction.completed_page_count
                == page_index + 1
            && context->state.transaction.scratch_page == 0
            && context->state.transaction.scratch_filled == 0
        );
    }
    return true;
}

static bool freeze_load(
    struct vm_core_test_context *context
)
{
    struct micros_vm_freeze_plan freeze_plan;
    const struct micros_vm_mapping_record *record =
        &context->state.mapping_records[2];
    size_t index;

    EXPECT_TRUE(
        micros_vm_load_freeze_plan(&context->state, &freeze_plan)
            == MICROS_VM_CORE_OK
        && freeze_plan.load_token == UINT64_C(0x2222)
        && freeze_plan.entry == MICROS_APPLICATION_ENTRY
        && freeze_plan.stack_pointer
            == context->state.transaction.stack_plan.stack_pointer
        && freeze_plan.image_page_count == 3
        && freeze_plan.data_page_count == 4
        && freeze_plan.page_plan_digest == vm_core_test_expected_page_digest(context)
        && micros_vm_load_frozen(&context->state, UINT64_C(0x3333))
            == MICROS_VM_CORE_OK
        && context->state.phase == MICROS_VM_LOADER_FROZEN
        && record->phase == MICROS_VM_MAPPING_RECORD_FROZEN
        && record->mapping_generation == UINT64_C(0x3333)
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    for (index = 0; index < MICROS_VM_PAGE_TABLE_FRAME_COUNT; ++index) {
        EXPECT_TRUE(
            context->boot_info.frame_states[TEST_FIRST_FREE_FRAME + index]
                == MICROS_VM_LOCAL_FRAME_PROCESS_PAGE_TABLE
        );
    }
    for (index = 0; index < 4; ++index) {
        EXPECT_TRUE(
            context->boot_info.frame_states[
                TEST_FIRST_FREE_FRAME
                    + MICROS_VM_PAGE_TABLE_FRAME_COUNT
                    + index
            ] == MICROS_VM_LOCAL_FRAME_PROCESS_WIRED
        );
    }
    return true;
}

static bool commit_and_preserve_active_load(
    struct vm_core_test_context *context
)
{
    const struct micros_vm_mapping_record *record =
        &context->state.mapping_records[2];

    EXPECT_TRUE(
        micros_vm_load_committed(
            &context->state,
            UINT64_C(0x2222),
            UINT64_C(0x3333),
            7
        ) == MICROS_VM_CORE_OK
        && context->state.phase == MICROS_VM_LOADER_IDLE
        && record->phase == MICROS_VM_MAPPING_RECORD_ACTIVE
        && vm_core_test_bytes_are_zero(
            &context->state.transaction,
            sizeof(context->state.transaction)
        )
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
    {
        struct micros_application_stack_values values =
            vm_core_test_stack_values(8);
        size_t executable_size = vm_core_test_build_elf(context, 1, 1, 1);
        size_t request_size = vm_core_test_build_request(context);

        EXPECT_TRUE(
            micros_vm_load_prepare(
                &context->state,
                UINT64_C(0x4444),
                values.pid,
                context->executable,
                executable_size,
                context->request,
                request_size,
                &values
            ) == MICROS_VM_CORE_OK
            && context->state.transaction.table_frames[0]
                == vm_core_test_frame_address(14)
            && micros_vm_load_bind(
                &context->state,
                (struct micros_process_handle){3, 9},
                UINT64_C(0x5555)
            ) == MICROS_VM_CORE_OK
            && micros_vm_load_aborted(
                &context->state,
                UINT64_C(0x5555),
                0
            ) == MICROS_VM_CORE_OK
            && context->state.mapping_records[2].phase
                == MICROS_VM_MAPPING_RECORD_ACTIVE
            && vm_core_test_bytes_are_zero(
                &context->state.mapping_records[3],
                sizeof(context->state.mapping_records[3])
            )
            && micros_vm_state_validate(&context->state)
                == MICROS_VM_CORE_OK
        );
    }
    return true;
}

static bool release_active_load(
    struct vm_core_test_context *context
)
{
    struct micros_vm_exit_action exit_action;
    const struct micros_vm_mapping_record *record =
        &context->state.mapping_records[2];
    size_t index;

    memset(&exit_action, 0xa5, sizeof(exit_action));
    {
        struct micros_vm_exit_action sentinel = exit_action;

        EXPECT_TRUE(
            micros_vm_exit_release_begin(
                &context->state,
                UINT64_C(0x2223),
                UINT64_C(0x3333),
                7,
                &exit_action
            ) == MICROS_VM_CORE_ERROR_STATE
            && memcmp(
                &exit_action,
                &sentinel,
                sizeof(exit_action)
            ) == 0
            && context->state.mapping_records[2].phase
                == MICROS_VM_MAPPING_RECORD_ACTIVE
        );
    }
    EXPECT_TRUE(
        micros_vm_exit_release_begin(
            &context->state,
            UINT64_C(0x2222),
            UINT64_C(0x3333),
            7,
            &exit_action
        ) == MICROS_VM_CORE_OK
        && exit_action.process_slot == 2
        && exit_action.process_generation == 7
        && context->state.phase == MICROS_VM_LOADER_EXIT_RELEASING
        && record->phase
            == MICROS_VM_MAPPING_RECORD_EXIT_RELEASING
        && micros_vm_exit_release_complete(&context->state, &exit_action)
            == MICROS_VM_CORE_OK
        && context->state.phase == MICROS_VM_LOADER_IDLE
        && vm_core_test_bytes_are_zero(record, sizeof(*record))
        && micros_vm_state_validate(&context->state) == MICROS_VM_CORE_OK
    );
    for (index = TEST_FIRST_FREE_FRAME; index < 14; ++index) {
        EXPECT_TRUE(
            context->boot_info.frame_states[index] == MICROS_VM_LOCAL_FRAME_FREE
        );
    }
    return true;
}

bool test_load_lifecycle(
    struct vm_core_test_context *context
)
{
    return (
        prepare_and_bind_load(context)
        && fill_and_commit_pages(context)
        && freeze_load(context)
        && commit_and_preserve_active_load(context)
        && release_active_load(context)
    );
}
