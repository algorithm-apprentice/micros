#include "kernel/vm_handoff_syscall.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/bootstrap_runtime.h"
#include "kernel/endpoint_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/plic.h"
#include "kernel/tty_handoff_runtime.h"
#include "kernel/user_address_space_internal.h"
#include "kernel/vm_handoff_core.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/ipc_core.h"
#include "micros/panic.h"
#include "micros/user_address_space.h"

static void clear_bytes(void *storage, size_t size)
{
    uint8_t *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static _Noreturn void panic_vm_handoff(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const char *reason
)
{
    MICROS_TRAP_PANIC(
        hart == NULL ? 0 : hart->hardware_id,
        reason,
        frame
    );
}

static enum micros_syscall_return return_result(
    struct micros_trap_frame *frame,
    enum micros_syscall_abi_result result
)
{
    frame->a0 = (uint64_t)(int64_t)result;
    return MICROS_SYSCALL_RETURN_NORMAL;
}

static _Noreturn void fail_vm_handoff(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_bootstrap_diagnostic_reason reason,
    const struct micros_vm_handoff_state *state
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();

    if (
        state != NULL
        && bootstrap != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
    ) {
        micros_bootstrap_runtime_fail(
            reason,
            state->service_id,
            state->endpoint,
            0
        );
    }
    panic_vm_handoff(hart, frame, "vm-handoff-invariant");
}

static bool vm_is_authorized(
    const struct micros_vm_handoff_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    const struct micros_syscall_context *context
)
{
    const struct micros_process *process;
    const struct micros_endpoint_record *endpoint;

    return (
        state != NULL
        && registry != NULL
        && objects != NULL
        && context->process.slot == state->process.slot
        && context->process.generation == state->process.generation
        && context->current.slot == state->thread.slot
        && context->current.generation == state->thread.generation
        && micros_process_resolve(
            objects,
            context->process,
            &process
        ) == MICROS_KERNEL_OBJECT_OK
        && process->primary_endpoint == state->endpoint
        && process->privilege_profile == state->profile_id
        && micros_endpoint_resolve_active(
            registry,
            objects,
            state->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->owner.slot == context->process.slot
        && endpoint->owner.generation
            == context->process.generation
        && micros_privilege_profile_allows_kernel_operation(
            registry,
            (uint8_t)state->profile_id,
            1
        ) == MICROS_ENDPOINT_OK
    );
}

static bool vm_phase_is_ready(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *state,
    const struct micros_frame_ownership *ownership
)
{
    size_t index;

    if (
        bootstrap == NULL
        || state == NULL
        || ownership == NULL
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || bootstrap->transitions.starting_service_id
            != state->service_id
        || state->phase != MICROS_VM_HANDOFF_PHASE_PREPARED
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_BOOTSTRAP
    ) {
        return false;
    }
    for (
        index = 0;
        index < bootstrap->transitions.entry_count;
        ++index
    ) {
        const struct micros_bootstrap_runtime_entry *entry =
            &bootstrap->transitions.entries[index];

        if (entry->service_id == state->service_id) {
            return entry->state
                == MICROS_BOOTSTRAP_SERVICE_STARTING;
        }
    }
    return false;
}

enum tty_map_phase_result {
    TTY_MAP_PHASE_READY = 0,
    TTY_MAP_PHASE_STATE,
    TTY_MAP_PHASE_INVARIANT,
};

static const struct micros_bootstrap_runtime_entry *
runtime_entry(
    const struct micros_bootstrap_runtime *runtime,
    uint32_t service_id
)
{
    size_t index;

    if (
        runtime == NULL
        || runtime->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        return NULL;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].service_id == service_id) {
            return &runtime->entries[index];
        }
    }
    return NULL;
}

