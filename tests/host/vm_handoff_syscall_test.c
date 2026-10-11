#include <setjmp.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/endpoint_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/pm_control_runtime.h"
#include "kernel/plic.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/user_address_space_internal.h"
#include "kernel/vm_handoff_runtime.h"
#include "kernel/vm_handoff_syscall.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_core.h"
#include "micros/panic.h"
#include "micros/user_address_space.h"

enum {
    TEST_LAUNCHER_SERVICE_ID = 1,
    TEST_VM_SERVICE_ID = 2,
    TEST_VM_PROFILE_ID = 2,
};

enum test_vm_operation {
    TEST_VM_OPERATION_NONE = 0,
    TEST_VM_OPERATION_SCRATCH_MAP,
    TEST_VM_OPERATION_SCRATCH_COMMIT,
    TEST_VM_OPERATION_FREEZE,
    TEST_VM_OPERATION_ABORT_LOADING,
    TEST_VM_OPERATION_ABORT_FROZEN,
    TEST_VM_OPERATION_EXIT_RELEASE,
};

static const struct micros_process_handle vm_process = {
    .slot = 1,
    .generation = 1,
};
static const struct micros_thread_handle vm_thread = {
    .slot = 1,
    .generation = 1,
};
static const struct micros_thread_handle launcher_thread = {
    .slot = 3,
    .generation = 1,
};
static const struct micros_process_handle foreign_process = {
    .slot = 3,
    .generation = 1,
};
static const struct micros_thread_handle foreign_thread = {
    .slot = 2,
    .generation = 1,
};
static const struct micros_process_handle tty_process = {
    .slot = MICROS_TTY_PROCESS_SLOT,
    .generation = 1,
};
static const struct micros_process_handle child_process = {
    .slot = 4,
    .generation = 7,
};
static const micros_endpoint_t launcher_endpoint = UINT32_C(0x00001003);
static const micros_endpoint_t vm_endpoint = UINT32_C(0x00001001);
static const micros_endpoint_t tty_endpoint = UINT32_C(0x00010004);

static _Alignas(MICROS_SV39_PAGE_SIZE)
    unsigned char user_memory[3 * MICROS_SV39_PAGE_SIZE];
static struct micros_kernel_objects objects;
static struct micros_endpoint_registry registry;
static struct micros_process vm_process_record;
static struct micros_process foreign_process_record;
static struct micros_process child_process_record;
static struct micros_thread vm_thread_record;
static struct micros_thread launcher_thread_record;
static struct micros_thread foreign_thread_record;
static struct micros_endpoint_record launcher_endpoint_record;
static struct micros_endpoint_record vm_endpoint_record;
static struct micros_bootstrap_control_state bootstrap;
static struct micros_vm_handoff_state vm_state;
static struct micros_pm_control_state pm_state;
static struct micros_tty_handoff_runtime_state tty_state;
static struct micros_frame_ownership ownership;
static jmp_buf panic_target;
static bool panic_armed;
static bool mapping_committed;
static bool notification_committed;
static bool notification_should_fail;
static bool order_violation;
static int operation_stage;
static enum test_vm_operation vm_operation;
static int vm_operation_stage;
static bool vm_order_violation;
static bool mutate_freeze_input;
static bool abort_has_scratch;
static enum micros_frame_ownership_error claim_preflight_error;
static enum micros_user_address_space_error scratch_mapping_error;
static enum micros_bootstrap_diagnostic_reason failure_reason;

static bool process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void reset_fixture(void)
{
    const struct micros_vm_boot_summary summary = {
        .version = MICROS_VM_BOOT_INFO_VERSION,
        .managed_range_count = 1,
        .address_space_count = 1,
        .mapping_count = 1,
        .managed_frame_count = 32,
        .free_frame_count = 16,
        .vm_self_wired_frame_count = 8,
        .digest = UINT64_C(0x1234),
    };

    bootstrap = (struct micros_bootstrap_control_state){
        .phase = MICROS_BOOTSTRAP_PHASE_RUNNING,
        .entry_count = 2,
        .plan = {
            .controller_service_id = TEST_LAUNCHER_SERVICE_ID,
            .console_service_id = MICROS_TTY_SERVICE_ID,
        },
        .transitions = {
            .phase = MICROS_BOOTSTRAP_PHASE_RUNNING,
            .entry_count = 2,
            .next_order_index = 1,
            .controller_service_id = TEST_LAUNCHER_SERVICE_ID,
            .entries = {
                {
                    .service_id = TEST_LAUNCHER_SERVICE_ID,
                    .state = MICROS_BOOTSTRAP_SERVICE_READY,
                    .endpoint_state =
                        MICROS_BOOTSTRAP_ENDPOINT_ACTIVE,
                    .profile_installed = true,
                    .scheduler_assigned = true,
                },
                {
                    .service_id = MICROS_TTY_SERVICE_ID,
                    .state = MICROS_BOOTSTRAP_SERVICE_PREPARED,
                    .endpoint_state =
                        MICROS_BOOTSTRAP_ENDPOINT_RESERVED,
                },
            },
            .ordered_service_ids = {
                TEST_LAUNCHER_SERVICE_ID,
                MICROS_TTY_SERVICE_ID,
            },
        },
        .bindings = {
            {
                .service_id = TEST_LAUNCHER_SERVICE_ID,
                .thread = launcher_thread,
                .endpoint = launcher_endpoint,
            },
        },
    };
    vm_state = (struct micros_vm_handoff_state){0};
    if (
        micros_vm_handoff_state_prepare(
            &vm_state,
            TEST_VM_SERVICE_ID,
            vm_endpoint,
            1,
            TEST_VM_PROFILE_ID,
            vm_process,
            vm_thread,
            UINT64_C(0x200000),
            UINT64_C(0x300000),
            MICROS_VM_BOOT_INFO_SIZE,
            &summary
        ) != MICROS_VM_HANDOFF_OK
    ) {
        abort();
    }
    micros_vm_handoff_commit_prevalidated(&vm_state);
    pm_state = (struct micros_pm_control_state){
        .service_id = 3,
        .endpoint = UINT32_C(0x00001002),
        .profile_id = MICROS_PRIVILEGE_PROFILE_PM,
        .process = {
            .slot = 2,
            .generation = 1,
        },
        .thread = {
            .slot = 5,
            .generation = 1,
        },
        .last_transaction = 1,
        .active_transaction = 1,
        .reserved_process = child_process,
    };
    tty_state = (struct micros_tty_handoff_runtime_state){
        .service_id = MICROS_TTY_SERVICE_ID,
        .endpoint = tty_endpoint,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .process = tty_process,
        .root_physical_address = UINT64_C(0x200000),
    };
    if (
        micros_tty_handoff_initialize(&tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_begin(&tty_state.handoff, 100, 100)
            != MICROS_TTY_HANDOFF_OK
    ) {
        abort();
    }
    ownership = (struct micros_frame_ownership){
        .phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
    };
    vm_process_record = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = vm_process.generation,
        .address_space_root = UINT64_C(0x200000),
        .primary_endpoint = vm_endpoint,
        .privilege_profile = TEST_VM_PROFILE_ID,
    };
    foreign_process_record = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = foreign_process.generation,
    };
    child_process_record = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = child_process.generation,
        .primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE,
    };
    vm_thread_record = (struct micros_thread){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = vm_thread.generation,
        .owner = vm_process,
    };
    launcher_thread_record = (struct micros_thread){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = launcher_thread.generation,
        .owner = vm_process,
    };
    foreign_thread_record = (struct micros_thread){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = foreign_thread.generation,
        .owner = foreign_process,
    };
    vm_endpoint_record = (struct micros_endpoint_record){
        .state = MICROS_ENDPOINT_STATE_ACTIVE,
        .owner = vm_process,
        .value = vm_endpoint,
    };
    launcher_endpoint_record = (struct micros_endpoint_record){
        .state = MICROS_ENDPOINT_STATE_ACTIVE,
        .owner = vm_process,
        .value = launcher_endpoint,
    };
    mapping_committed = false;
    notification_committed = false;
    notification_should_fail = false;
    order_violation = false;
    operation_stage = 0;
    vm_operation = TEST_VM_OPERATION_NONE;
    vm_operation_stage = 0;
    vm_order_violation = false;
    mutate_freeze_input = false;
    abort_has_scratch = false;
    claim_preflight_error = MICROS_FRAME_OWNERSHIP_OK;
    scratch_mapping_error = MICROS_USER_ADDRESS_SPACE_OK;
    failure_reason = (enum micros_bootstrap_diagnostic_reason)0;
    for (size_t index = 0; index < sizeof(user_memory); ++index) {
        user_memory[index] = UINT8_C(0xa5);
    }
}

