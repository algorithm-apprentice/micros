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

bool vm_handoff_test_test_prepares_matches_and_commits(void)
{
    const struct micros_process_handle process = {2, 7};
    const struct micros_thread_handle thread = {4, 9};
    struct micros_vm_boot_summary summary = vm_handoff_test_valid_summary();
    struct micros_syscall_arguments arguments = vm_handoff_test_valid_arguments();
    struct micros_vm_handoff_request request;
    struct micros_vm_handoff_state state;

    memset(&state, 0, sizeof(state));
    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(&state,
            2,
            UINT32_C(0x00007002),
            102,
            2,
            process,
            thread,
            UINT64_C(0x80001000),
            UINT64_C(0x40003000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary) == MICROS_VM_HANDOFF_OK
        && state.phase == MICROS_VM_HANDOFF_PHASE_PREPARED
        && micros_vm_handoff_state_validate(&state)
            == MICROS_VM_HANDOFF_OK
        && micros_vm_handoff_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && micros_vm_handoff_summary_matches(&state, &request)
    );
    ++request.version;
    EXPECT_TRUE(!micros_vm_handoff_summary_matches(&state, &request));
    --request.version;
    ++request.managed_range_count;
    EXPECT_TRUE(!micros_vm_handoff_summary_matches(&state, &request));
    --request.managed_range_count;
    ++request.address_space_count;
    EXPECT_TRUE(!micros_vm_handoff_summary_matches(&state, &request));
    --request.address_space_count;
    ++request.managed_frame_count;
    EXPECT_TRUE(!micros_vm_handoff_summary_matches(&state, &request));
    --request.managed_frame_count;
    ++request.free_frame_count;
    EXPECT_TRUE(!micros_vm_handoff_summary_matches(&state, &request));
    --request.free_frame_count;
    ++request.mapping_count;
    EXPECT_TRUE(!micros_vm_handoff_summary_matches(&state, &request));
    --request.mapping_count;
    request.digest ^= 1;
    EXPECT_TRUE(!micros_vm_handoff_summary_matches(&state, &request));
    request.digest ^= 1;
    micros_vm_handoff_commit_prevalidated(&state);
    EXPECT_TRUE(
        state.phase == MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        && micros_vm_handoff_state_validate(&state)
            == MICROS_VM_HANDOFF_OK
        && micros_vm_handoff_summary_matches(&state, &request)
    );
    return true;
}

bool vm_handoff_test_test_rejects_invalid_state_without_mutation(void)
{
    const struct micros_process_handle process = {2, 7};
    const struct micros_thread_handle thread = {4, 9};
    struct micros_vm_boot_summary summary = vm_handoff_test_valid_summary();
    struct micros_vm_handoff_state state;
    struct micros_vm_handoff_state sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    state = sentinel;
    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(&state,
            2,
            UINT32_C(0x00007002),
            102,
            2,
            process,
            thread,
            UINT64_C(0x80001000),
            UINT64_C(0x40003000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary) == MICROS_VM_HANDOFF_ERROR_STORAGE
        && memcmp(&state, &sentinel, sizeof(state)) == 0
    );

    memset(&state, 0, sizeof(state));
    summary.vm_self_wired_frame_count = 0;
    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(&state,
            2,
            UINT32_C(0x00007002),
            102,
            2,
            process,
            thread,
            UINT64_C(0x80001000),
            UINT64_C(0x40003000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary) == MICROS_VM_HANDOFF_ERROR_IDENTITY
        && memcmp(&state,
            &(struct micros_vm_handoff_state){0},
            sizeof(state)) == 0
    );
    return true;
}

bool vm_handoff_test_test_classifies_vm_self_fault_phase(void)
{
    EXPECT_TRUE(
        micros_vm_self_fault_classify(MICROS_BOOTSTRAP_PHASE_RUNNING,
            MICROS_BOOTSTRAP_SERVICE_STARTING) == MICROS_VM_SELF_FAULT_RUNNING_STARTING
        && micros_vm_self_fault_classify(MICROS_BOOTSTRAP_PHASE_RUNNING,
            MICROS_BOOTSTRAP_SERVICE_READY) == MICROS_VM_SELF_FAULT_RUNNING_READY
        && micros_vm_self_fault_classify(MICROS_BOOTSTRAP_PHASE_SEALED,
            MICROS_BOOTSTRAP_SERVICE_READY) == MICROS_VM_SELF_FAULT_SEALED
        && micros_vm_self_fault_classify(MICROS_BOOTSTRAP_PHASE_RUNNING,
            MICROS_BOOTSTRAP_SERVICE_PREPARED) == MICROS_VM_SELF_FAULT_INVALID
        && micros_vm_self_fault_classify(MICROS_BOOTSTRAP_PHASE_FAILED,
            MICROS_BOOTSTRAP_SERVICE_READY) == MICROS_VM_SELF_FAULT_INVALID
    );
    return true;
}
