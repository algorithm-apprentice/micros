#include "kernel/vm_handoff_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

bool micros_vm_handoff_test_run(void);

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

static struct micros_vm_boot_summary valid_summary(void)
{
    return (struct micros_vm_boot_summary){
        .version = MICROS_VM_BOOT_INFO_VERSION,
        .managed_range_count = 2,
        .address_space_count = 3,
        .mapping_count = 97,
        .managed_frame_count = 4096,
        .free_frame_count = 3000,
        .vm_self_wired_frame_count = 91,
        .digest = UINT64_C(0x0123456789abcdef),
    };
}

static struct micros_syscall_arguments valid_arguments(void)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_VM_HANDOFF_READY,
        .a1 = MICROS_VM_BOOT_INFO_VERSION,
        .a2 = (UINT64_C(3) << 16) | 2,
        .a3 = 4096,
        .a4 = 3000,
        .a5 = 97,
        .a6 = UINT64_C(0x0123456789abcdef),
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
}

static bool decode_preserves_output(
    struct micros_syscall_arguments arguments
)
{
    struct micros_vm_handoff_request request;
    struct micros_vm_handoff_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_vm_handoff_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

static bool test_decodes_exact_shape(void)
{
    struct micros_syscall_arguments arguments = valid_arguments();
    struct micros_vm_handoff_request request;
    size_t index;

    EXPECT_TRUE(
        micros_vm_handoff_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_VM_HANDOFF_READY
        && request.version == MICROS_VM_BOOT_INFO_VERSION
        && request.managed_range_count == 2
        && request.address_space_count == 3
        && request.managed_frame_count == 4096
        && request.free_frame_count == 3000
        && request.mapping_count == 97
        && request.digest == UINT64_C(0x0123456789abcdef)
    );
    arguments.a0 = 2;
    EXPECT_TRUE(decode_preserves_output(arguments));
    arguments = valid_arguments();
    arguments.a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL;
    EXPECT_TRUE(decode_preserves_output(arguments));
    for (index = 0; index <= 5; ++index) {
        arguments = valid_arguments();
        switch (index) {
        case 0:
            arguments.a0 |= UINT64_C(1) << 32;
            break;
        case 1:
            arguments.a1 |= UINT64_C(1) << 32;
            break;
        case 2:
            arguments.a2 |= UINT64_C(1) << 32;
            break;
        case 3:
            arguments.a3 |= UINT64_C(1) << 32;
            break;
        case 4:
            arguments.a4 |= UINT64_C(1) << 32;
            break;
        default:
            arguments.a5 |= UINT64_C(1) << 32;
            break;
        }
        EXPECT_TRUE(decode_preserves_output(arguments));
    }
    return true;
}

static bool test_prepares_matches_and_commits(void)
{
    const struct micros_process_handle process = {2, 7};
    const struct micros_thread_handle thread = {4, 9};
    struct micros_vm_boot_summary summary = valid_summary();
    struct micros_syscall_arguments arguments = valid_arguments();
    struct micros_vm_handoff_request request;
    struct micros_vm_handoff_state state;

    memset(&state, 0, sizeof(state));
    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(
            &state,
            2,
            UINT32_C(0x00007002),
            102,
            2,
            process,
            thread,
            UINT64_C(0x80001000),
            UINT64_C(0x40003000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary
        ) == MICROS_VM_HANDOFF_OK
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

static bool test_rejects_invalid_state_without_mutation(void)
{
    const struct micros_process_handle process = {2, 7};
    const struct micros_thread_handle thread = {4, 9};
    struct micros_vm_boot_summary summary = valid_summary();
    struct micros_vm_handoff_state state;
    struct micros_vm_handoff_state sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    state = sentinel;
    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(
            &state,
            2,
            UINT32_C(0x00007002),
            102,
            2,
            process,
            thread,
            UINT64_C(0x80001000),
            UINT64_C(0x40003000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary
        ) == MICROS_VM_HANDOFF_ERROR_STORAGE
        && memcmp(&state, &sentinel, sizeof(state)) == 0
    );

    memset(&state, 0, sizeof(state));
    summary.vm_self_wired_frame_count = 0;
    EXPECT_TRUE(
        micros_vm_handoff_state_prepare(
            &state,
            2,
            UINT32_C(0x00007002),
            102,
            2,
            process,
            thread,
            UINT64_C(0x80001000),
            UINT64_C(0x40003000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary
        ) == MICROS_VM_HANDOFF_ERROR_IDENTITY
        && memcmp(
            &state,
            &(struct micros_vm_handoff_state){0},
            sizeof(state)
        ) == 0
    );
    return true;
}

bool micros_vm_handoff_test_run(void)
{
    return (
        test_decodes_exact_shape()
        && test_prepares_matches_and_commits()
        && test_rejects_invalid_state_without_mutation()
    );
}