static enum tty_map_phase_result tty_map_phase(
    const struct micros_bootstrap_control_state *bootstrap,
    const struct micros_vm_handoff_state *vm,
    const struct micros_tty_handoff_runtime_state *tty,
    const struct micros_frame_ownership *ownership
)
{
    const struct micros_bootstrap_runtime_entry *launcher_entry;
    const struct micros_bootstrap_runtime_entry *tty_entry;

    if (
        bootstrap == NULL
        || vm == NULL
        || tty == NULL
        || ownership == NULL
    ) {
        return TTY_MAP_PHASE_STATE;
    }
    if (
        bootstrap->transitions.entry_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || bootstrap->transitions.next_order_index
            > bootstrap->transitions.entry_count
        || micros_tty_handoff_validate(&tty->handoff)
            != MICROS_TTY_HANDOFF_OK
        || tty->service_id != MICROS_TTY_SERVICE_ID
    ) {
        return TTY_MAP_PHASE_INVARIANT;
    }
    if (
        vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        || bootstrap->phase != MICROS_BOOTSTRAP_PHASE_RUNNING
        || tty->handoff.console_phase
            != MICROS_TTY_CONSOLE_MAP_REQUESTED
        || tty->handoff.route_phase != MICROS_TTY_ROUTE_DISABLED
        || bootstrap->transitions.starting_service_id != 0
        || bootstrap->transitions.next_order_index
            >= bootstrap->transitions.entry_count
        || bootstrap->transitions.ordered_service_ids[
            bootstrap->transitions.next_order_index
        ] != tty->service_id
    ) {
        return TTY_MAP_PHASE_STATE;
    }
    launcher_entry = runtime_entry(
        &bootstrap->transitions,
        bootstrap->plan.controller_service_id
    );
    tty_entry = runtime_entry(
        &bootstrap->transitions,
        tty->service_id
    );
    if (launcher_entry == NULL || tty_entry == NULL) {
        return TTY_MAP_PHASE_INVARIANT;
    }
    if (
        launcher_entry->state != MICROS_BOOTSTRAP_SERVICE_READY
        || launcher_entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        || !launcher_entry->profile_installed
        || !launcher_entry->scheduler_assigned
        || tty_entry->state != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || tty_entry->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || tty_entry->profile_installed
        || tty_entry->scheduler_assigned
        || tty_entry->ready_deadline != 0
    ) {
        return TTY_MAP_PHASE_STATE;
    }
    return TTY_MAP_PHASE_READY;
}

static bool vm_console_event_state_is_clear(
    const struct micros_vm_handoff_state *vm,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

    return (
        vm != NULL
        && micros_endpoint_resolve_active(
            registry,
            objects,
            vm->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_thread_resolve(
            objects,
            vm->thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && endpoint->pending_kernel_events == 0
        && micros_thread_ipc_state_is_clear(thread)
    );
}

static bool launcher_console_event_state_is_clear(
    const struct micros_bootstrap_binding *launcher,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

    return (
        launcher != NULL
        && micros_endpoint_resolve_active(
            registry,
            objects,
            launcher->endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->pending_kernel_events == 0
        && micros_thread_resolve(
            objects,
            launcher->thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && !micros_ipc_thread_has_staged_kernel_notification(
            thread
        )
    );
}

static _Noreturn void fail_console_map(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_tty_handoff_runtime_state *tty,
    uint64_t detail
)
{
    const struct micros_bootstrap_control_state *bootstrap =
        micros_bootstrap_runtime_state();

    if (
        tty != NULL
        && bootstrap != NULL
        && bootstrap->phase == MICROS_BOOTSTRAP_PHASE_RUNNING
    ) {
        micros_bootstrap_runtime_fail(
            MICROS_BOOTSTRAP_DIAGNOSTIC_CONSOLE_MAP_GATE,
            tty->service_id,
            tty->endpoint,
            detail
        );
    }
    panic_vm_handoff(hart, frame, "console-map-gate");
}

static enum micros_syscall_return handle_ready_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    struct micros_bootstrap_control_state *bootstrap,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_request *request
)
{
    if (!vm_phase_is_ready(bootstrap, state, ownership)) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_bootstrap_runtime_validate_vm_prepared()
            != MICROS_BOOTSTRAP_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (!micros_vm_handoff_summary_matches(state, request)) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            state
        );
    }
    if (
        micros_user_address_space_complete_wired_handoff()
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            state
        );
    }
    micros_vm_handoff_commit_prevalidated(state);
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static enum micros_syscall_return handle_tty_mapping_command(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *vm,
    struct micros_bootstrap_control_state *bootstrap,
    struct micros_endpoint_registry *registry,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_tty_mapping_request *request
)
{
    struct micros_tty_handoff_runtime_state *tty =
        micros_tty_handoff_runtime_authoritative_state();
    const struct micros_bootstrap_binding *launcher;
    struct micros_user_address_space_tty_uart_plan mapping_plan;
    struct micros_tty_handoff mapped_handoff;
    struct micros_ipc_kernel_notification_plan notification;
    enum micros_user_address_space_error address_error;
    enum micros_tty_handoff_error tty_error;
    enum tty_map_phase_result phase;

    clear_bytes(&mapping_plan, sizeof(mapping_plan));
    clear_bytes(&notification, sizeof(notification));
    phase = tty_map_phase(bootstrap, vm, tty, ownership);
    if (phase == TTY_MAP_PHASE_STATE) {
        return return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (phase != TTY_MAP_PHASE_READY) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            vm
        );
    }
    if (
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_vm_handoff_runtime_validate(
            bootstrap,
            registry,
            context->objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_tty_handoff_runtime_validate(
            bootstrap,
            registry,
            context->objects
        ) != MICROS_TTY_HANDOFF_OK
        || !micros_plic_validate(MICROS_PLIC_DISABLED)
        || !uart_console_handoff_is_quiesced()
        || micros_user_address_space_validate(tty->process)
            != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            vm
        );
    }
    launcher = micros_bootstrap_control_find_binding(
        bootstrap,
        bootstrap->plan.controller_service_id
    );
    if (launcher == NULL) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            vm
        );
    }
    if (
        !launcher_console_event_state_is_clear(
            launcher,
            registry,
            context->objects
        )
    ) {
        fail_console_map(hart, frame, tty, UINT64_C(0x301));
    }
    if (
        !vm_console_event_state_is_clear(
            vm,
            registry,
            context->objects
        )
    ) {
        fail_console_map(hart, frame, tty, UINT64_C(0x300));
    }
    if (!micros_vm_tty_mapping_matches(request, tty->endpoint)) {
        fail_console_map(hart, frame, tty, UINT64_C(1));
    }
    address_error =
        micros_user_address_space_prepare_tty_uart_mapping(
            tty->process,
            tty->root_physical_address,
            &mapping_plan
        );
    if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
        fail_console_map(
            hart,
            frame,
            tty,
            (uint64_t)address_error
        );
    }
    tty_error = micros_tty_handoff_runtime_prepare_mapped(
        &mapped_handoff
    );
    if (tty_error != MICROS_TTY_HANDOFF_OK) {
        fail_console_map(
            hart,
            frame,
            tty,
            UINT64_C(0x100) | (uint64_t)tty_error
        );
    }
    if (
        micros_ipc_prepare_kernel_notification(
            registry,
            context->objects,
            launcher->endpoint,
            MICROS_KERNEL_EVENT_CONSOLE_MAPPED,
            &notification
        ) != MICROS_IPC_OK
    ) {
        fail_console_map(hart, frame, tty, UINT64_C(0x200));
    }

    micros_user_address_space_commit_tty_uart_mapping(
        &mapping_plan
    );
    micros_tty_handoff_runtime_commit_mapped_prevalidated(
        &mapped_handoff
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        registry,
        context->objects,
        &notification
    );
    return return_result(frame, MICROS_SYSCALL_ABI_OK);
}

