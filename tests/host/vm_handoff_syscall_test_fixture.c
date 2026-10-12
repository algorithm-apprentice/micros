#include "vm_handoff_syscall_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static struct vm_handoff_syscall_test_context test_context = {
    .vm_process = {1, 1},
    .vm_thread = {1, 1},
    .launcher_thread = {3, 1},
    .foreign_process = {3, 1},
    .foreign_thread = {2, 1},
    .tty_process = {MICROS_TTY_PROCESS_SLOT, 1},
    .child_process = {4, 7},
    .launcher_endpoint = UINT32_C(0x00001003),
    .vm_endpoint = UINT32_C(0x00001001),
    .tty_endpoint = UINT32_C(0x00010004),
};

struct vm_handoff_syscall_test_context *vm_handoff_syscall_test_context(void)
{
    return &test_context;
}

bool vm_handoff_syscall_test_process_handles_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool vm_handoff_syscall_test_thread_handles_equal(
    struct micros_thread_handle left,
    struct micros_thread_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

bool vm_handoff_syscall_test_bytes_are_zero(const void *storage, size_t size)
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

static void reset_fixture_identities(
    struct vm_handoff_syscall_test_context *fixture
)
{
    fixture->vm_process = (struct micros_process_handle){1, 1};
    fixture->vm_thread = (struct micros_thread_handle){1, 1};
    fixture->launcher_thread = (struct micros_thread_handle){3, 1};
    fixture->foreign_process = (struct micros_process_handle){3, 1};
    fixture->foreign_thread = (struct micros_thread_handle){2, 1};
    fixture->tty_process = (struct micros_process_handle){
        MICROS_TTY_PROCESS_SLOT,
        1,
    };
    fixture->child_process = (struct micros_process_handle){4, 7};
    fixture->launcher_endpoint = UINT32_C(0x00001003);
    fixture->vm_endpoint = UINT32_C(0x00001001);
    fixture->tty_endpoint = UINT32_C(0x00010004);
}

static void reset_bootstrap_state(
    struct vm_handoff_syscall_test_context *fixture
)
{
    fixture->bootstrap = (struct micros_bootstrap_control_state){
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
                    .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE,
                    .profile_installed = true,
                    .scheduler_assigned = true,
                },
                {
                    .service_id = MICROS_TTY_SERVICE_ID,
                    .state = MICROS_BOOTSTRAP_SERVICE_PREPARED,
                    .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_RESERVED,
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
                .thread = fixture->launcher_thread,
                .endpoint = fixture->launcher_endpoint,
            },
        },
    };
}

static void reset_vm_pm_tty_state(
    struct vm_handoff_syscall_test_context *fixture
)
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

    fixture->vm_state = (struct micros_vm_handoff_state){0};
    if (micros_vm_handoff_state_prepare(
        &fixture->vm_state,
        TEST_VM_SERVICE_ID,
        fixture->vm_endpoint,
        1,
        TEST_VM_PROFILE_ID,
        fixture->vm_process,
        fixture->vm_thread,
        UINT64_C(0x200000),
        UINT64_C(0x300000),
        MICROS_VM_BOOT_INFO_SIZE,
        &summary
    ) != MICROS_VM_HANDOFF_OK) {
        abort();
    }
    micros_vm_handoff_commit_prevalidated(&fixture->vm_state);
    fixture->pm_state = (struct micros_pm_control_state){
        .service_id = 3,
        .endpoint = UINT32_C(0x00001002),
        .profile_id = MICROS_PRIVILEGE_PROFILE_PM,
        .process = {2, 1},
        .thread = {5, 1},
        .last_transaction = 1,
        .active_transaction = 1,
        .reserved_process = fixture->child_process,
    };
    fixture->tty_state = (struct micros_tty_handoff_runtime_state){
        .service_id = MICROS_TTY_SERVICE_ID,
        .endpoint = fixture->tty_endpoint,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY,
        .process = fixture->tty_process,
        .root_physical_address = UINT64_C(0x200000),
    };
    if (
        micros_tty_handoff_initialize(&fixture->tty_state.handoff)
            != MICROS_TTY_HANDOFF_OK
        || micros_tty_handoff_begin(&fixture->tty_state.handoff, 100, 100)
            != MICROS_TTY_HANDOFF_OK
    ) {
        abort();
    }
    fixture->ownership = (struct micros_frame_ownership){
        .phase = MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF,
    };
}