static struct micros_syscall_arguments mapping_arguments(void)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_VM_HANDOFF_MAP_TTY_UART,
        .a1 = MICROS_TTY_MAPPING_VERSION,
        .a2 = MICROS_TTY_SERVICE_ID,
        .a3 = tty_endpoint,
        .a4 = MICROS_TTY_UART_VIRTUAL_BASE,
        .a5 = MICROS_TTY_UART_PHYSICAL_BASE,
        .a6 = (
            (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
        ) | MICROS_TTY_UART_MAPPED_LENGTH,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
}

static struct micros_syscall_arguments load_begin_arguments(void)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_BEGIN,
        .a1 = 1,
        .a2 = MICROS_USER_VIRTUAL_BASE
            + MICROS_SV39_PAGE_SIZE - 8,
        .a3 = MICROS_VM_LOAD_BEGIN_RESULT_SIZE,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
}

static bool invoke_vm_command(
    const struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_syscall_context context = {
        .objects = &objects,
        .current = vm_thread,
        .process = vm_process,
    };

    return micros_vm_handoff_handle_captured_user_ecall(
        &hart,
        frame,
        &context,
        arguments
    ) == MICROS_SYSCALL_RETURN_NORMAL;
}

static bool invoke_vm_command_panics(
    const struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
)
{
    panic_armed = true;
    if (setjmp(panic_target) == 0) {
        (void)invoke_vm_command(arguments, frame);
        panic_armed = false;
        return false;
    }
    panic_armed = false;
    return true;
}

static struct micros_syscall_arguments scratch_map_arguments(
    uint64_t load_token,
    size_t page_index
)
{
    static const uint32_t permissions[] = {
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_EXECUTE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
        MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_WRITE,
    };
    static const uint64_t virtual_addresses[] = {
        MICROS_USER_VIRTUAL_BASE,
        MICROS_USER_VIRTUAL_BASE + MICROS_SV39_PAGE_SIZE,
        MICROS_USER_VIRTUAL_BASE + 2 * MICROS_SV39_PAGE_SIZE,
        MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
    };

    return (struct micros_syscall_arguments){
        .a0 = MICROS_VM_SCRATCH_MAP,
        .a1 = load_token,
        .a2 = virtual_addresses[page_index],
        .a3 = UINT64_C(0x00800000)
            + page_index * MICROS_SV39_PAGE_SIZE,
        .a4 = permissions[page_index],
        .a5 = MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
}

static struct micros_syscall_arguments scratch_commit_arguments(
    const struct micros_syscall_arguments *map
)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_VM_SCRATCH_COMMIT,
        .a1 = map->a1,
        .a2 = map->a2,
        .a3 = map->a3,
        .a4 = MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
}

static bool begin_load(struct micros_trap_frame *frame)
{
    struct micros_syscall_arguments arguments =
        load_begin_arguments();

    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    return (
        invoke_vm_command(&arguments, frame)
        && frame->a0 == MICROS_SYSCALL_ABI_OK
    );
}

static bool append_load_page(size_t page_index)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments map =
        scratch_map_arguments(vm_state.load.load_token, page_index);
    struct micros_syscall_arguments commit =
        scratch_commit_arguments(&map);

    vm_operation = TEST_VM_OPERATION_SCRATCH_MAP;
    vm_operation_stage = 0;
    if (
        !invoke_vm_command(&map, &frame)
        || frame.a0 != MICROS_SYSCALL_ABI_OK
        || vm_operation_stage != 4
        || vm_order_violation
    ) {
        return false;
    }
    vm_operation = TEST_VM_OPERATION_SCRATCH_COMMIT;
    vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    return (
        invoke_vm_command(&commit, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && vm_operation_stage == 2
        && !vm_order_violation
    );
}

static bool fill_minimum_load(void)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        if (!append_load_page(index)) {
            return false;
        }
    }
    return true;
}

static struct micros_vm_load_freeze_plan *prepare_freeze_plan(void)
{
    struct micros_vm_load_freeze_plan *plan =
        (struct micros_vm_load_freeze_plan *)
            &user_memory[MICROS_SV39_PAGE_SIZE - 40];
    const struct micros_vm_kernel_mapping_record *record =
        &vm_state.mappings[child_process.slot];

