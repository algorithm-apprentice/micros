#include "vm_handoff_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

bool vm_handoff_test_test_load_transaction_lifecycle(void)
{
    struct micros_vm_handoff_state state = vm_handoff_test_handed_off_state();
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_vm_load_abort_action abort;
    struct micros_vm_exit_release_action release;
    struct micros_process_handle process = vm_handoff_test_dynamic_process();
    uint64_t load_token;
    uint64_t mapping_generation;

    EXPECT_TRUE(vm_handoff_test_begin_and_fill_load(&state, UINT64_C(0x44)));
    load_token = state.load.load_token;
    request = vm_handoff_test_valid_freeze_plan(&state);
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(&state, &request, &freeze)
            == MICROS_VM_HANDOFF_OK
        && freeze.mapping_generation != 0
        && freeze.result.version == MICROS_VM_LOAD_FREEZE_RESULT_VERSION
        && freeze.result.size == MICROS_VM_LOAD_FREEZE_RESULT_SIZE
        && freeze.result.mapping_generation == freeze.mapping_generation
        && freeze.action.process.slot == process.slot
        && freeze.action.entry == MICROS_APPLICATION_ENTRY
        && freeze.action.stack_pointer == request.stack_pointer
        && freeze.action.entry_count == 4
        && request.page_plan_digest == UINT64_C(0x8bc20d9c24364308)
    );
    micros_vm_load_freeze_commit_prevalidated(&state, &freeze);
    mapping_generation = freeze.mapping_generation;
    EXPECT_TRUE(
        state.load.phase == MICROS_VM_LOAD_PHASE_FROZEN
        && state.mappings[process.slot].phase == MICROS_VM_KERNEL_MAPPING_FROZEN
        && micros_vm_load_commit_preflight(&state,
            load_token,
            mapping_generation) == MICROS_VM_HANDOFF_ERROR_STATE
        && micros_vm_load_mark_activated(&state,
            process,
            UINT64_C(0x44),
            load_token,
            mapping_generation) == MICROS_VM_HANDOFF_ERROR_STATE
        && micros_vm_load_mark_prepared(&state,
            process,
            UINT64_C(0x44),
            load_token,
            mapping_generation) == MICROS_VM_HANDOFF_OK
        && state.mappings[process.slot].phase
            == MICROS_VM_KERNEL_MAPPING_PREPARED
        && micros_vm_load_abort_preflight(&state,
            load_token,
            mapping_generation,
            &abort) == MICROS_VM_HANDOFF_ERROR_STATE
        && micros_vm_load_mark_unprepared(&state,
            process,
            UINT64_C(0x44),
            load_token,
            mapping_generation) == MICROS_VM_HANDOFF_OK
        && state.mappings[process.slot].phase
            == MICROS_VM_KERNEL_MAPPING_FROZEN
        && micros_vm_load_mark_prepared(&state,
            process,
            UINT64_C(0x44),
            load_token,
            mapping_generation) == MICROS_VM_HANDOFF_OK
        && micros_vm_load_mark_activated(&state,
            process,
            UINT64_C(0x44),
            load_token,
            mapping_generation) == MICROS_VM_HANDOFF_OK
        && micros_vm_load_commit_preflight(&state,
            load_token,
            mapping_generation) == MICROS_VM_HANDOFF_OK
    );
    micros_vm_load_commit_commit_prevalidated(&state);
    EXPECT_TRUE(
        state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
        && state.mappings[process.slot].phase == MICROS_VM_KERNEL_MAPPING_ACTIVE
        && micros_vm_load_mark_exit_detached(&state,
            process,
            UINT64_C(0x55),
            load_token,
            mapping_generation) == MICROS_VM_HANDOFF_OK
        && micros_vm_exit_release_preflight(&state,
            UINT64_C(0x55),
            load_token,
            mapping_generation,
            &release) == MICROS_VM_HANDOFF_OK
        && release.process.slot == process.slot
        && release.root_physical_address == request.table_frames[0]
        && release.entry_count == 4
    );
    micros_vm_exit_release_commit_prevalidated(&state, process);
    EXPECT_TRUE(
        state.mappings[process.slot].phase == MICROS_VM_KERNEL_MAPPING_UNUSED
        && micros_vm_handoff_state_validate(&state) == MICROS_VM_HANDOFF_OK
    );
    return true;
}

bool vm_handoff_test_test_load_abort_before_and_after_freeze(void)
{
    struct micros_vm_handoff_state state = vm_handoff_test_handed_off_state();
    struct micros_vm_load_abort_action abort_action;
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_process_handle process = vm_handoff_test_dynamic_process();

    EXPECT_TRUE(vm_handoff_test_begin_and_fill_load(&state, UINT64_C(0x70)));
    EXPECT_TRUE(
        micros_vm_load_abort_preflight(&state,
            state.load.load_token,
            0,
            &abort_action) == MICROS_VM_HANDOFF_OK
        && !abort_action.frozen
        && abort_action.entry_count == 4
    );
    micros_vm_load_abort_commit_prevalidated(&state);
    EXPECT_TRUE(
        state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
        && state.mappings[process.slot].phase == MICROS_VM_KERNEL_MAPPING_UNUSED
    );

    EXPECT_TRUE(vm_handoff_test_begin_and_fill_load(&state, UINT64_C(0x71)));
    request = vm_handoff_test_valid_freeze_plan(&state);
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(&state, &request, &freeze)
            == MICROS_VM_HANDOFF_OK
    );
    micros_vm_load_freeze_commit_prevalidated(&state, &freeze);
    EXPECT_TRUE(
        micros_vm_load_abort_preflight(&state,
            state.load.load_token,
            freeze.mapping_generation,
            &abort_action) == MICROS_VM_HANDOFF_OK
        && abort_action.frozen
        && abort_action.root_physical_address == request.table_frames[0]
        && abort_action.entry_count == 4
    );
    micros_vm_load_abort_commit_prevalidated(&state);
    EXPECT_TRUE(
        state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
        && state.mappings[process.slot].phase == MICROS_VM_KERNEL_MAPPING_UNUSED
        && micros_vm_handoff_state_validate(&state) == MICROS_VM_HANDOFF_OK
    );
    return true;
}