static void reset_object_records(
    struct vm_handoff_syscall_test_context *fixture
)
{
    fixture->vm_process_record = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = fixture->vm_process.generation,
        .address_space_root = UINT64_C(0x200000),
        .primary_endpoint = fixture->vm_endpoint,
        .privilege_profile = TEST_VM_PROFILE_ID,
    };
    fixture->foreign_process_record = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = fixture->foreign_process.generation,
    };
    fixture->child_process_record = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = fixture->child_process.generation,
        .primary_endpoint = MICROS_PROCESS_ENDPOINT_NONE,
    };
    fixture->vm_thread_record = (struct micros_thread){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = fixture->vm_thread.generation,
        .owner = fixture->vm_process,
    };
    fixture->launcher_thread_record = (struct micros_thread){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = fixture->launcher_thread.generation,
        .owner = fixture->vm_process,
    };
    fixture->foreign_thread_record = (struct micros_thread){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = fixture->foreign_thread.generation,
        .owner = fixture->foreign_process,
    };
    fixture->vm_endpoint_record = (struct micros_endpoint_record){
        .state = MICROS_ENDPOINT_STATE_ACTIVE,
        .owner = fixture->vm_process,
        .value = fixture->vm_endpoint,
    };
    fixture->launcher_endpoint_record = (struct micros_endpoint_record){
        .state = MICROS_ENDPOINT_STATE_ACTIVE,
        .owner = fixture->vm_process,
        .value = fixture->launcher_endpoint,
    };
}

static void reset_injection_state(
    struct vm_handoff_syscall_test_context *fixture
)
{
    fixture->mapping_committed = false;
    fixture->notification_committed = false;
    fixture->notification_should_fail = false;
    fixture->order_violation = false;
    fixture->operation_stage = 0;
    fixture->vm_operation = TEST_VM_OPERATION_NONE;
    fixture->vm_operation_stage = 0;
    fixture->vm_order_violation = false;
    fixture->mutate_freeze_input = false;
    fixture->abort_has_scratch = false;
    fixture->claim_preflight_error = MICROS_FRAME_OWNERSHIP_OK;
    fixture->scratch_mapping_error = MICROS_USER_ADDRESS_SPACE_OK;
    fixture->failure_reason = (enum micros_bootstrap_diagnostic_reason)0;
    for (size_t index = 0; index < sizeof(fixture->user_memory); ++index) {
        fixture->user_memory[index] = UINT8_C(0xa5);
    }
}

void vm_handoff_syscall_test_reset(
    struct vm_handoff_syscall_test_context *fixture
)
{
    reset_fixture_identities(fixture);
    reset_bootstrap_state(fixture);
    reset_vm_pm_tty_state(fixture);
    reset_object_records(fixture);
    reset_injection_state(fixture);
}

struct micros_syscall_arguments vm_handoff_syscall_test_mapping_arguments(
    struct vm_handoff_syscall_test_context *fixture
)
{
    return (struct micros_syscall_arguments){
        .a0 = MICROS_VM_HANDOFF_MAP_TTY_UART,
        .a1 = MICROS_TTY_MAPPING_VERSION,
        .a2 = MICROS_TTY_SERVICE_ID,
        .a3 = fixture->tty_endpoint,
        .a4 = MICROS_TTY_UART_VIRTUAL_BASE,
        .a5 = MICROS_TTY_UART_PHYSICAL_BASE,
        .a6 = (
            (uint64_t)MICROS_TTY_UART_IRQ_SOURCE << 32
        ) | MICROS_TTY_UART_MAPPED_LENGTH,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
}

struct micros_syscall_arguments vm_handoff_syscall_test_load_begin_arguments(void)
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

bool vm_handoff_syscall_test_invoke_vm_command(
    struct vm_handoff_syscall_test_context *fixture,
    const struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_syscall_context context = {
        .objects = &fixture->objects,
        .current = fixture->vm_thread,
        .process = fixture->vm_process,
    };

    return micros_vm_handoff_handle_captured_user_ecall(
        &hart,
        frame,
        &context,
        arguments
    ) == MICROS_SYSCALL_RETURN_NORMAL;
}

bool vm_handoff_syscall_test_invoke_vm_command_panics(
    struct vm_handoff_syscall_test_context *fixture,
    const struct micros_syscall_arguments *arguments,
    struct micros_trap_frame *frame
)
{
    fixture->panic_armed = true;
    if (setjmp(fixture->panic_target) == 0) {
        (void)vm_handoff_syscall_test_invoke_vm_command(fixture, arguments, frame);
        fixture->panic_armed = false;
        return false;
    }
    fixture->panic_armed = false;
    return true;
}

struct micros_syscall_arguments vm_handoff_syscall_test_scratch_map_arguments(
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

struct micros_syscall_arguments vm_handoff_syscall_test_scratch_commit_arguments(
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

bool vm_handoff_syscall_test_begin_load(
    struct vm_handoff_syscall_test_context *fixture,
    struct micros_trap_frame *frame
)
{
    struct micros_syscall_arguments arguments =
        vm_handoff_syscall_test_load_begin_arguments();

    fixture->bootstrap.phase = MICROS_BOOTSTRAP_PHASE_SEALED;
    return (
        vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, frame)
        && frame->a0 == MICROS_SYSCALL_ABI_OK
    );
}

bool vm_handoff_syscall_test_append_load_page(
    struct vm_handoff_syscall_test_context *fixture,
    size_t page_index
)
{
    struct micros_trap_frame frame = {0};
    struct micros_syscall_arguments map =
        vm_handoff_syscall_test_scratch_map_arguments(fixture->vm_state.load.load_token, page_index);
    struct micros_syscall_arguments commit =
        vm_handoff_syscall_test_scratch_commit_arguments(&map);

    fixture->vm_operation = TEST_VM_OPERATION_SCRATCH_MAP;
    fixture->vm_operation_stage = 0;
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &map, &frame)
        || frame.a0 != MICROS_SYSCALL_ABI_OK
        || fixture->vm_operation_stage != 4
        || fixture->vm_order_violation
    ) {
        return false;
    }
    fixture->vm_operation = TEST_VM_OPERATION_SCRATCH_COMMIT;
    fixture->vm_operation_stage = 0;
    frame = (struct micros_trap_frame){0};
    return (
        vm_handoff_syscall_test_invoke_vm_command(fixture, &commit, &frame)
        && frame.a0 == MICROS_SYSCALL_ABI_OK
        && fixture->vm_operation_stage == 2
        && !fixture->vm_order_violation
    );
}