    *plan = (struct micros_vm_load_freeze_plan){
        .version = MICROS_VM_LOAD_FREEZE_PLAN_VERSION,
        .size = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .load_token = vm_state.load.load_token,
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
    plan->page_plan_digest = micros_vm_load_page_plan_digest(
        plan->image_page_count,
        plan->data_page_count,
        record->entries
    );
    return plan;
}

static bool freeze_load(
    bool mutate_input,
    uint64_t *mapping_generation
)
{
    struct micros_vm_load_freeze_plan *plan =
        (struct micros_vm_load_freeze_plan *)
            &user_memory[MICROS_SV39_PAGE_SIZE - 40];
    const struct micros_vm_load_freeze_result *result =
        (const struct micros_vm_load_freeze_result *)
            &user_memory[2 * MICROS_SV39_PAGE_SIZE - 8];
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_VM_LOAD_FREEZE,
        .a1 = MICROS_USER_VIRTUAL_BASE
            + MICROS_SV39_PAGE_SIZE - 40,
        .a2 = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .a3 = MICROS_USER_VIRTUAL_BASE
            + 2 * MICROS_SV39_PAGE_SIZE - 8,
        .a4 = MICROS_VM_LOAD_FREEZE_RESULT_SIZE,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    struct micros_trap_frame frame = {0};

    plan = prepare_freeze_plan();
    vm_operation = TEST_VM_OPERATION_FREEZE;
    vm_operation_stage = 0;
    mutate_freeze_input = mutate_input;
    if (
        !invoke_vm_command(&arguments, &frame)
        || frame.a0 != MICROS_SYSCALL_ABI_OK
        || vm_operation_stage != 5
        || vm_order_violation
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        || child_process_record.address_space_root
            != UINT64_C(0x00810000)
        || result->version
            != MICROS_VM_LOAD_FREEZE_RESULT_VERSION
        || result->size != MICROS_VM_LOAD_FREEZE_RESULT_SIZE
        || result->mapping_generation == 0
    ) {
        return false;
    }
    if (
        mutate_input
        && plan->load_token == vm_state.load.load_token
    ) {
        return false;
    }
    *mapping_generation = result->mapping_generation;
    return true;
}

static bool invoke_mapping(
    struct micros_process_handle process,
    struct micros_thread_handle thread,
    struct micros_trap_frame *frame
)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_syscall_context context = {
        .objects = &objects,
        .current = thread,
        .process = process,
    };
    struct micros_syscall_arguments arguments = mapping_arguments();

    return micros_vm_handoff_handle_captured_user_ecall(
        &hart,
        frame,
        &context,
        &arguments
    ) == MICROS_SYSCALL_RETURN_NORMAL;
}

static bool test_load_begin_dispatches_post_handoff_command(void)
{
    const struct micros_vm_load_begin_result *result =
        (const struct micros_vm_load_begin_result *)
            &user_memory[MICROS_SV39_PAGE_SIZE - 8];
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_trap_frame frame = {0};
    struct micros_syscall_context context = {
        .objects = &objects,
        .current = vm_thread,
        .process = vm_process,
    };
    struct micros_syscall_arguments arguments =
        load_begin_arguments();

    reset_fixture();
    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    return (
        micros_vm_handoff_handle_captured_user_ecall(
            &hart,
            &frame,
            &context,
            &arguments
        ) == MICROS_SYSCALL_RETURN_NORMAL
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && result->version == MICROS_VM_LOAD_BEGIN_RESULT_VERSION
        && result->size == MICROS_VM_LOAD_BEGIN_RESULT_SIZE
        && result->load_token == 1
        && result->process_slot == child_process.slot
        && result->process_generation == child_process.generation
        && vm_state.load.phase == MICROS_VM_LOAD_PHASE_LOADING
    );
}

