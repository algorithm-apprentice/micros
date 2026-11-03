#include "kernel/diagnostic/diagnostic_history.h"

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/platform.h"
#include "kernel/boot/bootstrap_runtime.h"
#include "kernel/diagnostic/diagnostic_control_core.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "kernel/trap/syscall.h"
#include "kernel/tty/tty_control_core.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/ipc_core.h"
#include "micros/panic.h"
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
#include "tests/qemu/diagnostic_history_kernel.h"
#endif

static struct micros_diagnostic_reader diagnostic_reader;

static _Noreturn void panic_control(struct micros_hart *hart, struct micros_trap_frame *frame)
{
    MICROS_TRAP_PANIC(hart == NULL ? 0 : hart->hardware_id, "diagnostic-control-invariant", frame);
}

static struct micros_diagnostic_identity reader_identity(
    const struct micros_tty_handoff_runtime_state *state
)
{
    return (struct micros_diagnostic_identity){
        .service_id = state->service_id, .process = state->process, .thread = state->thread,
        .endpoint = state->endpoint, .profile_id = state->profile_id,
        .root_physical_address = state->root_physical_address,
    };
}

static bool registered_lifetime_valid(
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap
)
{
    const struct micros_bootstrap_runtime_entry *transition = NULL;
    size_t index;
    uint32_t handoff_cursor;

    if (
        state->handoff.console_phase != MICROS_TTY_CONSOLE_OWNED
        || !uart_console_handoff_is_active() || !riscv_external_interrupt_is_enabled()
        || !micros_diagnostic_handoff_cursor(&handoff_cursor)
        || bootstrap->transitions.phase != bootstrap->phase
        || bootstrap->entry_count == 0 || bootstrap->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || bootstrap->transitions.entry_count != bootstrap->entry_count
    ) {
        return false;
    }
    for (index = 0; index < bootstrap->transitions.entry_count; ++index) {
        if (bootstrap->transitions.entries[index].service_id == state->service_id) {
            if (transition != NULL) {
                return false;
            }
            transition = &bootstrap->transitions.entries[index];
        }
    }
    if (
        transition == NULL || transition->endpoint_state != MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        || !transition->profile_installed || !transition->scheduler_assigned
    ) {
        return false;
    }
    if (transition->state == MICROS_BOOTSTRAP_SERVICE_STARTING) {
        return bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
            && state->handoff.deadline_armed
            && transition->ready_deadline == state->handoff.deadline;
    }
    return transition->state == MICROS_BOOTSTRAP_SERVICE_READY
        && !state->handoff.deadline_armed && state->handoff.deadline == 0
        && transition->ready_deadline == 0;
}

static void require_reader_state(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap
)
{
    const struct micros_vm_handoff_state *vm = micros_vm_handoff_runtime_state();
    struct micros_diagnostic_identity identity;

    if (
        state == NULL || bootstrap == NULL || vm == NULL || riscv_irq_is_enabled()
        || (bootstrap->phase != MICROS_BOOTSTRAP_PHASE_RUNNING && bootstrap->phase != MICROS_BOOTSTRAP_PHASE_SEALED)
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || micros_tty_handoff_validate(&state->handoff) != MICROS_TTY_HANDOFF_OK
        || !micros_diagnostic_history_valid(micros_diagnostic_history_state())
        || !micros_diagnostic_reader_valid(&diagnostic_reader)
    ) {
        panic_control(hart, frame);
    }
    identity = reader_identity(state);
    if (diagnostic_reader.registered && (
        !micros_diagnostic_reader_matches(&diagnostic_reader, &identity)
        || !registered_lifetime_valid(state, bootstrap)
    )) {
        panic_control(hart, frame);
    }
}

static uint64_t control_result(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_diagnostic_control_result result
)
{
    switch (result) {
    case MICROS_DIAGNOSTIC_CONTROL_OK:
        return MICROS_SYSCALL_ABI_OK;
    case MICROS_DIAGNOSTIC_CONTROL_ARGUMENT:
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
    case MICROS_DIAGNOSTIC_CONTROL_STATE:
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE;
    case MICROS_DIAGNOSTIC_CONTROL_MEMORY_FAULT:
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_MEMORY_FAULT;
    case MICROS_DIAGNOSTIC_CONTROL_INVARIANT:
    default:
        panic_control(hart, frame);
    }
}

