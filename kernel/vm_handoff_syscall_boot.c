#include "kernel/vm_handoff_syscall_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/platform.h"
#include "kernel/platform/plic.h"
#include "micros/ipc_core.h"
#include "micros/sv39.h"

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
    micros_vm_handoff_syscall_panic(hart, frame, "console-map-gate");
}

enum micros_syscall_return micros_vm_handoff_syscall_handle_ready(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    struct micros_vm_handoff_state *state,
    struct micros_bootstrap_control_state *bootstrap,
    const struct micros_frame_ownership *ownership,
    const struct micros_vm_handoff_request *request
)
{
    if (!micros_vm_handoff_syscall_vm_phase_is_ready(bootstrap, state, ownership)) {
        return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_STATE);
    }
    if (
        micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
        || micros_bootstrap_runtime_validate_vm_prepared()
            != MICROS_BOOTSTRAP_OK
    ) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            state
        );
    }
    if (!micros_vm_handoff_summary_matches(state, request)) {
        micros_vm_handoff_syscall_fail(
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
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_READY_ROLE_GATE,
            state
        );
    }
    micros_vm_handoff_commit_prevalidated(state);
    return micros_vm_handoff_syscall_return_result(frame, MICROS_SYSCALL_ABI_OK);
}

static const struct micros_bootstrap_binding *
require_tty_mapping_runtime(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_vm_handoff_state *vm,
    struct micros_bootstrap_control_state *bootstrap,
    struct micros_endpoint_registry *registry,
    struct micros_tty_handoff_runtime_state *tty
)
{
    const struct micros_bootstrap_binding *launcher;

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
        micros_vm_handoff_syscall_fail(
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
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            vm
        );
    }
    return launcher;
}

static void require_clear_console_events(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_vm_handoff_state *vm,
    const struct micros_endpoint_registry *registry,
    const struct micros_tty_handoff_runtime_state *tty,
    const struct micros_bootstrap_binding *launcher
)
{
    if (!launcher_console_event_state_is_clear(
        launcher,
        registry,
        context->objects
    )) {
        fail_console_map(hart, frame, tty, UINT64_C(0x301));
    }
    if (!vm_console_event_state_is_clear(
        vm,
        registry,
        context->objects
    )) {
        fail_console_map(hart, frame, tty, UINT64_C(0x300));
    }
}

static void prepare_tty_mapping_plans(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    struct micros_endpoint_registry *registry,
    const struct micros_tty_handoff_runtime_state *tty,
    const struct micros_bootstrap_binding *launcher,
    const struct micros_vm_tty_mapping_request *request,
    struct micros_user_address_space_tty_uart_plan *mapping_plan,
    struct micros_tty_handoff *mapped_handoff,
    struct micros_ipc_kernel_notification_plan *notification
)
{
    enum micros_user_address_space_error address_error;
    enum micros_tty_handoff_error tty_error;

    if (!micros_vm_tty_mapping_matches(request, tty->endpoint)) {
        fail_console_map(hart, frame, tty, UINT64_C(1));
    }
    address_error = micros_user_address_space_prepare_tty_uart_mapping(
        tty->process,
        tty->root_physical_address,
        mapping_plan
    );
    if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
        fail_console_map(hart, frame, tty, (uint64_t)address_error);
    }
    tty_error = micros_tty_handoff_runtime_prepare_mapped(
        mapped_handoff
    );
    if (tty_error != MICROS_TTY_HANDOFF_OK) {
        fail_console_map(
            hart,
            frame,
            tty,
            UINT64_C(0x100) | (uint64_t)tty_error
        );
    }
    if (micros_ipc_prepare_kernel_notification(
        registry,
        context->objects,
        launcher->endpoint,
        MICROS_KERNEL_EVENT_CONSOLE_MAPPED,
        notification
    ) != MICROS_IPC_OK) {
        fail_console_map(hart, frame, tty, UINT64_C(0x200));
    }
}

enum micros_syscall_return micros_vm_handoff_syscall_handle_tty_mapping(
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
    enum tty_map_phase_result phase;

    micros_vm_handoff_syscall_clear_bytes(
        &mapping_plan,
        sizeof(mapping_plan)
    );
    micros_vm_handoff_syscall_clear_bytes(
        &notification,
        sizeof(notification)
    );
    phase = tty_map_phase(bootstrap, vm, tty, ownership);
    if (phase == TTY_MAP_PHASE_STATE) {
        return micros_vm_handoff_syscall_return_result(
            frame,
            MICROS_SYSCALL_ABI_STATE
        );
    }
    if (phase != TTY_MAP_PHASE_READY) {
        micros_vm_handoff_syscall_fail(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_AUTHORITY,
            vm
        );
    }
    launcher = require_tty_mapping_runtime(
        hart,
        frame,
        context,
        vm,
        bootstrap,
        registry,
        tty
    );
    require_clear_console_events(
        hart,
        frame,
        context,
        vm,
        registry,
        tty,
        launcher
    );
    prepare_tty_mapping_plans(
        hart,
        frame,
        context,
        registry,
        tty,
        launcher,
        request,
        &mapping_plan,
        &mapped_handoff,
        &notification
    );
    micros_user_address_space_commit_tty_uart_mapping(&mapping_plan);
    micros_tty_handoff_runtime_commit_mapped_prevalidated(
        &mapped_handoff
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        registry,
        context->objects,
        &notification
    );
    return micros_vm_handoff_syscall_return_result(
        frame,
        MICROS_SYSCALL_ABI_OK
    );
}