static bool test_load_begin_failures_preserve_state_and_output(void)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments arguments =
        load_begin_arguments();
    size_t output_offset = sizeof(user_memory) - 16;
    size_t index;

    reset_fixture();
    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    arguments.a1 = 2;
    if (
        !invoke_vm_command(&arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    for (index = 0; index < MICROS_VM_LOAD_BEGIN_RESULT_SIZE; ++index) {
        if (
            user_memory[MICROS_SV39_PAGE_SIZE - 8 + index]
                != UINT8_C(0xa5)
        ) {
            return false;
        }
    }

    reset_fixture();
    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    arguments = load_begin_arguments();
    arguments.a2 = MICROS_USER_VIRTUAL_BASE + output_offset;
    frame = (struct micros_trap_frame){0};
    if (
        !invoke_vm_command(&arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_MEMORY_FAULT
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    for (index = output_offset; index < sizeof(user_memory); ++index) {
        if (user_memory[index] != UINT8_C(0xa5)) {
            return false;
        }
    }

    reset_fixture();
    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    arguments = load_begin_arguments();
    arguments.a1 = 2;
    arguments.a2 = MICROS_USER_VIRTUAL_BASE + output_offset;
    frame = (struct micros_trap_frame){0};
    if (
        !invoke_vm_command(&arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_MEMORY_FAULT
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }
    return true;
}

static bool test_post_handoff_phase_and_authority_are_enforced(void)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_syscall_context foreign_context = {
        .objects = &objects,
        .current = foreign_thread,
        .process = foreign_process,
    };
    struct micros_syscall_arguments arguments =
        load_begin_arguments();
    struct micros_trap_frame frame = {0};

    reset_fixture();
    if (
        !invoke_vm_command(&arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }

    reset_fixture();
    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    frame = (struct micros_trap_frame){0};
    return (
        micros_vm_handoff_handle_captured_user_ecall(
            &hart,
            &frame,
            &foreign_context,
            &arguments
        ) == MICROS_SYSCALL_RETURN_NORMAL
        && frame.a0
            == (uint64_t)(int64_t)
                MICROS_SYSCALL_ABI_UNAUTHORIZED
        && vm_state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
    );
}

static bool test_load_freeze_failure_preserves_state_and_output(void)
{
    struct micros_vm_load_freeze_plan *plan;
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_VM_LOAD_FREEZE,
        .a1 = MICROS_USER_VIRTUAL_BASE
            + MICROS_SV39_PAGE_SIZE - 40,
        .a2 = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .a3 = MICROS_USER_VIRTUAL_BASE
            + 2 * MICROS_SV39_PAGE_SIZE - 8,
        .a4 = MICROS_VM_LOAD_FREEZE_RESULT_SIZE,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    struct micros_trap_frame frame = {0};
    size_t output_offset = 2 * MICROS_SV39_PAGE_SIZE - 8;
    size_t index;

    reset_fixture();
    if (!begin_load(&frame) || !fill_minimum_load()) {
        return false;
    }
    plan = prepare_freeze_plan();
    plan->version = 0;
    ++plan->load_token;
    vm_operation = TEST_VM_OPERATION_FREEZE;
    vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    if (
        !invoke_vm_command(&arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || child_process_record.address_space_root != 0
        || vm_operation_stage != 0
    ) {
        return false;
    }

    reset_fixture();
    if (!begin_load(&frame) || !fill_minimum_load()) {
        return false;
    }
    plan = prepare_freeze_plan();
    plan->page_plan_digest ^= 1;
    vm_operation = TEST_VM_OPERATION_FREEZE;
    vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    if (
        !invoke_vm_command(&arguments, &frame)
        || frame.a0
            != (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || child_process_record.address_space_root != 0
        || vm_operation_stage != 0
    ) {
        return false;
    }
    for (index = 0; index < MICROS_VM_LOAD_FREEZE_RESULT_SIZE; ++index) {
        if (user_memory[output_offset + index] != UINT8_C(0xa5)) {
            return false;
        }
    }
    return true;
}

static bool test_post_handoff_invariants_are_fatal(void)
{
    struct micros_syscall_arguments arguments;
    struct micros_trap_frame frame = {0};

    reset_fixture();
    bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    vm_state.last_load_token = UINT64_MAX;
    arguments = load_begin_arguments();
    if (
        !invoke_vm_command_panics(&arguments, &frame)
        || failure_reason != 0
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
    ) {
        return false;
    }

    reset_fixture();
    if (!begin_load(&frame)) {
        return false;
    }
    arguments = scratch_map_arguments(
        vm_state.load.load_token,
        0
    );
    vm_operation = TEST_VM_OPERATION_SCRATCH_MAP;
    claim_preflight_error =
        MICROS_FRAME_OWNERSHIP_ERROR_WRONG_OWNER;
    frame = (struct micros_trap_frame){0};
    if (
        !invoke_vm_command_panics(&arguments, &frame)
        || failure_reason != 0
        || vm_state.load.scratch_phase
            != MICROS_VM_SCRATCH_CLEAR
        || vm_operation_stage != 0
    ) {
        return false;
    }

    reset_fixture();
    if (!begin_load(&frame)) {
        return false;
    }
    arguments = scratch_map_arguments(
        vm_state.load.load_token,
        0
    );
    vm_operation = TEST_VM_OPERATION_SCRATCH_MAP;
    scratch_mapping_error =
        MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
    frame = (struct micros_trap_frame){0};
    return (
        invoke_vm_command_panics(&arguments, &frame)
        && failure_reason == 0
        && vm_state.load.scratch_phase
            == MICROS_VM_SCRATCH_CLEAR
        && vm_operation_stage == 1
    );
}

static bool test_load_lifecycle_commit_and_exit_release(void)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments commit;
    struct micros_syscall_arguments release;
    uint64_t load_token;
    uint64_t mapping_generation;

    reset_fixture();
    if (
        !begin_load(&frame)
        || !fill_minimum_load()
        || !freeze_load(true, &mapping_generation)
    ) {
        return false;
    }
    load_token = vm_state.load.load_token;
    if (
        micros_vm_load_mark_prepared(
            &vm_state,
            child_process,
            pm_state.active_transaction,
            load_token,
            mapping_generation
        ) != MICROS_VM_HANDOFF_OK
        || micros_vm_load_mark_activated(
            &vm_state,
            child_process,
            pm_state.active_transaction,
            load_token,
            mapping_generation
        ) != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    pm_state.active_transaction = 0;
    pm_state.reserved_process =
        (struct micros_process_handle){0};
    commit = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_COMMIT,
        .a1 = load_token,
        .a2 = mapping_generation,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    frame = (struct micros_trap_frame){0};
    if (
        !invoke_vm_command(&commit, &frame)
        || frame.a0 != MICROS_SYSCALL_ABI_OK
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_IDLE
        || vm_state.mappings[child_process.slot].phase
            != MICROS_VM_KERNEL_MAPPING_ACTIVE
    ) {
        return false;
    }
    if (
        micros_vm_load_mark_exit_detached(
            &vm_state,
            child_process,
            UINT64_C(0x77),
            load_token,
            mapping_generation
        ) != MICROS_VM_HANDOFF_OK
    ) {
        return false;
    }
    release = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_EXIT_RELEASE,
        .a1 = UINT64_C(0x77),
        .a2 = load_token,
        .a3 = mapping_generation,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    vm_operation = TEST_VM_OPERATION_EXIT_RELEASE;
    vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    return (
        invoke_vm_command(&release, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && vm_operation_stage == 4
        && !vm_order_violation
        && child_process_record.address_space_root == 0
        && vm_state.mappings[child_process.slot].phase
            == MICROS_VM_KERNEL_MAPPING_UNUSED
    );
}

static bool test_load_abort_before_freeze_removes_scratch(void)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments map;
    struct micros_syscall_arguments abort_arguments;

    reset_fixture();
    if (!begin_load(&frame)) {
        return false;
    }
    map = scratch_map_arguments(vm_state.load.load_token, 0);
    vm_operation = TEST_VM_OPERATION_SCRATCH_MAP;
    vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    if (
        !invoke_vm_command(&map, &frame)
        || frame.a0 != MICROS_SYSCALL_ABI_OK
        || vm_state.load.scratch_phase
            != MICROS_VM_SCRATCH_MAPPED
    ) {
        return false;
    }
    abort_arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_ABORT,
        .a1 = vm_state.load.load_token,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    vm_operation = TEST_VM_OPERATION_ABORT_LOADING;
    vm_operation_stage = 0;
    abort_has_scratch = true;
    frame = (struct micros_trap_frame){0};
    return (
        invoke_vm_command(&abort_arguments, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && vm_operation_stage == 4
        && !vm_order_violation
        && vm_state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
        && vm_state.mappings[child_process.slot].phase
            == MICROS_VM_KERNEL_MAPPING_UNUSED
    );
}

static bool test_load_abort_before_freeze_without_scratch(void)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments arguments;

    reset_fixture();
    if (!begin_load(&frame)) {
        return false;
    }
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_ABORT,
        .a1 = vm_state.load.load_token,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    vm_operation = TEST_VM_OPERATION_ABORT_LOADING;
    vm_operation_stage = 0;
    abort_has_scratch = false;
    frame = (struct micros_trap_frame){0};
    return (
        invoke_vm_command(&arguments, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && vm_operation_stage == 4
        && !vm_order_violation
        && vm_state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
    );
}

static bool test_load_abort_after_freeze_tears_down_child(void)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments arguments;
    uint64_t mapping_generation;

    reset_fixture();
    if (
        !begin_load(&frame)
        || !fill_minimum_load()
        || !freeze_load(false, &mapping_generation)
    ) {
        return false;
    }
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_LOAD_ABORT,
        .a1 = vm_state.load.load_token,
        .a2 = mapping_generation,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    vm_operation = TEST_VM_OPERATION_ABORT_FROZEN;
    vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    return (
        invoke_vm_command(&arguments, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && vm_operation_stage == 4
        && !vm_order_violation
        && child_process_record.address_space_root == 0
        && vm_state.load.phase == MICROS_VM_LOAD_PHASE_IDLE
    );
}

static bool test_successful_mapping(void)
{
    struct micros_trap_frame frame = {0};

    reset_fixture();
    return (
        invoke_mapping(vm_process, vm_thread, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && operation_stage == 6
        && mapping_committed
        && notification_committed
        && !order_violation
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAPPED
    );
}

static bool test_unauthorized_mapping_is_atomic(void)
{
    struct micros_trap_frame frame = {0};

    reset_fixture();
    return (
        invoke_mapping(
            foreign_process,
            foreign_thread,
            &frame
        )
        && frame.a0
            == (uint64_t)(int64_t)MICROS_SYSCALL_ABI_UNAUTHORIZED
        && operation_stage == 0
        && !mapping_committed
        && !notification_committed
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
    );
}

static bool test_wrong_phase_is_atomic(void)
{
    struct micros_trap_frame frame = {0};

    reset_fixture();
    if (
        micros_tty_handoff_mark_mapped(&tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return false;
    }
    return (
        invoke_mapping(vm_process, vm_thread, &frame)
        && frame.a0
            == (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE
        && operation_stage == 0
        && !mapping_committed
        && !notification_committed
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAPPED
    );
}

static bool test_notification_preflight_is_atomic(void)
{
    struct micros_trap_frame frame = {0};

    reset_fixture();
    notification_should_fail = true;
    panic_armed = true;
    if (setjmp(panic_target) == 0) {
        (void)invoke_mapping(vm_process, vm_thread, &frame);
        panic_armed = false;
        return false;
    }
    panic_armed = false;
    return (
        failure_reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_MAP_GATE
        && operation_stage == 3
        && !mapping_committed
        && !notification_committed
        && !order_violation
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
    );
}

static bool test_uncleared_console_event_is_rejected(int event_state)
{
    struct micros_trap_frame frame = {0};

    reset_fixture();
    if (event_state == 1) {
        vm_thread_record.ipc_delivery_pending = true;
    } else if (event_state == 2) {
        launcher_endpoint_record.pending_kernel_events =
            MICROS_KERNEL_EVENT_CONSOLE_MAPPED;
    } else if (event_state == 3) {
        launcher_thread_record.ipc_delivery_pending = true;
        launcher_thread_record.ipc_staged_result = MICROS_IPC_OK;
        launcher_thread_record.ipc_inbound_message.source =
            MICROS_ENDPOINT_NONE;
        launcher_thread_record.ipc_inbound_message.type =
            MICROS_IPC_TYPE_KERNEL_NOTIFICATION;
    } else {
        vm_endpoint_record.pending_kernel_events =
            MICROS_KERNEL_EVENT_CONSOLE_MAP_REQUEST;
    }
    panic_armed = true;
    if (setjmp(panic_target) == 0) {
        (void)invoke_mapping(vm_process, vm_thread, &frame);
        panic_armed = false;
        return false;
    }
    panic_armed = false;
    return (
        failure_reason
            == MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_MAP_GATE
        && operation_stage == 0
        && !mapping_committed
        && !notification_committed
        && tty_state.handoff.console_phase
            == MICROS_TTY_CONSOLE_MAP_REQUESTED
    );
}

int main(void)
{
    return (
        test_load_begin_dispatches_post_handoff_command()
        && test_load_begin_failures_preserve_state_and_output()
        && test_post_handoff_phase_and_authority_are_enforced()
        && test_load_freeze_failure_preserves_state_and_output()
        && test_post_handoff_invariants_are_fatal()
        && test_load_lifecycle_commit_and_exit_release()
        && test_load_abort_before_freeze_removes_scratch()
        && test_load_abort_before_freeze_without_scratch()
        && test_load_abort_after_freeze_tears_down_child()
        && test_successful_mapping()
        && test_unauthorized_mapping_is_atomic()
        && test_wrong_phase_is_atomic()
        && test_notification_preflight_is_atomic()
        && test_uncleared_console_event_is_rejected(0)
        && test_uncleared_console_event_is_rejected(1)
        && test_uncleared_console_event_is_rejected(2)
        && test_uncleared_console_event_is_rejected(3)
    ) ? 0 : 1;
}

const struct micros_bootstrap_control_state *
micros_bootstrap_runtime_state(void)
{
    return &bootstrap;
}

struct micros_bootstrap_control_state *
micros_bootstrap_runtime_authoritative_state(void)
{
    return &bootstrap;
}

enum micros_bootstrap_error micros_bootstrap_runtime_validate(void)
{
    return MICROS_BOOTSTRAP_OK;
}

enum micros_bootstrap_error
micros_bootstrap_runtime_validate_vm_prepared(void)
{
    return MICROS_BOOTSTRAP_OK;
}

_Noreturn void micros_bootstrap_runtime_fail(
    enum micros_bootstrap_diagnostic_reason reason,
    uint32_t service_id,
    micros_endpoint_t endpoint,
    uint64_t detail
)
{
    (void)service_id;
    (void)endpoint;
    (void)detail;
    failure_reason = reason;
    if (panic_armed) {
        longjmp(panic_target, 1);
    }
    abort();
}

const struct micros_bootstrap_binding *
micros_bootstrap_control_find_binding(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    return micros_bootstrap_control_find_binding_bounded(
        state,
        service_id
    );
}

struct micros_vm_handoff_state *
micros_vm_handoff_runtime_authoritative_state(void)
{
    return &vm_state;
}

const struct micros_vm_handoff_state *
micros_vm_handoff_runtime_state(void)
{
    return &vm_state;
}

enum micros_vm_handoff_error micros_vm_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    return (
        state == &bootstrap
        && endpoint_registry == &registry
        && kernel_objects == &objects
    ) ? MICROS_VM_HANDOFF_OK : MICROS_VM_HANDOFF_ERROR_INVARIANT;
}

const struct micros_pm_control_state *
micros_pm_control_runtime_state(void)
{
    return &pm_state;
}

enum micros_pm_control_error micros_pm_control_state_validate(
    const struct micros_pm_control_state *state
)
{
    return (
        state == &pm_state
        && state->service_id == 3
        && (
            state->active_transaction == 0
                ? state->reserved_process.slot == 0
                    && state->reserved_process.generation == 0
                : state->active_transaction
                    == state->last_transaction
                    && process_handles_equal(
                        state->reserved_process,
                        child_process
                    )
        )
    ) ? MICROS_PM_CONTROL_OK
      : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

enum micros_pm_control_error micros_pm_control_runtime_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    return (
        state == &bootstrap
        && endpoint_registry == &registry
        && kernel_objects == &objects
    ) ? MICROS_PM_CONTROL_OK
      : MICROS_PM_CONTROL_ERROR_INVARIANT;
}

struct micros_tty_handoff_runtime_state *
micros_tty_handoff_runtime_authoritative_state(void)
{
    return &tty_state;
}

enum micros_tty_handoff_error
micros_tty_handoff_runtime_prepare_mapped(
    struct micros_tty_handoff *candidate
)
{
    if (
        candidate == NULL
        || operation_stage != 1
        || mapping_committed
        || notification_committed
    ) {
        order_violation = true;
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    *candidate = tty_state.handoff;
    if (
        micros_tty_handoff_mark_mapped(candidate)
            != MICROS_TTY_HANDOFF_OK
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    operation_stage = 2;
    return MICROS_TTY_HANDOFF_OK;
}

void micros_tty_handoff_runtime_commit_mapped_prevalidated(
    const struct micros_tty_handoff *candidate
)
{
    if (
        candidate == NULL
        || operation_stage != 4
        || !mapping_committed
        || notification_committed
    ) {
        order_violation = true;
        return;
    }
    tty_state.handoff = *candidate;
    operation_stage = 5;
}

enum micros_tty_handoff_error micros_tty_handoff_runtime_validate(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects
)
{
    return (
        state == &bootstrap
        && endpoint_registry == &registry
        && kernel_objects == &objects
        && micros_tty_handoff_validate(&tty_state.handoff)
            == MICROS_TTY_HANDOFF_OK
    ) ? MICROS_TTY_HANDOFF_OK
      : MICROS_TTY_HANDOFF_ERROR_INVARIANT;
}

struct micros_endpoint_registry *
micros_ipc_runtime_authoritative_registry(void)
{
    return &registry;
}

const struct micros_frame_ownership *
micros_frame_ownership_runtime_ledger(void)
{
    return &ownership;
}

enum micros_kernel_object_error micros_process_resolve(
    const struct micros_kernel_objects *kernel_objects,
    struct micros_process_handle handle,
    const struct micros_process **process
)
{
    if (kernel_objects != &objects || process == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (process_handles_equal(handle, vm_process)) {
        *process = &vm_process_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (process_handles_equal(handle, foreign_process)) {
        *process = &foreign_process_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (process_handles_equal(handle, child_process)) {
        *process = &child_process_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    return MICROS_KERNEL_OBJECT_ERROR_STALE;
}

enum micros_kernel_object_error micros_thread_resolve(
    const struct micros_kernel_objects *kernel_objects,
    struct micros_thread_handle handle,
    const struct micros_thread **thread
)
{
    if (kernel_objects != &objects || thread == NULL) {
        return MICROS_KERNEL_OBJECT_ERROR_ARGUMENT;
    }
    if (thread_handles_equal(handle, vm_thread)) {
        *thread = &vm_thread_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (thread_handles_equal(handle, launcher_thread)) {
        *thread = &launcher_thread_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    if (thread_handles_equal(handle, foreign_thread)) {
        *thread = &foreign_thread_record;
        return MICROS_KERNEL_OBJECT_OK;
    }
    return MICROS_KERNEL_OBJECT_ERROR_STALE;
}

bool micros_thread_ipc_state_is_clear(
    const struct micros_thread *thread
)
{
    return (
        thread != NULL
        && thread->ipc_queue_kind == MICROS_IPC_QUEUE_NONE
        && thread->ipc_next.slot == 0
        && thread->ipc_next.generation == 0
        && bytes_are_zero(
            &thread->ipc_outbound_message,
            sizeof(thread->ipc_outbound_message)
        )
        && thread->ipc_send_destination == 0
        && thread->ipc_receive_source == 0
        && thread->ipc_receive_buffer == 0
        && !thread->ipc_delivery_pending
        && bytes_are_zero(
            &thread->ipc_inbound_message,
            sizeof(thread->ipc_inbound_message)
        )
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_reply_token == 0
        && thread->ipc_reply_callee == 0
    );
}

bool micros_ipc_thread_has_staged_kernel_notification(
    const struct micros_thread *thread
)
{
    return (
        thread != NULL
        && thread->ipc_delivery_pending
        && thread->ipc_staged_result == MICROS_IPC_OK
        && thread->ipc_inbound_message.source
            == MICROS_ENDPOINT_NONE
        && thread->ipc_inbound_message.type
            == MICROS_IPC_TYPE_KERNEL_NOTIFICATION
    );
}

enum micros_endpoint_error micros_endpoint_resolve_active(
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects,
    micros_endpoint_t endpoint,
    const struct micros_endpoint_record **record
)
{
    if (
        endpoint_registry != &registry
        || kernel_objects != &objects
        || record == NULL
    ) {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    if (endpoint == vm_endpoint) {
        *record = &vm_endpoint_record;
    } else if (endpoint == launcher_endpoint) {
        *record = &launcher_endpoint_record;
    } else {
        return MICROS_ENDPOINT_ERROR_STALE;
    }
    return MICROS_ENDPOINT_OK;
}

enum micros_endpoint_error
micros_privilege_profile_allows_kernel_operation(
    const struct micros_endpoint_registry *endpoint_registry,
    uint8_t profile_id,
    uint8_t operation
)
{
    return (
        endpoint_registry == &registry
        && profile_id == TEST_VM_PROFILE_ID
        && operation == 1
    ) ? MICROS_ENDPOINT_OK : MICROS_ENDPOINT_ERROR_UNAUTHORIZED;
}

bool micros_plic_validate(enum micros_plic_phase expected_phase)
{
    return expected_phase == MICROS_PLIC_DISABLED;
}

bool uart_console_handoff_is_quiesced(void)
{
    return true;
}

enum micros_user_address_space_error
micros_user_address_space_complete_wired_handoff(void)
{
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error micros_user_address_space_validate(
    struct micros_process_handle process
)
{
    return (
        process_handles_equal(process, tty_process)
        || process_handles_equal(process, vm_process)
    )
        ? MICROS_USER_ADDRESS_SPACE_OK
        : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
}

enum micros_user_address_space_error
micros_user_address_space_translate(
    struct micros_process_handle process,
    uint64_t user_address,
    uint64_t *physical_address,
    uint32_t *permissions,
    size_t *contiguous_bytes
)
{
    size_t offset;
    size_t page_offset;

    if (
        !process_handles_equal(process, vm_process)
        || physical_address == NULL
        || permissions == NULL
        || contiguous_bytes == NULL
        || user_address < MICROS_USER_VIRTUAL_BASE
        || user_address
            >= MICROS_USER_VIRTUAL_BASE + sizeof(user_memory)
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED;
    }
    offset = (size_t)(user_address - MICROS_USER_VIRTUAL_BASE);
    page_offset = offset % MICROS_SV39_PAGE_SIZE;
    *physical_address = (uint64_t)(uintptr_t)&user_memory[offset];
    *permissions = MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;
    *contiguous_bytes = MICROS_SV39_PAGE_SIZE - page_offset;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_tty_uart_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    struct micros_user_address_space_tty_uart_plan *plan
)
{
    if (
        !process_handles_equal(process, tty_process)
        || expected_root_physical_address
            != tty_state.root_physical_address
        || plan == NULL
        || operation_stage != 0
        || mapping_committed
        || notification_committed
    ) {
        order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    plan->active = true;
    operation_stage = 1;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_commit_tty_uart_mapping(
    struct micros_user_address_space_tty_uart_plan *plan
)
{
    if (
        plan == NULL
        || !plan->active
        || operation_stage != 3
        || mapping_committed
        || notification_committed
    ) {
        order_violation = true;
        return;
    }
    mapping_committed = true;
    operation_stage = 4;
}

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_claim_preflight(
    uint64_t physical_address,
    struct micros_frame_vm_claim_plan *plan
)
{
    if (physical_address == 0 || plan == NULL) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        vm_operation != TEST_VM_OPERATION_SCRATCH_MAP
        || vm_operation_stage != 0
        || vm_state.load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
    ) {
        vm_order_violation = true;
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    if (claim_preflight_error != MICROS_FRAME_OWNERSHIP_OK) {
        return claim_preflight_error;
    }
    plan->frame_index = physical_address / MICROS_SV39_PAGE_SIZE;
    vm_operation_stage = 1;
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_runtime_vm_claim_commit_prevalidated(
    const struct micros_frame_vm_claim_plan *plan
)
{
    if (
        plan == NULL
        || vm_operation != TEST_VM_OPERATION_SCRATCH_MAP
        || vm_operation_stage != 2
        || vm_state.load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
        || vm_state.load.scratch.physical_address == 0
    ) {
        vm_order_violation = true;
        return;
    }
    vm_operation_stage = 3;
}

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_release_transferable_preflight(
    const uint64_t *physical_addresses,
    size_t physical_address_count,
    struct micros_frame_vm_transferable_release_plan *plan
)
{
    if (
        plan == NULL
        || (
            physical_address_count != 0
            && physical_addresses == NULL
        )
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        vm_operation == TEST_VM_OPERATION_ABORT_LOADING
        && (
            vm_operation_stage == 0
            || vm_operation_stage == 1
        )
    ) {
        vm_operation_stage = 2;
    } else if (vm_operation == TEST_VM_OPERATION_ABORT_LOADING) {
        vm_order_violation = true;
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    plan->frame_count = (uint32_t)physical_address_count;
    return MICROS_FRAME_OWNERSHIP_OK;
}

void
micros_frame_ownership_runtime_vm_release_transferable_commit_prevalidated(
    const struct micros_frame_vm_transferable_release_plan *plan
)
{
    if (
        plan == NULL
        || vm_operation != TEST_VM_OPERATION_ABORT_LOADING
        || vm_operation_stage != (abort_has_scratch ? 3 : 2)
    ) {
        vm_order_violation = true;
        return;
    }
    vm_operation_stage = 4;
}

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_freeze_preflight(
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_freeze_plan *plan
)
{
    if (
        !process_handles_equal(process, child_process)
        || data_physical_addresses == NULL
        || data_physical_address_count == 0
        || table_physical_addresses == NULL
        || plan == NULL
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        vm_operation != TEST_VM_OPERATION_FREEZE
        || vm_operation_stage != 0
    ) {
        vm_order_violation = true;
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    plan->process = process;
    plan->data_frame_count = (uint32_t)data_physical_address_count;
    vm_operation_stage = 1;
    if (mutate_freeze_input) {
        struct micros_vm_load_freeze_plan *input =
            (struct micros_vm_load_freeze_plan *)
                &user_memory[MICROS_SV39_PAGE_SIZE - 40];

        input->load_token ^= UINT64_C(0x55);
    }
    return MICROS_FRAME_OWNERSHIP_OK;
}

void micros_frame_ownership_runtime_vm_freeze_commit_prevalidated(
    const struct micros_frame_vm_freeze_plan *plan
)
{
    if (
        plan == NULL
        || vm_operation != TEST_VM_OPERATION_FREEZE
        || vm_operation_stage != 3
        || vm_state.load.phase != MICROS_VM_LOAD_PHASE_LOADING
        || child_process_record.address_space_root != 0
    ) {
        vm_order_violation = true;
        return;
    }
    vm_operation_stage = 4;
}

enum micros_frame_ownership_error
micros_frame_ownership_runtime_vm_release_process_preflight(
    struct micros_process_handle process,
    const uint64_t *data_physical_addresses,
    size_t data_physical_address_count,
    const uint64_t
        table_physical_addresses[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    struct micros_frame_vm_process_release_plan *plan
)
{
    if (
        !process_handles_equal(process, child_process)
        || data_physical_addresses == NULL
        || data_physical_address_count == 0
        || table_physical_addresses == NULL
        || plan == NULL
    ) {
        return MICROS_FRAME_OWNERSHIP_ERROR_ARGUMENT;
    }
    if (
        (
            vm_operation != TEST_VM_OPERATION_ABORT_FROZEN
            && vm_operation != TEST_VM_OPERATION_EXIT_RELEASE
        )
        || vm_operation_stage != 1
    ) {
        vm_order_violation = true;
        return MICROS_FRAME_OWNERSHIP_ERROR_INVARIANT;
    }
    plan->process = process;
    plan->data_frame_count = (uint32_t)data_physical_address_count;
    vm_operation_stage = 2;
    return MICROS_FRAME_OWNERSHIP_OK;
}

void
micros_frame_ownership_runtime_vm_release_process_commit_prevalidated(
    const struct micros_frame_vm_process_release_plan *plan
)
{
    if (
        plan == NULL
        || (
            vm_operation != TEST_VM_OPERATION_ABORT_FROZEN
            && vm_operation != TEST_VM_OPERATION_EXIT_RELEASE
        )
        || vm_operation_stage != 3
        || child_process_record.address_space_root != 0
    ) {
        vm_order_violation = true;
        return;
    }
    vm_operation_stage = 4;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_scratch_mapping(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    if (
        !process_handles_equal(process, vm_process)
        || expected_root_physical_address
            != vm_state.root_physical_address
        || physical_address == 0
        || plan == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (
        vm_operation != TEST_VM_OPERATION_SCRATCH_MAP
        || vm_operation_stage != 1
    ) {
        vm_order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    if (scratch_mapping_error != MICROS_USER_ADDRESS_SPACE_OK) {
        return scratch_mapping_error;
    }
    plan->active = true;
    plan->process = process;
    plan->physical_address = physical_address;
    vm_operation_stage = 2;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_commit_vm_scratch_mapping(
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    if (
        plan == NULL
        || vm_operation != TEST_VM_OPERATION_SCRATCH_MAP
        || vm_operation_stage != 3
        || vm_state.load.scratch_phase != MICROS_VM_SCRATCH_CLEAR
        || vm_state.load.scratch.physical_address
            != plan->physical_address
    ) {
        vm_order_violation = true;
        return;
    }
    plan->active = false;
    vm_operation_stage = 4;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_scratch_removal(
    struct micros_process_handle process,
    uint64_t expected_root_physical_address,
    uint64_t physical_address,
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    if (
        !process_handles_equal(process, vm_process)
        || expected_root_physical_address
            != vm_state.root_physical_address
        || physical_address == 0
        || plan == NULL
        || (
            vm_operation != TEST_VM_OPERATION_SCRATCH_COMMIT
            && vm_operation != TEST_VM_OPERATION_ABORT_LOADING
        )
        || vm_operation_stage != 0
        || vm_state.load.scratch_phase
            != MICROS_VM_SCRATCH_MAPPED
    ) {
        vm_order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    plan->active = true;
    plan->process = process;
    plan->physical_address = physical_address;
    vm_operation_stage = 1;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_commit_vm_scratch_removal(
    struct micros_user_address_space_vm_scratch_plan *plan
)
{
    if (
        plan == NULL
        || !plan->active
        || (
            vm_operation == TEST_VM_OPERATION_SCRATCH_COMMIT
                ? vm_operation_stage != 1
                : vm_operation
                        != TEST_VM_OPERATION_ABORT_LOADING
                    || vm_operation_stage != 2
        )
        || vm_state.load.scratch_phase
            != MICROS_VM_SCRATCH_MAPPED
    ) {
        vm_order_violation = true;
        return;
    }
    plan->active = false;
    vm_operation_stage = vm_operation
            == TEST_VM_OPERATION_SCRATCH_COMMIT
        ? 2
        : 3;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_freeze(
    const struct micros_vm_load_freeze_action *action,
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    if (
        action == NULL
        || action->entry_count == 0
        || plan == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (
        vm_operation != TEST_VM_OPERATION_FREEZE
        || vm_operation_stage != 1
    ) {
        vm_order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    plan->phase = MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_PREPARED;
    plan->tables.process = action->process;
    plan->tables.entry_count = action->entry_count;
    for (
        size_t index = 0;
        index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT;
        ++index
    ) {
        plan->tables.table_frames[index] =
            action->table_frames[index];
    }
    vm_operation_stage = 2;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_construct_vm_freeze_prevalidated(
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    if (
        plan == NULL
        || vm_operation != TEST_VM_OPERATION_FREEZE
        || vm_operation_stage != 2
    ) {
        vm_order_violation = true;
        return;
    }
    plan->phase = MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_CONSTRUCTED;
    vm_operation_stage = 3;
}

void micros_user_address_space_install_vm_freeze_prevalidated(
    struct micros_user_address_space_vm_freeze_plan *plan
)
{
    if (
        plan == NULL
        || vm_operation != TEST_VM_OPERATION_FREEZE
        || vm_operation_stage != 4
        || plan->phase
            != MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_CONSTRUCTED
    ) {
        vm_order_violation = true;
        return;
    }
    child_process_record.address_space_root =
        plan->tables.table_frames[0];
    plan->phase = MICROS_USER_ADDRESS_SPACE_VM_FREEZE_PLAN_UNUSED;
    vm_operation_stage = 5;
}

enum micros_user_address_space_error
micros_user_address_space_prepare_vm_teardown(
    struct micros_process_handle process,
    uint64_t root_physical_address,
    const uint64_t
        table_frames[MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT],
    const struct micros_vm_kernel_mapping_entry *entries,
    size_t entry_count,
    struct micros_user_address_space_vm_teardown_plan *plan
)
{
    if (
        !process_handles_equal(process, child_process)
        || root_physical_address == 0
        || table_frames == NULL
        || entries == NULL
        || entry_count == 0
        || plan == NULL
    ) {
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
    if (
        (
            vm_operation != TEST_VM_OPERATION_ABORT_FROZEN
            && vm_operation != TEST_VM_OPERATION_EXIT_RELEASE
        )
        || vm_operation_stage != 0
    ) {
        vm_order_violation = true;
        return MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
    }
    plan->active = true;
    plan->process = process;
    plan->root_physical_address = root_physical_address;
    plan->entry_count = (uint32_t)entry_count;
    vm_operation_stage = 1;
    return MICROS_USER_ADDRESS_SPACE_OK;
}

void micros_user_address_space_commit_vm_teardown_prevalidated(
    struct micros_user_address_space_vm_teardown_plan *plan
)
{
    if (
        plan == NULL
        || !plan->active
        || (
            vm_operation != TEST_VM_OPERATION_ABORT_FROZEN
            && vm_operation != TEST_VM_OPERATION_EXIT_RELEASE
        )
        || vm_operation_stage != 2
    ) {
        vm_order_violation = true;
        return;
    }
    child_process_record.address_space_root = 0;
    plan->active = false;
    vm_operation_stage = 3;
}

enum micros_ipc_error micros_ipc_prepare_kernel_notification(
    const struct micros_endpoint_registry *endpoint_registry,
    const struct micros_kernel_objects *kernel_objects,
    micros_endpoint_t destination,
    uint64_t event_mask,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    if (
        endpoint_registry != &registry
        || kernel_objects != &objects
        || destination != launcher_endpoint
        || event_mask != MICROS_KERNEL_EVENT_CONSOLE_MAPPED
        || plan == NULL
        || operation_stage != 2
        || mapping_committed
        || notification_committed
    ) {
        order_violation = true;
        return MICROS_IPC_ERROR_INVARIANT;
    }
    operation_stage = 3;
    if (notification_should_fail) {
        return MICROS_IPC_ERROR_STATE;
    }
    plan->active = true;
    plan->event_mask = event_mask;
    return MICROS_IPC_OK;
}

void micros_ipc_commit_kernel_notification_prevalidated(
    struct micros_endpoint_registry *endpoint_registry,
    struct micros_kernel_objects *kernel_objects,
    struct micros_ipc_kernel_notification_plan *plan
)
{
    if (
        endpoint_registry != &registry
        || kernel_objects != &objects
        || plan == NULL
        || !plan->active
        || operation_stage != 5
        || !mapping_committed
        || notification_committed
    ) {
        order_violation = true;
        return;
    }
    notification_committed = true;
    operation_stage = 6;
}

_Noreturn void micros_trap_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *trap_frame
)
{
    (void)hart_id;
    (void)reason;
    (void)file;
    (void)line;
    (void)trap_frame;
    if (panic_armed) {
        longjmp(panic_target, 1);
    }
    abort();
}