bool vm_handoff_test_test_load_failures_preserve_state(void)
{
    struct micros_vm_handoff_state state = vm_handoff_test_handed_off_state();
    struct micros_vm_handoff_state snapshot;
    struct micros_vm_load_begin_plan begin;
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_vm_load_freeze_state_plan freeze_sentinel;
    struct micros_vm_scratch_map_request map_request;
    struct micros_vm_scratch_plan scratch;
    struct micros_process_handle process = vm_handoff_test_dynamic_process();

    snapshot = state;
    EXPECT_TRUE(
        micros_vm_load_begin_preflight(&state,
            UINT64_C(0x80),
            process,
            false,
            &begin) == MICROS_VM_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(vm_handoff_test_begin_and_fill_load(&state, UINT64_C(0x81)));
    snapshot = state;
    map_request = (struct micros_vm_scratch_map_request){
        .load_token = state.load.load_token,
        .virtual_address = MICROS_USER_VIRTUAL_BASE,
        .physical_address = UINT64_C(0x00900000),
        .permissions =
            MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ,
    };
    EXPECT_TRUE(
        micros_vm_scratch_map_preflight(&state, &map_request, &scratch)
            == MICROS_VM_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    request = vm_handoff_test_valid_freeze_plan(&state);
    request.page_plan_digest ^= 1;
    memset(&freeze_sentinel, 0xa5, sizeof(freeze_sentinel));
    freeze = freeze_sentinel;
    snapshot = state;
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(&state, &request, &freeze)
            == MICROS_VM_HANDOFF_ERROR_SUMMARY
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && memcmp(&freeze, &freeze_sentinel, sizeof(freeze)) == 0
    );
    return true;
}

bool vm_handoff_test_test_freeze_stack_boundaries(void)
{
    struct micros_vm_handoff_state state = vm_handoff_test_handed_off_state();
    struct micros_vm_handoff_state snapshot;
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_vm_load_freeze_state_plan sentinel;

    EXPECT_TRUE(vm_handoff_test_begin_and_fill_load(&state, UINT64_C(0x90)));
    request = vm_handoff_test_valid_freeze_plan(&state);
    request.stack_pointer =
        MICROS_APPLICATION_STACK_TOP - MICROS_APPLICATION_STACK_OCCUPIED_MAX;
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(&state, &request, &freeze)
            == MICROS_VM_HANDOFF_OK
    );

    request.stack_pointer -= 16;
    memset(&sentinel, 0xa5, sizeof(sentinel));
    freeze = sentinel;
    snapshot = state;
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(&state, &request, &freeze)
            == MICROS_VM_HANDOFF_ERROR_ARGUMENT
        && memcmp(&state, &snapshot, sizeof(state)) == 0
        && memcmp(&freeze, &sentinel, sizeof(freeze)) == 0
    );
    return true;
}

bool vm_handoff_test_test_rejects_oversized_later_mapping_record(void)
{
    struct micros_vm_handoff_state state = vm_handoff_test_handed_off_state();
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_process_handle process = vm_handoff_test_dynamic_process();
    struct micros_vm_kernel_mapping_record *record;
    size_t index;

    EXPECT_TRUE(vm_handoff_test_begin_and_fill_load(&state, UINT64_C(0x91)));
    request = vm_handoff_test_valid_freeze_plan(&state);
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(&state, &request, &freeze)
            == MICROS_VM_HANDOFF_OK
    );
    micros_vm_load_freeze_commit_prevalidated(&state, &freeze);
    EXPECT_TRUE(
        micros_vm_load_mark_prepared(
            &state,
            process,
            UINT64_C(0x91),
            state.load.load_token,
            freeze.mapping_generation) == MICROS_VM_HANDOFF_OK
        && micros_vm_load_mark_activated(
            &state,
            process,
            UINT64_C(0x91),
            state.load.load_token,
            freeze.mapping_generation) == MICROS_VM_HANDOFF_OK
    );
    micros_vm_load_commit_commit_prevalidated(&state);

    state.mappings[8] = state.mappings[process.slot];
    record = &state.mappings[8];
    record->process.slot = 8;
    record->process.generation = 10;
    record->load_token = state.last_load_token + 1;
    record->mapping_generation = state.last_mapping_generation + 1;
    for (index = 0; index < record->entry_count; ++index) {
        record->entries[index].physical_address += UINT64_C(0x00100000);
    }
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        record->table_frames[index] += UINT64_C(0x00100000);
    }
    record->root_physical_address = record->table_frames[0];
    record->entry_count = MICROS_APPLICATION_RESIDENT_PAGE_MAX + 1;
    state.last_load_token = record->load_token;
    state.last_mapping_generation = record->mapping_generation;

    EXPECT_TRUE(
        micros_vm_handoff_state_validate(&state)
            == MICROS_VM_HANDOFF_ERROR_INVARIANT
    );
    return true;
}