bool vm_handoff_syscall_test_fill_minimum_load(
    struct vm_handoff_syscall_test_context *fixture
)
{
    size_t index;

    for (index = 0; index < 4; ++index) {
        if (!vm_handoff_syscall_test_append_load_page(fixture, index)) {
            return false;
        }
    }
    return true;
}

struct micros_vm_load_freeze_plan *vm_handoff_syscall_test_prepare_freeze_plan(
    struct vm_handoff_syscall_test_context *fixture
)
{
    struct micros_vm_load_freeze_plan *plan =
        (struct micros_vm_load_freeze_plan *)
            &fixture->user_memory[MICROS_SV39_PAGE_SIZE - 40];
    const struct micros_vm_kernel_mapping_record *record =
        &fixture->vm_state.mappings[fixture->child_process.slot];

    *plan = (struct micros_vm_load_freeze_plan){
        .version = MICROS_VM_LOAD_FREEZE_PLAN_VERSION,
        .size = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .load_token = fixture->vm_state.load.load_token,
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

bool vm_handoff_syscall_test_freeze_load(
    struct vm_handoff_syscall_test_context *fixture,
    bool mutate_input,
    uint64_t *mapping_generation
)
{
    struct micros_vm_load_freeze_plan *plan =
        (struct micros_vm_load_freeze_plan *)
            &fixture->user_memory[MICROS_SV39_PAGE_SIZE - 40];
    const struct micros_vm_load_freeze_result *result =
        (const struct micros_vm_load_freeze_result *)
            &fixture->user_memory[2 * MICROS_SV39_PAGE_SIZE - 8];
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

    plan = vm_handoff_syscall_test_prepare_freeze_plan(fixture);
    fixture->vm_operation = TEST_VM_OPERATION_FREEZE;
    fixture->vm_operation_stage = 0;
    fixture->mutate_freeze_input = mutate_input;
    if (
        !vm_handoff_syscall_test_invoke_vm_command(fixture, &arguments, &frame)
        || frame.a0 != MICROS_SYSCALL_ABI_OK
        || fixture->vm_operation_stage != 5
        || fixture->vm_order_violation
        || fixture->vm_state.load.phase != MICROS_VM_LOAD_PHASE_FROZEN
        || fixture->child_process_record.address_space_root
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
        && plan->load_token == fixture->vm_state.load.load_token
    ) {
        return false;
    }
    *mapping_generation = result->mapping_generation;
    return true;
}

bool vm_handoff_syscall_test_invoke_mapping(
    struct vm_handoff_syscall_test_context *fixture,
    struct micros_process_handle process,
    struct micros_thread_handle thread,
    struct micros_trap_frame *frame
)
{
    struct micros_hart hart = {
        .hardware_id = 0,
    };
    struct micros_syscall_context context = {
        .objects = &fixture->objects,
        .current = thread,
        .process = process,
    };
    struct micros_syscall_arguments arguments = vm_handoff_syscall_test_mapping_arguments(fixture);

    return micros_vm_handoff_handle_captured_user_ecall(
        &hart,
        frame,
        &context,
        &arguments
    ) == MICROS_SYSCALL_RETURN_NORMAL;
}
