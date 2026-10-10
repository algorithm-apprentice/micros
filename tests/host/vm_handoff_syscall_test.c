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
    .slot = 2,
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
static const micros_endpoint_t launcher_endpoint = UINT32_C(0x00010001);
static const micros_endpoint_t vm_endpoint = UINT32_C(0x00010002);
static const micros_endpoint_t tty_endpoint = UINT32_C(0x00010004);

static struct micros_kernel_objects objects;
static struct micros_endpoint_registry registry;
static struct micros_process vm_process_record;
static struct micros_process foreign_process_record;
static struct micros_thread vm_thread_record;
static struct micros_thread launcher_thread_record;
static struct micros_thread foreign_thread_record;
static struct micros_endpoint_record launcher_endpoint_record;
static struct micros_endpoint_record vm_endpoint_record;
static struct micros_bootstrap_control_state bootstrap;
static struct micros_vm_handoff_state vm_state;
static struct micros_tty_handoff_runtime_state tty_state;
static struct micros_frame_ownership ownership;
static jmp_buf panic_target;
static bool panic_armed;
static bool mapping_committed;
static bool notification_committed;
static bool notification_should_fail;
static bool order_violation;
static int operation_stage;
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
    vm_state = (struct micros_vm_handoff_state){
        .phase = MICROS_VM_HANDOFF_PHASE_HANDED_OFF,
        .service_id = TEST_VM_SERVICE_ID,
        .endpoint = vm_endpoint,
        .profile_id = TEST_VM_PROFILE_ID,
        .process = vm_process,
        .thread = vm_thread,
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
        .primary_endpoint = vm_endpoint,
        .privilege_profile = TEST_VM_PROFILE_ID,
    };
    foreign_process_record = (struct micros_process){
        .slot_state = MICROS_KERNEL_OBJECT_SLOT_LIVE,
        .generation = foreign_process.generation,
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
    failure_reason = (enum micros_bootstrap_diagnostic_reason)0;
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
        test_successful_mapping()
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
    return process_handles_equal(process, tty_process)
        ? MICROS_USER_ADDRESS_SPACE_OK
        : MICROS_USER_ADDRESS_SPACE_ERROR_INVARIANT;
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
