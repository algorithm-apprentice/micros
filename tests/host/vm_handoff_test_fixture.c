#include "vm_handoff_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct micros_vm_boot_summary vm_handoff_test_valid_summary(void)
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

struct micros_syscall_arguments vm_handoff_test_valid_arguments(void)
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

struct micros_syscall_arguments vm_handoff_test_valid_tty_mapping_arguments(void)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_VM_HANDOFF_MAP_TTY_UART,
        .a1 = MICROS_TTY_MAPPING_VERSION,
        .a2 = MICROS_TTY_SERVICE_ID,
        .a3 = UINT32_C(0x00007003),
        .a4 = MICROS_TTY_UART_VIRTUAL_BASE,
        .a5 = MICROS_TTY_UART_PHYSICAL_BASE,
        .a6 = (
            (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
        ) | MICROS_TTY_UART_MAPPED_LENGTH,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
}

struct micros_process_handle vm_handoff_test_dynamic_process(void)
{
    return (struct micros_process_handle){
        .slot = 7,
        .generation = 9,
    };
}

struct micros_vm_handoff_state vm_handoff_test_handed_off_state(void)
{
    const struct micros_process_handle process = {2, 7};
    const struct micros_thread_handle thread = {4, 9};
    struct micros_vm_boot_summary summary = vm_handoff_test_valid_summary();
    struct micros_vm_handoff_state state;

    memset(&state, 0, sizeof(state));
    if (
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
        ) != MICROS_VM_HANDOFF_OK
    ) {
        abort();
    }
    micros_vm_handoff_commit_prevalidated(&state);
    return state;
}

struct micros_vm_load_freeze_plan vm_handoff_test_valid_freeze_plan(
    const struct micros_vm_handoff_state *state
)
{
    const struct micros_vm_kernel_mapping_record *record =
        &state->mappings[state->load.process.slot];
    struct micros_vm_load_freeze_plan plan = {
        .version = MICROS_VM_LOAD_FREEZE_PLAN_VERSION,
        .size = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .load_token = state->load.load_token,
        .table_frames = {
            UINT64_C(0x00810000),
            UINT64_C(0x00811000),
            UINT64_C(0x00812000),
            UINT64_C(0x00813000),
        },
        .entry = MICROS_USER_VIRTUAL_BASE,
        .stack_pointer = MICROS_USER_VIRTUAL_END - 16,
        .image_page_count = 3,
        .data_page_count = 4,
    };

    plan.page_plan_digest = micros_vm_load_page_plan_digest(
        plan.image_page_count,
        plan.data_page_count,
        record->entries
    );
    return plan;
}

bool vm_handoff_test_decode_preserves_output(
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

bool vm_handoff_test_tty_mapping_decode_preserves_output(
    struct micros_syscall_arguments arguments
)
{
    struct micros_vm_tty_mapping_request request;
    struct micros_vm_tty_mapping_request sentinel;

    memset(&sentinel, 0xa5, sizeof(sentinel));
    request = sentinel;
    return (
        micros_vm_handoff_decode_tty_mapping(&arguments, &request)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&request, &sentinel, sizeof(request)) == 0
    );
}

bool vm_handoff_test_append_page(
    struct micros_vm_handoff_state *state,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions
)
{
    struct micros_vm_scratch_map_request map_request = {
        .load_token = state->load.load_token,
        .virtual_address = virtual_address,
        .physical_address = physical_address,
        .permissions = permissions,
    };
    struct micros_vm_scratch_commit_request commit_request = {
        .load_token = state->load.load_token,
        .virtual_address = virtual_address,
        .physical_address = physical_address,
    };
    struct micros_vm_scratch_plan plan;

    if (
        micros_vm_scratch_map_preflight(state, &map_request, &plan)
        != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    micros_vm_scratch_map_commit_prevalidated(state, &plan);
    if (
        micros_vm_scratch_commit_preflight(state, &commit_request, &plan)
        != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    micros_vm_scratch_commit_commit_prevalidated(state, &plan);
    return true;
}

bool vm_handoff_test_begin_and_fill_load(
    struct micros_vm_handoff_state *state,
    uint64_t reservation_token
)
{
    struct micros_vm_load_begin_plan begin;
    struct micros_process_handle process = vm_handoff_test_dynamic_process();

    if (
        micros_vm_load_begin_preflight(
            state,
            reservation_token,
            process,
            true,
            &begin
        ) != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    micros_vm_load_begin_commit_prevalidated(state, &begin);
    return (
        begin.result.version == MICROS_VM_LOAD_BEGIN_RESULT_VERSION
        && begin.result.size == MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        && begin.result.load_token != 0
        && begin.result.process_slot == process.slot
        && begin.result.process_generation == process.generation
        && vm_handoff_test_append_page(
            state,
            MICROS_USER_VIRTUAL_BASE,
            UINT64_C(0x00800000),
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
        )
        && vm_handoff_test_append_page(
            state,
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            UINT64_C(0x00801000),
            MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
        )
        && vm_handoff_test_append_page(
            state,
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000),
            UINT64_C(0x00802000),
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
        && vm_handoff_test_append_page(
            state,
            MICROS_USER_VIRTUAL_END - UINT64_C(0x1000),
            UINT64_C(0x00803000),
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
    );
}