enum micros_syscall_return
micros_vm_handoff_handle_captured_user_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_syscall_arguments *arguments
)
{
    struct micros_vm_handoff_request ready_request;
    struct micros_vm_tty_mapping_request mapping_request;
    struct micros_vm_handoff_state *state;
    struct micros_bootstrap_control_state *bootstrap;
    struct micros_endpoint_registry *registry;
    const struct micros_frame_ownership *ownership;
    const struct micros_thread *current_thread;
    uint32_t command;

    if (
        hart == NULL
        || frame == NULL
        || context == NULL
        || arguments == NULL
        || context->objects == NULL
    ) {
        panic_vm_handoff(hart, frame, "vm-handoff-argument");
    }
    if (
        micros_vm_handoff_decode_command(arguments, &command)
            != MICROS_SYSCALL_ABI_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    if (
        command == MICROS_VM_HANDOFF_READY
            ? micros_vm_handoff_decode(
                arguments,
                &ready_request
            ) != MICROS_SYSCALL_ABI_OK
            : micros_vm_handoff_decode_tty_mapping(
                arguments,
                &mapping_request
            ) != MICROS_SYSCALL_ABI_OK
    ) {
        return return_result(frame, MICROS_SYSCALL_ABI_ARGUMENT);
    }
    if (
        micros_thread_resolve(
            context->objects,
            context->current,
            &current_thread
        ) != MICROS_KERNEL_OBJECT_OK
        || current_thread->owner.slot != context->process.slot
        || current_thread->owner.generation
            != context->process.generation
    ) {
        panic_vm_handoff(hart, frame, "vm-handoff-current");
    }
    state = micros_vm_handoff_runtime_authoritative_state();
    bootstrap = micros_bootstrap_runtime_authoritative_state();
    registry = micros_ipc_runtime_authoritative_registry();
    if (
        !vm_is_authorized(
            state,
            registry,
            context->objects,
            context
        )
    ) {
        return return_result(
            frame,
            MICROS_SYSCALL_ABI_UNAUTHORIZED
        );
    }
    ownership = micros_frame_ownership_runtime_ledger();
    if (ownership == NULL) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (
        bootstrap != NULL
        && bootstrap->transitions.entry_count
            > MICROS_BOOTSTRAP_SERVICE_CAPACITY
    ) {
        fail_vm_handoff(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (command == MICROS_VM_HANDOFF_READY) {
        return handle_ready_command(
            hart,
            frame,
            state,
            bootstrap,
            ownership,
            &ready_request
        );
    }
    return handle_tty_mapping_command(
        hart,
        frame,
        context,
        state,
        bootstrap,
        registry,
        ownership,
        &mapping_request
    );
}
