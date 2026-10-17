#include "kernel/bootstrap_syscall_internal.h"

#include <stddef.h>

#include "arch/riscv64/platform.h"
#include "kernel/endpoint/endpoint_internal.h"
#include "kernel/ipc/ipc_runtime_internal.h"
#include "kernel/platform/plic.h"
#include "kernel/tty/tty_handoff_runtime.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"

static const struct micros_bootstrap_runtime_entry *
find_runtime_entry(
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

static bool service_is_ready(
    const struct micros_bootstrap_control_state *state,
    uint32_t service_id
)
{
    const struct micros_bootstrap_runtime_entry *entry =
        find_runtime_entry(&state->transitions, service_id);

    return (
        service_id != 0
        && entry != NULL
        && entry->state == MICROS_BOOTSTRAP_SERVICE_READY
        && entry->endpoint_state
            == MICROS_BOOTSTRAP_ENDPOINT_ACTIVE
        && entry->profile_installed
        && entry->scheduler_assigned
        && entry->ready_deadline == 0
    );
}

static bool no_bootstrap_deadline_is_armed(
    const struct micros_bootstrap_runtime *runtime
)
{
    size_t index;

    if (
        runtime == NULL
        || runtime->entry_count > MICROS_BOOTSTRAP_SERVICE_CAPACITY
        || runtime->starting_service_id != 0
    ) {
        return false;
    }
    for (index = 0; index < runtime->entry_count; ++index) {
        if (runtime->entries[index].ready_deadline != 0) {
            return false;
        }
    }
    return true;
}

static bool console_event_state_is_clear(
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

    return (
        micros_endpoint_resolve_active(
            registry,
            objects,
            state->controller_endpoint,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && micros_thread_resolve(
            objects,
            state->controller_thread,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && endpoint->pending_kernel_events == 0
        && micros_thread_ipc_state_is_clear(thread)
    );
}

static bool kernel_notification_destination_is_clear(
    const struct micros_endpoint_registry *registry,
    const struct micros_kernel_objects *objects,
    micros_endpoint_t endpoint_value,
    struct micros_thread_handle thread_handle
)
{
    const struct micros_endpoint_record *endpoint;
    const struct micros_thread *thread;

    return (
        micros_endpoint_resolve_active(
            registry,
            objects,
            endpoint_value,
            &endpoint
        ) == MICROS_ENDPOINT_OK
        && endpoint->pending_kernel_events == 0
        && micros_thread_resolve(
            objects,
            thread_handle,
            &thread
        ) == MICROS_KERNEL_OBJECT_OK
        && !micros_ipc_thread_has_staged_kernel_notification(
            thread
        )
    );
}

static enum micros_syscall_abi_result prepare_console_begin_identity(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_bootstrap_control_request *request,
    const struct micros_bootstrap_control_state *state,
    const struct micros_tty_handoff_runtime_state **tty_result,
    const struct micros_bootstrap_manifest_entry **entry_result,
    const struct micros_bootstrap_binding **vm_binding_result,
    uint64_t *now
)
{
    const struct micros_frame_ownership *ownership =
        micros_frame_ownership_runtime_ledger();
    const struct micros_vm_handoff_state *vm =
        micros_vm_handoff_runtime_state();
    const struct micros_tty_handoff_runtime_state *tty =
        micros_tty_handoff_runtime_state();
    const struct micros_bootstrap_binding *tty_binding;
    const struct micros_bootstrap_binding *vm_binding;
    const struct micros_bootstrap_manifest_entry *tty_entry;
    const struct micros_bootstrap_runtime_entry *tty_transition;

    *now = riscv_read_time();
    if (
        request->service_id != state->plan.console_service_id
        || request->service_id != MICROS_TTY_SERVICE_ID
    ) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    tty_binding = micros_bootstrap_control_find_binding(
        state,
        request->service_id
    );
    vm_binding = micros_bootstrap_control_find_binding(
        state,
        state->plan.vm_service_id
    );
    tty_transition = find_runtime_entry(
        &state->transitions,
        request->service_id
    );
    if (
        tty == NULL
        || tty_binding == NULL
        || vm_binding == NULL
        || tty_binding->manifest_index >= state->entry_count
        || ownership == NULL
        || vm == NULL
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    tty_entry =
        &state->manifest.entries[tty_binding->manifest_index];
    if (
        ownership->phase
            != MICROS_FRAME_OWNERSHIP_PHASE_HANDED_OFF
        || vm->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF
        || !service_is_ready(state, state->plan.vm_service_id)
        || !service_is_ready(state, state->plan.pm_service_id)
        || state->transitions.next_order_index
            >= state->transitions.entry_count
        || state->transitions.ordered_service_ids[
            state->transitions.next_order_index
        ] != request->service_id
        || tty_transition == NULL
        || tty_transition->state
            != MICROS_BOOTSTRAP_SERVICE_PREPARED
        || tty_transition->endpoint_state
            != MICROS_BOOTSTRAP_ENDPOINT_RESERVED
        || tty_transition->profile_installed
        || tty_transition->scheduler_assigned
        || tty_transition->ready_deadline != 0
        || tty_entry->stack_page_count != 1
        || tty_entry->ready_timeout_counter_ticks == 0
        || tty->handoff.console_phase
            != MICROS_TTY_CONSOLE_EARLY
        || tty->handoff.route_phase
            != MICROS_TTY_ROUTE_DISABLED
        || tty->handoff.deadline_armed
        || !no_bootstrap_deadline_is_armed(
            &state->transitions
        )
    ) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    *tty_result = tty;
    *entry_result = tty_entry;
    *vm_binding_result = vm_binding;
    return MICROS_SYSCALL_ABI_OK;
}

static void validate_console_begin_runtime(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    const struct micros_bootstrap_control_state *state,
    const struct micros_endpoint_registry *registry
)
{
    if (
        micros_vm_handoff_runtime_validate(
            state,
            registry,
            context->objects
        ) != MICROS_VM_HANDOFF_OK
        || micros_tty_handoff_runtime_validate(
            state,
            registry,
            context->objects
        ) != MICROS_TTY_HANDOFF_OK
        || micros_frame_ownership_runtime_validate(
            context->objects
        ) != MICROS_FRAME_OWNERSHIP_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
}

static enum micros_syscall_abi_result
prepare_console_begin_mapping_deadline(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_bootstrap_control_request *request,
    const struct micros_tty_handoff_runtime_state *tty,
    const struct micros_bootstrap_manifest_entry *tty_entry,
    uint64_t now,
    struct micros_tty_handoff *begin
)
{
    struct micros_user_address_space_tty_uart_plan mapping_plan;
    enum micros_user_address_space_error address_error;
    enum micros_tty_handoff_error tty_error;

    address_error =
        micros_user_address_space_prepare_tty_uart_mapping(
            tty->process,
            tty->root_physical_address,
            &mapping_plan
        );
    if (
        address_error == MICROS_USER_ADDRESS_SPACE_ERROR_NOT_MAPPED
        || address_error == MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT
        || address_error == MICROS_USER_ADDRESS_SPACE_ERROR_STATE
    ) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (address_error != MICROS_USER_ADDRESS_SPACE_OK) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    tty_error = micros_tty_handoff_runtime_prepare_begin(
        now,
        tty_entry->ready_timeout_counter_ticks,
        begin
    );
    if (tty_error == MICROS_TTY_HANDOFF_ERROR_RANGE) {
        return MICROS_SYSCALL_ABI_RANGE;
    }
    if (tty_error == MICROS_TTY_HANDOFF_ERROR_STATE) {
        return MICROS_SYSCALL_ABI_STATE;
    }
    if (tty_error != MICROS_TTY_HANDOFF_OK) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    return MICROS_SYSCALL_ABI_OK;
}

static void prepare_console_begin_notification(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    const struct micros_endpoint_registry *registry,
    const struct micros_bootstrap_binding *vm_binding,
    struct micros_ipc_kernel_notification_plan *notification
)
{
    enum micros_ipc_error ipc_error;

    if (
        !uart_console_begin_preflight()
        || !micros_plic_validate(MICROS_PLIC_DISABLED)
        || !kernel_notification_destination_is_clear(
            registry,
            context->objects,
            vm_binding->endpoint,
            vm_binding->thread
        )
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    ipc_error = micros_ipc_prepare_kernel_notification(
        registry,
        context->objects,
        vm_binding->endpoint,
        MICROS_KERNEL_EVENT_CONSOLE_MAP_REQUEST,
        notification
    );
    if (ipc_error != MICROS_IPC_OK) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
}

enum micros_syscall_return bootstrap_syscall_handle_console_begin(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
)
{
    struct micros_ipc_kernel_notification_plan notification;
    struct micros_tty_handoff begin;
    const struct micros_tty_handoff_runtime_state *tty;
    const struct micros_bootstrap_manifest_entry *tty_entry;
    const struct micros_bootstrap_binding *vm_binding;
    enum micros_syscall_abi_result result;
    uint64_t now;

    result = prepare_console_begin_identity(
        hart,
        frame,
        request,
        state,
        &tty,
        &tty_entry,
        &vm_binding,
        &now
    );
    if (result != MICROS_SYSCALL_ABI_OK) {
        return bootstrap_syscall_return_result(frame, result);
    }
    validate_console_begin_runtime(
        hart,
        frame,
        context,
        request,
        state,
        registry
    );
    result = prepare_console_begin_mapping_deadline(
        hart,
        frame,
        request,
        tty,
        tty_entry,
        now,
        &begin
    );
    if (result != MICROS_SYSCALL_ABI_OK) {
        return bootstrap_syscall_return_result(frame, result);
    }
    prepare_console_begin_notification(
        hart,
        frame,
        context,
        request,
        registry,
        vm_binding,
        &notification
    );

    uart_console_begin_quiesce_prevalidated();
    micros_tty_handoff_runtime_commit_begin_deadline_prevalidated(
        &begin
    );
    uart_console_handoff_commit_prevalidated();
    micros_tty_handoff_runtime_commit_begin_phase_prevalidated(
        &begin
    );
    micros_ipc_commit_kernel_notification_prevalidated(
        registry,
        context->objects,
        &notification
    );
    if (
        !uart_console_handoff_is_quiesced()
        || micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            request->service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    return bootstrap_syscall_return_result(
        frame,
        MICROS_SYSCALL_ABI_OK
    );
}

static enum micros_syscall_return finish_release(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    uint32_t service_id,
    enum micros_bootstrap_error error
)
{
    if (
        error != MICROS_BOOTSTRAP_OK
        && error != MICROS_BOOTSTRAP_ERROR_ARGUMENT
        && error != MICROS_BOOTSTRAP_ERROR_RANGE
        && error != MICROS_BOOTSTRAP_ERROR_STATE
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    if (
        error == MICROS_BOOTSTRAP_OK
        && micros_bootstrap_runtime_validate()
            != MICROS_BOOTSTRAP_OK
    ) {
        bootstrap_syscall_fail_active_bootstrap(
            hart,
            frame,
            MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
            service_id,
            MICROS_ENDPOINT_NONE
        );
    }
    return bootstrap_syscall_return_result(
        frame,
        bootstrap_syscall_map_bootstrap_error(error, false)
    );
}

enum micros_syscall_return bootstrap_syscall_handle_release(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    const struct micros_syscall_context *context,
    const struct micros_bootstrap_control_request *request,
    struct micros_bootstrap_control_state *state,
    struct micros_endpoint_registry *registry
)
{
    const struct micros_bootstrap_binding *binding =
        micros_bootstrap_control_find_binding(
            state,
            request->service_id
        );
    const struct micros_bootstrap_manifest_entry *entry =
        binding == NULL
            ? NULL
            : &state->manifest.entries[binding->manifest_index];
    const struct micros_tty_handoff_runtime_state *tty;
    struct micros_tty_handoff released;
    enum micros_bootstrap_error error;
    enum micros_tty_handoff_error tty_error;
    bool console = (
        entry != NULL
        && (
            entry->role_flags
            & MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER
        ) != 0
    );
    uint64_t now = riscv_read_time();

    micros_bootstrap_runtime_check_deadline(now);
    if (!console) {
        error = micros_bootstrap_control_release(
            state,
            registry,
            context->objects,
            context->hart,
            request->service_id,
            now,
            bootstrap_syscall_role_gate_ready(
                state,
                registry,
                context->objects,
                request->service_id,
                true
            )
        );
    } else {
        tty = micros_tty_handoff_runtime_state();
        tty_error =
            micros_tty_handoff_runtime_prepare_release(&released);
        if (tty_error == MICROS_TTY_HANDOFF_ERROR_STATE) {
            return bootstrap_syscall_return_result(
                frame,
                MICROS_SYSCALL_ABI_STATE
            );
        }
        if (
            tty == NULL
            || tty_error != MICROS_TTY_HANDOFF_OK
            || tty->service_id != request->service_id
            || !uart_console_handoff_is_quiesced()
            || !micros_plic_validate(MICROS_PLIC_DISABLED)
            || micros_user_address_space_validate_tty_uart_mapping(
                tty->process,
                tty->root_physical_address
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || !console_event_state_is_clear(
                state,
                registry,
                context->objects
            )
        ) {
            bootstrap_syscall_fail_active_bootstrap(
                hart,
                frame,
                MICROS_BOOTSTRAP_DIAGNOSTIC_RELEASE_TRANSITION,
                request->service_id,
                MICROS_ENDPOINT_NONE
            );
        }
        error = micros_bootstrap_control_release_console(
            state,
            registry,
            context->objects,
            context->hart,
            request->service_id,
            now,
            released.deadline,
            true
        );
        if (error == MICROS_BOOTSTRAP_OK) {
            micros_tty_handoff_runtime_commit_release_prevalidated(
                &released
            );
        }
    }
    return finish_release(hart, frame, request->service_id, error);
}
