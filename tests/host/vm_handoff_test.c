#include "kernel/vm_handoff_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "micros/sv39.h"
#include "micros/user_address_space.h"

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

static struct micros_syscall_arguments valid_tty_mapping_arguments(void)
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

static struct micros_process_handle dynamic_process(void)
{
    return (struct micros_process_handle){
        .slot = 7,
        .generation = 9,
    };
}

static struct micros_vm_handoff_state handed_off_state(void)
{
    const struct micros_process_handle process = {2, 7};
    const struct micros_thread_handle thread = {4, 9};
    struct micros_vm_boot_summary summary = valid_summary();
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

static struct micros_vm_load_freeze_plan valid_freeze_plan(
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

static bool tty_mapping_decode_preserves_output(
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

static bool test_decodes_exact_shape(void)
{
    struct micros_syscall_arguments arguments = valid_arguments();
    struct micros_vm_handoff_request request;
    uint32_t command = 0;
    size_t index;

    EXPECT_TRUE(
        micros_vm_handoff_decode_command(&arguments, &command)
            == MICROS_SYSCALL_ABI_OK
        && command == MICROS_VM_HANDOFF_READY
        && micros_vm_handoff_decode(&arguments, &request)
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
    arguments.a0 = MICROS_VM_HANDOFF_MAP_TTY_UART;
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

static bool test_decodes_exact_tty_mapping(void)
{
    struct micros_syscall_arguments arguments =
        valid_tty_mapping_arguments();
    struct micros_vm_tty_mapping_request request;
    uint32_t command = 0;
    size_t index;

    EXPECT_TRUE(
        micros_vm_handoff_decode_command(&arguments, &command)
            == MICROS_SYSCALL_ABI_OK
        && command == MICROS_VM_HANDOFF_MAP_TTY_UART
        && micros_vm_handoff_decode_tty_mapping(&arguments,
            &request) == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_VM_HANDOFF_MAP_TTY_UART
        && request.version == MICROS_TTY_MAPPING_VERSION
        && request.service_id == MICROS_TTY_SERVICE_ID
        && request.endpoint == UINT32_C(0x00007003)
        && request.virtual_base == MICROS_TTY_UART_VIRTUAL_BASE
        && request.physical_base == MICROS_TTY_UART_PHYSICAL_BASE
        && request.irq_source == MICROS_TTY_UART_IRQ_SOURCE
        && request.mapped_length == MICROS_TTY_UART_MAPPED_LENGTH
        && micros_vm_tty_mapping_matches(&request,
            UINT32_C(0x00007003))
    );
    ++request.version;
    EXPECT_TRUE(
        !micros_vm_tty_mapping_matches(&request,
            UINT32_C(0x00007003))
    );
    --request.version;
    EXPECT_TRUE(
        !micros_vm_tty_mapping_matches(&request,
            UINT32_C(0x00008003))
    );

    arguments.a0 = UINT64_C(10);
    EXPECT_TRUE(tty_mapping_decode_preserves_output(arguments));
    command = UINT32_C(0xa5a5a5a5);
    EXPECT_TRUE(
        micros_vm_handoff_decode_command(&arguments, &command)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && command == UINT32_C(0xa5a5a5a5)
    );
    for (index = 0; index <= 3; ++index) {
        arguments = valid_tty_mapping_arguments();
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
        default:
            arguments.a3 |= UINT64_C(1) << 32;
            break;
        }
        EXPECT_TRUE(tty_mapping_decode_preserves_output(arguments));
    }
    arguments = valid_tty_mapping_arguments();
    arguments.a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL;
    EXPECT_TRUE(tty_mapping_decode_preserves_output(arguments));
    return true;
}

static bool test_decodes_load_commands(void)
{
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_VM_LOAD_BEGIN,
        .a1 = UINT64_C(0x1234),
        .a2 = UINT64_C(0x40001000),
        .a3 = MICROS_VM_LOAD_BEGIN_RESULT_SIZE,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    struct micros_vm_load_begin_request begin;
    struct micros_vm_scratch_map_request map;
    struct micros_vm_scratch_commit_request commit;
    struct micros_vm_load_freeze_request freeze;
    struct micros_vm_load_abort_request abort_request;
    struct micros_vm_load_commit_request load_commit;
    struct micros_vm_exit_release_request release;
    struct micros_vm_exit_release_request release_sentinel;
    uint32_t command = 0;

    EXPECT_TRUE(
        micros_vm_handoff_decode_command(&arguments, &command)
            == MICROS_SYSCALL_ABI_OK
        && command == MICROS_VM_LOAD_BEGIN
        && micros_vm_load_begin_decode(&arguments, &begin)
            == MICROS_SYSCALL_ABI_OK
        && begin.process_reservation_token == UINT64_C(0x1234)
        && begin.output_address == UINT64_C(0x40001000)
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_SCRATCH_MAP,
        .a1 = UINT64_C(0x10),
        .a2 = MICROS_USER_VIRTUAL_BASE,
        .a3 = UINT64_C(0x00800000),
        .a4 = MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_EXECUTE,
        .a5 = MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    EXPECT_TRUE(
        micros_vm_scratch_map_decode(&arguments, &map) == MICROS_SYSCALL_ABI_OK
        && map.load_token == UINT64_C(0x10)
        && map.virtual_address == MICROS_USER_VIRTUAL_BASE
        && map.physical_address == UINT64_C(0x00800000)
        && map.permissions
            == (MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE)
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_SCRATCH_COMMIT,
        .a1 = UINT64_C(0x10),
        .a2 = MICROS_USER_VIRTUAL_BASE,
        .a3 = UINT64_C(0x00800000),
        .a4 = MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    EXPECT_TRUE(
        micros_vm_scratch_commit_decode(&arguments, &commit)
            == MICROS_SYSCALL_ABI_OK
        && commit.load_token == UINT64_C(0x10)
        && commit.virtual_address == MICROS_USER_VIRTUAL_BASE
        && commit.physical_address == UINT64_C(0x00800000)
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_FREEZE,
        .a1 = UINT64_C(0x40002000),
        .a2 = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .a3 = UINT64_C(0x40003000),
        .a4 = MICROS_VM_LOAD_FREEZE_RESULT_SIZE,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    EXPECT_TRUE(
        micros_vm_load_freeze_decode(&arguments, &freeze)
            == MICROS_SYSCALL_ABI_OK
        && freeze.plan_address == UINT64_C(0x40002000)
        && freeze.output_address == UINT64_C(0x40003000)
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_ABORT,
        .a1 = UINT64_C(0x10),
        .a2 = UINT64_C(0x20),
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    EXPECT_TRUE(
        micros_vm_load_abort_decode(&arguments, &abort_request)
            == MICROS_SYSCALL_ABI_OK
        && abort_request.load_token == UINT64_C(0x10)
        && abort_request.mapping_generation == UINT64_C(0x20)
    );
    arguments.a0 = MICROS_VM_LOAD_COMMIT;
    EXPECT_TRUE(
        micros_vm_load_commit_decode(&arguments, &load_commit)
            == MICROS_SYSCALL_ABI_OK
        && load_commit.load_token == UINT64_C(0x10)
        && load_commit.mapping_generation == UINT64_C(0x20)
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_EXIT_RELEASE,
        .a1 = UINT64_C(0x30),
        .a2 = UINT64_C(0x10),
        .a3 = UINT64_C(0x20),
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    EXPECT_TRUE(
        micros_vm_exit_release_decode(&arguments, &release)
            == MICROS_SYSCALL_ABI_OK
        && release.exit_token == UINT64_C(0x30)
        && release.load_token == UINT64_C(0x10)
        && release.mapping_generation == UINT64_C(0x20)
    );

    arguments.a4 = 1;
    memset(&release_sentinel, 0xa5, sizeof(release_sentinel));
    release = release_sentinel;
    EXPECT_TRUE(
        micros_vm_exit_release_decode(&arguments, &release)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&release, &release_sentinel, sizeof(release)) == 0
    );
    return true;
}

static bool append_page(
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

static bool begin_and_fill_load(
    struct micros_vm_handoff_state *state,
    uint64_t reservation_token
)
{
    struct micros_vm_load_begin_plan begin;
    struct micros_process_handle process = dynamic_process();

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
        && append_page(
            state,
            MICROS_USER_VIRTUAL_BASE,
            UINT64_C(0x00800000),
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE
        )
        && append_page(
            state,
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x1000),
            UINT64_C(0x00801000),
            MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ
        )
        && append_page(
            state,
            MICROS_USER_VIRTUAL_BASE + UINT64_C(0x2000),
            UINT64_C(0x00802000),
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
        && append_page(
            state,
            MICROS_USER_VIRTUAL_END - UINT64_C(0x1000),
            UINT64_C(0x00803000),
            MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE
        )
    );
}

static bool test_load_transaction_lifecycle(void)
{
    struct micros_vm_handoff_state state = handed_off_state();
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_vm_exit_release_action release;
    struct micros_process_handle process = dynamic_process();
    uint64_t load_token;
    uint64_t mapping_generation;

    EXPECT_TRUE(begin_and_fill_load(&state, UINT64_C(0x44)));
    load_token = state.load.load_token;
    request = valid_freeze_plan(&state);
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

static bool test_load_abort_before_and_after_freeze(void)
{
    struct micros_vm_handoff_state state = handed_off_state();
    struct micros_vm_load_abort_action abort_action;
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_process_handle process = dynamic_process();

    EXPECT_TRUE(begin_and_fill_load(&state, UINT64_C(0x70)));
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

    EXPECT_TRUE(begin_and_fill_load(&state, UINT64_C(0x71)));
    request = valid_freeze_plan(&state);
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

static bool test_load_failures_preserve_state(void)
{
    struct micros_vm_handoff_state state = handed_off_state();
    struct micros_vm_handoff_state snapshot;
    struct micros_vm_load_begin_plan begin;
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_vm_load_freeze_state_plan freeze_sentinel;
    struct micros_vm_scratch_map_request map_request;
    struct micros_vm_scratch_plan scratch;
    struct micros_process_handle process = dynamic_process();

    snapshot = state;
    EXPECT_TRUE(
        micros_vm_load_begin_preflight(&state,
            UINT64_C(0x80),
            process,
            false,
            &begin) == MICROS_VM_HANDOFF_ERROR_STATE
        && memcmp(&state, &snapshot, sizeof(state)) == 0
    );
    EXPECT_TRUE(begin_and_fill_load(&state, UINT64_C(0x81)));
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
    request = valid_freeze_plan(&state);
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

static bool test_freeze_stack_boundaries(void)
{
    struct micros_vm_handoff_state state = handed_off_state();
    struct micros_vm_handoff_state snapshot;
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_vm_load_freeze_state_plan sentinel;

    EXPECT_TRUE(begin_and_fill_load(&state, UINT64_C(0x90)));
    request = valid_freeze_plan(&state);
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

static bool test_rejects_oversized_later_mapping_record(void)
{
    struct micros_vm_handoff_state state = handed_off_state();
    struct micros_vm_load_freeze_plan request;
    struct micros_vm_load_freeze_state_plan freeze;
    struct micros_process_handle process = dynamic_process();
    struct micros_vm_kernel_mapping_record *record;
    size_t index;

    EXPECT_TRUE(begin_and_fill_load(&state, UINT64_C(0x91)));
    request = valid_freeze_plan(&state);
    EXPECT_TRUE(
        micros_vm_load_freeze_preflight(&state, &request, &freeze)
            == MICROS_VM_HANDOFF_OK
    );
    micros_vm_load_freeze_commit_prevalidated(&state, &freeze);
    EXPECT_TRUE(
        micros_vm_load_mark_activated(
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

static bool test_classifies_vm_self_fault_phase(void)
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

bool micros_vm_handoff_test_run(void)
{
    return (
        test_decodes_exact_shape()
        && test_decodes_exact_tty_mapping()
        && test_decodes_load_commands()
        && test_prepares_matches_and_commits()
        && test_rejects_invalid_state_without_mutation()
        && test_load_transaction_lifecycle()
        && test_load_abort_before_and_after_freeze()
        && test_load_failures_preserve_state()
        && test_freeze_stack_boundaries()
        && test_rejects_oversized_later_mapping_record()
        && test_classifies_vm_self_fault_phase()
    );
}