static uint64_t register_reader(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_tty_control_request *request,
    const struct micros_diagnostic_identity *identity
)
{
    uint32_t cursor;
    enum micros_diagnostic_control_result result;

    if (diagnostic_reader.registered) {
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE;
    }
    if (
        request->version != MICROS_TTY_CONTROL_VERSION
        || request->service_id != identity->service_id || request->endpoint != identity->endpoint
    ) {
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (!micros_diagnostic_handoff_cursor(&cursor)) {
        panic_control(hart, frame);
    }
    result = micros_diagnostic_register(
        &diagnostic_reader, identity, request->virtual_base, request->mapped_length
    );
    if (result != MICROS_DIAGNOSTIC_CONTROL_OK) {
        return control_result(hart, frame, result);
    }
    if (micros_diagnostic_history_state()->size != 0) {
        micros_diagnostic_record_end();
    }
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    micros_diagnostic_test_registered(cursor);
#endif
    return cursor;
}

static uint64_t read_history(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_tty_control_request *request,
    const struct micros_diagnostic_identity *identity
)
{
    const struct micros_diagnostic_history *history = micros_diagnostic_history_state();
    struct micros_diagnostic_span span;
    enum micros_diagnostic_control_result result;

    if (!diagnostic_reader.registered) {
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE;
    }
    if (request->version != MICROS_TTY_CONTROL_VERSION) {
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
    }
    result = micros_diagnostic_prepare_read(
        &diagnostic_reader, identity, history, request->diagnostic_cursor, &span
    );
    if (result != MICROS_DIAGNOSTIC_CONTROL_OK) {
        return control_result(hart, frame, result);
    }
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    if (!micros_diagnostic_test_before_read(request->diagnostic_cursor)) {
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE;
    }
    /* The admitted fixture appends only after the original request preflight. */
    if (micros_diagnostic_prepare_read(
        &diagnostic_reader, identity, history, request->diagnostic_cursor, &span
    ) != MICROS_DIAGNOSTIC_CONTROL_OK) {
        panic_control(hart, frame);
    }
#endif
    micros_diagnostic_copy_prevalidated(&diagnostic_reader, history, &span);
#ifdef MICROS_BUILD_TTY_SERVICE_TEST
    micros_diagnostic_test_after_read();
#endif
    return MICROS_SYSCALL_ABI_OK;
}

uint64_t micros_diagnostic_control_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_tty_control_request *request,
    const struct micros_tty_handoff_runtime_state *state,
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_endpoint_registry *registry
)
{
    struct micros_diagnostic_identity identity;

    if (context == NULL || context->objects == NULL || request == NULL || registry == NULL) {
        panic_control(hart, frame);
    }
    require_reader_state(hart, frame, state, bootstrap);
    if (request->command == MICROS_TTY_CONTROL_DIAGNOSTIC_REGISTER && (
        micros_bootstrap_runtime_validate() != MICROS_BOOTSTRAP_OK
        || micros_tty_handoff_runtime_validate(bootstrap, registry, context->objects) != MICROS_TTY_HANDOFF_OK
        || micros_user_address_space_validate_tty_uart_mapping(state->process, state->root_physical_address)
            != MICROS_USER_ADDRESS_SPACE_OK
    )) {
        panic_control(hart, frame);
    }
    if (state->handoff.console_phase != MICROS_TTY_CONSOLE_OWNED) {
        return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_STATE;
    }
    if (!uart_console_handoff_is_active() || !riscv_external_interrupt_is_enabled()) {
        panic_control(hart, frame);
    }
    identity = reader_identity(state);
    if (request->command == MICROS_TTY_CONTROL_DIAGNOSTIC_REGISTER) {
        return register_reader(hart, frame, request, &identity);
    }
    if (request->command == MICROS_TTY_CONTROL_DIAGNOSTIC_READ) {
        return read_history(hart, frame, request, &identity);
    }
    return (uint64_t)(int64_t)MICROS_SYSCALL_ABI_ARGUMENT;
}

void micros_diagnostic_notify_pending(struct micros_hart *hart, struct micros_trap_frame *frame)
{
    const struct micros_tty_handoff_runtime_state *state;
    const struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    struct micros_kernel_objects *objects;
    struct micros_ipc_kernel_notification_plan plan;

    if (!diagnostic_reader.registered || !micros_diagnostic_notification_pending()) {
        return;
    }
    state = micros_tty_handoff_runtime_state();
    bootstrap = micros_bootstrap_runtime_state();
    registry = micros_ipc_runtime_authoritative_registry();
    objects = micros_kernel_object_runtime_authoritative_registry();
    require_reader_state(hart, frame, state, bootstrap);
    if (
        registry == NULL || objects == NULL
        || micros_tty_handoff_runtime_validate(bootstrap, registry, objects) != MICROS_TTY_HANDOFF_OK
        || micros_ipc_prepare_kernel_notification(
            registry, objects, state->endpoint, MICROS_KERNEL_EVENT_DIAGNOSTIC, &plan
        ) != MICROS_IPC_OK
    ) {
        panic_control(hart, frame);
    }
    micros_ipc_commit_kernel_notification_prevalidated(registry, objects, &plan);
    micros_diagnostic_notification_committed();
}
